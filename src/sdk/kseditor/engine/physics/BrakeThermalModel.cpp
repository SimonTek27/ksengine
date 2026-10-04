#include "BrakeThermalModel.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <cmath>
#include <algorithm>

// ============================================================================
// BrakeThermalModel implementation
// ============================================================================

void BrakeThermalModel::update(float dt, const BrakeState& state) {
    m_dt = dt;
    m_state = state;

    // Calculate brake power (heat generation)
    float brakePower = calculateBrakePower(state.brakeTorque, state.discAngularVelocity);
    m_dynamics.heatGeneration = brakePower;

    // Calculate heat dissipation
    float discTempK = celsiusToKelvin(m_disc.discTemp);
    m_dynamics.convectiveCooling = calculateConvectiveCooling(discTempK, m_airflow);
    m_dynamics.radiationCooling = calculateRadiationCooling(discTempK);
    m_dynamics.conductionToHub = calculateConductionCooling(discTempK);
    m_dynamics.heatDissipation = m_dynamics.convectiveCooling + m_dynamics.radiationCooling + m_dynamics.conductionToHub;

    // Net heat flow
    m_dynamics.netHeatFlow = brakePower - m_dynamics.heatDissipation;

    // Update disc temperature
    float discMass = m_disc.discDensity * 3.14159f *
                     (m_disc.discRadius * m_disc.discRadius - m_disc.discInnerRadius * m_disc.discInnerRadius) *
                     m_disc.discThickness;

    if (discMass > 0 && m_disc.discSpecificHeat > 0) {
        m_disc.discTemp += (m_dynamics.netHeatFlow * dt) / (discMass * m_disc.discSpecificHeat);
    }

    // Update pad temperature (simplified - follows disc with delay)
    float padTempTarget = m_disc.discTemp * 0.9f + state.pedalPressure * 100.0f;
    m_disc.padTemp += (padTempTarget - m_disc.padTemp) * 0.1f * dt;

    // Clamp temperatures
    m_disc.discTemp = std::max(m_disc.discTempMin, std::min(m_disc.discTempMax, m_disc.discTemp));
    m_disc.padTemp = std::max(m_disc.padTempMin, std::min(m_disc.padTempMax, m_disc.padTemp));

    // Update wear
    float heatEnergy = brakePower * dt;
    m_disc.discWear += calculateDiscWear(heatEnergy);
    m_disc.padWear += calculatePadWear(heatEnergy);

    m_disc.discWear = std::min(1.0f, m_disc.discWear);
    m_disc.padWear = std::min(1.0f, m_disc.padWear);
}

void BrakeThermalModel::reset() {
    m_disc.discTemp = 200.0f;
    m_disc.padTemp = 200.0f;
    m_disc.discWear = 0.0f;
    m_disc.padWear = 0.0f;
    m_state = BrakeState();
    m_dynamics = ThermalDynamics();
}

void BrakeThermalModel::setBrakeBias(float bias) {
    m_disc.biasFactor = std::max(0.3f, std::min(0.7f, bias));
}

void BrakeThermalModel::setDiscProperties(float radius, float thickness) {
    m_disc.discRadius = radius;
    m_disc.discThickness = thickness;
}

void BrakeThermalModel::setPadProperties(float area, float thickness) {
    m_disc.padArea = area;
    m_disc.padThickness = thickness;
}

float BrakeThermalModel::getDiscTempNormalized() const {
    return (m_disc.discTemp - m_disc.discTempMin) / (m_disc.discTempMax - m_disc.discTempMin);
}

float BrakeThermalModel::getPadTempNormalized() const {
    return (m_disc.padTemp - m_disc.padTempMin) / (m_disc.padTempMax - m_disc.padTempMin);
}

float BrakeThermalModel::calculateBrakePower(float brakeTorque, float angularVelocity) const {
    // Power = Torque * Angular Velocity
    return std::abs(brakeTorque * angularVelocity);
}

float BrakeThermalModel::calculateHeatFlux(float power) const {
    // Heat flux = Power / Contact Area
    float contactArea = m_disc.padArea * 2; // Two pads
    return (contactArea > 0) ? power / contactArea : 0;
}

float BrakeThermalModel::calculateConvectiveCooling(float temp, float airflow) const {
    // Convective cooling: Q = h * A * (T - T_ambient)
    float h = 50.0f + airflow * 10.0f; // Heat transfer coefficient
    float A = 2.0f * 3.14159f * m_disc.discRadius * m_disc.discThickness * 2; // Both sides
    return h * A * (temp - celsiusToKelvin(m_ambientTemp));
}

float BrakeThermalModel::calculateRadiationCooling(float temp) const {
    // Radiation cooling: Q = epsilon * sigma * A * (T^4 - T_amb^4)
    float epsilon = 0.8f; // Emissivity
    float sigma = 5.67e-8f; // Stefan-Boltzmann constant
    float A = 2.0f * 3.14159f * m_disc.discRadius * m_disc.discThickness * 2;

    return epsilon * sigma * A * (std::pow(temp, 4) - std::pow(celsiusToKelvin(m_ambientTemp), 4));
}

float BrakeThermalModel::calculateConductionCooling(float temp) const {
    // Conduction to hub: simplified
    float h = 100.0f;
    float A = 3.14159f * m_disc.discInnerRadius * m_disc.discInnerRadius;
    return h * A * (temp - celsiusToKelvin(m_ambientTemp)) * 0.1f;
}

float BrakeThermalModel::calculateDiscWear(float heatEnergy) const {
    // Wear increases with temperature
    float tempFactor = 1.0f;
    if (m_disc.discTemp > 600.0f) {
        tempFactor = 1.0f + (m_disc.discTemp - 600.0f) / 400.0f;
    }
    return m_disc.wearRate * heatEnergy * tempFactor;
}

float BrakeThermalModel::calculatePadWear(float heatEnergy) const {
    // Pad wear increases significantly with temperature
    float tempFactor = 1.0f;
    if (m_disc.padTemp > 400.0f) {
        tempFactor = 1.0f + (m_disc.padTemp - 400.0f) / 300.0f;
    }
    return m_disc.wearRate * 2.0f * heatEnergy * tempFactor;
}

float BrakeThermalModel::calculateBrakeFade() const {
    // Fade factor: 1.0 = no fade, 0.0 = complete fade
    return calculateFadeFactor(m_disc.discTemp) * calculateFadeFactor(m_disc.padTemp);
}

float BrakeThermalModel::calculateFadeFactor(float temperature) const {
    if (temperature < 400.0f) {
        return 1.0f;
    } else if (temperature < 800.0f) {
        return 1.0f - (temperature - 400.0f) / 1000.0f;
    } else {
        return 0.6f - (temperature - 800.0f) / 2000.0f;
    }
}

float BrakeThermalModel::calculateFrictionCoefficient() const {
    // Base friction coefficient
    float mu = 0.4f;

    // Temperature effect
    if (m_disc.discTemp < 200.0f) {
        mu *= 0.8f; // Cold brakes
    } else if (m_disc.discTemp < 600.0f) {
        mu *= 1.0f; // Optimal range
    } else {
        mu *= 0.7f; // Overheated
    }

    // Wear effect
    mu *= (1.0f - m_disc.padWear * 0.3f);

    // Fade effect
    mu *= calculateBrakeFade();

    return mu;
}

float BrakeThermalModel::getFrictionAtTemperature(float temp) const {
    float mu = 0.4f;
    if (temp < 200.0f) {
        mu *= 0.8f;
    } else if (temp < 600.0f) {
        mu *= 1.0f;
    } else {
        mu *= 0.7f;
    }
    return mu;
}

// ============================================================================
// Presets
// ============================================================================

BrakeThermalModel::BrakeComponent BrakeThermalModel::getIronDisc() {
    BrakeComponent disc;
    disc.discRadius = 0.14f;
    disc.discInnerRadius = 0.05f;
    disc.discThickness = 0.028f;
    disc.discDensity = 7200.0f;
    disc.discSpecificHeat = 460.0f;
    disc.discThermalConductivity = 40.0f;
    return disc;
}

BrakeThermalModel::BrakeComponent BrakeThermalModel::getCarbonDisc() {
    BrakeComponent disc;
    disc.discRadius = 0.14f;
    disc.discInnerRadius = 0.05f;
    disc.discThickness = 0.025f;
    disc.discDensity = 1800.0f;
    disc.discSpecificHeat = 710.0f;
    disc.discThermalConductivity = 10.0f;
    disc.discTempMin = 200.0f;
    disc.discTempMax = 1200.0f;
    return disc;
}

BrakeThermalModel::BrakeComponent BrakeThermalModel::getCeramicDisc() {
    BrakeComponent disc;
    disc.discRadius = 0.14f;
    disc.discInnerRadius = 0.05f;
    disc.discThickness = 0.022f;
    disc.discDensity = 2200.0f;
    disc.discSpecificHeat = 800.0f;
    disc.discThermalConductivity = 5.0f;
    disc.discTempMin = 300.0f;
    disc.discTempMax = 1400.0f;
    return disc;
}

// ============================================================================
// Validation
// ============================================================================

bool BrakeThermalModel::validate(QString* error) const {
    if (m_disc.discRadius <= 0 || m_disc.discRadius > 0.5f) {
        if (error) *error = "Disc radius out of range (0-0.5m)";
        return false;
    }

    if (m_disc.discThickness <= 0 || m_disc.discThickness > 0.1f) {
        if (error) *error = "Disc thickness out of range (0-0.1m)";
        return false;
    }

    if (m_disc.padArea <= 0 || m_disc.padArea > 0.01f) {
        if (error) *error = "Pad area out of range (0-0.01m^2)";
        return false;
    }

    return true;
}

// ============================================================================
// INI Configuration
// ============================================================================

void BrakeThermalModel::loadFromIni(const QString& brakeIniPath) {
    QFile file(brakeIniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('[') || line.startsWith(';')) continue;

        int eq = line.indexOf('=');
        if (eq < 0) continue;
        QString key = line.left(eq).trimmed().toLower();
        QString val = line.mid(eq + 1).trimmed();

        if (key == "disc_radius") m_disc.discRadius = val.toFloat();
        else if (key == "disc_inner_radius") m_disc.discInnerRadius = val.toFloat();
        else if (key == "disc_thickness") m_disc.discThickness = val.toFloat();
        else if (key == "pad_area") m_disc.padArea = val.toFloat();
        else if (key == "pad_thickness") m_disc.padThickness = val.toFloat();
        else if (key == "disc_density") m_disc.discDensity = val.toFloat();
        else if (key == "disc_specific_heat") m_disc.discSpecificHeat = val.toFloat();
        else if (key == "pad_density") m_disc.padDensity = val.toFloat();
        else if (key == "pad_specific_heat") m_disc.padSpecificHeat = val.toFloat();
        else if (key == "disc_temp_min") m_disc.discTempMin = val.toFloat();
        else if (key == "disc_temp_max") m_disc.discTempMax = val.toFloat();
        else if (key == "pad_temp_min") m_disc.padTempMin = val.toFloat();
        else if (key == "pad_temp_max") m_disc.padTempMax = val.toFloat();
        else if (key == "wear_rate") m_disc.wearRate = val.toFloat();
        else if (key == "bias_factor") m_disc.biasFactor = val.toFloat();
    }
}

// ============================================================================
// BrakeModelManager implementation
// ============================================================================

BrakeModelManager::BrakeModelManager() {
}

void BrakeModelManager::loadFromIni(const QString& brakeIniPath) {
    QFile file(brakeIniPath);
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

            if (key == "BIAS") {
                float bias = value.toFloat();
                for (int i = 0; i < 4; ++i) {
                    m_brakes[i].setBrakeBias(bias);
                }
            } else if (key == "FRONT_DISC_RADIUS") {
                float radius = value.toFloat();
                m_brakes[0].setDiscProperties(radius, 0.028f);
                m_brakes[1].setDiscProperties(radius, 0.028f);
            } else if (key == "REAR_DISC_RADIUS") {
                float radius = value.toFloat();
                m_brakes[2].setDiscProperties(radius, 0.028f);
                m_brakes[3].setDiscProperties(radius, 0.028f);
            }
        }
    }

    file.close();
}

void BrakeModelManager::saveToIni(const QString& brakeIniPath) const {
    QFile file(brakeIniPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream << "[BRAKES]\n";
    stream << "BIAS=" << m_brakes[0].getBrakeBalance() << "\n";
    stream << "FRONT_DISC_RADIUS=0.14\n";
    stream << "REAR_DISC_RADIUS=0.14\n";

    file.close();
}

void BrakeModelManager::update(float dt, float pedalPressure, float speed) {
    m_speed = speed;

    // Calculate wheel angular velocity
    float wheelRadius = 0.33f;
    float angularVelocity = speed / 3.6f / wheelRadius; // rad/s

    // Calculate brake torque based on pedal pressure
    float maxTorque = 2000.0f; // Nm

    for (int i = 0; i < 4; ++i) {
        BrakeThermalModel::BrakeState state;
        state.pedalPressure = pedalPressure;
        state.brakeTorque = pedalPressure * maxTorque;
        state.discAngularVelocity = angularVelocity;

        m_brakes[i].update(dt, state);
    }
}

void BrakeModelManager::reset() {
    for (int i = 0; i < 4; ++i) {
        m_brakes[i].reset();
    }
}

float BrakeModelManager::getAverageDiscTemp() const {
    float total = 0;
    for (int i = 0; i < 4; ++i) {
        total += m_brakes[i].calculateDiscTemp();
    }
    return total / 4.0f;
}

float BrakeModelManager::getAveragePadTemp() const {
    float total = 0;
    for (int i = 0; i < 4; ++i) {
        total += m_brakes[i].calculatePadTemp();
    }
    return total / 4.0f;
}

float BrakeModelManager::getBrakeFade() const {
    float total = 0;
    for (int i = 0; i < 4; ++i) {
        total += m_brakes[i].calculateBrakeFade();
    }
    return total / 4.0f;
}

float BrakeModelManager::getBrakeBalance() const {
    return m_brakes[0].getBrakeBalance();
}

bool BrakeModelManager::validate(QString* error) const {
    for (int i = 0; i < 4; ++i) {
        if (!m_brakes[i].validate(error)) {
            return false;
        }
    }
    return true;
}
