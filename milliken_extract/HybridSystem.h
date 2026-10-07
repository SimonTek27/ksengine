#pragma once

/**
 * @file HybridSystem.h
 * @brief Minimal ERS / hybrid energy store — Qt-free
 */

#include <algorithm>
#include <cmath>

namespace ks {
namespace physics {

class HybridSystem {
public:
    enum class Mode { Off = 0, Deploy, Harvest, Attack };

    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const { return m_enabled; }

    void setMode(Mode m) { m_mode = m; }
    Mode mode() const { return m_mode; }
    void activateAttack() { m_mode = Mode::Attack; m_attackTimer = 5.0f; }

    float soc() const { return m_soc; } // 0-1 state of charge
    float maxDeployKw() const { return m_maxDeployKw; }

    /** Extra torque (Nm) at wheels-ish from MGU; harvest returns negative. */
    float update(float dt, float throttle, float brake, float speedMs) {
        if (!m_enabled) return 0.0f;
        dt = std::clamp(dt, 1e-4f, 0.05f);

        if (m_attackTimer > 0.0f) {
            m_attackTimer -= dt;
            if (m_attackTimer <= 0.0f && m_mode == Mode::Attack)
                m_mode = Mode::Deploy;
        }

        float powerKw = 0.0f;
        if (m_mode == Mode::Deploy || m_mode == Mode::Attack) {
            if (throttle > 0.2f && m_soc > 0.05f) {
                powerKw = (m_mode == Mode::Attack ? m_maxDeployKw * 1.3f : m_maxDeployKw) * throttle;
                m_soc = std::max(0.0f, m_soc - powerKw * dt / (m_capacityKj));
            }
        } else if (m_mode == Mode::Harvest || brake > 0.1f) {
            powerKw = -m_maxHarvestKw * std::max(brake, 0.2f);
            m_soc = std::min(1.0f, m_soc - powerKw * dt / m_capacityKj);
        }

        // P = T * omega; approx omega from speed / r
        float omega = std::max(speedMs, 1.0f) / 0.33f;
        float torque = (powerKw * 1000.0f) / omega;
        return torque;
    }

    void reset() {
        m_soc = 0.8f;
        m_mode = Mode::Off;
        m_attackTimer = 0.0f;
    }

private:
    bool m_enabled = false;
    Mode m_mode = Mode::Off;
    float m_soc = 0.8f;
    float m_maxDeployKw = 120.0f;
    float m_maxHarvestKw = 150.0f;
    float m_capacityKj = 4000.0f; // ~ energy budget
    float m_attackTimer = 0.0f;
};

} // namespace physics
} // namespace ks
