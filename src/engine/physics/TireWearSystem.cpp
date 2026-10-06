#include "TireWearSystem.h"
#include <algorithm>
#include <cmath>

namespace ks {
namespace physics {

// ============================================================================
// TireCompoundData
// ============================================================================

TireCompoundData TireCompoundData::getCompoundData(TireCompound compound) {
    TireCompoundData data;
    data.compound = compound;

    switch (compound) {
        case TireCompound::SuperSoft:
            data.gripFactor = 1.15f;
            data.wearResistance = 0.5f;
            data.optimalTempMin = 65.0f;
            data.optimalTempMax = 95.0f;
            data.name = "Super Soft";
            break;
        case TireCompound::Soft:
            data.gripFactor = 1.08f;
            data.wearResistance = 0.7f;
            data.optimalTempMin = 70.0f;
            data.optimalTempMax = 100.0f;
            data.name = "Soft";
            break;
        case TireCompound::Medium:
            data.gripFactor = 1.0f;
            data.wearResistance = 1.0f;
            data.optimalTempMin = 75.0f;
            data.optimalTempMax = 110.0f;
            data.name = "Medium";
            break;
        case TireCompound::Hard:
            data.gripFactor = 0.93f;
            data.wearResistance = 1.4f;
            data.optimalTempMin = 80.0f;
            data.optimalTempMax = 120.0f;
            data.name = "Hard";
            break;
        case TireCompound::SuperHard:
            data.gripFactor = 0.87f;
            data.wearResistance = 1.8f;
            data.optimalTempMin = 85.0f;
            data.optimalTempMax = 130.0f;
            data.name = "Super Hard";
            break;
        case TireCompound::Wet:
            data.gripFactor = 0.75f;
            data.wearResistance = 1.2f;
            data.thermalConductivity = 0.8f;
            data.optimalTempMin = 40.0f;
            data.optimalTempMax = 70.0f;
            data.name = "Wet";
            break;
        case TireCompound::Intermediate:
            data.gripFactor = 0.85f;
            data.wearResistance = 1.1f;
            data.optimalTempMin = 50.0f;
            data.optimalTempMax = 85.0f;
            data.name = "Intermediate";
            break;
        case TireCompound::Custom:
            data.name = "Custom";
            break;
    }

    data.operatingTempRange = data.optimalTempMax - data.optimalTempMin;
    return data;
}

// ============================================================================
// TireWearSystem
// ============================================================================

TireWearSystem::TireWearSystem() {
    reset();
}

void TireWearSystem::setConfig(const TireThermalConfig& thermalConfig, const TireWearConfig& wearConfig) {
    m_thermalConfig = thermalConfig;
    m_wearConfig = wearConfig;
}

void TireWearSystem::setCompound(TireCompound compound) {
    m_compound = TireCompoundData::getCompoundData(compound);
}

void TireWearSystem::setCompound(const TireCompoundData& data) {
    m_compound = data;
}

void TireWearSystem::update(float speed, float normalLoad, float slipAngle, float slipRatio,
                             float lateralForce, float longitudinalForce, float dt) {
    updateThermal(speed, normalLoad, slipAngle, slipRatio, lateralForce, longitudinalForce, dt);
    updateWear(normalLoad, slipAngle, slipRatio, lateralForce, longitudinalForce, dt);
    updateGraining(dt);
    updateBlistering(dt);
}

void TireWearSystem::setAmbientTemp(float temp) {
    m_thermal.ambientTemp = temp;
}

void TireWearSystem::setRoadTemp(float temp) {
    m_thermal.roadTemp = temp;
}

void TireWearSystem::setTrackGrip(float grip) {
    m_trackGrip = std::clamp(grip, 0.1f, 1.0f);
}

void TireWearSystem::updateThermal(float speed, float normalLoad, float slipAngle, float slipRatio,
                                    float lateralForce, float longitudinalForce, float dt) {
    // Heat generation from friction
    float heatGen = calculateHeatGeneration(slipAngle, slipRatio, lateralForce, longitudinalForce);

    // Heat loss from convection (air flow)
    float heatLoss = calculateHeatLoss(speed);

    // Contact patch temperature from road
    float contactTemp = calculateContactPatchTemp();

    // Net temperature change
    float netHeat = (heatGen - heatLoss) * dt / (m_thermalConfig.specificHeat * m_thermalConfig.rubberMass);

    // Update surface temperatures (with spatial distribution)
    float heatDistribution = 0.3f; // Heat from contact to shoulders
    float coreHeatTransfer = (m_thermal.coreTemp - m_thermal.averageSurfaceTemp()) *
                             m_thermalConfig.coreHeatCapacity * dt;

    // Inner shoulder (closest to brake)
    m_thermal.surfaceTemp[0] += (netHeat * (1.0f - heatDistribution) + contactTemp * 0.01f) * dt;
    m_thermal.surfaceTemp[0] -= (m_thermal.surfaceTemp[0] - m_thermal.ambientTemp) * 0.05f * dt;

    // Middle
    m_thermal.surfaceTemp[1] += (netHeat * (1.0f - heatDistribution * 0.5f)) * dt;
    m_thermal.surfaceTemp[1] -= (m_thermal.surfaceTemp[1] - m_thermal.ambientTemp) * 0.04f * dt;

    // Outer shoulder
    m_thermal.surfaceTemp[2] += (netHeat * (1.0f - heatDistribution)) * dt;
    m_thermal.surfaceTemp[2] -= (m_thermal.surfaceTemp[2] - m_thermal.ambientTemp) * 0.05f * dt;

    // Core temperature lags surface
    float coreTarget = m_thermal.averageSurfaceTemp() * 0.7f + m_thermal.roadTemp * 0.3f;
    m_thermal.coreTemp += (coreTarget - m_thermal.coreTemp) * m_thermalConfig.coreHeatCapacity * dt;

    // Carcass temperature
    float carcassTarget = m_thermal.averageSurfaceTemp() * 0.5f + m_thermal.coreTemp * 0.5f;
    m_thermal.carcassTemp += (carcassTarget - m_thermal.carcassTemp) * m_thermalConfig.carcassHeatCapacity * dt;

    // Clamp temperatures
    for (int i = 0; i < 3; ++i) {
        m_thermal.surfaceTemp[i] = std::clamp(m_thermal.surfaceTemp[i], m_thermal.ambientTemp - 10.0f, 200.0f);
    }
    m_thermal.coreTemp = std::clamp(m_thermal.coreTemp, m_thermal.ambientTemp, 180.0f);
    m_thermal.carcassTemp = std::clamp(m_thermal.carcassTemp, m_thermal.ambientTemp, 160.0f);
}

void TireWearSystem::updateWear(float normalLoad, float slipAngle, float slipRatio,
                                 float lateralForce, float longitudinalForce, float dt) {
    // Base wear from normal load
    float loadFactor = std::pow(normalLoad / 5000.0f, m_wearConfig.loadWearExponent);

    // Slip wear (sliding causes more wear)
    float slipMagnitude = std::sqrt(slipAngle * slipAngle + slipRatio * slipRatio);
    float slipFactor = 1.0f + slipMagnitude * m_wearConfig.slipWearMultiplier;

    // Temperature wear (worn faster outside optimal range)
    float temp = m_thermal.averageSurfaceTemp();
    float tempOptimal = (m_compound.optimalTempMin + m_compound.optimalTempMax) / 2.0f;
    float tempDeviation = std::abs(temp - tempOptimal) / m_compound.operatingTempRange;
    float tempFactor = 1.0f + std::pow(tempDeviation, m_wearConfig.temperatureWearExponent);

    // Compound wear resistance
    float wearResistance = m_compound.wearResistance * m_trackGrip;

    // Calculate wear increment
    float wearIncrement = m_wearConfig.baseWearRate * loadFactor * slipFactor * tempFactor /
                          wearResistance * dt;

    // Update wear
    m_wearDetail.wear = std::min(1.0f, m_wearDetail.wear + wearIncrement);
    m_wearDetail.structuralWear = std::min(m_wearDetail.wear, m_wearDetail.structuralWear + wearIncrement * 0.7f);
    m_wearDetail.surfaceWear = std::min(m_wearDetail.wear, m_wearDetail.surfaceWear + wearIncrement * 1.3f);

    // Update grip loss
    m_wearDetail.gripLoss = 1.0f - m_wearDetail.gripMultiplier();

    // Track distance for clumps
    m_distanceTraveled += normalLoad * slipMagnitude * dt * 0.001f;
}

void TireWearSystem::updateGraining(float dt) {
    float temp = m_thermal.averageSurfaceTemp();

    if (temp > m_wearConfig.grainingOnsetTemp && temp < m_wearConfig.grainingOffsetTemp) {
        float peakTemp = (m_wearConfig.grainingOnsetTemp + m_wearConfig.grainingOffsetTemp) / 2.0f;
        float proximity = 1.0f - std::abs(temp - peakTemp) /
                          ((m_wearConfig.grainingOffsetTemp - m_wearConfig.grainingOnsetTemp) / 2.0f);
        proximity = std::clamp(proximity, 0.0f, 1.0f);

        m_wearDetail.graining = std::min(m_wearConfig.maxGraining,
                                   m_wearDetail.graining + proximity * 0.01f * dt);
    } else {
        // Graining slowly heals when temp is outside range
        m_wearDetail.graining = std::max(0.0f, m_wearDetail.graining - 0.005f * dt);
    }
}

void TireWearSystem::updateBlistering(float dt) {
    float temp = m_thermal.averageSurfaceTemp();

    if (temp > m_wearConfig.blisteringOnsetTemp) {
        float severity = (temp - m_wearConfig.blisteringOnsetTemp) / 30.0f;
        severity = std::clamp(severity, 0.0f, 1.0f);

        m_wearDetail.blistering = std::min(m_wearConfig.maxBlistering,
                                     m_wearDetail.blistering + severity * 0.02f * dt);
    } else {
        // Blistering slowly heals when cool
        m_wearDetail.blistering = std::max(0.0f, m_wearDetail.blistering - 0.003f * dt);
    }
}

float TireWearSystem::calculateHeatGeneration(float slipAngle, float slipRatio,
                                                float lateralForce, float longitudinalForce) const {
    // Heat from friction work
    float lateralWork = std::abs(lateralForce) * std::abs(slipAngle);
    float longitudinalWork = std::abs(longitudinalForce) * std::abs(slipRatio);
    float totalWork = (lateralWork + longitudinalWork) * m_thermalConfig.contactPatchArea;

    // Scale by thermal conductivity and compound
    return totalWork * m_thermalConfig.thermalConductivity * m_compound.thermalConductivity;
}

float TireWearSystem::calculateHeatLoss(float speed) const {
    // Convective cooling (proportional to speed)
    float area = m_thermalConfig.contactPatchArea * 4.0f; // Total tire surface
    float deltaT = m_thermal.averageSurfaceTemp() - m_thermal.ambientTemp;

    // Forced convection increases with speed
    float convection = m_thermalConfig.convectionCoeff * (1.0f + speed * 0.02f);
    return convection * area * deltaT;
}

float TireWearSystem::calculateContactPatchTemp() const {
    // Road temperature influence
    float roadEffect = m_thermal.roadTemp * 0.6f;
    float coreEffect = m_thermal.coreTemp * 0.3f;
    float ambientEffect = m_thermal.ambientTemp * 0.1f;
    return roadEffect + coreEffect + ambientEffect;
}

float TireWearSystem::effectiveMu() const {
    float baseMu = m_compound.gripFactor;
    float wearEffect = m_wearDetail.gripMultiplier();
    float tempEffect = m_thermal.isOptimalTemp(m_compound.optimalTempMin, m_compound.optimalTempMax) ? 1.0f : 0.9f;
    return baseMu * wearEffect * tempEffect * m_trackGrip;
}

bool TireWearSystem::needsPitStop() const {
    return m_wearDetail.wear > 0.85f || m_wearDetail.blistering > 0.4f;
}

float TireWearSystem::estimatedLapsRemaining() const {
    if (m_wearDetail.wear >= 1.0f) return 0.0f;
    float wearPerLap = m_wearDetail.wear / std::max(1.0f, m_distanceTraveled / 5000.0f); // rough lap estimate
    if (wearPerLap <= 0.0f) return 999.0f;
    return (1.0f - m_wearDetail.wear) / wearPerLap;
}

void TireWearSystem::reset() {
    m_thermal = TireThermalState{};
    m_wearDetail = TireWearDetailState{};
    m_distanceTraveled = 0.0f;
    m_slipHistory = 0.0f;
}


} // namespace physics
} // namespace ks
