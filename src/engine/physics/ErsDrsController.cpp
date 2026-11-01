#include "ErsDrsController.h"
#include "HybridSystem.h"
#include <algorithm>

namespace ks {
namespace physics {

ErsDrsController::ErsDrsController() = default;

void ErsDrsController::setHybridSystem(std::unique_ptr<HybridSystem> sys) {
    m_hybridSystem = std::move(sys);
}

HybridSystem& ErsDrsController::hybridSystem() { return *m_hybridSystem; }
const HybridSystem& ErsDrsController::hybridSystem() const { return *m_hybridSystem; }

void ErsDrsController::reset() {
    m_ers = ErsState();
    m_drs = DrsState();
}

void ErsDrsController::activateAttackMode() {
    if (m_hybridSystem) {
        m_hybridSystem->activateAttack();
    }
}

void ErsDrsController::setDrsDragReduction(double f) {
    m_drs.dragReduction = std::clamp(f, 0.0, 1.0);
}

void ErsDrsController::update(const ErsDrsInput& input) {
    updateDrs(input.speed);
    // input.speed is km/h (updateDrs divides the threshold by 3.6); the hybrid
    // unit works in m/s.
    updateErs(input, input.speed / 3.6);
}

void ErsDrsController::updateDrs(double speed) {
    if (m_drs.enabled && m_drs.autoActivate && speed > m_drs.speedThreshold / 3.6) {
        m_drs.active = true;
    } else {
        m_drs.active = false;
    }
}

void ErsDrsController::updateErs(const ErsDrsInput& input, double speedMs) {
    if (m_hybridSystem) {
        // The Qt-free unit models one net torque: positive deploys, negative
        // harvests. The editor version reported deploy and regen separately and
        // also a battery temperature, which this one does not simulate, so
        // batteryTemp is left to whatever the caller set.
        const double net = m_hybridSystem->update(
            static_cast<float>(input.dt), static_cast<float>(input.throttle),
            static_cast<float>(input.brake), static_cast<float>(speedMs));
        m_ers.deployTorque = net > 0.0 ? net : 0.0;
        m_ers.regenTorque = net < 0.0 ? -net : 0.0;
        m_ers.batterySoc = m_hybridSystem->soc();
    } else if (m_ers.enabled) {
        m_ers.deployTorque = input.throttle > 0.5 ? 100.0 : 0.0;
        m_ers.regenTorque = input.brake > 0.5 ? 50.0 : 0.0;
    }
}

} // namespace physics
} // namespace ks
