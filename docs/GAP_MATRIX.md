# Gap Matrix — prioritised parity roadmap

## Sprint status

| Sprint | Scope | Status |
|--------|--------|--------|
| 1–3 | Setup, FFB, surface, tyre, replay, session | **Done** |
| 4 | Limits, PB, AUTH | **Done** |
| 5 | Server browser + weather UI | **Done** |
| 6 | AI line + overtake | **Done** |
| 7 | CarStateSync UDP ≥20 Hz | **Done** |
| 8 | NetSync + pit strategy UI | **Done** |
| 9 | Full ksnet remote car state | **Done** |
| 9b | Session / countdown / penalty / lap bridge | **Done** |

## ksnet complete path

| Component | Role |
|-----------|------|
| `src/core/engine/Network/ksnet` | Reliable-UDP transport |
| `NetworkConfig.h` | Messages: join/welcome/state/spawn/input/chat/session/penalty |
| `NetworkLowLevel_*.inc` | Client handlers + server broadcasts + spawn grid replay |
| `NetworkManager` | Host 20 Hz car-state, public broadcast API, client callbacks |
| `SimulationLoop_FeatureTick.inl` | Host bridge: RaceSession → ksnet session/countdown/penalty |
| `SimulationLoop_NetSync.cpp` | Dual path: ksnet + CarStateSync UDP |
| Qt-free `SimulatorApp` | Links `ksnet` (`HAS_KSNET=1`) |

## Optional (P2+)

| Item | Notes |
|------|-------|
| Wire encryption / token auth | Today: LAN `InsecureConnect` |
| Cloud matchmaking | LAN discovery port 20779 |

## Restore SimulationLoop

```bash
bash tools/restore_simloop.sh
# or cmake -B build -DKSIMULATOR_QT_FREE=ON
```

Vedi `docs/PARITY_STATUS.md`.
