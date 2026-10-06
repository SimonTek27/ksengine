#include "DamageSystem.h"
#include <algorithm>
#include <cmath>
#include <algorithm>
#include <cmath>

namespace ks {
namespace physics {

// ============================================================================
// DamageZoneData
// ============================================================================

void DamageZoneData::applyImpact(float energy, const PhysVec3& direction, DamageType type) {
    impactEnergy += energy;
    impactCount++;
    damageTypes = damageTypes | type;

    float normalizedEnergy = std::min(energy / 50000.0f, 1.0f);

    structural = std::max(0.0f, structural - normalizedEnergy * 0.3f);
    cosmetic = std::max(0.0f, cosmetic - normalizedEnergy * 0.5f);
    deformation = std::min(1.0f, deformation + normalizedEnergy * 0.4f);

    deformationDir = direction.normalized();
}

float DamageZoneData::combinedDamage() const {
    return 1.0f - (structural * 0.7f + cosmetic * 0.3f);
}

// ============================================================================
// SuspensionDamageData
// ============================================================================

float SuspensionDamageData::handlingMultiplier() const {
    if (isBroken) return 0.0f;

    float mult = 1.0f;
    mult *= geometry;
    mult *= armStrength;
    mult *= (1.0f - dampingLoss * 0.5f);
    mult *= (1.0f - springDamage * 0.3f);

    float deviationFactor = std::abs(toeDeviation) / 5.0f + std::abs(camberDeviation) / 5.0f;
    mult *= std::max(0.3f, 1.0f - deviationFactor);

    return std::clamp(mult, 0.0f, 1.0f);
}

// ============================================================================
// EngineDamageData
// ============================================================================

float EngineDamageData::effectivePowerMultiplier() const {
    if (isSeized) return 0.0f;

    float mult = health;
    mult *= (1.0f - powerLoss);
    mult *= (1.0f - overheating * 0.4f);
    mult *= (1.0f - misfireRate * 0.3f);

    return std::clamp(mult, 0.05f, 1.0f);
}

float EngineDamageData::fuelConsumptionIncrease() const {
    float increase = 0.0f;
    increase += overheating * 0.2f;
    increase += misfireRate * 0.15f;
    increase += (1.0f - health) * 0.1f;
    return std::clamp(increase, 0.0f, 0.5f);
}

// ============================================================================
// AeroDamageData
// ============================================================================

float AeroDamageData::downforceMultiplier() const {
    float mult = 1.0f;
    mult -= frontWingDamage * 0.35f;
    mult -= rearWingDamage * 0.40f;
    mult -= diffuserDamage * 0.20f;
    mult -= floorDamage * 0.15f;
    return std::clamp(mult, 0.1f, 1.0f);
}

float AeroDamageData::dragMultiplier() const {
    float mult = 1.0f;
    mult += frontWingDamage * 0.15f;
    mult += rearWingDamage * 0.10f;
    mult += floorDamage * 0.20f;
    return std::clamp(mult, 1.0f, 2.0f);
}

float AeroDamageData::coolingEfficiency() const {
    float eff = 1.0f;
    eff -= radiatorDamage * 0.6f;
    eff -= floorDamage * 0.1f;
    return std::clamp(eff, 0.2f, 1.0f);
}

// ============================================================================
// TransmissionDamageData
// ============================================================================

float TransmissionDamageData::efficiencyMultiplier() const {
    if (isStuck) return 0.5f;

    float mult = health;
    float worstGearDamage = 0.0f;
    for (int i = 0; i < 8; ++i) {
        worstGearDamage = std::max(worstGearDamage, gearDamage[i]);
    }
    mult *= (1.0f - worstGearDamage * 0.3f);
    mult *= (1.0f - clutchDamage * 0.2f);
    mult *= (1.0f - diffDamage * 0.2f);

    return std::clamp(mult, 0.3f, 1.0f);
}

// ============================================================================
// BrakeDamageData
// ============================================================================

float BrakeDamageData::brakingMultiplier() const {
    float mult = 1.0f;
    mult *= (1.0f - discDamage * 0.4f);
    mult *= (1.0f - padWear * 0.6f);
    mult *= (1.0f - caliperDamage * 0.5f);
    mult *= (1.0f - fadeLevel * 0.3f);

    return std::clamp(mult, 0.1f, 1.0f);
}

// ============================================================================
// RepairData
// ============================================================================

float RepairData::totalTime() const {
    return bodyRepairTime + suspensionRepairTime + engineRepairTime + aeroRepairTime;
}

// ============================================================================
// DamageSystem
// ============================================================================

DamageSystem::DamageSystem()
{
    reset();
}


void DamageSystem::setConfig(const DamageConfig& config) {
    m_config = config;
}

void DamageSystem::update(float dt, float speed, float rpm) {
    if (!m_config.enabled) return;

    updatePhysicsImpact();
    updateWarningLevel();
}

void DamageSystem::processCollision(const CollisionEvent& event) {
    if (!m_config.enabled) return;

    float scaledEnergy = event.impactEnergy * m_config.globalDamageMultiplier;

    if (scaledEnergy < m_config.impactThreshold) return;

    distributeDamageToZones(event.contactPoint, scaledEnergy, event.damageType);

    float force = calculateImpactForce(scaledEnergy, 1500.0f);
    CollisionEvent scaledEvent = event;
    scaledEvent.impactEnergy = scaledEnergy;
    scaledEvent.impactForce = force;

    m_totalCollisions++;
    if (onCollisionOccurred) onCollisionOccurred(scaledEvent);
}

void DamageSystem::applyImpactDamage(float energy, const PhysVec3& direction,
                                      const PhysVec3& contactPoint, DamageType type) {
    if (!m_config.enabled) return;

    float scaledEnergy = energy * m_config.globalDamageMultiplier;
    if (scaledEnergy < m_config.impactThreshold) return;

    distributeDamageToZones(contactPoint, scaledEnergy, type);
}

void DamageSystem::distributeDamageToZones(const PhysVec3& contactPoint, float energy, DamageType type) {
    float frontBias = std::clamp((contactPoint.z + 2.0f) / 4.0f, 0.0f, 1.0f);
    float leftBias = std::clamp((contactPoint.x + 1.0f) / 2.0f, 0.0f, 1.0f);
    float heightBias = std::clamp(contactPoint.y / 1.0f, 0.0f, 1.0f);

    // Front zones
    float frontLeftWeight = frontBias * leftBias * 0.5f;
    float frontCenterWeight = frontBias * (1.0f - std::abs(leftBias - 0.5f) * 2.0f) * 0.6f;
    float frontRightWeight = frontBias * (1.0f - leftBias) * 0.5f;

    // Rear zones
    float rearLeftWeight = (1.0f - frontBias) * leftBias * 0.4f;
    float rearCenterWeight = (1.0f - frontBias) * (1.0f - std::abs(leftBias - 0.5f) * 2.0f) * 0.5f;
    float rearRightWeight = (1.0f - frontBias) * (1.0f - leftBias) * 0.4f;

    // Side zones
    float leftSideWeight = leftBias * (1.0f - frontBias) * 0.3f;
    float rightSideWeight = (1.0f - leftBias) * (1.0f - frontBias) * 0.3f;

    // Underbody (higher weight for low contact points)
    float underbodyWeight = (1.0f - heightBias) * 0.3f;

    // Engine bay (central front)
    float engineBayWeight = frontBias * (1.0f - std::abs(leftBias - 0.5f) * 2.0f) * 0.4f;

    // Cabin (central)
    float cabinWeight = (1.0f - std::abs(frontBias - 0.5f) * 2.0f) *
                        (1.0f - std::abs(leftBias - 0.5f) * 2.0f) * 0.3f;

    // Apply damage to zones
    PhysVec3 direction = -contactPoint.normalized();

    auto applyToZone = [&](DamageZone zone, float weight) {
        if (weight > 0.01f) {
            m_zones[static_cast<size_t>(zone)].applyImpact(energy * weight, direction, type);
        }
    };

    applyToZone(DamageZone::FrontLeft, frontLeftWeight);
    applyToZone(DamageZone::FrontCenter, frontCenterWeight);
    applyToZone(DamageZone::FrontRight, frontRightWeight);
    applyToZone(DamageZone::RearLeft, rearLeftWeight);
    applyToZone(DamageZone::RearCenter, rearCenterWeight);
    applyToZone(DamageZone::RearRight, rearRightWeight);
    applyToZone(DamageZone::LeftSide, leftSideWeight);
    applyToZone(DamageZone::RightSide, rightSideWeight);
    applyToZone(DamageZone::EngineBay, engineBayWeight);
    applyToZone(DamageZone::Cabin, cabinWeight);
    applyToZone(DamageZone::Underbody, underbodyWeight);

    // Distribute to components based on zone damage
    float normalizedEnergy = std::min(energy / 50000.0f, 1.0f) * m_config.physicsDamageMultiplier;

    // Front zones affect suspension, aero, steering
    if (frontLeftWeight + frontRightWeight > 0.2f) {
        float frontDmg = normalizedEnergy * (frontLeftWeight + frontRightWeight);
        m_suspension[0].armStrength = std::max(0.0f, m_suspension[0].armStrength - frontDmg * 0.3f);
        m_suspension[0].geometry = std::max(0.0f, m_suspension[0].geometry - frontDmg * 0.2f);
        m_suspension[1].armStrength = std::max(0.0f, m_suspension[1].armStrength - frontDmg * 0.3f);
        m_suspension[1].geometry = std::max(0.0f, m_suspension[1].geometry - frontDmg * 0.2f);
        m_aero.frontWingDamage = std::min(1.0f, m_aero.frontWingDamage + frontDmg * 0.4f);
    }

    // Rear zones affect suspension, aero, exhaust
    if (rearLeftWeight + rearRightWeight > 0.2f) {
        float rearDmg = normalizedEnergy * (rearLeftWeight + rearRightWeight);
        m_suspension[2].armStrength = std::max(0.0f, m_suspension[2].armStrength - rearDmg * 0.3f);
        m_suspension[3].armStrength = std::max(0.0f, m_suspension[3].armStrength - rearDmg * 0.3f);
        m_aero.rearWingDamage = std::min(1.0f, m_aero.rearWingDamage + rearDmg * 0.3f);
    }

    // Engine bay affects engine, cooling
    if (engineBayWeight > 0.1f) {
        float engDmg = normalizedEnergy * engineBayWeight;
        m_engine.health = std::max(0.0f, m_engine.health - engDmg * 0.3f);
        m_engine.overheating = std::min(1.0f, m_engine.overheating + engDmg * 0.2f);
        m_aero.radiatorDamage = std::min(1.0f, m_aero.radiatorDamage + engDmg * 0.25f);
    }

    // Underbody affects floor, diffuser
    if (underbodyWeight > 0.1f) {
        float floorDmg = normalizedEnergy * underbodyWeight;
        m_aero.floorDamage = std::min(1.0f, m_aero.floorDamage + floorDmg * 0.35f);
        m_aero.diffuserDamage = std::min(1.0f, m_aero.diffuserDamage + floorDmg * 0.25f);
    }

    // Check for component failures
    if (m_engine.health <= 0.05f) {
        m_engine.isSeized = true;
        if (onComponentFailed) onComponentFailed(DamageType::Engine);
    }

    for (int i = 0; i < 4; ++i) {
        if (m_suspension[i].armStrength <= 0.1f) {
            m_suspension[i].isBroken = true;
            if (onComponentFailed) onComponentFailed(DamageType::Suspension);
        }
    }

    if (onDamageChanged) onDamageChanged(overallDamage());
}

float DamageSystem::calculateImpactForce(float energy, float mass) const {
    // F = sqrt(2 * E * k) where k is effective stiffness
    float effectiveStiffness = 500000.0f; // N/m
    return std::sqrt(2.0f * energy * effectiveStiffness);
}

void DamageSystem::updatePhysicsImpact() {
    m_cachedPowerMultiplier = m_engine.effectivePowerMultiplier() *
                              m_transmission.efficiencyMultiplier();

    m_cachedHandlingMultiplier = 1.0f;
    for (int i = 0; i < 4; ++i) {
        m_cachedHandlingMultiplier = std::min(m_cachedHandlingMultiplier,
                                              m_suspension[i].handlingMultiplier());
    }

    m_cachedBrakingMultiplier = 1.0f;
    for (int i = 0; i < 4; ++i) {
        m_cachedBrakingMultiplier = std::min(m_cachedBrakingMultiplier,
                                             m_brakes[i].brakingMultiplier());
    }

    m_cachedDownforceMultiplier = m_aero.downforceMultiplier();
    m_cachedDragMultiplier = m_aero.dragMultiplier();

    // CG shift: damaged zones shift weight
    PhysVec3 totalShift;
    float totalDamage = 0.0f;

    // Front-rear shift
    float frontDamage = (m_zones[0].combinedDamage() + m_zones[1].combinedDamage() +
                         m_zones[2].combinedDamage()) / 3.0f;
    float rearDamage = (m_zones[3].combinedDamage() + m_zones[4].combinedDamage() +
                        m_zones[5].combinedDamage()) / 3.0f;
    totalShift.z = ((frontDamage - rearDamage) * 0.1f);

    // Left-right shift
    float leftDamage = (m_zones[0].combinedDamage() + m_zones[3].combinedDamage() +
                        m_zones[6].combinedDamage()) / 3.0f;
    float rightDamage = (m_zones[2].combinedDamage() + m_zones[5].combinedDamage() +
                         m_zones[7].combinedDamage()) / 3.0f;
    totalShift.x = ((leftDamage - rightDamage) * 0.05f);

    m_cachedCgShift = totalShift;
}

void DamageSystem::updateWarningLevel() {
    float overall = overallDamage();
    int newLevel = 0;

    if (overall > 0.8f || m_engine.isSeized) {
        newLevel = 2; // Critical
    } else if (overall > 0.3f || m_engine.health < 0.5f) {
        newLevel = 1; // Caution
    }

    if (newLevel != m_warningLevel) {
        m_warningLevel = newLevel;
        if (onWarningLevelChanged) onWarningLevelChanged(m_warningLevel);
    }
}

float DamageSystem::overallDamage() const {
    float zoneDmg = 0.0f;
    for (const auto& zone : m_zones) {
        zoneDmg += zone.combinedDamage();
    }
    zoneDmg /= static_cast<float>(m_zones.size());

    float componentDmg = 0.0f;
    componentDmg += (1.0f - m_engine.health) * 0.3f;
    componentDmg += (1.0f - m_aero.downforceMultiplier()) * 0.2f;
    componentDmg += (1.0f - m_transmission.health) * 0.2f;
    for (int i = 0; i < 4; ++i) {
        componentDmg += (1.0f - m_suspension[i].handlingMultiplier()) * 0.075f;
    }

    return std::clamp(zoneDmg * 0.6f + componentDmg * 0.4f, 0.0f, 1.0f);
}

float DamageSystem::structuralDamage() const {
    float total = 0.0f;
    for (const auto& zone : m_zones) {
        total += (1.0f - zone.structural);
    }
    return total / static_cast<float>(m_zones.size());
}

float DamageSystem::cosmeticDamage() const {
    float total = 0.0f;
    for (const auto& zone : m_zones) {
        total += zone.cosmetic;
    }
    return total / static_cast<float>(m_zones.size());
}

float DamageSystem::powerMultiplier() const { return m_cachedPowerMultiplier; }
float DamageSystem::handlingMultiplier() const { return m_cachedHandlingMultiplier; }
float DamageSystem::brakingMultiplier() const { return m_cachedBrakingMultiplier; }
float DamageSystem::downforceMultiplier() const { return m_cachedDownforceMultiplier; }
float DamageSystem::dragMultiplier() const { return m_cachedDragMultiplier; }
PhysVec3 DamageSystem::cgShift() const { return m_cachedCgShift; }

RepairData DamageSystem::calculateRepairData() const {
    RepairData repair;

    // Body repair time
    float bodyDmg = structuralDamage();
    repair.bodyRepairTime = bodyDmg * 300.0f; // 5 min per 10% damage

    // Suspension repair
    float suspDmg = 0.0f;
    for (int i = 0; i < 4; ++i) {
        suspDmg += (1.0f - m_suspension[i].handlingMultiplier());
    }
    suspDmg /= 4.0f;
    repair.suspensionRepairTime = suspDmg * 180.0f; // 3 min per 10% damage

    // Engine repair
    float engDmg = 1.0f - m_engine.health;
    if (m_engine.isSeized) {
        repair.engineRepairTime = 600.0f; // 10 min for full replacement
    } else {
        repair.engineRepairTime = engDmg * 420.0f; // 7 min per 10% damage
    }

    // Aero repair
    float aeroDmg = m_aero.frontWingDamage + m_aero.rearWingDamage +
                    m_aero.diffuserDamage + m_aero.floorDamage;
    aeroDmg /= 4.0f;
    repair.aeroRepairTime = aeroDmg * 120.0f; // 2 min per 10% damage

    // Cost
    float totalDmg = overallDamage();
    repair.totalRepairCost = totalDmg * 1000.0f * m_config.repairCostPerPoint;

    // Check for parts needing replacement
    for (size_t i = 0; i < static_cast<size_t>(DamageZone::COUNT); ++i) {
        repair.needsReplacement[i] = m_zones[i].structural < 0.2f;
    }

    return repair;
}

void DamageSystem::repairAll() {
    for (auto& zone : m_zones) {
        zone.structural = 1.0f;
        zone.cosmetic = 0.0f;
        zone.deformation = 0.0f;
        zone.impactEnergy = 0.0f;
        zone.damageTypes = DamageType::None;
        zone.impactCount = 0;
    }

    for (auto& susp : m_suspension) {
        susp.geometry = 1.0f;
        susp.armStrength = 1.0f;
        susp.dampingLoss = 0.0f;
        susp.springDamage = 0.0f;
        susp.toeDeviation = 0.0f;
        susp.camberDeviation = 0.0f;
        susp.isBroken = false;
    }

    m_engine = EngineDamageData{};
    m_aero = AeroDamageData{};
    m_transmission = TransmissionDamageData{};

    for (auto& brake : m_brakes) {
        brake = BrakeDamageData{};
    }

    m_totalCollisions = 0;
    updatePhysicsImpact();
}

void DamageSystem::repairPartial(float fraction) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);

    for (auto& zone : m_zones) {
        zone.structural = std::min(1.0f, zone.structural + fraction * (1.0f - zone.structural));
        zone.cosmetic *= (1.0f - fraction * 0.8f);
        zone.deformation *= (1.0f - fraction * 0.7f);
    }

    for (auto& susp : m_suspension) {
        susp.armStrength = std::min(1.0f, susp.armStrength + fraction * (1.0f - susp.armStrength));
        susp.geometry = std::min(1.0f, susp.geometry + fraction * (1.0f - susp.geometry));
        if (susp.isBroken && fraction > 0.5f) susp.isBroken = false;
    }

    m_engine.health = std::min(1.0f, m_engine.health + fraction * (1.0f - m_engine.health));
    m_engine.overheating *= (1.0f - fraction);
    if (m_engine.isSeized && fraction > 0.8f) m_engine.isSeized = false;

    m_aero.frontWingDamage *= (1.0f - fraction);
    m_aero.rearWingDamage *= (1.0f - fraction);
    m_aero.diffuserDamage *= (1.0f - fraction);
    m_aero.floorDamage *= (1.0f - fraction);
    m_aero.radiatorDamage *= (1.0f - fraction);

    m_transmission.health = std::min(1.0f, m_transmission.health + fraction * (1.0f - m_transmission.health));

    for (auto& brake : m_brakes) {
        brake.padWear *= (1.0f - fraction * 0.5f);
        brake.discDamage *= (1.0f - fraction * 0.3f);
        brake.fadeLevel *= (1.0f - fraction);
    }

    updatePhysicsImpact();
}

void DamageSystem::repairSystem(DamageType type) {
    float repairAmount = 0.8f;

    if (hasFlag(type, DamageType::Engine)) {
        m_engine.health = std::min(1.0f, m_engine.health + repairAmount);
        m_engine.overheating *= 0.2f;
        m_engine.misfireRate *= 0.2f;
        if (m_engine.isSeized) m_engine.isSeized = false;
    }

    if (hasFlag(type, DamageType::Suspension)) {
        for (auto& susp : m_suspension) {
            susp.armStrength = std::min(1.0f, susp.armStrength + repairAmount);
            susp.geometry = std::min(1.0f, susp.geometry + repairAmount * 0.8f);
            susp.isBroken = false;
        }
    }

    if (hasFlag(type, DamageType::Aero)) {
        m_aero.frontWingDamage *= 0.2f;
        m_aero.rearWingDamage *= 0.2f;
        m_aero.diffuserDamage *= 0.2f;
        m_aero.floorDamage *= 0.2f;
    }

    if (hasFlag(type, DamageType::Transmission)) {
        m_transmission.health = std::min(1.0f, m_transmission.health + repairAmount);
        m_transmission.isStuck = false;
    }

    if (hasFlag(type, DamageType::Brakes)) {
        for (auto& brake : m_brakes) {
            brake.padWear *= 0.2f;
            brake.discDamage *= 0.3f;
            brake.fadeLevel = 0.0f;
        }
    }

    updatePhysicsImpact();
}

void DamageSystem::reset() {
    for (auto& zone : m_zones) {
        zone = DamageZoneData{};
        zone.structural = 1.0f;
    }
    m_suspension.fill(SuspensionDamageData{});
    m_engine = EngineDamageData{};
    m_aero = AeroDamageData{};
    m_transmission = TransmissionDamageData{};
    m_brakes.fill(BrakeDamageData{});
    m_totalCollisions = 0;
    updatePhysicsImpact();
}

void DamageSystem::resetZone(DamageZone zone) {
    auto& z = m_zones[static_cast<size_t>(zone)];
    z.structural = 1.0f;
    z.cosmetic = 0.0f;
    z.deformation = 0.0f;
    z.impactEnergy = 0.0f;
    z.damageTypes = DamageType::None;
    z.impactCount = 0;
    updatePhysicsImpact();
}

/* toJson removed (Qt-free) */

/* fromJson removed (Qt-free) */

} // namespace physics
} // namespace ks
