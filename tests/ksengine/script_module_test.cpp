#include "KsTest.h"
#include "engine/Scripting/ScriptHost.h"
#include "engine/Scripting/ScriptModule.h"

#include <cstdio>

using ks::scripting::ScriptHost;
using ks::scripting::ScriptModule;

int main() {
    auto& mod = ScriptModule::instance();

    KS_CHECK(mod.moduleId() == "ks.script");
    KS_CHECK(mod.moduleName() == "ScriptModule");
    // Scripting must run after gameplay modules, not interleaved with them.
    KS_CHECK(mod.priority() > 0);

    // update() before initialize() is a no-op rather than a crash.
    mod.update(1.0);
    KS_CHECK(!ScriptHost::instance().isInitialized());

    // Startup scripts queued before initialize() run inside it.
    const char* path = "script_module_tmp.lua";
    std::FILE* f = std::fopen(path, "wb");
    KS_CHECK(f != nullptr);
    if (f) {
        std::fputs("calls = 0\n"
                   "function on_update(dt) calls = calls + dt end\n",
                   f);
        std::fclose(f);
    }
    mod.loadFile(path);

    KS_CHECK(mod.initialize());
    if (!mod.isInitialized()) {
        // HAS_LUA=0 build: unavailable by design, but explainably so.
        KS_CHECK(!ScriptHost::instance().lastError().empty());
        std::remove(path);
        return KS_TEST_RESULT("script_module_test");
    }
    KS_CHECK(mod.initialize()); // idempotent

    auto& host = ScriptHost::instance();
    double calls = -1.0;
    KS_CHECK(host.getNumber("calls", calls));
    KS_CHECK_NEAR(calls, 0.0, 1e-12);

    mod.update(0.5);
    mod.update(0.25);
    KS_CHECK(host.getNumber("calls", calls));
    KS_CHECK_NEAR(calls, 0.75, 1e-12);

    // dt of the most recent tick is published for scripts to read.
    double dt = -1.0;
    KS_CHECK(host.getNumber("delta_time", dt));
    KS_CHECK_NEAR(dt, 0.25, 1e-12);

    std::remove(path);

    mod.shutdown();
    KS_CHECK(!mod.isInitialized());
    KS_CHECK(!host.isInitialized());

    return KS_TEST_RESULT("script_module_test");
}
