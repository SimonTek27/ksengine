#pragma once

#include "PhysicsCoreTypes.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <cmath>

namespace ks {
namespace physics {

enum class DamageZone {
    FrontLeft = 0,
    FrontCenter,
    FrontRight,
    RearLeft,
    RearCenter,
    RearRight,
    LeftSide,
    RightSide,
    EngineBay,
    Cabin,
    Underbody,
    COUNT
};

enum class DamageType : uint32_t {
    None        = 0,
    BodyPanel   = 1 << 0,
    Suspension  = 1 << 1,
    Aero        = 1 << 2,
    Engine      = 1 << 3,
    Transmission = 1 << 4,
    Cooling     = 1 << 5,
    FuelSystem  = 1 << 6,
    Brakes      = 1 << 7,
    Steering    = 1 << 8,
    Exhaust     = 1 << 9,
    Electrical  = 1 << 10,
};

inline DamageType operator|(DamageType a, DamageType b) {
    return static_cast<DamageType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline DamageType operator&(DamageType a, DamageType b) {
    return static_cast<DamageType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool hasFlag(DamageType flags, DamageType flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

struct DamageZoneData {
    float structural = 0.0f;
    float cosmetic = 0.0f;
    float deformation = 0.0f;
    PhysVec3 deformationDir;
    float impactEnergy = 0.0f;
    DamageType damageTypes = DamageType::None;
    int impactCount = 0;

    void applyImpact(float energy, const PhysVec3& direction, DamageType type);
    float combinedDamage() const;
    float integrity() const { return structural; }
};

struct SuspensionDamageData {
    float geometry = 1.0f;
    float armStrength = 1.0f;
    float dampingLoss = 0.0f;
    float springDamage = 0.0f;
    float toeDeviation = 0.0f;
    float camberDeviation = 0.0f;
    bool isBroken = false;
    float handlingMultiplier() const;
};

struct EngineDamageData {
    float health = 1.0f;
    float powerLoss = 0.0f;
    float overheating = 0.0f;
    float oilPressureLoss = 0.0f;
    float coolantLeak = 0.0f;
    float misfireRate = 0.0f;
    bool isSeized = false;
    float effectivePowerMultiplier() const;
    float fuelConsumptionIncrease() const;
};

struct AeroDamageData {
    float frontWingDamage = 0.0f;
    float rearWingDamage = 0.0f;
    float diffuserDamage = 0.0f;
    float floorDamage = 0.0f;
    float radiatorDamage = 0.0f;
    float downforceMultiplier() const;
    float dragMultiplier() const;
    float coolingEfficiency() const;
};

struct TransmissionDamageData {
    float health = 1.0f;
    float gearDamage[8] = {0};
    float clutchDamage = 0.0f;
    float diffDamage = 0.0f;
    bool isStuck = false;
    float efficiencyMultiplier() const;
};

struct BrakeDamageData {
    float discDamage = 0.0f;
    float padWear = 0.0f;
    float caliperDamage = 0.0f;
    float fadeLevel = 0.0f;
    float temperature = 300.0f;
    bool isFaded = false;
    float brakingMultiplier() const;
    float padRemaining() const { return 1.0f - padWear; }
};

struct CollisionEvent {
    PhysVec3 contactPoint;
    PhysVec3 contactNormal;
    PhysVec3 impactVelocity;
    float impactEnergy = 0.0f;
    float impactForce = 0.0f;
    DamageZone primaryZone = DamageZone::FrontCenter;
    DamageType damageType = DamageType::BodyPanel;
    float timestamp = 0.0f;
};

struct RepairData {
    float bodyRepairTime = 0.0f;
    float suspensionRepairTime = 0.0f;
    float engineRepairTime = 0.0f;
    float aeroRepairTime = 0.0f;
    float totalRepairCost = 0.0f;
    bool needsReplacement[static_cast<size_t>(DamageZone::COUNT)] = {};
    float totalTime() const;
};

struct DamageConfig {
    bool enabled = true;
    float globalDamageMultiplier = 1.0f;
    float visualDamageMultiplier = 1.0f;
    float physicsDamageMultiplier = 1.0f;
    float wearMultiplier = 1.0f;
    float impactThreshold = 5000.0f;
    float maxStructuralDamage = 0.95f;
    bool allowPartialRepairs = true;
    float repairCostPerPoint = 100.0f;
};

class DamageSystem {
public:
    DamageSystem();
    ~DamageSystem() = default;

    void setConfig(const DamageConfig& config);
    const DamageConfig& config() const { return m_config; }

    void update(float dt, float speed, float rpm);
    void processCollision(const CollisionEvent& event);
    void applyImpactDamage(float energy, const PhysVec3& direction,
                           const PhysVec3& contactPoint, DamageType type);

    const DamageZoneData& zoneData(DamageZone zone) const { return m_zones[static_cast<size_t>(zone)]; }
    float zoneIntegrity(DamageZone zone) const { return m_zones[static_cast<size_t>(zone)].structural; }
    float zoneCosmetic(DamageZone zone) const { return m_zones[static_cast<size_t>(zone)].cosmetic; }

    const SuspensionDamageData& suspensionDamage(int wheel) const { return m_suspension[wheel]; }
    const EngineDamageData& engineDamage() const { return m_engine; }
    const AeroDamageData& aeroDamage() const { return m_aero; }
    const TransmissionDamageData& transmissionDamage() const { return m_transmission; }
    const BrakeDamageData& brakeDamage(int wheel) const { return m_brakes[wheel]; }

    float overallDamage() const;
    float structuralDamage() const;
    float cosmeticDamage() const;

    float powerMultiplier() const;
    float handlingMultiplier() const;
    float brakingMultiplier() const;
    float downforceMultiplier() const;
    float dragMultiplier() const;
    PhysVec3 cgShift() const;

    bool isEngineFailed() const { return m_engine.isSeized; }
    bool isTransmissionFailed() const { return m_transmission.isStuck; }
    bool isSuspensionBroken(int wheel) const { return m_suspension[wheel].isBroken; }
    int totalCollisions() const { return m_totalCollisions; }

    RepairData calculateRepairData() const;
    void repairAll();
    void repairPartial(float fraction);
    void repairSystem(DamageType type);

    void reset();
    void resetZone(DamageZone zone);

    std::function<void(const CollisionEvent& event)> onCollisionOccurred;
    std::function<void(DamageType type)> onComponentFailed;
    std::function<void(float overallDamage)> onDamageChanged;
    std::function<void(int level)> onWarningLevelChanged;

private:
    void updatePhysicsImpact();
    void updateWarningLevel();
    float calculateImpactForce(float energy, float mass) const;
    void distributeDamageToZones(const PhysVec3& contactPoint, float energy, DamageType type);

    DamageConfig m_config;
    std::array<DamageZoneData, static_cast<size_t>(DamageZone::COUNT)> m_zones;
    std::array<SuspensionDamageData, 4> m_suspension;
    EngineDamageData m_engine;
    AeroDamageData m_aero;
    TransmissionDamageData m_transmission;
    std::array<BrakeDamageData, 4> m_brakes;

    float m_cachedPowerMultiplier = 1.0f;
    float m_cachedHandlingMultiplier = 1.0f;
    float m_cachedBrakingMultiplier = 1.0f;
    float m_cachedDownforceMultiplier = 1.0f;
    float m_cachedDragMultiplier = 1.0f;
    PhysVec3 m_cachedCgShift;
    int m_warningLevel = 0;
    int m_totalCollisions = 0;
};

} // namespace physics
} // namespace ks
