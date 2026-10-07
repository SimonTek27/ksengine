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
constexpr float kSpraySpeedMin = 5.0f;    // m/s — walking pace throws no spray
constexpr float kSpraySpeedCap = 60.0f;   // beyond this the rate stops growing
constexpr float kSprayRatePerMs = 6.0f;   // spawns/s per (m/s of wet speed)

/** Base rain emitter (disabled; driveRainEmitter() configures it per frame). */
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
    em.color = {0.70f, 0.76f, 0.86f};     // grey-blue, not white rain
    em.alpha = 0.38f;
    em.gravity = {0.0f, -3.0f, 0.0f};     // keeps them falling, not shooting
    em.drag = 0.0f;
    em.enabled = false;
    return em;
}

/** Base wheel-spray emitter (disabled; driveSprayEmitter() per frame). */
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

/** Per-frame rain rule: rate scales with intensity, off when it is not raining. */
inline void driveRainEmitter(ParticleEmitter& em,
                             const ks::math::vec3& carPos,
                             float rainIntensity) {
    em.position = {carPos.x, carPos.y + kRainAnchorHeight, carPos.z};
    em.enabled = rainIntensity > kRainIntensityMin;
    em.spawnRate = em.enabled ? rainIntensity * kRainDropRate : 0.0f;
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
