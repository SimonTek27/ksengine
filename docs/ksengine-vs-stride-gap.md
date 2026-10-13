# ksengine vs Stride — capability gaps

> **Comparer:** ksengine / ksim (C++17, Vulkan, racing runtime Qt-free) vs [Stride](https://www.stride3d.net/) 4.4 (C# / .NET 10, open-source game engine)  
> **Date:** 2026-10-13 (updated)  
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
| Profiler | built-in | **E9 parziale** (PhysicsProfiler + C API + CSV) |

---

## 2. Capability table (aggiornata)

| # | Capability | Stato |
|---|------------|--------|
| **E1** | C API stabile | ✅ `ks_engine_create/step/get_state`, `load_track`/`load_car`, session, scene, version |
| **E9** | Profilazione frame | 🟡 `PhysicsProfiler` + `ks_engine_profiler_*` + CSV; manca wire in `SimulationLoop::tick` e GPU timestamps di prodotto |
| E2–E8, E10–E15 | vedi tabella storica | invariati |

### E1 — accettazione

- [x] `include/ksengine_c.h`
- [x] `ks_engine_load_track` / `ks_engine_load_car`
- [x] `tests/ksengine/c_api_test.cpp`
- [x] `ks_engine_version()`

### E9 — accettazione

- [x] Sezioni + report + CSV via C API
- [ ] Scope nominati in `SimulationLoop::tick`
- [ ] Timestamp GPU frame path

---

## 3. Non aggiungere ora

Riscrittura C#, Bepu al posto della fisica auto, secondo VkDevice senza design.

## 4. Riferimenti

- [include/ksengine_c.h](../include/ksengine_c.h)
- [src/engine/api/ksengine_c.cpp](../src/engine/api/ksengine_c.cpp)
- [PhysicsProfiler.h](../src/engine/physics/PhysicsProfiler.h)
- [ROADMAP.md](ROADMAP.md) · [CHECKLIST.md](CHECKLIST.md)
