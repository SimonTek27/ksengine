#include "ModSdk.h"

#include "ScriptHost.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace ks {
namespace scripting {
namespace modsdk {
namespace {

// SDK prelude, pure Lua on top of the C-backed ks.log registered by
// LuaScriptHost::initialize(). Idempotent: re-running it never clears
// registered handlers (install() also short-circuits on __ks_event).
//
// __ks_event is the dispatcher emitEvent() prefers: it fans an event out
// to every ks.on handler for it plus the conventional on_<name> global,
// isolating each call in pcall so a broken mod logs an error instead of
// taking the simulator down.
const char kPrelude[] = R"lua(
ks = ks or {}
ks._handlers = ks._handlers or {}
function ks.on(name, fn)
  if type(name) ~= "string" or type(fn) ~= "function" then
    error("ks.on(name, fn): bad arguments")
  end
  local list = ks._handlers[name]
  if not list then list = {} ks._handlers[name] = list end
  list[#list + 1] = fn
end
function ks.get(name, default)
  local v = rawget(_G, name)
  if v == nil then return default or 0 end
  return v
end
function ks.set(name, value) _G[name] = value end
function ks.version() return "ks-modsdk-1" end
ks.log = ks.log or function() end
function __ks_event(name, ...)
  local list = ks._handlers[name]
  if list then
    for i = 1, #list do
      local ok, err = pcall(list[i], ...)
      if not ok then ks.log("handler '" .. name .. "': " .. tostring(err)) end
    end
  end
  local f = rawget(_G, "on_" .. name)
  if f then
    local ok, err = pcall(f, ...)
    if not ok then ks.log("on_" .. name .. ": " .. tostring(err)) end
  end
end
)lua";

} // namespace

bool install() {
    auto& host = ScriptHost::instance();
    if (!host.initialize()) return false;
    if (host.hasFunction("__ks_event")) return true; // prelude already loaded
    return host.eval(kPrelude);
}

LoadReport loadModScripts(const std::string& modRoot) {
    LoadReport report;
    if (!install()) {
        report.errors.push_back(modRoot + ": scripting unavailable: " +
                                ScriptHost::instance().lastError());
        return report;
    }

    std::error_code ec;
    if (!fs::is_directory(modRoot, ec)) {
        report.errors.push_back(modRoot + ": not a directory");
        return report;
    }

    // Preferred layout is a scripts/ directory; tolerate mods that drop
    // their .lua files at the folder root instead.
    fs::path scriptDir = fs::path(modRoot) / "scripts";
    if (!fs::is_directory(scriptDir, ec))
        scriptDir = modRoot;

    std::vector<fs::path> scripts;
    for (fs::directory_iterator it(scriptDir, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".lua") scripts.push_back(it->path());
    }
    std::sort(scripts.begin(), scripts.end());

    auto& host = ScriptHost::instance();
    for (const fs::path& script : scripts) {
        if (host.runFile(script.string()))
            report.loaded.push_back(script.string());
        else
            report.errors.push_back(script.string() + ": " + host.lastError());
    }
    return report;
}

bool dispatch(const std::string& event, const std::vector<double>& numbers,
          const std::vector<std::string>& strings) {
    return ScriptHost::instance().emitEvent(event, numbers, strings);
}

std::vector<std::string> takeLog() {
    return ScriptHost::instance().takeLog();
}

} // namespace modsdk
} // namespace scripting
} // namespace ks
