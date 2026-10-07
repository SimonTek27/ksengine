# ksengine — codice completo (moduli sim/pit/server/physics)

Allineato a **GitHub** `SimonTek27/ksengine` branch `master` (commit hardening incluso).

## File principali (Qt-free)

| File | Path repo | Ruolo |
|------|-----------|--------|
| `SimulationLoop.h` | `src/simulator/` | API loop: pit, garage, freeze, telemetry flags |
| `SimulationLoop.cpp` | `src/simulator/` | tick, input, path guards, render HUD |
| `SimulationLoop_FeatureMethods.cpp` | `src/simulator/` | session, garage, pit, snap+freeze, AI, isSafePath |
| `SimulationLoop_Telemetry.cpp` | `src/simulator/` | SM / UDP / TCP publish + `fin()` |
| `SimulatorServerApp.cpp` | `src/simulator/` | headless server CLI hardened |
| `VehicleSimulator.h` | `src/engine/physics/` | `setFrozen`, damage, aero/tyres |
| `VehicleSimulator.cpp` | `src/engine/physics/` | integrate + NaN recovery |

## Build

```bash
# Qt-free
cmake -DKS_QT_FREE_BUILD=ON -DKSIMULATOR_QT_FREE=ON -B build && cmake --build build

# Targets
#   ksimulator          — client
#   SimulatorServer     — headless (CMakeLists_SimulatorServer.cmake)
#   SimulatorApp        — include FeatureMethods + Telemetry
```

## Server

```text
SimulatorServer [--announce] [--track DIR] [--ai N] [--name NAME] [--game-port 40000]
```

## Dipendenze header (già nel tree)

`FeatureHub.h`, `GarageExit.h`, `GarageSpawn.h`, `PitLaneQueue.h`, `PitLaneCollision.h`,
`PitLaneRepair.h`, `NetworkManager.h`, `MultiCarManager.h`, bridges UDP/TCP, adapters SM.

## Docs

- `docs/SECURITY_HARDENING.md`
- `docs/PARITY_STATUS.md`
- `docs/ARCHITECTURE.md` / `docs/PIT_WIRING.md`

Copia locale artifacts = stesso contenuto dei path sopra.
