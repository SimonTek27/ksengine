#include "CharacterPhysics.h"
#include <cstdio>

namespace ks {
namespace physics {

void CharacterSimulator::startSimulation() {
    if (m_running) return;
    m_running = true;
    if (onSimulationStarted) onSimulationStarted();
}

void CharacterSimulator::stopSimulation() {
    if (!m_running) return;
    m_running = false;
    if (onSimulationStopped) onSimulationStopped();
}

void CharacterSimulator::reset() {
    m_state = CharacterState();
    m_state.position = {0, 2, 0};
    m_state.grounded = true;
    m_state.canJump = true;
    m_throttle = m_brake = m_jump = false;
    m_moveDirection = {1, 0, 0};
}

void CharacterSimulator::setMoveDirection(float x, float y, float z) {
    m_moveDirection = {x, y, z};
}

void CharacterSimulator::setJump(bool jump) { m_jump = jump; }

void CharacterSimulator::setMass(float kg) {
    m_state.mass = std::max(1.0f, kg);
}

void CharacterSimulator::updatePhysics(double dt) {
    if (!m_running) return;
    dt = std::min(dt, 0.016);
    const float fdt = static_cast<float>(dt);

    CharVec3 gravity{0, -9.81f * fdt, 0};
    float forwardSpeed = m_throttle ? m_state.runSpeed : m_state.moveSpeed;
    if (m_brake) forwardSpeed *= 0.3f;
    CharVec3 desired = m_moveDirection.normalized() * forwardSpeed;

    if (!m_state.grounded) {
        m_state.velocity += gravity;
    } else {
        m_state.velocity = {desired.x, 0, desired.z};
        if (m_jump && m_state.canJump) {
            m_state.velocity.y = m_state.jumpHeight * 10.0f;
            m_state.grounded = false;
            m_state.canJump = false;
        }
    }

    m_state.position += m_state.velocity * fdt;

    // Simple ground plane at y=0
    if (m_state.position.y <= 0.0f) {
        m_state.position.y = 0.0f;
        m_state.velocity.y = 0.0f;
        m_state.grounded = true;
        m_state.canJump = true;
    }

    m_state.speed = m_state.velocity.length();
    m_state.previousVelocity = m_state.velocity;
}

} // namespace physics
} // namespace ks
