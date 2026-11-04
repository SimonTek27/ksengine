#pragma once

#include "ShowroomScene.h"

/**
 * SimulationLoop — fixed-timestep sim + NativeUiHub + GPU UI pass.
 * LapSectorTimer + shared-memory + UDP/TCP telemetry + TrackSurface.
 * FeatureHub + CarStateSync parity hooks (restored).
 */

#include "engine/physics/PhysicsCoreTypes.h"
#include "engine/physics/TrackSurface.h"
#include "engine/physics/LapSectorTimer.h"
#include "engine/physics/PhysicsGolden.h"
#include "engine/physics/WeatherPhysics.h"
#include "WeatherControl.h" // roadmap 2.11: KS_WEATHER preset -> WeatherState
#include "engine/scene/Registry.h"
#include "ui/NativeUiHub.h"
#include "CameraController.h"
#include "NativeRenderer.h"
#include "engine/Graphics/ParticleSystem.h"
#include "RaceSessionManager.h"
#include "FeatureHub.h"
#include "SessionController.h" // GameSessionMode + SessionStartParams
#include "CarStateSync.h"
#include "CarStateSyncBridge.h"
#include "TeamSessionBridge.h" // roadmap 2.8: team.ini roster -> session field
#include "PitStrategyBridge.h"  // roadmap 2.7: menu pit plan -> PitRepairInput
                             // (pulls in PitLaneRepair.h -> GarageExit.h /
                             // GarageSpawn.h, all self-contained)
#include "PitLaneQueue.h"    // roadmap 2.7: multi-car pit-lane order
#include "TrackLayout.h"      // phase 3.3: multi-layout catalog
#include "TracksidePosts.h"   // safety-car bay + four marshal posts per corner
#include "PitLaneCollision.h" // pit-lane body collision
#include "GarageSpawn.h"      // GarageLayout / GarageSpawnPolicy

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
class AcRemoteTelemetryServer;
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
struct UpgradeRuntime; // upgrade/appearance state for the loaded car (roadmap 2.9, .cpp)
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
    std::string layoutId;        // phase 3.3 active layout id
    std::string layoutFolder;    // resolved layout folder (may equal directory)
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
    bool openShowroom(const std::string& directory, std::string* error = nullptr);
    void loadShowroomHall();
    void loadShowroomAudio();
    bool loadShowroomHallFromKn5(const ShowroomConfig& cfg);
    bool loadKn5Meshes(const std::string& modelPath, const std::string& prefix,
                       bool faceInside, std::vector<ks::ecs::Entity>* entities, float outMin[3],
                       float outMax[3], int* outMeshes);
    bool loadCarMeshesFromKn5(const std::string& carDirectory);
    void loadShowroomCar();
    void applyShowroomLight();
    void buildShowroomVenue();
    void closeShowroom();
    bool showroomActive() const { return m_showroom.active(); }
    const ShowroomScene& showroom() const { return m_showroom; }
    bool loadTeam(const std::string& teamPath);
    const TeamInfo& team() const { return m_team; }
    std::vector<std::string> upgradeRowLabels() const;
    void cycleUpgradeRow(int row);
    int loadBakedScene(const std::string& manifestDir);
    void applyTrackVisuals(const std::string& trackDir);
    void ensureCarVisual(const std::string& carDir);
    ks::ecs::Registry& scene();

    void start();
    void stop();
    void reset();
    void teleportToPit(int carIndex);
    // Fase 1 exit: start a session of the mode chosen in the menu.
    void startSession(GameSessionMode mode);
    void startSession(GameSessionMode mode, const SessionStartParams& params);
    bool isRunning() const { return m_running; }
    void tick();

    bool handleUiKey(int virtualKey);
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

    FeatureHub& features() { return m_features; }
    const FeatureHub& features() const { return m_features; }
    void startFeatureServices(bool hostAnnounce = false);

    void requestPitService(bool on = true) { m_requestPitService = on; }
    PitStrategyState& pitStrategy() { return m_pitStrategy; }
    void updatePitRepair(float dt);
    const char* pitServiceStatus() const { return m_pitStatus.c_str(); }
    bool pitServiceBusy() const { return m_pitRepair.isBusy(); }
    void bindPlayerBox(const WorldPose& box) { m_playerBox = box; m_hasPlayerBox = true; }

    void requestLeaveGarage(bool on = true) { m_requestLeaveGarage = on; }
    void requestReturnToBox(bool on = true) { m_requestReturnBox = on; }
    void updateGarageExit(float dt);
    GarageExitPhase garagePhase() const { return m_garageExit.phase(); }
    bool isHeldInGarage() const;
    const char* garageStatus() const { return m_garageStatus.c_str(); }
    void pumpFeatureHub(float dt, bool withSession);
    bool startCarStateHost(uint16_t port = netsync::kDefaultSyncPort);
    bool startCarStateClient(const std::string& host, uint16_t port = netsync::kDefaultSyncPort);
    void stopCarStateSync();
    void updateNetworkSync(float dt);
    netsync::CarStateSync& carStateSync() { return m_carSync; }
    const netsync::CarStateSync& carStateSync() const { return m_carSync; }

    ks::physics::LapSectorTimer& lapTimer() { return m_lapTimer; }
    UdpTelemetryBridge* udpBridge() { return m_udp.get(); }
    TcpTelemetryBridge* tcpBridge() { return m_tcp.get(); }
    DashboardOverlay* dashboard() { return &m_ui.dashboard(); }
    TelemetryOverlay* telemetry() { return &m_ui.telemetry(); }
    void setParticlesEnabled(bool on) { m_particlesConfig = on; }
    bool particlesEnabled() const { return m_particlesConfig; }

    void setFrameLimit(int fps) { m_frameLimit = fps > 0 ? fps : 0; }
    int frameLimit() const { return m_frameLimit; }

    void setCameraMode(CameraController::Mode m) {
        if (m_camera) m_camera->setMode(m);
    }

    bool isVulkanMode() const { return m_vulkanMode; }
    void setVulkanRenderer(NativeRenderer* r);
    void setNativeRenderer(NativeRenderer* r) { setVulkanRenderer(r); }
    void setRenderer(NativeRenderer* r) { setVulkanRenderer(r); }
    NativeRenderer* vulkanRenderer() { return m_vulkanRenderer; }
    NativeRenderer* renderer() { return m_vulkanRenderer; }

    void setViewportSize(int w, int h);

    void setTimeOfDay(float hours) { m_timeOfDay = hours; }
    float timeOfDay() const { return m_timeOfDay; }
    void setWeatherPreset(const ks::physics::WeatherState& state);
    void setWeatherPresetByName(const std::string& name);
    const ks::physics::WeatherState& weatherState() const { return m_weather; }

    void setFfbEnabled(bool e) { m_ffbEnabled = e; }
    bool ffbEnabled() const { return m_ffbEnabled; }
    void setSharedMemoryEnabled(bool e) { m_shmEnabled = e; }
    void setRaceFlag(RaceFlag f) { m_raceSession.setFlag(f); }
    RaceFlag raceFlag() const { return m_raceSession.flag(); }
    RaceSessionManager& raceSession() { return m_raceSession; }
    bool sharedMemoryEnabled() const { return m_shmEnabled; }

    void setAiCarCount(int n) { m_aiCarCount = n < 0 ? 0 : n; }
    int aiCarCount() const { return m_aiCarCount; }

    void setUdpTelemetryEnabled(bool e) { m_udpEnabled = e; }
    bool udpTelemetryEnabled() const { return m_udpEnabled; }
    void setUdpTelemetryEndpoint(const std::string& host, uint16_t port);
    bool openUdp();
    void setTcpTelemetryEnabled(bool e) { m_tcpEnabled = e; }
    bool tcpTelemetryEnabled() const { return m_tcpEnabled; }
    void setTcpTelemetryPort(uint16_t port) { m_tcpPort = port; }
    bool openTcp();

    void applyRemoteInput(int clientIndex, const net::InputData& input);

    std::function<void()> onSimulationStarted;
    std::function<void()> onSimulationStopped;
    std::function<void(uint8_t, uint8_t, int, int, double)> onSessionStateChanged;

    bool switchLayout(const std::string& layoutId);
    const TrackLayoutCatalog& layoutCatalog() const { return m_layouts; }

    const TracksidePosts& tracksidePosts() const { return m_tracksidePosts; }
    RaceFlag localYellowFlag() const { return m_tracksidePosts.trackFlag(); }
    bool loadTracksidePostsFromTrack(const std::string& trackDir);
    void updateTracksidePosts(float dt);
    bool loadMarshalAsset(const std::string& trackDir);
    void spawnMarshalPosts();
    void despawnMarshalPosts();

    void applyDamageEffects();
    void beginSession(GameSessionMode mode);
    void configurePitAxis(float originX, float originZ, float headingRad, float lengthM);
    bool loadGarageFromTrack(const std::string& trackDir);
    bool loadReplayFile(const std::string& path);
    void setupDefaultGarageLayout(int boxCount);
    void snapVehicleToPose(ks::physics::VehicleSimulator* veh, const WorldPose& pose);
    void spawnAiGrid(int count);
    void updatePitLane(float dt);

private:
    void applyInput();
    void render(float dt = 1.0f / 60.0f);
    void updateCamera(float dt);
    void updateAndDrawParticles(float dt);
    void initTracksideTerrain();
    void updateWeather(float dt);
    void syncCarTransforms();
    int spawnSceneEntities(const std::string& manifestDir);
    void despawnSceneEntities();
    void despawnCarVisuals();
    void placePlayerForSession();
    void spawnSessionAiVisuals();
    void despawnAiVisuals();
    void loadUpgradesForCar(const std::string& carDir);
    void refreshUpgradeApplication();
    void applyUpgradePhysics();
    void applyUpgradeAudio();
    void applyUpgradeSelectionString(const char* spec);
    std::vector<std::string> loadCarMeshSet(const std::string& baked,
                                            const std::string& prefix,
                                            bool faceInside);

    ShowroomScene m_showroom;
    TrackLayoutCatalog m_layouts;
    TracksidePosts m_tracksidePosts;
    FeatureHub m_features;
    RaceSessionManager m_raceSession;
    netsync::CarStateSync m_carSync;
    PitStrategyState m_pitStrategy;
    bool m_requestPitService = false;
    bool m_requestLeaveGarage = false;
    bool m_requestReturnBox = false;
    bool m_garageExitEnabled = true;
    bool m_hasPlayerBox = false;
    bool m_running = false;
    bool m_vulkanMode = false;
    bool m_particlesConfig = false;
    bool m_ffbEnabled = false;
    bool m_shmEnabled = false;
    bool m_udpEnabled = false;
    bool m_tcpEnabled = false;
    int m_frameLimit = 0;
    int m_aiCarCount = 0;
    int m_viewW = 1280;
    int m_viewH = 720;
    float m_timeOfDay = 12.f;
    uint16_t m_udpPort = 9996;
    uint16_t m_tcpPort = 9997;
    std::string m_udpHost = "127.0.0.1";
    std::string m_pitStatus;
    std::string m_garageStatus;
    ks::physics::WeatherState m_weather;
    ks::physics::WeatherSimulator m_weatherSim;
    ks::physics::LapSectorTimer m_lapTimer;
    SimTrackData m_trackData;
    TeamInfo m_team;
    WorldPose m_playerBox{};
    GarageExit m_garageExit;
    PitLaneRepair m_pitRepair;
    ui::NativeUiHub m_ui;
    NativeRenderer* m_vulkanRenderer = nullptr;
    std::unique_ptr<InputManager> m_input;
    std::unique_ptr<CameraController> m_camera;
    std::unique_ptr<SimulatorAudio> m_audio;
    std::unique_ptr<SetupGarage> m_setupGarage;
    std::unique_ptr<ks::physics::VehicleSimulator> m_vehicle;
    std::unique_ptr<MultiCarManager> m_multiCar;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<UdpTelemetryBridge> m_udp;
    std::unique_ptr<TcpTelemetryBridge> m_tcp;
    std::unique_ptr<ui::UiGpuPass> m_uiGpu;

    static constexpr float kSnapHoldDuration = 0.35f;
};

} // namespace ks::sim
