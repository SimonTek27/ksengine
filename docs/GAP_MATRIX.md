# Matrice gap prioritaria — ksim / ksengine

**Data:** 2026-10-03  
**Riferimento prodotto:** SimulatorApp indipendente (stile racing sim completo)  
**Benchmark operativi:** rF2 (garage/pit/danni), LFS (ops/server), AC (formati/telemetria), non clone brand

Scala effort: **S** ≤1 sett · **M** 2–4 sett · **L** 1–3 mesi · **XL** >3 mesi  
Scala impatto: **Critico** (blocco uso serio) · **Alto** · **Medio** · **Basso**

---

## P0 — Critico (motore utilizzabile offline + sessione coerente)

| ID | Gap | Stato attuale | Target | Impatto | Effort | Dipendenze |
|----|-----|---------------|--------|---------|--------|------------|
| P0.1 | Fisica pneumatici/carico validata | Pacejka base + aero | Curve caricate da INI, load sensitivity, slip stabile | Critico | L | VehicleSimulator, tyre.ini |
| P0.2 | Superficie pista + grip | TrackSurface + wetness base | µ per materiale, bump, bordi | Critico | M | surfaces.ini, spline |
| P0.3 | Setup completo applicato al vehicle | SetupGarage + load file | Camber/toe/pressione/gears → integrate | Critico | M | ApplySetup, VehicleSimulator |
| P0.4 | FFB da sample pneumatici | VehicleFFBSample presente | Output DeviceManager (Logitech/Fanatec/Moza) continuo | Critico | M | FFBBridge, InputSystem |
| P0.5 | Replay play (non solo record) | loadReplay stub FeatureHub | Scrub, play, camera follow | Alto | M | ReplayRecorder |
| P0.6 | AI su spline con racing line | MultiCar + loadAiSpline | Speed profile, overtake base, pit-in | Alto | L | AiSpline, AIController |

**Criterio uscita P0:** una sessione practice offline con 1 auto player + AI, setup, FFB, senza crash physics/NaN.

---

## P1 — Alto (parity operativa gara / pit / server)

| ID | Gap | Stato attuale | Target | Impatto | Effort | Dipendenze |
|----|-----|---------------|--------|---------|--------|------------|
| P1.1 | Sync stato auto remote in tick | NetworkManager + HAS_KSNET stub | Broadcast CarState ≥20 Hz, lag comp | Critico (MP) | L | ksnet/yojimbo |
| P1.2 | Auth control TCP :20780 | API aperta LAN | Token / whitelist IP | Alto | S | ExternalControlApi |
| P1.3 | Track limits → penalità applicate | TrackLimitsMonitor + penalty hook | Cut detection → DT/SG reali | Alto | M | RaceSessionManager |
| P1.4 | Weather/time UI + runtime | WeatherControl + presets | Slider ora, pioggia, temp track live | Alto | M | FeatureHub, menu |
| P1.5 | Personal Best store persistente | PB submit base | File user/pb, leaderboard locale | Medio | S | PersonalBestStore |
| P1.6 | Multi-layout track | TrackLayoutCatalog | Switch layout senza reload completo | Medio | M | TrackLoader |
| P1.7 | Session modes completi | Practice/Race beginSession | Qualy → grid → race → results | Alto | M | SessionController |
| P1.8 | Pit strategy (fuel/tyres UI) | PitLaneRepair jobs | Menu richiesta servizio, tempi realistici | Alto | M | PitLaneRepair, HUD |
| P1.9 | Server browser (LAN + manual) | ServerDiscovery UDP | Lista host, join one-click | Alto | M | GameMenuOverlay |
| P1.10 | Damage HUD + telemetry channels | RaceHudSample damage | Canali SM dedicati body/engine/susp | Medio | S | Telemetry, DamageSystem |

**Criterio uscita P1:** host LAN con 2 client, penalità track limits, weather live, risultati sessione.

---

## P2 — Medio (qualità prodotto / contenuti / UX)

| ID | Gap | Stato attuale | Target | Impatto | Effort | Dipendenze |
|----|-----|---------------|--------|---------|--------|------------|
| P2.1 | Rendering pista/auto LOD | NativeRenderer parziale | KN5 mesh + texture pipeline stabile | Alto | XL | RenderSystem, Vulkan |
| P2.2 | Audio 3D motore/ambiente | SimulatorAudio + volumes CSP-like | Doppler, distanza, surface noise | Alto | L | Audio backend |
| P2.3 | Vehicle upgrades + livrea + sound per gara | Design _rd* / RaceComponentConfig | Load kn5 subset + skin + bank | Medio | L | vehicle_upgrades |
| P2.4 | Team info (n° auto, race numbers, box) | Spec definita | UI garage team + spawn | Medio | M | GarageSpawn |
| P2.5 | Menu stile racing UI completo | GameMenuOverlay | Garage / pit / results screens | Medio | L | Native UI |
| P2.6 | Triple monitor / viewport | Non prioritizzato | Multi-view camera | Basso | M | CameraController |
| P2.7 | Joystick mapping UI | InputManager | Bindings persistenti user | Medio | M | InputManager |
| P2.8 | Rain visual + aquaplaning | Rain intensity flag | Spray, grip wet curve | Medio | L | Physics + GFX |
| P2.9 | CMake opzionali editor (ImGui/Qt path) | Root CMake snellito | Restore se serve ksEditor | Basso | S | CMakeLists |
| P2.10 | Documentazione API esterna | SECURITY + PARITY | OpenAPI control TCP, SM layout | Medio | S | docs |

---

## P3 — Basso / lungo termine (parità “pro”)

| ID | Gap | Note | Effort |
|----|-----|------|--------|
| P3.1 | Ranked matchmaking / skill rating | Fuori scope open-source iniziale | XL |
| P3.2 | Laser-scan track pipeline | Tooling offline | XL |
| P3.3 | Validazione pneumatici vs telemetria reale | Campagna test + tuning | XL |
| P3.4 | Driver swap endurance | Dopo P1.7/P1.8 | L |
| P3.5 | VR (OpenXR) | Header opzionali HAS_OPENXR | L |

---

## Ordine di lavoro consigliato (8–12 settimane)

| Sprint | Focus | ID |
|--------|-------|-----|
| 1 | Setup → vehicle + FFB end-to-end | P0.3, P0.4 |
| 2 | Surface grip + tyre load from INI | P0.2, P0.1 (parziale) |
| 3 | Replay play + session results | P0.5, P1.7 |
| 4 | Track limits + PB + auth API | P1.3, P1.5, P1.2 |
| 5 | Server browser + weather UI | P1.9, P1.4 |
| 6 | AI racing line base | P0.6 |
| 7–8 | Multiplayer CarState broadcast | P1.1 |
| backlog | Render LOD, audio 3D, upgrades | P2.* |

## Già chiuso

FeatureHub, garage/pit stack, snap+freeze, telemetry SM/UDP/TCP, SimulatorServer hardened, damage→HUD.

## KPI

| Fase | Criterio |
|------|----------|
| Fine P0 | 30 min practice stabile, 0 NaN, FFB attivo, 4 AI |
| Fine P1 | 2 client stesso server, gara 5 giri con penalità |
| Fine P2 | Menu completo + 1 track KN5 visual + audio motore |

---

# Gap Matrix — prioritised parity roadmap

## Sprint status

| Sprint | Scope | Status |
|--------|--------|--------|
| 1–3 | Setup, FFB, surface, tyre, replay, session | **Done** |
| 4 | Limits, PB, AUTH | **Done** |
| 5 | Server browser + weather UI | **Done** |
| 6 | AI line + overtake | **Done** |
| 7 | CarStateSync UDP ≥20 Hz | **Done** |
| 8 | NetSync + pit strategy UI | **Done** |
| 9 | Full ksnet remote car state | **Done** |
| 9b | Session / countdown / penalty / lap bridge | **Done** |
| 10 | Client interpolation + single-path policy | **Done** |

## Multiplayer stack

| Component | Role |
|-----------|------|
| `ksnet` | Reliable-UDP transport |
| `NetworkLowLevel_*` | Join/spawn/state/chat/session handlers |
| `NetworkManager` | Host 20 Hz + **RemoteCarInterpolator** (100 ms delay) |
| `RemoteCarInterpolator.h` | Snapshot ring buffer, lerp pos/rot/controls, limited extrapolate |
| `SimulationLoop_NetSync` | **XOR**: ksnet active ⇒ stop CarStateSync; else UDP fallback |
| `FeatureTick` | Host session/penalty/countdown/lap → ksnet |

### Tuning

```cpp
m_network->setInterpolationDelay(0.10); // seconds behind latest snapshot
```

## Optional (P1–P2)

| Item | Notes |
|------|-------|
| `MSG_CAR_DAMAGE` / `MSG_CAR_SETUP` on wire | Enum present, handlers TBD |
| Client prediction + reconciliation | Own car only |
| Wire encryption / token auth | LAN `InsecureConnect` today |
| Multiplayer UI widget | Discovery exists; dedicated UI optional |
| Dedicated server + reconnect | `tools/ks_server` scaffold |

## Restore SimulationLoop

> **Not needed in this tree:** `src/simulator/SimulationLoop.cpp` is the full
> source, not the stub. The archived variant (`cmake/simloop_z*.b64`) predates
> the current implementation and would clobber it - only run this on a
> deliberately stubbed checkout.

```bash
bash tools/restore_simloop.sh
cmake -B build -DKSENGINE_QT_FREE=ON -DKSIMULATOR_QT_FREE=ON
```
