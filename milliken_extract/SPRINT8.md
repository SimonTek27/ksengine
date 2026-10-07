# Sprint 8 — NetSync wiring + P1.8 Pit strategy UI (2026-10-03)

## CarStateSync in SimulationLoop

- Members: `m_carSync`, `m_simTimeSec`
- API: `startCarStateHost` / `startCarStateClient` / `stopCarStateSync` / `updateNetworkSync`
- `tick()` physics step: after pit updates → `updateNetworkSync(dt)`
- Unit: `SimulationLoop_NetSync.cpp` (or merge into SimulationLoop.cpp)

## P1.8 Pit strategy UI

- `MenuState::PitStrategy` from Garage → **PIT STRATEGY**
- Fuel target ±5 L
- Toggles: Tyres / Body / Suspension / Aero / Engine
- **CONFIRM PLAN** → `onPitStrategyConfirmRequested`
- `PitStrategyBridge.h` maps to `PitRepairInput`

## Files

| Path | Role |
|------|------|
| `SimulationLoop.h/.cpp` | net sync hooks |
| `SimulationLoop_NetSync.cpp` | method bodies |
| `GameMenuOverlay.*` | PitStrategy menu |
| `PitStrategyBridge.h` | repair input mapping |
| `docs/SPRINT8.md` | this |

## Next

P1.6 multi-layout · P1.10 damage HUD SM · P2 backlog
