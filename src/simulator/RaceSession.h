#pragma once
/**
 * Race session state — flags, penalties, phases (parity 2.4 scaffold).
 * Qt-free. Independent of AC branding.
 */
#include "RaceFlag.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ks {
namespace sim {

enum class SessionType : uint8_t {
    Practice = 0,
    Qualify = 1,
    Race = 2,
    Hotlap = 3
};

enum class SessionPhase : uint8_t {
    Idle = 0,
    Countdown = 1,
    Green = 2,
    Yellow = 3,
    SafetyCar = 4,
    Checkered = 5,
    Finished = 6
};

enum class PenaltyType : uint8_t {
    None = 0,
    DriveThrough = 1,
    StopGo = 2,
    TimeSeconds = 3,
    GridDrop = 4,
    Disqualified = 5
};

// Named SessionPenalty: RaceSessionManager.h defines its own `Penalty`
// (Type/targetCarIndex/value shape), and some TUs now pull in both headers
// (GarageSpawn.h -> here, SimulationLoop.h -> RaceSessionManager.h).
struct SessionPenalty {
    PenaltyType type = PenaltyType::None;
    float timeSeconds = 0.f;
    std::string reason;
    bool served = false;
};

struct RaceSessionState {
    SessionType type = SessionType::Race;
    SessionPhase phase = SessionPhase::Idle;
    RaceFlag flag = RaceFlag::None;
    int currentLap = 0;
    int totalLaps = 0;
    double timeRemaining = 0.0; // session clock or countdown
    int position = 1;
    int totalCars = 1;
    bool pitLaneOpen = true;
    bool mandatoryPitDone = false;
    std::vector<SessionPenalty> penalties;
};

class RaceSession {
public:
    RaceSessionState& state() { return m_; }
    const RaceSessionState& state() const { return m_; }

    void configure(SessionType type, int totalLaps, double countdownSec = 5.0) {
        m_ = {};
        m_.type = type;
        m_.totalLaps = totalLaps;
        m_.timeRemaining = countdownSec;
        m_.phase = SessionPhase::Countdown;
        m_.flag = RaceFlag::None;
    }

    void startGreen() {
        m_.phase = SessionPhase::Green;
        m_.flag = RaceFlag::Green;
        m_.timeRemaining = 0.0;
    }

    void setFlag(RaceFlag f) {
        m_.flag = f;
        if (f == RaceFlag::Yellow) m_.phase = SessionPhase::Yellow;
        else if (f == RaceFlag::SafetyCar) m_.phase = SessionPhase::SafetyCar;
        else if (f == RaceFlag::Checkered) m_.phase = SessionPhase::Checkered;
        else if (f == RaceFlag::Green && m_.phase != SessionPhase::Finished)
            m_.phase = SessionPhase::Green;
    }

    void addPenalty(PenaltyType type, float seconds = 0.f, const std::string& reason = {}) {
        SessionPenalty p;
        p.type = type;
        p.timeSeconds = seconds;
        p.reason = reason;
        m_.penalties.push_back(p);
    }

    /** Countdown / checkered from lap count. Returns true if phase changed. */
    bool update(double dt, int completedLaps) {
        const auto prev = m_.phase;
        if (m_.phase == SessionPhase::Countdown) {
            m_.timeRemaining -= dt;
            if (m_.timeRemaining <= 0.0)
                startGreen();
        } else if (m_.phase == SessionPhase::Green || m_.phase == SessionPhase::Yellow) {
            m_.currentLap = completedLaps;
            if (m_.totalLaps > 0 && completedLaps >= m_.totalLaps) {
                m_.phase = SessionPhase::Checkered;
                m_.flag = RaceFlag::Checkered;
            }
        } else if (m_.phase == SessionPhase::Checkered) {
            m_.phase = SessionPhase::Finished;
        }
        return m_.phase != prev;
    }

    bool isDrivingAllowed() const {
        return m_.phase == SessionPhase::Green
            || m_.phase == SessionPhase::Yellow
            || m_.phase == SessionPhase::SafetyCar;
    }

    int pendingPenalties() const {
        int n = 0;
        for (const auto& p : m_.penalties)
            if (!p.served) ++n;
        return n;
    }

private:
    RaceSessionState m_;
};

} // namespace sim
} // namespace ks
