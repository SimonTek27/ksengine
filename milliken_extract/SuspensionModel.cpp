#include "SuspensionModel.h"

#include <algorithm>
#include <fstream>

namespace ks {
namespace physics {

SuspensionModel::SuspensionModel() {
    m_spring = getRaceSpring();
    m_damper = getRaceDamper();
    m_geom = getRaceGeometry();
    reset();
}

void SuspensionModel::reset() {
    for (int i = 0; i < 4; ++i) {
        m_corners[i] = CornerState{};
        m_corners[i].rideHeight = m_spring.staticRideHeight;
        m_corners[i].compression = 0.0f;
    }
}

float SuspensionModel::lut(const std::vector<std::pair<float, float>>& c, float x) const {
    if (c.empty()) return 0.0f;
    if (x <= c.front().first) return c.front().second;
    if (x >= c.back().first) return c.back().second;
    for (size_t i = 0; i + 1 < c.size(); ++i) {
        if (x >= c[i].first && x <= c[i + 1].first) {
            float t = (x - c[i].first) / std::max(1e-6f, c[i + 1].first - c[i].first);
            return c[i].second + t * (c[i + 1].second - c[i].second);
        }
    }
    return 0.0f;
}

float SuspensionModel::springForceAt(float compression) const {
    // compression > 0: spring pushes up
    return m_spring.rate * (compression + m_spring.preload) * m_geom.motionRatio;
}

float SuspensionModel::damperForceAt(float velocity) const {
    if (!m_damper.damperCurve.empty())
        return lut(m_damper.damperCurve, velocity);
    if (velocity >= 0.0f) {
        float rate = (velocity > m_damper.bumpThreshold) ? m_damper.fastBumpRate : m_damper.bumpRate;
        return rate * velocity;
    }
    float rate = ((-velocity) > m_damper.reboundThreshold) ? m_damper.fastReboundRate : m_damper.reboundRate;
    return rate * velocity; // velocity negative on rebound
}

float SuspensionModel::bumpStopAt(float compression) const {
    float gap = m_damper.bumpThreshold; // misuse gap from spring
    float excess = compression - (m_spring.staticRideHeight - m_spring.bumpStopGap);
    // better: travel into bump stop
    float into = compression - m_spring.bumpStopGap;
    if (into <= 0.0f) return 0.0f;
    return m_spring.bumpStopRate * into;
}

void SuspensionModel::update(float dt, float chassisAccZ, float lateralAccel, float longAccel,
                             float mass, float aeroDownforceFront, float aeroDownforceRear) {
    dt = std::clamp(dt, 1e-4f, 0.05f);
    const float g = Constants::GRAVITY;
    const float halfWb = m_geom.wheelBase * 0.5f;
    const float halfTf = m_geom.frontTrackWidth * 0.5f;
    const float halfTr = m_geom.rearTrackWidth * 0.5f;

    // Static + aero + weight transfer targets (desired normal loads)
    float W = mass * g;
    float Fzf = 0.5f * W * 0.5f + aeroDownforceFront * 0.5f; // per front wheel base
    float Fzr = 0.5f * W * 0.5f + aeroDownforceRear * 0.5f;
    // long transfer: ax positive accel -> load to rear
    float longT = (mass * longAccel * halfWb) / std::max(m_geom.wheelBase, 0.1f);
    Fzf -= longT * 0.5f;
    Fzr += longT * 0.5f;
    // lat transfer
    float latTf = (mass * lateralAccel * halfTf) / std::max(m_geom.frontTrackWidth, 0.1f);
    float latTr = (mass * lateralAccel * halfTr) / std::max(m_geom.rearTrackWidth, 0.1f);

    // Target loads FL FR RL RR
    float targets[4] = {
        std::max(200.0f, Fzf - latTf),
        std::max(200.0f, Fzf + latTf),
        std::max(200.0f, Fzr - latTr),
        std::max(200.0f, Fzr + latTr)
    };

    // ARB
    float frontARB = 0.0f, rearARB = 0.0f;
    if (m_damper.antiRollBarEnabled) {
        frontARB = (m_corners[1].compression - m_corners[0].compression) * m_damper.antiRollBarStiffness * 0.5f;
        rearARB = (m_corners[3].compression - m_corners[2].compression) * m_damper.antiRollBarStiffness * 0.5f;
    }

    for (int i = 0; i < 4; ++i) {
        auto& c = m_corners[i];
        // Simple mass-spring: force deficit drives compression
        float arb = 0.0f;
        if (i == 0) arb = -frontARB;
        if (i == 1) arb = frontARB;
        if (i == 2) arb = -rearARB;
        if (i == 3) arb = rearARB;

        float springF = springForceAt(c.compression);
        float dampF = damperForceAt(c.velocity);
        float bumpF = bumpStopAt(c.compression);
        float totalUp = springF + dampF + bumpF + arb;

        // Unbalanced force on corner sprung mass ~ mass/4
        float mCorner = mass * 0.25f;
        float net = targets[i] - totalUp - mCorner * chassisAccZ;
        // integrate compression (positive when loaded more)
        float acc = net / std::max(mCorner, 1.0f);
        c.velocity += acc * dt;
        c.velocity *= (1.0f - 0.05f * dt); // light numerical damping
        c.compression += c.velocity * dt;
        c.compression = std::clamp(c.compression, -0.05f, 0.15f);

        c.springForce = springForceAt(c.compression);
        c.damperForce = damperForceAt(c.velocity);
        c.bumpStopForce = bumpStopAt(c.compression);
        c.totalForce = c.springForce + c.damperForce + c.bumpStopForce + arb;
        c.rideHeight = m_spring.staticRideHeight - c.compression;
        c.normalLoad = std::max(0.0f, c.totalForce);
        if (!m_damper.camberCurve.empty())
            c.camberDeg = lut(m_damper.camberCurve, c.compression);
        else
            c.camberDeg = -1.0f - c.compression * 20.0f; // gain rough
    }
}

std::array<float, 4> SuspensionModel::normalLoads() const {
    return {
        m_corners[0].normalLoad,
        m_corners[1].normalLoad,
        m_corners[2].normalLoad,
        m_corners[3].normalLoad
    };
}

void SuspensionModel::loadFromIni(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key.find("RATE") != std::string::npos || key.find("WHEEL_RATE") != std::string::npos)
            m_spring.rate = std::stof(val);
        else if (key.find("BUMP") != std::string::npos && key.find("FAST") == std::string::npos)
            m_damper.bumpRate = std::stof(val);
        else if (key.find("REBOUND") != std::string::npos)
            m_damper.reboundRate = std::stof(val);
    }
}

SuspensionModel::SpringConfig SuspensionModel::getRaceSpring() {
    SpringConfig s;
    s.rate = 80000.0f;
    s.bumpStopRate = 200000.0f;
    s.staticRideHeight = 0.06f;
    s.bumpStopGap = 0.04f;
    return s;
}

SuspensionModel::DamperConfig SuspensionModel::getRaceDamper() {
    DamperConfig d;
    d.bumpRate = 4000.0f;
    d.reboundRate = 7000.0f;
    d.fastBumpRate = 3000.0f;
    d.fastReboundRate = 5500.0f;
    d.antiRollBarStiffness = 25000.0f;
    return d;
}

SuspensionModel::GeometryConfig SuspensionModel::getRaceGeometry() {
    GeometryConfig g;
    g.wheelBase = 2.7f;
    g.frontTrackWidth = 1.6f;
    g.rearTrackWidth = 1.55f;
    g.motionRatio = 1.1f;
    return g;
}

} // namespace physics
} // namespace ks
