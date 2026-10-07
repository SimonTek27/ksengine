#include "BrakeWearSystem.h"
#include <algorithm>
#include <cmath>

namespace ks {
namespace physics {

BrakeWearSystem::BrakeWearSystem() { reset(); }

void BrakeWearSystem::setThermalConfig(const BrakeThermalConfig& config) { m_thermalConfig = config; }
void BrakeWearSystem::setPadWearConfig(const BrakePadWearConfig& config) { m_padWearConfig = config; }
void BrakeWearSystem::setAmbientTemp(float temp) { m_thermal.ambientTemp = temp; }

void BrakeWearSystem::update(const BrakeWearData& data) {
    int w = std::clamp(data.wheel, 0, 3);
    m_thermal.airflowSpeed = data.vehicleSpeed;
    if (data.brakePressure > 0.5f || data.brakeTorque > 10.0f) {
        m_braking[w] = true;
        updateThermal(w, data.brakeTorque, data.wheelSpeed, data.dt);
        updateFade(w, data.dt);
        updatePadWear(w, data.brakeTorque, data.wheelSpeed, data.dt);
        updateDiscWear(w, data.brakeTorque, data.wheelSpeed, data.dt);
    } else {
        m_braking[w] = false;
        updateThermal(w, 0.0f, data.wheelSpeed, data.dt);
        updateFade(w, data.dt);
    }
}

void BrakeWearSystem::applyBrake(int wheel, float pressure, float dt) {
    wheel = std::clamp(wheel, 0, 3);
    float torque = calculateBrakeTorque(pressure, wheel);
    BrakeWearData d;
    d.wheel = wheel;
    d.brakePressure = pressure;
    d.brakeTorque = torque;
    d.dt = dt;
    d.vehicleSpeed = m_thermal.airflowSpeed;
    update(d);
}

void BrakeWearSystem::updateThermal(int wheel, float brakeTorque, float wheelSpeed, float dt) {
    float heat = calculateHeatGeneration(brakeTorque, wheelSpeed);
    float cool = calculateCooling(wheel, m_thermal.airflowSpeed);
    float discC = m_thermalConfig.discMass * m_thermalConfig.discHeatCapacity;
    float dT = (heat - cool) * dt / std::max(discC, 1.0f);
    m_thermal.discTemp[wheel] = std::clamp(m_thermal.discTemp[wheel] + dT, m_thermal.ambientTemp, 1200.0f);
    float padShare = heat * m_thermalConfig.conductionPadToDisc;
    float padC = m_thermalConfig.padMass * m_thermalConfig.padHeatCapacity;
    m_thermal.padTemp[wheel] = std::clamp(
        m_thermal.padTemp[wheel] + (padShare * dt / std::max(padC, 1.0f))
        - cool * 0.2f * dt / std::max(padC, 1.0f),
        m_thermal.ambientTemp, 1000.0f);
}

void BrakeWearSystem::updateFade(int wheel, float dt) {
    float temp = m_thermal.discTemp[wheel];
    auto& f = m_fade[wheel];
    if (temp > m_thermal.fadeOnsetTemp) {
        float over = (temp - m_thermal.fadeOnsetTemp) / std::max(1.0f, m_thermal.criticalDiscTemp - m_thermal.fadeOnsetTemp);
        f.fadeLevel = std::clamp(f.fadeLevel + over * dt * 0.5f, 0.0f, 1.0f);
        f.fadeType = BrakeFadeType::Thermal;
        f.pedalFirmness = 1.0f - f.fadeLevel * 0.6f;
    } else {
        f.fadeLevel = std::max(0.0f, f.fadeLevel - f.recoveryRate * dt);
        if (f.fadeLevel < 0.01f) {
            f.fadeLevel = 0.0f;
            f.fadeType = BrakeFadeType::None;
            f.pedalFirmness = 1.0f;
        }
    }
    f.peakFriction = m_thermalConfig.frictionCoeffBase * (1.0f - f.fadeLevel * 0.8f);
}

void BrakeWearSystem::updatePadWear(int wheel, float brakeTorque, float wheelSpeed, float dt) {
    float tempFactor = std::pow(std::max(m_thermal.padTemp[wheel], 1.0f) / 400.0f, m_padWearConfig.temperatureExponent);
    float pressFactor = std::pow(std::max(std::abs(brakeTorque) / 1000.0f, 0.01f), m_padWearConfig.pressureExponent);
    float velFactor = std::pow(std::max(std::abs(wheelSpeed), 0.01f) / 50.0f, m_padWearConfig.velocityExponent);
    float rate = m_padWearConfig.baseWearRate * tempFactor * pressFactor * velFactor;
    m_padWear.wearRate[wheel] = rate;
    m_padWear.padThickness[wheel] = std::max(0.0f, m_padWear.padThickness[wheel] - rate * dt);
    m_padWear.totalWear[wheel] += rate * dt;
    m_padWear.needsReplacement[wheel] = m_padWear.padThickness[wheel] < m_padWearConfig.warningThickness;
}

void BrakeWearSystem::updateDiscWear(int wheel, float brakeTorque, float wheelSpeed, float dt) {
    float rate = m_padWearConfig.baseWearRate * 0.05f * (std::abs(brakeTorque) / 1000.0f) * (std::abs(wheelSpeed) / 50.0f);
    m_discWear.discThickness[wheel] = std::max(5.0f, m_discWear.discThickness[wheel] - rate * dt);
    if (m_thermal.discTemp[wheel] > 700.0f)
        m_discWear.warpage[wheel] = std::clamp(m_discWear.warpage[wheel] + 0.01f * dt, 0.0f, 1.0f);
    m_discWear.isWarped[wheel] = m_discWear.warpage[wheel] > 0.3f;
}

float BrakeWearSystem::calculateBrakeTorque(float pressure, int wheel) const {
    (void)wheel;
    return pressure * 80.0f * effectiveFriction(wheel);
}

float BrakeWearSystem::calculateHeatGeneration(float torque, float angularVelocity) const {
    return std::abs(torque * angularVelocity); // Watts
}

float BrakeWearSystem::calculateCooling(int wheel, float speed) const {
    float coeff = (wheel < 2) ? m_thermalConfig.coolingCoeffFront : m_thermalConfig.coolingCoeffRear;
    float dT = m_thermal.discTemp[wheel] - m_thermal.ambientTemp;
    float air = 1.0f + 0.05f * std::max(speed, 0.0f);
    return coeff * m_thermalConfig.discArea * dT * air;
}

float BrakeWearSystem::effectiveFriction(int wheel) const {
    wheel = std::clamp(wheel, 0, 3);
    float base = m_thermalConfig.frictionCoeffBase;
    float tempDelta = m_thermal.discTemp[wheel] - 400.0f;
    base += tempDelta * m_thermalConfig.frictionTempSensitivity;
    return m_fade[wheel].effectiveFriction(std::max(base, 0.05f));
}

float BrakeWearSystem::brakingMultiplier(int wheel) const {
    wheel = std::clamp(wheel, 0, 3);
    float fadeEffect = m_fade[wheel].brakingMultiplier();
    float padEffect = m_padWear.remainingLife(wheel);
    float discEffect = m_discWear.remainingLife(wheel);
    return std::clamp(fadeEffect * padEffect * discEffect, 0.1f, 1.0f);
}

void BrakeWearSystem::reset() {
    m_thermal = BrakeThermalState{};
    for (int i = 0; i < 4; ++i) {
        m_fade[i] = BrakeFadeState{};
        m_braking[i] = false;
    }
    m_padWear = BrakePadWearState{};
    m_discWear = BrakeDiscWearState{};
}

} // namespace physics
} // namespace ks
