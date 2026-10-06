#include "DifferentialModel.h"

#include <algorithm>
#include <fstream>

namespace ks {
namespace physics {

void DifferentialModel::reset() {
    m_state = DiffState{};
    m_state.temperature = 80.0f;
}

float DifferentialModel::calculateSlipRatio(float leftSpeed, float rightSpeed) const {
    float avg = 0.5f * (leftSpeed + rightSpeed);
    if (std::abs(avg) < 0.1f) return 0.0f;
    return (leftSpeed - rightSpeed) / avg;
}

float DifferentialModel::calculateLockingTorque(float slipRatio, bool isDrive) const {
    float absSlip = std::abs(slipRatio);
    switch (m_config.type) {
    case DiffType::Open:
        return 0.0f;
    case DiffType::Locked:
        return m_config.maxLock;
    case DiffType::LSD_Viscous:
        return std::min(m_config.maxLock, m_config.preload + absSlip * 800.0f);
    case DiffType::LSD_Geared:
        return std::min(m_config.maxLock, m_config.preload + absSlip * 1200.0f);
    case DiffType::Active:
        return std::min(m_config.maxLock, m_config.preload + absSlip * 1500.0f);
    case DiffType::LSD_Cls:
    default: {
        float power = isDrive ? m_config.drivePower : m_config.coastPower;
        float lock = m_config.preload + power * absSlip * m_config.maxLock;
        return std::clamp(lock, 0.0f, m_config.maxLock);
    }
    }
}

void DifferentialModel::update(float dt, float inputTorque, float leftSpeed, float rightSpeed) {
    dt = std::clamp(dt, 1e-4f, 0.05f);
    float slip = calculateSlipRatio(leftSpeed, rightSpeed);
    m_state.slipRatio = slip;
    bool isDrive = inputTorque > 1.0f;
    float lock = calculateLockingTorque(slip, isDrive);
    m_state.lockingTorque = lock;
    m_state.isLocking = lock > m_config.preload * 1.1f;

    // Base open split 50/50
    float half = inputTorque * 0.5f;
    // Locking transfers torque toward the slower wheel when driving
    float transfer = 0.0f;
    if (m_config.type != DiffType::Open) {
        transfer = std::copysign(std::min(lock, std::abs(inputTorque) * 0.5f), -slip);
        // if left faster (slip>0), transfer torque to right (negative to left)
    }

    m_state.leftTorque = half - transfer;
    m_state.rightTorque = half + transfer;

    // Thermal
    m_state.temperature += (std::abs(lock) * 0.001f - (m_state.temperature - 40.0f) * 0.02f) * dt;
    m_state.temperature = std::clamp(m_state.temperature, 40.0f, 180.0f);
}

void DifferentialModel::loadFromIni(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key.find("PRELOAD") != std::string::npos) m_config.preload = std::stof(val);
        else if (key.find("POWER") != std::string::npos && key.find("COAST") == std::string::npos)
            m_config.drivePower = std::stof(val);
        else if (key.find("COAST") != std::string::npos) m_config.coastPower = std::stof(val);
    }
}

DifferentialModel::DiffConfig DifferentialModel::getOpenDiff() {
    DiffConfig c; c.type = DiffType::Open; c.preload = 0; c.maxLock = 0; return c;
}
DifferentialModel::DiffConfig DifferentialModel::getLSDClutch() {
    DiffConfig c; c.type = DiffType::LSD_Cls; return c;
}
DifferentialModel::DiffConfig DifferentialModel::getLSDViscous() {
    DiffConfig c; c.type = DiffType::LSD_Viscous; return c;
}
DifferentialModel::DiffConfig DifferentialModel::getLSDTorsen() {
    DiffConfig c; c.type = DiffType::LSD_Geared; c.preload = 10; return c;
}
DifferentialModel::DiffConfig DifferentialModel::getLockedDiff() {
    DiffConfig c; c.type = DiffType::Locked; c.maxLock = 2000; return c;
}
DifferentialModel::DiffConfig DifferentialModel::getActiveDiff() {
    DiffConfig c; c.type = DiffType::Active; return c;
}
std::string DifferentialModel::getDiffTypeName(DiffType type) {
    switch (type) {
    case DiffType::Open: return "Open";
    case DiffType::LSD_Cls: return "LSD Clutch";
    case DiffType::LSD_Viscous: return "LSD Viscous";
    case DiffType::LSD_Geared: return "LSD Torsen";
    case DiffType::Locked: return "Locked";
    case DiffType::Active: return "Active";
    }
    return "Unknown";
}

// --- GearboxModel ---
void GearboxModel::setRatios(const std::vector<float>& ratios, float finalDrive) {
    if (!ratios.empty()) m_ratios = ratios;
    m_finalDrive = finalDrive;
    if (m_gear > static_cast<int>(m_ratios.size())) m_gear = static_cast<int>(m_ratios.size());
    if (m_gear < 0) m_gear = 0;
}

void GearboxModel::setGear(int gear) {
    if (gear < 0) gear = 0;
    if (gear > static_cast<int>(m_ratios.size())) gear = static_cast<int>(m_ratios.size());
    m_gear = gear;
}

float GearboxModel::currentRatio() const {
    if (m_gear <= 0 || m_gear > static_cast<int>(m_ratios.size())) return 0.0f;
    return m_ratios[static_cast<size_t>(m_gear - 1)] * m_finalDrive;
}

void GearboxModel::autoShift(float rpm, float maxRpm, float idleRpm) {
    if (m_ratios.empty()) return;
    if (rpm > maxRpm * 0.92f && m_gear < static_cast<int>(m_ratios.size())) {
        ++m_gear;
    } else if (rpm < idleRpm * 1.8f && m_gear > 1) {
        --m_gear;
    }
}

} // namespace physics
} // namespace ks
