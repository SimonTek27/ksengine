#include "ScriptHost.h"
#include "luaScript/LuaScriptHost.h"

namespace ks { namespace scripting {

bool ScriptHost::initialize() { return LuaScriptHost::instance().initialize(); }

void ScriptHost::shutdown() { LuaScriptHost::instance().shutdown(); }

bool ScriptHost::isInitialized() const { return LuaScriptHost::instance().isInitialized(); }

bool ScriptHost::eval(const std::string& code) { return LuaScriptHost::instance().eval(code); }

bool ScriptHost::runFile(const std::string& path) { return LuaScriptHost::instance().runFile(path); }

bool ScriptHost::getNumber(const std::string& name, double& out) const {
    return LuaScriptHost::instance().getNumber(name, out);
}

bool ScriptHost::setNumber(const std::string& name, double value) {
    return LuaScriptHost::instance().setNumber(name, value);
}

const std::string& ScriptHost::lastError() const { return LuaScriptHost::instance().lastError(); }

bool ScriptHost::hasFunction(const std::string& name) const {
    return LuaScriptHost::instance().hasFunction(name);
}

bool ScriptHost::callFunction(const std::string& name, double arg) {
    return LuaScriptHost::instance().callFunction(name, arg);
}

bool ScriptHost::callFunction(const std::string& name, const std::vector<double>& args) {
    return LuaScriptHost::instance().callFunction(name, args);
}

bool ScriptHost::emitEvent(const std::string& name,
                           const std::vector<double>& numbers,
                           const std::vector<std::string>& strings) {
    return LuaScriptHost::instance().emitEvent(name, numbers, strings);
}

std::vector<std::string> ScriptHost::takeLog() {
    return LuaScriptHost::instance().takeLog();
}

}} // namespace ks::scripting
