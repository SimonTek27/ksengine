#pragma once

/**
 * SimulationLoop — fixed-timestep sim + NativeUiHub + GPU UI pass.
 * LapSectorTimer + shared-memory + UDP/TCP telemetry + TrackSurface.
 */

#include "engine/physics/PhysicsCoreTypes.h"
#include "engine/physics/TrackSurface.h"
#include "engine/physics/LapSectorTimer.h"
#include "engine/physics/PhysicsGolden.h"
#include "engine/scene/Registry.h"
#include "ui/NativeUiHub.h"
#include "CameraController.h"
#include "NativeRenderer.h"
#include "engine/Graphics/ParticleSystem.h"
#include "RaceSessionManager.h"

#include <memory>
#include <chrono>
#include <cstdint>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <array>
#include <cmath>

namespace ks::physics {
class VehicleSimulator;
}

namespace ks::device {
class FFBBase;
}

namespace ks::ac {
class AcSharedMemoryPublisher;
}

namespace ks::sim {

class InputManager;
class CameraController;
class SimulatorAudio;
class SetupGarage;
class NativeRenderer;
class MultiCarManager;
class NetworkManager;
class UdpTelemetryBridge;
class TcpTelemetryBridge;

namespace ui {
class UiGpuPass;
}

namespace net {
struct InputData;
struct CarStateData;
}

struct Vec3f {
    float x = 0, y = 0, z = 0;
    Vec3f() = default;
    Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3f operator+(Vec3f o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
};

struct Mat4f {
    std::array<float, 16> m{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    static Mat4f identity() { return Mat4f{}; }
    static Mat4f translation(float tx, float ty, float tz) {
        Mat4f r; r.m[12] = tx; r.m[13] = ty; r.m[14] = tz; return r;
    }
    static Mat4f translation(Vec3f t) { return translation(t.x, t.y, t.z); }
    Mat4f operator*(const Mat4f& o) const {
        Mat4f r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row)
                r.m[c * 4 + row] =
                    m[0 * 4 + row] * o.m[c * 4 + 0] +
                    m[1 * 4 + row] * o.m[c * 4 + 1] +
                    m[2 * 4 + row] * o.m[c * 4 + 2] +
                    m[3 * 4 + row] * o.m[c * 4 + 3];
        return r;
    }
    void translate(float tx, float ty, float tz) { *this = *this * translation(tx, ty, tz); }
};

struct SimTrackData {
    std::string name;
    std::string kn5Path;
    std::string directory;
    bool valid = false;
    float splineLength = 5000.f;
};

class SimulationLoop {
public:
    SimulationLoop();
    ~SimulationLoop();

    bool initialize();
    bool loadTrack(const std::string& kn5Path);
    bool loadTrackFolder(const std::string& trackDirectory);
    bool loadCar(const std::string& carDir);
    bool loadCarAudio(const std::string& carDirectory);
    int loadBakedScene(const std::string& manifestDir);
    ks::ecs::Registry& scene();

    void start();
    void stop();
    void reset();
    bool isRunning() const { return m_running; }
    void tick();

    bool handleUiKey(int virtualKey);
    // WM_CHAR / mouse routing into the native UI (roadmap 3.1): text input
    // needs the translated character, not the virtual key, and the menus/
    // server browser are click-driven.
    bool handleUiChar(int character);
    bool handleUiMouseMove(float x, float y);
    bool handleUiMouseButton(ui::MouseButton button, bool down, float x, float y);
    bool handleUiMouseWheel(float delta, float x, float y);

    InputManager* inputManager() { return m_input.get(); }
    CameraController* camera() { return m_camera.get(); }
    SimulatorAudio* audio() { return m_audio.get(); }
    SetupGarage* setupGarage() { return m_setupGarage.get(); }
    ui::NativeUiHub& ui() { return m_ui; }
    ui::UiGpuPass* uiGpu() { return m_uiGpu.get(); }
    const SimTrackData& trackData() const { return m_trackData; }
    ks::physics::VehicleSimulator* vehicle() { return m_vehicle.get(); }
    MultiCarManager* multiCarManager() { return m_multiCar.get(); }
    NetworkManager* networkManager() { return m_network.get(); }
    ks::physics::LapSectorTimer& lapTimer() { return m_lapTimer; }
    UdpTelemetryBridge* udpBridge() { return m_udp.get(); }
    TcpTelemetryBridge* tcpBridge() { return m_tcp.get(); }
    DashboardOverlay* dashboard() { return &m_ui.dashboard(); }
    TelemetryOverlay* telemetry() { return &m_ui.telemetry(); }
    void setCameraMode(CameraController::Mode m) {
        if (m_camera) m_camera->setMode(m);
    }

    bool isVulkanMode() const { return m_vulkanMode; }
    void setVulkanRenderer(NativeRenderer* r) {
        m_vulkanRenderer = r;
        if (r && m_uiGpu) r->setUiGpuPass(m_uiGpu);
    }
    void setNativeRenderer(NativeRenderer* r) { setVulkanRenderer(r); }
    void setRenderer(NativeRenderer* r) { setVulkanRenderer(r); }
    NativeRenderer* vulkanRenderer() { return m_vulkanRenderer; }
    NativeRenderer* renderer() { return m_vulkanRenderer; }

    void setViewportSize(int w, int h) {
        m_viewW = w > 0 ? w : m_viewW;
        m_viewH = h > 0 ? h : m_viewH;
        m_ui.resize(m_viewW, m_viewH);
        if (m_vulkanRenderer) m_vulkanRenderer->resize(m_viewW, m_viewH);
    }

    void setTimeOfDay(float hours) { m_timeOfDay = hours; }
    float timeOfDay() const { return m_timeOfDay; }
    void setWeatherPreset(const ks::physics::WeatherState& state) { m_weather = state; }
    const ks::physics::WeatherState& weatherState() const { return m_weather; }

    void setFfbEnabled(bool e) { m_ffbEnabled = e; }
    bool ffbEnabled() const { return m_ffbEnabled; }

    void setSharedMemoryEnabled(bool e) { m_shmEnabled = e; }

    /** Race flag (yellow/SC): drives the AC SHM `flag` channel and the AI
     *  speed limiter. GREEN/checkered are set by the session manager. */
    void setRaceFlag(RaceFlag f) { m_raceSession.setFlag(f); }
    RaceFlag raceFlag() const { return m_raceSession.flag(); }
    bool sharedMemoryEnabled() const { return m_shmEnabled; }

    /** AI grid size for the next race session (roadmap 3.5). N AI cars
     *  spawn on the track's ai/fast_lane.ai line; 0 = player only. */
    void setAiCarCount(int n) { m_aiCarCount = n < 0 ? 0 : n; }
    int aiCarCount() const { return m_aiCarCount; }

    void setUdpTelemetryEnabled(bool e) { m_udpEnabled = e; }
    bool udpTelemetryEnabled() const { return m_udpEnabled; }
    void setUdpTelemetryEndpoint(const std::string& host, uint16_t port) {
        m_udpHost = host;
        m_udpPort = port;
    }
    bool openUdp();

    void setTcpTelemetryEnabled(bool e) { m_tcpEnabled = e; }
    bool tcpTelemetryEnabled() const { return m_tcpEnabled; }
    void setTcpTelemetryPort(uint16_t port) { m_tcpPort = port; }
    bool openTcp();

    void applyRemoteInput(int clientIndex, const net::InputData& input);

    std::function<void()> onSimulationStarted;
    std::function<void()> onSimulationStopped;
    std::function<void(uint8_t, uint8_t, int, int, double)> onSessionStateChanged;

private:
    void applyInput();
    void render(float dt = 1.0f / 60.0f);
    void updateCamera(float dt);
    // Roadmap P1 (KS_PARTICLES=1): steps the CPU particle simulation and
    // uploads this frame's billboard quads to the renderer. No-op otherwise.
    void updateAndDrawParticles(float dt);
    // Roadmap P2 (KS_TERRAIN=1): builds the trackside ground once from a
    // generated heightmap and registers it as scene geometry. No-op otherwise.
    void initTracksideTerrain();
    void updateWeather();
    void syncCarTransforms();
    void broadcastLocalCarState();
    void handleRemoteCarState(uint32_t carId, const net::CarStateData& state);
    void ensureScenePipeline();
    void syncUiFromVehicle();
    void publishSharedMemory();
    void publishUdpTelemetry();
    void publishTcpTelemetry();
    void updateLapAndSurface(double dt);
    void beginRaceSession();
    void finishGoldenExport();
    static std::string readFileText(const std::string& path);

    bool m_vulkanMode = true;
    NativeRenderer* m_vulkanRenderer = nullptr;
    std::unique_ptr<InputManager> m_input;
    std::unique_ptr<CameraController> m_camera;
    std::unique_ptr<ks::physics::VehicleSimulator> m_vehicle;
    std::unique_ptr<MultiCarManager> m_multiCar;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<SimulatorAudio> m_audio;
    std::unique_ptr<SetupGarage> m_setupGarage;
    std::unique_ptr<ks::ac::AcSharedMemoryPublisher> m_shm;
    std::unique_ptr<UdpTelemetryBridge> m_udp;
    std::unique_ptr<TcpTelemetryBridge> m_tcp;

    ui::NativeUiHub m_ui;
    std::shared_ptr<ui::UiGpuPass> m_uiGpu;
    int m_viewW = 1280;
    int m_viewH = 720;

    // Roadmap P1 particles: off unless KS_PARTICLES=1 (the default image
    // must stay byte-identical), in which case a single dust emitter rides
    // the car and its quads are handed to NativeRenderer every frame.
    bool m_particlesWanted = false;
    bool m_particlesInit = false;
    ks::engine::graphics::ParticleSystem m_particles;
    std::vector<float> m_particleQuads;

    // Roadmap P2 terrain: off unless KS_TERRAIN=1 (default image unchanged).
    bool m_terrainWanted = false;
    bool m_terrainInit = false;

    SimTrackData m_trackData;
    ks::physics::LapSectorTimer m_lapTimer;
    RaceSessionManager m_raceSession;
    float m_lapDistance = 0.f;
    float m_normalizedSpline = 0.f;
    bool m_shmEnabled = true;
    bool m_udpEnabled = true;
    std::string m_udpHost = "127.0.0.1";
    uint16_t m_udpPort = 20777;
    bool m_tcpEnabled = true;
    uint16_t m_tcpPort = 20778;
    double m_simTime = 0.0;

    std::chrono::steady_clock::time_point m_lastTime{};
    double m_simAccumulator = 0;
    static constexpr double m_physicsDt = 0.001;

    std::unique_ptr<ks::device::FFBBase> m_ffb;
    bool m_ffbEnabled = true;
    bool m_running = false;
    bool m_trackLoaded = false;
    bool m_carLoaded = false;

    float m_timeOfDay = 12.0f;
    ks::physics::WeatherState m_weather{};

    uint8_t m_sessionType = 0;
    uint8_t m_sessionPhase = 0;
    int m_currentLap = 0;
    int m_totalLaps = 0;
    double m_timeRemaining = 0.0;

    // Opt-in telemetry export: set KS_GOLDEN_CSV=<path> to record the race
    // session (sampled at 50 Hz while green) and write a golden CSV on end.
    ks::physics::PhysicsGolden m_goldenRecorder;
    bool m_goldenExportActive = false;
    double m_goldenSampleAccum = 0.0;
    double m_goldenTime = 0.0;
    std::string m_goldenExportPath;

    std::string m_spawnedSceneDir;
    bool m_pipelineInitialized = false;
    uint32_t m_streamlineFrameIndex = 0;
    std::string m_carName;

    // AI racing field (roadmap 3.5): requested size, spawned car ids for
    // the current session, and the last lap count seen per car (lap events).
    int m_aiCarCount = 0;
    std::vector<int> m_aiCarIds;
    std::vector<int> m_aiLastLaps;

    // Multiplayer (roadmap 3.1): the host latches each server slot's latest
    // controls and pushes them to that slot's car once per physics step,
    // while a client maps the car ids the host relays onto the entries it
    // spawned locally. kNoNetCarId means "our own car not announced yet".
    static constexpr uint32_t kNoNetCarId = 0xFFFFFFFFu;
    struct RemoteInput {
        float throttle = 0.0f;
        float brake = 0.0f;
        float steering = 0.0f;
    };
    std::unordered_map<int, RemoteInput> m_remoteInputs;
    std::unordered_map<uint32_t, int> m_remoteCarIds;
    uint32_t m_ownNetCarId = kNoNetCarId;

    // F2 server-browser row for our own hosted session (-1 = not hosting,
    // so the static localhost row from SimulatorApp is left alone).
    int m_mpServerClients = -1;
};

} // namespace ks::sim
