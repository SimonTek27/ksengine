#pragma once
// ks::Engine module for scripting: owns the Lua lifecycle (initialize the
// host, run the queued startup scripts, release on shutdown) and pumps the
// per-tick update event at the fixed engine timestep.
//
// Header-only and a Meyers singleton, matching SceneModule.
#include "../EngineModule.h"
#include "ModSdk.h"
#include "ScriptHost.h"
#include <string>
#include <vector>

namespace ks::scripting {

class ScriptModule : public ::ks::EngineModule {
public:
    static ScriptModule& instance() {
        static ScriptModule s;
        return s;
    }

    std::string moduleName() const override { return "ScriptModule"; }
    std::string moduleId() const override { return "ks.script"; }

    // Queues a script file to run when the module initializes. Files loaded
    // after initialization run immediately.
    void loadFile(const std::string& path) {
        if (m_initialized) ScriptHost::instance().runFile(path);
        else m_pending.push_back(path);
    }

    bool eval(const std::string& code) { return ScriptHost::instance().eval(code); }

    bool initialize() override {
        if (m_initialized) return true;
        if (!ScriptHost::instance().initialize()) return false;
        // Mod SDK prelude (ks.on/ks.get/...) goes in before any queued
        // startup script so mods can register handlers in their body.
        if (!modsdk::install()) return false;
        for (const std::string& path : m_pending)
            ScriptHost::instance().runFile(path); // failure lands in lastError()
        m_pending.clear();
        m_initialized = true;
        return true;
    }

    void shutdown() override {
        ScriptHost::instance().shutdown();
        m_initialized = false;
    }

    void update(double dt) override {
        if (!m_initialized) return;
        auto& host = ScriptHost::instance();
        host.setNumber("delta_time", dt);
        // SDK event "update": every ks.on("update") handler, then the
        // conventional on_update global (both inside the dispatcher).
        host.emitEvent("update", {dt});
    }

    // Scripting observes simulation state, so it runs after gameplay modules.
    int priority() const override { return 100; }

private:
    std::vector<std::string> m_pending;
};

} // namespace ks::scripting
