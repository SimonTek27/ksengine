#include "CameraController.h"
#include <cmath>
#include <algorithm>

namespace ks::sim {

namespace {
// A look is a look, not a neck: past 80 degrees the cockpit view is pointing
// at the seat behind it. Yaw stays inside one full turn.
constexpr float kLookPitchLimit = 1.3963f;
constexpr float kLookYawLimit = 3.14159f;

/** Rodrigues rotation of `v` about the unit `axis` by `angle` radians. */
vec3 rotateAbout(const vec3& v, const vec3& axis, float angle)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float along = axis.x * v.x + axis.y * v.y + axis.z * v.z;
    return v * c + axis.cross(v) * s + axis * (along * (1.0f - c));
}
} // namespace

CameraController::CameraController() = default;

void CameraController::setLookKeys(bool up, bool down, bool left, bool right, bool recentre)
{
    m_lookUp = up;
    m_lookDown = down;
    m_lookLeft = left;
    m_lookRight = right;
    if (recentre) recentreLook();
    m_lookPending = true;
}

void CameraController::recentreLook()
{
    m_lookYaw = 0.0f;
    m_lookPitch = 0.0f;
    m_lookKeyYaw = 0.0f;
    m_lookKeyPitch = 0.0f;
    m_lookKeyYawRate = 0.0f;
    m_lookKeyPitchRate = 0.0f;
}

void CameraController::addMouseLook(float dxPixels, float dyPixels)
{
    // Mouse speeds are radians per pixel (0.004 in the shipped document):
    // pointer right looks right, pointer up looks up.
    m_lookYaw += dxPixels * static_cast<float>(m_controls.freelookMouseYawSpeed);
    m_lookPitch -= dyPixels * static_cast<float>(m_controls.freelookMousePitchSpeed);
    m_lookYaw = std::clamp(m_lookYaw, -kLookYawLimit, kLookYawLimit);
    m_lookPitch = std::clamp(m_lookPitch, -kLookPitchLimit, kLookPitchLimit);
}

void CameraController::updateLook(float dt)
{
    const float step = std::clamp(dt, 0.0f, 0.05f);
    const float yawSpeed = static_cast<float>(m_controls.freelookKeyboardYawSpeed);
    const float pitchSpeed = static_cast<float>(m_controls.freelookKeyboardPitchSpeed);
    const float yawTarget = (m_lookRight ? yawSpeed : 0.0f) - (m_lookLeft ? yawSpeed : 0.0f);
    const float pitchTarget =
        (m_lookUp ? pitchSpeed : 0.0f) - (m_lookDown ? pitchSpeed : 0.0f);

    // The authored accel/decel are the rate limits the look keys ramp within:
    // accelerate towards the requested rate, brake towards zero on release.
    const auto ramp = [step](float& rate, float target, double accel, double decel) {
        const float limit = static_cast<float>(target == 0.0f ? decel : accel) * step;
        const float delta = target - rate;
        rate += std::abs(delta) <= limit ? delta : std::copysign(limit, delta);
    };
    ramp(m_lookKeyYawRate, yawTarget, m_controls.freelookKeyboardYawAccel,
         m_controls.freelookKeyboardYawDecel);
    ramp(m_lookKeyPitchRate, pitchTarget, m_controls.freelookKeyboardPitchAccel,
         m_controls.freelookKeyboardPitchDecel);
    m_lookKeyYaw += m_lookKeyYawRate * step;
    m_lookKeyPitch += m_lookKeyPitchRate * step;

    // Keys released and the ramp settled: the offset walks back to centre at
    // the authored keyboard speed, the way a look key behaves when let go.
    if (yawTarget == 0.0f && m_lookKeyYawRate == 0.0f) {
        const float back = yawSpeed * step;
        m_lookKeyYaw = std::abs(m_lookKeyYaw) <= back
            ? 0.0f
            : m_lookKeyYaw - std::copysign(back, m_lookKeyYaw);
    }
    if (pitchTarget == 0.0f && m_lookKeyPitchRate == 0.0f) {
        const float back = pitchSpeed * step;
        m_lookKeyPitch = std::abs(m_lookKeyPitch) <= back
            ? 0.0f
            : m_lookKeyPitch - std::copysign(back, m_lookKeyPitch);
    }
    m_lookKeyYaw = std::clamp(m_lookKeyYaw, -kLookYawLimit, kLookYawLimit);
    m_lookKeyPitch = std::clamp(m_lookKeyPitch, -kLookPitchLimit, kLookPitchLimit);
}

vec3 CameraController::mapHeadTranslation(const vec3& metres) const
{
    const auto axis = [](float value, double range, double exponent) -> float {
        const float limit = static_cast<float>(range);
        if (limit <= 0.0f) return 0.0f;
        const float normalised = std::clamp(value / limit, -1.0f, 1.0f);
        const float curve = static_cast<float>(exponent) > 0.0f
            ? std::pow(std::abs(normalised), static_cast<float>(exponent))
            : std::abs(normalised);
        return std::copysign(curve * limit, normalised);
    };
    return {axis(metres.x, m_controls.hmdLeftRightRange, m_controls.hmdYawExponent),
            axis(metres.y, m_controls.hmdUpDownRange, m_controls.hmdPitchExponent),
            axis(metres.z, m_controls.hmdForeAftRange, m_controls.hmdRollExponent)};
}

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
    // The look keys were sampled once this frame (setLookKeys): integrate them
    // here so a second update for the tracked head cannot advance them twice.
    if (m_lookPending) {
        updateLook(dt);
        m_lookPending = false;
    }

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

    if (m_headTrackingActive)
        localPos = localPos + mapHeadTranslation(m_headTrackingPosition) - m_cockpitOffset;
    m_position = carMatrix * localPos;

    vec3 localTarget = m_cockpitLookTarget;
    vec3 worldTarget = carMatrix * localTarget;

    if (m_headTrackingActive) {
        const vec3 carOrigin = carMatrix * vec3(0, 0, 0);
        const vec3 trackedForward = carMatrix * m_headTrackingForward - carOrigin;
        const vec3 trackedUp = carMatrix * m_headTrackingUp - carOrigin;
        m_forward = trackedForward.normalized();
        m_up = trackedUp.normalized();
    } else {
        m_forward = (worldTarget - m_position).normalized();
        m_up = vec3(0, 1, 0);
    }

    // Freelook: yaw about the head's own up axis (the car's in an untracked
    // cockpit), then pitch about the resulting right axis, so the two offsets
    // compose instead of fighting over the same vector.
    const float lookYaw = m_lookYaw + m_lookKeyYaw;
    const float lookPitch = std::clamp(m_lookPitch + m_lookKeyPitch, -kLookPitchLimit,
                                       kLookPitchLimit);
    if (lookYaw != 0.0f || lookPitch != 0.0f) {
        const vec3 axisUp = m_up;
        vec3 forward = rotateAbout(m_forward, axisUp, lookYaw);
        vec3 up = rotateAbout(m_up, axisUp, lookYaw);
        const vec3 right = forward.cross(up).normalized();
        forward = rotateAbout(forward, right, lookPitch);
        up = rotateAbout(up, right, lookPitch);
        m_forward = forward;
        m_up = up;
    }

    m_viewMatrix = mat4::lookAt(m_position, m_position + m_forward, m_up);
}

void CameraController::setHeadTrackingOffset(const vec3& positionMeters,
                                              const vec3& forward,
                                              const vec3& up)
{
    m_headTrackingPosition = positionMeters;
    m_headTrackingForward = forward.normalized();
    m_headTrackingUp = up.normalized();
    m_headTrackingActive = true;
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
