#include "CameraController.h"
#include <cmath>
#include <algorithm>

namespace ks::sim {

CameraController::CameraController() = default;

void CameraController::update(float dt, const mat4& carBodyMatrix, float speedKmh)
{
    switch (m_mode) {
    case Mode::Cockpit: updateCockpit(carBodyMatrix, speedKmh, dt); break;
    case Mode::Chase:   updateChase(carBodyMatrix, speedKmh, dt); break;
    case Mode::Free:    updateFree(dt); break;
    }
    updateProjection();
}

void CameraController::updateCockpit(const mat4& carMatrix, float speedKmh, float dt)
{
    vec3 localPos = m_cockpitOffset;

    float bobSpeed = speedKmh / 3.6f;
    m_headBobPhase += bobSpeed * 0.05f;
    float bob = std::sin(m_headBobPhase) * m_headBobAmplitude * bobSpeed;
    localPos = localPos + vec3(0, bob, 0);

    // Head physics: the head hangs on a spring, so the car's acceleration moves
    // it instead of leaving it welded to the shell. Braking and throttle push it
    // along the seat, cornering swings it sideways, and the damper settles it.
    if (m_headPhysics) {
        const vec3 carPos = carMatrix * vec3(0, 0, 0);
        vec3 carForward = (carMatrix * vec3(0, 0, 1)) - carPos;
        const float forwardLen = std::sqrt(carForward.x * carForward.x +
                                           carForward.y * carForward.y +
                                           carForward.z * carForward.z);
        if (forwardLen > 1e-6f) carForward = carForward * (1.0f / forwardLen);
        if (!m_headInitialized) {
            m_lastCarForward = carForward;
            m_lastSpeedKmh = speedKmh;
            m_headInitialized = true;
        }
        const float step = std::clamp(dt, 0.0f, 0.05f);
        if (step > 0.0f) {
            const float longAccel = (speedKmh - m_lastSpeedKmh) / step / 3.6f;
            // How far the body turned this frame times the speed is the lateral
            // acceleration the head actually feels.
            const float cross = m_lastCarForward.x * carForward.z -
                                m_lastCarForward.z * carForward.x;
            const float yawRate = std::asin(std::clamp(cross, -1.0f, 1.0f)) / step;
            const float latAccel = (speedKmh / 3.6f) * yawRate;
            const vec3 target{
                std::clamp(latAccel * 0.0012f, -0.035f, 0.035f),
                0.0f,
                std::clamp(-longAccel * 0.0010f, -0.025f, 0.025f)};
            constexpr float kStiffness = 90.0f;
            constexpr float kDamping = 15.0f;
            const vec3 accel =
                (target - m_headOffset) * kStiffness - m_headVelocity * kDamping;
            m_headVelocity = m_headVelocity + accel * step;
            m_headOffset = m_headOffset + m_headVelocity * step;
        }
        m_lastSpeedKmh = speedKmh;
        m_lastCarForward = carForward;
        localPos = localPos + m_headOffset;
    } else {
        m_headInitialized = false;
    }

    m_position = carMatrix * localPos;

    vec3 localTarget = m_cockpitLookTarget;
    vec3 worldTarget = carMatrix * localTarget;

    m_forward = (worldTarget - m_position).normalized();
    m_up = vec3(0, 1, 0);

    m_viewMatrix = mat4::lookAt(m_position, m_position + m_forward, m_up);
}

void CameraController::updateChase(const mat4& carMatrix, float speedKmh, float dt)
{
    (void)speedKmh;

    vec3 carPos = carMatrix * vec3(0, 0, 0);
    vec3 carForward = (carMatrix * vec3(0, 0, 1)) - carPos;
    carForward = carForward.normalized();
    vec3 carUp = (carMatrix * vec3(0, 1, 0)) - carPos;
    carUp = carUp.normalized();

    vec3 targetPos = carPos - carForward * m_chaseDistance + carUp * m_chaseHeight;

    if (!m_chaseInitialized) {
        m_chaseCurrentPos = targetPos;
        m_chaseInitialized = true;
    }

    float lagFactor = 1.0f - std::exp(-m_chaseLag * dt);
    m_chaseCurrentPos = m_chaseCurrentPos + (targetPos - m_chaseCurrentPos) * lagFactor;

    m_position = m_chaseCurrentPos;

    vec3 lookTarget = carPos + carUp * 1.0f;
    m_forward = (lookTarget - m_position).normalized();
    m_up = vec3(0, 1, 0);

    m_viewMatrix = mat4::lookAt(m_position, lookTarget, m_up);
}

void CameraController::updateFree(float dt)
{
    (void)dt;

    m_viewMatrix = mat4::lookAt(m_position, m_position + m_forward, m_up);
}

void CameraController::updateProjection()
{
    m_projectionMatrix = mat4::perspective(
        m_fov * 3.14159f / 180.0f,
        m_aspectRatio,
        m_nearPlane,
        m_farPlane
    );
}

void CameraController::setAspectRatio(float ar)
{
    m_aspectRatio = ar;
}

void CameraController::moveFree(const vec3& delta)
{
    vec3 right = m_forward.cross(m_up).normalized();
    m_position = m_position + m_forward * delta.z + right * delta.x + m_up * delta.y;
}

void CameraController::rotateFree(float yawDelta, float pitchDelta)
{
    m_freeYaw += yawDelta;
    m_freePitch += pitchDelta;
    m_freePitch = std::clamp(m_freePitch, -89.0f, 89.0f);

    float yawRad = m_freeYaw * 3.14159f / 180.0f;
    float pitchRad = m_freePitch * 3.14159f / 180.0f;

    m_forward = vec3(
        std::cos(pitchRad) * std::sin(yawRad),
        std::sin(pitchRad),
        std::cos(pitchRad) * std::cos(yawRad)
    ).normalized();
}

} // namespace ks::sim
