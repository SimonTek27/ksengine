#include "PacejkaTireModel.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <cmath>
#include <algorithm>

// ============================================================================
// PacejkaTireModel implementation
// ============================================================================

PacejkaTireModel::PacejkaTireModel() {
    m_coefficients = getStreetTireCoefficients();
}

PacejkaTireModel::PacejkaTireModel(const TireCoefficients& coeffs)
    : m_coefficients(coeffs) {
}

float PacejkaTireModel::magicFormula(float x, float B, float C, float D, float E) {
    // Magic Formula: F = D * sin(C * arctan(B*x - E*(B*x - arctan(B*x))))
    float bx = B * x;
    float atan_bx = std::atan(bx);
    return D * std::sin(C * std::atan(bx - E * (bx - atan_bx)));
}

float PacejkaTireModel::calculateLateralForce(const TireState& state) const {
    float Fz = state.normalForce / 1000.0f; // Convert to kN

    // Calculate stiffness factor B
    float B = m_coefficients.a3 * std::sin(2.0f * std::atan(Fz / m_coefficients.a4))
              * (1.0f - m_coefficients.a5 * std::abs(state.camberAngle * 180.0f / 3.14159f));

    // Calculate shape factor C
    float C = m_coefficients.a1;

    // Calculate peak value D
    float D = (m_coefficients.a1 * Fz * Fz + m_coefficients.a2 * Fz);

    // Calculate curvature factor E
    float E = m_coefficients.a6 * Fz + m_coefficients.a7;

    // Calculate horizontal shift
    float Sh = m_coefficients.a8 * state.camberAngle * 180.0f / 3.14159f
               + m_coefficients.a9 * Fz + m_coefficients.a10;

    // Calculate vertical shift
    float Sv = m_coefficients.a11 * Fz * state.camberAngle * 180.0f / 3.14159f
               + m_coefficients.a12 * Fz + m_coefficients.a13;

    // Calculate lateral force
    float slipAngleDeg = state.slipAngle * 180.0f / 3.14159f;
    float Fy = magicFormula(slipAngleDeg + Sh, B, C, D, E) + Sv;

    // Apply temperature and pressure effects
    Fy *= calculateTemperatureEffect(state.tireTemp);
    Fy *= calculatePressureEffect(state.tirePressure);
    Fy *= state.frictionCoefficient;

    return Fy;
}

float PacejkaTireModel::calculateLongitudinalForce(const TireState& state) const {
    float Fz = state.normalForce / 1000.0f; // Convert to kN

    // Calculate stiffness factor B
    float B = m_coefficients.b3 * Fz * Fz + m_coefficients.b4 * Fz;

    // Calculate shape factor C
    float C = 1.65f; // Typical for longitudinal force

    // Calculate peak value D
    float D = m_coefficients.b1 * Fz * Fz + m_coefficients.b2 * Fz;

    // Calculate curvature factor E
    float E = m_coefficients.b5 * Fz * Fz + m_coefficients.b6 * Fz + m_coefficients.b7;

    // Calculate longitudinal force
    float slipRatio = state.slipRatio * 100.0f; // Convert to percentage
    float Fx = magicFormula(slipRatio, B, C, D, E);

    // Apply temperature and pressure effects
    Fx *= calculateTemperatureEffect(state.tireTemp);
    Fx *= calculatePressureEffect(state.tirePressure);
    Fx *= state.frictionCoefficient;

    return Fx;
}

float PacejkaTireModel::calculateAligningMoment(const TireState& state) const {
    float Fz = state.normalForce / 1000.0f; // Convert to kN

    // Calculate stiffness factor B
    float B = m_coefficients.c3 * Fz * Fz + m_coefficients.c4 * Fz;

    // Calculate shape factor C
    float C = m_coefficients.c1;

    // Calculate peak value D
    float D = m_coefficients.c1 * Fz * Fz + m_coefficients.c2 * Fz;

    // Calculate curvature factor E
    float E = m_coefficients.c5 * Fz * Fz + m_coefficients.c6 * Fz + m_coefficients.c7;

    // Calculate horizontal shift
    float Sh = m_coefficients.c8 * state.camberAngle * 180.0f / 3.14159f
               + m_coefficients.c9 * Fz + m_coefficients.c10;

    // Calculate vertical shift
    float Sv = m_coefficients.c11 * Fz * state.camberAngle * 180.0f / 3.14159f
               + m_coefficients.c12 * Fz + m_coefficients.c13;

    // Calculate aligning moment
    float slipAngleDeg = state.slipAngle * 180.0f / 3.14159f;
    float Mz = magicFormula(slipAngleDeg + Sh, B, C, D, E) + Sv;

    return Mz;
}

PacejkaTireModel::TireForces PacejkaTireModel::calculateForces(const TireState& state) const {
    TireForces forces;

    forces.lateralForce = calculateLateralForce(state);
    forces.longitudinalForce = calculateLongitudinalForce(state);
    forces.aligningMoment = calculateAligningMoment(state);
    forces.slipAngleDeg = radToDeg(state.slipAngle);
    forces.slipRatioPercent = state.slipRatio * 100.0f;

    return forces;
}

PacejkaTireModel::TireForces PacejkaTireModel::calculateCombinedSlip(
    float slipAngle, float slipRatio, float normalForce, float camber) const {

    TireState state;
    state.slipAngle = slipAngle;
    state.slipRatio = slipRatio;
    state.normalForce = normalForce;
    state.camberAngle = camber;
    state.tireTemp = 80.0f;
    state.tirePressure = 26.0f;
    state.frictionCoefficient = 1.0f;

    TireForces forces = calculateForces(state);

    // Combined slip correction (simplified)
    float slipMagnitude = std::sqrt(slipAngle * slipAngle + slipRatio * slipRatio);
    if (slipMagnitude > 0.001f) {
        float cosAngle = slipAngle / slipMagnitude;
        float sinAngle = slipRatio / slipMagnitude;

        float totalForce = std::sqrt(forces.lateralForce * forces.lateralForce +
                                     forces.longitudinalForce * forces.longitudinalForce);

        forces.lateralForce = totalForce * cosAngle;
        forces.longitudinalForce = totalForce * sinAngle;
    }

    return forces;
}

float PacejkaTireModel::calculateLoadSensitivity(float normalForce) const {
    // Load sensitivity: grip decreases with increasing load
    // Typical AC behavior: ~0.85 at 8000N, ~0.95 at 2000N
    float referenceLoad = 4000.0f; // N
    float sensitivity = 1.0f - 0.15f * (normalForce - referenceLoad) / referenceLoad;
    return std::max(0.7f, std::min(1.2f, sensitivity));
}

QVector<QPair<float, float>> PacejkaTireModel::generateLateralCurve(
    float maxSlipAngle, float normalForce, int points) const {

    QVector<QPair<float, float>> curve;

    for (int i = 0; i <= points; ++i) {
        float slipAngle = -maxSlipAngle + (2.0f * maxSlipAngle * i / points);
        float slipAngleRad = degToRad(slipAngle);

        TireState state;
        state.slipAngle = slipAngleRad;
        state.normalForce = normalForce;

        float force = calculateLateralForce(state);
        curve.append(QPair<float,float>(slipAngle, force));
    }

    return curve;
}

QVector<QPair<float, float>> PacejkaTireModel::generateLongitudinalCurve(
    float maxSlipRatio, float normalForce, int points) const {

    QVector<QPair<float, float>> curve;

    for (int i = 0; i <= points; ++i) {
        float slipRatio = -maxSlipRatio + (2.0f * maxSlipRatio * i / points);

        TireState state;
        state.slipRatio = slipRatio;
        state.normalForce = normalForce;

        float force = calculateLongitudinalForce(state);
        curve.append(QPair<float,float>(slipRatio * 100.0f, force));
    }

    return curve;
}

float PacejkaTireModel::calculatePeakLateralGrip(float normalForce) const {
    TireState state;
    state.normalForce = normalForce;
    state.tireTemp = 80.0f;
    state.tirePressure = 26.0f;

    // Find peak force by testing slip angles
    float maxForce = 0;
    for (float slipAngle = 0; slipAngle <= 20.0f; slipAngle += 0.5f) {
        state.slipAngle = degToRad(slipAngle);
        float force = std::abs(calculateLateralForce(state));
        maxForce = std::max(maxForce, force);
    }

    return maxForce / normalForce; // G-force
}

float PacejkaTireModel::calculatePeakLongitudinalGrip(float normalForce) const {
    TireState state;
    state.normalForce = normalForce;
    state.tireTemp = 80.0f;
    state.tirePressure = 26.0f;

    // Find peak force by testing slip ratios
    float maxForce = 0;
    for (float slipRatio = 0; slipRatio <= 0.3f; slipRatio += 0.01f) {
        state.slipRatio = slipRatio;
        float force = std::abs(calculateLongitudinalForce(state));
        maxForce = std::max(maxForce, force);
    }

    return maxForce / normalForce; // G-force
}

float PacejkaTireModel::calculateTemperatureEffect(float temp) const {
    // Optimal temperature around 80-100°C
    // Grip drops off significantly below 40°C and above 120°C
    float optimalTemp = 90.0f;
    float tempDiff = std::abs(temp - optimalTemp);

    if (tempDiff < 20.0f) {
        return 1.0f;
    } else if (temp < 40.0f) {
        return 0.7f + 0.3f * (temp / 40.0f);
    } else if (temp > 120.0f) {
        return 1.0f - 0.3f * ((temp - 120.0f) / 80.0f);
    } else {
        return 1.0f - 0.1f * (tempDiff - 20.0f) / 80.0f;
    }
}

float PacejkaTireModel::calculatePressureEffect(float pressure) const {
    // Optimal pressure around 26 PSI for most AC tires
    float optimalPressure = 26.0f;
    float pressureDiff = std::abs(pressure - optimalPressure);

    if (pressureDiff < 2.0f) {
        return 1.0f;
    } else {
        return 1.0f - 0.05f * (pressureDiff - 2.0f) / 10.0f;
    }
}

float PacejkaTireModel::calculateWearEffect(float wear) const {
    // Wear: 0 = new, 1 = completely worn
    // Grip decreases linearly with wear
    return 1.0f - 0.3f * wear;
}

PacejkaTireModel::TireCoefficients PacejkaTireModel::getStreetTireCoefficients() {
    TireCoefficients coeffs;
    // Default street tire values
    return coeffs;
}

PacejkaTireModel::TireCoefficients PacejkaTireModel::getSlickTireCoefficients() {
    TireCoefficients coeffs;
    // Slick tire: higher grip, stiffer
    coeffs.a1 = -20.0f;
    coeffs.a2 = 1100.0f;
    coeffs.a3 = 1200.0f;
    coeffs.a4 = 1.7f;
    coeffs.a5 = 0.18f;
    coeffs.b1 = -19.0f;
    coeffs.b2 = 1200.0f;
    return coeffs;
}

PacejkaTireModel::TireCoefficients PacejkaTireModel::getWetTireCoefficients() {
    TireCoefficients coeffs;
    // Wet tire: lower grip, more compliant
    coeffs.a1 = -25.0f;
    coeffs.a2 = 900.0f;
    coeffs.a3 = 950.0f;
    coeffs.a4 = 1.9f;
    coeffs.a5 = 0.22f;
    coeffs.b1 = -24.0f;
    coeffs.b2 = 1000.0f;
    return coeffs;
}

PacejkaTireModel::TireCoefficients PacejkaTireModel::getRallyTireCoefficients() {
    TireCoefficients coeffs;
    // Rally tire: good on loose surfaces
    coeffs.a1 = -23.0f;
    coeffs.a2 = 950.0f;
    coeffs.a3 = 1000.0f;
    coeffs.a4 = 1.85f;
    coeffs.a5 = 0.20f;
    coeffs.b1 = -22.0f;
    coeffs.b2 = 1050.0f;
    return coeffs;
}

bool PacejkaTireModel::validateCoefficients(const TireCoefficients& coeffs, QString* error) {
    // Basic validation
    if (coeffs.a4 <= 0) {
        if (error) *error = "a4 (peak slip angle) must be positive";
        return false;
    }

    if (coeffs.a1 >= 0) {
        if (error) *error = "a1 (shape factor) should be negative for lateral force";
        return false;
    }

    return true;
}

// ============================================================================
// TireModelManager implementation
// ============================================================================

TireModelManager::TireModelManager() {
}

void TireModelManager::setTireCompound(int compound) {
    m_compound = compound;

    PacejkaTireModel::TireCoefficients coeffs;
    switch (compound) {
        case 0: // Street
            coeffs = PacejkaTireModel::getStreetTireCoefficients();
            break;
        case 1: // Slick
            coeffs = PacejkaTireModel::getSlickTireCoefficients();
            break;
        case 2: // Wet
            coeffs = PacejkaTireModel::getWetTireCoefficients();
            break;
        case 3: // Rally
            coeffs = PacejkaTireModel::getRallyTireCoefficients();
            break;
        default:
            coeffs = PacejkaTireModel::getStreetTireCoefficients();
            break;
    }

    for (int i = 0; i < 4; ++i) {
        m_models[i].setCoefficients(coeffs);
    }
}

void TireModelManager::setTirePressure(float frontPressure, float rearPressure) {
    m_frontPressure = frontPressure;
    m_rearPressure = rearPressure;
}

void TireModelManager::loadFromIni(const QString& tireIniPath) {
    QFile file(tireIniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    QString currentSection;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();

        if (line.startsWith('[') && line.endsWith(']')) {
            currentSection = line.mid(1, line.length() - 2);
            continue;
        }

        if (line.contains('=')) {
            int eqPos = line.indexOf('=');
            QString key = line.left(eqPos).trimmed();
            QString value = line.mid(eqPos + 1).trimmed();

            if (currentSection == "TYRES") {
                if (key == "PRESSURE") {
                    m_frontPressure = value.toFloat();
                    m_rearPressure = value.toFloat();
                }
            }
        }
    }

    file.close();
}

void TireModelManager::saveToIni(const QString& tireIniPath) const {
    QFile file(tireIniPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream << "[TYRES]\n";
    stream << "FRONT_PRESSURE=" << m_frontPressure << "\n";
    stream << "REAR_PRESSURE=" << m_rearPressure << "\n";

    file.close();
}

QVector<float> TireModelManager::calculateGripCircle(int wheel, float normalForce) const {
    QVector<float> gripCircle;

    for (int angle = 0; angle <= 360; angle += 10) {
        float lateralComponent = std::cos(angle * 3.14159f / 180.0f);
        float longitudinalComponent = std::sin(angle * 3.14159f / 180.0f);

        PacejkaTireModel::TireState state;
        state.normalForce = normalForce;
        state.slipAngle = std::atan2(lateralComponent, longitudinalComponent) * 0.1f;
        state.slipRatio = std::sqrt(lateralComponent * lateralComponent +
                                    longitudinalComponent * longitudinalComponent) * 0.1f;

        PacejkaTireModel::TireForces forces = m_models[wheel].calculateForces(state);
        float totalForce = std::sqrt(forces.lateralForce * forces.lateralForce +
                                     forces.longitudinalForce * forces.longitudinalForce);

        gripCircle.append(totalForce / normalForce);
    }

    return gripCircle;
}

QVector<float> TireModelManager::calculateSlipCurve(int wheel, float normalForce) const {
    QVector<float> slipCurve;

    for (float slip = -0.2f; slip <= 0.2f; slip += 0.01f) {
        PacejkaTireModel::TireState state;
        state.normalForce = normalForce;
        state.slipAngle = slip;

        float force = m_models[wheel].calculateLateralForce(state);
        slipCurve.append(force / normalForce);
    }

    return slipCurve;
}

float TireModelManager::estimateLapTimeImpact(int wheel, float slipAngleChange) const {
    // Rough estimation: 0.1s per degree of slip angle improvement
    return slipAngleChange * 0.1f;
}
