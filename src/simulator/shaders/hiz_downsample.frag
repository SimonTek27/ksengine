#version 450

// Max-pool pass for CPU occlusion culling: one output texel per 8x8 tile of
// the depth buffer, holding the tile's FARTHEST surface (largest raw depth).
// max-depth is what makes the CPU test in OcclusionTest.h conservative: sky
// (cleared to the far plane) tiles can never occlude, and an object's own
// previous-frame pixels are >= its nearest point wherever it drew.
//
// Input is the geometry pass's depth image (SHADER_READ_ONLY after the
// barrier in NativeRenderer::recordOcclusionPass); output is an R32F image
// that gets copied to a host-visible buffer.

layout(set = 0, binding = 0) uniform sampler2D uDepth;

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 base = ivec2(gl_FragCoord.xy) * 8;
    ivec2 maxCoord = textureSize(uDepth, 0) - 1;
    float m = 0.0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            // Clamp: the grid is rounded up to whole tiles, so edge tiles
            // overhang the depth image by up to 7 pixels.
            ivec2 c = min(base + ivec2(x, y), maxCoord);
            m = max(m, texelFetch(uDepth, c, 0).r);
        }
    }
    outColor = vec4(m, 0.0, 0.0, 1.0);
}
