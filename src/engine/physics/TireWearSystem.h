#pragma once

#include "PhysicsCoreTypes.h"
#include <array>
#include <cmath>

namespace ks::physics {

// ============================================================================
// Tire Thermal Model
// ============================================================================
// Models tire temperature distribution and heat transfer

struct TireThermalState {
    float surfaceTemp[3] = {30.0f, 30.0f, 30.0f};  // Inner/Middle/Outer surface temp (C)
    float coreTemp = 35.0f;                          // Core temperature (C)
    float carcassTemp = 32.0f;                       // Carcass temperature (C)
    float beltTemp = 31.0f;                          // Belt temperature (C)
    float ambientTemp = 25.0f;                       // Ambient temperature (C)
    float roadTemp = 30.0f;                          // Road surface temperature (C)

    // Get average surface temperature
    float averageSurfaceTemp() const {
        return (surfaceTemp[0] + surfaceTemp[1] + surfaceTemp[2]) / 3.0f;
    }

    // Get temperature gradient (inner to outer)
    float temperatureGradient() const {
        return surfaceTemp[2] - surfaceTemp[0];
    }

    // Check if tire is in optimal temperature window
    bool isOptimalTemp(float optimalMin = 70.0f, float optimalMax = 110.0f) const {
        float avg = averageSurfaceTemp();
        return avg >= optimalMin && avg <= optimalMax;
    }
};

struct TireThermalConfig {
    float thermalConductivity = 0.15f;    // Heat transfer rate
    float specificHeat = 1200.0f;         // J/(kg*K)
    float rubberMass = 8.0f;              // kg per tire
    float contactPatchArea = 0.02f;       // m²
    float convectionCoeff = 50.0f;        // W/(m²*K)
    float coreHeatCapacity = 0.3f;        // Core thermal mass ratio
    float carcassHeatCapacity = 0.2f;     // Carcass thermal mass ratio

    // Temperature thresholds
    float optimalTempMin = 70.0f;
    float optimalTempMax = 110.0f;
    float criticalTemp = 140.0f;
    float grainingTempMin = 50.0f;
    float grainingTempMax = 80.0f;
    float blisteringTemp = 130.0f;
};

// ============================================================================
// Tire Wear Model
// ============================================================================
// Tracks wear progression and its effect on grip

struct TireWearDetailState {
    float wear = 0.0f;              // Total wear (0 = new, 1 = destroyed)
    float surfaceWear = 0.0f;       // Surface graining/blistering
    float structuralWear = 0.0f;    // Structural degradation
    float gripLoss = 0.0f;          // Current grip loss due to wear
    float graining = 0.0f;          // Graining level (0-1)
    float blistering = 0.0f;        // Blistering level (0-1)
    float clumps = 0.0f;            // Rubber clumps (pickup)

    // Get effective grip multiplier
    float gripMultiplier() const {
        float baseGrip = 1.0f - wear * 0.4f;
        float surfaceEffect = 1.0f - surfaceWear * 0.3f;
        float grainingEffect = 1.0f - graining * 0.25f;
        float blisteringEffect = 1.0f - blistering * 0.35f;
        return std::clamp(baseGrip * surfaceEffect * grainingEffect * blisteringEffect, 0.3f, 1.0f);
    }

    // Get wear percentage for display
    float wearPercent() const { return wear * 100.0f; }
};

struct TireWearConfig {
    float baseWearRate = 0.001f;         // Base wear per second at full load
    float loadWearExponent = 1.2f;       // Wear increase with load
    float slipWearMultiplier = 2.0f;     // Wear increase from sliding
    float temperatureWearExponent = 1.5f; // Wear increase with temperature
    float grainingOnsetTemp = 50.0f;
    float grainingPeakTemp = 65.0f;
    float grainingOffsetTemp = 80.0f;
    float blisteringOnsetTemp = 120.0f;
    float clumpBuildupRate = 0.0005f;
    float clumpShedSpeed = 80.0f;        // m/s - speed at which clumps shed
    float maxGraining = 0.4f;
    float maxBlistering = 0.5f;
    float maxClumps = 0.3f;
};

// ============================================================================
// Tire Compound Properties
// ============================================================================

enum class TireCompound {
    SuperSoft,
    Soft,
    Medium,
    Hard,
    SuperHard,
    Wet,
    Intermediate,
    Custom
};

struct TireCompoundData {
    TireCompound compound = TireCompound::Medium;
    float gripFactor = 1.0f;          // Relative grip (1.0 = baseline)
    float wearResistance = 1.0f;     // Wear resistance (1.0 = baseline)
    float thermalConductivity = 1.0f; // Heat transfer modifier
    float optimalTempMin = 70.0f;
    float optimalTempMax = 110.0f;
    float operatingTempRange = 40.0f; // Width of optimal window
    std::string name = "Medium";

    static TireCompoundData getCompoundData(TireCompound compound);
};

// ============================================================================
// Tire Wear System (per wheel)
// ============================================================================

class TireWearSystem {
public:
    TireWearSystem();

    // Configuration
    void setConfig(const TireThermalConfig& thermalConfig, const TireWearConfig& wearConfig);
    void setCompound(TireCompound compound);
    void setCompound(const TireCompoundData& data);

    // Update (call each physics step)
    // speed: vehicle speed (m/s)
    // normalLoad: tire normal load (N)
    // slipAngle: slip angle (rad)
    // slipRatio: slip ratio
    // lateralForce: lateral force (N)
    // longitudinalForce: longitudinal force (N)
    // dt: time step (s)
    void update(float speed, float normalLoad, float slipAngle, float slipRatio,
                float lateralForce, float longitudinalForce, float dt);

    // Set environmental conditions
    void setAmbientTemp(float temp);
    void setRoadTemp(float temp);
    void setTrackGrip(float grip);  // 0-1

    // Queries
    const TireThermalState& thermalState() const { return m_thermal; }
    const TireWearDetailState& wearState() const { return m_wearDetail; }
    float gripMultiplier() const { return m_wearDetail.gripMultiplier(); }
    float effectiveMu() const;
    bool needsPitStop() const;
    float estimatedLapsRemaining() const;

    // Reset
    void reset();

    // Serialization
private:
    void updateThermal(float speed, float normalLoad, float slipAngle, float slipRatio,
                       float lateralForce, float longitudinalForce, float dt);
    void updateWear(float normalLoad, float slipAngle, float slipRatio,
                    float lateralForce, float longitudinalForce, float dt);
    void updateGraining(float dt);
    void updateBlistering(float dt);
    float calculateHeatGeneration(float slipAngle, float slipRatio,
                                   float lateralForce, float longitudinalForce) const;
    float calculateHeatLoss(float speed) const;
    float calculateContactPatchTemp() const;

    TireThermalConfig m_thermalConfig;
    TireWearConfig m_wearConfig;
    TireCompoundData m_compound;

    TireThermalState m_thermal;
    TireWearDetailState m_wearDetail;

    float m_trackGrip = 1.0f;
    float m_distanceTraveled = 0.0f;
    float m_slipHistory = 0.0f;   // Accumulated sliding
};

} // namespace ks::physics
