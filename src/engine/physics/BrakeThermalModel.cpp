#include "BrakeThermalModel.h"
#include <fstream>
#include <cstdlib>

namespace ks {
namespace physics {

void BrakeThermalModel::reset() {
    for (auto& s : m_state) {
        s = BrakeThermalState{};
        s.discTemp = 80.0f;
        s.padTemp = 60.0f;
    }
}

void BrakeThermalModel::update(float dt, int wheel, float brakeTorqueNm, float wheelOmega, float speedMs) {
    if (wheel < 0 || wheel >= 4) return;
    dt = std::clamp(dt, 1e-4f, 0.05f);
    auto& s = m_state[wheel];
    const BrakeThermalConfig& cfg = wheelConfig(wheel);

    // Heat power = |T * omega|
    float power = std::abs(brakeTorqueNm * wheelOmega);
    s.energyAccum = power * dt;

    float heatCapacity = std::max(cfg.discMass * cfg.discSpecificHeat, 1.0f);
    float dT = (s.energyAccum / heatCapacity) * 1.0f; // K

    // Cooling ~ (T - ambient) * (c0 + c1 * speed)
    float coolWperK = cfg.coolingCoeff *
                       (cfg.coolingBase + cfg.coolingSpeedFactor * std::max(speedMs, 0.0f));
    float dTcool = -coolWperK * (s.discTemp - cfg.ambientTemp) * dt / heatCapacity;

    s.discTemp = std::clamp(s.discTemp + dT + dTcool, cfg.ambientTemp, cfg.maxTemp);
    s.padTemp += (s.discTemp - s.padTemp) * cfg.padCoupling * dt;

    if (!m_curve[wheel].empty()) {
        // The car's table already carries the fade shape, so its multiplier is
        // the whole answer; the thresholds below are the no-table fallback.
        s.fade = std::clamp(1.0f - perfCurveMultiplier(wheel, s.discTemp), 0.0f, 1.0f);
    } else if (s.discTemp <= cfg.fadeStartTemp) {
        s.fade = 0.0f;
    } else if (s.discTemp >= cfg.fadeFullTemp) {
        s.fade = 1.0f;
    } else {
        s.fade = (s.discTemp - cfg.fadeStartTemp) / (cfg.fadeFullTemp - cfg.fadeStartTemp);
    }
}

void BrakeThermalModel::setWheelConfig(int wheel, const BrakeThermalConfig& c) {
    if (wheel < 0 || wheel >= 4) return;
    m_wheelCfg[wheel] = c;
    m_wheelCfgSet[wheel] = true;
}

const BrakeThermalConfig& BrakeThermalModel::wheelConfig(int wheel) const {
    if (wheel >= 0 && wheel < 4 && m_wheelCfgSet[wheel]) return m_wheelCfg[wheel];
    return m_cfg;
}

void BrakeThermalModel::setPerfCurve(int wheel,
                                    const std::vector<std::pair<float, float>>& curve) {
    if (wheel < 0 || wheel >= 4) return;
    m_curve[wheel] = curve;
}

float BrakeThermalModel::perfCurveMultiplier(int wheel, float tempC) const {
    if (wheel < 0 || wheel >= 4) return 1.0f;
    const auto& curve = m_curve[wheel];
    if (curve.empty()) return 1.0f;
    if (tempC <= curve.front().first) return curve.front().second;
    if (tempC >= curve.back().first) return curve.back().second;
    for (std::size_t i = 1; i < curve.size(); ++i) {
        const auto& b = curve[i];
        if (tempC > b.first) continue;
        const auto& a = curve[i - 1];
        const float span = b.first - a.first;
        if (span <= 0.0f) return b.second;
        const float k = (tempC - a.first) / span;
        return a.second + (b.second - a.second) * k;
    }
    return curve.back().second;
}

bool BrakeThermalModel::loadPerfCurve(const std::string& path,
                                      std::vector<std::pair<float, float>>& out) {
    out.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const auto bar = line.find('|');
        if (bar == std::string::npos) continue;
        const std::string left = line.substr(0, bar);
        const std::string right = line.substr(bar + 1);
        if (left.find_first_of("0123456789") == std::string::npos) continue;
        const float temp = static_cast<float>(std::atof(left.c_str()));
        const float mult = static_cast<float>(std::atof(right.c_str()));
        out.push_back({temp, mult});
    }
    if (out.size() < 2) { out.clear(); return false; }
    // A table is a function of temperature: order it once here so the lookup
    // can walk forward without trusting the file's line order.
    std::sort(out.begin(), out.end(),
              [](const std::pair<float, float>& a, const std::pair<float, float>& b) {
                  return a.first < b.first;
              });
    return true;
}

} // namespace physics
} // namespace ks
