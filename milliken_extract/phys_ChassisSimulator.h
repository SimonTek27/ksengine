#pragma once

/**
 * @file ChassisSimulator.h
 * @brief Chassis dynamics, suspension, and weight transfer simulation
 * @copyright KS Physics Engine
 */

#include "PhysicsCoreTypes.h"
#include <QObject>

namespace ks {
namespace physics {

// ============================================================================
// Chassis Configuration
// ============================================================================

struct ChassisConfig {
    double mass = 1500.0;               ///< Vehicle mass (kg)
    double wheelBase = 2.7;             ///< Wheelbase (m)
    double trackWidth = 1.6;            ///< Track width (m)
    double cgHeight = 0.45;             ///< Center of gravity height (m)
    double frontAxleDist = 1.35;        ///< Distance from CG to front axle (m)
    double rearAxleDist = 1.35;         ///< Distance from CG to rear axle (m)
    double rollStiffness = 15000.0;     ///< Roll stiffness (Nm/rad)
    double yawInertia = 2500.0;         ///< Yaw moment of inertia (kg·m²)
};

// ============================================================================
// Suspension Configuration
// ============================================================================

struct SuspensionConfig {
    double springRateFront = 30000.0;   ///< Front spring rate (N/m)
    double springRateRear = 25000.0;    ///< Rear spring rate (N/m)
    double damperRateFront = 3000.0;    ///< Front damper rate (Ns/m)
    double damperRateRear = 2500.0;     ///< Rear damper rate (Ns/m)
    double antiRollBarFront = 1000.0;   ///< Front anti-roll bar stiffness (Nm/rad)
    double antiRollBarRear = 800.0;     ///< Rear anti-roll bar stiffness (Nm/rad)
    double rideHeightFront = 0.05;      ///< Front ride height (m)
    double rideHeightRear = 0.07;       ///< Rear ride height (m)
};

// ============================================================================
// Weight Transfer Result
// ============================================================================

struct WeightTransferResultChassis {
    double frontLeftLoad = 0.0;
    double frontRightLoad = 0.0;
    double rearLeftLoad = 0.0;
    double rearRightLoad = 0.0;
    double totalLoadTransfer = 0.0;
    double lateralLoadTransfer = 0.0;
    double longitudinalLoadTransfer = 0.0;
    double rollAngle = 0.0;
    double pitchAngle = 0.0;
};

// ============================================================================
// Chassis State
// ============================================================================

struct ChassisState {
    double yawRate = 0.0;
    double lateralAccel = 0.0;
    double longitudinalAccel = 0.0;
    double rollAngle = 0.0;
    double pitchAngle = 0.0;
    double sideslipAngle = 0.0;
    double speed = 0.0;
};

// ============================================================================
// Chassis Simulator Class
// ============================================================================

class ChassisSimulator {
public:
    ChassisSimulator();
    ~ChassisSimulator() = default;

    // Configuration
    void setChassisConfig(const ChassisConfig& config);
    void setSuspensionConfig(const SuspensionConfig& config);
    
    // State access
    ChassisState chassisState() const { return m_chassisState; }
    WeightTransferResultChassis weightTransferResult() const { return m_weightTransfer; }
    
    // Update
    void update(double dt, double throttle, double brake, double steering,
               const std::array<double, 4>& tireForces);
    
    // Weight transfer
    WeightTransferResultChassis calculateWeightTransfer(double lateralAccel, 
                                                       double longitudinalAccel) const;
    
    // Query
    double yawRate() const { return m_chassisState.yawRate; }
    double lateralAccel() const { return m_chassisState.lateralAccel; }
    double sideslipAngle() const { return m_chassisState.sideslipAngle; }
    
    // Stability derivatives
    struct StabilityDerivatives {
        double dFy_dAlpha = 0.0;        ///< Lateral force derivative w.r.t. sideslip
        double dMz_dAlpha = 0.0;        ///< Yaw moment derivative w.r.t. sideslip
        double dMz_dR = 0.0;            ///< Yaw moment derivative w.r.t. yaw rate
        double understeerGradient = 0.0;
        double yawVelocityGain = 0.0;
    };
    
    StabilityDerivatives calculateStabilityDerivatives(
        double frontCorneringStiffness, double rearCorneringStiffness) const;

private:
    // Internal calculations
    double calculateLateralAccel(double yawRate, double speed) const;
    double calculateYawAccel(double frontLateralForce, double rearLateralForce,
                            double frontLeverArm, double rearLeverArm) const;
    double calculateSideslipAngle(double vx, double vy, double yawRate) const;
    
    // Configuration
    ChassisConfig m_chassisConfig;
    SuspensionConfig m_suspensionConfig;
    
    // State
    ChassisState m_chassisState;
    WeightTransferResultChassis m_weightTransfer;
    
    // Physics constants
    static constexpr double GRAVITY = 9.81;
};

} // namespace physics
} // namespace ks