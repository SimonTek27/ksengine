#version 450

// GBuffer MRT. Three render targets, all cleared to zero, with coverage
// encoded in RT0.a / RT2.w (geometry always writes 1.0 there) so the
// deferred lighting pass can tell "no geometry here" from a real surface.
//
//   RT0  R8G8B8A8_UNORM    rgb = albedo, a = ambient occlusion
//   RT1  R16G16B16A16_SF   xyz = world normal, w = roughness
//   RT2  R16G16B16A16_SF   xyz = world position, w = coverage (1 = drawn)

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outAlbedoAO;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outWorldPosCoverage;

// Descriptor set 1 (Roadmap 2.3) — same layout and fallbacks as
// native_forward.frag: white albedo, flat normal, std140 material block.
// The GBuffer pipeline shares m_pipelineLayout with the forward one, so
// recordDrawList() binds this set identically for both passes.
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform MaterialData {
    float roughness;
    float metalness;
    float normalScale; // 0 = keep the vertex normal, 1 = full perturbation
    float pad;
} material;

// Derivative-based TBN (KN5 bakes no tangents). Derivatives are taken at
// top level and only the blend is behind normalScale, so implicit LODs and
// dFdx/dFdy stay well defined; the tbs guard keeps constant-UV meshes from
// normalizing by zero. Shared shape with native_forward.frag — keep in sync.
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

void main() {
    vec3 N = normalize(fragNormal);
    if (!gl_FrontFacing) N = -N;

    // Albedo: vertex colour × baked texture (white fallback = old look).
    outAlbedoAO = vec4(fragColor.rgb * texture(albedoMap, fragUV).rgb, 1.0);

    // Normal + roughness now come from the material instead of the old
    // hardcoded 0.75: material.roughness feeds deferred_lighting.frag's GGX
    // directly, so an authored ksRoughness finally reaches the lit frame.
    vec3 mapN = texture(normalMap, fragUV).xyz * 2.0 - 1.0;
    N = perturbNormal(N, fragWorldPos, fragUV, mapN, material.normalScale);
    outNormalRoughness = vec4(N, material.roughness);
    outWorldPosCoverage = vec4(fragWorldPos, 1.0);
}
