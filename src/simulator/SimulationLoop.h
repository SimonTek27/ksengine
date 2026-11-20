#pragma once
#include "SessionController.h" // GameSessionMode + SessionStartParams

#include "ShowroomScene.h"
#include "LocalLights.h"      // the room's own lamps, kept for a session inside the hall

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
#include "engine/physics/DrivingWarnings.h" // one answer for every warning light
#include "WeatherControl.h" // roadmap 2.11: KS_WEATHER preset -> WeatherState
#include "GrassFx.h"       // grass FX di data/data.json (P1 CSP)
#include "LightSchedule.h"  // schedulazione luci per ora del giorno (P1 CSP)
#include "SeasonControl.h" // fondazione seasons (P1 CSP)
#include "engine/scene/Registry.h"
#include "ui/NativeUiHub.h"
#include "CameraController.h"
#include "NativeRenderer.h"
#include "engine/Graphics/BrakeDiscFx.h" // content's [BRAKEDISC_FX] -> disc glow
#include "engine/Graphics/ParticleSystem.h"
#include "RaceSessionManager.h"
#include "FeatureHub.h"
#include "TyreVisuals.h" // four wheels' bulge/flat-spot state, published to the HUD
#include "WheelShadow.h" // the blob under each tyre
#include "VrHeadPose.h"      // the tracked head, in the cockpit camera's frame
#include "VrHeadRecentre.h" // where the head is relative to where it recentred
#include "Spotter.h"            // text race-engineer calls (SETTINGS -> GAMEPLAY)
#include "SurfaceGrip.h"    // what the ground under each tyre does to its grip
#include "VrInputAdapter.h" // the controllers, as InputManager understands them
#include "NetworkConfig.h" // net::CarDamageMessage handled by applyRemoteCarDamage
#include "CarStateSync.h"
#include "CarStateSyncBridge.h"
#include "TeamSessionBridge.h" // roadmap 2.8: team.ini roster -> session field
#include "PitStrategyBridge.h"  // roadmap 2.7: menu pit plan -> PitRepairInput
                             // (pulls in PitLaneRepair.h -> GarageExit.h /
                             // GarageSpawn.h, all self-contained)
#include "PitLaneQueue.h"    // roadmap 2.7: multi-car pit-lane order
#include "TrackLayout.h"      // phase 3.3: multi-layout catalog
#include "TracksidePosts.h"   // safety-car bay + four marshal posts per corner
#include "TrackRecovery.h"     // stranded car -> marshals tow it to its box
#include "PitLaneCollision.h" // pit-lane body collision
#include "GarageSpawn.h"      // GarageLayout / GarageSpawnPolicy
#include "DevScripting.h"      // devMode.scripting: console LUA <file> -> Lua
#include "SessionTelemetryCsv.h" // session telemetry: one CSV per session

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

struct SetupData; // garage rows (SetupGarage.h) -> vehicle, see applySetupToVehicle

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
    // [DYNAMIC_LIGHTS]: legge data/lighting.ini (layout, poi root pista) e
    // spawna le [LIGHT_N] nel renderer; nessuna sezione = lista vuota.
    void spawnTrackDynamicLights(const std::string& layoutFolder,
                                 const std::string& trackDirectory);
    bool loadCar(const std::string& carDir);
    bool loadCarAudio(const std::string& carDirectory);
    // --- showroom (content/showrooms/<id>/showroom.json) -------------------
    // The game opens in a showroom: while one is open the camera sits on its
    // turntable instead of following a car nobody is driving, and its two
    // optional tracks play. Starting a session closes it.
    bool openShowroom(const std::string& directory, std::string* error = nullptr);
    // Loads the hall meshes and the two tracks. Called from openShowroom, so
    // the showroom is complete on the first frame instead of waiting for the
    // audio object a car would otherwise create.
    void loadShowroomHall();
    void loadShowroomAudio();
    /** Hands the menu's clicks to the audio object: the theme's three sounds are
     *  loaded the first time audio exists, and every frame drains what the hub
     *  recorded. Without a device the clicks are dropped, not queued up to burst
     *  later - a menu with no sound is honest, a menu that fires eight clicks at
     *  once when the device appears is not. */
    void pumpThemeSounds();
    // A room built in code for a showroom with no baked hall (see SceneAssets.h):
    // an unbaked or empty folder must not open on an empty frame.
    // Reads the showroom's .kn5 straight into meshes in memory: no bake, no
    // temporary files, only the content the selected folder already holds.
    bool loadShowroomHallFromKn5(const ShowroomConfig& cfg);
    // Shared .kn5 reader: mesh nodes of any file (venue or car) become NativeMeshes
    // with their own textures decoded in memory. Nothing is written to disk.
    bool loadKn5Meshes(const std::string& modelPath, const std::string& prefix,
                       bool faceInside, std::vector<ks::ecs::Entity>* entities, float outMin[3],
                       float outMax[3], int* outMeshes, int* outTextured = nullptr,
                       std::vector<WorldPose>* outPitMarkers = nullptr);
    // A track with no bake: read its own .kn5 into the scene in memory, exactly
    // like the showroom hall does, and anchor the player on the AI line (a
    // .kn5 carries no AC spawn marker).
    bool loadTrackKn5Visuals(const std::string& kn5Path);
    // Meshes the last in-memory .kn5 track uploaded ("track_<i>"): the next
    // load destroys exactly those before uploading a new layout.
    int m_trackKn5Meshes = 0;
    // The car's own model, when it has one and no bake was made for it.
    bool loadCarMeshesFromKn5(const std::string& carDirectory);
    // A showroom without a car is an empty room: load one onto the turntable.
    void loadShowroomCar();
    // The room's own sun, fog and exposure, applied when it opens.
    void applyShowroomLight();
    /** The room's [AUTO_EXPOSURE] loop: it keeps one readback in flight, so a
     *  measurement costs a handful of frames when the room opens. Returns the
     *  exposure to draw with (the adapted one once the loop has started). */
    float updateShowroomAutoExposure();
    void buildShowroomVenue();
    void closeShowroom();
    /** The scene is no longer the hall: the sun it baked and the lamps it lit
     *  stop answering for it (a track's own light takes over). */
    void releaseShowroomScene();
    // Auto exposure state of the open room (see updateShowroomAutoExposure).
    std::string m_roomMeterRoom;      // the room this state belongs to
    float m_roomExposure = 0.f;       // adapted exposure, 0 = not started
    bool m_roomMeterWaiting = false;  // a readback was asked for and not read
    bool m_roomMeterDone = false;     // the loop settled (or gave up)
    int m_roomMeterSteps = 0;         // readbacks used
    int m_roomMeterWaited = 0;        // frames spent waiting for the readback
    int m_roomMeterAttempts = 0;      // readback requests that never came back
    // The room whose [COLOR] block is currently applied: the grading is
    // applied per frame (it is a handful of setters) but logged once.
    std::string m_roomColorRoom;
    /** The developer console's grading rows (TEMPERATURE / BRIGHTNESS /
     *  CONTRAST). While it is active those numbers own the display and the
     *  room's [COLOR] block stops being re-applied, or the next frame would
     *  undo what was typed to calibrate the room. */
    void applyConsoleGrade();
    bool m_consoleGradeActive = false;
    float m_consoleGradeTempK = 6500.f;
    float m_consoleGradeBrightness = 1.f;
    float m_consoleGradeContrast = 1.f;
    float m_consoleGradeSaturation = 1.f;
    /// The stop the console typed; 0 = the room and its [AUTO_EXPOSURE] own it.
    float m_consoleGradeExposure = 0.f;
    /// Colour-wheel rotation in degrees, 0 = as rendered.
    float m_consoleGradeHue = 0.f;
    /// Period wash, 0 = none.
    float m_consoleGradeSepia = 0.f;
    /// Per-channel gains riding on the white-balance filter, 1 = untouched.
    float m_consoleGradeRed = 1.f;
    float m_consoleGradeGreen = 1.f;
    float m_consoleGradeBlue = 1.f;
    bool showroomActive() const { return m_showroom.active(); }
    /** IBL state saved when a room turns it off, restored when it closes:
     *  the room lights itself, the track behind it still uses the sky. */
    bool m_showroomIblSaved = false;
    bool m_showroomSkyBaked = false; // the room's sun made the environment once
    /** The sun the room declared, kept after the room closes: a drive session
     *  started inside the hall keeps the hall on screen, and its direct light
     *  has to be the same sun the environment was baked for, or shading and
     *  reflections disagree. A room that declares intensity 0 (lamps and IBL
     *  only) keeps that too. */
    float m_roomSunDirection[3] = {0.3f, -0.8f, 0.2f};
    float m_roomSunColor[3] = {1.f, 0.95f, 0.9f};
    float m_roomSunIntensity = 1.f;
    /** Did the room carry a [LIGHT] block? ShowroomConfig forces sun_intensity
     *  to 0 when it did not, and "nothing was declared" is not "no sun": an
     *  undeclared level leaves the engine's daylight in place. */
    bool m_roomSunDeclared = false;
    bool m_roomSunValid = false;
    /** The room's own lamps (empty when it declares none). Kept with the sun
     *  because closeShowroom() hands the light slot to the track's dynamic
     *  lights and a session started in the hall must get both back. */
    std::vector<ks::sim::LocalLight> m_roomLamps;
    /** The rest of the display state the room was drawn with. A session that
     *  keeps the hall on screen must keep these too, or the same room is lit by
     *  one set of numbers and drawn with another (its haze is written for its
     *  radius — 12/27 of the track's — and its stop is its own). */
    float m_roomFogColor[3] = {0.34f, 0.36f, 0.40f};
    float m_roomFogDensity = 0.0001f;
    float m_roomFogFalloff = 0.002f;
    float m_roomDisplayExposure = 1.f;
    bool m_roomIblOn = true;
    /// One line per room, the first frame the session draws it: what the
    /// session took from the room instead of the track.
    bool m_roomDisplayLogged = false;
    /// True while the hall is what the scene shows (cleared when a track loads).
    bool m_showroomHallInScene = false;
    bool m_showroomIblWasOn = true;
    /** True when the open hall is a photo of a place (its surfaces are the
     *  reflection probe): it keeps the probe as its IBL, unlike a hall whose
     *  light is baked into painted materials. */
    bool m_hallIsPhoto = false;
    /** The exposure the open showroom's own ppeffects.ini asks for, or 0
     *  when it declares none: what AC and CSP apply in that room. */
    float showroomExposure() const {
        return m_showroom.active() ? m_showroom.config().exposure : 0.0f;
    }
    const ShowroomScene& showroom() const { return m_showroom; }
    // Roadmap 2.8: load a team roster (dir or team.ini). The roster decides
    // driver names, race numbers, liveries, garage boxes and the grid slots
    // of the player and the AI at session start. No team = legacy identity.
    bool loadTeam(const std::string& teamPath);
    const TeamInfo& team() const { return m_team; }
    // Roadmap 2.9: garage-menu upgrade rows — one per package of the car's
    // upgrades.ini; cycling a row re-resolves the appearance and re-applies
    // physics, render nodes, livery texture and the sound pack.
    std::vector<std::string> upgradeRowLabels() const;
    void cycleUpgradeRow(int row);
    // The loaded .kn5's recorded parameters (model.json's kn5 block) as the
    // CONTENT page's rows: the file the car is built from and the skin it
    // wears. Cycling one writes the document back and rebuilds the car, so the
    // row always describes what is on the turntable.
    std::vector<std::string> carImportRowLabels() const;
    void cycleCarImportRow(int row);
    // CONTENT -> SKINS: the texture sets the loaded car ships, the one it wears
    // (model.json's kn5.skin) and the choice that records it. Choosing rebuilds
    // the car, so the skin on the turntable is the one the document names.
    std::vector<ks::sim::CarSkin> carSkinChoices() const;
    std::string activeCarSkin() const;
    void setCarSkin(const std::string& id);
    int loadBakedScene(const std::string& manifestDir);
    // Roadmap 1.2 / GAP P2.1 — stable visuals for track and car load.
    // applyTrackVisuals swaps the static scene to the track's baked cache
    // (<track>/baked) when one exists, keeps the current scene otherwise.
    void applyTrackVisuals(const std::string& trackDir);
    // ensureCarVisual loads the car's baked meshes, or spawns a solid box
    // placeholder so the vehicle is always visible after load.
    void ensureCarVisual(const std::string& carDir);

    /** Bodywork paint from the CARS colour list: stored and, while a car is
     *  loaded, applied by rebuilding its visuals (the material block is
     *  written once per mesh, so a repaint re-uploads the body). */
    void setCarPaint(float r, float g, float b);
    const float* carPaint() const { return m_carPaint; }

    /** Where the session telemetry CSV files go (user/<player>/telemetry). Empty
     *  = no export. Every session writes its own file, opened on the first
     *  sampled physics step and closed when it stops; KS_TELEMETRY_CSV=<path> overrides
     *  the target, which is what an unattended run keys on. */
    void setTelemetryCsvDirectory(const std::string& dir) { m_telemetryCsvDir = dir; }
    /** Rows per simulated second for the session CSV (settings telemetry.csvHz). */
    void setTelemetryCsvRateHz(double hz);

    /** The cockpit knobs the driver left in the car last session: the TC and
     *  ABS levels and the brake bias in front per cent. The app pushes them
     *  from settings.json; the loop applies them at the end of every loadCar(),
     *  where the car's electronics, setup and upgrades are settled - a level
     *  stored for hardware the new car does not carry still lands on zero. */
    struct CockpitKnobs {
        int tcLevel = 0;
        int absLevel = 0;
        int brakeBiasPct = 0;
        bool hasTc = false;
        bool hasAbs = false;
        bool hasBias = false;
    };
    void setCockpitKnobs(const CockpitKnobs& knobs);
    ks::ecs::Registry& scene();

    void start();
    void stop();
    void reset();
    // Comandi UDP 9666 (REPORT_AC_RE_ENGINEERING.md §3.2): 'K'/'O' riportano
    // l'auto al box (goToSpawnPosition "PIT"), 'P'/'L' resettano l'auto.
    // teleportPlayerToPit = riposizionamento al box con velocità a zero;
    // recoverPlayerInPlace = reset meccanico completo dove si trova.
    void teleportPlayerToPit();
    void recoverPlayerInPlace();
    // Fase 1 exit: start a session of the mode chosen in the menu (practice
    // = open laps solo, race = grid vs AI + countdown, time attack = solo).
    // Configures laps/AI/session byte BEFORE start(), which then builds the
    // grid and countdown from those values.
    void startSession(GameSessionMode mode);
    /// Same, with the values MODE SETUP collected: laps, AI cars, session
    /// clock and whether the player starts in the garage.
    void startSession(GameSessionMode mode, const SessionStartParams& params);
    bool isRunning() const { return m_running; }
    void tick();

    bool handleUiKey(int virtualKey);
    bool handleUiChar(int character);
    bool handleUiMouseMove(float x, float y);
    bool handleUiMouseButton(ui::MouseButton button, bool down, float x, float y);
    bool handleUiMouseWheel(float delta, float x, float y);
    /** Content Manager's showroom gesture: the component of the car under the
     *  pointer is selected, tinted, and reported in the inspector panel (mesh
     *  name, material name, that material's parameters). Returns whether the
     *  click selected something, so the caller knows it was used. */
    bool pickShowroomPart(float designX, float designY);
    void clearShowroomSelection();
    void refreshShowroomInspector();
    /** Selects a mesh by name: the pick's second half, and the commit path's
     *  way back after the reload replaced the meshes. */
    bool selectShowroomMesh(const std::string& name);
    /** Writes one inspector row to the car's model.json (MAT's document) and
     *  reloads the car, then reselects the same part. Returns the footer.
     *  By value: the reload replaces the panel's strings the caller's
     *  references may alias, so a reference would dangle before the return
     *  reads it (the footer lost its key that way). */
    std::string commitInspectorEdit(std::string material, std::string key, std::string value);
    /** Pointer motion the UI did not consume: in cockpit view it drives the
     *  freelook the controls document authors. Returns whether it was used. */
    bool handleLookMouseMove(float x, float y);

    InputManager* inputManager() { return m_input.get(); }

    /** The last frame the controllers fed InputManager, and the hand poses the
     *  runtime gave us. Empty until a session runs and a controller answers;
     *  the poses are the hand-off point for the cockpit camera and the HUD. */
    const ks::sim::VrInputFrame& vrInputFrame() const { return m_vrFrame; }
    /** The tracked head for this frame: valid only while a session runs and the
     *  runtime gives a pose. The cockpit camera follows it. */
    const ks::sim::VrHeadPose& vrHeadPose() const { return m_vrHeadPose; }
    /** How many blades the last grass field carried, and whether one exists.
     *  Zero is the normal answer on a track whose surfaces declare no grass. */
    /** The grip scale of the last physics step, per wheel, and the kind of ground
     *  each one stood on. Readable so the HUD (and a test) can show why the car
     *  let go. */
    const ks::sim::WheelSurfaceGrip& wheelSurfaceGrip() const { return m_wheelSurfaceGrip; }
    unsigned grassBlades() const { return m_grassBlades; }
    bool grassFieldBuilt() const { return m_grassBuilt; }
    const ks::sim::VrControllerPose& vrControllerPose(int hand) const {
        return m_vrPose[(hand > 0) ? 1 : 0];
    }
    bool vrInputActive() const { return m_vrFrame.connected; }
    CameraController* camera() { return m_camera.get(); }
    SimulatorAudio* audio() { return m_audio.get(); }
    /** The garage screen, with the loaded car's own aid availability applied: a
     *  car whose electronics say PRESENT = 0 offers no TC/ABS level. */
    SetupGarage* setupGarage();
    /// The car folder the setup screen is currently showing: the screen reads
    /// the car's own setup.ini, so it has to follow a car change.
    std::string m_setupGarageCar;
    /// The setup screen is up: a capture keeps the interface out, but this is
    /// the screen the capture was asked for. Defined where SetupGarage is
    /// complete (SimulationLoop.cpp): the header only forward-declares it.
    bool setupScreenVisible() const;
    ui::NativeUiHub& ui() { return m_ui; }
    ui::UiGpuPass* uiGpu() { return m_uiGpu.get(); }
    const SimTrackData& trackData() const { return m_trackData; }
    ks::physics::VehicleSimulator* vehicle() { return m_vehicle.get(); }
    MultiCarManager* multiCarManager() { return m_multiCar.get(); }
    NetworkManager* networkManager() { return m_network.get(); }

    FeatureHub& features() { return m_features; }
    const FeatureHub& features() const { return m_features; }
    // Starts discovery + the external control API and wires the FeatureHub
    // callbacks back into this loop (see SimulationLoop.cpp).
    void startFeatureServices(bool hostAnnounce = false);
    // Replay (GAP_MATRIX P0.5): record the session to replay/<name>.ksrep and
    // play it back. While a recording plays it owns the car (see
    // updateReplayPlayback): the recorded pose and controls are authoritative,
    // so the driver's input and the physics step are skipped instead of
    // fighting them. The camera is unchanged — it already follows the car.
    bool startReplayPlayback(const std::string& path);
    void stopReplayPlayback();
    bool replayPlaying() const { return m_features.replay.isPlaying(); }
    bool replayRecording() const { return m_features.replay.isRecording(); }
    /** True while a loaded recording is what the car is showing. */
    bool replayLoaded() const { return m_features.replay.isLoaded(); }
    void pauseReplayPlayback();
    void resumeReplayPlayback();
    /** Scrub relative to the playhead, clamped to the recording. */
    void seekReplayBy(float deltaSec);
    void setReplaySpeed(float speed);
    float replaySpeed() const { return m_features.replay.playbackSpeed(); }
    /** Recordings found in replay/ (.ksrep), sorted by path. */
    std::vector<std::string> replayFiles() const;
    /** (file name, path) pairs for the REPLAY menu page. */
    std::vector<std::pair<std::string, std::string>> replayChoices() const;
    /** Starts recording, or stops and saves the current one. Returns the file
     *  written by a stop, empty otherwise. */
    std::string toggleReplayRecording();
    /** Saves the current (already stopped) recording to the next free
     *  replay/session_NNN.ksrep. Empty string on failure. Used by the
     *  manual toggle and by the session-end autosave (P0.3). */
    std::string saveReplayRecording();
    /** P0.2 commit (REPORT §6): reads track/ai/fast_lane.ai.candidate and
     *  writes it as fast_lane.ai for the loaded track. False when there is
     *  no candidate or the track directory is unknown. */
    bool promoteAiCandidate();

    // Roadmap 2.7: pit strategy + explicit box-service request. The plan
    // itself queues nothing (see PitStrategyBridge.h): requestPitService is
    // the player's "I am in the box, do the confirmed work now" signal,
    // gated on stationary-in-pit by updatePitRepair.
    void requestPitService(bool on = true) { m_requestPitService = on; }
    PitStrategyState& pitStrategy() { return m_pitStrategy; }
    /**
     * Roadmap 2.7: box service (SimulationLoop_Pit.cpp). Called from tick()
     * at the physics rate; a plan alone queues nothing, the service only
     * starts on requestPitService() while the car is stopped inside its box.
     */
    void updatePitRepair(float dt);
    /** Last PitLaneRepair status ("READY FOR SERVICE", "FUEL 62%", …) for HUD/log. */
    const char* pitServiceStatus() const { return m_pitStatus.c_str(); }
    bool pitServiceBusy() const { return m_pitRepair.isBusy(); }
    /** Box the player's service is bound to (roadmap 2.7 in-box test). */
    void bindPlayerBox(const WorldPose& box) { m_playerBox = box; m_hasPlayerBox = true; }

    // Roadmap 2.7 — garage exit / pit entry: the GarageExit.h state machine
    // (InGarage → Preparing → … → OnTrack, and back) driven from
    // SimulationLoop_Pit.cpp. The player leaves with requestLeaveGarage()
    // (menu row or the P key) and comes back by driving slowly into the box,
    // which is what arms the box service.
    void requestLeaveGarage(bool on = true) { m_requestLeaveGarage = on; }
    void requestReturnToBox(bool on = true) { m_requestReturnBox = on; }
    void updateGarageExit(float dt);
    GarageExitPhase garagePhase() const { return m_garageExit.phase(); }
    /** True while the loop keeps the car in its garage box (held or blocked). */
    bool isHeldInGarage() const {
        const GarageExitPhase p = m_garageExit.phase();
        return m_garageExitEnabled &&
               (p == GarageExitPhase::InGarage || p == GarageExitPhase::Preparing ||
                p == GarageExitPhase::EngineStart || p == GarageExitPhase::BoxClear ||
                p == GarageExitPhase::Blocked);
    }
    /** Last GarageExit status ("IN GARAGE", "PIT LIMIT", …) for HUD/log. */
    const char* garageStatus() const { return m_garageStatus.c_str(); }
    // FeatureHub pump (discovery, control API, track limits, weather).
    // withSession=false freezes track limits - used while the world is
    // paused or the session has not started yet.
    void pumpFeatureHub(float dt, bool withSession);
    /** Hands the loaded racing line to the track-limits monitor as its
     *  centreline, so a car wide of the track is detected in green instead of
     *  only when the phase says "not racing". Called after every spline load
     *  (track load, layout switch); an empty line leaves the monitor on the
     *  phase hint alone. */
    void updateTrackLimitsCenterline();
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
    // Frame pacing for the config's video.frameLimit: 0 leaves the frame free
    // (vsync still caps it), any other value sleeps the rest of the budget.
    // Particle emitters (dust, rain column, wheel spray) from video.particles.
    // KS_PARTICLES=1 still forces them on, for a capture.
    void setParticlesEnabled(bool on) { m_particlesConfig = on; }
    bool particlesEnabled() const { return m_particlesConfig; }

    void setFrameLimit(int fps) { m_frameLimit = fps > 0 ? fps : 0; }
    int frameLimit() const { return m_frameLimit; }

    /** Photo mode's camera: enters the orbital free camera around the player's
     *  car (around what the camera is looking at when there is no car) and
     *  leaves it again, restoring the mode that was in use before. */
    void setPhotoOrbit(bool on);
    bool photoOrbitActive() const { return m_camera && m_camera->photoOrbit(); }

    /** Spectator view (SpectatorMode): the camera rides the followed car. ON
     *  enters and follows the first car on the grid, OFF gives the player's car
     *  back, NEXT cycles the grid. Driven by the control API's SPECTATE verb. */
    bool setSpectating(bool on);
    int spectateNext();
    bool isSpectating() const;

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
    // Roadmap 2.1: applying a preset also feeds WeatherSimulator, which
    // evolves wetness from here on (rain soaks, dry air dries).
    void setWeatherPreset(const ks::physics::WeatherState& state) {
        m_weather = state;
        m_weatherSim.setState(state);
        m_weatherSim.start();
    }
    // Gate 2.11 enabler: the same dry/damp/wet presets the menu applies,
    // reachable without the UI (KS_WEATHER env in SimulatorApp, tests).
    void setWeatherPresetByName(const std::string& name) {
        const ks::sim::WeatherPreset wp = ks::sim::presetByName(name);
        ks::physics::WeatherState ws;
        ws.ambientTemp = wp.ambientC;
        ws.trackTemp = wp.trackC;
        ws.trackWetness = wp.wetness;
        ws.rainIntensity = wp.rain;
        ws.windSpeed = wp.windMs;
        ws.cloudCover = wp.cloud;
        setWeatherPreset(ws);
    }
    const ks::physics::WeatherState& weatherState() const { return m_weather; }

    /** [GRASS_FX] della pista caricata (P1 CSP): assente = erba autoriale. */
    const ks::sim::GrassFxConfig& grassFx() const { return m_grassFx; }
    const ks::sim::LightScheduler& lightSchedule() const { return m_lightSchedule; }

    void setFfbEnabled(bool e) { m_ffbEnabled = e; }
    bool ffbEnabled() const { return m_ffbEnabled; }

    void setSharedMemoryEnabled(bool e) { m_shmEnabled = e; }

    void setRaceFlag(RaceFlag f) { m_raceSession.setFlag(f); }
    RaceFlag raceFlag() const { return m_raceSession.flag(); }
    /** Standings feed for the RESULTS screen (roadmap 1.3). */
    RaceSessionManager& raceSession() { return m_raceSession; }
    bool sharedMemoryEnabled() const { return m_shmEnabled; }

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


    /** Phase 3.3: rebind layout-specific data without full track unload. */
    bool switchLayout(const std::string& layoutId);
    const TrackLayoutCatalog& layoutCatalog() const { return m_layouts; }
    /** Phase 3.3: what a picker shows, in catalog order, and what is bound. */
    std::vector<std::pair<std::string, std::string>> layoutChoices() const;
    std::string activeLayoutId() const;
    /** Why the last switchLayout() failed, for the HUD; empty = no problem.
     *  A guest whose copy lacks the host's layout keeps this set. */
    const std::string& layoutNotice() const { return m_layoutNotice; }

    // Trackside posts (TracksidePosts.h): the shared safety-car bay at the pit
    // exit and four marshals per corner. Loaded from the track manifest; the
    // yellow comes from the posts that actually see an incident.
    const TracksidePosts& tracksidePosts() const { return m_tracksidePosts; }
    RaceFlag localYellowFlag() const { return m_tracksidePosts.trackFlag(); }
    bool loadTracksidePostsFromTrack(const std::string& trackDir);
    void updateTracksidePosts(float dt);
    // TrackRecovery (TrackRecovery.h): a car that stops out on track with a dead
    // engine is towed to its box by the marshals — the one place the crew can
    // fire it again.
    void updateTrackRecovery(float dt);
    const TrackRecovery& trackRecovery() const { return m_recovery; }
    // The marshal is one shared asset (content/characters/marshal), loaded once
    // and instanced at each post's coordinates: a track names positions, not
    // geometry of its own.
    bool loadMarshalAsset(const std::string& trackDir);
    void spawnMarshalPosts();
    void despawnMarshalPosts();

    void snapVehicleToPose(ks::physics::VehicleSimulator* veh, const WorldPose& pose);

private:
    void applyInput();
    /** GAP_MATRIX P0.3: the garage screen edits SetupData, not the vehicle's
     *  own params. This is the only place the two meet, so a row the driver
     *  moves reaches the physics instead of staying a copy on the screen. */
    void applySetupToVehicle(const SetupData& s);
    /** Opens this session's telemetry file; stop() closes it. */
    void openSessionTelemetryCsv();
    /** One telemetry CSV row when the fixed sampler is due (physics step). */
    void sampleSessionTelemetry(float stepSeconds);
    /** Pushes CockpitKnobs onto the loaded car: only the three numbers move. */
    void applyCockpitKnobs();
    /** Feeds one physics step from the playhead. Returns true when the replay
     *  drove the car this step, so the caller skips input and physics. */
    bool updateReplayPlayback(float dt);
    /** Samples the running recording at the physics rate (no-op otherwise). */
    void recordReplayStep();
    void render(float dt = 1.0f / 60.0f);
    void updateCamera(float dt);
    void updateAndDrawParticles(float dt);
    void initTracksideTerrain();
    void updateWeather(float dt);
    void syncCarTransforms();
    // Roadmap 1.2: scene/car visual bookkeeping. Entities spawned from a
    // manifest are tracked so a track switch can despawn exactly them (the
    // car visuals live in a second list and survive the swap).
    int spawnSceneEntities(const std::string& manifestDir);
    void despawnSceneEntities();
    void despawnCarVisuals();
    // Rebuilds the car from what its document now says (the kn5 block): the
    // texture cache is dropped whole first, because it answers "already
    // uploaded" for the old skin's file names.
    bool reloadCarAfterParameters();
    // Roadmap 2.8: player garage/grid placement + AI visual entities (one
    // per spawned AI car, meshes shared per livery key). No-op headless.
    void placePlayerForSession();
    /** The garage row the venue itself describes, in the order a driver would
     *  look for the boxes: the ones its author placed (pit_boxes.ini), then
     *  AC's per-stall markers (AC_PIT_0..N, from the .kn5 or its bake), then
     *  the pit lane centre line boxed every 6 m. False when the track carries
     *  none of the three, which leaves the caller's invented row. */
    bool garageRowFromTrack(int boxCount, GarageLayout& out);
    void spawnSessionAiVisuals();
    void despawnAiVisuals();
    // Roadmap 2.9: upgrades.ini -> appearance -> physics/nodes/livery/sound.
    void loadUpgradesForCar(const std::string& carDir);
    void refreshUpgradeApplication();
    void applyUpgradePhysics();
    void applyUpgradeAudio();
    void applyUpgradeSelectionString(const char* spec);
    // Loads a bake's meshes under a name prefix (materials, textureDir and
    // node filtering applied); shared by the player car and the AI sets.
    std::vector<std::string> loadCarMeshSet(const std::string& baked,
                                            const std::string& prefix,
                                            const std::string& textureDir,
                                            bool* materialsApplied = nullptr);
    std::vector<ks::ecs::Entity> m_sceneEntities;
    std::vector<ks::ecs::Entity> m_carVisualEntities;
    // Showroom inspector: the mesh under the pointer, the mesh as it was before
    // the selection tint (so deselecting restores it exactly), and whether a
    // part is selected at all.
    std::string m_inspectedMesh;
    NativeMesh m_inspectedOriginal;
    bool m_inspectedValid = false;
    /** A tint the inspector wants on one mesh, applied in render(): uploading
     *  it from the window procedure would replace buffers a submitted frame
     *  still reads, which the driver does not survive. */
    struct ShowroomTintRequest {
        std::string mesh;
        float tint[3] = {1.f, 1.f, 1.f};
        float paintSolid = 0.f;
    };
    void applyPendingTints();
    std::vector<ShowroomTintRequest> m_pendingTints;
    // The surface's own tint and coat, captured when it is selected so
    // deselecting restores exactly what the content had.
    float m_inspectedTint[3] = {1.f, 1.f, 1.f};
    float m_inspectedPaintSolid = 0.f;
    void broadcastLocalCarState();
    void handleRemoteCarState(uint32_t carId, const net::CarStateData& state);
#if HAS_KSNET
    /** Host damage snapshot for a car in `m_remoteCarIds` (ignored for our own
     *  id, which the local physics owns). The type lives in the transport
     *  header, which this header does not pull in, so the declaration is
     *  guarded with the rest of the network wiring. */
    void applyRemoteCarDamage(const net::CarDamageMessage& msg);
#endif
    void ensureScenePipeline();
    void syncUiFromVehicle(float dtSeconds);
    /** Push the GameplayTweaks set into the car (fuel and tyre multipliers),
     *  the AI tuning and the track-limits rule ladder. */
    void applyGameplayTweaks();
    void applyDamageEffects();
    // Last AI tuning pushed to the field, so [AI] logs on a change instead of
    // on every re-apply.
    float m_appliedAiPace = 1.0f;
    float m_appliedAiAggression = 0.5f;
    // Last rule set applied, so [TWEAKS] logs the switch once.
    bool m_appliedStrictRules = false;
    // Four wheels' visual state: deformation from the load and the flat spot a
    // locked wheel grinds in. Reset when another car is loaded.
    ks::sim::TyreVisualTracker m_tyreVisuals;
    // The player car's wheel nodes: the entity that draws the node, its pivot
    // in car space, and the angle it has turned so far.
    struct WheelVisual {
        ks::ecs::Entity entity = ks::ecs::kNullEntity;
        ks::math::vec3 pivot;
        float spinRad = 0.0f;
    };
    WheelVisual m_wheelVisuals[4];
    bool m_wheelsBound = false;
    // Blob shadows: the entity that draws each one, and the opacity level whose
    // geometry was last uploaded (the alpha rides in the vertices).
    struct WheelShadowVisual {
        ks::ecs::Entity entity = ks::ecs::kNullEntity;
        std::string meshName;
        int opacityStep = -1;
    };
    ks::ecs::Entity m_grassEntity = ks::ecs::kNullEntity;
    ks::math::vec3 m_grassCenter{};
    bool m_grassBuilt = false;
    unsigned m_grassBlades = 0;
    ks::sim::VrInputFrame m_vrFrame;
    ks::sim::VrHeadPose m_vrHeadPose;
    ks::sim::VrHeadRecentre m_vrRecentre;
    unsigned long long m_vrSessionSerial = 0; // which play space the reference is from
    bool m_vrRecentreHeld = false;           // the grip's rising edge is what recentres
    ks::sim::WheelSurfaceGrip m_wheelSurfaceGrip;
    ks::sim::SurfaceGripConfig m_surfaceGripCfg;
    ks::sim::VrControllerPose m_vrPose[2];
    WheelShadowVisual m_wheelShadows[4];
    bool m_wheelShadowsBound = false;
    ks::sim::WheelShadowConfig m_wheelShadowCfg;
    ks::sim::WheelShadowQuad m_wheelShadowQuad[4];
    /** Wheel LED feed and shift flash, stepped once per physics step. */
    void updateWheelLeds(float stepSeconds);
    /** Tyre visual state (bulge under load, flat spot after a lock-up), stepped
     *  with the same clock as the wheel strip and published in the HUD sample. */
    void updateTyreVisuals(float stepSeconds);
    const ks::sim::TyreVisualTracker& tyreVisuals() const { return m_tyreVisuals; }
    /** Binds the car's wheel nodes (node name -> corner, pivot from the mesh's
     *  own bounds) and drives their per-node transform from the tyre state, so
     *  the visual stops at the HUD sample and reaches the mesh. */
    /** Reads the VR controllers for this frame and feeds them to InputManager.
     *  Called every tick; does nothing while no session runs, which is the
     *  desktop case and costs one cached call. */
    void pollVrInput();

    /** The grass field around the car: rebuilt when the car has moved far enough
     *  that the edge of the last one would show, not every frame. */
    void updateGrassField(const ks::math::vec3& focus);
    /** Reads the ground under each wheel from the shared query and hands the four
     *  multipliers to the vehicle, once per physics step. */
    void updateWheelSurfaceGrip();

    void bindWheelVisuals();
    void updateWheelVisuals(float stepSeconds);
    /** The blob shadow under each tyre: one blended mesh per wheel, placed at the
     *  contact point and fed by WheelShadowing. Called once per frame with the
     *  body pose, because the shadow is drawn, not simulated. */
    void bindWheelShadows();
    void updateWheelShadows(const ks::math::vec3& bodyPos, const ks::math::vec3& bodyRot);
    void publishSharedMemory();
    void publishUdpTelemetry();
    void publishTcpTelemetry();
    void publishAcRemoteTelemetry();
    void updateLapAndSurface(double dt);
    void beginRaceSession();
    void finishGoldenExport();
    static std::string readFileText(const std::string& path);

    bool m_vulkanMode = true;
    NativeRenderer* m_vulkanRenderer = nullptr;
    std::unique_ptr<InputManager> m_input;
    std::unique_ptr<CameraController> m_camera;
    float m_lookMouseX = 0.0f;
    float m_lookMouseY = 0.0f;
    bool m_lookMouseValid = false;
    // The camera mode photo mode took over, restored when it exits.
    CameraController::Mode m_photoPrevMode = CameraController::Mode::Chase;
    ShowroomScene m_showroom;
    bool m_showroomHallLoading = false; // true while loadShowroomHall() is loading its hall
    bool m_showroomAudioLoaded = false;
    bool m_themeSoundsLoaded = false;
    std::unique_ptr<ks::physics::VehicleSimulator> m_vehicle;
    std::unique_ptr<MultiCarManager> m_multiCar;
    // SETTINGS -> GAMEPLAY: the spotter's calls are text, in the notice list.
    Spotter m_spotter;
    std::vector<SpotterCar> m_spotterCars;
    FeatureHub m_features;
    netsync::CarStateSync m_carSync;
    double m_simTimeSec = 0.0;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<SimulatorAudio> m_audio;
    std::unique_ptr<SetupGarage> m_setupGarage;
    // What the loaded car says about its aids: 1 has it, 0 does not, -1 unknown.
    int m_tcPresent = -1;
    int m_absPresent = -1;
    std::unique_ptr<ks::ac::AcSharedMemoryPublisher> m_shm;
    std::unique_ptr<ks::ac::AcRemoteTelemetryServer> m_acRemote;
    std::unique_ptr<UdpTelemetryBridge> m_udp;
    std::unique_ptr<TcpTelemetryBridge> m_tcp;

    ui::NativeUiHub m_ui;
    // Dev-mode scripts: the console's LUA verb loads one, and every frame
    // hands it the session numbers below (see DevScripting.h for the rules).
    DevScriptBridge m_scripts;
    std::shared_ptr<ui::UiGpuPass> m_uiGpu;
    int m_viewW = 1280;
    int m_viewH = 720;

    bool m_particlesWanted = false;
    bool m_particlesInit = false;
    bool m_particlesConfig = false;
    ks::engine::graphics::ParticleSystem m_particles;
    std::vector<float> m_particleQuads;

    bool m_terrainWanted = false;
    bool m_terrainInit = false;

    SimTrackData m_trackData;
    ks::sim::GrassFxConfig m_grassFx{}; // [GRASS_FX] della pista caricata
    ks::sim::LightScheduler m_lightSchedule{}; // schedulazione luci
    ks::sim::SeasonControl m_season;    // fondazione seasons (P1 CSP)
    ks::physics::LapSectorTimer m_lapTimer;
    RaceSessionManager m_raceSession;
    float m_lapDistance = 0.f;
    float m_normalizedSpline = 0.f;
    // AI trace recorder (practice mode): samples (x,y,z,speed) during a lap
    // and writes them to track/ai/fast_lane.ai.candidate on lap completion.
    std::vector<std::array<float, 4>> m_aiTrace;
    bool m_aiTraceRecording = false;
    // P0.2 commit: a candidate was written this session (auto-promotion at
    // session end only when no authored fast_lane.ai exists to clobber).
    bool m_aiCandidateWritten = false;
    bool m_shmEnabled = true;
    bool m_udpEnabled = true;
    bool m_acRemoteEnabled = true;
    uint16_t m_acRemotePort = 9996;
    std::string m_udpHost = "127.0.0.1";
    uint16_t m_udpPort = 20777;
    bool m_tcpEnabled = true;
    uint16_t m_tcpPort = 20778;
    double m_simTime = 0.0;

    std::chrono::steady_clock::time_point m_lastTime{};
    void paceFrame();
    int m_frameLimit = 0;
    std::chrono::steady_clock::time_point m_frameStart{};
    double m_simAccumulator = 0;
    static constexpr double m_physicsDt = 0.001;

    // Last gameplay tweak value pushed to the car, so the log reports a change
    // once instead of on every setup apply. -1 means "nothing applied yet",
    // so the first apply always prints which set is in force.
    float m_appliedFuelTweak = -1.0f;
    float m_appliedTireTweak = -1.0f;

    // Wheel LED feed (updateWheelLeds): the last values pushed to the module,
    // so a change is reported once per step instead of every step.
    int m_ledRpmPct = -1;
    int m_ledGear = -1;
    int m_ledFuelPct = -1;
    // The two warnings as last pushed, so a steady strip is not rewritten every
    // physics step.
    bool m_ledBrakeHot = false;
    bool m_ledAbsActive = false;
    // Every driving warning, decided once per step (physics/DrivingWarnings.h)
    // and then read by the strip, the H-DASH and the car's own dash: one
    // calculation, so no consumer can be left out or disagree.
    ks::physics::DrivingWarnings m_drivingWarnings;

    // autoReverse (controls.json = 1): da fermo, tenere il freno per qualche
    // istante mette la retromarcia e scambia i pedali (freno = gas in retro,
    // gas = freno), finché la macchina torna a fermo (stile rF2).
    double m_autoReverseHold = 0.0;
    bool m_autoReverseActive = false;

    std::unique_ptr<ks::device::FFBBase> m_ffb;
    bool m_ffbEnabled = true;
    bool m_running = false;
    bool m_trackLoaded = false;
    bool m_carLoaded = false;
    CockpitKnobs m_cockpitKnobs;

    // Session telemetry dump: the row clock, where the files go, and the file
    // itself (SessionTelemetryCsv.h - header first, a row every 50 ms of sim).
    SessionTelemetryCsv m_sessionCsv;
    std::string m_telemetryCsvDir;
    SessionTelemetrySampler m_telemetrySampler;
    // Seconds between rows; the sampler gets it when the session opens.
    double m_telemetryIntervalS = SessionTelemetrySampler::kDefaultIntervalS;
    // Speed at the end of the previous physics step, so a row's longitudinal
    // acceleration is that step's own derivative. Seeded with the car's speed
    // when the session opens (openSessionTelemetryCsv), which is why there is
    // no "first row" special case to carry around.
    float m_telemetryPrevSpeedMps = 0.0f;

    float m_timeOfDay = 12.0f;
    // Frame clock behind the windscreen rain animation (see updateWeather).
    float m_weatherClock = 0.0f;
    ks::physics::WeatherState m_weather{};
    // Roadmap 2.1 (P2.8): evolves the preset above over time — rain
    // accumulation / track drying — and is mirrored back into m_weather
    // every frame by updateWeather().
    ks::physics::WeatherSimulator m_weatherSim;

    uint8_t m_sessionType = 0;
    uint8_t m_sessionPhase = 0;
    int m_currentLap = 0;
    int m_totalLaps = 0;
    double m_timeRemaining = 0.0;
    // false = rolling start: beginRaceSession() skips the countdown.
    bool m_sessionStandingStart = true;
    // 0 = no clock; otherwise the limit handed to RaceSessionManager.
    int m_sessionTimeLimitSeconds = 0;

    ks::physics::PhysicsGolden m_goldenRecorder;
    bool m_goldenExportActive = false;
    double m_goldenSampleAccum = 0.0;
    double m_goldenTime = 0.0;
    std::string m_goldenExportPath;

    std::string m_spawnedSceneDir;
    bool m_pipelineInitialized = false;
    uint32_t m_streamlineFrameIndex = 0;
    std::string m_carName;
    // Bodywork tint currently applied (1,1,1 = the texture as baked).
    float m_carPaint[3] = {1.0f, 1.0f, 1.0f};
    // True once the player picked a paint: an explicit choice may replace a
    // livery, while the factory colour never does.
    bool m_carPaintExplicit = false;
    // True when the car folder declares its own colour list. Without one
    // there is no factory paint at all, so the body keeps the texture it
    // ships with instead of being repainted white.
    bool m_carPaintDeclared = false;
    std::string m_carDir; // car folder of the loaded car (roadmap 2.8/2.9)
    // Content's [BRAKEDISC_FX], from the car's model document: ACTIVE turns the
    // disc glow off, GLOW_MULT scales it. Read at loadCar, never per frame.
    ks::graphics::BrakeDiscFx m_brakeDiscFx;

    // Roadmap 2.8 — team roster (team.ini): empty slots = no team loaded,
    // which keeps the legacy identity ("AI N", standings "Car N").
    TeamInfo m_team;
    std::vector<FieldSlot> m_sessionField; // built at beginRaceSession, grid order

    // Roadmap 2.8 — AI visual entities (one block per spawned AI car; meshes
    // are shared per livery key so N cars with the same skin load once).
    struct AiVisual {
        int carId = -1;
        std::string liveryKey; // empty = base car textures
        std::vector<ks::ecs::Entity> entities;
    };
    std::vector<AiVisual> m_aiVisuals;
    // livery key ("base", a skin id, ...) -> loaded render names: every AI
    // car with the same skin shares one mesh set.
    std::unordered_map<std::string, std::vector<std::string>> m_aiMeshSets;

    // Roadmap 2.9 — upgrade packages + resolved appearance for m_carDir
    // (UpgradeRuntime is defined in SimulationLoop.cpp).
    std::unique_ptr<UpgradeRuntime> m_upgradeRt;

    int m_aiCarCount = 0;
    std::vector<int> m_aiCarIds;
    std::vector<int> m_aiLastLaps;

    static constexpr uint32_t kNoNetCarId = 0xFFFFFFFFu;
    struct RemoteInput {
        float throttle = 0.0f;
        float brake = 0.0f;
        float steering = 0.0f;
    };
    std::unordered_map<int, RemoteInput> m_remoteInputs;
    std::unordered_map<uint32_t, int> m_remoteCarIds;
    uint32_t m_ownNetCarId = kNoNetCarId;

    int m_mpServerClients = -1;

    // Roadmap 2.7: pit strategy + box-service state. updatePitRepair() lives
    // in SimulationLoop_Pit.cpp (compiled); it reads the plan, gates the
    // service on the car being stopped inside m_playerBox and feeds
    // PitLaneRepair, which is self-contained.
    PitStrategyState m_pitStrategy;
    bool m_requestPitService = false;
    PitLaneRepair m_pitRepair;
    WorldPose m_playerBox{};     // garage box the service is bound to
    bool m_hasPlayerBox = false; // false until placePlayerForSession binds it
    float m_pitBoxRadiusM = 8.f; // "inside the box" footprint
    TrackRecovery m_recovery;    // stranded car -> towed home (TrackRecovery.h)
    std::string m_pitStatus;     // last PitLaneRepair status text
    bool m_pitTyresApplied = false; // new rubber already fitted this stop
    // P0.1 min-speed penalty (pit limiter, REPORT §5): one booking per pit
    // pass, cleared when a new pass starts (RollingOut).
    bool m_pitOverspeedBooked = false;

    // Roadmap 2.7 — garage exit / pit entry (GarageExit.h). Enabled only for
    // sessions that spawn in a garage box: a race grid start calls
    // forceOnTrack() and stays exactly as free as before.
    GarageExitController m_garageExit;
    bool m_garageExitEnabled = false;
    bool m_requestLeaveGarage = false;
    bool m_requestReturnBox = false;
    std::string m_garageStatus;
    // Replay: the recorder owns its clock and playhead, so the loop keeps no
    // replay state of its own — only the question "who drives the car this
    // step", which m_features.replay.isPlaying() answers.

    // Roadmap 2.7 — multi-car pit-lane order (PitLaneQueue.h): with one car
    // on track nothing ever blocks, so the behaviour is unchanged; with a
    // field it decides who rolls out first and holds the others in the box.
    // KS_PIT_QUEUE=0 turns it off (same env-var style as the rest of kssim).
    PitLaneQueue m_pitQueue;
    bool m_pitQueueEnabled = false; // true once placePlayerForSession binds
    static constexpr int kPitQueuePlayer = -1; // player's id in the queue

    // Phase 3.3: catalog of the track's layouts (TrackLayout.h).
    TrackLayoutCatalog m_layouts;
    // Set when a requested layout is not in the catalog (e.g. a guest told to
    // switch to a layout its copy of the track lacks); shown in the HUD.
    std::string m_layoutNotice;
    // Marshal posts + safety-car bay, from <track>/data/data.json.
    TracksidePosts m_tracksidePosts;
    // Instances of the shared marshal asset, one per post; the mesh itself is
    // kept in the renderer across tracks (m_marshalAssetLoaded).
    std::vector<ks::ecs::Entity> m_marshalEntities;
    bool m_marshalAssetLoaded = false;

    // Roadmap 2.7 — garage row (GarageSpawn.h) + pit-lane collision. The
    // default garage session lays the row out with makeLinearRow() and binds
    // box 0; m_snapHoldSec keeps the car planted for a moment after the snap
    // so the first throttle does not fight the teleport.
    GarageLayout m_garageLayout;
    // The venue's own stall centres (AC_PIT_0..N), collected while its .kn5 is
    // read in memory; empty when the track is baked or unbaked but unbaked
    // content still carries them, so the row is derived from what is there.
    std::vector<WorldPose> m_trackPitMarkers;
    PitLaneCollision m_pitCollision;
    bool m_pitSystemsReady = false;
    float m_snapHoldSec = 0.f;
    // Time the car stays frozen after snapVehicleToPose(), so the throttle
    // does not fight the teleport on the first frame out of the box.
    static constexpr float kSnapHoldDuration = 0.35f;

};

} // namespace ks::sim
