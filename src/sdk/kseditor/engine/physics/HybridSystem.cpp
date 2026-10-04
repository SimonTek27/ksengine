#include "HybridSystem.h"
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <QDebug>

// ============================================================================
// Constructor and Configuration
// ============================================================================

void HybridSystem::setConfig(const ErsConfig& config) {
    m_config = config;
    // Reset state on config change (keep SOC if reasonable)
    if (m_state.batterySoc < 0 || m_state.batterySoc > 100) {
        m_state.batterySoc = 50.0f;
    }
    m_state.batteryTemp = 30.0f;
}

void HybridSystem::setDeploymentMode(ErsMode mode) {
    m_config.deploymentMode = mode;
    if (mode != ErsMode::Attack) {
        m_state.attackModeActive = false;
        m_state.attackModeTimer = 0.0f;
    }
}

void HybridSystem::activateAttackMode() {
    if (m_state.attackModeCooldown <= 0.0f && !m_state.attackModeActive) {
        m_state.attackModeActive = true;
        m_state.attackModeTimer = m_config.attackDuration;
    }
}

void HybridSystem::deactivateAttackMode() {
    m_state.attackModeActive = false;
    m_state.attackModeTimer = 0.0f;
    m_state.attackModeCooldown = m_config.attackCooldown;
}

void HybridSystem::setAutoDeployRate(float rate) {
    m_config.autoDeployRate = std::clamp(rate, 0.0f, 1.0f);
}

// ============================================================================
// Main Simulation Update
// ============================================================================

void HybridSystem::update(float dt, float engineRpm, float throttle,
                          float brake, float speed, float gearRatio,
                          float engineTorque, float turboBoost,
                          float turboLag)
{
    if (!m_config.enabled) {
        m_state = ErsState();
        m_state.batterySoc = 50.0f;
        return;
    }

    float dtClamped = std::clamp(dt, 0.0f, 0.1f);

    // Detect coasting (off-throttle, no brake)
    m_state.coasting = (throttle < 0.05f && brake < 0.05f);

    // Phase 1: MGU-H harvests from exhaust (before deployment)
    updateMguh(dtClamped, engineRpm, throttle, turboBoost);

    // Phase 2: MGU-K regen from braking
    updateMguk(dtClamped, brake, speed);

    // Phase 3: Calculate deployment based on mode
    updateDeployment(dtClamped, engineRpm, throttle, brake, speed, gearRatio);

    // Phase 4: Energy balance and battery update
    updateEnergyBalance(dtClamped);

    // Track previous RPM for derivative calculations
    m_prevEngineRpm = engineRpm;
}

// ============================================================================
// MGU-K (Kinetic) - Regen Braking and Torque Assist
// ============================================================================

void HybridSystem::updateMguk(float dt, float brake, float speed) {
    if (speed < 5.0f) {
        m_state.regenTorqueNm = 0.0f;
        return;
    }

    float maxRegenKw = m_config.mguk.maxRegenKw;
    float socHeadroom = (m_state.batterySoc - m_config.battery.minSoc)
                        / (m_config.battery.maxSoc - m_config.battery.minSoc);
    float regenLimit = std::clamp(socHeadroom, 0.0f, 1.0f);

    float regenPowerKw = maxRegenKw * brake * regenLimit * m_config.mguk.regenEfficiency;

    if (regenPowerKw > 0) {
        // Check battery charge limit
        float batteryHeadroom = m_config.battery.maxChargeKw - m_state.batteryPowerKw;
        regenPowerKw = std::min(regenPowerKw, std::max(0.0f, batteryHeadroom));

        if (regenPowerKw > 0) {
            float wheelRpm = speed * 60.0f / (2.0f * 3.14159f * 0.33f);
            float regenTorque = (regenPowerKw * 1000.0f) / std::max(1.0f, wheelRpm * 0.10472f);
            m_state.regenTorqueNm = regenTorque * m_config.mguk.deployEfficiency;
            m_state.mgukPowerKw = -regenPowerKw;
            m_state.harvesting = true;

            chargeBattery(regenPowerKw, dt);
        }
    }
}

float HybridSystem::calculateRegenTorque(float brakePedal, float speed) const {
    if (!m_config.enabled || speed < 5.0f) return 0.0f;

    float maxRegenKw = m_config.mguk.maxRegenKw;
    float socHeadroom = (m_state.batterySoc - m_config.battery.minSoc)
                        / (m_config.battery.maxSoc - m_config.battery.minSoc);
    float regenLimit = std::clamp(socHeadroom, 0.0f, 1.0f);
    float regenPowerKw = maxRegenKw * brakePedal * regenLimit * m_config.mguk.regenEfficiency;

    float wheelRpm = speed * 60.0f / (2.0f * 3.14159f * 0.33f);
    return (regenPowerKw * 1000.0f) / std::max(1.0f, wheelRpm * 0.10472f);
}

float HybridSystem::calculateDeployTorque(float engineRpm, float throttle, float speed) const {
    if (!m_config.enabled || throttle < 0.05f || speed < 5.0f) return 0.0f;

    float deployPower = getAvailableMgukPower();
    if (deployPower <= 0) return 0.0f;

    // Engine speed in rad/s
    float engineRadPerSec = engineRpm * 2.0f * 3.14159f / 60.0f;
    return (deployPower * 1000.0f) / std::max(1.0f, engineRadPerSec);
}

float HybridSystem::getAvailableMgukPower() const {
    if (!m_config.enabled) return 0.0f;

    float deployPower = m_config.mguk.maxPowerKw * m_deployRate;

    // Battery discharge limit
    float dischargeLimit = m_config.battery.maxDischargeKw;
    float socLimit = (m_state.batterySoc - m_config.battery.minSoc)
                     / (m_config.battery.maxSoc - m_config.battery.minSoc) * dischargeLimit;
    float tempLimit = 1.0f - std::max(0.0f, (m_state.batteryTemp - m_config.battery.optimalTemp)
                                     / (m_config.battery.maxTemp - m_config.battery.optimalTemp));
    tempLimit = std::clamp(tempLimit, 0.2f, 1.0f);

    // Per-lap energy limit
    float lapEnergyLimit = std::max(0.0f, m_config.battery.perLapEnergyMj - m_state.energyDeployedMj);
    float lapEnergyKw = lapEnergyLimit > 0 ? lapEnergyLimit * 3.6f / 0.1f : 0.0f;

    float maxDeploy = std::min({deployPower, socLimit * tempLimit, lapEnergyKw,
                                m_config.mguk.maxPowerKw});

    // MGU-H can supplement if enabled (harvesting power is negative -> adds to deploy)
    if (m_config.mguh.canTransferToMguk && m_config.mguh.connectedToTurbo && maxDeploy > 0.0f) {
        maxDeploy = std::min(maxDeploy - m_state.mguhPowerKw, m_config.mguk.maxPowerKw);
    }

    return std::max(0.0f, maxDeploy);
}

// ============================================================================
// MGU-H (Heat) - Exhaust Energy Harvesting and Turbo Spool
// ============================================================================

void HybridSystem::updateMguh(float dt, float engineRpm, float throttle, float turboBoost) {
    if (!m_config.mguh.connectedToTurbo) {
        m_state.mguhPowerKw = 0.0f;
        return;
    }

    float exhaustEnergy = calculateExhaustEnergy(engineRpm, throttle, turboBoost);

    // Harvest from exhaust
    float harvestPower = calculateMguhHarvestPower(exhaustEnergy);
    harvestPower = std::min(harvestPower, m_config.mguh.maxHarvestKw);

    if (harvestPower > 0) {
        chargeBattery(harvestPower * m_config.mguh.harvestEfficiency, dt);
    }

    // Anti-lag: use MGU-H to spool turbo during coast/off-throttle
    float spoolPower = 0.0f;
    if (throttle < 0.05f && engineRpm > 4000.0f) {
        spoolPower = calculateTurboSpoolPower();
    }

    // MGU-H net power to system (negative = harvesting, positive = spooling)
    m_state.mguhPowerKw = spoolPower - harvestPower;
}

float HybridSystem::calculateExhaustEnergy(float engineRpm, float throttle,
                                            float turboBoost) const {
    if (!m_config.mguh.connectedToTurbo || engineRpm < 1000.0f) return 0.0f;

    float rpmFactor = engineRpm / 12000.0f;
    float throttleFactor = throttle;
    float boostFactor = 1.0f + turboBoost * 0.5f;

    // Exhaust energy scales with RPM, throttle, and boost
    float energy = rpmFactor * throttleFactor * boostFactor * 500.0f;

    return std::max(0.0f, energy);
}

float HybridSystem::calculateMguhHarvestPower(float exhaustEnergy) const {
    if (!m_config.mguh.connectedToTurbo || exhaustEnergy <= 0) return 0.0f;

    float harvestPower = exhaustEnergy * m_config.mguh.harvestEfficiency;
    return std::min(harvestPower, m_config.mguh.maxHarvestKw);
}

float HybridSystem::calculateTurboSpoolPower() const {
    if (!m_config.mguh.connectedToTurbo) return 0.0f;

    float socHeadroom = (m_state.batterySoc - m_config.battery.minSoc)
                        / (m_config.battery.maxSoc - m_config.battery.minSoc);
    float batteryOk = (socHeadroom > 0.2f) ? 1.0f : 0.0f;

    return m_config.mguh.spoolPowerKw * batteryOk;
}

bool HybridSystem::canTransferToMguk() const {
    return m_config.mguh.canTransferToMguk && m_state.mguhPowerKw < 0;
}

// ============================================================================
// Deployment Strategy
// ============================================================================

void HybridSystem::updateDeployment(float dt, float engineRpm, float throttle,
                                     float brake, float speed, float gearRatio)
{
    if (throttle < 0.05f || brake > 0.05f || speed < m_config.deployThresholdSpeed) {
        m_state.deployTorqueNm = 0.0f;
        m_state.deploying = false;
        m_deployRate = 0.0f;
        return;
    }

    // Attack mode timer
    if (m_state.attackModeActive) {
        m_state.attackModeTimer -= dt;
        if (m_state.attackModeTimer <= 0.0f) {
            deactivateAttackMode();
        }
    }
    if (m_state.attackModeCooldown > 0.0f) {
        m_state.attackModeCooldown -= dt;
    }

    // Calculate deploy rate from current mode
    m_deployRate = calculateDeployRate(m_config.deploymentMode, speed, throttle);

    // Get available power and convert to torque
    float deployPower = getAvailableMgukPower();
    if (deployPower <= 0) {
        m_state.deployTorqueNm = 0.0f;
        m_state.deploying = false;
        return;
    }

    float engineRadPerSec = engineRpm * 2.0f * 3.14159f / 60.0f;
    float deployTorque = (deployPower * 1000.0f) / std::max(1.0f, engineRadPerSec);
    deployTorque *= m_config.mguk.deployEfficiency;

    // Scale by gear ratio for torque at wheels
    deployTorque *= gearRatio;

    m_state.deployTorqueNm = deployTorque;
    m_state.mgukPowerKw = deployPower;
    m_state.deploying = true;

    dischargeBattery(deployPower, dt);
    m_state.energyDeployedMj += deployPower * dt / 1000.0f;
}

float HybridSystem::calculateDeployRate(ErsMode mode, float speed, float throttle) const {
    float rate = 0.0f;

    switch (mode) {
    case ErsMode::None:
        rate = 0.0f;
        break;
    case ErsMode::Low:
        rate = 0.25f;
        break;
    case ErsMode::Medium:
        rate = 0.50f;
        break;
    case ErsMode::High:
        rate = 0.75f;
        break;
    case ErsMode::Attack:
        rate = 1.0f;
        break;
    case ErsMode::Overtake:
        rate = 1.0f;
        break;
    case ErsMode::Qualifying:
        rate = 1.0f;
        break;
    case ErsMode::Auto:
        // Auto mode: deploy more on straights (high speed), less in corners
        float speedFactor = std::clamp((speed - 80.0f) / 200.0f, 0.0f, 1.0f);
        float throttleFactor = throttle;
        rate = m_config.autoDeployRate * speedFactor * throttleFactor;
        break;
    }

    // Attack mode override
    if (m_state.attackModeActive) {
        rate = 1.0f;
    }

    return std::clamp(rate, 0.0f, 1.0f);
}

// ============================================================================
// Battery / Energy Store Management
// ============================================================================

void HybridSystem::chargeBattery(float powerKw, float dt) {
    if (powerKw <= 0 || m_state.batterySoc >= m_config.battery.maxSoc) return;

    float chargeLimit = m_config.battery.maxChargeKw;
    float appliedKw = std::min(powerKw, chargeLimit);

    float energyMj = appliedKw * dt / 1000.0f;
    float socGain = energyMj / m_config.battery.capacityMj * 100.0f;

    m_state.batterySoc = std::min(m_state.batterySoc + socGain, m_config.battery.maxSoc);
    m_state.batteryPowerKw = appliedKw;
    m_state.energyHarvestedMj += energyMj;

    updateBatteryTemperature(dt, appliedKw);
}

void HybridSystem::dischargeBattery(float powerKw, float dt) {
    if (powerKw <= 0 || m_state.batterySoc <= m_config.battery.minSoc) return;

    float dischargeLimit = m_config.battery.maxDischargeKw;
    float appliedKw = std::min(powerKw, dischargeLimit);

    float energyMj = appliedKw * dt / 1000.0f;
    float socLoss = energyMj / m_config.battery.capacityMj * 100.0f;

    m_state.batterySoc = std::max(m_state.batterySoc - socLoss, m_config.battery.minSoc);
    m_state.batteryPowerKw = -appliedKw;

    updateBatteryTemperature(dt, powerKw);
}

void HybridSystem::updateBatteryTemperature(float dt, float powerKw) {
    if (dt <= 0) return;

    // Resistive heating: P_loss = I^2 * R = (P / V)^2 * R  (watts -> kW)
    float voltage = m_config.battery.nominalVoltage;
    float currentA = (std::abs(powerKw) * 1000.0f) / std::max(1.0f, voltage);
    float heatPowerKw = (currentA * currentA * m_config.battery.internalResistanceOhm) / 1000.0f;

    // Temperature change: dT = P_heat * dt / thermal_mass - cooling * (T - T_ambient) * dt
    float ambientTemp = m_config.battery.optimalTemp;
    float dTemp = (heatPowerKw * dt / m_config.battery.thermalMass)
                  - m_config.battery.coolingRate * (m_state.batteryTemp - ambientTemp) * dt;

    m_state.batteryTemp = std::clamp(m_state.batteryTemp + dTemp, 10.0f, m_config.battery.maxTemp + 10.0f);
    m_state.overTemperature = m_state.batteryTemp > m_config.battery.maxTemp;
}

void HybridSystem::updateEnergyBalance(float dt) {
    // Energy depletion check
    m_state.energyDepleted = (m_state.batterySoc <= m_config.battery.minSoc)
                             && (m_state.energyDeployedMj >= m_config.battery.perLapEnergyMj);

    // Laps of energy remaining estimate
    float energyRate = std::max(m_config.mguk.maxPowerKw * m_deployRate, 0.1f);
    float energyRemainingMj = (m_state.batterySoc - m_config.battery.minSoc)
                              * m_config.battery.capacityMj / 100.0f;
    float remainingLapEnergy = m_config.battery.perLapEnergyMj - m_state.energyDeployedMj;
    float totalAvailable = std::min(energyRemainingMj, remainingLapEnergy);

    m_state.lapsRemaining = (energyRate > 0)
        ? totalAvailable / (energyRate * 3.6f / 1000.0f * 120.0f) : 0.0f;

    // Total accumulation
    m_state.totalDeployedMj += m_state.deploying ? m_state.mgukPowerKw * dt / 1000.0f : 0.0f;
    m_state.totalHarvestedMj += m_state.harvesting ? std::abs(m_state.mgukPowerKw) * dt / 1000.0f : 0.0f;
}

void HybridSystem::resetLap() {
    m_state.energyDeployedMj = 0.0f;
    m_state.energyHarvestedMj = 0.0f;
}

void HybridSystem::reset() {
    m_state = ErsState();
    m_state.batterySoc = 50.0f;
    m_state.batteryTemp = 30.0f;
    m_deployRate = 0.0f;
    m_prevEngineRpm = 0.0f;
}

float HybridSystem::getDeployableEnergy() const {
    float socEnergy = (m_state.batterySoc - m_config.battery.minSoc)
                      * m_config.battery.capacityMj / 100.0f;
    float lapEnergy = m_config.battery.perLapEnergyMj - m_state.energyDeployedMj;
    return std::min(socEnergy, std::max(0.0f, lapEnergy));
}

float HybridSystem::getRemainingLapEnergy() const {
    return std::max(0.0f, m_config.battery.perLapEnergyMj - m_state.energyDeployedMj);
}

bool HybridSystem::isAttackModeAvailable() const {
    return m_state.attackModeCooldown <= 0.0f
           && !m_state.attackModeActive
           && m_state.batterySoc > 30.0f;
}

// ============================================================================
// Architecture Presets
// ============================================================================

HybridSystem::ErsConfig HybridSystem::getF1_2014() {
    ErsConfig cfg;
    cfg.architecture = HybridArchitecture::F1_2014;
    cfg.mguk.maxPowerKw = 120.0f;
    cfg.mguk.maxRegenKw = 120.0f;
    cfg.mguk.maxTorqueNm = 200.0f;
    cfg.mguh.maxPowerKw = 120.0f;
    cfg.mguh.maxHarvestKw = 120.0f;
    cfg.mguh.canTransferToMguk = true;
    cfg.battery.capacityMj = 4.0f;
    cfg.battery.perLapEnergyMj = 4.0f;
    cfg.battery.maxChargeKw = 120.0f;
    cfg.battery.maxDischargeKw = 120.0f;
    cfg.battery.nominalVoltage = 800.0f;
    cfg.attackDuration = 33.0f;
    cfg.attackCooldown = 60.0f;
    return cfg;
}

HybridSystem::ErsConfig HybridSystem::getF1_2026() {
    ErsConfig cfg;
    cfg.architecture = HybridArchitecture::F1_2026;
    cfg.mguk.maxPowerKw = 350.0f;
    cfg.mguk.maxRegenKw = 350.0f;
    cfg.mguk.maxTorqueNm = 500.0f;
    cfg.mguh.maxPowerKw = 0.0f;
    cfg.mguh.maxHarvestKw = 0.0f;
    cfg.mguh.connectedToTurbo = false;
    cfg.mguh.canTransferToMguk = false;
    cfg.battery.capacityMj = 8.5f;
    cfg.battery.perLapEnergyMj = 8.5f;
    cfg.battery.maxChargeKw = 350.0f;
    cfg.battery.maxDischargeKw = 350.0f;
    cfg.battery.nominalVoltage = 1000.0f;
    cfg.attackDuration = 30.0f;
    cfg.attackCooldown = 45.0f;
    return cfg;
}

HybridSystem::ErsConfig HybridSystem::getLMP1() {
    ErsConfig cfg;
    cfg.architecture = HybridArchitecture::LMP1;
    cfg.mguk.maxPowerKw = 200.0f;
    cfg.mguk.maxRegenKw = 200.0f;
    cfg.mguk.maxTorqueNm = 300.0f;
    cfg.mguh.maxPowerKw = 80.0f;
    cfg.mguh.maxHarvestKw = 80.0f;
    cfg.mguh.canTransferToMguk = true;
    cfg.battery.capacityMj = 7.0f;
    cfg.battery.perLapEnergyMj = 7.0f;
    cfg.battery.maxChargeKw = 200.0f;
    cfg.battery.maxDischargeKw = 200.0f;
    cfg.battery.nominalVoltage = 800.0f;
    cfg.attackDuration = 25.0f;
    cfg.attackCooldown = 50.0f;
    return cfg;
}

HybridSystem::ErsConfig HybridSystem::getLMDh() {
    ErsConfig cfg;
    cfg.architecture = HybridArchitecture::LMDh;
    cfg.mguk.maxPowerKw = 50.0f;
    cfg.mguk.maxRegenKw = 50.0f;
    cfg.mguk.maxTorqueNm = 100.0f;
    cfg.mguh.maxPowerKw = 0.0f;
    cfg.mguh.maxHarvestKw = 0.0f;
    cfg.mguh.connectedToTurbo = false;
    cfg.mguh.canTransferToMguk = false;
    cfg.battery.capacityMj = 1.5f;
    cfg.battery.perLapEnergyMj = 1.5f;
    cfg.battery.maxChargeKw = 50.0f;
    cfg.battery.maxDischargeKw = 50.0f;
    cfg.battery.nominalVoltage = 400.0f;
    cfg.attackDuration = 20.0f;
    cfg.attackCooldown = 40.0f;
    return cfg;
}

HybridSystem::ErsConfig HybridSystem::getRoadMild() {
    ErsConfig cfg;
    cfg.architecture = HybridArchitecture::Road_Mild;
    cfg.mguk.maxPowerKw = 15.0f;
    cfg.mguk.maxRegenKw = 15.0f;
    cfg.mguk.maxTorqueNm = 30.0f;
    cfg.mguh.maxPowerKw = 0.0f;
    cfg.mguh.maxHarvestKw = 0.0f;
    cfg.mguh.connectedToTurbo = false;
    cfg.mguh.canTransferToMguk = false;
    cfg.battery.capacityMj = 0.5f;
    cfg.battery.perLapEnergyMj = 100.0f;
    cfg.battery.maxChargeKw = 15.0f;
    cfg.battery.maxDischargeKw = 15.0f;
    cfg.battery.nominalVoltage = 48.0f;
    cfg.battery.internalResistanceOhm = 0.15f;
    cfg.attackDuration = 10.0f;
    cfg.attackCooldown = 30.0f;
    return cfg;
}

HybridSystem::ErsConfig HybridSystem::getRoadFull() {
    ErsConfig cfg;
    cfg.architecture = HybridArchitecture::Road_Full;
    cfg.mguk.maxPowerKw = 100.0f;
    cfg.mguk.maxRegenKw = 80.0f;
    cfg.mguk.maxTorqueNm = 200.0f;
    cfg.mguh.maxPowerKw = 0.0f;
    cfg.mguh.maxHarvestKw = 0.0f;
    cfg.mguh.connectedToTurbo = false;
    cfg.mguh.canTransferToMguk = false;
    cfg.battery.capacityMj = 2.0f;
    cfg.battery.perLapEnergyMj = 100.0f;
    cfg.battery.maxChargeKw = 80.0f;
    cfg.battery.maxDischargeKw = 100.0f;
    cfg.battery.nominalVoltage = 400.0f;
    cfg.attackDuration = 15.0f;
    cfg.attackCooldown = 30.0f;
    return cfg;
}

HybridSystem::ErsConfig HybridSystem::getRoadPlugIn() {
    ErsConfig cfg;
    cfg.architecture = HybridArchitecture::Road_PlugIn;
    cfg.mguk.maxPowerKw = 150.0f;
    cfg.mguk.maxRegenKw = 100.0f;
    cfg.mguk.maxTorqueNm = 300.0f;
    cfg.mguh.maxPowerKw = 0.0f;
    cfg.mguh.maxHarvestKw = 0.0f;
    cfg.mguh.connectedToTurbo = false;
    cfg.mguh.canTransferToMguk = false;
    cfg.battery.capacityMj = 40.0f;
    cfg.battery.perLapEnergyMj = 100.0f;
    cfg.battery.maxChargeKw = 100.0f;
    cfg.battery.maxDischargeKw = 150.0f;
    cfg.battery.nominalVoltage = 400.0f;
    cfg.battery.internalResistanceOhm = 0.08f;
    cfg.attackDuration = 15.0f;
    cfg.attackCooldown = 30.0f;
    return cfg;
}

HybridSystem::ErsConfig HybridSystem::getElectric() {
    ErsConfig cfg;
    cfg.architecture = HybridArchitecture::Electric;
    cfg.enabled = true;
    cfg.mguk.maxPowerKw = 500.0f;
    cfg.mguk.maxRegenKw = 250.0f;
    cfg.mguk.maxTorqueNm = 800.0f;
    cfg.mguh.maxPowerKw = 0.0f;
    cfg.mguh.maxHarvestKw = 0.0f;
    cfg.mguh.connectedToTurbo = false;
    cfg.mguh.canTransferToMguk = false;
    cfg.battery.capacityMj = 200.0f;
    cfg.battery.perLapEnergyMj = 1000.0f;
    cfg.battery.maxChargeKw = 250.0f;
    cfg.battery.maxDischargeKw = 500.0f;
    cfg.battery.nominalVoltage = 800.0f;
    cfg.battery.internalResistanceOhm = 0.02f;
    cfg.battery.optimalTemp = 30.0f;
    cfg.battery.maxTemp = 55.0f;
    cfg.attackDuration = 30.0f;
    cfg.attackCooldown = 60.0f;
    return cfg;
}

HybridSystem::ErsConfig HybridSystem::getDisabled() {
    ErsConfig cfg;
    cfg.enabled = false;
    cfg.architecture = HybridArchitecture::F1_2014;
    cfg.mguk.maxPowerKw = 0.0f;
    cfg.mguk.maxRegenKw = 0.0f;
    cfg.mguh.maxPowerKw = 0.0f;
    cfg.mguh.maxHarvestKw = 0.0f;
    cfg.battery.perLapEnergyMj = 1000.0f;
    return cfg;
}

// ============================================================================
// Validation
// ============================================================================

bool HybridSystem::validateConfig(const ErsConfig& config, QString* error) {
    if (config.mguk.maxPowerKw < 0) {
        if (error) *error = "MGU-K max power cannot be negative";
        return false;
    }
    if (config.mguk.maxRegenKw < 0) {
        if (error) *error = "MGU-K max regen cannot be negative";
        return false;
    }
    if (config.mguh.maxPowerKw < 0) {
        if (error) *error = "MGU-H max power cannot be negative";
        return false;
    }
    if (config.battery.capacityMj <= 0) {
        if (error) *error = "Battery capacity must be positive";
        return false;
    }
    if (config.battery.maxChargeKw < 0) {
        if (error) *error = "Battery max charge rate cannot be negative";
        return false;
    }
    if (config.battery.maxDischargeKw < 0) {
        if (error) *error = "Battery max discharge rate cannot be negative";
        return false;
    }
    if (config.battery.minSoc < 0 || config.battery.minSoc > 100) {
        if (error) *error = "Battery min SOC must be 0-100";
        return false;
    }
    if (config.battery.maxSoc < 0 || config.battery.maxSoc > 100) {
        if (error) *error = "Battery max SOC must be 0-100";
        return false;
    }
    if (config.mguk.maxPowerKw > 500) {
        if (error) *error = "MGU-K power exceeds realistic maximum (500kW)";
        return false;
    }
    if (config.mguh.maxPowerKw > 200) {
        if (error) *error = "MGU-H power exceeds realistic maximum (200kW)";
        return false;
    }
    return true;
}

// ============================================================================
// Utility
// ============================================================================

QString HybridSystem::getArchitectureName(HybridArchitecture arch) {
    switch (arch) {
    case HybridArchitecture::F1_2014: return "F1 2014-2020";
    case HybridArchitecture::F1_2026: return "F1 2026";
    case HybridArchitecture::LMP1: return "LMP1";
    case HybridArchitecture::LMDh: return "LMDh";
    case HybridArchitecture::Road_Mild: return "Road Mild Hybrid";
    case HybridArchitecture::Road_Full: return "Road Full Hybrid";
    case HybridArchitecture::Road_PlugIn: return "Road Plug-In Hybrid";
    case HybridArchitecture::Electric: return "Full Electric";
    }
    return "Unknown";
}

QString HybridSystem::getModeName(ErsMode mode) {
    switch (mode) {
    case ErsMode::None: return "None";
    case ErsMode::Low: return "Low";
    case ErsMode::Medium: return "Medium";
    case ErsMode::High: return "High";
    case ErsMode::Attack: return "Attack";
    case ErsMode::Overtake: return "Overtake";
    case ErsMode::Qualifying: return "Qualifying";
    case ErsMode::Auto: return "Auto";
    }
    return "Unknown";
}
