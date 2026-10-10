#pragma once
#include "KsExport.h"
// Lua Mod SDK (roadmap 3.4) — the engine-facing surface mods program
// against. A mod is a content folder (track/car) whose scripts/*.lua files
// are loaded at content load time; the SDK prelude (ks.on / ks.get /
// ks.set / ks.log / ks.version) is installed into the ScriptHost and
// engine events are dispatched through dispatch().
//
// The C++ side never touches lua_State: everything goes through ScriptHost.

#include <string>
#include <vector>

namespace ks {
namespace scripting {
namespace modsdk {

struct LoadReport {
    std::vector<std::string> loaded; // script paths executed, load order
    std::vector<std::string> errors; // "<path>: <message>" per failed file
    bool ok() const { return errors.empty(); }
};

// Installs the SDK prelude into the script host (initializing it when
// needed). Idempotent. False + ScriptHost::lastError() when HAS_LUA=0.
KSENGINE_API bool install();

// Runs the mod scripts under `modRoot`:
//   modRoot/scripts/*.lua when scripts/ exists, else modRoot/*.lua
// sorted by filename so load order is deterministic. Scripts are
// independent: one failing file is reported but never stops the others.
// A modRoot that is not a directory is an error; having no scripts at all
// is not (mods are optional). Requires (and ensures) install().
KSENGINE_API LoadReport loadModScripts(const std::string& modRoot);

// Dispatches an engine event to every ks.on handler for `event` plus the
// conventional global `on_<event>`. Numbers are passed first, then
// strings. No handler is success — mods opt in by defining one; false
// only when the Lua side raised an error (ScriptHost::lastError()).
KSENGINE_API bool dispatch(const std::string& event, const std::vector<double>& numbers = {},
          const std::vector<std::string>& strings = {});

// ks.log() lines since the last call (ring-bounded to 512 entries).
KSENGINE_API std::vector<std::string> takeLog();

} // namespace modsdk
} // namespace scripting
} // namespace ks
