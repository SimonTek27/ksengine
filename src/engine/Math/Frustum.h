#pragma once
// View-frustum culling primitive for ksengine.
//
// Why this exists: the runtime renderer used to submit *every* queued mesh to
// every pass (no frustum, no occlusion test), and the only frustum test in the
// repository lived in the Qt editor's RenderOptimizer, unreachable from the
// Qt-free path. This is the engine-owned, Qt-free equivalent: six planes
// extracted from a view-projection matrix (Gribb/Hartmann), then a slab test
// against a world-space AABB.
//
// Depth convention matters: ks::math::perspective/ortho produce Vulkan/D3D
// clip space (z in [0,1]), so DepthRange::ZeroToOne is the default. Pass
// NegativeToOneToOne for an OpenGL-style projection (z in [-1,1]) instead —
// only the near/far pair of planes changes.
//
// Header-only on purpose: no new TU in the ksengine target, and the test
// (tests/ksengine/frustum_test.cpp) can include it directly.

#include "MathTypesFree.h"

#include <cmath>

namespace ks::math {

struct Plane3 {
    float a = 0, b = 0, c = 0, d = 0;

    // Signed distance: >= 0 means the point is on the inside half-space.
    float distance(const vec3& p) const { return a * p.x + b * p.y + c * p.z + d; }

    void normalize() {
        const float len = std::sqrt(a * a + b * b + c * c);
        if (len > 1e-8f) { a /= len; b /= len; c /= len; d /= len; }
    }
};

class Frustum {
public:
    enum class DepthRange {
        ZeroToOne,          // Vulkan / D3D clip space
        NegativeToOneToOne  // OpenGL clip space
    };

    static constexpr int kPlaneCount = 6;
    // Plane order: left, right, bottom, top, near, far.
    enum PlaneIndex { Left = 0, Right = 1, Bottom = 2, Top = 3, Near = 4, Far = 5 };

    // Extracts the six half-spaces of clip = viewProj * worldPos. Assumes a
    // standard projection (w = -viewZ for perspective, w = 1 for ortho), which
    // is what perspective()/ortho() in this header's own math layer produce.
    static Frustum fromViewProj(const mat4& viewProj,
                                DepthRange depth = DepthRange::ZeroToOne) {
        Frustum f;
        auto row = [&viewProj](int r) {
            return vec4{viewProj(r, 0), viewProj(r, 1), viewProj(r, 2), viewProj(r, 3)};
        };
        const vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

        auto combine = [](const vec4& x, const vec4& y, float ky) {
            Plane3 p{x.x + ky * y.x, x.y + ky * y.y, x.z + ky * y.z, x.w + ky * y.w};
            p.normalize();
            return p;
        };

        f.m_planes[Left]   = combine(r3, r0, +1.0f);  // x + w >= 0
        f.m_planes[Right]  = combine(r3, r0, -1.0f);  // w - x >= 0
        f.m_planes[Bottom] = combine(r3, r1, +1.0f);  // y + w >= 0
        f.m_planes[Top]    = combine(r3, r1, -1.0f);  // w - y >= 0
        // Near/far are the only planes that depend on the depth convention:
        //   z >= zMin  ->  clip.z - zMin * clip.w >= 0
        f.m_planes[Near] = (depth == DepthRange::ZeroToOne)
                               ? combine(r2, r3, 0.0f)   // z >= 0
                               : combine(r3, r2, +1.0f); // z + w >= 0
        f.m_planes[Far]  = combine(r3, r2, -1.0f);       // w - z >= 0
        return f;
    }

    const Plane3& plane(int i) const { return m_planes[i]; }

    bool containsPoint(const vec3& p) const {
        for (const Plane3& pl : m_planes)
            if (pl.distance(p) < 0.0f) return false;
        return true;
    }

    // Conservative AABB test: rejects the box only if it lies entirely behind
    // at least one plane (uses the "p-vertex", the box corner furthest along
    // the plane normal).
    bool intersects(const vec3& bmin, const vec3& bmax) const {
        for (const Plane3& pl : m_planes) {
            const float px = pl.a >= 0.0f ? bmax.x : bmin.x;
            const float py = pl.b >= 0.0f ? bmax.y : bmin.y;
            const float pz = pl.c >= 0.0f ? bmax.z : bmin.z;
            if (pl.distance(vec3{px, py, pz}) < 0.0f) return false;
        }
        return true;
    }

    // World-space AABB of a local-space box transformed by `model`. Rotation
    // is handled by re-fitting the box around all eight transformed corners,
    // so a rotated mesh never gets a too-small (and thus wrongly culled) box.
    static void transformBounds(const mat4& model, const vec3& bmin, const vec3& bmax,
                                vec3& outMin, vec3& outMax) {
        outMin = vec3{ 1e30f,  1e30f,  1e30f};
        outMax = vec3{-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < 8; ++i) {
            const vec3 corner{
                (i & 1) ? bmax.x : bmin.x,
                (i & 2) ? bmax.y : bmin.y,
                (i & 4) ? bmax.z : bmin.z,
            };
            const vec3 w = model * corner;
            outMin.x = w.x < outMin.x ? w.x : outMin.x;
            outMin.y = w.y < outMin.y ? w.y : outMin.y;
            outMin.z = w.z < outMin.z ? w.z : outMin.z;
            outMax.x = w.x > outMax.x ? w.x : outMax.x;
            outMax.y = w.y > outMax.y ? w.y : outMax.y;
            outMax.z = w.z > outMax.z ? w.z : outMax.z;
        }
    }

private:
    Plane3 m_planes[kPlaneCount];
};

} // namespace ks::math
