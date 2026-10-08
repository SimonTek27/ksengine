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

## 2.7 — Servizio box end-to-end (GAP PRINCIPALE)

### Cosa esiste già

| Pezzo | Dove |
|-------|------|
| UI piano pit (fuel, tyres, body, susp, aero, engine) | `GameMenuOverlay` — `MenuState::PitStrategy`, `setPitStrategy`, `onPitStrategyConfirmRequested` |
| Mapper menu → `PitRepairInput` | `PitStrategyBridge.h` — `PitStrategyState`, `applyStrategyToRepairInput`, `wirePitStrategyMenu` |
| Motore riparazione | `PitLaneRepair.h` + `SimulationLoop::updatePitRepair` in `SimulationLoop_FeatureMethods.cpp` |
| Flag richiesta servizio | `m_requestPitService` (oggi impostato soprattutto da penalità Stop-Go) |

### Cosa manca

1. **`SimulatorApp.cpp` non cablare `wirePitStrategyMenu` / `onPitStrategyConfirmRequested`.**
   - Car, track, team e upgrade sono cablati (~righe 558–588).
   - Pit strategy confirm **non** compare in quel blocco.

2. **`updatePitRepair` ignora il piano del giocatore** (`SimulationLoop_FeatureMethods.cpp` ~336–360):

```cpp
in.targetFuelL = 100.f;
in.wantTyres = true; in.wantBody = true; in.wantSuspension = true; in.wantAero = true;
in.requestService = m_requestPitService && in.inGarageBox;
```

   I valori sono hardcoded; non leggono uno `PitStrategyState` persistito.

3. **Richiesta esplicita di servizio** distinta da “CONFIRM PLAN”.
   - La roadmap e `PitStrategyBridge` dicono: il piano da solo non accoda nulla.
   - Serve un’azione giocatore (tasto / riga menu) che setti `m_requestPitService = true`
     quando l’auto è in box (oltre al path Stop-Go già presente).

4. **HUD fuel/danni post-servizio** — verificare che `syncUiFromVehicle` / damage HUD
   riflettano `out.completedThisFrame` (repair + fuel). Se già leggono lo stato veicolo,
   basta far aggiornare fuel/damage in `updatePitRepair` (repairPartial c’è già).

### Patch consigliata (ordine)

**A. Stato del piano sul loop**

- Aggiungere a `SimulationLoop` (header + cpp):
  - `PitStrategyState m_pitStrategy;`
  - `void requestPitService(bool on = true);` → setta `m_requestPitService`
  - accessor `PitStrategyState& pitStrategy()`

**B. `updatePitRepair`**

```cpp
applyStrategyToRepairInput(m_pitStrategy, in); // invece degli hardcode
in.requestService = m_requestPitService && in.inGarageBox;
// on complete: aggiornare fuel del veicolo se PitLaneRepair non lo fa già
```

**C. `SimulatorApp.cpp` (blocco menu, dopo upgrade)**

```cpp
#include "simulator/PitStrategyBridge.h"

static ks::sim::PitStrategyState g_pitStrategy; // oppure stato sul loop

ks::sim::wirePitStrategyMenu(*uiMenu, g_simulation->pitStrategy(),
    []{ /* opzionale: log */ });

// Se il menu ha una riga "REQUEST SERVICE":
// uiMenu->onRequestPitService = []{ g_simulation->requestPitService(true); };
```

(Se preferisci lo stato solo sul loop, fai `wirePitStrategyMenu` scrivere su
`g_simulation->pitStrategy()`.)

**D. Header sync**

`SimulationLoop_FeatureMethods.cpp` usa già `m_requestPitService`, `m_pitRepair`,
`m_garageExit`, `m_damage`, … ma **`SimulationLoop.h` su master non dichiara
questi membri** (verificato 2026-10-08). Prima di ogni patch 2.7, allineare
l’header ai membri effettivamente usati dai `.cpp` / verificare che la build
Windows li veda (macro, include multipli, branch locale).

### Criterio di accettazione 2.7

1. Apri PIT STRATEGY → imposta fuel 40 L, solo tyres+body → CONFIRM PLAN.
2. Entra in box, fermo → REQUEST SERVICE (o equivalente).
3. Log: job avviati con i soli flag confermati; tempi realistici.
4. A fine servizio: HUD danni/fuel aggiornati; `m_requestPitService` false.

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

## Nota strutturale (build)

`SimulationLoop_FeatureMethods.cpp` referenzia membri pit/garage non elencati in
`src/simulator/SimulationLoop.h` del tree remoto. Prima di grandi patch:

```text
1. Compilare su Windows il path Qt-free.
2. Se fallisce per membri mancanti, allineare l'header ai .cpp.
3. Solo dopo applicare la patch 2.7 sopra.
```

---

## Riferimenti

- [CHECKLIST.md](CHECKLIST.md) — gate e ordine di lavoro
- [ROADMAP.md](ROADMAP.md) — priorità prodotto
- [PIT_WIRING.md](PIT_WIRING.md), [PIT_LANE_REPAIR.md](PIT_LANE_REPAIR.md)
- [GARAGE_TEAM_AUDIO.md](GARAGE_TEAM_AUDIO.md), [VEHICLE_UPGRADES.md](VEHICLE_UPGRADES.md)
