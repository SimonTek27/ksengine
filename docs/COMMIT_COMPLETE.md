# Full FeatureHub commit — 2026-10-03

## On GitHub (master)

### Core modules
- `FeatureHub.h`
- `SessionController.h`, `TrackLimitsMonitor.h`, `WeatherControl.h`
- `ServerDiscovery.h`, `ExternalControlApi.h`
- `PersonalBestStore.h`, `TrackLayout.h`, `ApplySetup.h`, `SetupFile.h`
- `PitLaneRepair.h`, `GarageExit.h`, `PitLaneQueue.h`, `PitLaneCollision.h`

### Wiring
- `GameMenuOverlay.h/.cpp` — session modes
- `SimulatorApp.cpp` — beginSession / replay / host discovery
- `SimulationLoop_Features.inl` + `SimulationLoop_FeatureMethods.cpp`
- `SimulationLoop.h` — FeatureHub API

### Docs
- `PARITY_STATUS.md`, `SIMLOOP_WIRING.md`, `COMMIT_NOTES.md`

## Local artifacts (full cpp if needed)

Copy into tree if linker misses symbols:
- `SimulationLoop.cpp` / `SimulationLoop_committed.cpp`
- `SimulationLoop_FeatureMethods.cpp`
- `SimulatorApp_wired.cpp`

### CMake

Add to simulator sources:
```
src/simulator/SimulationLoop_FeatureMethods.cpp
```

Or at end of `SimulationLoop.cpp`:
```cpp
#include "SimulationLoop_Features.inl"
```

And in `tick` after lap update, call `m_features.tick(...)`.
In `initialize`/`ctor` after vehicle: `startFeatureServices(false)`.
