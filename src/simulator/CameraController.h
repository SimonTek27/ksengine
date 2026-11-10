#pragma once

#include "MathTypes.h"
#include "ControlsConfig.h"

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
    /** Free mode: aim the camera by hand. The showroom turntable does this
     *  every frame instead of driving it with the mouse. */
    void setForward(const vec3& direction) { m_forward = direction.normalized(); }

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

    /** Cockpit look, as controls.json authors it: the mouse and keyboard
     *  freelook speeds, plus the HMD ranges/exponents that bound a tracked
     *  head. Read every frame by SimulationLoop, so an edited document is live. */
    void setControls(const ControlsConfig& controls) { m_controls = controls; }

    /** Mouse look in pixels since the last sample, scaled by
     *  freelookMouse*Speed (radians per pixel in the document). The offset is
     *  held until recentreLook(); the keyboard offsets spring back instead. */
    void addMouseLook(float dxPixels, float dyPixels);

    /** Keyboard look for this frame: the four look directions and the recentre
     *  key. A released key walks its offset back to centre at the authored
     *  keyboard speed. One integration per frame, whatever the caller does. */
    void setLookKeys(bool up, bool down, bool left, bool right, bool recentre);

    void recentreLook();
    float lookYaw() const { return m_lookYaw + m_lookKeyYaw; }
    float lookPitch() const { return m_lookPitch + m_lookKeyPitch; }

    /** HMDLeftRightRange/UpDownRange/ForeAftRange bound a tracked head in
     *  metres and the matching exponents shape its response (yaw for
     *  left/right, pitch for up/down, roll for fore/aft). Public so the mapping
     *  can be exercised without a device attached. */
    vec3 mapHeadTranslation(const vec3& metres) const;

    void setCockpitOffset(const vec3& offset) { m_cockpitOffset = offset; }
    void setCockpitLookTarget(const vec3& target) { m_cockpitLookTarget = target; }
    void setHeadTrackingOffset(const vec3& positionMeters, const vec3& forward,
                              const vec3& up);
    void clearHeadTracking() { m_headTrackingActive = false; }
    /** Head physics: the cockpit head hangs on a spring, so longitudinal and
     *  lateral acceleration sway it instead of leaving it welded to the body.
     *  Off by default; the config's video.headPhysics turns it on. */
    void setHeadPhysics(bool enabled) { m_headPhysics = enabled; }
    bool headPhysics() const { return m_headPhysics; }

private:
    void updateCockpit(const mat4& carMatrix, float speedKmh, float dt);
    void updateLook(float dt);
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

    ControlsConfig m_controls{};
    float m_lookYaw = 0.0f;      // mouse look: held until recentred
    float m_lookPitch = 0.0f;
    float m_lookKeyYaw = 0.0f;   // keyboard look: springs back to centre
    float m_lookKeyPitch = 0.0f;
    float m_lookKeyYawRate = 0.0f;
    float m_lookKeyPitchRate = 0.0f;
    bool m_lookUp = false;
    bool m_lookDown = false;
    bool m_lookLeft = false;
    bool m_lookRight = false;
    bool m_lookPending = false;  // setLookKeys() arms one integration per frame

    vec3 m_cockpitOffset{0, 1.2f, -0.3f};
    vec3 m_cockpitLookTarget{0, 1.0f, 10.0f};

    float m_headBobPhase = 0;
    float m_headBobAmplitude = 0.003f;

    bool m_headPhysics = false;
    bool m_headTrackingActive = false;
    vec3 m_headTrackingPosition;
    vec3 m_headTrackingForward{0, 0, 1};
    vec3 m_headTrackingUp{0, 1, 0};
    vec3 m_headOffset;
    vec3 m_headVelocity;
    vec3 m_lastCarForward{0, 0, 1};
    float m_lastSpeedKmh = 0.0f;
    bool m_headInitialized = false;
};

} // namespace ks::sim
