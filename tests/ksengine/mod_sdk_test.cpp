// Parity 3.4 — Lua Mod SDK: prelude (ks.on/ks.get/ks.set/ks.log/ks.version),
// engine→mod event dispatch (numeric + string args, handler isolation),
// and mod script discovery with deterministic load order and per-file
// error isolation.

#include "KsTest.h"
#include "engine/Scripting/ModSdk.h"
#include "engine/Scripting/ScriptHost.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ks::scripting::ScriptHost;
using ks::scripting::modsdk::dispatch;
using ks::scripting::modsdk::install;
using ks::scripting::modsdk::loadModScripts;
using ks::scripting::modsdk::takeLog;

namespace {

bool writeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << content;
    return static_cast<bool>(out);
}

bool logContains(const std::vector<std::string>& lines, const std::string& needle) {
    for (const std::string& line : lines)
        if (line.find(needle) != std::string::npos) return true;
    return false;
}

} // namespace

int main() {
    // install(): idempotent, explainable when HAS_LUA=0.
    KS_CHECK(install());
    KS_CHECK(install());
    auto& host = ScriptHost::instance();

    if (!host.isInitialized()) {
        // HAS_LUA=0 build: scripting is unavailable by design, but the
        // failure has to be explainable rather than silent.
        KS_CHECK(!host.lastError().empty());
        auto report = loadModScripts(".");
        KS_CHECK(!report.ok());
        KS_CHECK(!report.errors.empty());
        KS_CHECK(dispatch("lap", {1.0})); // no-op when scripting never starts
        return KS_TEST_RESULT("mod_sdk_test");
    }

    // --- SDK prelude: ks.version / ks.set / ks.get round trip -------------
    KS_CHECK(host.eval(
        "if ks.version() == \"ks-modsdk-1\" then ks.set(\"ver_ok\", 1) end"));
    double v = 0.0;
    KS_CHECK(host.getNumber("ver_ok", v));
    KS_CHECK(v == 1.0);

    KS_CHECK(host.eval("ks.set(\"slot\", 42.5); "
                       "pulled = ks.get(\"slot\") + ks.get(\"missing\", 0.5)"));
    double pulled = 0.0;
    KS_CHECK(host.getNumber("pulled", pulled));
    KS_CHECK_NEAR(pulled, 43.0, 1e-12);

    // --- ks.log capture ----------------------------------------------------
    KS_CHECK(host.eval("ks.log(\"boot\")"));
    std::vector<std::string> lines = takeLog();
    KS_CHECK(lines.size() == 1);
    if (!lines.empty()) KS_CHECK(lines.front() == "boot");
    KS_CHECK(takeLog().empty()); // drained, not sticky

    // --- dispatch → ks.on handlers, numeric args in order ------------------
    KS_CHECK(host.eval("ks.on(\"lap\", function(lap, time) "
                       "ks.set(\"seen_lap\", lap) ks.set(\"seen_time\", time) end)"));
    KS_CHECK(dispatch("lap", {7, 91.5}));
    double lap = 0.0, time = 0.0;
    KS_CHECK(host.getNumber("seen_lap", lap));
    KS_CHECK(host.getNumber("seen_time", time));
    KS_CHECK_NEAR(lap, 7.0, 1e-12);
    KS_CHECK_NEAR(time, 91.5, 1e-12);

    // --- conventional on_<name> global still works (dispatcher fallback) --
    KS_CHECK(host.eval("function on_sector(s) ks.set(\"seen_sector\", s) end"));
    KS_CHECK(dispatch("sector", {3}));
    double sector = 0.0;
    KS_CHECK(host.getNumber("seen_sector", sector));
    KS_CHECK_NEAR(sector, 3.0, 1e-12);

    // --- string args arrive after numbers ---------------------------------
    KS_CHECK(host.eval(
        "ks.on(\"penalty\", function(car, reason) ks.set(\"pen_car\", car) "
        "if reason == \"track limits\" then ks.set(\"pen_match\", 1) end end)"));
    KS_CHECK(dispatch("penalty", {2}, {"track limits"}));
    double car = 0.0, match = 0.0;
    KS_CHECK(host.getNumber("pen_car", car));
    KS_CHECK(host.getNumber("pen_match", match));
    KS_CHECK_NEAR(car, 2.0, 1e-12);
    KS_CHECK(match == 1.0);

    // --- every ks.on handler for an event runs ----------------------------
    KS_CHECK(host.eval("ks.on(\"lap\", function() "
                       "ks.set(\"lap_count\", ks.get(\"lap_count\", 0) + 1) end)"));
    KS_CHECK(dispatch("lap", {8, 92.0}));
    KS_CHECK(dispatch("lap", {9, 93.0}));
    double lapCount = 0.0;
    KS_CHECK(host.getNumber("lap_count", lapCount));
    KS_CHECK_NEAR(lapCount, 2.0, 1e-12);

    // --- broken handlers are isolated and logged, not fatal ---------------
    KS_CHECK(host.eval("ks.on(\"bad\", function() error(\"kaboom\") end)"));
    KS_CHECK(dispatch("bad", {})); // dispatcher pcall caught it
    lines = takeLog();
    KS_CHECK(logContains(lines, "kaboom"));
    KS_CHECK(dispatch("lap", {10, 94.0})); // host still healthy afterwards

    // --- event with no handler is a success no-op -------------------------
    KS_CHECK(dispatch("nothing_listens", {1.0}));

    // --- dispatch without the prelude: C-side on_<name> fallback ----------
    host.shutdown();
    KS_CHECK(host.initialize());
    KS_CHECK(!host.hasFunction("__ks_event")); // fresh state, no prelude
    KS_CHECK(host.eval("raw_update = 0.0; function on_update(dt) raw_update = dt end"));
    KS_CHECK(dispatch("update", {0.25}));
    double raw = 0.0;
    KS_CHECK(host.getNumber("raw_update", raw));
    KS_CHECK_NEAR(raw, 0.25, 1e-12);
    KS_CHECK(install()); // prelude back for the loader tests

    // --- loadModScripts: deterministic order, per-file isolation -----------
    const fs::path root = fs::temp_directory_path() / "ks_qtfree_modsdk_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "scripts");
    KS_CHECK(writeFile(root / "scripts" / "01_first.lua",
                       "first_ran = 1\nks.log(\"first\")\n"));
    KS_CHECK(writeFile(root / "scripts" / "02_second.lua",
                       "if first_ran == 1 then ks.set(\"order_ok\", 1) end\n"
                       "function on_lap(lap) ks.set(\"lap_from_mod\", lap) end\n"));
    KS_CHECK(writeFile(root / "scripts" / "99_broken.lua", "this is not lua (\n"));
    KS_CHECK(writeFile(root / "README.md", "not a script"));

    const auto report = loadModScripts(root.string());
    KS_CHECK(!report.ok());
    KS_CHECK(report.loaded.size() == 2);
    KS_CHECK(report.errors.size() == 1);
    if (report.loaded.size() == 2) {
        KS_CHECK(report.loaded[0].find("01_first.lua") != std::string::npos);
        KS_CHECK(report.loaded[1].find("02_second.lua") != std::string::npos);
    }
    if (report.errors.size() == 1)
        KS_CHECK(report.errors[0].find("99_broken.lua") != std::string::npos);

    // 01 ran before 02 despite 99 failing in between (sorted order).
    double orderOk = 0.0;
    KS_CHECK(host.getNumber("order_ok", orderOk));
    KS_CHECK(orderOk == 1.0);

    // The handler 02 registered is live.
    KS_CHECK(dispatch("lap", {5}));
    double modLap = 0.0;
    KS_CHECK(host.getNumber("lap_from_mod", modLap));
    KS_CHECK_NEAR(modLap, 5.0, 1e-12);

    // ks.log calls from loaded scripts are captured too.
    lines = takeLog();
    KS_CHECK(logContains(lines, "first"));

    // --- empty mod (no scripts at all) is not an error --------------------
    const fs::path plain = root / "plain";
    fs::create_directories(plain);
    const auto none = loadModScripts(plain.string());
    KS_CHECK(none.ok());
    KS_CHECK(none.loaded.empty());
    KS_CHECK(none.errors.empty());

    // --- missing root is an error -----------------------------------------
    const auto missing = loadModScripts((root / "nope").string());
    KS_CHECK(!missing.ok());
    KS_CHECK(!missing.errors.empty());

    // --- root-level fallback: no scripts/ dir, .lua at the root ------------
    const fs::path flat = root / "flat";
    fs::create_directories(flat);
    KS_CHECK(writeFile(flat / "main.lua", "ks.set(\"flat_ran\", 1)\n"));
    const auto flatReport = loadModScripts(flat.string());
    KS_CHECK(flatReport.ok());
    KS_CHECK(flatReport.loaded.size() == 1);
    double flatRan = 0.0;
    KS_CHECK(host.getNumber("flat_ran", flatRan));
    KS_CHECK(flatRan == 1.0);

    fs::remove_all(root, ec);
    return KS_TEST_RESULT("mod_sdk_test");
}
