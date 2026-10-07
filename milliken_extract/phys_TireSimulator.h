#pragma once

/**
 * @file TireSimulator.h
 * @brief Tire model, wear, and temperature simulation
 * @copyright KS Physics Engine
 */

#include "PhysicsCoreTypes.h"
#include "VehiclePhysicsModels.h"
#include <QObject>
#include <array>

namespace ks {
namespace physics {

// ============================================================================
// Tire Configuration
// ============================================================================

struct TireConfig {
    TireSlipCurve slipCurve;
    double relaxationLengthLateral = 0.04;    ///< Lateral relaxation length (m)
    double relaxationLengthLongitudinal = 0.04; ///< Longitudinal relaxation length (m)
    double optimalTemperature = 80.0;        ///< Optimal operating temperature (C)
    double temperatureWindow = 20.0;         ///< Temperature window for optimal grip
    double wearFactor = 0.3;                 ///< Wear effect on grip (0-1)
    double thermalMass = 100.0;              ///< Thermal mass for temperature changes
};

// ============================================================================
// Tire State (per wheel)
// ============================================================================

struct TireWheelState {
    double slipAngle = 0.0;          ///< Slip angle (degrees)
    double slipRatio = 0.0;          ///< Slip ratio
    double lateralForce = 0.0;       ///< Lateral force (N)
    double longitudinalForce = 0.0;  ///< Longitudinal force (N)
    double normalLoad = 0.0;         ///< Normal load (N)
    double temperature = 30.0;       ///< Surface temperature (C)
    double coreTemperature = 35.0;   ///< Core temperature (C)
    double wear = 0.0;               ///< Tire wear (0-1)
    double pressure = 2.2;           ///< Tire pressure (bar)
    double angularVelocity = 0.0;    ///< Angular velocity (rad/s)
    double frictionCoefficient = 1.0; ///< Current friction coefficient
};

// ============================================================================
// Tire Simulator Class
// ============================================================================

class TireSimulator {
public:
    TireSimulator();
    ~TireSimulator() = default;

    // Configuration
    void setTireConfig(const TireConfig& config);
    void setTireConfig(int wheel, const TireConfig& config);
    
    // State access
    TireWheelState wheelState(int wheel) const;
    std::array<TireWheelState, 4> allWheelStates() const { return m_wheelStates; }
    
    // Update
    void update(double dt, double vehicleSpeed, const WeatherState& weather);
    
    // Force calculation
    void calculateForces(int wheel, double slipAngle, double slipRatio, 
                        double normalLoad, double frictionCoefficient);
    
    // Query
    double frictionCoefficient(int wheel) const;
    double wear(int wheel) const;
    double temperature(int wheel) const;
    
    // Control
    void setNormalLoad(int wheel, double load);
    void setWear(int wheel, double wear);
    void resetWear();
    void reset();

private:
    // Internal calculations
    double calculateLateralForce(double slipAngle, double normalLoad, double friction) const;
    double calculateLongitudinalForce(double slipRatio, double normalLoad, double friction) const;
    double calculateSlipAngle(double vx, double vy, double yawRate, 
                             double wheelX, double wheelY, double steerAngle) const;
    double calculateSlipRatio(double wheelSpeed, double vehicleSpeed) const;
    
    // Thermal model
    void updateTemperature(int wheel, double dt, double slipAngle, double slipRatio, 
                          double normalLoad, double ambientTemp);
    double calculateHeatGeneration(double slipAngle, double slipRatio, double normalLoad) const;
    double calculateHeatDissipation(double temp, double ambientTemp) const;
    
    // Wear model
    void updateWear(int wheel, double dt, double slipAngle, double slipRatio, 
                   double normalLoad, double temperature);
    double calculateWearRate(double slipAngle, double slipRatio, double normalLoad, 
                            double temperature) const;
    
    // Configuration
    std::array<TireConfig, 4> m_configs;
    
    // State
    std::array<TireWheelState, 4> m_wheelStates;
    
    // Advanced models integration
    std::array<TireWearModel, 4> m_wearModels;
    
    // Physics constants
    static constexpr double GRAVITY = 9.81;
    static constexpr double RAD_TO_DEG = 180.0 / M_PI;
    static constexpr double DEG_TO_RAD = M_PI / 180.0;
};

} // namespace physics
} // namespace ks