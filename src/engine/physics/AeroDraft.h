#pragma once

#include "PhysicsCoreTypes.h"
#include <algorithm>
#include <cmath>

namespace ks {
namespace physics {

struct DraftState {
    float dragReduction = 0.0f;   // 0..0.32 fraction of drag removed
    float downforceLoss = 0.0f;   // 0..0.28 fraction of DF lost
};

/**
 * Slipstream / draft effect when following a leader car.
 * egoFwd should be unit-ish forward in world space.
 */
inline DraftState computeDraft(const PhysVec3& ego, const PhysVec3& egoFwd,
                               const PhysVec3& leader, float leaderWidth = 1.9f) {
    DraftState s;
    PhysVec3 d = ego - leader;
    float dist = d.length();
    if (dist < 0.5f || dist > 60.0f) return s;

    PhysVec3 fwd = egoFwd.normalized();
    float along = dot(d, fwd);
    if (along > 0.0f) return s; // not behind

    float behind = -along;
    PhysVec3 lat = d - fwd * along;
    float lateral = lat.length();
    float wakeWidth = leaderWidth * (0.6f + behind * 0.06f);
    if (lateral > wakeWidth) return s;

    float proximity = 1.0f - behind / 60.0f;
    float centerFactor = 1.0f - (lateral / wakeWidth) * 0.7f;
    s.dragReduction = std::clamp(0.32f * proximity * centerFactor, 0.0f, 0.32f);
    s.downforceLoss = std::clamp(0.28f * proximity * centerFactor, 0.0f, 0.28f);
    return s;
}

} // namespace physics
} // namespace ks
