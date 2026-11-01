#pragma once

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
#include "CarStateSync.h"
#include "CarStateSyncBridge.h"
#include "TeamSessionBridge.h" // roadmap 2.8: team.ini roster -> session field
#include "PitStrategyBridge.h"  // roadmap 2.7: menu pit plan -> PitRepairInput
                             // (pulls in PitLaneRepair.h -> GarageExit.h /
                             // GarageSpawn.h, all self-contained)
#include "PitLaneQueue.h"    // roadmap 2.7: multi-car pit-lane order
#include "TrackLayout.h"     // phase 3.3: multi-layout catalog
#include "TracksidePosts.h" // safety-car bay + four marshal posts per corner

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
    Vec3f operator+(Vec3f o) const { return {x + o.x, y + o.y, z + o.z};
    }
    Vec3f operator*(float s) const { return {x * s, y * s, z * s};
    }
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
    std::string directory;       // track root folder
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
    /** Phase 3.3: rebind layout-specific data without full track unload. */
    bool switchLayout(const std::string& layoutId);
    const TrackLayoutCatalog& layoutCatalog() const { return m_layouts; }
    bool loadCar(const std::string& carDir);
    bool loadCarAudio(const std::string& carDirectory);
    bool loadTeam(const std::string& teamPath);
    const TeamInfo& team() const { return m_team; }
    std::vector<std::string> upgradeRowLabels() const;
    void cycleUpgradeRow(int row);
    int loadBakedScene(const std::string& manifestDir);
    void applyTrackVisuals(const std::string& trackDir);
    void applyCarVisuals(const std::string& carDir);
    const SimTrackData& trackData() const { return m_trackData; }

    // Trackside posts (TracksidePosts.h): the shared safety-car bay at the pit
    // exit and four marshals per corner, two right and two left. Loaded from the
    // track manifest and driven by updateTracksidePosts(): the yellow comes from
    // the posts that actually see an incident, not from a session timer.
    const TracksidePosts& tracksidePosts() const { return m_tracksidePosts; }
    RaceFlag localYellowFlag() const { return m_tracksidePosts.trackFlag(); }
    bool loadTracksidePostsFromTrack(const std::string& trackDir);
    void updateTracksidePosts(float dt);

private:
    // Marshal posts + safety-car bay, from <track>/data/data.json.
    TracksidePosts m_tracksidePosts;
    // NOTE: truncated intermediate methods remain as on disk — full file follows in next push if needed
};

} // namespace ks::sim
