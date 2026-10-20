# Kinetika — ksengine

**Qt-free simulation engine · race runtime · optional modding suite**

C++17 engine for four-wheel racing: physics, AI, multiplayer (**ksnet**), telemetry.
The race client (**ksim**) runs without Qt. The editor (**ksEditor**) is optional.

[![License](https://img.shields.io/badge/license-GPL3-blue.svg)](LICENSE.txt)
[![Version](https://img.shields.io/badge/version-0.90-orange)]()
[![C++](https://img.shields.io/badge/C++-17-blue)]()
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey)]()

---

## Products

| Product | Path | Qt | Role |
|---------|------|----|------|
| **ksengine** | `src/engine/` | No | Simulation core (math, physics, devices, formats, scene, net) |
| **ksim** (SimulatorApp) | `src/simulator/` | No | Standalone race client — Vulkan, sessions, pits, HUD, multiplayer |
| **ksEditor** | modules / `sdk/` | Yes | Modding UI (modeler, audio, physics, liveries, …) |
| **adapters/** | `src/adapters/` | No | Content-format bridges (keep the core title-neutral) |

```
ksEditor (Qt6, optional)
        │ links
        ▼
ksim / SimulatorApp (Win32 + Vulkan)
        │ links
        ▼
ksengine (C++17 library)
        │ uses
        ▼
adapters/ (format bridges only)
```

---

## Documentation (start here)

| Document | Content |
|----------|---------|
| **[Architecture](docs/ksengine_architecture.md)** | Layers, source tree, FeatureHub, pit/garage, data flow, build matrix |
| **[Design](docs/ksengine_design.md)** | Design rules, ownership, non-goals, quality policy, branding |
| **[Roadmap](docs/ksengine.roadmap.md)** | Phases, gates (2.11–2.14), priorities, acceptance metrics |

Supporting: [PARITY_STATUS](docs/PARITY_STATUS.md) · [GAP_MATRIX](docs/GAP_MATRIX.md) ·
[GATE_PLAN_WINDOWS](docs/GATE_PLAN_WINDOWS.md) · [CONTROL_API](docs/CONTROL_API.md) ·
[CHECKLIST](docs/CHECKLIST.md)

Older notes (`ARCHITECTURE.md`, `ROADMAP.md`, `SIMLOOP_*`, `SPRINT*`) remain as
detail; prefer the three primary docs above for onboarding and planning.

---

## Status (summary)

| Area | State |
|------|--------|
| Physics, setup, FFB, surfaces, AI, session | ✅ |
| Multiplayer LAN + lobby (ksnet) | ✅ |
| Menu vertical slice, bindings, telemetry | ✅ |
| Wet physics, 3D audio, mesh rendering | ✅ |
| Box / team / upgrades / menu coherence (code) | 🟡 human gates open |
| Long race gate **2.11** | ✅ (2026-10-14) |
| Soak **2.12** / frame profile **2.14** | ✅ (2026-10-14) |

Full detail: [docs/ksengine.roadmap.md](docs/ksengine.roadmap.md).

---

## Design rules (short)

1. **Qt only in the editor** — `src/engine/` and `src/simulator/` stay std/Vulkan.
2. **Engine is generic** — no hard dependency on a commercial title in core code.
3. **Adapters are bridges** — formats in, neutral data out.
4. **FeatureHub** owns session, discovery `:20779`, control `:20780`, limits, weather, PB.
5. **Pit/garage** is an explicit state machine, separate from vehicle integrate.
6. **Compile ≠ product-ready** — features need tests or human gate evidence.

See [docs/ksengine_design.md](docs/ksengine_design.md).

---

## Runtime data flow

```
Input → InputManager → VehicleSimulator (ksengine physics)
                              │
                              ▼
                   SimulationLoop @ fixed dt
            ┌─────────────────┼─────────────────┐
            ▼                 ▼                 ▼
       FeatureHub       NativeRenderer      Telemetry
    (session, limits,    (Vulkan frame)   (UDP/TCP/SM)
     weather, API)
            │
            ▼
    GameMenuOverlay / NativeUiHub
```

---

## Project structure

```
ksengine/
├── CMakeLists.txt
├── docs/
│   ├── ksengine_architecture.md   # primary
│   ├── ksengine_design.md         # primary
│   ├── ksengine.roadmap.md        # primary
│   └── …
├── examples/MinimalSimulator/
├── external/
├── tests/
└── src/
    ├── engine/          # ★ Qt-free core
    ├── simulator/       # ★ Qt-free ksim
    ├── adapters/        # format bridges
    └── sdk/             # editor + plugins (Qt)
```

---

## Technical stack

| Category | Technology |
|----------|------------|
| Language | C++17 |
| Engine | Qt-free library (`KSENGINE_API` export surface) |
| Simulator | Win32 + Vulkan (precompiled SPIR-V) |
| Editor UI | Qt 6.11+ (Widgets, QML) |
| Audio | ksAudioStudio + WASAPI (simulator) |
| Physics | Custom vehicle model (+ optional Bullet) |
| Multiplayer | **ksnet** (reliable UDP, ~20 Hz car-state) |
| Build | CMake 3.16+ |
| Platform | Windows 10/11 x64 |

---

## Build

```bash
cmake -B build -DKSENGINE_QT_FREE=ON
cmake --build build -j
```

- Default engine target is shared (`ksengine.dll`); use `-DKSENGINE_SHARED=OFF` for a static archive.
- `HAS_KSNET` is on when the in-tree `ksnet` target exists (default).
- Qt-free checks: `tools/check_no_qt.ps1` / Qt-free CMake preset.

```bash
cmake --preset default
cmake --build --preset default
```

### Requirements

| Component | Required for |
|-----------|----------------|
| CMake 3.16+ | All |
| Vulkan SDK | SimulatorApp / NativeRenderer |
| Qt 6.11+ | **ksEditor only** |
| Windows 10/11 x64 | Primary platform |

---

## Install layout

```bash
cmake --build build --target ks_dist    # → <source>/dist/ksim
# or:
cmake --install build --prefix <dir> --config Release
```

```
ksim.exe              SimulatorApp
kssimserver.exe       headless host
ksengine.dll          engine
content/              reference content
user/                 per-player data (created on first run)
server/               kssimserver.ini
system/cfg/           ksengine.json defaults
system/shaders/       SPIR-V
```

Paths resolve relative to the executable working directory (`src/engine/assets/Paths.h`).

### Settings

| File | Role |
|------|------|
| `system/cfg/ksengine.json` | Install defaults (`physics.fixedDt`, `audio.master`, `assist.tc` / `assist.abs`) |
| `user/<player>/settings.json` | Per-player overrides (later wins; no default leakage) |

On first start: `user/<player>/controls.json`, `stats.json`, `screenshots/`, `replay/`, `telemetry/`.
Personal bests remain under `user/pb/`.

---

## Multiplayer (ksnet)

- Official name: **ksnet** (macros `KSNET_*`; `YOJIMBO_*` deprecated aliases only).
- Car-state ~20 Hz; damage / setup / collision messages; app-level auth tokens.
- LAN discovery UDP `:20779`; optional HTTP lobby via `NetworkManager::setLobbyBaseUrl`.
- CarStateSync UDP still works without `HAS_KSNET`; prefer ksnet for production sessions.

---

## Editor modules (ksEditor)

Optional Qt6 toolkit on top of ksengine: 3D modeler, audio studio (FMOD-compatible banks),
physics editor, livery/paint, showroom, events, server config, FFB curves, telemetry,
workshop, VR authoring, help. Not required to run **ksim**.

---

## License

GPL-3.0 — see [LICENSE.txt](LICENSE.txt).
