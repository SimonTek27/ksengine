/**
 * SimulationLoop.cpp — std-only / Qt-free (hardened path checks)
 */

#include "SimulationLoop.h"
#include "MultiCarManager.h"
#include "InputManager.h"
#include "NetworkManager.h"
#include "SetupGarage.h"
#include "FfbOutput.h"
#include "ApplySetup.h"
#include "SimulatorAudio.h"
#include "ui/UiGpuPass.h"
#include "ui/RaceTelemetryHud.h"
#include "UdpTelemetryBridge.h"
#include "TcpTelemetryBridge.h"

#include "engine/Engine.h"
#include "engine/scene/Components.h"
#include "engine/scene/SceneModule.h"
#include "engine/Graphics/RenderSystem.h"
#include "engine/devices/InputSystem.h"
#include "engine/Scripting/ScriptModule.h"

#include "adapters/assetto_corsa/AcSharedMemoryPublisher.h"
#include "adapters/assetto_corsa/AcSurfacesLoader.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>

#ifndef HAS_VEHICLE_SIM
#define HAS_VEHICLE_SIM 1
#endif

#if HAS_VEHICLE_SIM
#include "engine/physics/VehicleSimulator.h"
#endif

namespace ks::sim {

static constexpr uint8_t SESSION_RACE = 2;
static constexpr uint8_t PHASE_COUNTDOWN = 1;
static constexpr uint8_t PHASE_GREEN_FLAG = 2;
static constexpr uint8_t PHASE_CHECKERED_FLAG = 4;

namespace {
std::shared_ptr<ks::EngineModule> borrowModule(ks::EngineModule& m) {
    return std::shared_ptr<ks::EngineModule>(&m, [](ks::EngineModule*) {});
}
} // namespace

SimulationLoop::SimulationLoop() {
    m_input = std::make_unique<InputManager>();
#if HAS_VEHICLE_SIM
    m_vehicle = std::make_unique<ks::physics::VehicleSimulator>();
#endif
    m_lapTimer.configure(3);
}

SimulationLoop::~SimulationLoop() {
    stop();
    if (m_ffb) m_ffb->shutdown();
    if (m_shm) m_shm->close();
    if (m_udp) m_udp->close();
    if (m_tcp) m_tcp->close();
    ks::ecs::SceneModule::instance().clearSystems();
    ks::ecs::SceneModule::instance().attach(nullptr);
    ks::scripting::ScriptModule::instance().shutdown();
    ks::engine::devices::InputSystem::instance().shutdown();
    ks::engine::graphics::RenderSystem::instance().shutdown();
    Engine::instance().shutdown();
}

bool SimulationLoop::initialize() {
    auto& engine = Engine::instance();
    engine.initialize();
    engine.setFixedDt(1.0 / 120.0);
    engine.registerModule("ks.input", borrowModule(ks::engine::devices::InputSystem::instance()));
    engine.registerModule("ks.render", borrowModule(ks::engine::graphics::RenderSystem::instance()));
    auto& scene = ks::ecs::SceneModule::instance();
    scene.attach(&engine.registry());
    engine.registerModule("ks.scene", borrowModule(scene));
    ks::engine::devices::InputSystem::instance().initialize();
    ks::engine::graphics::RenderSystem::instance().initialize();
    auto& script = ks::scripting::ScriptModule::instance();
    engine.registerModule("ks.script", borrowModule(script));
    script.initialize();
    if (m_input) m_input->initialize();
    if (!m_setupGarage) m_setupGarage = std::make_unique<SetupGarage>();
    if (!m_ffb) {
        m_ffb = std::make_unique<FfbOutput>();
        m_ffb->initialize();
    }
    applyVehicleSetup();
    if (!m_multiCar) m_multiCar = std::make_unique<MultiCarManager>();
    if (m_shmEnabled) {
        m_shm = std::make_unique<ks::ac::AcSharedMemoryPublisher>();
        m_shm->open();
    }
    if (m_udpEnabled) {
        m_udp = std::make_unique<UdpTelemetryBridge>();
        m_udp->open(m_udpHost.c_str(), m_udpPort);
    }
    if (m_tcpEnabled) {
        m_tcp = std::make_unique<TcpTelemetryBridge>();
        m_tcp->listen("0.0.0.0", m_tcpPort);
    }

    startFeatureServices(false);
    setupDefaultGarageLayout(8);
    return true;
}

bool SimulationLoop::loadTrack(const std::string& kn5Path) {
    if (kn5Path.empty() || kn5Path.size() > 4096 || kn5Path.find("..") != std::string::npos)
        return false;
    m_trackData = TrackRuntimeData{};
    m_trackData.kn5Path = kn5Path;
    m_trackData.name = std::filesystem::path(kn5Path).stem().string();
    if (!std::filesystem::exists(kn5Path)) { m_trackLoaded = false; return false; }
    m_trackData.valid = true; m_trackLoaded = true;
    m_ui.menu().setTrackName(m_trackData.name);
    m_lapTimer.configure(3);
    return true;
}

bool SimulationLoop::loadTrackFolder(const std::string& trackDirectory) {
    namespace fs = std::filesystem;
    if (trackDirectory.empty() || trackDirectory.size() > 4096) return false;
    if (trackDirectory.find("..") != std::string::npos) return false;
    if (!fs::is_directory(trackDirectory)) return false;
    m_trackData.directory = trackDirectory;
    m_trackData.name = fs::path(trackDirectory).filename().string();
    loadGarageFromTrack(trackDirectory);
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
    return true;
}

bool SimulationLoop::loadCar(const std::string& carDir) {
    namespace fs = std::filesystem;
    if (carDir.empty() || carDir.size() > 4096 || carDir.find("..") != std::string::npos)
        return false;
    if (!fs::is_directory(carDir)) return false;
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
    applyVehicleSetup();
    return true;
}

bool SimulationLoop::loadCarAudio(const std::string&) { return true; }

void SimulationLoop::start() {
    if (m_running) return;
    m_running = true;
    Engine::instance().start();
#if HAS_VEHICLE_SIM
    if (m_vehicle) m_vehicle->startSimulation();
#endif
    m_lastTime = std::chrono::steady_clock::now();
    m_sessionType = SESSION_RACE; m_sessionPhase = PHASE_COUNTDOWN;
    m_timeRemaining = 5.0; m_currentLap = 0; m_totalLaps = 5;
    m_lapDistance = 0; m_normalizedSpline = 0; m_simTime = 0;
    m_lapTimer.reset(); m_lapTimer.start();
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
    if (onSimulationStopped) onSimulationStopped();
}

void SimulationLoop::reset() {
#if HAS_VEHICLE_SIM
    if (m_vehicle) m_vehicle->reset();
#endif
    if (m_input) m_input->reset();
    m_simAccumulator = 0; m_currentLap = 0;
    m_sessionPhase = PHASE_COUNTDOWN; m_timeRemaining = 5.0;
    m_lapDistance = 0; m_normalizedSpline = 0; m_simTime = 0;
    m_lapTimer.reset();
    if (m_running) m_lapTimer.start();
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
    if (!m_input) return;
    const bool blocked = m_ui.blocksDrivingInput() || m_snapHoldSec > 0.f;
    if (!m_ui.blocksDrivingInput()) m_input->update();
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    if (blocked) {
        m_vehicle->setThrottle(0);
        m_vehicle->setBrake(m_snapHoldSec > 0.f ? 1.0 : 0);
        m_vehicle->setSteering(0);
        return;
    }
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
        if (onSessionStateChanged)
            onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
        float sectors[3] = {0.f, 0.f, 0.f};
        m_features.onLapCompleted(m_trackData.name, m_carName, "Player",
                                  m_lapTimer.lastTimeMs() * 0.001f, sectors);
    }
#endif
}

void SimulationLoop::updateWeather() {
    auto& rs = ks::engine::graphics::RenderSystem::instance();
    const float phase = (m_timeOfDay - 12.0f) / 12.0f * 3.14159265f;
    const float c = std::cos(phase), s = std::sin(phase);
    const ks::engine::graphics::Vec3 sunDir{0.3f * c - 0.2f * s, -0.8f, 0.2f * c + 0.3f * s};
    rs.setSun(sunDir, {1.0f, 0.95f, 0.9f});
    rs.setFog(m_weather.cloudCover > 0.35f, {0.6f, 0.7f, 0.85f}, 0.0001f + m_weather.cloudCover * 0.002f);
    rs.setRain(m_weather.rainIntensity, m_weather.trackWetness);
    ks::physics::TrackSurface::instance().syncFromWeather(m_weather.trackWetness, m_weather.trackTemp);
}

void SimulationLoop::tick() {
    if (!m_running) {
        publishSharedMemory();
        render();
        return;
    }
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_lastTime).count();
    m_lastTime = now;
    if (elapsed > 0.05) elapsed = 0.05;
    m_simAccumulator += elapsed;
    while (m_simAccumulator >= m_physicsDt) {
        applyInput();
#if HAS_VEHICLE_SIM
        if (m_vehicle) m_vehicle->updatePhysics(m_physicsDt);
        if (m_multiCar) m_multiCar->update((float)m_physicsDt);
        updateForceFeedback();
#endif
        if (m_sessionPhase == PHASE_GREEN_FLAG)
            updateLapAndSurface(m_physicsDt);
        {
            vec3 pos{};
            bool onTrack = (m_sessionPhase == PHASE_GREEN_FLAG);
#if HAS_VEHICLE_SIM
            if (m_vehicle) {
                const auto st = m_vehicle->getState();
                pos = { (float)st.position.x, (float)st.position.y, (float)st.position.z };
            }
#endif
            m_features.tick((float)m_physicsDt, &m_raceSession, 0, pos, onTrack);
            updatePitLane((float)m_physicsDt);
            updateGarageExit((float)m_physicsDt);
            updatePitRepair((float)m_physicsDt);
            m_timeOfDay = m_features.weatherCtrl.time().hours;
        }
        m_simTime += m_physicsDt;
        m_simAccumulator -= m_physicsDt;
    }
    Engine::instance().tick(elapsed);

    if (m_sessionPhase == PHASE_COUNTDOWN) {
        m_timeRemaining -= elapsed;
        if (m_timeRemaining <= 0.0) {
            m_sessionPhase = PHASE_GREEN_FLAG;
            m_timeRemaining = 0.0;
            m_lapTimer.start();
        }
        if (onSessionStateChanged)
            onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
    } else if (m_sessionPhase == PHASE_GREEN_FLAG) {
        if (onSessionStateChanged)
            onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
    }
    if (m_sessionPhase == PHASE_GREEN_FLAG && m_currentLap >= m_totalLaps && m_totalLaps > 0) {
        m_sessionPhase = PHASE_CHECKERED_FLAG;
        if (onSessionStateChanged)
            onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
    }

    updateWeather();
    publishSharedMemory();
    publishUdpTelemetry();
    publishTcpTelemetry();
    render();
}

void SimulationLoop::render() {
#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        const auto st = m_vehicle->getState();
        const auto& dmg = m_vehicle->damage();
        ui::RaceHudSample s;
        s.speedMs = static_cast<float>(st.speed);
        s.rpm = static_cast<float>(m_vehicle->rpm());
        s.maxRpm = 8500.f;
        s.gear = m_vehicle->currentGear();
        s.throttle = static_cast<float>(st.throttle);
        s.brake = static_cast<float>(st.brake);
        s.steer = static_cast<float>(st.steering);
        s.fuelL = static_cast<float>(st.fuel);
        s.lap = m_currentLap + 1;
        s.totalLaps = m_totalLaps;
        s.currentTimeMs = m_lapTimer.currentTimeMs();
        s.lastTimeMs = m_lapTimer.lastTimeMs();
        s.bestTimeMs = m_lapTimer.bestTimeMs();
        s.damageOverall = dmg.overallDamage();
        s.engineHealth = dmg.powerMultiplier();
        s.powerMult = dmg.powerMultiplier();
        s.damageWarning = (s.damageOverall > 0.6f) ? 2 : (s.damageOverall > 0.25f ? 1 : 0);
        for (int i = 0; i < 4; ++i) {
            s.tyreTemp[i] = static_cast<float>(st.tyreTemp[i]);
            s.tyreWear[i] = static_cast<float>(st.tyreWear[i]);
        }
        m_ui.pushRaceSample(s);
    }
#endif
    m_ui.update(0.016f);
}

void SimulationLoop::updateCamera(float) {}

ks::ecs::Registry& SimulationLoop::scene() {
    return Engine::instance().registry();
}

int SimulationLoop::loadBakedScene(const std::string&) { return 0; }

void SimulationLoop::beginRaceSession() {
    beginSession(GameSessionMode::Race);
}

} // namespace ks::sim
