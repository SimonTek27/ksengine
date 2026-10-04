#include "KsTest.h"
#include "engine/Math/OcclusionTest.h"

#include <vector>

using namespace ks;
using namespace ks::math;
using namespace ks::math::occlusion;

namespace {

constexpr float kFovY = 60.0f * 3.14159265f / 180.0f;
constexpr float kAspect = 16.0f / 9.0f;
constexpr float kNear = 0.1f;
constexpr float kFar = 5000.0f;
constexpr int kW = 1920;
constexpr int kH = 1080;
constexpr int kGW = (kW + kTileSize - 1) / kTileSize; // 240
constexpr int kGH = (kH + kTileSize - 1) / kTileSize; // 135

// Identity view: the camera sits at the origin looking down -Z, so world z
// values double as view-space distances and the numbers stay readable.
mat4 proj() { return mat4::perspective(kFovY, kAspect, kNear, kFar); }

std::vector<float> gridFill(float v) {
    return std::vector<float>(static_cast<size_t>(kGW) * kGH, v);
}

constexpr float kMargin = 1.0f; // same value NativeRenderer tests with

} // namespace

int main() {
    const mat4 p = proj();
    const float r22 = p(2, 2);
    const float r23 = p(2, 3);

    // ---- linearizeDepth: Vulkan depth range endpoints + monotonicity. ----
    KS_CHECK_NEAR(linearizeDepth(0.0f, r22, r23), kNear, 1e-3);
    // Far endpoint only to ~1%: ndcZ + r22 cancels two ~1.0 floats down to
    // ~2e-5 there, so a float computation of the far plane is inherently
    // imprecise (the renderer has the same error; sky just needs to stay
    // "effectively infinite", which it does).
    KS_CHECK_NEAR(linearizeDepth(1.0f, r22, r23), kFar, 50.0);
    KS_CHECK(linearizeDepth(0.3f, r22, r23) < linearizeDepth(0.4f, r22, r23));
    KS_CHECK(linearizeDepth(2.0f, r22, r23) >= kFar); // beyond the far plane

    // A small box 38..42 m ahead of the camera.
    const vec3 bmin{-2.0f, -2.0f, -42.0f};
    const vec3 bmax{2.0f, 2.0f, -38.0f};

    // Nearer geometry everywhere (20 m < 38 m nearest point): hidden.
    std::vector<float> g = gridFill(20.0f);
    KS_CHECK(aabbOccluded(p, bmin, bmax, g.data(), kGW, kGH, kW, kH, kMargin));

    // Far plane everywhere — sky can never occlude.
    g = gridFill(kFar);
    KS_CHECK(!aabbOccluded(p, bmin, bmax, g.data(), kGW, kGH, kW, kH, kMargin));

    // As far as the box itself: it sticks out, visible.
    g = gridFill(40.0f);
    KS_CHECK(!aabbOccluded(p, bmin, bmax, g.data(), kGW, kGH, kW, kH, kMargin));

    // Margin band: 36.5 m beats (38 - 1), 37.5 m does not.
    g = gridFill(36.5f);
    KS_CHECK(aabbOccluded(p, bmin, bmax, g.data(), kGW, kGH, kW, kH, kMargin));
    g = gridFill(37.5f);
    KS_CHECK(!aabbOccluded(p, bmin, bmax, g.data(), kGW, kGH, kW, kH, kMargin));

    // One far tile *inside* the screen rect breaks the occlusion...
    g = gridFill(20.0f);
    {
        const vec4 centre = p * vec4{0.0f, 0.0f, -40.0f, 1.0f};
        const int tx = static_cast<int>((centre.x / centre.w * 0.5f + 0.5f) * kW) / kTileSize;
        const int ty = static_cast<int>((centre.y / centre.w * 0.5f + 0.5f) * kH) / kTileSize;
        g[static_cast<size_t>(ty) * kGW + tx] = kFar;
    }
    KS_CHECK(!aabbOccluded(p, bmin, bmax, g.data(), kGW, kGH, kW, kH, kMargin));

    // ...while the same far tile *outside* the rect does not (dilation
    // reaches one tile past the box, not across the screen).
    g = gridFill(20.0f);
    g[0] = kFar;
    KS_CHECK(aabbOccluded(p, bmin, bmax, g.data(), kGW, kGH, kW, kH, kMargin));

    // Behind the near plane: not testable, reports visible even under a
    // grid of near geometry.
    g = gridFill(20.0f);
    KS_CHECK(!aabbOccluded(p, vec3{-2.0f, -2.0f, 38.0f}, vec3{2.0f, 2.0f, 42.0f},
                           g.data(), kGW, kGH, kW, kH, kMargin));

    // Entirely outside the viewport: no pixel on screen can show it.
    g = gridFill(kFar);
    KS_CHECK(aabbOccluded(p, vec3{1000.0f, -2.0f, -42.0f}, vec3{1020.0f, 2.0f, -38.0f},
                          g.data(), kGW, kGH, kW, kH, kMargin));

    // No grid at all: never cull.
    KS_CHECK(!aabbOccluded(p, bmin, bmax, nullptr, kGW, kGH, kW, kH, kMargin));

    return KS_TEST_RESULT("occlusion_test");
}
