#pragma once

/**
 * @file Audio3D.h
 * @brief Roadmap 2.3 (P2.2) — doppler / distance / bearing math — Qt-free
 *
 * Pure helpers behind SimulatorAudio's other-car voices and the 3D feed
 * from SimulationLoop. Kept free of the mixer so the rules are unit
 * testable (tests/ksengine/audio_3d_test.cpp):
 *
 *  - dopplerFactor(): closing speed raises pitch, opening lowers it,
 *    clamped to a musically sane range;
 *  - distanceGain(): inverse-distance attenuation with a fade-out before
 *    maxDistance so cars do not pop off;
 *  - bearingPan(): where the car sits relative to the listener's forward
 *    axis, -1 fully left .. +1 fully right.
 */
#include <algorithm>
#include <cmath>

namespace ks::sim {
namespace audio3d {

constexpr float kSpeedOfSound = 343.0f; // m/s at ~15 °C
constexpr float kDopplerMin = 0.70f;
constexpr float kDopplerMax = 1.45f;

/** A hearable car: where it is, how it moves and how fast its engine spins. */
struct OtherCarVoice {
    float px = 0.0f, py = 0.0f, pz = 0.0f;   // world position (m)
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;   // world velocity (m/s)
    float rpm = 0.0f;                        // engine speed for the tone
    float gain = 1.0f;                       // per-car source level
};

/**
 * Pitch ratio from the line-of-sight closing speed (m/s, > 0 approaching).
 * Plain Newtonian f' = f (c + v) / c with the result clamped: a 500 km/h
 * approach would otherwise double the pitch, which reads as a toy siren.
 */
inline float dopplerFactor(float closingSpeed, float speedOfSound = kSpeedOfSound) {
    const float c = speedOfSound > 1.0f ? speedOfSound : 1.0f;
    return std::clamp(1.0f + closingSpeed / c, kDopplerMin, kDopplerMax);
}

/**
 * Inverse-distance gain: 1 up to refDist, refDist/d beyond, faded to 0
 * over the last 20% of maxDist so a car crossing the cutoff is not an
 * abrupt click.
 */
inline float distanceGain(float distance,
                          float refDist = 6.0f,
                          float maxDist = 300.0f) {
    if (distance <= refDist) return 1.0f;
    if (distance >= maxDist) return 0.0f;
    const float fade = std::clamp((maxDist - distance) / (maxDist * 0.2f),
                                  0.0f, 1.0f);
    return (refDist / distance) * fade;
}

/**
 * Equal-power pan target for a source offset (toX, toY, toZ) from the
 * listener: +1 = fully right. right = forward × up (right-handed), so the
 * sign flips correctly when the listener turns around.
 */
inline float bearingPan(float toX, float toY, float toZ,
                        float fwdX, float fwdY, float fwdZ,
                        float upX, float upY, float upZ) {
    const float len = std::sqrt(toX * toX + toY * toY + toZ * toZ);
    if (len < 1e-6f) return 0.0f;
    const float ix = toX / len, iy = toY / len, iz = toZ / len;

    float rx = fwdY * upZ - fwdZ * upY;
    float ry = fwdZ * upX - fwdX * upZ;
    float rz = fwdX * upY - fwdY * upX;
    const float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (rl < 1e-6f) return 0.0f;
    rx /= rl; ry /= rl; rz /= rl;

    return std::clamp(ix * rx + iy * ry + iz * rz, -1.0f, 1.0f);
}

} // namespace audio3d
} // namespace ks::sim
