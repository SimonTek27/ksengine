#include "EngineModel.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <cmath>
#include <algorithm>

// ============================================================================
// EngineModel implementation
// ============================================================================

void EngineModel::update(float dt, float throttle, float load) {
    m_state.throttle = throttle;
    m_state.load = load;

    // Calculate current torque
    m_state.torque = calculateTorque(m_state.rpm);

    // Apply turbo boost with lag + anti-lag
    if (m_config.turbo.enabled) {
        float effThrottle = throttle;
        if (m_config.turbo.antiLag && throttle < 0.05f && m_state.rpm > 3000.0f)
            effThrottle = m_config.turbo.antiLagThrottle;
        float targetBoost = calculateTurboBoost(m_state.rpm, effThrottle);
        m_state.boost = updateTurboLag(targetBoost, dt);
        m_state.torque = calculateTurboTorque(m_state.torque, m_state.boost);
    } else {
        m_state.boost = 0.0f;
    }

    // Calculate power
    m_state.power = m_state.torque * rpmToRadPerSec(m_state.rpm) / 1000.0f;

    // Apply engine braking when off throttle
    if (throttle < 0.05f) {
        m_state.torque -= calculateEngineBraking(m_state.rpm);
    }

    // Update RPM based on torque and load
    float angularAccel = (m_state.torque - load) / m_config.engineInertia;
    float angularVelocity = rpmToRadPerSec(m_state.rpm) + angularAccel * dt;
    m_state.rpm = radPerSecToRPM(angularVelocity);

    // Rev limiter
    if (m_state.rpm >= m_config.revLimiter) {
        m_state.rpm = m_config.revLimiter;
        m_state.isRevLimiter = true;
        m_state.torque = calculateRevLimiterTorque(m_state.rpm);
    } else {
        m_state.isRevLimiter = false;
    }

    // Idle control
    if (m_state.rpm < m_config.idleRPM && throttle < 0.05f) {
        m_state.rpm = m_config.idleRPM;
    }

    // Fuel consumption
    m_state.fuelFlow = calculateFuelFlow(m_state.power);
    m_state.fuel -= m_state.fuelFlow * dt / 3600.0f;
    m_state.fuel = std::max(0.0f, m_state.fuel);

    // Temperature (simplified)
    float heatGen = m_state.power * 0.01f;
    float cooling = (m_state.temperature - 80.0f) * 0.1f;
    m_state.temperature += (heatGen - cooling) * dt;
    m_state.temperature = std::max(20.0f, std::min(120.0f, m_state.temperature));
    // Water/oil thermal model with derating
    float loadF = 0.3f + 0.7f * throttle;
    m_state.waterTemp += ((85.0f + loadF * 25.0f + m_state.boost * 8.0f - m_state.waterTemp) * 0.05f + (m_state.power * 0.002f)) * dt * 10.0f;
    m_state.oilTemp += ((92.0f + loadF * 30.0f + m_state.boost * 10.0f - m_state.oilTemp) * 0.03f + (m_state.power * 0.0025f)) * dt * 10.0f;
    m_state.waterTemp = std::clamp(m_state.waterTemp, 20.0f, 130.0f);
    m_state.oilTemp = std::clamp(m_state.oilTemp, 20.0f, 150.0f);
    float derate = 1.0f;
    if (m_state.waterTemp > 110.0f) derate = std::min(derate, 1.0f - (m_state.waterTemp - 110.0f) * 0.02f);
    if (m_state.oilTemp > 130.0f) derate = std::min(derate, 1.0f - (m_state.oilTemp - 130.0f) * 0.025f);
    m_state.thermalDerate = std::clamp(derate, 0.6f, 1.0f);
    m_state.torque *= m_state.thermalDerate;
}

void EngineModel::reset() {
    m_state = EngineState();
    m_state.fuel = m_config.fuelCapacity;
}

void EngineModel::setConfig(const EngineConfig& config) {
    m_config = config;
}

float EngineModel::calculateTorque(float rpm) const {
    return interpolateTorqueCurve(rpm);
}

float EngineModel::calculatePower(float rpm) const {
    float torque = calculateTorque(rpm);
    return torque * rpmToRadPerSec(rpm) / 1000.0f;
}

float EngineModel::calculateTorqueAtRPM(float rpm) const {
    return interpolateTorqueCurve(rpm);
}

float EngineModel::calculateTurboBoost(float rpm, float throttle) const {
    if (!m_config.turbo.enabled) return 0.0f;

    float baseBoost = interpolateBoostCurve(rpm);
    float boost = baseBoost * throttle;

    // Apply wastegate limit
    if (rpm > m_config.turbo.wastegateRPM) {
        float wastegateFactor = 1.0f - (rpm - m_config.turbo.wastegateRPM) / 1000.0f;
        boost *= std::max(0.5f, wastegateFactor);
    }

    return std::min(boost, m_config.turbo.maxBoost);
}

float EngineModel::calculateTurboTorque(float baseTorque, float boost) const {
    // Turbo multiplies torque by (1 + boost)
    return baseTorque * (1.0f + boost);
}

float EngineModel::updateTurboLag(float targetBoost, float dt) {
    float tau = std::max(0.05f, m_config.turbo.turboLag);
    float alpha = dt / (tau + dt);
    m_state.boost += (targetBoost - m_state.boost) * alpha;
    return m_state.boost;
}

float EngineModel::calculateFuelConsumption(float rpm, float throttle) const {
    // Fuel consumption increases with RPM and load
    float baseConsumption = m_config.fuelConsumption;
    float rpmFactor = rpm / m_config.maxRPM;
    float loadFactor = 0.3f + 0.7f * throttle;

    return baseConsumption * rpmFactor * loadFactor;
}

float EngineModel::calculateFuelFlow(float power) const {
    // Fuel flow = Power * BSFC / fuel density
    float bsfc = 0.3f; // Brake Specific Fuel Consumption (kg/kWh)
    return (power * bsfc) / m_config.fuelDensity * 3.6f; // liters per hour
}

float EngineModel::calculateEngineBraking(float rpm) const {
    // Engine braking torque decreases with RPM
    float rpmRatio = rpm / m_config.coastRPM;
    float torque = m_config.coastTorque * rpmRatio;

    // Apply non-linearity
    if (m_config.coastNonLinearity > 0) {
        torque = std::pow(torque, 1.0f + m_config.coastNonLinearity);
    }

    return torque;
}

float EngineModel::calculateWheelRPM(float engineRPM, int gear) const {
    if (gear < 0 || gear >= m_config.gearRatios.size()) return 0;

    float totalRatio = m_config.gearRatios[gear].ratio * m_config.finalDrive;
    return engineRPM / totalRatio;
}

float EngineModel::calculateSpeed(float engineRPM, int gear) const {
    float wheelRPM = calculateWheelRPM(engineRPM, gear);
    // Speed = wheelRPM * tire circumference * 60 / 1000 (km/h)
    float tireCircumference = 2.0f * 3.14159f * 0.33f; // 0.33m radius
    return wheelRPM * tireCircumference * 60.0f / 1000.0f;
}

int EngineModel::calculateOptimalGear(float speed, float rpm) const {
    int bestGear = 0;
    float bestRPM = 0;

    for (int i = 0; i < m_config.gearRatios.size(); ++i) {
        float wheelRPM = speed / (2.0f * 3.14159f * 0.33f * 60.0f / 1000.0f);
        float engineRPM = wheelRPM * m_config.gearRatios[i].ratio * m_config.finalDrive;

        if (engineRPM >= m_config.peakTorqueRPM * 0.8f && engineRPM <= m_config.revLimiter) {
            if (bestRPM == 0 || std::abs(engineRPM - m_config.peakPowerRPM) < std::abs(bestRPM - m_config.peakPowerRPM)) {
                bestGear = i;
                bestRPM = engineRPM;
            }
        }
    }

    return bestGear;
}

QVector<float> EngineModel::calculateSpeedsAtRPM(float rpm) const {
    QVector<float> speeds;
    for (int i = 0; i < m_config.gearRatios.size(); ++i) {
        speeds.append(calculateSpeed(rpm, i));
    }
    return speeds;
}

bool EngineModel::isAtRevLimiter(float rpm) const {
    return rpm >= m_config.revLimiter;
}

float EngineModel::calculateRevLimiterTorque(float rpm) const {
    // Rev limiter cuts torque
    float overRev = rpm - m_config.revLimiter;
    return -overRev * 0.5f; // Negative torque to slow down
}

// ============================================================================
// Presets
// ============================================================================

EngineModel::EngineConfig EngineModel::getInline4_2000() {
    EngineConfig config;
    config.peakPower = 150.0f;
    config.peakPowerRPM = 6500.0f;
    config.peakTorque = 250.0f;
    config.peakTorqueRPM = 4000.0f;
    config.maxRPM = 7500.0f;
    config.idleRPM = 850.0f;
    config.revLimiter = 7500.0f;

    // Default torque curve
    config.torqueCurve.append({1000, 150});
    config.torqueCurve.append({2000, 200});
    config.torqueCurve.append({3000, 230});
    config.torqueCurve.append({4000, 250});
    config.torqueCurve.append({5000, 240});
    config.torqueCurve.append({6000, 220});
    config.torqueCurve.append({7000, 190});
    config.torqueCurve.append({7500, 170});

    config.gearRatios.append({1, 3.5f});
    config.gearRatios.append({2, 2.2f});
    config.gearRatios.append({3, 1.5f});
    config.gearRatios.append({4, 1.1f});
    config.gearRatios.append({5, 0.85f});
    config.gearRatios.append({6, 0.7f});

    config.finalDrive = 3.8f;
    config.fuelCapacity = 55.0f;

    return config;
}

EngineModel::EngineConfig EngineModel::getV6_3000() {
    EngineConfig config;
    config.peakPower = 220.0f;
    config.peakPowerRPM = 7000.0f;
    config.peakTorque = 320.0f;
    config.peakTorqueRPM = 5000.0f;
    config.maxRPM = 7800.0f;
    config.revLimiter = 7800.0f;

    config.torqueCurve.append({1000, 180});
    config.torqueCurve.append({2000, 250});
    config.torqueCurve.append({3000, 290});
    config.torqueCurve.append({4000, 310});
    config.torqueCurve.append({5000, 320});
    config.torqueCurve.append({6000, 300});
    config.torqueCurve.append({7000, 270});
    config.torqueCurve.append({7800, 230});

    config.gearRatios.append({1, 3.3f});
    config.gearRatios.append({2, 2.0f});
    config.gearRatios.append({3, 1.4f});
    config.gearRatios.append({4, 1.0f});
    config.gearRatios.append({5, 0.8f});
    config.gearRatios.append({6, 0.65f});

    config.finalDrive = 3.6f;
    config.fuelCapacity = 65.0f;

    return config;
}

EngineModel::EngineConfig EngineModel::getV8_4000() {
    EngineConfig config;
    config.peakPower = 350.0f;
    config.peakPowerRPM = 7500.0f;
    config.peakTorque = 450.0f;
    config.peakTorqueRPM = 5500.0f;
    config.maxRPM = 8500.0f;
    config.revLimiter = 8500.0f;

    config.torqueCurve.append({1000, 250});
    config.torqueCurve.append({2000, 320});
    config.torqueCurve.append({3000, 380});
    config.torqueCurve.append({4000, 420});
    config.torqueCurve.append({5000, 450});
    config.torqueCurve.append({6000, 430});
    config.torqueCurve.append({7000, 400});
    config.torqueCurve.append({8000, 350});
    config.torqueCurve.append({8500, 320});

    config.gearRatios.append({1, 3.0f});
    config.gearRatios.append({2, 1.8f});
    config.gearRatios.append({3, 1.3f});
    config.gearRatios.append({4, 0.95f});
    config.gearRatios.append({5, 0.75f});
    config.gearRatios.append({6, 0.6f});

    config.finalDrive = 3.5f;
    config.fuelCapacity = 80.0f;

    return config;
}

EngineModel::EngineConfig EngineModel::getV10_5000() {
    EngineConfig config;
    config.peakPower = 450.0f;
    config.peakPowerRPM = 8200.0f;
    config.peakTorque = 520.0f;
    config.peakTorqueRPM = 6000.0f;
    config.maxRPM = 9000.0f;
    config.revLimiter = 9000.0f;

    config.torqueCurve.append({1000, 280});
    config.torqueCurve.append({2000, 350});
    config.torqueCurve.append({3000, 420});
    config.torqueCurve.append({4000, 480});
    config.torqueCurve.append({5000, 510});
    config.torqueCurve.append({6000, 520});
    config.torqueCurve.append({7000, 490});
    config.torqueCurve.append({8000, 440});
    config.torqueCurve.append({9000, 380});

    config.gearRatios.append({1, 3.2f});
    config.gearRatios.append({2, 1.9f});
    config.gearRatios.append({3, 1.35f});
    config.gearRatios.append({4, 1.0f});
    config.gearRatios.append({5, 0.8f});
    config.gearRatios.append({6, 0.65f});

    config.finalDrive = 3.4f;
    config.fuelCapacity = 90.0f;

    return config;
}

EngineModel::EngineConfig EngineModel::getV12_6000() {
    EngineConfig config;
    config.peakPower = 550.0f;
    config.peakPowerRPM = 8500.0f;
    config.peakTorque = 600.0f;
    config.peakTorqueRPM = 6500.0f;
    config.maxRPM = 9500.0f;
    config.revLimiter = 9500.0f;

    config.torqueCurve.append({1000, 300});
    config.torqueCurve.append({2000, 380});
    config.torqueCurve.append({3000, 450});
    config.torqueCurve.append({4000, 520});
    config.torqueCurve.append({5000, 570});
    config.torqueCurve.append({6000, 600});
    config.torqueCurve.append({7000, 580});
    config.torqueCurve.append({8000, 530});
    config.torqueCurve.append({9000, 460});
    config.torqueCurve.append({9500, 420});

    config.gearRatios.append({1, 3.0f});
    config.gearRatios.append({2, 1.8f});
    config.gearRatios.append({3, 1.3f});
    config.gearRatios.append({4, 0.95f});
    config.gearRatios.append({5, 0.75f});
    config.gearRatios.append({6, 0.6f});

    config.finalDrive = 3.2f;
    config.fuelCapacity = 100.0f;

    return config;
}

EngineModel::EngineConfig EngineModel::getRotary_1300() {
    EngineConfig config;
    config.peakPower = 180.0f;
    config.peakPowerRPM = 9000.0f;
    config.peakTorque = 200.0f;
    config.peakTorqueRPM = 6500.0f;
    config.maxRPM = 10000.0f;
    config.revLimiter = 10000.0f;

    config.torqueCurve.append({2000, 120});
    config.torqueCurve.append({3000, 150});
    config.torqueCurve.append({4000, 170});
    config.torqueCurve.append({5000, 190});
    config.torqueCurve.append({6000, 200});
    config.torqueCurve.append({7000, 195});
    config.torqueCurve.append({8000, 185});
    config.torqueCurve.append({9000, 170});
    config.torqueCurve.append({10000, 150});

    config.gearRatios.append({1, 3.5f});
    config.gearRatios.append({2, 2.1f});
    config.gearRatios.append({3, 1.5f});
    config.gearRatios.append({4, 1.1f});
    config.gearRatios.append({5, 0.85f});
    config.gearRatios.append({6, 0.7f});

    config.finalDrive = 4.0f;
    config.fuelCapacity = 50.0f;

    return config;
}

EngineModel::EngineConfig EngineModel::getElectric() {
    EngineConfig config;
    config.peakPower = 300.0f;
    config.peakPowerRPM = 12000.0f;
    config.peakTorque = 400.0f;
    config.peakTorqueRPM = 0.0f; // Max torque from standstill
    config.maxRPM = 16000.0f;
    config.idleRPM = 0.0f;
    config.revLimiter = 16000.0f;

    // Electric motor torque curve (high torque at low RPM)
    config.torqueCurve.append({0, 400});
    config.torqueCurve.append({2000, 380});
    config.torqueCurve.append({4000, 350});
    config.torqueCurve.append({6000, 300});
    config.torqueCurve.append({8000, 250});
    config.torqueCurve.append({10000, 200});
    config.torqueCurve.append({12000, 180});
    config.torqueCurve.append({14000, 150});
    config.torqueCurve.append({16000, 120});

    // Single-speed transmission
    config.gearRatios.append({1, 8.0f});
    config.finalDrive = 1.0f;
    config.fuelCapacity = 0.0f; // Battery kWh would be separate

    return config;
}

// ============================================================================
// LUT operations
// ============================================================================

QVector<EngineModel::TorquePoint> EngineModel::loadPowerLut(const QString& lutPath) {
    QVector<TorquePoint> curve;

    QFile file(lutPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return curve;
    }

    QTextStream stream(&file);
    bool headerSkipped = false;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();

        if (!headerSkipped) {
            headerSkipped = true;
            if (line.startsWith(';') || line.contains('|')) {
                continue; // Skip header
            }
        }

        if (line.isEmpty() || line.startsWith(';')) continue;

        // Parse RPM|Torque format
        QStringList parts = line.split('|');
        if (parts.size() >= 2) {
            TorquePoint point;
            point.rpm = parts[0].trimmed().toFloat();
            point.torque = parts[1].trimmed().toFloat();
            point.power = point.torque * (point.rpm * 2.0f * 3.14159f / 60.0f) / 1000.0f;
            curve.append(point);
        }
    }

    file.close();
    return curve;
}

bool EngineModel::savePowerLut(const QVector<TorquePoint>& curve, const QString& lutPath) {
    QFile file(lutPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "; Power LUT - RPM|Torque(Nm)\n";
    stream << "; Generated by ksEditor\n";

    for (const TorquePoint& point : curve) {
        stream << QString::number(point.rpm, 'f', 0) << "|"
               << QString::number(point.torque, 'f', 2) << "\n";
    }

    file.close();
    return true;
}

QVector<EngineModel::TorquePoint> EngineModel::interpolateCurve(const QVector<TorquePoint>& points, int targetPoints) {
    if (points.size() < 2 || targetPoints < 2) return points;

    QVector<TorquePoint> result;
    float rpmStep = (points.last().rpm - points.first().rpm) / (targetPoints - 1);

    for (int i = 0; i < targetPoints; ++i) {
        float rpm = points.first().rpm + i * rpmStep;
        float torque = 0;

        // Find surrounding points
        for (int j = 0; j < points.size() - 1; ++j) {
            if (rpm >= points[j].rpm && rpm <= points[j+1].rpm) {
                float t = (rpm - points[j].rpm) / (points[j+1].rpm - points[j].rpm);
                torque = points[j].torque + (points[j+1].torque - points[j].torque) * t;
                break;
            }
        }

        TorquePoint point;
        point.rpm = rpm;
        point.torque = torque;
        point.power = torque * rpmToRadPerSec(rpm) / 1000.0f;
        result.append(point);
    }

    return result;
}

// ============================================================================
// Validation
// ============================================================================

bool EngineModel::validateConfig(const EngineConfig& config, QString* error) {
    if (config.peakPower <= 0 || config.peakPower > 1000.0f) {
        if (error) *error = "Peak power out of range (0-1000 kW)";
        return false;
    }

    if (config.peakTorque <= 0 || config.peakTorque > 2000.0f) {
        if (error) *error = "Peak torque out of range (0-2000 Nm)";
        return false;
    }

    if (config.maxRPM <= 0 || config.maxRPM > 20000.0f) {
        if (error) *error = "Max RPM out of range (0-20000)";
        return false;
    }

    if (config.torqueCurve.size() < 2) {
        if (error) *error = "Torque curve needs at least 2 points";
        return false;
    }

    if (config.gearRatios.isEmpty()) {
        if (error) *error = "No gear ratios defined";
        return false;
    }

    return true;
}

// ============================================================================
// Private helpers
// ============================================================================

float EngineModel::interpolateTorqueCurve(float rpm) const {
    if (m_config.torqueCurve.isEmpty()) return 0;

    // Clamp RPM
    rpm = std::max(m_config.torqueCurve.first().rpm, std::min(m_config.torqueCurve.last().rpm, rpm));

    // Find surrounding points
    for (int i = 0; i < m_config.torqueCurve.size() - 1; ++i) {
        if (rpm >= m_config.torqueCurve[i].rpm && rpm <= m_config.torqueCurve[i+1].rpm) {
            float t = (rpm - m_config.torqueCurve[i].rpm) /
                     (m_config.torqueCurve[i+1].rpm - m_config.torqueCurve[i].rpm);
            return m_config.torqueCurve[i].torque +
                   (m_config.torqueCurve[i+1].torque - m_config.torqueCurve[i].torque) * t;
        }
    }

    return m_config.torqueCurve.last().torque;
}

float EngineModel::interpolateBoostCurve(float rpm) const {
    if (m_config.turbo.boostCurve.isEmpty()) return 0;

    // Find surrounding points
    for (int i = 0; i < m_config.turbo.boostCurve.size() - 1; ++i) {
        if (rpm >= m_config.turbo.boostCurve[i].first && rpm <= m_config.turbo.boostCurve[i+1].first) {
            float t = (rpm - m_config.turbo.boostCurve[i].first) /
                     (m_config.turbo.boostCurve[i+1].first - m_config.turbo.boostCurve[i].first);
            return m_config.turbo.boostCurve[i].second +
                   (m_config.turbo.boostCurve[i+1].second - m_config.turbo.boostCurve[i].second) * t;
        }
    }

    return m_config.turbo.boostCurve.last().second;
}

// ============================================================================
// EngineModelManager implementation
// ============================================================================

void EngineModel::loadFromIni(const QString& engineIniPath) {
    EngineModelManager mgr;
    mgr.loadFromIni(engineIniPath);
    setConfig(mgr.model().getConfig());
}

EngineModelManager::EngineModelManager() {
}

void EngineModelManager::loadFromIni(const QString& engineIniPath) {
    QFile file(engineIniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    EngineModel::EngineConfig config;
    bool inEngine = false;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();

        if (line.startsWith('[') && line.endsWith(']')) {
            QString section = line.mid(1, line.length() - 2);
            inEngine = (section == "ENGINE");
            continue;
        }

        if (inEngine && line.contains('=')) {
            int eqPos = line.indexOf('=');
            QString key = line.left(eqPos).trimmed().toUpper();
            QString value = line.mid(eqPos + 1).trimmed();

            if (key == "PEAK_POWER") config.peakPower = value.toFloat();
            else if (key == "PEAK_POWER_RPM") config.peakPowerRPM = value.toFloat();
            else if (key == "PEAK_TORQUE") config.peakTorque = value.toFloat();
            else if (key == "PEAK_TORQUE_RPM") config.peakTorqueRPM = value.toFloat();
            else if (key == "MAX_RPM") config.maxRPM = value.toFloat();
            else if (key == "IDLE_RPM") config.idleRPM = value.toFloat();
            else if (key == "REV_LIMITER") config.revLimiter = value.toFloat();
            else if (key == "FUEL_CAPACITY") config.fuelCapacity = value.toFloat();
            else if (key == "FINAL_DRIVE") config.finalDrive = value.toFloat();
        }
    }

    file.close();
    m_model.setConfig(config);
}

void EngineModelManager::loadPowerLut(const QString& lutPath) {
    QVector<EngineModel::TorquePoint> curve = EngineModel::loadPowerLut(lutPath);
    EngineModel::EngineConfig config = m_model.getConfig();
    config.torqueCurve = curve;
    m_model.setConfig(config);
}

void EngineModelManager::saveToIni(const QString& engineIniPath) const {
    QFile file(engineIniPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    EngineModel::EngineConfig config = m_model.getConfig();

    QTextStream stream(&file);
    stream << "[ENGINE]\n";
    stream << "PEAK_POWER=" << config.peakPower << "\n";
    stream << "PEAK_POWER_RPM=" << config.peakPowerRPM << "\n";
    stream << "PEAK_TORQUE=" << config.peakTorque << "\n";
    stream << "PEAK_TORQUE_RPM=" << config.peakTorqueRPM << "\n";
    stream << "MAX_RPM=" << config.maxRPM << "\n";
    stream << "IDLE_RPM=" << config.idleRPM << "\n";
    stream << "REV_LIMITER=" << config.revLimiter << "\n";
    stream << "FUEL_CAPACITY=" << config.fuelCapacity << "\n";
    stream << "FINAL_DRIVE=" << config.finalDrive << "\n";

    file.close();
}

void EngineModelManager::savePowerLut(const QString& lutPath) const {
    EngineModel::savePowerLut(m_model.getConfig().torqueCurve, lutPath);
}

void EngineModelManager::update(float dt, float throttle, float load) {
    m_model.update(dt, throttle, load);
}

float EngineModelManager::getMaxPower() const {
    EngineModel::EngineConfig config = m_model.getConfig();
    float maxPower = 0;
    for (const EngineModel::TorquePoint& point : config.torqueCurve) {
        maxPower = std::max(maxPower, point.power);
    }
    return maxPower;
}

float EngineModelManager::getMaxTorque() const {
    return m_model.getConfig().peakTorque;
}

float EngineModelManager::get0100Time() const {
    // Simplified 0-100 km/h calculation
    float power = getMaxPower() * 1000.0f; // Convert to watts
    float weight = m_weight;
    float drag = 0.5f * 1.225f * m_dragCoefficient * m_frontalArea;

    // Time = mass * velocity^2 / (2 * (power - drag * velocity^2))
    float v100 = 100.0f / 3.6f; // 100 km/h in m/s
    float requiredPower = drag * v100 * v100 * v100;

    if (power > requiredPower) {
        return weight * v100 * v100 / (2.0f * (power - requiredPower));
    }

    return 10.0f; // Default if can't reach 100
}

float EngineModelManager::getTopSpeed() const {
    // Simplified top speed calculation
    float power = getMaxPower() * 1000.0f;
    float drag = 0.5f * 1.225f * m_dragCoefficient * m_frontalArea;

    // Power = drag * v^3
    // v = (power / drag)^(1/3)
    if (drag > 0) {
        float v = std::pow(power / drag, 1.0f / 3.0f);
        return v * 3.6f; // Convert to km/h
    }

    return 250.0f;
}

QVector<float> EngineModelManager::getTorqueCurve() const {
    QVector<float> curve;
    for (const EngineModel::TorquePoint& point : m_model.getConfig().torqueCurve) {
        curve.append(point.torque);
    }
    return curve;
}

QVector<float> EngineModelManager::getPowerCurve() const {
    QVector<float> curve;
    for (const EngineModel::TorquePoint& point : m_model.getConfig().torqueCurve) {
        curve.append(point.power);
    }
    return curve;
}

QMap<QString, QPair<float, float>> EngineModelManager::compareEngines(const EngineModelManager& other) const {
    QMap<QString, QPair<float, float>> comparison;

    comparison["Peak Power"] = QPair<float,float>(getMaxPower(), other.getMaxPower());
    comparison["Peak Torque"] = QPair<float,float>(getMaxTorque(), other.getMaxTorque());
    comparison["0-100 Time"] = QPair<float,float>(get0100Time(), other.get0100Time());
    comparison["Top Speed"] = QPair<float,float>(getTopSpeed(), other.getTopSpeed());
    comparison["Max RPM"] = QPair<float,float>(m_model.getConfig().maxRPM, other.m_model.getConfig().maxRPM);

    return comparison;
}
