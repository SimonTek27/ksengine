#pragma once

/**
 * @file DriverSimulator.h
 * @brief Driver behavior simulation and input processing — Qt-free
 */

#include "PhysicsCoreTypes.h"
#include <algorithm>
#include <cmath>

namespace ks {
namespace physics {

struct DriverConfig {
    float reactionTime = 0.25f;
    float fatigueRate = 0.001f;
    float focusDecayRate = 0.0005f;
    float aggressiveness = 0.5f;
    float consistency = 0.8f;
};

struct DriverState {
    float fatigueLevel = 0.0f;
    float focusLevel = 1.0f;
};

struct ProcessedDriverInput {
    float throttle = 0.0f;
    float brake = 0.0f;
    float steer = 0.0f;
    bool isReactionDelayed = false;
    float reactionDelay = 0.0f;
    float errorMagnitude = 0.0f;
};

/** Lightweight driver quality model (was in VehiclePhysicsModels). */
class DriverModel {
public:
    DriverState getDriverState() const { return m_state; }
    float calculateReactionDelay(float /*load*/) const {
        return 0.15f + (1.0f - m_state.focusLevel) * 0.2f + m_state.fatigueLevel * 0.15f;
    }
    float getDriverQuality() const {
        return std::clamp((1.0f - m_state.fatigueLevel) * m_state.focusLevel, 0.0f, 1.0f);
    }
    void update(float dt, float lateralLoad) {
        m_state.fatigueLevel = std::clamp(m_state.fatigueLevel + 0.001f * dt * (1.0f + lateralLoad), 0.0f, 1.0f);
        m_state.focusLevel = std::clamp(m_state.focusLevel - 0.0005f * dt, 0.2f, 1.0f);
    }
    void reset() { m_state = DriverState{}; }
private:
    DriverState m_state;
};

class DriverSimulator {
public:
    DriverSimulator() = default;
    ~DriverSimulator() = default;

    void setDriverConfig(const DriverConfig& config) { m_driverConfig = config; }
    DriverState driverState() const { return m_driverModel.getDriverState(); }

    void update(double dt, double /*speed*/, double lateralAccel,
                double /*brakingForce*/, double /*corneringLoad*/) {
        m_driverModel.update(static_cast<float>(dt), static_cast<float>(std::abs(lateralAccel) / 10.0));
    }

    ProcessedDriverInput processInput(float rawThrottle, float rawBrake, float rawSteer,
                                      double /*speed*/, double /*targetSpeed*/) {
        ProcessedDriverInput out;
        float err = (1.0f - m_driverConfig.consistency) * 0.05f;
        out.throttle = std::clamp(rawThrottle + err * (rawThrottle > 0 ? 1.f : 0.f), 0.0f, 1.0f);
        out.brake = std::clamp(rawBrake, 0.0f, 1.0f);
        out.steer = std::clamp(rawSteer, -1.0f, 1.0f);
        out.reactionDelay = m_driverModel.calculateReactionDelay(0.0f);
        out.isReactionDelayed = out.reactionDelay > 0.2f;
        out.errorMagnitude = err;
        return out;
    }

    float reactionTime() const { return m_driverModel.calculateReactionDelay(0.0f); }
    float fatigueLevel() const { return m_driverModel.getDriverState().fatigueLevel; }
    float focusLevel() const { return m_driverModel.getDriverState().focusLevel; }
    float driverQuality() const { return m_driverModel.getDriverQuality(); }

    void reset() {
        m_driverModel.reset();
        m_throttleDelay = m_brakeDelay = m_steerDelay = 0.0f;
    }

private:
    DriverConfig m_driverConfig;
    DriverModel m_driverModel;
    float m_throttleDelay = 0.0f;
    float m_brakeDelay = 0.0f;
    float m_steerDelay = 0.0f;
};

} // namespace physics
} // namespace ks
