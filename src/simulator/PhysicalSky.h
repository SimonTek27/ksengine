#pragma once
// Physically based sky: single scattering of the sun through a Rayleigh + Mie
// atmosphere, integrated along the view ray.
//
// This is the same model the deferred lighting pass draws as the background
// (deferred_lighting.frag, ksSkyScattered / skyColor) and the same one the
// image-based lighting environment is generated from (IblGenerator.h,
// generateSky): what the camera sees and what the car reflects then describe
// one sky instead of two. The constants below must stay identical to the
// shader's.
//
// Sea-level parameters: Rayleigh (5.5, 13.0, 22.4)e-6 /m with an 8 km scale
// height, Mie 21e-6 /m with a 1.2 km scale height and 0.758 asymmetry, planet
// 6371 km, atmosphere 6471 km, turbidity 2.2 (a clean alpine sky; 4+ is milky
// haze). kExposure brings the radiance into the frame's linear units — the
// same display calibration the shader applies, so an environment texel and the
// background pixel next to it are the same number.
//
// The sun's own disc is deliberately NOT part of the radiance: the sun is a
// directional light with shadows, and an environment that also carried a
// ~10^5 cd/m2 disc would put it into the lighting twice.
#include <algorithm>
#include <cmath>

namespace ks {
namespace sim {
namespace sky {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kPlanetRadius = 6371000.0f;
constexpr float kAtmosphereRadius = 6471000.0f;
constexpr float kRayleighHeight = 8000.0f;
constexpr float kMieHeight = 1200.0f;
constexpr float kMieG = 0.758f;
constexpr float kRayleighR = 5.5e-6f;
constexpr float kRayleighG = 13.0e-6f;
constexpr float kRayleighB = 22.4e-6f;
constexpr float kMie = 21.0e-6f;
constexpr float kTurbidity = 2.2f;
constexpr float kExposure = 48.0f;
constexpr int kViewSteps = 10;
constexpr int kSunSteps = 5;
// Ground albedo for the half of the environment below the horizon. There is no
// atmosphere down there, only asphalt and gravel. 0.22 is the ground-to-horizon
// ratio of the analytic sky this model replaced; 0.55 (a sunlit reading) was
// measured on the grid and came out 3-5% darker on the road and the grass with
// no gain on the grandstands, so the ratio the old environment used stays.
constexpr float kGroundAlbedo = 0.22f;

struct Range {
    float enter = 0.0f;
    float exit = 0.0f;
    bool hit = false;
};

// Interval [enter, exit] of the ray ro + t * rd inside a sphere at the origin.
inline Range raySphere(const float ro[3], const float rd[3], float radius) {
    const float b = ro[0] * rd[0] + ro[1] * rd[1] + ro[2] * rd[2];
    const float c = ro[0] * ro[0] + ro[1] * ro[1] + ro[2] * ro[2] - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f) return Range{0.0f, 0.0f, false};
    const float s = std::sqrt(disc);
    return Range{-b - s, -b + s, true};
}

// Scattered radiance along `dir` (unit, +y up) with the sun at `sunToward`
// (unit, pointing towards the sun), display-scaled by kExposure.
inline void radiance(const float dir[3], const float sunToward[3], float outRgb[3]) {
    outRgb[0] = outRgb[1] = outRgb[2] = 0.0f;
    const float ro[3] = {0.0f, kPlanetRadius + 2.0f, 0.0f}; // an eye 2 m up
    const Range air = raySphere(ro, dir, kAtmosphereRadius);
    if (!air.hit || air.exit <= 0.0f) return;
    float tMax = air.exit;
    // Looking down: stop where the ray meets the ground, so the haze under the
    // horizon is not integrated through the planet.
    const Range ground = raySphere(ro, dir, kPlanetRadius);
    if (ground.hit && ground.enter > 0.0f) tMax = std::min(tMax, ground.enter);
    const float stepLen = tMax / static_cast<float>(kViewSteps);
    const float betaMie = kMie * kTurbidity;
    const float mu = dir[0] * sunToward[0] + dir[1] * sunToward[1] + dir[2] * sunToward[2];
    const float gg = kMieG * kMieG;
    // Rayleigh and Henyey-Greenstein (Mie) phase functions.
    const float phaseR = 3.0f / (16.0f * kPi) * (1.0f + mu * mu);
    const float phaseM =
        3.0f / (8.0f * kPi) * ((1.0f - gg) * (mu * mu + 1.0f)) /
        (std::pow(std::max(1.0f + gg - 2.0f * mu * kMieG, 1e-6f), 1.5f) * (2.0f + gg));
    float sumR[3] = {0.0f, 0.0f, 0.0f};
    float sumM[3] = {0.0f, 0.0f, 0.0f};
    float depthR = 0.0f; // optical depth from the eye to this sample
    float depthM = 0.0f;
    for (int i = 0; i < kViewSteps; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) * stepLen;
        const float p[3] = {ro[0] + dir[0] * t, ro[1] + dir[1] * t, ro[2] + dir[2] * t};
        const float height =
            std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - kPlanetRadius;
        const float dR = std::exp(-height / kRayleighHeight) * stepLen;
        const float dM = std::exp(-height / kMieHeight) * stepLen;
        depthR += dR;
        depthM += dM;
        // ... plus the optical depth from this sample to the sun.
        const Range sun = raySphere(p, sunToward, kAtmosphereRadius);
        const float sunLen = (sun.hit ? sun.exit : 0.0f) / static_cast<float>(kSunSteps);
        float sunR = 0.0f;
        float sunM = 0.0f;
        for (int j = 0; j < kSunSteps; ++j) {
            const float st = (static_cast<float>(j) + 0.5f) * sunLen;
            const float q[3] = {p[0] + sunToward[0] * st, p[1] + sunToward[1] * st,
                                p[2] + sunToward[2] * st};
            const float hq =
                std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]) - kPlanetRadius;
            sunR += std::exp(-hq / kRayleighHeight) * sunLen;
            sunM += std::exp(-hq / kMieHeight) * sunLen;
        }
        // What survives the trip in and the trip to the sun.
        const float extinction = betaMie * (depthM + sunM);
        const float att[3] = {
            std::exp(-(extinction + kRayleighR * (depthR + sunR))),
            std::exp(-(extinction + kRayleighG * (depthR + sunR))),
            std::exp(-(extinction + kRayleighB * (depthR + sunR)))};
        sumR[0] += dR * att[0];
        sumR[1] += dR * att[1];
        sumR[2] += dR * att[2];
        sumM[0] += dM * att[0];
        sumM[1] += dM * att[1];
        sumM[2] += dM * att[2];
    }
    const float rayleigh[3] = {kRayleighR, kRayleighG, kRayleighB};
    for (int c = 0; c < 3; ++c)
        outRgb[c] = kExposure * (phaseR * rayleigh[c] * sumR[c] + phaseM * betaMie * sumM[c]);
}

// The environment around the scene: the sky above the horizon and a ground
// bounce below it, blended over the first degrees under the horizon the way
// the analytic sky blended its own ground band, so the lower hemisphere is not
// an atmosphere with a planet in it.
inline void environment(const float dir[3], const float sunToward[3], float outRgb[3]) {
    if (dir[1] >= 0.0f) {
        radiance(dir, sunToward, outRgb);
        return;
    }
    // The horizon value for this azimuth: the ray flattened onto the horizon.
    float flat[3] = {dir[0], 0.0f, dir[2]};
    const float len = std::sqrt(flat[0] * flat[0] + flat[2] * flat[2]);
    if (len < 1e-6f) {
        flat[1] = 1.0f; // straight down: no azimuth, take the zenith
        radiance(flat, sunToward, outRgb);
        return;
    }
    flat[0] /= len;
    flat[2] /= len;
    float horizon[3];
    radiance(flat, sunToward, horizon);
    const float t = std::pow(-dir[1], 0.5f);
    for (int c = 0; c < 3; ++c)
        outRgb[c] = horizon[c] * (1.0f - t) + horizon[c] * kGroundAlbedo * t;
}

} // namespace sky
} // namespace sim
} // namespace ks
