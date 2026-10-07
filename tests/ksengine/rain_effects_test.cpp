/**
 * Roadmap 2.2 / P2.8 — rain visual / spray made testable:
 *  - RainEffects.h rules: rain only while it rains (rate scales with mm/h),
 *    spray only on a wet track at speed (rate caps out at 60 m/s);
 *  - ParticleSystem integration: heavy rain really spawns drops that fall
 *    (negative mean vy), a dry stationary car throws nothing, a wet fast
 *    one throws mist.
 *
 * Note: ParticleSystem::clear() wipes the emitters too — use killAll()
 * between phases, and update(dt) clamps one call to 0.25 s of sim time.
 */
#include "KsTest.h"
#include "RainEffects.h"
#include "engine/Graphics/ParticleSystem.h"

#include <cstdio>

using ks::engine::graphics::ParticleEmitter;
using ks::engine::graphics::ParticleSystem;
using ks::math::vec3;
using ks::sim::rainfx::driveRainEmitter;
using ks::sim::rainfx::driveSprayEmitter;
using ks::sim::rainfx::makeRainEmitter;
using ks::sim::rainfx::makeSprayEmitter;

int main() {
    const vec3 car{10.f, 0.5f, -3.f};

    // --- Rain rule --------------------------------------------------------
    ParticleEmitter rain = makeRainEmitter();
    KS_CHECK(!rain.enabled);                       // base emitter is silent
    driveRainEmitter(rain, car, 0.0f);             // dry sky
    KS_CHECK(!rain.enabled && rain.spawnRate == 0.0f);
    driveRainEmitter(rain, car, 2.0f);             // light rain
    const float lightRate = rain.spawnRate;
    KS_CHECK(rain.enabled && lightRate > 0.0f);
    KS_CHECK(rain.position.x == car.x && rain.position.z == car.z);
    KS_CHECK(rain.position.y == car.y + ks::sim::rainfx::kRainAnchorHeight);
    driveRainEmitter(rain, car, 12.0f);            // heavy rain
    KS_CHECK(rain.spawnRate > lightRate);          // scales with intensity
    KS_CHECK(rain.spawnRate == 12.0f * ks::sim::rainfx::kRainDropRate);

    // --- Spray rule -------------------------------------------------------
    ParticleEmitter spray = makeSprayEmitter();
    KS_CHECK(!spray.enabled);
    driveSprayEmitter(spray, car, 0.0f, 30.f);     // dry, fast
    KS_CHECK(!spray.enabled && spray.spawnRate == 0.0f);
    driveSprayEmitter(spray, car, 1.0f, 3.0f);     // wet, crawling
    KS_CHECK(!spray.enabled && spray.spawnRate == 0.0f);
    driveSprayEmitter(spray, car, 0.3f, 10.f);     // just over both thresholds
    KS_CHECK(spray.enabled && spray.spawnRate > 0.0f);
    const float modest = spray.spawnRate;
    driveSprayEmitter(spray, car, 1.0f, 200.f);    // flat out in the wet
    KS_CHECK(spray.spawnRate > modest);
    KS_CHECK(spray.spawnRate <= ks::sim::rainfx::kSpraySpeedCap
                                * ks::sim::rainfx::kSprayRatePerMs + 1e-3f);

    // --- ParticleSystem integration --------------------------------------
    ParticleSystem ps;
    ps.setSeed(42u);
    ps.addEmitter(makeRainEmitter());   // 0
    ps.addEmitter(makeSprayEmitter());  // 1

    driveRainEmitter(ps.emitter(0), vec3{0.f, 0.f, 0.f}, 12.f);
    driveSprayEmitter(ps.emitter(1), vec3{0.f, 0.f, 0.f}, 0.f, 0.f);
    ps.update(0.25f);
    const int rainAlive = ps.aliveCount();
    KS_CHECK(rainAlive > 100);          // heavy rain spawns drops...
    float meanVy = 0.f;
    for (const auto& p : ps.particles()) meanVy += p.velocity.y;
    meanVy /= static_cast<float>(rainAlive);
    KS_CHECK(meanVy < 0.f);             // ...and they fall

    // Dry + stationary: nothing at all.
    ps.killAll();
    driveRainEmitter(ps.emitter(0), vec3{0.f, 0.f, 0.f}, 0.f);
    ps.update(0.25f);
    KS_CHECK(ps.aliveCount() == 0);

    // Wet + fast: mist spawns.
    ps.killAll();
    driveSprayEmitter(ps.emitter(1), vec3{0.f, 0.f, 0.f}, 1.f, 30.f);
    ps.update(0.25f);
    KS_CHECK(ps.aliveCount() > 10);

    std::printf("rain_effects: rainAlive=%d meanVy=%.2f\n", rainAlive, meanVy);
    return KS_TEST_RESULT("rain_effects_test");
}
