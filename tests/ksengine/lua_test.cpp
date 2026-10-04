#include "KsTest.h"
#include "engine/Scripting/ScriptHost.h"

#include <cstdio>
#include <string>

using ks::scripting::ScriptHost;

int main() {
    auto& host = ScriptHost::instance();

    KS_CHECK(host.initialize());
    KS_CHECK(host.isInitialized());
    KS_CHECK(host.initialize()); // idempotent, not a double-alloc

    if (!host.isInitialized()) {
        // HAS_LUA=0 build: scripting is unavailable by design, but the failure
        // has to be explainable rather than silent.
        KS_CHECK(!host.lastError().empty());
        return KS_TEST_RESULT("lua_test");
    }

    // A statement: eval() falls back to running the code verbatim once
    // `return <code>` fails to parse.
    KS_CHECK(host.eval("x = 41 + 1"));
    double x = 0.0;
    KS_CHECK(host.getNumber("x", x));
    KS_CHECK_NEAR(x, 42.0, 1e-12);
    KS_CHECK(host.lastError().empty());

    // An expression: wrapped as `return <code>` and executed.
    KS_CHECK(host.eval("1 + 2"));
    KS_CHECK(host.lastError().empty());

    // Round trip through a global number slot, which is how the engine hands
    // telemetry in and reads script output back out.
    KS_CHECK(host.setNumber("speed", 3.5));
    KS_CHECK(host.eval("doubled = speed * 2"));
    double doubled = 0.0;
    KS_CHECK(host.getNumber("doubled", doubled));
    KS_CHECK_NEAR(doubled, 7.0, 1e-12);

    // Function hooks: hasFunction() is a quiet poll (safe to call every tick),
    // callFunction() actually runs the global.
    KS_CHECK(host.eval("function add1(v) return v + 1 end"));
    KS_CHECK(host.lastError().empty());
    KS_CHECK(!host.hasFunction("does_not_exist"));
    KS_CHECK(host.lastError().empty());
    KS_CHECK(host.hasFunction("add1"));
    KS_CHECK(host.hasFunction("print")); // standard library is opened
    KS_CHECK(host.callFunction("add1", 1.0));
    KS_CHECK(host.lastError().empty());
    KS_CHECK(!host.callFunction("does_not_exist", 1.0));
    KS_CHECK(!host.lastError().empty());

    // A missing global is reported, not dereferenced.
    double bogus = -1.0;
    KS_CHECK(!host.getNumber("no_such_global", bogus));
    KS_CHECK(!host.lastError().empty());

    // Syntax errors are caught and reported.
    KS_CHECK(!host.eval("this is not lua"));
    KS_CHECK(!host.lastError().empty());

    // Runtime errors propagate their message back out of the pcall.
    KS_CHECK(!host.eval("error('boom')"));
    KS_CHECK(host.lastError().find("boom") != std::string::npos);

    // dofile()-style entry point.
    {
        std::FILE* f = std::fopen("lua_test_tmp.lua", "wb");
        KS_CHECK(f != nullptr);
        if (f) {
            std::fputs("from_file = 99\n", f);
            std::fclose(f);
        }
        KS_CHECK(host.runFile("lua_test_tmp.lua"));
        double fromFile = 0.0;
        KS_CHECK(host.getNumber("from_file", fromFile));
        KS_CHECK_NEAR(fromFile, 99.0, 1e-12);
        std::remove("lua_test_tmp.lua");
    }

    // A missing file is an error, not a crash.
    KS_CHECK(!host.runFile("definitely_missing_script.lua"));
    KS_CHECK(!host.lastError().empty());

    // shutdown() releases the state; further calls fail cleanly.
    host.shutdown();
    KS_CHECK(!host.isInitialized());
    KS_CHECK(!host.eval("x = 1"));
    KS_CHECK(!host.lastError().empty());

    return KS_TEST_RESULT("lua_test");
}
