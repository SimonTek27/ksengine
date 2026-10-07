#pragma once

/**
 * @file PhysicsCoreTypes.h
 * @brief Centralized core type definitions for all physics modules
 * @copyright KS Physics Engine
 *
 * This file contains all fundamental physics types that are shared across
 * multiple modules. It should be the single source of truth for these types.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ks {
namespace physics {

// ============================================================================
// Math (single source of truth for physics modules)
// ============================================================================

struct PhysVec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    PhysVec3() = default;
    PhysVec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float lengthSquared() const { return x * x + y * y + z * z; }

    PhysVec3 normalized() const {
        const float len = length();
        if (len < 1e-8f) return PhysVec3{};
        return PhysVec3{x / len, y / len, z / len};
    }

    PhysVec3 operator+(const PhysVec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    PhysVec3 operator-() const { return {-x, -y, -z}; }
    PhysVec3 operator-(const PhysVec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    PhysVec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    PhysVec3& operator+=(const PhysVec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    PhysVec3& operator-=(const PhysVec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
};

inline PhysVec3 operator*(float s, const PhysVec3& v) { return v * s; }
inline PhysVec3 operator/(const PhysVec3& v, float s) {
    return PhysVec3{v.x / s, v.y / s, v.z / s};
}
inline PhysVec3 cross(const PhysVec3& a, const PhysVec3& b) {
    return PhysVec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}
inline float dot(const PhysVec3& a, const PhysVec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// ============================================================================
// Physical Constants
// ============================================================================

namespace Constants {
    // Physical constants
    constexpr float GRAVITY = 9.81f;
    constexpr float DEFAULT_AIR_DENSITY = 1.225f;
    constexpr float PI = 3.14159265358979323846f;
    constexpr float TWO_PI = 2.0f * PI;
    constexpr float DEG_TO_RAD = PI / 180.0f;
    constexpr float RAD_TO_DEG = 180.0f / PI;
    
    // Vehicle defaults
    constexpr float DEFAULT_WHEEL_RADIUS = 0.33f;
    constexpr float DEFAULT_MASS = 1500.0f;
    constexpr float DEFAULT_WHEELBASE = 2.7f;
    constexpr float DEFAULT_TRACK_WIDTH = 1.6f;
    constexpr float DEFAULT_CG_HEIGHT = 0.45f;
    constexpr float DEFAULT_FRONT_AXLE_DIST = 1.35f;
    constexpr float DEFAULT_REAR_AXLE_DIST = 1.35f;
    
    // Tire defaults
    constexpr float OPTIMAL_TIRE_TEMP = 80.0f;
    constexpr float DEFAULT_TIRE_PRESSURE = 2.2f;
    constexpr float DEFAULT_PEAK_SLIP_ANGLE = 8.0f;
    constexpr float DEFAULT_PEAK_SLIP_RATIO = 0.12f;
    constexpr float DEFAULT_CORNERING_STIFFNESS = 80000.0f;
    
    // Brake defaults
    constexpr float OPTIMAL_BRAKE_TEMP = 300.0f;
    constexpr float DEFAULT_BRAKE_BIAS = 60.0f;
    
    // Engine defaults
    constexpr float DEFAULT_MAX_RPM = 7500.0f;
    constexpr float DEFAULT_IDLE_RPM = 800.0f;
    constexpr float DEFAULT_PEAK_TORQUE_RPM = 4000.0f;
    
    // Safety limits
    constexpr float MAX_REASONABLE_SPEED = 400.0f; // m/s
    constexpr float MAX_REASONABLE_RPM = 20000.0f;
    constexpr float MAX_REASONABLE_FORCE = 100000.0f; // N
    constexpr float MAX_REASONABLE_TORQUE = 10000.0f; // Nm
    
    // Simulation limits
    constexpr float MIN_TIMESTEP = 0.0001f;
    constexpr float MAX_TIMESTEP = 0.02f;
    constexpr float DEFAULT_TIMESTEP = 0.001f;
    constexpr int MAX_INTEGRATION_STEPS = 10;
}

// ============================================================================
// Weather State
// ============================================================================

/**
 * @brief Complete weather state for simulation
 */
struct WeatherState {
    float ambientTemp = 26.0f;        ///< Ambient temperature in Celsius
    float trackTemp = 30.0f;          ///< Track surface temperature in Celsius
    float airDensity = Constants::DEFAULT_AIR_DENSITY; ///< Air density in kg/m³
    float trackWetness = 0.0f;        ///< Track wetness factor (0= dry, 1= wet)
    float rainIntensity = 0.0f;       ///< Rain intensity in mm/h
    float windSpeed = 0.0f;           ///< Wind speed in m/s
    float windDirection = 0.0f;       ///< Wind direction in degrees (0= North)
    float humidity = 0.5f;            ///< Relative humidity (0-1)
    float cloudCover = 0.0f;          ///< Cloud cover (0-1)
    
    /// Calculate grip reduction due to wet conditions
    /// Matches WeatherSimulator::update() formula
    float gripReduction() const {
        float risk = aquaplaningRisk();
        float reduction = trackWetness * 0.3f + risk * 0.2f;
        return std::clamp(reduction, 0.0f, 0.8f);
    }
    
    /// Calculate aquaplaning risk (0-1)
    /// Matches WeatherSimulator::update() formula
    float aquaplaningRisk() const {
        float risk = trackWetness * 0.5f + rainIntensity * 0.001f;
        return std::clamp(risk, 0.0f, 1.0f);
    }
    
    /// Check if conditions are suitable for dry tires
    bool isDry() const { return trackWetness < 0.1f && rainIntensity < 0.1f; }
    
    /// Check if conditions require wet tires
    bool isWet() const { return trackWetness > 0.3f || rainIntensity > 0.5f; }
};

// ============================================================================
// Simulation State
// ============================================================================

/**
 * @brief Complete simulation state for any physics object
 */
struct SimulationState {
    // Position and movement
    PhysVec3 position;               ///< World position in meters
    PhysVec3 velocity;               ///< Velocity in m/s
    PhysVec3 acceleration;           ///< Acceleration in m/s²
    PhysVec3 angularVelocity;        ///< Angular velocity in rad/s
    PhysVec3 rotation;               ///< Euler angles in radians
    float heading = 0.0f;             ///< Heading angle in radians
    
    // Vehicle-specific
    float mass = 1200.0f;           ///< Vehicle mass kg
    float speed = 0.0f;               ///< Total speed in m/s
    float rpm = 0.0f;                 ///< Engine RPM
    int gear = 1;                     ///< Current gear
    float throttle = 0.0f;            ///< Throttle position (0-1)
    float brake = 0.0f;               ///< Brake position (0-1)
    float steering = 0.0f;            ///< Steering angle (-1 to 1)
    float fuel = 100.0f;              ///< Fuel remaining in liters
    
    // Lap timing
    float currentLapDistance = 0.0f;  ///< Distance traveled in current lap
    float lapTime = 0.0f;             ///< Current lap time in seconds
    float bestLapTime = 1e9f;         ///< Best lap time in seconds
    float sector1Time = 0.0f;         ///< Sector 1 time
    float sector2Time = 0.0f;         ///< Sector 2 time
    float sector3Time = 0.0f;         ///< Sector 3 time
    
    // Handling metrics (Milliken / SAE) — filled by VehicleSimulator
    float sideslipBeta = 0.0f;        ///< Vehicle sideslip β at CG (rad)
    float yawRate = 0.0f;             ///< Yaw rate r (rad/s)
    float understeerGradientDegG = 0.0f; ///< UG estimate (deg/g), >0 understeer
    float staticMargin = 0.0f;        ///< Static margin (−1..1), >0 understeer bias
    float lltdFront = 0.55f;          ///< Elastic lateral load transfer front fraction
    float yawAccel = 0.0f;            ///< Yaw acceleration ṙ (rad/s²)
    float yawDampingNr = 0.0f;        ///< Yaw damping derivative N_r (N·m·s/rad)
    float rollAngle = 0.0f;           ///< Body roll φ (rad, quasi-static)
    float aquaplaneFactor = 0.0f;     ///< 0..1 hydroplaning severity (G11)
    float effectiveMu = 1.0f;         ///< μ used this step after wet/aqua
    float waterDepthMm = 0.0f;        ///< proxy water film mm
    float aeroBalanceFront = 0.42f;   ///< front DF fraction
    float fltFront = 0.0f;            ///< (FzL−FzR)/Fz_axle front
    float fltRear = 0.0f;             ///< (FzL−FzR)/Fz_axle rear
    float diffLockTorque = 0.0f;      ///< LSD locking torque N·m
    float diffSlip = 0.0f;            ///< (ωL−ωR)/avg rear
    float tireCamber[4] = {0,0,0,0};  ///< camber rad FL..RR from suspension
    float brakeDiscTemp[4] = {80,80,80,80}; ///< disc °C
    float brakeFade[4] = {0,0,0,0};   ///< 0..1 torque fade
    float aeroDownforce = 0.0f;       ///< total DF N
    float aeroDrag = 0.0f;            ///< drag N
    float rideHeightFrontM = 0.05f;
    float rideHeightRearM = 0.07f;
    float pitchAngle = 0.0f;         ///< quasi-static pitch rad

    // Tire force telemetry (Milliken friction circle) — FL,FR,RL,RR
    float tireFx[4] = {0,0,0,0};      ///< Longitudinal force N
    float tireFy[4] = {0,0,0,0};      ///< Lateral force N
    float tireFz[4] = {0,0,0,0};      ///< Normal load N
    float frictionCircleUsage[4] = {0,0,0,0}; ///< |F|/ (μ Fz), 0..~1.2
    float slipAngle[4] = {0,0,0,0};   ///< α rad
    float slipRatioState[4] = {0,0,0,0}; ///< κ

    // Performance metrics
    double maxSpeed = 0.0;            ///< Maximum speed achieved
    double avgSpeed = 0.0;            ///< Average speed
    double tyreTemp[4] = {30.0, 30.0, 30.0, 30.0};  ///< Tire temperatures in Celsius
    double tyrePressure[4] = {2.2, 2.2, 2.0, 2.0};   ///< Tire pressures in bar
    double tyreWear[4] = {0.0, 0.0, 0.0, 0.0};       ///< Tire wear (0-1)
    
    // Session state
    bool inPitLane = false;           ///< Whether in pit lane
    bool pitLimiterActive = false;    ///< Pit limiter engaged
    bool drsActive = false;           ///< DRS active
    bool drsAvailable = false;        ///< DRS available
    bool crossedStartFinish = false;  ///< Crossed start/finish line
    bool cornerCutWarning = false;    ///< Corner cut warning active
    
    // World space
    PhysVec3 worldPosition;          ///< World space position
    PhysVec3 worldRotation;          ///< World space rotation
    
    /// Calculate kinetic energy
    float kineticEnergy(float mass) const {
        return 0.5f * mass * (velocity.x * velocity.x +
                              velocity.y * velocity.y +
                              velocity.z * velocity.z);
    }
    
    /// Calculate potential energy
    float potentialEnergy(float mass, float gravity = Constants::GRAVITY) const {
        return mass * gravity * position.y;
    }
    
    /// Check if state is valid (no NaN or Inf)
    bool isValid() const {
        auto checkVec = [](const PhysVec3& v) {
            return !std::isnan(v.x) && !std::isnan(v.y) && !std::isnan(v.z) &&
                   !std::isinf(v.x) && !std::isinf(v.y) && !std::isinf(v.z);
        };
        return checkVec(position) && checkVec(velocity) && checkVec(acceleration) &&
               checkVec(angularVelocity) && checkVec(rotation) &&
               speed >= 0.0f && speed <= Constants::MAX_REASONABLE_SPEED &&
               rpm >= 0.0f && rpm <= Constants::MAX_REASONABLE_RPM;
    }
};

// ============================================================================
// Wheel State
// ============================================================================

/**
 * @brief State of a single wheel
 */
struct WheelState {
    PhysVec3 position;               ///< Wheel position in local frame
    PhysVec3 velocity;               ///< Wheel velocity in local frame
    
    // Contact forces
    float normalLoad = 1.0f;          ///< Normal load in Newtons
    float slipAngle = 0.0f;           ///< Slip angle in radians
    float slipRatio = 0.0f;           ///< Slip ratio (0 = free rolling)
    float lateralForce = 0.0f;        ///< Lateral force in Newtons
    float longitudinalForce = 0.0f;   ///< Longitudinal force in Newtons
    float brakeTorque = 0.0f;         ///< Brake torque in Nm
    float angularVelocity = 0.0f;     ///< Angular velocity in rad/s
    float driveTorque = 0.0f;         ///< Drive torque in Nm
    
    // Thermal state
    float temperature = 30.0f;        ///< Tire surface temperature in Celsius
    float coreTemperature = 35.0f;    ///< Tire core temperature in Celsius
    float wear = 0.0f;                ///< Tire wear (0-1)
    float pressure = 2.0f;            ///< Tire pressure in bar
    
    /// Calculate combined force
    float combinedForce() const {
        return std::sqrt(lateralForce * lateralForce + longitudinalForce * longitudinalForce);
    }
    
    /// Calculate friction circle utilization
    float frictionCircleRatio(float frictionCoeff) const {
        float maxForce = normalLoad * frictionCoeff;
        if (maxForce < 0.001f) return 0.0f;
        return combinedForce() / maxForce;
    }
    
    /// Check if wheel is locked
    bool isLocked() const { return std::abs(angularVelocity) < 0.1f && std::abs(slipRatio) > 0.5f; }
    
    /// Check if wheel is spinning
    bool isSpinning(float vehicleSpeed) const {
        float wheelSpeed = angularVelocity * Constants::DEFAULT_WHEEL_RADIUS;
        return wheelSpeed > vehicleSpeed * 1.5f;
    }
};

// ============================================================================
// Tire Force Model
// ============================================================================

/**
 * @brief Tire force data from KsTireModel (Magic Formula) or other models
 */
struct TireForceData {
    float lateralForce = 0.0f;        ///< Lateral force (Fy) in N
    float longitudinalForce = 0.0f;   ///< Longitudinal force (Fx) in N
    float aligningMoment = 0.0f;      ///< Self-aligning moment (Mz) in Nm
    float overturningMoment = 0.0f;   ///< Overturning moment (Mx) in Nm
    float rollingResistance = 0.0f;   ///< Rolling resistance in N
    
    /// Combined force magnitude
    float magnitude() const {
        return std::sqrt(lateralForce * lateralForce + longitudinalForce * longitudinalForce);
    }
};

// ============================================================================
// Damage State
// ============================================================================

/**
 * @brief Damage state of a vehicle
 *
 * rFactor2-style damage model with per-component tracking.
 * Each subsystem tracks its own health independently.
 */
struct DamageState {
    // Legacy fields (backward compatible)
    float bodyDamage = 0.0f;          ///< Body damage (0-1)
    float aeroDamage = 0.0f;          ///< Aerodynamic damage (0-1)
    float suspensionDamage[4] = {0.0f}; ///< Per-wheel suspension damage
    float engineDamage = 0.0f;        ///< Engine damage (0-1)
    float accumulatedImpact = 0.0f;   ///< Accumulated impact energy in J
    int collisionCount = 0;           ///< Number of collisions
    bool isEliminated = false;        ///< Whether vehicle is eliminated

    // Extended per-component damage (rFactor2-style)
    float engineHealth = 1.0f;        ///< Engine health (1 = perfect, 0 = destroyed)
    float enginePowerLoss = 0.0f;     ///< Power loss from damage (0-1)
    float engineOverheat = 0.0f;      ///< Overheating damage (0-1)
    float transmissionHealth = 1.0f;  ///< Transmission health (0-1)
    float clutchDamage = 0.0f;        ///< Clutch wear/damage (0-1)
    float differentialDamage = 0.0f;  ///< Differential damage (0-1)
    float frontWingDamage = 0.0f;     ///< Front wing damage (0-1)
    float rearWingDamage = 0.0f;      ///< Rear wing damage (0-1)
    float diffuserDamage = 0.0f;      ///< Diffuser damage (0-1)
    float floorDamage = 0.0f;         ///< Underbody/floor damage (0-1)
    float radiatorDamage = 0.0f;      ///< Radiator damage (0-1)
    float steeringDamage = 0.0f;      ///< Steering damage (0-1)
    float brakeDamage[4] = {0.0f};    ///< Per-wheel brake damage (0-1)
    float brakePadWear[4] = {0.0f};   ///< Per-wheel brake pad wear (0-1)
    float tireWear[4] = {0.0f};       ///< Per-wheel tire wear (0-1)
    float tireGraining[4] = {0.0f};   ///< Per-wheel tire graining (0-1)
    float tireBlistering[4] = {0.0f}; ///< Per-wheel tire blistering (0-1)

    // Physics impact multipliers (derived from damage)
    float powerMultiplier = 1.0f;     ///< Effective engine power (0-1)
    float handlingMultiplier = 1.0f;  ///< Effective handling (0-1)
    float brakingMultiplier = 1.0f;   ///< Effective braking (0-1)
    float downforceMultiplier = 1.0f; ///< Effective downforce (0-1)
    float dragMultiplier = 1.0f;      ///< Drag change (>1 = more drag)

    /// Reset all damage
    void reset() {
        bodyDamage = 0.0f;
        aeroDamage = 0.0f;
        for (int i = 0; i < 4; ++i) suspensionDamage[i] = 0.0f;
        engineDamage = 0.0f;
        accumulatedImpact = 0.0f;
        collisionCount = 0;
        isEliminated = false;

        engineHealth = 1.0f;
        enginePowerLoss = 0.0f;
        engineOverheat = 0.0f;
        transmissionHealth = 1.0f;
        clutchDamage = 0.0f;
        differentialDamage = 0.0f;
        frontWingDamage = 0.0f;
        rearWingDamage = 0.0f;
        diffuserDamage = 0.0f;
        floorDamage = 0.0f;
        radiatorDamage = 0.0f;
        steeringDamage = 0.0f;
        for (int i = 0; i < 4; ++i) {
            brakeDamage[i] = 0.0f;
            brakePadWear[i] = 0.0f;
            tireWear[i] = 0.0f;
            tireGraining[i] = 0.0f;
            tireBlistering[i] = 0.0f;
        }
        powerMultiplier = 1.0f;
        handlingMultiplier = 1.0f;
        brakingMultiplier = 1.0f;
        downforceMultiplier = 1.0f;
        dragMultiplier = 1.0f;
    }

    /// Apply impact energy
    void applyImpact(float impactEnergy) {
        accumulatedImpact += impactEnergy;
        collisionCount++;
        bodyDamage = std::min(1.0f, bodyDamage + impactEnergy * 0.0001f);
        if (bodyDamage >= 1.0f) {
            isEliminated = true;
        }
    }

    /// Calculate overall damage level (0 = perfect, 1 = destroyed)
    float overallDamage() const {
        float total = bodyDamage + aeroDamage + engineDamage;
        for (int i = 0; i < 4; ++i) {
            total += suspensionDamage[i] + brakeDamage[i];
        }
        return std::clamp(total / 10.0f, 0.0f, 1.0f);
    }

    /// Check if vehicle is critically damaged
    bool isCriticallyDamaged() const {
        return engineHealth < 0.2f || isEliminated || overallDamage() > 0.8f;
    }
};

// ============================================================================
// Lap Timing
// ============================================================================

/**
 * @brief Lap time estimate and prediction
 */
struct LapTimeEstimate {
    float totalLapTime = 0.0f;        ///< Total lap time in seconds
    float sector1Time = 0.0f;         ///< Sector 1 time
    float sector2Time = 0.0f;         ///< Sector 2 time
    float sector3Time = 0.0f;         ///< Sector 3 time
    float avgSpeed = 0.0f;            ///< Average speed in m/s
    float minCornerSpeed = 0.0f;      ///< Minimum corner speed
    float fuelConsumption = 0.0f;     ///< Fuel consumption per lap in liters
    float confidenceLevel = 0.0f;     ///< Confidence in estimate (0-1)
    float topSpeed = 0.0f;            ///< Predicted top speed
    float maxLateralG = 0.0f;         ///< Predicted max lateral G
    int numGearChanges = 0;           ///< Number of gear changes per lap
};

// ============================================================================
// Validation Metrics
// ============================================================================

/**
 * @brief Validation metrics for comparing simulation with telemetry
 */
struct ValidationMetrics {
    int nSamples = 0;                 ///< Number of samples
    double speedRMSE = 0.0;           ///< Speed RMSE
    double lateralGRMSE = 0.0;        ///< Lateral G RMSE
    double longitudinalGRMSE = 0.0;   ///< Longitudinal G RMSE
    double rpmRMSE = 0.0;             ///< RPM RMSE
    double speedMaxError = 0.0;       ///< Maximum speed error
    double lateralGMaxError = 0.0;    ///< Maximum lateral G error
    double longitudinalGMaxError = 0.0; ///< Maximum longitudinal G error
    double rpmMaxError = 0.0;         ///< Maximum RPM error
    
    /// Calculate overall validation score (0-1)
    double score() const {
        if (nSamples == 0) return 0.0;
        double rmseAvg = (speedRMSE / 10.0 + lateralGRMSE + longitudinalGRMSE + rpmRMSE / 1000.0) / 4.0;
        return std::clamp(1.0 - rmseAvg, 0.0, 1.0);
    }
};

// ============================================================================
// Enums
// ============================================================================

/**
 * @brief Drive layout types
 */
enum class DriveLayout {
    FWD,  ///< Front-wheel drive
    RWD,  ///< Rear-wheel drive
    AWD   ///< All-wheel drive
};

/**
 * @brief Tire model types
 */
enum class TireModelType {
    MagicFormula,  ///< ksengine Magic Formula (KsTireModel)
    Pacejka = MagicFormula,  ///< @deprecated alias
    Generic,  ///< Generic simplified model
    Fiala,    ///< Fiala tire model
    Brush     ///< Brush tire model
};

/**
 * @brief Integration methods
 */
enum class IntegrationMethod {
    Euler,           ///< Simple Euler integration
    RungeKutta2,     ///< Second-order Runge-Kutta
    RungeKutta4,     ///< Fourth-order Runge-Kutta
    Verlet,          ///< Verlet integration
    SymplecticEuler  ///< Symplectic (semi-implicit) Euler
};

/**
 * @brief Driving conditions
 */
enum class DrivingCondition {
    Dry,
    Wet,
    LightRain,
    HeavyRain,
    StandingWater,
    Ice,
    Snow,
    Gravel,
    Mixed
};

// ============================================================================
// Tire Slip Curve
// ============================================================================

/**
 * @brief Tire slip curve parameters for force calculation
 */
struct TireSlipCurve {
    std::string name;
    std::string compound;
    double peakSlipAngle = 8.0;          ///< Slip angle at peak lateral force (degrees)
    double peakSlipRatio = 0.12;         ///< Slip ratio at peak longitudinal force
    double peakLateralMu = 1.0;          ///< Peak lateral friction coefficient
    double peakLongitudinalMu = 1.1;     ///< Peak longitudinal friction coefficient
    double stiffnessLateral = 30000.0;   ///< Lateral cornering stiffness (N/rad)
    double stiffnessLongitudinal = 50000.0; ///< Longitudinal slip stiffness (N)
};

} // namespace physics
} // namespace ks