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
// Coverage test: geometry writes w = 1.0 into RT2 (-1 on brake-disc
// pixels, brief P6 — abs() below decodes both), cleared pixels keep 0,
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
    vec4 motionBlurParams;  // declared to reach iblParams below (std140 offsets must be walked member by member)
    vec4 iblParams;         // x = 1 when the split-sum IBL branch is active, y = prefilter max LOD
    vec4 brakeGlow;         // brief P6: x = normalized hottest-disc temperature [0..1]
    vec4 ssgiParams;        // x = screen radius (uv), y = intensity, w = 1 when enabled
} frame;

layout(set = 0, binding = 1) uniform sampler2DArray shadowCascades;
layout(set = 0, binding = 2) uniform sampler2D gbufAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gbufNormal;
layout(set = 0, binding = 4) uniform sampler2D gbufWorldPos;
layout(set = 0, binding = 5) uniform sampler2D iblIrradiance;
layout(set = 0, binding = 6) uniform sampler2D iblPrefiltered;
layout(set = 0, binding = 7) uniform sampler2D iblBrdfLut;
layout(set = 0, binding = 8) uniform sampler2D ssgiColor;

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
    return mix(0.35, 1.0, lit);
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

void main() {
    vec4 coverage = texture(gbufWorldPos, vUV);
    float brakeDisc = coverage.w < 0.0 ? 1.0 : 0.0;
    if (abs(coverage.w) < 0.5) {
        outColor = vec4(frame.fogColor.rgb, 1.0);
        return;
    }
    vec3 worldPos = coverage.xyz;
    vec4 albedoAO = texture(gbufAlbedo, vUV);
    vec4 normalRough = texture(gbufNormal, vUV);
    vec3 albedo = albedoAO.rgb;
    float metalness = clamp(albedoAO.a, 0.0, 1.0);
    vec3 N = normalize(normalRough.xyz);
    float coated = normalRough.w < 0.0 ? 1.0 : 0.0;
    float roughness = clamp(abs(normalRough.w), 0.05, 1.0);
    vec3 camPos = frame.cameraPos.xyz;
    float viewDist = length(worldPos - camPos);
    vec3 V = normalize(camPos - worldPos);
    vec3 L = normalize(-frame.sunDirection.xyz);
    vec3 sunRad = frame.sunColor.rgb * frame.sunColor.a;
    float NdotL = max(dot(N, L), 0.0);
    float shadow = sampleShadow(worldPos, cascadeFor(viewDist));
    vec3 ambient;
    if (frame.iblParams.x > 0.5) {
        float NdotV = max(dot(N, V), 1e-4);
        vec3 F0 = mix(vec3(0.04), albedo, metalness);
        vec2 brdf = texture(iblBrdfLut, vec2(NdotV, roughness)).rg;
        vec3 R = reflect(-V, N);
        vec3 specIbl = textureLod(iblPrefiltered, equirectUv(R),
                                  roughness * frame.iblParams.y).rgb;
        vec3 diffIbl = texture(iblIrradiance, equirectUv(N)).rgb;
        ambient = (diffIbl * albedo * (1.0 - metalness) +
                   specIbl * (F0 * brdf.x + brdf.y)) * ssao(worldPos, N);
    } else {
        ambient = albedo * 0.25 * ssao(worldPos, N);
    }
    if (frame.ssgiParams.w > 0.5) {
        ambient += albedo * texture(ssgiColor, vUV).rgb *
                   frame.ssgiParams.y * sunRad * shadow * 0.5;
    }
    vec3 diffuse = albedo * (1.0 - metalness) * sunRad * NdotL * shadow;
    float a = roughness * roughness;
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float dd = NdotH * NdotH * (a * a - 1.0) + 1.0;
    float D = (a * a) / (PI * dd * dd);
    float k = a * 0.5;
    float G = (NdotL / (NdotL * (1.0 - k) + k)) * (NdotV / (NdotV * (1.0 - k) + k));
    vec3 F0 = mix(vec3(0.04), albedo, metalness);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 specular = ((D * G * F) / (4.0 * NdotL * NdotV + 1e-4)) * NdotL * shadow * sunRad;
    float acc = 0.055 * 0.055; // sharper clearcoat for lacquered bodywork
    float ccdd = NdotH * NdotH * (acc * acc - 1.0) + 1.0;
    float ccD = (acc * acc) / (PI * ccdd * ccdd);
    float cck = acc * 0.5;
    float ccG = (NdotL / (NdotL * (1.0 - cck) + cck)) *
                (NdotV / (NdotV * (1.0 - cck) + cck));
    vec3 ccF = vec3(0.04) + (1.0 - 0.04) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 specularCoat = ((ccD * ccG * ccF) / (4.0 * NdotL * NdotV + 1e-4)) *
                        NdotL * shadow * sunRad * coated * 1.35;
    vec3 lit = ambient + diffuse + specular + specularCoat;
    vec3 refl = ssr(worldPos, N, V, L, roughness);
    float f0 = mix(0.04, 1.0, metalness);
    float fres = f0 + (1.0 - f0) * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    lit += refl * fres * (1.0 - roughness);
    lit += vec3(1.0, 0.25, 0.03) * (frame.brakeGlow.x * 1.5) * brakeDisc;
    float fogT = clamp(1.0 - exp(-heightFogAmount(camPos, worldPos)), 0.0, frame.fogParams.w);
    vec3 inscatter = volumetricInscatter(camPos, worldPos, sunRad);
    vec3 fogColor = frame.fogColor.rgb + inscatter;
    outColor = vec4(mix(lit, fogColor, fogT), 1.0);
}
