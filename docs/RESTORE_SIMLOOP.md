# Restore SimulationLoop.cpp

`src/simulator/SimulationLoop.cpp` may be a stub on a fresh clone. Full source is embedded and restored automatically.

## Automatic (preferred)

```bash
cmake -B build -DKSIMULATOR_QT_FREE=ON
# cmake/restore_simloop.cmake expands the embedded source during configure
```

## Offline

```bash
bash tools/restore_simloop.sh
```

## Embedded sources

1. **Primary:** `cmake/simloop_src_0.txt` … `simloop_src_4.txt` (plain concatenation)
2. **Fallback:** `cmake/simloop_z0.b64` … `simloop_z4.b64` (zlib + base64)

Restored file includes `#include "SimulationLoop_FeatureTick.inl"` after `m_multiCar->update`.
