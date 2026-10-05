# ksengine

**ksengine** is the Qt-free core engine framework of this project: a static C++17
library (`src/engine/`) with math, physics, devices/force-feedback, file formats,
config, networking, materials and terrain. It builds without Qt (progress tracked
by `tools/check_no_qt.ps1`) and optionally links Vulkan, Bullet, Eigen, Lua and
mikktspace.

---

# SimulatorApp

**SimulatorApp** is the standalone runtime executable (`src/simulator/`) that links
*only* ksengine. It is a native Win32 window with a raw Vulkan renderer (`NativeRenderer`, precompiled `.spv` shaders), driving `SimulationLoop`: KN5 track/car loading, vehicle physics, FFB and sim-racing device input, audio, dashboard/telemetry overlays, setup garage and multiplayer networking. `examples/MinimalSimulator` shows the minimal way to run it.

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License](https://img.shields.io/badge/license-GPL3-blue.svg)](LICENSE.txt)
[![Version](https://img.shields.io/badge/version-0.90-orange)]()
[![C++](https://img.shields.io/badge/C++-17-blue)]()
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey)]()

---

# ksEditor

A comprehensive, professional-grade modding toolkit for racing-game content. ksEditor provides a unified environment for editing audio, 3D models, physics, telemetry, liveries, events, server configs, and more.

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License](https://img.shields.io/badge/license-GPL3-blue.svg)](LICENSE.txt)
[![Version](https://img.shields.io/badge/version-0.90-orange)]()
[![Qt](https://img.shields.io/badge/Qt-6.11-green)]()
[![C++](https://img.shields.io/badge/C++-17-blue)]()
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey)]()

---

## Overview

This repository ships three products that share one core:

1. **ksengine** — Qt-free C++17 static library (math, physics, devices, formats).
2. **SimulatorApp (ksim)** — Qt-free race runtime (Vulkan + `SimulationLoop`).
3. **ksEditor** — optional Qt6 modding UI that links ksengine for all non-UI work.

The editor (`kseditor.exe`) links `kslib`, which PUBLIC-links `ksengine`; `ksengine.lib` is copied next to the executable at build time. Physics, file-format parsing, FFB/device handling and engine logic used by editor modules come from ksengine — the Qt layer only provides the UI.

ksEditor still covers the full modding pipeline (audio, 3D, physics, liveries, events, server config) and **ksAudioStudio** for FMOD-compatible banks. See **Architecture** below and the full [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

---

## Architecture

> **Full detail:** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — layers, FeatureHub, pit/garage, control API, data flow, threading.

ksengine is organized in **layers**. The editor is optional; the engine and simulator build without Qt.

```
┌─────────────────────────────────────────────────────────────┐
│  ksEditor (Qt6)                                             │
│  MainWindow + modding modules (modeler, audio, physics UI…) │
└────────────────────────────┬────────────────────────────────┘
                             │ links
                             ▼
┌─────────────────────────────────────────────────────────────┐
│  SimulatorApp / ksim  (Qt-free)                             │
│  Win32 + Vulkan · SimulationLoop · FeatureHub · native UI   │
│  session modes · pit/garage · telemetry · multiplayer       │
└────────────────────────────┬────────────────────────────────┘
                             │ links
                             ▼
┌─────────────────────────────────────────────────────────────┐
│  ksengine  (Qt-free static lib)                             │
│  Math · physics · devices/FFB · formats · scene · network   │
│  Engine + EngineModule registry · fixed-timestep core       │
└────────────────────────────┬────────────────────────────────┘
                             │ uses (optional)
                             ▼
┌─────────────────────────────────────────────────────────────┐
│  adapters/  (format bridges, not game-specific runtime)     │
│  assetto_corsa → shared memory, surfaces.ini, CSP configs   │
└─────────────────────────────────────────────────────────────┘
```

### Responsibilities

| Component | Responsibility |
|-----------|----------------|
| **ksengine** | Generic sim engine: vehicle dynamics, input/FFB, file I/O, ECS scene, low-level net. No UI framework. |
| **SimulatorApp** | Standalone race client: load track/car, drive loop, HUD/menu, sessions, pits, replay, LAN discovery, control API. |
| **adapters/** | Read/write content formats used by existing mods. Keeps the engine independent of any one title. |
| **ksEditor** | Modding toolkit UI on top of ksengine; not required to run ksim. |

### Runtime data flow (SimulatorApp)

```
Input (wheel/keys) → InputManager → VehicleSimulator (ksengine physics)
                                         │
                                         ▼
                              SimulationLoop tick @ fixed dt
                                         │
                    ┌────────────────────┼────────────────────┐
                    ▼                    ▼                    ▼
              FeatureHub            NativeRenderer         Telemetry
         (session, limits,         (Vulkan frame)     (UDP/TCP/shared mem)
          weather, PB, API)
                    │
                    ▼
              GameMenuOverlay / NativeUiHub  (GPU UI pass)
```

### Design rules

1. **Qt only in the editor** — `src/engine/` and `src/simulator/` stay std/Vulkan.
2. **No hard dependency on a commercial title** — format adapters are isolated under `src/adapters/`.
3. **FeatureHub** centralizes session mode, LAN discovery (`:20779`), external control (`:20780`), track limits, weather, setup, and personal bests.
4. **Pit/garage** follows a clear state machine (garage exit → pit queue → collision → repair) separate from core vehicle physics.

---

## Features

### 3D Printing / VR / Workshop / Mod Manager / Assets Library

Editor modules for fabrication, OpenXR authoring, Steam Workshop, installed content, and centralized asset browsing.

### Event Editor / Server Config / FFB / Telemetry / Weather / Help

Career events, dedicated-server rules, force-feedback curves, high-rate telemetry, weather keyframes, context-sensitive help (F1).

## Modules

### Text / 3D / Audio / Physics editors

ksIDEEditor (LSP), ksModeler (Vulkan), ksAudioEditor + ksAudioStudio, ksPhysicsEditor (Pacejka, suspension, aero, powertrain).

### Display / Font / Paint / Plates / Showroom / PP Filters

Dashboards, font atlases, liveries, plates, showroom presentation, post-processing chains.

---

## Technical Stack

| Category | Technology |
|----------|------------|
| **Language** | C++17 |
| **Engine (ksengine)** | Qt-free static library |
| **SimulatorApp** | Win32 + Vulkan (no Qt) |
| **Editor UI** | Qt6 (Widgets + QML + Quick3D) |
| **3D Rendering** | Vulkan + GLSL / SPIR-V shaders |
| **Audio** | ksAudioStudio + WASAPI (simulator) |
| **Physics** | Bullet Physics 3.25+ (optional) |
| **Geometry** | CGAL, Eigen, libigl, OpenVDB, OpenSubdiv, mikktspace |
| **Scripting** | Python 3, Lua 5.4 |
| **Build System** | CMake 3.16+ |
| **Target Platform** | Windows 10/11 (x64) |

---

## Project Structure

```
ksengine/
├── CMakeLists.txt
├── docs/
│   ├── ARCHITECTURE.md          # Full architecture (layers, flow, API)
│   ├── PARITY_STATUS.md
│   ├── COMMIT_COMPLETE.md
│   ├── SIMLOOP_WIRING.md
│   └── …
├── examples/MinimalSimulator/
├── external/
├── include/
├── resources/                   # Qt UI / QML (editor only)
├── i18n/
├── tests/
└── src/
    ├── main.cpp / MainWindow.*  # ksEditor (Qt)
    ├── engine/                  # ★ Qt-free core library
    ├── simulator/               # ★ Qt-free SimulatorApp (ksim)
    ├── adapters/assetto_corsa/  # Format bridges
    └── sdk/
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the expanded tree and module tables.

**Layering**

| Layer | Path | Qt | Role |
|-------|------|----|------|
| **ksengine** | `src/engine/` | No | Core math, physics, devices, formats |
| **SimulatorApp (ksim)** | `src/simulator/` | No | Standalone race runtime |
| **Adapters** | `src/adapters/` | No | Content-format bridges |
| **ksEditor** | `src/MainWindow*`, modules | Yes | Modding UI on top of ksengine |

---

## Building

### Requirements

- **Qt 6.11+** — required only for **ksEditor**
- **CMake 3.16+**
- **Vulkan SDK** — for SimulatorApp / NativeRenderer
- **Windows 10/11 x64**

### Quick build

```bash
cmake --preset default
cmake --build --preset default
# Qt-free path: CMakeLists_ksengine_QtFree.txt / tools/check_no_qt.ps1
```

---

## License

GPL-3.0 — see [LICENSE.txt](LICENSE.txt).
