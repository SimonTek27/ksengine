#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Input: G-buffer data from render graph
layout(set = 0, binding = 0) uniform sampler2D gbufferDepth;      // Scene depth (linearized)
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;    // World-space normals
layout(set = 0, binding = 2) uniform sampler2D gbufferAlbedo;    // Albedo/base color

// Output: Indirect lighting result
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D ssgiOutput;      // GI result half-resolution

// Camera parameters (from push constants)
layout(push_constant) uniform SSGIParams {
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 invViewMatrix;
    mat4 invProjMatrix;
    vec4 params;        // x: radius, y: intensity, z: bias, w: max distance
    vec4 frameParams;   // x: frame index, y: temporal blend, z: noise scale, w: reserved
} params;

// Reconstruct view-space position from depth
vec3 reconstructViewPos(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = params.invProjMatrix * clipPos;
    return viewPos.xyz / viewPos.w;
}

// Reconstruct world-space position from depth
vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec3 viewPos = reconstructViewPos(uv, depth);
    return (params.viewMatrix * vec4(viewPos, 1.0)).xyz;
}

// Pseudo-random noise using screen position + frame index
float interleavedGradientNoise(vec2 screenPos) {
    return fract(52.9829189 * fract(dot(screenPos, vec2(0.06711056, 0.00583715))));
}

// Cosine-weighted hemisphere sampling
vec3 cosineSampleHemisphere(vec3 normal, vec2 random) {
    // Create orthonormal basis
    vec3 up = abs(normal.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    float r = sqrt(random.x);
    float theta = 2.0 * 3.14159265 * random.y;
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - random.x));

    return normalize(x * tangent + y * bitangent + z * normal);
}

// Screen-space ray march for GI
vec4 rayMarchSS(vec3 origin, vec3 direction, float maxDistance, int maxSteps) {
    vec4 result = vec4(0.0);

    // Sample start and end in screen space
    vec4 startClip = params.projMatrix * params.viewMatrix * vec4(origin, 1.0);
    vec2 startScreen = (startClip.xy / startClip.w) * 0.5 + 0.5;

    vec3 endPos = origin + direction * maxDistance;
    vec4 endClip = params.projMatrix * params.viewMatrix * vec4(endPos, 1.0);
    vec2 endScreen = (endClip.xy / endClip.w) * 0.5 + 0.5;

    // Ray direction in screen space
    vec2 rayDir = endScreen - startScreen;
    float rayLength = length(rayDir);
    rayDir /= max(rayLength, 0.0001);

    // March in screen space
    float stepSize = rayLength / float(maxSteps);
    vec2 step = rayDir * stepSize;

    vec2 currentUV = startScreen;
    float currentDepth = texture(gbufferDepth, currentUV).r;

    for (int i = 0; i < maxSteps; i++) {
        currentUV += step;

        // Sample depth at current position
        float sampledDepth = texture(gbufferDepth, currentUV).r;
        vec3 sampledWorldPos = reconstructWorldPos(currentUV, sampledDepth);

        // Check if we hit geometry
        vec3 rayPoint = origin + direction * (float(i) * stepSize);
        vec3 rayPointView = (params.viewMatrix * vec4(rayPoint, 1.0)).xyz;

        float depthDiff = rayPoint.z - sampledWorldPos.z;

        if (depthDiff > 0.001 && depthDiff < params.params.z) { // bias check
            // Hit! Sample albedo as indirect light color
            vec3 albedo = texture(gbufferAlbedo, currentUV).rgb;
            float attenuation = 1.0 - (float(i) * stepSize / maxDistance);
            result = vec4(albedo * attenuation * params.params.y, 1.0);
            break;
        }
    }

    return result;
}

void main() {
    ivec2 launchID = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outSize = imageSize(ssgiOutput);

    if (launchID.x >= outSize.x || launchID.y >= outSize.y) return;

    vec2 uv = (vec2(launchID) + 0.5) / vec2(outSize);

    // Sample G-buffer at current pixel
    float depth = texture(gbufferDepth, uv).r;
    vec3 normal = texture(gbufferNormal, uv).rgb;
    vec3 albedo = texture(gbufferAlbedo, uv).rgb;

    // Skip sky pixels (depth == 1.0 means far plane)
    if (depth >= 0.99) {
        imageStore(ssgiOutput, launchID, vec4(0.0));
        return;
    }

    // Reconstruct world position
    vec3 worldPos = reconstructWorldPos(uv, depth);
    vec3 viewPos = reconstructViewPos(uv, depth);

    // Generate random direction for hemisphere sampling
    float noise = interleavedGradientNoise(vec2(launchID) + vec2(params.frameParams.x));
    vec2 random = vec2(noise, fract(noise * 1.618033988749895));
    vec3 sampleDir = cosineSampleHemisphere(normal, random);

    // Ray march parameters
    float radius = params.params.x;
    int maxSteps = 16;

    // Perform screen-space ray march
    vec4 gi = rayMarchSS(worldPos, sampleDir, radius, maxSteps);

    // Store result
    imageStore(ssgiOutput, launchID, gi);
}