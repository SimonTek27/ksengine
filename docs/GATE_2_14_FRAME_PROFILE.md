# Gate 2.14 — profilo frame reale (CPU fisica + GPU)

> **Gate:** `CHECKLIST.md` 2.14 — "CPU fisica + GPU su hardware target con
> scena di riferimento; nessun KPI inventato".
> **Run:** 2026-10-14, build Debug Qt-free condivisa (VS 18 2026,
> `KSENGINE_QT_FREE=ON`, `KSIMULATOR_QT_FREE=ON`, `KSENGINE_SHARED=ON`).
> **Artefatti:** cartella [`gate_2_14/`](gate_2_14/) — `profile_2_14_report.txt`,
> `profile_2_14.csv`, `gate2_14_stdout.txt`, `gate2_14_scene.png`.

## Ambiente di misura

| | |
|---|---|
| GPU | NVIDIA GeForce RTX 3070 Ti, driver 32.0.16.1088 |
| Finestra | 1904×1041, SDR 8-bit (swapchain) |
| Scena di riferimento | `content/baked` (11 mesh) + `content/cars/refcar` + `content/tracks/mugello` (`data/data.json`) + 8 AI (`KS_AI_CARS=8`), sessione autostart |
| Fisica | `fixedDt=0.008333` → 120 Hz, 9 auto (giocatore + 8 AI) |
| Durata | 45 s (`KS_PROFILE_SECONDS=45`), **3837 frame profila­ti** |
| Comandi | `KS_PROFILE=1 KS_PROFILE_SECONDS=45 KS_PROFILE_CSV=profile_2_14.csv KS_PROFILE_REPORT=profile_2_14_report.txt KS_AUTOSTART=1 KS_CAR=content/cars/refcar KS_TRACK=<repo>/content/tracks/mugello KS_AI_CARS=8` (bat: `run_gate214_a.bat`, run di sola scena: `run_gate214_b.bat` con `KS_SCREENSHOT`) |

## Risultati (dal report/CSV del run, valori medi per tick o per step)

### CPU

| Voce | Media | Totale | Hits |
|------|------:|-------:|----:|
| Tick intero (`__frame__`, media = `Total`/frames) | **4.760 ms** (EMA 4.797, ultimo 5.094) | 18 264 ms | 3837 |
| `physics` (while-loop a passo fisso, input + step + AI + rete) | **854.7 µs** | 3 280 ms | 3837 |
| `render` (camera/submit/UI) | **3 516.6 µs** | 13 493 ms | 3837 |
| Resto del tick (FeatureHub, FFB, weather, telemetria, race session) | **388.8 µs** (residuo) | — | — |

Per **step di auto** (`VehicleSimulator.*`, 44 965 step = 9 auto × 120 Hz × ~41.7 s attivi):

| Sezione/sotto-sistema | Media per step |
|----------------------|---------------:|
| `VehicleSimulator.updatePhysics` | 42.0 µs |
| `VehicleDynamics` (sotto-sistema) | 47.7 µs |
| Tires | 9.6 µs (sotto-sistema 9.9 µs) |
| DamageModel | 9.9 µs |
| Aero | 8.8 µs (sezione 4.2 µs) |

### GPU (timestamp di coda reali, `GpuProfiler` in `NativeRenderer::endFrame`)

| Voce | Valore |
|------|-------:|
| `__gpu_frame__` (CSV, ultimo frame risolto) | **0.747 ms** |
| Media GPU EMA (`gpu avg`) | **1.249 ms** |
| Primo frame risolto (warm-up, shader) | 5.168 ms |
| `[render]` steady | `gpu frame≈0.79 ms avg≈1.24 ms` |
| Draw per frame | submitted 12, drawn 11, culled 1, occluded 0 |

### FPS a parete (contatore `[render]`, 44 finestre da ≥1 s)

- Σ finestre = **3766 frame** + finestra iniziale/finali parziali ≈ **3837** = `frames` del report ✓
- Burst iniziali ~165 fps, poi steady **~64.9 fps** (presenza/vsync della swapchain; il lavoro da profilare è 4.76 ms/tick).
- L'header `fps~196` del report è `1000 / tick-work` (esclude l'attesa del main loop): per gli fps a parete usare le righe `[render]`.

## Controlli di coerenza (nessun KPI inventato)

1. `physics` + `render` ≤ frame: 0.855 + 3.517 = 4.372 ≤ 4.760 ms ✓
2. `Total` (sotto-sistema dei frame) = somma degli span di frame, combacia con il conteggio `frames` ✓
3. Σ fps `[render]` (3766) ≈ `frames` (3837) sulle stesse 45 s ✓
4. 44 965 step ≈ 9 auto × 120 Hz × durata sessione ✓
5. `__gpu_frame__` scritto solo se la coda ha prodotto timestamp: 0 = nessun dato, mai una stima ✓

## Difetti trovati da questo run (e fixati prima di registrare i numeri)

1. **Pair di frame asimmetrico in `SimulationLoop::tick()`** — `PROFILE_FRAME`
   solo nel ramo idle, `PROFILE_END_FRAME` solo nel ramo attivo.
2. **`VehicleSimulator::updatePhysics()` scriveva il frame clock** ad ogni
   step di ogni auto: `frames` contava 50 592 (5578 tick + 45 014 step) e
   l'header del report misurava paia begin→end sbagliati. Rimosso; il costo
   dello step resta nella sezione + sotto-sistema. `ksvsprofile` ora apre il
   proprio frame-per-step.
3. **Sezione `physics` a scope di funzione** — misurava l'intera coda del
   tick (fino a `render()` compreso) come "physics". Ora lo scope coincide
   col while-loop a passo fisso.

Fix di supporto: `PhysicsProfiler::exportCsv()` come unico scrittore CSV (il C
API delega), dump report/CSV all'uscita di `kssim` (`KS_PROFILE*`), ripristino
dell'annotazione `KSENGINE_API` su `PhysicsProfiler` (la rimozione in WIP era
mascherata da un obj stale e avrebbe rotto il link alla prima ricompilazione).

## Limite oneste

- Build **Debug** (nessuna ottimizzazione): i numeri sono un bound superiore
  del runtime, non le prestazioni di rilascio.
- `content/tracks/mugello` contiene solo `data/data.json` (niente `.kn5`,
  niente `ai/fast_lane.ai`): le 8 AI partecipano alla fisica ma senza racing
  line dedicata; il kn5 di mugello non è nel repo.
- Audio/joystick: init falliti nel sessione (nessun dispositivo) — nessun
  impatto sul frame, presente nel log.
- Se l'"hardware target" del gate è un'albero macchina diverso, rilanciare
  `run_gate214_a.bat` lì: i valori cambiano, la procedura no.
