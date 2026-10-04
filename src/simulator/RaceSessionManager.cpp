#include "RaceSessionManager.h"
#include <algorithm>
#include <cstdio>

namespace ks::sim {

RaceSessionManager::RaceSessionManager() = default;
RaceSessionManager::~RaceSessionManager() = default;

void RaceSessionManager::configure(const RaceConfig& config)
{
    m_config = config;
    m_active = false;
    m_paused = false;
    setFlag(RaceFlag::None);
    m_sessionTime = 0;
    m_remainingTime = 0;
    m_lapStartTime = 0;
    m_countingDown = false;
    m_countdownValue = 0;
    m_countdownTimer = 0;
    m_lastCountdownInt = 0;
    m_currentLap = 0;
    m_timeWarningSent = false;
    m_lastCrossingDistance = 0;
    m_sectorStartTime = 0;
    m_sectorIndex = 0;
    m_crossedLine = false;
    m_timing = LapTiming();
    m_pendingPenalties.clear();
    m_trackLimitsViolations.clear();

    m_standings.clear();
    for (int i = 0; i < config.numCars; ++i) {
        DriverStanding standing;
        standing.carIndex = i;
        standing.carName = "Car " + std::to_string(i + 1);
        standing.gridPosition = i + 1;
        standing.position = i + 1;
        m_standings.push_back(standing);
    }
    if (m_playerCarIndex < 0 || m_playerCarIndex >= static_cast<int>(m_standings.size()))
        m_playerCarIndex = 0;
}

// sortStandings() reorders the vector, so identity lookups (player, target
// car) must go through the carIndex field — indexing m_standings by
// m_playerCarIndex pointed at the wrong driver once the order changed.
DriverStanding* RaceSessionManager::standingFor(int carIndex)
{
    for (auto& s : m_standings)
        if (s.carIndex == carIndex) return &s;
    return nullptr;
}

void RaceSessionManager::updateCarProgress(int carIndex, int currentLap, float totalDistance)
{
    if (!m_active) return;
    DriverStanding* standing = standingFor(carIndex);
    if (!standing) return;
    standing->currentLap = currentLap;
    standing->totalDistance = totalDistance;
    updateStandings();
}

void RaceSessionManager::startSession()
{
    m_active = true;
    m_paused = false;
    setFlag(RaceFlag::Green);
    m_sessionTime = 0;
    m_lapStartTime = 0;
    m_lastCrossingDistance = 0;
    m_sectorStartTime = 0;
    m_sectorIndex = 0;
    m_crossedLine = false;
    m_currentLap = 0;
    m_timeWarningSent = false;
    m_timing = LapTiming();
    m_countingDown = false;
    m_countdownValue = 0;
    m_countdownTimer = 0;
    m_lastCountdownInt = 0;

    const int timeLimit = timeLimitSeconds();
    if (timeLimit > 0) {
        m_remainingTime = static_cast<float>(timeLimit);
    } else if (m_config.totalLaps > 0) {
        m_remainingTime = 1e9f;
    } else {
        m_remainingTime = 0;
    }

    printf("RaceSessionManager: Session started Laps:%d Time:%ds\n",
           m_config.totalLaps, m_config.sessionTimeSeconds);
    if (onSessionStarted) onSessionStarted();
}

void RaceSessionManager::pauseSession()
{
    if (!m_active || m_paused) return;
    m_paused = true;
    printf("RaceSessionManager: Session paused at %fs\n", m_sessionTime);
    if (onSessionPaused) onSessionPaused();
}

void RaceSessionManager::resumeSession()
{
    if (!m_active || !m_paused) return;
    m_paused = false;
    printf("RaceSessionManager: Session resumed\n");
}

void RaceSessionManager::endSession()
{
    if (!m_active) return;

    m_active = false;
    m_paused = false;
    setFlag(RaceFlag::Checkered);
    m_countingDown = false;
    m_countdownValue = 0;
    m_countdownTimer = 0;
    m_lastCountdownInt = 0;

    applyPenalties();
    updateStandings();

    printf("RaceSessionManager: Session ended Lap:%d Time:%fs Best:%fs\n",
           m_currentLap, m_sessionTime, m_timing.bestLapTime);

    for (size_t i = 0; i < m_standings.size(); ++i) {
        const auto& s = m_standings[i];
        printf("  P%d %s Laps:%d Best:%fs %s\n",
               s.position, s.driverName.c_str(), s.currentLap, s.bestLapTime,
               s.disqualified ? "[DSQ]" : "");
    }

    if (onSessionEnded) onSessionEnded();
}

void RaceSessionManager::update(const ks::physics::SimulationState& state, float dt)
{
    if (!m_active || m_paused) return;

    updateCountdown(dt);
    if (m_countingDown) return;

    m_sessionTime += std::max(0.0f, dt);

    const int timeLimit = timeLimitSeconds();
    if (timeLimit > 0) {
        m_remainingTime = timeLimit - m_sessionTime;
        if (!m_timeWarningSent && m_config.sessionTimeWarningSeconds > 0 &&
            m_remainingTime <= m_config.sessionTimeWarningSeconds) {
            m_timeWarningSent = true;
            if (onSessionTimeWarning)
                onSessionTimeWarning(std::max(0.0f, m_remainingTime));
        }
        if (m_remainingTime <= 0) {
            endSession();
            return;
        }
    }

    m_timing.currentLapDistance = state.currentLapDistance;
    m_timing.lapTime = m_sessionTime - m_lapStartTime;

    if (DriverStanding* player = standingFor(m_playerCarIndex)) {
        player->currentLap = m_currentLap;
        player->totalDistance = state.currentLapDistance + m_currentLap * m_config.trackLength;
        player->lastLapTime = m_timing.lastLapTime;
        player->bestLapTime = m_timing.bestLapTime;
        player->totalTime = m_sessionTime;
    }

    checkLapCrossing(state);
}

void RaceSessionManager::checkLapCrossing(const ks::physics::SimulationState& state)
{
    const float lapLength = std::max(1.0f, m_config.trackLength);
    const float currentDist = std::clamp(state.currentLapDistance, 0.0f, lapLength);
    const float finishThreshold = std::min(10.0f, lapLength * 0.02f);
    const float approachThreshold = std::min(50.0f, lapLength * 0.1f);
    const bool crossedFinish = m_crossedLine &&
                               m_lastCrossingDistance >= lapLength * 0.8f &&
                               currentDist < finishThreshold;

    if (crossedFinish) {
        m_crossedLine = false;
        m_currentLap++;

        float lapTime = m_sessionTime - m_lapStartTime;
        m_timing.lastLapTime = lapTime;
        m_timing.lapNumber = m_currentLap;

        if (lapTime < m_timing.bestLapTime && lapTime > 1.0f) {
            m_timing.bestLapTime = lapTime;
        }

        if (DriverStanding* player = standingFor(m_playerCarIndex)) {
            player->currentLap = m_currentLap;
            player->lastLapTime = lapTime;
            player->bestLapTime = m_timing.bestLapTime;
            player->totalTime = m_sessionTime;
        }

        printf("RaceSessionManager: Lap %d completed in %fs (best: %fs)\n",
               m_currentLap, lapTime, m_timing.bestLapTime);

        const float finalSectorTime = m_sessionTime - m_sectorStartTime;
        m_timing.sectorTimes[2] = finalSectorTime;
        for (int sector = 0; sector < 3; ++sector)
            m_timing.lastSectorTimes[sector] = m_timing.sectorTimes[sector];
        if (onSectorCompleted) onSectorCompleted(3, finalSectorTime);
        if (onLapCompleted) onLapCompleted(m_currentLap, lapTime, m_timing.bestLapTime);

        if (m_config.totalLaps > 0 && m_currentLap >= m_config.totalLaps) {
            if (DriverStanding* player = standingFor(m_playerCarIndex)) {
                player->finished = true;
                player->totalDistance = currentDist +
                                        m_currentLap * m_config.trackLength;
            }
            m_lastCrossingDistance = currentDist;
            endSession();
            return;
        }

        m_lapStartTime = m_sessionTime;
        m_sectorStartTime = m_sessionTime;
        m_sectorIndex = 0;
        m_timing.sectorTimes[0] = 0;
        m_timing.sectorTimes[1] = 0;
        m_timing.sectorTimes[2] = 0;
    } else {
        while (m_sectorIndex < 2) {
            const float boundary = lapLength * static_cast<float>(m_sectorIndex + 1) / 3.0f;
            if (m_lastCrossingDistance >= boundary || currentDist < boundary) break;

            const float sectorTime = m_sessionTime - m_sectorStartTime;
            m_timing.sectorTimes[m_sectorIndex] = sectorTime;
            ++m_sectorIndex;
            m_sectorStartTime = m_sessionTime;
            if (onSectorCompleted) onSectorCompleted(m_sectorIndex, sectorTime);
        }
    }

    if (currentDist > approachThreshold) {
        m_crossedLine = true;
    }
    m_lastCrossingDistance = currentDist;
}

void RaceSessionManager::updateStandings()
{
    sortStandings();
    for (size_t i = 0; i < m_standings.size(); ++i) {
        int oldPos = m_standings[i].position;
        m_standings[i].position = static_cast<int>(i + 1);
        if (m_standings[i].carIndex == m_playerCarIndex && oldPos != m_standings[i].position) {
            if (onPositionChanged) onPositionChanged(m_standings[i].position);
        }
    }
}

void RaceSessionManager::sortStandings()
{
    std::sort(m_standings.begin(), m_standings.end(),
              [this](const DriverStanding& a, const DriverStanding& b) {
                  if (a.disqualified != b.disqualified) return !a.disqualified;
                  if (a.finished != b.finished) return a.finished;
                  if (a.currentLap != b.currentLap) return a.currentLap > b.currentLap;
                  if (a.totalDistance != b.totalDistance) return a.totalDistance > b.totalDistance;
                  if (a.totalTime != b.totalTime) return a.totalTime < b.totalTime;
                  if (m_config.useGridPositions && a.gridPosition != b.gridPosition)
                      return a.gridPosition < b.gridPosition;
                  return a.carIndex < b.carIndex;
              });
}

void RaceSessionManager::setGridPosition(int carIndex, int gridPosition)
{
    if (carIndex >= 0 && carIndex < (int)m_standings.size()) {
        m_standings[carIndex].gridPosition = gridPosition;
    }
}

void RaceSessionManager::startCountdown(float countdownSeconds)
{
    m_countingDown = true;
    m_countdownValue = countdownSeconds;
    m_countdownTimer = 0;
    m_lastCountdownInt = static_cast<int>(countdownSeconds) + 1;
    setFlag(RaceFlag::None);
    printf("RaceSessionManager: Starting countdown %fs\n", countdownSeconds);
}

float RaceSessionManager::aiSpeedFactor() const
{
    switch (m_flag) {
        case RaceFlag::Yellow:     return 0.6f;
        case RaceFlag::SafetyCar:  return 0.5f;
        default:                   return 1.0f;
    }
}

void RaceSessionManager::updateCountdown(float dt)
{
    if (!m_countingDown) return;

    m_countdownTimer += dt;
    m_countdownValue = std::max(0.0f, m_countdownValue - dt);

    int currentInt = static_cast<int>(m_countdownValue);
    if (currentInt != m_lastCountdownInt && currentInt >= 0) {
        m_lastCountdownInt = currentInt;
        if (onCountdownTick) onCountdownTick(currentInt);
        printf("RaceSessionManager: Countdown %d\n", currentInt);
    }

    if (m_countdownValue <= 0.0f) {
        m_countingDown = false;
        setFlag(RaceFlag::Green);
        if (onCountdownFinished) onCountdownFinished();
        printf("RaceSessionManager: Countdown finished - GO!\n");
    }
}

void RaceSessionManager::addPenalty(int carIndex, Penalty::Type type, float value, const std::string& reason)
{
    if (carIndex < 0 || carIndex >= (int)m_standings.size()) return;

    Penalty penalty;
    penalty.type = type;
    penalty.targetCarIndex = carIndex;
    penalty.value = value;
    penalty.reason = reason;
    m_pendingPenalties.push_back(penalty);
    if (DriverStanding* standing = standingFor(carIndex))
        standing->penalties.push_back(penalty);

    if (onPenaltyIssued) {
        const char* typeName = "Time Added";
        switch (type) {
            case Penalty::Type::DriveThrough: typeName = "Drive Through"; break;
            case Penalty::Type::StopGo: typeName = "Stop-Go"; break;
            case Penalty::Type::Disqualification: typeName = "Disqualification"; break;
            case Penalty::Type::TimeAdded: break;
        }
        onPenaltyIssued(carIndex, typeName, reason);
    }
}

void RaceSessionManager::servePenalty(int carIndex)
{
    for (auto& p : m_pendingPenalties) {
        const bool pitPenalty = p.type == Penalty::Type::DriveThrough ||
                    p.type == Penalty::Type::StopGo;
        if (p.targetCarIndex == carIndex && !p.served && pitPenalty) {
            p.served = true;
            if (DriverStanding* standing = standingFor(carIndex)) {
                for (auto& standingPenalty : standing->penalties) {
                    if (!standingPenalty.served && standingPenalty.type == p.type &&
                        standingPenalty.value == p.value && standingPenalty.reason == p.reason) {
                        standingPenalty.served = true;
                        break;
                    }
                }
            }
            printf("RaceSessionManager: Penalty served by car %d\n", carIndex);
            break;
        }
    }
}

void RaceSessionManager::reportTrackLimitsViolation(int carIndex)
{
    if (carIndex < 0 || carIndex >= (int)m_standings.size()) return;

    while ((int)m_trackLimitsViolations.size() <= carIndex) {
        m_trackLimitsViolations.push_back(0);
    }
    m_trackLimitsViolations[carIndex]++;

    int violations = m_trackLimitsViolations[carIndex];
    if (onTrackLimitsWarning) onTrackLimitsWarning(carIndex, violations);

    if (violations >= 3) {
        addPenalty(carIndex, Penalty::Type::TimeAdded, 1.0f, "Track limits (3 violations)");
        m_trackLimitsViolations[carIndex] = 0;
    }
}

void RaceSessionManager::applyPenalties()
{
    for (const auto& penalty : m_pendingPenalties) {
        if (penalty.served) continue;
        DriverStanding* standing = standingFor(penalty.targetCarIndex);
        if (!standing) continue;

        switch (penalty.type) {
            case Penalty::Type::TimeAdded:
                standing->totalTime += penalty.value;
                break;
            case Penalty::Type::Disqualification:
                standing->disqualified = true;
                break;
            default:
                break;
        }
    }
}

int RaceSessionManager::timeLimitSeconds() const
{
    if (m_config.sessionTimeSeconds > 0)
        return m_config.sessionTimeSeconds;
    if (m_config.sessionType == RaceConfig::SessionType::Qualifying)
        return std::max(0, m_config.qualifyingTimeSeconds);
    return 0;
}

} // namespace ks::sim
