#pragma once

#include "PhysicsCoreTypes.h"
#include <QObject>
#include <QVector3D>
#include <array>
#include <cmath>

namespace ks::physics {

// ============================================================================
// Damage Zone Definition (rFactor2-style)
// ============================================================================
// Each zone represents a physical region of the car that can sustain damage

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

// ============================================================================
// Damage Type Flags
// ============================================================================

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

// ============================================================================
// Per-Zone Damage Data
// ============================================================================

struct DamageZoneData {
    float structural = 0.0f;    // Structural integrity (0 = destroyed, 1 = perfect)
    float cosmetic = 0.0f;      // Cosmetic damage (0 = perfect, 1 = destroyed)
    float deformation = 0.0f;   // Visual deformation amount (0-1)
    QVector3D deformationDir;   // Direction of deformation
    float impactEnergy = 0.0f;  // Accumulated impact energy (J)
    DamageType damageTypes;     // Types of damage sustained
    int impactCount = 0;        // Number of impacts in this zone

    // Apply impact to this zone
    void applyImpact(float energy, const QVector3D& direction, DamageType type);

    // Get combined damage (0 = perfect, 1 = destroyed)
    float combinedDamage() const;

    // Get structural integrity (1 = intact, 0 = destroyed)
    float integrity() const { return structural; }
};

// ============================================================================
// Suspension Damage Data (per wheel)
// ============================================================================

struct SuspensionDamageData {
    float geometry = 1.0f;      // Geometry integrity (1 = perfect alignment)
    float armStrength = 1.0f;   // Control arm strength (1 = perfect)
    float dampingLoss = 0.0f;   // Damping effectiveness loss (0 = none, 1 = total)
    float springDamage = 0.0f;  // Spring rate degradation (0 = none, 1 = broken)
    float toeDeviation = 0.0f;  // Toe angle deviation (degrees)
    float camberDeviation = 0.0f; // Camber angle deviation (degrees)
    bool isBroken = false;      // Is suspension completely broken

    // Calculate handling impact multiplier
    float handlingMultiplier() const;
};

// ============================================================================
// Engine Damage Data
// ============================================================================

struct EngineDamageData {
    float health = 1.0f;        // Overall engine health (0 = destroyed)
    float powerLoss = 0.0f;     // Power loss factor (0 = none, 1 = total)
    float overheating = 0.0f;   // Overheating damage (0-1)
    float oilPressureLoss = 0.0f; // Oil pressure loss (0-1)
    float coolantLeak = 0.0f;   // Coolant leak rate (liters/min)
    float misfireRate = 0.0f;   // Engine misfire rate (0-1)
    bool isSeized = false;      // Engine completely seized

    // Calculate effective power
    float effectivePowerMultiplier() const;

    // Calculate fuel consumption increase
    float fuelConsumptionIncrease() const;
};

// ============================================================================
// Aero Damage Data
// ============================================================================

struct AeroDamageData {
    float frontWingDamage = 0.0f;  // Front wing damage (0 = perfect, 1 = destroyed)
    float rearWingDamage = 0.0f;   // Rear wing damage (0 = perfect, 1 = destroyed)
    float diffuserDamage = 0.0f;   // Diffuser damage (0-1)
    float floorDamage = 0.0f;      // Underbody/floor damage (0-1)
    float radiatorDamage = 0.0f;   // Radiator damage (0-1)

    // Calculate downforce loss
    float downforceMultiplier() const;

    // Calculate drag increase
    float dragMultiplier() const;

    // Calculate cooling effectiveness
    float coolingEfficiency() const;
};

// ============================================================================
// Transmission Damage Data
// ============================================================================

struct TransmissionDamageData {
    float health = 1.0f;        // Overall transmission health
    float gearDamage[8] = {0};  // Per-gear damage (0 = perfect, 1 = destroyed)
    float clutchDamage = 0.0f;  // Clutch wear/damage (0-1)
    float diffDamage = 0.0f;    // Differential damage (0-1)
    bool isStuck = false;       // Transmission stuck in gear

    float efficiencyMultiplier() const;
};

// ============================================================================
// Brake Damage Data (per wheel)
// ============================================================================

struct BrakeDamageData {
    float discDamage = 0.0f;    // Brake disc damage (0-1)
    float padWear = 0.0f;       // Pad wear (0 = new, 1 = worn out)
    float caliperDamage = 0.0f; // Caliper damage (0-1)
    float fadeLevel = 0.0f;     // Current fade level (0-1)
    float temperature = 300.0f; // Current brake temperature (C)
    bool isFaded = false;       // Is brake currently faded

    // Calculate braking effectiveness
    float brakingMultiplier() const;

    // Calculate pad remaining life
    float padRemaining() const { return 1.0f - padWear; }
};

// ============================================================================
// Collision Event
// ============================================================================

struct CollisionEvent {
    QVector3D contactPoint;         // World-space contact point
    QVector3D contactNormal;        // Contact normal (direction of impact)
    QVector3D impactVelocity;       // Relative velocity at impact
    float impactEnergy = 0.0f;     // Kinetic energy transferred (J)
    float impactForce = 0.0f;      // Peak force (N)
    DamageZone primaryZone;         // Primary damage zone
    DamageType damageType;          // Type of damage
    float timestamp = 0.0f;        // Simulation time of collision
};

// ============================================================================
// Repair Data
// ============================================================================

struct RepairData {
    float bodyRepairTime = 0.0f;    // Time to repair body (seconds)
    float suspensionRepairTime = 0.0f; // Time to repair suspension
    float engineRepairTime = 0.0f;  // Time to repair engine (0 = replace)
    float aeroRepairTime = 0.0f;    // Time to repair aero parts
    float totalRepairCost = 0.0f;   // Total repair cost (currency units)
    bool needsReplacement[static_cast<size_t>(DamageZone::COUNT)] = {}; // Parts needing replacement

    // Calculate total time
    float totalTime() const;
};

// ============================================================================
// Damage Configuration
// ============================================================================

struct DamageConfig {
    bool enabled = true;
    float globalDamageMultiplier = 1.0f;  // Scale all damage
    float visualDamageMultiplier = 1.0f;  // Scale visual deformation
    float physicsDamageMultiplier = 1.0f;  // Scale physics impact
    float wearMultiplier = 1.0f;           // Scale component wear
    float impactThreshold = 5000.0f;       // Min energy to cause damage (J)
    float maxStructuralDamage = 0.95f;     // Max structural damage before failure
    bool allowPartialRepairs = true;       // Allow repairing individual systems
    float repairCostPerPoint = 100.0f;     // Cost per damage point
};

// ============================================================================
// DamageSystem - Main Damage Manager
// ============================================================================
// rFactor2-style damage model:
// - Per-zone structural and cosmetic damage
// - Component-level damage (suspension, engine, aero, transmission, brakes)
// - Physics impact: CG shift, weight distribution changes, handling degradation
// - Visual damage: mesh deformation, particle effects
// - Repair system: pit stop time and cost

class DamageSystem : public QObject {
    Q_OBJECT

public:
    explicit DamageSystem(QObject* parent = nullptr);
    ~DamageSystem() override;

    // Configuration
    void setConfig(const DamageConfig& config);
    const DamageConfig& config() const { return m_config; }

    // Main update (call each physics step)
    void update(float dt, float speed, float rpm);

    // Collision handling
    void processCollision(const CollisionEvent& event);

    // Apply direct damage (e.g., from wall contact, other car)
    void applyImpactDamage(float energy, const QVector3D& direction,
                           const QVector3D& contactPoint, DamageType type);

    // Per-zone queries
    const DamageZoneData& zoneData(DamageZone zone) const { return m_zones[static_cast<size_t>(zone)]; }
    float zoneIntegrity(DamageZone zone) const { return m_zones[static_cast<size_t>(zone)].structural; }
    float zoneCosmetic(DamageZone zone) const { return m_zones[static_cast<size_t>(zone)].cosmetic; }

    // Per-component queries
    const SuspensionDamageData& suspensionDamage(int wheel) const { return m_suspension[wheel]; }
    const EngineDamageData& engineDamage() const { return m_engine; }
    const AeroDamageData& aeroDamage() const { return m_aero; }
    const TransmissionDamageData& transmissionDamage() const { return m_transmission; }
    const BrakeDamageData& brakeDamage(int wheel) const { return m_brakes[wheel]; }

    // Overall damage
    float overallDamage() const;           // 0 = perfect, 1 = destroyed
    float structuralDamage() const;        // Average structural damage
    float cosmeticDamage() const;          // Average cosmetic damage

    // Physics impact
    float powerMultiplier() const;         // Effective engine power (0-1)
    float handlingMultiplier() const;      // Effective handling (0-1)
    float brakingMultiplier() const;       // Effective braking (0-1)
    float downforceMultiplier() const;     // Effective downforce (0-1)
    float dragMultiplier() const;          // Drag change (>1 = more drag)
    QVector3D cgShift() const;             // Center of gravity shift due to damage

    // Component failure checks
    bool isEngineFailed() const { return m_engine.isSeized; }
    bool isTransmissionFailed() const { return m_transmission.isStuck; }
    bool isSuspensionBroken(int wheel) const { return m_suspension[wheel].isBroken; }
    int totalCollisions() const { return m_totalCollisions; }

    // Repair
    RepairData calculateRepairData() const;
    void repairAll();
    void repairPartial(float fraction);   // Repair fraction of damage (0-1)
    void repairSystem(DamageType type);   // Repair specific system

    // Reset
    void reset();
    void resetZone(DamageZone zone);

    // Serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

signals:
    void collisionOccurred(const CollisionEvent& event);
    void componentFailed(DamageType type);
    void damageChanged(float overallDamage);
    void warningLevelChanged(int level);  // 0=ok, 1=caution, 2=critical

private:
    void updatePhysicsImpact();
    void updateWarningLevel();
    float calculateImpactForce(float energy, float mass) const;
    void distributeDamageToZones(const QVector3D& contactPoint, float energy, DamageType type);

    DamageConfig m_config;

    // Per-zone damage
    std::array<DamageZoneData, static_cast<size_t>(DamageZone::COUNT)> m_zones;

    // Per-component damage
    std::array<SuspensionDamageData, 4> m_suspension;
    EngineDamageData m_engine;
    AeroDamageData m_aero;
    TransmissionDamageData m_transmission;
    std::array<BrakeDamageData, 4> m_brakes;

    // Cached physics impact
    float m_cachedPowerMultiplier = 1.0f;
    float m_cachedHandlingMultiplier = 1.0f;
    float m_cachedBrakingMultiplier = 1.0f;
    float m_cachedDownforceMultiplier = 1.0f;
    float m_cachedDragMultiplier = 1.0f;
    QVector3D m_cachedCgShift;
    int m_warningLevel = 0;
    int m_totalCollisions = 0;
};

} // namespace ks::physics
