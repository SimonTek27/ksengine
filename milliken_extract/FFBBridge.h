#pragma once

class KsTireModelTireModel;

namespace ks::device {

struct FFBInputs {
    float slipAngleFL = 0, slipAngleFR = 0;
    float loadFL = 3500, loadFR = 3500;
    float camberFL = -0.03f, camberFR = -0.03f;
    float speedMs = 0;
    float steerAngle = 0;
};

class FFBBridge {
public:
    static float computeSteeringTorque(const FFBInputs& in,
        const KsTireModelTireModel* tireFL, const KsTireModelTireModel* tireFR);
    static float normalize(float torqueNm);
};

} // namespace ks::device
