# Restore SimulationLoop.cpp

## One command

```bash
bash tools/restore_simloop.sh
git add src/simulator/SimulationLoop.cpp
git commit -m "fix: restore SimulationLoop.cpp with FeatureTick"
```

## Manual

```bash
git checkout c78b0a9a0f7a58ac439525b32ba54b239e664f72 -- src/simulator/SimulationLoop.cpp
```

Then after `m_multiCar->update(m_physicsDt);` add:

```cpp
#include "SimulationLoop_FeatureTick.inl"
```

## Already on master (do not revert)

- SimulationLoop.h — FeatureHub + CarStateSync
- SimulationLoop_NetSync.cpp + CMake Qt-free entry
- SimulationLoop_FeatureTick.inl
- MultiCarManager traffic / AI overtake
- FeatureHub stack, README, CarStateSync
