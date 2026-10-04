#include "KsTest.h"
#include "engine/Math/MathTypesFree.h"
#include "engine/scene/Components.h"

#include <cmath>

using namespace ks;

int main() {
    math::mat4 id;
    KS_CHECK(id(0, 0) == 1.0f && id(1, 1) == 1.0f && id(2, 2) == 1.0f && id(3, 3) == 1.0f);
    KS_CHECK(id(0, 3) == 0.0f && id(3, 0) == 0.0f);

    const math::mat4 t = math::mat4::translation({1.0f, 2.0f, 3.0f});
    KS_CHECK(t(0, 3) == 1.0f && t(1, 3) == 2.0f && t(2, 3) == 3.0f);
    const math::vec3 p = t * math::vec3{0.0f, 0.0f, 0.0f};
    KS_CHECK(p.x == 1.0f && p.y == 2.0f && p.z == 3.0f);

    const math::mat4 inv = t.inverse();
    KS_CHECK_NEAR(inv(0, 3), -1.0f, 1e-5);
    KS_CHECK_NEAR(inv(1, 3), -2.0f, 1e-5);
    KS_CHECK_NEAR(inv(2, 3), -3.0f, 1e-5);
    KS_CHECK_NEAR(inv(0, 0), 1.0f, 1e-5);

    const math::mat4 zero = [&] {
        math::mat4 m;
        for (int i = 0; i < 16; ++i) m.m[i] = 0.0f;
        return m;
    }();
    KS_CHECK(zero.inverse()(0, 0) == 1.0f);

    const math::mat4 rpy =
        math::mat4::fromPositionRollPitchYaw({1.0f, 2.0f, 3.0f}, 0.0f, 0.0f, 0.0f);
    KS_CHECK(rpy(0, 0) == 1.0f && rpy(1, 1) == 1.0f && rpy(2, 2) == 1.0f);
    KS_CHECK(rpy(0, 3) == 1.0f && rpy(1, 3) == 2.0f && rpy(2, 3) == 3.0f);

    const float halfPi = 1.57079632679f;
    const math::mat4 roll90 = math::mat4::fromPositionRollPitchYaw({0, 0, 0}, halfPi, 0, 0);
    KS_CHECK_NEAR(roll90(0, 1), -1.0f, 1e-5);
    KS_CHECK_NEAR(roll90(1, 0), 1.0f, 1e-5);
    KS_CHECK_NEAR(roll90(0, 0), 0.0f, 1e-5);

    const math::vec3 ex{1, 0, 0};
    const math::vec3 ey{0, 1, 0};
    const math::vec3 cr = ex.cross(ey);
    KS_CHECK(cr.x == 0.0f && cr.y == 0.0f && cr.z == 1.0f);
    KS_CHECK(ex.dot(ey) == 0.0f);
    KS_CHECK(ex.dot(ex) == 1.0f);

    const math::vec3 v34{3, 0, 4};
    const math::vec3 n = v34.normalized();
    KS_CHECK_NEAR(n.x, 0.6f, 1e-6);
    KS_CHECK_NEAR(n.z, 0.8f, 1e-6);
    KS_CHECK_NEAR(n.length(), 1.0f, 1e-6);
    KS_CHECK(math::vec3{}.length() == 0.0f);

    const math::quat q = math::quat::fromAxisAngle({0, 0, 1}, halfPi);
    const math::vec3 rotated = q.rotatedVec(ex);
    KS_CHECK_NEAR(rotated.x, 0.0f, 1e-5);
    KS_CHECK_NEAR(rotated.y, 1.0f, 1e-5);
    KS_CHECK_NEAR(rotated.z, 0.0f, 1e-5);

    const math::mat4 persp = math::mat4::perspective(1.0f, 16.0f / 9.0f, 0.5f, 500.0f);
    KS_CHECK(persp(0, 0) > 0.0f);
    // Vulkan's viewport transform puts NDC y = -1 at the top of the
    // framebuffer, so world-space up must project to negative y — otherwise
    // the frame renders upside down and front faces lose their winding.
    KS_CHECK(persp(1, 1) < 0.0f);
    KS_CHECK(persp(3, 2) == -1.0f);

    const math::mat4 view =
        math::mat4::lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
    KS_CHECK_NEAR(view(0, 0), 1.0f, 1e-6);
    KS_CHECK_NEAR(view(1, 1), 1.0f, 1e-6);
    KS_CHECK_NEAR(view(2, 2), 1.0f, 1e-6);
    KS_CHECK_NEAR(view(0, 3), 0.0f, 1e-6);

    const math::mat4 ortho = math::mat4::ortho(0, 10, 0, 10, -1, 1);
    KS_CHECK_NEAR(ortho(0, 0), 0.2f, 1e-6);
    KS_CHECK_NEAR(ortho(1, 1), -0.2f, 1e-6); // same y-down convention as perspective()
    KS_CHECK(ortho(3, 3) == 1.0f);

    const math::mat4 composed = t * rpy;
    KS_CHECK_NEAR(composed(0, 3), 2.0f, 1e-6);
    KS_CHECK_NEAR(composed(1, 3), 4.0f, 1e-6);
    KS_CHECK_NEAR(composed(2, 3), 6.0f, 1e-6);

    return KS_TEST_RESULT("math_test");
}
