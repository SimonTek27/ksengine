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

    void close() { m_active = false; }
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
    }

    /** Camera on the turntable: distance away, `cameraHeight` up, at the
     *  current angle (0 = in front of the car, +90 = to its right). */
    vec3 position() const {
        const float radians = m_angleDeg * (3.14159265f / 180.f);
        return {std::sin(radians) * m_config.cameraDistance, m_config.cameraHeight,
                std::cos(radians) * m_config.cameraDistance};
    }

    /** What the camera looks at: the car's middle, not its roof. */
    vec3 target() const { return {0.f, m_config.lookAtHeight, 0.f}; }

    float angleDeg() const { return m_angleDeg; }

private:
    ShowroomConfig m_config;
    float m_angleDeg = 0.f;
    bool m_active = false;
};

} // namespace sim
} // namespace ks
