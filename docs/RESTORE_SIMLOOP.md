# Restore SimulationLoop.cpp

If `src/simulator/SimulationLoop.cpp` is a stub (`#error`), expand the embedded archive:

```bash
bash tools/restore_simloop.sh
# or: cmake configure (cmake/restore_simloop.cmake runs automatically)
```

Archive: `cmake/simloop_z0.b64` … `simloop_z4.b64` (zlib + base64 of full source with FeatureTick inject).

After restore the file is ~52 KB / 1203 lines and includes:

- `#include "SimulationLoop_FeatureTick.inl"` after `m_multiCar->update`
- FeatureHub / CarStateSync hooks via NetSync

Do not commit a stub without the z*.b64 parts.
