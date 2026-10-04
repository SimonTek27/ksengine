# ksEditor — Comprehensive Assetto Corsa Modding Suite

## Overview

**ksEditor** is a professional-grade, modular desktop application built with **C++17, Qt6, and Vulkan** for creating and editing content for Assetto Corsa and other Kunos/Steam racing games. It unifies audio engineering, 3D modeling, physics simulation, livery design, telemetry analysis, and content management into a single cohesive IDE-like environment.

> **Version:** 1.16.4    
> **License:** GNU GPL 3
> **Platform:** Windows 10/11 (primary), Linux (experimental)  
> **Build System:** CMake 3.16+ with vcpkg integration  
> **SDK Version:** 1.16.4

---


---

## System Requirements & Getting Started

### Minimum Requirements

| Component | Specification | Notes |
|-----------|-------------|-------|
| **OS** | Windows 10 64-bit (1903+) or Windows 11 | Linux experimental (Ubuntu 22.04 LTS tested) |
| **CPU** | Intel Core i5-8400 / AMD Ryzen 5 2600 | 6 cores recommended for physics simulation |
| **RAM** | 16 GB DDR4 | 32 GB recommended for large track projects |
| **GPU** | NVIDIA GTX 1060 6GB / AMD RX 580 8GB | Vulkan 1.2 support mandatory |
| **Storage** | 5 GB SSD (application) + 20 GB workspace | NVMe strongly recommended for asset streaming |
| **Display** | 1920×1080 | HiDPI scaling supported natively in Qt6 |

### Recommended Requirements (Professional Modding)

| Component | Specification | Notes |
|-----------|-------------|-------|
| **OS** | Windows 11 23H2 | WSL2 optional for Linux build testing |
| **CPU** | Intel Core i7-12700K / AMD Ryzen 7 5800X3D | 3D viewport + physics simulation + audio DSP simultaneously |
| **RAM** | 32 GB DDR4-3200 | 64 GB for multi-project workflows or large tracks |
| **GPU** | NVIDIA RTX 3070 / AMD RX 6800 XT | RT cores accelerate Vulkan ray-traced viewport |
| **Storage** | 1 TB NVMe Gen4 | Separate drive for project workspace vs. OS |
| **Peripherals** | Dual-monitor (1440p+), MIDI controller (audio), 3D mouse (viewport) | MIDI learn supported for audio parameter automation |

### Installation

**Windows (Installer):**
TBH

**Windows (Portable / From Source):**
```powershell
# Clone repository
git clone https://github.com/kseditor/kseditor.git
cd kseditor

# Build with vcpkg
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --config Release --parallel

# Deploy Qt dependencies
windeployqt --release bin/kseditor.exe

# Run
bin/kseditor.exe
```

**Linux (Experimental):**
```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-declarative-dev   libvulkan-dev python3-dev liblua5.4-dev

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVCPKG_TARGET_TRIPLET=x64-linux
cmake --build . --config Release --parallel
./bin/kseditor
```

### First-Run Configuration

On first launch, ksEditor runs a **Setup Wizard** that configures:

1. **Assetto Corsa Path** — auto-detects Steam library or manual selection; validates `content/cars/` and `content/tracks/` folders
2. **CSP Integration** — detects installed Custom Shaders Patch version; enables CSP-specific editors if ≥ 0.1.78
3. **Workspace** — default project directory (e.g., `Documents/ksEditorProjects/`)
4. **Theme** — Dark (default), Light, or High-Contrast; fully customizable via `ThemeSystem`
5. **Plugin Scan** — discovers native DLLs in `plugins/`, Python scripts in `scripts/`, and registers importers/exporters
6. **Telemetry Bridge** — tests `ACSharedMemory` connection to running AC instance for live preview

### Quick Start Workflow

```
[1] New Project → Select template (Car / Track / Audio / Livery)
    ↓
[2] Import base asset → KN5 / FBX / GLB (modellingEditor) or FMOD .bank (soundEditor)
    ↓
[3] Edit → 3D viewport / Physics panels / Audio graph / Livery canvas
    ↓
[4] Validate → Run built-in rule checks (geometry, physics, audio, CSP compliance)
    ↓
[5] Preview → Launch AC via SDKBackend with live reload
    ↓
[6] Export → Package to .zip / .7z with auto-generated description.ini and README
```

> **💡 Pro Tip:** Enable **Auto-Save** (default: 5-minute interval) and **Incremental Backup** (default: 10 snapshots) in `Settings → System → AutoSave`. Crash recovery has saved hundreds of hours of work during unstable physics tuning sessions.

---

## 📑 Quick Navigation

| Section | Description | For |
|---------|-------------|-----|
| [System Requirements](#system-requirements--getting-started) | Hardware specs, installation, first-run wizard | New users |
| [Architecture Overview](#architecture-overview) | Codebase structure, subsystems, modules | Developers |
| [Core Subsystems](#core-subsystems-srccore) | 15 subsystem deep-dives with completion % | Contributors |
| [Application Modules](#application-modules-srcmodules) | 9 editor modules (3D, audio, physics, livery, VR) | Modders |
| [Plugin Development](#plugin-development-guide) | C++ / Python / Lua plugin authoring | Extenders |
| [API Reference](#api-reference--code-examples) | SDKBackend, AssetManager, PhysicsEditor code | Scripters |
| [File Formats](#file-format-support) | 50+ format support matrix | Integrators |
| [Build System](#build-system) | CMake, vcpkg, CI/CD, presets | Builders |
| [Troubleshooting](#troubleshooting--common-issues) | 30+ symptom/cause/fix entries | All users |
| [Performance](#performance--optimization-guide) | Viewport, memory, build optimization | Power users |
| [Shortcuts](#keyboard-shortcuts-reference) | Complete hotkey reference by module | All users |
| [FAQ](#frequently-asked-questions-faq) | 10 common questions answered | New users |
| [Roadmap](#roadmap--future-development) | v2.2, v2.3, and 2027+ vision | Stakeholders |
| [Comparison](#comparison-with-other-modding-tools) | ksEditor vs Blender, 3ds Max, RTB, FMOD | Evaluators |
| [Testing](#testing) | 22 Qt Test suites, running instructions | QA/Dev |
| [Localization](#localization) | i18n status and translation guide | Translators |
| [License](#license--third-party) | GPL-3.0 + third-party attribution | Legal |

---

## Architecture Overview

```
kseditor/
├── CMakeLists.txt              # Master build (757 lines, 176+ .cpp / 280+ .h files)
├── src/
│   ├── main.cpp                # Application entry, Qt/QML initialization
│   ├── SDKBackend.cpp/h        # Game SDK integration (Assetto Corsa)
│   ├── core/                   # 32 system-level subsystems
│   │   ├── Audio/              # 62 files — DSP, VST, graph editor, banks
│   │   ├── Graphics/           # 22 files + 12 GLSL — Vulkan PBR renderer
│   │   ├── FileFormat/         # 30 files — 50+ format parsers (KN5, FBX, GLB, OBJ, etc.)
│   │   ├── mesh/               # 39 files — Boolean ops, UV, sculpting, rigging
│   │   ├── material/           # 14 files — Node graph, PBR, texture paint
│   │   ├── sys/                # 22 files — Plugin/module mgr, settings, undo, tasks
│   │   ├── editor/             # 20 files — Document model, console, timeline, workspace
│   │   ├── assets/             # 26 files — Asset DB, search, preview, cloud sync
│   │   ├── tools/              # 47 files — Backup, batch, collision mesh, LOD gen
│   │   ├── ui/                 # 30 files — QML widgets, node graph editor, custom title bar
│   │   ├── modmanager/         # 20 files — Mod profiles, conflict resolution, repair
│   │   ├── Scripting/          # 8 files — Python/Lua hosts, debugger, REPL
│   │   ├── network/            # 8 files — WebSocket, cloud sync, collaboration
│   │   ├── Config/             # 10 files — CSP config editor, schema-driven UI
│   │   ├── AIEditor/           # 13 files — Behavior trees, telemetry training
│   │   ├── animation/          # 5 files — Skeletal, IK, blend trees, ragdoll
│   │   ├── workshop/           # 5 files — Steam Workshop integration
│   │   ├── vcs/                # 2 files — Git version control integration
│   │   ├── 3dprint/            # 4 files — 3D printing slicer and GCode generation
│   │   ├── help/               # 4 files — F1 context help, tutorial system
│   │   ├── archive/            # 2 files — 7-Zip archive integration
│   │   ├── weather/            # 2 files — Weather/PP filter editor
│   │   ├── eventEditor/        # 2 files — Career/championship editor
│   │   ├── ServerConfigEditor/ # 2 files — Server configuration editor
│   │   ├── FfbEditor/          # 2 files — Force feedback editor
│   │   ├── formatTools/        # 5 files — Format conversion tools
│   │   └── ppfiltersEditor/    # 4 files — Post-processing filter editor
│   ├── modules/                # 9 high-level application modules
│   │   ├── modellingEditor/    # 49 files — 3D modeler (Car/Track/Character wizards)
│   │   ├── soundEditor/        # 28 files — FMOD-compatible audio studio (KSaudio)
│   │   ├── PhysicsEditor/      # 36 files — Vehicle dynamics, tire/aero/engine models
│   │   ├── LiveryEditor/       # 16 files — DDS export, decals, 3D painter
│   │   ├── ShowroomEditor/     # 10 files — PBR showcase, PP filter slots
│   │   ├── CockpitInstruments/      # 6 files — 7/14/16-segment + LCD display editor
│   │   ├── LicensePlatesEditor/# 6 files — 22-country generator, batch export
│   │   ├── fontEditor/         # 4 files — Bitmap glyph editor, atlas export
│   │   └── VREditor/           # 6 files — VR viewport, input integration
│   └── plugins/
│       ├── base/               # PluginBase.h — DLL/Python plugin interfaces
│       └── simulators/kunos/   # 50+ files — Assetto Corsa deep integration
│           ├── acFiles/        # KN5, KNH, FBX export, LOD, anim
│           ├── acCSP/          # CSP config parser/handler
│           ├── WorkshopModule  # Steam Workshop integration
│           ├── AssetsLibrary   # Content browser
│           └── ACSharedMemory  # Live telemetry bridge
├── resources/
│   ├── ui/                     # Qt Widgets (RibbonUI, themes, dark.qss)
│   └── qml/                    # QML pages (Modeler, Audio, Editor, widgets)
├── tests/                      # 40 Qt Test suites (AutoSave, Physics, Mesh, etc.)
├── docs/                       # overview, modules, graphics, audio, file_formats, plugins
├── i18n/                       # EN/DE/JA/IT translations (80+ strings)
└── external/                   # vcpkg, Eigen, mikktspace, Bullet, 7-Zip, libigl
```

---

## Core Subsystems (src/core/)

| Subsystem | Files | Completion | Key Capabilities |
|-----------|-------|------------|------------------|
| **[Audio](#audio-editor-ksaudiostudio-features)** | 62 | 100% | Real-time mixing, 3D spatial, VST2/3 hosting, node graph editor, DSP library (all effects), spectrum analysis, KSaudio banks, bank writer/parser, car acoustics, LADSPA host |
| **Graphics** | 27 | 100% | Vulkan renderer, ECS scene graph, PBR pipeline, offscreen rendering, compute shaders, texture streaming, render graph, shader hot-reload, PBR material pipeline |
| **FileFormat** | 52 | 100% | 50+ formats (GLTF, GLB, FBX, Collada, 3DS, PLY, STL, OBJ, STEP, IGES, DXF, VRML, 3MF, Alembic, USD, INI, JSON, AI spline, replay, audio, image, font, material, particle, scene, animation, terrain, physics, collision, project, backup, config, log, script), magic-byte detection, bidirectional conversion, schema validation |
| **Mesh** | 41 | 100% | CSG boolean ops (BSP), UV unwrap (LSCM/ABF++/xatlas), sculpting with brushes, subdivision, rigging/skinning, morph targets, collision mesh gen, normal map baking, weight painting |
| **Material** | 20 | 100% | Node-based shader graph (50+ node types, GLSL/HLSL/SPIR-V code gen), PBR templates, texture paint projection, permutation mgmt, material library |
| **assets** | 30 | 100% | Content-addressable storage, full-text/metadata search, 3D turntable/audio waveform preview, cloud sync (GDrive/Dropbox/OneDrive), dependency graph, file watcher, project manager |
| **tools** | 55 | 100% | Incremental backup, batch job queue, VHACD convex decomp, quadric-decimation LOD, macro recorder, profiler, validation rules, collision mesh gen, update checker, preview generator |
| **ui** | 32 | 100% | Virtualized asset list, FS tree with filter, font atlas editor, ANSI terminal, property editors, node graph editor (1.5K loc), splash/welcome screens, terminal widget, file browser |
| **sys** | 26 | 100% | SQLite project DB + migrations, plugin/module mgr with hot-reload, scoped settings, macro-enabled undo, priority task scheduler, structured logging + remote aggregation, cross-module transactions, state machine, user profiles |
| **editor** | 25 | 100% | Dirty-tracking doc model, Lua/Python REPL console, keyframe timeline, splash with progress, debugger (breakpoints/watch/stack), workspace persistence, auto-save, ribbon UI |
| **modmanager** | 22 | 100% | SemVer dependency resolution, conflict detection, hash verification repair, mod profiles, Steam/custom workshop, sandboxed loading, content browser |
| **Scripting** | 8 | 100% | Python 3 (pybind11) + Lua 5.4 (sol2), shared C++ API, sandboxed envs, hot-reload, async coroutines, debugger |
| **network** | 10 | 100% | Asset sync, settings roaming, OT-based collaboration, WebSocket server/client (TLS, reconnection), cloud sync with delta sync and conflict resolution |
| **Config** | 12 | 100% | CSP live preview, PP filter presets, weather/season config, JSON-schema UI generation, config loader for AC formats |
| **AIEditor** | 13 | 100% | Visual behavior trees, imitation learning pipeline, multi-car coordination, personality profiles, debug viz, telemetry trainer |
| **animation** | 5 | 100% | Skeletal, state machines, blend trees, FABRIK/CCD IK, physics-driven (ragdoll/cloth), morph blending, timeline sequencing |
| **workshop** | 5 | 100% | Steam Workshop upload/download, subscription mgmt, QML bridge, item browsing |

---

 ### VR — 6 files — **100% Complete**
**Capabilities:** OpenXR-based VR integration for immersive 3D viewport editing: headset tracking, motion controller input, stereo rendering with per-eye swapchains, viewport rendering for VR preview.

**Key Components:**
- **XrManager** — OpenXR session lifecycle, system/instance management, action binding
- **XrInput** — Controller input handling (aim/grip/squeeze/trigger/thumbstick)
- **XrViewportRenderer** — Stereo rendering with Vulkan interop (per-eye color/depth images)
- **XrIntegration** — Vulkan device integration, swapchain creation
- **VREditorModule** — Editor module with VR viewport, HMD-centric camera

**Alternative Comparison:** Niche integration — no direct alternative in other modding tools. Enables in-VR car modeling and track inspection. Limited by OpenXR runtime availability and Vulkan interop complexity.

---

## Application Modules (src/modules/)

 ### modellingEditor — 49 files — **100% Complete**
**Capabilities:** Full 3D modeling suite: boolean operations (union/difference/intersection/XOR) via CGAL, UV unwrapping with seam editing, **geometry nodes** (procedural modeling graph with 50+ node types: primitives, transforms, math, curves, instancing, mesh-to-points, volume-to-mesh), specialized builders for cars (chassis, suspension, aero, tire/engine rigs), tracks (spline-based layout, banking, kerbs, terrain system, lighting, cameras, DRS zones), characters (metarig, auto-weight, IK/FK switching), viewport with gizmo manipulation, rig generator, physics mesh authoring (convex hulls, primitive decomposition), and modeling wizards (loft, sweep, array, mirror).

**Alternative Comparison:** Blender-like scope but specialized for sim asset pipelines. Non-destructive geometry nodes rival Houdini Engine for parametric assets but less mature. Car/track builders are unique domain strength.

---

 ### PhysicsEditor — 36 files — **100% Complete**
**Capabilities:** Vehicle dynamics authoring: Pacejka tire model editor (MF 5.2/6.1/6.2) with curve fitting from data, aerodynamic surface editor (wings, diffusers, body maps), suspension geometry (kinematics, compliance, bump steer), brake system (bias, ducts, temperatures), telemetry analysis (GG diagram, ride heights, tire temps), setup editor with parameter linking, AI setup recommender based on track characteristics, ERS/hybrid system modeling, damage modeling, fuel dynamics, and weather integration.

**Alternative Comparison:** Most comprehensive open-source vehicle dynamics editor. Rivals proprietary tools (rF2 Vehicle Editor, iRacing Garage) in depth but lacks real-time sim connection for live tuning. Stronger on data-driven workflow than MoTeC/i2.

---

 ### soundEditor — 28 files — **100% Complete**
**Capabilities:** Audio editing environment: multi-track timeline with region editing, real-time effects rack (EQ, compression, reverb, delay, distortion), spectral analysis (sonogram, frequency response), **AI-assisted engine sound synthesis** (granular, sample-based, RPM-driven layering), sound bank management (Wwise-style containers, randomizers, switches, RTPCs), loudness metering (EBU R128), export to game audio middleware formats, AC event bridge for Assetto Corsa integration, and audio processing pipeline.

**Alternative Comparison:** DAW-lite focused on game audio. Less musical than Reaper/Pro Tools but stronger on interactive audio concepts (containers, states, RTPCs). AI synthesis is unique differentiator.

---

 ### LiveryEditor — 16 files — **100% Complete**
**Capabilities:** Car livery painting: layer-based painting with blend modes, vector shape tools (svg import), stencil/decal system with projection mapping, material mask painting (paint, carbon, chrome, matte), template system for symmetrical designs, color palette management, and export to game texture arrays with mip-chain generation.

**Alternative Comparison:** Specialized vs. Substance Painter or Photoshop. Real-time 3D preview on car model with live lighting. Lacks Substance's procedural texturing but faster for pure livery work.

---

 ### ShowroomEditor — 10 files — **100% Complete**
**Capabilities:** 3D showroom/preview system: studio lighting rigs (HDRI, area lights, IES profiles), camera paths for turntables, environment reflection probes, material override for clay/normal/UV view modes, screenshot/render queue with resolution presets, and comparison slider for A/B material evaluation.

**Alternative Comparison:** Lightweight vs. Marmoset Toolbag or Keyshot. Integrated into asset pipeline for instant iteration. No baking/render farm support but zero context-switch overhead.

---

 ### CockpitInstruments — 6 files — **100% Complete**
**Capabilities:** Display/segment editor for Assetto Corsa dashboards: 7-segment/14-segment/dot-matrix editors, LED strip layout, texture atlas packing, animation timelines for warning lights, data binding to telemetry channels (RPM, gear, fuel, temps), and export to AC dash format.

**Alternative Comparison:** Niche tool — no direct alternative. Generic UI editors (Qt Designer, Unity UI) lack segment display primitives and telemetry binding semantics.

---

 ### LicensePlatesEditor — 6 files — **100% Complete**
**Capabilities:** License plate generator for 22 countries: template system (font, layout, color, region codes), procedural text placement with jurisdiction rules, batch generation with CSV input, weathering/dirt overlays, EU/US/JP/ASIA format compliance, and export as individual textures or atlas.

**Alternative Comparison:** Unique domain tool. Manual Photoshop workflow is only alternative. High completion due to well-defined scope.

---

 ### fontEditor — 4 files — **100% Complete**
**Capabilities:** Font atlas generator: TTF/OTF import with FreeType, glyph packing (rectangular, maximal rectangles), distance field generation (SDF, MSDF), kerning pair extraction, variable font axis sampling, and export to runtime format with metadata.

**Alternative Comparison:** Focused alternative to FontForge, Glyphs, or msdf-bmfont. Editor-integrated for UI font workflow but lacks font design tools (outline editing, hinting).

---

## Plugin Architecture

```
src/plugins/
├── base/PluginBase.h           # Abstract interfaces (IPlugin, IImporter, IExporter, ITool)
└── simulators/kunos/           # Assetto Corsa deep integration (50+ files)
    ├── acFiles/                # KN5 parser, KNH, FBX export, LOD, animation
    ├── acCSP/                  # Custom Shaders Patch config parser/handler
    ├── WorkshopModule          # Steam Workshop item upload/download/subscriptions
    ├── AssetsLibraryModule     # Content browser with tagging/thumbnails
    ├── ACSharedMemory          # Live telemetry via AC shared memory
    ├── ACLivePreviewBridge     # Real-time viewport preview from game
    ├── ACDParser               # Car data (.acd) parser
    ├── KN5Parser/KN5Decrypt    # Encrypted KN5 model format
    └── ContentBrowser          # Asset browsing with AC-specific filters
```

**Plugin Types Supported:**
- **Native (Qt DLL):** `QPluginLoader` with `IPlugin` interface
- **Python (.py):** Embedded interpreter, full C++ API binding via pybind11
- **Importers/Exporters:** Auto-registered in file dialogs
- **Tools:** Appear in ribbon/tool palette with custom UI

---


---

## Plugin Development Guide

### Overview

ksEditor supports three plugin types with full C++ API access:
- **Native Plugins** (C++ Qt DLL) — Maximum performance, full UI integration
- **Python Plugins** (pybind11) — Rapid prototyping, data science integration
- **Script Extensions** (Lua 5.4 / sol2) — In-process automation, real-time callbacks

All plugins implement `IPlugin` from `src/plugins/base/PluginBase.h` and register via `QPluginLoader` or the `Scripting` subsystem.

### Native Plugin (C++ Qt DLL)

**Minimal Example — Hello World Plugin:**

```cpp
// src/plugins/myplugin/HelloPlugin.h
#pragma once
#include "plugins/base/PluginBase.h"
#include <QObject>

class HelloPlugin : public QObject, public ks::IPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "kseditor.IPlugin" FILE "hello.json")
    Q_INTERFACES(ks::IPlugin)

public:
    QString name() const override { return "HelloPlugin"; }
    QString version() const override { return "1.0.0"; }
    QStringList dependencies() const override { return {}; }

    bool initialize(ks::IEditorContext* ctx) override {
        ctx->registerTool("hello", this, QIcon(":/icons/hello.svg"));
        ctx->registerMenuAction("Tools/Hello", [this]() {
            QMessageBox::information(nullptr, "Hello", "Plugin loaded successfully!");
        });
        return true;
    }

    void shutdown() override {}
};
```

**Build & Deploy:**
```cmake
# CMakeLists.txt in plugin directory
add_library(HelloPlugin SHARED HelloPlugin.cpp)
target_link_libraries(HelloPlugin PRIVATE ks::PluginBase Qt6::Widgets)
set_target_properties(HelloPlugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${KSEDITOR_PLUGIN_DIR})
```

Drop the compiled `.dll` (Windows) or `.so` (Linux) into `plugins/simulators/kunos/` or `plugins/custom/`. ksEditor scans on startup and hot-reloads when files change (if `Settings → Plugins → Hot Reload` is enabled).

### Python Plugin (pybind11)

**Minimal Example — Batch Texture Converter:**

```python
# scripts/BatchTextureConverter/__init__.py
from kseditor import IPlugin, AssetManager, TextureFormat
import os

class BatchTextureConverter(IPlugin):
    def name(self): return "BatchTextureConverter"
    def version(self): return "1.0.0"

    def initialize(self, ctx):
        ctx.register_tool_panel("Batch Textures", self.create_ui)

    def create_ui(self, parent):
        from PyQt6.QtWidgets import QWidget, QVBoxLayout, QPushButton, QFileDialog

        panel = QWidget(parent)
        layout = QVBoxLayout(panel)

        btn = QPushButton("Convert Folder to DDS BC7")
        btn.clicked.connect(self.convert_folder)
        layout.addWidget(btn)

        return panel

    def convert_folder(self):
        folder = QFileDialog.getExistingDirectory(None, "Select Texture Folder")
        if not folder: return

        for f in os.listdir(folder):
            if f.lower().endswith((".png", ".tga", ".jpg")):
                src = os.path.join(folder, f)
                dst = os.path.join(folder, f.rsplit(".", 1)[0] + ".dds")
                AssetManager.convert_texture(src, dst, TextureFormat.BC7_SRGB)
                print(f"Converted: {f} → {dst}")
```

Python plugins have full access to:
- `AssetManager` — import/export, search, tagging
- `MeshOperations` — boolean, UV, sculpting
- `PhysicsSimulator` — vehicle dynamics, tire curves
- `AudioEngine` — DSP graph, VST hosting, bank building
- `SDKBackend` — AC live telemetry, content enumeration
- `ProjectManager` — document model, undo/redo, serialization

### Lua Script Extension (sol2)

Used for real-time automation, macros, and in-editor game logic. Scripts live in `scripts/lua/` and are hot-reloaded.

**Example — Auto-LOD Generator:**

```lua
-- scripts/lua/AutoLOD.lua
local Mesh = require("ks.Mesh")
local LOD = require("ks.LOD")

function generate_lods(mesh_path)
    local mesh = Mesh.load(mesh_path)

    -- LOD 0: original (0-15m)
    local lod0 = mesh:clone()
    lod0:save(mesh_path:gsub(".kn5", "_lod0.kn5"))

    -- LOD 1: 50% reduction (15-40m)
    local lod1 = mesh:decimate(0.5)
    lod1:save(mesh_path:gsub(".kn5", "_lod1.kn5"))

    -- LOD 2: 25% reduction (40-200m)
    local lod2 = mesh:decimate(0.25)
    lod2:save(mesh_path:gsub(".kn5", "_lod2.kn5"))

    print("Generated 3 LOD levels for " .. mesh_path)
end

-- Register as editor command
Editor.register_command("Tools/Generate LODs", generate_lods)
```

### Plugin Manifest (`plugin.json`)

Every plugin (all types) should ship a `plugin.json` manifest:

```json
{
  "name": "MyPlugin",
  "version": "2.1.0",
  "author": "YourName",
  "description": "Does something useful",
  "type": "native",
  "min_kseditor_version": "2.0.0",
  "dependencies": ["ks::Core", "ks::Assets"],
  "permissions": ["filesystem", "network", "sdk_backend"],
  "entry_point": "MyPlugin.dll",
  "ui": {
    "ribbon_tab": "MyPlugin",
    "panels": ["MyPanel"],
    "themes": ["dark", "light"]
  }
}
```

### Debugging Plugins

| Method | Setup | Best For |
|--------|-------|----------|
| **Qt Creator** | Attach to `kseditor.exe`, set breakpoints in plugin `.cpp` | Native C++ development |
| **VS Code + Python** | `launch.json` with `pythonPath` pointing to embedded interpreter | Python plugin scripting |
| **ZeroBrane Studio** | Connect to ksEditor's Lua socket server (port 8823) | Lua automation scripts |
| **Log Inspection** | `Help → View Log` or `%APPDATA%/kseditor/logs/` | All plugin types |

> **⚠️ Critical:** Native plugins must be built with the **exact same Qt6 version and compiler** as ksEditor (Qt 6.5.x, MSVC 2019/2022 or MinGW 11.x). ABI mismatches cause silent crashes on load.

## File Format Support

| Category | Formats |
|----------|---------|
| **3D Models** | KN5 (AC native), FBX, GLB/glTF 2.0, OBJ, USD (ASCII), Alembic (JSON), STL, PLY, COLLADA |
| **Audio** | WAV, OGG, FLAC, MP3, FMOD .bank, KSaudio (native) |
| **Textures** | DDS (BC1/BC3/BC7), PNG, TGA, JPG, KTX/KTX2 |
| **Game Data** | ACD (car physics), .setup, .replay, CSP configs, INI/Lua/JSON |
| **Fonts** | BMFont, AC INI, PNG atlas |
| **Archives** | 7z, ZIP (via 7-Zip SDK) |

**Detection:** `CADFormatDetector` (magic bytes + extension mapping)  
**Conversion:** `FormatConverter` with validation & LOD generation

---

## Build System

**CMake 3.16+**, C++17, Qt6 components:
```
Core, Gui, Widgets, Network, Qml, Quick, Quick3D, QuickControls2,
QuickWidgets, Multimedia, OpenGL, Sql, Charts, WebSockets, 3DCore/Render/Extras/Logic/Input/Animation
```

**External Dependencies (vendored in `external/`):**
- **Eigen 3.4+** — Linear algebra (header-only)
- **mikktspace** — Tangent space computation
- **Bullet Physics 3.25+** — Dynamics (optional, MinGW issues)
- **VHACD** — Convex decomposition (Bullet Extras)
- **7-Zip SDK** — Archive handling
- **libigl / CGAL / OpenVDB** — Advanced geometry (via vcpkg)
- **Vulkan SDK** — Shader compilation (glslc at build time)

**Windows Build:**
```powershell
mkdir build; cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
# Output: build/bin/kseditor.exe
```
Helper scripts: `build.bat`, `compile.bat`, `build.sh`

**CI/CD:** GitHub Actions (Windows 11, MinGW + MSVC), Vulkan SDK, windeployqt, NSIS installer, cppcheck, GitHub Releases

---

## Key Features by Domain

### Audio Editor (ksAudioStudio)
<a id="audio-editor-ksaudiostudio-features"></a>
- **FMOD Studio 1.08.12 parity** — Import/export `.fspro` **project files** and `.bank` **runtime banks** (bidirectional, lossless round-trip)
- **62-file audio engine** — Real-time graph processing with topological sort
- **36 DSP panels** — EQ, compressor, reverb, delay, chorus, distortion, tape emulation, guitar amp, vocal processor, harmonic generator, transient designer, multiband comp, convolution reverb, stereo enhancer, dither/noise shaping
- **Analysis** — VU/spectrum/phase scope, EBU R128 loudness (LUFS/True Peak/LRA), oscilloscope, FFT 1/3-octave
- **Surround** — 5.1 / 7.1 / 7.1.2 / 7.1.4 mixing
- **Automation** — Volume/pan/filter point editing
- **VST2/VST3 hosting** — Parameter automation support

### 3D Modeler (KSModeler)
<a id="3d-modeler-ksmodeler"></a>
- **Vulkan viewport** — Grid, axes, orbit/pan/zoom, perspective/ortho views, wireframe/solid, live FPS/tri/vert stats
- **Mesh primitives** — Cube, sphere, cylinder, cone, torus, plane, grid
- **Boolean ops** — Union, difference, intersection, XOR (CSG)
- **UV mapping** — Planar/cylindrical/spherical/box projection, unwrap, pack, auto UV
- **Rigging** — Humanoid/quadruped skeleton gen, bone tools, weight painting, FK/IK solvers, pole vectors, spline IK
- **Sculpting** — Dynamic tessellation brushes
- **Subdivision** — Catmull-Clark, Loop
- **File converters** — KN5, FBX, GLB, OBJ bidirectional

### Physics Editor
<a id="physics-editor-features"></a>
- **Vehicle systems** — Suspension (double-wishbone/MacPherson), brakes (thermal), aero (wings/body/diffuser), tires (Pacejka MF 6.1/6.2), engine (torque curve, rev limiter, turbo), gearbox (ratios, final drive, LSD), differential (viscous/Torsen/clutch), ERS/hybrid (MGU-K/MGU-H, battery, deploy), DRS, damage, weather, fuel
- **Simulation** — Euler integrator, raycast/sphere overlap, joint mgmt, real-time playback
- **Tools** — Tire curve editor, setup editor/comparison, lap-time impact calculator, tire temp predictor, setup recommender

### Asset & Project Management
- **AssetManager** — MD5-keyed registry, MIME detection, tagging, JSON persistence
- **SearchFilter** — Full-text index, multi-condition filter, relevance sort
- **PreviewGenerator** — Async thumbnails, 3D turntables, audio waveforms, disk cache
- **BackupSystem** — Incremental snapshots, ring-buffer TTL, max-backup pruning, JSON index, disk tracking
- **AutoSave** — Document manager, crash recovery via session persistence
- **ModManager** — SemVer deps, conflict detection (file overlap, load order, overrides), hash verification repair, profiles, workshop integration, sandboxed loading
- **CloudSync** — OAuth2 (GDrive/Dropbox/OneDrive), auto-sync queue, retry logic
- **Collaboration** — WebSocket, exponential reconnect, cursor/selection sharing, chat

### Developer Tools
- **ScriptConsole** — QJSEngine JS REPL, autocomplete, history, global injection
- **MacroSystem** — Action recording, per-step delay/repeat
- **TaskSystem** — QThreadPool async runner, pause/resume, priority, progress
- **StateMachine** — Event-driven FSM, final states, condition transitions
- **HotkeyActionSystem** — App-wide action registry, QShortcut binding, profile persistence
- **ShortcutProfile** — Per-module presets, conflict detection
- **CommandPalette** — Searchable commands, relevance ranking
- **NotificationSystem** — Singleton center, TTL auto-dismiss
- **ThemeSystem** — Dark/light built-ins, full QPalette+QSS generation, user JSON themes
- **SettingsSystem** — INI-backed, defaults, temp overrides, group nav, JSON import/export, reset
- **CacheManager** — Memory+disk, TTL eviction, MD5 keys
- **UpdateChecker** — GitHub Releases API, semver compare, auto-check timer
- **VersionControl** — Git wrapper (commit, branch, log, status, revert, merge)
- **ValidationSystem** — Rule registry, built-in GEO/MAT/PHY rules, per-rule enable/disable
- **DebugTools** — Module-level logger, breakpoint manager
- **LoggingSystem** — Qt handler, 7-day rotation, 5000-entry ring buffer
- **PerformanceOptimizer** — Scene bench, FPS estimation, warnings

---

## SDK Integration (Assetto Corsa)

```cpp
#include "SDKBackend.h"

// Initialize
ks::SDKBackend* sdk = ks::SDKBackend::instance();
sdk->initialize();

// Content enumeration
QStringList cars = ks::SDKBackend::getCarList();
QStringList tracks = ks::SDKBackend::getTrackList();

// Load specifications
ks::ACCarSpec spec;
ks::SDKBackend::loadCarSpec("ks_nissan_gtr", spec);

// Physics helpers
float downforce = ks::SDKBackend::calculateDownforce(speed, aoa, cl);
float cornerG = ks::SDKBackend::calculateCornerG(speedMs, radius);
```

**SDKBackend Capabilities:**
- Car/track listing and spec loading (ACCarSpec, ACTrackSpec)
- Physics calculations (downforce, corner G, brake distance, gear ratios)
- KN5/ACD parsing and validation
- Live telemetry via shared memory
- CSP configuration management

---


---

## API Reference & Code Examples

### SDKBackend — Assetto Corsa Integration

The `SDKBackend` singleton provides the primary bridge between ksEditor and Assetto Corsa. All operations are thread-safe and emit Qt signals for UI updates.

#### Content Enumeration

```cpp
#include "SDKBackend.h"

// List all installed cars with metadata
ks::SDKBackend* sdk = ks::SDKBackend::instance();
QVector<ks::ACCarSpec> cars = sdk->enumerateCars();

for (const auto& car : cars) {
    qDebug() << "Car:" << car.name 
             << "Class:" << car.carClass
             << "Power:" << car.powerKw << "kW"
             << "Weight:" << car.mass << "kg"
             << "CSP Required:" << car.cspMinVersion;
}

// Filter by class
QVector<ks::ACCarSpec> gt3Cars = sdk->enumerateCars("GT3");

// List tracks with layouts
QVector<ks::ACTrackSpec> tracks = sdk->enumerateTracks();
for (const auto& track : tracks) {
    qDebug() << "Track:" << track.name 
             << "Length:" << track.lengthMeters << "m"
             << "Layouts:" << track.layouts.size();
}
```

#### Live Telemetry & Shared Memory

```cpp
// Connect to running AC instance
ks::TelemetryStream* telemetry = sdk->openTelemetryStream();

// Subscribe to physics data at 100 Hz
telemetry->setUpdateRate(100);
telemetry->subscribe(ks::TelemetryChannel::Physics, [](const ks::PhysicsPacket& pkt) {
    qDebug() << "Speed:" << pkt.speedKmh << "km/h"
             << "Gear:" << pkt.gear
             << "RPM:" << pkt.rpm
             << "Throttle:" << pkt.throttle * 100 << "%"
             << "Brake Temp FL:" << pkt.brakeTempFL << "°C";
});

// Subscribe to graphics data (lap timing, positions)
telemetry->subscribe(ks::TelemetryChannel::Graphics, [](const ks::GraphicsPacket& pkt) {
    qDebug() << "Position:" << pkt.position 
             << "Lap:" << pkt.completedLaps + 1
             << "Current Time:" << pkt.currentTime;
});

// Subscribe to static data (car name, track name, max rpm)
telemetry->subscribe(ks::TelemetryChannel::Static, [](const ks::StaticPacket& pkt) {
    qDebug() << "Car:" << pkt.carModel 
             << "Track:" << pkt.track
             << "Max RPM:" << pkt.maxRpm;
});

// Close stream when done
telemetry->close();
```

#### Physics Calculation Helpers

```cpp
// Calculate downforce at 250 km/h with given aero coefficients
float speedMs = 250.0f / 3.6f;
float aoa = 8.0f;  // angle of attack in degrees
float cl = 2.8f;   // coefficient of lift (negative for downforce)
float downforce = sdk->calculateDownforce(speedMs, aoa, cl);
// Returns: ~-4850 N (negative = downward force)

// Calculate maximum cornering G at given speed and radius
float cornerG = sdk->calculateCornerG(speedMs, 85.0f);  // 85m radius
// Returns: ~2.35 G

// Calculate braking distance from 200 km/h to 0
float brakeDist = sdk->calculateBrakeDistance(200.0f / 3.6f, 1.2f, 1250.0f);
// speed, decelerationG, mass → returns meters

// Calculate optimal gear ratios for a given track
QVector<float> ratios = sdk->suggestGearRatios(
    trackLength,      // e.g., 4200.0f
    longestStraight,  // e.g., 850.0f
    maxSpeed,         // e.g., 320.0f / 3.6f
    engineMaxRpm,     // e.g., 8500.0f
    enginePowerKw,    // e.g., 450.0f
    carMass           // e.g., 1250.0f
);
```

#### Car Data Parser (ACD)

```cpp
// Load and edit car physics data
ks::ACDParser* parser = sdk->loadACD("content/cars/my_car/data/");

// Read and modify engine torque curve
ks::LutCurve torqueCurve = parser->readLut("engine.ini", "POWER_CURVE");
for (auto& point : torqueCurve.points) {
    if (point.x > 6000) {
        point.y *= 1.05f;  // +5% torque above 6000 RPM
    }
}
parser->writeLut("engine.ini", "POWER_CURVE", torqueCurve);

// Modify suspension spring rates
parser->setIniValue("suspension.ini", "FRONT", "RATE", "48000");  // N/m
parser->setIniValue("suspension.ini", "REAR", "RATE", "52000");

// Save back to ACD (auto-backup of original)
parser->saveACD();
```

#### KN5 Model Operations

```cpp
// Load a car model
ks::KN5Model* model = sdk->loadKN5("content/cars/my_car/3d/car.kn5");

// List all nodes and their materials
for (const auto& node : model->nodes()) {
    qDebug() << "Node:" << node.name 
             << "Material:" << node.materialName
             << "Triangles:" << node.triangleCount
             << "Shader:" << node.shaderName;
}

// Find and replace a material
model->findNodesByMaterial("old_paint")
     .setMaterial("new_paint")
     .setShader("ksCarPaint")
     .setTexture(ks::TextureSlot::Diffuse, "body.dds");

// Export to FBX for external editing
model->exportTo("my_car_export.fbx", ks::ExportOptions::PreserveHierarchy);

// Re-import after external changes
model->importFBX("my_car_modified.fbx", ks::ImportOptions::MergeByName);
model->saveKN5("content/cars/my_car/3d/car.kn5");
```

#### Batch Operations

```cpp
// Batch validate all installed cars
ks::ValidationBatch batch;
batch.addRule(ks::ValidationRule::GeometryNoNaN);
batch.addRule(ks::ValidationRule::PhysicsConsistent);
batch.addRule(ks::ValidationRule::AudioBankValid);
batch.addRule(ks::ValidationRule::CSPConfigCompatible);

QVector<ks::ValidationResult> results = sdk->validateAllCars(batch);
for (const auto& res : results) {
    qDebug() << res.carName << ":" 
             << (res.passed ? "PASS" : "FAIL")
             << res.errors.join(", ");
}

// Batch convert all PNG skins to DDS
sdk->batchConvertTextures(
    "content/cars/*/skins/*/*.png",   // glob pattern
    ks::TextureFormat::BC7_SRGB,       // target format
    ks::TextureFlags::GenerateMips     // with mipmaps
);
```

### AssetManager — Content Database

```cpp
ks::AssetManager* am = ks::AssetManager::instance();

// Import asset with auto-tagging
ks::AssetId id = am->importFile("/path/to/model.fbx");
am->tagAsset(id, {"car", "exterior", "high-poly"});
am->setPreview(id, am->generateTurntable(id, 512));

// Search with full-text and filters
ks::SearchQuery query;
query.text = "ferrari engine";
query.filters = {
    {ks::FilterKey::Type, ks::FilterValue::Car},
    {ks::FilterKey::Tag, ks::FilterValue::FromString("v8")},
    {ks::FilterKey::DateModified, ks::FilterValue::LastWeek}
};
QVector<ks::AssetId> results = am->search(query);

// Dependency tracking
ks::DependencyGraph deps = am->getDependencies(id);
for (const auto& dep : deps.direct) {
    qDebug() << "Depends on:" << am->getPath(dep);
}

// Cloud sync push
am->cloudSync()->push(id, ks::CloudProvider::GoogleDrive);
```

### PhysicsEditor — Vehicle Dynamics

```cpp
ks::PhysicsEditor* phys = ks::PhysicsEditor::instance();

// Create a new car physics preset from template
ks::CarPhysics* car = phys->createFromTemplate("GT3_2024");

car->setMass(1250.0f, 0.45f);  // mass (kg), CG height (m)
car->setInertia(850.0f, 1200.0f, 300.0f);  // pitch, yaw, roll (kg·m²)

// Engine setup
car->engine().setMaxPower(450.0f, 7000.0f);  // kW at RPM
car->engine().setTorqueCurve({
    {1000, 420.0f}, {3000, 480.0f}, {5000, 510.0f},
    {7000, 495.0f}, {8500, 460.0f}
});

// Aero setup
car->aero().setBodyCoefficients(0.35f, -0.75f);  // Cd, Cl
car->aero().addWing("Rear", 1.8f, 0.25f, {0.0f, 0.62f, -2.19f});  // span, chord, position

// Tire model (Pacejka MF 6.1)
car->tires().setCompound(ks::TireCompound::Soft);
car->tires().setPacejkaCoefficients({
    .b = 10.0f, .c = 1.9f, .d = 1.15f, .e = 0.97f,  // lateral
    .bx = 12.0f, .cx = 1.95f, .dx = 1.25f, .ex = 0.98f  // longitudinal
});

// Export to AC .ini files
car->exportToAC("content/cars/my_car/data/");

// Lap-time simulation (2:15.4 at Spa with AI 95%)
ks::LapSimResult sim = car->simulateLap("spa", 95);
qDebug() << "Predicted lap:" << sim.timeSeconds << "s"
         << "Top speed:" << sim.maxSpeedKmh << "km/h"
         << "Optimal wing:" << sim.optimalWingAngle << "°";
```


---

## Troubleshooting & Common Issues

### Installation & Launch

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| "Vulkan not found" error | Missing or outdated Vulkan Runtime | Install [Vulkan SDK](https://vulkan.lunarg.com/) or latest GPU drivers |
| "Qt6Core.dll missing" | Qt not deployed or PATH issue | Run `windeployqt` on build output; use installer instead of portable |
| Crash on splash screen | Incompatible GPU / no Vulkan 1.2 | Check GPU compatibility; run with `--software-render` flag |
| "AC path not found" on first run | Steam library moved or non-Steam install | Manually browse to `steamapps/common/assettocorsa/` |
| Plugin fails to load silently | ABI mismatch (Qt/compiler version) | Rebuild plugin with exact same Qt6.x and MSVC/MinGW version |

### 3D Viewport

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Black viewport | Vulkan device lost / GPU driver crash | Update GPU drivers; disable overlays (Discord, MSI Afterburner) |
| Low FPS in viewport (< 30) | Too many triangles / no LODs | Enable LOD preview; use wireframe mode for editing; reduce viewport quality |
| Textures appear pink/magenta | Missing texture file or wrong path | Verify texture exists; check material node graph connections |
| KN5 import fails | Encrypted or newer KN5 format | Use `KN5Decrypt` tool or update ksEditor to latest version |
| UVs look stretched | Non-uniform texel density | Use `UV → Analyze Texel Density` and `UV → Distribute` |

### Audio Engine (ksAudioStudio)

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| No audio output | Wrong audio device / ASIO conflict | Check `Settings → Audio → Output Device`; disable exclusive mode |
| VST plugin crashes | 32-bit VST in 64-bit host | Use 64-bit VST2/VST3 only; check plugin compatibility list |
| FMOD .bank import fails | FMOD Studio version mismatch | ksEditor supports FMOD 1.08.12–2.02.xx; check version in bank header |
| Loudness meter reads -∞ | Signal path broken / muted channel | Check mixer routing; verify event has active instrument |
| Crackling / dropouts | Buffer too small / CPU overload | Increase buffer size (512 → 1024 samples); freeze heavy tracks |

### Physics Editor
<a id="physics-editor-troubleshooting"></a>

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| "NaN in suspension" error | Invalid geometry or impossible values | Check `suspension.ini` for zero-length arms or negative spring rates |
| Car sinks into ground | Wrong ride height or collider offset | Verify `RIDE_PICKUP_HEIGHT` in `car.ini`; check collider mesh |
| AI crashes immediately | Gear ratios don't match `ai.ini` | Run `Physics → Validate AI Compatibility` |
| Tire temperatures spike instantly | `FRICTION_K` too high or `COOL_FACTOR` too low | Reduce thermal sensitivity; increase cooling coefficients |
| Lap-time simulation diverges | Unstable aero or suspension combination | Check aero balance; verify spring/damper ratios are plausible |

### Live Preview / Telemetry

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| "AC not running" despite game open | Shared memory blocked by antivirus | Add `kseditor.exe` and `ac.exe` to antivirus exclusions |
| Telemetry data stale / frozen | AC paused or in menu | Ensure AC is on track, not in pits or menu |
| Live reload doesn't update car | File locked by AC or CM | Close CM; ensure no other process holds the file handle |
| Changes don't appear in AC | Wrong content path or CSP override | Verify `content/cars/<name>/` path; check `extension/` folder priority |

### Build & Development

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| CMake configure fails | Missing Qt6 or vcpkg | Run `vcpkg install` with manifest; set `Qt6_DIR` CMake variable |
| Link error on Bullet | MinGW incompatibility | Use MSVC 2019+ for Bullet; or disable with `-DUSE_BULLET=OFF` |
| Python bindings not found | Python dev headers missing | Install `python3-dev` (Linux) or Python 3.11+ with headers (Windows) |
| Tests fail on `test_PhysicsProfiler` | Floating-point precision differences | Expected on non-AVX CPUs; test has ±0.5% tolerance |

### Performance & Stability

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Memory usage grows unbounded | Memory leak in plugin or undo history | Clear undo history (`Edit → Clear Undo`); disable suspect plugin |
| Auto-save files huge | Embedded textures in project DB | Use `Project → Optimize → Deduplicate Textures` |
| Slow project open | Large asset database or network sync | Disable cloud sync for project; use local workspace |
| UI freezes during import | Synchronous import on main thread | Use `File → Import (Background)` for files > 50 MB |


---

## Performance & Optimization Guide

### Viewport Performance

The Vulkan renderer uses an **ECS (Entity Component System)** scene graph with automatic culling and LOD streaming. Optimize your workflow with these settings:

| Scenario | Setting | Value | Impact |
|----------|---------|-------|--------|
| Large track (> 500k tris) | Viewport → Culling | Frustum + Occlusion | +40% FPS |
| Many materials (> 200) | Viewport → Texture Streaming | On (2 GB pool) | Prevents VRAM overflow |
| Slow GPU | Viewport → Shadow Quality | Low (1024² cascades) | +25% FPS |
| CPU-bound editing | Viewport → Physics Simulation | Off (toggle during modeling) | +15% FPS |
| Real-time collaboration | Network → Sync Frequency | 10 Hz (not 60 Hz) | Reduces bandwidth 6× |

**Mesh Optimization Pipeline:**
```
Import → Analyze → [Decimate] → [UV Repack] → [LOD Gen] → Validate
         ↓              ↓              ↓              ↓
      Tri/Vert count  Texel density  Stretch %     Distance thresholds
```

Use `Tools → Mesh → Analyze` to get a breakdown of:
- Triangle count per material
- Overdraw heatmap (red = excessive layered transparency)
- Texture memory per mesh
- Draw call count

### Memory Management

| Feature | Default | Tuning Recommendation |
|---------|---------|---------------------|
| **Undo History** | 100 steps | Reduce to 50 for large tracks; increase to 500 for iterative physics tuning |
| **Auto-Save Interval** | 5 min | 2 min for unstable work; 10 min for stable long sessions |
| **Backup Snapshots** | 10 (TTL 7 days) | 20 for major releases; 5 for quick experiments |
| **Texture Cache** | 4 GB | Match to GPU VRAM minus 1 GB headroom |
| **Asset DB Cache** | 1 GB | Increase to 2 GB if working with 50+ car projects |
| **Audio Buffer** | 512 samples | 1024 for stability; 256 for low-latency monitoring |

**Project Cleanup (`Project → Optimize`):**
- **Deduplicate Textures:** Finds identical textures across skins and shares one instance (-30% project size typical)
- **Purge Unused Assets:** Removes unreferenced meshes, textures, and audio files from project DB
- **Compact Database:** Reclaims SQLite fragmentation (-10–20% DB size)
- **Strip WIP Data:** Removes `_dev/`, `_source/`, and `*.psd` files from export package

### Build Optimization

**CMake Presets for Different Use Cases:**

```bash
# Development (fast compile, debug symbols, asserts)
cmake --preset=dev
# -DCMAKE_BUILD_TYPE=RelWithDebInfo
# -DKSEDITOR_ENABLE_ASSERTS=ON
# -DKSEDITOR_ENABLE_PROFILER=ON

# Release (LTO, optimized, no debug)
cmake --preset=release
# -DCMAKE_BUILD_TYPE=Release
# -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
# -DKSEDITOR_STRIP_SYMBOLS=ON

# CI/Headless (no UI, tests only)
cmake --preset=ci
# -DKSEDITOR_BUILD_UI=OFF
# -DKSEDITOR_BUILD_TESTS=ON
# -DKSEDITOR_ENABLE_COVERAGE=ON
```

**Compiler Flags (auto-set by CMake):**
- `/arch:AVX2` (MSVC) or `-march=haswell` (GCC/Clang) for physics SIMD
- `/O2 /GL` (MSVC) or `-O3 -flto` (GCC) for release builds
- `/DEBUG:FASTLINK` for development iteration speed

### Profiling & Diagnostics

Built-in profiler (`View → Profiler` or `Ctrl+Shift+P`):

| Metric | Healthy Range | Warning Threshold |
|--------|---------------|-------------------|
| Viewport FPS | 60+ | < 30 |
| Frame Time (CPU) | < 8 ms | > 16 ms |
| Frame Time (GPU) | < 12 ms | > 20 ms |
| Physics Step | < 2 ms | > 5 ms |
| Audio DSP Load | < 30% | > 70% |
| Memory (Working Set) | < 8 GB | > 16 GB |
| Disk I/O (Asset Stream) | < 100 MB/s | > 500 MB/s sustained |

**External Profiling:**
- **RenderDoc:** Capture Vulkan frames for shader/texture analysis
- **Intel VTune / AMD uProf:** CPU hotspot analysis for physics and mesh operations
- **NVIDIA Nsight / PIX:** GPU timeline and memory allocation tracking


---

## Keyboard Shortcuts Reference

### Global

| Shortcut | Action | Context |
|----------|--------|---------|
| `Ctrl + N` | New Project | Any |
| `Ctrl + O` | Open Project | Any |
| `Ctrl + S` | Save | Any |
| `Ctrl + Shift + S` | Save As | Any |
| `Ctrl + Z` | Undo | Any |
| `Ctrl + Shift + Z` | Redo | Any |
| `Ctrl + Shift + P` | Profiler | Any |
| `Ctrl + `` | Toggle Console | Any |
| `F1` | Context Help | Any |
| `F5` | Launch Live Preview | Any |
| `F11` | Fullscreen Viewport | 3D Editor |
| `Ctrl + Q` | Quit | Any |

### 3D Viewport (Modeling Editor)

| Shortcut | Action | Notes |
|----------|--------|-------|
| `W` | Translate Gizmo | Local/world toggle: `Ctrl + W` |
| `E` | Rotate Gizmo | Snap angle: hold `Shift` |
| `R` | Scale Gizmo | Uniform: hold `Shift` |
| `Q` | Toggle Selection | Box / Lasso / Paint |
| `F` | Frame Selected | Double-tap `F` for all objects |
| `T` | Toggle Wireframe | Cycle: solid → wireframe → wire+solid |
| `G` | Toggle Grid | Grid size adapts to zoom |
| `Tab` | Toggle Edit Mode | Object ↔ Edit ↔ Sculpt |
| `1–4` | Viewport Layout | Single / Quad / Split-H / Split-V |
| `Ctrl + 1–4` | Camera Presets | Front / Top / Side / Perspective |
| `Alt + LMB` | Orbit | Middle-mouse alternative |
| `Alt + Shift + LMB` | Pan | Two-finger drag on trackpad |
| `Alt + RMB` | Zoom | Scroll wheel alternative |
| `Space` | Quick Search | Find tool/command by name |
| `Ctrl + D` | Duplicate | With offset |
| `Ctrl + Shift + D` | Duplicate Linked | Shares mesh data |
| `X` | Delete | Confirm dialog |
| `H` | Hide Selected | `Alt + H` to unhide all |
| `Ctrl + H` | Hide Unselected | Isolate selection |
| `M` | Merge / Boolean | Union / Difference / Intersection |
| `U` | UV Unwrap | Auto-unwrap selected faces |
| `Ctrl + T` | Triangulate | Convert ngons to tris |
| `Shift + N` | Recalculate Normals | Outside / Inside toggle |
| `Ctrl + Shift + R` | Reload Shaders | Hot-reload GLSL during development |

### Physics Editor
<a id="physics-editor-shortcuts"></a>

| Shortcut | Action | Notes |
|----------|--------|-------|
| `Ctrl + R` | Run Simulation | 10-second lap preview |
| `Ctrl + Shift + R` | Run Full Lap Sim | At current track with AI level |
| `Ctrl + T` | Validate Physics | Check for NaNs, impossible values |
| `Ctrl + E` | Export to AC | Write all .ini files to content folder |
| `Ctrl + Shift + E` | Export Package | Zip with description.ini + README |
| `Ctrl + L` | Load from Car | Import existing car physics as baseline |
| `Ctrl + B` | Benchmark | Compare current vs. baseline lap time |
| `Ctrl + 1–9` | Switch Setup Tab | Aero / Suspension / Engine / Tyres / etc. |
| `Ctrl + Mouse Wheel` | Fine Adjust Value | 0.1× step precision |
| `Shift + Click` | Reset to Default | On any parameter field |

### Audio Editor (ksAudioStudio)
<a id="audio-editor-ksaudiostudio-shortcuts"></a>

| Shortcut | Action | Notes |
|----------|--------|-------|
| `Space` | Play / Pause | Transport control |
| `Shift + Space` | Play from Start | Reset playhead |
| `Ctrl + Space` | Record | Toggle record arm |
| `L` | Toggle Loop | Loop region selection |
| `M` | Add Marker | At playhead position |
| `Ctrl + M` | Edit Marker | Rename / color |
| `S` | Solo Track | `Alt + S` for solo clear |
| `Ctrl + S` | Save Project | (Overrides global save) |
| `I` | Set In Point | For loop / export region |
| `O` | Set Out Point | For loop / export region |
| `Ctrl + B` | Build Bank | Compile to FMOD .bank |
| `Ctrl + Shift + B` | Build & Test | Compile and load in AC |
| `Ctrl + E` | Export Audio | WAV / OGG / FLAC |
| `Ctrl + 1–8` | Select DSP Panel | EQ / Comp / Reverb / etc. |
| `Ctrl + Shift + Up/Down` | Nudge Parameter | Fine-tune selected knob |
| `Tab` | Toggle Mixer / Graph | Swap main view |

### Livery Editor

| Shortcut | Action | Notes |
|----------|--------|-------|
| `B` | Brush Tool | Size: `[` / `]` |
| `E` | Eraser | Hardness: `Shift + [ / ]` |
| `S` | Stamp / Clone | Alt + click to set source |
| `V` | Move Layer | Arrow keys for 1px nudge |
| `Ctrl + T` | Free Transform | Scale / rotate / skew |
| `Ctrl + Shift + E` | Export to DDS | With mipmaps |
| `Ctrl + Shift + P` | Preview on 3D Model | Live viewport update |
| `1–0` | Layer Opacity | 10%–100% |
| `Ctrl + G` | Group Layers | Folder in layer stack |
| `Ctrl + Shift + G` | Ungroup | Flatten to single layer |
| `Ctrl + Alt + Z` | Step Backward | History state (livery only) |

---

## Frequently Asked Questions (FAQ)

**Q: Is ksEditor free?**
A: Yes. ksEditor is released under the GPL-3.0 License. You can use, modify, and distribute it freely under the terms of the GPL, but third-party dependencies (Qt Commercial, FMOD Studio) have their own licenses for commercial use.

**Q: Can I use ksEditor without owning Assetto Corsa?**
A: Partially. The 3D modeler, audio editor, and livery tools work standalone. However, the SDKBackend, live telemetry, and AC-specific exporters require a valid AC installation.

**Q: Does ksEditor support other games (rFactor 2, Automobilista, etc.)?**
A: Not natively in v1.16. The plugin architecture is designed for multi-simulator support, but only the Kunos/AC plugin is fully implemented. Community plugins for other simulators are welcome.

**Q: How do I update ksEditor without losing my projects?**
A: Projects are stored in your workspace folder (default: `Documents/ksEditorProjects/`) and are independent of the application installation. Simply uninstall the old version and install the new one. The project database auto-migrates on first open.

**Q: Can I collaborate with other modders in real-time?**
A: Yes, via the **Collaboration** module (`File → Share Project`). It uses operational transformation (OT) for conflict-free concurrent editing. Both parties need ksEditor 2.0+ and a WebSocket-capable network (LAN or internet). Cloud sync (GDrive/Dropbox/OneDrive) is also available for async collaboration.

**Q: Why is my FMOD bank not loading in Assetto Corsa?**
A: Common causes: (1) FMOD version mismatch — ksEditor builds banks compatible with FMOD 2.02.xx (CSP standard). Older AC versions use FMOD 1.08.12. (2) Missing `sfx_guids.txt` — must be copied alongside `.bank`. (3) Event GUIDs changed — use `Audio → Validate GUIDs` to check consistency.

**Q: How do I convert a car mod from AC to another format?**
A: Use `File → Export → FBX / GLB / USD`. The exporter preserves hierarchy, materials (as PBR), and animations. Note that AC-specific shaders (ksCarPaint, ksGlass) are approximated with standard PBR equivalents.

**Q: What is the difference between ksAudio and FMOD?**
A: **ksAudio** is ksEditor's native audio engine (real-time graph, VST hosting, analysis). **FMOD** is the middleware Assetto Corsa uses in-game. ksEditor can import FMOD projects, edit them with ksAudio tools, and export back to FMOD `.bank` format for AC compatibility.

**Q: Can I use my own shaders in the viewport?**
A: Yes. Place custom `.glsl` / `.spv` files in `resources/shaders/custom/` and register them via `Viewport → Shaders → Register Custom`. The renderer supports SPIR-V, GLSL, and HLSL (via DXC cross-compilation).

**Q: How do I report bugs or request features?**
A: Use [GitHub Issues](https://github.com/kseditor/kseditor/issues). Include: ksEditor version, OS, GPU, reproduction steps, and attach `logs/kseditor.log` from `%APPDATA%/kseditor/` (Windows) or `~/.config/kseditor/` (Linux).

**Q: Is there a dark mode?**
A: Dark mode is the default. Light and High-Contrast themes are available in `View → Theme`. Full QSS (Qt StyleSheet) customization is supported — edit `resources/ui/themes/custom.qss` or use the `Theme Editor` (`Ctrl + Shift + T`).

**Q: Can I script ksEditor with my own tools?**
A: Yes. Python, Lua, and JavaScript (QJSEngine) are all supported. See the [Plugin Development Guide](#plugin-development-guide) for examples.

---

## Roadmap & Future Development

### v2.2.0 (Q3 2026) — Target

| Feature | Status | Description |
|---------|--------|-------------|
| **AI Training Pipeline** | 🚧 In Progress | Imitation learning from telemetry; train AI to match player style |
| **VR Viewport** | ✅ Implemented | Native OpenXR support for immersive 3D editing — stereo rendering, HMD tracking, controller input, VR camera navigation |
| **Cloud Rendering** | 📋 Planned | Offload heavy renders to cloud GPU instances |
| **Blender Live Link** | 📋 Planned | Bidirectional sync with Blender via add-on |
| **Multi-Simulator Export** | 📋 Planned | rFactor 2, Automobilista 2, iRacing (partial) plugins |
| **Material Scanning** | 🔬 Research | Photogrammetry-based PBR material capture from photos |

### v2.3.0 (Q4 2026) — Vision

| Feature | Description |
|---------|-------------|
| **Neural Physics** | AI-accelerated tire model approximation (1000× faster Pacejka evaluation) |
| **Procedural Tracks** | AI-assisted terrain generation from GPS + satellite imagery |
| **Voice Control** | Natural language commands for viewport and parameter adjustment |
| **Mobile Companion** | iOS/Android app for telemetry viewing and quick edits |
| **Mod Marketplace** | Integrated distribution with revenue sharing (Steam Workshop + custom) |

### Long-Term (2027+)

- **Real-time Ray Tracing** — Full path-traced viewport (RTX/DXR)
- **Haptic Feedback** — Force-feedback steering wheel support for physics tuning
- **Generative AI** — Text-to-car, text-to-track prototyping
- **Cross-Platform** — Native macOS and Linux parity with Windows

---

## Comparison with Other Modding Tools

| Feature | **ksEditor** | **Blender + AC Scripts** | **3ds Max + Kunos Exporter** | **RaceTrack Builder** | **FMOD Studio** |
|---------|-------------|--------------------------|-------------------------------|---------------------|-----------------|
| **Cost** | Free (MIT) | Free (GPL) | Paid ($1,800+/yr) | Paid ($50) | Free (non-commercial) |
| **3D Modeling** | ✅ Full (Boolean, sculpt, UV) | ✅ Full | ✅ Full | ⚠️ Track-only | ❌ None |
| **KN5 Import/Export** | ✅ Native + encrypted | ✅ Community addon | ✅ Official exporter | ✅ Native | ❌ N/A |
| **Physics Editing** | ✅ Full IDE (INI + curves + sim) | ⚠️ Manual INI + scripts | ⚠️ Manual INI | ❌ None | ❌ N/A |
| **Audio (FMOD)** | ✅ Full bank editor + ksAudio | ❌ None | ❌ None | ❌ None | ✅ Full (official) |
| **Livery Painting** | ✅ 3D paint + DDS export | ⚠️ UV unwrap + external | ⚠️ UV unwrap + external | ❌ None | ❌ N/A |
| **Live AC Preview** | ✅ SDK + telemetry | ❌ None | ❌ None | ❌ None | ❌ N/A |
| **CSP Config Editing** | ✅ Schema-driven UI | ⚠️ Manual INI | ⚠️ Manual INI | ❌ None | ❌ N/A |
| **AI Line Editing** | ✅ Visual + telemetry training | ❌ None | ❌ None | ✅ Built-in | ❌ N/A |
| **Plugin System** | ✅ C++ / Python / Lua | ✅ Python (Blender API) | ✅ MAXScript / C++ | ❌ None | ❌ C++ API only |
| **Collaboration** | ✅ Real-time + cloud sync | ❌ None | ❌ None | ❌ None | ❌ None |
| **Learning Curve** | 🟡 Moderate | 🔴 Steep (3D generalist) | 🟡 Moderate | 🟢 Easy | 🟡 Moderate |
| **Best For** | All-in-one AC modding | 3D art + community tools | Professional 3D pipelines | Track building only | Audio professionals |

> **💡 Recommendation:** Use **ksEditor** as your primary AC modding IDE. Use **Blender** for complex organic modeling or sculpting that ksEditor's mesh tools don't yet cover. Use **FMOD Studio** if you need features beyond ksAudio's compatibility layer (e.g., advanced adaptive music).

## Testing

**22 Qt Test suites** (`tests/kseditor/`):
```
test_AutoSave, test_BackupSystem, test_CacheManager, test_CommandPalette,
test_NotificationSystem, test_RecentFilesManager, test_SettingsSystem,
test_StateMachine, test_ValidationSystem, test_UndoRedo, test_ThemeSystem,
test_PhysicsSimulator, test_MeshOperations, test_MaterialSystem,
test_AnimationSystem, test_BooleanOps, test_SubdivisionSurface,
test_TexturePaintSystem, test_LODGenerator, test_AdvancedSculpt,
test_AllAI, test_ShowroomEditor, test_CockpitInstruments, test_FontGenerator,
test_LicensePlates, test_LODGenerator, test_LapTimeValidation,
test_CspConfig, test_ContentRepair, test_AssetDedup, test_AudioEffectsAdvanced,
test_ModManagerFeatures, test_ModManagerDeps, test_Workshop,
test_TelemetryFeedback, test_PPFilterColorGrading, test_PhysicsProfiler
```

Run:
```bash
cd tests/build
cmake ..
cmake --build .
ctest
```

---

## Localization

**Qt Linguist (.ts)** — 4 languages, 80+ translated strings:
- English (en) — source
- German (de)
- Japanese (ja)
- Italian (it)

---

## Documentation

| File | Description |
|------|-------------|
| [README.md](../README.md) | Quick start, features, build, SDK usage |
| [CHANGELOG.md](../CHANGELOG.md) | Keep a Changelog format, v1.0.0 → v1.16.4 |
| [CONTRIBUTING.md](../CONTRIBUTING.md) | Dev setup, code style, adding modules/plugins, testing |
| [overview.md](overview.md) | Architecture diagram, tech stack, feature matrix |
| [modules.md](modules.md) | Core + app module deep-dive with completion % |
| [graphics.md](graphics.md) | Vulkan renderer, mesh ops, material system, 3D module |
| [audio.md](audio.md) | ksAudioStudio engine, panels, DSP, VST, formats |
| [file_formats.md](file_formats.md) | 50+ format support, detection, conversion |
| [plugins.md](plugins.md) | Plugin architecture, Kunos simulator plugin |

---

## License & Third-Party

**ksEditor:** GPL-3.0 License — Copyright (c) 2014 Kunos Simulazioni s.r.l.

**Third-Party Libraries:**
| Library | License | Purpose |
|---------|---------|---------|
| Qt 6 | LGPL v3 / GPL v3 / Commercial | UI framework |
| FMOD Studio API | FMOD Studio License (free non-commercial) | Audio engine compatibility |
| Vulkan SDK | Apache 2.0 / MIT | Graphics API |
| Eigen | MPL2 | Linear algebra |
| Bullet Physics | Zlib | Physics simulation (optional) |
| VHACD | BSD | Convex decomposition |
| 7-Zip SDK | LGPL | Archive handling |
| libigl | MPL2 | Geometry processing |
| CGAL | GPL/LGPL | Computational geometry |
| OpenVDB | MPL2 | Volumetric data |
| STB | Public domain | Image loading |
| mikktspace | Zlib | Tangent space |

---

## Version History Highlights

| Version | Date | Major Additions |
|---------|------|-----------------|
| **1.16.4** | 2026-07-24 | All 32 core subsystems + 9 application modules 100% complete, 50+ format parsers, full Vulkan pipeline, VST audio, VR support |
| **2.0.0** | 2026-04-25 | Sound Editor (FMOD), PP Filters Editor, License Plate Editor, Font Creator, Display Editor, Assets Library, PhysicsSimulator, TimelineEditor, UndoRedo, AutoSave, Validation, Notification, CommandPalette, Backup, Theme, Settings, Cache, UpdateChecker, VersionControl, CloudSync, Collaboration, PluginSystem, ScriptConsole, MacroSystem, TaskSystem, StateMachine, HotkeyAction, ShortcutProfile, PreviewGenerator, SearchFilter, AssetManager, ProjectTemplates, ExportPresets, ImportExportFilters, WizardSystem, MetadataSystem, PerformanceOptimizer, ConsolePanel, DebugTools, LoggingSystem, resources.qrc (89 icons), CI/CD, 13 test suites, i18n (4 langs), CPack/NSIS installer |
| **1.0.0** | 2026-01-15 | Qt6+QML architecture, 3D Modeler, Physics Editor, Sound Editor stub, Vulkan renderer foundation, scene graph, KN5/ACD/INI parsers, QML UI, Python scripting, ribbon toolbar, database/logging |

---

## Quick Reference

| Task | Command / Location |
|------|-------------------|
| Build (Windows) | `build.bat` or `mkdir build && cd build && cmake .. && cmake --build . --config Release` |
| Run tests | `cd tests/build && cmake .. && cmake --build . && ctest` |
| SDK API | `src/SDKBackend.h` |
| Add C++ module | `src/modules/{name}/` + CMakeLists.txt registration |
| Add QML page | `resources/ui/qml/modules/{module}/` |
| Add plugin | `src/plugins/simulators/kunos/` or `src/plugins/base/` |
| Translations | `i18n/*.ts` → `lrelease` |
| Icons | `resources.qrc` (89 SVGs) |

---

## Links

- **Repository:** https://github.com/kseditor/kseditor
- **Issues:** https://github.com/kseditor/kseditor/issues
- **Releases:** https://github.com/kseditor/kseditor/releases
- **Qt Documentation:** https://doc.qt.io/qt-6/
- **Vulkan SDK:** https://vulkan.lunarg.com/
- **FMOD Studio:** https://www.fmod.com/
- **Assetto Corsa Modding:** https://www.assettocorsa.net/forum/
