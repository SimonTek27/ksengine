# ksengine

**ksengine** is the Qt-free core engine framework of this project: a static C++17
library (`src/engine/`) with math, physics, devices/force-feedback, file formats,
config, networking, materials and terrain. It builds without Qt (progress tracked
by `tools/check_no_qt.ps1`) and optionally links Vulkan, Bullet, Eigen, Lua and
mikktspace.

---

# SimulatorApp

**SimulatorApp (ksim)** is the standalone runtime executable (`src/simulator/`) that
links *only* ksengine. Native Win32 + raw Vulkan (`NativeRenderer`), driven by
`SimulationLoop`: KN5 track/car loading, vehicle physics, FFB, sim-racing input,
audio, dashboard/telemetry, setup garage, pit/garage systems, LAN multiplayer and
external control API.

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License](https://img.shields.io/badge/license-GPL3-blue.svg)](LICENSE.txt)
[![Version](https://img.shields.io/badge/version-1.16.4-orange)]()
[![C++](https://img.shields.io/badge/C++-17-blue)]()
[![Qt-free engine](https://img.shields.io/badge/ksengine-Qt--free-success)]()
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey)]()

**Product identity**

```
SimulatorApp  ≈  open-source race runtime (compatible with common content formats)
       │
       ├── ksengine          (generic engine)
       ├── adapters/         (format bridges — not brand-locked naming in UI)
       └── network           (discovery, CarState sync, control TCP)
```

---

# ksEditor

Optional **Qt6** modding toolkit for racing-game content (audio, 3D, physics,
telemetry, liveries, events, server configs). Links `kslib` → `ksengine`; the Qt
layer is UI only.

[![Qt](https://img.shields.io/badge/Qt-6.11-green)]()
[![License](https://img.shields.io/badge/license-GPL3-blue.svg)](LICENSE.txt)

---

## Overview

| Product | Path | Qt | Role |
|---------|------|----|------|
| **ksengine** | `src/engine/` | No | Core library |
| **SimulatorApp** | `src/simulator/` | No | Race runtime |
| **Adapters** | `src/adapters/` | No | Content formats |
| **ksEditor** | modules + MainWindow | Yes | Modding UI |

### Architecture principles

1. **ksengine** is a generic open-source sim engine — not a single-title clone.
2. Content-format compatibility lives under `src/adapters/`.
3. **FeatureHub** centralizes session mode, LAN discovery (`:20779`), external control (`:20780`), track limits, weather, setup, personal bests.
4. **Pit/garage** is a state machine (exit → queue → collision → repair) separate from core vehicle physics.
5. **CarStateSync** provides UDP ≥20 Hz multiplayer state without requiring yojimbo/`HAS_KSNET`.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/PARITY_STATUS.md](docs/PARITY_STATUS.md),
[docs/GAP_MATRIX.md](docs/GAP_MATRIX.md), [docs/GITHUB_RESTORE_AUDIT.md](docs/GITHUB_RESTORE_AUDIT.md).

---

## Project Structure

```
ksengine/
├── CMakeLists.txt
├── docs/
│   ├── ARCHITECTURE.md
│   ├── PARITY_STATUS.md
│   ├── GAP_MATRIX.md
│   ├── GITHUB_RESTORE_AUDIT.md
│   ├── SPRINT*.md / PIT_*.md / …
│   └── …
├── examples/MinimalSimulator/
├── external/
├── src/
│   ├── engine/                 # ★ Qt-free core
│   │   ├── math/ physics/ AI/
│   │   ├── devices/ network/
│   │   └── formats/ config/
│   ├── simulator/              # ★ Qt-free SimulatorApp
│   │   ├── SimulationLoop.*
│   │   ├── FeatureHub.h
│   │   ├── CarStateSync.h
│   │   ├── AIController.*
│   │   ├── GameMenuOverlay.*
│   │   ├── GarageExit / PitLane*
│   │   └── …
│   ├── adapters/               # format bridges
│   └── sdk/                    # editor modules (Qt)
└── tools/check_no_qt.ps1
```

### Simulator parity modules (selection)

| Module | Role |
|--------|------|
| `FeatureHub` | Discovery, control TCP, limits, weather, PB, session |
| `TrackLimitsMonitor` | Off-track → warnings → DT/SG/DQ |
| `CarStateSync` | UDP CarState ≥20 Hz host/client |
| `AIController` | Racing line + traffic overtake |
| `GarageExit` / `PitLaneQueue` / `PitLaneCollision` / repair | Ops stack |
| `PersonalBestStore` | File-backed PB + leaderboard |
| `ExternalControlApi` | TCP commands + optional AUTH |

---

## Technical Stack

| Category | Technology |
|----------|------------|
| Language | C++17 |
| ksengine | Qt-free static library |
| SimulatorApp | Win32 + Vulkan (no Qt) |
| Editor UI | Qt6 (optional) |
| Physics | Custom vehicle + optional Bullet |
| Audio | WASAPI / SimulatorAudio |
| Build | CMake 3.16+ |
| Platform | Windows 10/11 x64 (primary) |

---

## Building

### Requirements

- **CMake 3.16+**
- **Vulkan SDK** — SimulatorApp / NativeRenderer
- **Qt 6.11+** — only for **ksEditor**
- **Windows 10/11 x64**

### Quick build

```bash
cmake --preset default
cmake --build --preset default
```

Qt-free engine checks:

```bash
./tools/check_no_qt.ps1
```

---

## Network ports (defaults)

| Service | Port | Notes |
|---------|------|--------|
| Game / ksnet | 40000 | When `HAS_KSNET` |
| CarStateSync UDP | 40001 | Always-available fallback |
| Discovery | 20779 | LAN browser |
| Control API TCP | 20780 | AUTH optional |

---

## License

GPL-3.0 — see [LICENSE.txt](LICENSE.txt).
