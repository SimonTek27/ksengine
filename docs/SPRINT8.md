# Sprint 8 — NetSync wiring + P1.8 Pit strategy UI (2026-10-03)

## CarStateSync in SimulationLoop

- `m_carSync`, `m_simTimeSec`
- `startCarStateHost` / `startCarStateClient` / `updateNetworkSync`
- Called each physics step in `tick()` after pit updates
- Bodies: `SimulationLoop_NetSync.cpp`

## P1.8 Pit strategy UI

- Garage → **PIT STRATEGY**
- Fuel ±5 L, toggles Tyres/Body/Susp/Aero/Engine
- CONFIRM → `onPitStrategyConfirmRequested`
- `PitStrategyBridge.h` → `PitRepairInput`

## Next

P1.6 multi-layout · P1.10 damage HUD SM
