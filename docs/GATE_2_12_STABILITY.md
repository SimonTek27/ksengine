# Gate 2.12 — stabilità lunga (1 h di gara)

> **Gate:** `CHECKLIST.md` 2.12 — "run 1 ora senza NaN/recovery; log e
> telemetria ispezionabili".
> **Run:** 2026-10-14, build Debug Qt-free condivisa (VS 18 2026), 1 h di
> gara windowed con renderer Vulkan reale. **Esito: 0 recovery NaN, log e
> telemetria ispezionabili — GATE VERDE.**
> **Artefatti:** cartella [`gate_2_12/`](gate_2_12/) — report profilo, log
> completo, telemetria UDP decodificata.

## Cosa dimostra il gate

Un run windowed di **1 ora** di gara reale (renderer Vulkan attivo, 8 AI +
giocatore, meteo, danni, telemetria) che termina con:

1. **zero recovery NaN** — il percorso di recovery di
   `VehicleSimulator::updatePhysics()` conta ogni evento (`nanRecoveries()`)
   e scrive una riga `[nan] VehicleSimulator recovery #N: ...` su stderr; il
   log del soak si greppa per `[nan]` e si conta **0**;
2. **log ispettabile** — `soak_2_12_stdout.txt` (stdout+stderr del build
   WIN32 via redirezione diretta del bat, pattern gate 2.14) con le righe
   `[render]` (fps + gpu), `[INIT]`, `[team]`, `[nan]`;
3. **telemetria ispettabile** — sink UDP su `127.0.0.1:20777`
   (`UdpTelemPacket` v2, magic `"KSIM"`, 244 byte/pacchetto) che registra
   pacchetti decodificati: simTime, velocità, giri, best, road/air temp,
   damage warning, contatore NaN sui campioni.

## Enabler aggiunti per questo gate

- **Contatore/log NaN** (`src/engine/physics/VehicleSimulator.{h,cpp}`): la
  recovery non è più silenziosa. Prima un run lungo poteva "recuperare" senza
  traccia; ora ogni evento è numerato e loggato.
- **Linea AI sintetica** (`content/tracks/mugello/ai/fast_lane.ai`): il repo
  non ha geometria di tracciato reale, quindi senza `ai/fast_lane.ai` le auto
  non spawnano affatto (`spawnGrid()` è un no-op e l'AI frena al 100%). La
  linea è un cerchio R=150 m, 125 punti, formato testo
  `x y z curvature speed` (`AiFileReader::readTextSpline`), generata e
  validata in un run corto (8 AI spawnate, in gira, 0 NaN in 90 s).

## Ambiente di misura

| | |
|---|---|
| Hardware | RTX 3070 Ti, driver 32.0.16.1088 (stessa macchina del gate 2.14) |
| Build | Debug Qt-free condivisa (VS 18 2026) |
| Run | `run_soak_2_12.bat` — `KS_PROFILE=1 KS_PROFILE_SECONDS=3600 KS_AUTOSTART=1 KS_SESSION=race KS_AI_CARS=8 KS_TRACK=<repo>/content/tracks/mugello KS_CAR=<repo>/content/cars/refcar` |
| Telemetria | sink UDP `:20777` per tutta la durata (pacchetti decodificati, non raw) |
| Scena | refcar + mugello + pad `content/baked` (come gate 2.14) + linea AI sintetica |

## Risultati

### Conteggi autoritativi (dal report profilo e dal log)

| Voce | Valore |
|------|-------|
| Durata wall | 3600 s (`KS_PROFILE_SECONDS=3600`), exit pulito (`SOAK_EXIT=0`) |
| Frame profili | **121 461** (tick work medio 16.638 ms, ultimo 16.450) |
| Step fisica `updatePhysics` | **32 200 940** = 9 auto × 1 kHz × 3578 s ✓ |
| Sim time (telemetria) | 0 → 3582,3 s, **lineare** (173→362→…→3582, nessun salto) ✓ |
| Real-time ratio | 3582 / 3600 = **99,5 %** (dt clamp 50 ms tiene il passo anche a 34 fps) |
| **`[nan]` nel log** | **0** (su 7 226 righe di log) ✓ |
| Telemetria UDP | **121 461 pacchetti** (1/frame), **0 NaN**, 0 badMagic ✓ |

### CPU (per tick, 121 461 frame)

| Sezione | Media | Totale |
|---------|------:|-------:|
| `physics` (input + step 9 auto + AI + rete) | **14 208 µs** | 1 726 s |
| `render` (camera/submit/UI) | **3 645 µs** | 443 s |
| `VehicleSimulator.updatePhysics` (per step) | **36,4 µs** | 1 171 s |
| Tires / Damage / Aero (per step) | 5,3 / 5,1 / 3,1 µs | — |

### GPU (timestamp di coda reali)

| Voce | Valore |
|------|-------|
| `__gpu_frame__` avg | **1,238 ms** (fps~1352) |
| gpu avg-of-avg (dalle righe `[render]`) | 0,769 ms |

### FPS a parete (3 561 righe `[render]`, 1 ogni ~10 s)

| | |
|---|---|
| min / max / avg | 8,4 / 152,2 / **33,7 fps** |
| nota | min 8,4 = stallo occasionale (present minimizzato + lavoro 9 auto); max 152 = burst iniziale. La finestra era minimizzata per tutto il run. |

### Telemetria (sink `:20777`, pacchetti `KSIM` v2 decodificati)

| Voce | Valore |
|------|-------|
| Pacchetti | 121 461 (bytes 29 636 484, short 0, badMagic 0) |
| Auto pubblicata | giocatore (`m_vehicle`): speed 0, rpm 800 (idle, nessun input nel soak) |
| roadTemp / airTemp | 30 / 26 °C (dry preset) — costanti |
| damageWarning | 0 |
| NaN sui campioni | 0 |

## Note oneste

- **Il giocatore non guida nel soak** (nessun input): la telemetria UDP
  pubblica solo `m_vehicle` (auto del giocatore), quindi speed/rpm/giri nel
  pacchetto restano a idle. Le 8 AI sono nei numeri di fisica (32,2 M step) e
  nel conteggio entità (`submitted=20`), non nel pacchetto UDP. Per la
  telemetria delle AI servirebbe un publisher dedicato (non esiste).
- **Residuo di sezione negativo**: `physics` (14,2) + `render` (3,6) = 17,8 ms
  > tick work medio 16,6 ms — le sezioni del profiler si sovrappongono di
  ~1,2 ms (caratteristica del profiler, non un errore di misura; i conteggi
  frame/step restano autoritativi). Il gate 2.14 aveva residuo positivo
  (0,39 ms) con 1 auto.
- **Audio/joystick**: fallimenti di init ambientali (nessun dispositivo),
  nessun impatto sui numeri (come gate 2.14).
- **Build Debug**: numeri onesti come upper bound.
- **La finestra era minimizzata** per tutto il run: i fps a parete (33,7)
  riflettono il throttling della present, non il budget di lavoro (16,6 ms).
- **Prima tentativo annullato**: il primo soak (senza linea AI) aveva
  `spawned 0 AI cars` — 9 auto ferme, troppo debole per il gate. Annullato e
  rifatto con la linea sintetica.
