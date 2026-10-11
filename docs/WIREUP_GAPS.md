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
| Uscita/rientro box | `updateGarageExit()` + righe **LEAVE GARAGE** / **RETURN TO BOX** nel menu GARAGE, tasto `P` | ✅ |
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

- ~~**Coda box multi-auto**~~ → **cablata (11/10)**: `updateGarageExit()`
  alimenta `PitLaneQueue` con giocatore e campo, `pathBlocked` arriva da
  `shouldBlockGarageExit()` (`KS_PIT_QUEUE=0` per disattivare). Corretto nel
  frattempo il release gap, calcolato al contrario: la seconda auto in coda
  non sarebbe mai uscita (`pit_queue_test`). Resta da cablare l'ingresso in
  corsia (`requestEnter`) per le auto AI che rientrano.
- **Finestra box in gara**: `pitLaneOpen` è sempre true; va presa da
  `RaceSessionManager`.
- ~~**Cambio pneumatici**~~ → **chiuso (11/10)**: `out.tyresChanged` ora azzera
  l'usura del modello pneumatici (`VehicleSimulator::resetTyreWear()`,
  applicato una volta per stop).
- ~~**Capacità serbatoio reale**~~ → **chiuso (11/10)**: `fuelCapacityL` viene
  da `VehicleSimulator::fuelTankCapacityL()` (default 150 L, override con
  `FUEL_CAPACITY` in `data/engine.ini`) e `PitLaneRepair::plan()` plafona il
  target al serbatoio. Resta aperto solo il parser di `powertrain.ini`
  ([FUEL] capacity, in joule) per l'AC content puro.
- ~~**HUD dedicato**~~ → **chiuso da 2.10 (11/10)**: il race HUD ha una riga
  di stato (`RaceHudSample::statusText`) alimentata da `pitServiceStatus()` /
  `garageStatus()`.
- **Gate umano**: tempi realistici, esperienza di box, FFB/audio reali.

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

**Fatto (2026-10-11)** — i tre path paralleli trovati:

| Trovato | Rimedio |
|---------|---------|
| `RaceHudSample` / `DashboardOverlay` mostravano **posizione 1/1 hardcodata** mentre le classifiche vivevano in `RaceSessionManager::standings()` | `syncUiFromVehicle()` legge `RaceSessionManager::playerStanding()` + `standings().size()` (stessa sorgente di RESULTS e control API) |
| Stato pit/garage esposto (`pitServiceStatus()`, `garageStatus()`) ma **non mostrato da nessuna parte** | Una sola riga di stato nel race HUD (`RaceHudSample::statusText`, allineata a destra nella top bar; priorità al servizio in corso) |
| `src/simulator/ui/PitLaneOverlay.h`: **secondo** overlay pit, non compilato e non usato, con tempi e semantica propri | Rimosso; `docs/PIT_LANE_UI.md` resta come design con nota |

Risultati e multiplayer avevano già un solo path (`onShowResultsRequested` →
`raceSession().standings()` → `GameMenuOverlay::showResults`). La mappa di
proprietà dello stato è in `docs/ARCHITECTURE.md` § 5.3.1.

**Resta aperto**: la verifica manuale dei path menu dopo il fix 2.7 (gate
umano) e i tempi/gate dell'esperienza di box.

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
