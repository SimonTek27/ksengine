#version 450

// Screen-space indirect bounce, gathered once per half-resolution pixel into an
// offscreen target so the result can be filtered before the lighting pass reads
// it. The inline version this replaces could not be denoised at all: every pixel
// only had its own taps, and a neighbour's bounce was never available.
//
// The pass runs at the same half resolution as bloom (its render pass and blur
// pipeline are reused), so the gaussian that follows reaches twice the radius in
// world space for the same number of taps - which is what makes a large
// video.ssgiRadius usable instead of blotchy.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D gbufWorldPos;
layout(set = 0, binding = 1) uniform sampler2D gbufNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufAlbedo;

layout(push_constant) uniform GatherPC {
    float radiusUV;   // screen-space footprint in UV units (video.ssgiRadius)
    float pad0;
    float pad1;
    float pad2;
} pc;

const int TAPS = 16;
const float GOLDEN_ANGLE = 2.39996323;

void main() {
    vec4 coverage = texture(gbufWorldPos, vUV);
    if (abs(coverage.w) < 0.5) {           // sky: nothing to bounce off
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 worldPos = coverage.xyz;
    vec3 N = normalize(texture(gbufNormal, vUV).xyz);

    vec3 bounce = vec3(0.0);
    float total = 0.0;
    for (int i = 0; i < TAPS; ++i) {
        float t = (float(i) + 0.5) / float(TAPS);
        float ang = float(i) * GOLDEN_ANGLE;   // golden angle: no clumping
        vec2 uv = vUV + vec2(cos(ang), sin(ang)) * (pc.radiusUV * sqrt(t));
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;

        vec4 s = texture(gbufWorldPos, uv);
        if (abs(s.w) < 0.5) continue;
        vec3 d = s.xyz - worldPos;
        float dist = length(d);
        if (dist < 1e-3) continue;

        // Only light arriving from in front of the surface contributes; the
        // inverse-square term keeps the nearest occluders dominant, which is
        // also what limits the contact shadowing a plain blur would smear.
        float facing = max(dot(N, d / dist), 0.0);
        if (facing <= 0.0) continue;
        float falloff = 1.0 / (1.0 + dist * dist);
        bounce += texture(gbufAlbedo, uv).rgb * facing * falloff;
        total += facing * falloff;
    }

    outColor = vec4(total > 0.0 ? bounce / total : vec3(0.0), 1.0);
}
