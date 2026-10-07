# Parity status — 2026-10-07

## Wired + hardened

| Feature | Status |
|---------|--------|
| FeatureHub / pit / garage / AI / telemetry | Done |
| Vehicle setFrozen + NaN recovery | Done |
| Path guards (`..`, length) | Done |
| SimulatorServer CLI sanitize | Done |
| Telemetry `fin()` | Done |
| **P0.3 Setup → VehicleSimulator** | Done |
| **P0.4 FFB sample → hardware** | Done (FfbOutput) |
| **P0.1 Tyre INI load sensitivity** | Done |
| **P0.2 Surface grip** | Done |
| **P0.5 Replay play** | Done |
| **P1.7 Session flow** | Done |
| **P1.2 Control AUTH** | Done |
| **P1.3 Track limits penalties** | Done |
| **P1.5 PB store** | Done |
| **P1.9 Server browser UI** | Done |
| **P1.4 Weather/time UI** | Done |
| **P0.6 AI racing line + overtake** | Done |
| **P1.1 CarState sync ≥20Hz (UDP)** | Done |
| **P1.8 Pit strategy UI** | Done |
| **NetSync in SimulationLoop** | Done |
| **ksnet host 20Hz + RemoteCarInterpolator** | Done |
| **ksnet XOR CarStateSync single-path** | Done |
| **ksnet CAR_DAMAGE / CAR_SETUP / CAR_COLLISION wire** | Done |
| **DamageWireBridge + NetworkDamageSetupBridge** | Done |
| **Host damage publish (FeatureTick ~5Hz on change)** | Done |
| **ksnet auth token (ClientJoin + constant-time)** | Done |
| **Matchmaking (LAN discovery + optional HTTP lobby)** | Done |
| **Matchmaking wired into NetworkManager** | Done |
| **kslobby registry (`kslobby` + `kslobby-cli`)** | Done |
| **Roadmap 1.1 audio base (SimulatorAudio telemetry + synth SFX)** | Done |
| **Roadmap 1.5 Damage HUD + damage channels (HUD/SM/UDP/TCP)** | Done |
| **Roadmap 1.6 Control API docs (`docs/CONTROL_API.md`)** | Done |
| **Roadmap 1.4 Bindings volante (KeyboardMapping + rebind overlay)** | Done |
| **Roadmap 1.3 Menu minimo (car/track select + results)** | Done |
| **Roadmap 1.2 Track/car stable load (bake swap + solid placeholder)** | Done |

See `docs/SECURITY_HARDENING.md`.

## Branding

- Official multiplayer transport name: **ksnet** (not yojimbo).
- Message macros: `KSNET_*` (`YOJIMBO_*` kept as deprecated aliases for migration).
- CMake links target `ksnet` (`Yojimbo::yojimbo` is an ALIAS when needed).

## Notes on P2 crypto

- **Application auth:** host `setAuthToken`, client `setJoinToken`; empty = open LAN.
- **Transport:** ksnet still uses `InsecureConnect` by default. Optional secure-connect private-key path is the next hardening step (same PROTOCOL_ID).
- **Matchmaking:** `Matchmaking.h` merges LAN (`ServerDiscovery` UDP 20779) with optional `lobbyBaseUrl` HTTP registry (`GET/POST /servers`). Soft-fails if URL empty or unreachable.
- **NetworkManager API:** `setLobbyBaseUrl`, `startMatchmaking` / `stopMatchmaking`, `refreshServerList`, `matchmakingServers` / `matchmakingBrowserRows`, callback `onServerListUpdated`. Host auto-announces on `hostServer()`.

## Still open (optional)

| Item | Notes |
|------|-------|
| ksnet secure-connect private-key plumbing | optional production hardening |

## Roadmap

Vedi **GAP_MATRIX.md** (P0–P3 prioritizzata).
