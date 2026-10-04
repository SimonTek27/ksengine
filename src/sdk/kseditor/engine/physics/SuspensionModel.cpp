#include "SuspensionModel.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <cmath>
#include <algorithm>

// ============================================================================
// SuspensionModel implementation
// ============================================================================

void SuspensionModel::update(float dt, float chassisVerticalVelocity, float load) {
    // Update state based on chassis movement
    m_state.velocity = chassisVerticalVelocity;

    // Calculate forces
    m_state.force = calculateTotalForce(m_state.compression, m_state.velocity);

    // Update compression based on forces and load
    float netForce = load - m_state.force;
    float acceleration = netForce / 100.0f; // Simplified mass
    m_state.compression += m_state.velocity * dt;

    // Clamp compression
    float maxCompression = m_spring.maxLength - m_spring.minLength;
    m_state.compression = std::max(-maxCompression * 0.5f, std::min(maxCompression * 0.5f, m_state.compression));

    // Update ride height
    m_state.rideHeight = m_spring.maxLength - m_state.compression;

    // Calculate camber change due to compression
    m_state.camber = calculateCamberGain(m_state.compression);

    // Calculate roll center
    m_state.rollCenterHeight = calculateRollCenterHeight(m_state.compression);
}

void SuspensionModel::reset() {
    m_state = SuspensionState();
}

void SuspensionModel::setSpringConfig(const SpringConfig& config) {
    m_spring = config;
}

void SuspensionModel::setDamperConfig(const DamperConfig& config) {
    m_damper = config;
}

void SuspensionModel::setGeometryConfig(const GeometryConfig& config) {
    m_geometry = config;
}

float SuspensionModel::calculateSpringForce(float compression) const {
    // F = k * x
    float force = m_spring.rate * compression;

    // Add preload
    force += m_spring.preload * m_spring.rate;

    return force;
}

float SuspensionModel::calculateDamperForce(float velocity) const {
    float absVelocity = std::abs(velocity);

    if (velocity > 0) {
        // Bump (compression)
        if (absVelocity > m_damper.bumpThreshold) {
            return m_damper.fastBumpRate * velocity;
        }
        return m_damper.bumpRate * velocity;
    } else {
        // Rebound (extension)
        if (absVelocity > m_damper.reboundThreshold) {
            return m_damper.fastReboundRate * velocity;
        }
        return m_damper.reboundRate * velocity;
    }
}

float SuspensionModel::calculateBumpStopForce(float compression) const {
    if (compression > (m_spring.maxLength - m_spring.minLength - m_spring.bumpStopGap)) {
        float overTravel = compression - (m_spring.maxLength - m_spring.minLength - m_spring.bumpStopGap);
        return m_spring.bumpStopRate * overTravel;
    }
    return 0.0f;
}

float SuspensionModel::calculateTotalForce(float compression, float velocity) const {
    float springForce = calculateSpringForce(compression);
    float damperForce = m_damper.damperCurve.isEmpty() ? calculateDamperForce(velocity) : damperCurveForce(velocity);
    float bumpStopForce = calculateBumpStopForce(compression);
    return springForce + damperForce + bumpStopForce;
}

static float lutCurve(const QVector<QPair<float,float>>& c, float x) {
    if (c.isEmpty()) return 0.0f;
    if (x <= c.first().first) return c.first().second;
    if (x >= c.last().first) return c.last().second;
    for (int i = 0; i + 1 < c.size(); ++i) {
        if (x >= c[i].first && x <= c[i+1].first) {
            float d = c[i+1].first - c[i].first;
            float t = d < 1e-6f ? 0.0f : (x - c[i].first) / d;
            return c[i].second + t * (c[i+1].second - c[i].second);
        }
    }
    return 0.0f;
}

float SuspensionModel::damperCurveForce(float velocity) const {
    return lutCurve(m_damper.damperCurve, velocity);
}
float SuspensionModel::bumpSteerAngle(float travel) const {
    if (m_damper.bumpSteerCurve.isEmpty()) return travel * 2.0f;
    return lutCurve(m_damper.bumpSteerCurve, travel);
}
float SuspensionModel::camberFromCurve(float travel) const {
    if (m_damper.camberCurve.isEmpty()) return calculateCamberGain(travel);
    return lutCurve(m_damper.camberCurve, travel);
}
float SuspensionModel::antiRollBarForce(float leftTravel, float rightTravel) const {
    if (!m_damper.antiRollBarEnabled) return 0.0f;
    return (rightTravel - leftTravel) * m_damper.antiRollBarStiffness * 0.5f;
}

float SuspensionModel::calculateCamberGain(float compression) const {
    // Camber gain depends on suspension geometry
    // Simplified: camber changes with compression
    float baseCamber = -1.0f; // Static camber
    float gainFactor = 0.1f; // Camber change per meter of compression

    return baseCamber + compression * gainFactor * 57.2958f; // Convert to degrees
}

float SuspensionModel::calculateRollCenterHeight(float compression) const {
    // Roll center height changes with suspension geometry
    float baseHeight = 0.05f;
    float compressionFactor = 0.2f;

    return baseHeight + compression * compressionFactor;
}

float SuspensionModel::calculateMotionRatio() const {
    // Motion ratio = wheel travel / spring compression
    // Calculated from suspension geometry:
    // For double wishbone: ratio of spring mounting arm to wheel center arm
    float upperArmLength = std::sqrt(
        m_geometry.frontUpperArm[0] * m_geometry.frontUpperArm[0] +
        m_geometry.frontUpperArm[1] * m_geometry.frontUpperArm[1] +
        m_geometry.frontUpperArm[2] * m_geometry.frontUpperArm[2]);
    float lowerArmLength = std::sqrt(
        m_geometry.frontLowerArm[0] * m_geometry.frontLowerArm[0] +
        m_geometry.frontLowerArm[1] * m_geometry.frontLowerArm[1] +
        m_geometry.frontLowerArm[2] * m_geometry.frontLowerArm[2]);

    // Motion ratio ≈ lower arm length / (upper arm length + lower arm length)
    // Clamp to typical range [0.5, 1.0]
    float total = upperArmLength + lowerArmLength;
    if (total < 0.001f) return 0.8f;
    float ratio = lowerArmLength / total;
    return qBound(0.5f, ratio, 1.0f);
}

float SuspensionModel::calculateWheelRate() const {
    // Wheel rate = spring rate * motion ratio^2
    float motionRatio = calculateMotionRatio();
    return m_spring.rate * motionRatio * motionRatio;
}

// ============================================================================
// Presets
// ============================================================================

SuspensionModel::SpringConfig SuspensionModel::getStreetSpring() {
    SpringConfig config;
    config.rate = 15000.0f;
    config.preload = 0.0f;
    config.minLength = 0.30f;
    config.maxLength = 0.50f;
    config.bumpStopRate = 50000.0f;
    config.bumpStopGap = 0.02f;
    return config;
}

SuspensionModel::SpringConfig SuspensionModel::getSportSpring() {
    SpringConfig config;
    config.rate = 25000.0f;
    config.preload = 0.005f;
    config.minLength = 0.28f;
    config.maxLength = 0.48f;
    config.bumpStopRate = 60000.0f;
    config.bumpStopGap = 0.015f;
    return config;
}

SuspensionModel::SpringConfig SuspensionModel::getRaceSpring() {
    SpringConfig config;
    config.rate = 40000.0f;
    config.preload = 0.01f;
    config.minLength = 0.25f;
    config.maxLength = 0.45f;
    config.bumpStopRate = 80000.0f;
    config.bumpStopGap = 0.01f;
    return config;
}

SuspensionModel::SpringConfig SuspensionModel::getSoftSpring() {
    SpringConfig config;
    config.rate = 10000.0f;
    config.preload = 0.0f;
    config.minLength = 0.32f;
    config.maxLength = 0.52f;
    config.bumpStopRate = 40000.0f;
    config.bumpStopGap = 0.025f;
    return config;
}

SuspensionModel::DamperConfig SuspensionModel::getStreetDamper() {
    DamperConfig config;
    config.bumpRate = 1500.0f;
    config.reboundRate = 3000.0f;
    config.fastBumpRate = 1200.0f;
    config.fastReboundRate = 2500.0f;
    config.bumpThreshold = 0.05f;
    config.reboundThreshold = 0.05f;
    return config;
}

SuspensionModel::DamperConfig SuspensionModel::getSportDamper() {
    DamperConfig config;
    config.bumpRate = 2500.0f;
    config.reboundRate = 5000.0f;
    config.fastBumpRate = 2000.0f;
    config.fastReboundRate = 4000.0f;
    config.bumpThreshold = 0.04f;
    config.reboundThreshold = 0.04f;
    return config;
}

SuspensionModel::DamperConfig SuspensionModel::getRaceDamper() {
    DamperConfig config;
    config.bumpRate = 4000.0f;
    config.reboundRate = 8000.0f;
    config.fastBumpRate = 3000.0f;
    config.fastReboundRate = 6000.0f;
    config.bumpThreshold = 0.03f;
    config.reboundThreshold = 0.03f;
    return config;
}

// ============================================================================
// Validation
// ============================================================================

bool SuspensionModel::validateConfig(const SpringConfig& spring, const DamperConfig& damper,
                                      QString* error) {
    if (spring.rate <= 0 || spring.rate > 100000.0f) {
        if (error) *error = "Spring rate out of range";
        return false;
    }

    if (damper.bumpRate <= 0 || damper.reboundRate <= 0) {
        if (error) *error = "Damper rates must be positive";
        return false;
    }

    return true;
}

// ============================================================================
// AntiRollBar implementation
// ============================================================================

float AntiRollBar::calculateForce(float leftCompression, float rightCompression) const {
    if (!m_config.enabled) return 0.0f;

    float diff = leftCompression - rightCompression;
    return m_config.stiffness * diff;
}

// ============================================================================
// SuspensionModelManager implementation
// ============================================================================

void SuspensionModel::loadFromIni(const QString& iniPath) {
    Q_UNUSED(iniPath);
    // Stub - parsing handled by SuspensionModelManager
}

SuspensionModelManager::SuspensionModelManager() {
}

void SuspensionModelManager::loadFromIni(const QString& suspensionIniPath) {
    QFile file(suspensionIniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    SuspensionModel::SpringConfig frontSpring;
    SuspensionModel::SpringConfig rearSpring;
    SuspensionModel::DamperConfig frontDamper;
    SuspensionModel::DamperConfig rearDamper;

    QString currentSection;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();

        if (line.startsWith('[') && line.endsWith(']')) {
            currentSection = line.mid(1, line.length() - 2);
            continue;
        }

        if (line.contains('=')) {
            int eqPos = line.indexOf('=');
            QString key = line.left(eqPos).trimmed().toUpper();
            QString value = line.mid(eqPos + 1).trimmed();

            if (currentSection.contains("FRONT")) {
                if (key == "SPRING_RATE") frontSpring.rate = value.toFloat();
                else if (key == "DAMP_BUMP") frontDamper.bumpRate = value.toFloat();
                else if (key == "DAMP_REBOUND") frontDamper.reboundRate = value.toFloat();
            } else if (currentSection.contains("REAR")) {
                if (key == "SPRING_RATE") rearSpring.rate = value.toFloat();
                else if (key == "DAMP_BUMP") rearDamper.bumpRate = value.toFloat();
                else if (key == "DAMP_REBOUND") rearDamper.reboundRate = value.toFloat();
            } else if (key == "FRONT_ARB") {
                m_frontARB.setConfig({value.toFloat(), true});
            } else if (key == "REAR_ARB") {
                m_rearARB.setConfig({value.toFloat(), true});
            }
        }
    }

    file.close();

    // Apply configs to wheels
    for (int i = 0; i < 2; ++i) {
        m_wheels[i].setSpringConfig(frontSpring);
        m_wheels[i].setDamperConfig(frontDamper);
    }
    for (int i = 2; i < 4; ++i) {
        m_wheels[i].setSpringConfig(rearSpring);
        m_wheels[i].setDamperConfig(rearDamper);
    }
}

void SuspensionModelManager::saveToIni(const QString& suspensionIniPath) const {
    QFile file(suspensionIniPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream << "[SUSPENSION]\n";
    stream << "FRONT_SPRING_RATE=" << m_wheels[0].getState().force << "\n";
    stream << "REAR_SPRING_RATE=" << m_wheels[2].getState().force << "\n";
    stream << "FRONT_ARB=" << m_frontARB.getConfig().stiffness << "\n";
    stream << "REAR_ARB=" << m_rearARB.getConfig().stiffness << "\n";

    file.close();
}

void SuspensionModelManager::update(float dt, float chassisVerticalVelocity, float leftLoad, float rightLoad) {
    // Update front wheels
    m_wheels[0].update(dt, chassisVerticalVelocity, leftLoad);
    m_wheels[1].update(dt, chassisVerticalVelocity, leftLoad);

    // Update rear wheels
    m_wheels[2].update(dt, chassisVerticalVelocity, rightLoad);
    m_wheels[3].update(dt, chassisVerticalVelocity, rightLoad);

    // Apply ARB forces
    float frontARBForce = m_frontARB.calculateForce(m_wheels[0].getCompression(), m_wheels[1].getCompression());
    float rearARBForce = m_rearARB.calculateForce(m_wheels[2].getCompression(), m_wheels[3].getCompression());
}

float SuspensionModelManager::getRideHeightFront() const {
    return (m_wheels[0].getRideHeight() + m_wheels[1].getRideHeight()) / 2.0f;
}

float SuspensionModelManager::getRideHeightRear() const {
    return (m_wheels[2].getRideHeight() + m_wheels[3].getRideHeight()) / 2.0f;
}

float SuspensionModelManager::getCamberFront() const {
    return (m_wheels[0].getCamber() + m_wheels[1].getCamber()) / 2.0f;
}

float SuspensionModelManager::getCamberRear() const {
    return (m_wheels[2].getCamber() + m_wheels[3].getCamber()) / 2.0f;
}

float SuspensionModelManager::getWheelRate(int wheel) const {
    if (wheel < 0 || wheel > 3) return 0;
    return m_wheels[wheel].calculateWheelRate();
}

QMap<QString, QPair<float, float>> SuspensionModelManager::compareSuspension(
    const SuspensionModelManager& other) const {

    QMap<QString, QPair<float, float>> comparison;

    comparison["Front Ride Height"] = QPair<float,float>(getRideHeightFront(), other.getRideHeightFront());
    comparison["Rear Ride Height"] = QPair<float,float>(getRideHeightRear(), other.getRideHeightRear());
    comparison["Front Camber"] = QPair<float,float>(getCamberFront(), other.getCamberFront());
    comparison["Rear Camber"] = QPair<float,float>(getCamberRear(), other.getCamberRear());
    comparison["Front Wheel Rate"] = QPair<float,float>(getWheelRate(0), other.getWheelRate(0));
    comparison["Rear Wheel Rate"] = QPair<float,float>(getWheelRate(2), other.getWheelRate(2));

    return comparison;
}
