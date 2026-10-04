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

## ksnet path (Sprint 9)

| Component | Role |
|-----------|------|
| `src/core/engine/Network/ksnet` | Reliable-UDP transport (yojimbo-shaped API) |
| `NetworkConfig.h` | Messages: join/welcome/car state/spawn/input/chat/session |
| `NetworkLowLevel.cpp` | Client/Server process + broadcast |
| `NetworkManager` | Host 20 Hz car-state pump, late MultiCar rebind |
| `SimulationLoop` | `broadcastLocalCarState` + remote apply (archive) |
| `SimulationLoop_NetSync` | Dual path: ksnet **and** CarStateSync UDP |
| Qt-free `SimulatorApp` | Links `ksnet` when target exists (`HAS_KSNET=1`) |

CarStateSync UDP remains the zero-dependency fallback when `HAS_KSNET=0`.

## Optional (P2+)

| Item | Notes |
|------|-------|
| Wire encryption / token auth | Today: LAN `InsecureConnect` |
| Cloud matchmaking | LAN discovery port 20779 |

## Restore SimulationLoop

Stub on clone → auto-expand from `cmake/simloop_z0.b64`…`z4.b64`:

```bash
bash tools/restore_simloop.sh
# or cmake -B build -DKSIMULATOR_QT_FREE=ON
```

Vedi `docs/PARITY_STATUS.md` e `docs/RESTORE_SIMLOOP.md`.
