# ksengine vs Stride — capability gaps

> **Comparer:** ksengine / ksim (C++17, Vulkan, racing runtime Qt-free) vs [Stride](https://www.stride3d.net/) 4.4 (C# / .NET 10, open-source game engine)  
> **Date:** 2026-10-13  
> **Purpose:** Elencare le *caratteristiche di motore* che Stride offre e ksengine non ha (o ha solo in forma parziale), così da poterle pianificare senza confondere “diventare Stride” con “chiudere la vertical slice racing”.

**Non-goal:** riscrivere ksim in C#, sostituire Milliken/Bepu, o adottare Game Studio come editor ufficiale.  
**Goal:** inventario onesto + priorità rispetto alla [ROADMAP](ROADMAP.md) racing.

---

## 1. Executive summary

| Dimension | Stride 4.4 | ksengine / ksim | Parity (indicativa) |
|-----------|------------|-----------------|---------------------|
| Linguaggio / runtime | C# 14, .NET 10, Native AOT | C++17, DLL/EXE nativi | n/a (scelta di prodotto) |
| Grafica API | Vulkan + D3D12 (overhaul 4.4); D3D11 legacy | Vulkan (path unico runtime) | ~parità API; feature set diverso |
| Editor | Game Studio + CLI asset | ksEditor (Qt, authoring) + bake offline | tool diversi, non confrontabili 1:1 |
| Fisica | Bepu (generica) | VehicleSimulator / Milliken (racing) | **ksengine più forte sul dominio auto** |
| Scope | Game engine general-purpose | Simulatore auto + adapters contenuto | intenzionale |

**Conclusione:** il gap non è “manca Vulkan”, è **ampiezza di piattaforma** (scripting, asset hot-reload, multi-platform packaging, animation characters, scene tooling generica). Per il prodotto gara le priorità restano i gate umani e 2.11–2.14; le voci sotto sono *capability engine* da valutare in Fase 3+ o come spike isolati.

---

## 2. Caratteristiche mancanti o deboli (da aggiungere)

Priorità relativa al **prodotto ksim** (non alla parity Stride totale).

| # | Capability | Stato ksengine | Perché conta | Priorità prodotto | Note implementative |
|---|------------|----------------|--------------|-------------------|---------------------|
| **E1** | **C API stabile del simulation core** | Assente (solo C++ interno) | Embedding, tool esterni, eventuale client C#/Stride, test headless | **Alta** (sblocca integrazioni) | `extern "C"` su loadTrack/loadCar/tick/getState; ABI versionata; niente STL nei confini |
| **E2** | **Hot-reload asset / shader** | Parziale (bake offline, restart) | Iterazione contenuti e materiali senza riavvio | Media | Watcher su `materials.txt` / shader SPIR-V; invalidate cache texture |
| **E3** | **Scene serializzata generica** | Solo track/car folder + bake | Livelli test, paddock, showroom oltre la gara | Media | Formato JSON/binary scene (entities + components) leggibile da SimulationLoop |
| **E4** | **Scripting in-process documentato** | Lua Mod SDK presente ma limitato | Modder e logic di evento senza ricompilare | Media | Contratto ufficiale eventi (session/lap/pit) + sandbox |
| **E5** | **Multi-platform runtime packaging** | Windows-first | Linux/Steam Deck, CI headless | Media (dopo gate Win) | CI Linux Vulkan; install layout già avviato |
| **E6** | **Animation skeletal in runtime** | Debole / assente nel path gara | Crew, bandiere, personaggi paddock | Bassa | Skinning GPU; non bloccante per auto |
| **E7** | **Visual scripting / graph logic** | Assente | Autori non-programmer | Bassa | Fuori scope runtime; eventualmente in ksEditor |
| **E8** | **Prefab / entity template system** | Parziale (team slots, upgrades) | Riutilizzo setup gara e AI | Media | Unificare spawn AI/player su prefab dati |
| **E9** | **Profilazione frame integrata** | Parziale (contatori, niente tool UI) | Gate 2.14 | **Alta** (ROADMAP 2.14) | CPU scopes + GPU timestamps; export CSV/chrome trace |
| **E10** | **Reflection / SSR / DoF di produzione** | Parziale (SSAO/SSR opt-in, DoF no) | Presentazione “pro” | Bassa–Media | Solo dopo misura 2.14; non prima dei gate gara |
| **E11** | **Audio spatial / occlusion middleware** | Base 3D presente | Immersione | Bassa | Estendere path esistente; non sostituire stack |
| **E12** | **Editor-time live link al runtime** | Assente | ksEditor ↔ ksim pose/mesh | Media (tooling) | Shared memory o TCP già usati in telemetria |
| **E13** | **Asset database / dependency graph** | Cartelle + bake | Rebuild incrementale, missing-ref report | Media | Manifest per track/car con hash |
| **E14** | **VR compositor path** | ROADMAP 3.5 ⬜ | VR racing | Bassa | Solo con camera loop stabile |
| **E15** | **Integrazione motore generico esterno (es. Stride come view)** | Assente | Spike “renderer alternativo” | Opzionale | Richiede E1 + un solo owner GPU; vedi sotto |

---

## 3. Cosa **non** aggiungere (o non ora)

| Voce | Motivo |
|------|--------|
| Riscrivere ksim in C# / .NET | Contrasta policy Qt-free C++ e costo di migrazione |
| Sostituire fisica auto con Bepu | Dominio racing già coperto e prioritario |
| Game Studio come editor ufficiale | ksEditor + bake restano la toolchain contenuto |
| Secondo `VkDevice` (ksim + Stride nello stesso processo senza design) | Sync e ownership GPU ingestibili |
| Ranked / anti-cheat / platform store | ROADMAP Fase 3, dopo loop offline/LAN |

---

## 4. Se si volesse “usare Stride” (opzionale)

Non è una feature da checklist Fase 2; è un **progetto parallelo**.

| Prerequisito | Descrizione |
|--------------|-------------|
| **E1** C API | `ksengine` espone tick/stato senza C++ ABI fragile |
| Host unico | O Stride possiede il device grafico, o ksim; non entrambi |
| Asset bridge | NMS2/DDS → mesh/material Stride *oppure* buffer grezzi ogni frame |
| Scope chiaro | Solo *view* (renderer) **o** solo *client* C#; non “drop-in replace” |

Spike consigliato (1–2 settimane): processo o DLL che pubblica pose auto in shared memory; hello Stride che disegna box/mesh di prova. Stop-go prima di qualsiasi migrazione asset.

---

## 5. Mapping su ROADMAP esistente

| Gap | ROADMAP / lavoro esistente |
|-----|----------------------------|
| E9 Profiling | **2.14** Profilo frame reale |
| E5 Multi-platform | CI / install layout; non esplicitato come deliverable gara |
| E2 Hot-reload | Non in Fase 2; utile post-gate |
| E4 Scripting | Lua Mod SDK già citato in Architecture |
| E14 VR | **3.5** |
| E15 Stride view | Nessuna voce; solo dopo E1 e gate Fase 2 |

**Ordine consigliato se si “aggiungono caratteristiche engine” senza deragliare la gara:**

1. Chiudere gate umani + 2.7/2.10 collaudo + **2.11–2.14**
2. **E1** C API + **E9** profiler (misura prima di ottimizzare)
3. **E2** / **E13** iterazione contenuti
4. Solo poi spike **E15** o VR **E14**

---

## 6. Criteri di accettazione (per voce prioritaria)

### E1 — C API

- [ ] Header `ksengine_c_api.h` con `ks_sim_create/destroy`, `ks_sim_load_track`, `ks_sim_load_car`, `ks_sim_tick`, `ks_sim_get_vehicle_state`
- [ ] Test C o C# che linka la DLL e fa 1000 tick senza leak
- [ ] Documentazione ABI + version macro

### E9 — Profiler

- [ ] Scope CPU nominati (physics, net, render submit) in log o file
- [ ] Opzionale: timestamp GPU per frame su Vulkan
- [ ] Report usabile per gate 2.14 (niente KPI inventati)

---

## 7. Riferimenti

- [Stride](https://www.stride3d.net/) · [Docs](https://doc.stride3d.net/latest/manual/) · [4.4 notes](https://www.stride3d.net/blog/announcing-stride-4-4/)
- [ROADMAP.md](ROADMAP.md) · [ARCHITECTURE.md](ARCHITECTURE.md)
- [ksengine-vs-cryengine-gap.md](ksengine-vs-cryengine-gap.md) (gap vs engine AAA generico)
- [TRACK_DATA_JSON.md](TRACK_DATA_JSON.md) · [CHECKLIST.md](CHECKLIST.md)

---

*Aggiornare questa tabella quando una capability passa da “missing” a “partial/full” con commit di evidenza (test o gate).*
