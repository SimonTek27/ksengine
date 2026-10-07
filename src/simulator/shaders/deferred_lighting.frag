#version 450

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
// Coverage test: geometry writes w = 1.0 into RT2, cleared pixels keep 0,
// so background pixels take the early-out and reproduce the forward
// path's clear colour exactly.

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
} frame;

layout(set = 0, binding = 1) uniform sampler2DArray shadowCascades;
layout(set = 0, binding = 2) uniform sampler2D gbufAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gbufNormal;
layout(set = 0, binding = 4) uniform sampler2D gbufWorldPos;

const float PI = 3.14159265;

// In-scatter shaping only — density/falloff/opacity come from the UBO so the
// weather system can drive them (see NativeRenderer::setFog).
const float FOG_ANISOTROPY = 0.55;
const float VOL_STRENGTH = 4.0;
const int FOG_STEPS = 12;

int cascadeFor(float viewDist) {
    if (viewDist < frame.cascadeSplits.x) return 0;
    if (viewDist < frame.cascadeSplits.y) return 1;
    return 2;
}

// Same contract as native_forward.frag's sampleShadow(): 1.0 = lit,
// 0.35 = shadowed (never fully black, avoids acne-looking terminators),
// 1.0 = outside the light frustum.
float sampleShadow(vec3 worldPos, int cascadeIndex) {
    vec4 lightClip = frame.cascadeViewProj[cascadeIndex] * vec4(worldPos, 1.0);
    vec3 ndc = lightClip.xyz / lightClip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    float storedDepth = texture(shadowCascades, vec3(uv, float(cascadeIndex))).r;
    float bias = 0.0015;
    return (ndc.z - bias > storedDepth) ? 0.35 : 1.0;
}

// Analytic integral of density * exp(-height * falloff) along the segment
// camPos -> worldPos. Degenerates to the uniform-fog case as dy -> 0.
float heightFogAmount(vec3 camPos, vec3 worldPos) {
    vec3 d = worldPos - camPos;
    float dist = max(length(d) - frame.fogParams.z, 0.0);
    float dy = d.y;
    float k = frame.fogParams.x * exp(-camPos.y * frame.fogParams.y);
    if (abs(dy) < 1e-3) return k * dist;
    return k * dist * (1.0 - exp(-frame.fogParams.y * dy)) / (frame.fogParams.y * dy);
}

// Transmittance of sunlight through the shadow cascades averaged along the
// camera ray — this is what turns the fog into shafts instead of soup.
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

// Screen-space ambient occlusion (roadmap ksengine-vs-cryengine P0, KS_SSAO).
// Runs inline in this pass and reuses the two GBuffer bindings the shader
// already has (world position + the normal computed above), so it costs no
// extra image, no extra pass and no descriptor change - the AO that a
// compute pass would have written is folded straight into the ambient term.
// Samples are pushed out along the normal on a golden-angle hemisphere and
// tested against the scene by radial distance from the camera: both points
// of a test sit on nearly the same view ray, so the perspective warp of the
// radial metric cancels in the comparison.
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
        float cosT = 1.0 - t;                       // dense near the normal
        float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));
        vec3 dir = tangent * (cos(phi) * sinT) + bitangent * (sin(phi) * sinT) + N * cosT;
        vec3 sp = worldPos + dir * radius;

        vec4 clip = frame.viewProj * vec4(sp, 1.0);
        if (clip.w <= 0.0) continue;
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;

        vec4 scene = texture(gbufWorldPos, uv);
        if (scene.w < 0.5) continue;                // sky: never occludes
        float sceneDist = length(scene.xyz - camPos);
        float sampleDist = length(sp - camPos);
        // Occluded when the scene at that pixel is closer to the camera than
        // the sample point, and not so much further that the difference is
        // outside the occlusion radius.
        if (sceneDist + bias < sampleDist && (sampleDist - sceneDist) < radius) {
            occluded += 1.0;
        }
    }
    float ao = 1.0 - occluded / float(AO_SAMPLES);
    return mix(1.0, ao, strength);
}

// Screen-space reflections (roadmap ksengine-vs-cryengine P0, KS_SSR).
// `ssr.comp` on disk needs an `r32f` depth storage image this GBuffer does
// not expose, so the march lives here instead and uses `gbufWorldPos` as the
// scene-depth oracle - the same trick the SSAO above uses, and it keeps the
// feature inside the pass that is already bound to every texture it needs.
// The hit is shaded from the GBuffer's own albedo/normal with the sun, which
// is an approximation (no separate lit scene copy, no roughness mip chain),
// attenuated by the caller's fresnel and roughness terms.
const int SSR_STEPS = 16;

vec3 ssr(vec3 worldPos, vec3 N, vec3 V, vec3 L, float roughness) {
    if (frame.ssrParams.w < 0.5) return vec3(0.0);
    if (roughness > frame.ssrParams.z) return vec3(0.0);

    vec3 R = reflect(-V, N);
    float maxDist = frame.ssrParams.x;
    float stepLen = maxDist / float(SSR_STEPS);
    float thickness = stepLen * 0.75;   // strict enough that the ray never
                                        // "hits" the surface it starts from

    vec2 hitUV = vec2(0.0);
    bool hit = false;
    for (int i = 1; i <= SSR_STEPS; ++i) {
        vec3 p = worldPos + R * (stepLen * float(i));
        vec4 clip = frame.viewProj * vec4(p, 1.0);
        if (clip.w <= 0.0) break;
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        vec4 scene = texture(gbufWorldPos, uv);
        if (scene.w < 0.5) continue;    // sky: nothing to reflect off
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

void main() {
    vec4 coverage = texture(gbufWorldPos, vUV);
    if (coverage.w < 0.5) {
        outColor = vec4(frame.fogColor.rgb, 1.0);
        return;
    }

    vec3 worldPos = coverage.xyz;
    vec4 albedoAO = texture(gbufAlbedo, vUV);
    vec4 normalRough = texture(gbufNormal, vUV);

    vec3 albedo = albedoAO.rgb;
    float ao = albedoAO.a;
    vec3 N = normalize(normalRough.xyz);
    float roughness = clamp(normalRough.w, 0.05, 1.0);

    vec3 camPos = frame.cameraPos.xyz;
    float viewDist = length(worldPos - camPos);
    vec3 V = normalize(camPos - worldPos);
    vec3 L = normalize(-frame.sunDirection.xyz);
    vec3 sunRad = frame.sunColor.rgb * frame.sunColor.a;

    float NdotL = max(dot(N, L), 0.0);
    float shadow = sampleShadow(worldPos, cascadeFor(viewDist));

    vec3 ambient = albedo * 0.25 * ao * ssao(worldPos, N);
    vec3 diffuse = albedo * sunRad * NdotL * shadow;

    float a = roughness * roughness;
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float dd = NdotH * NdotH * (a * a - 1.0) + 1.0;
    float D = (a * a) / (PI * dd * dd);
    float k = a * 0.5;
    float G = (NdotL / (NdotL * (1.0 - k) + k)) * (NdotV / (NdotV * (1.0 - k) + k));
    vec3 F0 = vec3(0.04);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 specular = ((D * G * F) / (4.0 * NdotL * NdotV + 1e-4)) * NdotL * shadow;

    // -------------------------------------------------------------------
    // Clear-coat (P4) — secondary specular layer on top of the base paint.
    // Simulates a clear clear-coated finish (e.g. factory clear coat over base paint).
    // -------------------------------------------------------------------
    float clearcoat = 0.0f;           // authored per-mesh or fallback 0
    float clearcoat_roughness = 0.05f;// default very sharp clear coat
    float Fcc = 0.25;                 // clear coat Fresnel at normal incidence
    // Simple heuristic: if mesh has no explicit clearcoat, stay at 0.
    // Future: read from Kn5Material or materials.txt.

    // Clear-coat GGX about H, with its own roughness
    float a_cc = clearcoat_roughness;
    float a_cc2 = a_cc * a_cc;
    float NdotH2 = max(dot(N, H), 0.0);
    float NdotV2 = max(dot(N, V), 1e-4);
    float ccD = a_cc2 / (PI * pow(max(NdotH2, 1e-4), 3.0) * (1.0 - (a_cc2) * (1.0 - NdotH2) + 1e-4));
    float ccG = (NdotL * (1.0 - (a_cc2) / 3.0 + (a_cc2) * NdotL) + NdotV * (1.0 - (a_cc2) / 3.0 + (a_cc2) * NdotV)) / (2.0 * (NdotL + NdotV + 1e-4));
    vec3 Fcc_vec = vec3(Fcc);
    vec3 specular_cc = (ccD * ccG * Fcc_vec) / (4.0 * NdotL * NdotV2 + 1e-4) * NdotL * shadow;

    // Blend clear-coat with base specular (clear coat on top)
    vec3 lit = ambient + diffuse + specular + specular_cc;

    // Screen-space reflections on top of the specular term: fresnel-weighted
    // (metals are the only strong reflectors this GBuffer can express, and it
    // has no metallic channel, so roughness gates it instead) and faded out
    // as roughness rises because there is no mip chain to blur with.
    vec3 refl = ssr(worldPos, N, V, L, roughness);
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    lit += refl * fres * (1.0 - roughness);

    float fogT = clamp(1.0 - exp(-heightFogAmount(camPos, worldPos)), 0.0, frame.fogParams.w);
    vec3 inscatter = volumetricInscatter(camPos, worldPos, sunRad);
    vec3 fogColor = frame.fogColor.rgb + inscatter;

    outColor = vec4(mix(lit, fogColor, fogT), 1.0);
}
