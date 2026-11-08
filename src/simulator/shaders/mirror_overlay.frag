#version 450

// Mirror tile (stage 1) fragment: reads the rear-view target the scene pass
// wrote earlier this frame. The horizontal flip IS the mirror optics — the
// scene pass renders the view from the yaw-180 camera straight (no image
// mirror), so the flip lands here where it costs one multiply per tap.
// The slight dim is glass, not a window: the tile sits on top of the scene
// and must read like a surface over it, at the 0.92 that does not blacken
// a night rear window to unreadable.

layout(set = 0, binding = 0) uniform sampler2D mirrorMap;

layout(push_constant) uniform Push {
    vec4 rect;    // unused here, but the layout is shared with the vertex one
    vec4 params;  // x = flip X, y = glass dim, z = flip Y, w spare
} pc;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = fragUv;
    if (pc.params.x > 0.5) uv.x = 1.0 - uv.x;
    if (pc.params.z > 0.5) uv.y = 1.0 - uv.y;
    vec3 color = texture(mirrorMap, uv).rgb;
    outColor = vec4(color * pc.params.y, 1.0);
}
