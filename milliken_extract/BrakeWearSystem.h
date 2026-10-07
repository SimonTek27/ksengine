#pragma once

#include "PhysicsCoreTypes.h"
#include <array>
#include <cmath>

namespace ks::physics {

// ============================================================================
// Brake Thermal Model
// ============================================================================

struct BrakeThermalState {
    float discTemp[4] = {300.0f, 300.0f, 300.0f, 300.0f};   // Disc temperature (C)
    float padTemp[4] = {250.0f, 250.0f, 250.0f, 250.0f};    // Pad temperature (C)
    float hubTemp[4] = {80.0f, 80.0f, 80.0f, 80.0f};        // Hub/bearing temperature (C)
    float ambientTemp = 25.0f;
    float airflowSpeed = 0.0f;  // m/s - for cooling calculation

    // Optimal operating ranges
    float optimalDiscTempMin = 250.0f;
    float optimalDiscTempMax = 500.0f;
    float criticalDiscTemp = 800.0f;
    float fadeOnsetTemp = 600.0f;
};

struct BrakeThermalConfig {
    float discMass = 4.0f;            // kg per disc
    float padMass = 0.5f;             // kg per pad
    float discHeatCapacity = 500.0f;  // J/(kg*K)
    float padHeatCapacity = 900.0f;   // J/(kg*K)
    float frictionCoeffBase = 0.42f;  // Base friction coefficient
    float frictionTempSensitivity = 0.0005f; // Friction change per degree
    float coolingCoeffFront = 45.0f;  // W/(m²*K) - front brakes get more air
    float coolingCoeffRear = 35.0f;   // W/(m²*K)
    float discArea = 0.03f;           // m² disc surface area
    float conductionPadToDisc = 0.6f; // Heat transfer from pad to disc
};

// ============================================================================
// Brake Fade Model
// ============================================================================

enum class BrakeFadeType {
    None,
    Thermal,     // Fade from overheating
    Friction,    // Pad material breakdown
    Gas,         // Outgassing at high temp
    Mechanical   // Physical deformation
};

struct BrakeFadeState {
    BrakeFadeType fadeType = BrakeFadeType::None;
    float fadeLevel = 0.0f;          // 0 = no fade, 1 = total fade
    float peakFriction = 0.42f;      // Current peak friction
    float pedalFirmness = 1.0f;      // Pedal feel (1 = firm, 0 = mushy)
    float responseTime = 0.0f;       // Added response delay from fade
    float recoveryRate = 0.1f;       // How fast fade recovers when cooled

    // Calculate effective friction
    float effectiveFriction(float baseFriction) const {
        return baseFriction * (1.0f - fadeLevel * 0.6f);
    }

    // Calculate braking force multiplier
    float brakingMultiplier() const {
        return 1.0f - fadeLevel * 0.5f;
    }
};

// ============================================================================
// Brake Pad Wear Model
// ============================================================================

struct BrakePadWearState {
    float padThickness[4] = {10.0f, 10.0f, 10.0f, 10.0f};   // mm remaining
    float initialThickness = 10.0f;   // mm when new
    float wearRate[4] = {0.0f};       // mm per second of braking
    float totalWear[4] = {0.0f};      // Total mm worn
    bool needsReplacement[4] = {false};

    // Get remaining life fraction (0-1)
    float remainingLife(int wheel) const {
        return std::clamp(padThickness[wheel] / initialThickness, 0.0f, 1.0f);
    }

    // Check if pad needs replacement
    bool isWornOut(int wheel) const {
        return padThickness[wheel] < 1.0f; // 1mm minimum
    }
};

struct BrakePadWearConfig {
    float baseWearRate = 0.001f;       // mm per second at 1000Nm, 400C
    float pressureExponent = 1.1f;     // Wear increase with pressure
    float temperatureExponent = 1.3f;  // Wear increase with temperature
    float velocityExponent = 0.9f;     // Wear increase with speed
    float padMaterialFactor = 1.0f;    // Material hardness (1.0 = standard)
    float minimumThickness = 1.0f;     // mm - must replace below this
    float warningThickness = 3.0f;     // mm - warn when below this
};

// ============================================================================
// Brake Disc Wear Model
// ============================================================================

struct BrakeDiscWearState {
    float discThickness[4] = {28.0f, 28.0f, 28.0f, 28.0f};  // mm remaining
    float initialThickness = 28.0f;   // mm when new
    float surfaceRoughness[4] = {0.0f}; // 0 = smooth, 1 = rough
    float warpage[4] = {0.0f};        // Disc warpage (0-1)
    bool isWarped[4] = {false};

    float remainingLife(int wheel) const {
        return std::clamp(discThickness[wheel] / initialThickness, 0.0f, 1.0f);
    }
};

// ============================================================================
// Brake Wear System (per wheel)
// ============================================================================

struct BrakeWearData {
    int wheel = 0;
    float brakeTorque = 0.0f;       // Applied brake torque (Nm)
    float brakePressure = 0.0f;     // Hydraulic pressure (bar)
    float wheelSpeed = 0.0f;        // Wheel angular velocity (rad/s)
    float vehicleSpeed = 0.0f;      // Vehicle speed (m/s)
    float normalLoad = 0.0f;        // Tire normal load (N)
    float dt = 0.016f;              // Time step (s)
};

class BrakeWearSystem {
public:
    BrakeWearSystem();

    // Configuration
    void setThermalConfig(const BrakeThermalConfig& config);
    void setPadWearConfig(const BrakePadWearConfig& config);
    void setAmbientTemp(float temp);

    // Update (call each physics step)
    void update(const BrakeWearData& data);

    // Apply brake input
    void applyBrake(int wheel, float pressure, float dt);

    // Queries
    const BrakeThermalState& thermalState() const { return m_thermal; }
    const BrakeFadeState& fadeState(int wheel) const { return m_fade[wheel]; }
    const BrakePadWearState& padWearState() const { return m_padWear; }
    const BrakeDiscWearState& discWearState() const { return m_discWear; }

    float discTemperature(int wheel) const { return m_thermal.discTemp[wheel]; }
    float padTemperature(int wheel) const { return m_thermal.padTemp[wheel]; }
    float effectiveFriction(int wheel) const;
    float brakingMultiplier(int wheel) const;
    bool isFaded(int wheel) const { return m_fade[wheel].fadeLevel > 0.1f; }
    bool needsPadReplacement(int wheel) const { return m_padWear.isWornOut(wheel); }
    bool needsDiscReplacement(int wheel) const { return m_discWear.discThickness[wheel] < 15.0f; }

    // Reset
    void reset();

    // Serialization
private:
    void updateThermal(int wheel, float brakeTorque, float wheelSpeed, float dt);
    void updateFade(int wheel, float dt);
    void updatePadWear(int wheel, float brakeTorque, float wheelSpeed, float dt);
    void updateDiscWear(int wheel, float brakeTorque, float wheelSpeed, float dt);
    float calculateBrakeTorque(float pressure, int wheel) const;
    float calculateHeatGeneration(float torque, float angularVelocity) const;
    float calculateCooling(int wheel, float speed) const;

    BrakeThermalConfig m_thermalConfig;
    BrakePadWearConfig m_padWearConfig;

    BrakeThermalState m_thermal;
    std::array<BrakeFadeState, 4> m_fade;
    BrakePadWearState m_padWear;
    BrakeDiscWearState m_discWear;

    bool m_braking[4] = {false};
};

} // namespace ks::physics
