# Checklist operativa — ksengine / ksim

**Fonte:** `docs/ROADMAP.md`, `docs/PARITY_STATUS.md`, `docs/WIREUP_GAPS.md`, codice su `master`  
**Aggiornata:** 2026-10-10  
**HEAD di riferimento:** `ffb77d32`  
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

### Priorità alta — 2.7 Servizio box end-to-end 🟡

Gap principale documentato in `docs/WIREUP_GAPS.md`.

- [ ] Stato piano sul loop: `PitStrategyState m_pitStrategy` + accessor
- [ ] `void requestPitService(bool on = true)` pubblico sul loop
- [ ] `updatePitRepair`: usare `applyStrategyToRepairInput(m_pitStrategy, in)` al posto dei valori hardcoded (`targetFuelL = 100`, tutti i want* = true)
- [ ] `SimulatorApp.cpp`: cablare `wirePitStrategyMenu` / `onPitStrategyConfirmRequested`
- [ ] Azione giocatore **REQUEST SERVICE** (distinta da CONFIRM PLAN) → `requestPitService(true)`
- [ ] Verificare HUD fuel/danni dopo `out.completedThisFrame`
- [ ] Allineare `SimulationLoop.h` ai membri pit usati in `SimulationLoop_FeatureMethods.cpp` (se la build locale lo richiede)

**Criterio di accettazione 2.7**

1. PIT STRATEGY → fuel 40 L, solo tyres+body → CONFIRM PLAN  
2. In box, fermo → REQUEST SERVICE  
3. Job solo con i flag confermati; tempi realistici  
4. Fine servizio → HUD aggiornato; flag richiesta a false  

### Priorità media — 2.10 Menu racing coerente 🟡

- [ ] Una sola fonte di verità per lo stato sessione (FeatureHub / RaceSessionManager)
- [ ] Garage, pit, risultati e multiplayer senza callback/overlay duplicati
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
- [ ] **2.12** Stabilità lunga — run 1 ora senza NaN/recovery; log e telemetria ispezionabili ⬜
- [ ] **2.13** Regressione automatica — CTest Qt-free verdi; consolidare test bridge team/upgrades/materiali se servono casi extra 🟡
- [ ] **2.14** Profilo frame reale — CPU fisica + GPU su hardware target con scena di riferimento; nessun KPI inventato ⬜

---

## D. Ordine di esecuzione consigliato

| # | Blocco | Dipende da | Stato |
|---|--------|------------|--------|
| 1 | Gate umani Fase 1 (A) | Build Windows | [ ] |
| 2 | Gate umani 2A / Block C (A) | Contenuto AC | [ ] |
| 3 | **2.7** Servizio box end-to-end | — | [ ] |
| 4 | **2.10** Menu coerente | 2.7 | [ ] |
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

---

## Documenti correlati

| Documento | Ruolo |
|-----------|--------|
| [ROADMAP.md](https://github.com/SimonTek27/ksengine/blob/master/docs/ROADMAP.md) | Priorità prodotto |
| [WIREUP_GAPS.md](https://github.com/SimonTek27/ksengine/blob/master/docs/WIREUP_GAPS.md) | Dettaglio tecnico gap 2.7 |
| [PARITY_STATUS.md](https://github.com/SimonTek27/ksengine/blob/master/docs/PARITY_STATUS.md) | Feature cablate |
| [GAP_MATRIX.md](https://github.com/SimonTek27/ksengine/blob/master/docs/GAP_MATRIX.md) | Inventario storico P0–P3 |

---

*Prossimo passo più utile: (1) gate umani su Windows, (2) patch 2.7 box end-to-end.  
Aggiornare stato e data a ogni gate superato; non cambiare stato senza test o misura.*
