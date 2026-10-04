#include "DifferentialModel.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <cmath>
#include <algorithm>

// ============================================================================
// DifferentialModel implementation
// ============================================================================

void DifferentialModel::update(float dt, float inputTorque, float leftSpeed, float rightSpeed) {
    // Calculate slip ratio
    m_state.slipRatio = calculateSlipRatio(leftSpeed, rightSpeed);

    // Calculate locking torque based on differential type
    bool isDrive = inputTorque > 0;
    m_state.lockingTorque = calculateLockingTorque(m_state.slipRatio, isDrive);
    m_state.isLocking = std::abs(m_state.lockingTorque) > 1.0f;

    // Distribute torque
    m_state.leftTorque = calculateLeftTorque(inputTorque, m_state.slipRatio);
    m_state.rightTorque = calculateRightTorque(inputTorque, m_state.slipRatio);

    // Update temperature
    updateTemperature(dt, m_state.lockingTorque);
}

void DifferentialModel::reset() {
    m_state = DiffState();
}

void DifferentialModel::setConfig(const DiffConfig& config) {
    m_config = config;
}

float DifferentialModel::calculateLeftTorque(float inputTorque, float slipRatio) const {
    float halfTorque = inputTorque / 2.0f;
    float lockEffect = m_state.lockingTorque / 2.0f;

    if (slipRatio > 0) {
        // Left wheel spinning faster - transfer torque to right
        return halfTorque - lockEffect;
    } else {
        // Right wheel spinning faster - transfer torque to left
        return halfTorque + lockEffect;
    }
}

float DifferentialModel::calculateRightTorque(float inputTorque, float slipRatio) const {
    float halfTorque = inputTorque / 2.0f;
    float lockEffect = m_state.lockingTorque / 2.0f;

    if (slipRatio > 0) {
        return halfTorque + lockEffect;
    } else {
        return halfTorque - lockEffect;
    }
}

float DifferentialModel::calculateLockingTorque(float slipRatio, bool isDrive) const {
    float absSlip = std::abs(slipRatio);

    switch (m_config.type) {
        case DiffType::Open:
            return 0.0f;

        case DiffType::LSD_Cls:
            return calculateClutchLsdTorque(slipRatio);

        case DiffType::LSD_Viscous:
            return calculateViscousLsdTorque(slipRatio);

        case DiffType::LSD_Geared:
            return calculateGearedLsdTorque(slipRatio);

        case DiffType::Locked:
            return m_config.maxLock;

        case DiffType::Active:
            // Active diff adjusts based on conditions
            return calculateClutchLsdTorque(slipRatio) * 0.8f;

        default:
            return 0.0f;
    }
}

float DifferentialModel::calculateSlipRatio(float leftSpeed, float rightSpeed) const {
    float avgSpeed = (leftSpeed + rightSpeed) / 2.0f;
    if (std::abs(avgSpeed) < 0.1f) return 0;

    return ((leftSpeed - rightSpeed) / std::abs(avgSpeed)) * 100.0f;
}

float DifferentialModel::calculateSpeedDifference(float leftSpeed, float rightSpeed) const {
    return leftSpeed - rightSpeed;
}

float DifferentialModel::calculateTemperatureEffect() const {
    // Temperature affects LSD behavior
    if (m_state.temperature < 50.0f) {
        return 0.8f; // Cold - less effective
    } else if (m_state.temperature < 120.0f) {
        return 1.0f; // Optimal
    } else {
        return 0.9f; // Overheated - slightly less effective
    }
}

void DifferentialModel::updateTemperature(float dt, float lockingTorque) {
    // Heat generation from friction
    float heatGen = std::abs(lockingTorque) * 0.01f;

    // Cooling (simplified)
    float cooling = (m_state.temperature - 80.0f) * 0.1f;

    m_state.temperature += (heatGen - cooling) * dt;
    m_state.temperature = std::max(20.0f, std::min(200.0f, m_state.temperature));
}

// ============================================================================
// LSD calculations
// ============================================================================

float DifferentialModel::calculateClutchLsdTorque(float slipRatio) const {
    float absSlip = std::abs(slipRatio);
    float tempEffect = calculateTemperatureEffect();

    if (absSlip < m_config.slipThreshold) {
        return m_config.preload * tempEffect;
    }

    // Clutch-type LSD: locking increases with slip
    float rampFactor = std::sin(m_config.rampAngle * 3.14159f / 180.0f);
    float slipFactor = (absSlip - m_config.slipThreshold) / 100.0f;

    float lockTorque = m_config.preload + m_config.maxLock * slipFactor * rampFactor;

    // Apply power/coast factors
    if (slipRatio > 0) {
        lockTorque *= m_config.drivePower;
    } else {
        lockTorque *= m_config.coastPower;
    }

    return std::min(lockTorque, m_config.maxLock) * tempEffect;
}

float DifferentialModel::calculateViscousLsdTorque(float slipRatio) const {
    float absSlip = std::abs(slipRatio);
    float tempEffect = calculateTemperatureEffect();

    // Viscous LSD: torque proportional to speed difference
    float lockTorque = absSlip * m_config.maxLock / 50.0f;

    return std::min(lockTorque, m_config.maxLock) * tempEffect;
}

float DifferentialModel::calculateGearedLsdTorque(float slipRatio) const {
    float absSlip = std::abs(slipRatio);
    float tempEffect = calculateTemperatureEffect();

    // Geared LSD (Torsen-type): torque bias ratio
    float tbr = 3.0f; // Typical TBR
    float lockTorque = absSlip * tbr * 0.5f;

    return std::min(lockTorque, m_config.maxLock) * tempEffect;
}

// ============================================================================
// Presets
// ============================================================================

DifferentialModel::DiffConfig DifferentialModel::getOpenDiff() {
    DiffConfig config;
    config.type = DiffType::Open;
    config.preload = 0.0f;
    config.coastPower = 0.0f;
    config.drivePower = 0.0f;
    return config;
}

DifferentialModel::DiffConfig DifferentialModel::getLSDClutch() {
    DiffConfig config;
    config.type = DiffType::LSD_Cls;
    config.preload = 20.0f;
    config.coastPower = 0.3f;
    config.drivePower = 0.5f;
    config.maxLock = 100.0f;
    config.rampAngle = 30.0f;
    config.slipThreshold = 5.0f;
    return config;
}

DifferentialModel::DiffConfig DifferentialModel::getLSDViscous() {
    DiffConfig config;
    config.type = DiffType::LSD_Viscous;
    config.preload = 10.0f;
    config.maxLock = 80.0f;
    return config;
}

DifferentialModel::DiffConfig DifferentialModel::getLSDTorsen() {
    DiffConfig config;
    config.type = DiffType::LSD_Geared;
    config.preload = 15.0f;
    config.maxLock = 120.0f;
    return config;
}

DifferentialModel::DiffConfig DifferentialModel::getLockedDiff() {
    DiffConfig config;
    config.type = DiffType::Locked;
    config.maxLock = 200.0f;
    return config;
}

DifferentialModel::DiffConfig DifferentialModel::getActiveDiff() {
    DiffConfig config;
    config.type = DiffType::Active;
    config.preload = 15.0f;
    config.coastPower = 0.4f;
    config.drivePower = 0.6f;
    config.maxLock = 150.0f;
    return config;
}

// ============================================================================
// Validation
// ============================================================================

bool DifferentialModel::validateConfig(const DiffConfig& config, QString* error) {
    if (config.preload < 0 || config.preload > 100.0f) {
        if (error) *error = "Preload out of range (0-100 Nm)";
        return false;
    }

    if (config.coastPower < 0 || config.coastPower > 1.0f) {
        if (error) *error = "Coast power out of range (0-1)";
        return false;
    }

    if (config.drivePower < 0 || config.drivePower > 1.0f) {
        if (error) *error = "Drive power out of range (0-1)";
        return false;
    }

    if (config.maxLock < 0 || config.maxLock > 500.0f) {
        if (error) *error = "Max lock out of range (0-500 Nm)";
        return false;
    }

    return true;
}

QString DifferentialModel::getDiffTypeName(DiffType type) {
    switch (type) {
        case DiffType::Open: return "Open";
        case DiffType::LSD_Cls: return "LSD (Clutch)";
        case DiffType::LSD_Viscous: return "LSD (Viscous)";
        case DiffType::LSD_Geared: return "LSD (Geared/Torsen)";
        case DiffType::Locked: return "Locked";
        case DiffType::Active: return "Active";
        default: return "Unknown";
    }
}

// ============================================================================
// DifferentialModelManager implementation
// ============================================================================

void DifferentialModel::loadFromIni(const QString& iniPath) {
    DifferentialModelManager mgr;
    mgr.loadFromIni(iniPath);
    setConfig(mgr.model().getConfig());
}

DifferentialModelManager::DifferentialModelManager() {
}

void DifferentialModelManager::loadFromIni(const QString& drivetrainIniPath) {
    QFile file(drivetrainIniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    DifferentialModel::DiffConfig config;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();

        if (line.contains('=')) {
            int eqPos = line.indexOf('=');
            QString key = line.left(eqPos).trimmed().toUpper();
            QString value = line.mid(eqPos + 1).trimmed();

            if (key == "TYPE") {
                if (value.contains("OPEN")) config.type = DifferentialModel::DiffType::Open;
                else if (value.contains("CLUTCH") || value.contains("LSD")) config.type = DifferentialModel::DiffType::LSD_Cls;
                else if (value.contains("VISCOUS")) config.type = DifferentialModel::DiffType::LSD_Viscous;
                else if (value.contains("GEARED") || value.contains("TORSEN")) config.type = DifferentialModel::DiffType::LSD_Geared;
                else if (value.contains("LOCKED")) config.type = DifferentialModel::DiffType::Locked;
                else if (value.contains("ACTIVE")) config.type = DifferentialModel::DiffType::Active;
            } else if (key == "PRELOAD") {
                config.preload = value.toFloat();
            } else if (key == "COAST_POWER") {
                config.coastPower = value.toFloat();
            } else if (key == "DRIVE_POWER") {
                config.drivePower = value.toFloat();
            } else if (key == "MAX_LOCK") {
                config.maxLock = value.toFloat();
            } else if (key == "RAMP_ANGLE") {
                config.rampAngle = value.toFloat();
            }
        }
    }

    file.close();
    m_model.setConfig(config);
}

void DifferentialModelManager::saveToIni(const QString& drivetrainIniPath) const {
    QFile file(drivetrainIniPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    DifferentialModel::DiffConfig config = m_model.getConfig();

    QTextStream stream(&file);
    stream << "[DIFFERENTIAL]\n";
    stream << "TYPE=" << DifferentialModel::getDiffTypeName(config.type) << "\n";
    stream << "PRELOAD=" << QString::number(config.preload, 'f', 2) << "\n";
    stream << "COAST_POWER=" << QString::number(config.coastPower, 'f', 4) << "\n";
    stream << "DRIVE_POWER=" << QString::number(config.drivePower, 'f', 4) << "\n";
    stream << "MAX_LOCK=" << QString::number(config.maxLock, 'f', 2) << "\n";
    stream << "RAMP_ANGLE=" << QString::number(config.rampAngle, 'f', 2) << "\n";

    file.close();
}

void DifferentialModelManager::update(float dt, float engineTorque, float leftWheelSpeed, float rightWheelSpeed) {
    m_model.update(dt, engineTorque, leftWheelSpeed, rightWheelSpeed);
}

float DifferentialModelManager::getLockingPercentage() const {
    float lockTorque = m_model.getLockingTorque();
    DifferentialModel::DiffConfig config = m_model.getConfig();
    return (config.maxLock > 0) ? (lockTorque / config.maxLock) * 100.0f : 0.0f;
}

float DifferentialModelManager::getTemperature() const {
    return m_model.getState().temperature;
}

bool DifferentialModelManager::isLocking() const {
    return m_model.getState().isLocking;
}

QMap<QString, QPair<float, float>> DifferentialModelManager::compareDiffs(
    const DifferentialModelManager& other) const {

    QMap<QString, QPair<float, float>> comparison;

    DifferentialModel::DiffConfig config1 = m_model.getConfig();
    DifferentialModel::DiffConfig config2 = other.m_model.getConfig();

    comparison["Preload"] = QPair<float,float>(config1.preload, config2.preload);
    comparison["Coast Power"] = QPair<float,float>(config1.coastPower, config2.coastPower);
    comparison["Drive Power"] = QPair<float,float>(config1.drivePower, config2.drivePower);
    comparison["Max Lock"] = QPair<float,float>(config1.maxLock, config2.maxLock);

    return comparison;
}
