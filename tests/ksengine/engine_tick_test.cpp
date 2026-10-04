#include "KsTest.h"
#include "Engine.h"

#include <memory>
#include <string>
#include <vector>

namespace {

struct CountingModule : ks::EngineModule {
    explicit CountingModule(std::string id, std::vector<int>* log, ks::Engine* reenter = nullptr)
        : m_id(std::move(id)), m_log(log), m_reenter(reenter) {}

    std::string moduleName() const override { return m_id; }

    void update(double dt) override {
        if (m_log) m_log->push_back(static_cast<int>(dt * 1000000.0 + 0.5));
        if (m_reenter) {
            ++reentries;
            m_reenter->tick(1.0);
        }
    }

    static int reentries;

private:
    std::string m_id;
    std::vector<int>* m_log = nullptr;
    ks::Engine* m_reenter = nullptr;
};

int CountingModule::reentries = 0;

} // namespace

int main() {
    auto& engine = ks::Engine::instance();
    engine.shutdown();
    KS_CHECK(engine.initialize());
    KS_CHECK(engine.isInitialized());
    engine.setFixedDt(0.0625);
    KS_CHECK_NEAR(engine.fixedDt(), 0.0625, 1e-12);

    std::vector<int> log;
    auto a = std::make_shared<CountingModule>("a", &log);
    auto b = std::make_shared<CountingModule>("b", &log);
    engine.registerModule("a", a);
    engine.registerModule("b", b);
    KS_CHECK(engine.tickCount() == 0);

    engine.start();
    engine.tick(0.2);
    KS_CHECK(engine.tickCount() == 3);
    KS_CHECK(log.size() == 6);
    for (int v : log) KS_CHECK(v == 62500);

    engine.tick(100.0);
    KS_CHECK(engine.tickCount() == 11);

    engine.stop();
    const uint64_t frozen = engine.tickCount();
    engine.tick(1.0);
    KS_CHECK(engine.tickCount() == frozen);

    engine.start();
    engine.unregisterModule("a");
    log.clear();
    engine.tick(0.125);
    KS_CHECK(log.size() == 2);

    CountingModule::reentries = 0;
    engine.registerModule(
        "reentrant", std::make_shared<CountingModule>("reentrant", &log, &engine));
    log.clear();
    engine.tick(0.125);
    KS_CHECK(CountingModule::reentries == 2);
    KS_CHECK(engine.tickCount() == frozen + 4);

    engine.stop();
    engine.unregisterModule("b");
    engine.unregisterModule("reentrant");
    KS_CHECK(engine.tickCount() > 0);
    engine.shutdown();
    KS_CHECK(!engine.isInitialized());

    return KS_TEST_RESULT("engine_tick_test");
}
