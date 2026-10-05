# Security & robustness hardening

## Path loading

- `loadTrack` / `loadTrackFolder` / `loadCar`: reject empty, >4096 chars, `..` segments
- `loadGarageFromTrack`: `isSafePath()` rejects traversal and null bytes

## SimulatorServer CLI

| Check | Rule |
|-------|------|
| `--track` | `safePathArg` — no `..`, max 4096 |
| `--name` | alnum + `-_ .`, max 64 |
| `--ai` | clamp 0–32 |
| `--game-port` | 1024–65535 (no privileged) |
| unknown args | exit 2 |
| SIGPIPE | ignored on POSIX |
| tick | try/catch + slow-frame log |

## Physics

- `VehicleSimulator::updatePhysics`: skip if `!m_running`, `m_frozen`, non-finite/≤0/>0.1 dt
- Post-integrate: recover if position/speed/heading non-finite
- Snap pose: `finiteOr` on coordinates

## Telemetry

- Shared memory / UDP / TCP publish uses `fin()` so non-finite values become 0

## Caps

- Garage boxes 1–32, AI cars ≤32, session time ≤86400s, laps ≤200
