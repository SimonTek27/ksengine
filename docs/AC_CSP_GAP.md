# Gap analysis — ksim/ksengine vs Assetto Corsa + Custom Shaders Patch (CSP)

**Data:** 2026-11-09  
**Ambito:** runtime Qt-free (`ksim` / `SimulatorApp`) rispetto a AC 1 + CSP 0.1.78+  
**Fonti:** `REPORT_AC_RE_ENGINEERING.md`, `ROADMAP.md`, `GAP_MATRIX.md`, `PARITY_STATUS.md`,
`WIREUP_GAPS.md`, `PHASE3_PLAN.md`, `ksAssettoCorsa.MD`, codice su `master`.

> Nota: AC+CSP non è una specifica unica. Qui si confronta il simulatore con le capacità
> pubbliche/osservabili di AC (formati, telemetria, sessioni, multiplayer) e le estensioni CSP
> (grafica, weather, scripting, audio, display).

---

## 0. Sintesi

ksengine ha già coperto la maggior parte del nucleo di guida AC: fisica, setup, superfici,
wet/aquaplaning, FFB, sessioni, penalità base, track limits, replay, AI, contenuti AC,
telemetria shared memory/UDP/TCP, multiplayer `ksnet`, renderer Vulkan con LOD/materiali/
IBL/ombre/pioggia/particles/mirrors/parabrezza, audio 3D.

I gap reali rispetto ad AC+CSP sono soprattutto:

1. **Interoperabilità AC incompleta**: mapping `Local\acpmf`, semantica comandi UDP 9666,
   penalties enum AC, AI trace saver, replay `.rep`, ACP server protocol, race control UDP.
2. **Parità CSP parziale**: seasons, dynamic lights, grass FX, shader replacement, dashboards
   digitali, convolution reverb/geometry occlusion, CSP Lua scripting.
3. **Feature prodotto ancora aperte**: VR, triple monitor, driver swap, championship end-to-end,
   ranked, laser-scan, gate umani.

---

## 1. Già allineato (non è il problema)

| Area | Evidenza |
|---|---|
| Fisica 4 ruote, setup, FFB, superfici, wet/aquaplaning | `VehicleSimulator`, `TrackSurface`, `WeatherSimulator`, `FfbOutput` |
| Sessioni, lap/sector, penalità base, track limits | `RaceSessionManager`, `TrackLimitsMonitor`, `LapSectorTimer` |
| Replay record/play | `ReplayRecorder` + control API |
| AI su spline | `AiSpline`, `AIController`, `MultiCarManager` |
| Contenuti AC | `ks_cartool`, `TrackLoader`, `CarPhysicsJson`, `AcCarManifestConverter` |
| Shared memory AC | `AcSharedMemoryPublisher` con mapping 592 B fissi |
| UDP telemetry 9996 | op 0/1/2/3/4/5 implementati |
| UDP commands 9666 | socket dgram, non-blocking, dispatch `'B'..'Q'` |
| TCP control | `ExternalControlApi` |
| Multiplayer | `ksnet`, `NetworkManager`, `RemoteCarInterpolator` |
| Renderer | Vulkan, LOD, DDS/Kn5, materials, IBL, SSAO/SSR, motion blur, rain, particles, mirrors, windscreen, wipers, brake glow |
| Audio | `SimulatorAudio`, WASAPI, audio 3D, surface noise |
| Tools | `ks_cartool`, `ks_tracktool`, `kstirenorm`, `ksmmm` |

---

## 2. Gap rispetto ad AC (interoperabilità e protocollo)

| ID | Area AC | Stato ksengine | Gap / azione |
|---|---|---|---|
| AC-1 | Mapping `Local\acpmf` (412 B) | Implementato (mapping 412 B, zero-filled, soft fallback) | Mapping opzionale per tool AC che la leggono (P6) — **fatto** |
| AC-2 | UDP 9666 comandi | Socket + dispatch byte-comando | Semantica delle lettere `'B'..'Q'` non implementata: teleport pit, reset, recover, camera/replay, ecc. (§3.2 report) |
| AC-3 | Penalties AC | Parziale (enum JumpStart/MandatoryPit/TrackCut + PT/PTA accumulators aggiunti) | Restano DQ pending, soppressione clear min-speed, wiring HUD/SM/UDP (P4) |
| AC-4 | AI trace saver | Carica `fast_lane.ai` | Salvataggio `fast_lane.ai.candidate` dalla best lap e promozione a linea AI (P5) |
| AC-5 | Replay `.rep` v15+ | Formato proprio `.ksrep` | Compatibilità import/export `.rep` AC; qualità Hz configurabile + autosave (P7) |
| AC-6 | ACP server protocol (9600) | Protocollo proprietario `ksnet` | Non joinabile con client/server AC; item XL se si vuole sostituire `acServer.exe` |
| AC-7 | Race control UDP | Non analizzato/assente | Bind/porta e ping `0xF9/0xF8` da verificare in round dedicato |
| AC-8 | Shared memory size | Fatto | Mapping `acpmf_*` a 592 B fissi, verificato da test |

---

## 3. Gap rispetto a CSP (grafica, weather, scripting, audio)

| ID | Feature CSP | Stato ksengine | Gap |
|---|---|---|---|
| CSP-1 | Weather/night/rain visual completo | Pioggia, spray, wet physics presenti | Mancano seasons (texture swap), luci dinamiche con schedule, grass FX con vento/interazione, shader replacement system |
| CSP-2 | CSP Lua scripting | Mod SDK Lua generico | Non interpreta `script*.lua` AC/CSP né estende fisica/UI/luci come CSP |
| CSP-3 | Digital dashboards | Non presente | Mancano display CSP (`digital_instruments.ini`, HTML/JS) con LED, RPM, tyre slip, KERS/ERS |
| CSP-4 | Planar/virtual mirrors | Mirror tile recente | Verificare parità con CSP planar mirrors: aggiornamento ≥30 Hz, ghosting, occlusione |
| CSP-5 | Audio CSP | Audio 3D base + surface noise | Mancano convolution reverb (`CSP_Reverb`), geometry occlusion raycast, rain/wet layers automatici, replay/TV mix |
| CSP-6 | Track extensions CSP | Trackside posts presenti | Mancano blocchi CSP reali: `[RAIN_FX]`, `[GRASS_FX]`, `[DYNAMIC_LIGHTS]`, `[SEASONS]`, `[SHADER_REPLACEMENT]` |
| CSP-7 | Car extensions CSP | Converter gestisce molti ini | Verificare caso per caso thermal completo, aero maps, electronics, AWD2 su contenuto reale |
| CSP-8 | CSP config editor | `CspConfigParser`/editor presenti | Parziale: gestione config CSP esiste, ma non tutte le feature CSP sono runtime |

---

## 4. Gap prodotto ancora aperti (roadmap)

| ID | Feature | Stato | Priorità |
|---|---|---|---|
| PROD-1 | VR OpenXR | Non iniziato (3.5) | Bassa |
| PROD-2 | Triple monitor | Non iniziato (3.6) | Bassa |
| PROD-3 | Driver swap endurance | Non iniziato (3.4) | Media |
| PROD-4 | Championship mode end-to-end | Solo enum/UI | Media |
| PROD-5 | Ranked/skill rating | Non iniziato (3.7) | Bassa |
| PROD-6 | Laser-scan track pipeline | Non iniziato (3.8) | Bassa |
| PROD-7 | Gate umani | Aperti | Alta (collaudo) |

Gate umani da chiudere:
- Build/install pulito Qt-free
- Practice ~10 min audio + FFB reali
- Host + 1 client LAN → cinque giri
- Riavvio → binding e selezione persistenti
- Pista/auto AC reali (texture, LOD, materiali)
- Gate box: uscita → corsia → REQUEST SERVICE → HUD
- Verifica path menu coerenti

---

## 5. Priorità consigliata

### P0 — AC interop minima
1. Chiudere P4: penalties enum AC + PT/PTA + DQ + min-speed suppression.
2. Chiudere P5: AI trace saver `fast_lane.ai.candidate`.
3. Chiudere P7: replay qualità Hz configurabile + autosave.
4. Implementare semantica comandi UDP 9666 (almeno teleport pit, reset, recover).
5. Esponere `Local\acpmf` se servono tool AC.

### P1 — CSP parity visiva/audio
1. Seasons (texture swap).
2. Dynamic lights con schedule.
3. Grass FX con vento/interazione.
4. Convolution reverb + geometry occlusion audio.
5. Digital dashboards CSP.

### P2 — CSP scripting
1. Supporto `script*.lua` AC/CSP.
2. Supporto `digital_instruments.ini`.
3. Variabili setup custom esposte a UI.

### P3 — Pro features
1. VR OpenXR.
2. Triple monitor.
3. Driver swap.
4. Championship end-to-end.
5. Ranked/skill rating.
6. Laser-scan pipeline.

---

## 6. Note metodologiche

- AC+CSP è stato trattato come benchmark di funzionalità, non come clone brand.
- Le priorità derivano da `ROADMAP.md`, `GAP_MATRIX.md`, `PARITY_STATUS.md`, `WIREUP_GAPS.md`,
  `PHASE3_PLAN.md` e `REPORT_AC_RE_ENGINEERING.md`.
- Le feature marcate “presenti” vanno comunque collaudate su contenuto AC reale prima di
  dichiararle chiuse (policy del repo: nessun gate chiuso senza evidence).
- I dettagli CSP su car/track (thermal, aero maps, electronics, AWD2, dynamic lights,
  seasons, grass FX, shader replacement) richiedono verifica su contenuto reale, non solo
  parser presenti.
- Questo documento va aggiornato alla chiusura di ogni gap, con link all'evidence o al test.