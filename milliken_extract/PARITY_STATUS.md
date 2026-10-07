# Parity status — 2026-10-05

Stato di parità **ksengine** rispetto al percorso auto (AC/rF2-like): fisica, sessione, multiplayer **ksnet**, lobby.

Documenti collegati: `ROADMAP.md`, `GAP_MATRIX.md`, `ARCHITECTURE.md`.

---

## Wired + hardened

| Feature | Status |
|---------|--------|
| FeatureHub / pit / garage / AI / telemetry | Done |
| Vehicle setFrozen + NaN recovery | Done |
| Path guards (`..`, length) | Done |
| SimulatorServer CLI sanitize | Done |
| Telemetry `fin()` | Done |
| **P0.1** Tyre INI load sensitivity | Done |
| **P0.2** Surface grip | Done |
| **P0.3** Setup → VehicleSimulator | Done |
| **P0.4** FFB sample → hardware (FfbOutput) | Done |
| **P0.5** Replay play | Done |
| **P0.6** AI racing line + overtake | Done |
| **KsTireModel** rename + opt + unify | Done |
| Physics hot-path profiling (`ksphprofile`) | Done |
| Full VehicleSimulator profiling (`ksvsprofile`) | Done |
| **P1.1** CarState sync ≥20 Hz (UDP) | Done |
| **P1.2** Control AUTH | Done |
| **P1.3** Track limits penalties | Done |
| **P1.4** Weather/time UI | Done |
| **P1.5** PB store | Done |
| **P1.7** Session flow | Done |
| **P1.8** Pit strategy UI | Done |
| **P1.9** Server browser UI | Done |
| NetSync in SimulationLoop | Done |
| ksnet host 20 Hz + RemoteCarInterpolator | Done |
| ksnet XOR CarStateSync single-path | Done |
| ksnet CAR_DAMAGE / CAR_SETUP / CAR_COLLISION wire | Done |
| DamageWireBridge + NetworkDamageSetupBridge | Done |
| Host damage publish (FeatureTick ~5 Hz on change) | Done |
| ksnet auth token (ClientJoin + constant-time) | Done |
| Matchmaking (LAN discovery + optional HTTP lobby) | Done |
| Matchmaking wired into NetworkManager | Done |
| ksnet SecureConnect private-key plumbing | Done |
| Hosted lobby server binary (`kslobby`) | Done |
| Lobby client (`kslobby-cli` + MenuNetworkBridge) | Done |
| Lobby web client (browser UI in kslobby) | Done |

---

## Branding

| Elemento | Nome ufficiale |
|----------|----------------|
| Transport multiplayer | **ksnet** (non yojimbo) |
| Macro messaggi | `KSNET_*` (`YOJIMBO_*` = alias deprecati) |
| Target CMake | `ksnet` (`Yojimbo::yojimbo` = ALIAS se serve) |
| Modello pneumatico | **KsTireModel** (`PacejkaTireModel` = alias deprecato) |

---

## Fisica e profiling

- **KsTireModel:** Magic Formula di progetto. Cache coeff Fz/camber/µ, fast atan/sin, identità `sin(2·atan)`, API batch 4 ruote. `TireSimulator` e `VehicleSimulator` unificati su KsTireModel.
- **ksphprofile** (`PhysicsHotPathProfile.cpp`): @O2 — force warm ~0.07 µs, batch×4 ~0.19 µs, TireSimulator 1 auto ~0.33 µs, 16 auto ~4.5 µs/step. CSV: `physics_hotpath_profile.csv`.
- **ksvsprofile** (`VehicleSimProfile.cpp`): `VehicleSimulator::updatePhysics` @1 kHz — ~1.5 µs/auto; 16 auto ~24 µs/step (~2.4% di 1 ms). Section: tires / aero / damage / VehicleDynamics. Report: `VEHICLE_SIM_PROFILE_REPORT.txt`.
- **PhysicsProfiler:** accumulo section/subsystem + `report()`.
- Build support: `Rf2DamageModel.h`, membri `TrackSurface`, `SimulationState::mass`.

---

## Multiplayer / crypto / lobby

- **Application auth:** host `setAuthToken`, client `setJoinToken`; token vuoto = LAN aperta.
- **Secure-connect:** `Server::SetPrivateKey` / `Client::SecureConnect` (chiave 32 byte). CONNECT con key-proof 8 byte (reject reason 2 = mismatch). Payload dopo header 13 byte in chiaro: XOR-keystream. Default `InsecureConnect` se nessuna chiave.
- **NetworkManager:** `setPrivateKey` / `clearPrivateKey` / `hasPrivateKey` (server + client locale).
- **Matchmaking:** `Matchmaking.h` — LAN (`ServerDiscovery` UDP 20779) + registry HTTP opzionale (`GET/POST /servers`, `DELETE` on stop). Soft-fail se URL assente o irraggiungibile.
- **NetworkManager matchmaking:** `setLobbyBaseUrl`, `startMatchmaking` / `stopMatchmaking`, `refreshServerList`, `matchmakingServers` / `matchmakingBrowserRows`, callback `onServerListUpdated`. Host auto-announce su `hostServer()`.
- **kslobby:** binary lobby HTTP (`LobbyServerApp.cpp`), registry TTL, inferenza host da peer-IP.
- **kslobby-cli:** `list` / `health` / `register` / `delete` — env `KS_LOBBY_URL`.
- **Lobby web:** `LobbyWebClient.html` embedded (`LobbyWebEmbedded.h`); path `/`, `/index.html`, `/web`, `/browser`. Tabella live, auto-refresh, register/delete, CORS. HTML standalone: `?api=http://host:port/api/v1`.
- **MenuNetworkBridge:** host/join/refresh menu → NetworkManager (LAN + lobby). `SimulatorApp` browser usa matchmaking + `KS_LOBBY_URL`.

Vedi anche `docs/SECURITY_HARDENING.md` se presente nel tree sorgente completo.

---

## Still open (prodotto / P2+)

Il path multiplayer P2 tecnico (auth + secure-connect + lobby) è **completo**.

Aperti lato **prodotto** (vedi `ROADMAP.md` / `GAP_MATRIX.md`):

| Area | Note |
|------|------|
| Rendering KN5 / LOD | P2.1 — parziale |
| Audio 3D | P2.2 |
| Menu racing completo | P2.5 |
| Aquaplaning / rain fisico | P2.8 |
| Damage HUD telemetry channels | P1.10 |
| Docs API control TCP | P2.10 |
| Joystick mapping UI | P2.7 |

---

## Roadmap

Vedi **ROADMAP.md** (fasi sul motore auto attuale) e **GAP_MATRIX.md** (gap P0–P3).


## Milliken theory (2026-10-06)

**G1–G18 CLOSED** — LLTD, load transfer, κ, UG/β/SM, friction circle, yaw transient, compliance, MMM, g-g, tire normalize, aquaplane, aero/thermal, diff/FLT, camber/wear, brake thermal, RH aero/coast, pressure lag, pair/pitch.
