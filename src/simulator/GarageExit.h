#pragma once
/**
 * Garage exit logic (rF2-like practice/qualify).
 *
 * Flow:
 *   InGarage → (request) → Preparing → EngineStart → BoxClear →
 *   RollingOut (pit limiter) → PitLane → TrackEntry → OnTrack
 *
 * Blocking reasons: box occupied ahead, pit closed, engine off, session lock.
 */
#include "GarageSpawn.h"
#include "RaceSession.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <functional>
#include <algorithm>

namespace ks {
namespace sim {

enum class GarageExitPhase : uint8_t {
    InGarage = 0,    // parked, optional engine off
    Preparing,       // player requested leave — checks running
    EngineStart,     // cranking / idle
    BoxClear,        // wait until exit path free
    RollingOut,      // moving out of box toward pit lane
    PitLane,         // on pit lane, limiter active
    TrackEntry,      // blending to track (pit exit blend)
    OnTrack,         // free
    Returning,       // coming back to box
    Blocked          // cannot proceed (see blockReason)
};

enum class GarageBlockReason : uint8_t {
    None = 0,
    SessionLocked,     // race grid / countdown forbids leave
    PitClosed,
    EngineOff,
    PathOccupied,      // another car in exit cone
    DoorClosed,        // optional animated door
    SpeedTooHigh,      // still in box but already moving too fast (abort)
    UserCancel
};

struct GarageExitConfig {
    float prepareTimeoutSec = 8.f;     // max wait in Preparing/BoxClear
    float engineStartSec = 1.2f;       // min time in EngineStart
    float rollOutDistanceM = 12.f;     // distance from box center to count as out
    float pitLimiterKmh = 60.f;        // max speed while RollingOut + PitLane
    float pitLaneLengthM = 80.f;       // along pit before TrackEntry
    float trackEntryBlendM = 25.f;     // distance of TrackEntry phase
    float exitConeHalfWidthM = 2.5f;   // path occupancy half-width
    float exitConeLengthM = 15.f;      // look-ahead from box
    bool requireEngineOn = true;
    bool autoStartEngineOnRequest = true;
    bool holdCarWhileInGarage = true;  // zero controls / freeze pose until RollingOut
    bool allowExitInRace = false;      // if true, only when pitLaneOpen
};

struct GarageExitInput {
    bool requestLeave = false;   // player / AI wants to leave
    bool requestCancel = false;
    bool requestReturn = false;  // go back to garage
    bool engineRunning = false;
    bool ignitionOn = false;
    float speedMs = 0.f;
    float throttle = 0.f;
    float brake = 0.f;
    float steer = 0.f;
    float posX = 0.f, posY = 0.f, posZ = 0.f;
    float heading = 0.f;
    /** True if another vehicle intersects exit cone this frame. */
    bool pathBlocked = false;
    bool pitLaneOpen = true;
    SessionType session = SessionType::Practice;
    SessionPhase sessionPhase = SessionPhase::Green;
};

struct GarageExitOutput {
    GarageExitPhase phase = GarageExitPhase::InGarage;
    GarageBlockReason blockReason = GarageBlockReason::None;
    bool holdControls = false;       // ignore driver throttle in box
    bool pitLimiterActive = false;
    float pitLimiterMaxMs = 0.f;
    bool engineShouldRun = false;
    bool allowDrive = false;         // physics may integrate motion
    /** Suggested pose snap while held in garage (optional). */
    bool snapToBox = false;
    WorldPose boxPose{};
    std::string statusText;          // UI: "WAITING PATH", "PIT LIMIT 60", …
};

class GarageExitController {
public:
    void setConfig(const GarageExitConfig& c) { m_cfg = c; }
    const GarageExitConfig& config() const { return m_cfg; }

    void bindBox(int garageIndex, const WorldPose& boxPose, float pitHeading = 0.f) {
        m_garageIndex = garageIndex;
        m_boxPose = boxPose;
        m_pitHeading = pitHeading != 0.f ? pitHeading : boxPose.heading;
        m_phase = GarageExitPhase::InGarage;
        m_block = GarageBlockReason::None;
        m_timer = 0.f;
        m_distAlongExit = 0.f;
        m_lastX = boxPose.x;
        m_lastZ = boxPose.z;
    }

    GarageExitPhase phase() const { return m_phase; }
    GarageBlockReason blockReason() const { return m_block; }
    int garageIndex() const { return m_garageIndex; }

    void forceOnTrack() {
        m_phase = GarageExitPhase::OnTrack;
        m_block = GarageBlockReason::None;
        m_timer = 0.f;
    }

    void forceEnterGarage() {
        m_phase = GarageExitPhase::InGarage;
        m_block = GarageBlockReason::None;
        m_timer = 0.f;
        m_distAlongExit = 0.f;
    }

    /**
 * Tick state machine. Call once per sim frame after reading vehicle state.
 */
    GarageExitOutput update(float dt, const GarageExitInput& in) {
        m_timer += dt;
        accumulateDistance(in);

        GarageExitOutput out;
        out.boxPose = m_boxPose;
        out.phase = m_phase;

        // Cancel
        if (in.requestCancel && m_phase != GarageExitPhase::OnTrack &&
            m_phase != GarageExitPhase::TrackEntry) {
            m_phase = GarageExitPhase::InGarage;
            m_block = GarageBlockReason::UserCancel;
            m_timer = 0.f;
            m_distAlongExit = 0.f;
        }

        // Return request from track
        if (in.requestReturn && m_phase == GarageExitPhase::OnTrack) {
            m_phase = GarageExitPhase::Returning;
            m_timer = 0.f;
        }

        switch (m_phase) {
        case GarageExitPhase::InGarage:
            tickInGarage(in, out);
            break;
        case GarageExitPhase::Preparing:
            tickPreparing(in, out);
            break;
        case GarageExitPhase::EngineStart:
            tickEngineStart(in, out);
            break;
        case GarageExitPhase::BoxClear:
            tickBoxClear(in, out);
            break;
        case GarageExitPhase::RollingOut:
            tickRollingOut(in, out);
            break;
        case GarageExitPhase::PitLane:
            tickPitLane(in, out);
            break;
        case GarageExitPhase::TrackEntry:
            tickTrackEntry(in, out);
            break;
        case GarageExitPhase::OnTrack:
            tickOnTrack(in, out);
            break;
        case GarageExitPhase::Returning:
            tickReturning(in, out);
            break;
        case GarageExitPhase::Blocked:
            tickBlocked(in, out);
            break;
        }

        out.phase = m_phase;
        out.blockReason = m_block;
        if (out.statusText.empty())
            out.statusText = phaseLabel(m_phase, m_block);
        return out;
    }

    /** The line the HUD shows for a state.
     *
     *  A closed lane outranks the parked label: "IN GARAGE" tells the driver
     *  where the car is, "PIT CLOSED" tells him why LEAVE GARAGE would be
     *  refused, and he should learn that before pressing the key, not after.
     *  Any other phase keeps its own label — a car already rolling down the
     *  lane is not "closed", it is on it. */
    static std::string statusLabel(GarageExitPhase phase, bool pitLaneOpen,
                                   const std::string& label) {
        if (!pitLaneOpen && phase == GarageExitPhase::InGarage) return "PIT CLOSED";
        return label;
    }

    static const char* phaseLabel(GarageExitPhase p, GarageBlockReason b = GarageBlockReason::None) {
        if (p == GarageExitPhase::Blocked) {
            switch (b) {
            case GarageBlockReason::SessionLocked: return "SESSION LOCK";
            case GarageBlockReason::PitClosed: return "PIT CLOSED";
            case GarageBlockReason::EngineOff: return "ENGINE OFF";
            case GarageBlockReason::PathOccupied: return "PATH BLOCKED";
            case GarageBlockReason::DoorClosed: return "DOOR CLOSED";
            case GarageBlockReason::UserCancel: return "CANCELLED";
            default: return "BLOCKED";
            }
        }
        switch (p) {
        case GarageExitPhase::InGarage: return "IN GARAGE";
        case GarageExitPhase::Preparing: return "PREPARING";
        case GarageExitPhase::EngineStart: return "ENGINE START";
        case GarageExitPhase::BoxClear: return "WAITING CLEAR";
        case GarageExitPhase::RollingOut: return "ROLLING OUT";
        case GarageExitPhase::PitLane: return "PIT LANE";
        case GarageExitPhase::TrackEntry: return "PIT EXIT";
        case GarageExitPhase::OnTrack: return "ON TRACK";
        case GarageExitPhase::Returning: return "RETURNING";
        default: return "";
        }
    }

    /** Clamp speed for pit limiter (m/s). */
    static float applyPitLimiter(float speedMs, float throttle, float maxMs) {
        if (maxMs <= 0.f) return throttle;
        if (speedMs > maxMs * 0.98f)
            return std::min(throttle, 0.05f); // cut throttle near limit
        if (speedMs > maxMs * 0.85f)
            return std::min(throttle, 0.35f);
        return throttle;
    }

    std::function<void(GarageExitPhase from, GarageExitPhase to)> onPhaseChanged;

private:
    void setPhase(GarageExitPhase next) {
        if (next == m_phase) return;
        const auto prev = m_phase;
        m_phase = next;
        m_timer = 0.f;
        m_block = GarageBlockReason::None;
        if (onPhaseChanged) onPhaseChanged(prev, next);
    }

    void accumulateDistance(const GarageExitInput& in) {
        const float dx = in.posX - m_lastX;
        const float dz = in.posZ - m_lastZ;
        const float ds = std::sqrt(dx * dx + dz * dz);
        m_lastX = in.posX;
        m_lastZ = in.posZ;
        if (m_phase == GarageExitPhase::RollingOut || m_phase == GarageExitPhase::PitLane ||
            m_phase == GarageExitPhase::TrackEntry) {
            // progress along pit heading
            const float hx = std::sin(m_pitHeading);
            const float hz = std::cos(m_pitHeading);
            const float along = dx * hx + dz * hz;
            if (along > 0.f) m_distAlongExit += along;
        }
        (void)ds;
    }

    bool sessionAllowsLeave(const GarageExitInput& in) const {
        if (in.session == SessionType::Practice || in.session == SessionType::Qualify ||
            in.session == SessionType::Hotlap)
            return true;
        if (in.session == SessionType::Race) {
            if (!m_cfg.allowExitInRace) return false;
            return in.pitLaneOpen && in.sessionPhase != SessionPhase::Countdown;
        }
        return true;
    }

    float distFromBox(const GarageExitInput& in) const {
        const float dx = in.posX - m_boxPose.x;
        const float dz = in.posZ - m_boxPose.z;
        return std::sqrt(dx * dx + dz * dz);
    }

    void tickInGarage(const GarageExitInput& in, GarageExitOutput& out) {
        out.holdControls = m_cfg.holdCarWhileInGarage;
        out.snapToBox = m_cfg.holdCarWhileInGarage;
        out.allowDrive = !m_cfg.holdCarWhileInGarage;
        out.engineShouldRun = in.engineRunning;
        out.pitLimiterActive = false;

        if (in.requestLeave) {
            if (!sessionAllowsLeave(in)) {
                setPhase(GarageExitPhase::Blocked);
                m_block = in.pitLaneOpen ? GarageBlockReason::SessionLocked
                                         : GarageBlockReason::PitClosed;
                return;
            }
            setPhase(GarageExitPhase::Preparing);
        }
    }

    void tickPreparing(const GarageExitInput& in, GarageExitOutput& out) {
        out.holdControls = true;
        out.snapToBox = true;
        out.allowDrive = false;
        out.statusText = "PREPARING EXIT";

        if (!sessionAllowsLeave(in)) {
            setPhase(GarageExitPhase::Blocked);
            m_block = GarageBlockReason::SessionLocked;
            return;
        }
        if (!in.pitLaneOpen) {
            setPhase(GarageExitPhase::Blocked);
            m_block = GarageBlockReason::PitClosed;
            return;
        }

        if (m_cfg.requireEngineOn) {
            if (!in.engineRunning) {
                if (m_cfg.autoStartEngineOnRequest) {
                    out.engineShouldRun = true;
                    setPhase(GarageExitPhase::EngineStart);
                    return;
                }
                out.engineShouldRun = false;
                out.statusText = "START ENGINE";
                if (m_timer > m_cfg.prepareTimeoutSec) {
                    setPhase(GarageExitPhase::Blocked);
                    m_block = GarageBlockReason::EngineOff;
                }
                return;
            }
        }

        out.engineShouldRun = true;
        setPhase(GarageExitPhase::BoxClear);
    }

    void tickEngineStart(const GarageExitInput& in, GarageExitOutput& out) {
        out.holdControls = true;
        out.snapToBox = true;
        out.allowDrive = false;
        out.engineShouldRun = true;
        out.statusText = "ENGINE START";

        if (in.engineRunning && m_timer >= m_cfg.engineStartSec)
            setPhase(GarageExitPhase::BoxClear);
        else if (m_timer > m_cfg.prepareTimeoutSec + m_cfg.engineStartSec) {
            setPhase(GarageExitPhase::Blocked);
            m_block = GarageBlockReason::EngineOff;
        }
    }

    void tickBoxClear(const GarageExitInput& in, GarageExitOutput& out) {
        out.holdControls = true;
        out.snapToBox = true;
        out.allowDrive = false;
        out.engineShouldRun = true;

        if (in.pathBlocked) {
            out.statusText = "WAITING — PATH BUSY";
            m_block = GarageBlockReason::PathOccupied;
            // stay in BoxClear, don't go Blocked unless timeout
            if (m_timer > m_cfg.prepareTimeoutSec) {
                setPhase(GarageExitPhase::Blocked);
                m_block = GarageBlockReason::PathOccupied;
            }
            return;
        }

        m_block = GarageBlockReason::None;
        out.statusText = "CLEAR — GO";
        // brief settle then roll
        if (m_timer > 0.35f) {
            m_distAlongExit = 0.f;
            setPhase(GarageExitPhase::RollingOut);
        }
    }

    void tickRollingOut(const GarageExitInput& in, GarageExitOutput& out) {
        out.holdControls = false;
        out.snapToBox = false;
        out.allowDrive = true;
        out.engineShouldRun = true;
        out.pitLimiterActive = true;
        out.pitLimiterMaxMs = m_cfg.pitLimiterKmh / 3.6f;
        out.statusText = "PIT LIMIT";

        if (in.pathBlocked && distFromBox(in) < m_cfg.rollOutDistanceM * 0.5f) {
            // someone entered cone while leaving — soft hold
            out.holdControls = true;
            out.statusText = "YIELD";
            return;
        }

        if (distFromBox(in) >= m_cfg.rollOutDistanceM || m_distAlongExit >= m_cfg.rollOutDistanceM)
            setPhase(GarageExitPhase::PitLane);
    }

    void tickPitLane(const GarageExitInput& in, GarageExitOutput& out) {
        out.holdControls = false;
        out.allowDrive = true;
        out.engineShouldRun = true;
        out.pitLimiterActive = true;
        out.pitLimiterMaxMs = m_cfg.pitLimiterKmh / 3.6f;
        out.statusText = "PIT LANE";

        if (m_distAlongExit >= m_cfg.rollOutDistanceM + m_cfg.pitLaneLengthM)
            setPhase(GarageExitPhase::TrackEntry);
        // also allow speed+distance heuristic if car took a shortcut
        if (distFromBox(in) > m_cfg.rollOutDistanceM + m_cfg.pitLaneLengthM * 0.6f &&
            in.speedMs > 5.f)
            setPhase(GarageExitPhase::TrackEntry);
    }

    void tickTrackEntry(const GarageExitInput& in, GarageExitOutput& out) {
        out.allowDrive = true;
        out.engineShouldRun = true;
        // limiter fades off
        const float t = std::clamp(m_timer / 2.5f, 0.f, 1.f);
        out.pitLimiterActive = t < 0.85f;
        out.pitLimiterMaxMs = (m_cfg.pitLimiterKmh / 3.6f) * (1.f + t * 2.f);
        out.statusText = "PIT EXIT";

        if (m_timer > 2.5f || m_distAlongExit > m_cfg.rollOutDistanceM + m_cfg.pitLaneLengthM +
                                                    m_cfg.trackEntryBlendM)
            setPhase(GarageExitPhase::OnTrack);
    }

    void tickOnTrack(const GarageExitInput& /*in*/, GarageExitOutput& out) {
        out.allowDrive = true;
        out.engineShouldRun = true;
        out.pitLimiterActive = false;
        out.holdControls = false;
    }

    void tickReturning(const GarageExitInput& in, GarageExitOutput& out) {
        out.allowDrive = true;
        out.pitLimiterActive = true;
        out.pitLimiterMaxMs = m_cfg.pitLimiterKmh / 3.6f;
        out.engineShouldRun = true;
        out.statusText = "TO GARAGE";

        if (distFromBox(in) < 3.f && in.speedMs < 1.5f) {
            setPhase(GarageExitPhase::InGarage);
            m_distAlongExit = 0.f;
        }
    }

    void tickBlocked(const GarageExitInput& in, GarageExitOutput& out) {
        out.holdControls = true;
        out.snapToBox = (m_block != GarageBlockReason::PathOccupied);
        out.allowDrive = false;
        out.engineShouldRun = in.engineRunning;

        // Recovery: clear condition → Preparing again if still requesting
        if (in.requestLeave && sessionAllowsLeave(in) && in.pitLaneOpen) {
            if (m_block == GarageBlockReason::PathOccupied && !in.pathBlocked) {
                setPhase(GarageExitPhase::BoxClear);
                return;
            }
            if (m_block == GarageBlockReason::EngineOff && in.engineRunning) {
                setPhase(GarageExitPhase::BoxClear);
                return;
            }
            if (m_block == GarageBlockReason::PitClosed && in.pitLaneOpen) {
                setPhase(GarageExitPhase::Preparing);
                return;
            }
            if (m_block == GarageBlockReason::UserCancel || m_block == GarageBlockReason::SessionLocked) {
                if (sessionAllowsLeave(in))
                    setPhase(GarageExitPhase::Preparing);
            }
        }
        if (!in.requestLeave && m_block == GarageBlockReason::UserCancel)
            setPhase(GarageExitPhase::InGarage);
    }

    GarageExitConfig m_cfg;
    GarageExitPhase m_phase = GarageExitPhase::InGarage;
    GarageBlockReason m_block = GarageBlockReason::None;
    int m_garageIndex = 0;
    WorldPose m_boxPose{};
    float m_pitHeading = 0.f;
    float m_timer = 0.f;
    float m_distAlongExit = 0.f;
    float m_lastX = 0.f, m_lastZ = 0.f;
};

/**
 * Simple exit-cone occupancy test (XZ plane).
 * @param others positions of other cars
 */
inline bool isExitPathBlocked(
    const WorldPose& box,
    float pitHeading,
    float coneLen,
    float coneHalfWidth,
    const float* otherX, const float* otherZ, int count,
    float selfX, float selfZ) {

    const float hx = std::sin(pitHeading);
    const float hz = std::cos(pitHeading);
    const float lx = std::sin(pitHeading + 1.5707963f);
    const float lz = std::cos(pitHeading + 1.5707963f);

    for (int i = 0; i < count; ++i) {
        const float dx = otherX[i] - box.x;
        const float dz = otherZ[i] - box.z;
        // ignore self
        const float dsx = otherX[i] - selfX;
        const float dsz = otherZ[i] - selfZ;
        if (dsx * dsx + dsz * dsz < 0.25f) continue;

        const float along = dx * hx + dz * hz;
        const float lat = dx * lx + dz * lz;
        if (along > 0.5f && along < coneLen && std::fabs(lat) < coneHalfWidth)
            return true;
    }
    return false;
}

} // namespace sim
} // namespace ks
