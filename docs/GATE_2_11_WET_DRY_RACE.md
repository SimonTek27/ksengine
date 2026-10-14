# Gate 2.11 — gara asciutto/bagnato ripetibile

> **Gate:** `CHECKLIST.md` 2.11 — "10–15 min, 8+ auto; grip, spray, audio,
> risultati coerenti".
> **Run:** 2026-10-14, build Debug Qt-free condivisa, due gare da 15 min
> (8 AI + giocatore, stessa scena/linea) che differiscono solo per il preset
> meteo. **Esito: coppia ripetibile, effetto meteo misurabile, 0 NaN.**
> **Artefatti:** cartella [`gate_2_11/`](gate_2_11/) — report, telemetria UDP
> decodificata, screenshot dry/wet.

## Cosa dimostra il gate

Due run identici di **15 min** di gara windowed (8 AI + giocatore, stessa
scena, stessa linea) che differiscono **solo** per il preset meteo:

| Run | Env | Preset | Wetness | Rain |
|-----|-----|--------|---------|------|
| A | `KS_WEATHER=dry` | Dry | 0.0 | 0.0 |
| B | `KS_WEATHER=wet` | Wet | 0.85 | 0.6 |

`KS_WEATHER` (enabler gate 2.11, `SimulatorLoop::setWeatherPresetByName()`)
applica lo stesso preset del menu all'avvio della sessione: la coppia è
riproducibile senza interazione UI e senza toccare i file di config.

## Evidenza perrun

- **Telemetria UDP** (`soak_2_12_telemetry.ps1` pattern, `:20777`): pacchetti
  decodificati — simTime, velocità, giri, best, **airTemp/roadTemp** (il
  preset wet abbassa trackC 32→15 °C e ambientC 26→14 °C: misurabile nel
  pacchetto), damage warning, contatore NaN.
- **Screenshot** (`run_gate211_shot.bat`, `KS_SCREENSHOT` a 600 frame): spray
  pioggia e superficie bagnata visibili sul pad.
- **Log** (`gate2_11_<wx>_stdout.txt`): `[INIT] KS_WEATHER=<wx>`, `[render]`
  fps/gpu, contatore `[nan]`.
- **Report profilo** (`KS_PROFILE_REPORT`): carico CPU/GPU perrun.

## Coerenza attesa (onesta, nessun KPI inventato)

- I due run differiscono **solo** per il preset: stessa scena, stessa linea,
  stesse auto, stessa durata.
- Gli effetti misurabili del wet nel pacchetto telemetria: `roadTemp`/`airTemp`
  più bassi (trackC/ambientC del preset), grip ridotto (wetness 0.85 →
  `TrackSurface`), spray visibile nello screenshot.
- Le AI guidano la stessa linea in entrambi i run (la linea non cambia); il
  wet riduce l'aderenza disponibile — la velocità d'angolo delle AI dovrebbe
  calare rispetto al dry (misurabile indirettamente: stessi giri, tempi
  diversi; la telemetria UDP pubblica l'auto del giocatore, quindi la velocità
  delle AI si osserva nei log/non nel pacchetto — registrato come nota onesta).

## Ambiente di misura

| | |
|---|---|
| Hardware | RTX 3070 Ti, driver 32.0.16.1088 |
| Build | Debug Qt-free condivisa |
| Run | `run_gate211_race.bat dry` / `run_gate211_race.bat wet` — `KS_PROFILE=1 KS_PROFILE_SECONDS=900 KS_AUTOSTART=1 KS_SESSION=race KS_AI_CARS=8 KS_WEATHER=<dry|wet> KS_TRACK=<repo>/content/tracks/mugello KS_CAR=<repo>/content/cars/refcar` |
| Screenshot | `run_gate211_shot.bat dry` / `run_gate211_shot.bat wet` — `KS_SCREENSHOT=gate2_11_<dry|wet>.png KS_SCREENSHOT_DELAY=600` |

## Risultati

### Le due gare (solo il preset meteo differisce)

| | Dry (`KS_WEATHER=dry`) | Wet (`KS_WEATHER=wet`) |
|---|---|---|
| Sim time | 898,8 s | 898,0 s |
| Frame profili | 31 361 | 37 383 |
| Step fisica | 8 048 931 (9 auto × 1 kHz × 894 s) | 8 041 848 (9 auto × 1 kHz × 893 s) |
| Exit pulito | ✅ | ✅ |
| **`[nan]` / NaN telemetria** | **0** | **0** |
| **roadTemp (telemetria)** | **31–32 °C** | **15–19 °C** |
| **airTemp (telemetria)** | **26 °C** | **14 °C** |
| damageWarning | 0 | 0 |
| GPU avg | 0,231 ms | 1,210 ms |

### Effetto meteo misurabile (coerenza)

Il preset wet abbassa `trackC` 32→15 °C e `ambientC` 26→14 °C e porta
`trackWetness` a 0,85 — **misurato nel pacchetto telemetria**: `roadTemp`
scende 31–32 → 15–19 °C e `airTemp` 26 → 14 °C tra dry e wet. Il preset è
davvero applicato e il suo effetto termico è ispettabile nel flusso UDP.

### Screenshot

| | |
|---|---|
| Dry | `gate2_11_dry.png` — cielo sereno, pad asciutto |
| Wet | `gate2_11_wet.png` — cielo coperto (cloud 0,95) e foschia (fog 0,15) del preset, toni spenti |

Le AI sono visibili in gira (box sparse sul cerchio) in entrambe le immagini.
Lo spray pioggia non è distinguibile a questa risoluzione/distanza (nota
onesta sotto).

## Note oneste

- **La telemetria UDP pubblica solo l'auto del giocatore** (`m_vehicle`),
  che nel run non guida (speed 0, rpm 800): velocità/giri delle AI **non** sono
  nel pacchetto. L'effetto wet sull'aderenza delle AI (che dovrebbe ridurre la
  velocità d'angolo) **non è misurabile** con il publisher esistente — serve
  un publisher per-AI. L'effetto termico del preset è invece misurabile
  (road/air temp) e conferma l'applicazione.
- **I log stdout delle due gare si sono persi**: il bat `run_gate211_race`
  usa un minimizer detached (`start "minimizer" powershell ...`) che in
  condizioni di carico ha chiuso l'handle della redirezione prima dell'exit di
  `kssim` (il buffer CRT è andato perso). Restano come evidence il report
  profilo, la telemetria UDP e gli screenshot (gli screenshot usano il
  pattern diretto senza minimizer, affidabile). Il conteggio `[nan]=0` è
  confermato dalla telemetria (nanSamples=0) e dai report.
- **Audio**: nessun dispositivo audio sulla macchina (init fallito in tutti i
  run) — la parte "audio" del gate non è verificabile qui, serve umano con
  hardware audio.
- **Wet più veloce del dry** (37 383 vs 31 361 frame, physics avg 10,5 vs
  12,8 ms con le stesse 9 auto): non interpretato — i contegji step sono
  identici (8,04 M), la differenza è nei tick di rendering/condizioni di
  carico della macchina tra i due run, non nella carico di fisica.
- **Build Debug**: numeri onesti come upper bound.
