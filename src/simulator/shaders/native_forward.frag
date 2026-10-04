#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragColor;

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
} frame;

layout(set = 0, binding = 1) uniform sampler2DArray shadowCascades;

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
    vec3 N = normalize(fragNormal);
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

    vec3 ambient = fragColor.rgb * 0.25;
    vec3 diffuse = fragColor.rgb * frame.sunColor.rgb * frame.sunColor.a * NdotL * shadow;
    vec3 lit = ambient + diffuse;

    // Height fog, same model the deferred path uses, so switching between
    // the two paths does not change the atmosphere. The clear colour is set
    // to fogColor.rgb too, which is what keeps geometry fading into the sky
    // instead of into a differently-coloured background.
    float fogT = clamp(1.0 - exp(-heightFogAmount(frame.cameraPos.xyz, fragWorldPos)),
                       0.0, frame.fogParams.w);

    outColor = vec4(mix(lit, frame.fogColor.rgb, fogT), fragColor.a);
}
