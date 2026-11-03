#pragma once
/**
 * The showroom as the game runs it: the loaded config plus the turntable the
 * camera sits on.
 *
 * Kept apart from ShowroomConfig (the file format) and from SimulationLoop (the
 * scene) so the orbit can be exercised without a car, a renderer or a window.
 * The camera is a free camera aimed at the car: chase mode keeps snapping to a
 * body that is not driven here.
 */
#include "CameraController.h"
#include "ShowroomConfig.h"

#include <cmath>
#include <string>

namespace ks {
namespace sim {

class ShowroomScene {
public:
    /** Load `directory/showroom.json` and start the turntable. */
    bool open(const std::string& directory, std::string* error = nullptr) {
        ShowroomConfig config = loadShowroom(directory, error);
        if (!config.valid) return false;
        m_config = std::move(config);
        m_angleDeg = m_config.cameraAngleDeg;
        m_active = true;
        return true;
    }

    /** Where the hall actually is, from the meshes that were loaded. Until a
     *  hall sets this, the turntable orbits the engine origin — which is only
     *  right when the venue happens to be centred there. */
    void setHallBounds(float centreX, float centreZ, float floorY, float radius) {
        m_hallCentre = vec3(centreX, 0.f, centreZ);
        m_hallFloor = floorY;
        m_hallRadius = radius;
    }

    void close() {
        m_active = false;
        m_hallRadius = 0.f;
        m_hallFloor = 0.f;
        m_hallCentre = vec3(0.f, 0.f, 0.f);
    }
    bool active() const { return m_active; }
    const ShowroomConfig& config() const { return m_config; }

    /** Advance the turntable and pose the camera on it, looking at the car.
     *  A showroom with rotation 0 stands still. */
    void apply(float dt, CameraController& camera) {
        if (!m_active) return;
        if (dt > 0.f) {
            m_angleDeg += m_config.cameraRotationDegPerSec * dt;
            m_angleDeg = std::fmod(m_angleDeg, 360.f);
            if (m_angleDeg < 0.f) m_angleDeg += 360.f;
        }
        camera.setMode(CameraController::Mode::Free);
        camera.setPosition(position());
        camera.setForward((target() - camera.position()).normalized());
        camera.setFov(m_config.cameraFov);
        // The cascade splits are derived from the camera's clip range: left at
        // the engine default (10 km) the shadow map covers kilometres and a car
        // casts no resolvable shadow at all. A room needs a room-sized range.
        camera.setNearPlane(0.1f);
        camera.setFarPlane(160.f);
    }

    /** Camera on the turntable: distance away, `cameraHeight` up, at the
     *  current angle (0 = in front of the car, +90 = to its right). */
    /** How far out the camera sits: what the file asked for, unless the hall
     *  is big enough to need more room than that. */
    float distance() const {
        // Never so far that the camera leaves the room: a 63 m dome is a
        // background, not a place to stand outside of.
        const float cap = m_hallRadius > 1.f ? m_hallRadius * 0.5f : m_config.cameraDistance;
        return cap < m_config.cameraDistance ? cap : m_config.cameraDistance;
    }

    vec3 position() const {
        const float radians = m_angleDeg * (3.14159265f / 180.f);
        const float d = distance();
        return {m_hallCentre.x + std::sin(radians) * d,
                m_hallFloor + m_config.cameraHeight,
                m_hallCentre.z + std::cos(radians) * d};
    }

    /** What the camera looks at: the middle of the room, at the car's height. */
    vec3 target() const {
        return {m_hallCentre.x, m_hallFloor + m_config.lookAtHeight, m_hallCentre.z};
    }

    float angleDeg() const { return m_angleDeg; }

private:
    ShowroomConfig m_config;
    float m_angleDeg = 0.f;
    bool m_active = false;
    vec3 m_hallCentre{0.f, 0.f, 0.f};
    float m_hallFloor = 0.f;
    float m_hallRadius = 0.f;
};

} // namespace sim
} // namespace ks
