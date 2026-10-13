# E9 — PhysicsProfiler in SimulationLoop::tick

## Apply

```bash
cd /path/to/ksengine
git apply docs/patches/SimulationLoop_E9_profiler.patch
# or: patch -p1 < docs/patches/SimulationLoop_E9_profiler.patch
```

## What it does

| Hook | Section |
|------|--------|
| `KS_PROFILE=1` | enables profiler once at first tick |
| `PROFILE_FRAME` / `PROFILE_END_FRAME` | whole frame |
| `PROFILE_SECTION("physics")` | 1 kHz while-loop |
| `PROFILE_SECTION("render")` | camera + render |

## Run

```text
set KS_PROFILE=1
ksim.exe
```

From C API / headless: `ks_engine_profiler_report(nullptr)` or `ks_engine_profiler_export_csv("profile.csv")`.

## Status

- C API profiler: **done** (`include/ksengine_c.h`)
- Tick sections: **patch ready** (apply above)
- GPU timestamps: still open (gate 2.14)
