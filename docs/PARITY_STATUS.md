# Parity status — 2026-10-05

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

See `docs/SECURITY_HARDENING.md`.

## Notes on P2 crypto

- **Application auth:** host `setAuthToken`, client `setJoinToken`; empty = open LAN.
- **Transport:** ksnet/yojimbo still uses `InsecureConnect` by default. For full packet encryption, supply a private key to the yojimbo secure connect path (optional; same PROTOCOL_ID).
- **Matchmaking:** `Matchmaking.h` merges LAN (`ServerDiscovery`) with optional `lobbyBaseUrl` HTTP registry (`GET/POST /servers`). Soft-fails if URL empty or unreachable.

## Still open (optional)

| Item | Notes |
|------|-------|
| yojimbo secure connect private-key plumbing | optional production hardening |
| Hosted lobby server binary | optional; any JSON HTTP endpoint works |

## Roadmap

Vedi **GAP_MATRIX.md** (P0–P3 prioritizzata).
