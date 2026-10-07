#pragma once

/**
 * @file SuspensionModel.h
 * @brief Spring/damper per-corner + anti-roll — Qt-free
 */

#include "PhysicsCoreTypes.h"

#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace ks {
namespace physics {

class SuspensionModel {
public:
    struct SpringConfig {
        float rate = 15000.0f;          // N/m
        float preload = 0.0f;           // m equivalent
        float minLength = 0.3f;
        float maxLength = 0.5f;
        float bumpStopRate = 50000.0f;
        float bumpStopGap = 0.02f;      // travel before bump stop
        float staticRideHeight = 0.08f; // m
    };

    struct DamperConfig {
        float bumpRate = 2000.0f;       // Ns/m
        float reboundRate = 4000.0f;
        float fastBumpRate = 1500.0f;
        float fastReboundRate = 3000.0f;
        float bumpThreshold = 0.05f;    // m/s
        float reboundThreshold = 0.05f;
        float antiRollBarStiffness = 15000.0f; // N/m effective
        bool antiRollBarEnabled = true;
        std::vector<std::pair<float, float>> damperCurve; // vel -> force
        std::vector<std::pair<float, float>> camberCurve; // travel -> camber deg
    };

    struct GeometryConfig {
        float wheelBase = 2.7f;
        float frontTrackWidth = 1.55f;
        float rearTrackWidth = 1.50f;
        float motionRatio = 1.0f;
        float rollCenterFront = 0.05f;
        float rollCenterRear = 0.08f;
    };

    struct CornerState {
        float compression = 0.0f;  // m positive compressed from static
        float velocity = 0.0f;     // m/s
        float springForce = 0.0f;
        float damperForce = 0.0f;
        float bumpStopForce = 0.0f;
        float totalForce = 0.0f;   // upward on chassis
        float rideHeight = 0.08f;
        float camberDeg = -1.0f;
        float normalLoad = 0.0f;   // on tire
    };

    SuspensionModel();

    void setSpringConfig(const SpringConfig& c) { m_spring = c; }
    void setDamperConfig(const DamperConfig& c) { m_damper = c; }
    void setGeometryConfig(const GeometryConfig& c) { m_geom = c; }
    const SpringConfig& springConfig() const { return m_spring; }
    const DamperConfig& damperConfig() const { return m_damper; }
    const GeometryConfig& geometryConfig() const { return m_geom; }

    /**
     * Update 4 corners.
     * @param chassisAccZ vertical accel of chassis (m/s^2, +up)
     * @param lateralAccel for weight transfer (m/s^2)
     * @param longAccel for weight transfer
     * @param mass vehicle mass kg
     * @param aeroDownforceFront / Rear N
     */
    void update(float dt, float chassisAccZ, float lateralAccel, float longAccel,
                float mass, float aeroDownforceFront, float aeroDownforceRear);

    const CornerState& corner(int i) const { return m_corners[i]; }
    std::array<float, 4> normalLoads() const;

    float rideHeightFront() const {
        return 0.5f * (m_corners[0].rideHeight + m_corners[1].rideHeight);
    }
    float rideHeightRear() const {
        return 0.5f * (m_corners[2].rideHeight + m_corners[3].rideHeight);
    }

    void reset();
    void loadFromIni(const std::string& path);

    static SpringConfig getRaceSpring();
    static DamperConfig getRaceDamper();
    static GeometryConfig getRaceGeometry();

private:
    float springForceAt(float compression) const;
    float damperForceAt(float velocity) const;
    float bumpStopAt(float compression) const;
    float lut(const std::vector<std::pair<float, float>>& c, float x) const;

    SpringConfig m_spring;
    DamperConfig m_damper;
    GeometryConfig m_geom;
    std::array<CornerState, 4> m_corners{};
};

} // namespace physics
} // namespace ks
