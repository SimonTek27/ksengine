#pragma once

/**
 * @file ChassisSimulator.h
 * @brief Planar bicycle-ish chassis kinematics for yaw / sideslip — Qt-free
 */

#include "PhysicsCoreTypes.h"

#include <algorithm>
#include <cmath>

namespace ks {
namespace physics {

struct ChassisConfig {
    float mass = 1300.0f;
    float wheelBase = 2.7f;
    float trackWidth = 1.6f;
    float cgHeight = 0.45f;
    float frontAxleDist = 1.35f;
    float rearAxleDist = 1.35f;
    float yawInertia = 2500.0f;
    float corneringStiffnessFront = 80000.0f; // N/rad
    float corneringStiffnessRear = 90000.0f;
};

struct ChassisState {
    float yawRate = 0.0f;       // rad/s
    float sideslip = 0.0f;      // rad
    float lateralAccel = 0.0f;  // m/s^2
    float longitudinalAccel = 0.0f;
    float rollAngle = 0.0f;
    float pitchAngle = 0.0f;
    float speed = 0.0f;
};

class ChassisSimulator {
public:
    void setConfig(const ChassisConfig& c) { m_cfg = c; }
    const ChassisConfig& config() const { return m_cfg; }
    const ChassisState& state() const { return m_state; }

    /**
     * Integrate planar dynamics from total Fx, Fy at CG and steer angle.
     */
    void update(float dt, float speed, float steerRad, float forceX, float forceY) {
        dt = std::clamp(dt, 1e-4f, 0.05f);
        m_state.speed = speed;
        m_state.longitudinalAccel = forceX / std::max(m_cfg.mass, 1.0f);
        m_state.lateralAccel = forceY / std::max(m_cfg.mass, 1.0f);

        const float v = std::max(speed, 0.5f);
        // Slip angles (bicycle)
        const float a = m_cfg.frontAxleDist;
        const float b = m_cfg.rearAxleDist;
        const float yaw = m_state.yawRate;
        const float beta = m_state.sideslip;

        const float alphaF = steerRad - beta - (a * yaw) / v;
        const float alphaR = -beta + (b * yaw) / v;

        const float FyF = m_cfg.corneringStiffnessFront * alphaF;
        const float FyR = m_cfg.corneringStiffnessRear * alphaR;

        // Blend with external lateral force (from tires) lightly
        const float Fy = 0.5f * (FyF + FyR) + 0.5f * forceY;
        m_state.lateralAccel = Fy / m_cfg.mass;

        const float yawMoment = a * FyF - b * FyR;
        const float yawAcc = yawMoment / std::max(m_cfg.yawInertia, 1.0f);
        m_state.yawRate += yawAcc * dt;
        m_state.yawRate *= (1.0f - 0.02f * dt);

        // Sideslip dynamics: v_dot beta ≈ ay/v - yaw
        m_state.sideslip += (m_state.lateralAccel / v - m_state.yawRate) * dt;
        m_state.sideslip = std::clamp(m_state.sideslip, -0.5f, 0.5f);

        // Quasi-static roll/pitch from accel
        m_state.rollAngle = std::clamp(m_state.lateralAccel * m_cfg.cgHeight / 50.0f, -0.15f, 0.15f);
        m_state.pitchAngle = std::clamp(-m_state.longitudinalAccel * m_cfg.cgHeight / 60.0f, -0.1f, 0.1f);
    }

    void reset() { m_state = ChassisState{}; }

private:
    ChassisConfig m_cfg;
    ChassisState m_state;
};

} // namespace physics
} // namespace ks
