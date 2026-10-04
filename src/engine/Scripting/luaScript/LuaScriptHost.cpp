#include "LuaScriptHost.h"

// Defensive: ksengine always publishes HAS_LUA through its PUBLIC compile
// definitions, but a TU that reaches this file without them should read as
// "no Lua" rather than trip a warning about an undefined macro.
#ifndef HAS_LUA
#define HAS_LUA 0
#endif

#if HAS_LUA
// Lua's headers are written for C and carry no `extern "C"` guard of their
// own, so without this wrapper every api call would be name-mangled here and
// never match the symbols in ksengine_lua.
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#endif

#include <cstdio>

namespace ks { namespace scripting {

#if HAS_LUA
namespace {
// Wrap a chunk as `return <code>` so eval() accepts plain expressions
// (`1+2`, `speed * 2`) as well as statements. Anything that still fails to
// parse is retried verbatim, which is how `x = 1` gets through.
int loadAsExpression(lua_State* L, const std::string& code) {
    const std::string wrapped = "return " + code;
    if (luaL_loadbuffer(L, wrapped.c_str(), wrapped.size(), "eval") == LUA_OK) return LUA_OK;
    lua_pop(L, 1);
    return luaL_loadbuffer(L, code.c_str(), code.size(), "eval");
}
} // namespace
#endif

LuaScriptHost::~LuaScriptHost() { shutdown(); }

bool LuaScriptHost::initialize() {
#if HAS_LUA
    if (m_state) return true;
    m_state = luaL_newstate();
    if (!m_state) {
        m_lastError = "luaL_newstate() returned null";
        return false;
    }
    luaL_openlibs(m_state);
    // Mod SDK `ks` table: the C-backed half (ks.log) lives here; the rest
    // (ks.on / ks.get / ks.set / ks.version / __ks_event) is added by the
    // pure-Lua prelude in ModSdk.cpp.
    lua_newtable(m_state);
    lua_pushcfunction(m_state, &LuaScriptHost::l_log);
    lua_setfield(m_state, -2, "log");
    lua_setglobal(m_state, "ks");
    m_lastError.clear();
    return true;
#else
    m_lastError = "Lua support was not compiled in (HAS_LUA=0)";
    return false;
#endif
}

void LuaScriptHost::shutdown() {
#if HAS_LUA
    if (m_state) {
        lua_close(m_state);
        m_state = nullptr;
    }
#endif
}

bool LuaScriptHost::isInitialized() const {
#if HAS_LUA
    return m_state != nullptr;
#else
    return false;
#endif
}

bool LuaScriptHost::report(int status) {
#if HAS_LUA
    if (status == LUA_OK) {
        m_lastError.clear();
        return true;
    }
    const char* msg = lua_tostring(m_state, -1);
    m_lastError = msg ? msg : "unknown Lua error";
    lua_pop(m_state, 1);
    return false;
#else
    (void)status;
    return false;
#endif
}

bool LuaScriptHost::eval(const std::string& code) {
#if HAS_LUA
    if (!m_state) {
        m_lastError = "script host not initialized";
        return false;
    }
    if (!report(loadAsExpression(m_state, code))) return false;
    return report(lua_pcall(m_state, 0, 0, 0));
#else
    (void)code;
    m_lastError = "Lua support was not compiled in (HAS_LUA=0)";
    return false;
#endif
}

bool LuaScriptHost::runFile(const std::string& path) {
#if HAS_LUA
    if (!m_state) {
        m_lastError = "script host not initialized";
        return false;
    }
    if (!report(luaL_loadfile(m_state, path.c_str()))) return false;
    return report(lua_pcall(m_state, 0, 0, 0));
#else
    (void)path;
    m_lastError = "Lua support was not compiled in (HAS_LUA=0)";
    return false;
#endif
}

bool LuaScriptHost::getNumber(const std::string& name, double& out) const {
#if HAS_LUA
    if (!m_state) {
        m_lastError = "script host not initialized";
        return false;
    }
    lua_getglobal(m_state, name.c_str());
    const bool ok = lua_isnumber(m_state, -1) != 0;
    if (ok) out = lua_tonumber(m_state, -1);
    else m_lastError = "global '" + name + "' is not a number";
    lua_pop(m_state, 1);
    return ok;
#else
    (void)name;
    (void)out;
    m_lastError = "Lua support was not compiled in (HAS_LUA=0)";
    return false;
#endif
}

bool LuaScriptHost::setNumber(const std::string& name, double value) {
#if HAS_LUA
    if (!m_state) {
        m_lastError = "script host not initialized";
        return false;
    }
    lua_pushnumber(m_state, value);
    lua_setglobal(m_state, name.c_str());
    m_lastError.clear();
    return true;
#else
    (void)name;
    (void)value;
    m_lastError = "Lua support was not compiled in (HAS_LUA=0)";
    return false;
#endif
}

bool LuaScriptHost::hasFunction(const std::string& name) const {
#if HAS_LUA
    if (!m_state) return false;
    lua_getglobal(m_state, name.c_str());
    const bool ok = lua_isfunction(m_state, -1) != 0;
    lua_pop(m_state, 1);
    return ok;
#else
    (void)name;
    return false;
#endif
}

bool LuaScriptHost::callFunction(const std::string& name, double arg) {
    return callFunction(name, std::vector<double>{arg});
}

bool LuaScriptHost::callFunction(const std::string& name,
                                 const std::vector<double>& args) {
#if HAS_LUA
    if (!m_state) {
        m_lastError = "script host not initialized";
        return false;
    }
    lua_getglobal(m_state, name.c_str());
    if (!lua_isfunction(m_state, -1)) {
        m_lastError = "global '" + name + "' is not a function";
        lua_pop(m_state, 1);
        return false;
    }
    for (double value : args)
        lua_pushnumber(m_state, value);
    return report(lua_pcall(m_state, static_cast<int>(args.size()), 0, 0));
#else
    (void)name;
    (void)args;
    m_lastError = "Lua support was not compiled in (HAS_LUA=0)";
    return false;
#endif
}

bool LuaScriptHost::emitEvent(const std::string& name,
                              const std::vector<double>& numbers,
                              const std::vector<std::string>& strings) {
#if HAS_LUA
    if (!m_state) return true; // scripting not started: nothing to dispatch
    const auto pushArgs = [&]() {
        for (double value : numbers)
            lua_pushnumber(m_state, value);
        for (const std::string& value : strings)
            lua_pushlstring(m_state, value.data(), value.size());
    };
    const int nargs = static_cast<int>(numbers.size() + strings.size());

    // Prefer the SDK dispatcher when the prelude from ModSdk.cpp is loaded:
    // it fans the event out to every ks.on handler plus on_<name>.
    lua_getglobal(m_state, "__ks_event");
    if (lua_isfunction(m_state, -1)) {
        lua_pushlstring(m_state, name.data(), name.size());
        pushArgs();
        return report(lua_pcall(m_state, nargs + 1, 0, 0));
    }
    lua_pop(m_state, 1);

    // Plain scripts without the prelude: the conventional global hook.
    const std::string hook = "on_" + name;
    lua_getglobal(m_state, hook.c_str());
    if (!lua_isfunction(m_state, -1)) {
        lua_pop(m_state, 1);
        return true; // no handler registered — not an error
    }
    pushArgs();
    return report(lua_pcall(m_state, nargs, 0, 0));
#else
    (void)name;
    (void)numbers;
    (void)strings;
    return true;
#endif
}

std::vector<std::string> LuaScriptHost::takeLog() {
    std::vector<std::string> out;
    out.swap(m_log);
    return out;
}

int LuaScriptHost::l_log(lua_State* L) {
#if HAS_LUA
    auto& self = LuaScriptHost::instance();
    const char* msg = luaL_optstring(L, 1, "");
    self.m_log.emplace_back(msg ? msg : "");
    constexpr std::size_t kMaxLogLines = 512;
    if (self.m_log.size() > kMaxLogLines)
        self.m_log.erase(self.m_log.begin(),
                         self.m_log.begin() +
                             static_cast<std::ptrdiff_t>(self.m_log.size() -
                                                         kMaxLogLines));
    return 0;
#else
    (void)L;
    return 0;
#endif
}

}} // namespace ks::scripting
