#pragma once

/**
 * @file BrakeThermalModel.h
 * @brief Disc/pad thermal model per corner — Qt-free
 */

#include <array>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace ks {
namespace physics {

struct BrakeThermalConfig {
    float discMass = 6.5f;           // kg
    float discSpecificHeat = 480.0f; // J/(kg·K)
    float coolingCoeff = 12.0f;      // W/K at the reference airflow
    // The airflow term: cooling ~ coolingCoeff * (base + speedFactor * v).
    // 1.0 / 0.05 is the shape the model always had; a car that declares
    // COOL_SPEED_FACTOR(_0) replaces the two numbers.
    float coolingBase = 1.0f;
    float coolingSpeedFactor = 0.05f; // per m/s
    float padCoupling = 0.15f;        // disc -> pad share per second
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

    /**
     * Fade from the car's own curve (data/brake_temp_front.lut and the rear one,
     * a temperature -> torque multiplier table) instead of the config
     * thresholds. An empty curve keeps fadeStartTemp/fadeFullTemp, which is what
     * a car that ships no table falls back to.
     */
    /** Per-wheel thermal config, for the axles a car describes separately
     *  (data.json brakes.TEMPS_FRONT / TEMPS_REAR). Unset wheels keep the
     *  config given to setConfig(). */
    void setWheelConfig(int wheel, const BrakeThermalConfig& c);
    const BrakeThermalConfig& wheelConfig(int wheel) const;

    void setPerfCurve(int wheel, const std::vector<std::pair<float, float>>& curve);
    /** Curve value at tempC, linearly interpolated between the points and held
     *  flat outside them. 1.0 when the wheel has no curve. */
    float perfCurveMultiplier(int wheel, float tempC) const;
    /** Parse a "temperature|multiplier" .lut. False when the file is missing or
     *  holds fewer than two usable points. */
    static bool loadPerfCurve(const std::string& path,
                              std::vector<std::pair<float, float>>& out);

    /** Effective brake torque after fade. */
    float effectiveTorque(int wheel, float commandedTorque) const {
        return commandedTorque * (1.0f - fade(wheel));
    }

    void reset();

private:
    BrakeThermalConfig m_cfg;
    std::array<BrakeThermalState, 4> m_state{};
    std::array<std::vector<std::pair<float, float>>, 4> m_curve{};
    std::array<BrakeThermalConfig, 4> m_wheelCfg{};
    std::array<bool, 4> m_wheelCfgSet{};
};

} // namespace physics
} // namespace ks
