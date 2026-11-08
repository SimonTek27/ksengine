#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec3 cameraPos;
    float padding;
} camera;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float occlusionStrength;
    float normalScale;
    int useBaseColorTexture;
    int useMetallicRoughnessTexture;
    int useNormalTexture;
    int useOcclusionTexture;
    int useEmissiveTexture;
    float brakeDiscGlow;   // 0 = off, 1 = full glow (set per-draw for BrakeDisc)
    vec3 padding2;
} material;

layout(set = 2, binding = 0) uniform sampler2D baseColorMap;
layout(set = 2, binding = 1) uniform sampler2D metallicRoughnessMap;
layout(set = 2, binding = 2) uniform sampler2D normalMap;
layout(set = 2, binding = 3) uniform sampler2D occlusionMap;
layout(set = 2, binding = 4) uniform sampler2D emissiveMap;

layout(set = 3, binding = 0) uniform samplerCube irradianceMap;
layout(set = 3, binding = 1) uniform samplerCube prefilterMap;
layout(set = 3, binding = 2) uniform sampler2D brdfLUT;

// SSGI (set 4)
layout(set = 4, binding = 0) uniform sampler2D ssgiTexture;
layout(set = 4, binding = 1) uniform SSGIParams {
    int enableSSGI;
    float ssgiIntensity;
    vec2 padding;
} ssgi;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragViewDir;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

vec3 saturate(vec3 x) { return clamp(x, 0.0, 1.0); }
float saturate(float x) { return clamp(x, 0.0, 1.0); }

float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

vec3 F_Schlick(float VdotH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

float V_SmithGGX(float NdotV, float NdotL, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;
    float V = NdotL * (NdotV * (1.0 - k) + k);
    float L = NdotV * (NdotL * (1.0 - k) + k);
    return 1.0 / max(V + L, 1e-5);
}

vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;
    mat3 TBN = mat3(fragTangent, fragBitangent, fragNormal);
    return normalize(TBN * tangentNormal);
}

vec3 getBaseColor() {
    vec3 baseColor = material.baseColorFactor.rgb;
    if (material.useBaseColorTexture > 0) {
        baseColor *= texture(baseColorMap, fragTexCoord).rgb;
    }
    return baseColor;
}

float getMetallic() {
    float m = material.metallicFactor;
    if (material.useMetallicRoughnessTexture > 0) {
        m *= texture(metallicRoughnessMap, fragTexCoord).b;
    }
    return m;
}

float getRoughness() {
    float r = material.roughnessFactor;
    if (material.useMetallicRoughnessTexture > 0) {
        r *= texture(metallicRoughnessMap, fragTexCoord).g;
    }
    return saturate(r);
}

float getOcclusion() {
    float ao = 1.0;
    if (material.useOcclusionTexture > 0) {
        ao = texture(occlusionMap, fragTexCoord).r;
    }
    return 1.0 - material.occlusionStrength * (1.0 - ao);
}

vec3 getEmissive() {
    vec3 em = material.emissiveFactor;
    if (material.useEmissiveTexture > 0) {
        em *= texture(emissiveMap, fragTexCoord).rgb;
    }
    // Brake disc glow: if brakeDiscGlow > 0, add a warm glow proportional
    // to the glow factor. The CPU side sets this only for BrakeDisc materials.
    if (material.brakeDiscGlow > 0.01f) {
        float g = material.brakeDiscGlow;
        // Warm orange glow, intensity proportional to the set factor
        em += vec3(g * 0.8f, g * 0.5f, g * 0.2f) * material.emissiveFactor.a;
    }
    return em;
}

vec3 getF0(float metallic, vec3 baseColor) {
    return mix(vec3(0.04), baseColor, metallic);
}

vec3 IBL_Diffuse(vec3 N, vec3 baseColor, float metallic) {
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * baseColor * (1.0 - metallic);
    return diffuse;
}

vec3 IBL_Specular(vec3 N, vec3 V, vec3 F0, float roughness) {
    vec3 R = reflect(-V, N);
    int maxMip = 9;
    float mip = clamp(roughness * maxMip, 0.0, float(maxMip));
    vec3 prefilteredColor = textureLod(prefilterMap, R, mip).rgb;
    vec2 brdf = texture(brdfLUT, vec2(saturate(dot(N, V)), roughness)).xy;
    return prefilteredColor * (F0 * brdf.x + brdf.y);
}

float computeDirectLight(vec3 N, vec3 V, vec3 L, float roughness, vec3 F0) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, normalize(L + V)), 0.0);
    float VdotH = max(dot(V, normalize(L + V)), 0.0);
    
    if (NdotL <= 0.0 || NdotV <= 0.0) return 0.0;
    
    float D = D_GGX(NdotH, roughness);
    vec3 F = F_Schlick(VdotH, F0);
    float V_smith = V_SmithGGX(NdotV, NdotL, roughness);
    
    float brdf = D * dot(F, vec3(1.0)) * V_smith / (4.0 * NdotV * NdotL);
    return brdf * NdotL;
}

void main() {
    vec3 N = fragNormal;
    if (material.useNormalTexture > 0) {
        N = getNormalFromMap();
    }
    
    vec3 V = normalize(fragViewDir);
    float NdotV = max(dot(N, V), 0.0);
    
    vec3 baseColor = getBaseColor();
    float metallic = getMetallic();
    float roughness = getRoughness();
    float occlusion = getOcclusion();
    vec3 emissive = getEmissive();
    
    vec3 F0 = getF0(metallic, baseColor);
    
    // Image-based lighting
    vec3 diffuseIBL = IBL_Diffuse(N, baseColor, metallic);
    vec3 specularIBL = IBL_Specular(N, V, F0, roughness);
    
    // Analytic direct light (sun)
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 lightColor = vec3(1.0, 0.95, 0.9) * 10.0;
    float directBRDF = computeDirectLight(N, V, lightDir, roughness, F0);
    vec3 directLight = lightColor * (baseColor * (1.0 - metallic) * (1.0 / PI) + F0 * directBRDF) * NdotV;
    
    // Combine indirect lighting with SSGI
vec3 indirectLighting = (diffuseIBL + specularIBL) * occlusion;
if (ssgi.enableSSGI > 0) {
    vec3 ssgiColor = texture(ssgiTexture, fragTexCoord).rgb;
    indirectLighting += ssgiColor * ssgi.ssgiIntensity;
}
vec3 color = indirectLighting + directLight + emissive;
    
    // Tone mapping (ACES filmic)
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    
    float alpha = material.baseColorFactor.a;
    if (material.useBaseColorTexture > 0) {
        alpha *= texture(baseColorMap, fragTexCoord).a;
    }
    
    outColor = vec4(color, alpha);
}