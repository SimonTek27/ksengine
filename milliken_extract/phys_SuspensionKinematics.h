#pragma once
#include <QVector>
#include <QPair>
#include <QtGlobal>
#include <algorithm>

class SuspensionKinematics {
public:
    struct DamperCurvePoint { float velocity; float force; };
    QVector<DamperCurvePoint> damperCurve;
    QVector<QPair<float,float>> camberCurve;
    QVector<QPair<float,float>> bumpSteerCurve;
    float antiRollBarStiffness = 15000.0f;
    bool antiRollBarEnabled = true;

    static SuspensionKinematics stock() {
        SuspensionKinematics k;
        k.damperCurve = {{-1.0f,-4500},{-0.3f,-2600},{-0.05f,-400},{0.0f,0},{0.05f,350},{0.3f,1800},{1.0f,3200}};
        k.camberCurve = {{-0.05f,0.6f},{0.0f,0.0f},{0.05f,-0.9f}};
        k.bumpSteerCurve = {{-0.05f,-0.25f},{0.0f,0.0f},{0.05f,0.3f}};
        return k;
    }
    float damperForce(float v) const {
        if (damperCurve.isEmpty()) return v * 2500.0f;
        if (v <= damperCurve.first().velocity) return damperCurve.first().force;
        if (v >= damperCurve.last().velocity) return damperCurve.last().force;
        for (int i = 0; i + 1 < damperCurve.size(); ++i) {
            auto a = damperCurve[i], b = damperCurve[i+1];
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
    static float lut(const QVector<QPair<float,float>>& c, float x) {
        if (c.isEmpty()) return 0.0f;
        if (x <= c.first().first) return c.first().second;
        if (x >= c.last().first) return c.last().second;
        for (int i = 0; i + 1 < c.size(); ++i) {
            if (x >= c[i].first && x <= c[i+1].first) {
                float t = (x - c[i].first) / std::max(1e-6f, c[i+1].first - c[i].first);
                return c[i].second + t * (c[i+1].second - c[i].second);
            }
        }
        return 0.0f;
    }
};
