// Parity 3.5 — AI racing: staggered grid spawn on the track's real
// fast_lane.ai, AI driving (position + speed factor), lap wrap detection,
// and the RaceSessionManager standings feed (identity lookup after re-sort).
//
// Qt-free: compiles the simulator TUs directly (track_loader_test pattern).

#include "KsTest.h"
#include "AIController.h"
#include "MultiCarManager.h"
#include "RaceSessionManager.h"
#include "engine/AI/AiFileReader.h"
#include "engine/AI/AiFileWriter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ks::sim;

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kRadius = 50.0f;          // oval radius [m]
constexpr int kPoints = 180;              // ~1.7 m between points
constexpr float kLineSpeed = 14.0f;       // AI line speed [m/s] (ay = v^2/R = 3.9)
constexpr float kDt = 0.001f;             // production physics step (1 kHz)

// Circular test oval written to <dir>/ai/fast_lane.ai so
// MultiCarManager/AIController load it exactly like real content.
bool writeOval(const fs::path& dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "ai", ec);
    ks::ai::AiSpline spline;
    for (int i = 0; i < kPoints; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kPoints;
        ks::ai::AiSplinePoint p;
        p.position = {kRadius * std::cos(a), 0.0f, kRadius * std::sin(a)};
        p.curvature = 1.0f / kRadius;
        p.speed = kLineSpeed;
        spline.points.push_back(p);
    }
    return ks::ai::AiFileWriter::writeSpline(
        (dir / "ai" / "fast_lane.ai").string(), spline);
}

float distToLine(const ks::ai::AiSpline& spline, float x, float z)
{
    float best = 1e9f;
    for (const auto& p : spline.points) {
        const float dx = p.position.x - x;
        const float dz = p.position.z - z;
        best = std::min(best, std::sqrt(dx * dx + dz * dz));
    }
    return best;
}

float dist3(const ks::physics::SimulationState& a,
            const ks::physics::SimulationState& b)
{
    const float dx = a.position.x - b.position.x;
    const float dy = a.position.y - b.position.y;
    const float dz = a.position.z - b.position.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Average player-AI speed over `steps` physics ticks (settles transients).
float avgSpeed(MultiCarManager& mc, int carId, int steps)
{
    double sum = 0.0;
    for (int i = 0; i < steps; ++i) {
        mc.update(kDt);
        sum += mc.getCar(carId)->vehicle->getState().speed;
    }
    return static_cast<float>(sum / steps);
}

} // namespace

int main()
{
    const fs::path trackDir = fs::temp_directory_path() / "ks_ai_race_test_track";
    KS_CHECK(writeOval(trackDir));

    ks::ai::AiSpline line = ks::ai::AiFileReader::readSpline(
        (trackDir / "ai" / "fast_lane.ai").string());
    KS_CHECK(line.isValid());
    KS_CHECK(line.closed);
    const float trackLen = line.totalDistance;
    KS_CHECK(trackLen > 250.0f && trackLen < 400.0f);

    // --- Grid spawn (MultiCarManager::spawnGrid) ---
    MultiCarManager mc;
    mc.loadAiSpline(trackDir.string());
    const std::vector<int> ids = mc.spawnGrid(3, "test_car", "AI ", 6.0f);
    KS_CHECK(ids.size() == 3);
    for (int id : ids) {
        CarEntry* e = mc.getCar(id);
        KS_CHECK(e != nullptr && e->ai != nullptr && e->ai->isReady());
        KS_CHECK(e && !e->isPlayer);
        KS_CHECK(e && e->vehicle->isRunning());
    }

    const auto spawn0 = mc.getCar(ids[0])->vehicle->getState();
    const auto spawn1 = mc.getCar(ids[1])->vehicle->getState();
    const auto spawn2 = mc.getCar(ids[2])->vehicle->getState();

    // On/near the racing line, pointing along it (start tangent ~ +z).
    KS_CHECK(distToLine(line, spawn0.position.x, spawn0.position.z) < 4.0f);
    KS_CHECK(distToLine(line, spawn2.position.x, spawn2.position.z) < 4.0f);
    KS_CHECK(std::fabs(spawn0.heading) < 0.2f);
    // Two-wide: pole pair side by side (~3 m), next row 6 m further back.
    {
        const float d01 = dist3(spawn0, spawn1);
        const float d02 = dist3(spawn0, spawn2);
        KS_CHECK(d01 > 2.0f && d01 < 4.0f);
        KS_CHECK(d02 > 4.0f && d02 < 8.0f);
    }

    // No spline loaded -> no grid, no crash.
    {
        MultiCarManager none;
        KS_CHECK(none.spawnGrid(2, "c", "AI ").empty());
    }

    // --- addCar honours startPosition and starts the simulation ---
    {
        MultiCarManager pm;
        const int pid = pm.addCar("test_car", "Player", vec3(1.0f, 0.35f, 2.0f), true);
        CarEntry* pe = pm.getCar(pid);
        KS_CHECK(pe && pe->vehicle->isRunning());
        auto p0 = pe->vehicle->getState();
        KS_CHECK_NEAR(p0.position.x, 1.0f, 1e-4);
        KS_CHECK_NEAR(p0.position.z, 2.0f, 1e-4);
        for (int i = 0; i < 10; ++i) pm.update(kDt);
        auto p1 = pe->vehicle->getState();
        KS_CHECK_NEAR(p1.position.x, 1.0f, 0.01); // no input -> stays put
        KS_CHECK_NEAR(p1.position.z, 2.0f, 0.01);
    }

    // --- AI driving: accelerate, hold the line, complete laps ---
    int lapsSeen = 0;
    for (int id : ids)
        mc.getCar(id)->ai->onLapCompleted = [&](int) { ++lapsSeen; };
    // "Hold the line" is the behaviour under test here; the traffic/overtake
    // pass deliberately steps ~2.75 m off the racing line to go around, which
    // would invalidate the tight 12 m bound below.
    for (int id : ids)
        mc.getCar(id)->ai->setOvertakeEnabled(false);

    for (int i = 0; i < 2000; ++i) mc.update(kDt); // 2 s off the line
    const auto mid = mc.getCar(ids[0])->vehicle->getState();
    KS_CHECK(mid.speed > 3.0f);
    KS_CHECK(dist3(spawn0, mid) > 5.0f);

    float maxOffTrack = 0.0f;
    for (int i = 0; i < 28000; ++i) { // 28 s settle (transient decay)
        mc.update(kDt);
    }
    for (int i = 0; i < 8000; ++i) { // 8 s measured
        mc.update(kDt);
        if (i % 1000 == 0) {
            const auto st = mc.getCar(ids[0])->vehicle->getState();
            maxOffTrack = std::max(
                maxOffTrack, distToLine(line, st.position.x, st.position.z));
        }
    }
    KS_CHECK(maxOffTrack < 12.0f);

    for (int i = 0; i < 25000; ++i) mc.update(kDt); // +25 s -> full lap
    for (int id : ids) {
        CarEntry* e = mc.getCar(id);
        KS_CHECK(e->ai->lapCount() >= 1);
        // Progress is reported for the standings (lap * length + distance).
        KS_CHECK(e->ai->splineLength() > 0.0f);
        KS_CHECK(e->ai->progressDistance() >= 0.0f &&
                 e->ai->progressDistance() <= e->ai->splineLength());
    }
    KS_CHECK(lapsSeen >= 3);

    // --- Flag limiter (setAISpeedFactor) slows the whole field ---
    const float speedFull = avgSpeed(mc, ids[0], 3000); // 3 s at factor 1.0
    KS_CHECK(speedFull > 12.0f);
    mc.setAISpeedFactor(0.6f);
    KS_CHECK_NEAR(mc.getCar(ids[0])->ai->speedFactor(), 0.6f, 1e-4);
    const float speedLimited = avgSpeed(mc, ids[0], 8000); // 8 s
    KS_CHECK(speedLimited < speedFull - 4.0f);

    // --- Hand a car to a remote driver (setCarClientIndex, roadmap 3.1) ---
    {
        MultiCarManager rm;
        rm.loadAiSpline(trackDir.string());
        const std::vector<int> rid = rm.spawnGrid(2, "c", "AI ");
        KS_CHECK(rid.size() == 2);
        if (rid.size() < 2) return KS_TEST_RESULT("ai_race_test");
        const int remoteCar = rid[1];
        CarEntry* rc = rm.getCar(remoteCar);
        KS_CHECK(rc != nullptr && rc->ai != nullptr);
        KS_CHECK(rm.setCarClientIndex(remoteCar, 3));
        KS_CHECK(rc->ai == nullptr);                  // spline no longer drives it
        KS_CHECK(rc->clientIndex == 3);
        KS_CHECK(rm.getCarByClientIndex(3) == rc);
        KS_CHECK(rm.getCarByClientIndex(4) == nullptr);
        KS_CHECK(!rm.setCarClientIndex(-999, 1));     // no such car
        KS_CHECK(!rm.setCarExternallyDriven(-999));
        // update() must not resurrect AI controls for a networked car.
        rc->vehicle->setThrottle(0.9);
        for (int i = 0; i < 60; ++i) rm.update(1.0f / 60.0f);
        KS_CHECK(rc->vehicle->getState().throttle > 0.5);
    }

    // --- RaceSessionManager: standings feed + identity lookup ---
    RaceSessionManager rsm;
    RaceConfig cfg;
    cfg.trackLength = trackLen;
    cfg.numCars = 4;
    rsm.configure(cfg);
    rsm.startSession();
    int posSeen = -1;
    rsm.onPositionChanged = [&](int p) { posSeen = p; };

    rsm.updateCarProgress(1, 1, 100.0f); // AI car 1 completes a lap
    KS_CHECK(posSeen == 2);              // player drops P1 -> P2
    {
        bool leadOk = false;
        for (const auto& s : rsm.standings())
            if (s.carIndex == 1)
                leadOk = (s.position == 1 && s.currentLap == 1);
        KS_CHECK(leadOk);
    }

    // Player telemetry must land on the carIndex-0 standing even though the
    // vector slot 0 now holds the reordered leader (regression for the
    // vector-index player lookup).
    ks::physics::SimulationState pstate{};
    pstate.currentLapDistance = 42.0f;
    rsm.update(pstate, 0.016f);
    for (const auto& s : rsm.standings()) {
        if (s.carIndex == 0) KS_CHECK_NEAR(s.totalDistance, 42.0f, 1e-3);
        if (s.carIndex == 1) KS_CHECK_NEAR(s.totalDistance, 100.0f, 1e-3);
    }

    rsm.updateCarProgress(0, 2, 50.0f); // player takes the lead back
    KS_CHECK(posSeen == 1);

    // Inactive sessions ignore progress updates.
    rsm.endSession();
    rsm.updateCarProgress(0, 5, 9999.0f);
    for (const auto& s : rsm.standings())
        if (s.carIndex == 0) KS_CHECK(s.currentLap != 5);

    return KS_TEST_RESULT("ai_race_test");
}
