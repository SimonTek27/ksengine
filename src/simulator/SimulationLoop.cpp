/**
 * SimulationLoop.cpp — std-only / Qt-free
 * 1 kHz physics + LapSectorTimer + SM + UDP + TCP telemetry
 */

#include "SimulationLoop.h"
#include "InputManager.h"
#include "MultiCarManager.h"
#include "NetworkManager.h"
#include "SetupGarage.h"
#include "SetupFile.h"
#include "SceneAssets.h"
#include "RainEffects.h"
#include "SimulatorAudio.h"
#include "engine/physics/DamageTelemetry.h"
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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
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
    fs::path surf = fs::path(trackDirectory) / "data" / "surfaces.ini";
    if (!fs::exists(surf)) surf = fs::path(trackDirectory) / "surfaces.ini";
    if (fs::exists(surf)) { ks::ac::AcSurfacesLoader loader; loader.load(surf.string()); }
    ks::physics::TrackSurface::instance().syncFromWeather(m_weather.trackWetness, m_weather.trackTemp);
    std::string kn5;
    for (auto& e : fs::directory_iterator(trackDirectory))
        if (e.path().extension() == ".kn5") { kn5 = e.path().string(); break; }
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
    m_carLoaded = true;
    m_ui.menu().setCarName(m_carName);
    if (m_shm) m_shm->invalidateStatic();
    // Roadmap 1.2: visual for the freshly loaded car (baked meshes or
    // solid box placeholder). Never fails the physics load.
    ensureCarVisual(carDir);
    // Best effort: a missing audio device must not fail car load.
    loadCarAudio(carDir);
    return true;
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

void SimulationLoop::beginRaceSession() {
    // AI field (roadmap 3.5): respawn on every session so a reset re-staggers
    // everyone behind the line. Only our own ids are removed — cars spawned
    // by the multiplayer transport stay in the manager.
    if (m_multiCar) {
        for (int id : m_aiCarIds) m_multiCar->removeCar(id);
        m_aiCarIds.clear();
        m_aiLastLaps.clear();
        if (m_aiCarCount > 0 && !m_trackData.directory.empty()) {
            m_aiCarIds = m_multiCar->spawnGrid(
                m_aiCarCount, m_carName.empty() ? "ks_car" : m_carName, "AI ");
            m_aiLastLaps.assign(m_aiCarIds.size(), 0);
            std::fprintf(stderr, "SimulationLoop: spawned %d AI cars\n",
                         static_cast<int>(m_aiCarIds.size()));
        }
    }
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
    rc.numCars = 1 + static_cast<int>(m_aiCarIds.size());
    m_raceSession.configure(rc);
    m_raceSession.setPlayerCarIndex(0);
    m_raceSession.startSession();
    if (rc.sessionType == RaceConfig::SessionType::Race) {
        m_raceSession.startCountdown(5.0f);
        m_sessionPhase = PHASE_COUNTDOWN;
        m_timeRemaining = 5.0;
    } else {
        // Open session: straight to green (onCountdownFinished never fires,
        // so set the phase here — lap timing was armed by start()).
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
    if (m_running) stop(); // picking a mode from the menu starts a fresh session
    // Configure BEFORE start(): beginRaceSession() reads the session byte
    // (countdown + RaceSessionManager type), the lap limit and the AI count.
    m_sessionType = toNetSessionType(mode);
    const SessionStartParams p = defaultsForMode(mode);
    // 0 = open session (RaceSessionManager only finishes on totalLaps > 0).
    m_totalLaps = p.totalLaps;
    if (mode == GameSessionMode::Race) {
        // QUICK RACE promises a grid: keep an explicit KS_AI_CARS value,
        // otherwise default to 5 opponents.
        if (m_aiCarCount == 0) setAiCarCount(5);
    } else {
        // Practice / time attack are solo by definition.
        setAiCarCount(0);
    }
    std::fprintf(stderr, "SimulationLoop: menu session %s (%d laps, %d AI)\n",
                 sessionModeName(mode), m_totalLaps, m_aiCarCount);
    start();
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

void SimulationLoop::updateWeather(float dt) {
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
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    m_ui.telemetry().update(st.speed, m_vehicle->rpm(), st.throttle, st.brake, st.steering, 0.f, 0.f);
    m_ui.dashboard().update(st.speed, m_vehicle->rpm(), m_vehicle->currentGear(), st.throttle, st.brake,
        m_lapTimer.completedLaps(), m_lapTimer.currentTimeMs() * 0.001f, m_lapTimer.bestTimeMs() * 0.001f, 0.f, 1, 1);
    ui::RaceHudSample s;
    s.speedMs = static_cast<float>(st.speed); s.rpm = static_cast<float>(m_vehicle->rpm()); s.maxRpm = 8500.f;
    s.gear = m_vehicle->currentGear(); s.throttle = static_cast<float>(st.throttle);
    s.brake = static_cast<float>(st.brake); s.steer = static_cast<float>(st.steering);
    s.latG = st.acceleration.x / 9.81f; s.lonG = st.acceleration.z / 9.81f; s.fuelL = static_cast<float>(st.fuel);
    for (int i = 0; i < 4; ++i) { s.tyreTemp[i] = static_cast<float>(st.tyreTemp[i]); s.tyreWear[i] = static_cast<float>(st.tyreWear[i]); }
    s.position = 1; s.totalCars = 1; s.lap = m_lapTimer.completedLaps() + 1; s.totalLaps = m_totalLaps;
    s.currentTimeMs = m_lapTimer.currentTimeMs(); s.lastTimeMs = m_lapTimer.lastTimeMs(); s.bestTimeMs = m_lapTimer.bestTimeMs();
    s.sector = m_lapTimer.sectorIndex(); s.inPit = st.inPitLane; s.pitLimiter = st.pitLimiterActive;
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
        m_vulkanRenderer->drawUi(m_ui.renderer());
        m_vulkanRenderer->endFrame();
    } else if (m_uiGpu) {
        m_uiGpu->uploadFrame(m_ui.renderer());
        m_uiGpu->draw();
    }
}

void SimulationLoop::syncCarTransforms() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    const ks::math::vec3 pos{st.position.x, st.position.y, st.position.z};
    // Tracked car visuals (roadmap 1.2): moved by entity id, so baked mesh
    // names that don't carry the "car_" prefix still follow the vehicle.
    for (const ks::ecs::Entity e : m_carVisualEntities)
        if (auto* t = scene().tryGet<ks::ecs::Transform>(e)) t->position = pos;
    scene().each<ks::ecs::Transform, ks::ecs::MeshInstance>([&](ks::ecs::Entity, ks::ecs::Transform& t, ks::ecs::MeshInstance& mesh) {
        if (mesh.meshName.find("car_") != 0) return;
        t.position = pos;
    });
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
}

void SimulationLoop::ensureCarVisual(const std::string& carDir) {
    if (!m_vulkanRenderer) return;
    despawnCarVisuals();
    const std::string baked = ks::sim::findBakedManifestDir(carDir);
    if (!baked.empty()) {
        std::ifstream manifest(baked + "/manifest.txt");
        std::string name;
        while (manifest && std::getline(manifest, name)) {
            if (name.empty()) continue;
            // Prefixed so baked names (original kn5 node names) can neither
            // collide with nor destroy scene meshes of the same name, and
            // so the legacy "car_" prefix sync sees them too.
            const std::string renderName = "car_" + name;
            if (!m_vulkanRenderer->loadMeshFromFile(renderName, baked + "/" + name + ".nmsh"))
                continue;
            const ks::ecs::Entity e = scene().create();
            if (e == ks::ecs::kNullEntity) break;
            scene().emplace<ks::ecs::Name>(e, ks::ecs::Name{renderName});
            scene().emplace<ks::ecs::Transform>(e);
            scene().emplace<ks::ecs::MeshInstance>(e, ks::ecs::MeshInstance{renderName});
            m_carVisualEntities.push_back(e);
        }
    }
    if (!m_carVisualEntities.empty()) {
        std::fprintf(stderr, "[scene] car visuals: %zu baked mesh(es) from %s\n",
                     m_carVisualEntities.size(), baked.c_str());
        return;
    }
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

void SimulationLoop::tick() {
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
    while (!paused && m_simAccumulator >= m_physicsDt) {
        applyInput();
#if HAS_VEHICLE_SIM
        if (m_vehicle) m_vehicle->updatePhysics(m_physicsDt);
#endif
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
    }
#endif
    updateCamera(static_cast<float>(elapsed));
    render(static_cast<float>(elapsed));
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
        const char* flag = std::getenv("KS_PARTICLES");
        m_particlesWanted = flag && *flag && std::atoi(flag) != 0;
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
            std::fprintf(stderr, "[particles] KS_PARTICLES=1: dust + rain + spray emitters up to %d sprites\n",
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
