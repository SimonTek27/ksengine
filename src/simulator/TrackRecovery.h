#pragma once
/**
 * TrackRecovery — what happens after the yellow.
 *
 * TracksidePosts raises the flag: a car stopped on the racing line is an
 * incident. This is the rest of it. A car that stops OUT on track with a dead
 * engine is stranded — nobody can fire it again out there, because the crew only
 * works in the pit lane (VehicleSimulator::setCrewStarterAvailable) — so the
 * marshals reach it, hook it up and tow it back to its box, where the crew can.
 *
 * The rule, per car:
 *   - moving, engine running, or in the pit lane/box -> not an incident, the
 *     clock is cleared;
 *   - stopped with the engine off, out on track      -> stranded;
 *   - stranded for stopSec                           -> the marshals know
 *                                                       (armed(): the yellow);
 *   - stranded for stopSec + marshalSec              -> towed: update() returns
 *                                                       true once and the caller
 *                                                       moves the car.
 *
 * Only the engine-dead case is ours: braking to a halt, waiting out a red flag
 * and parking on the grid are not recoveries, and a car beached in gravel with
 * the engine still running has nothing to restart yet — the surface logic owns
 * that. Header-only and Qt-free like TracksidePosts.h / GarageExit.h.
 */
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdint>

namespace ks {
namespace sim {

/// What the world tells the rule about one car in one step.
struct StrandedCar {
    float speedMs = 0.f;
    bool engineRunning = true;
    /// Pit lane or garage box: the crew is there, so a dead engine is normal.
    bool inPitLane = false;
};

enum class RecoveryState : uint8_t {
    Clear = 0,  // nothing to do
    Stranded,   // stopped with a dead engine out on track, marshals on the way
    Towed       // reported on the step the tow happens; the caller moved it
};

class TrackRecovery {
public:
    struct Config {
        /// Below this the car counts as stopped. It does not have to be exactly
        /// zero: a car creeping at walking pace is not going anywhere either.
        float stopSpeedMs = 0.5f;
        /// Stopped this long before it counts as stranded, so a spin that ends
        /// and a stall the driver restarts on the button are not recoveries.
        float stopSec = 2.f;
        /// How long the marshals take to reach the car and hook it up.
        float marshalSec = 20.f;
    };

    void setConfig(const Config& c) { m_cfg = c; }
    const Config& config() const { return m_cfg; }

    /**
     * One step.
     * @return true on the step the car is towed — the caller moves it, once.
     */
    bool update(float dt, const StrandedCar& car) {
        if (car.inPitLane || car.engineRunning || car.speedMs >= m_cfg.stopSpeedMs) {
            clear();
            return false;
        }
        m_strandedSec += std::max(0.f, dt);
        m_state = RecoveryState::Stranded;
        if (m_strandedSec < m_cfg.stopSec + m_cfg.marshalSec) return false;
        m_state = RecoveryState::Towed;
        m_strandedSec = 0.f;
        return true;
    }

    RecoveryState state() const { return m_state; }
    /// Seconds the car has been stopped with the engine off.
    float strandedSec() const { return m_strandedSec; }
    /// True once the marshals know about it — the yellow TracksidePosts shows.
    bool armed() const { return m_strandedSec >= m_cfg.stopSec; }
    /// Seconds until the tow at the current rate, or -1 before the car is armed.
    float secondsToTow() const {
        if (!armed()) return -1.f;
        return std::max(0.f, m_cfg.stopSec + m_cfg.marshalSec - m_strandedSec);
    }
    void clear() {
        m_state = RecoveryState::Clear;
        m_strandedSec = 0.f;
    }

private:
    Config m_cfg;
    RecoveryState m_state = RecoveryState::Clear;
    float m_strandedSec = 0.f;
};

/**
 * The one line the HUD shows while a car is stranded: nothing at all until the
 * marshals know, then the countdown to the tow. Returns false (and leaves the
 * buffer empty) when there is nothing to say, so the caller keeps its own
 * status chain instead of printing an empty label.
 */
inline bool recoveryStatusText(const TrackRecovery& rule, char* buf, std::size_t n) {
    if (!buf || n == 0) return false;
    buf[0] = '\0';
    if (rule.state() != RecoveryState::Stranded) return false;
    // Before the marshals know there is no countdown to print; rounding the -1
    // of secondsToTow() would truncate -0.5 to 0 and show a fake "0s".
    if (!rule.armed()) {
        std::snprintf(buf, n, "RECOVERY");
        return true;
    }
    std::snprintf(buf, n, "RECOVERY %ds", static_cast<int>(rule.secondsToTow() + 0.5f));
    return true;
}

} // namespace sim
} // namespace ks
