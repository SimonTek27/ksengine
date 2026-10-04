#version 450

// Deferred GBuffer fragment stage of the particle sprite (roadmap
// ksengine-vs-cryengine P1). Same inputs as particle.frag — both are driven
// by particle.vert — but it writes the three MRT targets
// deferred_lighting.frag reads.
//
// An opaque GBuffer has no way to hold fractional alpha: RT0.a is ambient
// occlusion and RT2.w is the coverage test (>= 0.5 means "there is a
// surface here"). So the sprite's soft edge is resolved the only honest way
// available - the rim below the alpha threshold is discarded, and the
// surviving interior writes coverage 1 exactly like solid geometry, then
// gets lit by the lighting pass with the camera-facing normal the vertex
// stage supplies.
//
//   RT0  rgb = sprite tint,        a = 1 (no AO term for a sprite)
//   RT1  xyz = camera-facing normal, w = roughness
//   RT2  xyz = sprite centre position, w = coverage

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec3 fragNormal;

layout(location = 0) out vec4 outAlbedoAO;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outWorldPosCoverage;

void main() {
    vec2 centered = fragUV * 2.0 - 1.0;
    float d2 = dot(centered, centered);
    if (d2 > 1.0) discard;

    float shape = 1.0 - smoothstep(0.25, 1.0, d2);
    // The rim is cut where the sprite has faded past usefulness: because
    // alpha also carries the life fade, the disc *shrinks* over the second
    // half of its life instead of vanishing in one pop at death.
    if (fragColor.a * shape < 0.2) discard;

    outAlbedoAO = vec4(fragColor.rgb, 1.0);
    // Sprites are vapour/dust/smoke: very rough, so the specular lobe stays
    // a wide sheen instead of a mirror dot.
    outNormalRoughness = vec4(normalize(fragNormal), 0.85);
    outWorldPosCoverage = vec4(fragWorldPos, 1.0);
}
