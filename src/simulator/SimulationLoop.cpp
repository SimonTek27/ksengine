#include <set>
#if defined(_WIN32)
// mmsystem.h drags in the whole of windows.h to declare one function; the
// timer call is declared here so this translation unit stays Win32-free.
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);
#pragma comment(lib, "winmm.lib")
#endif
#include <thread>
#include "engine/Graphics/MaterialPresets.h"
#include "engine/Graphics/MaterialOverrides.h"
#include "engine/Graphics/TextureNames.h"
#include "engine/Graphics/HiddenNodes.h" // the nodes the loaded content hides
#include "engine/FileFormat/Json.h"
#include "engine/Config/PPFilterPreset.h" // loaded preset (KS_PPFILTER)
/**
 * SimulationLoop.cpp — std-only
 * 1 kHz physics + LapSectorTimer + SM + UDP + TCP telemetry
 */

#include "SimulationLoop.h"
#include "ContentLibrary.h"
#include "InputManager.h"
#include "MultiCarManager.h"
#include "NetworkManager.h"
#include "SetupGarage.h"
#include "SetupFile.h"
#include "SceneAssets.h"
#include "engine/FileFormat/Kn5Reader.h"
#include <array>
#include <functional>
#include "engine/FileFormat/DdsReader.h" // hall albedo decoded from the .kn5, in memory
#include <map>
#include "MaterialCache.h"   // car bakes pair with materials.txt too (roadmap 2.4)
#include "GarageSpawn.h"     // rF2-like garage/grid placement (roadmap 2.8)
#include "RainEffects.h"
#include "SimulatorAudio.h"
#include "VehicleAudioHook.h" // upgrade sound pack -> SimulatorAudio (roadmap 2.9)
#include "engine/physics/DamageTelemetry.h"
#include "engine/physics/AeroModel.h"   // baseline aero capture (roadmap 2.9)
#include "engine/physics/PhysicsProfiler.h"
#include "engine/vehicle/VehicleAppearanceBundle.h"      // resolveAppearance (2.9)
#include "engine/vehicle/ApplyVehicleUpgrades.h"         // physics bridge (2.9)
#include "ui/UiGpuPass.h"
#include "ui/RaceTelemetryHud.h"
#include "UdpTelemetryBridge.h"
#include "TcpTelemetryBridge.h"

#include "engine/Engine.h"
#include "engine/Math/MathTypesFree.h"
#include "engine/scene/Components.h"
#include "engine/scene/SceneModule.h"
#include "engine/Graphics/RenderSystem.h"
#include "engine/devices/InputSystem.h"
#include "engine/Scripting/ScriptModule.h"
#include "engine/Scripting/ModSdk.h"
#include "engine/terrain/TerrainMesh.h"
#include "engine/terrain/TerrainHeightmap.h"
#include "engine/FileFormat/PngReader.h"

#include "adapters/assetto_corsa/AcSharedMemoryPublisher.h"
#include "adapters/assetto_corsa/AcSurfacesLoader.h"
#include "adapters/assetto_corsa/TrackDataJsonLoader.h"
#include "adapters/assetto_corsa/AcRemoteTelemetryServer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <vector>
#include <string>

#ifndef HAS_VEHICLE_SIM
#define HAS_VEHICLE_SIM 1
#endif
#ifndef HAS_KSNET
#define HAS_KSNET 0
#endif
#ifndef HAS_FFB
#define HAS_FFB 1
#endif

#if HAS_VEHICLE_SIM
#include "engine/physics/VehicleSimulator.h"
#endif
#if HAS_FFB
#include "engine/devices/FFBBridge.h"
#include "engine/devices/simracing/FFBSDKFactory.h"
#endif

namespace ks::sim {

// Roadmap 2.9 — everything the loop needs to (re)apply upgrade selections for
// the loaded car: the package list, the resolved appearance, and the car's
// post-ini baseline so re-selection recomputes from absolutes instead of
// stacking on already-modified state (the aero config is restored before
// every apply for the same reason — applyUpgrades reads the current value).
struct UpgradeRuntime {
    ks::vehicle::VehicleUpgradeSystem system;
    ks::vehicle::VehicleAppearanceBundle appearance;
    ks::vehicle::BaselineVehicleParams basePhysics;
    ks::physics::AeroModel::AeroConfig baseAero{};
    bool fromCarIni = false; // upgrades.ini parsed (false = built-in defaults)
};

static constexpr uint8_t SESSION_PRACTICE = 0;
static constexpr uint8_t SESSION_QUALIFYING = 1;
static constexpr uint8_t SESSION_RACE = 2;
static constexpr uint8_t PHASE_COUNTDOWN = 1;
static constexpr uint8_t PHASE_GREEN_FLAG = 2;
static constexpr uint8_t PHASE_CHECKERED_FLAG = 4;

namespace {
std::shared_ptr<ks::EngineModule> borrowModule(ks::EngineModule& m) {
    return std::shared_ptr<ks::EngineModule>(&m, [](ks::EngineModule*) {});
}
ks::sim::mat4 toNativeMat4(const ks::math::mat4& m) {
    ks::sim::mat4 r;
    std::memcpy(r.m, m.m, sizeof(m.m));
    return r;
}
// Lua Mod SDK: run a content folder's scripts/*.lua, surfacing per-file
// failures on stderr without ever failing the content load itself.
void loadContentMods(const std::string& contentRoot) {
    const ks::scripting::modsdk::LoadReport report =
        ks::scripting::modsdk::loadModScripts(contentRoot);
    for (const std::string& error : report.errors)
        std::fprintf(stderr, "SimulationLoop: mod script: %s\n", error.c_str());
}
} // namespace

std::string SimulationLoop::readFileText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

SimulationLoop::SimulationLoop() : m_vulkanMode(true) {
    m_input = std::make_unique<InputManager>();
#if HAS_VEHICLE_SIM
    m_vehicle = std::make_unique<ks::physics::VehicleSimulator>();
#endif
    // AI/multi-car manager (roadmap 3.5). Must be built before the
    // NetworkManager: its constructor arms client/server transports with
    // multiCarManager() and a null pointer there is never re-checked.
    m_multiCar = std::make_unique<MultiCarManager>();
    m_network = std::make_unique<NetworkManager>(this);
    // Multiplayer (roadmap 3.1): NetworkManager only pumps the transport,
    // everything that reaches the simulation is wired here.
    m_network->onRemoteClientJoined = [this](int slot, uint32_t, const std::string&) {
        if (!m_multiCar || !m_network->server()) return;
        const uint32_t carId = m_network->server()->getClientCarId(slot);
        if (carId == 0) return;
        // Without this the entry keeps its spline AI, and MultiCarManager::
        // update() would overwrite the networked controls with the AI's own
        // on every physics step.
        m_multiCar->setCarClientIndex(static_cast<int>(carId), slot);
#if HAS_VEHICLE_SIM
        // The transport spawns our own wire avatar at the origin while the
        // local player drives m_vehicle elsewhere: seed it from the real car
        // so the state broadcast back to the guests matches what we see.
        if (m_vehicle && slot == static_cast<int>(m_network->localClientId())) {
            CarEntry* own = m_multiCar->getCar(static_cast<int>(carId));
            if (own && own->vehicle) {
                const auto src = m_vehicle->getState();
                ks::physics::SimulationState& dst = own->vehicle->state();
                dst.position = src.position;
                dst.rotation = src.rotation;
                dst.heading = src.heading;
                dst.velocity = src.velocity;
                dst.speed = src.speed;
                dst.rpm = src.rpm;
                dst.gear = src.gear;
            }
        }
#endif
    };
    m_network->onRemoteClientLeft = [this](int slot, const std::string&) {
        m_remoteInputs.erase(slot);
    };
    m_network->onRemoteCarSpawned = [this](uint32_t carId, const std::string& driver,
                                           uint32_t clientId) {
        if (!m_multiCar || m_network->isHosting()) return; // host already owns those cars
        if (clientId == m_network->localClientId()) { m_ownNetCarId = carId; return; }
        if (m_remoteCarIds.count(carId) != 0) return;
        const int localId = m_multiCar->addCar({}, driver, vec3(0.0f, 0.5f, 0.0f), false);
        if (localId < 0) return;
        // The pose is replayed from the host's state messages, so no AI.
        m_multiCar->setCarExternallyDriven(localId);
        m_remoteCarIds.emplace(carId, localId);
    };
    m_network->onRemoteCarDespawned = [this](uint32_t carId) {
        if (!m_multiCar) return;
        const auto it = m_remoteCarIds.find(carId);
        if (it == m_remoteCarIds.end()) return;
        m_multiCar->removeCar(it->second);
        m_remoteCarIds.erase(it);
    };
    m_network->onRemoteCarStateReceived = [this](uint32_t carId, const net::CarStateData& state) {
        handleRemoteCarState(carId, state);
    };
    m_network->onChatMessageReceived = [this](uint32_t, const std::string& from,
                                              const std::string& message) {
        m_ui.chat().addLine(from + ": " + message);
    };
    m_lapTimer.configure(3);
    m_ui.resize(m_viewW, m_viewH);
    m_ui.dashboard().setVisible(true);
    m_ui.menu().setVisible(false);
    m_raceSession.onCountdownFinished = [this]() {
        m_sessionPhase = PHASE_GREEN_FLAG;
        m_timeRemaining = 0.0;
        m_lapTimer.start();
    };
    m_raceSession.onSessionEnded = [this]() {
        ks::scripting::modsdk::dispatch("session_end", {});
        if (m_sessionPhase == PHASE_GREEN_FLAG || m_sessionPhase == PHASE_COUNTDOWN) {
            m_sessionPhase = PHASE_CHECKERED_FLAG;
            if (onSessionStateChanged)
                onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, 0.0);
        }
        finishGoldenExport();
    };
    // Lua Mod SDK (roadmap 3.4): forward race-session events to mod hooks
    // (ks.on("lap"/"sector"/"position"/"penalty"/"flag", ...) and the
    // conventional on_<name> globals).
    m_raceSession.onSessionStarted = []() {
        ks::scripting::modsdk::dispatch("session_start", {});
    };
    m_raceSession.onLapCompleted = [](int lap, float time, float best) {
        ks::scripting::modsdk::dispatch("lap", {double(lap), double(time), double(best)});
    };
    m_raceSession.onSectorCompleted = [](int sector, float time) {
        ks::scripting::modsdk::dispatch("sector", {double(sector), double(time)});
    };
    m_raceSession.onPositionChanged = [](int position) {
        ks::scripting::modsdk::dispatch("position", {double(position)});
    };
    m_raceSession.onPenaltyIssued = [](int car, const std::string& type,
                                       const std::string& reason) {
        ks::scripting::modsdk::dispatch("penalty", {double(car)}, {type, reason});
    };
    m_raceSession.onFlagChanged = [](RaceFlag flag) {
        ks::scripting::modsdk::dispatch("flag", {double(static_cast<int>(flag))});
    };
}

SimulationLoop::~SimulationLoop() {
    stop();
    if (m_shm) m_shm->close();
    if (m_udp) m_udp->close();
    if (m_tcp) m_tcp->close();
    ks::ecs::SceneModule::instance().clearSystems();
    ks::ecs::SceneModule::instance().attach(nullptr);
    ks::scripting::ScriptModule::instance().shutdown();
    ks::engine::devices::InputSystem::instance().shutdown();
    ks::engine::graphics::RenderSystem::instance().shutdown();
    Engine::instance().shutdown();
#if HAS_FFB
    if (m_ffb) { m_ffb->shutdown(); m_ffb.reset(); }
#endif
}

bool SimulationLoop::initialize() {
    auto& engine = Engine::instance();
    engine.initialize();
    engine.setFixedDt(1.0 / 120.0);
    // The camera existed as a member but was never constructed, so
    // camera() was always null and the renderer never received a view/
    // projection pair (it rendered with identity matrices). Build it here
    // and push it to the renderer every frame from updateCamera().
    m_camera = std::make_unique<CameraController>();
    m_camera->setAspectRatio(static_cast<float>(m_viewW) / static_cast<float>(m_viewH));
    m_camera->setNearPlane(0.1f);
    m_camera->setFarPlane(5000.0f);
    engine.registerModule("ks.input", borrowModule(ks::engine::devices::InputSystem::instance()));
    engine.registerModule("ks.render", borrowModule(ks::engine::graphics::RenderSystem::instance()));
    auto& scene = ks::ecs::SceneModule::instance();
    scene.attach(&engine.registry());
    scene.addSystem("syncCarTransforms", [this](ks::ecs::Registry&, double) { syncCarTransforms(); });
    engine.registerModule("ks.scene", borrowModule(scene));
    ks::engine::devices::InputSystem::instance().initialize();
    ks::engine::graphics::RenderSystem::instance().initialize();
    auto& script = ks::scripting::ScriptModule::instance();
    engine.registerModule("ks.script", borrowModule(script));
    script.initialize();
    if (m_input) m_input->initialize();
#if HAS_FFB
    if (m_ffbEnabled && !m_ffb) m_ffb = ks::device::FFBSDKFactory::createFFB();
#endif
    {
        auto pass = std::make_shared<ui::UiGpuPass>();
        pass->initialize(m_ui.renderer().font());
        m_uiGpu = pass;
        if (m_vulkanRenderer) m_vulkanRenderer->setUiGpuPass(pass);
    }
    if (m_shmEnabled) {
        m_shm = std::make_unique<ks::ac::AcSharedMemoryPublisher>();
        m_shm->open();
    }
    if (m_udpEnabled) {
        m_udp = std::make_unique<UdpTelemetryBridge>();
        if (m_udp->open(m_udpHost.c_str(), m_udpPort))
            std::fprintf(stderr, "SimulationLoop: UDP → %s:%u\n", m_udpHost.c_str(), unsigned(m_udpPort));
    }
    if (m_tcpEnabled) {
        m_tcp = std::make_unique<TcpTelemetryBridge>();
        if (m_tcp->listen("0.0.0.0", m_tcpPort))
            std::fprintf(stderr, "SimulationLoop: TCP listen :%u\n", unsigned(m_tcpPort));
    }
    return true;
}

bool SimulationLoop::loadTrack(const std::string& kn5Path) {
    m_trackData = SimTrackData{};
    m_trackData.kn5Path = kn5Path;
    m_trackData.name = std::filesystem::path(kn5Path).stem().string();
    if (!std::filesystem::exists(kn5Path)) { m_trackLoaded = false; return false; }
    m_trackData.valid = true; m_trackLoaded = true;
    // Roadmap 1.2: also swap the visuals when a baked cache sits next to
    // the .kn5 (metadata-only load already succeeded above).
    applyTrackVisuals(std::filesystem::path(kn5Path).parent_path().string());
    m_ui.menu().setTrackName(m_trackData.name);
    m_lapTimer.configure(3);
    return true;
}

bool SimulationLoop::loadTrackFolder(const std::string& trackDirectory) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(trackDirectory)) return false;
    loadContentMods(trackDirectory); // Lua Mod SDK: <track>/scripts/*.lua
    m_trackData.directory = trackDirectory;
    m_trackData.name = fs::path(trackDirectory).filename().string();
    // AI racing line (roadmap 3.5): load it at track-load time so the grid
    // can be spawned when the session starts. No file = spawnGrid is a no-op.
    if (m_multiCar) m_multiCar->loadAiSpline(trackDirectory);
    // Native JSON manifest (data/data.json) preferred; surfaces.ini remains
    // the AC compatibility path when JSON is absent or fails to parse.
    {
        ks::ac::TrackDataJsonLoader trackJson;
        if (trackJson.loadForTrackDir(trackDirectory)) {
            if (!trackJson.meta().name.empty())
                m_trackData.name = trackJson.meta().name;
            if (trackJson.meta().lengthM > 1.f)
                m_trackData.splineLength = trackJson.meta().lengthM;
        } else {
            fs::path surf = fs::path(trackDirectory) / "data" / "surfaces.ini";
            if (!fs::exists(surf)) surf = fs::path(trackDirectory) / "surfaces.ini";
            if (fs::exists(surf)) {
                ks::ac::AcSurfacesLoader loader;
                loader.load(surf.string());
            }
        }
    }
    ks::physics::TrackSurface::instance().syncFromWeather(m_weather.trackWetness, m_weather.trackTemp);
    // A real AC track folder holds several .kn5 — the layout plus its
    // sub-layouts and decorative props — so directory order is not a choice:
    // it picked a 7 MB deco file over the 406 MB vallelunga.kn5. Prefer the
    // file named like the folder, else the largest one.
    std::string kn5;
    {
        const std::string folderStem = fs::path(trackDirectory).filename().string();
        std::uintmax_t largest = 0;
        std::error_code walkEc;
        for (const auto& entry : fs::directory_iterator(trackDirectory, walkEc)) {
            const fs::path path = entry.path();
            if (path.extension() != ".kn5") continue;
            if (path.stem().string() == folderStem) { kn5 = path.string(); break; }
            std::error_code sizeEc;
            const std::uintmax_t size = fs::file_size(path, sizeEc);
            if (sizeEc || size <= largest) continue;
            largest = size;
            kn5 = path.string();
        }
    }
    if (!kn5.empty()) return loadTrack(kn5);
    m_trackData.valid = true; m_trackLoaded = true;
    m_ui.menu().setTrackName(m_trackData.name);
    m_lapTimer.configure(3);
    // Roadmap 1.2: a folder without a .kn5 can still carry a baked cache.
    applyTrackVisuals(trackDirectory);
    return true;
}

bool SimulationLoop::loadCar(const std::string& carDir) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(carDir)) return false;
    loadContentMods(carDir); // Lua Mod SDK: <car>/scripts/*.lua
    m_carName = fs::path(carDir).filename().string();
    m_carDir = carDir; // roadmap 2.8/2.9: livery + upgrades resolve against it
#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        m_vehicle->setMass(1200); m_vehicle->setEnginePower(260); m_vehicle->setMaxRpm(8500);
        auto tryLoad = [&](const std::string& name, auto loader) {
            fs::path p1 = fs::path(carDir) / "data" / name;
            fs::path p2 = fs::path(carDir) / name;
            if (fs::exists(p1)) loader(p1.string());
            else if (fs::exists(p2)) loader(p2.string());
        };
        tryLoad("tyres.ini", [&](const std::string& p) { m_vehicle->loadTyresFromIni(p); });
        tryLoad("engine.ini", [&](const std::string& p) { m_vehicle->loadEngineFromIni(p); });
        tryLoad("drivetrain.ini", [&](const std::string& p) { m_vehicle->loadDrivetrainFromIni(p); });
        tryLoad("aero.ini", [&](const std::string& p) { m_vehicle->loadAeroFromIni(p); });
        tryLoad("suspension.ini", [&](const std::string& p) { m_vehicle->loadSuspensionFromIni(p); });
    }
#endif
    // Roadmap 2.9: upgrade packages for this car (upgrades.ini, or the
    // built-in stock packages when that file is missing = base content
    // fallback), then apply the selected physics on top of the baseline the
    // car's own inis just produced.
    loadUpgradesForCar(carDir);
    applyUpgradePhysics();
    m_carLoaded = true;
    m_ui.menu().setCarName(m_carName);
    if (m_shm) m_shm->invalidateStatic();
    // Factory paint: the first colour the car folder declares. A car with no
    // list goes back to white, so one car's paint never follows the player into
    // the next. The CARS page can repaint from here (setCarPaint).
    {
        const std::vector<ks::sim::CarPaint> paints = ks::sim::carPaintsFor(carDir);
        m_carPaint[0] = m_carPaint[1] = m_carPaint[2] = 1.0f;
        if (!paints.empty()) {
            m_carPaint[0] = paints.front().r;
            m_carPaint[1] = paints.front().g;
            m_carPaint[2] = paints.front().b;
        }
    }
    // Roadmap 1.2: visual for the freshly loaded car (baked meshes or
    // solid box placeholder). Never fails the physics load. Applies the
    // appearance's node filter and livery texture override (roadmap 2.9).
    ensureCarVisual(carDir);
    // Best effort: a missing audio device must not fail car load.
    loadCarAudio(carDir);
    applyUpgradeAudio(); // appearance sound pack on top of the baseline (2.9)
    return true;
}

bool SimulationLoop::openShowroom(const std::string& directory, std::string* error) {
    m_showroomAudioLoaded = false;
    if (!m_showroom.open(directory, error)) return false;
    loadShowroomHall();
    loadShowroomAudio();
    loadShowroomCar();
    // Nothing else will: the menu sits in the idle branch of tick(), which
    // returns before the weather update ever runs.
    applyShowroomLight();
    return true;
}

void SimulationLoop::loadShowroomCar() {
    if (!m_vulkanRenderer || !m_showroom.active()) return;
    // A real model already on the stage is left alone; the placeholder box is not
    // a car, so the room still has to find one.
    if (!m_carVisualEntities.empty()) {
        bool placeholder = false;
        if (m_carVisualEntities.size() == 1)
            if (auto* mesh = scene().tryGet<ks::ecs::MeshInstance>(m_carVisualEntities.front()))
                placeholder = mesh->meshName == "car_placeholder";
        if (!placeholder) return;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string carDirectory = m_carDir;
    if (carDirectory.empty()) {
        // The room is a turntable: with no car selected yet, put the first one
        // that ships with a model on it. Cars sit next to the showrooms in the
        // content folder, so no path needs to be configured for this.
        const fs::path carsRoot =
            fs::path(m_showroom.config().directory).parent_path().parent_path() / "cars";
        std::vector<fs::path> candidates;
        for (const auto& entry : fs::directory_iterator(carsRoot, ec))
            if (entry.is_directory(ec)) candidates.push_back(entry.path());
        std::sort(candidates.begin(), candidates.end());
        for (const auto& dir : candidates) {
            bool hasModel = false;
            for (const auto& file : fs::directory_iterator(dir, ec)) {
                if (file.path().extension() != ".kn5") continue;
                hasModel = true;
                break;
            }
            if (hasModel) {
                carDirectory = dir.string();
                break;
            }
        }
    }
    if (carDirectory.empty()) {
        std::fprintf(stderr, "[showroom] no car to show: no model in %s\n",
                     (fs::path(m_showroom.config().directory).parent_path().parent_path() /
                      "cars").string().c_str());
        m_ui.notify("Showroom: no car model found in content/cars",
                    ks::sim::ui::NoticeKind::Warning);
        return;
    }
    ensureCarVisual(carDirectory);
    std::fprintf(stderr, "[showroom] car on the turntable: %s\n", carDirectory.c_str());
}

void SimulationLoop::loadShowroomAudio() {
    if (!m_showroom.active()) return;
    const ShowroomConfig& cfg = m_showroom.config();
    // Nothing to play: do not open an output device for a silent showroom.
    if (!cfg.hasAmbience() && !cfg.hasMusic()) return;
    if (!m_audio) {
        // The showroom comes before any car, and the audio object used to be
        // created by the car load, so its music never started. Same rule as
        // loadCarAudio: a missing device leaves the showroom silent, not broken.
        auto audio = std::make_unique<SimulatorAudio>();
        if (!audio->initialize()) {
            std::fprintf(stderr, "[showroom] no audio device, tracks stay silent\n");
            m_ui.notify("No audio device: the showroom stays silent",
                        ks::sim::ui::NoticeKind::Warning);
            return;
        }
        m_audio = std::move(audio);
    }
    m_showroomAudioLoaded = m_audio->loadShowroomAudio(cfg.ambience, cfg.music);
    // The hall plays its own ambience: no car is being driven there, and when
    // the car ships no WAV samples the synth engine drones through the whole
    // menu. The driving channels come back in closeShowroom().
    m_audio->setEngineVolume(0.f);
    m_audio->setEnvironmentVolume(0.f);
}

void SimulationLoop::loadShowroomHall() {
    if (!m_showroom.active()) return;
    const ShowroomConfig& cfg = m_showroom.config();
    namespace fs = std::filesystem;

    if (!ks::sim::findBakedManifestDir(cfg.directory).empty()) {
        // The hall is content like a circuit: the loader scans the folder for
        // .kn5 and keeps the largest, which is the venue rather than a
        // decoration. It also stamps track metadata and the menu's TRACK row,
        // and those belong to the circuit the player will actually drive, so
        // both are put back: only the meshes stay in the scene.
        const std::string previousLabel = m_ui.menu().trackName();
        if (loadTrackFolder(cfg.directory)) {
            m_trackData = SimTrackData{};
            m_trackLoaded = false;
            m_ui.menu().setTrackName(previousLabel);
            // The hall load swapped the static scene: the car has to be put
            // back on it.
            if (m_carLoaded && !m_carDir.empty()) ensureCarVisual(m_carDir);
            std::printf("[showroom] hall %s loaded\n", cfg.directory.c_str());
            return;
        }
    }
    // Nothing baked: read the .kn5 itself, in memory. No bake, no temporary
    // files — the selected folder's own content is all the showroom uses.
    if (loadShowroomHallFromKn5(cfg)) return;
    if (!cfg.model.empty() && !fs::is_regular_file(fs::path(cfg.directory) / cfg.model))
        std::fprintf(stderr, "[showroom] %s: no %s in the folder\n",
                     cfg.id.c_str(), cfg.model.c_str());
    buildShowroomVenue();
}

// One .kn5 reader for both the venue and the car: parse the file in memory, turn every
// mesh node into a NativeMesh, name each one with a prefix, and hand the entity that
// draws it back to the caller. Textures are decoded from the file itself — the
// showroom is only allowed to read its content, so nothing is written anywhere.
bool SimulationLoop::loadKn5Meshes(const std::string& modelPath, const std::string& prefix,
                                   bool faceInside, std::vector<ks::ecs::Entity>* entities,
                                   float outMin[3], float outMax[3], int* outMeshes) {
    if (!m_vulkanRenderer) return false;
    namespace ff = ::ks::engine::fileformat;
    ff::Kn5ParseOptions parseOptions;
    parseOptions.keep_texture_data = true; // the albedo is inside the file
    const auto parsed = ff::parseKn5File(modelPath, parseOptions);
    if (!parsed.ok()) {
        std::fprintf(stderr, "[kn5] %s unreadable: %s\n", modelPath.c_str(),
                     parsed.error.c_str());
        return false;
    }

    // Row-vector convention, as the parser stores each node's accumulated transform:
    // the same arithmetic the offline baker applies before writing.
    auto transform = [](const std::array<float, 16>& m, float x, float y, float z,
                        float w, float out[3]) {
        out[0] = x * m[0] + y * m[4] + z * m[8] + w * m[12];
        out[1] = x * m[1] + y * m[5] + z * m[9] + w * m[13];
        out[2] = x * m[2] + y * m[6] + z * m[10] + w * m[14];
    };

    int meshes = 0;
    int mappedNormal = 0;
    int painted = 0;
    int metalSurfaces = 0;
    int cutouts = 0;
    int seeThrough = 0;
    int selfLit = 0;
    std::map<std::string, std::string> decodedTextures; // kn5 texture -> cache key

    // The nodes the content hides: the car's overrides name them and the
    // converter folds them into the car's documents, so the list is read where
    // the model is described. A venue ships no model document and hides nothing.
    // KS_SHOW_HIDDEN turns hiding off, which is the only way to see what a skin
    // actually removes.
    graphics::HiddenNodes hiddenNodes;
    int hiddenSkipped = 0;
    if (std::getenv("KS_SHOW_HIDDEN") == nullptr) {
        std::error_code documentError;
        const std::filesystem::path carFolder =
            std::filesystem::path(modelPath).parent_path();
        for (const char* document : {"model.json", "driver.json"}) {
            const std::filesystem::path file = carFolder / "data" / document;
            if (!std::filesystem::is_regular_file(file, documentError)) continue;
            std::ifstream in(file, std::ios::binary);
            if (!in) continue;
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            bool parsedDocument = false;
            const ks::Json parsedJson = ks::Json::parse(text, &parsedDocument);
            if (!parsedDocument) continue;
            hiddenNodes.addDocument(parsedJson);
        }
        if (!hiddenNodes.empty()) {
            std::fprintf(stderr, "[kn5] car hidden nodes: %zu pattern(s)",
                         hiddenNodes.size());
            std::putchar(10);
        }
    }

    // Folds a KN5 name to lower case: shader/mapping names are author-written
    // and inconsistent about it ("Normal", "normal", "KS_NM").
    auto lower = [](const std::string& text) {
        std::string folded;
        folded.reserve(text.size());
        for (char c : text) {
            folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return folded;
    };

    // First float of a shader property. AC stores each property as four
    // float4s' worth of 40 bytes (ValueA/B/C/D), and every scalar the surface
    // model needs (ksSpecularEXP, ksSpecular, fresnelC) lives at offset 0.
    auto propertyFloat = [](const ff::Kn5Material& material, const char* name) {
        for (const auto& property : material.properties) {
            if (property.name != name) continue;
            float value = 0.0f;
            std::memcpy(&value, property.value.data(), sizeof(value));
            return value;
        }
        return -1.0f;
    };

    // First three floats of a vector property (ksDiffuse and friends). AC
    // keeps every property in a 40-byte ValueA/B/C/D block; a vec3 starts at
    // offset 0 exactly like the scalars above.
    auto propertyVec3 = [](const ff::Kn5Material& material, const char* name,
                          float out[3]) -> bool {
        for (const auto& property : material.properties) {
            if (property.name != name) continue;
            std::memcpy(out, property.value.data(), sizeof(float) * 3);
            return true;
        }
        return false;
    };

    // Per-car material overrides: material_presets.json next to the model,
    // or the file KS_MATERIAL_PRESETS points at. Absent (or broken) means
    // the presets alone decide, which is what every car gets by default.
    graphics::MaterialOverrides overrides;
    int overridden = 0;
    // KS_MATERIAL_DEBUG lists each material once with the preset it landed
    // on, which is the only way to see why a surface looks the way it does.
    const bool debugMaterials = std::getenv("KS_MATERIAL_DEBUG") != nullptr;
    std::set<std::string> printedMaterials;
    {
        std::error_code ec;
        std::string overridePath;
        if (const char* env = std::getenv("KS_MATERIAL_PRESETS");
            env != nullptr && *env != 0)
            overridePath = env;
        else {
            const std::filesystem::path sibling =
                std::filesystem::path(modelPath).parent_path() / "material_presets.json";
            if (std::filesystem::is_regular_file(sibling, ec)) overridePath = sibling.string();
        }
        if (!overridePath.empty()) {
            std::string why;
            if (overrides.loadFile(overridePath, &why)) {
                std::fprintf(stderr, "[materials] %zu override(s) from %s",
                             overrides.size(), overridePath.c_str());
            } else {
                std::fprintf(stderr, "[materials] %s ignored: %s",
                             overridePath.c_str(), why.c_str());
            }
            std::putchar(10);
        }
    }

    // Skin textures live next to the model, not inside it: AC keeps the body
    // diffuse and its maps in skins/<id>/. The embedded copy is the file's
    // default, so a file of the same name in the skin folder wins.
    std::string skinDir;
    {
        std::error_code ec;
        const std::filesystem::path carDir = std::filesystem::path(modelPath).parent_path();
        const std::filesystem::path skins = carDir / "skins";
        std::string wantedSkin;
        if (const char* env = std::getenv("KS_SKIN"); env != nullptr && *env != 0)
            wantedSkin = env;
        if (!wantedSkin.empty()) {
            const std::filesystem::path cand = skins / wantedSkin;
            if (std::filesystem::is_directory(cand, ec)) skinDir = cand.string();
            else {
                std::fprintf(stderr, "[kn5] skin '%s' is not in %s",
                             wantedSkin.c_str(), skins.string().c_str());
                std::putchar(10);
            }
        }
        if (skinDir.empty() && std::filesystem::is_directory(skins, ec)) {
            std::vector<std::filesystem::path> folders;
            for (const auto& entry : std::filesystem::directory_iterator(skins, ec))
                if (entry.is_directory(ec)) folders.push_back(entry.path());
            std::sort(folders.begin(), folders.end());
            if (!folders.empty()) skinDir = folders.front().string();
        }
        if (!skinDir.empty()) {
            std::fprintf(stderr, "[kn5] skin: %s", skinDir.c_str());
            std::putchar(10);
            // The skin's own overrides, folded into its folder as skin.json: what
            // this skin hides and the driver model it asks for. Only the loaded
            // skin applies, which is what a per-skin file means.
            if (std::getenv("KS_SHOW_HIDDEN") == nullptr) {
                const std::filesystem::path skinDocument =
                    std::filesystem::path(skinDir) / "skin.json";
                if (std::filesystem::is_regular_file(skinDocument, ec)) {
                    std::ifstream skinFile(skinDocument, std::ios::binary);
                    const std::string skinText((std::istreambuf_iterator<char>(skinFile)),
                                               std::istreambuf_iterator<char>());
                    bool parsedSkin = false;
                    const ks::Json skinJson = ks::Json::parse(skinText, &parsedSkin);
                    if (parsedSkin) {
                        hiddenNodes.addSkinDocument(skinJson);
                        std::fprintf(stderr, "[kn5] skin hidden nodes: %zu node(s)",
                                     hiddenNodes.size());
                        std::putchar(10);
                    }
                }
            }
        }
    }

    auto readSkinTexture = [&](const std::string& wanted) -> std::vector<unsigned char> {
        if (skinDir.empty()) return {};
        std::error_code ec;
        const std::filesystem::path candidates[2] = {
            std::filesystem::path(skinDir) / wanted,
            std::filesystem::path(skinDir) / "textures" / wanted};
        for (const std::filesystem::path& candidate : candidates) {
            if (!std::filesystem::is_regular_file(candidate, ec)) continue;
            std::ifstream file(candidate, std::ios::binary);
            if (!file) continue;
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                             std::istreambuf_iterator<char>());
            if (!bytes.empty()) return bytes;
        }
        return {};
    };

    // Decodes a KN5 texture once and returns the key the mesh names. Empty
    // when neither the skin nor the file carries it, which leaves the surface
    // untextured rather than failing the whole load.
    // Box-halves an RGBA image in place; the caller loops it. A showroom floor
    // is 40 m wide and its albedo is a photograph, so the top mips are the only
    // ones that read as a surface instead of as noise.
    auto halveImage = [](std::vector<unsigned char>& rgba, int& width, int& height) {
        if (width < 4 || height < 4) return;
        const int dstW = width / 2;
        const int dstH = height / 2;
        std::vector<unsigned char> out(static_cast<std::size_t>(dstW) * dstH * 4u);
        for (int y = 0; y < dstH; ++y) {
            for (int x = 0; x < dstW; ++x) {
                unsigned sum[4] = {0, 0, 0, 0};
                for (int sy = 0; sy < 2; ++sy) {
                    for (int sx = 0; sx < 2; ++sx) {
                        const unsigned char* s =
                            rgba.data() + (static_cast<std::size_t>(y * 2 + sy) * width +
                                          (x * 2 + sx)) * 4u;
                        for (int c = 0; c < 4; ++c) sum[c] += s[c];
                    }
                }
                unsigned char* d = out.data() + (static_cast<std::size_t>(y) * dstW + x) * 4u;
                for (int c = 0; c < 4; ++c) d[c] = static_cast<unsigned char>(sum[c] / 4u);
            }
        }
        rgba.swap(out);
        width = dstW;
        height = dstH;
    };

    auto textureKey = [&](const std::string& wanted, bool soften) -> std::string {
        if (wanted.empty()) return std::string();
        auto known = decodedTextures.find(wanted);
        if (known != decodedTextures.end()) return known->second;

        std::string key;
        // ksim owns the name of this cache entry, so the third-party file name
        // stops at the door: the body is CARBODY here.
        const std::string canonical = graphics::canonicalTextureName(wanted);
        char cacheKey[200];
        std::snprintf(cacheKey, sizeof(cacheKey), "kn5://%s/%s", prefix.c_str(),
                      canonical.c_str());

        const std::vector<unsigned char> skinBytes = readSkinTexture(wanted);
        std::string_view bytes;
        if (!skinBytes.empty())
            bytes = std::string_view(reinterpret_cast<const char*>(skinBytes.data()),
                                     skinBytes.size());
        else {
            for (const auto& texture : parsed.file.textures) {
                if (texture.name != wanted || texture.data.empty()) continue;
                bytes = std::string_view(reinterpret_cast<const char*>(texture.data.data()),
                                         texture.data.size());
                break;
            }
        }

        if (!bytes.empty() && ff::isDds(bytes)) {
            const auto image = ff::decodeDds(bytes);
            if (!image.ok() || image.rgba.empty()) {
                std::fprintf(stderr, "[kn5] texture %s: %s", wanted.c_str(),
                             image.info.error.c_str());
                std::putchar(10);
            } else {
                std::vector<unsigned char> rgba = image.rgba;
                int width = image.info.width;
                int height = image.info.height;
                if (soften) {
                    for (int pass = 0; pass < 4; ++pass) halveImage(rgba, width, height);
                }
                if (m_vulkanRenderer->addTextureFromMemory(
                        cacheKey, rgba, width, height)) {
                    key = cacheKey;
                }
            }
        }
        decodedTextures.emplace(wanted, key);
        return key;
    };
    float bboxMin[3] = {1e9f, 1e9f, 1e9f};
    float bboxMax[3] = {-1e9f, -1e9f, -1e9f};
    std::function<void(const ff::Kn5Node&)> walk = [&](const ff::Kn5Node& node) {
        // A hidden node takes its children with it: that is what hiding a group
        // in the editor means, and the driver's face sits under its own group.
        if (hiddenNodes.hides(node.name)) {
            ++hiddenSkipped;
            return;
        }
        const auto& m = node.mesh;
        if (node.node_class == ff::Kn5NodeClass::mesh && node.has_mesh &&
            !m.indices.empty() && m.positions.size() >= 3) {
            const std::size_t count = m.positions.size() / 3;
            NativeMesh nm;
            nm.vertices.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                NativeVertex& v = nm.vertices[i];
                float pos[3];
                transform(m.world, m.positions[i * 3], m.positions[i * 3 + 1],
                          m.positions[i * 3 + 2], 1.f, pos);
                v.px = pos[0];
                v.py = pos[1];
                v.pz = pos[2];
                if (m.normals.size() >= (i + 1) * 3) {
                    float n[3];
                    transform(m.world, m.normals[i * 3], m.normals[i * 3 + 1],
                              m.normals[i * 3 + 2], 0.f, n);
                    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    if (len > 1e-6f) {
                        v.nx = n[0] / len;
                        v.ny = n[1] / len;
                        v.nz = n[2] / len;
                    } else {
                        v.ny = 1.f;
                    }
                } else {
                    v.ny = 1.f;
                }
                if (m.uvs.size() >= (i + 1) * 2) {
                    v.u = m.uvs[i * 2];
                    v.v = m.uvs[i * 2 + 1];
                }
                // White: the albedo carries the colour, so nothing is tinted twice.
                v.r = 1.f;
                v.g = 1.f;
                v.b = 1.f;
                v.a = 1.f;
            }
            nm.indices = m.indices;
            // A room read as a closed shell is a backdrop: it receives light and
            // shadows but casts none, or the shell would shade everything inside it.
            nm.castShadow = !faceInside;

            // The material's diffuse texture, decoded here and cached under a key the
            // mesh can name. A file that cannot be decoded simply leaves the surface
            // untextured rather than failing the load.
            if (m.material_id >= 0 &&
                m.material_id < static_cast<std::int32_t>(parsed.file.materials.size())) {
                const auto& material =
                    parsed.file.materials[static_cast<std::size_t>(m.material_id)];

                // Albedo: the mapping the file names as diffuse, else the
                // first one it lists. Slot 0 is not a reliable answer — on a
                // reflection shader it is the environment probe, and painting
                // `*_Ref` as diffuse is what turned the showroom floor into a
                // white speckled sheet instead of a surface.
                std::string albedo;
                std::string fallbackAlbedo;
                std::string normal;
                for (const auto& mapping : material.mappings) {
                    if (mapping.texture.empty()) continue;
                    const std::string mappingName = lower(mapping.name);
                    // The normal map is authored under its own mapping name
                    // ("Normal", "NormalDetail"), not a fixed slot, and it is
                    // never the colour.
                    if (mappingName.find("normal") != std::string::npos) {
                        normal = mapping.texture;
                        continue;
                    }
                    if (mappingName.find("diffuse") != std::string::npos) {
                        albedo = mapping.texture;
                        continue;
                    }
                    if (fallbackAlbedo.empty()) fallbackAlbedo = mapping.texture;
                }
                if (albedo.empty()) albedo = fallbackAlbedo;
                // "..._Ref" names an environment/reflection probe: it carries
                // the room reflected in a surface, not the surface's own colour.
                // A venue is the exception: its hall is painted with exactly such
                // a map, and that map — softened above — is the room's only light.
                if (!faceInside) {
                    std::string base = albedo;
                    const std::size_t dot = base.find_last_of('.');
                    if (dot != std::string::npos) base.resize(dot);
                    const std::string folded = lower(base);
                    const bool probe = folded.size() >= 4 &&
                                       folded.compare(folded.size() - 4, 4, "_ref") == 0;
                    if (probe) albedo.clear();
                }
                nm.albedoTexture = textureKey(albedo, faceInside);
                // No diffuse authored: the surface keeps the colour the file
                // gives it (ksDiffuse) instead of rendering as flat white.
                if (!faceInside && albedo.empty()) {
                    float authored[3] = {1.f, 1.f, 1.f};
                    if (propertyVec3(material, "ksDiffuse", authored)) {
                        for (NativeVertex& v : nm.vertices) {
                            v.r = std::min(std::max(authored[0], 0.f), 1.f);
                            v.g = std::min(std::max(authored[1], 0.f), 1.f);
                            v.b = std::min(std::max(authored[2], 0.f), 1.f);
                        }
                    }
                }
                if (!normal.empty() && normal != albedo) {
                    nm.normalTexture = textureKey(normal, false);
                    if (!nm.normalTexture.empty()) ++mappedNormal;
                }

                // Surface response. Without this every surface of the car is a
                // 0.35-roughness dielectric, which is what made it read flat.
                const float exponent = propertyFloat(material, "ksSpecularEXP");
                if (exponent > 0.0f) {
                    // Blinn-Phong exponent -> GGX roughness (the lobe of
                    // exponent E has alpha ~ sqrt(2 / (E + 2))).
                    nm.roughness = std::sqrt(2.0f / (exponent + 2.0f));
                }
                // What the surface is, from the shader and material names the
                // file ships (a 499P alone uses twelve of them). The preset carries
                // the roughness/metalness/clearcoat a chrome trim, a tyre or a
                // carbon panel should have, and the authored ksSpecularEXP only
                // moves the roughness as far as that preset allows
                // (engine/Graphics/MaterialPresets.h). Without this every surface
                // of the car read as the same flat dielectric.
                const graphics::MaterialSurface surface =
                    overrides.resolve(material.shader, material.name, nm.roughness);
                nm.roughness = surface.roughness;
                nm.metalness = surface.metalness;
                nm.clearcoat = surface.clearcoat;
                const graphics::MaterialPresetId presetId =
                    overrides.presetFor(material.shader, material.name);
                if (overrides.findEffective(material.name) != nullptr) ++overridden;
                if (debugMaterials && printedMaterials.insert(lower(material.name)).second) {
    const std::string albedoName = graphics::canonicalTextureName(albedo);
                    std::printf(
                        "[material] %-22s shader=%-26s preset=%-13s r=%.3f m=%.3f cc=%.3f albedo=%-24s%s",
                        material.name.c_str(), material.shader.c_str(),
                        graphics::materialPreset(presetId).name, nm.roughness, nm.metalness, nm.clearcoat,
                        albedo.empty() ? "-" : albedoName.c_str(),
                        overrides.findEffective(material.name) != nullptr ? " (override)" : "");
                    std::putchar(10);
                }
                if (presetId == graphics::MaterialPresetId::Paint) {
                    ++painted;
                    // Bodywork carries the paint the car folder declares (the
                    // CARS colour list). The tint multiplies the albedo in both
                    // shaders, so the panel keeps its texture detail and only
                    // its colour moves; 1,1,1 is the texture as baked.
                    nm.tint[0] = m_carPaint[0];
                    nm.tint[1] = m_carPaint[1];
                    nm.tint[2] = m_carPaint[2];
                }
                if (presetId == graphics::MaterialPresetId::BrushedMetal ||
                    presetId == graphics::MaterialPresetId::Chrome ||
                    presetId == graphics::MaterialPresetId::BrakeDisc)
                    ++metalSurfaces;

                // Cut-outs: the file flags them either as alpha_tested
                // (grilles, outlines) or as a blended surface (glass,
                // decals, windscreen), and both need the same discard or
                // the quad renders opaque.
                // Two different things, and they must not be confused: an
                // alpha_tested surface (grille, outline) is solid or absent,
                // while a blended one (glass, windscreen, decal) is mixed
                // over whatever is behind it. Cutting a windscreen out at
                // half alpha made it vanish instead of turning see-through.
                if (material.alpha_tested != 0) {
                    const float ref = propertyFloat(material, "ksAlphaRef");
                    nm.alphaTested = true;
                    nm.alphaRef = ref > 0.0f ? ref : 0.5f;
                    ++cutouts;
                }
                if (material.blend != 0) {
                    nm.blended = true;
                    nm.alphaTested = false; // the pass blends, it does not cut
                    // Glass is a pane; a decal is not. The pane keeps a real
                    // opacity and reflects, the decal only blends its own alpha.
                    const bool pane = presetId == graphics::MaterialPresetId::Glass;
                    nm.opacity = pane ? 0.30f : 1.0f;
                    nm.glassiness = pane ? 1.0f : 0.0f;
                    nm.castShadow = false; // a pane casts no useful shadow
                    ++seeThrough;
                }
                const float emissiveValue = propertyFloat(material, "ksEmissive");
                if (emissiveValue > 0.0f) {
                    nm.emissive = std::min(emissiveValue, 8.0f);
                    ++selfLit;
                }
            }

            // A room is looked at from inside it: faces that point away from the
            // middle would be culled and the room would vanish. A car is looked at
            // from outside, so it keeps the winding its author gave it.
            if (faceInside) {
                float centroid[3] = {0.f, 0.f, 0.f};
                for (const NativeVertex& v : nm.vertices) {
                    centroid[0] += v.px;
                    centroid[1] += v.py;
                    centroid[2] += v.pz;
                }
                if (count > 0) {
                    centroid[0] /= static_cast<float>(count);
                    centroid[1] /= static_cast<float>(count);
                    centroid[2] /= static_cast<float>(count);
                }
                float facing = 0.f;
                for (const NativeVertex& v : nm.vertices)
                    facing += v.nx * (v.px - centroid[0]) + v.ny * (v.py - centroid[1]) +
                              v.nz * (v.pz - centroid[2]);
                if (facing > 0.f) {
                    for (std::size_t k = 0; k + 2 < nm.indices.size(); k += 3)
                        std::swap(nm.indices[k + 1], nm.indices[k + 2]);
                    for (NativeVertex& v : nm.vertices) {
                        v.nx = -v.nx;
                        v.ny = -v.ny;
                        v.nz = -v.nz;
                    }
                }
            }

            const std::string name = prefix + std::to_string(meshes);
            m_vulkanRenderer->setMesh(name, nm);
            const ks::ecs::Entity entity = scene().create();
            if (entity != ks::ecs::kNullEntity) {
                scene().emplace<ks::ecs::Name>(entity, ks::ecs::Name{name});
                scene().emplace<ks::ecs::Transform>(entity);
                scene().emplace<ks::ecs::MeshInstance>(entity, ks::ecs::MeshInstance{name});
                if (entities) entities->push_back(entity);
            }
            for (const NativeVertex& v : nm.vertices) {
                const float pos[3] = {v.px, v.py, v.pz};
                for (int axis = 0; axis < 3; ++axis) {
                    if (pos[axis] < bboxMin[axis]) bboxMin[axis] = pos[axis];
                    if (pos[axis] > bboxMax[axis]) bboxMax[axis] = pos[axis];
                }
            }
            ++meshes;
        }
        for (const auto& child : node.children) walk(child);
    };
    for (const auto& node : parsed.file.nodes) walk(node);

    if (meshes == 0) return false;
        std::printf("[kn5] %s: %d mesh(es), %d normal map(s), %d painted, %d bare metal, "
                    "%d cut-out, %d see-through, %d self-lit",
            std::filesystem::path(modelPath).filename().string().c_str(), meshes,
            mappedNormal, painted, metalSurfaces, cutouts, seeThrough, selfLit);
std::putchar(10);
        if (overridden > 0) {
            std::printf("  %d material override(s) applied", overridden);
            std::putchar(10);
        }
        if (hiddenSkipped > 0) {
            std::printf("  hidden nodes: %d node(s)", hiddenSkipped);
            std::putchar(10);
        }
    if (outMin && outMax) {
        for (int axis = 0; axis < 3; ++axis) {
            outMin[axis] = bboxMin[axis];
            outMax[axis] = bboxMax[axis];
        }
    }
    if (outMeshes) *outMeshes = meshes;
    return true;
}

bool SimulationLoop::loadShowroomHallFromKn5(const ShowroomConfig& cfg) {
    if (!m_vulkanRenderer) return false;
    namespace fs = std::filesystem;
    std::error_code ec;

    // The named model when it is there, otherwise the largest .kn5 in the
    // folder — the venue rather than a decoration.
    fs::path model;
    if (!cfg.model.empty()) {
        const fs::path named = fs::path(cfg.directory) / cfg.model;
        if (fs::is_regular_file(named, ec)) model = named;
    }
    if (model.empty()) {
        uintmax_t largest = 0;
        for (const auto& entry : fs::directory_iterator(cfg.directory, ec)) {
            if (entry.path().extension() != ".kn5") continue;
            const uintmax_t size = entry.file_size(ec);
            if (size >= largest) {
                largest = size;
                model = entry.path();
            }
        }
    }
    if (model.empty()) return false;

    // What is on screen right now belongs to whatever was loaded before this showroom:
    // the track scene, its meshes and its entities. A venue read from its own .kn5
    // replaces all of it, the same way loadTrackFolder() does for a baked hall.
    despawnSceneEntities();
    m_vulkanRenderer->clearStaticScene();
    m_spawnedSceneDir.clear();

    float mn[3] = {0.f, 0.f, 0.f};
    float mx[3] = {0.f, 0.f, 0.f};
    int meshes = 0;
    if (!loadKn5Meshes(model.string(), "showroom_hall_", true, nullptr, mn, mx, &meshes)) {
        std::fprintf(stderr, "[showroom] %s: no mesh in %s\n", cfg.id.c_str(),
                     model.filename().string().c_str());
        return false;
    }

    const float sizeX = mx[0] - mn[0];
    const float sizeY = mx[1] - mn[1];
    const float sizeZ = mx[2] - mn[2];
    const float radius =
        0.5f * std::sqrt(sizeX * sizeX + sizeY * sizeY + sizeZ * sizeZ);
    // The camera frames the room it actually loaded: its own centre, on its own
    // floor, never further out than half the radius.
    m_showroom.setHallBounds((mn[0] + mx[0]) * 0.5f, (mn[2] + mx[2]) * 0.5f, mn[1], radius);
    std::fprintf(stderr, "[showroom] hall read from %s: %d mesh(es), no files written\n",
                 model.filename().string().c_str(), meshes);
    std::fprintf(stderr, "[showroom] hall bounds x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f] radius %.1f\n",
                 mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], radius);
    // The hall owned the static scene for a moment: put the car back on it.
    if (m_carLoaded && !m_carDir.empty()) ensureCarVisual(m_carDir);
    return true;
}

bool SimulationLoop::loadCarMeshesFromKn5(const std::string& carDirectory) {
    if (!m_vulkanRenderer || carDirectory.empty()) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    // The car's own model: the file named like its folder, otherwise the largest
    // .kn5 that is not one of the distance copies.
    // The car's own model: the file named like its folder, otherwise the biggest
    // one. A LOD copy is only skipped when something else is available — some
    // folders ship nothing but LODs, and a LOD is still the car.
    const std::string stem = fs::path(carDirectory).filename().string();
    fs::path model;
    fs::path bestLod;
    fs::path bestAny;
    uintmax_t bestLodSize = 0;
    uintmax_t bestAnySize = 0;
    for (const auto& entry : fs::directory_iterator(carDirectory, ec)) {
        if (entry.path().extension() != ".kn5") continue;
        const std::string name = entry.path().stem().string();
        const uintmax_t size = entry.file_size(ec);
        if (name == stem) {
            model = entry.path();
            break;
        }
        const bool lod = name.find("LOD") != std::string::npos ||
                         name.find("lod") != std::string::npos;
        if (size > bestAnySize) {
            bestAnySize = size;
            bestAny = entry.path();
        }
        if (!lod && size > bestLodSize) {
            bestLodSize = size;
            bestLod = entry.path();
        }
    }
    if (model.empty()) model = !bestLod.empty() ? bestLod : bestAny;
    if (model.empty()) {
        std::fprintf(stderr, "[kn5] no model in %s\n", carDirectory.c_str());
        return false;
    }

    float mn[3] = {0.f, 0.f, 0.f};
    float mx[3] = {0.f, 0.f, 0.f};
    int meshes = 0;
    if (!loadKn5Meshes(model.string(), "car_model_", false, &m_carVisualEntities, mn, mx,
                       &meshes))
        return false;
    std::fprintf(stderr,
                 "[scene] car visual: %d mesh(es) from %s, read in memory\n", meshes,
                 model.filename().string().c_str());
    // The room's fixed views orbit the car, not the hall, and the wheel view
    // needs to know which corner it is looking at.
    if (m_showroom.active()) {
        m_showroom.setCarBounds((mn[0] + mx[0]) * 0.5f, (mn[2] + mx[2]) * 0.5f,
                                (mx[0] - mn[0]) * 0.5f, (mx[2] - mn[2]) * 0.5f);
    }
    return true;
}

void SimulationLoop::buildShowroomVenue() {
    if (!m_vulkanRenderer) return; // headless: nothing to put on screen
    const ShowroomConfig& cfg = m_showroom.config();
    // The room has to hold the turntable whatever distance the file asked for,
    // or a wide orbit ends up looking at a wall from the wrong side.
    const float halfWidth = cfg.cameraDistance * 2.5f > 12.f ? cfg.cameraDistance * 2.5f : 12.f;
    const float height = cfg.cameraHeight * 3.f > 7.f ? cfg.cameraHeight * 3.f : 7.f;
    const ks::sim::ShowroomVenue venue = ks::sim::makeShowroomVenue(halfWidth, height);
    NativeMesh nm;
    static_assert(sizeof(ks::sim::SolidBoxVertex) == sizeof(NativeVertex),
                  "SolidBoxVertex must be memcpy-compatible with NativeVertex");
    nm.vertices.resize(venue.vertices.size());
    std::memcpy(nm.vertices.data(), venue.vertices.data(),
                venue.vertices.size() * sizeof(ks::sim::SolidBoxVertex));
    nm.indices = venue.indices;
    nm.castShadow = false; // the room is a backdrop, as in the .kn5 path
    // Same rule as the .kn5 path: the room is the scene, not a decoration on
    // top of the track that was loaded first.
    despawnSceneEntities();
    m_spawnedSceneDir.clear();
    m_vulkanRenderer->clearStaticScene();
    // In memory, and nothing else: the room is an ordinary mesh plus the
    // entity that draws it, exactly like the car's placeholder box. No file is
    // written anywhere — the showroom is content the game only reads.
    m_vulkanRenderer->setMesh("showroom_venue", nm);
    const ks::ecs::Entity venueEntity = scene().create();
    if (venueEntity != ks::ecs::kNullEntity) {
        scene().emplace<ks::ecs::Name>(venueEntity, ks::ecs::Name{"showroom_venue"});
        scene().emplace<ks::ecs::Transform>(venueEntity);
        scene().emplace<ks::ecs::MeshInstance>(venueEntity,
                                              ks::ecs::MeshInstance{"showroom_venue"});
    }
    std::fprintf(stderr, "[showroom] no baked hall: venue built in memory (%.0f x %.0f m, %.0f m high)\n",
                 halfWidth * 2.f, halfWidth * 2.f, height);
}

void SimulationLoop::closeShowroom() {
    m_showroom.close();
    if (m_vulkanRenderer) m_vulkanRenderer->setExposure(1.0f); // the track's own
    m_showroomAudioLoaded = false;
    if (m_audio) {
        m_audio->stopShowroomAudio();
        // A session starts here: the engine, tyres and wind are the car's
        // again once the showroom has let go of the camera.
        m_audio->setEngineVolume(1.f);
        m_audio->setEnvironmentVolume(1.f);
    }
}

bool SimulationLoop::loadCarAudio(const std::string& carDirectory) {
    if (carDirectory.empty()) return false;
    if (!m_audio) {
        // Created on first use because WASAPI is not guaranteed to be there
        // (headless runs, CI, machines without an output device): a failed
        // device open leaves the sim running silently rather than failing.
        auto audio = std::make_unique<SimulatorAudio>();
        if (!audio->initialize()) return false;
        m_audio = std::move(audio);
    }
    return m_audio->loadCarAudio(carDirectory);
}

// Roadmap 2.8 — team roster: team.ini decides drivers, numbers, liveries,
// garage boxes and grid slots at session start. Loading it re-applies the
// player livery immediately when a car is already loaded.
bool SimulationLoop::loadTeam(const std::string& teamPath) {
    namespace fs = std::filesystem;
    std::string path = teamPath;
    if (fs::is_directory(path)) path = (fs::path(path) / "team.ini").string();
    if (!fs::is_regular_file(path)) {
        std::fprintf(stderr, "[team] no team.ini at %s\n", path.c_str());
        return false;
    }
    TeamInfo loaded = TeamInfoLoader::loadIni(path);
    if (loaded.slots.empty()) {
        std::fprintf(stderr, "[team] %s: no active [Car_N] slots\n", path.c_str());
        return false;
    }
    m_team = std::move(loaded);
    std::fprintf(stderr, "[team] loaded %s (%d car(s) in roster)\n", path.c_str(),
                 m_team.assignedCarCount());
    m_ui.menu().setTeamName(m_team.name.empty() ? m_team.id : m_team.name);
    // A team picked after the car still has to reach this car's visuals
    // (player slot livery texture override).
    if (m_carLoaded && !m_carDir.empty()) ensureCarVisual(m_carDir);
    return true;
}

void SimulationLoop::beginRaceSession() {
    // Roadmap 2.8: the starting field (drivers, numbers, liveries, garage
    // boxes, grid slots) comes from team.ini when one is loaded; without it
    // buildFieldFromTeam yields the legacy identity (synthetic "AI N",
    // standings keep falling back to "Car N" because driverName is empty).
    m_sessionField = buildFieldFromTeam(m_team, m_aiCarCount);
    std::fprintf(stderr, "%s", fieldTable(m_sessionField, m_team.name).c_str());

    // AI field (roadmap 3.5): respawn on every session so a reset re-staggers
    // everyone behind the line. Only our own ids are removed — cars spawned
    // by the multiplayer transport stay in the manager.
    if (m_multiCar) {
        for (int id : m_aiCarIds) m_multiCar->removeCar(id);
        m_aiCarIds.clear();
        m_aiLastLaps.clear();
        despawnAiVisuals(); // session respawn owns the AI visuals (2.8)
        if (m_aiCarCount > 0 && !m_trackData.directory.empty()) {
            // Roster first: each non-player slot keeps its grid slot, so the
            // player's row is reserved on the grid (roadmap 2.8). Spawn order
            // == field order, which is the zip used for identity + visuals.
            std::vector<MultiCarManager::GridCar> aiField;
            for (const auto& slot : m_sessionField) {
                if (slot.isPlayer) continue;
                aiField.push_back({slot.driverName,
                                   slot.carModel.empty()
                                       ? (m_carName.empty() ? "ks_car" : m_carName)
                                       : slot.carModel,
                                   slot.gridSlot});
            }
            m_aiCarIds = m_multiCar->spawnGrid(aiField, 6.0f);
            m_aiLastLaps.assign(m_aiCarIds.size(), 0);
            std::fprintf(stderr, "SimulationLoop: spawned %d AI cars\n",
                         static_cast<int>(m_aiCarIds.size()));
            spawnSessionAiVisuals(); // renderer-backed, no-op headless (2.8)
        }
    }
    // Roadmap 2.8: the player takes his garage box or grid slot too (rF2-like
    // policy: race = grid, practice/qualify = garage, time attack = line).
    placePlayerForSession();

    RaceConfig rc;
    // The session byte decides the RaceSessionManager type: race gets the
    // 5 s countdown and a 10-lap-style limit already set by the caller;
    // practice / qualifying / time attack start green with open laps.
    if (m_sessionType == SESSION_RACE)
        rc.sessionType = RaceConfig::SessionType::Race;
    else if (m_sessionType == SESSION_QUALIFYING)
        rc.sessionType = RaceConfig::SessionType::Qualifying;
    else
        rc.sessionType = RaceConfig::SessionType::Practice;
    rc.trackLength = std::max(100.0f, m_trackData.splineLength);
    rc.totalLaps = m_totalLaps;
    // The clock the session ends on (0 = open session).
    rc.sessionTimeSeconds = m_sessionTimeLimitSeconds;
    rc.numCars = 1 + static_cast<int>(m_aiCarIds.size());
    m_raceSession.configure(rc);
    m_raceSession.setPlayerCarIndex(0);
    // Roadmap 2.8: roster identity reaches the standings — standings[0] is
    // the player, then the AI in spawn (= field) order; grid positions stay
    // the roster's, so RESULTS shows real drivers and door numbers.
    if (!m_team.slots.empty()) {
        const FieldSlot* player = nullptr;
        for (const auto& s : m_sessionField)
            if (s.isPlayer) { player = &s; break; }
        auto apply = [this](int idx, const FieldSlot& s) {
            m_raceSession.setDriverIdentity(
                idx, s.driverName, s.carModel.empty() ? m_carName : s.carModel,
                s.raceNumber, s.gridSlot + 1);
        };
        if (player) apply(0, *player);
        int standing = 1;
        for (const auto& s : m_sessionField) {
            if (s.isPlayer) continue;
            if (standing >= rc.numCars) break;
            apply(standing++, s);
        }
    }
    m_raceSession.startSession();
    if (rc.sessionType == RaceConfig::SessionType::Race && m_sessionStandingStart) {
        m_raceSession.startCountdown(5.0f);
        m_sessionPhase = PHASE_COUNTDOWN;
        m_timeRemaining = 5.0;
    } else {
        // Green straight away: an open session, or a race asked for a rolling
        // start. onCountdownFinished never fires, so the phase is set here —
        // lap timing was armed by start().
        m_sessionPhase = PHASE_GREEN_FLAG;
        m_timeRemaining = 0.0;
    }
    m_goldenExportActive = false;
    m_goldenTime = 0.0;
    m_goldenSampleAccum = 0.0;
    if (const char* csvPath = std::getenv("KS_GOLDEN_CSV"); csvPath && *csvPath) {
        m_goldenExportPath = csvPath;
        m_goldenRecorder.clearRef();
        m_goldenExportActive = true;
    }
}

void SimulationLoop::finishGoldenExport() {
    if (!m_goldenExportActive) return;
    m_goldenExportActive = false;
    if (m_goldenRecorder.refCount() == 0) return;
    if (m_goldenRecorder.saveCsv(m_goldenExportPath))
        std::fprintf(stderr, "[golden] exported %zu samples -> %s\n",
                     m_goldenRecorder.refCount(), m_goldenExportPath.c_str());
}

// Roadmap 2.8 — the player takes his garage box or grid slot at session
// start (rF2-like policy: race = grid, practice/qualy = garage box, time
// attack = on the line), anchored on the same spline geometry as the AI.
// No spline = no world anchor: content-baked / trackless runs keep the
// legacy behaviour (spawn wherever the physics default put the car).
void SimulationLoop::placePlayerForSession() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle || !m_multiCar) return;
    const FieldSlot* pslot = nullptr;
    for (const auto& s : m_sessionField)
        if (s.isPlayer) { pslot = &s; break; }
    const int slot = pslot ? pslot->gridSlot : 0;

    vec3 gpos;
    float gheading = 0.0f;
    if (!m_multiCar->gridPose(slot, 6.0f, gpos, gheading)) return;

    // Session byte -> the policy's SessionType (3/4 are time attack / hotlap).
    SessionType st = SessionType::Practice;
    switch (m_sessionType) {
        case SESSION_QUALIFYING: st = SessionType::Qualify; break;
        case SESSION_RACE:       st = SessionType::Race; break;
        case 3: case 4:          st = SessionType::Hotlap; break;
        default: break;
    }

    // One pose per field slot (index == gridSlot): the policy's grid branch
    // reads this, and the garage fallback lands on the line instead of the
    // origin when a box is missing.
    const int nslots = std::max(static_cast<int>(m_sessionField.size()), slot + 1);
    std::vector<WorldPose> gridPoses(static_cast<size_t>(nslots),
                                     WorldPose{gpos.x, gpos.y, gpos.z, gheading});
    for (int i = 0; i < nslots; ++i) {
        vec3 p;
        float h = 0.0f;
        if (m_multiCar->gridPose(i, 6.0f, p, h))
            gridPoses[static_cast<size_t>(i)] = WorldPose{p.x, p.y, p.z, h};
    }

    // Garage row beside the start line: track pit boxes are not wired yet
    // (their loader lives in the orphan FeatureMethods TU), so a linear row
    // 12 m off the line carries the team's Garage= indices. Practice/qualy
    // spawn no AI, so nothing else occupies that strip.
    int boxes = 20;
    for (const auto& slot : m_team.slots)
        if (slot.active) boxes = std::max(boxes, slot.garageIndex + 1);
    const WorldPose& first = gridPoses[0];
    WorldPose rowStart = first;
    rowStart.x += -std::cos(first.heading) * 12.f;
    rowStart.z += std::sin(first.heading) * 12.f;
    // makeLinearRow advances the boxes along pitHeading+90°: rotating the
    // heading by +90° makes the row recede upstream (parallel to the track,
    // cars facing it) instead of marching straight across the grid.
    const GarageLayout layout = GarageSpawnPolicy::makeLinearRow(
        boxes, rowStart, 6.f, first.heading + 1.5707963f);
    GarageLayout assigned = layout;
    if (!m_team.slots.empty()) GarageSpawnPolicy::assignTeamToGarage(assigned, m_team);

    SpawnRequest req;
    req.session = st;
    req.garageIndex = pslot ? std::max(0, pslot->garageIndex) : 0;
    req.gridPosition = slot + 1;
    // The row is sized to cover every roster garage index, so the Garage
    // branch always finds the box; Track (hotlap/time attack) means "on the
    // line" and resolves here, because the policy leaves that to the caller.
    WorldPose pose;
    if (GarageSpawnPolicy::locationForSession(st) == SpawnLocation::Track)
        pose = gridPoses[static_cast<size_t>(slot)];
    else
        pose = GarageSpawnPolicy::resolvePose(req, assigned, &gridPoses);

    auto& state = m_vehicle->state();
    state.position = {pose.x, pose.y, pose.z};
    state.heading = pose.heading;
    std::fprintf(stderr,
                 "[team] player spawn: slot %d at (%.1f, %.1f, %.1f) heading %.2f [%s]\n",
                 slot + 1, pose.x, pose.y, pose.z, pose.heading,
                 GarageSpawnPolicy::locationForSession(st) == SpawnLocation::Garage
                     ? "garage box"
                     : GarageSpawnPolicy::locationForSession(st) == SpawnLocation::Grid
                           ? "grid" : "line");
    // Roadmap 2.7: remember where the player's box is, so updatePitRepair()
    // knows what "stopped inside the box" means later in the session. Bound
    // to the garage row (not to the spawn pose): a car that starts on the
    // grid can still drive back to its own box for service.
    if (const GarageBox* pb = assigned.find(req.garageIndex))
        bindPlayerBox(pb->pose);
    else if (!assigned.boxes.empty())
        bindPlayerBox(assigned.boxes.front().pose);

    // Roadmap 2.7: bind the garage-exit state machine to the same box. A
    // session that spawns in the garage starts held there (leave on request,
    // pit limiter on the way out); a grid/track start is forced OnTrack and
    // behaves exactly as before — the machine only adds the way back in.
    if (m_hasPlayerBox) {
        m_garageExit.bindBox(req.garageIndex, m_playerBox, assigned.pitLaneHeading);
        m_garageExitEnabled = true;
        // Roadmap 2.7 — multi-car pit-lane order (PitLaneQueue): runs on the
        // same box and heading as the garage exit. With a single car on track
        // nothing can occupy the lane, so those sessions behave exactly as
        // before; KS_PIT_QUEUE=0 opts out entirely.
        {
            PitAxis pitAxis;
            pitAxis.originX = m_playerBox.x;
            pitAxis.originZ = m_playerBox.z;
            pitAxis.heading = assigned.pitLaneHeading;
            m_pitQueue = PitLaneQueue(); // fresh queue for this session
            m_pitQueue.setAxis(pitAxis);
            const char* pitQueueEnv = std::getenv("KS_PIT_QUEUE");
            m_pitQueueEnabled = !(pitQueueEnv && pitQueueEnv[0] == '0');
        }
        if (GarageSpawnPolicy::locationForSession(st) != SpawnLocation::Garage)
            m_garageExit.forceOnTrack();
        else
            std::fprintf(stderr,
                         "SimulationLoop: IN GARAGE — press P (or LEAVE GARAGE "
                         "in the GARAGE menu) to leave the box\n");
    } else {
        m_garageExitEnabled = false;
    }
#endif
}

// Roadmap 2.8 — visual entities for the spawned AI cars: one mesh set per
// livery key (base textures when the slot has no skin folder), shared by
// every car that wears it. No-op without a renderer, an AI field or a bake.
void SimulationLoop::spawnSessionAiVisuals() {
    if (!m_vulkanRenderer || !m_multiCar || m_aiCarIds.empty()) return;
    const std::string baked = ks::sim::findBakedManifestDir(m_carDir);
    if (baked.empty()) return; // unbaked car: AI stay physics-only (as before)

    int ord = 0;
    for (const auto& slot : m_sessionField) {
        if (slot.isPlayer) continue;
        if (ord >= static_cast<int>(m_aiCarIds.size())) break;
        const int carId = m_aiCarIds[ord++];

        const std::string key = slot.liveryId.empty() ? std::string("base") : slot.liveryId;
        const std::string prefix = "ai_" + key + "_";
        if (m_aiMeshSets.find(prefix) == m_aiMeshSets.end()) {
            std::string textureDir = baked + "/textures";
            if (!slot.liveryId.empty()) {
                const std::string cand = m_carDir + "/liveries/" + slot.liveryId;
                if (std::filesystem::is_directory(cand)) textureDir = cand;
            }
            std::vector<std::string> names = loadCarMeshSet(baked, prefix, textureDir);
            if (names.empty()) continue; // no mesh for this slot: stays invisible
            m_aiMeshSets.emplace(prefix, std::move(names));
        }

        AiVisual av;
        av.carId = carId;
        av.liveryKey = slot.liveryId;
        for (const auto& renderName : m_aiMeshSets.at(prefix)) {
            const ks::ecs::Entity e = scene().create();
            if (e == ks::ecs::kNullEntity) break;
            scene().emplace<ks::ecs::Name>(e, ks::ecs::Name{renderName});
            scene().emplace<ks::ecs::Transform>(e);
            scene().emplace<ks::ecs::MeshInstance>(e, ks::ecs::MeshInstance{renderName});
            av.entities.push_back(e);
        }
        // Land on the grid pose immediately so no frame is drawn at origin.
        if (CarEntry* c = m_multiCar->getCar(carId); c && c->vehicle) {
            const auto as = c->vehicle->getState();
            const ks::math::vec3 apos{as.position.x, as.position.y, as.position.z};
            const ks::math::vec3 arot{as.rotation.x, as.rotation.y, as.rotation.z};
            for (const ks::ecs::Entity e : av.entities)
                if (auto* t = scene().tryGet<ks::ecs::Transform>(e)) {
                    t->position = apos;
                    t->rotation = arot;
                }
        }
        if (!av.entities.empty()) m_aiVisuals.push_back(std::move(av));
    }
    if (!m_aiVisuals.empty())
        std::fprintf(stderr, "[scene] AI visuals: %zu car(s), %zu mesh set(s)\n",
                     m_aiVisuals.size(), m_aiMeshSets.size());
}

void SimulationLoop::despawnAiVisuals() {
    if (m_vulkanRenderer) {
        for (const auto& kv : m_aiMeshSets) // each render name exactly once
            for (const auto& n : kv.second) m_vulkanRenderer->destroyMesh(n);
    }
    for (auto& av : m_aiVisuals)
        for (const ks::ecs::Entity e : av.entities)
            if (scene().valid(e)) scene().destroy(e);
    m_aiVisuals.clear();
    m_aiMeshSets.clear();
}

void SimulationLoop::start() {
    if (m_running) return;
    m_running = true;
    Engine::instance().start();
#if HAS_VEHICLE_SIM
    if (m_vehicle) m_vehicle->startSimulation();
#endif
    m_lastTime = std::chrono::steady_clock::now();
    // Session byte / lap limit are NOT forced here: startSession() and the
    // control API set them before this runs, and a bare start() (F5,
    // KS_HEADLESS) keeps whatever the current session is (default: practice,
    // open laps — member defaults 0).
    m_currentLap = 0;
    m_lapDistance = 0; m_normalizedSpline = 0; m_simTime = 0;
    m_lapTimer.reset(); m_lapTimer.start();
    beginRaceSession();
    m_ui.menu().setVisible(false);
    if (onSimulationStarted) onSimulationStarted();
}

void SimulationLoop::stop() {
    Engine::instance().stop();
    if (!m_running) return;
    m_running = false;
    m_lapTimer.stop();
#if HAS_VEHICLE_SIM
    if (m_vehicle) m_vehicle->stopSimulation();
#endif
    finishGoldenExport();
    if (onSimulationStopped) onSimulationStopped();
}

void SimulationLoop::startSession(GameSessionMode mode) {
    // Rows that skip MODE SETUP start with the mode's own defaults; a race
    // still promises a grid, as the menu did before MODE SETUP existed.
    SessionStartParams p = defaultsForMode(mode);
    if (mode == GameSessionMode::Race && p.aiCars == 0) p.aiCars = 5;
    startSession(mode, p);
}

void SimulationLoop::startSession(GameSessionMode mode, const SessionStartParams& params) {
    // A session starts from the showroom: the turntable stops owning the
    // camera (and its music) the moment the player is driving.
    closeShowroom();
    if (m_running) stop(); // picking a mode from the menu starts a fresh session
    // Configure BEFORE start(): beginRaceSession() reads the session byte
    // (countdown + RaceSessionManager type), the lap limit and the AI count.
    m_sessionType = toNetSessionType(mode);
    // 0 = open session (RaceSessionManager only finishes on totalLaps > 0).
    m_totalLaps = std::max(0, params.totalLaps);
    // The grid the player asked for wins over the count left by the last
    // session. A race with no AI in either place still fills five places;
    // practice, hotlap and time attack are solo by definition.
    // MODE SETUP decides the grid; a race with no AI is a solo race, which
    // the screen now lets the player ask for.
    if (mode == GameSessionMode::Race)
        setAiCarCount(std::max(0, params.aiCars));
    else
        setAiCarCount(0); // practice, hotlap and time attack are solo by definition
    std::fprintf(stderr, "SimulationLoop: menu session %s (%d laps, %d AI, clock %ds)\n",
                 sessionModeName(mode), m_totalLaps, m_aiCarCount, params.sessionTimeSeconds);
    // beginRaceSession() reads these: countdown or green flag, and the clock.
    m_sessionStandingStart = params.standingStart;
    m_sessionTimeLimitSeconds = std::max(0, params.sessionTimeSeconds);
    start();
    // The manager owns the clock now; the HUD reads it back every frame.
    m_timeRemaining = static_cast<double>(m_sessionTimeLimitSeconds);
    // MODE SETUP's garage start: the pose policy already parks practice and
    // qualifying in the box, so this only holds a car that started on the grid.
    if (params.startInGarage && m_hasPlayerBox)
        m_garageExit.forceEnterGarage();
}

void SimulationLoop::reset() {
#if HAS_VEHICLE_SIM
    if (m_vehicle) m_vehicle->reset();
#endif
    if (m_input) m_input->reset();
    m_simAccumulator = 0; m_currentLap = 0;
    m_lapDistance = 0; m_normalizedSpline = 0; m_simTime = 0;
    m_lapTimer.reset();
    beginRaceSession();
    if (m_running) m_lapTimer.start();
}

namespace {
// Paths handed to the control API come from the network: reject traversal
// and NULs before they ever reach the filesystem.
bool isSafeControlPath(const std::string& path) {
    if (path.empty() || path.size() > 4096) return false;
    if (path.find('\0') != std::string::npos) return false;
    std::string norm = path;
    for (char& c : norm)
        if (c == '\\') c = '/';
    return !(norm.find("/../") != std::string::npos || norm.rfind("../", 0) == 0 ||
             norm == "..");
}
} // namespace

// FeatureHub glue. FeatureHub::startServices() already routes the TCP verbs
// (SESSION / WEATHER / TIME / LIMITS / PB / REPLAY / RESULT) to its own
// handlers; these callbacks are the road from those handlers into the loop -
// without them the hub runs but does nothing. Discovery itself is driven by
// the apps (announceHost / queryLan), exactly as before.
void SimulationLoop::startFeatureServices(bool hostAnnounce) {
    m_features.startServices(hostAnnounce);

    m_features.onBeginSession = [this](GameSessionMode mode,
                                       const SessionStartParams& p) {
        if (!m_running) start();
        m_sessionType = toNetSessionType(mode);
        // 0 = no lap limit (RaceSessionManager treats totalLaps <= 0 as an
        // open session: practice, qualifying, time attack).
        m_totalLaps = p.totalLaps > 0 ? std::min(p.totalLaps, 200) : 0;
        m_currentLap = 0;
        reset(); // re-staggers the grid and reconfigures RaceSessionManager
        std::fprintf(stderr, "SimulationLoop: %s session requested (%d laps)\n",
                     sessionModeName(mode), m_totalLaps);
    };

    m_features.onSetTimeOfDay = [this](float hours) { setTimeOfDay(hours); };

    m_features.onSetWeather = [this](const std::string& name) {
        m_features.weatherCtrl.applyPreset(name);
        const WeatherPreset& wp = m_features.weatherCtrl.weather();
        ks::physics::WeatherState ws = m_weather;
        ws.ambientTemp = wp.ambientC;
        ws.trackTemp = wp.trackC;
        ws.trackWetness = wp.wetness;
        ws.rainIntensity = wp.rain;
        ws.windSpeed = wp.windMs;
        ws.windDirection = wp.windDirDeg;
        ws.cloudCover = wp.cloud;
        setWeatherPreset(ws);
        std::fprintf(stderr, "SimulationLoop: weather -> %s\n", name.c_str());
    };

    m_features.onPenalty = [this](int car, int kind, float value,
                                  const std::string& reason) {
        Penalty::Type ty = Penalty::Type::TimeAdded;
        if (kind == 1) ty = Penalty::Type::DriveThrough;
        else if (kind == 2) ty = Penalty::Type::StopGo;
        m_raceSession.addPenalty(car, ty, value, reason);
        // Roadmap 2.7: a stop-go is served by the same box service the player
        // requests manually — same flag, same gate (stopped inside the box).
        if (kind == 2) m_requestPitService = true;
        std::fprintf(stderr, "SimulationLoop: penalty car %d (%s)\n", car,
                     reason.c_str());
    };

    m_features.onSetupLoad = [this](const std::string& path) {
        if (!m_setupGarage || !isSafeControlPath(path)) return;
        SetupData s = m_setupGarage->setup();
        if (loadSetupFromFile(s, path)) {
            m_setupGarage->setSetup(s);
            std::fprintf(stderr, "SimulationLoop: setup loaded from %s\n", path.c_str());
        }
    };

    m_features.onSetupSave = [this](const std::string& path) {
        if (!m_setupGarage || !isSafeControlPath(path)) return;
        if (saveSetupToFile(m_setupGarage->setup(), path))
            std::fprintf(stderr, "SimulationLoop: setup saved to %s\n", path.c_str());
    };

    m_features.onRequestResults = [this]() {
        for (const auto& d : m_raceSession.standings()) {
            char line[192];
            std::snprintf(line, sizeof(line),
                          "RESULT pos=%d car=%s driver=%s lap=%d best=%.3f%s",
                          d.position, d.carName.c_str(), d.driverName.c_str(),
                          d.currentLap, d.bestLapTime,
                          d.finished ? " finished" : "");
            m_features.control.emitEvent(line);
        }
    };
}

void SimulationLoop::pumpFeatureHub(float dt, bool withSession) {
    vec3 pos{};
#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        const auto st = m_vehicle->getState();
        pos = {static_cast<float>(st.position.x),
               static_cast<float>(st.position.y),
               static_cast<float>(st.position.z)};
    }
#endif
    m_features.tick(dt, withSession ? &m_raceSession : nullptr, 0, pos,
                    m_sessionPhase == PHASE_GREEN_FLAG);
}

bool SimulationLoop::handleUiKey(int vk) { return m_ui.handleKey(vk); }
bool SimulationLoop::handleUiChar(int c) { return m_ui.handleChar(c); }
bool SimulationLoop::handleUiMouseMove(float x, float y) { return m_ui.handleMouseMove(x, y); }
bool SimulationLoop::handleUiMouseButton(ui::MouseButton b, bool down, float x, float y) {
    return m_ui.handleMouseButton(b, down, x, y);
}
bool SimulationLoop::handleUiMouseWheel(float d, float x, float y) {
    return m_ui.handleMouseWheel(d, x, y);
}

void SimulationLoop::applyInput() {
#if HAS_KSNET
    // Remote drivers (roadmap 3.1): their controls arrive with the network
    // pump, so they are latched here and pushed to the vehicle once per
    // physics step - exactly like the local player's controls below. The
    // driving-input block (menus/HUD) intentionally does not gate them.
    if (m_multiCar) {
        for (auto it = m_remoteInputs.begin(); it != m_remoteInputs.end();) {
            CarEntry* car = m_multiCar->getCarByClientIndex(it->first);
            if (!car || !car->vehicle) { it = m_remoteInputs.erase(it); continue; }
            car->vehicle->setThrottle(it->second.throttle);
            car->vehicle->setBrake(it->second.brake);
            car->vehicle->setSteering(it->second.steering);
            ++it;
        }
    }
#endif
    if (!m_input) return;
    const bool blocked = m_ui.blocksDrivingInput();
    if (!blocked) m_input->update();
    // The pad drives the menu even while the menu is blocking the car's input:
    // polled outside that branch, and handed to the UI path only, so a d-pad
    // press can never reach the vehicle.
    if (m_input) {
        m_input->pollMenuNavigation();
        if (const int padKey = m_input->takeMenuKey()) m_ui.handleKey(padKey);
    }
    ks::engine::devices::InputState input;
    input.throttle = m_input->throttle();
    input.brake = m_input->brake();
    input.steering = m_input->steer();
    ks::engine::devices::InputSystem::instance().setState(input);
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    if (blocked) { m_vehicle->setThrottle(0); m_vehicle->setBrake(0); m_vehicle->setSteering(0); return; }
    m_vehicle->setThrottle(m_input->throttle());
    m_vehicle->setBrake(m_input->brake());
    m_vehicle->setSteering(m_input->steer());
#endif
}

void SimulationLoop::updateLapAndSurface(double dt) {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    const float speed = std::max(0.f, static_cast<float>(st.speed));
    m_lapDistance += speed * static_cast<float>(dt);
    const float len = std::max(100.f, m_trackData.splineLength);
    m_normalizedSpline = std::fmod(m_lapDistance, len) / len;
    if (m_normalizedSpline < 0.f) m_normalizedSpline += 1.f;
    const int lapsBefore = m_lapTimer.completedLaps();
    m_lapTimer.update(dt, m_normalizedSpline);
    if (m_lapTimer.completedLaps() > lapsBefore) {
        m_currentLap = m_lapTimer.completedLaps();
        // FeatureHub: personal-best store + LAP/PB events on the control API.
        const float lapSec = static_cast<float>(m_lapTimer.lastTimeMs()) * 0.001f;
        const std::string& trackKey =
            m_trackData.name.empty() ? m_trackData.directory : m_trackData.name;
        m_features.onLapCompleted(trackKey, m_carName, "PLAYER", lapSec, nullptr);
        if (onSessionStateChanged)
            onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
    }
    ks::physics::PhysVec3 p{st.position.x, st.position.y, st.position.z};
    if (speed > 5.f && st.throttle > 0.8f)
        ks::physics::TrackSurface::instance().depositRubber(p, 0.0002f * static_cast<float>(dt), 1.5f);
#endif
}

bool SimulationLoop::openUdp() {
    if (!m_udp) m_udp = std::make_unique<UdpTelemetryBridge>();
    m_udp->setEnabled(m_udpEnabled);
    return m_udp->open(m_udpHost.c_str(), m_udpPort);
}

bool SimulationLoop::openTcp() {
    if (!m_tcp) m_tcp = std::make_unique<TcpTelemetryBridge>();
    m_tcp->setEnabled(m_tcpEnabled);
    return m_tcp->listen("0.0.0.0", m_tcpPort);
}

void SimulationLoop::publishUdpTelemetry() {
    if (!m_udp || !m_udpEnabled) return;
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    UdpTelemSample s;
    s.timeSec = m_simTime;
    s.speedMs = static_cast<float>(st.speed);
    s.rpm = static_cast<float>(m_vehicle->rpm());
    s.throttle = static_cast<float>(st.throttle);
    s.brake = static_cast<float>(st.brake);
    s.steer = static_cast<float>(st.steering);
    s.gear = m_vehicle->currentGear();
    s.fuelL = static_cast<float>(st.fuel);
    s.posX = st.position.x; s.posY = st.position.y; s.posZ = st.position.z;
    s.velX = st.velocity.x; s.velY = st.velocity.y; s.velZ = st.velocity.z;
    s.accGX = st.acceleration.x / 9.81f; s.accGY = st.acceleration.y / 9.81f; s.accGZ = st.acceleration.z / 9.81f;
    s.heading = st.heading;
    for (int i = 0; i < 4; ++i) {
        s.tyreTemp[i] = static_cast<float>(st.tyreTemp[i]);
        s.tyreWear[i] = static_cast<float>(st.tyreWear[i]);
        s.tyrePressure[i] = static_cast<float>(st.tyrePressure[i]);
    }
    s.completedLaps = m_lapTimer.completedLaps();
    s.currentSector = m_lapTimer.sectorIndex();
    s.currentTimeMs = m_lapTimer.currentTimeMs();
    s.lastTimeMs = m_lapTimer.lastTimeMs();
    s.bestTimeMs = m_lapTimer.bestTimeMs();
    s.position = 1;
    s.sessionType = static_cast<int>(m_sessionType);
    s.status = m_running ? 2 : 0;
    s.normalizedSpline = m_normalizedSpline;
    s.airTemp = m_weather.ambientTemp;
    s.roadTemp = m_weather.trackTemp;
    s.inPit = st.inPitLane;
    s.pitLimiter = st.pitLimiterActive;
    // Roadmap 1.5 / P1.10: damage channels (docs/DAMAGE_TELEMETRY.md).
    {
        const auto dmg = ks::physics::sampleDamage(m_vehicle->damage());
        s.damageOverall = dmg.overall;
        s.engineHealth = dmg.engineHealth;
        s.powerMult = dmg.powerMult;
        s.dragMult = dmg.dragMult;
        s.downforceMult = dmg.downforceMult;
        s.damageWarning = dmg.warningLevel;
        s.engineSeized = dmg.engineSeized;
        for (int i = 0; i < 5; ++i) s.carDamage[i] = dmg.carDamage[i];
        for (int i = 0; i < 4; ++i) s.suspIntegrity[i] = dmg.suspIntegrity[i];
    }
    m_udp->publish(s);
#endif
}

void SimulationLoop::publishTcpTelemetry() {
    if (!m_tcp || !m_tcpEnabled) return;
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    UdpTelemSample s;
    s.timeSec = m_simTime;
    s.speedMs = static_cast<float>(st.speed);
    s.rpm = static_cast<float>(m_vehicle->rpm());
    s.throttle = static_cast<float>(st.throttle);
    s.brake = static_cast<float>(st.brake);
    s.steer = static_cast<float>(st.steering);
    s.gear = m_vehicle->currentGear();
    s.fuelL = static_cast<float>(st.fuel);
    s.posX = st.position.x; s.posY = st.position.y; s.posZ = st.position.z;
    s.velX = st.velocity.x; s.velY = st.velocity.y; s.velZ = st.velocity.z;
    s.accGX = st.acceleration.x / 9.81f; s.accGY = st.acceleration.y / 9.81f; s.accGZ = st.acceleration.z / 9.81f;
    s.heading = st.heading;
    for (int i = 0; i < 4; ++i) {
        s.tyreTemp[i] = static_cast<float>(st.tyreTemp[i]);
        s.tyreWear[i] = static_cast<float>(st.tyreWear[i]);
        s.tyrePressure[i] = static_cast<float>(st.tyrePressure[i]);
    }
    s.completedLaps = m_lapTimer.completedLaps();
    s.currentSector = m_lapTimer.sectorIndex();
    s.currentTimeMs = m_lapTimer.currentTimeMs();
    s.lastTimeMs = m_lapTimer.lastTimeMs();
    s.bestTimeMs = m_lapTimer.bestTimeMs();
    s.position = 1;
    s.sessionType = static_cast<int>(m_sessionType);
    s.status = m_running ? 2 : 0;
    s.normalizedSpline = m_normalizedSpline;
    s.airTemp = m_weather.ambientTemp;
    s.roadTemp = m_weather.trackTemp;
    s.inPit = st.inPitLane;
    s.pitLimiter = st.pitLimiterActive;
    // Roadmap 1.5 / P1.10: same damage channels as the UDP path.
    {
        const auto dmg = ks::physics::sampleDamage(m_vehicle->damage());
        s.damageOverall = dmg.overall;
        s.engineHealth = dmg.engineHealth;
        s.powerMult = dmg.powerMult;
        s.dragMult = dmg.dragMult;
        s.downforceMult = dmg.downforceMult;
        s.damageWarning = dmg.warningLevel;
        s.engineSeized = dmg.engineSeized;
        for (int i = 0; i < 5; ++i) s.carDamage[i] = dmg.carDamage[i];
        for (int i = 0; i < 4; ++i) s.suspIntegrity[i] = dmg.suspIntegrity[i];
    }
    m_tcp->publish(s);
#endif
}

void SimulationLoop::publishSharedMemory() {
    if (!m_shm || !m_shmEnabled) return;
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    const auto& ffb = m_vehicle->ffbSample();
    ks::ac::AcLiveInput live;
    live.throttle = static_cast<float>(st.throttle);
    live.brake = static_cast<float>(st.brake);
    live.steer = static_cast<float>(st.steering);
    live.speedMs = static_cast<float>(st.speed);
    live.rpm = static_cast<float>(m_vehicle->rpm());
    live.gear = m_vehicle->currentGear();
    live.fuel = static_cast<float>(st.fuel);
    live.velocity[0] = st.velocity.x; live.velocity[1] = st.velocity.y; live.velocity[2] = st.velocity.z;
    live.accG[0] = st.acceleration.x / 9.81f; live.accG[1] = st.acceleration.y / 9.81f; live.accG[2] = st.acceleration.z / 9.81f;
    live.heading = st.heading;
    live.wheelSlip[0] = ffb.slipAngleFL; live.wheelSlip[1] = ffb.slipAngleFR;
    live.wheelLoad[0] = ffb.loadFL; live.wheelLoad[1] = ffb.loadFR;
    live.finalFF = ffb.aligningMomentNm;
    for (int i = 0; i < 4; ++i) {
        live.tyreTemp[i] = static_cast<float>(st.tyreTemp[i]);
        live.tyreWear[i] = static_cast<float>(st.tyreWear[i]);
        live.tyrePressure[i] = static_cast<float>(st.tyrePressure[i]);
    }
    live.carX = st.position.x; live.carY = st.position.y; live.carZ = st.position.z;
    live.normalizedSpline = m_normalizedSpline;
    live.distanceTraveled = m_lapDistance;
    live.airTemp = m_weather.ambientTemp; live.roadTemp = m_weather.trackTemp;
    live.completedLaps = m_lapTimer.completedLaps();
    live.currentSector = m_lapTimer.sectorIndex();
    live.iCurrentTimeMs = m_lapTimer.currentTimeMs();
    live.iLastTimeMs = m_lapTimer.lastTimeMs();
    live.iBestTimeMs = m_lapTimer.bestTimeMs();
    live.sessionType = static_cast<int>(m_sessionType);
    live.status = m_running ? 2 : 0;
    live.inPit = st.inPitLane; live.pitLimiter = st.pitLimiterActive;
    live.carModel = m_carName; live.trackName = m_trackData.name;
    live.maxRpm = 8500; live.totalLaps = m_totalLaps;
    live.sectorCount = m_lapTimer.sectorCount();
    live.sessionTimeLeft = static_cast<float>(m_timeRemaining);
    live.trackSplineLength = m_trackData.splineLength;
    live.flag = ks::ac::ksRaceFlagToAcFlag(static_cast<int>(m_raceSession.flag()));
    // Roadmap 1.5 / P1.10: AC carDamage[5] body zones (front, rear, left,
    // right, overall) plus the ksengine-side damage snapshot in AcLiveInput.
    {
        const auto dmg = ks::physics::sampleDamage(m_vehicle->damage());
        for (int i = 0; i < 5; ++i) live.carDamage[i] = dmg.carDamage[i];
        live.damageOverall = dmg.overall;
        live.engineHealth = dmg.engineHealth;
        live.powerMult = dmg.powerMult;
        live.dragMult = dmg.dragMult;
        live.downforceMult = dmg.downforceMult;
        live.damageWarning = dmg.warningLevel;
        live.engineSeized = dmg.engineSeized;
    }
    m_shm->publish(live);
#endif
}

void SimulationLoop::applyShowroomLight() {
    if (!m_showroom.active() || !m_vulkanRenderer) return;
    {
        const ShowroomConfig& cfg = m_showroom.config();
        DirectionalLight light;
        // The file points at the sun, the renderer wants the way the light
        // travels (its own weather sun carries a negative y for daylight) —
        // feeding it unconverted left the room lit by ambient alone.
        light.direction = {-cfg.sunDirection[0], -cfg.sunDirection[1],
                           -cfg.sunDirection[2]};
        light.color = {cfg.sunColor[0], cfg.sunColor[1], cfg.sunColor[2]};
        light.intensity = cfg.sunIntensity;
        m_vulkanRenderer->setSun(light);
        m_vulkanRenderer->setFog({cfg.ambientColor[0], cfg.ambientColor[1], cfg.ambientColor[2]},
                                 0.0001f, 0.002f);
        // A photographed room is bright: without the extra stop the concrete and
        // the trees read as if the sun were going down. A post-processing
        // preset, when one is loaded (KS_PPFILTER), owns the display exposure
        // instead: it is the value the filter was written around, and the room
        // stop below is only the fallback for the no-preset case.
        const float presetExposure =
            ks::engine::config::PPFilterPreset::instance().display.exposure;
        m_vulkanRenderer->setExposure(presetExposure > 0.0f ? presetExposure : 1.35f);
    }
}

void SimulationLoop::updateWeather(float dt) {
    // A showroom comes with its own light. The menu runs the idle branch of
    // tick(), which never reaches this function, so the room applies its light
    // when it opens (see openShowroom) — this branch is for a session that
    // carries one.
    if (m_showroom.active()) {
        applyShowroomLight();
        return;
    }
    // Roadmap 2.1 (P2.8): evolve the preset first — rain soaks the track,
    // dry air dries it (dt == 0 while paused leaves the state untouched) —
    // then mirror the result so visuals, audio and TrackSurface all read
    // the same evolving wetness.
    m_weatherSim.update(dt);
    m_weather = m_weatherSim.state();
    auto& rs = ks::engine::graphics::RenderSystem::instance();
    const float phase = (m_timeOfDay - 12.0f) / 12.0f * 3.14159265f;
    const float c = std::cos(phase), s = std::sin(phase);
    const ks::engine::graphics::Vec3 sunDir{0.3f * c - 0.2f * s, -0.8f, 0.2f * c + 0.3f * s};
    rs.setSun(sunDir, {1.0f, 0.95f, 0.9f});
    rs.setFog(m_weather.cloudCover > 0.35f, {0.6f, 0.7f, 0.85f}, 0.0001f + m_weather.cloudCover * 0.002f);
    rs.setRain(m_weather.rainIntensity, m_weather.trackWetness);
    ks::physics::TrackSurface::instance().syncFromWeather(m_weather.trackWetness, m_weather.trackTemp);
    if (m_vulkanRenderer) {
        DirectionalLight light; light.direction = {sunDir.x, sunDir.y, sunDir.z};
        m_vulkanRenderer->setSun(light);
        // Weather-driven atmosphere. RenderSystem::setFog() above only stores
        // the values (it has no GPU work behind it), so the shader-facing fog
        // has to be fed here as well: density rises with cloud cover, while a
        // small base haze keeps distant geometry from looking unnaturally
        // crisp on a clear day.
        const float fogDensity = 0.0004f + m_weather.cloudCover * 0.004f;
        m_vulkanRenderer->setFog({0.6f, 0.7f, 0.85f}, fogDensity, 0.018f);
    }
}

void SimulationLoop::ensureScenePipeline() {
    if (m_pipelineInitialized || !m_vulkanRenderer) return;
    if (m_uiGpu) m_vulkanRenderer->setUiGpuPass(m_uiGpu);
    m_pipelineInitialized = true;
}

void SimulationLoop::syncUiFromVehicle() {
#if HAS_VEHICLE_SIM
    // In-car pause overlay gate (see inCarMenuAllowed): a live network session
    // counts as multiplayer, otherwise the driver is alone on track.
    const bool multiplayer =
        m_network && (m_network->isHosting() || m_network->isConnected());
    if (!m_vehicle) {
        m_ui.setInCarMenuAllowed(true); // no car, no in-car overlay to gate
        return;
    }
    const auto st = m_vehicle->getState();
    m_ui.setInCarMenuAllowed(
        inCarMenuAllowed(multiplayer, static_cast<float>(st.speed) * 3.6f));
    m_ui.telemetry().update(st.speed, m_vehicle->rpm(), st.throttle, st.brake, st.steering, 0.f, 0.f);
    // Roadmap 2.10 — single source of truth for the session display: the
    // player's row is read from RaceSessionManager (the same standings the
    // RESULTS screen and the control API report) instead of a hard-coded 1/1.
    const DriverStanding* ps = m_raceSession.playerStanding();
    const int posNow = (ps && ps->position > 0) ? ps->position : 1;
    const int carsNow = std::max(1, static_cast<int>(m_raceSession.standings().size()));
    m_ui.dashboard().update(st.speed, m_vehicle->rpm(), m_vehicle->currentGear(), st.throttle, st.brake,
        m_lapTimer.completedLaps(), m_lapTimer.currentTimeMs() * 0.001f, m_lapTimer.bestTimeMs() * 0.001f,
        0.f, posNow, carsNow);
    ui::RaceHudSample s;
    s.speedMs = static_cast<float>(st.speed); s.rpm = static_cast<float>(m_vehicle->rpm()); s.maxRpm = 8500.f;
    s.gear = m_vehicle->currentGear(); s.throttle = static_cast<float>(st.throttle);
    s.brake = static_cast<float>(st.brake); s.steer = static_cast<float>(st.steering);
    s.latG = st.acceleration.x / 9.81f; s.lonG = st.acceleration.z / 9.81f; s.fuelL = static_cast<float>(st.fuel);
    for (int i = 0; i < 4; ++i) { s.tyreTemp[i] = static_cast<float>(st.tyreTemp[i]); s.tyreWear[i] = static_cast<float>(st.tyreWear[i]); }
    s.position = posNow; s.totalCars = carsNow; s.lap = m_lapTimer.completedLaps() + 1; s.totalLaps = m_totalLaps;
    s.currentTimeMs = m_lapTimer.currentTimeMs(); s.lastTimeMs = m_lapTimer.lastTimeMs(); s.bestTimeMs = m_lapTimer.bestTimeMs();
    s.sector = m_lapTimer.sectorIndex(); s.inPit = st.inPitLane; s.pitLimiter = st.pitLimiterActive;
    // Roadmap 2.10 — one rendering point for the state the loop owns: the pit
    // service status wins while a stop is running, otherwise the garage exit
    // status (skipping the "ON TRACK" no-op label). Empty = stay quiet.
    s.statusText[0] = '\0';
    const std::string garageNow = garageStatus();
    if (pitServiceBusy())
        std::snprintf(s.statusText, sizeof(s.statusText), "%s", pitServiceStatus());
    else if (!garageNow.empty() && garageNow != "ON TRACK")
        std::snprintf(s.statusText, sizeof(s.statusText), "%s", garageNow.c_str());
    // Roadmap 1.5 / P1.10: damage strip channels (docs/DAMAGE_TELEMETRY.md —
    // HUD, UDP/TCP and SM all read the same sampleDamage snapshot).
    const auto dmg = ks::physics::sampleDamage(m_vehicle->damage());
    s.damageOverall = dmg.overall;
    s.engineHealth = dmg.engineHealth;
    s.powerMult = dmg.powerMult;
    s.damageWarning = dmg.warningLevel;
    s.engineSeized = dmg.engineSeized;
    m_ui.pushRaceSample(s);
#endif
}

// Rebuilds the camera from the simulated car state and hands the resulting
// view/projection to the renderer. Runs every frame before render(): beginFrame()
// snapshots the frustum for culling from exactly these matrices, so the camera
// has to be up to date before the frame starts, not halfway through it.
void SimulationLoop::updateCamera(float dt) {
    if (!m_camera) return;

    // Showroom: the camera rides the turntable and looks at the car. The
    // chase camera would keep snapping to a body that is not being driven.
    if (m_showroom.active()) {
        if (m_viewW > 0 && m_viewH > 0)
            m_camera->setAspectRatio(static_cast<float>(m_viewW) /
                                     static_cast<float>(m_viewH));
        m_showroom.apply(dt, *m_camera);
        m_camera->update(dt, mat4(), 0.0f); // free camera: view + projection
        if (m_vulkanRenderer)
            m_vulkanRenderer->setCamera(m_camera->viewMatrix(),
                                        m_camera->projectionMatrix(),
                                        m_camera->nearPlane(), m_camera->farPlane());
        return;
    }

    mat4 carBody;
    float speedKmh = 0.0f;
#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        const auto st = m_vehicle->getState();
        // rotation.y mirrors heading in VehicleSimulator, and the Euler order
        // here matches mat4::fromPositionRollPitchYaw's (roll, pitch, yaw).
        carBody = toNativeMat4(ks::math::mat4::fromPositionRollPitchYaw(
            {st.position.x, st.position.y, st.position.z},
            st.rotation.x, st.rotation.y, st.rotation.z));
        speedKmh = st.speed * 3.6f;
    }
#endif

    if (m_viewW > 0 && m_viewH > 0)
        m_camera->setAspectRatio(static_cast<float>(m_viewW) / static_cast<float>(m_viewH));
    m_camera->update(dt, carBody, speedKmh);

    if (m_vulkanRenderer)
        m_vulkanRenderer->setCamera(m_camera->viewMatrix(), m_camera->projectionMatrix(),
                                    m_camera->nearPlane(), m_camera->farPlane());
}

void SimulationLoop::render(float dt) {
    ensureScenePipeline();
    syncUiFromVehicle();
    auto& renderSys = ks::engine::graphics::RenderSystem::instance();
    renderSys.beginFrame();
    renderSys.runPass(ks::engine::graphics::RenderPass::Shadow);
    renderSys.runPass(ks::engine::graphics::RenderPass::Geometry);
    renderSys.runPass(ks::engine::graphics::RenderPass::Post);
    renderSys.endFrame();
    m_ui.renderFrame(m_viewW, m_viewH);
    if (m_vulkanRenderer) {
        // Periodic report: fps + submitted vs drawn vs culled, so the frustum
        // test's effect is visible without attaching a profiler.
        static uint32_t s_frame = 0;
        static std::chrono::steady_clock::time_point s_lastReport =
            std::chrono::steady_clock::now();
        ++s_frame;
        const auto nowReport = std::chrono::steady_clock::now();
        const double elapsedReport =
            std::chrono::duration<double>(nowReport - s_lastReport).count();
        if (elapsedReport >= 1.0) {
            const auto& st = m_vulkanRenderer->lastFrameStats();
            std::fprintf(stderr, "[render] %.1f fps submitted=%u drawn=%u culled=%u occluded=%u\n",
                         s_frame / elapsedReport, st.submitted, st.drawn, st.culled, st.occluded);
            // E9 / gate 2.14: the GPU half of the same frame, only when the
            // queue actually produced timestamps (0 is "no data", printed as
            // such rather than as a frame time).
            const double gpuMs = m_vulkanRenderer->gpuFrameTimeMs();
            if (gpuMs > 0.0)
                std::fprintf(stderr, "[render] gpu frame=%.3f ms avg=%.3f ms (~%d fps)\n",
                             gpuMs, m_vulkanRenderer->gpuAvgFrameTimeMs(),
                             m_vulkanRenderer->gpuProfiler().fps());
            if (gpuMs > 0.0) {
                // Same slots as GpuProfiler::Pass: the span each marker closes,
                // skipping markers the running path never wrote (forward writes four).
                auto& gp = m_vulkanRenderer->gpuProfiler();
                char passLine[256];
                passLine[0] = 0;
                int used = 0;
                for (int pi = 1; pi < ks::sim::GpuProfiler::kPassCount; ++pi) {
                    const auto pass = static_cast<ks::sim::GpuProfiler::Pass>(pi);
                    const double passMs = gp.passTimeMs(pass);
                    if (passMs <= 0.0 || used > 200) continue;
                    used += std::snprintf(passLine + used, sizeof(passLine) - used, " %s=%.3f",
                                          gp.passName(pass), passMs);
                }
                std::fprintf(stderr, "[render] gpu passes%s ms\n", passLine);
            }
            s_frame = 0;
            s_lastReport = nowReport;
        }
        m_vulkanRenderer->beginFrame();
        initTracksideTerrain();
        scene().each<ks::ecs::Transform, ks::ecs::MeshInstance>([this](ks::ecs::Entity, ks::ecs::Transform& t, ks::ecs::MeshInstance& mesh) {
            m_vulkanRenderer->drawMesh(mesh.meshName, toNativeMat4(ks::ecs::worldMatrix(t)));
        });
        // Roadmap P1: sprite quads, recorded with the same draw list (before
        // the UI overlay so the HUD is never covered by world-space dust).
        updateAndDrawParticles(dt);
        // KS_HIDE_MENU keeps the interface out of a capture (see the m_uiGpu
        // branch below, which is the other way the UI can reach a frame).
        if (std::getenv("KS_HIDE_MENU") == nullptr) {
            m_vulkanRenderer->drawUi(m_ui.renderer());
        }
        m_vulkanRenderer->endFrame();
    } else if (m_uiGpu) {
        // KS_HIDE_MENU keeps the interface out of a capture: the fixed
        // showroom views exist to judge materials, and the menu sits right
        // on top of the car.
        if (std::getenv("KS_HIDE_MENU") == nullptr) {
            m_uiGpu->uploadFrame(m_ui.renderer());
            m_uiGpu->draw();
        }
    }
}

void SimulationLoop::syncCarTransforms() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    const ks::math::vec3 pos{st.position.x, st.position.y, st.position.z};
    // Full body attitude (roll/pitch/yaw — rotation.y mirrors heading): with
    // position only, the visual slid through corners without ever turning,
    // while the camera already followed the rotated body.
    const ks::math::vec3 rot{st.rotation.x, st.rotation.y, st.rotation.z};
    // Tracked car visuals (roadmap 1.2): moved by entity id, so baked mesh
    // names that don't carry the "car_" prefix still follow the vehicle.
    for (const ks::ecs::Entity e : m_carVisualEntities)
        if (auto* t = scene().tryGet<ks::ecs::Transform>(e)) {
            t->position = pos;
            t->rotation = rot;
        }
    scene().each<ks::ecs::Transform, ks::ecs::MeshInstance>([&](ks::ecs::Entity, ks::ecs::Transform& t, ks::ecs::MeshInstance& mesh) {
        if (mesh.meshName.find("car_") != 0) return;
        t.position = pos;
        t.rotation = rot;
    });
    // Roadmap 2.8: every AI visual follows its own car entry — position and
    // attitude, because the AI steers along the spline.
    if (m_multiCar) {
        for (auto& av : m_aiVisuals) {
            CarEntry* c = m_multiCar->getCar(av.carId);
            if (!c || !c->vehicle) continue;
            const auto as = c->vehicle->getState();
            const ks::math::vec3 apos{as.position.x, as.position.y, as.position.z};
            const ks::math::vec3 arot{as.rotation.x, as.rotation.y, as.rotation.z};
            for (const ks::ecs::Entity e : av.entities)
                if (auto* t = scene().tryGet<ks::ecs::Transform>(e)) {
                    t->position = apos;
                    t->rotation = arot;
                }
        }
    }
#endif
}

ks::ecs::Registry& SimulationLoop::scene() { return Engine::instance().registry(); }

int SimulationLoop::loadBakedScene(const std::string& manifestDir) {
    if (!m_vulkanRenderer || manifestDir.empty() || manifestDir == m_spawnedSceneDir) return 0;
    if (m_vulkanRenderer->loadMeshesFromManifest(manifestDir) <= 0) return 0;
    if (!spawnSceneEntities(manifestDir)) return 0;
    m_spawnedSceneDir = manifestDir;
    std::fprintf(stderr, "[scene] %d entities from %s\n", static_cast<int>(scene().alive()),
                 manifestDir.c_str());
    return static_cast<int>(scene().alive());
}

// --- Roadmap 1.2 / GAP P2.1: stable track & car visuals ------------------

int SimulationLoop::spawnSceneEntities(const std::string& manifestDir) {
    std::ifstream manifest(manifestDir + "/manifest.txt");
    if (!manifest.is_open()) return 0;
    int spawned = 0;
    std::string name;
    while (std::getline(manifest, name)) {
        if (name.empty()) continue;
        const ks::ecs::Entity e = scene().create();
        if (e == ks::ecs::kNullEntity) break;
        scene().emplace<ks::ecs::Name>(e, ks::ecs::Name{name});
        scene().emplace<ks::ecs::Transform>(e);
        scene().emplace<ks::ecs::MeshInstance>(e, ks::ecs::MeshInstance{name});
        m_sceneEntities.push_back(e);
        ++spawned;
    }
    return spawned;
}

void SimulationLoop::despawnSceneEntities() {
    for (const ks::ecs::Entity e : m_sceneEntities)
        if (scene().valid(e)) scene().destroy(e);
    m_sceneEntities.clear();
}

// The marshal is one asset shared by every track: content/characters/marshal
// holds the bake (manifest.txt + .nmsh + textures/), and a track carries only
// the coordinates of its posts. Loaded as an instanced mesh, not as static
// scene, because it stands at a different place in every track.
bool SimulationLoop::loadMarshalAsset(const std::string& trackDir) {
    namespace fs = std::filesystem;
    if (m_marshalAssetLoaded) return true;
    if (!m_vulkanRenderer) return false;

    std::vector<fs::path> candidates;
    std::error_code ec;
    if (!trackDir.empty()) {
        // <content>/tracks/<track> -> <content>/objects3D/marshal
        candidates.push_back(fs::path(trackDir).parent_path().parent_path() / kMarshalAssetDir);
    }
    candidates.push_back(fs::current_path(ec) / "content" / kMarshalAssetDir);

    for (const fs::path& dir : candidates) {
        std::ifstream manifest(dir / "manifest.txt");
        if (!manifest) continue;
        std::string name;
        std::getline(manifest, name);
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
        if (name.empty()) continue;
        const fs::path file = dir / (name + ".nmsh");
        if (!fs::is_regular_file(file, ec)) continue;
        MaterialCache materials;
        const bool hasMaterials = materials.load((dir / "materials.txt").string());
        const MeshMaterial* material = hasMaterials ? materials.find(name) : nullptr;
        if (!m_vulkanRenderer->loadMeshFromFile("marshal", file.string(), material,
                                                (dir / "textures").string()))
            continue;
        m_marshalAssetLoaded = true;
        std::fprintf(stderr, "[posts] marshal asset %s (%s)\n", file.string().c_str(),
                     hasMaterials ? "materials.txt" : "no materials");
        return true;
    }
    std::fprintf(stderr, "[posts] no marshal asset under content/%s\n", kMarshalAssetDir);
    return false;
}

void SimulationLoop::despawnMarshalPosts() {
    for (const ks::ecs::Entity e : m_marshalEntities)
        if (scene().valid(e)) scene().destroy(e);
    m_marshalEntities.clear();
}

void SimulationLoop::spawnMarshalPosts() {
    despawnMarshalPosts();
    if (!m_marshalAssetLoaded || !m_vulkanRenderer) return;
    int posted = 0;
    for (const MarshalPost& post : m_tracksidePosts.posts()) {
        const ks::ecs::Entity e = scene().create();
        if (e == ks::ecs::kNullEntity) break;
        scene().emplace<ks::ecs::Name>(e,
                                      ks::ecs::Name{"marshal_post_" + std::to_string(posted)});
        ks::ecs::Transform transform;
        transform.position = ks::math::vec3{post.pose.x, post.pose.y, post.pose.z};
        transform.rotation = ks::math::vec3{0.f, post.pose.heading, 0.f};
        scene().emplace<ks::ecs::Transform>(e, transform);
        scene().emplace<ks::ecs::MeshInstance>(e, ks::ecs::MeshInstance{"marshal"});
        m_marshalEntities.push_back(e);
        ++posted;
    }
    std::fprintf(stderr, "[posts] %d marshal instance(s) from the shared asset\n", posted);
}

void SimulationLoop::despawnCarVisuals() {
    for (const ks::ecs::Entity e : m_carVisualEntities) {
        if (!scene().valid(e)) continue;
        if (m_vulkanRenderer)
            if (auto* mesh = scene().tryGet<ks::ecs::MeshInstance>(e))
                m_vulkanRenderer->destroyMesh(mesh->meshName);
        scene().destroy(e);
    }
    m_carVisualEntities.clear();
}

namespace {
// Vertex bounds of a baked .nmsh: "NMS2" is magic + counts + the lod window,
// "NMSH" is the legacy magic + counts. False on anything unreadable, so a
// malformed bake can never fabricate a spawn point.
bool readNmshBounds(const std::string& path, float outMin[3], float outMax[3]) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[4] = {0, 0, 0, 0};
    in.read(magic, 4);
    std::uint32_t counts[2] = {0, 0};
    in.read(reinterpret_cast<char*>(counts), sizeof(counts));
    if (!in) return false;
    const bool nms2 = std::memcmp(magic, "NMS2", 4) == 0;
    if (!nms2 && std::memcmp(magic, "NMSH", 4) != 0) return false;
    if (nms2) in.seekg(8, std::ios::cur); // lodIn, lodOut
    const std::uint32_t vertexCount = counts[0];
    if (vertexCount == 0 || vertexCount > 4000000u) return false;
    float v[12];
    bool any = false;
    for (std::uint32_t i = 0; i < vertexCount; ++i) {
        in.read(reinterpret_cast<char*>(v), sizeof(v));
        if (!in) return false;
        for (int k = 0; k < 3; ++k) {
            if (!any) { outMin[k] = v[k]; outMax[k] = v[k]; }
            else if (v[k] < outMin[k]) outMin[k] = v[k];
            else if (v[k] > outMax[k]) outMax[k] = v[k];
        }
        any = true;
    }
    return any;
}

bool markerCentre(const std::string& baked, const char* name, vec3& out) {
    float lo[3], hi[3];
    if (!readNmshBounds(baked + "/" + name + ".nmsh", lo, hi)) return false;
    out = vec3{(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    return true;
}

// AC keeps its spawn points as tiny marker meshes (AC_START_0,
// AC_HOTLAP_START_0, AC_PIT_0..N) and the baker applies each node's world
// matrix, so the box centre is the spawn position in world space. The pit row
// runs parallel to the track, so PIT_0 -> PIT_1 gives a usable yaw.
bool trackSpawnPose(const std::string& baked, vec3& pos, float& heading) {
    vec3 start, pit0, pit1;
    const bool hasStart = markerCentre(baked, "AC_START_0", start);
    const bool hasHotlap = !hasStart && markerCentre(baked, "AC_HOTLAP_START_0", start);
    const bool hasPit0 = markerCentre(baked, "AC_PIT_0", pit0);
    if (!hasStart && !hasHotlap && !hasPit0) return false;
    pos = (hasStart || hasHotlap) ? start : pit0;
    if (hasPit0 && markerCentre(baked, "AC_PIT_1", pit1)) {
        const float dx = pit1.x - pit0.x;
        const float dy = pit1.y - pit0.y;
        if (dx * dx + dy * dy > 0.25f) heading = std::atan2(dx, dy);
    }
    return true;
}
} // namespace


void SimulationLoop::applyTrackVisuals(const std::string& trackDir) {
    if (!m_vulkanRenderer) return; // headless: metadata-only load is valid
    const std::string baked = ks::sim::findBakedManifestDir(trackDir);
    if (baked.empty()) {
        // Unbaked or mesh-less track: keep the current scene on screen
        // instead of blanking it (this is the stability guarantee), and
        // print how to bake a .kn5 when there is one to bake.
        namespace fs = std::filesystem;
        if (fs::is_directory(trackDir)) {
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(trackDir, ec))
                if (e.path().extension() == ".kn5") {
                    std::fprintf(stderr,
                                 "[scene] track has .kn5 but no bake; run: "
                                 "kn5baker \"%s\" <out>/baked\n",
                                 e.path().string().c_str());
                    break;
                }
        }
        return;
    }
    if (baked == m_spawnedSceneDir) return;
    despawnSceneEntities();
    m_vulkanRenderer->clearStaticScene();
    if (m_vulkanRenderer->loadMeshesFromManifest(baked) <= 0) {
        std::fprintf(stderr, "[scene] track bake %s failed to load\n", baked.c_str());
        m_spawnedSceneDir.clear();
        return;
    }
    spawnSceneEntities(baked);
    m_spawnedSceneDir = baked;
    std::fprintf(stderr, "[scene] track visuals from %s (%d entities)\n",
                 baked.c_str(), static_cast<int>(m_sceneEntities.size()));
    // The baked world is not centred on the engine origin (vallelunga spans
    // Y -7620..+7827 and has nothing within 100 m of 0), so a car left at the
    // origin renders an empty frustum: 963 of 971 meshes cull away and the
    // track is loaded but invisible. Anchor the player on the track's own
    // spawn marker, which is what makes loading a real track mean anything.
    vec3 spawn;
    float spawnHeading = 0.0f;
    bool haveSpawn = trackSpawnPose(baked, spawn, spawnHeading);
    // A bake without AC markers (content converted from another format) still
    // has its AI line: the grid anchor is the same point its cars start from,
    // so use it rather than leaving the player at the engine origin.
    if (!haveSpawn && m_multiCar) {
        vec3 linePos;
        float lineHeading = 0.0f;
        if (m_multiCar->gridPose(0, 6.0f, linePos, lineHeading)) {
            spawn = linePos;
            spawnHeading = lineHeading;
            haveSpawn = true;
            std::fprintf(stderr, "[scene] no AC marker, spawn from the AI line\n");
        }
    }
    if (haveSpawn) {
#if HAS_VEHICLE_SIM
        if (m_vehicle) {
            auto& st = m_vehicle->state();
            st.position = {spawn.x, spawn.y, spawn.z};
            st.heading = spawnHeading;
        }
#endif
        std::fprintf(stderr,
                     "[scene] anchored at the track spawn (%.1f, %.1f, %.1f) heading %.2f\n",
                     spawn.x, spawn.y, spawn.z, spawnHeading);
    }
}

// Loads a bake's meshes under a name prefix — materials.txt, the texture dir
// (livery override) and the resolved appearance's node filter all apply.
// Shared by the player car ("car_") and the AI livery sets ("ai_<key>_"),
// roadmap 2.8/2.9.
std::vector<std::string> SimulationLoop::loadCarMeshSet(const std::string& baked,
                                                        const std::string& prefix,
                                                        const std::string& textureDir,
                                                        bool* materialsApplied) {
    std::vector<std::string> loaded;
    if (baked.empty() || !m_vulkanRenderer) return loaded;
    ks::sim::MaterialCache materials;
    const bool hasMaterials = materials.load(baked + "/materials.txt");
    if (materialsApplied) *materialsApplied = hasMaterials && materials.size() > 0;
    // Announce once for the player set only ("car_"); the AI livery sets
    // reuse the same materials silently to keep the log readable.
    if (prefix == "car_" && hasMaterials && materials.size() > 0)
        std::fprintf(stderr, "[scene] car materials.txt: %zu mesh material(s)\n",
                     materials.size());
    std::ifstream manifest(baked + "/manifest.txt");
    std::string name;
    while (manifest && std::getline(manifest, name)) {
        if (!name.empty() && name.back() == '\r') name.pop_back(); // CRLF manifests
        if (name.empty()) continue;
        // Roadmap 2.9: a node the appearance disabled (race [Nodes] x=0 or an
        // upgrade DisableNode) is not spawned at all.
        if (m_upgradeRt &&
            !ks::vehicle::appearanceNodeVisible(m_upgradeRt->appearance, name)) {
            std::fprintf(stderr, "[scene] node %s hidden by appearance\n", name.c_str());
            continue;
        }
        const ks::sim::MeshMaterial* material =
            hasMaterials ? materials.find(name) : nullptr;
        // Bodywork only: the mesh whose albedo is the CARBODY texture carries
        // the paint colour. Glass, wheels and lights keep their own textures,
        // so picking a paint never tints them.
        ks::sim::MeshMaterial painted;
        const ks::sim::MeshMaterial* useMaterial = material;
        if (material &&
            graphics::canonicalTextureName(material->albedo) == "CARBODY") {
            painted = *material;
            painted.tint[0] = m_carPaint[0];
            painted.tint[1] = m_carPaint[1];
            painted.tint[2] = m_carPaint[2];
            useMaterial = &painted;
            std::printf("[paint] body mesh %s tinted %.3f %.3f %.3f\n", name.c_str(),
                        m_carPaint[0], m_carPaint[1], m_carPaint[2]);
        }
        // Prefixed so baked names (original kn5 node names) can neither
        // collide with nor destroy scene meshes of the same name, and
        // so the legacy "car_" prefix sync sees them too.
        const std::string renderName = prefix + name;
        if (!m_vulkanRenderer->loadMeshFromFile(renderName, baked + "/" + name + ".nmsh",
                                                useMaterial, textureDir))
            continue;
        loaded.push_back(renderName);
    }
    return loaded;
}

void SimulationLoop::setCarPaint(float r, float g, float b)
{
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    const float want[3] = {clamp01(r), clamp01(g), clamp01(b)};
    if (want[0] == m_carPaint[0] && want[1] == m_carPaint[1] &&
        want[2] == m_carPaint[2])
        return; // same paint: no rebuild
    for (int i = 0; i < 3; ++i) m_carPaint[i] = want[i];
    std::printf("[paint] body tint %.3f %.3f %.3f\n", m_carPaint[0], m_carPaint[1],
                m_carPaint[2]);
    // The material block is static per mesh, so the body has to be re-uploaded.
    if (m_carLoaded && !m_carDir.empty()) ensureCarVisual(m_carDir);
}

void SimulationLoop::ensureCarVisual(const std::string& carDir) {
    if (!m_vulkanRenderer) return;
    despawnCarVisuals();
    despawnAiVisuals(); // AI share this car's bake + appearance (2.8/2.9)
    if (!carDir.empty()) m_carDir = carDir;
    const std::string baked = ks::sim::findBakedManifestDir(carDir);
    // Roadmap 2.4: car bakes pair with materials.txt exactly like track
    // bakes, so the car reaches the frame painted instead of vertex-white.
    bool appliedMaterials = false;
    std::string liveryLabel;
    if (!baked.empty()) {
        // Roadmap 2.8/2.9: texture override — the selected upgrade livery
        // first (explicit intent), then the team slot's skin, else the bake's
        // own textures. A folder that does not exist falls through: base
        // content is the fallback.
        std::string textureDir = baked + "/textures";
        auto carRelative = [this](std::string p) {
            if (p.size() > 1 && (p[0] == '/' || (p.size() > 2 && p[1] == ':'))) return p;
            if (!m_carDir.empty()) {
                if (m_carDir.back() == '/' || m_carDir.back() == '\\') return m_carDir + p;
                return m_carDir + "/" + p;
            }
            return p;
        };
        if (m_upgradeRt && m_upgradeRt->appearance.hasLivery() &&
            !m_upgradeRt->appearance.livery.folder.empty()) {
            const std::string cand = carRelative(m_upgradeRt->appearance.livery.folder);
            if (std::filesystem::is_directory(cand)) {
                textureDir = cand;
                liveryLabel = m_upgradeRt->appearance.livery.id;
            } else {
                std::fprintf(stderr, "[scene] upgrade livery folder missing (%s) -> base\n",
                             cand.c_str());
            }
        }
        if (textureDir == baked + "/textures") {
            if (const TeamCarSlot* p = playerSlot(m_team); p && !p->liveryId.empty()) {
                const std::string cand = m_carDir + "/liveries/" + p->liveryId;
                if (std::filesystem::is_directory(cand)) {
                    textureDir = cand;
                    liveryLabel = p->liveryId;
                } else {
                    std::fprintf(stderr,
                                 "[scene] team livery '%s' missing (%s) -> base\n",
                                 p->liveryId.c_str(), cand.c_str());
                }
            }
        }

        const std::vector<std::string> meshes =
            loadCarMeshSet(baked, "car_", textureDir, &appliedMaterials);
        for (const auto& renderName : meshes) {
            const ks::ecs::Entity e = scene().create();
            if (e == ks::ecs::kNullEntity) break;
            scene().emplace<ks::ecs::Name>(e, ks::ecs::Name{renderName});
            scene().emplace<ks::ecs::Transform>(e);
            scene().emplace<ks::ecs::MeshInstance>(e, ks::ecs::MeshInstance{renderName});
            m_carVisualEntities.push_back(e);
        }
    }
    // Roadmap 2.8/2.9: a running session's AI wear this bake + appearance too
    // (re-selection mid-session respawns them with the new nodes/livery).
    if (m_running && !m_aiCarIds.empty()) spawnSessionAiVisuals();
    if (!m_carVisualEntities.empty()) {
        std::string suffix = appliedMaterials ? " (materials.txt applied)" : "";
        if (!liveryLabel.empty()) suffix += " (livery: " + liveryLabel + ")";
        std::fprintf(stderr, "[scene] car visuals: %zu baked mesh(es) from %s%s\n",
                     m_carVisualEntities.size(), baked.c_str(), suffix.c_str());
        return;
    }
    // No bake, but the car's own .kn5 is right there: read it in memory, materials
    // included, so the showroom shows the car instead of a box.
    // A bake folder may exist and still yield no mesh; what matters is whether any
    // car visual was actually created.
    if (m_carVisualEntities.empty() && loadCarMeshesFromKn5(m_carDir)) return;
    // Solid placeholder box (ROADMAP 1.2 "placeholder solido"): ~4.4x1.8 m
    // body spanning y -0.40..1.00 in vehicle-local space, so the car is
    // visible even when no visual asset exists.
    const ks::sim::SolidBox box = ks::sim::makeSolidBox(0.90f, -0.40f, 1.00f, -2.20f, 2.20f);
    NativeMesh nm;
    static_assert(sizeof(ks::sim::SolidBoxVertex) == sizeof(NativeVertex),
                  "SolidBoxVertex must be memcpy-compatible with NativeVertex");
    nm.vertices.resize(box.vertices.size());
    std::memcpy(nm.vertices.data(), box.vertices.data(),
                box.vertices.size() * sizeof(ks::sim::SolidBoxVertex));
    nm.indices = box.indices;
    m_vulkanRenderer->setMesh("car_placeholder", nm);
    const ks::ecs::Entity e = scene().create();
    if (e != ks::ecs::kNullEntity) {
        scene().emplace<ks::ecs::Name>(e, ks::ecs::Name{"car_placeholder"});
        scene().emplace<ks::ecs::Transform>(e);
        scene().emplace<ks::ecs::MeshInstance>(e, ks::ecs::MeshInstance{"car_placeholder"});
        m_carVisualEntities.push_back(e);
    }
    std::fprintf(stderr, "[scene] car visual: solid placeholder box\n");
}

// Roadmap 2.9 — upgrades.ini -> package list for the loaded car. A missing
// or unparsable file falls back to the built-in stock packages: level 0
// everywhere carries zero deltas, so physics, nodes, livery and audio all
// resolve to the base content.
void SimulationLoop::loadUpgradesForCar(const std::string& carDir) {
    auto rt = std::make_unique<UpgradeRuntime>();
    const bool fromIni = rt->system.loadFromIni(carDir + "/upgrades.ini");
    if (!fromIni) rt->system.loadDefaults();
    m_upgradeRt = std::move(rt);

    // Selection override for headless/CI runs: "Engine:stage2,Livery:...".
    // The menu rows (cycleUpgradeRow) drive the same path interactively.
    if (const char* sel = std::getenv("KS_UPGRADES"); sel && sel[0])
        applyUpgradeSelectionString(sel);

    // Race _rd*.ini next to the kn5 + the selected upgrade levels, priority
    // upgrades > race > defaults (round 1: no race-round context yet).
    m_upgradeRt->appearance = ks::vehicle::resolveAppearance(
        carDir + "/" + m_carName + ".kn5", 1, &m_upgradeRt->system);
#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        // Baseline = the car AFTER its own data inis (the tuned values, not
        // the hardcoded defaults), captured once per car load so every later
        // re-selection recomputes from absolutes instead of stacking.
        m_upgradeRt->basePhysics = {m_vehicle->mass(), m_vehicle->enginePowerKw(),
                                    m_vehicle->maxRpm(), m_vehicle->dragCoeff(),
                                    m_vehicle->frontalArea()};
        m_upgradeRt->baseAero = m_vehicle->aero().getConfig();
    }
#endif
    std::fprintf(stderr, "[upgrade] %s: %zu package(s) from %s\n", m_carName.c_str(),
                 m_upgradeRt->system.types().size(),
                 fromIni ? "upgrades.ini" : "built-in defaults");
}

// Re-resolves the appearance after a selection change and re-applies every
// domain: physics, render nodes, livery texture and the sound pack.
void SimulationLoop::refreshUpgradeApplication() {
    if (!m_upgradeRt || m_carDir.empty()) return;
    m_upgradeRt->appearance = ks::vehicle::resolveAppearance(
        m_carDir + "/" + m_carName + ".kn5", 1, &m_upgradeRt->system);
    applyUpgradePhysics();
    if (m_carLoaded) ensureCarVisual(m_carDir); // nodes + livery (+ AI respawn)
    applyUpgradeAudio();
}

void SimulationLoop::applyUpgradePhysics() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle || !m_upgradeRt) return;
    // applyUpgradesToVehicleSimulator reads the CURRENT aero config, so the
    // captured baseline goes back first — otherwise every re-selection would
    // stack lift/mults on the previous result.
    m_vehicle->aero().setConfig(m_upgradeRt->baseAero);
    ks::vehicle::applyUpgradesToVehicleSimulator(*m_vehicle, m_upgradeRt->basePhysics,
                                                 m_upgradeRt->appearance.physics);
    std::fprintf(stderr, "[upgrade] physics: %.1f kW, %.0f kg, cd %.3f, cl %.3f\n",
                 m_vehicle->enginePowerKw(), m_vehicle->mass(), m_vehicle->dragCoeff(),
                 m_vehicle->aero().getConfig().liftCoefficient);
#endif
}

void SimulationLoop::applyUpgradeAudio() {
    if (!m_audio || !m_upgradeRt) return;
    if (!m_upgradeRt->appearance.hasSound()) return;
    // false = bank/sounds.ini missing on disk: the baseline audio loaded by
    // loadCarAudio() stays untouched (base-content fallback).
    if (ks::sim::applyAppearanceAudio(*m_audio, m_upgradeRt->appearance, m_carDir))
        std::fprintf(stderr, "[upgrade] sound pack applied (%s)\n",
                     m_upgradeRt->appearance.sound.id.empty()
                         ? "-" : m_upgradeRt->appearance.sound.id.c_str());
    else
        std::fprintf(stderr, "[upgrade] sound pack unavailable -> baseline audio\n");
}

// "Engine:stage2,Livery:sponsor_red,Sound:race_exhaust" — category names are
// matched the same way the ini parser maps UpgradeType= lines.
void SimulationLoop::applyUpgradeSelectionString(const char* spec) {
    if (!m_upgradeRt || !spec) return;
    using ks::vehicle::UpgradeCategory;
    auto catOf = [](std::string low) -> int {
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (low.find("engine") != std::string::npos) return static_cast<int>(UpgradeCategory::Engine);
        if (low.find("aero") != std::string::npos || low.find("wing") != std::string::npos)
            return static_cast<int>(UpgradeCategory::Aero);
        if (low.find("trans") != std::string::npos || low.find("gear") != std::string::npos)
            return static_cast<int>(UpgradeCategory::Transmission);
        if (low.find("susp") != std::string::npos) return static_cast<int>(UpgradeCategory::Suspension);
        if (low.find("brake") != std::string::npos) return static_cast<int>(UpgradeCategory::Brakes);
        if (low.find("liver") != std::string::npos || low.find("skin") != std::string::npos)
            return static_cast<int>(UpgradeCategory::Livery);
        if (low.find("sound") != std::string::npos || low.find("audio") != std::string::npos)
            return static_cast<int>(UpgradeCategory::Sound);
        if (low.find("body") != std::string::npos) return static_cast<int>(UpgradeCategory::Body);
        return -1;
    };
    const std::string s = spec;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        const std::string tok =
            s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = (comma == std::string::npos) ? s.size() + 1 : comma + 1;
        if (tok.empty()) continue;
        const size_t colon = tok.find(':');
        if (colon == std::string::npos || colon == 0) {
            std::fprintf(stderr, "[upgrade] KS_UPGRADES: bad token '%s' (want Cat:id)\n",
                         tok.c_str());
            continue;
        }
        const std::string cat = tok.substr(0, colon);
        const std::string id = tok.substr(colon + 1);
        const int c = catOf(cat);
        if (c < 0) {
            std::fprintf(stderr, "[upgrade] KS_UPGRADES: unknown category '%s'\n", cat.c_str());
            continue;
        }
        if (m_upgradeRt->system.selectById(static_cast<UpgradeCategory>(c), id))
            std::fprintf(stderr, "[upgrade] selected %s:%s\n", cat.c_str(), id.c_str());
        else
            std::fprintf(stderr, "[upgrade] no level '%s' in category %s\n", id.c_str(),
                         cat.c_str());
    }
}

// Garage menu rows (roadmap 2.9): one label per package of the car's
// upgrades.ini, showing the currently selected level.
std::vector<std::string> SimulationLoop::upgradeRowLabels() const {
    std::vector<std::string> out;
    if (!m_upgradeRt) return out;
    for (const auto& t : m_upgradeRt->system.types()) {
        const auto* L = t.current();
        std::string label = t.name + "  ";
        label += L ? L->name : "-";
        out.push_back(std::move(label));
    }
    return out;
}

// Cycling a row selects the next level of that package and re-applies
// everything (physics, nodes, livery, sound) through refreshUpgradeApplication.
void SimulationLoop::cycleUpgradeRow(int row) {
    if (!m_upgradeRt || row < 0 ||
        row >= static_cast<int>(m_upgradeRt->system.types().size()))
        return;
    auto& t = m_upgradeRt->system.types()[static_cast<size_t>(row)];
    if (t.levels.empty()) return;
    const int n = static_cast<int>(t.levels.size());
    const int next = ((t.selected % n) + 1) % n;
    if (!m_upgradeRt->system.select(t.category, next)) return;
    std::fprintf(stderr, "[upgrade] %s -> %s\n", t.name.c_str(),
                 t.levels[static_cast<size_t>(next)].name.c_str());
    refreshUpgradeApplication();
}

// Multiplayer (roadmap 3.1). All three bodies are no-ops unless the build
// carries HAS_KSNET and the transport is actually running.
void SimulationLoop::broadcastLocalCarState() {
#if HAS_KSNET
    if (!m_network || !m_network->isHosting() || !m_multiCar) return;
    net::NetworkServer* srv = m_network->server();
    // With only the host's own loopback client there is nobody to send to;
    // the host's own entry is mirrored back through that client anyway.
    if (!srv || m_network->clientCount() <= 1) return;
    net::CarStateData wire{};
    for (const auto& entry : m_multiCar->cars()) {
        if (!entry || !entry->isActive || !entry->vehicle) continue;
        const auto s = entry->vehicle->getState();
        wire.carId = static_cast<uint32_t>(entry->id);
        wire.posX = static_cast<float>(s.position.x);
        wire.posY = static_cast<float>(s.position.y);
        wire.posZ = static_cast<float>(s.position.z);
        wire.rotX = static_cast<float>(s.rotation.x);
        wire.rotY = static_cast<float>(s.rotation.y);
        wire.rotZ = static_cast<float>(s.rotation.z);
        wire.velX = static_cast<float>(s.velocity.x);
        wire.velY = static_cast<float>(s.velocity.y);
        wire.velZ = static_cast<float>(s.velocity.z);
        wire.speed = static_cast<float>(s.speed);
        wire.rpm = static_cast<float>(s.rpm);
        wire.gear = s.gear;
        wire.throttle = s.throttle;
        wire.brake = s.brake;
        wire.steering = s.steering;
        srv->broadcastCarState(wire.carId, wire);
    }
#endif
}

void SimulationLoop::handleRemoteCarState(uint32_t carId, const net::CarStateData& state) {
#if HAS_KSNET
    if (!m_multiCar || carId == m_ownNetCarId) return;
    const auto it = m_remoteCarIds.find(carId);
    if (it == m_remoteCarIds.end()) return; // spawn message still in flight
    CarEntry* car = m_multiCar->getCar(it->second);
    if (!car || !car->vehicle) return;
    ks::physics::SimulationState& s = car->vehicle->state();
    s.position = {state.posX, state.posY, state.posZ};
    s.rotation = {state.rotX, state.rotY, state.rotZ};
    s.heading = state.rotY;
    s.velocity = {state.velX, state.velY, state.velZ};
    s.speed = state.speed;
    s.rpm = state.rpm;
    s.gear = state.gear;
    s.throttle = state.throttle;
    s.brake = state.brake;
    s.steering = state.steering;
#else
    (void)carId;
    (void)state;
#endif
}

void SimulationLoop::applyRemoteInput(int clientIndex, const net::InputData& input) {
#if HAS_KSNET
    if (clientIndex < 0) return;
    RemoteInput& r = m_remoteInputs[clientIndex];
    r.throttle = input.throttle;
    r.brake = input.brake;
    r.steering = input.steering;
#else
    (void)clientIndex;
    (void)input;
#endif
}

void SimulationLoop::paceFrame() {
    if (m_frameLimit <= 0) return;
#if defined(_WIN32)
    // Windows sleeps on the system timer tick (15.6 ms by default), which is
    // wider than a 30 fps budget and would land the frame a tick late: ask for
    // a 1 ms timer once, so sleeping out the budget stays accurate.
    static const bool timerRaised = [] { timeBeginPeriod(1); return true; }();
    (void)timerRaised;
#endif
    const double budget = 1.0 / static_cast<double>(m_frameLimit);
    const auto since = [this] {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_frameStart).count();
    };
    const double left = budget - since();
    if (left <= 0.0) return;
    // Sleep the bulk, then spin the last millisecond: the timer cannot place a
    // wake-up that precisely, and a frame limiter has to land on the limit.
    if (left > 0.0015)
        std::this_thread::sleep_for(std::chrono::duration<double>(left - 0.0012));
    while (since() < budget) std::this_thread::yield();
}

void SimulationLoop::tick() {
    m_frameStart = std::chrono::steady_clock::now();
    static bool profileInit = false;
    if (!profileInit) {
        profileInit = true;
        if (const char* e = std::getenv("KS_PROFILE"))
            ks::physics::PhysicsProfiler::instance().setEnabled(e[0] != '0');
    }

    // One frame pair per tick, in BOTH branches: the idle branch used to open
    // a frame it never closed and the running branch closed one that was last
    // opened by whoever ran before it, so the report's frame count/time mixed
    // tick pairs with per-car step pairs (found by the gate 2.14 run).
    PROFILE_FRAME();
    if (!m_running) {
        updateCamera(1.0f / 60.0f);
        publishSharedMemory();
        publishUdpTelemetry();
        publishTcpTelemetry();
        // The control API and LAN discovery keep answering while the session
        // is idle - otherwise a stopped sim could never be started again
        // through the external API.
        updateNetworkSync(1.0f / 60.0f);
        pumpFeatureHub(1.0f / 60.0f, false);
        render();
        PROFILE_END_FRAME();
        paceFrame();
        return;
    }
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_lastTime).count();
    m_lastTime = now;
    if (elapsed > 0.05) elapsed = 0.05;
    // Pause the world while a modal overlay is up (roadmap 3.1): the frame
    // still renders - otherwise the menu could never be drawn - but no
    // physics is stepped and no time is banked, so resume has no burst.
    const bool paused = m_ui.blocksDrivingInput();
    if (paused) m_simAccumulator = 0.0;
    else m_simAccumulator += elapsed;
    // AI speed authority (roadmap 3.5): the session flag limiter while the
    // race is on (yellow 0.6 / SC 0.5), hard 0 after the flag so the field
    // coasts to a stop instead of racing on through the checkered.
    if (m_multiCar)
        m_multiCar->setAISpeedFactor(
            m_raceSession.isActive() ? m_raceSession.aiSpeedFactor() : 0.0f);
    // Scoped block on purpose: the section must end with the fixed-step loop
    // below it, not at function exit (which would swallow FeatureHub, FFB,
    // weather, telemetry publish and render into "physics" — gate 2.14
    // needs the physics number to be the physics step).
    {
        PROFILE_SECTION("physics");
        while (!paused && m_simAccumulator >= m_physicsDt) {
            applyInput();
            // Roadmap 2.7: garage exit / pit entry runs before the physics step
            // so its hold, snap and pit limiter win over the raw driver input
            // for this step (updatePitRepair runs after it, on the same phase).
            updateGarageExit(static_cast<float>(m_physicsDt));
#if HAS_VEHICLE_SIM
            if (m_vehicle) m_vehicle->updatePhysics(m_physicsDt);
#endif
            // Roadmap 2.7: box service at the physics rate, so the control hold
            // (PitLaneRepair::holdCar) wins over the driver's next input instead
            // of being overwritten by applyInput() in the same step.
            updatePitRepair(static_cast<float>(m_physicsDt));
            // Trackside marshals watch the same field on the same clock: an
            // incident raised by this step is flagged before the next frame.
            updateTracksidePosts(static_cast<float>(m_physicsDt));
            // AI cars freeze on the grid during the countdown and race once it
            // is over; physics runs at the same fixed 1 kHz as the player's.
            if (m_multiCar && !m_raceSession.isCountingDown())
                m_multiCar->update(m_physicsDt);
            if (m_sessionPhase == PHASE_GREEN_FLAG) updateLapAndSurface(m_physicsDt);
            m_simTime += m_physicsDt;
            m_simAccumulator -= m_physicsDt;
            // Parity hooks (SimulationLoop_ParityHooks.h): ksnet / CarStateSync
            // pump, stamped with the physics clock.
            m_simTimeSec = m_simTime;
            updateNetworkSync(static_cast<float>(m_physicsDt));
        }
    } // PROFILE_SECTION("physics") scope ends with the step loop
    // FeatureHub is pumped once per frame (not per physics step): discovery,
    // the external control API's command poll and weather keep running even
    // when a modal overlay pauses the world, while track limits only see the
    // session while physics actually steps.
    pumpFeatureHub(static_cast<float>(elapsed), !paused);
    Engine::instance().tick(elapsed);
#if HAS_FFB
    if (m_ffbEnabled && m_vehicle && !m_ui.blocksDrivingInput()) {
        const auto& samp = m_vehicle->ffbSample();
        ks::device::FFBInputs in;
        in.slipAngleFL = samp.slipAngleFL; in.slipAngleFR = samp.slipAngleFR;
        in.loadFL = samp.loadFL; in.loadFR = samp.loadFR;
        in.camberFL = samp.camberFL; in.camberFR = samp.camberFR;
        in.speedMs = samp.speedMs; in.steerAngle = samp.steerAngle;
        float torqueNm = ks::device::FFBBridge::computeSteeringTorque(in, &m_vehicle->tires(), &m_vehicle->tires());
        if (m_ffb) m_ffb->updateFFB(torqueNm);
    }
#endif
    // Weather evolution freezes while a modal overlay pauses the world.
    updateWeather(paused ? 0.0f : static_cast<float>(elapsed));
    publishSharedMemory();
    publishUdpTelemetry();
    publishTcpTelemetry();
    ks::physics::SimulationState mgrState{};
    mgrState.currentLapDistance =
        static_cast<float>(m_normalizedSpline * std::max(100.0f, m_trackData.splineLength));
    m_raceSession.update(mgrState, paused ? 0.0f : static_cast<float>(elapsed));
    // AI standings feed (roadmap 3.5): each AI car reports laps + spline
    // distance into the race order; completed laps also reach mods as
    // "ai_lap" (carIndex, lap) alongside the player's "lap" event.
    if (m_raceSession.isActive() && m_multiCar) {
        for (size_t i = 0; i < m_aiCarIds.size(); ++i) {
            CarEntry* car = m_multiCar->getCar(m_aiCarIds[i]);
            if (!car || !car->ai || !car->ai->isReady()) continue;
            const int carIndex = static_cast<int>(i) + 1; // 0 = player
            const int lap = car->ai->lapCount();
            m_raceSession.updateCarProgress(
                carIndex, lap,
                lap * car->ai->splineLength() + car->ai->progressDistance());
            if (i < m_aiLastLaps.size() && lap > m_aiLastLaps[i]) {
                ks::scripting::modsdk::dispatch("ai_lap",
                                                {double(carIndex), double(lap)});
                m_aiLastLaps[i] = lap;
            }
        }
    }
    if (m_raceSession.isCountingDown())
        m_timeRemaining = m_raceSession.countdownValue();
    else if (m_raceSession.isActive())
        m_timeRemaining = m_raceSession.remainingTime();
    if (m_sessionPhase != PHASE_CHECKERED_FLAG && onSessionStateChanged)
        onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
#if HAS_VEHICLE_SIM
    if (m_goldenExportActive && m_sessionPhase == PHASE_GREEN_FLAG && m_vehicle) {
        m_goldenTime += elapsed;
        m_goldenSampleAccum += elapsed;
        if (m_goldenSampleAccum >= 0.02) {
            m_goldenSampleAccum = 0.0;
            const auto st = m_vehicle->getState();
            ks::physics::GoldenSample g;
            g.time = m_goldenTime;
            g.speedMs = static_cast<float>(st.speed);
            g.rpm = static_cast<float>(m_vehicle->rpm());
            g.x = st.position.x;
            g.y = st.position.y;
            g.z = st.position.z;
            g.throttle = st.throttle;
            g.brake = st.brake;
            g.steer = st.steering;
            m_goldenRecorder.pushRef(g);
        }
    }
#endif
    // Network HUD (roadmap 3.1): the chat strip only appears with a live
    // transport, and the F2 browser carries the same status line.
    if (m_network) {
        const bool up = m_network->isHosting() || m_network->isConnected();
        m_ui.chat().setVisible(up);
        char net[96];
        if (m_network->isHosting())
            std::snprintf(net, sizeof(net), "HOSTING  %d player(s)",
                          m_network->clientCount());
        else if (m_network->isConnected())
            std::snprintf(net, sizeof(net), "CONNECTED  rtt %d ms",
                          static_cast<int>(m_network->stats().rtt));
        else
            std::snprintf(net, sizeof(net), "OFFLINE");
        m_ui.multiplayer().setStatus(net);
        // Keep our own row's player count honest while hosting.
        const int hosting = m_network->isHosting() ? m_network->clientCount() : -1;
        if (hosting != m_mpServerClients) {
            m_mpServerClients = hosting;
            m_ui.multiplayer().setServers(
                {{"ksim local", "127.0.0.1", hosting < 0 ? 0 : hosting, 8, 0}});
        }
    }
    broadcastLocalCarState();
    if (m_network) m_network->update(elapsed);
#if HAS_VEHICLE_SIM
    // Brief P6 — brake glow: the hottest disc temperature drives the
    // renderer's emissive on brake-disc meshes (setBrakeGlow). Normalized
    // on the same 400..800 C anchor the physics brake-fade uses, so the
    // glow only ever shows what the thermal model computed — a cold disc
    // (or no vehicle at all) means signal 0, never a faked glow. Player
    // car only; AI cars reuse the same meshes and stay inert. No renderer
    // (headless/menu) or no vehicle: explicit 0 so a stale value can't
    // keep glowing after despawn.
    if (m_vulkanRenderer) {
        float glow = 0.0f;
        if (m_vehicle) {
            float maxDisc = 0.0f;
            for (int w = 0; w < 4; ++w)
                maxDisc = std::max(maxDisc, m_vehicle->brakes().discTemp(w));
            glow = std::clamp((maxDisc - 400.0f) / 400.0f, 0.0f, 1.0f);
        }
        m_vulkanRenderer->setBrakeGlow(glow);
    }
    // Roadmap 1.1: hand the mixer the same vehicle snapshot the HUD reads.
    // The per-wheel telemetry the mixer needs exists since the milliken
    // G-package: slip ratio/angle drive the skid layer, the hottest brake
    // disc scales the squeal layer, cosmetic damage feeds the bodywork
    // rattle. Boost stays zero (VehicleSimulator does not step the
    // EngineModel instance), so the turbo layer remains silent until a
    // real boost signal exists — signals are never faked.
    if (m_audio && m_vehicle) {
        const auto ast = m_vehicle->getState();
        const float wetness = m_weather.trackWetness;
        float slipRatio = 0.f, brakeTemp = 0.f;
        for (int w = 0; w < 4; ++w) {
            slipRatio = std::max(
                slipRatio, std::abs(static_cast<float>(m_vehicle->slipRatio(w))));
            brakeTemp = std::max(brakeTemp, m_vehicle->brakes().discTemp(w));
        }
        const auto& ffb = m_vehicle->ffbSample();
        const float slipAngle = std::max(
            {std::abs(ffb.slipAngleFL), std::abs(ffb.slipAngleFR),
             std::abs(ast.sideslipBeta)});
        const float bodyDamage = m_vehicle->damage().cosmeticDamage();
        // Surface: wet wins; otherwise a large grip deficit means the car
        // left the racing surface (TrackSurface has no per-position
        // material id, but tires already consume its mu as effectiveMu).
        SimulatorAudio::SurfaceType surf = SimulatorAudio::SurfaceType::Asphalt;
        if (wetness > 0.6f)
            surf = SimulatorAudio::SurfaceType::Wet;
        else if (ast.effectiveMu < 0.8f)
            surf = SimulatorAudio::SurfaceType::Grass;
        m_audio->updatePhysics(
            static_cast<float>(m_vehicle->rpm()), ast.throttle, ast.brake,
            static_cast<float>(ast.speed), ast.steering, m_vehicle->currentGear(),
            false, slipRatio, slipAngle, surf, bodyDamage, brakeTemp,
            0.0f, static_cast<float>(ast.speed), wetness,
            m_weather.rainIntensity, static_cast<float>(elapsed));
        // Roadmap 2.3 (P2.2): the listener rides the player's car —
        // position, world velocity (the doppler half) and heading — and
        // every other active car becomes an OtherCarVoice the mixer
        // attenuates, pans and pitch-shifts (Audio3D.h).
        m_audio->setListenerPosition(ast.position.x, ast.position.y + 0.5f,
                                     ast.position.z);
        if (ast.speed > 1.0f) {
            const auto& v = ast.velocity;
            const float vl = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (vl > 1e-3f)
                m_audio->setListenerOrientation(v.x / vl, v.y / vl, v.z / vl,
                                                0.f, 1.f, 0.f);
            m_audio->setListenerVelocity(v.x, v.y, v.z);
        } else {
            m_audio->setListenerVelocity(0.f, 0.f, 0.f);
        }
        if (m_multiCar) {
            ks::sim::audio3d::OtherCarVoice voices[SimulatorAudio::kMaxOtherCars];
            int n = 0;
            for (const auto& entry : m_multiCar->cars()) {
                if (n >= SimulatorAudio::kMaxOtherCars) break;
                if (!entry || !entry->isActive || entry->isPlayer || !entry->vehicle)
                    continue;
                const auto cs = entry->vehicle->getState();
                auto& voice = voices[n++];
                voice.px = cs.position.x;
                voice.py = cs.position.y;
                voice.pz = cs.position.z;
                voice.vx = cs.velocity.x;
                voice.vy = cs.velocity.y;
                voice.vz = cs.velocity.z;
                voice.rpm = static_cast<float>(entry->vehicle->rpm());
                voice.gain = 1.0f;
            }
            m_audio->setOtherCarVoices(voices, n);
        }
    } else if (m_audio) {
        // No player car (menu/teardown): drop the feed so a stale voice
        // from the last session keeps ringing in the ears.
        m_audio->setOtherCarVoices(nullptr, 0);
    }
#endif
    {
        PROFILE_SECTION("render");
        updateCamera(static_cast<float>(elapsed));
        render(static_cast<float>(elapsed));
    }
    PROFILE_END_FRAME();
    paceFrame();
}

namespace {

float envFloat(const char* name, float fallback) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') return fallback;
    char* end = nullptr;
    const float parsed = std::strtof(raw, &end);
    return (end != nullptr && *end == '\0') ? parsed : fallback;
}

} // namespace

// Roadmap P2 (KS_TERRAIN=1): ground for the world outside the KN5. Built
// once from a generated heightmap - or, with KS_TERRAIN_PATH=<png>, from an
// edited one (the grayscale PNG TrackTerrainEditor saves) - pressed down
// below the baked track near the origin when generated, tinted per vertex
// (the GBuffer takes albedo from vertex colour and there is no terrain
// texture to bind), then registered as an ECS MeshInstance so it rides the
// same draw list, shadow pass and frustum test as every other mesh. With
// the flag unset nothing is generated and no mesh is created, so the default
// image is untouched.
void SimulationLoop::initTracksideTerrain() {
    if (m_terrainInit) return;
    m_terrainInit = true;
    const char* flag = std::getenv("KS_TERRAIN");
    m_terrainWanted = flag && *flag && std::atoi(flag) != 0;
    if (!m_terrainWanted || !m_vulkanRenderer) return;

    namespace terrain = ks::engine::terrain;
    constexpr float kWorld = 1600.0f;
    constexpr const char* kTerrainMeshName = "trackside_terrain";
    constexpr int kMaxGrid = 257; // above this the mesh is strided down (LOD)
    const float half = kWorld * 0.5f;

    int gridW = 129;
    int gridH = 129;
    std::vector<float> heights;
    bool imported = false;

    // An edited heightmap wins over the procedural one: TrackTerrainEditor
    // saves exactly this format, so KS_TERRAIN_PATH=<png> is the hand-off
    // from the Qt terrain tool to the Qt-free simulator. KS_TERRAIN_MIN_H /
    // KS_TERRAIN_MAX_H say what black and white mean in metres.
    if (const char* path = std::getenv("KS_TERRAIN_PATH"); path != nullptr && *path != '\0') {
        ks::engine::fileformat::GrayImage img;
        std::string err;
        if (!ks::engine::fileformat::loadPngGrayFile(path, img, &err)) {
            std::fprintf(stderr, "[terrain] KS_TERRAIN_PATH %s: %s - falling back to noise\n",
                         path, err.c_str());
        } else {
            const float minH = envFloat("KS_TERRAIN_MIN_H", -2.0f);
            const float maxH = envFloat("KS_TERRAIN_MAX_H", 48.0f);
            gridW = img.width;
            gridH = img.height;
            heights.resize(img.pixels.size());
            const float span = maxH - minH;
            for (std::size_t i = 0; i < img.pixels.size(); ++i)
                heights[i] = minH + span * (static_cast<float>(img.pixels[i]) / 65535.0f);
            imported = true;
            std::fprintf(stderr, "[terrain] %s: %dx%d heightmap, %.1f..%.1f m\n", path, gridW,
                         gridH, minH, maxH);
        }
    }

    if (!imported) {
        terrain::FbmHeightmapParams params;
        params.seed = 20251004u;
        params.baseHeight = 7.0f;
        params.amplitude = 26.0f;
        params.frequency = 4.0f;
        params.octaves = 5;
        gridW = gridH = 129;
        heights = terrain::generateFbmHeightmap(gridW, gridH, params);
        if (heights.empty()) return;

        // Press the ground down under the baked track (the KN5 sits around
        // the origin at y = 0) and let it rise into hills further out. An
        // imported heightmap deliberately skips this: it was authored to fit
        // its own track.
        for (int z = 0; z < gridH; ++z) {
            for (int x = 0; x < gridW; ++x) {
                const float wx = (static_cast<float>(x) / static_cast<float>(gridW - 1)) * kWorld - half;
                const float wz = (static_cast<float>(z) / static_cast<float>(gridH - 1)) * kWorld - half;
                const float d = std::sqrt(wx * wx + wz * wz);
                float t = (d - 200.0f) / 400.0f;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                t = t * t * (3.0f - 2.0f * t);
                float& h = heights[static_cast<std::size_t>(z) * static_cast<std::size_t>(gridW) + static_cast<std::size_t>(x)];
                h = h * t - 0.8f * (1.0f - t);
            }
        }
    }

    int stride = 1;
    const int largest = std::max(gridW, gridH);
    if (largest > kMaxGrid)
        stride = (largest - 1 + (kMaxGrid - 2)) / (kMaxGrid - 1);
    terrain::TerrainMeshData data =
        stride > 1 ? terrain::generateTerrainMeshLOD(heights, gridW, gridH, kWorld, kWorld, stride, 30.0f)
                   : terrain::generateTerrainMesh(heights, gridW, gridH, kWorld, kWorld, 30.0f);
    if (data.vertices.empty()) return;

    NativeMesh mesh;
    mesh.vertices.reserve(data.vertices.size());
    for (terrain::TerrainVertex& v : data.vertices) {
        v.px -= half;
        v.pz -= half;
        const float rock = std::clamp((1.0f - v.ny) * 5.0f, 0.0f, 1.0f);
        const float snow = std::clamp((v.py - 26.0f) / 10.0f, 0.0f, 1.0f);
        float r = (1.0f - rock) * 0.31f + rock * 0.44f;
        float g = (1.0f - rock) * 0.40f + rock * 0.41f;
        float b = (1.0f - rock) * 0.22f + rock * 0.37f;
        r = (1.0f - snow) * r + snow * 0.72f;
        g = (1.0f - snow) * g + snow * 0.73f;
        b = (1.0f - snow) * b + snow * 0.75f;
        NativeVertex nv;
        nv.px = v.px; nv.py = v.py; nv.pz = v.pz;
        nv.nx = v.nx; nv.ny = v.ny; nv.nz = v.nz;
        nv.u = v.u; nv.v = v.v;
        nv.r = r; nv.g = g; nv.b = b; nv.a = 1.0f;
        mesh.vertices.push_back(nv);
    }
    mesh.indices = std::move(data.indices);
    m_vulkanRenderer->setMesh(kTerrainMeshName, mesh);

    const ks::ecs::Entity e = scene().create();
    if (e != ks::ecs::kNullEntity) {
        scene().emplace<ks::ecs::Name>(e, ks::ecs::Name{kTerrainMeshName});
        scene().emplace<ks::ecs::Transform>(e);
        scene().emplace<ks::ecs::MeshInstance>(e, ks::ecs::MeshInstance{kTerrainMeshName});
    }
    std::fprintf(stderr, "[terrain] KS_TERRAIN=1: %zu vertices of ground over %.0f m\n",
                 mesh.vertices.size(), kWorld);
}

// Roadmap P1 (KS_PARTICLES=1): one dust emitter riding the car, stepped with
// the frame's real dt and handed to the renderer as flat quads. Everything
// here is behind the env flag — with it unset nothing is created, no emitter
// exists and not a single float reaches setParticleVertices(), so the
// default image is untouched.
void SimulationLoop::updateAndDrawParticles(float dt) {
    using ks::engine::graphics::ParticleEmitter;
    using ks::engine::graphics::ParticleSystem;

    if (!m_particlesInit) {
        m_particlesInit = true;
        // The config owns the switch (video.particles); KS_PARTICLES=1 still
        // forces the emitters on for a capture, whatever the file says.
        const char* flag = std::getenv("KS_PARTICLES");
        const bool envForced = flag && *flag && std::atoi(flag) != 0;
        m_particlesWanted = envForced || m_particlesConfig;
        if (m_particlesWanted) {
            m_particles.setSeed(20251004u);
            ParticleEmitter dust;
            dust.position = {0.0f, 0.2f, 0.0f};   // re-anchored to the car below
            dust.direction = {0.0f, 1.0f, 0.0f};  // kicked up, not backwards
            dust.spreadDeg = 45.0f;
            dust.spawnRate = 90.0f;
            dust.lifetime = 1.6f;
            dust.speed = 2.2f;
            dust.speedJitter = 1.1f;
            dust.size = 0.30f;
            dust.sizeJitter = 0.15f;
            dust.color = {0.62f, 0.57f, 0.48f};   // road dust, not smoke
            dust.alpha = 0.55f;
            dust.gravity = {0.0f, -1.6f, 0.0f};   // hangs, then settles
            dust.drag = 1.4f;
            m_particles.addEmitter(dust);
            // Roadmap 2.2 (P2.8): rain column + wheel spray, driven per
            // frame from the weather state below. Same KS_PARTICLES gate as
            // the dust emitter — with the flag unset no emitter exists, so
            // the default image is untouched.
            m_particles.addEmitter(rainfx::makeRainEmitter());   // index 1
            m_particles.addEmitter(rainfx::makeSprayEmitter());  // index 2
            std::fprintf(stderr,
                         "[particles] emitters on (config %d, KS_PARTICLES %d): "
                         "dust + rain + spray up to %d sprites\n",
                         m_particlesConfig ? 1 : 0, envForced ? 1 : 0,
                         ParticleSystem::kMaxParticles);
        }
    }
    if (!m_particlesWanted || !m_vulkanRenderer) return;

#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        const auto st = m_vehicle->getState();
        ks::engine::graphics::ParticleEmitter& em = m_particles.emitter(0);
        em.position = {st.position.x, st.position.y + 0.15f, st.position.z};
        // Dust is kicked up by rolling tyres: stationary, there is none.
        em.enabled = std::fabs(st.speed) > 2.0f;
        // Roadmap 2.2: rain follows the car from above, spray only comes off
        // a wet track while the car is moving (both rules unit-tested in
        // rain_effects_test). m_weather evolves since Roadmap 2.1, so a
        // drying track turns the spray off by itself.
        const ks::math::vec3 carPos{st.position.x, st.position.y, st.position.z};
        rainfx::driveRainEmitter(m_particles.emitter(1), carPos,
                                 m_weather.rainIntensity);
        rainfx::driveSprayEmitter(m_particles.emitter(2), carPos,
                                  m_weather.trackWetness,
                                  static_cast<float>(st.speed));
    }
#endif

    m_particles.update(dt);

    static constexpr std::size_t kMaxFloats =
        static_cast<std::size_t>(ParticleSystem::kMaxParticles) *
        ParticleSystem::kVerticesPerParticle * ParticleSystem::kFloatsPerVertex;
    m_particleQuads.resize(kMaxFloats);
    const std::size_t floats = m_particles.buildQuads(m_particleQuads.data(), m_particleQuads.size());
    m_vulkanRenderer->setParticleVertices(floats ? m_particleQuads.data() : nullptr, floats);
}

} // namespace ks::sim
