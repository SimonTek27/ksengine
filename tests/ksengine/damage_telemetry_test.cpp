/**
 * Roadmap 1.5 / GAP P1.10 — damage telemetry channels
 * (docs/DAMAGE_TELEMETRY.md).
 *
 * sampleDamage() is the single snapshot that feeds the damage strip of the
 * race HUD (RaceHudSample), the UDP/TCP packet v2 damage fields and the AC
 * shared-memory carDamage[5] body zones. Header-only helper over
 * DamageSystem, so this test only needs the ksengine lib.
 *
 * Guards two regressions found while wiring it up:
 *  - a pristine zone used to read 0.3 damage (combinedDamage mixed integrity
 *    and damage semantics), which made a brand-new car report ~18% damage;
 *  - applyImpact() decremented the cosmetic channel, pinning it at 0 forever.
 */
#include "KsTest.h"
#include "engine/physics/DamageTelemetry.h"

#include <algorithm>

using ks::physics::DamageSystem;
using ks::physics::DamageType;
using ks::physics::PhysVec3;
using ks::physics::sampleDamage;

namespace {

// Hits that together cover every body zone: front/rear centres, the four
// corners (corners also reach the side zones) and a low centre point for
// cabin + underbody. 3 reps x 100 kJ saturates most zones, so the overall
// channel crosses the documented 0.3 warning threshold with margin.
void bombard(DamageSystem& d) {
    static const PhysVec3 points[] = {
        {0.f, 0.5f, 2.f},   // front centre
        {0.f, 0.5f, -2.f},  // rear centre
        {1.f, 0.5f, 2.f},   // front left corner
        {-1.f, 0.5f, 2.f},  // front right corner
        {1.f, 0.5f, -2.f},  // rear left corner + left side
        {-1.f, 0.5f, -2.f}, // rear right corner + right side
        {0.f, 0.1f, 0.f},   // low centre: cabin + underbody
    };
    for (int rep = 0; rep < 3; ++rep)
        for (const auto& p : points)
            d.applyImpactDamage(100000.f, PhysVec3(0.f, 0.f, -1.f), p,
                                DamageType::BodyPanel);
}

bool in01(float v) { return v >= 0.f && v <= 1.f; }

} // namespace

int main() {
    // --- Fresh car: every channel healthy (a pristine zone reads 0) --------
    DamageSystem d;
    auto s = sampleDamage(d);
    KS_CHECK_NEAR(s.overall, 0.f, 1e-6f);
    KS_CHECK_NEAR(s.structural, 0.f, 1e-6f);
    KS_CHECK_NEAR(s.cosmetic, 0.f, 1e-6f);
    KS_CHECK_NEAR(s.engineHealth, 1.f, 1e-6f);
    KS_CHECK_NEAR(s.powerMult, 1.f, 1e-6f);
    KS_CHECK_NEAR(s.handlingMult, 1.f, 1e-6f);
    KS_CHECK(s.warningLevel == 0);
    KS_CHECK(!s.engineSeized);
    KS_CHECK(!s.transmissionStuck);
    KS_CHECK(s.collisionCount == 0);
    for (int i = 0; i < 5; ++i) KS_CHECK_NEAR(s.carDamage[i], 0.f, 1e-6f);
    for (int i = 0; i < 4; ++i) KS_CHECK_NEAR(s.suspIntegrity[i], 1.f, 1e-6f);

    // --- Front impact: damage appears, front channel moves first ----------
    d.applyImpactDamage(60000.f, PhysVec3(0.f, 0.f, -1.f),
                        PhysVec3(0.f, 0.5f, 2.f), DamageType::BodyPanel);
    s = sampleDamage(d);
    KS_CHECK(s.overall > 0.f);
    KS_CHECK(s.cosmetic > 0.f);                // cosmetic accumulates on impact
    KS_CHECK(s.carDamage[0] > s.carDamage[1]); // front hit, rear still clean
    KS_CHECK(s.engineHealth < 1.f);            // engine-bay zone takes damage
    KS_CHECK(s.suspIntegrity[0] < 1.f);        // front corners weaken front susp
    KS_CHECK(s.carDamage[4] == std::clamp(s.overall, 0.f, 1.f));
    for (int i = 0; i < 5; ++i) KS_CHECK(in01(s.carDamage[i]));
    for (int i = 0; i < 4; ++i) KS_CHECK(in01(s.suspIntegrity[i]));
    KS_CHECK(in01(s.powerMult));
    KS_CHECK(in01(s.engineHealth));
    // Warning bands documented in sampleDamage / DAMAGE_TELEMETRY.md:
    if (s.overall > 0.8f) KS_CHECK(s.warningLevel == 2);
    else if (s.overall > 0.3f || s.engineHealth < 0.5f) KS_CHECK(s.warningLevel == 1);
    else KS_CHECK(s.warningLevel == 0);

    // --- Same impact again: damage never goes down ------------------------
    const float before = s.overall;
    d.applyImpactDamage(60000.f, PhysVec3(0.f, 0.f, -1.f),
                        PhysVec3(0.f, 0.5f, 2.f), DamageType::BodyPanel);
    s = sampleDamage(d);
    KS_CHECK(s.overall >= before);

    // --- Bombardment: crosses the documented 0.3 warning threshold --------
    bombard(d);
    s = sampleDamage(d);
    KS_CHECK(s.overall > 0.3f);
    KS_CHECK(s.warningLevel >= 1);
    KS_CHECK(s.engineHealth < 1.f);
    for (int i = 0; i < 5; ++i) KS_CHECK(in01(s.carDamage[i]));
    for (int i = 0; i < 4; ++i) KS_CHECK(in01(s.suspIntegrity[i]));

    // The sim calls DamageSystem::update() every tick (VehicleSimulator), which
    // refreshes the cached physics multipliers the telemetry samples.
    d.update(0.016f, 50.f, 4000.f);
    s = sampleDamage(d);
    KS_CHECK(s.powerMult < 1.f);    // damaged engine derates power
    KS_CHECK(s.powerMult > 0.f);
    KS_CHECK(s.handlingMult < 1.f); // broken/damaged suspension

    // --- repairAll restores every channel to pristine ---------------------
    d.repairAll();
    s = sampleDamage(d);
    KS_CHECK_NEAR(s.overall, 0.f, 1e-6f);
    KS_CHECK_NEAR(s.structural, 0.f, 1e-6f);
    KS_CHECK_NEAR(s.cosmetic, 0.f, 1e-6f);
    KS_CHECK_NEAR(s.engineHealth, 1.f, 1e-6f);
    KS_CHECK_NEAR(s.powerMult, 1.f, 1e-6f);
    KS_CHECK_NEAR(s.handlingMult, 1.f, 1e-6f);
    KS_CHECK(s.warningLevel == 0);
    KS_CHECK(!s.engineSeized);
    for (int i = 0; i < 5; ++i) KS_CHECK_NEAR(s.carDamage[i], 0.f, 1e-6f);
    for (int i = 0; i < 4; ++i) KS_CHECK_NEAR(s.suspIntegrity[i], 1.f, 1e-6f);

    return KS_TEST_RESULT("damage_telemetry_test");
}
