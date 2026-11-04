#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Input: SSGI raw result
layout(set = 0, binding = 0, rgba16f) uniform readonly image2D ssgiInput;

// Output: SSGI filtered result
layout(set = 0, binding = 1, rgba16f) uniform image2D ssgiOutput; // read for the temporal blend

// Scene depth for bilateral filtering
layout(set = 0, binding = 2) uniform sampler2D depthTexture;

// Bilateral filter parameters
layout(push_constant) uniform BlurParams {
    vec4 params; // x: blur radius (in pixels), y: depth threshold, z: temporal blend, w: reserved
} pc;

void main() {
    ivec2 launchID = ivec2(gl_GlobalInvocationID.xy);
    ivec2 imageSize = imageSize(ssgiOutput);

    if (launchID.x >= imageSize.x || launchID.y >= imageSize.y) return;

    vec2 uv = (vec2(launchID) + 0.5) / vec2(imageSize);
    float centerDepth = texture(depthTexture, uv).r;

    vec3 color = vec3(0.0);
    float totalWeight = 0.0;

    int radius = int(pc.params.x);

    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            vec2 offset = vec2(float(x), float(y)) / vec2(imageSize);
            vec2 sampleUV = uv + offset;

            // Clamp to texture bounds
            sampleUV = clamp(sampleUV, vec2(0.0), vec2(1.0));

            float sampleDepth = texture(depthTexture, sampleUV).r;

            // Depth-based weight (bilateral filter)
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff * pc.params.y);

            // Spatial weight (Gaussian)
            float spatialDist = length(vec2(float(x), float(y)));
            float spatialWeight = exp(-(spatialDist * spatialDist) / (2.0 * pc.params.x * pc.params.x));

            float weight = depthWeight * spatialWeight;
            color += imageLoad(ssgiInput, ivec2(sampleUV * vec2(imageSize))).rgb * weight;
            totalWeight += weight;
        }
    }

    color /= max(totalWeight, 0.001);

    // Temporal blend with previous frame
    vec3 prevColor = imageLoad(ssgiOutput, launchID).rgb;
    color = mix(prevColor, color, pc.params.z);

    imageStore(ssgiOutput, launchID, vec4(color, 1.0));
}