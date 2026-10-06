# Memory map — ksengine

Mappa mentale del **motore attuale** (auto): moduli, ownership, flusso dati e confini.
Companion di `ARCHITECTURE.md`, `PARITY_STATUS.md`, `ROADMAP.md`.

**Ambito:** veicolo a 4 ruote + sessione racing + ksnet.  
**Non in mappa come dominio:** moto, barche, aerei.

---

## 1. Product layers

```
ksEditor (opzionale, Qt6)     → solo authoring
        │
SimulatorApp / ksim           → client gara (Win32 + Vulkan)
        │
ksengine (lib)                → core simulazione Qt-free
        │
adapters/                     → formati esterni (KN5, SM, surfaces.ini)
```

| Layer | Ruolo | Qt |
|-------|--------|-----|
| ksengine | Fisica, net, device, math, scene | No |
| SimulatorApp | Loop, menu, render, HUD, wire FeatureHub | No (Win32) |
| adapters | Bridge contenuti AC/CSP | No |
| ksEditor | Tooling | Sì (opzionale) |

---

## 2. Runtime ownership (chi possiede cosa)

| Owner | Possiede | Non possiede |
|-------|----------|--------------|
| **SimulationLoop** | Tick 1 kHz / frame, orchestrazione | Dettaglio MF pneumatico |
| **VehicleSimulator** | 1 auto locale: engine, tyre, aero, susp, diff, damage | Auto remote |
| **MultiCarManager** | AI / traffic cars | Input giocatore |
| **FeatureHub** | Session, pit, limits, weather, PB, setup, replay helpers | Transport UDP |
| **NetworkManager** | Host/join, matchmaking, bridge damage/setup | Fisica |
| **ksnet** (Client/Server) | UDP affidabile, canali, SecureConnect | Logica gara |
| **DeviceManager / FFB*** | Hardware input + FFB out | Stato veicolo |
| **NativeRenderer** | GPU frame | Fisica |
| **TrackSurface** | Griglia grip/wet/rubber (singleton) | Mesh visual |

---

## 3. Fisica — mappa moduli

```
VehicleSimulator::updatePhysics(dt)
    ├── EngineModel          potenza / RPM / marce
    ├── DifferentialModel    split coppia asse
    ├── SuspensionModel      carico / camber proxy
    ├── AeroModel            drag / downforce (auto, non volo)
    ├── KsTireModel          Magic Formula (batch ×4)
    ├── DamageSystem         zone + power/handling scale
    │     └── Rf2DamageModel bridge impulso → applyImpactDamage
    └── TrackSurface         µ locale, rubber deposit
```

| Tipo | File | Note |
|------|------|------|
| Core types | `PhysicsCoreTypes.h` | PhysVec3, SimulationState, … |
| Pneumatico | `KsTireModel.h/.cpp` | Brand ufficiale; `PacejkaTireModel` = alias |
| Manager 4 ruote | `TireSimulator.h/.cpp` | Thermal/wear path alternativo |
| Profiler | `PhysicsProfiler.h/.cpp` | Section + subsystem accumulati |
| Bench | `PhysicsHotPathProfile.cpp` | target `ksphprofile` |
| Bench full | `VehicleSimProfile.cpp` | target `ksvsprofile` |

**Budget misurato (@O2):** ~1.5 µs/auto/step; 16 auto ~24 µs ≪ 1 ms @1 kHz.

---

## 4. Multiplayer — mappa

```
SimulatorApp / Menu
    │
MenuNetworkBridge ──► NetworkManager
                          ├── NetworkLowLevel (Client/Server .inc)
                          │       └── ksnet Client / Server
                          ├── Matchmaking (LAN UDP 20779 + HTTP lobby)
                          ├── CarStateSync / Bridge (≥20 Hz)
                          └── DamageWireBridge / NetworkDamageSetupBridge
                                    │
                         kslobby (processo separato, HTTP registry)
```

| Componente | Responsabilità |
|------------|----------------|
| `ksnet` | Transport; `KSNET_*` macros; SecureConnect 32-byte key |
| `NetworkAuth` | Token join constant-time |
| `NetworkConfig` | PROTOCOL_VERSION, message IDs |
| `Matchmaking` | Discovery + `lobbyBaseUrl` |
| `kslobby` / `kslobby-cli` / web embed | Registry hosted + UI browser |
| `CarStateSync` | Snapshot posizione/vel/input remote |
| Damage/Setup wire | CAR_DAMAGE / CAR_SETUP / CAR_COLLISION |

---

## 5. Sessione e parity services

**FeatureHub** concentra i servizi “gara” (non il transport):

| Servizio | Hook tipico |
|----------|-------------|
| Session flow | start/finish, risultati |
| Pit / garage | queue, repair, exit |
| Track limits | cut → penalità |
| Weather / time | preset + runtime |
| Personal Best | store locale |
| Setup | ApplySetup → VehicleSimulator |
| Replay | record + play |
| Telemetry | SM / UDP / TCP + `fin()` |

Bridge UI: `MenuFeatureBridge`, `PitStrategyBridge`, `GameMenuOverlay`.

---

## 6. Input e FFB

```
Hardware (wheel / pad)
    → DirectInput / XInput / vendor SDK
    → InputManager / InputSystem
    → VehicleSimulator (throttle, brake, steer)

VehicleSimulator (VehicleFFBSample)
    → FfbOutput / FFBBridge
    → LogitechFFB | FanatecFFB | MozaFFB | …
```

---

## 7. Flusso tick (semplificato)

```
frame / physics step
  1. InputManager.poll
  2. FeatureHub pre-tick (session, limits, weather)
  3. VehicleSimulator.updatePhysics (player)
  4. MultiCarManager / AIController update
  5. NetworkManager tick (send CarState, recv remote, damage/setup)
  6. FeatureHub post-tick (PB, replay sample, telemetry)
  7. NativeRenderer + HUD
  8. FFB output
```

NetSync: `SimulationLoop_NetSync` / path singolo CarState (no doppio publish).

---

## 8. Binary / target utili

| Target | Ruolo |
|--------|--------|
| `ksengine` | Libreria core |
| `ksim` / SimulatorApp | Client gara |
| `kslobby` | Lobby HTTP hosted |
| `kslobby-cli` | CLI registry |
| `ksphprofile` | Microbench pneumatico |
| `ksvsprofile` | Bench VehicleSimulator completo |
| SimulatorServer | Headless server (se abilitato) |

CMake: `CMakeLists_physics_QtFree.cmake`, `CMakeLists_LobbyServer.cmake`, root Qt-free hooks.

---

## 9. Confini da non confondere

| Nome | È | Non è |
|------|---|--------|
| AeroModel | Deportanza/drag **auto** | Dinamica di volo |
| TrackSurface | Grip griglia mondo | Renderer pista |
| KsTireModel | MF + cache | Intero veicolo |
| FeatureHub | Servizi sessione | Socket UDP |
| ksnet | Transport | Matchmaking HTTP (sta in Matchmaking/kslobby) |
| CharacterPhysics | Pedone/altro | Auto |

---

## 10. Indice file “ancora” (artifacts / engine)

| Area | File chiave |
|------|-------------|
| Veicolo | `VehicleSimulator.*`, `KsTireModel.*`, `EngineModel.*`, `AeroModel.*`, `DifferentialModel.*`, `SuspensionModel.*`, `DamageSystem.*`, `Rf2DamageModel.h` |
| Superficie | `TrackSurface.h`, `AcSurfacesLoader.h` |
| AI | `AIController.*`, `AiSpline.h`, `MultiCarManager.*` |
| Net | `NetworkManager.*`, `NetworkLowLevel*`, `ksnet.*`, `Matchmaking.h`, `CarStateSync*` |
| Lobby | `LobbyServerApp.cpp`, `LobbyClientApp.cpp`, `LobbyWebClient.html`, `LobbyWebEmbedded.h` |
| Sessione | `FeatureHub.h`, `SessionFlow.h`, `SessionController.h`, `PersonalBestStore.h` |
| Input/FFB | `InputManager.*`, `FfbOutput.h`, `FFBBridge.*`, `*FFB.*` |
| Loop | `SimulationLoop.*`, `SimulationLoop_NetSync.cpp` |
| Status | `PARITY_STATUS.md`, `GAP_MATRIX.md`, `ROADMAP.md` |

---

## 11. Aggiornamento

Aggiornare questa mappa quando:

- nasce un owner nuovo (es. AudioSystem di produzione);
- cambia il path unico di sync rete;
- si introduce un secondo tipo veicolo (richiederebbe sezione dominio dedicata).

*Memory map allineata allo stato 2026-10-05 (P0/P1 done, focus P2 prodotto).*

## 12. Teoria di riferimento

**Milliken & Milliken, *Race Car Vehicle Dynamics* (SAE R-146)** — mappa operativa in `MILLIKEN_KSENGINE.md` (bicycle, LLTD, friction circle, gap G1–G8).
