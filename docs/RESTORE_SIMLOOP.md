# Restore SimulationLoop.cpp

On a fresh clone, `src/simulator/SimulationLoop.cpp` is a stub. Full source is embedded in:

`cmake/simloop_z0.b64` … `cmake/simloop_z4.b64` (zlib + base64)

## Automatic

```bash
cmake -B build -DKSIMULATOR_QT_FREE=ON
```

`cmake/restore_simloop.cmake` expands the archive during configure.

## Offline

```bash
bash tools/restore_simloop.sh
```

Restored file includes `#include "SimulationLoop_FeatureTick.inl"` (FeatureHub + CarStateSync tick).
