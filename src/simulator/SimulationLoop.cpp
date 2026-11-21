#include <set>
#if defined(_WIN32)
// mmsystem.h drags in the whole of windows.h to declare one function; the
// timer call is declared here so this translation unit stays Win32-free.
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);
#pragma comment(lib, "winmm.lib")
#endif
#include <thread>
#include "engine/Graphics/LocationMaterialApply.h" // a track .kn5's location presets
#include "simulator/LocationMaterialDocument.h" // where that document lives, install included
#include "engine/Graphics/MaterialPresets.h"
#include "engine/Graphics/MaterialDatabase.h"
#include "engine/Graphics/MaterialOverrides.h"
#include "engine/Graphics/ShaderSettings.h"
#include "engine/Graphics/MaterialResponse.h"
#include "engine/Graphics/TextureNames.h"
#include "engine/Graphics/HiddenNodes.h" // the nodes the loaded content hides
#include "simulator/ModelMaterials.h"   // the car's own material entries (model.json)
#include "simulator/Kn5ModelParameters.h" // the .kn5's load parameters, kept in model.json
#include "simulator/ModelSource.h"      // the car's model file, named by model.json
#include "simulator/ShowroomInspector.h" // CM's part picker: mesh/material under the pointer
#include "engine/FileFormat/Json.h"
#include "engine/Config/PPFilterPreset.h" // loaded preset (KS_PPFILTER)
/**
 * SimulationLoop.cpp — std-only
 * 1 kHz physics + LapSectorTimer + SM + UDP + TCP telemetry
 */

#include "SimulationLoop.h"
#include "ContentLibrary.h"
#include "InputManager.h"
#include "GrassField.h" // the blades, built from the shared surface query
#include "SurfaceEffects.h" // dust, stones and the rolling sound, per wheel
#include "engine/input/WheelLedControl.h" // LED strip del volante (stato RPM/marcia)
#include "NodeTransform.h" // per-node transforms: which node is which wheel, and its pivot
#include "engine/sim/GameplayTweaks.h"     // tweak set the loop applies to the car
#include "AiTuning.h"                     // difficulty/aggression -> the field's AI
#include "CockpitCamera.h"                // fov/offset cockpit dichiarati dall'auto
#include "MultiCarManager.h"
#include "GarageRow.h"      // a row laid along the pit lane
#include "PitBoxesIni.h"    // the boxes an author placed, for the garage row
#include "PitLaneSpline.h"  // the venue's own pit lane, for the garage row
#include "NetworkManager.h"
#include "SetupGarage.h"
#include "SetupFile.h"
#include "SceneAssets.h"
#include "engine/FileFormat/Kn5Reader.h"
#include <array>
#include <functional>
#include "engine/physics/CarPhysicsJson.h"
#include "engine/FileFormat/DdsReader.h" // hall albedo decoded from the .kn5, in memory
#include <map>
#include "MaterialCache.h"   // car bakes pair with materials.txt too (roadmap 2.4)
#include "GarageSpawn.h"     // rF2-like garage/grid placement (roadmap 2.8)
#include "PitBoxMarkers.h"   // AC_PIT_n markers: hidden outside console mode
#include "TrackDynamicLights.h" // [DYNAMIC_LIGHTS]: le lampade [LIGHT_N] della pista
#include "RainEffects.h"
#include "SimulatorAudio.h"
#include "VehicleAudioHook.h" // upgrade sound pack -> SimulatorAudio (roadmap 2.9)
#include "engine/physics/DamageTelemetry.h"
#include "engine/physics/AeroModel.h"   // baseline aero capture (roadmap 2.9)
#include "engine/physics/PhysicsProfiler.h"
#include "engine/AI/AiFileReader.h"
#include "engine/AI/AiFileWriter.h"
#include <ctime>
#include "engine/vehicle/VehicleAppearanceBundle.h"      // resolveAppearance (2.9)
#include "engine/vehicle/ApplyVehicleUpgrades.h"         // physics bridge (2.9)
#include "ui/UiGpuPass.h"
#include "ui/RaceTelemetryHud.h"
#include "ui/LapDelta.h"           // best-lap delta the HUD bar draws
#include "UdpTelemetryBridge.h"
#include "TcpTelemetryBridge.h"

#include "engine/Engine.h"
#include "engine/Math/MathTypesFree.h"
#include "engine/scene/Components.h"
#include "engine/scene/SceneModule.h"
#include "engine/Graphics/RenderSystem.h"
#include "engine/devices/InputSystem.h"
#include "engine/devices/vr/XrManager.h"
#include "engine/devices/vr/XrInput.h"
#include "engine/devices/vr/XrIntegration.h"
#include "engine/input/HeadTracking.h"
#include "engine/input/VRInput.h"
#include "engine/Scripting/ScriptModule.h"
#include "engine/Scripting/ModSdk.h"
#include "engine/terrain/TerrainMesh.h"
#include "engine/terrain/TerrainHeightmap.h"
#include "engine/FileFormat/PngReader.h"
#include "engine/Graphics/SkinColour.h"
#include "engine/material/PngCodec.h"

#include "adapters/assetto_corsa/AcSharedMemoryPublisher.h"
#include "TrackTweaks.h"
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
#include "ApplyGarageSetup.h"
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
    // Dev-mode scripting (devMode.scripting): one console verb runs a Lua file
    // against the live session. LUA <file> loads and ticks it, LUA STOP drops
    // it, LUA alone reports what is loaded - the console is the dev mode's own
    // surface, so the feature needed no new key and no new page.
    m_ui.console().registerCommand("LUA", [this](const std::vector<std::string>& args) {
        ui::ConsoleOverlay& console = m_ui.console();
        if (args.empty()) {
            if (!m_scripts.available()) {
                console.addLine("  scripting: not built into this binary (HAS_LUA=0)");
            } else if (!m_scripts.active()) {
                console.addLine("  scripting: no script loaded (LUA <file>)");
            } else {
                console.addLine("  scripting: " + m_scripts.path() + " (LUA STOP drops it)");
            }
            return true;
        }
        std::string first = args[0];
        for (char& c : first)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (first == "STOP") {
            if (!m_scripts.active()) {
                console.addLine("  scripting: nothing to stop");
                return true;
            }
            console.addLine("  scripting: stopped " + m_scripts.path());
            m_scripts.stop();
            return true;
        }
        std::string message;
        const bool loaded = m_scripts.runFile(args[0], &message);
        console.addLine("  scripting: " + message);
        // Whatever the script logged on the way in (a load-time ks.log) comes
        // out here, in the same place the error would have.
        for (const std::string& line : m_scripts.takeLog()) console.addLine("  [lua] " + line);
        return loaded || !message.empty();
    });
    m_input = std::make_unique<InputManager>();
    // The console's grading rows write straight into the display pass, so a
    // room can be calibrated live instead of editing its file between frames.
    m_ui.console().setGradeHook([this](const ui::ConsoleOverlay::GradeState& grade) {
        m_consoleGradeActive = grade.active;
        m_consoleGradeTempK = grade.temperatureK;
        m_consoleGradeBrightness = grade.brightness;
        m_consoleGradeContrast = grade.contrast;
        m_consoleGradeSaturation = grade.saturation;
        m_consoleGradeExposure = grade.exposure;
        m_consoleGradeHue = grade.hue;
        m_consoleGradeSepia = grade.sepia;
        m_consoleGradeRed = grade.red;
        m_consoleGradeGreen = grade.green;
        m_consoleGradeBlue = grade.blue;
        applyConsoleGrade();
        std::fprintf(stderr,
                     "[console] grade %.0f K, brightness %.2f, contrast %.2f, "
                     "saturation %.2f, hue %.0f deg, sepia %.2f, rgb "
                     "%.2f/%.2f/%.2f, exposure %.2f%s",
                     static_cast<double>(grade.temperatureK),
                     static_cast<double>(grade.brightness),
                     static_cast<double>(grade.contrast),
                     static_cast<double>(grade.saturation),
                     static_cast<double>(grade.hue), static_cast<double>(grade.sepia),
                     static_cast<double>(grade.red), static_cast<double>(grade.green),
                     static_cast<double>(grade.blue), static_cast<double>(grade.exposure),
                     grade.exposure > 0.0f ? "" : " (the room owns the stop)");
        std::fputc(10, stderr);
    });
    // SAVE / LOAD / USE / RESET: the calibrations live in the open room's
    // grade.json, one named preset each, plus the one the room loads with. The
    // room's own ppeffects.ini belongs to the room and is read, never rewritten.
    const auto roomGradePath = [this](std::string& path, std::string& error) -> bool {
        if (!m_showroom.active()) {
            error = "no showroom open: nothing to write into";
            return false;
        }
        const std::string directory = m_showroom.config().directory;
        if (directory.empty()) {
            error = "the room does not say where it lives";
            return false;
        }
        path = (std::filesystem::path(directory) / "grade.json").string();
        return true;
    };
    m_ui.console().setMaterialHooks(
        [this]() {
            std::vector<ui::ConsoleOverlay::MaterialLine> lines;
            std::vector<ModelMaterial> materials;
            if (m_carDir.empty() || !readModelMaterials(m_carDir, materials, nullptr)) return lines;
            for (const ModelMaterial& material : materials) {
                ui::ConsoleOverlay::MaterialLine line;
                line.name = material.name;
                line.shader = material.shader;
                line.preset = material.preset;
                for (const ModelMaterialProperty& property : material.properties)
                    line.properties.emplace_back(property.name, model_materials_detail::numbersToText(property.values));
                lines.push_back(std::move(line));
            }
            return lines;
        },
        [this](const std::string& material, const std::string& key,
               const std::string& value) -> std::string {
            if (m_carDir.empty()) return "  no car loaded: nothing to edit";
            std::string error;
            if (!setModelMaterialEntry(m_carDir, material, key, value, &error))
                return "  " + error;
            if (m_carLoaded) ensureCarVisual(m_carDir);
            std::fprintf(stderr, "[materials] %s.%s = %s written to %s\n", material.c_str(),
                         key.c_str(), value.c_str(),
                         modelMaterialsPath(m_carDir).c_str());
            return "  " + material + "." + key + " = " + value + " (saved, car reloaded)";
        },
        [this](const std::string& material) -> std::string {
            if (m_carDir.empty()) return "  no car loaded: nothing to edit";
            std::string error;
            if (!removeModelMaterial(m_carDir, material, &error)) return "  " + error;
            if (m_carLoaded) ensureCarVisual(m_carDir);
            return "  " + material + " removed from model.json, car reloaded";
        },
        [this]() -> std::string {
            if (m_carDir.empty()) return "  no car loaded";
            if (m_carLoaded) ensureCarVisual(m_carDir);
            std::vector<ModelMaterial> materials;
            readModelMaterials(m_carDir, materials, nullptr);
            return "  reloaded: " + std::to_string(materials.size()) + " material entr(y/ies)";
        });
    // MATADD: one write for the shader and the preset together, one reload.
    m_ui.console().setMaterialAddHook(
        [this](const std::string& material, const std::string& shader,
               const std::string& preset) -> std::string {
            if (m_carDir.empty()) return "  no car loaded: nothing to edit";
            std::string error;
            bool created = false;
            if (!addModelMaterial(m_carDir, material, shader, preset, &created, &error))
                return "  " + error;
            if (m_carLoaded) ensureCarVisual(m_carDir);
            std::fprintf(stderr, "[materials] %s added to %s\n", material.c_str(),
                         modelMaterialsPath(m_carDir).c_str());
            return "  " + material +
                   (created ? " added (saved, car reloaded)"
                            : " topped up (saved, car reloaded)");
        });
    // Driver aids while driving: the console is the in-car switch, because a
    // wheel's own buttons are a protocol this build does not speak. The car
    // decides what a level means; this side only reports it, prints it in the
    // console and puts it on the HUD so the driver sees what he just set.
    m_ui.console().setAidHook([this](int which, int level) -> std::string {
        const char* name = which == 0 ? "TC" : "ABS";
        if (m_vehicle == nullptr) return std::string("  ") + name + ": no car loaded";
        const bool has = which == 0 ? m_vehicle->tractionControlPresent() != 0
                                    : m_vehicle->absPresent() != 0;
        if (level >= 0) {
            const int next = std::clamp(level, 0, 12);
            m_vehicle->setDriverAidLevels(which == 0 ? next : m_vehicle->tcLevel(),
                                          which == 1 ? next : m_vehicle->absLevel());
            char notice[48];
            std::snprintf(notice, sizeof(notice), "TC %d   ABS %d",
                          m_vehicle->tcLevel(), m_vehicle->absLevel());
            m_ui.notify(notice);
            std::fprintf(stderr, "[aids] %s\n", notice);
        }
        const int now = which == 0 ? m_vehicle->tcLevel() : m_vehicle->absLevel();
        char line[128];
        if (!has)
            std::snprintf(line, sizeof(line),
                          "  %s: the car's electronics declare no such system, level stays %d",
                          name, now);
        else if (level >= 0 && level != now)
            std::snprintf(line, sizeof(line), "  %s %d (asked for %d)", name, now, level);
        else
            std::snprintf(line, sizeof(line), "  %s %d", name, now);
        return line;
    });
    // Brake bias, the knob a driver moves between corners: the front share in
    // per cent, through the light path that leaves the rest of the car alone.
    // The car keeps the last word - it clamps, and it says whether its brakes
    // can be adjusted at all - which is why the hook answers with the line.
    m_ui.console().setBiasHook([this](int percent) -> std::string {
        if (m_vehicle == nullptr) return std::string("  BB: no car loaded");
        if (percent >= 0) {
            m_vehicle->setBrakeBias(static_cast<float>(std::clamp(percent, 30, 80)) / 100.f);
            char notice[48];
            std::snprintf(notice, sizeof(notice), "BB %d%%",
                          static_cast<int>(m_vehicle->brakeBias() * 100.f + 0.5f));
            m_ui.notify(notice);
            std::fprintf(stderr, "[bias] %s\n", notice);
        }
        const int now = static_cast<int>(m_vehicle->brakeBias() * 100.f + 0.5f);
        char line[160];
        if (!m_vehicle->brakeBiasAdjustable())
            std::snprintf(line, sizeof(line),
                          "  BB %d%% (the car's brakes declare no cockpit adjustment)", now);
        else
            std::snprintf(line, sizeof(line), "  BB %d%%", now);
        return line;
    });
    // ENTER on a focused inspector row: same document, same reload as MAT.
    m_ui.setInspectorCommitHook(
        [this](const std::string& material, const std::string& key,
               const std::string& value) -> std::string {
            return commitInspectorEdit(material, key, value);
        });
    const auto toGradeState = [](const RoomGradeFile& saved) {
        ui::ConsoleOverlay::GradeState grade;
        grade.temperatureK = saved.hasTemperature ? static_cast<float>(saved.temperatureK) : 6500.f;
        grade.brightness = saved.hasBrightness ? saved.brightness : 1.f;
        grade.contrast = saved.hasContrast ? saved.contrast : 1.f;
        grade.saturation = saved.hasSaturation ? saved.saturation : 1.f;
        grade.exposure = saved.hasExposure ? saved.exposure : 0.f;
        grade.hue = saved.hasHue ? saved.hueDeg : 0.f;
        grade.sepia = saved.hasSepia ? saved.sepia : 0.f;
        grade.red = saved.hasRed ? saved.red : 1.f;
        grade.green = saved.hasGreen ? saved.green : 1.f;
        grade.blue = saved.hasBlue ? saved.blue : 1.f;
        grade.active = true;
        return grade;
    };
    m_ui.console().setGradeSaveHook(
        [roomGradePath](const ui::ConsoleOverlay::GradeState& grade, const std::string& name,
                        bool makeActive) -> std::string {
            std::string path;
            std::string error;
            if (!roomGradePath(path, error)) return "  " + error;
            if (!saveRoomGradePreset(path, name, grade.brightness, grade.contrast,
                                     grade.saturation, static_cast<int>(grade.temperatureK),
                                     6500, grade.exposure, grade.hue, grade.sepia,
                                     grade.red, grade.green, grade.blue, makeActive, &error))
                return "  " + error;
            std::fprintf(stderr, "[console] grade '%s' written to %s\n", name.c_str(),
                         path.c_str());
            char line[352];
            std::snprintf(line, sizeof(line),
                          "  '%s' written: %.0f K, brightness %.2f, contrast %.2f, "
                          "saturation %.2f, hue %.0f deg, sepia %.2f, "
                          "rgb %.2f/%.2f/%.2f%s%s",
                          name.c_str(), static_cast<double>(grade.temperatureK),
                          static_cast<double>(grade.brightness),
                          static_cast<double>(grade.contrast),
                          static_cast<double>(grade.saturation),
                          static_cast<double>(grade.hue), static_cast<double>(grade.sepia),
                          static_cast<double>(grade.red), static_cast<double>(grade.green),
                          static_cast<double>(grade.blue),
                          grade.exposure > 0.0f ? ", exposure set" : ", room stop kept",
                          makeActive ? " (active)" : "");
            return line;
        });
    m_ui.console().setGradeLoadHook(
        [roomGradePath, toGradeState](const std::string& name,
                                      ui::ConsoleOverlay::GradeState& out) -> bool {
            std::string path;
            std::string error;
            if (!roomGradePath(path, error)) return false;
            RoomGradePresets presets;
            if (!loadRoomGrades(path, presets)) return false;
            const auto found = presets.presets.find(name);
            if (found == presets.presets.end()) return false;
            out = toGradeState(found->second);
            return true;
        });
    m_ui.console().setGradeListHook([roomGradePath, toGradeState]() {
        std::vector<ui::ConsoleOverlay::GradePreset> list;
        std::string path;
        std::string error;
        if (!roomGradePath(path, error)) return list;
        RoomGradePresets presets;
        if (!loadRoomGrades(path, presets)) return list;
        for (const auto& entry : presets.presets) {
            ui::ConsoleOverlay::GradePreset preset;
            preset.name = entry.first;
            preset.grade = toGradeState(entry.second);
            preset.active = entry.first == presets.active;
            list.push_back(preset);
        }
        return list;
    });
#if HAS_VEHICLE_SIM
    m_vehicle = std::make_unique<ks::physics::VehicleSimulator>();
#endif
    // AI/multi-car manager (roadmap 3.5). Must be built before the
    // NetworkManager: its constructor arms client/server transports with
    // multiCarManager() and a null pointer there is never re-checked.
    m_multiCar = std::make_unique<MultiCarManager>();
    m_network = std::make_unique<NetworkManager>(this);
    // Phase 3.3 — the transport carries the layout id but does not act on it:
    // the loop announces the layout it has bound and rebinds to the host's one
    // when a welcome arrives, so both sides drive the same layout.
    m_network->localLayoutId = [this]() { return activeLayoutId(); };
    m_network->onServerWelcome = [this](const std::string&, const std::string& layoutId) {
        if (layoutId.empty()) return;
        if (switchLayout(layoutId)) return;
        // The host drives a layout this copy does not have. Staying on the
        // default silently would leave two drivers on different lengths, so
        // the reason goes to the chat as well as to the HUD notice.
        m_ui.chat().addLine("HOST LAYOUT: " + m_layoutNotice);
    };
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
    // GAP_MATRIX P1.10: the host publishes its damage ~5 Hz. The client used to
    // drop the snapshot on the floor, so the other player's car always looked
    // intact here; it now lands on the car the id maps to.
    m_network->onCarDamageReceived = [this](const net::CarDamageMessage& msg) {
        applyRemoteCarDamage(msg);
    };
    m_network->onChatMessageReceived = [this](uint32_t, const std::string& from,
                                              const std::string& message) {
        m_ui.chat().addLine(from + ": " + message);
    };
    m_lapTimer.configure(3);
    m_ui.resize(m_viewW, m_viewH);
    m_ui.dashboard().setVisible(true);
    m_ui.menu().setVisible(false);
    // Phase 3.3 — the SINGLE PLAYER page picks the layout of the chosen track.
    // The loop owns the catalog, so the menu asks for the pairs and hands back
    // an id: switching goes through switchLayout like any other caller.
    m_ui.menu().layoutChoices = [this]() { return layoutChoices(); };
    m_ui.menu().activeLayoutId = [this]() { return activeLayoutId(); };
    m_ui.menu().onLayoutChosen = [this](const std::string& id) { switchLayout(id); };
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
        // P0.3 autosave: a recording that runs through the session end saves
        // itself — nobody is around to press the toggle when the checkered
        // falls. Manual toggles keep working exactly as before.
        if (m_features.replay.isRecording()) {
            m_features.replay.stopRecording();
            saveReplayRecording();
        }
        // P0.2 learning commit: AC overwrites fast_lane.ai from the candidate
        // on commit. With no authored line to clobber, the trace learned this
        // session becomes the line automatically; a manual AI PROMOTE always
        // wins for the overwrite case.
        if (m_aiCandidateWritten) {
            if (!m_trackData.directory.empty()) {
                const std::string dst =
                    m_trackData.directory + "/ai/fast_lane.ai";
                std::error_code ec;
                if (!std::filesystem::exists(dst, ec)) promoteAiCandidate();
            }
            m_aiCandidateWritten = false;
        }
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
    m_raceSession.onPenaltyIssued = [this](int car, const std::string& type,
                                           const std::string& reason) {
        ks::scripting::modsdk::dispatch("penalty", {double(car)}, {type, reason});
        // P0.1 HUD wiring: AC puts a centred message on the screen the moment
        // the race director hands one out ("PENALTY — DRIVE THROUGH"). The
        // notification centre is that surface here; modsdk above stays the
        // scripted-apps channel. Player car is index 0 (setPlayerCarIndex).
        if (car == 0) {
            std::string text = "PENALTY " + type;
            if (!reason.empty()) text += " (" + reason + ")";
            m_ui.notify(std::move(text), ui::NoticeKind::Warning);
        }
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
    ks::engine::input::HeadTracking::instance().shutdown();
    ks::engine::input::VRInput::instance().shutdown();
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
    auto& headTracking = ks::engine::input::HeadTracking::instance();
    auto& vrInput = ks::engine::input::VRInput::instance();
    engine.registerModule("ks.input.head_tracking", borrowModule(headTracking));
    engine.registerModule("ks.input.vr", borrowModule(vrInput));
    headTracking.initialize();
    vrInput.initialize();
    auto& scene = ks::ecs::SceneModule::instance();
    scene.attach(&engine.registry());
    scene.addSystem("syncCarTransforms", [this](ks::ecs::Registry&, double) { syncCarTransforms(); });
    engine.registerModule("ks.scene", borrowModule(scene));
    ks::engine::devices::InputSystem::instance().initialize();
    ks::engine::graphics::RenderSystem::instance().initialize();
    auto& script = ks::scripting::ScriptModule::instance();
    engine.registerModule("ks.script", borrowModule(script));
    script.initialize();
    // Gameplay tweaks: the module owns the set, the loop owns applying it to
    // the car. Registered here so its lifecycle runs with the other modules,
    // and the change callback reaches the vehicle mid-session instead of
    // waiting for the next setup apply.
    auto& gameplayTweaks = ks::engine::sim::GameplayTweaks::instance();
    engine.registerModule("ks.sim.tweaks", borrowModule(gameplayTweaks));
    gameplayTweaks.initialize();
    gameplayTweaks.onTweaksChanged =
        [this](const ks::engine::sim::GameplayTweakSet&) { applyGameplayTweaks(); };
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
    // devMode.scripting: KS_SCRIPT=<file> loads a dev-mode script at start, so
    // an example can be watched without typing the console first (one ships
    // in content/scripts). A bad path says why and leaves the session alone.
    if (const char* script = std::getenv("KS_SCRIPT"); script != nullptr && script[0] != '\0') {
        std::string message;
        m_scripts.runFile(script, &message);
        std::printf("[script] %s\n", message.c_str());
        m_ui.console().addLine(std::string("scripting: ") + message);
        for (const std::string& line : m_scripts.takeLog())
            m_ui.console().addLine("[lua] " + line);
        if (!m_scripts.active() && !m_scripts.lastError().empty())
            m_ui.console().addLine("  scripting: " + m_scripts.lastError());
    }
    return true;
}

bool SimulationLoop::loadTrack(const std::string& kn5Path) {
    // Caricare una pista chiude lo showroom: la sala non deve restare in scena
    // né continuare a pilotare camera/luce/meteo mentre si guida in pista.
    if (!m_showroomHallLoading && m_showroom.active()) closeShowroom();
    // Only a track gives the sky its sun back: closing the room is not enough
    // (a drive session started in the showroom keeps the hall on screen, and
    // the sky maps must keep coming from the room's sun there).
    if (m_vulkanRenderer) m_vulkanRenderer->setSkyIblFrozen(false);
    releaseShowroomScene();
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
    // Caricare una pista chiude lo showroom: la sala non deve restare in scena
    // né continuare a pilotare camera/luce/meteo mentre si guida in pista.
    if (!m_showroomHallLoading && m_showroom.active()) closeShowroom();
    if (m_vulkanRenderer) m_vulkanRenderer->setSkyIblFrozen(false);
    releaseShowroomScene();
    if (!fs::is_directory(trackDirectory)) return false;
    loadContentMods(trackDirectory); // Lua Mod SDK: <track>/scripts/*.lua
    m_trackData.directory = trackDirectory;
    m_trackData.name = fs::path(trackDirectory).filename().string();
    // Phase 3.3 — a track folder may expose several layouts that share its
    // assets (TrackLayout.h). Discover them once, let KS_LAYOUT pick one for
    // headless runs, and keep the resolved folder on the track data so a
    // later switchLayout() does not have to rediscover anything.
    m_layouts.discover(trackDirectory);
    if (const char* layoutEnv = std::getenv("KS_LAYOUT"); layoutEnv && layoutEnv[0]) {
        if (!m_layouts.selectById(layoutEnv)) {
            const TrackLayoutInfo* active = m_layouts.active();
            std::fprintf(stderr, "SimulationLoop: KS_LAYOUT=%s not found, using '%s'\n",
                         layoutEnv, active ? active->id.c_str() : "default");
        }
    }
    const TrackLayoutInfo* layout = m_layouts.active();
    const std::string layoutId = layout ? layout->id : std::string("default");
    TrackLayoutBinding binding;
    binding.folder = trackDirectory;
    binding.aiFolder = trackDirectory;
    if (layout) binding = resolveLayoutBinding(*layout, trackDirectory);
    // AI racing line (roadmap 3.5): load it at track-load time so the grid
    // can be spawned when the session starts. No file = spawnGrid is a no-op.
    // A layout that ships its own ai/ folder brings its own line; otherwise
    // the one at the track root still applies.
    if (m_multiCar) m_multiCar->loadAiSpline(binding.aiFolder);
    updateTrackLimitsCenterline();
    // Native JSON manifest (data/data.json) preferred for the track's name and
    // length; surfaces.ini remains the AC compatibility path for its grip.
    // The surface widths are read either way: the track-limits corridor is
    // built from them (TrackLimitsMonitor::setBands) and the JSON manifest
    // carries none, so a track with a manifest would otherwise keep the flat
    // 8 m corridor.
    {
        ks::ac::TrackDataJsonLoader trackJson;
        const bool haveManifest = trackJson.loadForTrackDir(binding.folder);
        fs::path surf = fs::path(binding.folder) / "data" / "surfaces.ini";
        if (!fs::exists(surf)) surf = fs::path(binding.folder) / "surfaces.ini";
        if (!fs::exists(surf)) surf = fs::path(trackDirectory) / "data" / "surfaces.ini";
        if (fs::exists(surf)) {
            ks::ac::AcSurfacesLoader loader;
            // Grip stays the compatibility path's job, as before; only the
            // bands are read whichever manifest won.
            loader.load(surf.string(), !haveManifest);
            std::vector<ks::sim::TrackSurfaceBand> bands;
            for (const auto& band : loader.bands())
                bands.push_back({band.key, band.halfWidthM, band.validTrack});
            m_features.trackLimits.setBands(std::move(bands));
            const auto& declared = m_features.trackLimits.bands();
            if (!declared.empty()) {
                std::fprintf(stderr, "[limits] %zu surface band(s), innermost first:",
                             declared.size());
                for (const auto& band : declared)
                    std::fprintf(stderr, " %s %.1f m%s", band.name.c_str(), band.halfWidthM,
                                 band.validTrack ? "" : " (off track)");
                std::fputc('\n', stderr);
            }
        } else {
            m_features.trackLimits.setBands({});
        }
    }
    // Per-track tweaks (system/cfg/tracktweaks.json, content.trackTweaks): the
    // track keeps declaring its own grip and the file scales what it declared,
    // so an update to the content is not overwritten by a number frozen in a
    // config. No file, no entry or 1.0 leaves the track exactly as it declares
    // itself.
    {
        static const ks::sim::TrackTweakTable kTrackTweaks = [] {
            ks::sim::TrackTweakTable table;
            if (!table.loadDefault()) {
                std::fprintf(stderr, "[track-tweaks] %s (every track as it declares itself)\n",
                             table.lastError().c_str());
            } else {
                std::fprintf(stderr, "[track-tweaks] %zu track(s) tweaked (system/cfg/tracktweaks.json)\n",
                             table.size());
            }
            return table;
        }();
        const std::string trackId = fs::path(binding.folder).filename().string();
        if (const ks::sim::TrackTweak* tweak = kTrackTweaks.find(trackId);
            tweak != nullptr && !tweak->empty()) {
            auto& surface = ks::physics::TrackSurface::instance();
            const float declared = surface.baseGrip();
            surface.setBaseGrip(ks::sim::tweakedGrip(declared, *tweak));
            std::fprintf(stderr, "[track-tweaks] %s: grip x%.3f (%.3f -> %.3f)\n",
                         trackId.c_str(), tweak->gripMultiplier, declared,
                         surface.baseGrip());
        }
    }
    if (!binding.name.empty()) m_trackData.name = binding.name;
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
    // A layout folder of its own may carry its own geometry; the root scan
    // above only sees the layouts that share the base assets.
    if (kn5.empty() && !binding.kn5Path.empty()) kn5 = binding.kn5Path;
    if (!kn5.empty()) {
        if (!loadTrack(kn5)) return false;
    } else {
        m_trackData.valid = true; m_trackLoaded = true;
        // Roadmap 1.2: a folder without a .kn5 can still carry a baked cache.
        applyTrackVisuals(binding.folder);
    }
    // loadTrack() rebuilds SimTrackData from the .kn5, so the layout identity,
    // its length and the manifest name are applied after it, not before.
    m_trackData.directory = trackDirectory;
    m_trackData.layoutId = layoutId;
    m_trackData.layoutFolder = binding.folder;
    if (binding.lengthM > 1.f) m_trackData.splineLength = binding.lengthM;
    if (!binding.name.empty()) m_trackData.name = binding.name;
    // Luci dinamiche del tracciato — le lampade che la pista dichiara in
    // data/data.json (formato nativo del progetto). Loaded where the track
    // is: closeShowroom() cleared the room's lamps above, so a track that
    // declares none just leaves the empty list behind instead of another
    // venue's lamps. While the showroom hall is still loading the room owns
    // the lamps, the same way it owns closeShowroom() above.
    if (!m_showroomHallLoading)
        spawnTrackDynamicLights(binding.folder, trackDirectory);
    // Grass FX — tinta/vento dell'erba che la pista dichiara in data/data.json
    // (cartella layout prima, root del tracciato dopo). Una pista senza il file
    // mantiene il look autoriale; il cablaggio render consuma grassFx() quando
    // la mesh erba cresce la risposta al vento.
    {
        m_grassFx = ks::sim::GrassFxConfig{};
        const std::string grassJson =
            ks::sim::GrassFxConfig::jsonPathFor(binding.folder, trackDirectory);
        if (!grassJson.empty()) {
            m_grassFx = ks::sim::GrassFxConfig::load(grassJson);
            if (m_grassFx.present) {
                std::fprintf(stderr,
                             "[grass-fx] %s: tinta (%.2f, %.2f, %.2f) diffuse %.2f "
                             "densita' %.2f vento %.1f m/s @%.0fdeg raffiche %.2f (%s)\n",
                             layoutId.c_str(), m_grassFx.color[0], m_grassFx.color[1],
                             m_grassFx.color[2], m_grassFx.diffuse, m_grassFx.density,
                             m_grassFx.windSpeed, m_grassFx.windDirection,
                             m_grassFx.gusts, grassJson.c_str());
            }
        }
    }
    // Seasons fondazione: il mese reale del giorno determina la stagione
    // (SeasonControl), che il texture swap e il meteo consumeranno; un
    // KS_SEASON manuale arriva col texture swap.
    {
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        m_season.setDate(1970, local.tm_mon + 1, local.tm_mday);
    }
    m_ui.menu().setTrackName(m_trackData.name);
    m_lapTimer.configure(3);
    std::fprintf(stderr,
                 "SimulationLoop: track '%s' layout '%s' length=%.1f (%d layouts)\n",
                 m_trackData.name.c_str(), layoutId.c_str(), m_trackData.splineLength,
                 m_layouts.count());
    return true;
}

bool SimulationLoop::switchLayout(const std::string& layoutId) {
    namespace fs = std::filesystem;
    if (m_trackData.directory.empty()) {
        std::fprintf(stderr, "SimulationLoop::switchLayout: no track loaded\n");
        return false;
    }
    if (m_layouts.empty()) m_layouts.discover(m_trackData.directory);
    if (!m_layouts.selectById(layoutId)) {
        // Kept rather than only logged: a guest whose copy of the track lacks
        // the host's layout has to be told why it is on a different one.
        m_layoutNotice = m_layouts.missingLayoutMessage(layoutId);
        if (m_layoutNotice.empty())
            m_layoutNotice = "layout '" + layoutId + "' not available";
        std::fprintf(stderr, "SimulationLoop::switchLayout: %s\n", m_layoutNotice.c_str());
        return false;
    }
    m_layoutNotice.clear();
    const TrackLayoutInfo* layout = m_layouts.active();
    if (!layout) return false;

    const TrackLayoutBinding binding = resolveLayoutBinding(*layout, m_trackData.directory);
    m_trackData.layoutId = layout->id;
    m_trackData.layoutFolder = binding.folder;

    // The layout's own manifest wins over its catalog entry for length and
    // display name; both stay untouched when the folder has neither.
    if (binding.lengthM > 1.f) m_trackData.splineLength = binding.lengthM;
    if (!binding.name.empty()) m_trackData.name = binding.name;

    // Prefer a layout-local .kn5; without one the current geometry stays, so
    // only the data the layout owns actually changes.
    if (!binding.kn5Path.empty()) {
        m_trackData.kn5Path = binding.kn5Path;
        applyTrackVisuals(binding.folder);
    }

    // Layout-local AI line when the layout ships one, otherwise the root's.
    if (m_multiCar) m_multiCar->loadAiSpline(binding.aiFolder);
    // A layout is its own circuit: its line replaces the corridor, and a layout
    // without one leaves limits on the phase hint rather than the old line.
    updateTrackLimitsCenterline();

    // The car, renderer and session stack stay alive: only the lap clock and
    // the distance travelled along the new spline are reset.
    m_ui.menu().setTrackName(m_trackData.name);
    m_lapTimer.configure(3);
    m_lapDistance = 0.f;
    m_normalizedSpline = 0.f;
    m_trackData.valid = true;
    m_trackLoaded = true;
    m_layoutNotice.clear();
    std::fprintf(stderr, "SimulationLoop: switched layout '%s' (%s) length=%.1f\n",
                 layout->id.c_str(), m_trackData.name.c_str(), m_trackData.splineLength);
    return true;
}

std::vector<std::pair<std::string, std::string>> SimulationLoop::layoutChoices() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& layout : m_layouts.layouts()) {
        std::string name = layout.name.empty() ? layout.id : layout.name;
        // The length is what actually changes on most layouts, so it belongs
        // in the label; the default entry keeps the track's own name alone.
        if (!layout.isDefault && layout.lengthM > 1.f)
            name += " (" + std::to_string(static_cast<int>(layout.lengthM + 0.5f)) + " m)";
        out.emplace_back(layout.id, name);
    }
    return out;
}

std::string SimulationLoop::activeLayoutId() const {
    const TrackLayoutInfo* active = m_layouts.active();
    return active ? active->id : std::string();
}

bool SimulationLoop::setupScreenVisible() const {
    return m_setupGarage != nullptr && m_setupGarage->isVisible();
}

SetupGarage* SimulationLoop::setupGarage() {
    // Built on first use (menu page, setup load): the loop owns it so the screen
    // outlives a car change, and its edits have somewhere to go.
    if (!m_setupGarage) {
        m_setupGarage = std::make_unique<SetupGarage>();
        // GAP_MATRIX P0.3: every row the driver moves goes straight to the
        // vehicle. Before this, onSetupChanged was never assigned, so the garage
        // edited a copy nothing read.
        m_setupGarage->onSetupChanged = [this](const SetupData& s) {
            applySetupToVehicle(s);
        };
        // ENTER / L on the screen: the setup file lives beside the car, in AC's
        // own format, so the same numbers read back row for row.
        m_setupGarage->onSave = [this]() -> std::string {
            if (m_carDir.empty()) return m_setupGarage->noCarSaveStatus();
            const std::string path =
                (std::filesystem::path(m_carDir) / "ksim.setup").string();
            std::string error;
            if (!m_setupGarage->saveSetupFile(path, &error))
                return m_setupGarage->saveFailedStatus(error);
            std::fprintf(stderr, "[setup] written to %s\n", path.c_str());
            return m_setupGarage->savedStatus(path);
        };
        // The hub draws it: the loop owns the screen, the UI layer paints it.
        m_ui.setSetupScreen(m_setupGarage.get());
        m_setupGarage->onLoad = [this](const std::string& path) -> std::string {
            std::string error;
            if (!m_setupGarage->loadSetupFile(path, &error))
                return m_setupGarage->loadFailedStatus(error);
            applySetupToVehicle(m_setupGarage->currentSetup());
            std::fprintf(stderr, "[setup] read back from %s\n", path.c_str());
            return m_setupGarage->loadedStatus(path);
        };
    }
    // The screen is created at different points on different paths, so the car's
    // answer is applied every time it is handed out, not only at car load.
    m_setupGarage->setAidsAvailable(m_tcPresent != 0, m_absPresent != 0);
    // The screen is the car's own document: it follows the loaded car, so a
    // change of car changes the rows, the ranges and the numbers on them.
    if (!m_carDir.empty() && m_carDir != m_setupGarageCar) {
        m_setupGarage->loadCar(m_carDir);
        m_setupGarageCar = m_carDir;
        m_setupGarage->setCarName(m_carName);
    }
    return m_setupGarage.get();
}

void SimulationLoop::applyGameplayTweaks() {
    const auto& tweaks = ks::engine::sim::GameplayTweaks::instance().getTweakSet();
#if HAS_VEHICLE_SIM
    // The two tweaks with a physics effect today: the tank drains at the car's
    // own rate times the first multiplier, the tyres wear at the model's rate
    // times the second (0 keeps either untouched for a practice run).
    if (m_vehicle) {
        m_vehicle->setFuelConsumptionMultiplier(tweaks.fuelConsumption);
        m_vehicle->setTireWearMultiplier(tweaks.tireDamageRate);
        m_vehicle->setDamageEnabled(tweaks.mechanicalFailures);
    }
#endif
    // The field's AI rides on the same set: difficulty is the drivers' pace
    // against the line, aiAggression their willingness to fight for a place.
    // Applied to the cars already on track; the manager hands both to any car
    // spawned later, so a grid built after this call inherits them too.
    if (m_multiCar) {
        const AiTuning tuning = aiTuningFrom(tweaks);
        m_multiCar->setAiTuning(tuning.pace, tuning.aggression);
        if (tuning.pace != m_appliedAiPace ||
            tuning.aggression != m_appliedAiAggression) {
            m_appliedAiPace = tuning.pace;
            m_appliedAiAggression = tuning.aggression;
            std::printf("[AI] difficulty x%.2f, aggression %.2f\n",
                        static_cast<double>(tuning.pace),
                        static_cast<double>(tuning.aggression));
        }
    }
    // Racing rules: the monitor's ladder, not its on/off switch. enabled is
    // carried over from the current config so a LIMITS OFF from the control
    // API (or the menu) survives a gameplay change instead of being undone
    // here.
    {
        TrackLimitsConfig limits = trackLimitsConfigForRules(tweaks.racingRulesStrict);
        limits.enabled = m_features.trackLimits.config().enabled;
        m_features.setTrackLimitsConfig(limits);
        if (tweaks.racingRulesStrict != m_appliedStrictRules) {
            m_appliedStrictRules = tweaks.racingRulesStrict;
            std::printf("[TWEAKS] racing rules %s (time every %d cut(s), drive-through at %d)\n",
                        tweaks.racingRulesStrict ? "strict" : "lenient",
                        limits.timePenaltyAt, limits.driveThroughAt);
        }
    }
    applyDamageEffects();
    if (tweaks.fuelConsumption != m_appliedFuelTweak ||
        tweaks.tireDamageRate != m_appliedTireTweak) {
        m_appliedFuelTweak = tweaks.fuelConsumption;
        m_appliedTireTweak = tweaks.tireDamageRate;
        std::printf("[TWEAKS] fuel consumption x%.2f, tyre wear x%.2f\n",
                    static_cast<double>(tweaks.fuelConsumption),
                    static_cast<double>(tweaks.tireDamageRate));
    }
}

void SimulationLoop::applyDamageEffects() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto& tweaks = ks::engine::sim::GameplayTweaks::instance().getTweakSet();
    if (tweaks.damageAffectsDriving) {
        m_vehicle->setEnginePower(260.0 * std::max(0.15, (double)m_vehicle->damage().powerMultiplier()));
    } else {
        m_vehicle->setEnginePower(260.0);
    }
#endif
}

void SimulationLoop::setCockpitKnobs(const CockpitKnobs& knobs) {
    m_cockpitKnobs = knobs;
    applyCockpitKnobs(); // a car may already be in the garage
}

void SimulationLoop::applyCockpitKnobs() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return; // stored until the next loadCar()
    if (m_cockpitKnobs.hasTc || m_cockpitKnobs.hasAbs) {
        m_vehicle->setDriverAidLevels(
            m_cockpitKnobs.hasTc ? m_cockpitKnobs.tcLevel : m_vehicle->tcLevel(),
            m_cockpitKnobs.hasAbs ? m_cockpitKnobs.absLevel : m_vehicle->absLevel());
        m_tcPresent = m_vehicle->tractionControlPresent();
        m_absPresent = m_vehicle->absPresent();
        if (m_setupGarage) m_setupGarage->setAidsAvailable(m_tcPresent != 0, m_absPresent != 0);
    }
    if (m_cockpitKnobs.hasBias)
        m_vehicle->setBrakeBias(static_cast<float>(m_cockpitKnobs.brakeBiasPct) / 100.f);
#endif
}

void SimulationLoop::applySetupToVehicle(const SetupData& s) {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return; // no car loaded: the screen still holds the values
    // Start from the car's resolved params: the screen does not own roll
    // centres, compliances or roll steer, and those must survive a fuel change.
    // It does own the anti-roll bars now — the car's own setup.ini carries them
    // on the suspension page.
    m_vehicle->applySetup(setupParamsFromGarage(s, m_vehicle->setupParams()));
    // applySetup() resets the setup, not the session's tweaks: push them back
    // on the fresh values so a garage visit cannot undo the multiplier.
    applyGameplayTweaks();
#else
    (void)s;
#endif
}

bool SimulationLoop::loadCar(const std::string& carDir) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(carDir)) return false;
    loadContentMods(carDir); // Lua Mod SDK: <car>/scripts/*.lua
    m_carName = fs::path(carDir).filename().string();
    m_carDir = carDir; // roadmap 2.8/2.9: livery + upgrades resolve against it
    despawnCarVisuals();
    // Another car's tyres have no history in this one: no bulge, no flat spot.
    m_tyreVisuals.reset();
    // The content's own brake-disc settings: the [BRAKEDISC_FX] sections the
    // conversion folded into model.json. A car that declares none keeps the
    // glow the thermal model computes, unchanged.
    m_brakeDiscFx = ks::graphics::readBrakeDiscFx(carDir);
    if (m_brakeDiscFx.declared) {
        std::fprintf(stderr, "[brakes] content disc fx: active=%d glowMult=%.3f\n",
                     m_brakeDiscFx.active ? 1 : 0, m_brakeDiscFx.glowMult);
    }
#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        m_vehicle->setMass(1200); m_vehicle->setEnginePower(260); m_vehicle->setMaxRpm(8500);
        // The manifest first, then the .ini set on top of it. This path loads the
        // physics file by file, so the unified loader that also reads the manifest
        // never runs during a session: without this the car kept the placeholder
        // box's mass (1200 kg) and had no torque curve at all - engine.ini only
        // names power.lut, and nothing on this path read it, which is why a car
        // revved at idle and would not pull. A folder that still ships .ini keeps
        // it: those loaders run after and win where both declare a value.
        ks::physics::CarPhysicsJson::loadAcPhysicsForCarDir(carDir, *m_vehicle);
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
        tryLoad("brakes.ini", [&](const std::string& p) { m_vehicle->loadBrakesFromIni(p); });
        // The car's own brake temperature tables (data/brake_temp_front.lut and
        // the rear one, temperature|multiplier): the fade follows the car, not a
        // generic threshold. A car without them keeps the model's defaults.
        {
            std::vector<std::pair<float, float>> front, rear;
            const fs::path dataDir = fs::path(carDir) / "data";
            const bool hasFront = ks::physics::BrakeThermalModel::loadPerfCurve(
                (dataDir / "brake_temp_front.lut").string(), front);
            const bool hasRear = ks::physics::BrakeThermalModel::loadPerfCurve(
                (dataDir / "brake_temp_rear.lut").string(), rear);
            // The cooling shape lives in the manifest's brakes.TEMPS_* block,
            // not in brakes.ini: same car data, other file.
            ks::physics::CarPhysicsJson::loadAcBrakeTempsForCarDir(carDir, *m_vehicle);
            if (hasFront || hasRear) {
                m_vehicle->setBrakeTempCurves(front, rear);
                std::fprintf(stderr, "[brakes] brake temp curves: front=%zu rear=%zu points\n",
                             front.size(), rear.size());
            }
        }
    }
#endif
    // The car's electronics are read here as well: this path loads the physics
    // file by file, so the unified loader that also reads the manifest never
    // runs during a session.
    if (m_vehicle) {
        m_vehicle->loadElectronicsForCar(carDir);
        m_tcPresent = m_vehicle->tractionControlPresent();
        m_absPresent = m_vehicle->absPresent();
    }
    if (m_setupGarage) {
        m_setupGarage->setAidsAvailable(m_tcPresent != 0, m_absPresent != 0);
    }
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
        m_carPaintExplicit = false;
        m_carPaint[0] = m_carPaint[1] = m_carPaint[2] = 1.0f;
        m_carPaintDeclared = !paints.empty();
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
    // The cockpit knobs last: everything above may reset the aid levels and the
    // bias (upgrades re-apply the setup), and this is the point where the car is
    // final. A session the driver changes them in keeps his values afterwards.
    applyCockpitKnobs();
    // Camera cockpit dell'auto: fov e offset che l'auto dichiara in
    // data/data.json (sezione "cockpitCamera"). Un'auto senza sezione mantiene
    // i default runtime (fov 60, offset {0,1.2,-0.3}): neutralita' legacy.
    {
        const std::string camJson = CockpitCameraConfig::jsonPathFor(carDir);
        if (!camJson.empty()) {
            const CockpitCameraConfig cc = CockpitCameraConfig::load(camJson);
            if (cc.present && camera()) {
                camera()->setFov(std::clamp(cc.fovDeg, cc.minFov, cc.maxFov));
                camera()->setCockpitOffset(
                    vec3(cc.offsetX, cc.offsetY, cc.offsetZ));
                std::fprintf(stderr,
                             "[camera] cockpit auto: fov=%.1f offset=(%.2f, %.2f, %.2f)\n",
                             cc.fovDeg, cc.offsetX, cc.offsetY, cc.offsetZ);
            }
        }
    }
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
    // From here the hall is the scene, and it stays the scene through the
    // session a player starts inside it: the room's sun is what lights it.
    m_showroomHallInScene = true;
    return true;
}

void SimulationLoop::loadShowroomCar() {
    if (!m_vulkanRenderer || !m_showroom.active()) return;
    // KS_CONSOLE=1 opens the room as a calibration stage and KS_NO_CAR=1 does
    // the same for anyone who only wants the empty hall: no car on the
    // turntable, so the backdrop, the grading and the hall are what is judged.
    const char* noCar = std::getenv("KS_NO_CAR");
    const char* consoleEnv = std::getenv("KS_CONSOLE");
    if ((noCar && noCar[0] && noCar[0] != '0') ||
        (consoleEnv && consoleEnv[0] && consoleEnv[0] != '0')) {
        std::fprintf(stderr, "[showroom] empty stage: no car on the turntable\n");
        return;
    }
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

// --- Showroom part inspector (Content Manager behaviour) --------------------
// A click projects the pointer into the frame and walks the triangles the
// renderer keeps for each car mesh, so the surface the player sees is what gets
// selected. The pick is CPU-only and runs once per click.
bool SimulationLoop::pickShowroomPart(float designX, float designY) {
    const bool inspectDebug = std::getenv("KS_INSPECT_DEBUG") != nullptr;
    if (inspectDebug)
        std::fprintf(stderr, "[inspector] pick entered at %.1f,%.1f\n",
                     static_cast<double>(designX), static_cast<double>(designY));
    if (m_vulkanRenderer == nullptr || m_carVisualEntities.empty()) return false;
    if (m_viewW <= 0 || m_viewH <= 0) return false;
    // Design-space pointer -> NDC. The projection carries the aspect ratio, so
    // the two axes are independent here.
    const float ndcX = (designX / static_cast<float>(m_viewW)) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (designY / static_cast<float>(m_viewH)) * 2.0f;
    const auto viewProj = m_vulkanRenderer->viewProjection();

    // KS_INSPECT_DEBUG: what the pick saw, for checking the projection
    // convention against a screenshot (the pointer is the only input).
    if (inspectDebug) {
        std::fprintf(stderr,
                     "[inspector] click design %.1f %.1f -> ndc %.3f %.3f, %zu car mesh(es)\n",
                     static_cast<double>(designX), static_cast<double>(designY),
                     static_cast<double>(ndcX), static_cast<double>(ndcY),
                     m_carVisualEntities.size());
    }

    std::string hit;
    float hitDepth = -1.f;
    for (const ks::ecs::Entity entity : m_carVisualEntities) {
        const auto* instance = scene().tryGet<ks::ecs::MeshInstance>(entity);
        const auto* transform = scene().tryGet<ks::ecs::Transform>(entity);
        if (instance == nullptr || transform == nullptr) continue;
        const NativeMesh* mesh = m_vulkanRenderer->findMesh(instance->meshName);
        if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.empty()) continue;
        InspectorGeometry geometry;
        geometry.vertices = reinterpret_cast<const float*>(mesh->vertices.data());
        geometry.vertexCount = mesh->vertices.size();
        geometry.strideFloats = sizeof(NativeVertex) / sizeof(float);
        geometry.indices = mesh->indices.data();
        geometry.indexCount = mesh->indices.size();
        const auto world = ks::ecs::worldMatrix(*transform);
        const float* cells = world.data();
        for (int i = 0; i < 16; ++i) geometry.world[i] = cells[i];
        const float depth = inspectorHitDepth(viewProj.data(), ndcX, ndcY, geometry);
        if (inspectDebug) {
            std::fprintf(stderr, "[inspector]   %-14s depth=%.4f\n",
                         instance->meshName.c_str(), static_cast<double>(depth));
        }
        if (depth < 0.f) continue; // pointer not over this part
        if (hit.empty() || depth < hitDepth) {
            hit = instance->meshName;
            hitDepth = depth;
        }
    }
    if (hit.empty()) {
        // A click on the room clears the selection, as CM does.
        if (inspectDebug)
            std::fprintf(stderr, "[inspector] nothing under %.1f,%.1f: selection cleared\n",
                         static_cast<double>(designX), static_cast<double>(designY));
        clearShowroomSelection();
        return false;
    }
    return selectShowroomMesh(hit);
}

bool SimulationLoop::selectShowroomMesh(const std::string& name) {
    if (name.empty()) return false;
    if (name == m_inspectedMesh) return true; // same part: nothing to redo
    clearShowroomSelection();
    const NativeMesh* mesh = m_vulkanRenderer->findMesh(name);
    if (mesh == nullptr) return false;
    m_inspectedOriginal = *mesh;
    // What the surface looked like before the wash: deselecting puts exactly
    // these back, so a selection never survives the click that ends it.
    for (int i = 0; i < 3; ++i) m_inspectedTint[i] = mesh->tint[i];
    m_inspectedPaintSolid = mesh->paintSolid;
    m_inspectedMesh = name;
    m_inspectedValid = true;
    // CM's selection wash: the part keeps its shape and reads amber, so it can
    // be told apart from the rest of the car at a glance. The upload itself is
    // queued for render(), never done here (see applyPendingTints).
    ShowroomTintRequest wash;
    wash.mesh = name;
    wash.tint[0] = 0.95f;
    wash.tint[1] = 0.55f;
    wash.tint[2] = 0.12f;
    wash.paintSolid = 1.0f;
    m_pendingTints.push_back(std::move(wash));
    refreshShowroomInspector();
    std::fprintf(stderr, "[inspector] selected mesh %s (material %s)\n", name.c_str(),
                 m_inspectedOriginal.materialName.empty() ? "-"
                                                          : m_inspectedOriginal.materialName.c_str());
    return true;
}

std::string SimulationLoop::commitInspectorEdit(std::string material, std::string key,
                                                std::string value) {
    if (m_carDir.empty()) return "no car loaded: nothing to edit";
    if (!m_inspectedValid || m_inspectedMesh.empty()) return "nothing selected";
    const std::string mesh = m_inspectedMesh;
    std::string error;
    if (!setModelMaterialEntry(m_carDir, material, key, value, &error)) return error;
    std::fprintf(stderr, "[inspector] %s.%s = %s written to %s\n", material.c_str(),
                 key.c_str(), value.c_str(), modelMaterialsPath(m_carDir).c_str());
    // The meshes are about to be replaced, so the selection goes first and
    // comes back by name right after: same part, new values, wash intact.
    if (m_carLoaded) ensureCarVisual(m_carDir);
    if (!selectShowroomMesh(mesh)) return material + "." + key + " = " + value + " (saved)";
    return material + "." + key + " = " + value + " (saved, car reloaded)";
}

void SimulationLoop::clearShowroomSelection() {
    if (m_inspectedValid && m_vulkanRenderer != nullptr && !m_inspectedMesh.empty()) {
        // The surface's own tint and coat go back as they were, through the same
        // deferred path the wash used.
        ShowroomTintRequest restore;
        restore.mesh = m_inspectedMesh;
        for (int i = 0; i < 3; ++i) restore.tint[i] = m_inspectedTint[i];
        restore.paintSolid = m_inspectedPaintSolid;
        m_pendingTints.push_back(std::move(restore));
    }
    m_inspectedValid = false;
    m_inspectedMesh.clear();
    m_inspectedOriginal = NativeMesh{};
    m_ui.inspector().clear();
}

// Applies the tint requests the clicks queued. It runs at the top of render(),
// after the previous frame's fence and before the draw list is built, so a
// mesh's buffers are never replaced while a submitted frame reads them.
void SimulationLoop::applyPendingTints() {
    if (m_pendingTints.empty()) return;
    if (m_vulkanRenderer != nullptr) {
        for (const ShowroomTintRequest& request : m_pendingTints) {
            // In place, through the host-visible material block: replacing the
            // mesh's buffers here is what killed the driver before.
            m_vulkanRenderer->setMeshTint(request.mesh, request.tint, request.paintSolid);
        }
    }
    m_pendingTints.clear();
}

void SimulationLoop::refreshShowroomInspector() {
    if (!m_inspectedValid) return;
    InspectorMaterialInfo info;
    info.shader = m_inspectedOriginal.shaderName;
    info.albedo = m_inspectedOriginal.albedoTexture;
    info.normal = m_inspectedOriginal.normalTexture;
    info.detail = m_inspectedOriginal.detailTexture;
    info.emissiveMap = m_inspectedOriginal.emissiveTexture;
    info.roughness = m_inspectedOriginal.roughness;
    info.metalness = m_inspectedOriginal.metalness;
    info.clearcoat = m_inspectedOriginal.clearcoat;
    info.specular = m_inspectedOriginal.specular;
    info.fresnelC = m_inspectedOriginal.fresnelC;
    info.fresnelExp = m_inspectedOriginal.fresnelExp;
    info.fresnelMax = m_inspectedOriginal.fresnelMax;
    info.emissive = m_inspectedOriginal.emissive;
    info.alphaRef = m_inspectedOriginal.alphaRef;
    info.alphaTested = m_inspectedOriginal.alphaTested;
    info.blended = m_inspectedOriginal.blended;
    info.glass = m_inspectedOriginal.glassiness > 0.f;
    for (int i = 0; i < 3; ++i) info.tint[i] = m_inspectedOriginal.tint[i];

    // A bake keeps no separate material name: the node name is the key into
    // materials.txt, which is what the mesh was built with.
    const std::string material = m_inspectedOriginal.materialName.empty()
                                     ? m_inspectedMesh
                                     : m_inspectedOriginal.materialName;
    std::string footer = "click another part to inspect it";
    if (!m_carDir.empty()) {
        // The car's own model.json entry for this material, if it has one: the
        // same block the console's MAT verbs write.
        std::vector<ModelMaterial> entries;
        if (readModelMaterials(m_carDir, entries, nullptr)) {
            const std::string wanted = model_materials_detail::lowerText(material);
            for (const ModelMaterial& entry : entries) {
                if (model_materials_detail::lowerText(entry.name) != wanted) continue;
                footer = "model.json: " +
                         (entry.shader.empty() ? std::string("shader inherited") : entry.shader) +
                         ", " + std::to_string(entry.properties.size()) + " propert(y/ies)";
                break;
            }
        }
    }
    m_ui.inspector().show(m_inspectedMesh, material, inspectorPanelRows(info), footer);
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
        m_showroomHallLoading = true;
        if (loadTrackFolder(cfg.directory)) {
            m_showroomHallLoading = false;
            m_trackData = SimTrackData{};
            m_trackLoaded = false;
            m_ui.menu().setTrackName(previousLabel);
            // The hall load swapped the static scene: the car has to be put
            // back on it.
            if (m_carLoaded && !m_carDir.empty()) ensureCarVisual(m_carDir);
            std::printf("[showroom] hall %s loaded\n", cfg.directory.c_str());
            return;
        }
        m_showroomHallLoading = false;
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
                                   float outMin[3], float outMax[3], int* outMeshes, int* outTextured,
                                   std::vector<WorldPose>* outPitMarkers) {
    if (!m_vulkanRenderer) return false;
    namespace ff = ::ks::engine::fileformat;
    // A track is read through this same reader, but the car-only halves (skin
    // folder, livery swatch, baked blur stand-ins) must not run for a venue: a
    // track folder can ship a skins/ directory of its own.
    const bool carModel = prefix.rfind("car_model_", 0) == 0;
    // The location document applies to a track .kn5 read in memory, and only
    // when KS_LOCATION_MATERIALS says so: a car's surfaces are the car
    // presets' business, and a venue that came through the baker is handled
    // where its bake is loaded (NativeRenderer::loadMeshesFromManifest).
    graphics::LocationMaterialApplier location;
    bool locationOn = !carModel && prefix.rfind("track_", 0) == 0 &&
                      graphics::locationMaterialsEnabled();
    if (locationOn) {
        std::string locationError;
        std::vector<std::string> tried;
        const std::string document =
            findLocationMaterialDocument(executableDirectory(), &tried);
        if (document.empty()) {
            std::fprintf(stderr, "[location] %s; track presets stay off\n",
                         locationMaterialNotFoundMessage(tried).c_str());
            locationOn = false;
        } else {
            locationOn = location.load(document, &locationError);
            if (!locationOn)
                std::fprintf(stderr, "[location] %s; track presets stay off\n",
                             locationError.c_str());
        }
    }
    // The car's import parameters (model.json's kn5 block): its texture set, and
    // the scale/rotation it is imported with — the job FBXImportSettings does
    // for an .fbx. A venue read through this same reader has no such block.
    sim::Kn5ModelParameters kn5Record;
    if (carModel)
        sim::readKn5Parameters(std::filesystem::path(modelPath).parent_path().string(), kn5Record,
                               nullptr);
    sim::Kn5ImportTransform importTransform;
    importTransform.scale = kn5Record.scale;
    for (int axis = 0; axis < 3; ++axis)
        importTransform.rotationDeg[axis] = kn5Record.rotationDeg[axis];
    if (!importTransform.identity()) {
        std::fprintf(stderr,
                     "[kn5] import transform: scale %.3f, rotation %.1f %.1f %.1f deg "
                     "(model.json kn5)",
                     static_cast<double>(importTransform.scale),
                     static_cast<double>(importTransform.rotationDeg[0]),
                     static_cast<double>(importTransform.rotationDeg[1]),
                     static_cast<double>(importTransform.rotationDeg[2]));
        std::putchar(10);
    }
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
    int tinted = 0;
    int metalSurfaces = 0;
    int cutouts = 0;
    int seeThrough = 0;
    int selfLit = 0;
    // The colour the bodywork actually took, for the [paint] line below: the
    // skin's own swatch and the car's declared list are different sources.
    float appliedPaint[3] = {1.0f, 1.0f, 1.0f};
    std::map<std::string, std::string> decodedTextures; // kn5 texture -> cache key

    // The nodes the content hides: the car's overrides name them and the
    // converter folds them into the car's documents, so the list is read where
    // the model is described. A venue ships no model document and hides nothing.
    // KS_SHOW_HIDDEN turns hiding off, which is the only way to see what a skin
    // actually removes.
    graphics::HiddenNodes hiddenNodes;
    int hiddenSkipped = 0;
    int pitBoxesHidden = 0;
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
    // The car's model document carries the content's own shader settings (the
    // converted [SHADER_REPLACEMENT_*] sections): MATERIALS, SHADER, PROP_*.
    // A venue has none, and its materials then keep their authored response.
    const graphics::ShaderSettings shaderSettings =
        graphics::ShaderSettings::readModelDocument(modelPath);
    int overridden = 0;
    // KS_MATERIAL_DEBUG lists each material once with the preset it landed
    // on, which is the only way to see why a surface looks the way it does.
    const bool debugMaterials = std::getenv("KS_MATERIAL_DEBUG") != nullptr;
    // Diagnostic: paint each mesh a flat colour derived from its material name
    // and log the name with that colour, so a screenshot names the surface.
    const bool debugMaterialIds = std::getenv("KS_MATERIAL_IDS") != nullptr;
    std::set<std::string> printedMaterialIds;
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
        // The skin is part of the car's own record now (model.json's kn5.skin),
        // the way an .fbx import keeps its texture folder: the environment stays
        // the one-off override, the document is what the car loads with.
        const std::string carFolder = carDir.string();
        if (wantedSkin.empty() && !kn5Record.skin.empty()) wantedSkin = kn5Record.skin;
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
        if (!carModel) skinDir.clear();
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
        // First load of a car with no kn5 block: record what it was built from,
        // so no later start has to guess. The model is always known here; the
        // skin is left empty when the folder ships none. An existing block is
        // never rewritten, and nothing is written for a venue.
        if (carModel) {
            sim::Kn5ModelParameters resolved = kn5Record;
            resolved.model = std::filesystem::path(modelPath).filename().string();
            if (!skinDir.empty())
                resolved.skin = std::filesystem::path(skinDir).filename().string();
            std::string why;
            if (sim::ensureKn5Parameters(carFolder, resolved, &why))
                std::fprintf(stderr, "[kn5] model.json: kn5 parameters recorded (%s%s%s)",
                             resolved.model.c_str(), resolved.skin.empty() ? "" : ", ",
                             resolved.skin.c_str());
            else if (!why.empty())
                std::fprintf(stderr, "[kn5] model.json: kn5 parameters not recorded (%s)",
                             why.c_str());
            std::putchar(10);
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

    // AC keeps a skin's paint colour in its livery.png swatch: a Kunos skin
    // such as the Countach's 0_rosso_siviglia_gold ships that file and no body
    // texture, so the neutral paint map inside the KN5 has to take the skin's
    // own colour or the car loads white.
    float skinPaint[3] = {0.0f, 0.0f, 0.0f};
    bool hasSkinPaint = false;
    if (!skinDir.empty()) {
        const std::vector<unsigned char> swatch = readSkinTexture("livery.png");
        if (!swatch.empty()) {
            ks::image::RawImage swatchImage;
            if (ks::image::decodePng(swatch.data(), swatch.size(), &swatchImage) &&
                graphics::skinColour(swatchImage.rgba.data(), swatchImage.width,
                                     swatchImage.height, skinPaint)) {
                hasSkinPaint = true;
                std::fprintf(stderr, "[paint] skin colour %.3f %.3f %.3f from livery.png",
                             skinPaint[0], skinPaint[1], skinPaint[2]);
                std::putchar(10);
            }
        }
    }

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

        std::vector<unsigned char> rgba;
        int width = 0;
        int height = 0;
        if (!bytes.empty()) {
            if (ff::isDds(bytes)) {
                const auto image = ff::decodeDds(bytes);
                if (!image.ok() || image.rgba.empty()) {
                    std::fprintf(stderr, "[kn5] texture %s: %s", wanted.c_str(),
                                 image.info.error.c_str());
                    std::putchar(10);
                } else {
                    rgba = image.rgba;
                    width = image.info.width;
                    height = image.info.height;
                }
            } else {
                // KN5 textures are not all DDS: this car's window panes are
                // Windows_D.png, and skipping them left the glass untextured
                // (white fallback, no alpha to blend).
                ks::image::RawImage png;
                std::string why;
                if (ks::image::decodePng(
                        reinterpret_cast<const std::uint8_t*>(bytes.data()),
                        bytes.size(), &png, &why)) {
                    rgba = std::move(png.rgba);
                    width = png.width;
                    height = png.height;
                } else if (!why.empty()) {
                    std::fprintf(stderr, "[kn5] texture %s: %s\n", wanted.c_str(),
                                 why.c_str());
                }
            }
        }
        if (!rgba.empty() && width > 0 && height > 0) {
                if (soften) {
                    for (int pass = 0; pass < 4; ++pass) halveImage(rgba, width, height);
                }
                // Diagnostic (KS_DUMP_TEXTURES): write the decoded map out so a
                // surface that renders wrong can be traced to the pixels the
                // engine actually sampled (alpha of a pane, an AO map used as
                // albedo, a fallback to white).
                if (const char* dumpDir = std::getenv("KS_DUMP_TEXTURES")) {
                    if (dumpDir[0] != '\0') {
                        std::string safe = canonical;
                        for (char& c : safe)
                            if (c == '/' || c == '\\' || c == ':') c = '_';
                        ks::image::RawImage dumped;
                        dumped.width = width;
                        dumped.height = height;
                        dumped.rgba = rgba;
                        const std::string out = std::string(dumpDir) + "/" + safe + ".png";
                        std::string why;
                        if (!ks::image::saveImageFile(out, dumped, &why))
                            std::fprintf(stderr, "[tex] dump %s failed: %s\n",
                                         out.c_str(), why.c_str());
                    }
                }
                if (m_vulkanRenderer->addTextureFromMemory(
                        cacheKey, rgba, width, height)) {
                    key = cacheKey;
                }
        }
        decodedTextures.emplace(wanted, key);
        return key;
    };
    // AC fakes wheel blur with a second, pre-blurred copy of the rim and the
    // tyre, swapped in above a speed threshold (data/blurred_objects.ini).
    // This engine knows the motion, so it blurs at runtime: the baked stand-in
    // mesh is never drawn and a baked blur texture is never sampled.
    int blurStandIns = 0;
    // AC ships the car's reflection probe as an equirect *_Ref map on the first
    // slot of a material (the Countach carries HDR_111_Parking_Lot_2_Ref.dds).
    // It is the environment the body and the chrome reflect, so it is
    // remembered here and never used as a surface colour.
    std::string reflectionProbe;
    auto stripBakedBlur = [](const std::string& name) -> std::string {
        std::string folded;
        folded.reserve(name.size());
        for (char c : name) {
            if (c >= 'A' && c <= 'Z') folded.push_back(static_cast<char>(c - 'A' + 'a'));
            else folded.push_back(c);
        }
        const std::size_t at = folded.find("blur");
        if (at == std::string::npos) return name;
        // "rim_blur" -> "rim", "Tyre_Blur_D" -> "Tyre_D": the sharp twin the
        // stand-in was baked from.
        std::string fixed = name.substr(0, at) + name.substr(at + 4);
        std::string collapsed;
        collapsed.reserve(fixed.size());
        for (char c : fixed) {
            if (c == '_' && (collapsed.empty() || collapsed.back() == '_')) continue;
            collapsed.push_back(c);
        }
        while (!collapsed.empty() && collapsed.back() == '_') collapsed.pop_back();
        const std::size_t dangling = collapsed.find("_.");
        if (dangling != std::string::npos) collapsed.erase(dangling, 1);
        std::printf("[blur] baked blur texture %s -> %s (engine blurs from motion)\n",
                    name.c_str(), collapsed.c_str());
        return collapsed;
    };

    float bboxMin[3] = {1e9f, 1e9f, 1e9f};
    float bboxMax[3] = {-1e9f, -1e9f, -1e9f};
    // Hall meshes are oriented in a second pass, once the whole model's bounds
    // are known: a flat mesh (the floor disc, a logo panel) has its centroid in
    // its own plane, so the old per-mesh facing test decided the flip from
    // rounding noise and could cull the whole surface from the inside view.
    struct DeferredHallMesh { std::string name; NativeMesh mesh; };
    std::vector<DeferredHallMesh> hallMeshes;
    // AC's pit markers are one small mesh per stall, AC_PIT_<n>: the centre of
    // that mesh is where the car stands in the stall. Collected by number, so
    // the order the tree happens to be walked in does not matter.
    std::map<int, WorldPose> pitMarkers;
    std::function<void(const ff::Kn5Node&)> walk = [&](const ff::Kn5Node& node) {
        // A hidden node takes its children with it: that is what hiding a group
        // in the editor means, and the driver's face sits under its own group.
        if (hiddenNodes.hides(node.name)) {
            ++hiddenSkipped;
            return;
        }
        if (outPitMarkers && node.has_mesh && node.mesh.positions.size() >= 3) {
            const std::string& name = node.mesh.name;
            if (name.rfind("AC_PIT_", 0) == 0) {
                int index = 0;
                for (std::size_t at = 7; at < name.size() && name[at] >= '0' && name[at] <= '9';
                     ++at)
                    index = index * 10 + (name[at] - '0');
                const std::size_t count = node.mesh.positions.size() / 3;
                double sum[3] = {0.0, 0.0, 0.0};
                for (std::size_t i = 0; i < count; ++i) {
                    float p[3];
                    transform(node.mesh.world, node.mesh.positions[i * 3],
                              node.mesh.positions[i * 3 + 1], node.mesh.positions[i * 3 + 2], 1.f,
                              p);
                    for (int k = 0; k < 3; ++k) sum[k] += p[k];
                }
                WorldPose pose;
                pose.x = static_cast<float>(sum[0] / static_cast<double>(count));
                pose.y = static_cast<float>(sum[1] / static_cast<double>(count));
                pose.z = static_cast<float>(sum[2] / static_cast<double>(count));
                pitMarkers[index] = pose;
            }
        }
        // The cube is a marker, not scenery: read above for the garage row, and
        // drawn only where the developer console asks to see it.
        if (!ks::sim::showsPitBoxMarkers() && node.has_mesh &&
            ks::sim::isPitBoxMarkerName(node.mesh.name)) {
            ++pitBoxesHidden;
            return;
        }
        const auto& m = node.mesh;
        if (node.node_class == ff::Kn5NodeClass::mesh && node.has_mesh &&
            !m.indices.empty() && m.positions.size() >= 3) {
            // A stand-in exists only to be swapped in by speed; drawing it
            // puts a frozen baked blur on a wheel that is standing still.
            if (carModel && (m.name.find("BLUR") != std::string::npos ||
                             m.name.find("blur") != std::string::npos)) {
                ++blurStandIns;
                std::printf("[blur] stand-in skipped: %s\n", m.name.c_str());
                return;
            }
            const std::size_t count = m.positions.size() / 3;
            NativeMesh nm;
            nm.vertices.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                NativeVertex& v = nm.vertices[i];
                float pos[3];
                transform(m.world, m.positions[i * 3], m.positions[i * 3 + 1],
                          m.positions[i * 3 + 2], 1.f, pos);
                // The car's import transform, applied here so the bounds below,
                // the lift onto the floor and every later view see the same
                // model. Identity for a venue and for a car that states none.
                importTransform.applyPosition(pos);
                v.px = pos[0];
                v.py = pos[1];
                v.pz = pos[2];
                if (m.normals.size() >= (i + 1) * 3) {
                    float n[3];
                    transform(m.world, m.normals[i * 3], m.normals[i * 3 + 1],
                              m.normals[i * 3 + 2], 0.f, n);
                    importTransform.applyDirection(n);
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
            // Diagnostic (KS_MESH_BOUNDS): where a mesh actually landed in
            // the scene. Geometry that is loaded but never seen can then be
            // told apart from geometry that was never loaded at all.
            if (std::getenv("KS_MESH_BOUNDS") != nullptr) {
                float lo[3] = {1e9f, 1e9f, 1e9f};
                float hi[3] = {-1e9f, -1e9f, -1e9f};
                for (const NativeVertex& v : nm.vertices) {
                    const float p[3] = {v.px, v.py, v.pz};
                    for (int k = 0; k < 3; ++k) {
                        lo[k] = std::min(lo[k], p[k]);
                        hi[k] = std::max(hi[k], p[k]);
                    }
                }
                std::string matName = "?";
                if (m.material_id >= 0 &&
                    m.material_id < static_cast<std::int32_t>(parsed.file.materials.size()))
                    matName = parsed.file.materials[static_cast<std::size_t>(m.material_id)].name;
                std::fprintf(stderr,
                             "[bounds] %-28s mesh=%-20s min=(%.2f %.2f %.2f) max=(%.2f %.2f %.2f)\n",
                             matName.c_str(), m.name.c_str(), static_cast<double>(lo[0]),
                             static_cast<double>(lo[1]), static_cast<double>(lo[2]),
                             static_cast<double>(hi[0]), static_cast<double>(hi[1]),
                             static_cast<double>(hi[2]));
            }
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

                // The model document's shader settings rewrite this material's
                // shader and properties before the response below reads them:
                // the content said what this surface is, so it is not a guess.
                const graphics::ShaderSetting* setting =
                    shaderSettings.find(material.name);
                ks::engine::fileformat::Kn5Material effective = material;
                if (setting != nullptr) setting->applyTo(effective);
                const ks::engine::fileformat::Kn5Material& response =
                    setting != nullptr ? effective : material;

                // Albedo: the mapping the file names as diffuse, else the
                // first one it lists. Slot 0 is not a reliable answer — on a
                // reflection shader it is the environment probe, and painting
                // `*_Ref` as diffuse is what turned the showroom floor into a
                // white speckled sheet instead of a surface.
                std::string albedo;
                std::string fallbackAlbedo;
                std::string normal;
                std::string detail;
                std::string emissiveTex;
                // Set when this surface IS the room's photograph: the hall of a
                // photographed showroom is a single shell over a picture of a
                // place, and that picture must not be shaded as geometry.
                bool photoSurface = false;
                for (const auto& mapping : material.mappings) {
                    if (mapping.texture.empty()) continue;
                    // The reflection probe is not a surface: painting it turns
                    // the part into a photograph of a parking lot, which is
                    // what happened to the Countach's default material.
                    if (graphics::isReflectionProbeTexture(mapping.texture)) {
                        if (reflectionProbe.empty()) reflectionProbe = mapping.texture;
                        // A hall whose only map is the probe is a photo of a
                        // place, and the probe mapping IS its surface: CSP's
                        // custom showrooms are exactly that (industrial.kn5 is
                        // one shell over HDR_111_Parking_Lot_Ref.dds). A car
                        // keeps the probe off the paint: there it is only the
                        // environment it reflects.
                        if (faceInside && albedo.empty()) {
                            albedo = mapping.texture;
                            m_hallIsPhoto = true;
                            photoSurface = true;
                        }
                        continue;
                    }
                    // The mapping NAME says the slot, but the texture itself can
                    // disqualify it: damage/scratch/dirt/dust maps are data for
                    // AC's weathering shaders (WCCARBODY names its damage map
                    // txNormal=damage.dds), and using one as the surface normal
                    // is what made a clean car look dented. The rule lives in
                    // MaterialResponse.h so it is testable.
                    // AC txEmissive: the map that says what a lamp emits. It
                    // is not a surface colour, so it must not fall through to
                    // the albedo fallback below.
                    if (lower(mapping.name).find("emissive") != std::string::npos) {
                        if (emissiveTex.empty()) emissiveTex = mapping.texture;
                        continue;
                    }
                    switch (graphics::textureRole(mapping.name, mapping.texture)) {
                        case graphics::TextureRole::normal:
                            normal = mapping.texture;
                            continue;
                        case graphics::TextureRole::diffuse:
                            albedo = mapping.texture;
                            continue;
                        case graphics::TextureRole::detail:
                            detail = mapping.texture;
                            continue;
                        case graphics::TextureRole::other:
                            break;
                    }
                    // Never promote a weathering or data map (specular, AO,
                    // normal) to the surface colour when no diffuse is named.
                    if (graphics::isWeatheringTexture(mapping.texture) ||
                        graphics::isDataTexture(mapping.texture))
                        continue;
                    if (fallbackAlbedo.empty()) fallbackAlbedo = mapping.texture;
                }
                if (albedo.empty()) albedo = fallbackAlbedo;
                // A baked blur map is one still frame of a spinning wheel: the
                // engine's own blur replaces it, so the sharp twin is sampled.
                albedo = stripBakedBlur(albedo);
                normal = stripBakedBlur(normal);
                detail = stripBakedBlur(detail);
                // "..._Ref" names an environment/reflection probe: it carries
                // the room reflected in a surface, not the surface's own colour.
                // It is also the IBL environment (see the probe upload below), so
                // it is never painted as albedo. A hall whose only diffuse was its
                // probe used to render as a white void: the probe lights the room
                // as irradiance, the surface gets a neutral colour further down.
                {
                    std::string base = albedo;
                    const std::size_t dot = base.find_last_of('.');
                    if (dot != std::string::npos) base.resize(dot);
                    const std::string folded = lower(base);
                    const bool probe = folded.size() >= 4 &&
                                       folded.compare(folded.size() - 4, 4, "_ref") == 0;
                    if (probe) {
                        // A car keeps its probe off the paint: there the probe is
                        // the environment it reflects. A hall built as a photo
                        // (CSP's custom showrooms: one mesh whose only diffuse is
                        // the parking-lot probe) has no other colour to show, and
                        // painting it is exactly what that room is.
                        if (!faceInside) {
                            albedo.clear();
                        } else {
                            m_hallIsPhoto = true;
                            photoSurface = true;
                        }
                    }
                }
                // A txDiffuse that is an occlusion bake carries no colour at
                // all (pure greyscale, alpha 0): the cockpit leather, carpet
                // and dash keep their colour in txDetail instead. Painting the
                // bake is what rendered the cabin as grey plastic, so the
                // colour map becomes the albedo and the bake stops doubling it.
                // OCC maps share the UV space of the diffuse, so the bake is
                // sampled untiled, and an occlusion mask is not the neutral 0.5
                // grain the shader doubles: its gain is 1.
                float detailTiles = 0.0f;
                if (graphics::isOcclusionTexture(albedo) && !detail.empty()) {
                    const std::string occlusion = albedo;
                    albedo = detail;
                    detail = occlusion;
                    detailTiles = 1.0f;
                    // The gain is also the signature the shaders read to know
                    // that the alpha test's map is this bake, not the wear layer
                    // that took its place in the albedo (alphaMaskFromDetail).
                    nm.detailGain = KS_MATERIAL_DETAIL_GAIN_BAKE;
                }
                // The soften (four box halvings) exists for a floor: a 40 m
                // surface reads as noise at full resolution, and its top mips
                // are the only ones that look like a floor. It must not touch
                // the hall's photograph: industrial's probe is 5120x2560, and
                // blurring it took the picture to 320x160 — a grey-green smear
                // stretched over the whole room, which is most of why the room
                // measured luminance 214 with a chromaticity of 0.04. The
                // photograph is shown as the file drew it.
                // Soften only large, flat, horizontal surfaces (floors/ceilings):
                // a 40 m floor reads as noise at full resolution, but applying
                // the 4x halving to every non-photo hall surface (walls, the
                // turntable, logo panels) blurs them into a 32x16 smear.
                bool soften = false;
                if (faceInside && !photoSurface && !nm.vertices.empty()) {
                    float minY = 1e9f, maxY = -1e9f;
                    float minX = 1e9f, maxX = -1e9f;
                    float minZ = 1e9f, maxZ = -1e9f;
                    float nySum = 0.f;
                    for (const NativeVertex& v : nm.vertices) {
                        if (v.py < minY) minY = v.py;
                        if (v.py > maxY) maxY = v.py;
                        if (v.px < minX) minX = v.px;
                        if (v.px > maxX) maxX = v.px;
                        if (v.pz < minZ) minZ = v.pz;
                        if (v.pz > maxZ) maxZ = v.pz;
                        nySum += v.ny;
                    }
                    const float yExt = maxY - minY;
                    const float xzExt = std::max(maxX - minX, maxZ - minZ);
                    const float meanNy = nySum / static_cast<float>(nm.vertices.size());
                    soften = (xzExt > 8.0f) && (yExt < 1.0f) && (std::fabs(meanNy) > 0.8f);
                }
                nm.albedoTexture = textureKey(albedo, soften);
                if (outTextured != nullptr && !nm.albedoTexture.empty()) ++*outTextured;
                nm.detailTexture = textureKey(detail, faceInside);
                if (!nm.detailTexture.empty()) {
                    // AC authors the tiling as detailUVMultiplier; ksDetailU
                    // was only ever a guess (rule in MaterialResponse.h).
                    nm.detailScale = detailTiles > 0.0f
                                          ? detailTiles
                                          : graphics::authoredDetailTiles(response);
                    if (nm.detailScale <= 0.0f) nm.detailScale = 1.0f;
                    // useDetail=0 means the map belongs to another shader: a
                    // spec/data map multiplied into the albedo looks like dirt.
                    if (!graphics::usesDetail(response)) {
                        nm.detailTexture.clear();
                        nm.detailScale = 0.0f;
                    }
                }
                // No diffuse authored: the surface keeps the colour the file
                // gives it (ksDiffuse) instead of rendering as flat white.
                if (!faceInside && albedo.empty()) {
                    float authored[3] = {1.f, 1.f, 1.f};
                    if (propertyVec3(response, "ksDiffuse", authored)) {
                        // ksDiffuse is AC's scalar multiplier, not a colour:
                        // (0.4, 0, 0) would paint the part dark red.
                        float factors[3] = {1.f, 1.f, 1.f};
                        graphics::diffuseFactors(authored, factors);
                        for (NativeVertex& v : nm.vertices) {
                            v.r = std::min(std::max(factors[0], 0.f), 1.f);
                            v.g = std::min(std::max(factors[1], 0.f), 1.f);
                            v.b = std::min(std::max(factors[2], 0.f), 1.f);
                        }
                    }
                }
                if (faceInside && albedo.empty()) {
                    // A hall with no colour map of its own (the usual case: its
                    // only diffuse is the reflection probe) is a concrete room,
                    // not a white one. The probe reaches it through the IBL, so
                    // the surface only needs a neutral albedo to receive it.
                    for (NativeVertex& v : nm.vertices) {
                        v.r = 0.42f;
                        v.g = 0.42f;
                        v.b = 0.42f;
                    }
                }
                if (!normal.empty() && normal != albedo) {
                    nm.normalTexture = textureKey(normal, false);
                    if (!nm.normalTexture.empty()) ++mappedNormal;
                }
                if (!emissiveTex.empty()) {
                    emissiveTex = stripBakedBlur(emissiveTex);
                    nm.emissiveTexture = textureKey(emissiveTex, false);
                }

                // The hall's photograph is shown, not shaded: the picture
                // already holds the room's light, and lighting it again is
                // what left the industrial hall bright, flat and colourless.
                // The room still uses its probe for everything else (the car's
                // paint reflects it), so only this surface changes.
                if (photoSurface) nm.unlit = 1.0f;

                // Surface response. Without this every surface of the car is a
                // 0.35-roughness dielectric, which is what made it read flat.
                const float exponent = propertyFloat(response, "ksSpecularEXP");
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
                // The model document's own shader settings decide the surface
                // before the name heuristics do: the content named this material.
                // An explicit per-car material override still beats both.
                graphics::MaterialPresetId presetId =
                    overrides.presetFor(response.shader, response.name);
                bool settingsDecidePreset = false;
                if (setting != nullptr &&
                    overrides.findEffective(material.name) == nullptr) {
                    if (setting->hasPreset) {
                        presetId = setting->preset;
                        settingsDecidePreset = true;
                    } else if (const auto fromShader =
                                   graphics::presetForShader(response.shader)) {
                        presetId = *fromShader;
                        settingsDecidePreset = true;
                    }
                }
                graphics::MaterialSurface surface =
                    overrides.resolve(response.shader, response.name, nm.roughness);
                if (settingsDecidePreset) {
                    surface = graphics::applyMaterialPreset(
                        graphics::materialPreset(presetId), nm.roughness);
                }
                nm.roughness = surface.roughness;
                nm.metalness = surface.metalness;
                nm.clearcoat = surface.clearcoat;
                // Cars only: the material database is the cars' domain, and
                // the hall/track resolution stays exactly as it was. A type
                // whose look states numbers overrides the preset's roughness
                // and metalness; a type whose look lives in template
                // properties (paint: ClearCoat*, glass: extIOR, carbon: the
                // detail texture) states none and therefore changes nothing.
                // The track's own vocabulary, when the switch is on: the bake
                // is not involved on this path, so the document is the only
                // thing that can say what a material of this venue is.
                if (locationOn) {
                    // Same rule as the baked path: a field with a texture is
                    // driven by that texture, and the scalar there is only the
                    // identity multiplier the pipeline reads with it.
                    const graphics::LocationMaterialApplier::Surface target =
                        graphics::presetWritableSurface(
                            !nm.roughnessTexture.empty(), !nm.metalnessTexture.empty(),
                            nm.roughness, nm.metalness, nm.clearcoat);
                    location.apply(material.name, target);
                }
                if (prefix.rfind("car_model_", 0) == 0) {
                    if (const graphics::MaterialLook* look =
                            graphics::MaterialDatabase::instance().lookFor(presetId,
                                                                          response.name)) {
                        if (look->hasRoughness) nm.roughness = look->roughness;
                        if (look->hasMetalness) nm.metalness = look->metalness;
                    }
                }
                // PBR the file itself authors, which outranks the database
                // above: pbSmoothness is the inverse of GGX alpha (the same
                // convention the database uses) and pbMetalness is the metal
                // term. These are our model's fields, read from whichever
                // carrier the content used — not an AC layer being emulated.
                float cspValue[3] = {0.f, 0.f, 0.f};
                if (propertyVec3(response, "pbSmoothness", cspValue))
                    nm.roughness =
                        std::min(std::max(1.0f - cspValue[0], 0.01f), 1.0f);
                if (propertyVec3(response, "pbMetalness", cspValue))
                    nm.metalness = std::min(std::max(cspValue[0], 0.0f), 1.0f);
                // Authored response the presets cannot know. ksSpecular is a
                // multiplier around the 0.5 AC default (0.5 keeps the legacy
                // 0.04 dielectric F0), and the fresnel triplet is what AC's
                // reflection shaders use to bring a pane in at grazing angles.
                const graphics::AuthoredSurface authored =
                    graphics::authoredSurface(response);
                nm.specular = authored.specular;
                nm.fresnelC = authored.fresnelC;
                nm.fresnelExp = authored.fresnelExp;
                nm.fresnelMax = authored.fresnelMax;
                nm.ksAmbient = authored.ksAmbient;
                // Computed before the diagnostic below so the line reports the
                // value the frame actually uses, txEmissive fallback included.
                const float emissiveValue = propertyFloat(response, "ksEmissive");
                if (emissiveValue > 0.0f) {
                    nm.emissive = std::min(emissiveValue, 8.0f);
                }
                // A txEmissive with no authored scalar is still a lit surface:
                // the map is the statement. Without this the lamp texture loads
                // and is then multiplied by zero.
                if (!nm.emissiveTexture.empty() && nm.emissive <= 0.0f)
                    nm.emissive = 1.0f;
                // What the showroom inspector reports for this mesh: the KN5
                // material it came from and the shader the content gave it.
                nm.materialName = material.name;
                nm.shaderName = response.shader;
                // A sky dome is a backdrop, not a lit surface: AC shows it with
                // its stored albedo (no sun shading), otherwise the inverted sun
                // blackens it. (showroom retune)
                {
                    std::string lowerName = lower(material.name);
                    std::string lowerAlbedo = lower(graphics::canonicalTextureName(nm.albedoTexture));
                    if (lowerName.find("sky") != std::string::npos ||
                        lowerAlbedo.find("sky") != std::string::npos)
                        nm.unlit = 1.0f;
                }
                if (overrides.findEffective(material.name) != nullptr) ++overridden;
                if (debugMaterials && printedMaterials.insert(lower(material.name)).second) {
    const std::string albedoName = graphics::canonicalTextureName(albedo);
                    std::printf(
                        "[material] %-22s shader=%-26s preset=%-13s r=%.3f m=%.3f cc=%.3f spec=%.2f fres=%.2f/%.2f/%.2f albedo=%-24s detail=%-20s ds=%.2f gain=%.2f use=%d at=%d blend=%d ref=%.3f em=%.2f%s",
                        material.name.c_str(), response.shader.c_str(),
                        graphics::materialPreset(presetId).name, nm.roughness, nm.metalness, nm.clearcoat,
                        static_cast<double>(authored.specular),
                        static_cast<double>(authored.fresnelC),
                        static_cast<double>(authored.fresnelExp),
                        static_cast<double>(authored.fresnelMax),
                        albedo.empty() ? "-" : albedoName.c_str(),
                        detail.empty() ? "-" : detail.c_str(),
                        static_cast<double>(nm.detailScale),
                        static_cast<double>(nm.detailGain),
                        graphics::usesDetail(response) ? 1 : 0,
                        static_cast<int>(material.alpha_tested),
                        static_cast<int>(material.blend),
                        static_cast<double>(propertyFloat(response, "ksAlphaRef")),
                        static_cast<double>(nm.emissive),
                        overrides.findEffective(material.name) != nullptr ? " (override)" : "");
                    std::putchar(10);
                }
                if (presetId == graphics::MaterialPresetId::Paint) {
                    ++painted;
                    // The paint material's detail map is a grime pattern, not
                    // a weave: multiplying it into a livery only greys the body
                    // down. The colour on this surface is the car's own.
                    nm.detailTexture.clear();
                    nm.detailScale = 0.0f;
                    // A livery texture already carries the body colour: the
                    // factory colour must never multiply it, or a bright livery
                    // red turns into a dark maroon. The factory paint only lands
                    // on a body that shows the car's own texture; an explicit
                    // pick lands everywhere, and there it replaces the colour.
                    const bool liveryBody =
                        !skinDir.empty() && !readSkinTexture(albedo).empty();
                    // The skin's own colour paints the body map it shipped no
                    // texture for. A multiply keeps the map's black trim black,
                    // which a solid paint would cover. A colour declared by the
                    // car or picked by the player still wins below.
                    if (!liveryBody && hasSkinPaint && !m_carPaintExplicit) {
                        nm.tint[0] = skinPaint[0];
                        nm.tint[1] = skinPaint[1];
                        nm.tint[2] = skinPaint[2];
                        appliedPaint[0] = skinPaint[0];
                        appliedPaint[1] = skinPaint[1];
                        appliedPaint[2] = skinPaint[2];
                        nm.paintSolid = 0.0f;
                        ++tinted;
                    }
                    // No declared colours means no factory paint: tinting by a
                    // white default would only be the identity, while the log
                    // then claimed a repaint that never happened.
                    if ((!liveryBody && m_carPaintDeclared) || m_carPaintExplicit) {
                        nm.tint[0] = m_carPaint[0];
                        nm.tint[1] = m_carPaint[1];
                        nm.tint[2] = m_carPaint[2];
                        appliedPaint[0] = m_carPaint[0];
                        appliedPaint[1] = m_carPaint[1];
                        appliedPaint[2] = m_carPaint[2];
                        nm.paintSolid = liveryBody ? 1.0f : 0.0f;
                        ++tinted;
                    }
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
                    // The reference is the file's, and an unauthored one is 0:
                    // it cuts the texels the mask marks fully transparent and
                    // nothing else. The 0.5 this used to substitute shredded
                    // the car's soft alphas — see graphics::alphaReference.
                    nm.alphaTested = true;
                    nm.alphaRef = graphics::alphaReference(response);
                    ++cutouts;
                }
                if (material.blend != 0) {
                    nm.blended = true;
                    nm.alphaTested = false; // the pass blends, it does not cut
                    // Glass is a pane; a decal is not. The pane keeps a real
                    // opacity and reflects, the decal only blends its own alpha.
                    // A reflection shader that authored a fresnelMaxLevel is a
                    // pane too: the Benetton windscreen is ksPerPixelReflection
                    // with C/EXP/Max authored, and reading only the preset left
                    // it as a flat blended card.
                    const bool presetGlass =
                        presetId == graphics::MaterialPresetId::Glass;
                    const bool pane = graphics::isGlassPane(
                        response.shader, presetGlass, nm.fresnelMax, material.blend);
                    // A preset pane keeps the historical 0.30; an authored one
                    // keeps its texture alpha and lets the fresnel term decide.
                    nm.opacity = pane ? (presetGlass ? 0.30f : 1.0f) : 1.0f;
                    nm.glassiness = pane ? 1.0f : 0.0f;
                    // Diagnostic (KS_MATERIAL_DEBUG): a pane that never got the
                    // glass treatment is the difference between "the cabin is
                    // black" and "a lit card sits in front of the cabin".
                    if (debugMaterials)
                        std::fprintf(stderr, "[glass] %-24s pane=%d opacity=%.2f\n",
                                     material.name.c_str(), pane ? 1 : 0,
                                     static_cast<double>(nm.opacity));
                    nm.castShadow = false; // a pane casts no useful shadow
                    ++seeThrough;
                }
                if (nm.emissive > 0.0f) ++selfLit;
                // Diagnostic (KS_MATERIAL_IDS): one flat colour per material so
                // a screenshot names the surface that carries an artifact.
                if (debugMaterialIds) {
                    std::uint32_t hash = 2166136261u;
                    for (unsigned char c : material.name) {
                        hash ^= c;
                        hash *= 16777619u;
                    }
                    const unsigned r = (hash >> 0) & 15u;
                    const unsigned g = (hash >> 4) & 15u;
                    const unsigned b = (hash >> 8) & 15u;
                    nm.tint[0] = static_cast<float>(r) / 15.0f;
                    nm.tint[1] = static_cast<float>(g) / 15.0f;
                    nm.tint[2] = static_cast<float>(b) / 15.0f;
                    nm.paintSolid = 1.0f;
                    if (printedMaterialIds.insert(lower(material.name)).second) {
                        std::fprintf(stderr, "[matid] %-22s %u %u %u\n",
                                     material.name.c_str(), r, g, b);
                    }
                }
            }

            // A room is looked at from inside it: faces that point away from the
            // middle would be culled and the room would vanish. A car is looked at
            // from outside, so it keeps the winding its author gave it.
            // The orientation test is deferred to the end of the load (see the
            // second pass after the root-node loop): a flat mesh (the floor disc,
            // a logo panel) has its centroid in its own plane, so testing it
            // against its own centroid decides the flip from rounding noise and
            // can cull the whole surface from the inside view.
            const std::string name = prefix + std::to_string(meshes);
            for (const NativeVertex& v : nm.vertices) {
                const float pos[3] = {v.px, v.py, v.pz};
                for (int axis = 0; axis < 3; ++axis) {
                    if (pos[axis] < bboxMin[axis]) bboxMin[axis] = pos[axis];
                    if (pos[axis] > bboxMax[axis]) bboxMax[axis] = pos[axis];
                }
            }
            if (faceInside) {
                hallMeshes.push_back({name, std::move(nm)});
            } else {
                m_vulkanRenderer->setMesh(name, nm);
                const ks::ecs::Entity entity = scene().create();
                if (entity != ks::ecs::kNullEntity) {
                    scene().emplace<ks::ecs::Name>(entity, ks::ecs::Name{name});
                    scene().emplace<ks::ecs::Transform>(entity);
                    scene().emplace<ks::ecs::MeshInstance>(entity, ks::ecs::MeshInstance{name});
                    if (entities) entities->push_back(entity);
                }
            }
            ++meshes;
        }
        for (const auto& child : node.children) walk(child);
    };
    for (const auto& node : parsed.file.nodes) walk(node);

    // Second pass for a room viewed from inside: orient each mesh so its
    // faces point into the room. The reference point is the whole model's
    // centre (a point strictly inside the room), not the mesh's own centroid
    // — a flat mesh has its centroid in its own plane, which made the old
    // per-mesh test decide the flip from rounding noise and could cull the
    // whole floor (or every logo panel) from the inside view.
    if (faceInside && !hallMeshes.empty()) {
        const float centre[3] = {
            (bboxMin[0] + bboxMax[0]) * 0.5f,
            (bboxMin[1] + bboxMax[1]) * 0.5f,
            (bboxMin[2] + bboxMax[2]) * 0.5f};
        for (auto& deferred : hallMeshes) {
            NativeMesh& nm = deferred.mesh;
            // A sky dome is a shell seen from inside: keep its authored
            // winding (do NOT flip), otherwise the inside becomes a back face
            // and the dome vanishes, leaving the background.
            const bool isSky = lower(nm.materialName).find("sky") != std::string::npos ||
                               lower(graphics::canonicalTextureName(nm.albedoTexture)).find("sky") !=
                                   std::string::npos;
            float facing = 0.f;
            for (const NativeVertex& v : nm.vertices)
                facing += v.nx * (v.px - centre[0]) + v.ny * (v.py - centre[1]) +
                          v.nz * (v.pz - centre[2]);
            if (!isSky && facing > 0.f) {
                for (std::size_t k = 0; k + 2 < nm.indices.size(); k += 3)
                    std::swap(nm.indices[k + 1], nm.indices[k + 2]);
                for (NativeVertex& v : nm.vertices) {
                    v.nx = -v.nx;
                    v.ny = -v.ny;
                    v.nz = -v.nz;
                }
            }
            m_vulkanRenderer->setMesh(deferred.name, nm);
            const ks::ecs::Entity entity = scene().create();
            if (entity != ks::ecs::kNullEntity) {
                scene().emplace<ks::ecs::Name>(entity, ks::ecs::Name{deferred.name});
                scene().emplace<ks::ecs::Transform>(entity);
                scene().emplace<ks::ecs::MeshInstance>(entity, ks::ecs::MeshInstance{deferred.name});
                if (entities) entities->push_back(entity);
            }
        }
        hallMeshes.clear();
    }

    // The car's own reflection probe becomes the IBL environment: that is the
    // environment AC reflects on the body, the chrome and the glass, so the
    // prefiltered and irradiance maps are rebuilt from it instead of the
    // procedural gradient sky. Runs before the first frame of the showroom,
    // which is why the one-shot upload can reuse the idle command buffer.
    // A venue with no surface colour is refused below, and its probe belongs to
    // it: keeping the built-in room means keeping the sky as its environment.
    const bool venueWithoutSurface = faceInside && outTextured != nullptr && *outTextured == 0;
    if (!reflectionProbe.empty() && m_vulkanRenderer && !venueWithoutSurface) {
        std::vector<unsigned char> probeBytes;
        for (const auto& texture : parsed.file.textures) {
            if (texture.name != reflectionProbe || texture.data.empty()) continue;
            probeBytes.assign(texture.data.begin(), texture.data.end());
            break;
        }
        bool applied = false;
        if (!probeBytes.empty()) {
            const std::string_view probeView(
                reinterpret_cast<const char*>(probeBytes.data()), probeBytes.size());
            const auto probe = ff::decodeDds(probeView);
            if (probe.ok() && !probe.rgba.empty())
                applied = m_vulkanRenderer->setCarReflectionEnvironment(
                    probe.rgba, probe.info.width, probe.info.height);
        }
        if (applied) {
            std::printf("[ibl] reflection probe %s replaces the sky",
                        reflectionProbe.c_str());
            std::putchar(10);
        } else {
            std::fprintf(stderr, "[ibl] reflection probe %s unusable, sky kept\n",
                         reflectionProbe.c_str());
        }
    }

    if (outPitMarkers != nullptr) {
        outPitMarkers->clear();
        for (const auto& entry : pitMarkers) outPitMarkers->push_back(entry.second);
    }
    if (meshes == 0) return false;
    if (tinted > 0) {
        // Runtime proof that the paint reached the bodywork, not just the file.
        std::printf("[paint] body %d mesh(es) tinted %.3f %.3f %.3f%s\n", tinted,
                    appliedPaint[0], appliedPaint[1], appliedPaint[2],
                    m_carPaintExplicit ? " (solid)" : "");
        std::putchar(10);
    } else if (painted > 0) {
        // Say which of the two it is: a livery already carries the body colour,
        // while a car with no colour list has no factory paint to apply at all.
        std::printf("[paint] %d painted mesh(es): %s\n", painted,
                    (m_carPaintDeclared || hasSkinPaint)
                        ? "livery owns them, paint not applied"
                                       : "no factory colour declared, left as authored");
        std::putchar(10);
    }
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
        if (pitBoxesHidden > 0) {
            std::printf("  pit box cubes: %d hidden (console mode shows them)",
                        pitBoxesHidden);
            std::putchar(10);
        }
    if (outMin && outMax) {
        for (int axis = 0; axis < 3; ++axis) {
            outMin[axis] = bboxMin[axis];
            outMax[axis] = bboxMax[axis];
        }
    }
    if (locationOn) {
        const graphics::LocationMaterialCounts& counts = location.counts();
        std::fprintf(stderr,
                     "[location] %d of %d track material(s) classified (%d unnamed by any "
                     "pattern), %d preset(s) applied\n",
                     counts.classified, counts.seen, counts.other, counts.applied);
        for (const auto& entry : counts.perCategory)
            std::fprintf(stderr, "  %-9s %d mesh(es)\n", entry.first.c_str(), entry.second);
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
    int textured = 0;
    m_hallIsPhoto = false; // set by loadKn5Meshes when a hall surface is a probe
    if (!loadKn5Meshes(model.string(), "showroom_hall_", true, nullptr, mn, mx, &meshes, &textured)) {
        std::fprintf(stderr, "[showroom] %s: no mesh in %s\n", cfg.id.c_str(),
                     model.filename().string().c_str());
        return false;
    }

    // A hall with no map of any kind (not even a reflection probe) has nothing
    // to show: the room built in memory reads as a room, so it stays. A hall
    // whose only map is its probe is a photo of a place (see loadKn5Meshes) and
    // is kept - that is the CSP custom showroom.
    if (textured == 0) {
        std::fprintf(stderr,
                     "[showroom] %s: %s carries no surface colour, built-in room kept\n",
                     cfg.id.c_str(), model.filename().string().c_str());
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
    // The car material database (system/cfg/carmaterial.json) is read once for
    // the whole process: it says what each material type looks like, and the
    // surfaces below consult it. A missing document is not an error — every
    // surface then keeps the preset its name resolved to.
    static const bool kMaterialDbReady = [] {
        graphics::MaterialDatabase& database = graphics::MaterialDatabase::instance();
        if (database.loadDefault()) {
            std::fprintf(stderr,
                         "[material-db] %zu of %zu type(s) state numbers "
                         "(system/cfg/carmaterial.json)\n",
                         database.categories(), database.documentedTypes());
            return true;
        }
        std::fprintf(stderr, "[material-db] %s\n", database.lastError().c_str());
        return false;
    }();
    (void)kMaterialDbReady;
    if (!m_vulkanRenderer || carDirectory.empty()) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    // The car's own model, as its model document names it: the FILE of the
    // closest LOD (the smallest IN) that exists, read from model.json the
    // same way every other model document fact is read. A car without a
    // document, or one whose lods name no file on disk, keeps the size
    // heuristic below.
    fs::path model;
    // The car's own record first (model.json's kn5 block): the file a previous
    // load recorded, or the one CONTENT set. The lods table below is the
    // content's answer only while the car states no model of its own, and the
    // size heuristic answers only when there is no table either.
    {
        sim::Kn5ModelParameters recorded;
        if (sim::readKn5Parameters(carDirectory, recorded, nullptr) &&
            !recorded.model.empty()) {
            const fs::path named = fs::path(carDirectory) / recorded.model;
            if (fs::is_regular_file(named, ec)) {
                model = named;
                std::fprintf(stderr, "[kn5] car model: %s (model.json kn5)\n",
                             model.filename().string().c_str());
            }
        }
    }
    if (model.empty()) {
        const std::string fromDocument = sim::modelKn5FromDocument(carDirectory);
        if (!fromDocument.empty()) {
            model = fromDocument;
            std::fprintf(stderr, "[kn5] car model: %s (model.json lods)\n",
                         model.filename().string().c_str());
        }
    }
    if (model.empty()) {
    // The car's own model: the file named like its folder, otherwise the largest
    // .kn5 that is not one of the distance copies.
    // The car's own model: the file named like its folder, otherwise the biggest
    // one. A LOD copy is only skipped when something else is available — some
    // folders ship nothing but LODs, and a LOD is still the car.
    const std::string stem = fs::path(carDirectory).filename().string();
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
    } // no model.json lods: the size heuristic above picked the file

    float mn[3] = {0.f, 0.f, 0.f};
    float mx[3] = {0.f, 0.f, 0.f};
    int meshes = 0;
    if (!loadKn5Meshes(model.string(), "car_model_", false, &m_carVisualEntities, mn, mx,
                       &meshes))
        return false;
    // The car stands on the floor it is shown on: a KN5's origin is not
    // guaranteed to be the contact patch (the Lotus' wheels reach below y=0),
    // which is why the model sank into the showroom ground. It is lifted by
    // its own lowest vertex, and only when it is actually off — a file already
    // standing on the floor is left alone. A driving car is placed by its
    // physics pose instead, which overwrites these transforms every frame.
    const float groundLift = mn[1] < -0.001f ? -mn[1] : 0.0f;
    if (groundLift > 0.0f) {
        for (const ks::ecs::Entity entity : m_carVisualEntities)
            if (auto* transform = scene().tryGet<ks::ecs::Transform>(entity))
                transform->position.y += groundLift;
    }
    std::fprintf(stderr, "[scene] car ground: bounds y[%.3f..%.3f], lift %.3f m\n",
                 static_cast<double>(mn[1]), static_cast<double>(mx[1]),
                 static_cast<double>(groundLift));
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

void SimulationLoop::spawnTrackDynamicLights(const std::string& layoutFolder,
                                             const std::string& trackDirectory) {
    const std::string lightsJson =
        ks::sim::TrackDynamicLights::jsonPathFor(layoutFolder, trackDirectory);
    // schedulazione luci: stesso file data.json, sezione "schedule"
    if (!lightsJson.empty()) {
        m_lightSchedule.setConfig(ks::sim::LightScheduleConfig::load(lightsJson));
    } else {
        m_lightSchedule.setConfig(ks::sim::LightScheduleConfig{});
    }
    const float factor = m_lightSchedule.intensityAt(m_timeOfDay);
    std::vector<ks::sim::LocalLight> lamps =
        lightsJson.empty() ? std::vector<ks::sim::LocalLight>{}
                           : ks::sim::TrackDynamicLights::load(lightsJson);
    if (factor < 0.999f) {
        for (auto& l : lamps) l.intensity *= factor;
    }
    if (m_vulkanRenderer) m_vulkanRenderer->setLocalLights(lamps);
    if (!lamps.empty()) {
        std::fprintf(stderr, "[track-lights] %s: %zu lampade da %s (sched %.2f @ %.1fh)\n",
                     m_trackData.layoutId.empty() ? "default" : m_trackData.layoutId.c_str(),
                     lamps.size(), lightsJson.c_str(), factor, m_timeOfDay);
    }
    if (m_lightSchedule.present()) {
        std::fprintf(stderr, "[light-schedule] sunrise %.1f sunset %.1f -> %.2f @ %.1fh\n",
                     m_lightSchedule.config().sunriseHour, m_lightSchedule.config().sunsetHour,
                     factor, m_timeOfDay);
    }
}

// The hall stopped being the scene: the two spawners below are the only places
// that put a track's visuals on screen in its place, and they cannot tell (the
// public loaders do) whether the room's light is still answering for it. Leaving
// the sun and the lamps set is how a car ended up lit by the last room it was
// previewed in.
void SimulationLoop::releaseShowroomScene() {
    const bool hadRoom = m_showroomHallInScene || m_roomSunValid;
    m_showroomHallInScene = false;
    m_roomSunValid = false;
    m_roomSunDeclared = false;
    m_roomLamps.clear();
    // The hall load goes through loadTrackFolder() with this set: it is the
    // room arriving, not the room leaving, and saying otherwise in the log
    // would be the one thing a debug run cannot afford.
    m_roomDisplayLogged = false;
    // A photo hall installed its own probe as the environment, and the track's
    // sky cannot be baked over it: give the environment back before the track
    // is drawn, or the circuit drives under the room's reflections.
    if (m_vulkanRenderer) m_vulkanRenderer->releaseProbeEnvironment();
    // The room's [COLOR] block was applied to the display pass and nothing else
    // put the track's grade back: the circuit drove on the room's saturation
    // and sepia (measured: hue 10 / sat 0.70 / sepia 0.10 leaking into the
    // session, the last 7.6/765 of difference between two rooms).
    if (m_roomGradeApplied && m_roomGradeSaved) writeDisplayGrade(m_roomGradeWas);
    m_roomGradeApplied = false;
    m_roomGradeSaved = false;
    if (hadRoom && !m_showroomHallLoading)
        std::printf("[showroom] the hall left the scene: sun, lamps, fog, exposure, IBL and "
                    "[COLOR] grade are the track's again\n");
}

void SimulationLoop::closeShowroom() {
    // Capture before close(): whether a room was actually open decides what
    // "leaving the room" means below. startSession() calls this defensively
    // even when no room is open — the track's own lamps ([DYNAMIC_LIGHTS])
    // are already in place there, and wiping them was the bug.
    const bool roomWasOpen = m_showroom.active() || m_showroomHallLoading;
    clearShowroomSelection();
    m_showroom.close();
    // Back to the sky IBL for the track (the room turned it off).
    if (m_vulkanRenderer) m_vulkanRenderer->setIblEnabled(m_showroomIblWasOn);
    // Leaving the room leaves its lamps behind — and the track that follows
    // gets its own lights back: the session drives on the track's
    // [DYNAMIC_LIGHTS], not on the empty frame the room left.
    if (m_vulkanRenderer && roomWasOpen) {
        m_vulkanRenderer->setLocalLights({});
        if (!m_showroomHallLoading && m_trackLoaded && !m_trackData.directory.empty())
            spawnTrackDynamicLights(m_trackData.layoutFolder, m_trackData.directory);
    }
    m_showroomIblSaved = false;
    m_showroomSkyBaked = false; // a room opened after this bakes its sun again
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
namespace {
// Defined with the baked-scene helpers further down: the centre of one of the
// AC_PIT_n markers a bake keeps, in world space.
bool markerCentre(const std::string& baked, const char* name, vec3& out);

/** Each stall faces the way the pit lane runs where it stands, so a car in it
 *  leaves the way the real one does. Without a lane the row itself is the
 *  direction, which at least keeps the cars parallel. */
void faceStallsDownTheLane(std::vector<WorldPose>& stalls, const std::vector<WorldPose>& lane) {
    if (!lane.empty()) {
        for (WorldPose& stall : stalls) {
            std::size_t best = 0;
            float bestDistance = 3.4e38f;
            for (std::size_t i = 0; i < lane.size(); ++i) {
                const float dx = lane[i].x - stall.x;
                const float dz = lane[i].z - stall.z;
                const float distance = dx * dx + dz * dz;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = i;
                }
            }
            stall.heading = lane[best].heading;
        }
        return;
    }
    for (std::size_t i = 0; i < stalls.size(); ++i) {
        const WorldPose& a = stalls[i > 0 ? i - 1 : i];
        const WorldPose& b = stalls[i + 1 < stalls.size() ? i + 1 : i];
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        if (dx * dx + dz * dz > 0.01f) stalls[i].heading = std::atan2(dx, dz);
    }
}
} // namespace

// The venue's own boxes, in the order a driver would look for them: the ones
// its author placed (pit_boxes.ini), then AC's own per-stall markers
// (AC_PIT_0..N, in the venue's .kn5 or in its bake), then the pit lane centre
// line with a box every 6 m. A track that has none of the three keeps the
// linear row the caller invents beside the start line.
bool SimulationLoop::garageRowFromTrack(int boxCount, GarageLayout& out) {
    if (m_trackData.directory.empty()) return false;
    boxCount = std::clamp(boxCount, 1, 32);

    std::vector<std::string> layoutFolders;
    for (const TrackLayoutInfo& layout : m_layouts.layouts())
        layoutFolders.push_back(layout.folder);
    const std::vector<WorldPose> lane = trackPitLane(m_trackData.directory, layoutFolders);

    // 1) The boxes the track's author placed by hand: their own positions and
    //    headings, which is exactly what a team's Garage= numbers name.
    GarageLayout authored;
    if (readPitBoxesIni(m_trackData.directory, authored)) {
        std::fprintf(stderr, "SimulationLoop: garage row from pit_boxes.ini (%d boxes)\n",
                     static_cast<int>(authored.boxes.size()));
        out = authored;
        return true;
    }

    // 2) AC's own pit markers: one small mesh per stall, so the car stands in
    //    the stall rather than on the lane's centre line. The venue's .kn5 is
    //    read in memory when it loads; a baked track carries .nmsh instead.
    std::vector<WorldPose> stalls = m_trackPitMarkers;
    std::string markerSource = "the .kn5";
    if (stalls.empty()) {
        const std::string baked = ks::sim::findBakedManifestDir(m_trackData.directory);
        if (!baked.empty()) {
            for (int n = 0; n < 32; ++n) {
                vec3 centre;
                const std::string name = "AC_PIT_" + std::to_string(n);
                if (!markerCentre(baked, name.c_str(), centre)) break;
                stalls.push_back(WorldPose{centre.x, centre.y, centre.z, 0.f});
            }
            markerSource = "the baked cache";
        }
    }
    if (stalls.size() >= 2) {
        faceStallsDownTheLane(stalls, lane);
        if (static_cast<int>(stalls.size()) > boxCount)
            stalls.resize(static_cast<std::size_t>(boxCount));
        GarageLayout markers;
        markers.pitLaneHeading = stalls.front().heading;
        for (std::size_t i = 0; i < stalls.size(); ++i) {
            GarageBox box;
            box.index = static_cast<int>(i);
            box.pose = stalls[i];
            markers.boxes.push_back(box);
        }
        std::fprintf(stderr, "SimulationLoop: garage row from %s (%d AC_PIT markers)\n",
                     markerSource.c_str(), static_cast<int>(markers.boxes.size()));
        if (static_cast<int>(markers.boxes.size()) < boxCount)
            std::fprintf(stderr,
                         "SimulationLoop: %d box(es) asked for, %d stall(s) on the venue: the "
                         "extra cars share the last one\n",
                         boxCount, static_cast<int>(markers.boxes.size()));
        out = markers;
        return true;
    }

    // 3) The pit lane itself: a box every 6 m along it, bowed with the lane,
    //    and never past the exit: the row is only as long as the lane allows.
    if (lane.size() < 2) return false;
    GarageLayout layout = rowAlongPitLane(lane, boxCount);
    if (layout.boxes.size() < 2) return false;
    out = layout;
    std::fprintf(stderr,
                 "SimulationLoop: garage row from the pit lane (%d boxes, %.0f m of lane)\n",
                 static_cast<int>(out.boxes.size()),
                 static_cast<double>(pit_lane_detail::lineLength(lane)));
    if (static_cast<int>(out.boxes.size()) < boxCount)
        std::fprintf(stderr,
                     "SimulationLoop: %d box(es) asked for, %d fit before the pit exit: the "
                     "extra cars share the last one\n",
                     boxCount, static_cast<int>(out.boxes.size()));
    return true;
}

void SimulationLoop::placePlayerForSession() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle || !m_multiCar) return;
    const FieldSlot* pslot = nullptr;
    for (const auto& s : m_sessionField)
        if (s.isPlayer) { pslot = &s; break; }
    const int slot = pslot ? pslot->gridSlot : 0;

    // The garage row comes first: a track with a pit lane but no grid line
    // still has a place to put the car, and its boxes are where the team's
    // garage indices point.
    int boxes = 20;
    for (const auto& teamSlot : m_team.slots)
        if (teamSlot.active) boxes = std::max(boxes, teamSlot.garageIndex + 1);
    GarageLayout laneRow;
    const bool haveLaneRow = garageRowFromTrack(boxes, laneRow);

    vec3 gpos;
    float gheading = 0.0f;
    const bool haveGrid = m_multiCar->gridPose(slot, 6.0f, gpos, gheading);
    if (!haveGrid) {
        // Without a grid line the pit lane is the only world anchor there is.
        if (!haveLaneRow) return; // no anchor at all: the legacy behaviour
        const WorldPose& box = laneRow.boxes.front().pose;
        gpos = vec3{box.x, box.y, box.z};
        gheading = box.heading;
        std::fprintf(stderr,
                     "SimulationLoop: no grid line on this track: the pit lane places "
                     "the car\n");
    }

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

    // The row the team's Garage= indices refer to: the track's own pit lane
    // when it has one, else the linear row invented beside the start line
    // (practice/qualy spawn no AI, so nothing else occupies that strip).
    GarageLayout assigned;
    if (haveLaneRow) {
        assigned = laneRow;
    } else {
        const WorldPose& first = gridPoses[0];
        WorldPose rowStart = first;
        rowStart.x += -std::cos(first.heading) * 12.f;
        rowStart.z += std::sin(first.heading) * 12.f;
        // makeLinearRow advances the boxes along pitHeading+90°: rotating the
        // heading by +90° makes the row recede upstream (parallel to the
        // track, cars facing it) instead of marching straight across the grid.
        assigned = GarageSpawnPolicy::makeLinearRow(
            boxes, rowStart, 6.f, first.heading + 1.5707963f);
    }
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
    // The step pins the body 0.35 above the ground it was placed on: without
    // this the spline height is forgotten on the first integration step and
    // the car sits through the track wherever the surface is not at y=0.
    m_vehicle->setGroundHeight(pose.y);
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
    // A headless run has no window, but the wheel and the pad are still attached:
    // polling them means a device at rest decides the steering of a run that only
    // asked for throttle, and every measured launch became a left-hand corner.
    // Windowed sessions keep the devices; headless keeps the keyboard (nothing
    // pressed) and whatever the test forces with KS_THROTTLE / KS_STEER.
    if (m_input) {
        const char* hl = std::getenv("KS_HEADLESS");
        const bool headlessRun = hl != nullptr && hl[0] != '\0' && std::strcmp(hl, "0") != 0;
        m_input->setDevicesEnabled(!headlessRun);
        if (headlessRun) std::printf("[input] headless: device axes not polled\n");
    }
    // A session owns the world, not the room: starting with the showroom open
    // (KS_AUTOSTART without KS_SESSION) left the turntable camera and the
    // room light over a running session, with the car placed by neither. The
    // menu path already closes the room in startSession before calling here.
    if (m_showroom.active()) closeShowroom();
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
    // The session telemetry file: one per session, from its first step.
    openSessionTelemetryCsv();
    // devMode capture aid: KS_LAP_REF=<ms> gives the timer a best lap to compare
    // with, so the delta bar can be captured before a real lap has been driven
    // (a session starting on an empty timer only has "no best lap" to show).
    // The maths is the widget's own; the first real lap replaces the reference
    // as soon as it is faster.
    if (const char* lapRef = std::getenv("KS_LAP_REF"); lapRef && lapRef[0]) {
        const int refMs = std::atoi(lapRef);
        if (refMs > 0 && !m_lapTimer.hasBestLap()) {
            m_lapTimer.seedBestLap(refMs);
            std::printf("[lap] KS_LAP_REF=%d ms -> delta bar reference\n", refMs);
        }
    }
    beginRaceSession();
#if HAS_VEHICLE_SIM
    // A car whose start block says BY=CREW is fired by the team. The garage-exit
    // phase is where that happens when the driver drives out of the box, but a
    // session that begins on the pit lane never passes through it - and a car
    // with an external starter has no key of its own - so the crew fires it here,
    // once, at a standstill. The ignition is the team's, not a driver press.
    if (m_vehicle && m_vehicle->engineStartConfig().startedByCrew &&
        !m_vehicle->isEngineRunning()) {
        m_vehicle->setIgnition(true);
        m_vehicle->setCrewStarterAvailable(true);
        // The crank takes the car's own CRANK_TIME, so the engine answers on the
        // steps that follow, not on this one.
        m_vehicle->pressStarterByCrew(0.0);
        std::printf("[engine] crew start: cranking\n");
    }
#endif
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
    // The session's telemetry file closes with it: the last rows are flushed and
    // the row count is what the driver (or a script) can check on disk.
    if (m_sessionCsv.isOpen()) {
        const long rows = m_sessionCsv.rowsWritten();
        std::printf("[TELEMETRY] %s: %ld rows\n", m_sessionCsv.path().c_str(), rows);
        m_sessionCsv.close();
    }
    m_telemetrySampler.reset();
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
    // The box belongs to the track this session runs on: placement below binds
    // it again, and a track without a garage row must not inherit the previous
    // track's coordinates.
    m_hasPlayerBox = false;
    // MODE SETUP owns the field's pace and aggression: they go into the tweak
    // set before start() spawns the grid, so the cars are built with them and
    // the loop's change callback only has to keep later spawns in step.
    {
        auto& tweaks = ks::engine::sim::GameplayTweaks::instance();
        ks::engine::sim::GameplayTweakSet set = tweaks.getTweakSet();
        const float difficulty = std::clamp(params.aiDifficulty, 0.8f, 1.2f);
        const float aggression = std::clamp(params.aiAggression, 0.0f, 1.0f);
        if (set.difficultyMultiplier != difficulty || set.aiAggression != aggression) {
            set.difficultyMultiplier = difficulty;
            set.aiAggression = aggression;
            tweaks.setTweakSet(set); // the callback re-applies it to the field
        }
    }
    start();
    // The manager owns the clock now; the HUD reads it back every frame.
    m_timeRemaining = static_cast<double>(m_sessionTimeLimitSeconds);
    // MODE SETUP's garage start: the pose policy already parks practice and
    // qualifying in the box, so a race is the one that arrives here on the
    // grid - and a garage start that left the car there was not one. The car
    // moves to its box (the one bound to the track just loaded), then the exit
    // machine holds it until the driver asks to leave.
    if (params.startInGarage && m_hasPlayerBox) {
#if HAS_VEHICLE_SIM
        if (m_vehicle) {
            auto& state = m_vehicle->state();
            state.position = {m_playerBox.x, m_playerBox.y, m_playerBox.z};
            state.heading = m_playerBox.heading;
            state.velocity = {};
            state.acceleration = {};
            state.angularVelocity = {};
            state.speed = 0;
            m_vehicle->setGroundHeight(m_playerBox.y);
            std::fprintf(stderr,
                         "SimulationLoop: session start in the pit box %.1f, %.1f, %.1f "
                         "heading %.2f\n",
                         m_playerBox.x, m_playerBox.y, m_playerBox.z, m_playerBox.heading);
        }
#endif
        m_garageExit.forceEnterGarage();
    } else if (params.startInGarage) {
        std::fprintf(stderr,
                     "SimulationLoop: garage start asked, but this track ships no pit "
                     "box: the car stays where the pose policy put it\n");
    }
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

// UDP 9666 'K'/'O' (goToSpawnPosition "PIT" nel reverse): l'auto torna al box
// con velocità annullate, come fa placePlayerForSession() allo spawn. Senza
// box (sessione senza garage row) ripiega sul reset sul posto.
void SimulationLoop::teleportPlayerToPit() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    if (!m_hasPlayerBox) {
        std::fprintf(stderr,
                     "SimulationLoop: UDP teleport-pit senza box: reset sul posto\n");
        recoverPlayerInPlace();
        return;
    }
    auto& state = m_vehicle->state();
    state.position = {m_playerBox.x, m_playerBox.y, m_playerBox.z};
    state.heading = m_playerBox.heading;
    m_vehicle->setGroundHeight(m_playerBox.y);
    state.velocity = {};
    state.acceleration = {};
    state.angularVelocity = {};
    state.speed = 0;
    m_garageExit.forceEnterGarage();
    std::fprintf(stderr,
                 "SimulationLoop: UDP teleport-pit: auto al box (%.1f, %.1f, %.1f)\n",
                 static_cast<double>(m_playerBox.x),
                 static_cast<double>(m_playerBox.y),
                 static_cast<double>(m_playerBox.z));
#endif
}

// UDP 9666 'P'/'L' (FUN_1400da280 nel reverse): reset meccanico completo
// dell'auto DOVE SI TROVA — VehicleSimulator::reset() riporta la posizione al
// default {0,0.35,0}, quindi la posa corrente viene salvata e ripristinata.
void SimulationLoop::recoverPlayerInPlace() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const ks::physics::PhysVec3 pos = m_vehicle->state().position;
    const float heading = m_vehicle->state().heading;
    m_vehicle->reset();
    auto& state = m_vehicle->state();
    state.position = pos;
    state.heading = heading;
    // reset() drops the ground back to 0: the recovered pose keeps its own.
    m_vehicle->setGroundHeight(static_cast<double>(pos.y) - 0.35);
    std::fprintf(stderr, "SimulationLoop: UDP recover: auto resettata sul posto\n");
#endif
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
    // P0.2 commit: the control API's AI PROMOTE runs the AC commit step.
    m_features.onPromoteAi = [this]() { return promoteAiCandidate(); };

    m_features.onSetupLoad = [this](const std::string& path) {
        SetupGarage* garage = setupGarage();
        if (!garage || !isSafeControlPath(path)) return;
        SetupData s = garage->setup();
        if (loadSetupFromFile(s, path)) {
            garage->setSetup(s);
            // A loaded setup is a setup like any other: the car gets it now,
            // not only when a row is moved afterwards.
            applySetupToVehicle(s);
            std::fprintf(stderr, "SimulationLoop: setup loaded from %s\n", path.c_str());
        }
    };

    m_features.onSetupSave = [this](const std::string& path) {
        if (!m_setupGarage || !isSafeControlPath(path)) return;
        if (saveSetupToFile(m_setupGarage->setup(), path))
            std::fprintf(stderr, "SimulationLoop: setup saved to %s\n", path.c_str());
    };

    // Spectator view: entering follows the first car on the grid, NEXT cycles
    // it, and updateCamera() rides whichever car is followed.
    m_features.onSpectate = [this](bool on) { return setSpectating(on); };
    m_features.onSpectateNext = [this]() { return spectateNext(); };
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

    // Comandi UDP 9666 (report §3.2). 'K' riporta l'auto 0 al box; 'O' con
    // indice 0 fa lo stesso, con un indice AI non c'è ancora una via per
    // spostare un'auto del MultiCarManager (nessuna API di riposizionamento),
    // quindi resta annunciato e ignorato. 'P'/'L' resettano, 'D' riavvia la
    // sessione come il reset del menu.
    m_features.onUdpTeleportPit = [this]() { teleportPlayerToPit(); };
    m_features.onUdpTeleportPitCar = [this](int idx) {
        if (idx <= 0) {
            teleportPlayerToPit();
            return;
        }
        std::fprintf(stderr,
                     "SimulationLoop: UDP teleport-pit car %d: non ancora "
                     "supportata (solo auto 0)\n", idx);
    };
    m_features.onUdpRecover = [this]() { recoverPlayerInPlace(); };
    m_features.onUdpRestart = [this]() {
        if (!m_running) start();
        reset();
        std::fprintf(stderr, "SimulationLoop: UDP 'D' — sessione riavviata\n");
    };
}

// Track limits need a shape, not just a phase: the hint says "off track" for
// everything that is not green, and nothing about a car 30 m wide of the road
// during a race. The loaded racing line is already the session's geometry (the
// grid spawns on it, the AI follows it), so it becomes the monitor's
// centreline and the configured width (TrackLimitsConfig::maxLateralM) is the
// corridor either side of it. No line, no corridor: detection stays on the
// phase hint alone, exactly as before a track with an ai/ folder is loaded.
void SimulationLoop::updateTrackLimitsCenterline() {
    static const std::vector<vec3> kNoLine;
    const std::vector<vec3> line = m_multiCar
        ? trackLimitsCenterlineFromSpline(m_multiCar->aiSpline())
        : kNoLine;
    m_features.trackLimits.setCenterline(line);
    std::fprintf(stderr, "[track-limits] corridor from %zu racing-line point(s)%s\n",
                 line.size(),
                 line.empty() ? " - detection stays on the phase hint" : "");
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

// The overlays lay themselves out in the UI's own design space (m_viewW x
// m_viewH, the size UiRenderer normalizes its vertices against) while the frame
// is presented at the swapchain's size. A pointer therefore arrives in client
// pixels and has to come back into design units before any hit test can match
// what is drawn; when the two sizes agree the factors are 1 and nothing moves.
static void uiPointerFromClient(NativeRenderer* renderer, int uiW, int uiH, float& x, float& y) {
    if (renderer == nullptr || uiW <= 0 || uiH <= 0) return;
    const VkExtent2D ext = renderer->extent();
    if (ext.width == 0 || ext.height == 0) return;
    x *= static_cast<float>(uiW) / static_cast<float>(ext.width);
    y *= static_cast<float>(uiH) / static_cast<float>(ext.height);
}

bool SimulationLoop::handleUiKey(int vk) { return m_ui.handleKey(vk); }
bool SimulationLoop::handleUiChar(int c) { return m_ui.handleChar(c); }
bool SimulationLoop::handleUiMouseMove(float x, float y) {
    uiPointerFromClient(m_vulkanRenderer, m_viewW, m_viewH, x, y);
    return m_ui.handleMouseMove(x, y);
}

bool SimulationLoop::handleLookMouseMove(float x, float y) {
    // The cockpit looks around and photo mode orbits; every other view ignores
    // the pointer. The first sample after a screen change positions it instead
    // of yanking the driver's head (or the orbit).
    const bool photo = m_camera && m_camera->mode() == CameraController::Mode::Free &&
                       m_camera->photoOrbit();
    if (!m_camera || (!photo && m_camera->mode() != CameraController::Mode::Cockpit)) {
        m_lookMouseValid = false;
        return false;
    }
    const float dx = m_lookMouseValid ? x - m_lookMouseX : 0.0f;
    const float dy = m_lookMouseValid ? y - m_lookMouseY : 0.0f;
    m_lookMouseX = x;
    m_lookMouseY = y;
    m_lookMouseValid = true;
    if (dx != 0.0f || dy != 0.0f) {
        if (photo) m_camera->addPhotoOrbitLook(dx, dy);
        else m_camera->addMouseLook(dx, dy);
    }
    return true;
}
bool SimulationLoop::handleUiMouseButton(ui::MouseButton b, bool down, float x, float y) {
    uiPointerFromClient(m_vulkanRenderer, m_viewW, m_viewH, x, y);
    const bool consumed = m_ui.handleMouseButton(b, down, x, y);
    if (const char* dbg = std::getenv("KS_INSPECT_DEBUG"); dbg != nullptr) {
        if (down && b == ui::MouseButton::Left)
            std::fprintf(stderr,
                         "[inspector] press at %.1f,%.1f consumed=%d showroom=%d menu=%d "
                         "console=%d carMeshes=%zu\n",
                         static_cast<double>(x), static_cast<double>(y), consumed ? 1 : 0,
                         showroomActive() ? 1 : 0, m_ui.menu().isVisible() ? 1 : 0,
                         m_ui.console().isVisible() ? 1 : 0, m_carVisualEntities.size());
    }
    if (consumed) return true;
    // Nothing in the UI took the press: in a showroom it selects the car part
    // under the pointer, the way Content Manager reports a component.
    if (down && b == ui::MouseButton::Left && showroomActive())
        return pickShowroomPart(x, y);
    return false;
}
bool SimulationLoop::handleUiMouseWheel(float d, float x, float y) {
    // Photo mode has no menus on screen: the wheel is the orbit's zoom.
    if (m_camera && m_camera->mode() == CameraController::Mode::Free && m_camera->photoOrbit()) {
        m_camera->addPhotoOrbitZoom(d / 120.0f);
        return true;
    }
    uiPointerFromClient(m_vulkanRenderer, m_viewW, m_viewH, x, y);
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
    // The pad remap panel needs the devices polled while driving is blocked,
    // otherwise a press on the wheel never reaches applyMapping and cannot be
    // captured. The blocked branch below zeroes the car, so nothing polled
    // here can move it.
    if (!blocked || m_ui.padRebind().isCapturing()) m_input->update();
    if (m_ui.padRebind().isCapturing()) {
        const int captured = m_input->takeCapturedButton();
        if (captured >= 0) {
            m_ui.padRebind().captureButton(captured);
            m_ui.padRebind().cancelCapture();
        }
    }
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
    // Le richieste di cambio non devono restare in coda mentre un menu blocca
    // la guida: si leggono comunque (e si scartano), si applicano solo a
    // auto sbloccata. I flag shiftUp/shiftDown vengono azzerati da update()
    // al frame successivo, quindi un menu aperto li disperde da sé.
    if (blocked) {
        m_vehicle->setThrottle(0);
        m_vehicle->setBrake(0);
        m_vehicle->setSteering(0);
        m_vehicle->setHandbrake(0);
        // Menus hold the clutch in: the car must not stall while the driver is
        // in a menu it cannot drive out of.
        m_vehicle->setClutchPedal(1.0);
        m_input->takeGearRequest();
        return;
    }
    // Cambio manuale (stile rF2): leva sequenziale a fronte e H-shifter a
    // livello arrivano da InputManager; ogni richiesta manuale spegne
    // l'autocambio nel veicolo.
    if (m_input->shiftUp()) m_vehicle->requestGearShift(1);
    if (m_input->shiftDown()) m_vehicle->requestGearShift(-1);
    const int gearReq = m_input->takeGearRequest();
    // 0 = nessuna richiesta, 1..7 = marce avanti, -1 = bottone retromarcia.
    // Il modello veicolo non modella ancora la retromarcia: il bottone retro
    // passa per ora in folle (autoReverse di controls.json resta in coda).
    if (gearReq != 0) m_vehicle->requestGear(gearReq);
    // KS_THROTTLE=0..1 drives the car with no keyboard: the headless way to
    // prove a session moves (a capture cannot hold W). Only the demand is
    // forced; the engine, clutch and gears behave exactly as with a pedal.
    float throttleDemand = static_cast<float>(m_input->throttle());
    const char* forceThrottle = std::getenv("KS_THROTTLE");
    const bool throttleForced = forceThrottle != nullptr && *forceThrottle != '\0';
    if (throttleForced) {
        throttleDemand = std::min(
            std::max(static_cast<float>(std::atof(forceThrottle)), 0.f), 1.f);
    }
    m_vehicle->setThrottle(throttleDemand);
    m_vehicle->setBrake(m_input->brake());
    float steerValue = static_cast<float>(m_input->steer());
    const auto& cc = m_input->controlsConfig();
    if (cc.speedSensitiveSteering > 0.0 && cc.steerRatioSpeed > 0.0) {
        double speed = 0.0;
        if (m_vehicle) speed = m_vehicle->getState().speed;
        double factor = 1.0 - cc.speedSensitiveSteering *
            std::clamp(speed / cc.steerRatioSpeed, 0.0, 1.0);
        steerValue = static_cast<float>(steerValue * factor);
    }
    // KS_STEER=-1..1 forces the steering demand, the twin of KS_THROTTLE: a
    // headless run has no window but the devices are still there, and a wheel
    // resting off-centre would otherwise steer every measured run. Straight-line
    // work (a launch, a stop) needs the input to be the number the test asks for.
    const char* forceSteer = std::getenv("KS_STEER");
    if (forceSteer != nullptr && *forceSteer != '\0') {
        steerValue = std::clamp(
            static_cast<float>(std::atof(forceSteer)), -1.f, 1.f);
    } else if (throttleForced) {
        // Throttle alone means "drive the car forward": the same resting axis
        // that would ruin a measured run would otherwise send a capture into
        // the wall. Straight ahead is the only sane default, and it is loud
        // enough to find in the log when someone expected the wheel.
        steerValue = 0.f;
        static bool announced = false;
        if (!announced) {
            announced = true;
            printf("[input] KS_THROTTLE set without KS_STEER: steering forced to 0 "
                   "(a wheel/pad axis at rest would steer the capture)\n");
        }
    }
    m_vehicle->setSteering(steerValue);
    m_vehicle->setHandbrake(m_input->handbrake());
    m_vehicle->setClutchPedal(m_input->clutch());
    // autoReverse (controls.json): da fermo, tenere il freno scambia
    // temporaneamente i pedali (freno = gas in retro, gas = freno) finché
    // la macchina torna a fermo, come in rF2/RaceRoom.
    if (cc.autoReverse) {
        const double speed = m_vehicle->getState().speed;
        const double brk = m_input->brake();
        const double thr = m_input->throttle();
        const double dt = 1.0 / 60.0;
        if (!m_autoReverseActive) {
            if (speed < 0.5 && brk > 0.9 && thr < 0.1) {
                m_autoReverseHold += dt;
                if (m_autoReverseHold >= 0.4) {
                    m_autoReverseActive = true;
                    m_vehicle->requestGear(-1);
                }
            } else {
                m_autoReverseHold = 0.0;
            }
        } else if (speed > 0.3) {
            // ancora in moto: freno diventa gas in retro, gas diventa freno.
            m_vehicle->setThrottle(brk);
            m_vehicle->setBrake(thr);
        } else if (brk > 0.9 && thr < 0.1) {
            m_autoReverseHold += dt;
            if (m_autoReverseHold >= 0.4) {
                m_autoReverseActive = false;
                m_autoReverseHold = 0.0;
                m_vehicle->requestGear(1);
            }
        } else {
            m_autoReverseHold = 0.0;
            m_autoReverseActive = false;
            m_vehicle->requestGear(1);
        }
    }
    // The starter key is the driver's button: ignition on, then crank. While
    // the engine runs it is the kill instead, and the vehicle applies its own
    // standstill rule. A car that declares a crew start accepts only the kill
    // here — the team fires it in the box, through the garage sequence. A car
    // without a declared start ignores both.
    if (m_input->takeStarterRequest()) {
        if (!m_vehicle->isEngineRunning()) m_vehicle->setIgnition(true);
        m_vehicle->pressStarter(static_cast<double>(m_vehicle->getState().speed));
    }
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

        // AI trace saver (P0.2): in practice mode, a completed lap becomes a
        // candidate AI line. Write it to track/ai/fast_lane.ai.candidate so
        // the editor/AC tooling can promote it to fast_lane.ai. Best-effort:
        // a missing track dir or a too-short trace is silently skipped.
        if (m_sessionType == 0 && !m_aiTrace.empty() && m_aiTrace.size() >= 100 &&
            !m_trackData.directory.empty()) {
            ks::ai::AiSpline spline;
            spline.closed = true;
            for (const auto& s : m_aiTrace) {
                ks::ai::AiSplinePoint pt;
                pt.position.x = s[0]; pt.position.y = s[1]; pt.position.z = s[2]; pt.speed = s[3];
                spline.points.push_back(pt);
            }
            const std::string outPath = m_trackData.directory + "/ai/fast_lane.ai.candidate";
            if (ks::ai::AiFileWriter::writeSpline(outPath, spline)) {
                std::fprintf(stderr, "[ai] wrote candidate trace: %s (%zu points)\n",
                             outPath.c_str(), spline.points.size());
                m_aiCandidateWritten = true;
            }
            m_aiTrace.clear();
        }
    }
    ks::physics::PhysVec3 p{st.position.x, st.position.y, st.position.z};
    if (speed > 5.f && st.throttle > 0.8f)
        ks::physics::TrackSurface::instance().depositRubber(p, 0.0002f * static_cast<float>(dt), 1.5f);

    // Sample the AI trace during the lap (practice mode only).
    if (m_sessionType == 0 && speed > 5.f) {
        m_aiTrace.push_back({st.position.x, st.position.y, st.position.z, speed});
    }
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
    // The car's limiter, not a fixed scale: AC dashes draw their rev bar from
    // this and would stop short on any car that does not rev to 8500.
    live.maxRpm = static_cast<float>(std::max(1.0, m_vehicle->maxRpm()));
    live.totalLaps = m_totalLaps;
    live.sectorCount = m_lapTimer.sectorCount();
    live.sessionTimeLeft = static_cast<float>(m_timeRemaining);
    live.trackSplineLength = m_trackData.splineLength;
    live.flag = ks::ac::ksRaceFlagToAcFlag(static_cast<int>(m_raceSession.flag()));
    // P0.1 penalties: AC's graphics page carries the pending penalty seconds
    // (PTA); dashboards like Live Timers read exactly this field.
    live.penaltyTime = m_raceSession.pendingPenaltyTime();
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
    // Standing and field size: the same rows the HUD and the RESULTS screen
    // read, so a dashboard's position widget cannot disagree with the screen.
    {
        const DriverStanding* ps = m_raceSession.playerStanding();
        live.position = (ps && ps->position > 0) ? ps->position : 1;
        live.numCars = std::max(1, static_cast<int>(m_raceSession.standings().size()));
    }
    // Wind lives on AC's graphics page; a dash draws its wind arrow from there.
    live.windSpeed = std::isfinite(m_weather.windSpeed) ? m_weather.windSpeed : 0.f;
    // WeatherState keeps degrees; AC's page wants radians.
    live.windDirectionRad = std::isfinite(m_weather.windDirection)
        ? m_weather.windDirection * 3.14159265f / 180.f : 0.f;
    live.penaltiesEnabled = true;
    // No lap count means the session runs on the clock: that is what
    // SPageFileStatic.isTimedRace tells the dashboard to display.
    live.timedRace = m_totalLaps <= 0;
    m_shm->publish(live);
#endif
}

float SimulationLoop::updateShowroomAutoExposure() {
    if (!m_vulkanRenderer) return m_roomExposure;
    const AutoExposureSettings& meter = m_showroom.config().autoExposure;
    if (!meter.declared() || m_roomMeterDone) return m_roomExposure;

    // One readback per step, and the renderer only fills it at the end of the
    // frame it was asked in: ask, then read on a later frame. A room is static,
    // so a few frames of latency cost nothing.
    if (!m_roomMeterWaiting) {
        m_vulkanRenderer->requestScreenshot();
        m_roomMeterWaiting = true;
        return m_roomExposure;
    }
    if (!m_vulkanRenderer->screenshotReady()) {
        // The renderer drops a request it cannot record (a swapchain that is not
        // up yet, an HDR10 target, a driver refusing the copy): ask again a few
        // frames later instead of waiting on a request that is already gone,
        // and give up only after a dozen of them.
        if (++m_roomMeterWaited > 4) {
            m_roomMeterWaited = 0;
            m_roomMeterWaiting = false;
            if (++m_roomMeterAttempts > 12) {
                m_roomMeterDone = true;
                std::fprintf(stderr,
                             "[showroom] auto exposure unavailable: no readback in %d attempts",
                             m_roomMeterAttempts);
                std::fputc(10, stderr);
            }
        }
        return m_roomExposure;
    }
    m_roomMeterWaiting = false;
    m_roomMeterWaited = 0;

    const VkExtent2D extent = m_vulkanRenderer->extent();
    const std::vector<unsigned char>& pixels = m_vulkanRenderer->screenshotPixels();
    if (extent.width == 0 || extent.height == 0 ||
        pixels.size() != std::size_t(extent.width) * std::size_t(extent.height) * 4) {
        m_roomMeterDone = true;
        std::fprintf(stderr,
                     "[showroom] auto exposure unavailable: readback is %zu bytes for %ux%u",
                     pixels.size(), extent.width, extent.height);
        std::fputc(10, stderr);
        return m_roomExposure;
    }
    const float measured =
        meteredLuminance(pixels.data(), int(extent.width), int(extent.height), meter);
    ++m_roomMeterSteps;
    if (autoExposureSettled(measured, meter)) {
        m_roomMeterDone = true;
        std::fprintf(stderr,
                     "[showroom] auto exposure %.2f (target %.3f, metered %.3f, %d step%s)",
                     static_cast<double>(m_roomExposure), static_cast<double>(meter.target),
                     static_cast<double>(measured), m_roomMeterSteps,
                     m_roomMeterSteps == 1 ? "" : "s");
        std::fputc(10, stderr);
        return m_roomExposure;
    }
    bool clamped = false;
    m_roomExposure = nextAutoExposure(m_roomExposure, measured, meter, &clamped);
    std::fprintf(stderr,
                 "[showroom] auto exposure step %d: metered %.3f against %.3f, exposure %.2f%s",
                 m_roomMeterSteps, static_cast<double>(measured),
                 static_cast<double>(meter.target), static_cast<double>(m_roomExposure),
                 clamped ? " (at the room's MIN/MAX)" : "");
    std::fputc(10, stderr);
    if (clamped || m_roomMeterSteps >= 6) {
        m_roomMeterDone = true;
        if (clamped) {
            std::fprintf(stderr,
                         "[showroom] auto exposure stopped at %.2f: the frame needs more than "
                         "MIN/MAX %.2f..%.2f allows",
                         static_cast<double>(m_roomExposure),
                         static_cast<double>(meter.minExposure),
                         static_cast<double>(meter.maxExposure));
            std::fputc(10, stderr);
        }
    }
    return m_roomExposure;
}

// The five values a [COLOR] block writes, as one piece of state: the room
// turns them on, the session keeps them, and leaving the hall has to put back
// exactly what the renderer carried before (see releaseShowroomScene()).
SimulationLoop::DisplayGrade SimulationLoop::readDisplayGrade() const {
    DisplayGrade grade;
    if (!m_vulkanRenderer) return grade;
    const float* filter = m_vulkanRenderer->colorFilter();
    grade.filter[0] = filter[0];
    grade.filter[1] = filter[1];
    grade.filter[2] = filter[2];
    grade.saturation = m_vulkanRenderer->saturation();
    grade.contrast = m_vulkanRenderer->contrast();
    grade.hue = m_vulkanRenderer->gradeHue();
    grade.sepia = m_vulkanRenderer->gradeSepia();
    return grade;
}

void SimulationLoop::writeDisplayGrade(const DisplayGrade& grade) {
    if (!m_vulkanRenderer) return;
    m_vulkanRenderer->setColorFilter(grade.filter);
    m_vulkanRenderer->setSaturation(grade.saturation);
    m_vulkanRenderer->setContrast(grade.contrast);
    m_vulkanRenderer->setGradeHue(grade.hue);
    m_vulkanRenderer->setGradeSepia(grade.sepia);
}

void SimulationLoop::applyShowroomLight() {
    if (!m_showroom.active() || !m_vulkanRenderer) return;
    {
        const ShowroomConfig& cfg = m_showroom.config();
        // A different room owns a different meter: start that one from its own
        // seed instead of carrying the previous room's exposure over.
        if (m_roomMeterRoom != cfg.id) {
            m_roomMeterRoom = cfg.id;
            m_roomExposure = 0.0f;
            m_roomMeterWaiting = false;
            m_roomMeterDone = false;
            m_roomMeterSteps = 0;
            m_roomMeterWaited = 0;
            m_roomMeterAttempts = 0;
        }
        // One line per room: what its [AUTO_EXPOSURE] block asked for, so a
        // room that does not meter says so instead of looking like a no-op.
        if (cfg.autoExposure.declared()) {
            std::fprintf(stderr,
                         "[showroom] auto exposure: target %.3f, MIN/MAX %.2f..%.2f, "
                         "metering %.2f x %.2f at %.2f/%.2f",
                         static_cast<double>(cfg.autoExposure.target),
                         static_cast<double>(cfg.autoExposure.minExposure),
                         static_cast<double>(cfg.autoExposure.maxExposure),
                         static_cast<double>(cfg.autoExposure.width),
                         static_cast<double>(cfg.autoExposure.height),
                         static_cast<double>(cfg.autoExposure.offsetX),
                         static_cast<double>(cfg.autoExposure.offsetY));
        } else {
            std::fputs("[showroom] auto exposure: the room declares none", stderr);
        }
        std::fputc(10, stderr);
        DirectionalLight light;
        // The file points at the sun, the renderer wants the way the light
        // travels (its own weather sun carries a negative y for daylight) —
        // feeding it unconverted left the room lit by ambient alone.
        light.direction = {-cfg.sunDirection[0], -cfg.sunDirection[1],
                           -cfg.sunDirection[2]};
        light.color = {cfg.sunColor[0], cfg.sunColor[1], cfg.sunColor[2]};
        // The same rule updateWeather() applies to a session inside the hall: a
        // room that declared no [LIGHT] block keeps the engine's daylight, a
        // declared level is its own. Without this the menu drew the room with
        // no sun at all and the session with one, which is the one difference
        // the two views cannot explain away.
        light.intensity = cfg.lightDeclared ? cfg.sunIntensity : 1.0f;
        // Kept after the room's own frames: a drive session started inside the
        // hall leaves the hall on screen (closeShowroom keeps the meshes), and
        // its direct light has to stay this sun — see updateWeather().
        m_roomSunDirection[0] = light.direction.x;
        m_roomSunDirection[1] = light.direction.y;
        m_roomSunDirection[2] = light.direction.z;
        m_roomSunColor[0] = light.color.x;
        m_roomSunColor[1] = light.color.y;
        m_roomSunColor[2] = light.color.z;
        m_roomSunIntensity = light.intensity;
        // An AC room carries no [LIGHT] block at all, and the loader turns that
        // into sunIntensity 0 (ShowroomConfig.h): 0 here means "the room said
        // nothing", not "the room wants no sun", so it must not mute the level.
        m_roomSunDeclared = cfg.lightDeclared;
        m_roomSunValid = true;
        // While the room owns the frame the session line has not been written
        // yet; a new room (or a new session) writes it again.
        m_roomDisplayLogged = false;
        // The room's own sun bakes the room's environment once, then the maps
        // are frozen: the menu keeps running the weather branch behind the
        // showroom, and its 65.7-degree track sun used to re-bake the sky under
        // the room a couple of seconds in.
        if (!m_showroomSkyBaked && m_vulkanRenderer->bakeSkyForSun(light)) {
            m_showroomSkyBaked = true;
            m_vulkanRenderer->setSkyIblFrozen(true);
        }
        m_vulkanRenderer->setSun(light);
        // The room's own lamps, when the document declares any: they become the
        // renderer's local lights, shaded by the deferred lighting pass with the
        // same surface response as the sun. A room that declares none leaves the
        // list empty, which is the frame the engine has always drawn.
        {
            std::vector<ks::sim::LocalLight> lamps;
            lamps.reserve(cfg.localLights.size());
            for (const ShowroomLocalLight& lamp : cfg.localLights) {
                ks::sim::LocalLight lampLight;
                for (int axis = 0; axis < 3; ++axis) {
                    lampLight.position[axis] = lamp.position[axis];
                    lampLight.color[axis] = lamp.color[axis];
                    lampLight.direction[axis] = lamp.direction[axis];
                }
                lampLight.range = lamp.range > 0.0f ? lamp.range : 12.0f;
                lampLight.intensity = lamp.intensity;
                if (lamp.spot) {
                    lampLight.spot = true;
                    lampLight.cosOuter =
                        std::cos(lamp.outerAngleDeg * 3.14159265358979f / 180.0f);
                    lampLight.cosInner =
                        std::cos(lamp.innerAngleDeg * 3.14159265358979f / 180.0f);
                }
                lamps.push_back(lampLight);
            }
            // Kept past closeShowroom(): the room's lamps are part of what the
            // hall looks like, and a session started inside it is still the hall.
            m_roomLamps = lamps;
            m_vulkanRenderer->setLocalLights(m_roomLamps);
        }
        // AC lights a showroom from the hall's own baked materials and a
        // fixed room light; its reflection probe exists for what the
        // surfaces reflect, not as a sky poured over the whole room.
        // Feeding it in as the ambient IBL is what washed the floor out
        // and tinted it cyan compared with the room's own preview.
        // Remember what the app asked for (KS_IBL=0 must survive the room).
        if (!m_showroomIblSaved) {
            m_showroomIblWasOn = m_vulkanRenderer->isIblEnabled();
            m_showroomIblSaved = true;
        }
        // A room whose surfaces carry their light in their own albedo needs no
        // sky poured over it. A room that IS a photo of a place needs the
        // environment: the probe upload replaced the sky with it, and that is
        // what the paint of the car reflects.
        if (!m_hallIsPhoto) m_vulkanRenderer->setIblEnabled(false);
        // Whether the room wants the IBL is part of its look: closeShowroom()
        // restores the track's, and a session in the hall has to get the room's
        // back (a hall that lights itself keeps the sky off).
        m_roomIblOn = m_vulkanRenderer->isIblEnabled();
        // Fog belongs to the room's size: these values were written for the
        // 24 m in-memory hall, and at 0.002 a real venue 84 m across dissolves
        // into its ambient colour before the far wall is reached.
        const float hallRadius = std::max(m_showroom.hallRadius(), 12.0f);
        const float fogScale = 12.0f / hallRadius;
        m_vulkanRenderer->setFog({cfg.ambientColor[0], cfg.ambientColor[1], cfg.ambientColor[2]},
                                 0.0001f * fogScale, 0.002f * fogScale);
        // Kept for a session started inside the hall: the weather branch would
        // otherwise draw the same room under a track's haze.
        m_roomFogColor[0] = cfg.ambientColor[0];
        m_roomFogColor[1] = cfg.ambientColor[1];
        m_roomFogColor[2] = cfg.ambientColor[2];
        m_roomFogDensity = 0.0001f * fogScale;
        m_roomFogFalloff = 0.002f * fogScale;
        // A photographed room is bright: without the extra stop the concrete and
        // the trees read as if the sun were going down. A post-processing
        // preset, when one is loaded (KS_PPFILTER), owns the display exposure
        // instead: it is the value the filter was written around, and the room
        // stop below is only the fallback for the no-preset case.
        const float presetExposure =
            ks::engine::config::PPFilterPreset::instance().display.exposure;
        // Priority: a loaded ppfilter owns the display, then the room's own
        // ppeffects.ini (what AC and CSP apply in this room), then the fallback
        // stop the small built-in room was tuned with.
        // AC applies the showroom's own ppeffects.ini in the room, so it beats
        // the user's ppfilter: that is the value the room was authored against.
        float exposure = 1.35f;
        if (cfg.exposure > 0.0f) exposure = cfg.exposure;
        else if (presetExposure > 0.0f) exposure = presetExposure;
        // [AUTO_EXPOSURE]: the room's own file meters the frame and drives the
        // exposure towards its TARGET inside its MIN/MAX, which is what AC and
        // CSP do here. The stop above is then only the seed of the loop.
        if (m_consoleGradeActive && m_consoleGradeExposure > 0.0f) {
            // The console owns the stop: the room's number and its metering
            // would fight the value being calibrated, so neither runs.
            exposure = m_consoleGradeExposure;
        } else {
            if (cfg.autoExposure.declared()) {
                if (m_roomExposure <= 0.0f) {
                    m_roomExposure = exposure;
                    if (cfg.autoExposure.minExposure > 0.0f)
                        m_roomExposure = std::max(m_roomExposure, cfg.autoExposure.minExposure);
                    if (cfg.autoExposure.maxExposure > 0.0f)
                        m_roomExposure = std::min(m_roomExposure, cfg.autoExposure.maxExposure);
                }
                exposure = updateShowroomAutoExposure();
            }
            m_vulkanRenderer->setExposure(exposure);
        }
        // Kept for a session inside the hall: the track's 1.0 stop would draw
        // the very same room with a different exposure.
        m_roomDisplayExposure = exposure;
        std::fprintf(stderr, "[showroom] exposure %.2f (room %.2f, preset %.2f)",
                     static_cast<double>(exposure), static_cast<double>(cfg.exposure),
                     static_cast<double>(presetExposure));
        std::fputc(10, stderr);
        // What the panel's state row reports before the console takes over: the
        // room's own numbers, which are the ones the frame is drawn with. A room
        // that declares no [COLOR] block is the neutral display.
        {
            ui::ConsoleOverlay::RoomGrade room;
            if (cfg.colorDeclared) {
                room.temperatureK =
                    cfg.colorTemp > 0 ? static_cast<float>(cfg.colorTemp) : 6500.f;
                room.brightness = cfg.colorBrightness;
                room.contrast = cfg.colorContrast;
                room.saturation = cfg.colorSaturation;
                room.hue = cfg.colorHueDeg;
                room.sepia = cfg.colorSepia;
            }
            room.exposure = exposure;
            m_ui.console().setRoomGrade(room);
        }
        // No second setExposure here: it used to overwrite the value picked
        // above with the ppfilter's, so the room's own EXPOSURE was logged
        // and then thrown away before the frame was drawn.
        // [COLOR]: the room's own grading block, which AC and CSP apply in
        // the room. Hue, saturation, contrast and sepia go to the display
        // pass; BRIGHTNESS and the white-balance pair are solved on the CPU
        // into the three-float colour filter the shader multiplies with. A
        // room that declares no block is left exactly as the user's filter
        // set it up, so this only ever turns the grading on.
        if (cfg.colorDeclared && !m_consoleGradeActive) {
            // What the renderer carried before the room turned its block on:
            // the ppfilter's grade, which the track behind the hall expects.
            if (!m_roomGradeSaved) {
                m_roomGradeWas = readDisplayGrade();
                m_roomGradeSaved = true;
            }
            float filter[3] = {1.f, 1.f, 1.f};
            showroomColorFilter(cfg.colorBrightness, cfg.colorTemp, cfg.whiteBalance,
                                filter);
            m_vulkanRenderer->setColorFilter(filter);
            m_vulkanRenderer->setSaturation(cfg.colorSaturation);
            m_vulkanRenderer->setContrast(cfg.colorContrast);
            m_vulkanRenderer->setGradeHue(cfg.colorHueDeg);
            m_vulkanRenderer->setGradeSepia(cfg.colorSepia);
            // Read back rather than copy the block: the setters clamp, and the
            // session has to re-apply exactly what the frame was drawn with.
            m_roomGrade = readDisplayGrade();
            m_roomGradeApplied = true;
            if (m_roomColorRoom != cfg.id) {
                m_roomColorRoom = cfg.id;
                std::fprintf(stderr,
                             "[showroom] color: hue %.0f deg, saturation %.2f, contrast "
                             "%.2f, sepia %.2f, brightness %.2f, %d/%d K, filter "
                             "%.3f/%.3f/%.3f",
                             static_cast<double>(cfg.colorHueDeg),
                             static_cast<double>(cfg.colorSaturation),
                             static_cast<double>(cfg.colorContrast),
                             static_cast<double>(cfg.colorSepia),
                             static_cast<double>(cfg.colorBrightness), cfg.colorTemp,
                             cfg.whiteBalance, static_cast<double>(filter[0]),
                             static_cast<double>(filter[1]),
                             static_cast<double>(filter[2]));
                std::fputc(10, stderr);
            }
        }
        // The console's numbers outrank the room's block: they were typed to
        // calibrate the display, so the next frame must not put the room's back.
        applyConsoleGrade();
    }
}

void SimulationLoop::applyConsoleGrade() {
    if (!m_vulkanRenderer || !m_consoleGradeActive) return;
    // Temperature and brightness are the same pair the room's [COLOR] solves
    // (ShowroomColor.h): the balance is measured from a neutral 6500 K, the
    // brightness is the linear gain before the curve. Contrast is the
    // display pass's own, applied around mid-grey.
    float filter[3] = {1.f, 1.f, 1.f};
    showroomColorFilter(m_consoleGradeBrightness, static_cast<int>(m_consoleGradeTempK), 6500,
                        filter);
    // The channel rows trim the white-balance filter per channel.
    filter[0] *= m_consoleGradeRed;
    filter[1] *= m_consoleGradeGreen;
    filter[2] *= m_consoleGradeBlue;
    m_vulkanRenderer->setColorFilter(filter);
    m_vulkanRenderer->setContrast(m_consoleGradeContrast);
    // How much colour the image carries: the room's [COLOR] SATURATION, which
    // the display pass applies after the transfer function.
    m_vulkanRenderer->setSaturation(m_consoleGradeSaturation);
    // The wheel and the wash the console turned: neutral 0 leaves the room's
    // own grading block untouched.
    m_vulkanRenderer->setGradeHue(m_consoleGradeHue);
    m_vulkanRenderer->setGradeSepia(m_consoleGradeSepia);
    // The stop is only the console's while it typed one: otherwise the room's
    // own number (and its metering loop) stays in charge.
    if (m_consoleGradeExposure > 0.0f)
        m_vulkanRenderer->setExposure(m_consoleGradeExposure);
}

void SimulationLoop::updateWeather(float dt) {
    // The windscreen rain animates off this clock (frame time, not wall
    // time): dt == 0 while a modal overlay pauses the world, and suspended
    // drops hang on the glass until the loop moves again.
    m_weatherClock += dt;
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
    ks::engine::graphics::Vec3 sunDir{0.3f * c - 0.2f * s, -0.8f, 0.2f * c + 0.3f * s};
    ks::engine::graphics::Vec3 sunColor{1.0f, 0.95f, 0.9f};
    // A hall the camera is inside keeps the sun it declared: the environment
    // maps were baked for that one (applyShowroomLight + setSkyIblFrozen), so
    // lighting the hall with the track's weather sun made shading and
    // reflections disagree. Its intensity comes along only when the room
    // declared one (see below): a room that says nothing keeps the engine's
    // daylight, a room that writes 0 is honoured.
    const bool roomOwnsTheSun = m_showroomHallInScene && m_roomSunValid;
    if (roomOwnsTheSun) {
        sunDir = {m_roomSunDirection[0], m_roomSunDirection[1], m_roomSunDirection[2]};
        sunColor = {m_roomSunColor[0], m_roomSunColor[1], m_roomSunColor[2]};
    }
    rs.setSun(sunDir, sunColor);
    // The haze tint is authored as a display-space colour (it reads like the
    // pale blue the sky shows), but every place it lands — the sky background
    // write, the fog mix against scene-linear lit values, the clear colour —
    // is scene-linear light. Feeding the encoded number in as if linear made
    // the sky (and every distance behind fog) ~40% brighter than authored and
    // pushed the whole horizon towards white; this is the display->linear
    // step the albedo decode in gbuffer.frag mirrors for textures.
    const float hazeLinear[3] = {std::pow(0.6f, 2.2f), std::pow(0.7f, 2.2f),
                                 std::pow(0.85f, 2.2f)};
    rs.setFog(m_weather.cloudCover > 0.35f,
              {hazeLinear[0], hazeLinear[1], hazeLinear[2]},
              0.0001f + m_weather.cloudCover * 0.002f);
    rs.setRain(m_weather.rainIntensity, m_weather.trackWetness);
    ks::physics::TrackSurface::instance().syncFromWeather(m_weather.trackWetness, m_weather.trackTemp);
    if (m_vulkanRenderer) {
        DirectionalLight light; light.direction = {sunDir.x, sunDir.y, sunDir.z};
        if (roomOwnsTheSun) {
            light.color = {sunColor.x, sunColor.y, sunColor.z};
            // Direction and colour are the room's; the level is its own when it
            // declared one, and the engine's daylight when it did not. The asr
            // hall has no [LIGHT] block (so the loader's sun_intensity is 0,
            // not an authored 0) — taking that literally left a drive session
            // inside the hall with no key light at all (measured: walls 185 ->
            // 125, floor 91 -> 59, and the blue of the sky ambient everywhere).
            // The sky takes the colour and not the intensity for the same reason
            // (see deferred_lighting.frag); a room that really writes
            // sun_intensity = 0 is still honoured.
            light.intensity = m_roomSunDeclared ? m_roomSunIntensity : 1.0f;
        }
        m_vulkanRenderer->setSun(light);
        if (roomOwnsTheSun) {
            // The hall keeps its lamps as well as its sun: closeShowroom()
            // cleared this slot for the track's dynamic lights, which is what a
            // session started in the room would otherwise drive under.
            m_vulkanRenderer->setLocalLights(m_roomLamps);
            // And the rest of its display state, which closeShowroom() handed
            // to the track: the room's own stop and its IBL choice.
            m_vulkanRenderer->setExposure(m_roomDisplayExposure);
            m_vulkanRenderer->setIblEnabled(m_roomIblOn);
            // The room's grading block belongs to the same display state.
            if (m_roomGradeApplied) writeDisplayGrade(m_roomGrade);
            if (!m_roomDisplayLogged) {
                m_roomDisplayLogged = true;
                const float elev = std::asin(std::max(
                    -1.0f, std::min(1.0f, -m_roomSunDirection[1]))) * 57.29578f;
                std::printf("[showroom] the session keeps the room's display: sun %.1f deg "
                            "(x%.2f), exposure %.2f, fog %.2e/%.2e, IBL %s, %zu lamp(s)\n",
                            static_cast<double>(elev),
                            static_cast<double>(m_roomSunDeclared ? m_roomSunIntensity : 1.0f),
                            static_cast<double>(m_roomDisplayExposure),
                            static_cast<double>(m_roomFogDensity),
                            static_cast<double>(m_roomFogFalloff),
                            m_roomIblOn ? "on" : "off", m_roomLamps.size());
            }
        }
        // Weather-driven atmosphere. RenderSystem::setFog() above only stores
        // the values (it has no GPU work behind it), so the shader-facing fog
        // has to be fed here as well: density rises with cloud cover, while a
        // small base haze keeps distant geometry from looking unnaturally
        // crisp on a clear day.
        const float fogDensity = 0.0004f + m_weather.cloudCover * 0.004f;
        if (roomOwnsTheSun) {
            // Fog belongs to the scene: the hall's haze was written for its
            // radius (a 27 m room under a track's 0.018 falloff tints the far
            // wall), so the room keeps its own while it is what is on screen.
            m_vulkanRenderer->setFog({m_roomFogColor[0], m_roomFogColor[1], m_roomFogColor[2]},
                                     m_roomFogDensity, m_roomFogFalloff);
        } else {
            m_vulkanRenderer->setFog({hazeLinear[0], hazeLinear[1], hazeLinear[2]},
                                     fogDensity, 0.018f);
        }
        // The track's own look follows the wetness the grip model reads, so a
        // soaked surface is dark and glossy and dries back to matte as the
        // weather clears (videoSources.weatherShaders). Wetness 0 leaves the
        // shader's numbers untouched.
        // KS_WETNESS=<0..100> overrides the look alone for a capture: the grip
        // model keeps the real trackWetness, exactly like KS_RAIN does for the
        // windscreen. It is how a wet frame and a dry frame can be shot under
        // the same sun, which is the only way to tell the wet shading apart
        // from the darker weather that comes with it.
        float wetnessForLook = m_weather.trackWetness;
        if (const char* wetEnv = std::getenv("KS_WETNESS"); wetEnv && *wetEnv)
            wetnessForLook =
                std::clamp(static_cast<float>(std::atof(wetEnv) / 100.0), 0.0f, 1.0f);
        m_vulkanRenderer->setWetness(wetnessForLook);
        // Rain on the windscreen (stage 1). Coverage is the evolving mm/h
        // capped at the deluge end, and the animation clock is this file's
        // own frame time — the clock freezes with the world when the loop
        // pauses, so suspended drops stay where they were. KS_RAIN (0..100)
        // overrides the coverage for a test run without touching the grip
        // model the real rainIntensity feeds. Only the driver's eye has
        // glass in front of it: any other camera keeps the layer off.
        // (The layer is screen-space on purpose — the glass pipeline
        // depth-tests without writing, so the depth buffer cannot locate
        // the glass; see rainOnGlass() in tonemap.frag.)
        double coverage =
            m_weather.rainIntensity > 0.0f
                ? static_cast<double>(m_weather.rainIntensity) / 40.0
                : 0.0;
        if (const char* rainEnv = std::getenv("KS_RAIN"); rainEnv && *rainEnv)
            coverage = std::atof(rainEnv) / 100.0;
        const bool inCockpit = m_camera &&
            m_camera->mode() == CameraController::Mode::Cockpit;
        m_vulkanRenderer->setWindscreen(
            static_cast<float>(std::clamp(coverage, 0.0, 1.0)),
            inCockpit,
            m_weatherClock);
    }
}

void SimulationLoop::ensureScenePipeline() {
    if (m_pipelineInitialized || !m_vulkanRenderer) return;
    if (m_uiGpu) m_vulkanRenderer->setUiGpuPass(m_uiGpu);
    m_pipelineInitialized = true;
}

namespace {
// Where this session's telemetry file goes: KS_TELEMETRY_CSV wins (unattended
// runs key on a fixed name), otherwise a timestamped file in the player's
// telemetry folder. Empty when the app never set a directory.
std::string sessionTelemetryCsvPath(const std::string& dir) {
    if (const char* env = std::getenv("KS_TELEMETRY_CSV"); env && env[0] != '\0')
        return env;
    if (dir.empty()) return std::string();
    std::tm tm{};
    const std::time_t now = std::time(nullptr);
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    tm = *std::localtime(&now);
#endif
    char name[96];
    std::snprintf(name, sizeof(name), "%s/session-%04d%02d%02d-%02d%02d%02d.csv",
                  dir.c_str(), tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec);
    return name;
}
} // namespace

void SimulationLoop::setTelemetryCsvRateHz(double hz) {
    // The settings speak rows per second, the sampler speaks seconds; a
    // nonsense rate (0, negative, NaN) keeps the 20 Hz default rather than
    // dividing by zero.
    if (!(hz > 0.0)) return;
    m_telemetryIntervalS = std::clamp(1.0 / hz, 1.0 / 240.0, 10.0);
}

void SimulationLoop::openSessionTelemetryCsv() {
    const std::string path = sessionTelemetryCsvPath(m_telemetryCsvDir);
    if (!m_sessionCsv.open(path)) {
        std::fprintf(stderr, "[TELEMETRY] csv FAILED: %s\n", path.c_str());
        return;
    }
    m_telemetrySampler.setInterval(m_telemetryIntervalS);
    // The longitudinal column differences the speed step to step, so the
    // baseline is the speed the car has right now: the first row is a real
    // derivative instead of a spike measured against zero.
#if HAS_VEHICLE_SIM
    m_telemetryPrevSpeedMps = m_vehicle ? m_vehicle->speedMps() : 0.0f;
#else
    m_telemetryPrevSpeedMps = 0.0f;
#endif
    std::printf("[TELEMETRY] csv sample rate: %.1f Hz\n",
                m_telemetryIntervalS > 0.0 ? 1.0 / m_telemetryIntervalS : 0.0);
    m_telemetrySampler.reset();
    std::printf("[TELEMETRY] csv -> %s\n", m_sessionCsv.path().c_str());
}

// Tyre visuals: the state the renderer would need to draw a bulging or
// flat-spotted tyre. It is stepped with the same clock as the wheel strip, from
// the contact patch the physics produced this step (normal load) and the pedal
// plus longitudinal slip that decide a lock-up. Published in the HUD sample;
// nothing scales a wheel mesh yet (see the video/tyreVisuals note in
// system/cfg/ksengine.json).
// Binds the car's wheel nodes the first time visuals and physics coexist. The
// baked set carries one mesh per kn5 node, named after it, and each node's
// transform is baked into its vertices, so the mesh's own bounds are in car
// space: their centre is the wheel's pivot. Called lazily because the visuals
// are loaded after the physics.
void SimulationLoop::bindWheelVisuals() {
    for (auto& wheel : m_wheelVisuals) wheel = WheelVisual{};
    m_wheelsBound = true;
    if (!m_vulkanRenderer) return;
    int bound = 0;
    for (const ks::ecs::Entity e : m_carVisualEntities) {
        const auto* instance = scene().tryGet<ks::ecs::MeshInstance>(e);
        if (instance == nullptr) continue;
        const int corner = ks::sim::wheelCornerFromNodeName(instance->meshName);
        if (corner < 0) continue;
        auto* mesh = scene().tryGet<ks::ecs::MeshInstance>(e);
        if (mesh == nullptr) continue;
        const NativeMesh* uploaded = m_vulkanRenderer->findMesh(instance->meshName);
        if (uploaded == nullptr) continue;
        if (uploaded->hasBounds) {
            const ks::math::vec3 pivot{
                (uploaded->boundsMin.x + uploaded->boundsMax.x) * 0.5f,
                (uploaded->boundsMin.y + uploaded->boundsMax.y) * 0.5f,
                (uploaded->boundsMin.z + uploaded->boundsMax.z) * 0.5f};
            mesh->pivot = pivot;
            m_wheelVisuals[corner].entity = e;
            m_wheelVisuals[corner].pivot = pivot;
        } else {
            // No bounds (a mesh that never uploaded its AABB): the node still
            // spins, about the car's own origin, which is better than a mesh
            // that silently ignores its state.
            m_wheelVisuals[corner].entity = e;
        }
        ++bound;
    }
    if (bound > 0)
        std::fprintf(stderr, "[tyres] %d wheel node(s) bound for the visual state\n", bound);
}

// Per-node transforms from the tyre's own state: the bulge the contact patch
// produces, the flat spot a locked wheel grinds, and the spin the slip ratio
// implies. The body matrix stays the car's; these ride on top of it.
void SimulationLoop::updateWheelVisuals(float stepSeconds) {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    if (!m_wheelsBound) bindWheelVisuals();
    const auto st = m_vehicle->getState();
    const float speed = static_cast<float>(st.speed);
    for (int i = 0; i < 4; ++i) {
        WheelVisual& wheel = m_wheelVisuals[i];
        if (wheel.entity == ks::ecs::kNullEntity) continue;
        auto* mesh = scene().tryGet<ks::ecs::MeshInstance>(wheel.entity);
        if (mesh == nullptr) continue;
        const ks::sim::TyreVisualState& tyre = m_tyreVisuals.state(i);
        wheel.spinRad = ks::sim::advanceWheelSpin(
            wheel.spinRad,
            ks::sim::wheelSpinRateRadPerSec(speed, static_cast<float>(st.slipRatioState[i]),
                                            tyre.effectiveRadius),
            stepSeconds);
        const ks::sim::TyreNodePose pose = ks::sim::tyreNodePose(tyre, wheel.spinRad);
        mesh->nodeScale = {pose.scale.x, pose.scale.y, pose.scale.z};
        mesh->nodeOffset = {pose.offset.x, pose.offset.y, pose.offset.z};
        mesh->nodeSpinRad = pose.spinRad;
    }
#else
    (void)stepSeconds;
#endif
}

// One entity per wheel for the blob shadow, bound once: the mesh is uploaded
// later, when the opacity the wheel asks for is known (the alpha is per vertex).
void SimulationLoop::bindWheelShadows() {
    m_wheelShadowsBound = true;
    if (m_vulkanRenderer == nullptr) return;
    int bound = 0;
    for (int i = 0; i < 4; ++i) {
        WheelShadowVisual& shadow = m_wheelShadows[i];
        shadow.meshName = "wheel_shadow_" + std::to_string(i);
        shadow.entity = scene().create();
        if (shadow.entity == ks::ecs::kNullEntity) continue;
        scene().emplace<ks::ecs::Name>(shadow.entity, ks::ecs::Name{shadow.meshName});
        scene().emplace<ks::ecs::Transform>(shadow.entity);
        scene().emplace<ks::ecs::MeshInstance>(shadow.entity,
                                               ks::ecs::MeshInstance{shadow.meshName});
        shadow.opacityStep = -1;
        ++bound;
    }
    if (bound > 0)
        std::fprintf(stderr, "[tyres] %d wheel shadow quad(s) bound\n", bound);
}

void SimulationLoop::updateWheelShadows(const ks::math::vec3& bodyPos,
                                       const ks::math::vec3& bodyRot) {
    if (m_vulkanRenderer == nullptr) return;
    if (!m_wheelsBound) bindWheelVisuals();
    if (!m_wheelShadowsBound) bindWheelShadows();

    ks::ecs::Transform body;
    body.position = bodyPos;
    body.rotation = bodyRot;
    const ks::math::mat4 bodyMatrix = ks::ecs::worldMatrix(body);

    // The contact point of each wheel, in world space, and the lowest of the
    // four: a wheel lifted over a kerb is higher than its neighbours, which is
    // the height the model fades the blob over. (A whole car in the air has no
    // ground to be measured against - see the wheelShadowing note.)
    ks::math::vec3 contact[4];
    float radius[4] = {0.f, 0.f, 0.f, 0.f};
    bool have[4] = {false, false, false, false};
    float lowest = 1e9f;
    for (int i = 0; i < 4; ++i) {
        if (m_wheelVisuals[i].entity == ks::ecs::kNullEntity) continue;
        radius[i] = m_tyreVisuals.state(i).effectiveRadius;
        const ks::math::vec3 pivot = m_wheelVisuals[i].pivot;
        contact[i] = bodyMatrix * ks::math::vec3{pivot.x, pivot.y - radius[i], pivot.z};
        have[i] = true;
        if (contact[i].y < lowest) lowest = contact[i].y;
    }
    if (lowest > 1e8f) return;

    for (int i = 0; i < 4; ++i) {
        if (!have[i]) continue;
        auto* transform = scene().tryGet<ks::ecs::Transform>(m_wheelShadows[i].entity);
        if (transform == nullptr) continue;
        // The floor comes from the shared surface query - the track's racing
        // line, not the wheel's neighbours - so a car in the air fades its
        // blobs and a wheel over a kerb keeps a contact patch. The lowest
        // contact is only the fallback for a track with no line.
        const ks::sim::SurfaceSample ground =
            m_features.trackLimits.query().at(contact[i].x, contact[i].z);
        const float floorY = ground.found ? ground.floorYM : lowest;
        const float height = std::max(0.0f, contact[i].y - floorY);
        m_wheelShadowQuad[i] = ks::sim::wheelShadowQuad(m_wheelShadowCfg, radius[i], height);
        transform->position = {contact[i].x, contact[i].y + 0.02f, contact[i].z};
        // The blob lies flat: yaw only, no roll or pitch to lift it off the road.
        transform->rotation = {0.0f, 0.0f, bodyRot.z};

        // The vertex alpha is what carries the opacity, so the mesh is rebuilt
        // when the level changes and not every frame: eight levels is finer than
        // the eye reads on a dark blob and keeps uploads to a handful per lap.
        const int step = static_cast<int>(std::lround(m_wheelShadowQuad[i].opacity * 8.0f));
        if (step <= 0) {
            transform->scale = {0.0f, 0.0f, 0.0f}; // nothing to draw
            continue;
        }
        transform->scale = {1.0f, 1.0f, 1.0f};
        if (step == m_wheelShadows[i].opacityStep) continue;
        m_wheelShadows[i].opacityStep = step;

        ks::sim::WheelShadowQuad quad = m_wheelShadowQuad[i];
        quad.opacity = static_cast<float>(step) / 8.0f;
        ks::sim::WheelShadowVertex verts[ks::sim::kWheelShadowVertexCount];
        uint32_t indices[ks::sim::kWheelShadowIndexCount];
        // Built at the origin: the entity's transform places it under the wheel.
        if (!ks::sim::buildWheelShadowBlob(0.0f, 0.0f, 0.0f, quad, verts, indices)) continue;
        NativeMesh mesh;
        mesh.blended = true;   // the transparent pass: premultiplied alpha, no depth write
        mesh.castShadow = false;
        mesh.vertices.resize(ks::sim::kWheelShadowVertexCount);
        for (int v = 0; v < ks::sim::kWheelShadowVertexCount; ++v) {
            NativeVertex& out = mesh.vertices[static_cast<size_t>(v)];
            const ks::sim::WheelShadowVertex& in = verts[v];
            out.px = in.x; out.py = in.y; out.pz = in.z;
            out.nx = 0.0f; out.ny = 1.0f; out.nz = 0.0f;
            out.u = in.u; out.v = in.v;
            out.r = in.r; out.g = in.g; out.b = in.b; out.a = in.a;
        }
        mesh.indices.assign(indices, indices + ks::sim::kWheelShadowIndexCount);
        m_vulkanRenderer->setMesh(m_wheelShadows[i].meshName, mesh);
    }
}

// The blades' axes are InputManager's, by construction: the adapter is built to
// feed it, and a silent disagreement here would put the throttle on the clutch.
static_assert(ks::sim::kVrAxisLX == ks::sim::InputManager::AXIS_LX, "vr axis order");
static_assert(ks::sim::kVrAxisLT == ks::sim::InputManager::AXIS_LT, "vr axis order");
static_assert(ks::sim::kVrAxisRT == ks::sim::InputManager::AXIS_RT, "vr axis order");
static_assert(ks::sim::kVrAxisCount == ks::sim::InputManager::AXIS_COUNT, "vr axis count");

void SimulationLoop::pollVrInput() {
    auto* xr = ks::device::XrManager::instance();
    if (xr == nullptr || !xr->isInitialized() || !xr->isSessionRunning()) {
        // No headset (or the session ended): nothing to read, and the last frame
        // must not linger as if a controller were still held down.
        if (m_vrFrame.connected || m_vrHeadPose.valid) {
            m_vrFrame = ks::sim::VrInputFrame{};
            m_vrHeadPose = ks::sim::VrHeadPose{};
            m_vrPose[0] = ks::sim::VrControllerPose{};
            m_vrPose[1] = ks::sim::VrControllerPose{};
        }
        // A session that ended takes its play space with it: the next one must
        // not inherit a reference taken in the old one.
        m_vrRecentre.clear();
        return;
    }
    // The same handover kseditor makes: the integration owns the XrInput, and
    // the simulator polls it. A null here means the engine was built without
    // OpenXR, which is a silent no-op rather than a crash.
    ks::device::XrIntegration* integration = ks::device::XrIntegration::instance();
    ks::device::XrInput* input = integration ? integration->xrInput() : nullptr;
    if (input == nullptr || m_input == nullptr) return;
    input->update();

    // The head is not a controller: XrManager tracks it, and the only consumer
    // this engine has is the cockpit camera (see updateCamera). Read it here so
    // the pose a frame renders with is the pose the same frame just collected.
    const auto head = xr->headPose(); // global XrPosef: the runtime's own struct
    m_vrHeadPose = ks::sim::vrHeadPoseFromXr(head.position.x, head.position.y, head.position.z,
                                            head.orientation.x, head.orientation.y,
                                            head.orientation.z, head.orientation.w,
                                            xr->headPoseValid());
    // The reference is not taken here: it belongs to the cockpit and is handled
    // in updateCamera, where the camera mode is known (VrHeadRecentre.h).

    ks::sim::VrControllerSnapshot snapshot;
    for (int hand = 0; hand < 2; ++hand) {
        const ks::device::XrInput::Hand h =
            hand == 0 ? ks::device::XrInput::Left : ks::device::XrInput::Right;
        snapshot.connected[hand] = input->isControllerConnected(h);
        snapshot.trigger[hand] = input->getTriggerValue(h);
        snapshot.squeeze[hand] = input->getSqueezeValue(h);
        const ks::device::Vec2 stick = input->getThumbstickValue(h);
        snapshot.stickX[hand] = stick.x;
        snapshot.stickY[hand] = stick.y;
        snapshot.menu[hand] = input->isButtonPressed(h, ks::device::XrInput::Menu);
        snapshot.shiftUp[hand] = input->isButtonPressed(h, ks::device::XrInput::A);
        snapshot.shiftDown[hand] = input->isButtonPressed(h, ks::device::XrInput::B);
        snapshot.poseValid[hand] = input->isPoseValid(h);
        if (snapshot.poseValid[hand]) {
            const ks::device::XrMat4 grip = input->getGripPose(h);
            snapshot.gripPosition[hand][0] = grip.m[12];
            snapshot.gripPosition[hand][1] = grip.m[13];
            snapshot.gripPosition[hand][2] = grip.m[14];
            input->getAimRay(h, snapshot.aimOrigin[hand], snapshot.aimDirection[hand]);
        }
        m_vrPose[hand].connected = snapshot.connected[hand];
        m_vrPose[hand].valid = snapshot.poseValid[hand];
        for (int c = 0; c < 3; ++c) {
            m_vrPose[hand].position[c] = snapshot.gripPosition[hand][c];
            m_vrPose[hand].aimOrigin[c] = snapshot.aimOrigin[hand][c];
            m_vrPose[hand].aimDirection[c] = snapshot.aimDirection[hand][c];
        }
    }

    const ks::sim::JoystickMapping& mapping = m_input->mapping();
    m_vrFrame = ks::sim::vrInputFrame(snapshot, mapping.handbrakeButton, mapping.shiftUpButton,
                                      mapping.shiftDownButton);
    // Only when a controller is actually holding something: injecting zeros
    // would also take the keyboard away from a player sitting in the headset.
    if (m_vrFrame.connected) m_input->injectAxes(m_vrFrame.axes, m_vrFrame.buttons);
}

void SimulationLoop::updateWheelSurfaceGrip() {
#if HAS_VEHICLE_SIM
    if (m_vehicle == nullptr) return;
    // Without the wheel nodes there are no contact points to sample: the car then
    // keeps the track's own mu, which is the behaviour every build had before.
    if (!m_wheelsBound) return;

    const auto st = m_vehicle->getState();
    ks::ecs::Transform body;
    body.position = {st.position.x, st.position.y, st.position.z};
    body.rotation = {st.rotation.x, st.rotation.y, st.rotation.z};
    const ks::math::mat4 bodyMatrix = ks::ecs::worldMatrix(body);

    float xs[4] = {0.f, 0.f, 0.f, 0.f};
    float zs[4] = {0.f, 0.f, 0.f, 0.f};
    for (int i = 0; i < 4; ++i) {
        const auto& wheel = m_wheelVisuals[i];
        if (wheel.entity == ks::ecs::kNullEntity) continue;
        // The contact patch, not the wheel centre: the same point the blob
        // shadow sits on, lowered by the tyre's current radius.
        const float radius = m_tyreVisuals.state(i).effectiveRadius;
        const ks::math::vec3 contact =
            bodyMatrix * ks::math::vec3{wheel.pivot.x, wheel.pivot.y - radius, wheel.pivot.z};
        xs[i] = contact.x;
        zs[i] = contact.z;
    }
    m_wheelSurfaceGrip = ks::sim::wheelSurfaceGrip(m_features.trackLimits.query(), xs, zs,
                                                   m_surfaceGripCfg);
    m_vehicle->setWheelSurfaceGrip(m_wheelSurfaceGrip.scale);
#endif
}

void SimulationLoop::updateGrassField(const ks::math::vec3& focus) {
    if (m_vulkanRenderer == nullptr) return;
    const float dx = focus.x - m_grassCenter.x;
    const float dz = focus.z - m_grassCenter.z;
    // The field is scenery: it is rebuilt when the car has moved far enough that
    // its edge would show, not every frame.
    if (m_grassBuilt && dx * dx + dz * dz < 64.0f) return;

    const ks::sim::TrackSurfaceQuery& query = m_features.trackLimits.query();
    ks::sim::GrassFieldConfig cfg;
    const ks::sim::GrassField field =
        ks::sim::buildGrassField(query, m_grassFx, cfg, focus.x, focus.z, focus.y);

    m_grassCenter = focus;
    m_grassBuilt = true;
    m_grassBlades = field.blades;
    if (m_grassEntity == ks::ecs::kNullEntity) {
        m_grassEntity = scene().create();
        if (m_grassEntity != ks::ecs::kNullEntity) {
            scene().emplace<ks::ecs::Name>(m_grassEntity, ks::ecs::Name{"grass_field"});
            scene().emplace<ks::ecs::Transform>(m_grassEntity);
            scene().emplace<ks::ecs::MeshInstance>(m_grassEntity,
                                                   ks::ecs::MeshInstance{"grass_field"});
        }
    }
    if (field.empty()) {
        // A track without grass, gravel or soil bands: nothing to draw, and the
        // leftover mesh from the previous field must go.
        if (auto* transform = scene().tryGet<ks::ecs::Transform>(m_grassEntity))
            transform->scale = {0.0f, 0.0f, 0.0f};
        m_vulkanRenderer->destroyMesh("grass_field");
        return;
    }
    if (auto* transform = scene().tryGet<ks::ecs::Transform>(m_grassEntity))
        transform->scale = {1.0f, 1.0f, 1.0f};

    NativeMesh mesh;
    mesh.grass = true;
    mesh.castShadow = false; // a blade is too thin to be worth a shadow map slot
    mesh.vertices.resize(field.vertices.size());
    for (size_t i = 0; i < field.vertices.size(); ++i) {
        NativeVertex& out = mesh.vertices[i];
        const ks::sim::GrassVertex& in = field.vertices[i];
        out.px = in.px; out.py = in.py; out.pz = in.pz;
        // The normal slot carries the field's edge fade, not a direction.
        out.nx = in.fade; out.ny = in.ny; out.nz = in.nz;
        out.u = in.u; out.v = in.v;
        out.r = in.r; out.g = in.g; out.b = in.b; out.a = in.a;
    }
    mesh.indices = field.indices;
    m_vulkanRenderer->setMesh("grass_field", mesh);
}

void SimulationLoop::updateTyreVisuals(float stepSeconds) {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    float load[4] = {};
    float slip[4] = {};
    for (int i = 0; i < 4; ++i) {
        load[i] = static_cast<float>(st.tireFz[i]);
        slip[i] = static_cast<float>(st.slipRatioState[i]);
    }
    m_tyreVisuals.update(stepSeconds, load, slip, static_cast<float>(st.brake));
#else
    (void)stepSeconds;
#endif
}

void SimulationLoop::sampleSessionTelemetry(float stepSeconds) {
#if HAS_VEHICLE_SIM
    // The file belongs to the physics, not to the frame: the sampler advances on
    // the fixed step, so a session writes 20 rows per simulated second whether
    // the frame rate is 60 or 240 - and whether there are frames at all, which
    // is what makes this work in a headless run. start() opens the file and
    // stop() closes it; this is only the row.
    if (!m_vehicle || !m_sessionCsv.isOpen()) return;
    // The longitudinal column is the derivative of the step that just ran, so
    // the tracker advances on every step, not only on the ones that owe a row -
    // an accessor, not getState(), to keep that free.
    const float speedMps = m_vehicle->speedMps();
    const float prevSpeedMps = m_telemetryPrevSpeedMps;
    m_telemetryPrevSpeedMps = speedMps;
    if (!m_telemetrySampler.step(static_cast<double>(stepSeconds))) return;
    // One state copy per sampled step (20 a second, not 1000): the channels the
    // CSV carries are the ones the physics just wrote.
    const auto st = m_vehicle->getState();
    SessionTelemetryRow row;
    row.timeS = m_telemetrySampler.clock();
    row.speedKmh = static_cast<float>(st.speed) * 3.6f;
    row.rpm = static_cast<float>(m_vehicle->rpm());
    row.throttle = static_cast<float>(st.throttle);
    row.brake = static_cast<float>(st.brake);
    row.gear = m_vehicle->currentGear();
    row.lap = m_lapTimer.completedLaps() + 1;
    row.lapTimeMs = m_lapTimer.currentTimeMs();
    // Sectors, delta, standing, damage and pit state: the numbers the HUD shows,
    // from the same owners (the lap timer, the race session, the damage sample
    // and the physics state), so a CSV row and a screen frame agree.
    const ui::LapDelta delta = ui::lapDeltaFrom(m_lapTimer);
    row.sector = m_lapTimer.sectorIndex() + 1;
    // sectorTimeMs(i) is a CLOSED sector's duration, so the one being driven
    // is the lap clock minus the sectors already closed - the same reading the
    // live delta interpolates on.
    int closedSectorsMs = 0;
    for (int i = 0; i < m_lapTimer.sectorIndex(); ++i)
        closedSectorsMs += m_lapTimer.sectorTimeMs(i);
    row.sectorTimeMs = std::max(0, m_lapTimer.currentTimeMs() - closedSectorsMs);
    row.lapDeltaMs = delta.lapMs;
    row.sectorDeltaMs = delta.sectorsMeasured > 0
        ? delta.sectorMs[delta.sectorsMeasured - 1] : 0;
    {
        const DriverStanding* ps = m_raceSession.playerStanding();
        row.position = (ps && ps->position > 0) ? ps->position : 1;
        row.cars = std::max(1, static_cast<int>(m_raceSession.standings().size()));
    }
    row.inPit = st.inPitLane ? 1 : 0;
    row.pitLimiter = st.pitLimiterActive ? 1 : 0;
    {
        const auto dmg = ks::physics::sampleDamage(m_vehicle->damage());
        row.damage = dmg.overall;
        row.engineHealth = dmg.engineHealth;
    }
    for (int i = 0; i < 4; ++i) {
        row.slip[i] = st.slipRatioState[i];
        row.loadN[i] = st.tireFz[i];
        row.tempC[i] = static_cast<float>(st.tyreTemp[i]);
        row.pressBar[i] = static_cast<float>(st.tyrePressure[i]);
        row.brakeTempC[i] = st.brakeDiscTemp[i];
    }
    row.fuelL = static_cast<float>(st.fuel);
    // Brake heat is the discs' own thermal state (the model that fades torque),
    // steering is the normalized axis the physics was fed, and the two
    // accelerations are speed x yaw rate and this step's change in speed. Both
    // are float already, so no cast is pretending to convert anything.
    row.steer = st.steering;
    row.accelLatMs2 = st.speed * st.yawRate;
    row.accelLongMs2 = stepSeconds > 1e-6f
        ? (speedMps - prevSpeedMps) / stepSeconds : 0.0f;
    m_sessionCsv.write(row);
#endif
}

void SimulationLoop::updateWheelLeds(float stepSeconds) {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    // Accessors, not getState(): this runs once per physics step, and a state
    // copy per step would be paid for nothing.
    // The strip's share of the car's own limiter, from the rule the HUD and the
    // dash use (ui/RevScale.h): the shift light starts at 96% of THIS car's
    // limiter, so a 7200 rpm car flashes where it should.
    const int rpmPct = ui::revPercent(m_vehicle->rpm(), m_vehicle->maxRpm());
    const int gear = std::max(0, m_vehicle->currentGear());
    const double tank = std::max(0.0, static_cast<double>(m_vehicle->fuelTankCapacityL()));
    const int fuelPct = tank > 0.0
        ? static_cast<int>(std::clamp(
              static_cast<double>(m_vehicle->fuelLevelL()) / tank * 100.0, 0.0, 100.0))
        : 100;

    // Every warning the car raises is decided here, once, and read by the wheel
    // strip below, the H-DASH and the car's own dash (physics/DrivingWarnings.h):
    // one answer, so two panels cannot call the same lamp on and off differently,
    // and a new light is added there instead of in three places.
    {
        ks::physics::DrivingWarnings::From in;
        for (int i = 0; i < 4; ++i) {
            in.brakeDiscTempC[i] = m_vehicle->brakes().discTemp(i);
            in.fadeStartTempC[i] = m_vehicle->brakes().wheelConfig(i).fadeStartTemp;
            in.brakeFade[i] = m_vehicle->brakes().fade(i);
        }
        in.absActive = m_vehicle->absActive();
        in.tcActive = m_vehicle->tcActive();
        in.fuelPct = static_cast<float>(fuelPct);
        in.engineRunning = m_vehicle->isEngineRunning();
        in.oilBar = m_vehicle->oilPressureBar();
        in.oilC = m_vehicle->oilTempC();
        in.coolantC = m_vehicle->coolantTempC();
        in.batteryV = m_vehicle->batteryVoltage();
        m_drivingWarnings = ks::physics::DrivingWarnings::build(in);
    }
    const bool brakeHot = m_drivingWarnings.brakeHot;
    const bool absActive = m_drivingWarnings.absActive;

    auto& leds = ks::engine::input::WheelLedControl::instance();
    if (rpmPct != m_ledRpmPct || gear != m_ledGear || fuelPct != m_ledFuelPct ||
        brakeHot != m_ledBrakeHot || absActive != m_ledAbsActive) {
        m_ledRpmPct = rpmPct;
        m_ledGear = gear;
        m_ledFuelPct = fuelPct;
        m_ledBrakeHot = brakeHot;
        m_ledAbsActive = absActive;
        ks::engine::input::WheelLedState led;
        led.rpmPercentage = static_cast<uint8_t>(rpmPct);
        led.gearNumber = static_cast<uint8_t>(gear);
        led.fuelPercentage = static_cast<float>(fuelPct);
        led.brakeTempWarning = brakeHot;
        led.absActive = absActive;
        leds.setWheelLedState(led);
    }
    // The flash advances on the physics clock, not the render clock: a paused
    // world freezes the strip, and the blink stays in step with the revs it
    // belongs to at any frame rate.
    leds.advance(static_cast<double>(std::max(0.f, stepSeconds)));
#endif
}

void SimulationLoop::syncUiFromVehicle(float dtSeconds) {
#if HAS_VEHICLE_SIM
    // Spectator classification (ui/SpectatorScoreboard.h): the panel draws the
    // session's own standings, the same rows RESULTS and the control API
    // report, and it is filled without a player car too - a spectator may have
    // none of his own. Leaving the mode hides it.
    {
        auto& spectator = ks::engine::sim::SpectatorMode::instance();
        auto& board = m_ui.spectatorScoreboard();
        if (spectator.isSpectating()) {
            std::string followedName;
            if (m_multiCar) {
                if (CarEntry* watched = m_multiCar->getCar(spectator.followedDriver()))
                    followedName = watched->driverName;
            }
            std::vector<ui::SpectatorScoreboardRow> rows;
            rows.reserve(m_raceSession.standings().size());
            for (const auto& d : m_raceSession.standings()) {
                ui::SpectatorScoreboardRow row;
                row.position = d.position;
                row.driver = !d.driverName.empty() ? d.driverName : d.carName;
                row.laps = d.currentLap;
                row.bestLapSeconds =
                    (d.bestLapTime > 0.f && d.bestLapTime < 1.0e8f) ? d.bestLapTime : -1.f;
                if (d.disqualified) row.status = "DSQ";
                else if (d.finished) row.status = "FIN";
                row.followed = !followedName.empty() && d.driverName == followedName;
                rows.push_back(std::move(row));
            }
            board.setFollowedName(followedName);
            board.setRows(std::move(rows));
            board.setVisible(spectator.scoreboardVisible());
        } else {
            board.setVisible(false);
        }
    }
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
    // SETTINGS -> GAMEPLAY: the spotter. Its calls are text in the notice list
    // (the voice channel exists but has no recordings yet), and only while the
    // race is green: a car waiting on the grid is not alongside anything.
    {
        const auto& tweaks = ks::engine::sim::GameplayTweaks::instance().getTweakSet();
        if (!tweaks.spotterEnabled || m_sessionPhase != PHASE_GREEN_FLAG) {
            m_spotter.reset();
        } else {
            m_spotterCars.clear();
            if (m_multiCar) {
                for (const auto& entry : m_multiCar->cars()) {
                    if (!entry || !entry->isActive || entry->isPlayer || !entry->vehicle)
                        continue;
                    const auto cs = entry->vehicle->getState();
                    m_spotterCars.push_back({static_cast<float>(cs.position.x),
                                             static_cast<float>(cs.position.z)});
                }
            }
            const std::string call = m_spotter.update(
                static_cast<float>(st.position.x), static_cast<float>(st.position.z),
                static_cast<float>(st.heading), m_spotterCars, dtSeconds);
            if (!call.empty()) m_ui.notify(call, ui::NoticeKind::Info);
        }
    }
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
    s.speedMs = static_cast<float>(st.speed); s.rpm = static_cast<float>(m_vehicle->rpm());
    // The car's own limiter, not a fixed number: the H-DASH bar, the shift
    // flash, the wheel strip's frame and the car's dash all read this sample,
    // and 8500 drew a half-empty bar on a 7200 rpm car.
    s.maxRpm = static_cast<float>(std::max(1.0, m_vehicle->maxRpm()));
    s.gear = m_vehicle->currentGear(); s.throttle = static_cast<float>(st.throttle);
    s.brake = static_cast<float>(st.brake); s.steer = static_cast<float>(st.steering);
    // The strip on the HUD is the module's own frame, which updateWheelLeds()
    // rebuilds at the physics rate: what the driver sees and what a wheel
    // would receive cannot drift.
    s.leds = ks::engine::input::WheelLedControl::instance().currentFrame();
    s.latG = st.acceleration.x / 9.81f; s.lonG = st.acceleration.z / 9.81f; s.fuelL = static_cast<float>(st.fuel);
    // The percentage the strip warns on, from the same tank the litres come from,
    // so the DASH and the wheel cannot disagree about how empty the car is.
    {
        const double tankL = std::max(0.0, static_cast<double>(m_vehicle->fuelTankCapacityL()));
        s.fuelPct = tankL > 0.0
            ? static_cast<float>(std::clamp(static_cast<double>(st.fuel) / tankL * 100.0, 0.0, 100.0))
            : 0.f;
    }
    // The pedal box and the shifter, from the same step this frame is drawn
    // from: the clutch is the pedal the physics was given, the queue is what the
    // sequential shifter has asked for and the gearbox has not taken yet.
    s.clutch = static_cast<float>(m_vehicle->clutchPedal());
    s.pendingShifts = m_vehicle->pendingGearShifts();
    s.autoShift = m_vehicle->autoShifting();
    // The engine vitals the H-DASH lights read, and the air they cool to: the
    // session's own ambient, not a constant, so a cold morning reads as one.
    m_vehicle->setAmbientTempC(static_cast<float>(m_weather.ambientTemp));
    s.coolantC = m_vehicle->coolantTempC();
    s.oilC = m_vehicle->oilTempC();
    s.oilBar = m_vehicle->oilPressureBar();
    s.batteryV = m_vehicle->batteryVoltage();
    s.engineRunning = m_vehicle->isEngineRunning();
    // The handbrake the car's own display declares a light for: the demand the
    // rear axle is getting, not a key state.
    s.handbrake = m_vehicle->handbrakeTorque() > 0.1f;
    // The driver aids the car is running, and whether it carries them: the DASH
    // prints them and the console's TC/ABS verbs move them while driving.
    s.tcLevel = m_vehicle->tcLevel();
    s.absLevel = m_vehicle->absLevel();
    s.tcPresent = m_vehicle->tractionControlPresent();
    s.absPresent = m_vehicle->absPresent();
    // ...and the warnings themselves, decided once per step in updateWheelLeds:
    // the same booleans the wheel strip is running on, so the panel and a wheel
    // cannot show different lamps.
    s.warnings = m_drivingWarnings;
    // The third cockpit knob: the front brake share the car is braking with.
    s.brakeBiasPct = static_cast<int>(m_vehicle->brakeBias() * 100.f + 0.5f);
    for (int i = 0; i < 4; ++i) { s.tyreTemp[i] = static_cast<float>(st.tyreTemp[i]); s.tyreWear[i] = static_cast<float>(st.tyreWear[i]); }
    // Virtual tyres: the contact patch, from the state this frame produced.
    for (int i = 0; i < 4; ++i) {
        s.tyrePressure[i] = static_cast<float>(st.tyrePressure[i]);
        s.tyreSlip[i] = st.slipRatioState[i];
        s.tyreLoadN[i] = st.tireFz[i];
        s.tyreDeform[i] = m_tyreVisuals.state(i).deformation;
        s.tyreFlatSpot[i] = m_tyreVisuals.state(i).flatSpot;
    }
    // The clock a time-limited session ends on. During the countdown the
    // remaining time IS the countdown, and an open or lap-limited session has
    // no clock to show, so both stay negative (nothing drawn).
    s.sessionTimeLeftS = (m_sessionTimeLimitSeconds > 0 && m_sessionPhase == PHASE_GREEN_FLAG)
                             ? static_cast<float>(m_timeRemaining)
                             : -1.f;

    s.position = posNow; s.totalCars = carsNow; s.lap = m_lapTimer.completedLaps() + 1; s.totalLaps = m_totalLaps;
    s.currentTimeMs = m_lapTimer.currentTimeMs(); s.lastTimeMs = m_lapTimer.lastTimeMs(); s.bestTimeMs = m_lapTimer.bestTimeMs();
    s.sector = m_lapTimer.sectorIndex(); s.inPit = st.inPitLane; s.pitLimiter = st.pitLimiterActive;
    // Lap and sector deltas against the best lap, from the same timer the lap
    // clock above comes from: the HUD draws one bar and three chips, and the
    // maths lives in the UI layer (ui/LapDelta.h) so it is testable alone.
    {
        const ui::LapDelta delta = ui::lapDeltaFrom(m_lapTimer);
        s.deltaHasReference = delta.hasReference;
        s.deltaMs = delta.lapMs;
        s.sectorDeltaCount = delta.sectorsMeasured;
        for (int i = 0; i < 3; ++i) s.sectorDeltaMs[i] = delta.sectorMs[i];
    }
    // Roadmap 2.10 — one rendering point for the state the loop owns: the pit
    // service status wins while a stop is running, otherwise the garage exit
    // status (skipping the "ON TRACK" no-op label). Empty = stay quiet.
    s.statusText[0] = '\0';
    const std::string garageNow = garageStatus();
    // A stranded car speaks before the pit labels: it is out on track with the
    // marshals on the way, and the countdown is the one thing the driver can
    // act on (vai ai box prima che ti portino loro).
    char recoveryLine[32] = {};
    const bool stranded = recoveryStatusText(m_recovery, recoveryLine, sizeof(recoveryLine));
    if (!m_layoutNotice.empty()) {
        // A layout the host announced but this copy lacks outranks the pit
        // labels: it is the reason this car is on a different track length.
        std::snprintf(s.statusText, sizeof(s.statusText), "%s", m_layoutNotice.c_str());
    } else if (m_features.replay.isLoaded()) {
        // A replay is not a drive: say so, where in the recording we are, and
        // whether it is paused or running at an unusual speed.
        const ReplayRecorder& rec = m_features.replay;
        if (!rec.isPlaying())
            std::snprintf(s.statusText, sizeof(s.statusText), "REPLAY %.1f / %.1f s (paused)",
                          static_cast<double>(rec.playbackTime()),
                          static_cast<double>(rec.duration()));
        else if (rec.playbackSpeed() < 0.99f || rec.playbackSpeed() > 1.01f)
            std::snprintf(s.statusText, sizeof(s.statusText), "REPLAY %.1f / %.1f s  %.1fx",
                          static_cast<double>(rec.playbackTime()),
                          static_cast<double>(rec.duration()),
                          static_cast<double>(rec.playbackSpeed()));
        else
            std::snprintf(s.statusText, sizeof(s.statusText), "REPLAY %.1f / %.1f s",
                          static_cast<double>(rec.playbackTime()),
                          static_cast<double>(rec.duration()));
    } else if (stranded)
        std::snprintf(s.statusText, sizeof(s.statusText), "%s", recoveryLine);
    else if (pitServiceBusy())
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
    // Kinetika apps: the same live data the native panels draw, filled once here so
    // every app sees the step the HUD is about to show.
    {
        namespace fs = std::filesystem;
        std::snprintf(s.carName, sizeof(s.carName), "%s",
                      fs::path(m_carDir).filename().string().c_str());
        std::snprintf(s.trackName, sizeof(s.trackName), "%s", m_trackData.name.c_str());
        s.aeroDownforceN = st.aeroDownforce;
        s.aeroDragN = st.aeroDrag;
        s.rideHeightFrontM = st.rideHeightFrontM;
        s.rideHeightRearM = st.rideHeightRearM;
        s.aeroBalanceFront = st.aeroBalanceFront;
        s.drsActive = st.drsActive;
        s.drsAvailable = st.drsAvailable;
        s.fps = dtSeconds > 1.0e-5f ? 1.f / dtSeconds : 0.f;
        s.frameMs = dtSeconds * 1000.f;
        s.timeOfDay = m_timeOfDay;
        s.cameraMode = m_camera ? static_cast<int>(m_camera->mode()) : -1;
        s.replayLoaded = m_features.replay.isLoaded();
        s.replayPlaying = m_features.replay.isPlaying();
        s.replayTime = m_features.replay.playbackTime();
        s.replayDuration = m_features.replay.duration();
        s.aiCars = m_multiCar ? std::max(0, m_multiCar->carCount() - 1) : 0;
        for (int i = 0; i < 4; ++i) {
            s.brakeTempC[i] = st.brakeDiscTemp[i];
            s.brakeFade[i] = st.brakeFade[i];
            s.camberDeg[i] = st.tireCamber[i] * 57.2957795f;
            const auto& corner = m_vehicle->suspension().corner(i);
            s.suspCompressionM[i] = corner.compression;
            s.suspVelocity[i] = corner.velocity;
            s.suspLoadN[i] = corner.normalLoad;
        }
        const auto& rows = m_raceSession.standings();
        s.standingCount = std::min(ui::RaceHudSample::kHudStandings,
                                   static_cast<int>(rows.size()));
        for (int i = 0; i < s.standingCount; ++i) {
            const DriverStanding& d = rows[static_cast<std::size_t>(i)];
            ui::HudStanding& out = s.standings[i];
            out.position = d.position;
            out.laps = d.currentLap;
            out.bestLapSeconds =
                (d.bestLapTime > 0.f && d.bestLapTime < 1.0e8f) ? d.bestLapTime : -1.f;
            out.player = ps != nullptr && d.carIndex == ps->carIndex;
            out.disqualified = d.disqualified;
            std::snprintf(out.driver, sizeof(out.driver), "%s",
                          !d.driverName.empty() ? d.driverName.c_str() : d.carName.c_str());
        }
    }
    // Dev-mode scripting: the frame the script sees is the same one the HUD is
    // about to be handed, so a script cannot read numbers the driver does not.
    if (m_scripts.active()) {
        ScriptFrame frame;
        frame.dtSeconds = dtSeconds;
        frame.speedMs = static_cast<float>(st.speed);
        frame.rpm = static_cast<float>(m_vehicle->rpm());
        frame.fuelL = static_cast<float>(st.fuel);
        frame.gear = m_vehicle->currentGear();
        frame.lap = m_lapTimer.completedLaps() + 1;
        m_scripts.tick(frame);
        for (const std::string& line : m_scripts.takeLog())
            m_ui.console().addLine("[lua] " + line);
        // ks_notice is the one line a script can put on the HUD: copied every
        // frame, so an empty string clears it, and logged only when it changes.
        const std::string changed = m_scripts.takeNoticeChange();
        if (!changed.empty()) m_ui.console().addLine("[lua] " + changed);
        std::snprintf(s.scriptText, sizeof(s.scriptText), "%s", m_scripts.notice().c_str());
        if (!m_scripts.active() && !m_scripts.lastError().empty())
            m_ui.console().addLine("  scripting: stopped - " + m_scripts.lastError());
    }
    // The car's own digital dash: the display its data/data.json declares, read
    // once per car (the panel keeps the path it read) and fed by the same push
    // that feeds the HUD. KS_CAR_DASH names a document of its own, so a capture
    // can show a display on a car whose content declares none.
    if (const char* dashFile = std::getenv("KS_CAR_DASH"); dashFile && *dashFile)
        m_ui.carDash().loadFromFile(dashFile);
    else
        m_ui.carDash().loadFromCarFolder(m_carDir);
    m_ui.pushRaceSample(s);
#endif
}

bool SimulationLoop::setSpectating(bool on) {
    auto& spectator = ks::engine::sim::SpectatorMode::instance();
    if (on == spectator.isSpectating()) return spectator.isSpectating();
    if (!on) {
        // The classification belongs to the spectated session, not to the next
        // one: leaving the mode puts it away with the camera.
        spectator.showScoreboard(false);
        spectator.exitSpectatorMode();
        std::fprintf(stderr, "[spectate] off (the camera is back on the player)\n");
        return false;
    }
    spectator.enterSpectatorMode();
    spectator.setViewMode(ks::engine::sim::SpectatorViewMode::Driver);
    const int cars = m_multiCar ? m_multiCar->carCount() : 0;
    if (cars > 0) spectator.followNext(cars);
    std::fprintf(stderr, "[spectate] on: following car %d of %d\n",
                 spectator.followedDriver(), cars);
    return true;
}

int SimulationLoop::spectateNext() {
    auto& spectator = ks::engine::sim::SpectatorMode::instance();
    const int cars = m_multiCar ? m_multiCar->carCount() : 0;
    if (!spectator.isSpectating()) {
        setSpectating(true);
        return 0;
    }
    if (cars > 0 && spectator.followNext(cars))
        std::fprintf(stderr, "[spectate] following car %d of %d\n",
                     spectator.followedDriver(), cars);
    return spectator.followedDriver();
}

void SimulationLoop::setPhotoOrbit(bool on) {
    if (!m_camera) return;
    if (!on) {
        m_camera->setPhotoOrbit(false, {});
        m_camera->setMode(m_photoPrevMode);
        return;
    }
    m_photoPrevMode = m_camera->mode();
    m_camera->setMode(CameraController::Mode::Free);
    // The shot is of the car; with no car in the world, of what the camera was
    // already looking at, five metres ahead.
    ks::sim::vec3 target = m_camera->position() + m_camera->forward() * 5.0f;
#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        const auto st = m_vehicle->getState();
        target = {static_cast<float>(st.position.x),
                  static_cast<float>(st.position.y) + 0.7f,
                  static_cast<float>(st.position.z)};
    }
#endif
    m_camera->setPhotoOrbit(true, target);
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
        // The meter needs one readback per step and runs with the camera:
        // the menu's idle path never reaches updateWeather, so this is the
        // only place a room is drawn frame after frame before a session.
        if (m_vulkanRenderer && m_showroom.config().autoExposure.declared()) {
            const float adapted = updateShowroomAutoExposure();
            if (adapted > 0.0f) m_vulkanRenderer->setExposure(adapted);
        }
        m_camera->update(dt, mat4(), 0.0f); // free camera: view + projection
        if (m_vulkanRenderer)
            m_vulkanRenderer->setCamera(m_camera->viewMatrix(),
                                        m_camera->projectionMatrix(),
                                        m_camera->nearPlane(), m_camera->farPlane());
        return;
    }

    mat4 carBody;
    float speedKmh = 0.0f;
    // Spectator view: the camera rides the car the spectator follows instead of
    // the player's. That was the whole of what SpectatorMode was missing - the
    // camera stack already takes a body matrix, it just always got the player's.
    bool spectated = false;
    {
        const auto& spectator = ks::engine::sim::SpectatorMode::instance();
        if (spectator.isSpectating() && m_multiCar) {
            if (CarEntry* watched = m_multiCar->getCar(spectator.followedDriver());
                watched != nullptr && watched->vehicle) {
                const auto ws = watched->vehicle->getState();
                carBody = toNativeMat4(ks::math::mat4::fromPositionRollPitchYaw(
                    {ws.position.x, ws.position.y, ws.position.z},
                    ws.rotation.x, ws.rotation.y, ws.rotation.z));
                speedKmh = ws.speed * 3.6f;
                spectated = true;
            }
        }
    }
#if HAS_VEHICLE_SIM
    if (!spectated && m_vehicle) {
        const auto st = m_vehicle->getState();
        // (roll, pitch, yaw) = (x, y, z): heading steers rotation.z, the yaw
        // channel of fromPositionRollPitchYaw.
        carBody = toNativeMat4(ks::math::mat4::fromPositionRollPitchYaw(
            {st.position.x, st.position.y, st.position.z},
            st.rotation.x, st.rotation.y, st.rotation.z));
        speedKmh = st.speed * 3.6f;
    }
#endif

    if (m_viewW > 0 && m_viewH > 0)
        m_camera->setAspectRatio(static_cast<float>(m_viewW) / static_cast<float>(m_viewH));
    if (m_input) {
        // Cockpit look, straight from the controls document: the freelook
        // speeds and the HMD ranges/exponents are read every frame, so an
        // edited controls.json takes effect without a restart. Numpad 8/2/4/6
        // look, numpad 5 recentres (the arrow keys stay the steering
        // alternates, so the look keys cannot collide with driving).
        m_camera->setControls(m_input->controlsConfig());
        m_camera->setLookKeys(m_input->isKeyDown(0x68),  // NUMPAD8
                              m_input->isKeyDown(0x62),  // NUMPAD2
                              m_input->isKeyDown(0x64),  // NUMPAD4
                              m_input->isKeyDown(0x66),  // NUMPAD6
                              m_input->isKeyDown(0x65)); // NUMPAD5
        // The same key recentres a tracked head: what the player faces now is
        // what the cockpit calls forward, for the look offset and the visor. Only
        // in the cockpit, because that is the only place the reference is used.
        // This is the keyboard's half; the right controller grip does the same
        // further down, where the visor's own frame is read.
        if (m_input->isKeyDown(0x65) && m_vrHeadPose.valid &&
            m_camera->mode() == CameraController::Mode::Cockpit)
            m_vrRecentre.recentre(m_vrHeadPose);
    }
    // Photo mode's orbit takes the keyboard itself (the session stops when it
    // opens): W/S walk the orbit's point along the view, A/D across it, Q/E down
    // and up. The mouse orbits it and the wheel zooms, from the window messages.
    if (m_input && m_camera->mode() == CameraController::Mode::Free &&
        m_camera->photoOrbit()) {
        const float f = (m_input->isKeyDown(0x57) ? 1.0f : 0.0f) -
                        (m_input->isKeyDown(0x53) ? 1.0f : 0.0f);
        const float r = (m_input->isKeyDown(0x44) ? 1.0f : 0.0f) -
                        (m_input->isKeyDown(0x41) ? 1.0f : 0.0f);
        const float u = (m_input->isKeyDown(0x45) ? 1.0f : 0.0f) -
                        (m_input->isKeyDown(0x51) ? 1.0f : 0.0f);
        if (f != 0.0f || r != 0.0f || u != 0.0f)
            m_camera->addPhotoOrbitPan(f * 8.0f, r * 8.0f, u * 4.0f);
    }
    m_camera->update(dt, carBody, speedKmh);

    // A tracked head, from the headset when one is running and from the
    // external tracker otherwise: the cockpit camera already knew how to follow
    // one (setHeadTrackingOffset), what it never had was the OpenXR pose.
    const auto& head = ks::engine::input::HeadTracking::instance();
    bool headDriven = false;
    // Taken on the frame the camera enters the cockpit and dropped when it
    // leaves, so chase, the garage or a photo-mode detour cannot leave a
    // reference behind. Calling it again is harmless: an existing reference is
    // never replaced. A headset that restarted brings a new play space, which is
    // noticed by its serial rather than by catching a stopped frame.
    bool playSpaceChanged = false;
    if (auto* xrManager = ks::device::XrManager::instance()) {
        const unsigned long long serial = xrManager->sessionSerial();
        playSpaceChanged = serial != m_vrSessionSerial;
        m_vrSessionSerial = serial;
    }
    const bool cockpitNow = m_camera->mode() == CameraController::Mode::Cockpit;
    ks::sim::vrHeadUpdateReference(m_vrRecentre, m_vrHeadPose, cockpitNow, playSpaceChanged);
    // The right grip recentres the head too: with the headset on, numpad 5 is a
    // key nobody can find by touch. Rising edge only, or holding the grip would
    // drag the reference along with the head and the view would never move.
    if (m_vrFrame.recentrePressed && !m_vrRecentreHeld && cockpitNow && m_vrHeadPose.valid)
        m_vrRecentre.recentre(m_vrHeadPose);
    m_vrRecentreHeld = m_vrFrame.recentrePressed;
    if (cockpitNow) {
        if (m_vrHeadPose.valid) {
            // A headset outranks a separate tracker: if both are live, the one
            // the player is wearing is the truth. The pose reaches the camera as
            // an offset from the recentre point, not as the play space's own
            // position: see VrHeadRecentre.h. The ranges are the visor's, read
            // from controls.json every frame like the rest of them.
            ks::sim::VrHeadRange range;
            if (m_input) {
                const ControlsConfig& controls = m_input->controlsConfig();
                range.leftRight = static_cast<float>(controls.vrHeadLeftRightRange);
                range.upDown = static_cast<float>(controls.vrHeadUpDownRange);
                range.foreAft = static_cast<float>(controls.vrHeadForeAftRange);
            }
            const ks::sim::VrHeadTracked tracked =
                ks::sim::vrHeadTrack(m_vrHeadPose, m_vrRecentre, range);
            if (tracked.valid) {
                m_camera->setHeadTrackingDelta(
                    {tracked.position[0], tracked.position[1], tracked.position[2]},
                    {tracked.forward[0], tracked.forward[1], tracked.forward[2]},
                    {tracked.up[0], tracked.up[1], tracked.up[2]});
                headDriven = true;
            }
        }
        if (!headDriven && head.isPoseValid()) {
            const auto& pose = head.getCurrentPose();
            ks::math::quat orientation = pose.orientation;
            const ks::math::vec3 localForward = orientation.rotatedVec({0, 0, 1});
            const ks::math::vec3 localUp = orientation.rotatedVec({0, 1, 0});
            m_camera->setHeadTrackingOffset(
                {pose.position.x * 0.001f, pose.position.y * 0.001f,
                 pose.position.z * 0.001f},
                {localForward.x, localForward.y, localForward.z},
                {localUp.x, localUp.y, localUp.z});
            headDriven = true;
        }
    }
    if (headDriven) m_camera->update(dt, carBody, speedKmh);
    else m_camera->clearHeadTracking();

    if (m_vulkanRenderer)
        m_vulkanRenderer->setCamera(m_camera->viewMatrix(), m_camera->projectionMatrix(),
                                    m_camera->nearPlane(), m_camera->farPlane());
}

void SimulationLoop::pumpThemeSounds() {
    bool anyNamed = false;
    for (int i = 0; i < ks::sim::ui::kUiSoundCount; ++i)
        anyNamed = anyNamed || !ks::sim::ui::NativeUiHub::themeSound[i].empty();

    // The menu is interactive before any car or showroom asks for audio, so a
    // theme that names clicks is the one thing that opens the device up front.
    // A theme that names none opens nothing: an idle mixer is not a feature.
    if (!m_audio && !m_themeSoundsLoaded && anyNamed) {
        auto audio = std::make_unique<SimulatorAudio>();
        if (audio->initialize())
            m_audio = std::move(audio);
        else {
            m_themeSoundsLoaded = true; // one complaint, not one per frame
            std::fprintf(stderr, "[ui] no audio device: the theme's clicks stay silent\n");
        }
    }
    if (m_audio && !m_themeSoundsLoaded) {
        m_themeSoundsLoaded = true;
        for (int i = 0; i < ks::sim::ui::kUiSoundCount; ++i) {
            const std::string& path = ks::sim::ui::NativeUiHub::themeSound[i];
            if (path.empty()) continue;
            const auto sound = static_cast<ks::sim::ui::UiSound>(i);
            if (m_audio->loadUiSound(i, path, ks::sim::ui::NativeUiHub::themeSoundGain))
                std::printf("[ui] sound %s loaded from %s\n", ks::sim::ui::uiSoundKey(sound),
                            path.c_str());
            else
                std::fprintf(stderr, "[ui] sound %s not loaded: %s\n",
                             ks::sim::ui::uiSoundKey(sound),
                             m_audio->uiSoundError(i).c_str());
        }
    }

    ks::sim::ui::UiSound events[8];
    const int count = m_ui.drainSounds(events, 8);
    if (count <= 0) return;
    // No device: the click has nowhere to go, and it must not wait in a queue
    // for a device that may never open.
    if (!m_audio) return;
    for (int i = 0; i < count; ++i) m_audio->playUiSound(static_cast<int>(events[i]));
}

void SimulationLoop::render(float dt) {
    pumpThemeSounds();
    // KS_INSPECT_AT="x,y": run the part pick once, in design units, a second
    // into the room. The click path is the same function; this is how a capture
    // shows a selection on a machine whose synthetic pointer never reaches the
    // window.
    if (showroomActive() && !m_carVisualEntities.empty()) {
        static const std::vector<std::pair<float, float>> inspectPoints = []() {
            std::vector<std::pair<float, float>> points;
            const char* at = std::getenv("KS_INSPECT_AT");
            if (at == nullptr) return points;
            const std::string list(at);
            std::size_t start = 0;
            while (start <= list.size()) {
                const std::size_t separator = list.find(';', start);
                const std::string piece = list.substr(
                    start, separator == std::string::npos ? std::string::npos : separator - start);
                float px = 0.f;
                float py = 0.f;
                if (std::sscanf(piece.c_str(), "%f,%f", &px, &py) == 2)
                    points.emplace_back(px, py);
                if (separator == std::string::npos) break;
                start = separator + 1;
            }
            return points;
        }();
        // One pick every 70 frames from frame 90: "a,b;c,d" selects one part
        // and then, forty frames before the capture, clicks somewhere else.
        static int inspectFrames = 0;
        static std::size_t nextPoint = 0;
        ++inspectFrames;
        if (nextPoint < inspectPoints.size() &&
            inspectFrames == 90 + static_cast<int>(nextPoint) * 70) {
            pickShowroomPart(inspectPoints[nextPoint].first, inspectPoints[nextPoint].second);
            ++nextPoint;
        }
        // KS_INSPECT_EDIT="roughness=0.5;emissive=1": the headless equivalent
        // of focusing those rows and pressing ENTER, through the same commit.
        // Labels are the sheet's, values the row's own units.
        static bool inspectEditsDone = false;
        if (!inspectEditsDone && nextPoint >= inspectPoints.size() &&
            std::getenv("KS_INSPECT_EDIT") != nullptr) {
            inspectEditsDone = true;
            const std::string edits(std::getenv("KS_INSPECT_EDIT"));
            std::size_t start = 0;
            while (start <= edits.size()) {
                const std::size_t separator = edits.find(';', start);
                const std::string piece = edits.substr(
                    start, separator == std::string::npos ? std::string::npos
                                                         : separator - start);
                const std::size_t equals = piece.find('=');
                if (equals != std::string::npos) {
                    const std::string label = piece.substr(0, equals);
                    const float display =
                        static_cast<float>(std::strtod(piece.c_str() + equals + 1, nullptr));
                    const InspectorPanelRow* row = m_ui.inspector().findEditable(label);
                    if (row == nullptr) {
                        std::fprintf(stderr, "[inspector] edit ignored: no editable row '%s'\n",
                                     label.c_str());
                    } else {
                        const float clamped = std::min(std::max(display, row->edit.min),
                                                      row->edit.max);
                        m_ui.inspector().setFooter(commitInspectorEdit(
                            m_ui.inspector().materialName(), row->edit.key,
                            inspectorCommitText(inspectorPropertyValue(row->edit, clamped))));
                    }
                }
                if (separator == std::string::npos) break;
                start = separator + 1;
            }
        }
    }
    // The inspector's selection wash is uploaded here, where no frame is in
    // flight; a click only queues it.
    applyPendingTints();
    ensureScenePipeline();
    syncUiFromVehicle(dt);
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
            std::fprintf(stderr,
                         "[render] %.1f fps submitted=%u drawn=%u culled=%u occluded=%u "
                         "mirror=%u mirrorOccl=%u\n",
                         s_frame / elapsedReport, st.submitted, st.drawn, st.culled, st.occluded,
                         st.mirrorDrawn, st.mirrorOccluded);
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
            // body * node: the car's pose, then the node's own spin/bulge/offset
            // (a wheel; identity for every other mesh).
            const ks::math::mat4 model = mesh.hasNodeTransform()
                ? ks::ecs::worldMatrix(t) * ks::ecs::nodeMatrix(mesh)
                : ks::ecs::worldMatrix(t);
            m_vulkanRenderer->drawMesh(mesh.meshName, toNativeMat4(model));
        });
        // Roadmap P1: sprite quads, recorded with the same draw list (before
        // the UI overlay so the HUD is never covered by world-space dust).
        updateAndDrawParticles(dt);
        // KS_HIDE_MENU keeps the interface out of a capture (see the m_uiGpu
        // branch below, which is the other way the UI can reach a frame).
        // KS_HIDE_MENU keeps the interface out of a capture, but an open
        // developer console is the calibration tool the capture exists to show:
        // the menu stays hidden, the console still draws.
        // KS_HIDE_MENU hides the game interface, not the developer tools: the
        // console and the showroom inspector are what a capture exists to show.
        if (std::getenv("KS_HIDE_MENU") == nullptr || m_ui.console().isVisible() ||
            m_ui.inspector().isVisible() || setupScreenVisible() ||
            std::getenv("KS_APPS") != nullptr || std::getenv("KS_APPS_VISIBLE") != nullptr) {
            m_vulkanRenderer->drawUi(m_ui.renderer());
        }
        m_vulkanRenderer->endFrame();
    } else if (m_uiGpu) {
        // KS_HIDE_MENU keeps the interface out of a capture: the fixed
        // showroom views exist to judge materials, and the menu sits right
        // on top of the car.
        if (std::getenv("KS_HIDE_MENU") == nullptr || m_ui.console().isVisible() ||
            m_ui.inspector().isVisible() || setupScreenVisible() ||
            std::getenv("KS_APPS") != nullptr || std::getenv("KS_APPS_VISIBLE") != nullptr) {
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
    // Full body attitude, (roll, pitch, yaw) = (x, y, z): heading steers the
    // yaw channel. With position only, the visual slid through corners
    // without ever turning, while the camera already followed the body.
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
    // The blob shadows are placed from the same pose, once per frame: they are
    // drawn, not simulated, so they follow the visual the player sees.
    updateWheelShadows(pos, rot);
    updateGrassField(pos);
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
        // A baked venue writes its pit box cubes as meshes like any other: the
        // row reads their centres elsewhere, and they are not drawn outside
        // the developer console.
        if (!ks::sim::showsPitBoxMarkers() && ks::sim::isPitBoxMarkerName(name)) continue;
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
    // The wheel nodes went with them: the next car binds its own.
    for (auto& wheel : m_wheelVisuals) wheel = WheelVisual{};
    m_wheelsBound = false;
    // ...and so did the blob shadows, which are entities of their own.
    for (auto& shadow : m_wheelShadows) {
        if (shadow.entity != ks::ecs::kNullEntity) scene().destroy(shadow.entity);
        if (!shadow.meshName.empty() && m_vulkanRenderer)
            m_vulkanRenderer->destroyMesh(shadow.meshName);
        shadow = WheelShadowVisual{};
    }
    m_wheelShadowsBound = false;
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
    // A new venue answers for its own stalls: the previous one's markers must
    // not place this track's car.
    m_trackPitMarkers.clear();
    if (!m_vulkanRenderer) return; // headless: metadata-only load is valid
    const std::string baked = ks::sim::findBakedManifestDir(trackDir);
    if (baked.empty()) {
        // Unbaked or mesh-less track: keep the current scene on screen
        // instead of blanking it (this is the stability guarantee), and
        // print how to bake a .kn5 when there is one to bake.
        namespace fs = std::filesystem;
        std::string kn5Path;
        if (fs::is_directory(trackDir)) {
            // The same choice loadTrackFolder makes: the file named like the
            // folder, else the largest. A track ships deco .kn5 beside the
            // circuit, and the small one is not the venue.
            const std::string folderStem = fs::path(trackDir).filename().string();
            std::uintmax_t largest = 0;
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(trackDir, ec)) {
                const fs::path path = e.path();
                if (path.extension() != ".kn5") continue;
                if (path.stem().string() == folderStem) { kn5Path = path.string(); break; }
                std::error_code sizeEc;
                const std::uintmax_t size = fs::file_size(path, sizeEc);
                if (sizeEc || size <= largest) continue;
                largest = size;
                kn5Path = path.string();
            }
        }
        if (!kn5Path.empty()) {
            // No bake, but the venue is there: read its own .kn5 in memory,
            // with the same reader the showroom hall uses. Nothing is written
            // to disk; the textures are decoded from the file.
            std::fprintf(stderr, "[scene] %s: no bake, reading the .kn5 in memory\n",
                         kn5Path.c_str());
            despawnSceneEntities();
            if (loadTrackKn5Visuals(kn5Path)) return;
            std::fprintf(stderr, "[scene] %s: .kn5 unreadable, the current scene stays\n",
                         kn5Path.c_str());
            return;
        }
        // Neither geometry nor a bake. A data-only track (ai/ + data/) is
        // legitimate and the previous scene stays on screen on purpose, but
        // saying so beats leaving an unchanged view to be diagnosed.
        std::fprintf(stderr,
                     "[scene] %s has no .kn5 and no baked cache: nothing to load, "
                     "the current scene stays on screen\n",
                     trackDir.c_str());
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
    releaseShowroomScene();
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

bool SimulationLoop::loadTrackKn5Visuals(const std::string& kn5Path) {
    if (!m_vulkanRenderer) return false;
    // A reload must not stack the previous layout on top of the new one.
    for (int i = 0; i < m_trackKn5Meshes; ++i)
        m_vulkanRenderer->destroyMesh("track_" + std::to_string(i));
    m_trackKn5Meshes = 0;

    float kn5Min[3] = {0.0f, 0.0f, 0.0f};
    float kn5Max[3] = {0.0f, 0.0f, 0.0f};
    int meshes = 0;
    std::vector<WorldPose> pitMarkers;
    if (!loadKn5Meshes(kn5Path, "track_", false, nullptr, kn5Min, kn5Max, &meshes, nullptr,
                       &pitMarkers))
        return false;
    m_trackKn5Meshes = meshes;
    // The stalls the venue itself placed, kept for the garage row: a box in a
    // stall beats a box on the lane's centre line.
    m_trackPitMarkers = std::move(pitMarkers);
    if (!m_trackPitMarkers.empty())
        std::printf("[scene] %zu AC pit marker(s) in the .kn5\n", m_trackPitMarkers.size());
    m_spawnedSceneDir = kn5Path;
    releaseShowroomScene();
    std::printf("[scene] track %s loaded from its .kn5 (%d meshes)\n", kn5Path.c_str(),
                meshes);

    // A .kn5 carries no AC spawn marker, so the grid anchor is the AI line the
    // track already ships (the baked path uses the marker, then the same line).
    vec3 spawn;
    float heading = 0.0f;
    if (m_multiCar && m_multiCar->gridPose(0, 6.0f, spawn, heading)) {
#if HAS_VEHICLE_SIM
        if (m_vehicle) {
            auto& st = m_vehicle->state();
            st.position = {spawn.x, spawn.y, spawn.z};
            st.heading = heading;
        }
#endif
        std::fprintf(stderr, "[scene] anchored on the AI line (%.1f, %.1f, %.1f)\n",
                     spawn.x, spawn.y, spawn.z);
    }
    return true;
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
    m_carPaintExplicit = true;
    std::printf("[paint] body tint %.3f %.3f %.3f\n", m_carPaint[0], m_carPaint[1],
                m_carPaint[2]);
    // The material block is static per mesh, so the body has to be re-uploaded.
    if (m_carLoaded && !m_carDir.empty()) ensureCarVisual(m_carDir);
}

void SimulationLoop::ensureCarVisual(const std::string& carDir) {
    if (!m_vulkanRenderer) return;
    // The meshes are about to be replaced: whatever was inspected no longer
    // exists, and the panel must not describe a part that is gone.
    clearShowroomSelection();
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

// The .kn5's own record as CONTENT rows (model.json's kn5 block): the file the
// car is built from and the skin it wears — the two choices the loader used to
// guess on every start. A list the folder does not offer gets no row, so a
// value on screen is always one the car can actually be loaded with.
std::vector<std::string> SimulationLoop::carImportRowLabels() const {
    return sim::kn5ImportRowLabels(m_carDir);
}

// Cycling a row steps to the next choice the folder offers, writes it into the
// car's model document and rebuilds the car from it: the record and the model
// on screen are never allowed to disagree. It does not run inside a session,
// where the car is placed by its physics pose and its textures are in use.
void SimulationLoop::cycleCarImportRow(int row) {
    if (m_carDir.empty()) return;
    if (isRunning()) {
        std::fprintf(stderr, "[kn5] stop the session to change the car's model\n");
        return;
    }
    sim::Kn5ModelParameters recorded;
    if (!sim::readKn5Parameters(m_carDir, recorded, nullptr)) return;
    if (!sim::cycleKn5Parameters(m_carDir, row, recorded)) return;
    const std::string& chosen = row == 0 ? recorded.model : recorded.skin;
    std::string error;
    if (!sim::writeKn5Parameters(m_carDir, recorded, &error)) {
        std::fprintf(stderr, "[kn5] parameters not saved: %s\n", error.c_str());
        return;
    }
    std::fprintf(stderr, "[kn5] %s -> %s (model.json)\n", row == 0 ? "model" : "skin",
                 chosen.c_str());
    reloadCarAfterParameters();
}

// The car's recorded parameters as the SKINS page asks for them: the folders it
// ships, and the id the document names (empty when it names none).
std::vector<sim::CarSkin> SimulationLoop::carSkinChoices() const {
    std::vector<sim::CarSkin> skins;
    sim::readCarSkins(m_carDir, skins);
    return skins;
}

std::string SimulationLoop::activeCarSkin() const {
    sim::Kn5ModelParameters recorded;
    if (!sim::readKn5Parameters(m_carDir, recorded, nullptr)) return std::string();
    return recorded.skin;
}

// CHOOSING a skin on CONTENT -> SKINS: only a folder the car actually ships is
// accepted (the loader would reject anything else and fall back), it is written
// into the car's document, and the car is rebuilt with it.
void SimulationLoop::setCarSkin(const std::string& id) {
    if (m_carDir.empty() || id.empty()) return;
    if (isRunning()) {
        std::fprintf(stderr, "[kn5] stop the session to change the car's skin\n");
        return;
    }
    std::vector<sim::CarSkin> skins;
    sim::CarSkin skin;
    if (!sim::readCarSkins(m_carDir, skins) || !sim::findCarSkin(skins, id, skin)) {
        std::fprintf(stderr, "[kn5] skin '%s' is not shipped by %s\n", id.c_str(),
                     m_carDir.c_str());
        return;
    }
    sim::Kn5ModelParameters recorded;
    if (!sim::readKn5Parameters(m_carDir, recorded, nullptr)) return;
    recorded.skin = id;
    std::string error;
    if (!sim::writeKn5Parameters(m_carDir, recorded, &error)) {
        std::fprintf(stderr, "[kn5] skin not saved: %s\n", error.c_str());
        return;
    }
    std::fprintf(stderr, "[kn5] skin -> %s (model.json)\n", id.c_str());
    reloadCarAfterParameters();
}

bool SimulationLoop::reloadCarAfterParameters() {
    // The skin's textures are cached under their file names and the cache
    // answers "already uploaded": without dropping it the car would keep the
    // skin it just left. A change of car content is exactly a cache flush.
    if (m_vulkanRenderer) m_vulkanRenderer->clearTextureCache();
    despawnCarVisuals();
    if (loadCarMeshesFromKn5(m_carDir)) return true;
    std::fprintf(stderr, "[kn5] car reload failed for %s\n", m_carDir.c_str());
    return false;
}

// Multiplayer (roadmap 3.1). All three bodies are no-ops unless the build
// carries HAS_KSNET and the transport is actually running.
// --- GAP_MATRIX P0.5: replay record / playback ----------------------------
// ReplayRecorder is pure data: frames, playhead, clock. The four entry points
// below are what makes it a feature — the loop names and saves the files,
// drives the car from the playhead while it runs, and lists what is on disk.

bool SimulationLoop::updateReplayPlayback(float dt) {
    ReplayRecorder& rec = m_features.replay;
    if (!rec.isPlaying()) return false;
    rec.updatePlayback(dt);
    if (!rec.isPlaying()) return false; // the recording ran out on this step
    const ReplayFrame f = rec.getPlaybackFrame();
#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        ks::physics::SimulationState& s = m_vehicle->state();
        s.position = {f.position.x, f.position.y, f.position.z};
        s.rotation = {f.rotation.x, f.rotation.y, f.rotation.z};
        s.heading = f.rotation.z; // yaw channel (rotation.z), same as net sync
        // Velocity is not in the file: the pose is authoritative, and a stale
        // speed would drag the camera and the damage model along with it.
        s.velocity = {0.0f, 0.0f, 0.0f};
        s.speed = f.speed;
        s.rpm = f.rpm;
        s.gear = f.gear;
        s.throttle = f.throttle;
        s.brake = f.brake;
        s.steering = f.steering;
    }
#endif
    return true;
}

void SimulationLoop::recordReplayStep() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle || !m_features.replay.isRecording()) return;
    if (m_features.replay.isPlaying()) return; // never record a replay into itself
    const auto& s = m_vehicle->getState();
    m_features.replay.recordFrame(s, static_cast<float>(s.throttle),
                                  static_cast<float>(s.brake),
                                  static_cast<float>(s.steering));
#endif
}

bool SimulationLoop::startReplayPlayback(const std::string& path) {
    ReplayRecorder& rec = m_features.replay;
    if (rec.isRecording()) toggleReplayRecording(); // the file is the new truth
    if (!rec.loadReplay(path)) return false;
    rec.startPlayback();
    m_ui.menu().setVisible(false);
    printf("Replay: playing %s (%d frames, %.1f s)\n", path.c_str(), rec.frameCount(),
           static_cast<double>(rec.duration()));
    return true;
}

void SimulationLoop::stopReplayPlayback() { m_features.replay.stopPlayback(); }

void SimulationLoop::pauseReplayPlayback() { m_features.replay.pausePlayback(); }

void SimulationLoop::resumeReplayPlayback() { m_features.replay.resumePlayback(); }

void SimulationLoop::seekReplayBy(float deltaSec) {
    ReplayRecorder& rec = m_features.replay;
    if (!rec.isLoaded()) return;
    rec.seekTo(rec.playbackTime() + deltaSec);
}

void SimulationLoop::setReplaySpeed(float speed) {
    m_features.replay.setPlaybackSpeed(speed);
    printf("Replay: speed %.2fx\n", static_cast<double>(m_features.replay.playbackSpeed()));
}

std::vector<std::string> SimulationLoop::replayFiles() const {
    namespace fs = std::filesystem;
    std::vector<std::string> found;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("replay", ec)) {
        if (ec) break;
        std::error_code typeEc;
        if (entry.is_regular_file(typeEc) && entry.path().extension() == ".ksrep")
            found.push_back(entry.path().string());
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::vector<std::pair<std::string, std::string>> SimulationLoop::replayChoices() const {
    namespace fs = std::filesystem;
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto& file : replayFiles())
        rows.emplace_back(fs::path(file).filename().string(), file);
    return rows;
}

std::string SimulationLoop::toggleReplayRecording() {
    ReplayRecorder& rec = m_features.replay;
    if (!rec.isRecording()) {
        if (rec.isPlaying()) rec.stopPlayback();
        rec.startRecording();
        m_ui.chat().addLine("Recording started");
        return {};
    }
    rec.stopRecording();
    return saveReplayRecording();
}

std::string SimulationLoop::saveReplayRecording() {
    ReplayRecorder& rec = m_features.replay;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories("replay", ec);
    for (int n = 1; n < 1000; ++n) {
        char name[64];
        std::snprintf(name, sizeof(name), "replay/session_%03d.ksrep", n);
        if (fs::exists(name, ec)) continue;
        if (!rec.saveReplay(name)) return {};
        printf("Replay: saved %s (%d frames, %.1f s)\n", name, rec.frameCount(),
               static_cast<double>(rec.duration()));
        m_ui.chat().addLine(std::string("Replay saved: ") + name);
        return name;
    }
    return {};
}

bool SimulationLoop::promoteAiCandidate() {
    // P0.2 commit (REPORT §6, FUN_140296ae0): AC's learning writes the
    // candidate and a commit overwrites the real line from it. The reader
    // already handles the binary .ai layout the candidate uses.
    if (m_trackData.directory.empty()) return false;
    const std::string cand = m_trackData.directory + "/ai/fast_lane.ai.candidate";
    ks::ai::AiSpline spline = ks::ai::AiFileReader::readSpline(cand);
    if (spline.points.empty()) return false;
    const std::string dst = m_trackData.directory + "/ai/fast_lane.ai";
    if (!ks::ai::AiFileWriter::writeSpline(dst, spline)) return false;
    std::fprintf(stderr, "[ai] promoted candidate: %s (%zu points)\n",
                 dst.c_str(), spline.points.size());
    m_ui.chat().addLine("AI line updated from candidate");
    return true;
}

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

#if HAS_KSNET
void SimulationLoop::applyRemoteCarDamage(const net::CarDamageMessage& msg) {
    if (!m_multiCar) return;
    // The host's own car is simulated by the host: a snapshot travelling back
    // to its author would let the network overwrite the physics that made it.
    if (msg.carId == m_ownNetCarId) return;
    const auto it = m_remoteCarIds.find(msg.carId);
    if (it == m_remoteCarIds.end()) return; // spawn message still in flight
    CarEntry* car = m_multiCar->getCar(it->second);
    if (!car || !car->vehicle) return;
    applyCarDamageMessage(car->vehicle->damage(), msg);
}
#endif

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
    s.heading = state.rotZ;
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
    auto* xrManager = ks::device::XrManager::instance();
    if (xrManager->isInitialized()) xrManager->pollEvents();
    // Controllers -> InputManager, before the step reads the pedals.
    pollVrInput();
    // The video options page asks for a rebuild of every pipeline from the
    // .spv on disk; the renderer owns the device wait and the reload, and this
    // is before the frame opens, so nothing is recording. A dev hook
    // (KS_RELOAD_SHADERS_AFTER=<seconds>) drives the same path headless.
    if (m_vulkanRenderer) {
        static const double reloadAfter = []() {
            const char* value = std::getenv("KS_RELOAD_SHADERS_AFTER");
            return value ? std::atof(value) : 0.0;
        }();
        static bool armed = false;
        if (!armed && reloadAfter > 0.0) {
            armed = true;
            std::printf("[shaders] reload hook armed: %.1f s\n", reloadAfter);
        }
        if (m_ui.menu().takeShaderReloadRequest()) m_vulkanRenderer->reloadShaders();
        if (reloadAfter > 0.0) {
            static const auto started = std::chrono::steady_clock::now();
            static bool reloaded = false;
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            if (!reloaded && elapsed >= reloadAfter) {
                reloaded = true;
                m_vulkanRenderer->reloadShaders();
            }
        }
    }

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
        if (xrManager->isSessionRunning()) xrManager->beginXRFrame();
        render();
        if (xrManager->isFrameBegun()) xrManager->endXRFrame();
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
            // Replay playback owns the car while it runs: the recorded pose and
            // controls are authoritative, so the driver's input, the garage
            // hold and the physics step are skipped for this one step.
            const bool replayDrives = updateReplayPlayback(static_cast<float>(m_physicsDt));
            if (!replayDrives) {
                applyInput();
                // Roadmap 2.7: garage exit / pit entry runs before the physics step
                // so its hold, snap and pit limiter win over the raw driver input
                // for this step (updatePitRepair runs after it, on the same phase).
                updateGarageExit(static_cast<float>(m_physicsDt));
#if HAS_VEHICLE_SIM
                if (m_vehicle) {
                    // The line's heights, not a flat plane: same corridor the
                    // AI rides, rate-clamped so spline noise never pops the
                    // body. Off-corridor (paddock, far runoff) the spawn
                    // ground simply stays. Slope follow rides along: pitch
                    // from the axle heights, roll from the track across —
                    // both smoothed inside setGroundTilt.
                    if (m_multiCar) {
                        const auto pre = m_vehicle->getState();
                        float gy = 0.f;
                        if (m_multiCar->groundHeightAt(pre.position.x,
                                                     pre.position.z, gy)) {
                            const float cur = static_cast<float>(
                                m_vehicle->groundHeight());
                            const float maxStep =
                                3.0f * static_cast<float>(m_physicsDt);
                            m_vehicle->setGroundHeight(
                                cur + std::clamp(gy - cur, -maxStep, maxStep));
                            const float fx = std::sin(pre.heading);
                            const float fz = std::cos(pre.heading);
                            const float hb =
                                static_cast<float>(m_vehicle->wheelBase()) *
                                0.5f;
                            const float ht =
                                static_cast<float>(m_vehicle->trackWidth()) *
                                0.5f;
                            float yF = gy, yR = gy, yL = gy, yRg = gy;
                            const bool okF = m_multiCar->groundHeightAt(
                                pre.position.x + fx * hb, pre.position.z + fz * hb, yF);
                            const bool okR = m_multiCar->groundHeightAt(
                                pre.position.x - fx * hb, pre.position.z - fz * hb, yR);
                            const bool okL = m_multiCar->groundHeightAt(
                                pre.position.x + fz * ht, pre.position.z - fx * ht, yL);
                            const bool okRg = m_multiCar->groundHeightAt(
                                pre.position.x - fz * ht, pre.position.z + fx * ht, yRg);
                            if (okF && okR && okL && okRg && hb > 0.5f &&
                                ht > 0.2f) {
                                // Nose-up is negative pitch on this composition
                                // (rx tips +Z down for positive angles).
                                const float pitch = -std::atan2(
                                    yF - yR, 2.0f * hb);
                                const float roll = std::atan2(
                                    yRg - yL, 2.0f * ht);
                                m_vehicle->setGroundTilt(
                                    pitch, roll,
                                    static_cast<float>(m_physicsDt));
                            }
                        }
                    }
                    // The ground under each tyre, from the same query the track
                    // limits read, so leaving the track costs grip and not just
                    // a penalty.
                    updateWheelSurfaceGrip();
                    m_vehicle->updatePhysics(m_physicsDt);
                }
#endif
                // Roadmap 2.7: box service at the physics rate, so the control hold
                // (PitLaneRepair::holdCar) wins over the driver's next input instead
                // of being overwritten by applyInput() in the same step.
                updatePitRepair(static_cast<float>(m_physicsDt));
            }
            // A running recording samples the same clock, never while a replay
            // is driving (the replay is the truth for that step).
            recordReplayStep();
            // Trackside marshals watch the same field on the same clock: an
            // incident raised by this step is flagged before the next frame.
            updateTracksidePosts(static_cast<float>(m_physicsDt));
            // ...and the yellow is not the end of it: a car that stays there
            // with a dead engine is recovered to its box on the same clock.
            updateTrackRecovery(static_cast<float>(m_physicsDt));
            // AI cars freeze on the grid during the countdown and race once it
            // is over; physics runs at the same fixed 1 kHz as the player's.
            if (m_multiCar && !m_raceSession.isCountingDown())
                m_multiCar->update(m_physicsDt);
            if (m_sessionPhase == PHASE_GREEN_FLAG) updateLapAndSurface(m_physicsDt);
            // The wheel LED strip belongs to the step that just ran: rpm, gear
            // and fuel come from this step's state, and the shift flash
            // advances by the same fixed dt.
            updateWheelLeds(static_cast<float>(m_physicsDt));
            // ...and the tyres' own visual state, on the same clock: first the
            // state, then the node transforms it asks for.
            updateTyreVisuals(static_cast<float>(m_physicsDt));
            updateWheelVisuals(static_cast<float>(m_physicsDt));
            // The session telemetry row, on the same fixed clock.
            sampleSessionTelemetry(static_cast<float>(m_physicsDt));
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
        torqueNm *= static_cast<float>(m_input->controlsConfig().ffbGain);
        if (m_ffb) m_ffb->updateFFB(torqueNm);
    }
#endif
    // Weather evolution freezes while a modal overlay pauses the world.
    updateWeather(paused ? 0.0f : static_cast<float>(elapsed));
    applyDamageEffects();
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
            // The car's own [BRAKEDISC_FX]: ACTIVE = 0 means the content replaced
            // the disc shader, so there is no glow to show, and GLOW_MULT scales
            // what the thermal model produced. A cold disc is 0 either way: the
            // content scales the signal, it never invents one.
            if (!m_brakeDiscFx.active) glow = 0.0f;
            else glow = std::clamp(glow * static_cast<float>(m_brakeDiscFx.glowMult), 0.0f, 1.0f);
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
        else {
            // The rolling sound follows the ground the tyres are actually on
            // (garage.surfaceEffects): the loudest surface the four wheels
            // report wins, so two wheels in the gravel sound like gravel.
            const bool measured = m_wheelSurfaceGrip.measured[0] ||
                                  m_wheelSurfaceGrip.measured[1] ||
                                  m_wheelSurfaceGrip.measured[2] ||
                                  m_wheelSurfaceGrip.measured[3];
            if (!measured) {
                // No contact patch sampled (no wheel nodes, no racing line):
                // the grip-deficit guess is what the car sounded like before.
                if (ast.effectiveMu < 0.8f) surf = SimulatorAudio::SurfaceType::Grass;
            } else {
                switch (surfacefx::dominantSurfaceKind(m_wheelSurfaceGrip.kind,
                                                       m_wheelSurfaceGrip.measured)) {
                case GroundKind::Gravel: surf = SimulatorAudio::SurfaceType::Gravel; break;
                case GroundKind::Kerb: surf = SimulatorAudio::SurfaceType::Kerb; break;
                case GroundKind::Grass: surf = SimulatorAudio::SurfaceType::Grass; break;
                // Loose ground the mixer has no layer for: soil and sand roll
                // on the grass layer rather than pretending to be gravel.
                case GroundKind::Soil:
                case GroundKind::Sand: surf = SimulatorAudio::SurfaceType::Grass; break;
                default: break; // asphalt, or a surface nobody named
                }
            }
        }
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
        if (xrManager->isSessionRunning()) xrManager->beginXRFrame();
        render(static_cast<float>(elapsed));
        if (xrManager->isFrameBegun()) xrManager->endXRFrame();
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
            // Skidmark emitters: one per wheel FL..RR (previously a single
            // car-centre emitter that was never driven: the update call
            // below now feeds the real brake input and per-wheel rim
            // speed, with the emitter parked at the wheel's position).
            for (int w = 0; w < 4; ++w)
                m_particles.addEmitter(rainfx::makeSkidEmitter());  // index 4 + w
            // Brake-disc sparks: one per wheel, driven from the same disc
            // temperatures that feed the glow (setBrakeGlow above).
            for (int w = 0; w < 4; ++w)
                m_particles.addEmitter(rainfx::makeSparkEmitter()); // index 8 + w
            // Surface effects (garage.surfaceEffects): dust and stones per
            // wheel, from the ground that wheel is actually on. Parked at the
            // contact point and driven below, next to the skid and spark
            // emitters; a wheel on the asphalt leaves both off.
            for (int w = 0; w < 4; ++w)
                m_particles.addEmitter(surfacefx::makeDustEmitter());  // index 11 + w
            for (int w = 0; w < 4; ++w)
                m_particles.addEmitter(surfacefx::makeStoneEmitter()); // index 15 + w
            std::fprintf(stderr,
                         "[particles] emitters on (config %d, KS_PARTICLES %d): "
                         "dust + rain + spray + 4 skid + 4 spark + 4 surface dust + "
                         "4 stones up to %d sprites\n",
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
        // Skidmarks, one emitter per wheel: the wheel frame is the car's
        // travel direction, offsets are half the wheelbase fore/aft and
        // half the track across (FL=+fwd+lat, FR=+fwd-lat, RL=-fwd+lat,
        // RR=-fwd-lat). Velocity sets the frame, not heading: it is the
        // direction the wheels actually roll toward, whatever convention
        // the mesh yaw uses, and nothing fires at a standstill anyway.
        // The lock test is the per-wheel rim speed (spin * radius) — this
        // replaces the call that passed brake=0/wheelSpeed=0 hardcoded and
        // so never enabled the emitter.
        const float vxz2 = st.velocity.x * st.velocity.x +
                           st.velocity.z * st.velocity.z;
        float fx = 0.f, fz = -1.f;
        if (vxz2 > 0.25f) {
            const float inv = 1.0f / std::sqrt(vxz2);
            fx = st.velocity.x * inv;
            fz = st.velocity.z * inv;
        }
        const float rx = -fz, rz = fx; // right = forward x up
        const float hb = static_cast<float>(m_vehicle->wheelBase()) * 0.5f;
        const float ht = static_cast<float>(m_vehicle->trackWidth()) * 0.5f;
        static constexpr float kFwdSign[4] = {1.f, 1.f, -1.f, -1.f};
        static constexpr float kLatSign[4] = {1.f, -1.f, 1.f, -1.f};
        const float roadSpeed = std::fabs(st.speed);
        for (int w = 0; w < 4; ++w) {
            const ks::math::vec3 wheelPos{
                carPos.x + fx * kFwdSign[w] * hb + rx * kLatSign[w] * ht,
                carPos.y,
                carPos.z + fz * kFwdSign[w] * hb + rz * kLatSign[w] * ht};
            const float rim = std::fabs(m_vehicle->wheelOmega(w) *
                                        m_vehicle->wheelRadius());
            rainfx::driveSkidEmitter(m_particles.emitter(3 + w), wheelPos,
                                     st.brake, rim);
            // Sparks off the disc, from the same temperature the glow uses.
            rainfx::driveSparkEmitter(m_particles.emitter(7 + w), wheelPos,
                                      st.brake, m_vehicle->brakes().discTemp(w),
                                      roadSpeed);
            // Dust and stones from the ground under this wheel. The kind comes
            // from the same contact-patch sample that sets its grip, so a wheel
            // sliding on the grass throws dust while the other three on the
            // asphalt throw nothing, and a wet track throws neither.
            const surfacefx::WheelSurfaceEffect fx = surfacefx::wheelSurfaceEffect(
                m_wheelSurfaceGrip.measured[w] ? m_wheelSurfaceGrip.kind[w]
                                               : GroundKind::Unknown,
                roadSpeed, m_weather.trackWetness);
            surfacefx::driveDustEmitter(m_particles.emitter(11 + w), wheelPos, fx.dustRate);
            surfacefx::driveStoneEmitter(m_particles.emitter(15 + w), wheelPos, fx.stoneRate,
                                         roadSpeed);
        }
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
