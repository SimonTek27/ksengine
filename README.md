# ksengine

Qt-free / optional-Qt simulation engine (physics, AI, multiplayer, telemetry).

## Status

See [docs/PARITY_STATUS.md](docs/PARITY_STATUS.md) and [docs/GAP_MATRIX.md](docs/GAP_MATRIX.md).

Highlights:
- FeatureHub, pit/garage, AI overtake, setup/FFB, surface grip, tyre load, replay, track limits, PB, session, weather/browser UI
- CarStateSync UDP ≥20 Hz (no ksnet required)
- **ksnet** multiplayer transport (reliable UDP): car-state 20 Hz, damage/setup/collision, auth token, matchmaking

## Build

```bash
cmake -B build -DKSENGINE_QT_FREE=ON
cmake --build build -j
```

`HAS_KSNET` is enabled when the in-tree `ksnet` target is present (default).

## Multiplayer (ksnet)

- Transport: `src/core/engine/Network/ksnet` — official name **ksnet**
- Macros: `KSNET_*` (`YOJIMBO_*` deprecated aliases)
- CMake target: `ksnet` (`Yojimbo::yojimbo` ALIAS for older links)
- Auth: host `setAuthToken`, client `setJoinToken` (constant-time)
- Matchmaking: LAN discovery + optional HTTP lobby via `NetworkManager::setLobbyBaseUrl` / `startMatchmaking`

## Design notes

1. ksengine is a generic open-source sim engine (not a single-title clone).
2. Brand-specific formats stay under `adapters/`; UI stays product-neutral.
3. FeatureHub owns session, discovery `:20779`, control `:20780`, limits, weather, PB.
4. Pit/garage is a state machine separate from vehicle integrate.
5. CarStateSync UDP works without `HAS_KSNET`; ksnet path remains the preferred multiplayer transport.

Docs: [ARCHITECTURE](docs/ARCHITECTURE.md) · [PARITY_STATUS](docs/PARITY_STATUS.md) ·
[GAP_MATRIX](docs/GAP_MATRIX.md) · [GITHUB_RESTORE_AUDIT](docs/GITHUB_RESTORE_AUDIT.md)

---
