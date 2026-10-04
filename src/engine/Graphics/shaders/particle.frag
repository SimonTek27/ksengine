#version 450

// Forward-pass fragment stage of the particle sprite: a procedural soft
// disc, no texture. The old version sampled `particleTexture` from set 1,
// which nothing ever bound — that binding is why this shader stayed dead.
// Generating the shape from the interpolated UV keeps the pipeline down to
// push constants alone: no sampler, no descriptor set, nothing to leak when
// the sprite system is switched off.

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
// Written by the shared vertex stage, unused here (the GBuffer variant of
// this sprite is the one that reads them).
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 centered = fragUV * 2.0 - 1.0;
    float d2 = dot(centered, centered);
    if (d2 > 1.0) discard;

    // Radial falloff: opaque core, soft shoulder, gone at the rim. Squared
    // distance keeps the gradient smooth across the whole disc without a
    // sqrt per fragment.
    float shape = 1.0 - smoothstep(0.25, 1.0, d2);
    outColor = vec4(fragColor.rgb, fragColor.a * shape);
}
