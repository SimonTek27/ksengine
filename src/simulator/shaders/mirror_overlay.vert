#version 450

// Mirror tile (stage 1): a screen-space quad, geometry driven entirely from
// push constants — no vertex buffer, the strip index makes four corners.
// rect is the NDC window the tile occupies on screen; the UVs map it to the
// rear-view target one-to-one (the flip is the fragment shader's review,
// since a rear-view mirror really does swap the sides).

layout(push_constant) uniform Push {
    vec4 rect;    // NDC x0, y0, x1, y1
    vec4 params;  // x = flip X, y = glass dim, z = flip Y, w spare
} pc;

layout(location = 0) out vec2 fragUv;

void main() {
    // Triangle strip, four vertices, no index buffer:
    //   0 = bottom-left, 1 = bottom-right, 2 = top-left, 3 = top-right.
    const vec2 corners[4] = vec2[4](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
    const vec2 corner = corners[gl_VertexIndex];
    const vec2 ndc = mix(pc.rect.xy, pc.rect.zw, corner);
    gl_Position = vec4(ndc, 0.0, 1.0);
    fragUv = corner;
}
