#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

// Full FrameData block: members past cameraPos are not read here, but they
// must be declared (in this exact order) so fogColor/fogParams/aoParams land
// on the offsets NativeRenderer::FrameDataUBO writes them to.
layout(set = 0, binding = 0) uniform FrameData {
    vec4 sunDirection;      // xyz, w unused
    vec4 sunColor;          // rgb, a = intensity
    mat4 cascadeViewProj[3];
    vec4 cascadeSplits;     // view-space far distance of each cascade (xyz), w unused
    vec4 cameraPos;         // xyz, w unused
    mat4 viewProj;          // unjittered, current frame
    mat4 prevViewProj;      // unjittered, previous frame
    vec4 taaParams;         // x = history feedback (0 disables TAA)
    vec4 fogColor;          // rgb = atmosphere tint, a unused
    vec4 fogParams;         // x = density, y = height falloff, z = start distance, w = max opacity
    vec4 aoParams;          // x = radius, y = bias, z = strength, w = 1 when enabled
    vec4 ssrParams;         // x = max distance, y = intensity, z = roughness cutoff, w = 1 when enabled
    vec4 motionBlurParams;  // x = strength, y = sample count, z = max length, w = 1 when enabled
    vec4 iblParams;         // x = 1 when the split-sum IBL branch is active, y = prefilter max LOD
} frame;

layout(set = 0, binding = 1) uniform sampler2DArray shadowCascades;
// Brief P2: split-sum IBL samplers (irradiance / prefilter chain / BRDF LUT),
// the same three the deferred lighting pass binds at its own set 0 5..7.
layout(set = 0, binding = 2) uniform sampler2D iblIrradiance;
layout(set = 0, binding = 3) uniform sampler2D iblPrefiltered;
layout(set = 0, binding = 4) uniform sampler2D iblBrdfLut;

vec2 equirectUv(vec3 d) {
    return vec2(atan(d.z, d.x) * 0.15915494 + 0.5,
                acos(clamp(d.y, -1.0, 1.0)) * 0.31830989);
}

// Descriptor set 1 (Roadmap 2.3) — per-mesh material, bound for every
// instance inside NativeRenderer::recordDrawList(). Empty texture paths are
// already resolved by TextureRuntime to a white albedo and a flat (0,0,255)
// normal, so every descriptor is valid to sample: a mesh without authored
// textures renders exactly like before (white × vertex colour, normalScale 0).
// The layout also carries the P1 roughness/metalness map samplers at
// bindings 3/4 — only gbuffer.frag samples them; this forward path stays
// ambient + diffuse (specular terms land with brief P3).
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform MaterialData {
    float roughness;
    float metalness;
    float normalScale; // 0 = keep the vertex normal, 1 = full perturbation
    float pad;
} material;

// Derivative-based TBN: KN5 bakes no tangents, so the tangent frame is
// reconstructed from screen-space position/UV derivatives. The derivatives
// are taken at top level (uniform control flow) and only the blend happens
// behind normalScale, so implicit LODs and dFdx/dFdy stay well defined.
// tbs guard: meshes with degenerate/constant UVs (placeholder solids) would
// otherwise normalize by zero into NaN normals.
vec3 perturbNormal(vec3 N, vec3 worldPos, vec2 uv, vec3 mapN, float scale) {
    vec3 dp1 = dFdx(worldPos), dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 T = cross(dp2, N) * duv1.x + cross(N, dp1) * duv2.x;
    vec3 B = cross(dp2, N) * duv1.y + cross(N, dp1) * duv2.y;
    float tbs = max(dot(T, T), dot(B, B));
    if (tbs <= 1e-12) return N;
    float invmax = inversesqrt(tbs);
    vec3 mapped = normalize(mat3(T * invmax, B * invmax, N) * mapN);
    return normalize(mix(N, mapped, clamp(scale, 0.0, 1.0)));
}

float sampleShadow(vec3 worldPos, int cascadeIndex) {
    vec4 lightClip = frame.cascadeViewProj[cascadeIndex] * vec4(worldPos, 1.0);
    vec3 ndc = lightClip.xyz / lightClip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;

    // ksShadow.frag writes gl_FragCoord.z, and the light projection is
    // Vulkan clip space, so ndc.z and the stored value are the same [0,1]
    // range — no remapping needed.
    float storedDepth = texture(shadowCascades, vec3(uv, float(cascadeIndex))).r;
    float bias = 0.0015;
    return (ndc.z - bias > storedDepth) ? 0.35 : 1.0; // 0.35 = not fully black, avoids unlit-looking shadow acne on large casters
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

void main() {
    // Roadmap 2.3: albedo = vertex colour × baked texture (white fallback
    // keeps untextured meshes byte-identical to the pre-2.3 path).
    vec3 albedo = fragColor.rgb * texture(albedoMap, fragUV).rgb;

    vec3 N = normalize(fragNormal);
    // Normal map sampled unconditionally so the derivatives inside
    // perturbNormal() are computed in uniform control flow; normalScale 0
    // (no authored map) blends straight back to the vertex normal.
    vec3 mapN = texture(normalMap, fragUV).xyz * 2.0 - 1.0;
    N = perturbNormal(N, fragWorldPos, fragUV, mapN, material.normalScale);

    vec3 L = normalize(-frame.sunDirection.xyz);
    float NdotL = max(dot(N, L), 0.0);

    // Pick the nearest cascade whose split still covers this fragment's
    // view-space depth (cameraPos to fragment distance is used as a cheap
    // stand-in for true view-space Z, which is fine for a single forward
    // draw with no wide-FOV distortion concerns here).
    float viewDist = length(fragWorldPos - frame.cameraPos.xyz);
    int cascadeIndex = 2;
    if (viewDist < frame.cascadeSplits.x) cascadeIndex = 0;
    else if (viewDist < frame.cascadeSplits.y) cascadeIndex = 1;

    float shadow = sampleShadow(fragWorldPos, cascadeIndex);

    // Ambient: split-sum IBL when enabled (brief P2), legacy flat albedo *
    // 0.25 otherwise. Diffuse lobe from the irradiance map, specular from
    // the prefiltered sky weighted by the BRDF LUT — that is what puts the
    // sky/horizon on a glossy car body in the forward path too.
    vec3 ambient;
    if (frame.iblParams.x > 0.5) {
        vec3 V = normalize(frame.cameraPos.xyz - fragWorldPos);
        float NdotV = max(dot(N, V), 1e-4);
        vec3 F0 = mix(vec3(0.04), albedo, material.metalness);
        vec2 brdf = texture(iblBrdfLut, vec2(NdotV, material.roughness)).rg;
        vec3 R = reflect(-V, N);
        vec3 specIbl = textureLod(iblPrefiltered, equirectUv(R),
                                  material.roughness * frame.iblParams.y).rgb;
        vec3 diffIbl = texture(iblIrradiance, equirectUv(N)).rgb;
        ambient = diffIbl * albedo * (1.0 - material.metalness) +
                  specIbl * (F0 * brdf.x + brdf.y);
    } else {
        ambient = albedo * 0.25;
    }
    vec3 diffuse = albedo * frame.sunColor.rgb * frame.sunColor.a * NdotL * shadow;
    vec3 lit = ambient + diffuse;

    // Height fog, same model the deferred path uses, so switching between
    // the two paths does not change the atmosphere. The clear colour is set
    // to fogColor.rgb too, which is what keeps geometry fading into the sky
    // instead of into a differently-coloured background.
    float fogT = clamp(1.0 - exp(-heightFogAmount(frame.cameraPos.xyz, fragWorldPos)),
                       0.0, frame.fogParams.w);

    outColor = vec4(mix(lit, frame.fogColor.rgb, fogT), fragColor.a);
}
