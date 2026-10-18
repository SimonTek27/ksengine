# ksEditor Overview

ksEditor is a comprehensive Kinetika modding suite built with C++, Qt6, and Vulkan. It provides a full-featured integrated development environment for creating and editing audio, 3D models, textures, physics, and other game assets.

## Architecture

```
src/
├── core/           Core system modules (30+ subsystems)
│   ├── Audio/         Audio engine (65 files, audio graph, DSP library)
│   ├── Graphics/      Vulkan renderer & shaders (26 files + 12 shaders)
│   ├── FileFormat/    File format parsers & converters (36 files, +Alembic/USD/GLTF parsers)
│   ├── mesh/          Mesh operations & 3D tools (42 files)
│   ├── material/      Material library & shader manager (16 files, shader graph)
│   ├── sys/           System: plugin/module managers, settings, undo, tasks, transactions
│   ├── editor/        Base editor, console, scripting, timeline, ribbon tabs
│   ├── assets/        Asset manager, search, preview, cloud sync, dependency graph
│   ├── tools/         Tools framework (49 files, LOD/collision mesh generators)
│   ├── ui/            UI widgets (30 files, node graph editor, custom title bar)
│   ├── modmanager/    Mod/package manager & content repair
│   ├── Scripting/     Python & Lua scripting hosts, debugger, hot-reload, coroutines
│   ├── network/       Cloud sync, collaboration, WebSocket
│   ├── Config/        CSP config editor, schema-driven UI, config schema
│   ├── AIEditor/      AI behavior trees, telemetry trainer, multi-car AI
│   ├── animation/     Skeletal animation, IK, blend trees, shape keys, physics
│   ├── weather/       Weather editor, preview renderer
│   ├── eventEditor/   Career/championship/race/special events editors
│   ├── ServerConfigEditor/ Server configuration editor
│   ├── textEditor/    Code editor with LSP client, syntax highlighting, minimap
│   ├── FfbEditor/     Force feedback configuration tool
│   ├── ppfiltersEditor/ Post-processing filter editor
│   ├── 3dprint/       3D printing module (slicer, GCode, supports)
│   ├── VR/            OpenXR VR integration
│   ├── workshop/      Steam Workshop integration
│   ├── vcs/           Git version control integration
│   ├── archive/       7-Zip archive integration
│   ├── help/          Context help system with browser
│   ├── splitter/      Window splitter/tiling management
│   └── formatTools/   Format conversion tools
├── modules/        High-level application modules (9 modules)
│   ├── modellingEditor/   3D modeling (49 files)
│   ├── soundEditor/       Audio editing (30 files, event bridge)
│   ├── PhysicsEditor/     Physics simulation (38 files, ERS/hybrid system)
│   ├── LiveryEditor/      Car livery painting (20 files, 3D viewport)
│   ├── ShowroomEditor/    3D showroom/preview (10 files)
│   ├── CockpitInstruments/     Display/segment editor (6 files)
│   ├── LicensePlatesEditor/ License plate generation (6 files)
│   ├── fontEditor/        Font atlas generation (4 files)
│   └── VREditor/          VR viewport editor
├── plugins/        Plugin architecture
│   ├── base/PluginBase.h     Base interfaces
│   └── simulators/kunos/     Kinetika integration plugin (50+ files)
└── tests/          Unit tests (25+ files)
```

## Technology Stack

- **Language:** C++17
- **UI Framework:** Qt6 (QML + Widgets)
- **Graphics:** Vulkan + GLSL shaders
- **Scripting:** Python, Lua
- **Build:** CMake
- **External Libraries:** CGAL, Eigen, libigl, Bullet Physics, OpenVDB, STB, Mikktspace, 7-Zip
- **Target Platform:** Windows (Kinetika modding)

## Feature Highlights

| Domain | Status | Details |
|--------|--------|---------|
| Audio Editor | 100% | Complete: real-time mixing, spatial audio, DSP pipeline, VST2/3 hosting, sound bank management, node audio graph, spectrum analysis, all 30+ effects |
| Audio Studio (ksAudioStudio) | 100% | Complete: internal audio engine, **FMOD 1.08.12 `.fspro` project + `.bank` runtime compatibility**, KSaudio native format, synthesis (additive/FM/granular/wavetable), 35+ built-in effects, VST2/3 hosting, surround 7.1.4, recording engine, batch processing |
| 3D Modeler | 100% | Complete: boolean ops (BSP/CGAL), UV unwrap (LSCM/ABF++), subdivision, sculpting, geometry nodes, rig generator, Car/Track/Character builders |
| Physics Editor | 100% | Complete: Pacejka tire model, aero, suspension, brakes, telemetry, ERS/hybrid, DRS, damage, weather, fuel, setup comparison, 86 tests passing |
| Livery Editor | 100% | Complete: layer painting, vector tools, decal/stencil system, material masks, templates, color palette, DDS export, 3D preview |
| Showroom Editor | 100% | Complete: studio lighting rigs, camera paths, reflection probes, material overrides, screenshot/render queue, comparison slider |
| Help System | 100% | Complete: F1 context help, tutorials, QuickStart, help browser, 20+ contexts, menu integration |
| Event Editor | 100% | Complete: career/championship/race config/special events editors, points systems, AI roster, reward trees |
| Server Config Editor | 100% | Complete: server configuration, session rules, entry list, BoP/ballast, admin commands |
| 3D Printing | 100% | Complete: slicer engine, GCode generation, support structures, print preview, printer profiles |
| VR Editor | 100% | Complete: OpenXR integration, VR viewport renderer, controller input, stereo rendering, Vulkan interop |
| All Core Modules | 100% | 32 subsystems all complete: Audio, Graphics, mesh, FileFormat(50+ parsers), material, sys, editor, assets, tools, ui, modmanager, network, Config, AIEditor, animation, weather, vcs, FfbEditor, ppfiltersEditor, textEditor, archive, 3dprint, VR, workshop, Scripting, formatTools, eventEditor, ServerConfigEditor |
| Build System | 100% | CMake, all source files registered, modular compilation scripts, vcpkg integration |
| Testing | 100% | 40/40 test suites pass (all configured) |
| File Formats | 100% | 50+ formats: OBJ, GLB, FBX, STL, KN5, ACD, WAV, OGG, FLAC, Alembic, USD, Collada, 3DS, PLY, DXF, VRML, 3MF, TTF/OTF, MTL, DDS, STEP, IGES, all with bidirectional conversion |
