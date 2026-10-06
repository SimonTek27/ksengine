#pragma once

/**
 * @file PacejkaTireModel.h
 * @deprecated Compatibility shim — use KsTireModel.h directly.
 *
 * Branding: the Magic Formula tire model is KsTireModel (see KsTireModel.h);
 * the historical name PacejkaTireModel is kept as an alias so existing call
 * sites (FFB bridge, physics models, tests) keep compiling unchanged.
 *
 * TireModelManager (4-wheel set with INI round-trip: axle + per-wheel
 * sections) stays in this header; its implementation is in
 * PacejkaTireModel.cpp.
 */

#include "KsTireModel.h"

#include <string>
#include <vector>

namespace ks {
namespace physics {

using PacejkaTireModel = KsTireModel;

/** Four-wheel tire-set manager sharing one compound (INI round-trip helper). */
class TireModelManager {
public:
    TireModelManager();

    PacejkaTireModel& getModel(int wheel) { return m_models[wheel]; }
    const PacejkaTireModel& getModel(int wheel) const { return m_models[wheel]; }

    void setTireCompound(int compound);
    void setTirePressure(float frontPressure, float rearPressure);
    void loadFromIni(const std::string& tireIniPath);
    void saveToIni(const std::string& tireIniPath) const;

    std::vector<float> calculateGripCircle(int wheel, float normalForce) const;
    std::vector<float> calculateSlipCurve(int wheel, float normalForce) const;
    float estimateLapTimeImpact(int wheel, float slipAngleChange) const;

private:
    PacejkaTireModel m_models[4];
    int m_compound = 0;
    float m_frontPressure = 26.0f;
    float m_rearPressure = 24.0f;
};

} // namespace physics
} // namespace ks
