# Architecture — ksengine

> Living document for the **ksengine** repository: layers, data flow, runtime systems, and design rules.
> Companion to the [README](../README.md) and [PARITY_STATUS](PARITY_STATUS.md).

---

## 1. Goals

| Goal | Meaning |
|------|---------|
| **Qt-free engine** | `src/engine/` builds with standard C++17 only (no Qt headers). |
| **Qt-free simulator** | `src/simulator/` (SimulatorApp / ksim) uses Win32 + Vulkan, not Qt widgets. |
| **Independent runtime** | ksim is a standalone race client; it does not require a commercial title at runtime. |
| **Format adapters** | Content formats (e.g. KN5, surfaces.ini, shared memory layouts) live under `src/adapters/`, not inside the engine core. |
| **Optional editor** | ksEditor (Qt6) is a modding UI that **links** ksengine; it is not required to drive or race. |

---

## 2. Product map

```
┌──────────────────────────────────────────────────────────────────┐
│  ksEditor (optional)                                             │
│  Qt6 · MainWindow · modeler / audio / physics / paint modules    │
│  Role: authoring & tools only                                    │
└────────────────────────────┬─────────────────────────────────────┘
                             │ links ksengine.lib
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  SimulatorApp / ksim                                             │
│  Win32 window · NativeRenderer (Vulkan) · SimulationLoop         │
│  Role: race client — sessions, pits, HUD, net, telemetry         │
└────────────────────────────┬─────────────────────────────────────┘
                             │ links
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  ksengine (static library)                                       │
│  Engine + modules: Math, physics, devices/FFB, FileFormat,       │
│  scene (ECS), network, Audio utilities, Graphics helpers         │
│  Role: generic simulation core                                   │
└────────────────────────────┬─────────────────────────────────────┘
                             │ optional use
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  adapters/                                                       │
│  assetto_corsa → shared memory, surfaces, CSP config parsers     │
│  Role: bridge existing mod formats without coupling the core     │
└──────────────────────────────────────────────────────────────────┘
```

| Product | Path | Qt | Output |
|---------|------|----|--------|
| **ksengine** | `src/engine/` | No | `ksengine.lib` |
| **SimulatorApp** | `src/simulator/` | No | ksim / SimulatorApp executable |
| **Adapters** | `src/adapters/` | No | Linked into engine or simulator as needed |
| **ksEditor** | `src/MainWindow*`, modules | Yes | `kseditor.exe` |

---

## 3. Source tree (detail)

```
ksengine/
├── CMakeLists.txt
├── docs/
│   ├── ARCHITECTURE.md          ← this file
│   ├── PARITY_STATUS.md
│   ├── SIMLOOP_WIRING.md
│   └── …
├── examples/MinimalSimulator/
├── external/                    # Eigen, Bullet, mikktspace, stb, …
└── src/
    ├── engine/                  # ★ core library
    │   ├── Engine.h / Engine.cpp / EngineModule.h
    │   ├── KsQtFreeGuard.h
    │   ├── Math/
    │   ├── physics/             # vehicle, Pacejka, suspension, aero, weather, …
    │   ├── devices/             # FFB + simracing (Fanatec, Logitech, Moza, …)
    │   ├── Graphics/
    │   ├── Audio/
    │   ├── FileFormat/
    │   ├── scene/               # ECS registry / systems
    │   ├── network/
    │   ├── vehicle/
    │   ├── material/ mesh/ terrain/
    │   └── Config/ Scripting/ AI/ Tools/
    │
    ├── simulator/               # ★ race runtime
    │   ├── SimulatorApp.cpp
    │   ├── SimulationLoop.*
    │   ├── FeatureHub.h
    │   ├── session / discovery / control / limits / weather / setup / PB
    │   ├── garage / pit stack
    │   ├── NativeRenderer / UI / audio / net / telemetry
    │   ├── ui/
    │   └── shaders/
    │
    ├── adapters/assetto_corsa/
    └── sdk/
```

---

## 4. Engine layer (`src/engine/`)

### 4.1 Engine shell

- **`Engine`** — process-wide registry of modules, fixed timestep (`setFixedDt`), start/stop.
- **`EngineModule`** — interface for pluggable subsystems (input, render, scene, script, …).
- **`KsQtFreeGuard.h`** — compile-time guard against accidental Qt includes in Qt-free targets.

### 4.2 Physics

| Area | Examples |
|------|----------|
| Vehicle | mass, powertrain, gears, fuel |
| Tires | Pacejka magic formula, wear, pressure, temperature |
| Suspension | kinematics, dampers, arb |
| Aero | downforce, drag, ride-height maps |
| Brakes | bias, thermal model |
| Surface | grip from weather / rubber deposit |
| Weather | ambient/track temp, wetness, rain, wind |

Physics is **Qt-free** and driven by the simulator’s fixed-step loop.

### 4.3 Devices & FFB

- Abstract FFB base + SDK factory.
- Vendor backends under `devices/simracing/`.
- Input state fed into `VehicleSimulator` each tick.

### 4.4 Scene & formats

- ECS-style **Registry** + systems (e.g. sync car transforms).
- **FileFormat**: KN5-related I/O, INI loaders, banks, mesh helpers.
- **Graphics**: helpers for editor viewports and simulator Vulkan path.

---

## 5. Simulator layer (`src/simulator/`)

### 5.1 Entry & loop

| File | Role |
|------|------|
| `SimulatorApp.cpp` | Win32 + Vulkan instance/surface, menu callbacks, main loop |
| `SimulationLoop` | Fixed-timestep physics, UI, telemetry, FeatureHub tick |
| `SimulationLoop_FeatureMethods.cpp` / `*_Features.inl` | `beginSession`, `startFeatureServices`, `loadReplayFile` |

**Tick outline**

```
poll input
applyInput → vehicle
while accumulator >= physicsDt:
    vehicle step
    update laps / surface
    FeatureHub.tick (discovery, control API, track limits, weather)
publish shared memory / UDP / TCP
render (NativeRenderer + GPU UI pass)
```

### 5.2 FeatureHub

Central façade for parity features (no Qt):

| Service | Type | Notes |
|---------|------|--------|
| **SessionController** | modes | Practice / Qualify / Race / Time Attack / Hotlap |
| **ServerDiscovery** | UDP `:20779` | LAN announce + query for server browser |
| **ExternalControlApi** | TCP `:20780` | InSim-style text commands |
| **TrackLimitsMonitor** | runtime | track-limit penalties → session |
| **WeatherControl** | runtime | time of day, presets, wetness |
| **PersonalBestStore** | disk | `user/pb` lap storage |
| **TrackLayout** | content | multi-layout tracks |
| **ApplySetup / SetupFile** | setup | load/save garage setup |
| **ReplayRecorder** | I/O | record / playback |

**Control API verbs (examples)**

```
SESSION <mode> [laps]
FLAG GREEN|YELLOW|RED|CHECKERED
PENALTY <car> DT|SG|TIME [value] [reason]
WEATHER <preset>
TIME <hours>
SETUP LOAD|SAVE <path>
REPLAY LOAD <path> | PLAY | STOP
CHAT <text>
RESULT
```

### 5.3 Session modes

| Menu | Mode | Typical params |
|------|------|----------------|
| PRACTICE | Practice | long time, unlimited laps |
| QUICK RACE | Race | fixed laps, grid |
| TIME ATTACK | TimeAttack / Hotlap | clean laps, PB focus |

Wiring: `GameMenuOverlay::onStartSessionRequested` → `SimulationLoop::beginSession` → race session start.

### 5.4 Garage & pit stack

```
InGarage → Preparing → BoxClear → RollingOut → PitLane
                ↑                      │
                └──── Blocked ←────────┘
```

| Module | Role |
|--------|------|
| **GarageSpawn** | Spawn pose / box assignment |
| **GarageExit** | State machine, hold controls, pit limiter, snap-to-box |
| **PitLaneQueue** | Spacing (≥ ~8 m), clearance, block garage exit |
| **PitLaneCollision** | 2D OBB/SAT, soft restitution, damage threshold |
| **PitLaneRepair** | Parallel jobs: tyres, fuel, body, suspension, aero, engine, … |
| **Mechanical damage** | Continuous wear + impact multipliers on power/handling/brakes |

### 5.5 Audio

| Piece | Role |
|-------|------|
| `SimulatorAudio` | Runtime playback |
| `VehicleAudioHook` | Bind vehicle events to banks |
| `CarEventVolumes` | Per-event gain/pitch (engine, turbo, wind, …) |
| `SoundsIniParser` / bank managers | Load content audio packs |

### 5.6 UI & rendering

- **NativeRenderer** — Vulkan swapchain, scene, shadows, post-process.
- **NativeUiHub** + **UiGpuPass** — GPU-side overlays (menu, dash, telemetry).
- **GameMenuOverlay** — main / single / multi / garage / replay / settings.
- **DashboardOverlay** / **TelemetryOverlay** — race HUD.

### 5.7 Networking & telemetry

| Channel | Use |
|---------|-----|
| **NetworkManager** | Host/join race sessions |
| **ServerDiscovery** | LAN browser (F2) |
| **UdpTelemetryBridge** | Outbound telemetry samples |
| **TcpTelemetryBridge** | Listen + stream |
| **AcSharedMemoryPublisher** (adapter) | Shared-memory live page for external tools |

### 5.8 Environment variables

| Variable | Purpose |
|----------|---------|
| `KS_REPLAY_FILE` | Path for menu “Load replay” |
| `KS_GOLDEN_CSV` | Golden telemetry export |
| `KS_AI_CARS` | AI grid size hint |

---

## 6. Adapters (`src/adapters/`)

Adapters **must not** define core physics or UI. They only parse or publish **formats** used by existing content ecosystems and keep ksengine free of title-specific APIs.

Current: `adapters/assetto_corsa/`

| Module | Role |
|--------|------|
| `AcSharedMemory*` | Live shared-memory layout publisher |
| `AcSurfacesLoader*` | `surfaces.ini` → track surface table |
| `CspConfigParser*` | CSP-style config fragments |

---

## 7. Runtime data flow

```
┌──────────────┐     ┌─────────────────┐     ┌──────────────────────┐
│ Devices /    │────▶│ InputManager    │────▶│ VehicleSimulator     │
│ keyboard     │     └─────────────────┘     │ (ksengine physics)   │
└──────────────┘                             └──────────┬───────────┘
                                                       │ state
                                                       ▼
┌─────────────────────────────────────────────────────────────────────┐
│ SimulationLoop::tick                                                │
│  · fixed physics steps                                              │
│  · laps / sectors / surface                                         │
│  · FeatureHub.tick → discovery · control · limits · weather         │
│  · publish SM / UDP / TCP                                           │
│  · NativeRenderer frame + GPU UI                                    │
└─────────────────────────────────────────────────────────────────────┘
```

**Session start path**

```
Menu PRACTICE / QUICK RACE / TIME ATTACK
        │
        ▼
onStartSessionRequested(label)
        │
        ▼
modeFromMenuEntry → beginSession(mode)
        │
        ▼
FeatureHub session params → countdown / green → driving
```

---

## 8. Threading & safety (guidelines)

| Topic | Practice |
|-------|----------|
| Physics | Single fixed-step owner inside `SimulationLoop` |
| Vulkan | Record/submit on the render path; avoid concurrent command buffers without sync |
| Control API | `poll()` on the sim thread; commands applied as queued work |
| Shared memory | Publish after physics step; readers are external processes |
| Callbacks | FeatureHub sinks run on the thread that calls `tick` / `poll` |

---

## 9. Build & Qt-free checks

| Target | Qt | Notes |
|--------|----|--------|
| `ksengine` | No | `KsQtFreeGuard`, `tools/check_no_qt.ps1` |
| SimulatorApp | No | Win32 + Vulkan SDK |
| ksEditor | Yes | Qt 6.11+ |

CMake may enable a Qt-free subset (`CMakeLists_ksengine_QtFree.txt`). Prefer no full simulator build until the engine is verified 100% Qt-free if that policy is active on the branch.

---

## 10. Design rules (summary)

1. **Qt only in the editor** — never include Qt from `engine/` or `simulator/`.
2. **Engine is generic** — no hard-coded dependency on a single commercial sim title.
3. **Adapters are bridges** — formats in, neutral data structures out.
4. **FeatureHub owns parity services** — session, discovery, control, limits, weather, PB, setup, replay helpers.
5. **Pit/garage is explicit state** — not buried inside tire or chassis code.
6. **Telemetry is multi-channel** — shared memory + UDP + TCP without blocking the physics step.

---

## 11. Related docs

| Doc | Content |
|-----|---------|
| [PARITY_STATUS.md](PARITY_STATUS.md) | Feature checklist vs roadmap |
| [SIMLOOP_WIRING.md](SIMLOOP_WIRING.md) | Snippets to wire FeatureHub into `SimulationLoop.cpp` |
| [COMMIT_COMPLETE.md](COMMIT_COMPLETE.md) | Snapshot of wired files |
| [README.md](../README.md) | Product overview + project structure tree |

---

*Last updated: 2026-10-03*
