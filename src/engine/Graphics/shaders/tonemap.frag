#version 450

#extension GL_GOOGLE_include_directive : require
// KS_MATERIAL_COVERAGE_UNLIT_BASE: the GBuffer coverage magnitude tells this
// pass which pixels are a photograph rather than a lit surface.
#include "../MaterialResponseConstants.h"

// Display pass of the Qt-free deferred path (NativeRenderer): reads the
// linear-HDR frame the TAA resolve just wrote plus the half-res bloom buffer
// and writes the *encoded* backbuffer. Nothing else in the frame touches the
// swapchain, so this is the single place the output transfer function lives.
//
// Two encodings, picked by pc.hdrOutput:
//
//   0 = SDR   : tone curve -> grade -> sRGB OETF. The swapchain is 8-bit
//               B8G8R8A8_UNORM with VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, so
//               the transfer function has to be applied here (writing the
//               linear value straight out is what made the old resolve pass
//               dark and clipped).
//   1 = HDR10 : tone curve -> grade, then the graded signal is mapped to
//               absolute luminance (1.0 = the OS's SDR-white in nits, so the
//               frame matches what the SDR path would show; anything
//               brighter than paper white rides a compressive shoulder up to
//               pc.peakNits instead of clipping at white), converted to
//               Rec.2020 primaries and PQ-encoded (ST.2084) for a
//               VK_COLOR_SPACE_HDR10_ST2084_EXT swapchain.
//
// Bindings and push constants are written by
// NativeRenderer::writeDisplayDescriptorSets() / NativeRenderer::endFrame().
// (The editor's ksPostProcess.frag also takes an AO input; this renderer has
// no AO pass, so that slot was dropped instead of kept as a bound dummy.)

layout(set = 0, binding = 0) uniform sampler2D hdrBuffer;
layout(set = 0, binding = 1) uniform sampler2D bloomBuffer;
// Colour grading strip: a 32^3 cube flattened into 32 tiles of 32x32 laid out
// horizontally (1024 x 32). Bound by NativeRenderer::loadColorLut(); when
// pc.lutEnabled is 0 it is never sampled.
layout(set = 0, binding = 2) uniform sampler2D gradeLut;
// The geometry depth this frame's geometry pass wrote: the GBuffer depth on
// the deferred path, m_depthImage on the forward one. Its layout is turned
// to SHADER_READ_ONLY just before this pass runs (see the DOF barrier in
// NativeRenderer::endFrame) and turned back to the attachment layout right
// after, so the occlusion downsample sees exactly what it saw before. Only
// read when pc.dofEnabled == 1; when DOF is off the binding is still written
// (the set must stay complete) but the shader never samples it.
layout(set = 0, binding = 3) uniform sampler2D depthSampler;
// Rain on the windscreen (stage 1), appended at the END of the display set
// (the add-only rule for bindings): one vec4 in a small dedicated UBO — the
// push block below fills exactly 128 bytes, the guaranteed limit, so it has
// no room left. rainFx.x 0 (or z = 0: not the cockpit view) makes every read
// of it dead code: the effect costs nothing but the uniform fetch.
layout(set = 0, binding = 4) uniform WindscreenFx {
    vec4 rainFx; // x coverage 0..1, y animation clock (s),
                 // z in-cockpit flag (0/1), w aspect
} rainFx;
// Windscreen glass rect (stage 3): which pixels of the cockpit view actually
// carry glass, so the rain only lands on the real windows instead of the
// whole screen band. params.x = 1 when a glass mesh supplied the rect (rect
// is its screen-space AABB in UV space); 0 falls back to the stage-1 band.
layout(set = 0, binding = 5) uniform WindscreenGlass {
    vec4 glassRect; // xy = min uv, zw = max uv
    vec4 glassParams; // x = hasRect (0/1), y = viewW, z = viewH
} windscreenGlass;
// Per-pixel glass mask (stage 4): R8, 1 where a real glass mesh covers the
// pixel. When glassParams.x > 0.5 this gates the rain instead of the rect.
layout(set = 0, binding = 6) uniform sampler2D windscreenMask;
// GBuffer world position / coverage, sampled for its w only: a magnitude at or
// above KS_MATERIAL_COVERAGE_UNLIT_BASE marks a photographic surface, which is
// display-referred and skips the tone curve below.
layout(set = 0, binding = 7) uniform sampler2D surfaceFlags;

// Push-constant layout is std430-like and mirrored byte-for-byte by
// NativeRenderer::TonemapPC — keep the two in sync.
layout(push_constant) uniform ToneMapPC {
    vec3  colorFilter;   // 0
    float exposure;      // 12
    float gamma;         // 16  SDR display gamma (ignored in HDR10)
    float whitePoint;    // 20  Uncharted2 white point
    float saturation;    // 24
    float contrast;      // 28
    int   mode;          // 32  0 none, 1 Reinhard, 2 ACES, 3 Uncharted2, 4 Filmic
    int   hdrOutput;     // 36  1 = PQ / Rec.2020
    float whiteNits;     // 40  reference white for graded 1.0 (must match the
                         //     OS SDR-white for parity — default 80, see
                         //     NativeRenderer::m_hdrWhiteNits / KS_HDR_WHITE_NITS)
    float peakNits;      // 44  HDR highlight ceiling
    // Brief P4 — light sharpen, appended at the END of the block (the
    // add-only rule): one HDR texel step + the unsharp amount. 0 = off;
    // NativeRenderer only raises it when TAA is on (its history blend is
    // what softens the frame), and the four extra taps are skipped then.
    float sharpenTexelX; // 48
    float sharpenTexelY; // 52
    float sharpenAmount; // 56    // [COLOR] HUE / SEPIA of the room's own ppeffects.ini, packed into the
    // bytes the display block never used: the pad at 60 and the four left
    // over before the 128-byte push-constant guarantee. 0 = off for both,
    // and each branch below is skipped then.
    float gradeHue;       // 60  hue rotation, degrees
    // Post-processing preset grading, appended at the END (add-only rule).
    float lutStrength;   // 64  0 = off
    int   lutEnabled;    // 68  1 = sample gradeLut
    // Bloom fold-in weight, appended at the END (add-only rule): the gain the
    // half-res bloom is folded back in with. 1.0 folds what the bright pass
    // produced; the constant the shader used to hardcode was 0.3.
    float bloomStrength; // 72
    // Depth of field (photo mode), appended at the END (add-only rule). The
    // whole block finishes at exactly 128 bytes, the guaranteed push-constant
    // limit, so nothing can grow beyond it.
    float depthR22;      // 76  projection row 2 (see engine/Math/OcclusionTest.h
    float depthR23;      // 80  linearizeDepth(): dist = r23 / (ndc + r22))
    int   dofEnabled;    // 84  1 = the preset's depthOfField block is active
    int   dofSamples;    // 88  tap count (quality-mapped on the CPU, <= 24)
    float dofFocal;      // 92  focal length (metres, dofFocalLength())
    float dofAperture;   // 96  aperture diameter (metres) = focal / fNumber
    float dofGain;       // 100 px per metre of sensor CoC (extent/sensor * apertureParameter)
    float dofFocus;      // 104 focus distance (metres); 0 = focus inside the metering check
    float dofMeterHalfX; // 108 focus metering half-size, centred on the screen (UV)
    float dofMeterHalfY; // 112
    float dofFrontPx;    // 116 blur radius caps (px) in front of / behind focus
    float dofBackPx;     // 120
    float gradeSepia;     // 124 0 = off, 1 = full sepia
} pc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 Reinhard(vec3 x) {
    return x / (1.0 + x);
}

vec3 Uncharted2Partial(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// Exposure is applied once, before the curve — the old version multiplied it
// in twice for this operator (again inside the fit), which made mode 3
// visibly darker than the others for the same setting.
vec3 Uncharted2(vec3 x) {
    vec3 curr = Uncharted2Partial(x);
    vec3 whiteScale = 1.0 / Uncharted2Partial(vec3(pc.whitePoint));
    return curr * whiteScale;
}

vec3 Filmic(vec3 x) {
    vec3 X = max(vec3(0.0), x - 0.004);
    vec3 result = (X * (6.2 * X + 0.5)) / (X * (6.2 * X + 1.7) + 0.06);
    return pow(result, vec3(2.2));
}

// Exact sRGB OETF — must match ks::engine::graphics::linearToSrgb()
// (src/engine/Graphics/ColorSpace.cpp) so CPU and GPU agree.
vec3 linearToSrgb(vec3 c) {
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// Inverse of the OETF above, for a picture that is already encoded.
vec3 srgbToLinear(vec3 c) {
    c = max(c, vec3(0.0));
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

// SMPTE ST.2084 PQ, input in nits (0..10000).
vec3 pqEncode(vec3 nits) {
    const float m1 = 2610.0 / 16384.0;        // 0.1593017578125
    const float m2 = 2523.0 / 4096.0 * 128.0; // 78.84375
    const float c1 = 3424.0 / 4096.0;         // 0.8359375
    const float c2 = 2413.0 / 4096.0 * 32.0;  // 18.8515625
    const float c3 = 2392.0 / 4096.0 * 32.0;  // 18.6875
    vec3 L = max(nits, vec3(0.0)) / 10000.0;
    vec3 Lm = pow(L, vec3(m1));
    vec3 num = c1 + c2 * Lm;
    vec3 den = 1.0 + c3 * Lm;
    return pow(num / den, vec3(m2));
}

// Linear sRGB (D65) -> BT.2020 (BT.2087 primaries matrix).
const mat3 SRGB_TO_BT2020 = mat3(
    0.62740389, 0.06909729, 0.01641414,
    0.32929986, 0.92927073, 0.08801331,
    0.04330665, 0.00162898, 0.89557256);

// One tile of the grading strip: rg picks the texel inside the 32x32 tile,
// slice picks the tile (the graded blue). Sampled at level 0 so the strip's
// mip chain can never blend neighbouring blue slices together.
vec3 lutTile(vec2 rg, float slice) {
    float u = (slice * 32.0 + clamp(rg.r, 0.0, 1.0) * 31.0 + 0.5) / 1024.0;
    float v = (clamp(rg.g, 0.0, 1.0) * 31.0 + 0.5) / 32.0;
    return textureLod(gradeLut, vec2(u, v), 0.0).rgb;
}

// Trilinear read of the 32^3 cube: the two blue slices around c.b, blended.
vec3 applyGradeLut(vec3 c) {
    float b = clamp(c.b, 0.0, 1.0) * 31.0;
    float b0 = floor(b);
    float b1 = min(b0 + 1.0, 31.0);
    vec3 lo = lutTile(c.rg, b0);
    vec3 hi = lutTile(c.rg, b1);
    return mix(lo, hi, b - b0);
}
// ---- Depth of field (the preset's depthOfField block, photo mode) ----
// The thin-lens circle of confusion, in metres on the sensor plane, for a
// point at view distance d:
//
//   coc = aperture * focal * |d - focus| / (d * |focus - focal|)
//
// dofGain converts it to pixels (view height / sensor height, times the
// preset's apertureParameter). The view distance comes from the geometry
// depth exactly like the occlusion grid linearises it — same formula, same
// numbers passed at record time.
float dofViewDist(ivec2 px) {
    float ndc = texelFetch(depthSampler, px, 0).r;
    float den = ndc + pc.depthR22;
    if (den >= -1e-9) return 1e7; // beyond the far plane = sky
    return pc.depthR23 / den;
}

// Focus distance: the metering rectangle is what the autoExposure block
// already describes (meteringWidth/Height in 0..100%), centred like the
// default offsets do. Sky distances do not vote; if the rectangle holds only
// sky the focus falls back to the far plane (a view of nothing close —
// everything near gets defocused, which is what the view physically is).
float dofFocusDistance(ivec2 fragPx, ivec2 extent) {
    ivec2 meterHalf = ivec2(vec2(extent) * vec2(pc.dofMeterHalfX, pc.dofMeterHalfY));
    ivec2 lo = ivec2(vec2(extent) * 0.5) - meterHalf;
    ivec2 hi = ivec2(vec2(extent) * 0.5) + meterHalf;
    float sum = 0.0;
    float count = 0.0;
    const int taps = 12;
    for (int i = 0; i < taps; ++i) {
        // Golden-angle disc inside the metering rectangle (one gaussian of
        // 13 points, the exact focus spot AC's autopip brackets).
        float a = float(i) * 2.399963229728653;
        float r = 0.9 * sqrt((float(i) + 0.5) / float(taps));
        vec2 uv = (vec2(lo + hi) * 0.5 + vec2(cos(a), sin(a)) * r * vec2(meterHalf)) /
                  vec2(extent);
        float d = dofViewDist(ivec2(uv * vec2(extent)));
        if (d < 9e5) {
            sum += d;
            count += 1.0;
        }
    }
    return count > 0.0 ? sum / count : 1e7;
}

vec3 dofBlur(vec3 center, vec2 fragUV, ivec2 px, ivec2 extent, float focus) {
    float d = dofViewDist(px);
    float coc = pc.dofAperture * pc.dofFocal * abs(d - focus) /
                (d * max(abs(focus - pc.dofFocal), 1e-4));
    float radiusPx = min(coc * pc.dofGain,
                         d < focus ? pc.dofFrontPx : pc.dofBackPx);
    if (radiusPx < 0.5)
        return center;
    const int taps = 24;
    vec3 sum = center; // the centre pixel counts as one tap (its own weight)
    float weight = 1.0;
    for (int i = 0; i < taps && i < pc.dofSamples; ++i) {
        float a = float(i) * 2.399963229728653; // golden angle
        float r = radiusPx * sqrt((float(i) + 0.5) / float(taps));
        vec2 off = vec2(cos(a), sin(a)) * r * vec2(pc.sharpenTexelX, pc.sharpenTexelY);
        vec3 c = texture(hdrBuffer, fragUV + off).rgb;
        float w = 1.0;
        // A tap that lives far away must not drag a near pixel towards the
        // sky: weight the harder-looking samples down (the classic bleeding
        // guard of a cheap gather DOF).
        ivec2 tp = clamp(px + ivec2(off * vec2(extent)), ivec2(0), extent - ivec2(1));
        float td = dofViewDist(tp);
        if (td < 9e5 && d < 9e5 && abs(td - d) > max(focus * 0.25, 2.0))
            w = 0.25;
        sum += c * w;
        weight += w;
    }
    return sum / weight;
}

// ---- Rain on the windscreen (stage 1) ----
// Screen-space for stage 1: the depth buffer cannot locate the glass because
// the glass pipeline is depth-tested, never written (NativeRenderer glass
// blend state) — a windscreen pixel carries the depth of the world *behind*
// it, so a near-depth gate would wet the dashboard and leave the glass dry.
// What the display pass knows instead is the cockpit layout: rainFx.z is the
// session's in-cockpit flag, and the drops ride the windscreen's screen band.
// Three hash-built procedural layers, no texture shipping with the effect:
//   1. a fine sliding ladder of small drops,
//   2. a sparse slow layer of large beads,
//   3. a static wetting film the fresh coverage seeds.
// Each layer refracts the HDR it covers (a drop is a lens sitting on the
// glass) and leaves a hair of darkening.
float rainHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec3 rainOnGlass(vec3 color, vec2 uv) {
    if (rainFx.rainFx.x <= 0.001 || rainFx.rainFx.z < 0.5) return color;

    // Windscreen glass rect (stage 3): when a real glass mesh supplied its
    // screen-space AABB, the rain only lands inside it; otherwise fall back
    // to the stage-1 screen band.
    float coverage;
    if (windscreenGlass.glassParams.x > 0.5) {
        // Stage 4: per-pixel mask wins over the rect when it is available.
        float m = texture(windscreenMask, uv).r;
        if (m <= 0.5) return color;
        coverage = rainFx.rainFx.x;
    } else {
        coverage = rainFx.rainFx.x *
            smoothstep(0.10, 0.30, uv.y) * (1.0 - smoothstep(0.62, 0.88, uv.y));
    }
    if (coverage <= 0.002) return color;

    // Aspect-corrected coordinates keep the cells round.
    const vec2 coords = uv * vec2(rainFx.rainFx.w, 1.0);
    // coords-space displacement -> uv displacement: only the x axis was
    // scaled up by the aspect, so only it scales back down.
    const vec2 cellToUv = vec2(1.0 / rainFx.rainFx.w, 1.0);

    // Layer 1: a fine ladder of fast sliding drops.
    vec2 g1 = coords * vec2(24.0, 16.0);
    g1.y += rainFx.rainFx.y * 1.4;
    vec2 c1 = floor(g1);
    vec2 f1 = fract(g1) - 0.5;
    const float hit1 = step(rainHash(c1 + 3.7), coverage * 0.45);
    vec2 o1 = vec2(rainHash(c1 + 17.1), rainHash(c1 + 31.7)) - 0.5;
    const float drop1 = hit1 * smoothstep(0.16, 0.03, length(f1 - o1));

    // Layer 2: slower, sparser, bigger beads.
    vec2 g2 = coords * vec2(12.0, 8.0);
    g2.y += rainFx.rainFx.y * 0.6;
    vec2 c2 = floor(g2);
    vec2 f2 = fract(g2) - 0.5;
    const float hit2 = step(rainHash(c2 + 91.3), coverage * 0.30);
    vec2 o2 = vec2(rainHash(c2 + 113.1), rainHash(c2 + 127.7)) - 0.5;
    const float drop2 = hit2 * smoothstep(0.15, 0.04, length(f2 - o2));

    // The wetting film: static nucleation the fresh coverage seeds.
    vec2 g3 = coords * vec2(90.0, 60.0);
    vec2 c3 = floor(g3);
    vec2 f3 = fract(g3) - 0.5;
    const float film = smoothstep(coverage * 0.5, 0.0, length(f3)) *
                       step(0.55, rainHash(c3 + 211.7));

    const float drop = clamp(drop1 + drop2 + film, 0.0, 1.0);
    if (drop <= 0.002) return color;

    // Windscreen wipers (stage 2, screen-space): a blade that sweeps top to
    // bottom on the frame clock. Pixels under the blade carry no drops; the
    // rest of the glass keeps its film, so the sweep reads as a real wiper
    // pass without a per-mesh blade.
    const float blade = fract(rainFx.rainFx.y * 0.3);
    const float wiperBand = smoothstep(0.06, 0.0, abs(uv.y - blade));
    if (wiperBand > 0.5) return color;

    // Refraction: shift what the covered pixel shows through, plus a hair of
    // darkening at the rim. The shift is a fraction of one cell in uv space.
    vec2 refr = (f1 - o1) * drop1 + (f2 - o2) * drop2 + f3 * film;
    vec3 behind =
        texture(hdrBuffer, uv + refr * cellToUv * 0.035).rgb;
    return mix(color, behind, drop * 0.85) * (1.0 - drop * 0.08);
}

// [COLOR] HUE: a rotation about the luminance axis, the same 601-weighted
// matrix every hue-rotate filter uses. Degrees in, clamped back into gamut.
vec3 rotateHue(vec3 c, float degrees) {
    float a = radians(degrees);
    float cs = cos(a);
    float sn = sin(a);
    mat3 m = mat3(
        0.213 + cs * 0.787 - sn * 0.213, 0.213 - cs * 0.213 + sn * 0.143,
        0.213 - cs * 0.213 - sn * 0.787,
        0.715 - cs * 0.715 - sn * 0.715, 0.715 + cs * 0.285 + sn * 0.140,
        0.715 - cs * 0.715 + sn * 0.715,
        0.072 - cs * 0.072 + sn * 0.928, 0.072 - cs * 0.072 - sn * 0.283,
        0.072 + cs * 0.928 + sn * 0.072);
    return clamp(m * c, 0.0, 1.0);
}

// [COLOR] SEPIA: the classic warm monochrome, mixed in by its own amount.
vec3 sepiaTone(vec3 c) {
    return clamp(vec3(dot(c, vec3(0.393, 0.769, 0.189)),
                      dot(c, vec3(0.349, 0.686, 0.168)),
                      dot(c, vec3(0.272, 0.534, 0.131))), 0.0, 1.0);
}

void main() {
    // The HDR frame and the bloom buffer are both scene-linear and both
    // pre-exposure, so exposure lands on their sum (same order the old
    // shader used).
    vec3 color = texture(hdrBuffer, fragUV).rgb;
    // A photographic surface is display-referred, not scene-referred: it IS a
    // photograph of the place, already exposed and graded by whoever took it.
    // Running it through the frame's exposure, tone curve, grade and bloom is
    // what left the industrial hall at luminance 199 with a chromaticity of
    // 0.04, against the 174 / 0.25 the photograph itself holds. It reaches the
    // backbuffer through the display's transfer function alone — what a
    // compositor does with a background plate — while the car standing in the
    // room is lit, tone-mapped and graded exactly as before.
    if (abs(texture(surfaceFlags, fragUV).w) >= KS_MATERIAL_COVERAGE_UNLIT_BASE) {
        // The albedo samplers are VK_FORMAT_R8G8B8A8_UNORM (TextureRuntime), so
        // an sRGB image arrives here exactly as its file stores it — already
        // display-referred. Encoding it again (linearToSrgb) lifted the black
        // end and halved the colour: 191 / 0.10 against the file's 174 / 0.25.
        vec3 picture = clamp(color, 0.0, 1.0);
        // How much colour the picture carries is a display-referred property of
        // the image, so the colour row applies to the backdrop too — in a
        // showroom the photograph is most of what the frame shows. The same
        // formula the graded branch uses below; at the neutral 1.0 the guard
        // skips it, so a frame without grading stays byte-identical.
        if (pc.saturation != 1.0) {
            const vec3 pictureLum = vec3(dot(picture, vec3(0.2126, 0.7152, 0.0722)));
            picture = clamp(mix(pictureLum, picture, pc.saturation), 0.0, 1.0);
        }
        if (pc.hdrOutput != 0) {
            // PQ wants scene-linear nits, so the encoded picture is decoded and
            // scaled to reference white — the same appearance the SDR path
            // shows, in the same units the graded branch uses.
            vec3 nits = srgbToLinear(picture) * pc.whiteNits;
            outColor = vec4(pqEncode(SRGB_TO_BT2020 * nits), 1.0);
        } else {
            outColor = vec4(picture, 1.0);
        }
        return;
    }
    // Rain on the windscreen: a screen-space droplet layer for the cockpit
    // view, applied on the scene-linear light so DOF, the sharpen and the
    // bloom fold all see the same refracted image as the displayed one. The
    // gate (coverage 0, or not the cockpit view) makes the call a no-op
    // before its first texture fetch.
    color = rainOnGlass(color, fragUV);
    // Depth of field runs on the scene-linear light, before the bloom fold
    // and before the curve: a photographic lens blurs light, not the graded
    // image. dofEnabled 0 leaves every byte of this untouched.
    if (pc.dofEnabled != 0) {
        ivec2 extent = ivec2(vec2(1.0) / vec2(pc.sharpenTexelX, pc.sharpenTexelY));
        ivec2 px = ivec2(fragUV * vec2(extent));
        float focus = pc.dofFocus > 0.0 ? pc.dofFocus
                                        : dofFocusDistance(px, extent);
        color = dofBlur(color, fragUV, px, extent, focus);
    }

    // Brief P4 — light sharpen: unsharp mask on the scene HDR, applied
    // BEFORE the bloom glow is folded in so the glow itself stays soft
    // ("small radius, no foggy glow"). Amount 0 (non-TAA frames) skips
    // the four taps entirely — byte-identical to the pre-P4 image.
    if (pc.sharpenAmount > 0.0) {
        vec2 t = vec2(pc.sharpenTexelX, pc.sharpenTexelY);
        vec3 blur = (texture(hdrBuffer, fragUV + vec2( t.x, 0.0)).rgb +
                     texture(hdrBuffer, fragUV + vec2(-t.x, 0.0)).rgb +
                     texture(hdrBuffer, fragUV + vec2(0.0,  t.y)).rgb +
                     texture(hdrBuffer, fragUV + vec2(0.0, -t.y)).rgb) * 0.25;
        color += (color - blur) * pc.sharpenAmount;
    }
    color += texture(bloomBuffer, fragUV).rgb * pc.bloomStrength;
    // Lens flare (bloomAndFlare): ghost images along the centre-to-pixel vector,
    // sampled from the bloom buffer. Only when bloom is on (bloomStrength > 0).
    if (pc.bloomStrength > 0.0) {
        vec2 center = vec2(0.5);
        vec2 v = fragUV - center;
        vec3 flare = vec3(0.0);
        for (int i = 1; i <= 4; i++) {
            float t = float(i) * 0.25;
            vec2 sampleUV = center - v * t;
            vec3 bloom = texture(bloomBuffer, sampleUV).rgb;
            float weight = 1.0 - float(i) * 0.2;
            flare += bloom * weight * vec3(0.9, 0.7, 1.0);
        }
        // Anamorphic streak: horizontal samples from bright pixels.
        vec3 streak = vec3(0.0);
        for (int i = 1; i <= 6; i++) {
            float off = float(i) * pc.sharpenTexelX * 2.0;
            vec3 b1 = texture(bloomBuffer, fragUV + vec2(off, 0.0)).rgb;
            vec3 b2 = texture(bloomBuffer, fragUV - vec2(off, 0.0)).rgb;
            float w = 1.0 - float(i) / 7.0;
            streak += (b1 + b2) * w;
        }
        streak /= 6.0;
        color += (flare + streak * vec3(1.0, 0.8, 0.6)) * pc.bloomStrength * 0.15;
    }
    // The room's [COLOR] BRIGHTNESS and its white-balance gain are *light*
    // gains: they sit on the scene-linear value next to exposure, which is where
    // a two-temperature white balance is defined and where BRIGHTNESS means
    // "more light" rather than "a brighter picture" (pushing the display value
    // after the curve instead crushes the top against 1.0).
    color *= pc.colorFilter;
    vec3 exposed = color * pc.exposure;

    vec3 y;
    switch (pc.mode) {
        case 1:  y = Reinhard(exposed); break;
        case 2:  y = ACESFilm(exposed); break;
        case 3:  y = Uncharted2(exposed); break;
        case 4:  y = Filmic(exposed); break;
        default: y = exposed; break;
    }

    // Gamma is part of the SDR display transform only: in HDR10 the PQ curve
    // is the transfer function, and pre-gammaing would double-encode.
    if (pc.hdrOutput == 0)
        y = pow(max(y, vec3(0.0)), vec3(1.0 / max(pc.gamma, 1e-3)));

    // Grade the *display-referred* value. CONTRAST and SATURATION in a room's
    // ppeffects.ini (and in a ppfilter's colorGrading block) are authored against
    // the displayed image, where mid-grey is 0.5; the linear value of mid-grey is
    // about 0.21, so pivoting at 0.5 before the transfer function lifted every
    // shadow and clipped every highlight — a room asking for CONTRAST=1.1 came
    // out hard and washed out instead of gently deeper. SDR encodes first and
    // outputs what it graded; HDR grades the SDR-relative signal it hands to PQ.
    // Grading strip: unchanged in place and domain. A shipped look must not move
    // silently, so the LUT keeps seeing exactly the post-curve value it always
    // has, and a frame without a [COLOR] block stays byte-identical.
    if (pc.lutEnabled != 0 && pc.lutStrength > 0.0) {
        y = mix(y, applyGradeLut(clamp(y, 0.0, 1.0)), clamp(pc.lutStrength, 0.0, 1.0));
    }

    vec3 graded = y;
    if (pc.hdrOutput == 0) graded = linearToSrgb(clamp(y, 0.0, 1.0));

    float lum = dot(graded, vec3(0.2126, 0.7152, 0.0722));
    graded = mix(vec3(lum), graded, pc.saturation);
    graded = (graded - 0.5) * pc.contrast + 0.5;
    // The room's own [COLOR] block (see the push constant). Both terms are 0
    // when the room declares no grading, so this costs nothing then.
    if (pc.gradeHue != 0.0) graded = rotateHue(graded, pc.gradeHue);
    if (pc.gradeSepia > 0.0) graded = mix(graded, sepiaTone(graded), clamp(pc.gradeSepia, 0.0, 1.0));

    if (pc.hdrOutput != 0) {
        // HDR10. The graded signal up to paper white is scaled to reference
        // white, which keeps every mid-tone at exactly its SDR-relative
        // level; the pre-curve headroom above 1.0 (highlights the SDR grade
        // would clip) expands compressively towards peak luminance instead.
        vec3 paper = clamp(graded, 0.0, 1.0);
        vec3 nits = paper * pc.whiteNits;
        vec3 over = max(exposed - 1.0, 0.0);
        nits += max(pc.peakNits - pc.whiteNits, 0.0) * over / (over + 1.0);

        vec3 rec2020 = SRGB_TO_BT2020 * nits;
        outColor = vec4(pqEncode(rec2020), 1.0);
    } else {
        // graded is already display-encoded (see linearToSrgb above): encoding
        // it a second time here would lift the whole picture.
        outColor = vec4(clamp(graded, 0.0, 1.0), 1.0);
    }
}
