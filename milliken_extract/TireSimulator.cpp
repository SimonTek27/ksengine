#include "TireSimulator.h"

#include <algorithm>

namespace ks {
namespace physics {

TireSimulator::TireSimulator() {
    TireConfig cfg;
    for (int i = 0; i < 4; ++i) {
        m_configs[i] = cfg;
        m_ksTire[static_cast<size_t>(i)].setCoefficients(KsTireModel::getSlickTireCoefficients());
        m_wheelStates[i].pressure = (i < 2) ? 2.2 : 2.0;
    }
}

void TireSimulator::setTireConfig(const TireConfig& config) {
    for (int i = 0; i < 4; ++i) m_configs[i] = config;
}

void TireSimulator::setTireConfig(int wheel, const TireConfig& config) {
    if (wheel >= 0 && wheel < 4) m_configs[wheel] = config;
}

TireWheelState TireSimulator::wheelState(int wheel) const {
    if (wheel < 0 || wheel >= 4) return {};
    return m_wheelStates[wheel];
}

void TireSimulator::reset() {
    for (int i = 0; i < 4; ++i) {
        m_wheelStates[i] = TireWheelState{};
        m_wheelStates[i].pressure = (i < 2) ? 2.2 : 2.0;
        m_flatSpot[i].reset();
        m_filtSlipAngle[i] = 0.0f;
        m_filtSlipRatio[i] = 0.0f;
    }
}

void TireSimulator::update(float dt, float speed, float yawRate, float steerAngle,
                           float throttle, float brake,
                           const std::array<float, 4>& normalLoads,
                           float trackGrip,
                           float driveTorqueNm, float brakeTorqueNm) {
    dt = std::clamp(dt, 1e-4f, 0.05f);
    const float v = std::max(speed, 0.5f);
    const float halfWb = 1.35f;

    for (int i = 0; i < 4; ++i) {
        const auto& cfg = m_configs[i];
        auto& st = m_wheelStates[i];
        st.normalLoad = normalLoads[i];

        const bool front = (i < 2);
        const float xArm = front ? halfWb : -halfWb;

        const float vx = v;
        const float vy = yawRate * xArm;
        const float delta = front ? steerAngle : 0.0f;

        const float c = std::cos(delta);
        const float s = std::sin(delta);
        const float vwx =  vx * c + vy * s;
        const float vwy = -vx * s + vy * c;
        float slipAngleRad = std::atan2(vwy, std::max(std::abs(vwx), 0.5f));

        const float radius = static_cast<float>(cfg.wheelRadius);
        const float load = std::max(static_cast<float>(st.normalLoad), 200.0f);

        float torque = 0.0f;
        if (!front && throttle > 0.01f)
            torque = driveTorqueNm * 0.5f; // RWD split

        float brakeT = brakeTorqueNm * 0.25f;
        float omegaFree = vwx / radius;
        float omega = omegaFree + (torque - brakeT) / (load * 0.02f + 1.0f) * dt;
        st.angularVelocity = omega;

        float slipRatio = 0.0f;
        if (std::abs(vwx) > 0.5f)
            slipRatio = (omega * radius - vwx) / std::abs(vwx);
        slipRatio = std::clamp(slipRatio, -1.0f, 1.0f);

        const float tauA = static_cast<float>(cfg.relaxationLengthLateral) / v;
        const float tauR = static_cast<float>(cfg.relaxationLengthLongitudinal) / v;
        const float aA = dt / (tauA + dt);
        const float aR = dt / (tauR + dt);
        m_filtSlipAngle[i] += aA * (slipAngleRad - m_filtSlipAngle[i]);
        m_filtSlipRatio[i] += aR * (slipRatio - m_filtSlipRatio[i]);

        KsTireModel::TireState ts;
        ts.slipAngle = m_filtSlipAngle[i];
        ts.slipRatio = m_filtSlipRatio[i];
        ts.normalForce = load;
        ts.tireTemp = static_cast<float>(st.temperature);
        ts.tirePressure = static_cast<float>(st.pressure * 14.5038f);
        ts.frictionCoefficient = trackGrip;

        auto forces = m_ksTire[static_cast<size_t>(i)].calculateForces(ts);

        float tempEff = m_ksTire[static_cast<size_t>(i)].calculateTemperatureEffect(ts.tireTemp);
        float wearEff = 1.0f - static_cast<float>(st.wear) * static_cast<float>(cfg.wearFactor);
        wearEff = std::clamp(wearEff, 0.4f, 1.0f);

        bool locked = (brake > 0.85f && std::abs(slipRatio) > 0.9f);
        m_flatSpot[i].update(locked, speed, load, dt);
        float flatPen = m_flatSpot[i].gripPenalty();

        float scale = tempEff * wearEff * flatPen * trackGrip;
        st.lateralForce = forces.lateralForce * scale;
        st.longitudinalForce = forces.longitudinalForce * scale;
        st.slipAngle = KsTireModel::radToDeg(m_filtSlipAngle[i]);
        st.slipRatio = m_filtSlipRatio[i];
        st.flatSpotSeverity = m_flatSpot[i].state.severity;

        float heat = (std::abs(st.lateralForce) + std::abs(st.longitudinalForce)) * 0.00002f * v;
        st.temperature += (heat - (st.temperature - 25.0) * 0.02) * dt * (100.0 / cfg.thermalMass);
        st.coreTemperature += (st.temperature - st.coreTemperature) * 0.1 * dt;
        st.wear = std::clamp(st.wear + heat * 0.000001 * dt, 0.0, 1.0);
        st.frictionCoefficient = tempEff * wearEff * flatPen;
    }
}

float TireSimulator::totalLongitudinalForce() const {
    float s = 0.0f;
    for (const auto& w : m_wheelStates) s += static_cast<float>(w.longitudinalForce);
    return s;
}

float TireSimulator::totalLateralForce() const {
    float s = 0.0f;
    for (const auto& w : m_wheelStates) s += static_cast<float>(w.lateralForce);
    return s;
}

} // namespace physics
} // namespace ks
