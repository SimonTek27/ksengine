#include "FFBBridge.h"
#include "../Physics/KsTireModelTireModel.h"
#include <cmath>
#include <algorithm>

namespace ks::device {

float FFBBridge::computeSteeringTorque(const FFBInputs& in,
    const KsTireModelTireModel* tireFL, const KsTireModelTireModel* tireFR) {
    auto mzFor = [](const KsTireModelTireModel* tire, float slip, float load, float camber) -> float {
        if (!tire) return 0.0f;
        KsTireModelTireModel::TireState s;
        s.slipAngle = slip;
        s.slipRatio = 0.0f;
        s.normalForce = std::max(500.0f, load);
        s.camberAngle = camber;
        s.tireTemp = 90.0f;
        s.tirePressure = 26.0f;
        s.frictionCoefficient = 1.0f;
        return tire->calculateAligningMoment(s);
    };
    float mzL = mzFor(tireFL, in.slipAngleFL, in.loadFL, in.camberFL);
    float mzR = mzFor(tireFR, in.slipAngleFR, in.loadFR, in.camberFR);
    float mzTotal = (mzL + mzR) * 0.5f;
    float loadScale = (in.loadFL + in.loadFR) / 7000.0f;
    loadScale = std::clamp(loadScale, 0.3f, 1.6f);
    float speedScale = std::clamp(in.speedMs / 30.0f, 0.0f, 1.0f);
    float scrubRadius = 0.03f;
    float mechTrail = 0.02f;
    float torqueNm = (mzTotal * 0.9f + (in.loadFL + in.loadFR) * 0.5f * scrubRadius * 0.02f * in.steerAngle) * loadScale * (0.25f + 0.75f * speedScale);
    torqueNm += mechTrail * (in.loadFL + in.loadFR) * 0.001f * in.steerAngle * speedScale;
    return std::clamp(torqueNm, -25.0f, 25.0f);
}

float FFBBridge::normalize(float torqueNm) {
    return std::clamp(torqueNm / 12.0f, -1.0f, 1.0f);
}

} // namespace ks::device
