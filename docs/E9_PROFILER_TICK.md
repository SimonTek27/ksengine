# E9 — PhysicsProfiler in SimulationLoop::tick

## Apply

Already in `master` (`SimulationLoop::tick()` carries the hooks) — the patch
below is only for branches that predate the wiring:

```bash
cd /path/to/ksengine
git apply docs/patches/SimulationLoop_E9_profiler.patch
# or: patch -p1 < docs/patches/SimulationLoop_E9_profiler.patch
```

## What it does

| Hook | Section |
|------|---------|
| `KS_PROFILE=1` | enables profiler once at first tick |
| `PROFILE_FRAME` / `PROFILE_END_FRAME` | whole frame |
| `PROFILE_SECTION("physics")` | 1 kHz while-loop |
| `PROFILE_SECTION("render")` | camera + render |

## GPU frame path (E9 acceptance)

Same `KS_PROFILE` switch, `GpuProfiler` (`src/engine/physics/GpuProfiler.*`,
double-buffered `VK_QUERY_TYPE_TIMESTAMP` pools):

| Where | Marker |
|------|--------|
| `NativeRenderer::createDevice()` | `init()` + `KS_PROFILE` gate (needs `timestampValidBits > 0`, else disabled) |
| `endFrame()` after `vkBeginCommandBuffer` | `FrameBegin` (also resolves the slot two frames old) |
| after the shadow cascades | `Shadow` |
| after the deferred/forward display pass | `MainPass`, `UI` (overlay is recorded outside this command buffer today ⇒ ~0, honestly) |
| before `vkEndCommandBuffer` | `FrameEnd` |
| after `vkQueueSubmit` | ring advance; `shutdown()` drops the pools before the device |

The resolved frame is published with `PhysicsProfiler::setGpuFrameTimeMs()`,
so `ks_engine_profiler_report()`, the `__gpu_frame__` CSV row, the
`ks_engine_profiler_gpu_ms()` / `gpu_avg_ms()` / `gpu_fps()` getters and the
1 s `[render]` log line all read the same number. No renderer or no
timestamps ⇒ 0, never an estimated KPI.

## Run

```text
set KS_PROFILE=1
ksim.exe
```

KS_PROFILE=0 silences both the CPU sections and the GPU markers.

From C API / headless: `ks_engine_profiler_report(nullptr)` or `ks_engine_profiler_export_csv("profile.csv")`.

## Status

- C API profiler: **done** (`include/ksengine_c.h`)
- Tick sections: **in tree** — `SimulationLoop::tick()` carries `KS_PROFILE`
  + `PROFILE_FRAME` / `PROFILE_SECTION("physics"|"render")`;
  `docs/patches/SimulationLoop_E9_profiler.patch` stays as reference for
  branches that predate the wiring.
- GPU timestamps: **done** (table above; verified by `test_renderer`, which
  drives real frames through `NativeRenderer`).
- Gate 2.14: still open — the instrumentation exists, the run on target
  hardware with the reference scene has to happen.
