# Plugins

ksEditor uses a plugin-based architecture to support multiple simulators, with Kinetika as the primary implementation.

## Architecture

```
src/plugins/
├── base/
│   └── PluginBase.h          Plugin interface + PluginManagerBase
├── simulators/
│   └── kunos/                Kinetika plugin (legacy path)
│       ├── KsPlugin.h/.cpp   Main plugin class
│       └── assettocorsa/     Full Kinetika integration (50+ files)
└── Plugins.h                 Convenience header
```

## Plugin Interface

```cpp
class PluginBase {
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual QStringList supportedFileExtensions() const = 0;
    virtual QStringList supportedContentTypes() const = 0;
};
```

## Plugin Manager

**`src/core/sys/PluginManager.h` / `.cpp`**

Manages plugin lifecycle: discovery, loading, unloading, enable/disable. Supports native (Qt plugin) and script-based plugins.

**Key features:**
- `scan()` — Scan plugin directory for available plugins
- `loadPlugin()` / `unloadPlugin()` — Runtime load/unload
- Plugin info tracking (name, version, author, state)
- Importer/exporter registration per plugin
- Settings persistence per plugin
- Event signals: loaded, unloaded, error, scanComplete

## Kinetika Plugin

The primary simulator plugin provides full Kinetika integration:
- Car/track/content discovery via `KsContentPaths`
- KN5 model parsing and decryption
- INI configuration parsing (`KsIni`)
- Shared memory telemetry reading
- Workshop module integration
- Custom Shaders Patch (CSP) support
- Assets library & content browser
- Setup comparison tools

## Adding a New Simulator

1. Create `src/plugins/simulators/<name>/`
2. Implement `<Name>Plugin` inheriting `PluginBase`
3. Register in `SDKBackend::initialize()`
4. Add to `CMakeLists.txt`
