#pragma once
/**
 * SessionFlow — Sprint 3 / P1.7
 * Qualifying → GridPublished → Countdown → Race → Finished
 */
#include "SessionController.h"
#include "RaceSessionManager.h"
#include <vector>
#include <algorithm>
#include <cstdio>
#include <string>
#include <cmath>

namespace ks {
namespace sim {

enum class SessionFlowPhase : uint8_t {
    Idle = 0,
    Qualifying,
    GridPublished,
    Countdown,
    Race,
    Finished
};

inline const char* sessionFlowPhaseName(SessionFlowPhase p) {
    switch (p) {
    case SessionFlowPhase::Idle: return "Idle";
    case SessionFlowPhase::Qualifying: return "Qualifying";
    case SessionFlowPhase::GridPublished: return "Grid";
    case SessionFlowPhase::Countdown: return "Countdown";
    case SessionFlowPhase::Race: return "Race";
    case SessionFlowPhase::Finished: return "Finished";
    default: return "?";
    }
}

struct QualyEntry {
    int carId = 0;
    std::string name;
    float bestLap = 1e9f;
    int gridPos = 0;
};

struct RaceResultEntry {
    int carId = 0;
    std::string name;
    int position = 0;
    int laps = 0;
    float totalTime = 0.f;
    float bestLap = 1e9f;
    bool finished = false;
    bool dnf = false;
};

class SessionFlow {
public:
    void reset() {
        m_phase = SessionFlowPhase::Idle;
        m_qualy.clear();
        m_results.clear();
        m_countdown = 0.f;
        m_phaseTime = 0.f;
    }

    void startQualifying(float durationSec, const std::vector<QualyEntry>& cars) {
        m_phase = SessionFlowPhase::Qualifying;
        m_phaseTime = 0.f;
        m_qualyDuration = std::max(30.f, durationSec);
        m_qualy = cars;
        for (auto& e : m_qualy) { e.bestLap = 1e9f; e.gridPos = 0; }
        std::fprintf(stderr, "SessionFlow: QUALIFYING %.0fs, %zu cars\n",
                     m_qualyDuration, m_qualy.size());
    }

    void startRaceDirect(int totalLaps, float countdownSec = 5.f) {
        m_totalLaps = std::max(1, totalLaps);
        m_phase = SessionFlowPhase::Countdown;
        m_countdown = std::max(1.f, countdownSec);
        m_phaseTime = 0.f;
        std::fprintf(stderr, "SessionFlow: COUNTDOWN %.1fs → race %d laps\n",
                     m_countdown, m_totalLaps);
    }

    void reportQualyLap(int carId, float lapTime) {
        if (m_phase != SessionFlowPhase::Qualifying) return;
        if (!(lapTime > 1.f) || !std::isfinite(lapTime)) return;
        for (auto& e : m_qualy) {
            if (e.carId == carId && lapTime < e.bestLap) {
                e.bestLap = lapTime;
                std::fprintf(stderr, "SessionFlow: qualy car %d best %.3f\n", carId, lapTime);
            }
        }
    }

    void tick(float dt) {
        if (m_phase == SessionFlowPhase::Idle || m_phase == SessionFlowPhase::Finished)
            return;
        m_phaseTime += dt;
        if (m_phase == SessionFlowPhase::Qualifying) {
            if (m_phaseTime >= m_qualyDuration)
                publishGrid();
        } else if (m_phase == SessionFlowPhase::GridPublished) {
            if (m_phaseTime >= 2.f)
                enterCountdown(5.f);
        } else if (m_phase == SessionFlowPhase::Countdown) {
            m_countdown -= dt;
            if (m_countdown <= 0.f) {
                m_countdown = 0.f;
                m_phase = SessionFlowPhase::Race;
                m_phaseTime = 0.f;
                std::fprintf(stderr, "SessionFlow: GREEN — race start\n");
            }
        }
    }

    void publishGrid() {
        std::sort(m_qualy.begin(), m_qualy.end(), [](const QualyEntry& a, const QualyEntry& b) {
            const bool aOk = a.bestLap < 1e8f;
            const bool bOk = b.bestLap < 1e8f;
            if (aOk != bOk) return aOk;
            return a.bestLap < b.bestLap;
        });
        for (size_t i = 0; i < m_qualy.size(); ++i)
            m_qualy[i].gridPos = static_cast<int>(i) + 1;
        m_phase = SessionFlowPhase::GridPublished;
        m_phaseTime = 0.f;
        std::fprintf(stderr, "SessionFlow: GRID published (%zu)\n", m_qualy.size());
    }

    void enterCountdown(float sec) {
        m_phase = SessionFlowPhase::Countdown;
        m_countdown = std::max(1.f, sec);
        m_phaseTime = 0.f;
    }

    void finishRace(const std::vector<RaceResultEntry>& standings) {
        m_results = standings;
        std::sort(m_results.begin(), m_results.end(), [](const RaceResultEntry& a, const RaceResultEntry& b) {
            if (a.finished != b.finished) return a.finished;
            if (a.laps != b.laps) return a.laps > b.laps;
            return a.totalTime < b.totalTime;
        });
        for (size_t i = 0; i < m_results.size(); ++i)
            m_results[i].position = static_cast<int>(i) + 1;
        m_phase = SessionFlowPhase::Finished;
        std::fprintf(stderr, "SessionFlow: RESULTS (%zu)\n", m_results.size());
    }

    SessionFlowPhase phase() const { return m_phase; }
    float countdown() const { return m_countdown; }
    float phaseTime() const { return m_phaseTime; }
    int totalLaps() const { return m_totalLaps; }
    const std::vector<QualyEntry>& qualy() const { return m_qualy; }
    const std::vector<RaceResultEntry>& results() const { return m_results; }
    bool isGreen() const { return m_phase == SessionFlowPhase::Race; }
    bool blocksDrive() const {
        return m_phase == SessionFlowPhase::Countdown ||
               m_phase == SessionFlowPhase::GridPublished ||
               m_phase == SessionFlowPhase::Finished;
    }

private:
    SessionFlowPhase m_phase = SessionFlowPhase::Idle;
    float m_phaseTime = 0.f;
    float m_qualyDuration = 600.f;
    float m_countdown = 0.f;
    int m_totalLaps = 5;
    std::vector<QualyEntry> m_qualy;
    std::vector<RaceResultEntry> m_results;
};

} // namespace sim
} // namespace ks
