#pragma once

#include <vector>
#include <utility>
#include <algorithm>
#include <cmath>

/**
 * Suspension kinematics LUTs — Qt-free (std::vector).
 */
class SuspensionKinematics {
public:
    struct DamperCurvePoint { float velocity; float force; };

    std::vector<DamperCurvePoint> damperCurve;
    std::vector<std::pair<float, float>> camberCurve;
    std::vector<std::pair<float, float>> bumpSteerCurve;
    float antiRollBarStiffness = 15000.0f;
    bool antiRollBarEnabled = true;

    static SuspensionKinematics stock() {
        SuspensionKinematics k;
        k.damperCurve = {
            {-1.0f, -4500.f}, {-0.3f, -2600.f}, {-0.05f, -400.f},
            {0.0f, 0.f}, {0.05f, 350.f}, {0.3f, 1800.f}, {1.0f, 3200.f}
        };
        k.camberCurve = {{-0.05f, 0.6f}, {0.0f, 0.0f}, {0.05f, -0.9f}};
        k.bumpSteerCurve = {{-0.05f, -0.25f}, {0.0f, 0.0f}, {0.05f, 0.3f}};
        return k;
    }

    float damperForce(float v) const {
        if (damperCurve.empty()) return v * 2500.0f;
        if (v <= damperCurve.front().velocity) return damperCurve.front().force;
        if (v >= damperCurve.back().velocity) return damperCurve.back().force;
        for (size_t i = 0; i + 1 < damperCurve.size(); ++i) {
            const auto& a = damperCurve[i];
            const auto& b = damperCurve[i + 1];
            if (v >= a.velocity && v <= b.velocity) {
                float t = (v - a.velocity) / std::max(1e-6f, b.velocity - a.velocity);
                return a.force + t * (b.force - a.force);
            }
        }
        return v * 2500.0f;
    }

    float camberFromTravel(float travel) const { return lut(camberCurve, travel); }
    float bumpSteerFromTravel(float travel) const { return lut(bumpSteerCurve, travel); }

    float antiRollForce(float leftTravel, float rightTravel) const {
        if (!antiRollBarEnabled) return 0.0f;
        return (rightTravel - leftTravel) * antiRollBarStiffness * 0.5f;
    }

private:
    static float lut(const std::vector<std::pair<float, float>>& c, float x) {
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
};
