#pragma once

/**
 * Character / driver body kinematics for garage / trackside — Qt-free.
 */

#include <cmath>
#include <algorithm>
#include <functional>

namespace ks {
namespace physics {

struct CharVec3 {
    float x = 0, y = 0, z = 0;
    CharVec3() = default;
    CharVec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    CharVec3 operator+(CharVec3 o) const { return {x+o.x, y+o.y, z+o.z}; }
    CharVec3 operator*(float s) const { return {x*s, y*s, z*s}; }
    CharVec3& operator+=(CharVec3 o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
    float length() const { return std::sqrt(x*x+y*y+z*z); }
    CharVec3 normalized() const {
        float l = length();
        if (l < 1e-6f) return {0,0,0};
        return {x/l, y/l, z/l};
    }
};

struct CharacterState {
    CharVec3 position{0, 2, 0};
    CharVec3 velocity;
    CharVec3 acceleration;
    CharVec3 angularVelocity;
    CharVec3 previousVelocity;
    float rotation = 0;
    float speed = 0;
    float mass = 75.0f;
    float moveSpeed = 3.0f;
    float runSpeed = 6.0f;
    float jumpHeight = 0.5f;
    bool grounded = true;
    bool canJump = true;
};

class CharacterSimulator {
public:
    CharacterSimulator() = default;
    ~CharacterSimulator() = default;

    void startSimulation();
    void stopSimulation();
    void reset();
    bool isRunning() const { return m_running; }

    void setMoveDirection(float x, float y, float z);
    void setJump(bool jump);
    void setThrottle(bool t) { m_throttle = t; }
    void setBrake(bool b) { m_brake = b; }
    void setMass(float kg);
    float mass() const { return m_state.mass; }

    void updatePhysics(double dt);

    const CharacterState& state() const { return m_state; }
    CharacterState& state() { return m_state; }

    std::function<void()> onSimulationStarted;
    std::function<void()> onSimulationStopped;

private:
    CharacterState m_state;
    bool m_running = false;
    bool m_throttle = false;
    bool m_brake = false;
    bool m_jump = false;
    CharVec3 m_moveDirection{1, 0, 0};
};

} // namespace physics
} // namespace ks
