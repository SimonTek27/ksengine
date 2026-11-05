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
    vec4 brakeGlow;         // brief P6: x = normalized hottest-disc temperature [0..1]
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
// bindings 3/4 — only gbuffer.frag samples them; this forward path adds
// its sun specular with brief P3 (same two-lobe math as deferred).
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
// AC txDetail: a weave or grain multiplied over the albedo, tiled. A missing
// map binds white 1x1, which leaves the albedo exactly as authored.
layout(set = 1, binding = 5) uniform sampler2D detailMap;
layout(set = 1, binding = 2) uniform MaterialData {
    float roughness;
    float metalness;
    float normalScale; // 0 = keep the vertex normal, 1 = full perturbation
    float clearcoat;   // brief P3: materials.txt cell 6, >= 0.5 = coated
    float brakeDisc;   // brief P6: >= 0.5 = brake disc (glow below)
    float alphaTest;   // >= 0.5 = discard below alphaRef (KN5 alpha_tested)
    float alphaRef;
    float emissive;    // unlit term added after shading
    float opacity;     // multiplies the albedo alpha on a blended surface
    float glassiness;  // 0 = decal, 1 = glass pane
    float tintR;       // bodywork paint: multiplies the albedo (1,1,1 = baked)
    float tintG;
    float tintB;
    float paintSolid;  // 1 = tint replaces the albedo instead of multiplying it
    float detailScale; // >0 = tile the detail map over the albedo
} material;

const float PI = 3.14159265;

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
    // range - no remapping needed.
    float bias = 0.0015;
    // One tap per pixel is what made the shadow edges stair-step: the depth
    // map stores a manual value, so the LINEAR sampler alone cannot soften a
    // hard comparison. Nine taps over one texel, and the penumbra comes out of
    // how many of them agree.
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
    // 0.35 is the old unlit floor: fully shadowed stays grey, not black, so a
    // large caster does not read as a hole in the room.
    return mix(0.35, 1.0, lit);
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
    // One sample feeds both the cut-out test and the shading.
    vec4 albedoSample = fragColor * texture(albedoMap, fragUV);
    if (material.detailScale > 0.0) {
        // Carbon and leather carry their weave here, not in the base map.
        // A detail map is neutral at 0.5 grey, so doubling it makes a
        // flat detail the identity and only the weave shows through.
        albedoSample.rgb *= texture(detailMap, fragUV * material.detailScale).rgb * 2.0;
    }
    // KN5 alpha test: decals, grilles and light covers are cut out instead of
    // drawn as opaque cards.
    if (material.alphaTest >= 0.5 && albedoSample.a < material.alphaRef) discard;
    // A livery texture carries its own colour, so a paint the player picked
    // replaces it; painting a plain body only tints it.
    vec3 paint = vec3(material.tintR, material.tintG, material.tintB);
    vec3 albedo = mix(albedoSample.rgb * paint, paint, material.paintSolid);

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
    // cascadeSplits.w = live cascade count (1..3): never sample a layer the
    // shadow pass did not render.
    int liveCascades = int(frame.cascadeSplits.w + 0.5);
    if (liveCascades < 1) liveCascades = 3;
    cascadeIndex = min(cascadeIndex, liveCascades - 1);

    float shadow = sampleShadow(fragWorldPos, cascadeIndex);

    vec3 V = normalize(frame.cameraPos.xyz - fragWorldPos);

    // Ambient: split-sum IBL when enabled (brief P2), legacy flat albedo *
    // 0.25 otherwise. Diffuse lobe from the irradiance map, specular from
    // the prefiltered sky weighted by the BRDF LUT — that is what puts the
    // sky/horizon on a glossy car body in the forward path too.
    vec3 ambient;
    if (frame.iblParams.x > 0.5) {
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

    // Brief P3 — sun specular, same two-lobe math as deferred_lighting.frag
    // (base GGX over the paint + optional sharp clear-coat lobe), scaled by
    // sunRad so the highlight tracks the sun's intensity and tint and
    // vanishes when the sun does. The coat flag comes straight from the
    // per-mesh MaterialData — the forward path never touches the GBuffer.
    vec3 sunRad = frame.sunColor.rgb * frame.sunColor.a;
    float NdotV = max(dot(N, V), 1e-4);
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);

    float a = material.roughness * material.roughness;
    float dd = NdotH * NdotH * (a * a - 1.0) + 1.0;
    float D = (a * a) / (PI * dd * dd);
    float k = a * 0.5;
    float G = (NdotL / (NdotL * (1.0 - k) + k)) *
              (NdotV / (NdotV * (1.0 - k) + k));
    vec3 F0 = mix(vec3(0.04), albedo, material.metalness);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 specular = ((D * G * F) / (4.0 * NdotL * NdotV + 1e-4)) *
                    NdotL * shadow * sunRad;

    float coated = material.clearcoat >= 0.5 ? 1.0 : 0.0;
    float acc = 0.055 * 0.055; // sharper clearcoat for lacquered bodywork // coat alpha = 0.07^2, brief P3 sharp lobe
    float ccdd = NdotH * NdotH * (acc * acc - 1.0) + 1.0;
    float ccD = (acc * acc) / (PI * ccdd * ccdd);
    float cck = acc * 0.5;
    float ccG = (NdotL / (NdotL * (1.0 - cck) + cck)) *
                (NdotV / (NdotV * (1.0 - cck) + cck));
    vec3 ccF = vec3(0.04) + (1.0 - 0.04) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 specularCoat = ((ccD * ccG * ccF) / (4.0 * NdotL * NdotV + 1e-4)) *
                        NdotL * shadow * sunRad * coated * 1.35;

    vec3 lit = ambient + diffuse + specular + specularCoat;

    // Brief P6 — brake glow, same emissive term as deferred_lighting.frag
    // (keep the two paths in sync): the flag comes from this mesh's own
    // MaterialData, the heat from FrameData.brakeGlow.x (setBrakeGlow).
    lit += vec3(1.0, 0.25, 0.03) * (frame.brakeGlow.x * 1.5) *
           (material.brakeDisc >= 0.5 ? 1.0 : 0.0);

    // Self-lit surfaces (ksEmissive): added after shading, so a lamp cover or
    // a lit panel reads at full strength regardless of the light on it.
    lit += albedo * material.emissive;

    // Height fog, same model the deferred path uses, so switching between
    // the two paths does not change the atmosphere. The clear colour is set
    // to fogColor.rgb too, which is what keeps geometry fading into the sky
    // instead of into a differently-coloured background.
    float fogT = clamp(1.0 - exp(-heightFogAmount(frame.cameraPos.xyz, fragWorldPos)),
                       0.0, frame.fogParams.w);

    // --- glass ----------------------------------------------------------
    // A pane is not a lit surface: almost none of the diffuse survives, which is
    // what made the windscreen read as a milky slab. What you see is the tint
    // plus the reflection, and the reflection takes over towards grazing angles.
    float fresnel = pow(1.0 - NdotV, 5.0);
    float glass = clamp(material.glassiness, 0.0, 1.0);
    vec3 surface = lit;
    if (glass > 0.001) {
        vec3 reflection = ambient * (0.30 + 0.70 * fresnel) + specular * (0.5 + fresnel);
        surface = mix(lit, reflection + albedo * 0.05, glass);
    }
    // The authored alpha, the pane's own opacity, and Fresnel so the edge of a
    // windscreen goes opaque while you look straight through the middle.
    float alpha = clamp(albedoSample.a * material.opacity * (1.0 + 2.5 * fresnel * glass),
                        0.0, 1.0);
    // Premultiplied for the see-through pipeline; the opaque one ignores alpha,
    // and with opacity 1 this is exactly the previous colour.
    outColor = vec4(mix(surface, frame.fogColor.rgb, fogT) * alpha, alpha);
}
