# Fix: AcRemoteTelemetryServer incomplete type

## Cause

`SimulationLoop.h` declares:

```cpp
std::unique_ptr<ks::ac::AcRemoteTelemetryServer> m_acRemote;
```

with only a forward declaration. Any TU that defines `SimulationLoop::~SimulationLoop()` must see a **complete** type (C++ unique_ptr destructor rule). The header file was missing → ~99 incomplete-type errors.

## Fix (in repo)

1. **Added** `src/adapters/assetto_corsa/AcRemoteTelemetryServer.h` (header-only stub).
2. **Apply include** in `SimulationLoop.cpp`:

```bash
git pull
git apply docs/patches/AcRemoteTelemetryServer_include.patch
# or add manually after TrackDataJsonLoader.h:
# #include "adapters/assetto_corsa/AcRemoteTelemetryServer.h"
```

## Notes

- Stub does not open sockets; real UDP remote telemetry can replace the class body later.
- `publishAcRemoteTelemetry()` remains optional / unimplemented; it is not called from `tick()`.
- Unrelated to E9 profiler or track JSON work.
