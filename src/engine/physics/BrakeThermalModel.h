#pragma once

/**
 * @file BrakeThermalModel.h
 * @brief Disc/pad thermal model per corner — Qt-free
 */

#include <array>
#include <algorithm>
#include <cmath>

namespace ks {
namespace physics {

struct BrakeThermalConfig {
    float discMass = 6.5f;           // kg
    float discSpecificHeat = 480.0f; // J/(kg·K)
    float coolingCoeff = 12.0f;      // W/K at 1 m/s reference
    float ambientTemp = 25.0f;       // C
    float fadeStartTemp = 320.0f;    // C
    float fadeFullTemp = 620.0f;     // C
    float maxTemp = 900.0f;
};

struct BrakeThermalState {
    float discTemp = 80.0f;
    float padTemp = 60.0f;
    float fade = 0.0f;          // 0-1 torque reduction
    float energyAccum = 0.0f;   // J this step
};

class BrakeThermalModel {
public:
    BrakeThermalModel() = default;

    void setConfig(const BrakeThermalConfig& c) { m_cfg = c; }
    const BrakeThermalConfig& config() const { return m_cfg; }

    /**
     * @param brakeTorqueNm applied brake torque at this wheel
     * @param wheelOmega rad/s
     * @param speedMs vehicle speed (cooling airflow)
     */
    void update(float dt, int wheel, float brakeTorqueNm, float wheelOmega, float speedMs);

    BrakeThermalState state(int wheel) const {
        return (wheel >= 0 && wheel < 4) ? m_state[wheel] : BrakeThermalState{};
    }

    float fade(int wheel) const { return state(wheel).fade; }
    float discTemp(int wheel) const { return state(wheel).discTemp; }

    /** Effective brake torque after fade. */
    float effectiveTorque(int wheel, float commandedTorque) const {
        return commandedTorque * (1.0f - fade(wheel));
    }

    void reset();

private:
    BrakeThermalConfig m_cfg;
    std::array<BrakeThermalState, 4> m_state{};
};

} // namespace physics
} // namespace ks
