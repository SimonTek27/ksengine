#pragma once

#include "MathTypes.h"

namespace ks::sim {

class CameraController {
public:
    enum class Mode { Cockpit, Chase, Free };

    CameraController();

    void setMode(Mode m) { m_mode = m; }
    Mode mode() const { return m_mode; }

    void update(float dt, const mat4& carBodyMatrix, float speedKmh);

    mat4 viewMatrix() const { return m_viewMatrix; }
    mat4 projectionMatrix() const { return m_projectionMatrix; }

    vec3 position() const { return m_position; }
    vec3 forward() const { return m_forward; }
    void setPosition(const vec3& pos) { m_position = pos; }

    void setAspectRatio(float ar);
    void setFov(float fov) { m_fov = fov; }
    void setNearPlane(float n) { m_nearPlane = n; }
    void setFarPlane(float f) { m_farPlane = f; }
    // Read back by SimulationLoop so the renderer (and the shadow cascades,
    // which fit themselves to the camera frustum) use the *same* clip range
    // the projection matrix was built with, instead of hardcoded defaults.
    float nearPlane() const { return m_nearPlane; }
    float farPlane() const { return m_farPlane; }
    float aspectRatio() const { return m_aspectRatio; }

    void setChaseDistance(float d) { m_chaseDistance = d; }
    void setChaseHeight(float h) { m_chaseHeight = h; }
    void setChaseLag(float l) { m_chaseLag = l; }

    void moveFree(const vec3& delta);
    void rotateFree(float yawDelta, float pitchDelta);

    void setCockpitOffset(const vec3& offset) { m_cockpitOffset = offset; }
    void setCockpitLookTarget(const vec3& target) { m_cockpitLookTarget = target; }

private:
    void updateCockpit(const mat4& carMatrix, float speedKmh);
    void updateChase(const mat4& carMatrix, float speedKmh, float dt);
    void updateFree(float dt);
    void updateProjection();

    Mode m_mode = Mode::Chase;

    mat4 m_viewMatrix;
    mat4 m_projectionMatrix;

    vec3 m_position;
    vec3 m_forward;
    vec3 m_up{0, 1, 0};

    float m_aspectRatio = 16.0f / 9.0f;
    float m_fov = 60.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 10000.0f;

    float m_chaseDistance = 6.0f;
    float m_chaseHeight = 2.5f;
    float m_chaseLag = 5.0f;
    vec3 m_chaseCurrentPos;
    bool m_chaseInitialized = false;

    float m_freeYaw = 0;
    float m_freePitch = 0;
    float m_freeSpeed = 20.0f;

    vec3 m_cockpitOffset{0, 1.2f, -0.3f};
    vec3 m_cockpitLookTarget{0, 1.0f, 10.0f};

    float m_headBobPhase = 0;
    float m_headBobAmplitude = 0.003f;
};

} // namespace ks::sim
