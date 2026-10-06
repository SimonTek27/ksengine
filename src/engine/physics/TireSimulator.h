#pragma once

/**
 * @file TireSimulator.h
 * @brief Per-wheel tire forces (KsTireModel / Magic Formula), temp, wear, flat-spot — Qt-free
 * Unified runtime path used by VehicleSimulator and optional VehiclePhysics bridge.
 */

#include "PhysicsCoreTypes.h"
#include "KsTireModel.h"
#include "TireFlatSpot.h"

#include <array>
#include <cmath>

namespace ks {
namespace physics {

struct TireConfig {
    TireSlipCurve slipCurve;
    double relaxationLengthLateral = 0.04;
    double relaxationLengthLongitudinal = 0.04;
    double optimalTemperature = 80.0;
    double temperatureWindow = 20.0;
    double wearFactor = 0.3;
    double thermalMass = 100.0;
    double wheelRadius = 0.33;
};

struct TireWheelState {
    double slipAngle = 0.0;
    double slipRatio = 0.0;
    double lateralForce = 0.0;
    double longitudinalForce = 0.0;
    double normalLoad = 0.0;
    double temperature = 30.0;
    double coreTemperature = 35.0;
    double wear = 0.0;
    double pressure = 2.2;
    double angularVelocity = 0.0;
    double frictionCoefficient = 1.0;
    float flatSpotSeverity = 0.0f;
};

class TireSimulator {
public:
    TireSimulator();

    void setTireConfig(const TireConfig& config);
    void setTireConfig(int wheel, const TireConfig& config);

    TireWheelState wheelState(int wheel) const;
    std::array<TireWheelState, 4> allWheelStates() const { return m_wheelStates; }

    KsTireModel& ksTire(int wheel) { return m_ksTire[static_cast<size_t>(wheel & 3)]; }
    const KsTireModel& ksTire(int wheel) const { return m_ksTire[static_cast<size_t>(wheel & 3)]; }
    /** @deprecated Prefer ksTire() */
    KsTireModel& pacejka(int wheel) { return ksTire(wheel); }
    const KsTireModel& pacejka(int wheel) const { return ksTire(wheel); }

    void update(float dt, float speed, float yawRate, float steerAngle,
                float throttle, float brake,
                const std::array<float, 4>& normalLoads,
                float trackGrip,
                float driveTorqueNm, float brakeTorqueNm);

    float totalLongitudinalForce() const;
    float totalLateralForce() const;

    void reset();

private:
    std::array<TireConfig, 4> m_configs{};
    std::array<TireWheelState, 4> m_wheelStates{};
    std::array<KsTireModel, 4> m_ksTire{};
    std::array<TireFlatSpot, 4> m_flatSpot{};
    float m_filtSlipAngle[4] = {};
    float m_filtSlipRatio[4] = {};
};

} // namespace physics
} // namespace ks
