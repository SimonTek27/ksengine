#version 450

#extension GL_GOOGLE_include_directive : require
#include "../../engine/Graphics/MaterialResponseConstants.h"
#include "../LightConstants.h"

// Shadowed surfaces keep this much of the sun's energy, as light from the sky
// rather than as grey (coloured shadows). Same number the old grey floor used.
const float KS_SHADOW_FLOOR = 0.35;

// Deferred lighting + volumetric atmosphere.
//
// Reads the GBuffer, re-runs the same directional-light + cascaded-shadow
// model native_forward.frag used (so the base image is recognisably the same
// scene), then layers two things the forward path never had:
//
//   1. analytic exponential height fog integrated along the view ray, and
//   2. a shadow-map raymarch (FOG_STEPS taps) modulated by a Henyey-
//      Greenstein phase function — i.e. real light shafts / volumetric
//      shadows, not a flat fog colour.
//
// Coverage test: geometry writes w = 1.0 into RT2 (-1 on brake-disc
// pixels, brief P6 — abs() below decodes both), cleared pixels keep 0,
// so background pixels take the early-out and draw the sky along the ray
// they were shot on (skyColor below).

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform FrameData {
    vec4 sunDirection;      // xyz, w unused
    vec4 sunColor;          // rgb, a = intensity
    mat4 cascadeViewProj[3];
    vec4 cascadeSplits;     // view-space far distance of each cascade
    vec4 cameraPos;         // xyz, w unused
    mat4 viewProj;          // unjittered, current frame
    mat4 prevViewProj;      // unjittered, previous frame
    vec4 taaParams;         // x = history feedback (0 disables TAA)
    vec4 fogColor;          // rgb = atmosphere tint, a unused
    vec4 fogParams;         // x = density, y = height falloff, z = start distance, w = max opacity
    vec4 aoParams;          // x = radius, y = bias, z = strength, w = 1 when enabled
    vec4 ssrParams;         // x = max distance, y = intensity, z = roughness cutoff, w = 1 when enabled
    vec4 motionBlurParams;  // declared to reach iblParams below (std140 offsets must be walked member by member)
    vec4 iblParams;         // x = 1 when the split-sum IBL branch is active, y = prefilter max LOD
    vec4 brakeGlow;         // brief P6: x = normalized hottest-disc temperature [0..1]
    vec4 ssgiParams;        // x = screen radius (uv), y = intensity, w = 1 when enabled
    vec4 lightParams;       // x/y = tile grid, z = light count, w = first record float
    vec4 wetParams;         // x = track wetness [0..1] (weatherShaders, appended)
    mat4 invViewProj;       // inverse of viewProj: uv -> world ray (sky background, appended)
} frame;

// Wet ground, the same numbers as simulator/WetLook.h (WetLookConfig) and as
// native_forward.frag; wet_look_test asserts the three agree.
const float KS_WET_ALBEDO_SCALE = 0.62;
const float KS_WET_ROUGH_FLOOR = 0.06;
const float KS_WET_ROUGH_SCALE = 0.30;
const float KS_WET_UP_MIN = -0.15;
const float KS_WET_UP_MAX = 0.35;
const float KS_WET_WATER_F0 = 0.02;

layout(set = 0, binding = 1) uniform sampler2DArray shadowCascades;
layout(set = 0, binding = 2) uniform sampler2D gbufAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gbufNormal;
layout(set = 0, binding = 4) uniform sampler2D gbufWorldPos;
layout(set = 0, binding = 5) uniform sampler2D iblIrradiance;
layout(set = 0, binding = 6) uniform sampler2D iblPrefiltered;
layout(set = 0, binding = 7) uniform sampler2D iblBrdfLut;
layout(set = 0, binding = 8) uniform sampler2D ssgiColor;
// Native local lights: one flat float array the CPU builds every frame
// (simulator/LocalLights.h) — per-tile (offset,count), then the light indices
// the tiles point at, then the light records. Floats hold the integer fields
// exactly, which is what lets one buffer carry the whole table.
layout(set = 0, binding = 9, std430) readonly buffer LocalLightTable {
    float values[];
} localLights;
// Local light shadows: one layer per shadowed face (a spot's cone, or one cube
// face of a point light), rendered by simulator/LocalShadowAtlas.h with the
// same ksShadow pipeline the sun's cascades use. Cleared to 1 and clamped to
// opaque white, so a layer with no caster reads as fully lit.
layout(set = 0, binding = 10) uniform sampler2DArray localShadows;

const float PI = 3.14159265;

vec2 equirectUv(vec3 d) {
    return vec2(atan(d.z, d.x) * 0.15915494 + 0.5,
                acos(clamp(d.y, -1.0, 1.0)) * 0.31830989);
}

const float FOG_ANISOTROPY = 0.55;
const float VOL_STRENGTH = 4.0;
const int FOG_STEPS = 12;

int cascadeFor(float viewDist) {
    // cascadeSplits.w is how many cascades the stack really has (1..3);
    // sampling a layer nobody rendered would read stale shadow depth.
    int count = int(frame.cascadeSplits.w + 0.5);
    if (count < 1) count = 3;
    int index = 2;
    if (viewDist < frame.cascadeSplits.x) index = 0;
    else if (viewDist < frame.cascadeSplits.y) index = 1;
    return min(index, count - 1);
}

float sampleShadow(vec3 worldPos, int cascadeIndex) {
    vec4 lightClip = frame.cascadeViewProj[cascadeIndex] * vec4(worldPos, 1.0);
    vec3 ndc = lightClip.xyz / lightClip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    float bias = 0.0015;
    vec2 texel = 1.0 / vec2(textureSize(shadowCascades, 0).xy);
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(float(x), float(y)) * texel;
            float storedDepth =
                texture(shadowCascades, vec3(uv + offset, float(cascadeIndex))).r;
            lit += (ndc.z - bias > storedDepth) ? 0.0 : 1.0;
        }
    }
    lit /= 9.0;
    // The unlit floor used to live here, as a neutral grey 0.35. It moved to
    // the call site, which turns it into a colour: a shadowed surface keeps
    // the sky's light rather than a grey fraction of the sun's (coloured
    // shadows). This function stays a plain visibility in [0, 1].
    return lit;
}

float heightFogAmount(vec3 camPos, vec3 worldPos) {
    vec3 d = worldPos - camPos;
    float dist = max(length(d) - frame.fogParams.z, 0.0);
    float dy = d.y;
    float k = frame.fogParams.x * exp(-camPos.y * frame.fogParams.y);
    if (abs(dy) < 1e-3) return k * dist;
    return k * dist * (1.0 - exp(-frame.fogParams.y * dy)) / (frame.fogParams.y * dy);
}

vec3 volumetricInscatter(vec3 camPos, vec3 worldPos, vec3 sunRad) {
    vec3 delta = worldPos - camPos;
    float dist = length(delta);
    if (dist < 1e-3) return vec3(0.0);
    vec3 rayDir = delta / dist;
    vec3 L = normalize(-frame.sunDirection.xyz);
    float cosT = dot(rayDir, L);
    float g = FOG_ANISOTROPY;
    float denom = 1.0 + g * g - 2.0 * g * cosT;
    float phase = (1.0 - g * g) / (4.0 * PI * pow(max(denom, 1e-4), 1.5));
    float stepLen = dist / float(FOG_STEPS);
    float lit = 0.0;
    for (int i = 0; i < FOG_STEPS; ++i) {
        float t = stepLen * (float(i) + 0.5);
        vec3 p = camPos + rayDir * t;
        lit += sampleShadow(p, cascadeFor(t));
    }
    lit /= float(FOG_STEPS);
    return sunRad * phase * lit * VOL_STRENGTH;
}

const int AO_SAMPLES = 16;
const float AO_GOLDEN_ANGLE = 2.39996323;

float aoHash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float ssao(vec3 worldPos, vec3 N) {
    if (frame.aoParams.w < 0.5) return 1.0;
    float radius = frame.aoParams.x;
    float bias = frame.aoParams.y;
    float strength = frame.aoParams.z;
    vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    float rot = aoHash(gl_FragCoord.xy) * 6.2831853;
    vec3 camPos = frame.cameraPos.xyz;
    float occluded = 0.0;
    for (int i = 0; i < AO_SAMPLES; ++i) {
        float t = (float(i) + 0.5) / float(AO_SAMPLES);
        float phi = float(i) * AO_GOLDEN_ANGLE + rot;
        float cosT = 1.0 - t;
        float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));
        vec3 dir = tangent * (cos(phi) * sinT) + bitangent * (sin(phi) * sinT) + N * cosT;
        vec3 sp = worldPos + dir * radius;
        vec4 clip = frame.viewProj * vec4(sp, 1.0);
        if (clip.w <= 0.0) continue;
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
        vec4 scene = texture(gbufWorldPos, uv);
        if (abs(scene.w) < 0.5) continue;
        float sceneDist = length(scene.xyz - camPos);
        float sampleDist = length(sp - camPos);
        if (sceneDist + bias < sampleDist && (sampleDist - sceneDist) < radius) {
            occluded += 1.0;
        }
    }
    float ao = 1.0 - occluded / float(AO_SAMPLES);
    return mix(1.0, ao, strength);
}

// ---- native local lights ------------------------------------------------
// Windowed inverse square, shared with lightFalloff() on the CPU: exactly zero
// at the range, so the tile binning and the shading agree on where a light
// stops instead of a lamp showing a hard rim where its tile ends.
float localLightFalloff(float distSq, float range) {
    float d2 = max(distSq, 1e-4);
    float w = clamp(1.0 - d2 / (range * range), 0.0, 1.0);
    return (w * w) / d2;
}

// Adds every local light this pixel's tile holds. The tile is the CPU's, so the
// loop length is the lights that can reach this part of the screen rather than
// the lights in the scene; inside it the falloff, the cone and the same GGX the
// sun uses decide what actually lands, and the lamp's own shadow layer removes
// what a caster blocks.
vec3 localLightsShading(vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metalness,
                        float roughness, float f0Scale) {
    if (frame.lightParams.z < 0.5) return vec3(0.0);
    ivec2 tileCount = ivec2(frame.lightParams.xy + vec2(0.5));
    if (tileCount.x < 1 || tileCount.y < 1) return vec3(0.0);
    ivec2 tile = clamp(ivec2(gl_FragCoord.xy / KS_LIGHT_TILE_PIXELS), ivec2(0),
                       tileCount - ivec2(1));
    int tileIndex = tile.y * tileCount.x + tile.x;
    // The table's regions are derived from what the shader already has: the
    // records come first (light index * stride), then the per-tile cells, then
    // the index list. No second base has to be fetched for a lamp.
    int lightStride = int(KS_LIGHT_FLOATS_PER_RECORD);
    int tileBase = int(frame.lightParams.z + 0.5) * lightStride;
    int indexBase = tileBase + tileCount.x * tileCount.y * 2;
    int offset = int(localLights.values[tileBase + tileIndex * 2 + 0] + 0.5);
    int count = int(localLights.values[tileBase + tileIndex * 2 + 1] + 0.5);

    float NdotV = max(dot(N, V), 1e-4);
    float a = roughness * roughness;
    float k = a * 0.5;
    vec3 F0 = mix(vec3(0.04) * f0Scale, albedo, metalness);
    vec3 sum = vec3(0.0);

    for (int i = 0; i < count; ++i) {
        int light = int(localLights.values[indexBase + offset + i] + 0.5);
        int base = light * lightStride;
        vec3 lightPos = vec3(localLights.values[base + 0], localLights.values[base + 1],
                             localLights.values[base + 2]);
        float range = localLights.values[base + 3];
        vec3 color = vec3(localLights.values[base + 4], localLights.values[base + 5],
                          localLights.values[base + 6]);
        float intensity = localLights.values[base + 7];
        vec3 toLight = lightPos - worldPos;
        float distSq = dot(toLight, toLight);
        if (distSq > range * range) continue;
        vec3 L = toLight * inversesqrt(max(distSq, 1e-6));
        float atten = localLightFalloff(distSq, range);
        if (localLights.values[base + 13] > 0.5) {
            // Spot: the lamp points along direction, so the cone is measured
            // against the direction the light travels to reach this pixel.
            vec3 aim = vec3(localLights.values[base + 8], localLights.values[base + 9],
                            localLights.values[base + 10]);
            float cosOuter = localLights.values[base + 11];
            float cosInner = localLights.values[base + 12];
            atten *= smoothstep(cosOuter, max(cosInner, cosOuter + 1e-3), dot(-L, aim));
        }
        if (atten <= 0.0) continue;
        // Shadow, in the same units as the sun's cascades: clip this fragment
        // through the lamp's own matrix and compare the depth it lands at with
        // the nearest caster. A spot owns one perspective layer; a point light
        // owns six cube faces and the face follows the dominant axis of the
        // vector from the lamp — the same rule cubeFaceForDirection() applies
        // on the CPU, which is what lets one sampler2DArray serve a cubemap.
        int firstLayer = int(localLights.values[base + 14] + 0.5);
        int faces = int(localLights.values[base + 15] + 0.5);
        if (faces > 0 && firstLayer >= 0) {
            float shadowNear = localLights.values[base + 16];
            float shadowFar = localLights.values[base + 17];
            int face = 0;
            if (faces > 1) {
                vec3 rel = worldPos - lightPos;
                vec3 mag = abs(rel);
                if (mag.x >= mag.y && mag.x >= mag.z) face = rel.x > 0.0 ? 0 : 1;
                else if (mag.y >= mag.z) face = rel.y > 0.0 ? 2 : 3;
                else face = rel.z > 0.0 ? 4 : 5;
            }
            int mb = base + 24 + face * 16;
            mat4 lightViewProj = mat4(
                vec4(localLights.values[mb + 0], localLights.values[mb + 1],
                     localLights.values[mb + 2], localLights.values[mb + 3]),
                vec4(localLights.values[mb + 4], localLights.values[mb + 5],
                     localLights.values[mb + 6], localLights.values[mb + 7]),
                vec4(localLights.values[mb + 8], localLights.values[mb + 9],
                     localLights.values[mb + 10], localLights.values[mb + 11]),
                vec4(localLights.values[mb + 12], localLights.values[mb + 13],
                     localLights.values[mb + 14], localLights.values[mb + 15]));
            vec4 lightClip = lightViewProj * vec4(worldPos, 1.0);
            if (lightClip.w > 0.0) {
                vec3 lightNdc = lightClip.xyz / lightClip.w;
                vec2 luv = lightNdc.xy * 0.5 + 0.5;
                if (luv.x >= 0.0 && luv.x <= 1.0 && luv.y >= 0.0 && luv.y <= 1.0) {
                    // Compare in metres, not in clip depth. A lamp's near plane
                    // sits far closer to it than its range, so the perspective
                    // depth packs a whole metre at the far end into ~1e-4 of
                    // NDC: any constant NDC bias then declares every caster
                    // closer than the surface and the shadow vanishes.
                    float ndcRange = shadowFar - shadowNear;
                    float nearFar = shadowNear * shadowFar;
                    float surfaceZ = nearFar / max(shadowFar - lightNdc.z * ndcRange, 1e-4);
                    float layer = float(firstLayer + face);
                    vec2 texel = 1.0 / vec2(textureSize(localShadows, 0).xy);
                    float litTaps = 0.0;
                    for (int sy = -1; sy <= 1; ++sy) {
                        for (int sx = -1; sx <= 1; ++sx) {
                            float stored = texture(
                                localShadows,
                                vec3(luv + vec2(float(sx), float(sy)) * texel, layer)).r;
                            // 1.0 is the clear value: no caster, so "at the far
                            // plane" and never closer than the surface.
                            float storedZ = nearFar / max(shadowFar - stored * ndcRange, 1e-4);
                            litTaps +=
                                (surfaceZ - storedZ > KS_LIGHT_SHADOW_WORLD_BIAS) ? 0.0 : 1.0;
                        }
                    }
                    // atten feeds both the diffuse and the specular term the
                    // loop adds below, so one multiply shadows the whole lamp.
                    atten *= litTaps / 9.0;
                    if (atten <= 0.0) continue;
                }
            }
        }
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        vec3 radiance = color * intensity * atten;
        vec3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        float dd = NdotH * NdotH * (a * a - 1.0) + 1.0;
        float D = (a * a) / (PI * dd * dd);
        float G = (NdotL / (NdotL * (1.0 - k) + k)) *
                  (NdotV / (NdotV * (1.0 - k) + k));
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
        vec3 specular = ((D * G * F) / (4.0 * NdotL * NdotV + 1e-4)) * NdotL * radiance;
        vec3 diffuse = albedo * (1.0 - metalness) * radiance * NdotL;
        sum += diffuse + specular;
    }
    return sum;
}

const int SSR_STEPS = 16;

vec3 ssr(vec3 worldPos, vec3 N, vec3 V, vec3 L, float roughness) {
    if (frame.ssrParams.w < 0.5) return vec3(0.0);
    if (roughness > frame.ssrParams.z) return vec3(0.0);
    vec3 R = reflect(-V, N);
    float maxDist = frame.ssrParams.x;
    float stepLen = maxDist / float(SSR_STEPS);
    float thickness = stepLen * 0.75;
    vec2 hitUV = vec2(0.0);
    bool hit = false;
    for (int i = 1; i <= SSR_STEPS; ++i) {
        vec3 p = worldPos + R * (stepLen * float(i));
        vec4 clip = frame.viewProj * vec4(p, 1.0);
        if (clip.w <= 0.0) break;
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        vec4 scene = texture(gbufWorldPos, uv);
        if (abs(scene.w) < 0.5) continue;
        if (distance(scene.xyz, p) < thickness) { hitUV = uv; hit = true; break; }
    }
    if (!hit) return vec3(0.0);
    vec3 hitAlbedo = texture(gbufAlbedo, hitUV).rgb;
    vec3 hitN = normalize(texture(gbufNormal, hitUV).xyz);
    float hitNdotL = max(dot(hitN, L), 0.0);
    vec3 hitSun = frame.sunColor.rgb * frame.sunColor.a;
    vec3 hitColor = hitAlbedo * (hitSun * hitNdotL + hitSun * 0.05);
    return hitColor * frame.ssrParams.y;
}

// ---- Physical sky (background) -----------------------------------------
// The sky is not a texture and not a two-band gradient: it is the light the
// sun actually leaves in the air between the camera and space. Single
// scattering through a Rayleigh + Mie atmosphere, integrated along the view
// ray (viewSteps x sunSteps samples, each with its own transmittance to the
// sun), which is what gives a blue zenith, a pale and hazy horizon, the warm
// glow around the sun and the sun's own disc, all from the sun direction the
// frame is lit with. Sea-level parameters:
//   Rayleigh coefficients (5.5, 13.0, 22.4)e-6 /m, scale height 8 km;
//   Mie coefficient 21e-6 /m, scale height 1.2 km, asymmetry 0.758;
//   planet 6371 km, atmosphere 6471 km.
// Turbidity scales the Mie term (2.0 = a clean alpine sky, 4+ = milky haze).
// KS_SKY_EXPOSURE is the display calibration from radiance to the linear
// frame the tonemap expects: it only sets how bright the day reads, not the
// shape or the colours of the sky.
const float KS_SKY_PLANET_RADIUS = 6371000.0;
const float KS_SKY_ATMOSPHERE_RADIUS = 6471000.0;
const float KS_SKY_RAYLEIGH_HEIGHT = 8000.0;
const float KS_SKY_MIE_HEIGHT = 1200.0;
const float KS_SKY_MIE_G = 0.758;
const vec3 KS_SKY_RAYLEIGH = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float KS_SKY_MIE = 21.0e-6;
const float KS_SKY_TURBIDITY = 2.2;
const float KS_SKY_EXPOSURE = 48.0;
const float KS_SKY_SUN_DISC = 45.0;   // the disc itself, above the scattered halo
const int KS_SKY_VIEW_STEPS = 10;
const int KS_SKY_SUN_STEPS = 5;

// Interval [x, y] of the ray ro + t * rd inside the sphere; x > y on a miss.
vec2 ksSkyRaySphere(vec3 ro, vec3 rd, float radius) {
    const float b = dot(ro, rd);
    const float c = dot(ro, ro) - radius * radius;
    const float d = b * b - c;
    if (d < 0.0) return vec2(1.0, -1.0);
    const float s = sqrt(d);
    return vec2(-b - s, -b + s);
}

// Scattered radiance along dir (unit, +y up) for the sun at sunDir (unit,
// pointing towards the sun), without the sun's own colour or disc.
vec3 ksSkyScattered(vec3 dir, vec3 sunDir) {
    const vec3 ro = vec3(0.0, KS_SKY_PLANET_RADIUS + 2.0, 0.0); // eye, 2 m up
    const vec2 air = ksSkyRaySphere(ro, dir, KS_SKY_ATMOSPHERE_RADIUS);
    if (air.x > air.y) return vec3(0.0);
    float tMax = air.y;
    // Looking down: stop where the ray meets the ground, so the haze under
    // the horizon is not integrated through the planet.
    const float ground = ksSkyRaySphere(ro, dir, KS_SKY_PLANET_RADIUS).x;
    if (ground > 0.0) tMax = min(tMax, ground);
    const float stepLen = tMax / float(KS_SKY_VIEW_STEPS);
    const vec3 betaMie = KS_SKY_MIE * KS_SKY_TURBIDITY * vec3(1.0);
    const float mu = dot(dir, sunDir);
    const float gg = KS_SKY_MIE_G * KS_SKY_MIE_G;
    // Rayleigh and Henyey-Greenstein (Mie) phase functions.
    const float phaseRayleigh = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    const float phaseMie = 3.0 / (8.0 * PI) * ((1.0 - gg) * (mu * mu + 1.0)) /
                           (pow(max(1.0 + gg - 2.0 * mu * KS_SKY_MIE_G, 1e-6), 1.5) *
                            (2.0 + gg));
    vec3 sumRayleigh = vec3(0.0);
    vec3 sumMie = vec3(0.0);
    float depthRayleigh = 0.0; // optical depth from the eye to this sample
    float depthMie = 0.0;
    for (int i = 0; i < KS_SKY_VIEW_STEPS; ++i) {
        const vec3 p = ro + dir * ((float(i) + 0.5) * stepLen);
        const float height = length(p) - KS_SKY_PLANET_RADIUS;
        const float dRayleigh = exp(-height / KS_SKY_RAYLEIGH_HEIGHT) * stepLen;
        const float dMie = exp(-height / KS_SKY_MIE_HEIGHT) * stepLen;
        depthRayleigh += dRayleigh;
        depthMie += dMie;
        // ... plus the optical depth from this sample to the sun.
        const float sunLen = ksSkyRaySphere(p, sunDir, KS_SKY_ATMOSPHERE_RADIUS).y /
                             float(KS_SKY_SUN_STEPS);
        float sunRayleigh = 0.0;
        float sunMie = 0.0;
        for (int j = 0; j < KS_SKY_SUN_STEPS; ++j) {
            const vec3 q = p + sunDir * ((float(j) + 0.5) * sunLen);
            const float hq = length(q) - KS_SKY_PLANET_RADIUS;
            sunRayleigh += exp(-hq / KS_SKY_RAYLEIGH_HEIGHT) * sunLen;
            sunMie += exp(-hq / KS_SKY_MIE_HEIGHT) * sunLen;
        }
        // What survives the trip in and the trip to the sun.
        const vec3 attn = exp(-(betaMie * (depthMie + sunMie) +
                                KS_SKY_RAYLEIGH * (depthRayleigh + sunRayleigh)));
        sumRayleigh += dRayleigh * attn;
        sumMie += dMie * attn;
    }
    return phaseRayleigh * KS_SKY_RAYLEIGH * sumRayleigh + phaseMie * betaMie * sumMie;
}

// The sky a background pixel sees, from the ray its uv was shot along. In the
// last few degrees above the horizon it is blended into the haze the fog fades
// distant geometry into (frame.fogColor), so background and fog meet without a
// seam whatever the weather or the sun angle; above that band the physical
// model is shown as it is.
vec3 skyColor(vec2 uv) {
    // z = 1 is the far plane, so the point is on the sky in every direction.
    vec4 far = frame.invViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(far.xyz / far.w - frame.cameraPos.xyz);
    const float horizonMix = smoothstep(0.0, 0.055, dir.y);
    if (horizonMix <= 0.0) return frame.fogColor.rgb;
    // -sunDirection points at the sun (the uniform carries the direction the
    // sunlight travels in). The sky takes the sun's *colour* but not its
    // intensity knob: sunColor.a is an artistic multiplier (a showroom declares
    // 0 and lights its room with the IBL and its lamps), while the atmosphere
    // is not a lamp — scaling scattering by it turned the environment black.
    const vec3 sunToward = normalize(-frame.sunDirection.xyz);
    const vec3 sunRadiance = frame.sunColor.rgb;
    vec3 sky = ksSkyScattered(dir, sunToward) * sunRadiance * KS_SKY_EXPOSURE;
    // The disc: 0.53 deg of arc, softened over its limb so it never aliases.
    const float sunAngle = acos(clamp(dot(dir, sunToward), -1.0, 1.0));
    sky += (1.0 - smoothstep(0.0085, 0.0093, sunAngle)) * sunRadiance *
           KS_SKY_SUN_DISC * KS_SKY_EXPOSURE;
    return mix(frame.fogColor.rgb, sky, horizonMix);
}

void main() {
    vec4 coverage = texture(gbufWorldPos, vUV);
    float brakeDisc = coverage.w < 0.0 ? 1.0 : 0.0;
    if (abs(coverage.w) < 0.5) {
        outColor = vec4(skyColor(vUV), 1.0);
        return;
    }
    vec3 worldPos = coverage.xyz;
    vec4 albedoAO = texture(gbufAlbedo, vUV);
    vec4 normalRough = texture(gbufNormal, vUV);
    vec3 albedo = albedoAO.rgb;
    // A photographic backdrop (a showroom hall that is one image of a place) is
    // not a surface: the picture already holds the light of the room it shows,
    // so it is displayed as it is. Shading it again with the sun and the sky
    // put a second exposure on a finished photograph, and that is what left
    // the industrial hall at luminance 214 with its colour washed down to a
    // chromaticity of 0.04 while AC draws the same room at 145 / 0.25.
    // gbuffer.frag marks such a surface by adding KS_MATERIAL_COVERAGE_UNLIT_BASE
    // to the coverage magnitude, which no authored material can reach.
    if (abs(coverage.w) >= KS_MATERIAL_COVERAGE_UNLIT_BASE) {
        vec3 photoCamPos = frame.cameraPos.xyz;
        vec3 photoSunRad = frame.sunColor.rgb * frame.sunColor.a;
        // Fog and its inscatter still apply, so the distance reads the same as
        // it does on every other surface of the room.
        float photoFog = clamp(1.0 - exp(-heightFogAmount(photoCamPos, worldPos)), 0.0,
                               frame.fogParams.w);
        vec3 photoInscatter = volumetricInscatter(photoCamPos, worldPos, photoSunRad);
        outColor = vec4(mix(albedo, frame.fogColor.rgb + photoInscatter, photoFog), 1.0);
        return;
    }
    float metalness = clamp(albedoAO.a, 0.0, 1.0);
    vec3 N = normalize(normalRough.xyz);
    float coated = normalRough.w < 0.0 ? 1.0 : 0.0;
    float roughness = clamp(abs(normalRough.w), 0.05, 1.0);
    // --- wet ground (videoSources.weatherShaders): the rule the forward path
    // and simulator/WetLook.h carry, applied to what the GBuffer holds. It has
    // no glass and no unlit flag (a photographic backdrop returned above), so
    // metal is the only material response left to keep out of it.
    float wet = clamp(frame.wetParams.x, 0.0, 1.0);
    float wetActive = metalness < 0.5 ? 1.0 : 0.0;
    float wetUp = clamp((N.y - KS_WET_UP_MIN) / (KS_WET_UP_MAX - KS_WET_UP_MIN), 0.0, 1.0);
    float wetAmount = wet * wetActive;
    albedo *= mix(1.0, KS_WET_ALBEDO_SCALE, wetAmount);
    roughness = mix(roughness, max(KS_WET_ROUGH_FLOOR, roughness * KS_WET_ROUGH_SCALE),
                    wetAmount);
    // gbuffer.frag packs the authored ksSpecular, fresnelMaxLevel and
    // ksAmbient into the coverage magnitude as three integer levels (see the
    // constants header).
    // The stored magnitude is a float, so every split first rounds the
    // storage noise away: the packed value is always an integer, and the
    // ambient bucket is floor((packed + 0.5) / 289) — adding 0.5 to the
    // PACKED value, never to the quotient, because a remainder can reach
    // 288/289 and a round of the quotient would steal it for the next
    // bucket.
    float coverageLevels =
        (abs(coverage.w) - 1.0) / KS_MATERIAL_COVERAGE_STEP;
    float ambientLevel =
        floor((coverageLevels + 0.5) / KS_MATERIAL_COVERAGE_AMBIENT_BASE);
    float remainderLevels =
        clamp(coverageLevels - ambientLevel * KS_MATERIAL_COVERAGE_AMBIENT_BASE,
              0.0, 288.49);
    float remainderInt = floor(remainderLevels + 0.5);
    float authoredAmbient =
        ambientLevel / KS_MATERIAL_COVERAGE_LEVEL_MAX;
    float fresnelLevel = mod(remainderInt, KS_MATERIAL_COVERAGE_FRESNEL_BASE);
    float authoredFresnelMax =
        clamp(fresnelLevel / KS_MATERIAL_COVERAGE_LEVEL_MAX, 0.0, 1.0);
    float authoredSpecular =
        clamp(floor((remainderInt - fresnelLevel) /
                         KS_MATERIAL_COVERAGE_FRESNEL_BASE +
                     0.5) /
                   KS_MATERIAL_COVERAGE_LEVEL_MAX,
               0.0, 1.0);
    float f0Scale = clamp(authoredSpecular * KS_MATERIAL_SPECULAR_F0_SCALE,
                          KS_MATERIAL_SPECULAR_F0_MIN,
                          KS_MATERIAL_SPECULAR_F0_MAX);
    vec3 camPos = frame.cameraPos.xyz;
    float viewDist = length(worldPos - camPos);
    vec3 V = normalize(camPos - worldPos);
    vec3 L = normalize(-frame.sunDirection.xyz);
    vec3 sunRad = frame.sunColor.rgb * frame.sunColor.a;
    float NdotL = max(dot(N, L), 0.0);
    float shadow = sampleShadow(worldPos, cascadeFor(viewDist));
    // Coloured shadows, the same rule as native_forward.frag: what a shadow
    // keeps is the atmosphere's colour at the old 0.35 energy, so a shadowed
    // side reads cool instead of grey. Keep the two paths in step.
    vec3 shadowTint = frame.fogColor.rgb /
                      max(dot(frame.fogColor.rgb, vec3(0.2126, 0.7152, 0.0722)), 1e-3) *
                      KS_SHADOW_FLOOR;
    vec3 sunVis = mix(shadowTint, vec3(1.0), shadow);
    // AC's ksAmbient is the multiplier the file puts on the ambient light.
    // The legacy flat fallback was exactly the 0.25 neutral, so the flat
    // branch applies the authored value directly, and the IBL branch's
    // irradiance is calibrated to the same neutral, so the authored value
    // rides as a ratio around it: a file that authored nothing (level 4 ->
    // 0.25) keeps today's image byte for byte, and a lit hall wall that
    // authors 0.8 reads twice as bright under the same sky. The specular IBL
    // term is a reflection, not ambient light, and stays untouched.
    float ambientBoost = authoredAmbient / KS_MATERIAL_AMBIENT_NEUTRAL;
    vec3 ambient;
    if (frame.iblParams.x > 0.5) {
        float NdotV = max(dot(N, V), 1e-4);
        vec3 F0 = mix(vec3(0.04) * f0Scale, albedo, metalness);
        vec2 brdf = texture(iblBrdfLut, vec2(NdotV, roughness)).rg;
        vec3 R = reflect(-V, N);
        vec3 specIbl = textureLod(iblPrefiltered, equirectUv(R),
                                  roughness * frame.iblParams.y).rgb;
        vec3 diffIbl = texture(iblIrradiance, equirectUv(N)).rgb;
        // AC authors the reflection curve of a lacquered dielectric with
        // fresnelC / fresnelEXP / fresnelMaxLevel, and the Countach body
        // authors 0.05 rising to 0.60: ignoring it left the flank as matte as
        // the leather, because the split-sum term only ever reflects the
        // legacy 0.04. The GBuffer carries the cap (the base stays the legacy
        // 0.04 and the curve uses the default exponent), so a material that
        // authored one now reflects the environment as AC intends. Metals
        // keep the split-sum term: their F0 is the albedo, not a dielectric
        // curve.
        vec3 reflection = F0 * brdf.x + brdf.y;
        if (authoredFresnelMax > 0.001 && metalness < 0.5) {
            float base = F0.r;
            reflection = vec3(base +
                              (max(base, authoredFresnelMax) - base) *
                                  pow(1.0 - NdotV, KS_MATERIAL_FRESNEL_POWER));
        }
        ambient = (diffIbl * albedo * (1.0 - metalness) * ambientBoost +
                   specIbl * reflection) * ssao(worldPos, N);
    } else {
        ambient = albedo * authoredAmbient * ssao(worldPos, N);
    }
    if (frame.ssgiParams.w > 0.5) {
        ambient += albedo * texture(ssgiColor, vUV).rgb *
                   frame.ssgiParams.y * sunRad * sunVis * 0.5;
    }
    vec3 diffuse = albedo * (1.0 - metalness) * sunRad * NdotL * sunVis;
    float a = roughness * roughness;
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float dd = NdotH * NdotH * (a * a - 1.0) + 1.0;
    float D = (a * a) / (PI * dd * dd);
    float k = a * 0.5;
    float G = (NdotL / (NdotL * (1.0 - k) + k)) * (NdotV / (NdotV * (1.0 - k) + k));
    vec3 F0 = mix(vec3(0.04) * f0Scale, albedo, metalness);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 specular = ((D * G * F) / (4.0 * NdotL * NdotV + 1e-4)) * NdotL * sunVis * sunRad;
    float acc = 0.055 * 0.055; // sharper clearcoat for lacquered bodywork
    float ccdd = NdotH * NdotH * (acc * acc - 1.0) + 1.0;
    float ccD = (acc * acc) / (PI * ccdd * ccdd);
    float cck = acc * 0.5;
    float ccG = (NdotL / (NdotL * (1.0 - cck) + cck)) *
                (NdotV / (NdotV * (1.0 - cck) + cck));
    vec3 ccF = vec3(0.04) + (1.0 - 0.04) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 specularCoat = ((ccD * ccG * ccF) / (4.0 * NdotL * NdotV + 1e-4)) *
                        NdotL * sunVis * sunRad * coated * 1.35;
    vec3 lit = ambient + diffuse + specular + specularCoat;
    // Native local lights (headlights, track lamps): after the sun, before the
    // screen-space terms, with the same surface response.
    vec3 litBeforeLocal = lit;
    lit += localLightsShading(worldPos, N, V, albedo, metalness, roughness, f0Scale);

    vec3 refl = ssr(worldPos, N, V, L, roughness);
    float f0 = mix(0.04 * f0Scale, 1.0, metalness);
    float fres = f0 + (1.0 - f0) * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    lit += refl * fres * (1.0 - roughness);
    lit += vec3(1.0, 0.25, 0.03) * (frame.brakeGlow.x * 1.5) * brakeDisc;
    // Standing water: a dielectric film over the ground, Schlick at water's F0.
    // It reflects the sky in place of the road - barely at all looking straight
    // down, most of it at grazing - so the near asphalt stays dark and the
    // distance turns into the bright band a wet straight shows.
    float wetF = KS_WET_WATER_F0 + (1.0 - KS_WET_WATER_F0) * pow(1.0 - NdotV, 5.0);
    float wetReflect = clamp(wetAmount * wetUp * wetF, 0.0, 1.0);
    lit = lit * (1.0 - wetReflect) + frame.fogColor.rgb * wetReflect;
    float fogT = clamp(1.0 - exp(-heightFogAmount(camPos, worldPos)), 0.0, frame.fogParams.w);
    vec3 inscatter = volumetricInscatter(camPos, worldPos, sunRad);
    vec3 fogColor = frame.fogColor.rgb + inscatter;
    outColor = vec4(mix(lit, fogColor, fogT), 1.0);


}
