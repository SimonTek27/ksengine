#pragma once

// Roadmap 2.4 (P2.1) — authored KN5 distance windows (lodIn / lodOut).
//
// Every KN5 mesh ends its payload with a float pair: the engine renders the
// mesh only while the camera sits within [lodIn, lodOut] metres (kseditor
// exposes the fields as "LOD IN"/"LOD OUT", CSP's lods.ini overrides them
// per object as LOD_IN/LOD_OUT). The Qt-free reader used to skip the pair
// on the floor; NativeRenderer::drawMesh() now applies it per instance, so
// authored content stops costing draw calls past its window without any
// change to the content itself.
//
// Design notes:
//   - A window that is not usable (inverted, zero, negative) fails OPEN: a
//     malformed mod file must never blank the scene — stability first.
//   - Distance is measured to the *nearest point* of the world AABB, not to
//     its centre: one big grandstand mesh must not vanish while the camera
//     is still standing inside its bounding box.

#include "../Math/MathTypesFree.h"

#include <algorithm>
#include <cmath>

namespace ks::scene {

// "No far limit": bigger than any authored value, any track extent and any
// sane camera far plane.
inline constexpr float kLodNoLimit = 1.0e9f;

struct LodWindow {
    float in = 0.0f;
    float out = kLodNoLimit;
};

// True when the pair can be enforced as-is (sane, non-empty range).
inline bool lodWindowUsable(const LodWindow& w) {
    return w.in >= 0.0f && w.out > w.in;
}

// True when the mesh carries a window worth testing. The renderer uses this
// as its fast-path gate: a default LodWindow skips the distance math and
// stays on the exact pre-2.4 code path.
inline bool hasAuthoredWindow(const LodWindow& w) {
    return w.in > 0.0f || w.out < kLodNoLimit;
}

// Should a mesh `distance` metres from the camera be queued this frame?
inline bool inLodWindow(float distance, const LodWindow& w) {
    if (!lodWindowUsable(w)) return true; // malformed -> fail-open
    return distance >= w.in && distance <= w.out;
}

// Distance from `point` to an AABB: 0 when inside, otherwise the gap to the
// nearest face or corner.
inline float distanceToBounds(const ks::math::vec3& point,
                              const ks::math::vec3& bmin,
                              const ks::math::vec3& bmax) {
    const float dx = std::max({bmin.x - point.x, 0.0f, point.x - bmax.x});
    const float dy = std::max({bmin.y - point.y, 0.0f, point.y - bmax.y});
    const float dz = std::max({bmin.z - point.z, 0.0f, point.z - bmax.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace ks::scene
