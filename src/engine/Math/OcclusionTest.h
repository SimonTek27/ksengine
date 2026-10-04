#pragma once

// CPU occlusion culling over a previous-frame depth grid.
//
// NativeRenderer max-pools the depth buffer into an 8x8-tile grid
// (hiz_downsample.frag), copies it to a host-visible buffer and linearises
// it once per frame in beginFrame(). drawMesh() then tests each world AABB
// against that grid: the object is culled only when *every* tile its screen
// rect (dilated by one tile) touches holds a surface nearer than the AABB's
// nearest point, minus a safety margin.
//
// The grid stores the FARTHEST surface per tile, which makes two things fall
// out for free:
//   * background sky (cleared depth = far plane) linearises to ~far and can
//     never occlude anything;
//   * an object can never occlude itself — wherever it drew last frame the
//     depth buffer holds a value >= its own nearest point, and where it was
//     fully depth-occluded the nearer occluder's depth is what got written.
//
// Everything here is pure math over ks::math types, so the predicate is unit
// tested without a Vulkan device (tests/ksengine/occlusion_test.cpp).

#include "MathTypesFree.h"

#include <algorithm>
#include <cmath>

namespace ks::math {
namespace occlusion {

// Must match the downsample factor in hiz_downsample.frag and the grid the
// renderer allocates: gridW = (fullW + kTileSize - 1) / kTileSize.
inline constexpr int kTileSize = 8;

// NDC depth in [0,1] (0 = near, 1 = far, Vulkan convention) -> positive
// view-space distance along the camera forward axis. Inverse of rows 2/3 of
// mat4::perspective: z_ndc = clip.z/clip.w = (-r22*z_v - r23)/z_v, hence
// z_v = -r23/(z_ndc + r22) and distance = -z_v = r23/(z_ndc + r22).
inline float linearizeDepth(float ndcZ, float r22, float r23) {
    const float denom = ndcZ + r22;
    // denom == 0 is the far plane at infinity: anything at or beyond it gets
    // an effectively infinite distance instead of a division by ~0.
    if (denom >= -1e-9f) return 1e7f;
    return r23 / denom;
}

// Projects the world-space AABB [bmin,bmax] with viewProj (the same matrix
// the grid was rendered with) and reports whether it was fully hidden behind
// nearer geometry in that grid.
//
//   grid            row-major gridH * gridW linear distances (metres),
//                   kTileSize pixels per tile of the full-res depth image
//   fullW/fullH     extent of the depth image the grid was built from
//   margin          metres the occluder must beat the object's nearest point
//                   by; absorbs one frame of camera/object motion
//
// Anything ambiguous reports "visible": a corner behind the near plane (the
// silhouette would be unbounded on screen) or a missing/empty grid.
inline bool aabbOccluded(const mat4& viewProj, const vec3& bmin, const vec3& bmax,
                         const float* grid, int gridW, int gridH,
                         int fullW, int fullH, float margin) {
    if (!grid || gridW <= 0 || gridH <= 0 || fullW <= 0 || fullH <= 0) return false;

    float minDist = 3.4e38f;
    float x0 = 3.4e38f, x1 = -3.4e38f, y0 = 3.4e38f, y1 = -3.4e38f;
    for (int i = 0; i < 8; ++i) {
        const vec4 corner{(i & 1) ? bmax.x : bmin.x,
                          (i & 2) ? bmax.y : bmin.y,
                          (i & 4) ? bmax.z : bmin.z,
                          1.0f};
        const vec4 clip = viewProj * corner;
        if (clip.w <= 0.0f) return false; // behind the near plane: not testable
        // Vulkan NDC: x right, y down (the projection negates row 1) with
        // y = -1 at framebuffer row 0, so (ndc*0.5+0.5) maps straight to
        // pixels on both axes. The bbox of the projected corners contains
        // the whole projected silhouette (the hull of a convex box).
        const float px = (clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(fullW);
        const float py = (clip.y / clip.w * 0.5f + 0.5f) * static_cast<float>(fullH);
        x0 = std::min(x0, px); x1 = std::max(x1, px);
        y0 = std::min(y0, py); y1 = std::max(y1, py);
        // clip.w = -z_view: the view-space forward distance, the same metric
        // linearizeDepth() produces for the grid.
        minDist = std::min(minDist, clip.w);
    }

    // Tile rect, dilated by one tile per side: the dilation absorbs the
    // sub-pixel TAA jitter and lateral motion between the grid's frame and
    // this one; `margin` below absorbs the depth change that comes with it.
    int tx0 = static_cast<int>(x0) / kTileSize;
    int tx1 = static_cast<int>(x1) / kTileSize;
    int ty0 = static_cast<int>(y0) / kTileSize;
    int ty1 = static_cast<int>(y1) / kTileSize;
    tx0 = std::max(tx0 - 1, 0);
    ty0 = std::max(ty0 - 1, 0);
    tx1 = std::min(tx1 + 1, gridW - 1);
    ty1 = std::min(ty1 + 1, gridH - 1);
    if (tx0 > tx1 || ty0 > ty1) return true; // entirely outside the viewport

    const float limit = minDist - margin;
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            // Farthest surface in the tile still reaches (almost) as far as
            // the object's nearest point -> something sticks out: visible.
            if (grid[ty * gridW + tx] >= limit) return false;
        }
    }
    return true;
}

} // namespace occlusion
} // namespace ks::math
