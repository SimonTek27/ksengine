#pragma once
/**
 * Pit lane queue management.
 *
 * - Cars leaving garage join the exit queue (ordered by garage index / request time)
 * - Cars entering pits join the entry queue toward their box
 * - Spacing enforced along pit axis; head of queue may proceed when clear
 * - Integrates with GarageExit pathBlocked / pit limiter
 */
#include "GarageSpawn.h"
#include "GarageExit.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <cstdio>

namespace ks {
namespace sim {

enum class PitQueueRole : uint8_t {
    Leaving = 0,  // box → track
    Entering,     // track → box
    Holding       // stationary in box / service
};

enum class PitQueueStatus : uint8_t {
    Waiting = 0,
    ClearedToMove,
    Moving,
    Done
};

struct PitQueueEntry {
    int carId = -1;
    int raceNumber = 0;
    int garageIndex = 0;
    PitQueueRole role = PitQueueRole::Leaving;
    PitQueueStatus status = PitQueueStatus::Waiting;
    float joinTime = 0.f;       // sim time when joined
    float alongPit = 0.f;       // position on pit axis (m from pit start)
    float speedMs = 0.f;
    float posX = 0.f, posZ = 0.f;
    int priority = 0;           // higher = preferred (player boost)
    bool isPlayer = false;
};

struct PitLaneQueueConfig {
    float minSpacingM = 8.f;          // bumper-to-bumper along pit
    float releaseGapM = 12.f;         // gap required before next leave
    float maxPitSpeedMs = 60.f / 3.6f;
    float pitStartAlong = 0.f;        // axis origin
    float pitEndAlong = 120.f;
    float boxPullInAlong = 5.f;       // offset from box projection onto axis
    bool playerPriority = true;
    int playerPriorityBoost = 10;
    float maxWaitSec = 45.f;          // soft — used for UI urgency
};

/**
 * 1D pit axis: along = (p - origin)·headingDir
 */
struct PitAxis {
    float originX = 0.f, originZ = 0.f;
    float heading = 0.f; // rad

    float along(float x, float z) const {
        const float hx = std::sin(heading);
        const float hz = std::cos(heading);
        return (x - originX) * hx + (z - originZ) * hz;
    }

    void worldFromAlong(float s, float lateral, float& outX, float& outZ) const {
        const float hx = std::sin(heading);
        const float hz = std::cos(heading);
        const float lx = std::sin(heading + 1.5707963f);
        const float lz = std::cos(heading + 1.5707963f);
        outX = originX + hx * s + lx * lateral;
        outZ = originZ + hz * s + lz * lateral;
    }
};

class PitLaneQueue {
public:
    void setConfig(const PitLaneQueueConfig& c) { m_cfg = c; }
    const PitLaneQueueConfig& config() const { return m_cfg; }

    void setAxis(const PitAxis& axis) { m_axis = axis; }
    const PitAxis& axis() const { return m_axis; }

    void setSimTime(float t) { m_simTime = t; }

    const std::vector<PitQueueEntry>& entries() const { return m_entries; }

    /** Join leave queue (from garage). */
    void requestLeave(int carId, int raceNumber, int garageIndex, bool isPlayer,
                      float posX, float posZ) {
        if (find(carId)) return;
        PitQueueEntry e;
        e.carId = carId;
        e.raceNumber = raceNumber;
        e.garageIndex = garageIndex;
        e.role = PitQueueRole::Leaving;
        e.status = PitQueueStatus::Waiting;
        e.joinTime = m_simTime;
        e.posX = posX;
        e.posZ = posZ;
        e.alongPit = m_axis.along(posX, posZ);
        e.isPlayer = isPlayer;
        e.priority = isPlayer && m_cfg.playerPriority ? m_cfg.playerPriorityBoost : 0;
        // earlier garage index slightly higher priority for leave order stability
        e.priority += std::max(0, 50 - garageIndex);
        m_entries.push_back(e);
        sortQueue();
    }

    /** Join entry queue (toward garage). */
    void requestEnter(int carId, int raceNumber, int garageIndex, bool isPlayer,
                      float posX, float posZ) {
        if (auto* existing = find(carId)) {
            existing->role = PitQueueRole::Entering;
            existing->garageIndex = garageIndex;
            existing->status = PitQueueStatus::Waiting;
            return;
        }
        PitQueueEntry e;
        e.carId = carId;
        e.raceNumber = raceNumber;
        e.garageIndex = garageIndex;
        e.role = PitQueueRole::Entering;
        e.status = PitQueueStatus::Waiting;
        e.joinTime = m_simTime;
        e.posX = posX;
        e.posZ = posZ;
        e.alongPit = m_axis.along(posX, posZ);
        e.isPlayer = isPlayer;
        e.priority = isPlayer && m_cfg.playerPriority ? m_cfg.playerPriorityBoost : 0;
        m_entries.push_back(e);
        sortQueue();
    }

    void leaveQueue(int carId) {
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
            [carId](const PitQueueEntry& e) { return e.carId == carId; }),
            m_entries.end());
    }

    void updateCar(int carId, float posX, float posZ, float speedMs) {
        auto* e = find(carId);
        if (!e) return;
        e->posX = posX;
        e->posZ = posZ;
        e->speedMs = speedMs;
        e->alongPit = m_axis.along(posX, posZ);
    }

    /**
 * Per-frame: recompute who is ClearedToMove vs Waiting based on spacing.
 * Leaving cars ordered by priority then joinTime; entering by alongPit toward box.
 */
    void update(float dt) {
        (void)dt;
        sortQueue();

        // Reset moving clear flags for waiting cars — recompute
        for (auto& e : m_entries) {
            if (e.status == PitQueueStatus::Done) continue;
            if (e.status == PitQueueStatus::Moving) {
                // keep moving until caller marks Done
                continue;
            }
            e.status = PitQueueStatus::Waiting;
        }

        // --- Leaving: only one (or spacing-separated) may enter pit axis from boxes ---
        float lastReleasedAlong = -1e9f;
        for (auto& e : m_entries) {
            if (e.role != PitQueueRole::Leaving) continue;
            if (e.status == PitQueueStatus::Done) continue;

            if (e.status == PitQueueStatus::Moving) {
                lastReleasedAlong = std::max(lastReleasedAlong, e.alongPit);
                continue;
            }

            // Can clear if the car released before this one is at least
            // releaseGapM ahead of it (a car still on the box has nobody in
            // front: lastReleasedAlong is still the -1e9 sentinel).
            //
            // The comparison used to run the other way round
            // (e.alongPit - lastReleasedAlong), which blocked the second car
            // forever: two cars leaving adjacent boxes share the same
            // alongPit, and a leader moving away down the lane only grew that
            // difference in the wrong direction.
            const bool gapOk = lastReleasedAlong < -1e8f ||
                               (lastReleasedAlong - e.alongPit) >= m_cfg.releaseGapM;
            // Also check no entering car immediately ahead on axis
            bool conflict = false;
            for (const auto& o : m_entries) {
                if (o.carId == e.carId) continue;
                if (o.role == PitQueueRole::Holding) continue;
                const float d = o.alongPit - e.alongPit;
                // someone ahead within min spacing moving opposite or same
                if (d > 0.f && d < m_cfg.minSpacingM)
                    conflict = true;
            }

            if (gapOk && !conflict) {
                e.status = PitQueueStatus::ClearedToMove;
                lastReleasedAlong = e.alongPit;
            } else {
                e.status = PitQueueStatus::Waiting;
            }
        }

        // --- Entering: FIFO by distance to assigned box projection ---
        // Allow move if no one within minSpacing toward the box direction
        for (auto& e : m_entries) {
            if (e.role != PitQueueRole::Entering) continue;
            if (e.status == PitQueueStatus::Done || e.status == PitQueueStatus::Moving)
                continue;

            bool blocked = false;
            for (const auto& o : m_entries) {
                if (o.carId == e.carId) continue;
                const float d = std::fabs(o.alongPit - e.alongPit);
                if (d < m_cfg.minSpacingM && o.status != PitQueueStatus::Done)
                    blocked = true;
            }
            e.status = blocked ? PitQueueStatus::Waiting : PitQueueStatus::ClearedToMove;
        }
    }

    void markMoving(int carId) {
        if (auto* e = find(carId)) e->status = PitQueueStatus::Moving;
    }

    void markDone(int carId) {
        if (auto* e = find(carId)) e->status = PitQueueStatus::Done;
        leaveQueue(carId);
    }

    bool isCleared(int carId) const {
        const auto* e = findConst(carId);
        return e && (e->status == PitQueueStatus::ClearedToMove ||
                     e->status == PitQueueStatus::Moving);
    }

    bool isWaiting(int carId) const {
        const auto* e = findConst(carId);
        return e && e->status == PitQueueStatus::Waiting;
    }

    /** Position in leave queue (0 = next to go), or -1 if not leaving. */
    int leaveQueuePosition(int carId) const {
        int pos = 0;
        for (const auto& e : m_entries) {
            if (e.role != PitQueueRole::Leaving) continue;
            if (e.status == PitQueueStatus::Done) continue;
            if (e.carId == carId) return pos;
            ++pos;
        }
        return -1;
    }

    int leaveQueueLength() const {
        int n = 0;
        for (const auto& e : m_entries)
            if (e.role == PitQueueRole::Leaving && e.status != PitQueueStatus::Done)
                ++n;
        return n;
    }

    /**
 * Feed GarageExitInput.pathBlocked for a car leaving its box.
 * True if this car is not cleared OR another car is in the exit cone region.
 */
    bool shouldBlockGarageExit(int carId) const {
        const auto* e = findConst(carId);
        if (!e) return false; // not in queue → don't block via queue
        if (e->role != PitQueueRole::Leaving) return false;
        if (e->status == PitQueueStatus::ClearedToMove || e->status == PitQueueStatus::Moving)
            return false;
        return true; // Waiting → path blocked for garage exit
    }

    /** Suggested max speed for car in queue (head can use full pit limit). */
    float suggestedMaxSpeedMs(int carId) const {
        const auto* e = findConst(carId);
        if (!e) return m_cfg.maxPitSpeedMs;
        if (e->status == PitQueueStatus::Waiting)
            return 0.f; // hold
        // gap to car ahead
        float ahead = 1e9f;
        for (const auto& o : m_entries) {
            if (o.carId == carId) continue;
            if (o.role == PitQueueRole::Holding) continue;
            const float d = o.alongPit - e->alongPit;
            if (d > 0.5f && d < ahead) ahead = d;
        }
        if (ahead < m_cfg.minSpacingM * 1.5f)
            return m_cfg.maxPitSpeedMs * 0.35f;
        return m_cfg.maxPitSpeedMs;
    }

    std::string debugStatus(int carId) const {
        const auto* e = findConst(carId);
        if (!e) return "NOT IN QUEUE";
        char buf[96];
        const char* st =
            e->status == PitQueueStatus::Waiting ? "WAIT" :
            e->status == PitQueueStatus::ClearedToMove ? "CLEAR" :
            e->status == PitQueueStatus::Moving ? "MOVE" : "DONE";
        const char* role =
            e->role == PitQueueRole::Leaving ? "LEAVE" :
            e->role == PitQueueRole::Entering ? "ENTER" : "HOLD";
        std::snprintf(buf, sizeof(buf), "%s %s q=%d", role, st, leaveQueuePosition(carId));
        return buf;
    }

private:
    PitQueueEntry* find(int carId) {
        for (auto& e : m_entries)
            if (e.carId == carId) return &e;
        return nullptr;
    }
    const PitQueueEntry* findConst(int carId) const {
        for (const auto& e : m_entries)
            if (e.carId == carId) return &e;
        return nullptr;
    }

    void sortQueue() {
        std::stable_sort(m_entries.begin(), m_entries.end(),
            [](const PitQueueEntry& a, const PitQueueEntry& b) {
                if (a.role != b.role) return static_cast<int>(a.role) < static_cast<int>(b.role);
                if (a.role == PitQueueRole::Leaving) {
                    if (a.priority != b.priority) return a.priority > b.priority;
                    return a.joinTime < b.joinTime;
                }
                // Entering: closer to pit start first or by along
                return a.alongPit < b.alongPit;
            });
    }

    PitLaneQueueConfig m_cfg;
    PitAxis m_axis;
    std::vector<PitQueueEntry> m_entries;
    float m_simTime = 0.f;
};

/**
 * Helper: wire queue + garage exit for one car this frame.
 */
inline void integratePitQueueWithGarageExit(
    PitLaneQueue& queue,
    GarageExitController& exitCtrl,
    int carId,
    GarageExitInput& in,
    float& throttle)
{
    if (exitCtrl.phase() == GarageExitPhase::Preparing ||
        exitCtrl.phase() == GarageExitPhase::BoxClear ||
        exitCtrl.phase() == GarageExitPhase::EngineStart) {
        // ensure registered as leaving
        // (caller should requestLeave once; path block from queue)
        if (queue.shouldBlockGarageExit(carId))
            in.pathBlocked = true;
    }

    if (exitCtrl.phase() == GarageExitPhase::RollingOut ||
        exitCtrl.phase() == GarageExitPhase::PitLane) {
        queue.markMoving(carId);
        const float vmax = queue.suggestedMaxSpeedMs(carId);
        throttle = GarageExitController::applyPitLimiter(in.speedMs, throttle, vmax);
    }

    if (exitCtrl.phase() == GarageExitPhase::OnTrack)
        queue.markDone(carId);
}

} // namespace sim
} // namespace ks
