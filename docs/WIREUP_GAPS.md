# Wire-up gaps — Fase 2B

**Aggiornata:** 2026-10-14  
Analisi puntuale del tree su `master`. Obiettivo: non riscrivere modelli già presenti; chiudere i collegamenti mancanti.

---

## Sintesi (aggiornata 2026-10-14)

| ID | Stato codice | Cosa resta | Priorità |
|----|--------------|------------|----------|
| **2.7** | ✅ cablato end-to-end | Gate umano (tempi, HUD, esperienza box) | **Alta (collaudo)** |
| **2.8** | ✅ team/roster | Collaudo con `team.ini` AC reale | Media (collaudo) |
| **2.9** | ✅ upgrade menu → physics/audio | Collaudo con `upgrades.ini` reale | Media (collaudo) |
| **2.10** | ✅ standings/HUD unificati | Verifica manuale path menu | Media (collaudo) |
| **2.12** | — | ✅ soak 1 h (evidence) | chiuso |
| **2.14** | — | ✅ profilo frame (evidence) | chiuso |
| **2.11** | — | Gara 10–15 min 8+ auto | Aperta |

Piano operativo gate: [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md).

---

## 2.7 — Servizio box end-to-end

### Codice (chiuso 2026-10-11+)

| Pezzo | Dove | Stato |
|-------|------|-------|
| UI piano pit | `GameMenuOverlay` PitStrategy | ✅ |
| REQUEST SERVICE | distinto da CONFIRM PLAN | ✅ |
| Uscita/rientro box | `updateGarageExit`, tasto P, menu GARAGE | ✅ |
| `PitStrategyBridge` → `PitRepairInput` | header + loop | ✅ |
| `updatePitRepair` a 1 kHz | `SimulationLoop_Pit.cpp` | ✅ |
| Fuel / tyres / capacity | `setFuelL`, `resetTyreWear`, `fuelTankCapacityL` | ✅ |
| Coda multi-auto | `PitLaneQueue` | ✅ |
| Test | `pit_strategy_menu_test` | ✅ |
| **Gate umano** | tempi, HUD, esperienza | ⬜ |

### Fuori scope codice (opzionale)

- AI `requestEnter` in corsia
- Parser `powertrain.ini` per capacità (oggi `FUEL_CAPACITY` in `engine.ini`)

---

## 2.8 / 2.9 / 2.10

Codice consegnato e coperto da `menu_session_gate_test`. Resta solo collaudo con contenuto AC reale e verifica path menu (vedi GATE_PLAN_WINDOWS G2/G3).

---

## Riferimenti

- [CHECKLIST.md](CHECKLIST.md)
- [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md)
- [ROADMAP.md](ROADMAP.md)
- [GATE_2_12_STABILITY.md](GATE_2_12_STABILITY.md)
- [GATE_2_14_FRAME_PROFILE.md](GATE_2_14_FRAME_PROFILE.md)
