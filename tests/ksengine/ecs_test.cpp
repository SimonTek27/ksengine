#include "KsTest.h"
#include "Engine.h"
#include "engine/scene/Components.h"
#include "engine/scene/Registry.h"
#include "engine/scene/SceneModule.h"

#include <memory>
#include <string>

using namespace ks;

int main() {
    ecs::Registry reg;
    KS_CHECK(reg.alive() == 0);

    const ecs::Entity a = reg.create();
    const ecs::Entity b = reg.create();
    KS_CHECK(reg.valid(a) && reg.valid(b));
    KS_CHECK(a != b);
    KS_CHECK(reg.alive() == 2);

    reg.emplace<ecs::Name>(a, ecs::Name{"alpha"});
    reg.emplace<ecs::Transform>(a);
    reg.emplace<ecs::MeshInstance>(a, ecs::MeshInstance{"car_01"});
    reg.emplace<ecs::Transform>(b);

    KS_CHECK(reg.has<ecs::Name>(a));
    KS_CHECK(!reg.has<ecs::Name>(b));
    KS_CHECK(reg.has<ecs::Transform>(a) && reg.has<ecs::Transform>(b));
    KS_CHECK(reg.tryGet<ecs::MeshInstance>(a) != nullptr);
    KS_CHECK(reg.tryGet<ecs::MeshInstance>(b) == nullptr);
    KS_CHECK(reg.get<ecs::Name>(a).value == "alpha");

    int seen = 0;
    reg.each<ecs::Transform>([&](ecs::Entity, ecs::Transform&) { ++seen; });
    KS_CHECK(seen == 2);

    seen = 0;
    reg.each<ecs::Transform, ks::ecs::MeshInstance>(
        [&](ecs::Entity, ecs::Transform&, ecs::MeshInstance&) { ++seen; });
    KS_CHECK(seen == 1);

    seen = 0;
    reg.each<ecs::Transform, ecs::Name>(
        [&](ecs::Entity, ecs::Transform&, ecs::Name&) { ++seen; });
    KS_CHECK(seen == 1);

    reg.get<ecs::Transform>(a).position = {1.0f, 2.0f, 3.0f};
    KS_CHECK(reg.get<ecs::Transform>(a).position.x == 1.0f);

    const ecs::Entity stale = a;
    reg.destroy(a);
    KS_CHECK(!reg.valid(stale));
    KS_CHECK(!reg.has<ecs::Transform>(stale));
    KS_CHECK(!reg.has<ecs::Name>(stale));
    KS_CHECK(reg.alive() == 1);

    const ecs::Entity recycled = reg.create();
    KS_CHECK(reg.valid(recycled));
    KS_CHECK(recycled != stale);
    KS_CHECK(ecs::entityIndex(recycled) == ecs::entityIndex(stale));
    KS_CHECK(ecs::entityGeneration(recycled) != ecs::entityGeneration(stale));
    KS_CHECK(!reg.has<ecs::Transform>(recycled));
    KS_CHECK(!reg.has<ecs::Name>(recycled));
    KS_CHECK(reg.alive() == 2);

    const ecs::Registry& cref = reg;
    int cseen = 0;
    cref.each<ecs::Transform>([&](ecs::Entity, const ecs::Transform&) { ++cseen; });
    KS_CHECK(cseen == 1);

    cseen = 0;
    cref.each<ecs::Transform, ecs::Name>(
        [&](ecs::Entity, const ecs::Transform&, const ecs::Name&) { ++cseen; });
    KS_CHECK(cseen == 0);

    ecs::Registry r2;
    const ecs::Entity e1 = r2.create();
    const ecs::Entity e2 = r2.create();
    const ecs::Entity e3 = r2.create();
    r2.emplace<ecs::Transform>(e1).position = {1, 0, 0};
    r2.emplace<ecs::Transform>(e2).position = {2, 0, 0};
    r2.emplace<ecs::Transform>(e3).position = {3, 0, 0};
    r2.remove<ecs::Transform>(e2);
    KS_CHECK(!r2.has<ecs::Transform>(e2));
    KS_CHECK(r2.has<ecs::Transform>(e1) && r2.has<ecs::Transform>(e3));
    KS_CHECK(r2.get<ecs::Transform>(e1).position.x == 1.0f);
    KS_CHECK(r2.get<ecs::Transform>(e3).position.x == 3.0f);

    int n = 0;
    r2.each<ecs::Transform>([&](ecs::Entity id, ecs::Transform& t) {
        ++n;
        KS_CHECK(id == e1 || id == e3);
        KS_CHECK(t.position.x == 1.0f || t.position.x == 3.0f);
    });
    KS_CHECK(n == 2);

    r2.emplace<ecs::Transform>(e2).position = {9, 0, 0};
    KS_CHECK(r2.has<ecs::Transform>(e2));
    KS_CHECK(r2.get<ecs::Transform>(e2).position.x == 9.0f);

    ecs::Transform def;
    const math::mat4 wm = ecs::worldMatrix(def);
    KS_CHECK(wm(0, 0) == 1.0f && wm(1, 1) == 1.0f && wm(2, 2) == 1.0f);
    KS_CHECK(wm(0, 3) == 0.0f && wm(1, 3) == 0.0f && wm(2, 3) == 0.0f);
    def.position = {5, 0, 0};
    KS_CHECK(ecs::worldMatrix(def)(0, 3) == 5.0f);

    ecs::SceneModule sm;
    sm.attach(&reg);
    int calls = 0;
    KS_CHECK(sm.addSystem("s1", [&](ecs::Registry& r, double) {
        calls += static_cast<int>(r.alive());
    }));
    KS_CHECK(sm.systemCount() == 1);
    KS_CHECK(sm.hasSystem("s1"));
    sm.addSystem("s1", [&](ecs::Registry&, double) { calls += 10; });
    KS_CHECK(sm.systemCount() == 1);
    sm.update(0.016);
    KS_CHECK(calls == 10);
    sm.removeSystem("s1");
    KS_CHECK(sm.systemCount() == 0);
    sm.update(0.016);
    KS_CHECK(calls == 10);

    auto& engine = Engine::instance();
    engine.shutdown();
    engine.initialize();
    engine.setFixedDt(1.0 / 120.0);
    const ecs::Entity en = engine.createEntity("root");
    KS_CHECK(engine.registry().valid(en));
    KS_CHECK(engine.registry().has<ecs::Name>(en));
    KS_CHECK(engine.registry().get<ecs::Name>(en).value == "root");
    engine.destroyEntity(en);
    KS_CHECK(!engine.registry().valid(en));
    KS_CHECK(engine.registry().alive() == 0);

    sm.attach(&engine.registry());
    sm.addSystem("tick", [&](ecs::Registry&, double) { calls += 100; });
    engine.registerModule(
        "ks.scene", std::shared_ptr<EngineModule>(&sm, [](EngineModule*) {}));
    engine.start();
    engine.tick(0.02);
    KS_CHECK(engine.tickCount() == 2);
    KS_CHECK(calls == 210);
    engine.stop();
    engine.tick(1.0);
    KS_CHECK(engine.tickCount() == 2);
    KS_CHECK(calls == 210);
    engine.unregisterModule("ks.scene");
    sm.clearSystems();
    sm.attach(nullptr);
    engine.shutdown();
    KS_CHECK(!engine.isInitialized());

    return KS_TEST_RESULT("ecs_test");
}
