#pragma once
/**
 * Pit-lane service / repair (rF2-like).
 *
 * Car must be InGarage (or stationary in assigned box). Work items run in parallel
 * or sequence; each has a duration from DamageSystem::calculateRepairData() and
 * optional fuel/tyre jobs. On completion applies repairPartial / repairSystem.
 */
#include "GarageExit.h"
#include "GarageSpawn.h"
#include "engine/physics/DamageSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace ks {
namespace sim {

enum class PitJobType : uint8_t {
    Body = 0,
    Suspension,
    Aero,
    Engine,
    Transmission,
    Brakes,
    Tyres,
    Fuel,
    FullService,
    COUNT
};

enum class PitJobState : uint8_t {
    Queued = 0,
    Running,
    Done,
    Cancelled
};

struct PitJob {
    PitJobType type = PitJobType::Body;
    PitJobState state = PitJobState::Queued;
    float durationSec = 1.f;
    float elapsedSec = 0.f;
    float progress = 0.f;
    float fuelAddL = 0.f;
    bool changeTyres = false;
};

struct PitRepairConfig {
    float bodyTimeScale = 1.f;
    float suspTimeScale = 1.f;
    float aeroTimeScale = 1.f;
    float engineTimeScale = 1.f;
    float transTimeScale = 1.f;
    float brakesTimeScale = 1.f;
    float tyreChangeSec = 4.5f;
    float fuelRateLps = 2.5f;
    float minStationarySpeedMs = 0.35f;
    float maxRefuelL = 120.f;
    bool parallelJobs = true;
    int maxParallel = 2;
    float stopGoRepairSec = 10.f;
};

struct PitRepairInput {
    bool inGarageBox = false;
    bool requestService = false;
    bool requestAbort = false;
    bool serveStopGo = false;
    float speedMs = 0.f;
    float fuelL = 50.f;
    float fuelCapacityL = 100.f;
    float targetFuelL = -1.f;
    bool wantTyres = false;
    bool wantBody = true;
    bool wantSuspension = true;
    bool wantAero = true;
    bool wantEngine = false;
    bool wantTransmission = false;
    bool wantBrakes = false;
    bool wantFull = false;
};

struct PitRepairOutput {
    bool serviceActive = false;
    bool holdCar = false;
    bool completedThisFrame = false;
    float overallProgress = 0.f;
    float remainingSec = 0.f;
    float fuelL = 0.f;
    bool tyresChanged = false;
    std::string statusText;
    int jobsDone = 0;
    int jobsTotal = 0;
};

class PitLaneRepair {
public:
    void setConfig(const PitRepairConfig& c) { m_cfg = c; }
    const PitRepairConfig& config() const { return m_cfg; }

    bool isBusy() const { return m_busy; }
    const std::vector<PitJob>& jobs() const { return m_jobs; }

    void plan(ks::physics::DamageSystem& dmg, const PitRepairInput& in) {
        m_jobs.clear();
        m_busy = false;
        m_tyresDone = false;
        m_fuelL = in.fuelL;
        m_fuelCap = in.fuelCapacityL;

        const auto rd = dmg.calculateRepairData();

        auto add = [&](PitJobType t, float dur) {
            if (dur < 0.05f) dur = 0.05f;
            PitJob j;
            j.type = t;
            j.durationSec = dur;
            m_jobs.push_back(j);
        };

        if (in.serveStopGo) {
            add(PitJobType::Body, m_cfg.stopGoRepairSec);
        } else if (in.wantFull) {
            const float body = rd.bodyRepairTime * m_cfg.bodyTimeScale * 0.5f;
            const float sus = rd.suspensionRepairTime * m_cfg.suspTimeScale * 0.5f;
            const float aero = rd.aeroRepairTime * m_cfg.aeroTimeScale * 0.5f;
            add(PitJobType::FullService,
                std::max({body, sus, aero, m_cfg.tyreChangeSec}) + 1.f);
            m_jobs.back().changeTyres = true;
            const float target = in.targetFuelL >= 0.f ? in.targetFuelL : m_fuelCap;
            m_jobs.back().fuelAddL = std::clamp(target - in.fuelL, 0.f, m_cfg.maxRefuelL);
        } else {
            if (in.wantBody && rd.bodyRepairTime > 0.2f)
                add(PitJobType::Body, rd.bodyRepairTime * m_cfg.bodyTimeScale);
            if (in.wantSuspension && rd.suspensionRepairTime > 0.2f)
                add(PitJobType::Suspension, rd.suspensionRepairTime * m_cfg.suspTimeScale);
            if (in.wantAero && rd.aeroRepairTime > 0.2f)
                add(PitJobType::Aero, rd.aeroRepairTime * m_cfg.aeroTimeScale);
            if (in.wantEngine && rd.engineRepairTime > 0.2f)
                add(PitJobType::Engine, rd.engineRepairTime * m_cfg.engineTimeScale);
            if (in.wantTransmission)
                add(PitJobType::Transmission, 25.f * m_cfg.transTimeScale);
            if (in.wantBrakes)
                add(PitJobType::Brakes, 12.f * m_cfg.brakesTimeScale);
            if (in.wantTyres) {
                add(PitJobType::Tyres, m_cfg.tyreChangeSec);
                m_jobs.back().changeTyres = true;
            }
            const float target = in.targetFuelL >= 0.f ? in.targetFuelL : -1.f;
            if (target >= 0.f) {
                const float need = std::clamp(target - in.fuelL, 0.f, m_cfg.maxRefuelL);
                if (need > 0.05f) {
                    add(PitJobType::Fuel, need / std::max(0.1f, m_cfg.fuelRateLps));
                    m_jobs.back().fuelAddL = need;
                }
            }
        }
    }

    PitRepairOutput update(float dt, const PitRepairInput& in,
                           ks::physics::DamageSystem& dmg,
                           GarageExitPhase garagePhase) {
        PitRepairOutput out;
        out.fuelL = m_fuelL;

        const bool canService =
            (in.inGarageBox || garagePhase == GarageExitPhase::InGarage ||
             garagePhase == GarageExitPhase::Returning) &&
            in.speedMs <= m_cfg.minStationarySpeedMs;

        if (in.requestAbort && m_busy) {
            for (auto& j : m_jobs)
                if (j.state != PitJobState::Done) j.state = PitJobState::Cancelled;
            m_busy = false;
            out.statusText = "SERVICE ABORTED";
            return out;
        }

        if (!m_busy && in.requestService && canService) {
            plan(dmg, in);
            if (m_jobs.empty()) {
                out.statusText = "NO WORK";
                return out;
            }
            m_busy = true;
            int started = 0;
            for (auto& j : m_jobs) {
                if (j.state != PitJobState::Queued) continue;
                j.state = PitJobState::Running;
                if (++started >= (m_cfg.parallelJobs ? m_cfg.maxParallel : 1))
                    break;
            }
        }

        if (!m_busy) {
            out.statusText = canService ? "READY FOR SERVICE" : "ENTER GARAGE";
            return out;
        }

        if (!canService) {
            out.serviceActive = true;
            out.holdCar = true;
            out.statusText = "HOLD \xe2\x80\x94 MUST BE STATIONARY IN BOX";
            return out;
        }

        out.serviceActive = true;
        out.holdCar = true;

        int running = 0;
        int done = 0;
        float rem = 0.f;
        float progSum = 0.f;

        for (auto& j : m_jobs) {
            if (j.state == PitJobState::Running) {
                j.elapsedSec += dt;
                j.progress = std::clamp(j.elapsedSec / j.durationSec, 0.f, 1.f);
                if (j.type == PitJobType::Fuel && j.fuelAddL > 0.f) {
                    const float add = m_cfg.fuelRateLps * dt;
                    const float room = m_fuelCap - m_fuelL;
                    const float got = std::min({add, room, j.fuelAddL});
                    m_fuelL += got;
                    j.fuelAddL -= got;
                }
                if (j.elapsedSec >= j.durationSec) {
                    completeJob(j, dmg);
                    j.state = PitJobState::Done;
                    j.progress = 1.f;
                    out.completedThisFrame = true;
                } else {
                    ++running;
                    rem += j.durationSec - j.elapsedSec;
                }
            }
            if (j.state == PitJobState::Done) ++done;
            progSum += j.progress;
        }

        if (running < (m_cfg.parallelJobs ? m_cfg.maxParallel : 1)) {
            for (auto& j : m_jobs) {
                if (j.state != PitJobState::Queued) continue;
                j.state = PitJobState::Running;
                ++running;
                if (running >= (m_cfg.parallelJobs ? m_cfg.maxParallel : 1))
                    break;
            }
        }

        out.jobsDone = done;
        out.jobsTotal = static_cast<int>(m_jobs.size());
        out.overallProgress =
            m_jobs.empty() ? 1.f : progSum / static_cast<float>(m_jobs.size());
        out.remainingSec = rem;
        out.fuelL = m_fuelL;
        out.tyresChanged = m_tyresDone;

        const bool allDone = done >= out.jobsTotal;
        if (allDone) {
            m_busy = false;
            out.serviceActive = false;
            out.holdCar = false;
            out.overallProgress = 1.f;
            out.statusText = "SERVICE COMPLETE";
            out.completedThisFrame = true;
            if (onServiceComplete) onServiceComplete(*this);
        } else {
            out.statusText = statusFromJobs();
        }
        return out;
    }

    std::function<void(PitLaneRepair&)> onServiceComplete;

    static const char* jobName(PitJobType t) {
        switch (t) {
        case PitJobType::Body: return "BODY";
        case PitJobType::Suspension: return "SUSPENSION";
        case PitJobType::Aero: return "AERO";
        case PitJobType::Engine: return "ENGINE";
        case PitJobType::Transmission: return "GEARBOX";
        case PitJobType::Brakes: return "BRAKES";
        case PitJobType::Tyres: return "TYRES";
        case PitJobType::Fuel: return "FUEL";
        case PitJobType::FullService: return "FULL SERVICE";
        default: return "JOB";
        }
    }

private:
    void completeJob(PitJob& j, ks::physics::DamageSystem& dmg) {
        using DT = ks::physics::DamageType;
        switch (j.type) {
        case PitJobType::Body:
            dmg.repairPartial(0.85f);
            break;
        case PitJobType::Suspension:
            dmg.repairSystem(DT::Suspension);
            break;
        case PitJobType::Aero:
            dmg.repairSystem(DT::Aero);
            break;
        case PitJobType::Engine:
            dmg.repairSystem(DT::Engine);
            break;
        case PitJobType::Transmission:
            dmg.repairSystem(DT::Transmission);
            break;
        case PitJobType::Brakes:
            dmg.repairSystem(DT::Brakes);
            break;
        case PitJobType::Tyres:
            m_tyresDone = true;
            break;
        case PitJobType::Fuel:
            break;
        case PitJobType::FullService:
            dmg.repairPartial(0.75f);
            dmg.repairSystem(DT::Suspension);
            dmg.repairSystem(DT::Aero);
            m_tyresDone = j.changeTyres;
            break;
        default:
            break;
        }
    }

    std::string statusFromJobs() const {
        for (const auto& j : m_jobs) {
            if (j.state == PitJobState::Running) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s %.0f%%", jobName(j.type),
                              j.progress * 100.f);
                return buf;
            }
        }
        return "SERVICING";
    }

    PitRepairConfig m_cfg;
    std::vector<PitJob> m_jobs;
    bool m_busy = false;
    bool m_tyresDone = false;
    float m_fuelL = 0.f;
    float m_fuelCap = 100.f;
};

} // namespace sim
} // namespace ks
