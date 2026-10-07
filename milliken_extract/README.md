# ksengine

**ksengine** is the Qt-free core engine framework: a static C++17 library
(`src/engine/`) with math, physics, devices/FFB, file formats, config, networking,
materials and terrain. Builds without Qt (`tools/check_no_qt.ps1`). Optional:
Vulkan, Bullet, Eigen, Lua, mikktspace.

---

# SimulatorApp (ksim)

Standalone **Qt-free** race runtime (`src/simulator/`) linking only ksengine.
Win32 + Vulkan (`NativeRenderer`) + `SimulationLoop`:

- KN5 track/car load, vehicle physics, FFB & devices
- Garage / pit (exit, queue, collision, repair)
- FeatureHub: session, LAN discovery, control TCP, track limits, weather, PB
- AI racing line + traffic overtake
- CarStateSync UDP ≥20 Hz (no ksnet required)
- Shared memory / UDP / TCP telemetry

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License](https://img.shields.io/badge/license-GPL3-blue.svg)](LICENSE.txt)
[![Version](https://img.shields.io/badge/version-1.16.4-orange)]()
[![C++](https://img.shields.io/badge/C++-17-blue)]()
[![ksengine](https://img.shields.io/badge/ksengine-Qt--free-success)]()
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey)]()

```
SimulatorApp  (open race runtime; format-compatible content)
       │
       ├── ksengine           generic engine
       ├── adapters/          content-format bridges
       └── network            discovery · CarState · control API
```

---

# ksEditor

Optional **Qt6** modding UI (audio, 3D, physics, liveries, events, server config).
Links `kslib` → `ksengine`; Qt is UI only.

[![Qt](https://img.shields.io/badge/Qt-6.11-green)]()
[![License](https://img.shields.io/badge/license-GPL3-blue.svg)](LICENSE.txt)

---

## Architecture

| Layer | Path | Qt | Role |
|-------|------|----|------|
| **ksengine** | `src/engine/` | No | Core library |
| **SimulatorApp** | `src/simulator/` | No | Race runtime |
| **Adapters** | `src/adapters/` | No | Formats |
| **ksEditor** | modules / MainWindow | Yes | Modding UI |

**Principles**

1. ksengine is a generic open-source sim engine (not a single-title clone).
2. Brand-specific formats stay under `adapters/`; UI stays product-neutral.
3. FeatureHub owns session, discovery `:20779`, control `:20780`, limits, weather, PB.
4. Pit/garage is a state machine separate from vehicle integrate.
5. CarStateSync UDP works without `HAS_KSNET`; ksnet path remains the preferred multiplayer transport.

Docs: [ARCHITECTURE](docs/ARCHITECTURE.md) · [PARITY_STATUS](docs/PARITY_STATUS.md) ·
[GAP_MATRIX](docs/GAP_MATRIX.md) · [GITHUB_RESTORE_AUDIT](docs/GITHUB_RESTORE_AUDIT.md)

---

## Project structure

```
ksengine/
├── CMakeLists.txt
├── docs/
├── examples/MinimalSimulator/
├── src/
│   ├── engine/          # Qt-free core
│   ├── simulator/       # Qt-free SimulatorApp
│   │   ├── SimulationLoop.*
│   │   ├── FeatureHub.h · CarStateSync.h
│   │   ├── AIController.* · MultiCarManager.*
│   │   ├── GameMenuOverlay.* · GarageExit / PitLane*
│   │   └── …
│   ├── adapters/
│   └── sdk/             # editor (Qt)
└── tools/check_no_qt.ps1
```

### Key simulator modules

| Module | Role |
|--------|------|
| FeatureHub | Discovery, control, limits, weather, PB, session |
| TrackLimitsMonitor | Warnings → time / DT / SG / DQ |
| CarStateSync | UDP state ≥20 Hz |
| AIController | Spline + overtake |
| GarageExit / PitLane* | Ops stack |
| PersonalBestStore | File PB + leaderboard |
| ExternalControlApi | TCP + optional AUTH |

---

## Network defaults

| Service | Port |
|---------|------|
| Game (ksnet) | 40000 |
| CarStateSync | 40001 |
| Discovery | 20779 |
| Control API | 20780 |

---

## Build

```bash
cmake --preset default
cmake --build --preset default
./tools/check_no_qt.ps1   # engine Qt-free check
```

- **Vulkan SDK** — SimulatorApp  
- **Qt 6.11+** — ksEditor only  
- **Windows 10/11 x64** primary

---

## License

GPL-3.0 — [LICENSE.txt](LICENSE.txt)
