#pragma once
/** Network damage snapshot using only DamageSystem public API. */
#include "DamageSystem.h"
#include <algorithm>

namespace ks {
namespace physics {

struct DamageWireSnapshot {
    float overall = 0.f;
    float engineHealth = 1.f;
    float powerMul = 1.f;
    float handlingMul = 1.f;
    float brakingMul = 1.f;
    float downforceMul = 1.f;
    float dragMul = 1.f;
    float frontWing = 0.f;
    float rearWing = 0.f;
    uint16_t flags = 0;
};

inline DamageWireSnapshot captureDamageWire(const DamageSystem& d) {
    DamageWireSnapshot o;
    o.overall = d.overallDamage();
    o.engineHealth = d.engineDamage().health;
    o.powerMul = d.powerMultiplier();
    o.handlingMul = d.handlingMultiplier();
    o.brakingMul = d.brakingMultiplier();
    o.downforceMul = d.downforceMultiplier();
    o.dragMul = d.dragMultiplier();
    o.frontWing = d.aeroDamage().frontWingDamage;
    o.rearWing = d.aeroDamage().rearWingDamage;
    o.flags = 0;
    if (d.isEngineFailed()) o.flags |= 1u;
    if (d.isTransmissionFailed()) o.flags |= 2u;
    for (int i = 0; i < 4; ++i)
        if (d.isSuspensionBroken(i)) o.flags |= static_cast<uint16_t>(1u << (2 + i));
    return o;
}

/** Approximate apply via synthetic impacts (clients). */
inline void applyDamageWire(DamageSystem& d, const DamageWireSnapshot& in) {
    d.reset();
    if (in.overall <= 0.001f && (in.flags & 3u) == 0 && in.frontWing < 0.05f && in.rearWing < 0.05f)
        return;
    const float energy = std::max(in.overall, 0.01f) * 50000.f;
    PhysVec3 dir{0, 0, 1};
    PhysVec3 pt{0, 0.4f, 1.5f};
    d.applyImpactDamage(energy, dir, pt, DamageType::BodyPanel);
    if (in.frontWing > 0.05f || in.rearWing > 0.05f)
        d.applyImpactDamage(std::max(in.frontWing, in.rearWing) * 20000.f, dir, pt, DamageType::Aero);
    if (in.flags & 1u)
        d.applyImpactDamage(80000.f, dir, pt, DamageType::Engine);
    if (in.flags & 2u)
        d.applyImpactDamage(60000.f, dir, pt, DamageType::Transmission);
    for (int i = 0; i < 4; ++i) {
        if (in.flags & (1u << (2 + i))) {
            PhysVec3 wp{(i % 2 == 0) ? -0.7f : 0.7f, 0.f, (i < 2) ? 1.2f : -1.2f};
            d.applyImpactDamage(40000.f, dir, wp, DamageType::Suspension);
        }
    }
}

} // namespace physics
} // namespace ks
