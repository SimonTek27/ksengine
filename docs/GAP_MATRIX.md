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
| 10 | Client interpolation + single-path policy | **Done** |

## Multiplayer stack

| Component | Role |
|-----------|------|
| `ksnet` | Reliable-UDP transport |
| `NetworkLowLevel_*` | Join/spawn/state/chat/session handlers |
| `NetworkManager` | Host 20 Hz + **RemoteCarInterpolator** (100 ms delay) |
| `RemoteCarInterpolator.h` | Snapshot ring buffer, lerp pos/rot/controls, limited extrapolate |
| `SimulationLoop_NetSync` | **XOR**: ksnet active ⇒ stop CarStateSync; else UDP fallback |
| `FeatureTick` | Host session/penalty/countdown/lap → ksnet |

### Tuning

```cpp
m_network->setInterpolationDelay(0.10); // seconds behind latest snapshot
```

## Optional (P1–P2)

| Item | Notes |
|------|-------|
| `MSG_CAR_DAMAGE` / `MSG_CAR_SETUP` on wire | Enum present, handlers TBD |
| Client prediction + reconciliation | Own car only |
| Wire encryption / token auth | LAN `InsecureConnect` today |
| Multiplayer UI widget | Discovery exists; dedicated UI optional |
| Dedicated server + reconnect | `tools/ks_server` scaffold |

## Restore SimulationLoop

```bash
bash tools/restore_simloop.sh
cmake -B build -DKSENGINE_QT_FREE=ON -DKSIMULATOR_QT_FREE=ON
```
