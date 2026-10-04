#include "KsTest.h"
#include "engine/Math/Frustum.h"

#include <cmath>

using namespace ks::math;

namespace {

mat4 cameraViewProj() {
    const mat4 view = mat4::lookAt(vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, -1.0f},
                                   vec3{0.0f, 1.0f, 0.0f});
    const mat4 proj = mat4::perspective(60.0f * 3.14159265f / 180.0f, 16.0f / 9.0f,
                                        0.1f, 100.0f);
    return proj * view;
}

} // namespace

int main() {
    const Frustum f = Frustum::fromViewProj(cameraViewProj());

    // --- Points: a camera at the origin looking down -Z, 60 deg FOV.
    KS_CHECK(f.containsPoint(vec3{0.0f, 0.0f, -10.0f}));    // straight ahead
    KS_CHECK(f.containsPoint(vec3{1.0f, 0.5f, -20.0f}));    // inside the cone
    KS_CHECK(!f.containsPoint(vec3{0.0f, 0.0f, 10.0f}));    // behind the camera
    KS_CHECK(!f.containsPoint(vec3{0.0f, 0.0f, -0.05f}));   // closer than near=0.1
    KS_CHECK(!f.containsPoint(vec3{0.0f, 0.0f, -200.0f}));  // beyond far=100
    KS_CHECK(!f.containsPoint(vec3{-100.0f, 0.0f, -10.0f})); // way off to the left
    KS_CHECK(!f.containsPoint(vec3{0.0f, 200.0f, -10.0f}));  // way above

    // --- AABBs.
    KS_CHECK(f.intersects(vec3{-1.0f, -1.0f, -11.0f}, vec3{1.0f, 1.0f, -9.0f}));
    KS_CHECK(!f.intersects(vec3{-1.0f, -1.0f, 9.0f}, vec3{1.0f, 1.0f, 11.0f}));   // behind
    KS_CHECK(!f.intersects(vec3{-1.0f, -1.0f, -300.0f}, vec3{1.0f, 1.0f, -250.0f})); // too far
    // Straddles the near plane (z=-0.1): the far half of the box is visible,
    // so the conservative test must keep it.
    KS_CHECK(f.intersects(vec3{-0.05f, -0.05f, -0.5f}, vec3{0.05f, 0.05f, -0.05f}));
    // Entirely between the camera and the near plane: correctly rejected.
    KS_CHECK(!f.intersects(vec3{-0.05f, -0.05f, -0.09f}, vec3{0.05f, 0.05f, -0.02f}));

    // --- Transform helper: rotation must re-fit, not just rotate, the box.
    vec3 tmin, tmax;
    mat4 rotY = mat4::fromPositionRollPitchYaw(vec3{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f,
                                               3.14159265f * 0.5f);
    Frustum::transformBounds(rotY, vec3{-1.0f, -1.0f, -1.0f}, vec3{1.0f, 1.0f, 1.0f},
                             tmin, tmax);
    KS_CHECK_NEAR(tmin.x, -1.0f, 1e-4);
    KS_CHECK_NEAR(tmax.x, 1.0f, 1e-4);
    // A translated box lands where the translation puts it.
    Frustum::transformBounds(mat4::translation(vec3{10.0f, 0.0f, -5.0f}),
                             vec3{-1.0f, -1.0f, -1.0f}, vec3{1.0f, 1.0f, 1.0f},
                             tmin, tmax);
    KS_CHECK_NEAR(tmin.x, 9.0f, 1e-5);
    KS_CHECK_NEAR(tmax.z, -4.0f, 1e-5);

    const mat4 backView = mat4::lookAt(vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, 1.0f},
                                       vec3{0.0f, 1.0f, 0.0f});
    const Frustum lookingAway =
        Frustum::fromViewProj(mat4::perspective(1.0f, 1.777f, 0.1f, 100.0f) * backView);
    KS_CHECK(!lookingAway.intersects(vec3{9.0f, -1.0f, -6.0f},
                                     vec3{11.0f, 1.0f, -4.0f})); // behind that camera
    KS_CHECK(lookingAway.intersects(vec3{-1.0f, -1.0f, 4.0f},
                                    vec3{1.0f, 1.0f, 6.0f}));

    // --- OpenGL depth convention: near plane moves to z + w >= 0.
    const mat4 glProj = [&] {
        mat4 p;
        const float n = 0.1f, fq = 100.0f;
        p(0, 0) = 1.0f / (1.777f * std::tan(0.5f));
        p(1, 1) = 1.0f / std::tan(0.5f);
        p(2, 2) = -(fq + n) / (fq - n);
        p(2, 3) = -(2.0f * fq * n) / (fq - n);
        p(3, 2) = -1.0f;
        p(3, 3) = 0.0f;
        return p;
    }();
    const Frustum glF = Frustum::fromViewProj(glProj * backView,
                                              Frustum::DepthRange::NegativeToOneToOne);
    // Same camera geometry as before, so the same expectations hold.
    KS_CHECK(glF.containsPoint(vec3{0.0f, 0.0f, 10.0f}));
    KS_CHECK(!glF.containsPoint(vec3{0.0f, 0.0f, -10.0f}));
    KS_CHECK(!glF.containsPoint(vec3{0.0f, 0.0f, 200.0f}));

    // A ZeroToOne frustum fed the same GL matrix must reject points closer
    // than the near plane it mistakes for the middle of the depth range.
    const Frustum mismatched =
        Frustum::fromViewProj(glProj * backView, Frustum::DepthRange::ZeroToOne);
    KS_CHECK(!mismatched.containsPoint(vec3{0.0f, 0.0f, 0.05f}));

    return KS_TEST_RESULT("frustum_test");
}
