// Roadmap 4.3 — scene bridge: the C API surface a Godot GDExtension host
// uses to mirror the ksengine ECS scene (create/handle safety, transform
// and mesh round-trip, snapshot sizing, revision dirty counter, vehicle
// pose binding).
#include "KsTest.h"
#include "ksengine_c.h"
#include <cmath>
#include <cstring>

namespace {

float horizontalDist(const KsSceneNode& n) {
    return std::sqrt(n.position[0] * n.position[0] + n.position[2] * n.position[2]);
}

} // namespace

int main() {
    KsEngine* eng = ks_engine_create(1);
    KS_CHECK(eng != nullptr);
    if (!eng) return KS_TEST_RESULT("scene_bridge");

    // --- create / alive / name round-trip -------------------------------
    const KsEntity car = ks_engine_scene_create(eng, "car");
    KS_CHECK(car != KS_ENTITY_NULL);
    KS_CHECK(ks_engine_scene_alive(eng, car) == 1);
    KS_CHECK(ks_engine_scene_alive(eng, KS_ENTITY_NULL) == 0);

    KsSceneNode n{};
    KS_CHECK(ks_engine_scene_get(eng, car, &n) == 1);
    KS_CHECK(std::strcmp(n.name, "car") == 0);
    KS_CHECK(std::strcmp(n.mesh, "") == 0);
    KS_CHECK_NEAR(n.scale[0], 1.0f, 1e-6); // identity by default

    // --- revision: mutations bump, reads do not -------------------------
    const uint64_t r0 = ks_engine_scene_revision(eng);
    KS_CHECK(ks_engine_scene_get(eng, car, &n) == 1);
    KS_CHECK(ks_engine_scene_snapshot(eng, nullptr, 0) == 1);
    KS_CHECK(ks_engine_scene_revision(eng) == r0);

    // --- transform round-trip -------------------------------------------
    const float pos[3] = {1.0f, 2.0f, 3.0f};
    const float rot[3] = {0.1f, 0.2f, 0.3f};
    const float scl[3] = {2.0f, 2.0f, 2.0f};
    KS_CHECK(ks_engine_scene_set_transform(eng, car, pos, rot, scl) == 1);
    KS_CHECK(ks_engine_scene_revision(eng) > r0);
    KS_CHECK(ks_engine_scene_get(eng, car, &n) == 1);
    KS_CHECK_NEAR(n.position[0], 1.0f, 1e-6);
    KS_CHECK_NEAR(n.position[1], 2.0f, 1e-6);
    KS_CHECK_NEAR(n.position[2], 3.0f, 1e-6);
    KS_CHECK_NEAR(n.rotation[0], 0.1f, 1e-6);
    KS_CHECK_NEAR(n.rotation[2], 0.3f, 1e-6);
    KS_CHECK_NEAR(n.scale[1], 2.0f, 1e-6);

    // Partial update: NULL arrays keep their component.
    const float pos2[3] = {4.0f, 5.0f, 6.0f};
    KS_CHECK(ks_engine_scene_set_transform(eng, car, pos2, nullptr, nullptr) == 1);
    KS_CHECK(ks_engine_scene_get(eng, car, &n) == 1);
    KS_CHECK_NEAR(n.position[0], 4.0f, 1e-6);
    KS_CHECK_NEAR(n.rotation[0], 0.1f, 1e-6); // untouched

    // --- mesh attach / detach -------------------------------------------
    KS_CHECK(ks_engine_scene_set_mesh(eng, car, "body.kn5") == 1);
    KS_CHECK(ks_engine_scene_get(eng, car, &n) == 1);
    KS_CHECK(std::strcmp(n.mesh, "body.kn5") == 0);
    KS_CHECK(ks_engine_scene_set_mesh(eng, car, nullptr) == 1);
    KS_CHECK(ks_engine_scene_get(eng, car, &n) == 1);
    KS_CHECK(std::strcmp(n.mesh, "") == 0);

    // --- snapshot sizing contract ---------------------------------------
    const KsEntity a = ks_engine_scene_create(eng, "a");
    const KsEntity b = ks_engine_scene_create(eng, "b");
    KS_CHECK(a != KS_ENTITY_NULL && b != KS_ENTITY_NULL);
    KS_CHECK(ks_engine_scene_snapshot(eng, nullptr, 0) == 3);
    KsSceneNode buf[3];
    buf[0].id = 12345u;
    KS_CHECK(ks_engine_scene_snapshot(eng, buf, 2) == 3); // too small
    KS_CHECK(buf[0].id == 12345u);                        // nothing written
    KS_CHECK(ks_engine_scene_snapshot(eng, buf, 3) == 3);
    int foundCar = 0, foundA = 0, foundB = 0;
    for (int i = 0; i < 3; ++i) {
        if (buf[i].id == car) ++foundCar;
        if (buf[i].id == a) ++foundA;
        if (buf[i].id == b) ++foundB;
    }
    KS_CHECK(foundCar == 1 && foundA == 1 && foundB == 1);

    // --- stale handles after destroy + slot reuse ------------------------
    ks_engine_scene_destroy(eng, b);
    KS_CHECK(ks_engine_scene_alive(eng, b) == 0);
    const KsEntity b2 = ks_engine_scene_create(eng, "b2");
    KS_CHECK(b2 != b);                       // generation differs
    KS_CHECK(ks_engine_scene_alive(eng, b) == 0); // no aliasing
    KS_CHECK(ks_engine_scene_snapshot(eng, nullptr, 0) == 3); // car, a, b2
    const uint64_t rDestroy = ks_engine_scene_revision(eng);
    ks_engine_scene_destroy(eng, b);         // stale destroy = no-op
    KS_CHECK(ks_engine_scene_revision(eng) == rDestroy);

    // --- vehicle pose binding --------------------------------------------
    const KsEntity v = ks_engine_scene_create(eng, "vehicle");
    KS_CHECK(ks_engine_scene_bind_vehicle(eng, v) == 1);
    KS_CHECK(ks_engine_scene_bind_vehicle(eng, b) == 0); // stale handle
    KsInput in{};
    in.throttle = 1.0f;
    ks_engine_set_input(eng, &in);
    const uint64_t rDrive = ks_engine_scene_revision(eng);
    for (int i = 0; i < 100; ++i) ks_engine_step(eng, 0.01); // 1 s
    KsVehicleState st{};
    ks_engine_get_state(eng, &st);
    KS_CHECK(st.speed_ms > 0.5f);
    KS_CHECK(ks_engine_scene_revision(eng) > rDrive);
    KS_CHECK(ks_engine_scene_get(eng, v, &n) == 1);
    KS_CHECK(horizontalDist(n) > 0.01);      // pose mirrored into the node

    // Unbind: steps no longer touch the scene.
    KS_CHECK(ks_engine_scene_bind_vehicle(eng, KS_ENTITY_NULL) == 1);
    const uint64_t rUnbound = ks_engine_scene_revision(eng);
    ks_engine_step(eng, 0.01);
    KS_CHECK(ks_engine_scene_revision(eng) == rUnbound);

    // Destroying the bound entity unbinds safely (no dangling write).
    KS_CHECK(ks_engine_scene_bind_vehicle(eng, v) == 1);
    ks_engine_scene_destroy(eng, v);
    KS_CHECK(ks_engine_scene_alive(eng, v) == 0);
    const uint64_t rAfter = ks_engine_scene_revision(eng);
    ks_engine_step(eng, 0.01);
    KS_CHECK(ks_engine_scene_revision(eng) == rAfter);

    // --- ks_engine_get_state completion (declared fields now filled) -----
    KS_CHECK(std::isfinite(st.yaw));
    KS_CHECK_NEAR(st.fuel_l, 100.0f, 1.0f);
    KS_CHECK_NEAR(st.tyre_temp_fl, 30.0f, 1.0f);

    ks_engine_destroy(eng);
    return KS_TEST_RESULT("scene_bridge");
}
