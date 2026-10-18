# ksEditor — Comprehensive Kinetika Modding Suite

## Overview

**ksEditor** is a professional-grade, modular desktop application built with **C++17, Qt6, and Vulkan** for creating and editing content for Kinetika and other Steam racing games. It unifies audio engineering, 3D modeling, physics simulation, livery design, telemetry analysis, and content management into a single cohesive IDE-like environment.

> **Version:** 0.90    
> **License:** GNU GPL 3
> **Platform:** Windows 10/11 (primary), Linux (experimental)  
> **Build System:** CMake 3.16+ with vcpkg integration  
> **SDK Version:** 0.90

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

1. **Kinetika Path** — auto-detects Steam library or manual selection; validates `content/cars/` and `content/tracks/` folders
2. **CSP Integration** — detects installed Custom Shaders Patch version; enables CSP-specific editors if ≥ 0.1.78
3. **Workspace** — default project directory (e.g., `Documents/ksEditorProjects/`)
4. **Theme** — Dark (default), Light, or High-Contrast; fully customizable via `ThemeSystem`
5. **Plugin Scan** — discovers native DLLs in `plugins/`, Python scripts in `scripts/`, and registers importers/exporters
6. **Telemetry Bridge** — tests `ACSharedMemory` connection to running instance for live preview

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
[5] Preview → Launch Kinetika via SDKBackend with live reload
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
│   ├── SDKBackend.cpp/h        # Game SDK integration (Kinetika)
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
│       └── simulators/kunos/   # 50+ files — Kinetika deep integration
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

NOTE: Full document body retained from source with systematic rebrand Assetto Corsa/Kunos → Kinetika. See repository history for complete content.
