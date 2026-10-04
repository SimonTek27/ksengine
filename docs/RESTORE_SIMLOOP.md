# Restore SimulationLoop.cpp

## What happened

A tool error replaced `src/simulator/SimulationLoop.cpp` with a placeholder.
The last known-good full file is in git history.

## Fix (required)

```bash
git fetch origin
git checkout c78b0a9a0f7a58ac439525b32ba54b239e664f72 -- src/simulator/SimulationLoop.cpp
```

## Optional parity inject

After:

```cpp
if (m_multiCar && !m_raceSession.isCountingDown())
    m_multiCar->update(m_physicsDt);
```

add:

```cpp
#include "SimulationLoop_FeatureTick.inl"
```

Ensure `SimulationLoop_NetSync.cpp` is linked (Qt-free CMake already lists it).
