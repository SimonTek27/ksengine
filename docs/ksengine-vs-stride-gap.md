# ksengine vs Stride — capability gaps

> **Comparer:** ksengine / ksim (C++17, Vulkan, racing runtime Qt-free) vs [Stride](https://www.stride3d.net/) 4.4 (C# / .NET 10, open-source game engine)  
> **Date:** 2026-10-14 (updated)  
> **Purpose:** Elencare le *caratteristiche di motore* che Stride offre e ksengine non ha (o ha solo in forma parziale).

**Non-goal:** riscrivere ksim in C#, sostituire Milliken/Bepu, o adottare Game Studio come editor ufficiale.

---

## 1. Executive summary

| Dimension | Stride 4.4 | ksengine / ksim |
|-----------|------------|-----------------|
| Linguaggio | C# 14 / .NET 10 | C++17 |
| Grafica | Vulkan + D3D12 | Vulkan |
| Fisica | Bepu generica | VehicleSimulator / Milliken |
| C API headless | n/a | **E1 consegnato** (`include/ksengine_c.h`) |
| Profiler | built-in | **E9 consegnato** (PhysicsProfiler + C API + CSV + tick + GPU timestamps) |

---

## 2. Capability table (aggiornata)

| # | Capability | Stato |
|---|------------|--------|
| **E1** | C API stabile | ✅ `ks_engine_create/step/get_state`, `load_track`/`load_car`, session, scene, version |
| **E9** | Profilazione frame | ✅ `PhysicsProfiler` + `ks_engine_profiler_*` + CSV + scope in `SimulationLoop::tick` + GPU timestamp di frame (`GpuProfiler` in `NativeRenderer::endFrame`) |
| E2–E8, E10–E15 | vedi tabella storica | invariati |

### E1 — accettazione

- [x] `include/ksengine_c.h`
- [x] `ks_engine_load_track` / `ks_engine_load_car`
- [x] `tests/ksengine/c_api_test.cpp`
- [x] `ks_engine_version()`

### E9 — accettazione

- [x] Sezioni + report + CSV via C API
- [x] Scope nominati in `SimulationLoop::tick`
- [x] Timestamp GPU frame path

### E9 — dove si trova cosa

- **CPU**: `SimulationLoop::tick()` (`PROFILE_FRAME` / `PROFILE_SECTION("physics")` /
  `PROFILE_SECTION("render")`, enable da `KS_PROFILE`).
- **GPU**: `GpuProfiler` (`src/engine/physics/GpuProfiler.*`, query pool
  `VK_QUERY_TYPE_TIMESTAMP` ×2 frame-in-flight) cablato in
  `NativeRenderer::createDevice()` / `endFrame()` / `shutdown()`; marcatori
  `FrameBegin → Shadow → MainPass → UI → FrameEnd`.
- **Uscita unica**: il frame GPU risolto alimenta
  `PhysicsProfiler::setGpuFrameTimeMs()`, quindi report
  (`ks_engine_profiler_report`), CSV (`__gpu_frame__`) e getter
  `ks_engine_profiler_gpu_ms()` / `gpu_avg_ms()` / `gpu_fps()`.
- **Invariante**: nessun renderer o coda senza `timestampValidBits` ⇒ 0, mai
  un KPI stimato (gate 2.14).
- **Test**: `tests/ksengine/c_api_test.cpp` (getter GPU a 0 in headless).

---

## 3. Non aggiungere ora

Riscrittura C#, Bepu al posto della fisica auto, secondo VkDevice senza design.

## 4. Riferimenti

- [include/ksengine_c.h](../include/ksengine_c.h)
- [src/engine/api/ksengine_c.cpp](../src/engine/api/ksengine_c.cpp)
- [PhysicsProfiler.h](../src/engine/physics/PhysicsProfiler.h)
- [ROADMAP.md](ROADMAP.md) · [CHECKLIST.md](CHECKLIST.md)
