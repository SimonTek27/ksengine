# Parity status — 2026-10-09

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
| **Fase 1 exit: menu sessions gated (practice / quick race / time attack)** | Done |
| **Roadmap 2.1 Wet physics (grip curve bagnato + aquaplaning G11 + WeatherSimulator dynamics)** | Done |
| **Roadmap 2.2 Rain visual / spray (emitter pioggia + spray di ruota, `KS_PARTICLES=1`)** | Done |
| **Roadmap 2.3 Audio 3D (doppler/distanza/bearing per altre auto + rumore rolling per superficie)** | Done |
| **Roadmap 2.4 Rendering LOD (finestre KN5 lodIn/lodOut → cache NMS2 + distance culling in drawMesh)** | Done |
| **Rendering 2A texture + materiali (TextureRuntime DDS + `MaterialCache` materials.txt → descriptor set 1 albedo/normal/UBO → shader forward/GBuffer)** | Done |
| **Roadmap 2.4 Contenuto di riferimento (bake commitati `content/baked` + `content/cars/refcar` via `tools/make_reference_content.ps1`; `test_renderer` su manifest→materiali→DDS→pixel; sessione windowed `KS_AUTOSTART`+`KS_CAR`+`KS_SCREENSHOT`)** | Done |
| **Roadmap 2.5 Verifica GPU (artifact PNG `test_renderer_reference.png` + `KS_SCREENSHOT` in SimulatorApp, contatori `submitted/drawn/culled`, messaggi stderr per manifesto/mesh/DDS/shader mancanti)** | Done |
| **Rendering AI brief S1 (P0) (mip chain CPU a runtime in `TextureRuntime` + feature `samplerAnisotropy` con `maxLod=mipLevels-1`)** | Done |
| **Rendering AI brief S2 (P1) (`materials.txt` celle roughness/metalness dual-typed numero O mappa → binding 3/4 del set 1 → `gbuffer.frag` RT0.a = metalness → F0 = mix(0.04, albedo, metalness) + diffuse ×(1−metalness) in `deferred_lighting.frag`; `Kn5Baker` emette mappe + euristica paint/carbon)** | Done |
| **Rendering AI brief S3 (P2) (`IblGenerator.h` CPU deterministica: cielo equirect 128×64 RGBA16F senza disco solare → prefilter 5 mip + irradiance E/π + BRDF LUT 128×128, upload con viste dummy mai-nulle → `iblParams[4]` in coda di `FrameDataUBO` → branch split-sum in `native_forward.frag` e `deferred_lighting.frag`; `KS_IBL=0` opt-out, fallback ambient flat ×0.25 fail-open; A/B sole spento su contenuto di riferimento in `test_renderer`)** | Done |
| **Rendering AI brief S4 (P3+P4) (clear-coat: 6ª cella opzionale di `materials.txt` → `MaterialData.clearcoat` (ex `pad`) → flag nel segno di `RT1.w` → secondo lobo speculare F0 0.04 / roughness 0.07 in `deferred_lighting.frag` **e** `native_forward.frag`, lobo base ×`sunRad` contro il phantom specular, baker che emette la cella solo per righe vernice; sharpen 4-tap pre-bloom appendito in coda a `TonemapPC` 48→64 B che segue lo stato TAA; A/B quad "coated" vs "rough" + pin roll-off ACES a exposure 3× in `test_renderer`; `KS_EXPOSURE`/`KS_TONEMAP` già cablati in SimulatorApp)** | Done |
| **Roadmap 2.8 Team + grid (`TeamSessionBridge` → `MultiCarManager` GridCar/`spawnGrid` → `RaceSessionManager`, menu TEAM, `KS_TEAM`/`KS_TRACK`/`KS_SESSION`, `GarageSpawnPolicy`; `menu_session_gate_test`)** | Done (wire-up; collaudo AC reale = gate umano ROADMAP) |
| **Roadmap 2.9 Upgrade + audio (menu upgrade → `cycleUpgradeRow` → `ApplyVehicleUpgrades` su fisica/render/livrea + `SoundPack`, `KS_UPGRADES`; `menu_session_gate_test`)** | Done (wire-up; collaudo AC reale = gate umano ROADMAP) |

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

Vedi [**ROADMAP.md**](ROADMAP.md) per le priorità correnti e i gate di
rilascio del runtime. [**GAP_MATRIX.md**](GAP_MATRIX.md) resta l'inventario
storico dei gap P0–P3.
