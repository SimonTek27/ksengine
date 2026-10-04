# Parity Status — ksengine vs AC/rF2 (P0–P1)

Last update: 2026-10-05

## Done

| Item | Notes |
|------|-------|
| **P0 FeatureHub** | Done (discovery / control / limits / weather / PB / session / replay) |
| **P0.1 Tyre load sensitivity** | Done |
| **P0.2 Surface grip** | Done |
| **P0.3 Setup continuous apply** | Done |
| **P0.4 FFB continuous** | Done |
| **P0.5 Replay** | Done |
| **P0.6 AI racing line + overtake** | Done |
| **P0.7 Garage exit / pit queue / collision / repair** | Done |
| **P0.8 Damage telemetry (rF2-style)** | Done |
| **P1.1 CarState sync ≥20Hz (UDP)** | Done |
| **P1.2 Control AUTH** | Done (`setControlAuthToken` / `AUTH <token>`) |
| **P1.3 Track limits penalties** | Done |
| **P1.4 Weather/time UI** | Done |
| **P1.5 PB store** | Done |
| **P1.7 Session flow** | Done |
| **P1.8 Pit strategy UI** | Done |
| **P1.9 Server browser UI** | Done |
| **NetSync in SimulationLoop** | Done |
| **SimulationLoop full restore (cmake z0–z4)** | Done |
| **Full ksnet remote car state** | Done (`HAS_KSNET=1`, host 20 Hz `MSG_CAR_STATE`, join/spawn/input/chat) |

See `docs/SECURITY_HARDENING.md`.

## Still open (optional P2+)

| Item | Notes |
|------|-------|
| Encryption / auth on ksnet wire | LAN trust model today (`InsecureConnect`) |
| Dedicated matchmaking service | Local discovery `:20779` covers LAN |

## Restore

```bash
bash tools/restore_simloop.sh
# or: cmake configure with KSIMULATOR_QT_FREE=ON
```

## Build with ksnet

```bash
cmake -B build -DKSENGINE_QT_FREE=ON -DKSIMULATOR_QT_FREE=ON
cmake --build build --target SimulatorApp
# HAS_KSNET=1 when src/core/engine/Network/ksnet is present (linked automatically)
```

## Roadmap

Vedi **GAP_MATRIX.md** (P0–P3 prioritizzata).
