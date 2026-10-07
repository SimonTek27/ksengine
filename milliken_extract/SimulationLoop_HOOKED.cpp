/**
 * SimulationLoop.cpp - std-only / Qt-free
 * 1 kHz physics + LapSectorTimer + SM + UDP + TCP telemetry
 */

#include "SimulationLoop.h"
#include "InputManager.h"
#include "MultiCarManager.h"
#include "NetworkManager.h"
#include "SetupGarage.h"
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
    m_lapTimer.configure(3);
    m_ui.resize(m_viewW, m_viewH);
    m_ui.dashboard().setVisible(true);
    m_ui.menu().setVisible(false);
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
    startFeatureServices(false);
    // Default pit axis until track provides real boxes/pit spline
    configurePitAxis(0.f, 0.f, 0.f, 120.f);
    return true;
}

bool SimulationLoop::loadTrack(const std::string& kn5Path) {
    m_trackData = SimTrackData{};
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
    if (!fs::is_directory(trackDirectory)) return false;
    m_trackData.directory = trackDirectory;
    m_trackData.name = fs::path(trackDirectory).filename().string();
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
    if (m_shm) m_shm->invalidateStatic();
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

void SimulationLoop::applyInput() {
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
        if (onSessionStateChanged)
            onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
        {
            float sectors[3] = {0.f, 0.f, 0.f};
            const float lapSec = m_lapTimer.lastTimeMs() * 0.001f;
            m_features.onLapCompleted(m_trackData.name, m_carName, "Player", lapSec, sectors);
        }
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
    m_shm->publish(live);
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
    if (m_vulkanRenderer) {
        DirectionalLight light; light.direction = {sunDir.x, sunDir.y, sunDir.z};
        m_vulkanRenderer->setSun(light);
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
    m_ui.pushRaceSample(s);
#endif
}

void SimulationLoop::render() {
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
        m_vulkanRenderer->beginFrame();
        scene().each<ks::ecs::Transform, ks::ecs::MeshInstance>([this](ks::ecs::Entity, ks::ecs::Transform& t, ks::ecs::MeshInstance& mesh) {
            m_vulkanRenderer->drawMesh(mesh.meshName, toNativeMat4(ks::ecs::worldMatrix(t)));
        });
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
    std::ifstream manifest(manifestDir + "/manifest.txt");
    if (!manifest.is_open()) return 0;
    std::string name;
    while (std::getline(manifest, name)) {
        if (name.empty()) continue;
        const ks::ecs::Entity e = scene().create();
        if (e == ks::ecs::kNullEntity) break;
        scene().emplace<ks::ecs::Name>(e, ks::ecs::Name{name});
        scene().emplace<ks::ecs::Transform>(e);
        scene().emplace<ks::ecs::MeshInstance>(e, ks::ecs::MeshInstance{name});
    }
    m_spawnedSceneDir = manifestDir;
    return static_cast<int>(scene().alive());
}

void SimulationLoop::broadcastLocalCarState() {}
void SimulationLoop::handleRemoteCarState(uint32_t, const net::CarStateData&) {}
void SimulationLoop::applyRemoteInput(int, const net::InputData&) {}

void SimulationLoop::tick() {
    if (!m_running) {
        publishSharedMemory();
        publishUdpTelemetry();
        publishTcpTelemetry();
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
#endif
        if (m_sessionPhase == PHASE_GREEN_FLAG) updateLapAndSurface(m_physicsDt);
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
            m_timeOfDay = m_features.weatherCtrl.time().hours;
        }
        m_simTime += m_physicsDt;
        m_simAccumulator -= m_physicsDt;
    }
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
    updateWeather();
    publishSharedMemory();
    publishUdpTelemetry();
    publishTcpTelemetry();
    if (m_sessionPhase == PHASE_COUNTDOWN) {
        m_timeRemaining -= elapsed;
        if (m_timeRemaining <= 0.0) { m_sessionPhase = PHASE_GREEN_FLAG; m_timeRemaining = 0.0; m_lapTimer.start(); }
        if (onSessionStateChanged) onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
    } else if (m_sessionPhase == PHASE_GREEN_FLAG) {
        if (onSessionStateChanged) onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
    }
    if (m_sessionPhase == PHASE_GREEN_FLAG && m_currentLap >= m_totalLaps && m_totalLaps > 0) {
        m_sessionPhase = PHASE_CHECKERED_FLAG;
        if (onSessionStateChanged) onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
    }
    broadcastLocalCarState();
    render();
}

} // namespace ks::sim
