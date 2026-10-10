#pragma once
#include "KsExport.h"

namespace ks {
namespace physics {
class KsTireModel;
// Deprecated alias of KsTireModel (PacejkaTireModel.h); redeclared here so
// this header can forward-declare the parameter type without pulling in the
// full tire model.
using PacejkaTireModel = KsTireModel;
} // namespace physics
} // namespace ks

namespace ks::device {

struct FFBInputs {
    float slipAngleFL = 0, slipAngleFR = 0;
    float loadFL = 3500, loadFR = 3500;
    float camberFL = -0.03f, camberFR = -0.03f;
    float speedMs = 0;
    float steerAngle = 0;
};

class KSENGINE_API FFBBridge {
public:
    static float computeSteeringTorque(const FFBInputs& in,
        const physics::PacejkaTireModel* tireFL,
        const physics::PacejkaTireModel* tireFR);
    static float normalize(float torqueNm);
};

} // namespace ks::device
