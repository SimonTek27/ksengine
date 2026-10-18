# ksengine — Architecture

**Product:** Kinetika (ksengine / ksim / ksEditor)  
**Updated:** 2026-10-18  
**Scope:** layers, data flow, runtime systems, source layout.

This is the single architecture reference for the repository. Older notes
(`ARCHITECTURE.md`, `ENGINE_ARCHITECTURE.md`, scattered SIMLOOP_*) remain
historical; prefer this document for new work.

---

## 1. Goals

| Goal | Meaning |
|------|---------|
| **Qt-free engine** | `src/engine/` builds with C++17 only (no Qt headers). |
| **Qt-free simulator** | `src/simulator/` uses Win32 + Vulkan, not Qt widgets. |
| **Independent runtime** | ksim is a standalone race client; no commercial title required at runtime. |
| **Format adapters** | Content formats live under `src/adapters/`, not inside the engine core. |
| **Optional editor** | ksEditor (Qt6) links ksengine; it is not required to drive or race. |
| **Product brand** | User-facing identity is **Kinetika**; plugins may keep legacy folder names. |

---

## 2. Product map

```
┌──────────────────────────────────────────────────────────────────┐
│  ksEditor (optional)                                             │
│  Qt6 · MainWindow · modeler / audio / physics / paint modules    │
│  Role: authoring & tools only                                    │
└────────────────────────────┬─────────────────────────────────────┘
                             │ links ksengine
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  SimulatorApp / ksim                                             │
│  Win32 window · NativeRenderer (Vulkan) · SimulationLoop         │
│  Role: race client — sessions, pits, HUD, net, telemetry         │
└────────────────────────────┬─────────────────────────────────────┘
                             │ links
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  ksengine (library)                                              │
│  Engine + modules: Math, physics, devices/FFB, FileFormat,       │
│  scene (ECS), network, Audio utilities, Graphics helpers         │
│  Role: generic simulation core                                   │
└────────────────────────────┬─────────────────────────────────────┘
                             │ optional use
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  adapters/                                                       │
│  Format bridges (shared memory, surfaces, CSP config, telemetry) │
│  Role: bridge existing mod formats without coupling the core     │
└──────────────────────────────────────────────────────────────────┘
```

| Product | Path | Qt | Output |
|---------|------|----|--------|
| **ksengine** | `src/engine/` | No | `ksengine` (static or shared) |
| **SimulatorApp** | `src/simulator/` | No | `ksim.exe` / SimulatorApp |
| **Adapters** | `src/adapters/` | No | Linked as needed |
| **ksEditor** | `src/MainWindow*`, modules, sdk | Yes | `kseditor.exe` |

---

## 3. Source tree

```
ksengine/
├── CMakeLists.txt
├── docs/
│   ├── ksengine_architecture.md   ← this file
│   ├── ksengine_design.md
│   ├── ksengine.roadmap.md
│   └── …
├── examples/MinimalSimulator/
├── external/
└── src/
    ├── engine/                  # ★ core library
    │   ├── Engine.* / EngineModule.h / KsExport.h
    │   ├── Math/
    │   ├── physics/             # vehicle, Pacejka, suspension, aero, damage, weather
    │   ├── devices/             # FFB + simracing backends
    │   ├── Graphics/ Audio/ FileFormat/
    │   ├── scene/               # ECS registry / systems
    │   ├── network/ AI/ Config/ assets/
    │   └── …
    ├── simulator/               # ★ race runtime
    │   ├── SimulatorApp.cpp
    │   ├── SimulationLoop.*
    │   ├── FeatureHub.h
    │   ├── session / discovery / control / limits / weather / setup / PB
    │   ├── garage / pit stack
    │   ├── NativeRenderer / UI / audio / net / telemetry
    │   └── shaders/
    ├── adapters/                # format bridges (legacy path names allowed)
    └── sdk/                     # editor modules + plugins (Qt)
```

---

## 4. Engine layer (`src/engine/`)

### 4.1 Engine shell

- **`Engine`** — process-wide module registry, fixed timestep (`setFixedDt`), start/stop.
- **`EngineModule`** — pluggable subsystem interface.
- **`KsExport.h` / `KSENGINE_API`** — explicit export surface for shared builds.
- **`KsQtFreeGuard.h`** — compile-time guard against accidental Qt includes.

### 4.2 Physics

| Area | Examples |
|------|----------|
| Vehicle | mass, powertrain, gears, fuel |
| Tires | Pacejka, load sensitivity, wear, temperature |
| Suspension | kinematics, dampers, ARB |
| Aero | downforce, drag, draft, ride-height maps |
| Brakes | bias, thermal model, fade |
| Damage | multi-system mechanical model (impulse thresholds) |
| Surface / weather | grip, wetness, aquaplaning, wind |

Physics is Qt-free and driven by the simulator fixed-step loop.

### 4.3 Devices & FFB

- Abstract FFB base + vendor backends (`devices/simracing/`).
- Input state fed into the vehicle each tick.

### 4.4 Scene & formats

- ECS-style registry + systems.
- FileFormat: KN5/ACD/ksanim/DDS/INI readers, banks, mesh helpers.
- Graphics helpers for editor viewports and the simulator Vulkan path.

---

## 5. Simulator layer (`src/simulator/`)

### 5.1 Entry & loop

| File | Role |
|------|------|
| `SimulatorApp.cpp` | Win32 + Vulkan, menu callbacks, main loop |
| `SimulationLoop` | Fixed-timestep physics, UI, telemetry, FeatureHub |
| Feature methods / inl | `beginSession`, services, replay |

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

Central façade for session and parity services (no Qt):

| Service | Notes |
|---------|--------|
| **SessionController** | Practice / Qualify / Race / Time Attack / Hotlap |
| **ServerDiscovery** | UDP `:20779` LAN announce + query |
| **ExternalControlApi** | TCP `:20780` text commands |
| **TrackLimitsMonitor** | penalties → session |
| **WeatherControl** | time of day, presets, wetness |
| **PersonalBestStore** | `user/pb` |
| **ApplySetup / SetupFile** | garage setup load/save |
| **ReplayRecorder** | record / playback |

**Control API (examples):** `SESSION`, `FLAG`, `PENALTY`, `WEATHER`, `TIME`, `SETUP`, `REPLAY`, `CHAT`, `RESULT`.

### 5.3 State ownership (single source of truth)

| State | Owner | Readers |
|-------|--------|---------|
| Session type/phase, laps, time left | `SimulationLoop` | Control API, UDP/SM, menu |
| Standings, position, penalties, flags | `RaceSessionManager` | HUD, RESULTS, control API |
| Selected mode + start params | `FeatureHub` | Menu, discovery |
| Live pit/garage status | `SimulationLoop` pit/garage status | Race HUD |
| Pit plan (fuel, tyres, repairs) | `PitStrategyBridge` → `PitLaneRepair` | Pit UI, service tick |

Overlays and renderers **must not** hold independent copies of session state.

### 5.4 Garage & pit stack

```
InGarage → Preparing → BoxClear → RollingOut → PitLane
                ↑                      │
                └──── Blocked ←────────┘
```

Modules: `GarageSpawn`, `GarageExit`, `PitLaneQueue`, `PitLaneCollision`, `PitLaneRepair`, mechanical damage multipliers.

### 5.5 UI & rendering

- **NativeRenderer** — Vulkan swapchain, scene, shadows, post-process, materials/IBL.
- **NativeUiHub** + **UiGpuPass** — GPU overlays.
- **GameMenuOverlay** — main / single / multi / garage / replay / settings.
- **Dashboard / Telemetry overlays** — race HUD.

### 5.6 Networking & telemetry

| Channel | Use |
|---------|-----|
| **ksnet** | Preferred multiplayer (car-state ~20 Hz, damage/setup/collision, auth) |
| **ServerDiscovery** | LAN browser |
| **UdpTelemetryBridge / TcpTelemetryBridge** | Outbound streams |
| **Shared memory publisher** (adapter) | Live page for external tools |
| **CarStateSync UDP** | Works without `HAS_KSNET` |

### 5.7 Useful environment variables

| Variable | Purpose |
|----------|---------|
| `KS_TRACK` / `KS_CAR` | Content selection |
| `KS_SESSION` / `KS_TEAM` / `KS_UPGRADES` | Session options |
| `KS_PROFILE=1` | Profiler tick (E9) |
| `KS_PARTICLES=1` | Rain/spray |
| `KS_AUTOSTART` / `KS_SCREENSHOT` | Automated runs |
| `KS_REPLAY_FILE` | Menu load replay |

---

## 6. Adapters

Adapters **must not** define core physics or UI. They parse or publish formats only.

Typical modules: shared-memory layout, surfaces.ini → grip table, CSP-style config, remote telemetry protocol bridges.

Legacy directory names under `adapters/` may remain for include stability; branding in comments and docs uses **Kinetika**.

---

## 7. Runtime data flow

```
Devices / keyboard → InputManager → VehicleSimulator (ksengine physics)
                                           │
                                           ▼
                                SimulationLoop::tick
                         · fixed physics steps
                         · laps / sectors / surface
                         · FeatureHub.tick
                         · publish SM / UDP / TCP
                         · NativeRenderer + GPU UI
```

**Session start**

```
Menu PRACTICE / QUICK RACE / TIME ATTACK
  → onStartSessionRequested
  → beginSession(mode)
  → FeatureHub params → countdown / green → driving
```

---

## 8. Threading guidelines

| Topic | Practice |
|-------|----------|
| Physics | Single fixed-step owner inside `SimulationLoop` |
| Vulkan | Record/submit on the render path with proper sync |
| Control API | `poll()` on the sim thread; queue work |
| Shared memory | Publish after physics step |
| FeatureHub callbacks | Same thread as `tick` / `poll` |

---

## 9. Build matrix

| Target | Qt | Notes |
|--------|----|--------|
| `ksengine` | No | `KsQtFreeGuard`, `tools/check_no_qt.ps1` |
| SimulatorApp | No | Win32 + Vulkan SDK |
| ksEditor | Yes | Qt 6.11+ |

```bash
cmake -B build -DKSENGINE_QT_FREE=ON
cmake --build build -j
```

---

## 10. Related documents

| Doc | Content |
|-----|---------|
| [ksengine_design.md](ksengine_design.md) | Design rules, ownership, non-goals |
| [ksengine.roadmap.md](ksengine.roadmap.md) | Product roadmap and gates |
| [PARITY_STATUS.md](PARITY_STATUS.md) | Feature checklist |
| [GAP_MATRIX.md](GAP_MATRIX.md) | Historical gap inventory |
| [CONTROL_API.md](CONTROL_API.md) | Control API reference |
| [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md) | Human acceptance plan |

---

*Consolidates former ARCHITECTURE.md and ENGINE_ARCHITECTURE.md content.*
