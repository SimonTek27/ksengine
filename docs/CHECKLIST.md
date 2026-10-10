# Checklist operativa — ksengine / ksim

**Fonte:** `docs/ROADMAP.md`, `docs/PARITY_STATUS.md`, `docs/GAP_MATRIX.md`  
**Aggiornata:** 2026-10-09  
**Ambito:** runtime Qt-free (SimulatorApp / ksim). Non include il piano completo di ksEditor.

---

## Come usare questa checklist

| Simbolo | Significato |
|---------|-------------|
| `[ ]` | Da fare |
| `[x]` | Fatto / verificato |
| 🟡 | Parziale (modello o infrastruttura presente; manca wire-up o collaudo) |
| ✅ | Consegnato e coperto da test o verifica riproducibile |
| ⬜ | Pianificato, non ancora iniziato |

Una voce non va marcata completa solo perché compila: serve il gate di accettazione indicato.

---

## A. Gate di accettazione (obbligatori)

Eseguire su **build pulito** prima di dichiarare chiusa la Fase 1 / 2A.

- [ ] Build/install pulito: `cmake -B build -DKSENGINE_QT_FREE=ON` + `cmake --build build -j` senza errori
- [ ] Practice ~10 minuti con audio reale e FFB reale (volante collegato)
- [ ] Host + 1 client LAN → cinque giri, risultati visibili, nessun desync grossolano
- [ ] Riavvio del gioco → binding volante e selezione auto/pista ancora validi
- [ ] Sessione windowed con **pista e auto reali** (cache AC):
  - [ ] Texture, LOD e materiali visibili
  - [ ] Nessun fallback bianco/inatteso
  - [ ] (Opzionale) Screenshot con `KS_SCREENSHOT=<path>` (+ `KS_SCREENSHOT_DELAY=N` se serve)
- [ ] Sessione windowed Block C (2.8/2.9) con contenuto reale:
      `KS_AUTOSTART=1 KS_SESSION=race KS_TEAM=... KS_AI_CARS=3 KS_UPGRADES=...`
      → roster, numero, livrea, fisica upgrade e audio applicati

> I gate sono test di accettazione umani: non marcarli completi senza report della macchina che li ha eseguiti.

---

## B. Fase 2B — Gara, box e identità vettura

Priorità di sviluppo corrente. I modelli dati esistono già (`TeamInfo`, `GarageSpawn`, `VehicleAppearanceBundle`, `ApplyVehicleUpgrades`); il wire-up nel selettore e in `SimulationLoop` è consegnato (Block C, `menu_session_gate_test`). Resta il collaudo con contenuto AC reale (sezione A).

| # | Task | Stato | Criterio di accettazione |
|---|------|--------|---------------------------|
| [ ] | **2.7 Servizio box end-to-end** | 🟡 | Conferma piano → richiesta esplicita in box → tempi di servizio → stato, danni e fuel aggiornati nel HUD |
| [x] | **2.8 Team, numero e box assegnati** | ✅ | `team.ini` determina roster, livrea/numero e garage/grid del player e degli AI all’avvio sessione (`TeamSessionBridge` → `MultiCarManager`/`RaceSessionManager`, `KS_TEAM`/`KS_SESSION`; test: `menu_session_gate_test`) |
| [x] | **2.9 Upgrade, livrea e sound pack** | ✅ | La selezione applica `VehicleUpgradeSystem` a fisica, nodi render, texture/livrea e banca audio; fallback = contenuto base (`cycleUpgradeRow` → `ApplyVehicleUpgrades` + `SoundPack`, `KS_UPGRADES`; test: `menu_session_gate_test`) |
| [ ] | **2.10 Menu racing coerente** | 🟡 | Garage, pit, risultati e multiplayer espongono lo stesso stato di sessione, senza callback o overlay duplicati |

**Nota:** il piano pit in UI (2.6) è già ✅; non richiede automaticamente il servizio.

---

## C. Fase 2C — Accettazione e qualità

| # | Task | Stato | Criterio di accettazione |
|---|------|--------|---------------------------|
| [ ] | **2.11 Gara asciutto/bagnato ripetibile** | ⬜ | Gara 10–15 min, 8+ auto; grip, spray, audio e risultati coerenti |
| [ ] | **2.12 Stabilità lunga** | ⬜ | Run di un’ora senza NaN o recovery; log e telemetria ispezionabili |
| [ ] | **2.13 Regressione automatica** | 🟡 | CTest Qt-free (52/52) e standalone verdi; test per manifest materiali (`material_cache_test`), fallback texture (`test_renderer`) e bridge team/upgrades (`menu_session_gate_test`) presenti. Resta il collaudo umano di Fase 2 |
| [ ] | **2.14 Profilo di frame reale** | ⬜ | Misurare CPU fisica e GPU su hardware target con la scena di riferimento; nessun KPI GPU dichiarato prima della misura |

---

## D. Ordine di esecuzione consigliato

| Blocco | Dipende da | Risultato osservabile | Stato |
|--------|------------|------------------------|--------|
| A. Texture + materiali (2.2–2.3) | Cache/baker già presenti | Auto e pista non più mesh monocromatiche | [x] |
| B. Contenuto di riferimento (2.4–2.5) | A | Scena windowed verificabile con LOD, fallback e screenshot | [x] |
| C. Team + upgrade (2.8–2.9) | B | La scelta del player cambia spawn, numero, fisica, look e audio | [x] (wire-up + test; gate umano sez. A) |
| D. Servizio box e UX (2.7, 2.10) | C | Una gara completa passa dal garage al pit e ai risultati | [ ] |
| E. Soak / performance (2.11–2.14) | A–D | Gate di rilascio basati su misure, non su stime | [ ] |

> C e D non dovrebbero iniziare prima che B renda visibile la vettura selezionata.  
> Il gate umano con contenuto AC reale (sezione A) resta obbligatorio anche se il contenuto di riferimento sintetico è già ✅.

---

## E. Cose da non fare ora

Decisioni esplicite dalla roadmap:

- [ ] **Non** riscrivere fisica, sessione o `ksnet` (fondazioni già funzionali)
- [ ] **Non** aggiungere domini moto / barca / aereo
- [ ] **Non** fare ranked globale prima del loop offline/LAN
- [x] **Non** promuovere il clear-coat come “finito” senza deliverable — ora consegnato dallo sprint S4/P3 (A/B su contenuto di riferimento, secondo lobo in deferred e forward); l'IBL è consegnato dallo sprint S3
- [ ] **Non** riattivare l’editor Qt per compensare un gap del runtime Qt-free

---

## F. Fase 3 — Profondità pro (solo dopo Fase 2)

| # | Deliverable | Priorità | Note |
|---|-------------|----------|------|
| [ ] | **3.1** Validazione pneumatici contro telemetria reale | Alta | Campagna dati, tuning coefficienti; modello Milliken come riferimento |
| [ ] | **3.2** Secure-connect ksnet, reconnect e server dedicato | Media | Hardening produzione dopo collaudo LAN |
| [ ] | **3.3** Multi-layout senza reload completo | Media | Solo se i contenuti scelti lo richiedono |
| [ ] | **3.4** Driver swap endurance | Media | Dopo che pit strategy e sessione lunga sono validate |
| [ ] | **3.5** VR OpenXR | Bassa | Solo con camera/render loop stabile |
| [ ] | **3.6** Triple monitor | Bassa | Manager config esiste; manca multi-view nel runtime |
| [ ] | **3.7** Ranked / skill rating | Bassa | Infrastruttura server + anti-cheat |
| [ ] | **3.8** Laser-scan track pipeline | Bassa | Tooling offline, separato dal runtime |

### Backlog visivo non bloccante

Terrain, acqua, vegetazione instanced, skinning (IBL, clear-coat e bagliore freni sono invece consegnati dai brief S3/S4/S6): avviare solo con brief che definisca contenuto sorgente, budget GPU, fallback e test immagine.

---

## G. Metriche e gate di rilascio

| Milestone | Gate |
|-----------|------|
| Fase 1 | Practice giocabile; FFB/audio, bind persistenti e LAN 2-client verificati su build pulito |
| Fase 2 | Gara 10–15 min in asciutto e bagnato, 8+ auto, una pista/auto textured con LOD; nessun fallback inatteso |
| Stabilità | Zero NaN in soak di un’ora; nessun crash o desync grave nel test LAN previsto |
| Prestazioni | Fisica entro il budget già profilato; budget GPU dichiarato solo dopo la misura sulla scena di riferimento |
| Qualità | Suite automatizzate verdi e ogni nuovo bridge end-to-end coperto da un test mirato |

---

## H. Build di riferimento

```bash
# Qt-free (runtime)
cmake -B build -DKSENGINE_QT_FREE=ON
cmake --build build -j

# Oppure preset
cmake --preset default
cmake --build --preset default
```

**Requisiti:** Windows 10/11 x64 · CMake 3.16+ · Vulkan SDK  
Qt 6.11+ solo se si compila ksEditor.

Variabili utili per test:

- `KS_AUTOSTART=1`
- `KS_CAR=content/cars/refcar` (o path auto reale)
- `KS_SCREENSHOT=<path.png>`
- `KS_SCREENSHOT_DELAY=N`
- `KS_PARTICLES=1` (rain/spray)

---

## Documenti correlati

| Documento | Ruolo |
|-----------|--------|
| [ROADMAP.md](ROADMAP.md) | Piano prodotto e priorità correnti |
| [PARITY_STATUS.md](PARITY_STATUS.md) | Stato puntuale delle feature cablate |
| [GAP_MATRIX.md](GAP_MATRIX.md) | Inventario storico gap P0–P3 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Layer, FeatureHub, data flow, threading |
| [CONTROL_API.md](CONTROL_API.md) | API di controllo esterna |

---

*Aggiornare lo stato delle caselle e la data in testa a ogni cambio di fase o gate superato. Non cambiare stato senza test o misura che lo giustifichi.*
