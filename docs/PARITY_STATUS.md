# Parity status — 2026-10-05

## Branding

- Official multiplayer transport name: **ksnet** (not yojimbo).
- Message macros: `KSNET_*` (`YOJIMBO_*` kept as deprecated aliases).
- CMake links target `ksnet` (`Yojimbo::yojimbo` is an ALIAS when needed).

## Wired + hardened

| Feature | Status |
|---------|--------|
| FeatureHub / pit / garage / AI / telemetry | Done |
| **ksnet host 20Hz + RemoteCarInterpolator** | Done |
| **ksnet XOR CarStateSync single-path** | Done |
| **ksnet CAR_DAMAGE / CAR_SETUP / CAR_COLLISION wire** | Done |
| **ksnet auth token (ClientJoin + constant-time)** | Done |
| **Matchmaking (LAN + optional HTTP lobby)** | Done |
| **KSNET_* branding (ex-YOJIMBO macros)** | Done |

## Notes on P2 crypto

- **Application auth:** host `setAuthToken`, client `setJoinToken`; empty = open LAN.
- **Transport:** ksnet still uses `InsecureConnect` by default. Optional private-key secure-connect path can be added later (same PROTOCOL_ID).
- **Matchmaking:** `Matchmaking.h` merges LAN (`ServerDiscovery`) with optional `lobbyBaseUrl` HTTP registry.

## Still open (optional)

| Item | Notes |
|------|-------|
| ksnet secure-connect private-key plumbing | optional production hardening |
| Hosted lobby server binary | optional; any JSON HTTP endpoint works |

## Roadmap

Vedi **GAP_MATRIX.md** (P0–P3 prioritizzata).
