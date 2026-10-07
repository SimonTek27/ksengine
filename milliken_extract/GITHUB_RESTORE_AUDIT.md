# GitHub restore audit — 2026-10-04

## Context

Latest master commit: user **"Upload changes"** overwrote / diverged from Sprint 1–8
incremental pushes. Several parity modules present only in agent artifacts or older
commits are **missing from current tree**.

## Missing on master (must restore)

| Module | Role | Source |
|--------|------|--------|
| `FeatureHub.h` | discovery, control, limits, weather, PB, session | restore |
| `SessionController.h` | session modes | restore |
| `WeatherControl.h` | presets + time of day | restore |
| `ServerDiscovery.h` | LAN UDP discovery | restore |
| `TrackLayout.h` | multi-layout catalog | restore |
| `ApplySetup.h` | setup apply helper | restore |
| `TrackLimitsMonitor.h` | P1.3 penalties | restore (escalation) |
| `PersonalBestStore.h` | P1.5 PB + leaderboard | restore |
| `ExternalControlApi.h` | TCP control + AUTH | restore |
| `CarStateSync.h` | P1.1 UDP ≥20 Hz | artifacts |
| `CarStateSyncBridge.h` | MultiCar publish/apply | artifacts |
| `MenuFeatureBridge.h` | menu ↔ features | restore |
| `PitStrategyBridge.h` | P1.8 pit plan | artifacts |
| `SimulationLoop_NetSync.cpp` | tick wiring | artifacts |
| docs SPRINT4–8, GAP_MATRIX, PARITY_STATUS | tracking | restore |

## Present but incomplete vs sprint work

| File | Issue |
|------|--------|
| `SimulationLoop.*` | No FeatureHub, no updatePitLane/GarageExit, no CarStateSync, no applySetup/FFB hooks from sprints |
| `AIController.*` | Line follow OK; **no traffic/overtake** (Sprint 6) |
| `MultiCarManager.cpp` | No `AiTrafficCar` snapshot feed |
| `GameMenuOverlay.*` | Has `onOpenServerBrowserRequested`; **no** ServerBrowser/Weather/PitStrategy menu states from Sprint 5/8 |

## Still on master (good)

- `GarageExit.h`, `PitLaneQueue.h`, `PitLaneCollision.h`
- `RaceSessionManager`, `ReplayRecorder`
- `NetworkManager` / `NetworkLowLevel` (HAS_KSNET path)
- Telemetry UDP/TCP/SM, TrackSurface weather sync
- MechanicalDamage (engine/physics)

## Recommended fix order

1. Restore standalone headers (this push)
2. Merge AI overtake into AIController
3. Re-wire SimulationLoop: FeatureHub + pit updates + CarStateSync (careful merge with user upload)
4. Re-apply GameMenu ServerBrowser / Weather / PitStrategy
5. CMake list new sources if needed

## Note

Do **not** blindly replace entire `SimulationLoop.cpp` — user upload is larger and
has valuable telemetry/render paths. Surgical merge only.
