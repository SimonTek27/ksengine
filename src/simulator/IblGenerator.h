// ---------------------------------------------------------------------------
// IblGenerator.h — image-based lighting precomputation (brief P2 / S3).
//
// One-shot, CPU-only, fully deterministic generation of the three maps the
// split-sum IBL path samples (Karis, "Physically Based Rendering" course
// notes — the same formulation the deferred/forward shaders implement):
//
//   1. procedural equirect sky    128x64  RGBA16F  (mirror env, mip 0)
//   2. GGX-prefiltered env chain  5 mips  RGBA16F  (roughness per mip)
//   3. irradiance convolution     32x16   RGBA16F  (stores E / PI, so a flat
//      sky of radiance L reproduces exactly the legacy `albedo * L` ambient)
//   4. BRDF LUT                   128x128 RGBA16F  (split-sum scale/offset)
//
// The sky is a pure zenith -> horizon -> ground gradient with *no sun disc*:
// the environment must stay stable when the sun direction changes at runtime,
// and the sun keeps doing its job as the dominant hard light through the CSM
// path. Everything here is Qt-free (raw float buffers; the renderer uploads
// them as R16G16B16A16_SFLOAT, whose linear filtering is mandatory in Vulkan)
// and uses fixed-sample Hammersley integration — no RNG, so the maps are
// bit-identical on every run and platform.
//
// Cost is a few milliseconds of CPU once at load, for maps that are ~220 KB
// of GPU memory total.
// ---------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ks::sim::ibl {

constexpr int kSkyWidth = 128;
constexpr int kSkyHeight = 64;
constexpr int kPrefilterMips = 5; // 128x64 down to 8x4
constexpr int kIrradianceWidth = 32;
constexpr int kIrradianceHeight = 16;
constexpr int kBrdfLutSize = 128;
constexpr int kPrefilterSamples = 64; // per texel, per mip
constexpr int kIrradianceSamples = 64;
constexpr int kBrdfLutSamples = 64;

constexpr float kPi = 3.14159265358979323846f;

// Linear float RGBA image. The prefiltered chain stores all mips packed
// mip-major (offset of mip i = sum of every smaller mip's texel count).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<float> rgba; // width * height * 4, linear

    size_t texelCount() const {
        return static_cast<size_t>(width) * static_cast<size_t>(height);
    }
};

// Byte offset (in texels) of mip `level` inside a packed chain starting at
// width x height and halving each level.
inline size_t chainMipOffset(int width, int height, int level) {
    size_t off = 0;
    for (int i = 0; i < level; ++i) {
        off += static_cast<size_t>(std::max(width >> i, 1)) *
               static_cast<size_t>(std::max(height >> i, 1));
    }
    return off;
}

// IEEE 754 binary16. Values here are radiance in [0, ~65500], so the simple
// round-to-nearest-even-free truncation of the mantissa is plenty precise and
// keeps the converter branch-light.
inline uint16_t floatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, sizeof(x));
    const uint32_t sign = (x >> 16) & 0x8000u;
    const int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
    const uint32_t mant = x & 0x7FFFFFu;
    if (exp <= 0) return static_cast<uint16_t>(sign);           // -> 0
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7C00u); // -> +inf
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
}

// The procedural sky. Horizon is a bright blue-white band, zenith deepens,
// everything below the horizon blends into a neutral dark ground bounce.
// Deliberately sun-disc free (see file header).
inline void skyRadiance(float dx, float dy, float dz, float outRgb[3]) {
    (void)dx; (void)dz; // gradient is elevation-only by design
    const float horizon[3] = {0.62f, 0.68f, 0.80f};
    const float zenith[3] = {0.22f, 0.36f, 0.66f};
    const float ground[3] = {0.14f, 0.135f, 0.125f};
    if (dy >= 0.0f) {
        // Most of the brightening happens in the first stretch above the
        // horizon, which is what reads as "sky" on car-body reflections.
        const float t = std::pow(dy, 0.45f);
        for (int c = 0; c < 3; ++c) outRgb[c] = horizon[c] + (zenith[c] - horizon[c]) * t;
    } else {
        const float t = std::pow(-dy, 0.5f);
        for (int c = 0; c < 3; ++c) outRgb[c] = horizon[c] + (ground[c] - horizon[c]) * t;
    }
}

// Direction at the centre of equirect texel (x, y). Row 0 is the zenith
// (v = 0 -> theta = 0 -> +y), matching the shader's
// v = acos(dir.y) / PI and the upload's row order.
inline void dirFromTexel(int x, int y, int w, int h, float out[3]) {
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
    const float phi = (u - 0.5f) * 2.0f * kPi;
    const float theta = v * kPi;
    const float st = std::sin(theta);
    out[0] = st * std::cos(phi);
    out[1] = std::cos(theta);
    out[2] = st * std::sin(phi);
}

// Bilinear fetch of the float env with horizontal wrap / vertical clamp —
// the exact addressing the GPU samplers use (REPEAT U, CLAMP V).
inline void sampleEnvBilinear(const Image& env, float dx, float dy, float dz, float out[3]) {
    float u = std::atan2(dz, dx) / (2.0f * kPi) + 0.5f;
    float v = std::acos(std::clamp(dy, -1.0f, 1.0f)) / kPi;
    const float fx = u * env.width - 0.5f;
    const float fy = v * env.height - 0.5f;
    // Clamp-to-edge on V, wrap on U — both on the *floor* index first, so a
    // fetch past the pole degenerates to the edge texel instead of bleeding
    // two rows together.
    const int x0static = static_cast<int>(std::floor(fx));
    const int iy0 = static_cast<int>(std::floor(fy));
    const int y0 = std::clamp(iy0, 0, env.height - 1);
    const int y1 = std::clamp(iy0 + 1, 0, env.height - 1);
    const float ax = fx - std::floor(fx);
    const float ay = fy - std::floor(fy);
    auto wrapX = [&](int x) {
        int m = x % env.width;
        return m < 0 ? m + env.width : m;
    };
    const int x0 = wrapX(x0static);
    const int x1 = wrapX(x0static + 1);
    for (int c = 0; c < 3; ++c) {
        const float top = env.rgba[(static_cast<size_t>(y0) * env.width + x0) * 4 + c] * (1.0f - ax) +
                          env.rgba[(static_cast<size_t>(y0) * env.width + x1) * 4 + c] * ax;
        const float bot = env.rgba[(static_cast<size_t>(y1) * env.width + x0) * 4 + c] * (1.0f - ax) +
                          env.rgba[(static_cast<size_t>(y1) * env.width + x1) * 4 + c] * ax;
        out[c] = top * (1.0f - ay) + bot * ay;
    }
}

// Van der Corput radical inverse — the deterministic Hammersley base.
inline float radicalInverseVdc(uint32_t bits) {
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return static_cast<float>(bits) * 2.3283064365386963e-10f; // * 2^-32
}

// GGX importance sample in tangent space around N = +Z.
inline void importanceSampleGgx(float xi1, float xi2, float alpha, float outH[3]) {
    const float phi = 2.0f * kPi * xi1;
    const float ct = std::sqrt(std::max((1.0f - xi2) / (1.0f + (alpha * alpha - 1.0f) * xi2), 0.0f));
    const float st = std::sqrt(std::max(1.0f - ct * ct, 0.0f));
    outH[0] = st * std::cos(phi);
    outH[1] = st * std::sin(phi);
    outH[2] = ct;
}

// Orthonormal basis around n (Duff et al. branchless frame).
inline void buildBasis(const float n[3], float t[3], float b[3]) {
    const float s = n[2] >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (s + n[2]);
    const float c = n[0] * n[1] * a;
    t[0] = 1.0f + s * n[0] * n[0] * a;
    t[1] = s * c;
    t[2] = -s * n[0];
    b[0] = c;
    b[1] = s + n[1] * n[1] * a;
    b[2] = -n[1];
}

// 1. Procedural equirect sky.
inline Image generateSky(int width = kSkyWidth, int height = kSkyHeight) {
    Image img;
    img.width = width;
    img.height = height;
    img.rgba.resize(img.texelCount() * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float d[3];
            dirFromTexel(x, y, width, height, d);
            float rgb[3];
            skyRadiance(d[0], d[1], d[2], rgb);
            const size_t o = (static_cast<size_t>(y) * width + x) * 4;
            img.rgba[o + 0] = rgb[0];
            img.rgba[o + 1] = rgb[1];
            img.rgba[o + 2] = rgb[2];
            img.rgba[o + 3] = 1.0f;
        }
    }
    return img;
}

// 2. GGX-prefiltered environment. Packed mip chain: mip 0 is the raw mirror
// env (roughness 0), mip i blurs with roughness i / (mips - 1), which is
// exactly the lod mapping the shaders use.
inline Image generatePrefilteredEnv(const Image& env, int mips = kPrefilterMips) {
    Image img;
    img.width = env.width;
    img.height = env.height;
    img.rgba.resize(chainMipOffset(env.width, env.height, mips) * 4);

    for (int mip = 0; mip < mips; ++mip) {
        const int w = std::max(env.width >> mip, 1);
        const int h = std::max(env.height >> mip, 1);
        const float roughness = static_cast<float>(mip) / static_cast<float>(mips - 1);
        const float alpha = roughness * roughness;
        const size_t base = chainMipOffset(env.width, env.height, mip);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float n[3];
                dirFromTexel(x, y, w, h, n);
                float acc[3] = {0.0f, 0.0f, 0.0f};
                if (mip == 0) {
                    // Roughness 0 = mirror: the texel direction itself.
                    sampleEnvBilinear(env, n[0], n[1], n[2], acc);
                } else {
                    float t[3], b[3];
                    buildBasis(n, t, b);
                    float wsum = 0.0f;
                    for (int s = 0; s < kPrefilterSamples; ++s) {
                        const float xi1 = (static_cast<float>(s) + 0.5f) / kPrefilterSamples;
                        const float xi2 = radicalInverseVdc(static_cast<uint32_t>(s) + 1);
                        float ht[3];
                        importanceSampleGgx(xi1, xi2, alpha, ht);
                        // V = N (the "reflect around the normal" shortcut from
                        // the Karis prefiltering paper): in the N = +Z tangent
                        // space L = 2(H.V)H - V reduces to the expression
                        // below, and ndotl is just its z component.
                        const float lv[3] = {2.0f * ht[2] * ht[0],
                                             2.0f * ht[2] * ht[1],
                                             2.0f * ht[2] * ht[2] - 1.0f};
                        const float ndotl = lv[2];
                        if (ndotl <= 0.0f) continue;
                        const float lw[3] = {lv[0] * t[0] + lv[1] * b[0] + lv[2] * n[0],
                                             lv[0] * t[1] + lv[1] * b[1] + lv[2] * n[1],
                                             lv[0] * t[2] + lv[1] * b[2] + lv[2] * n[2]};
                        float rgb[3];
                        sampleEnvBilinear(env, lw[0], lw[1], lw[2], rgb);
                        for (int c = 0; c < 3; ++c) acc[c] += rgb[c] * ndotl;
                        wsum += ndotl;
                    }
                    if (wsum > 0.0f)
                        for (int c = 0; c < 3; ++c) acc[c] /= wsum;
                }
                const size_t o = (base + static_cast<size_t>(y) * w + x) * 4;
                img.rgba[o + 0] = acc[0];
                img.rgba[o + 1] = acc[1];
                img.rgba[o + 2] = acc[2];
                img.rgba[o + 3] = 1.0f;
            }
        }
    }
    return img;
}

// 3. Irradiance convolution, cosine-weighted hemisphere MC. With pdf =
// cos(theta) / PI the estimator collapses to a plain average of radiance,
// so the map stores E / PI and the shader is a single texture * albedo
// multiply — a flat sky of radiance L yields exactly the legacy ambient L.
inline Image generateIrradiance(const Image& env, int width = kIrradianceWidth,
                                int height = kIrradianceHeight) {
    Image img;
    img.width = width;
    img.height = height;
    img.rgba.resize(img.texelCount() * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float n[3];
            dirFromTexel(x, y, width, height, n);
            float t[3], b[3];
            buildBasis(n, t, b);
            float acc[3] = {0.0f, 0.0f, 0.0f};
            for (int s = 0; s < kIrradianceSamples; ++s) {
                const float xi1 = (static_cast<float>(s) + 0.5f) / kIrradianceSamples;
                const float xi2 = radicalInverseVdc(static_cast<uint32_t>(s) + 1);
                const float phi = 2.0f * kPi * xi1;
                const float ct = std::sqrt(1.0f - xi2); // cosine-weighted
                const float st = std::sqrt(xi2);
                const float d[3] = {st * std::cos(phi) * t[0] + st * std::sin(phi) * b[0] + ct * n[0],
                                    st * std::cos(phi) * t[1] + st * std::sin(phi) * b[1] + ct * n[1],
                                    st * std::cos(phi) * t[2] + st * std::sin(phi) * b[2] + ct * n[2]};
                float rgb[3];
                sampleEnvBilinear(env, d[0], d[1], d[2], rgb);
                for (int c = 0; c < 3; ++c) acc[c] += rgb[c];
            }
            const size_t o = (static_cast<size_t>(y) * width + x) * 4;
            for (int c = 0; c < 3; ++c) img.rgba[o + c] = acc[c] / kIrradianceSamples;
            img.rgba[o + 3] = 1.0f;
        }
    }
    return img;
}

// 4. Split-sum BRDF LUT (Karis). RG = (scale, offset) for
// specular = prefiltered * (F0 * scale + offset); x = N.V, y = roughness.
inline Image generateBrdfLut(int size = kBrdfLutSize) {
    Image img;
    img.width = size;
    img.height = size;
    img.rgba.resize(img.texelCount() * 4);
    for (int y = 0; y < size; ++y) {
        const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
        const float alpha = roughness * roughness;
        // Schlick-GGX k for IBL: (roughness^2) / 2.
        const float k = alpha * 0.5f;
        for (int x = 0; x < size; ++x) {
            const float ndotv = std::clamp((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                           1e-3f, 1.0f);
            const float v[3] = {std::sqrt(std::max(1.0f - ndotv * ndotv, 0.0f)), 0.0f, ndotv};
            const float n[3] = {0.0f, 0.0f, 1.0f};
            float a = 0.0f, b = 0.0f;
            for (int s = 0; s < kBrdfLutSamples; ++s) {
                const float xi1 = (static_cast<float>(s) + 0.5f) / kBrdfLutSamples;
                const float xi2 = radicalInverseVdc(static_cast<uint32_t>(s) + 1);
                float h[3];
                importanceSampleGgx(xi1, xi2, alpha, h);
                const float vdoth = std::clamp(v[0] * h[0] + v[1] * h[1] + v[2] * h[2], 0.0f, 1.0f);
                // L = 2(V.H)H - V; N = +Z so ndotl is just l.z.
                const float l[3] = {2.0f * vdoth * h[0] - v[0], 2.0f * vdoth * h[1] - v[1],
                                    2.0f * vdoth * h[2] - v[2]};
                const float ndotl = std::clamp(l[2], 0.0f, 1.0f);
                if (ndotl <= 0.0f) continue;
                const float ndoth = std::clamp(h[2], 0.0f, 1.0f);
                const float g = (ndotv / (ndotv * (1.0f - k) + k)) *
                                (ndotl / (ndotl * (1.0f - k) + k));
                const float gvis = (g * vdoth) / (ndoth * ndotv);
                const float fc = std::pow(1.0f - vdoth, 5.0f);
                a += (1.0f - fc) * gvis;
                b += fc * gvis;
            }
            const size_t o = (static_cast<size_t>(y) * size + x) * 4;
            img.rgba[o + 0] = a / kBrdfLutSamples;
            img.rgba[o + 1] = b / kBrdfLutSamples;
            img.rgba[o + 2] = 0.0f;
            img.rgba[o + 3] = 1.0f;
        }
    }
    return img;
}

// Pack a float image (or mip chain) into the RGBA16F byte layout the GPU
// upload copies, row-major per mip.
inline std::vector<uint16_t> toRgba16F(const Image& img) {
    std::vector<uint16_t> out(img.rgba.size());
    for (size_t i = 0; i < img.rgba.size(); ++i) out[i] = floatToHalf(img.rgba[i]);
    return out;
}

} // namespace ks::sim::ibl
