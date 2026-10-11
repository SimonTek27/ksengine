# Wire-up gaps — Fase 2B (analisi codice 2026-10-08)

Analisi puntuale del tree su `master` per i deliverable 🟡 della roadmap.
Obiettivo: non riscrivere modelli già presenti; chiudere i collegamenti mancanti.

---

## Sintesi

| ID | Cosa manca davvero | Priorità |
|----|--------------------|----------|
| **2.7** | Piano pit del menu → `PitRepairInput` + richiesta esplicita servizio | **Alta** |
| **2.8** | Quasi chiuso: menu + `loadTeam` + `buildFieldFromTeam` + spawn AI da roster | Media (collaudo) |
| **2.9** | Quasi chiuso: menu upgrade → `cycleUpgradeRow` | Media (collaudo) |
| **2.10** | Coerenza stato menu/HUD tra garage, pit, risultati, multiplayer | Dopo 2.7 |

---

## 2.7 — Servizio box end-to-end

### Cosa esiste (aggiornato 2026-10-11 — cablaggio fatto)

| Pezzo | Dove | Stato |
|-------|------|-------|
| UI piano pit (fuel, tyres, body, susp, aero, engine) | `GameMenuOverlay` — `MenuState::PitStrategy` | ✅ |
| Riga **REQUEST SERVICE** distinta da CONFIRM PLAN | `GameMenuOverlay::buildPitStrategyMenu` + `onPitServiceRequested` | ✅ |
| Mapper menu → `PitRepairInput` | `PitStrategyBridge.h` (4° argomento: hook della richiesta) | ✅ |
| Stato del piano sul loop | `SimulationLoop::m_pitStrategy` + accessor `pitStrategy()` | ✅ |
| Motore riparazione | `PitLaneRepair.h` | ✅ |
| Servizio effettivo | `SimulationLoop_Pit.cpp` — `updatePitRepair()`, chiamata da `tick()` a 1 kHz | ✅ |
| Test "in box" | footprint 8 m attorno al box bound da `placePlayerForSession()` | ✅ |
| Carburante nel veicolo | `VehicleSimulator::setFuelL()` (serbatoio + massa) — segue il rifornimento mentre gira | ✅ |
| Riparazioni | solo sui job confermati (`PitLaneRepair::completeJob`), niente riparazione totale implicita | ✅ |
| Stop-Go | `onPenalty(kind == 2)` imposta `m_requestPitService` (stesso flag, stesso gate) | ✅ |
| Gate umano (tempi, HUD, esperienza) | — | ⬜ |

### Cosa è mancato davvero (perché la checklist sembrava quasi pronta)

`SimulationLoop_FeatureMethods.cpp` — da cui proveniva l'analisi precedente —
**non è compiles da nessun target** (non è in
`cmake/CMakeLists_ksimulator_QtFree.cmake`) e non compilerebbe nemmeno:
referenzia `m_garageExit`, `m_pitQueue`, `m_pitCollision`, `m_garageLayout`,
`m_damage`, membri assenti da `SimulationLoop.h`. Il 2.7 quindi non era
"quasi cablato": il piano finiva in uno stato che nessuno leggeva. Le due
definizioni duplicate (`startFeatureServices`, `updatePitRepair`) sono state
rimosse da quel file con una nota che rimanda ai TU compilati.

### Rimane aperto (fuori dal perimetro di questa patch)

- **Uscita/rientro garage**: `GarageExit.h` ha la macchina a stati completa
  ma il loop compilato non la guida ancora (nessuna richiesta di uscita, nessun
  pit limiter, nessun ritorno in box guidato). Conseguenza: il servizio si
  richiede fermando la vettura nel proprio box, non c'è ancora il flusso
  "entro in p box dall'esterno → mi viene chiesto il servizio".
- **Cambio pneumatici**: `PitLaneRepair` segnala `tyresChanged` ma non azzera
  l'usura del modello pneumatici (API dedicata da collegare).
- **Capacità serbatoio reale**: `in.fuelCapacityL` è ancora la costante 100 L.
- **HUD dedicato**: lo stato è esposto (`pitServiceStatus()`, `pitServiceBusy()`)
  e compare nei log; non c'è ancora una riga nel dashboard.
- **Capacità/durata**: gate umano su tempi realistici.

---

## 2.8 — Team (stato reale)

| Pezzo | Stato |
|-------|--------|
| `TeamSessionBridge.h` / `buildFieldFromTeam` | ✅ |
| `SimulationLoop::loadTeam` | ✅ |
| Menu `TeamSelect` + `onTeamChosen` in `SimulatorApp` | ✅ |
| `KS_TEAM=<dir>` all’avvio | ✅ |
| `beginRaceSession` → field + `spawnGrid` da roster + `placePlayerForSession` | ✅ |
| Collaudo con `team.ini` reale (numeri, livree AI, box) | ⬜ gate umano |

**Azione:** collaudare con un `content/teams/.../team.ini` di prova; non riscrivere il bridge.

---

## 2.9 — Upgrade (stato reale)

| Pezzo | Stato |
|-------|--------|
| `upgradeRowLabels` / `cycleUpgradeRow` sul loop | ✅ |
| Menu garage → `onUpgradeRowCycled` in `SimulatorApp` | ✅ |
| `loadUpgradesForCar` / `refreshUpgradeApplication` | ✅ (presenti in header) |
| Collaudo con `upgrades.ini` + livrea/sound pack su auto reale | ⬜ gate umano |

**Azione:** collaudare; se un path (audio/nodes) non si applica, fix mirato lì.

---

## 2.10 — Menu coerente

Dopo 2.7: una sola fonte di verità per sessione (FeatureHub / RaceSessionManager),
niente overlay duplicati per risultati/pit/garage. Verificare che
`onShowResultsRequested` e lo stato pit non aprano path paralleli inconsistenti.

---

## Nota strutturale (build) — aggiornata 2026-10-11

`SimulationLoop_FeatureMethods.cpp` **non fa parte di nessun target** e va
trattato come documentazione, non come codice: le sue funzioni esistono nei
TU compilati (`SimulationLoop.cpp`, `SimulationLoop_NetSync.cpp`,
`SimulationLoop_Pit.cpp`). La lista sorgenti è in
`cmake/CMakeLists_ksimulator_QtFree.cmake` e SimulatorServer la riusa.

Regola operativa: **prima** di scrivere una funzione di `SimulationLoop`,
verificare che il `.cpp` che la contiene sia elencato lì; se un file non è
compilato, non usare i suoi membri come "requisiti dell'header".

---

## Riferimenti

- [CHECKLIST.md](CHECKLIST.md) — gate e ordine di lavoro
- [ROADMAP.md](ROADMAP.md) — priorità prodotto
- [PIT_WIRING.md](PIT_WIRING.md), [PIT_LANE_REPAIR.md](PIT_LANE_REPAIR.md)
- [GARAGE_TEAM_AUDIO.md](GARAGE_TEAM_AUDIO.md), [VEHICLE_UPGRADES.md](VEHICLE_UPGRADES.md)
