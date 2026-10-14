# Checklist operativa — ksengine / ksim

**Fonte:** `docs/ROADMAP.md`, `docs/PARITY_STATUS.md`, `docs/WIREUP_GAPS.md`, codice su `master`  
**Aggiornata:** 2026-10-11  
**HEAD di riferimento:** `f78e795` + lavoro locale 2.7 (vedi sezione B)  
**Ambito:** runtime Qt-free (SimulatorApp / ksim). Non include il piano completo di ksEditor.

---

## Come usare questa checklist

| Simbolo | Significato |
|---------|-------------|
| `[ ]` | Da fare |
| `[x]` | Fatto / verificato |
| 🟡 | Parziale (codice presente; manca wire-up completo o collaudo) |
| ✅ | Consegnato e coperto da test o verifica riproducibile |
| ⬜ | Pianificato, non ancora iniziato |

Una voce non va marcata completa solo perché compila: serve il gate di accettazione indicato.

---

## A. Gate di accettazione umani (obbligatori)

Eseguire su **build pulito Windows**. Non chiudibili da commit di documentazione.

### Fase 1 — rilascio

- [ ] Build/install pulito: `cmake -B build -DKSENGINE_QT_FREE=ON` + `cmake --build build -j` senza errori
- [ ] Practice ~10 minuti con **audio reale** e **FFB reale** (volante collegato)
- [ ] Host + 1 client LAN → cinque giri, risultati visibili, nessun desync grossolano
- [ ] Riavvio del gioco → binding volante e selezione auto/pista ancora validi

### Fase 2A / Block C — contenuto reale

- [ ] Sessione windowed con **pista e auto AC reali** (cache):
  - [ ] Texture, LOD e materiali visibili
  - [ ] Nessun fallback bianco/inatteso
  - [ ] (Opzionale) `KS_SCREENSHOT=<path>`
- [ ] Sessione Block C: `KS_AUTOSTART=1` + `KS_SESSION=race` + `KS_TEAM` + `KS_AI_CARS` + `KS_UPGRADES` su contenuto reale
  - [ ] Roster, numero, livrea applicati
  - [ ] Fisica upgrade e audio applicati

> Non marcarli completi senza report della macchina che li ha eseguiti.

---

## B. Sviluppo ancora aperto (codice)

### Priorità alta — 2.7 Servizio box end-to-end 🟡 (cablato 2026-10-11)

Gap tecnico chiuso: il servizio esiste nel runtime compilato
(`src/simulator/SimulationLoop_Pit.cpp`, chiamata da `tick()`), il piano del
menu lo guida, la richiesta è una riga separata e il rifornimento arriva al
veicolo. Dettaglio in `docs/WIREUP_GAPS.md` (sezione 2.7 aggiornata).

- [x] Stato piano sul loop: `PitStrategyState m_pitStrategy` + accessor
- [x] `void requestPitService(bool on = true)` pubblico sul loop
- [x] `updatePitRepair`: usa `applyStrategyToRepairInput(m_pitStrategy, in)`
- [x] `SimulatorApp.cpp`: cablato `wirePitStrategyMenu` (+ hook REQUEST SERVICE)
- [x] Azione giocatore **REQUEST SERVICE** distinta da CONFIRM PLAN
- [x] Fuel aggiornato sul veicolo durante il rifornimento (`setFuelL`)
- [x] Gomme nuove: `out.tyresChanged` → `VehicleSimulator::resetTyreWear()`
      (una volta per stop; temperature/pressioni le riporta su il modello)
- [x] Serbatoio reale: `fuelCapacityL` = `fuelTankCapacityL()` (default 150 L,
      override `FUEL_CAPACITY` in `data/engine.ini`) e `PitLaneRepair::plan()`
      plafona il target — "riempi" non supera più il serbatoio
- [x] Coda box multi-auto: `PitLaneQueue` → `pathBlocked` in `BoxClear`
      (release gap + spaziamento; `KS_PIT_QUEUE=0` per disattivare; release
      gap corretto — era calcolato al contrario)
- [x] Uscita/rientro box: `updateGarageExit()` guidato dal loop, tasto `P` +
      righe **LEAVE GARAGE** / **RETURN TO BOX**, pit limiter in corsia
- [x] `SimulationLoop_FeatureMethods.cpp`: rimosse le duplicazioni non compilate
- [x] Test: `pit_strategy_menu_test` copre riga, hook, job confermati, servizio completo, righe garage
- [ ] Gate umano: tempi realistici, HUD, esperienza di box (uscita → corsia → rientro → servizio)

Restano fuori da questa patch (vedi WIREUP_GAPS): ingresso in corsia per le
AI che rientrano (`requestEnter`), finestra box in gara, parser AC
`powertrain.ini` per la capacità del serbatoio (oggi `FUEL_CAPACITY` in
`engine.ini`).

**Criterio di accettazione 2.7**

1. PIT STRATEGY → fuel 40 L, solo tyres+body → CONFIRM PLAN  
2. In box, fermo → REQUEST SERVICE  
3. Job solo con i flag confermati; tempi realistici  
4. Fine servizio → HUD aggiornato; flag richiesta a false  

### Priorità media — 2.10 Menu racing coerente 🟡

- [x] Una sola fonte di verità per lo stato sessione (FeatureHub / RaceSessionManager)
      — HUD e dashboard leggono posizione/numero auto da
      `RaceSessionManager::playerStanding()` / `standings().size()` (prima 1/1
      hardcodato); mappa di proprietà in `docs/ARCHITECTURE.md` § 5.3.1
- [x] Garage, pit, risultati e multiplayer senza callback/overlay duplicati
      — rimosso `ui/PitLaneOverlay.h` (secondo overlay pit, non usato, tempi
      propri); risultati avevano già un solo path; aggiunta la riga di stato
      pit/garage nel race HUD (`RaceHudSample::statusText`)
- [ ] Verifica manuale dei path menu dopo il fix 2.7

### Già consegnati (non rifare) ✅

| ID | Deliverable | Note |
|----|-------------|------|
| [x] | 2.6 Piano pit in UI | Fuel/lavori modificabili; non auto-richiede servizio |
| [x] | 2.8 Team, numero e box | Wire-up + `menu_session_gate_test`; resta solo gate umano AC |
| [x] | 2.9 Upgrade, livrea, sound | Wire-up + test; resta solo gate umano AC |
| [x] | 2.1–2.5 Rendering 2A | LOD, DDS, materiali, ref content, GPU test |
| [x] | Rendering S1–S6 | Mip/aniso, PBR maps, IBL, clear-coat, surface, brake glow |

---

## C. Fase 2C — Accettazione e qualità

- [ ] **2.11** Gara asciutto/bagnato ripetibile — 10–15 min, 8+ auto; grip, spray, audio, risultati coerenti ⬜
- [x] **2.12** Stabilità lunga — run 1 ora senza NaN/recovery; log e telemetria ispezionabili ✅ 2026-10-14: soak 1 h windowed (9 auto, renderer reale) con **0 recovery NaN** (contatore/log `[nan]`), 121 461 frame, 32,2 M step, telemetria UDP 121 461 pacchetti 0 NaN; evidence in `docs/gate_2_12/`, sintesi in `docs/GATE_2_12_STABILITY.md`
- [x] **2.13** Regressione automatica — CTest Qt-free verdi; consolidare test bridge team/upgrades/materiali se servono casi extra ✅ 2026-10-14: suite Qt-free 60/60 verdi; aggiunto `team_upgrade_bridge_test` (parser `team.ini` + `buildFieldFromTeam` con bench/padding AI, catalogo upgrade + `appliedMods` + `applyUpgradesToVehicleSimulator` su VehicleSimulator reale, instance/material swap); materiali già coperti da `material_cache_test` + `content_library_test`
- [x] **2.14** Profilo frame reale — CPU fisica + GPU su hardware target con scena di riferimento; nessun KPI inventato ✅ 2026-10-14: run 45 s con renderer Vulkan reale (RTX 3070 Ti, Debug Qt-free), scena refcar + mugello + 8 AI; report/CSV/log/screenshot in `docs/gate_2_14/`, sintesi in `docs/GATE_2_14_FRAME_PROFILE.md` (il run ha trovato e fatto fixare 3 difetti di wiring del profiler E9)

---

## D. Ordine di esecuzione consigliato

| # | Blocco | Dipende da | Stato |
|---|--------|------------|--------|
| 1 | Gate umani Fase 1 (A) | Build Windows | [ ] |
| 2 | Gate umani 2A / Block C (A) | Contenuto AC | [ ] |
| 3 | **2.7** Servizio box end-to-end | — | [x] cablato (gate umano) |
| 4 | **2.10** Menu coerente | 2.7 | [x] codice (gate umano) |
| 5 | **2.11–2.14** Soak / gara / profilo / test | 2.7 + gate | [ ] |
| — | Team 2.8 + Upgrade 2.9 (wire-up) | — | [x] |
| — | Rendering 2A + S1–S6 | — | [x] |

---

## E. Cose da non fare ora

- [ ] **Non** riscrivere fisica, sessione o `ksnet`
- [ ] **Non** aggiungere moto / barco / aereo
- [ ] **Non** fare ranked globale prima del loop offline/LAN
- [ ] **Non** usare l’editor Qt per chiudere gap del runtime Qt-free
- [ ] **Non** dichiarare chiusi i gate umani senza report di macchina

> IBL e clear-coat sono stati consegnati dagli sprint S3/S4: non sono più “solo infrastruttura”.

---

## F. Fase 3 — Solo dopo Fase 2

| # | Deliverable | Priorità | Stato |
|---|-------------|----------|--------|
| [ ] | **3.1** Validazione pneumatici vs telemetria reale | Alta | ⬜ |
| [ ] | **3.2** Secure-connect ksnet, reconnect, server dedicato | Media | ⬜ |
| [ ] | **3.3** Multi-layout senza reload completo | Media | ⬜ |
| [ ] | **3.4** Driver swap endurance | Media | ⬜ |
| [ ] | **3.5** VR OpenXR | Bassa | ⬜ |
| [ ] | **3.6** Triple monitor | Bassa | ⬜ |
| [ ] | **3.7** Ranked / skill rating | Bassa | ⬜ |
| [ ] | **3.8** Laser-scan track pipeline | Bassa | ⬜ |

### Backlog visivo non bloccante

- [ ] Marcature pneumatici
- [ ] Heat haze
- [ ] Scarichi

---

## G. Manutenzione documentazione (opzionale ma utile)

- [ ] Aggiornare `docs/WIREUP_GAPS.md` (2.8/2.9 non sono più “quasi chiusi”)
- [ ] Allineare `CHECKLIST_STATUS_SUMMARY.md` in root allo stato del 10/10
- [ ] Tenere questa checklist in sync con `docs/ROADMAP.md` a ogni gate chiuso

---

## H. Build e variabili utili

```bash
cmake -B build -DKSENGINE_QT_FREE=ON
cmake --build build -j

# oppure
cmake --preset default
cmake --build --preset default
```

| Variabile | Uso |
|-----------|-----|
| `KS_AUTOSTART=1` | Avvio sessione senza menu |
| `KS_CAR=<dir>` | Auto (es. `content/cars/refcar`) |
| `KS_TRACK=<dir>` | Pista |
| `KS_TEAM=<dir>` | Roster `team.ini` |
| `KS_UPGRADES=...` | Selezione upgrade da CLI |
| `KS_SESSION=race` | Tipo sessione |
| `KS_AI_CARS=N` | Numero AI |
| `KS_SCREENSHOT=<png>` | Cattura e uscita |
| `KS_PARTICLES=1` | Rain/spray |
| `KS_IBL=0` / `KS_SSAO=0` | Opt-out rendering |

**Requisiti:** Windows 10/11 x64 · CMake 3.16+ · Vulkan SDK  
Qt solo per ksEditor.

> **Nota build:** `SimulatorApp` e `SimulatorServer` fanno entrambi lo staging
> di `system/shaders` nella stessa cartella di output; con `--build -j` ad alta
> concorrenza possono scontrarsi (`MSB3073` su `cmake -E copy_directory`).
> Non è un errore di compilazione: rilanciare il target singolo con `-j 1`
> (o buildare i due target in sequenza) risolve. Da sistemare in
> `cmake/KsInstallLayout.cmake`.

---

## Documenti correlati

| Documento | Ruolo |
|-----------|--------|
| [ROADMAP.md](https://github.com/SimonTek27/ksengine/blob/master/docs/ROADMAP.md) | Priorità prodotto |
| [WIREUP_GAPS.md](https://github.com/SimonTek27/ksengine/blob/master/docs/WIREUP_GAPS.md) | Dettaglio tecnico gap 2.7 |
| [PARITY_STATUS.md](https://github.com/SimonTek27/ksengine/blob/master/docs/PARITY_STATUS.md) | Feature cablate |
| [GAP_MATRIX.md](https://github.com/SimonTek27/ksengine/blob/master/docs/GAP_MATRIX.md) | Inventario storico P0–P3 |

---

*Prossimo passo più utile: (1) gate umani su Windows — inclusa la nuova
sequenza box (P per uscire, rientro lento nel box, REQUEST SERVICE) e i path
menu del fix 2.7/2.10, (2) coda box multi-auto `PitLaneQueue`.  
Aggiornare stato e data a ogni gate superato; non cambiare stato senza test o misura.*
