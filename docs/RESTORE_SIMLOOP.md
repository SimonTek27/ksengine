# SimulationLoop.cpp restore

The large `.cpp` cannot be pushed in one API call from the agent. Two automatic paths:

## 1. CMake (recommended — zero manual steps)

On configure, `cmake/restore_simloop.cmake` (included by Qt-free simulator CMake):

1. Detects the stub `#error`
2. Downloads the last good file from commit `c78b0a9a`
3. Injects `#include "SimulationLoop_FeatureTick.inl"`

Just run your normal cmake configure/build.

## 2. Script

```bash
bash tools/restore_simloop.sh
git add src/simulator/SimulationLoop.cpp
git commit -m "fix: restore SimulationLoop.cpp with FeatureTick"
```

## Already complete on master

- SimulationLoop.h (FeatureHub + CarStateSync)
- SimulationLoop_NetSync.cpp + CMake entry
- SimulationLoop_FeatureTick.inl
- MultiCar traffic, AI overtake, FeatureHub stack, README
