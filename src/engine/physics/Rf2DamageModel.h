#pragma once
/**
 * Rf2DamageModel — thin bridge from rF2-style collision impulse to DamageSystem.
 */
#include "DamageSystem.h"
#include "PhysicsCoreTypes.h"

#include <algorithm>
#include <cmath>

namespace ks {
namespace physics {

struct Rf2DamageParams {
    float softImpulse = 500.f;
    float hardImpulse = 5000.f;
    float bodyScale = 1.0f;
    float suspensionScale = 1.0f;
    float aeroScale = 1.0f;
    bool enabled = true;
};

inline float impulseFromImpact(float relativeSpeedMs, float massKg)
{
    const float v = std::max(0.f, relativeSpeedMs);
    const float m = std::max(1.f, massKg);
    return m * v * 0.35f;
}

inline void applyRf2Impulse(DamageSystem& dmg, const Rf2DamageParams& p,
                            float impulse, const PhysVec3& contactPoint,
                            const PhysVec3& contactNormal)
{
    if (!p.enabled || impulse < 1.f) return;
    PhysVec3 dir = contactNormal;
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len > 1e-4f) {
        dir.x /= len; dir.y /= len; dir.z /= len;
    } else {
        dir = PhysVec3{0.f, 0.f, 1.f};
    }
    float energy = impulse * impulse * 0.001f * p.bodyScale;
    energy = std::clamp(energy, 0.f, 1.0e7f);
    DamageType type = DamageType::BodyPanel;
    if (impulse > p.hardImpulse) type = type | DamageType::Suspension | DamageType::Aero;
    dmg.applyImpactDamage(energy, dir, contactPoint, type);
    (void)p.suspensionScale;
    (void)p.aeroScale;
    (void)p.softImpulse;
}

} // namespace physics
} // namespace ks
