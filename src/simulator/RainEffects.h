#pragma once

/**
 * @file RainEffects.h
 * @brief Roadmap 2.2 (P2.8) — rain + spray particle emitters, pure logic — Qt-free
 *
 * Two extra emitters for SimulationLoop::updateAndDrawParticles(), next to
 * the KS_PARTICLES dust one:
 *
 *  - rain:  a column of fast drops falling around the car while it rains,
 *           anchored above the car so the visible area stays covered as it
 *           moves (world-space drops + moving car gives the usual slant for
 *           free);
 *  - spray: white mist kicked up around the wheels while the track is wet
 *           and the car is actually moving.
 *
 *  - skid:  dark rubber strips deposited when wheels lock under hard braking,
 *           one emitter per wheel (FL..RR), positioned from the car frame;
 *
 *  - spark: white-hot crumbs shed by a brake disc that is hot enough to glow,
 *           under the same anchors that feed the glow (BRAKEDISC_FX gate,
 *           400..800 °C scale) plus a braking and speed floor.
 *
 * The enable/rate rules are the "is it raining yet" test of Roadmap 2.2, so
 * they live here as free functions instead of inside SimulationLoop — see
 * tests/ksengine/rain_effects_test.cpp.
 */
#include "engine/Graphics/ParticleSystem.h"

#include <algorithm>
#include <cmath>

namespace ks::sim {
namespace rainfx {

using ks::engine::graphics::ParticleEmitter;

// Tunables. Rates are chosen so the steady-state population
// (rate * lifetime) fits inside ParticleSystem::kMaxParticles (4096)
// even with the dust emitter running at the same time: heavy rain is
// 12 mm/h * 150 = 1800/s * 1.1 s ≈ 1980 drops, full spray 60 m/s * 6 * 1
// = 360/s * 0.7 s ≈ 252.
constexpr float kRainDropRate = 150.0f;   // spawns/s per mm/h of rain
constexpr float kRainAnchorHeight = 7.0f; // emitter sits this far above the car
constexpr float kRainIntensityMin = 0.05f;
constexpr float kSprayWetnessMin = 0.25f;
constexpr float kSpraySpeedMin = 5.0f;    // walking pace throws no spray
constexpr float kSpraySpeedCap = 60.0f;   // beyond this the rate stops growing
constexpr float kSprayRatePerMs = 6.0f;   // spawns/s per (m/s of wet speed)

// Base rain emitter (disabled; driveRainEmitter() configures it per frame).
inline ParticleEmitter makeRainEmitter() {
    ParticleEmitter em;
    em.position = {0.0f, kRainAnchorHeight, 0.0f};
    em.direction = {0.0f, -1.0f, 0.0f};   // falling straight down
    em.spreadDeg = 25.0f;                 // wide enough to cover the cockpit
    em.spawnRate = 0.0f;
    em.lifetime = 1.1f;
    em.speed = 9.0f;                      // near terminal velocity for a drop
    em.speedJitter = 2.0f;
    em.size = 0.045f;
    em.sizeJitter = 0.02f;
    em.color = {0.70f, 0.76f, 0.86f};     // grey-blue, not smoke
    em.alpha = 0.38f;
    em.gravity = {0.0f, -3.0f, 0.0f};     // keeps them falling, not shooting
    em.drag = 0.0f;
    em.enabled = false;
    return em;
}

// Base wheel-spray emitter (disabled; driveSprayEmitter() per frame).
inline ParticleEmitter makeSprayEmitter() {
    ParticleEmitter em;
    em.position = {0.0f, 0.12f, 0.0f};
    em.direction = {0.0f, 1.0f, 0.0f};    // kicked up, trails behind by motion
    em.spreadDeg = 55.0f;
    em.spawnRate = 0.0f;
    em.lifetime = 0.7f;
    em.speed = 3.5f;
    em.speedJitter = 1.5f;
    em.size = 0.28f;
    em.sizeJitter = 0.15f;
    em.color = {0.82f, 0.85f, 0.90f};     // water mist
    em.alpha = 0.45f;
    em.gravity = {0.0f, -1.2f, 0.0f};     // hangs, then settles
    em.drag = 1.8f;
    em.enabled = false;
    return em;
}

// Skidmark emitter: one per locked wheel, spawned at car position.
// Enabled per-frame from SimulationLoop when any wheel's brake ratio
// exceeds the lock threshold and wheel lateral speed is low.
// Particles are dark-brown quads that fade out over their lifetime.
inline ParticleEmitter makeSkidEmitter() {
    ParticleEmitter em;
    em.position = {0.0f, 0.0f, 0.0f};
    em.direction = {1.0f, 0.0f, 0.0f};    // spit out sideways from car centre
    em.spreadDeg = 30.0f;
    em.spawnRate = 0.0f;
    em.lifetime = 3.0f;              // skid marks linger a while
    em.speed = 1.0f;
    em.speedJitter = 0.5f;
    em.size = 0.6f;
    em.sizeJitter = 0.2f;
    em.color = {0.35f, 0.25f, 0.15f};    // dark road rubber
    em.alpha = 0.8f;
    em.gravity = {0.0f, -0.5f, 0.0f};     // slowly settle
    em.drag = 2.0f;
    em.enabled = false;
    return em;
}

/** Per-frame rain rule: rate scales with intensity, off when it is not raining. */
inline void driveRainEmitter(ParticleEmitter& em,
                             const ks::math::vec3& carPos,
                             float rainIntensity) {
    em.position = {carPos.x, carPos.y + kRainAnchorHeight, carPos.z};
    em.enabled = rainIntensity > kRainIntensityMin;
    em.spawnRate = em.enabled ? rainIntensity * kRainDropRate : 0.0f;
}

/** Per-frame skidmark rule: enabled when any wheel is locked (high brake,
 *  low wheel spin). The caller must set em.position per-wheel. */
inline void driveSkidEmitter(ParticleEmitter& em,
                             const ks::math::vec3& carPos,
                             float brake, float wheelSpeedMag) {
    // Lock threshold: brake > 0.7 and wheel spin below 5 m/s
    em.enabled = brake > 0.7f && std::fabs(wheelSpeedMag) < 5.0f;
    em.position = {carPos.x, 0.0f, carPos.z};
    em.spawnRate = em.enabled ? 20.0f : 0.0f;
}

// Spark emitter brackets: a disc turns red just past 600 °C (the same scale
// the physics brake-fade uses), and only near-lock braking at real road speed
// throws material. Rate is proportional to how far past the anchor the disc
// is, so a glowing-but-cool disc sheds nothing.
constexpr float kSparkTempMin = 600.0f;   // °C: below this, no sparks at all
constexpr float kSparkTempCap = 900.0f;   // °C: full rate
constexpr float kSparkBrakeMin = 0.85f;   // only near-lock braking sheds
constexpr float kSparkSpeedMin = 15.0f;   // m/s of road speed to throw against
constexpr float kSparkRateMax = 60.0f;    // spawns/s at full heat, per wheel
constexpr float kSparkDiscHeight = 0.25f; // above the road: the disc plane

// Base disc-spark emitter (disabled; driveSparkEmitter() configures it per
// frame). Tiny, bright, short-lived: a hot crumb falls back to the road with
// plain gravity, no drag to fake a stream.
inline ParticleEmitter makeSparkEmitter() {
    ParticleEmitter em;
    em.position = {0.0f, kSparkDiscHeight, 0.0f};
    em.direction = {0.0f, 1.0f, 0.0f};      // thrown up in a shallow arc
    em.spreadDeg = 30.0f;
    em.spawnRate = 0.0f;
    em.lifetime = 0.45f;
    em.speed = 5.0f;
    em.speedJitter = 3.5f;
    em.size = 0.06f;
    em.sizeJitter = 0.04f;
    em.color = {1.0f, 0.65f, 0.25f};        // white-hot orange
    em.alpha = 0.9f;
    em.gravity = {0.0f, -9.8f, 0.0f};
    em.drag = 0.0f;
    em.enabled = false;
    return em;
}

/** Per-frame spark rule: on only past all three gates (gate brake, disc
 *  temperature, road speed), rate scaled by the disc temperature. */
inline void driveSparkEmitter(ParticleEmitter& em,
                              const ks::math::vec3& discPos,
                              float brake, float discTempC, float speedMps) {
    const float heat = std::min(
        std::max((discTempC - kSparkTempMin) / (kSparkTempCap - kSparkTempMin),
                 0.0f), 1.0f);
    em.enabled = brake > kSparkBrakeMin && discTempC > kSparkTempMin &&
                 speedMps > kSparkSpeedMin;
    em.position = {discPos.x, std::max(discPos.y, 0.0f) + kSparkDiscHeight,
                   discPos.z};
    em.spawnRate = em.enabled ? heat * kSparkRateMax : 0.0f;
}

/** Per-frame spray rule: only a wet track at speed throws water. */
inline void driveSprayEmitter(ParticleEmitter& em,
                              const ks::math::vec3& carPos,
                              float wetness, float speedMs) {
    em.position = {carPos.x, carPos.y + 0.12f, carPos.z};
    const float v = std::fabs(speedMs);
    em.enabled = wetness > kSprayWetnessMin && v > kSpraySpeedMin;
    em.spawnRate = em.enabled
        ? wetness * std::min(v, kSpraySpeedCap) * kSprayRatePerMs
        : 0.0f;
}

} // namespace rainfx
} // namespace ks::sim