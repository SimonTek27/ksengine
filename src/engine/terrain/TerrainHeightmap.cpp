#include "TerrainHeightmap.h"

#include <cmath>
#include <cstdint>

namespace ks::engine::terrain {

namespace {

float latticeHash(int x, int z, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x9E3779B1u;
    h ^= static_cast<std::uint32_t>(z) * 0x85EBCA77u;
    h ^= seed * 0xC2B2AE3Du;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return static_cast<float>(h & 0x00FFFFFFu) / 16777215.0f;
}

float smoothstep5(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float valueNoise(float x, float z, std::uint32_t seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int z0 = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(x0);
    const float fz = z - static_cast<float>(z0);
    const float u = smoothstep5(fx);
    const float v = smoothstep5(fz);
    const float a = latticeHash(x0, z0, seed);
    const float b = latticeHash(x0 + 1, z0, seed);
    const float c = latticeHash(x0, z0 + 1, seed);
    const float d = latticeHash(x0 + 1, z0 + 1, seed);
    return (a * (1.0f - u) + b * u) * (1.0f - v) + (c * (1.0f - u) + d * u) * v;
}

} // namespace

std::vector<float> generateFbmHeightmap(int gridW, int gridH, const FbmHeightmapParams& params) {
    if (gridW < 1 || gridH < 1) return {};

    std::vector<float> heights(static_cast<std::size_t>(gridW) * static_cast<std::size_t>(gridH));
    const float stepX = params.frequency / static_cast<float>(gridW > 1 ? gridW - 1 : 1);
    const float stepZ = params.frequency / static_cast<float>(gridH > 1 ? gridH - 1 : 1);
    const std::uint32_t seed = static_cast<std::uint32_t>(params.seed);

    for (int z = 0; z < gridH; ++z) {
        for (int x = 0; x < gridW; ++x) {
            float fx = static_cast<float>(x) * stepX;
            float fz = static_cast<float>(z) * stepZ;
            float sum = 0.0f;
            float amp = 1.0f;
            float norm = 0.0f;
            for (int octave = 0; octave < params.octaves; ++octave) {
                const float n = valueNoise(fx, fz, seed + static_cast<std::uint32_t>(octave) * 0x9E3779B1u) * 2.0f - 1.0f;
                sum += n * amp;
                norm += amp;
                amp *= params.gain;
                fx *= params.lacunarity;
                fz *= params.lacunarity;
            }
            const float n = norm > 0.0f ? sum / norm : 0.0f;
            heights[static_cast<std::size_t>(z) * static_cast<std::size_t>(gridW) + static_cast<std::size_t>(x)] =
                params.baseHeight + params.amplitude * n;
        }
    }
    return heights;
}

} // namespace ks::engine::terrain
