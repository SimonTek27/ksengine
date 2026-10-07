#pragma once
/**
 * Track-limits detection → warnings → RaceSessionManager penalties (Sprint 4 / P1.3).
 */
#include "RaceSessionManager.h"
#include "MathTypes.h"
#include <cmath>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstdio>

namespace ks {
namespace sim {

struct TrackLimitsConfig {
    float maxLateralM = 8.0f;
    float graceSec = 0.35f;
    float cooldownSec = 1.5f;
    bool enabled = true;
    // Escalation thresholds (reports within session, reset after penalty)
    int warnEvery = 1;           // emit warning each report
    int timePenaltyAt = 3;       // +1s time
    float timePenaltySec = 1.0f;
    int driveThroughAt = 5;      // DT
    int stopGoAt = 8;            // SG
    int dqAt = 12;               // DQ
};

class TrackLimitsMonitor {
public:
    void setConfig(const TrackLimitsConfig& c) { m_cfg = c; }
    const TrackLimitsConfig& config() const { return m_cfg; }
    void setCenterline(const std::vector<vec3>& pts) { m_center = pts; }

    void update(float dt, int carIndex, const vec3& pos, bool onTrackHint,
                RaceSessionManager* session) {
        if (!m_cfg.enabled || !session) return;
        bool off = !onTrackHint;
        if (!m_center.empty()) {
            float lat = lateralDistance(pos);
            if (lat > m_cfg.maxLateralM) off = true;
            else if (onTrackHint) off = false;
        }
        if (carIndex < 0) carIndex = 0;
        if ((int)m_state.size() <= carIndex)
            m_state.resize(static_cast<size_t>(carIndex) + 1);
        auto& st = m_state[static_cast<size_t>(carIndex)];
        st.cooldown = std::max(0.f, st.cooldown - dt);
        if (off) {
            st.offTimer += dt;
            if (st.offTimer >= m_cfg.graceSec && st.cooldown <= 0.f) {
                st.cooldown = m_cfg.cooldownSec;
                st.offTimer = 0.f;
                st.reports++;
                applyEscalation(carIndex, st.reports, session);
                if (onViolation) onViolation(carIndex, st.reports);
            }
        } else {
            st.offTimer = 0.f;
        }
    }

    int reportsFor(int carIndex) const {
        if (carIndex < 0 || carIndex >= (int)m_state.size()) return 0;
        return m_state[static_cast<size_t>(carIndex)].reports;
    }

    void resetCar(int carIndex) {
        if (carIndex >= 0 && carIndex < (int)m_state.size())
            m_state[static_cast<size_t>(carIndex)] = {};
    }

    std::function<void(int carIndex, int totalReports)> onViolation;

private:
    void applyEscalation(int carIndex, int reports, RaceSessionManager* session) {
        // Always notify RSM for warning counter
        session->reportTrackLimitsViolation(carIndex);

        if (m_cfg.dqAt > 0 && reports >= m_cfg.dqAt) {
            session->addPenalty(carIndex, Penalty::Type::Disqualification, 0.f,
                                "Track limits DQ");
            std::fprintf(stderr, "TrackLimits: car %d DQ (%d reports)\n", carIndex, reports);
            return;
        }
        if (m_cfg.stopGoAt > 0 && reports == m_cfg.stopGoAt) {
            session->addPenalty(carIndex, Penalty::Type::StopGo, 10.f,
                                "Track limits Stop-Go");
            std::fprintf(stderr, "TrackLimits: car %d Stop-Go (%d)\n", carIndex, reports);
            return;
        }
        if (m_cfg.driveThroughAt > 0 && reports == m_cfg.driveThroughAt) {
            session->addPenalty(carIndex, Penalty::Type::DriveThrough, 0.f,
                                "Track limits Drive-Through");
            std::fprintf(stderr, "TrackLimits: car %d DT (%d)\n", carIndex, reports);
            return;
        }
        if (m_cfg.timePenaltyAt > 0 && reports > 0 &&
            reports % m_cfg.timePenaltyAt == 0) {
            session->addPenalty(carIndex, Penalty::Type::TimeAdded, m_cfg.timePenaltySec,
                                "Track limits time");
            std::fprintf(stderr, "TrackLimits: car %d +%.1fs (%d)\n",
                         carIndex, m_cfg.timePenaltySec, reports);
        }
    }

    float lateralDistance(const vec3& pos) const {
        if (m_center.size() < 2) return 0.f;
        float best = 1e9f;
        for (size_t i = 0; i + 1 < m_center.size(); ++i) {
            const vec3& a = m_center[i];
            const vec3& b = m_center[i + 1];
            float abx = b.x - a.x, abz = b.z - a.z;
            float apx = pos.x - a.x, apz = pos.z - a.z;
            float ab2 = abx * abx + abz * abz;
            float t = ab2 > 1e-6f ? std::clamp((apx * abx + apz * abz) / ab2, 0.f, 1.f) : 0.f;
            float cx = a.x + abx * t, cz = a.z + abz * t;
            float dx = pos.x - cx, dz = pos.z - cz;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d < best) best = d;
        }
        return best;
    }

    struct CarState {
        float offTimer = 0.f;
        float cooldown = 0.f;
        int reports = 0;
    };
    TrackLimitsConfig m_cfg;
    std::vector<vec3> m_center;
    std::vector<CarState> m_state;
};

} // namespace sim
} // namespace ks
