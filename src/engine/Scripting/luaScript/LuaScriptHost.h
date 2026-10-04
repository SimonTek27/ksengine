#pragma once

#include <string>
#include <vector>

// Opaque, declared here so lua.h never has to leak into the public engine
// interface — the state is created, used and destroyed entirely inside
// LuaScriptHost.cpp.
struct lua_State;

namespace ks { namespace scripting {

// Real Lua-backed script host. Owns a single lua_State for the process,
// with the standard libraries opened, and exposes three operations:
//
//   initialize()  create the state (idempotent; false when HAS_LUA=0)
//   eval(code)    run a chunk, or an expression — `1+2` and `x = 1` both
//                 work because the chunk is first tried as `return <code>`
//   runFile(path) load and run a file, like dofile()
//
// Failures never throw: they leave the state untouched, record the message
// in lastError() and return false, so a broken script cannot take the
// simulator down with it.
class LuaScriptHost {
public:
    static LuaScriptHost& instance() { static LuaScriptHost s; return s; }

    LuaScriptHost() = default;
    ~LuaScriptHost();

    LuaScriptHost(const LuaScriptHost&) = delete;
    LuaScriptHost& operator=(const LuaScriptHost&) = delete;

    bool initialize();
    void shutdown();
    bool isInitialized() const;

    bool eval(const std::string& code);
    bool runFile(const std::string& path);

    // Global number slot access — the minimal binding surface a sim needs to
    // hand telemetry in and read script output back out without pushing raw
    // Lua types through the engine.
    bool getNumber(const std::string& name, double& out) const;
    bool setNumber(const std::string& name, double value);

    // True when `name` is a global holding a function. Never touches
    // lastError(), so it is safe to poll every tick looking for a hook.
    bool hasFunction(const std::string& name) const;

    // Calls the global function `name` with a single numeric argument.
    bool callFunction(const std::string& name, double arg);

    // Calls the global function `name` with numeric arguments.
    bool callFunction(const std::string& name, const std::vector<double>& args);

    // Mod SDK event dispatch: routes to the SDK dispatcher `__ks_event(name,
    // ...)` when the prelude from ModSdk.cpp is loaded, otherwise falls back
    // to the conventional global `on_<name>(...)`. Numbers are pushed first,
    // then strings. No handler at all is success — mods opt in by defining
    // one; false only when the Lua side raised an error (see lastError()).
    // With no state (scripting never started / HAS_LUA=0) it is a no-op.
    bool emitEvent(const std::string& name,
                   const std::vector<double>& numbers = {},
                   const std::vector<std::string>& strings = {});

    // Lines collected from the `ks.log()` API since the last call
    // (ring-bounded to 512 entries, oldest dropped).
    std::vector<std::string> takeLog();

    const std::string& lastError() const { return m_lastError; }

    lua_State* state() { return m_state; }

private:
    bool report(int status);

    // C binding registered as `ks.log` at initialize(); keeps the buffer
    // ring-bounded so a chatty mod cannot grow it without limit.
    static int l_log(lua_State* L);

    lua_State* m_state = nullptr;
    mutable std::string m_lastError;
    std::vector<std::string> m_log;
};

}} // namespace ks::scripting
