# Roadmap ksim / ksengine — motore auto

**Aggiornata:** 2026-10-08
**Ambito:** simulatore **auto**: dinamica a quattro ruote, sessioni racing,
contenuti pista/auto e multiplayer `ksnet`.
**Fuori ambito:** moto, barche e aerei; richiedono modelli fisici e flussi di
prodotto distinti.

Questa è la roadmap di prodotto del runtime Qt-free (`ksim`). Non è una lista
di tutte le possibilità di `ksEditor`, né un piano per rendere ksengine un
motore general-purpose.

## Come leggere lo stato

| Stato | Significato |
|---|---|
| ✅ Consegnato | Codice cablato nel runtime e coperto da test mirati o da una verifica riproducibile. |
| 🟡 Parziale | Esiste il modello, il parser o l'infrastruttura; manca il collegamento end-to-end oppure il collaudo di prodotto. |
| ⬜ Pianificato | Non è ancora iniziato nel runtime. |

Una funzionalità non diventa “prodotto pronto” solo perché compila: le uscite
di fase richiedono anche il test manuale indicato. Questa distinzione evita di
confondere le basi tecniche già presenti con una vertical slice verificata.

---

## Stato consolidato

| Area | Stato | Evidenza / confine |
|---|---|---|
| Fisica, setup, FFB, superfici, AI e sessione | ✅ | P0/P1 chiusi; teoria e tool Milliken G1–G18 sono documentati e cablati. |
| Multiplayer LAN e lobby | ✅ | `ksnet`, sync 20 Hz, danni/setup, auth applicativa, discovery e lobby opzionale. Il secure-connect a chiave privata resta opzionale. |
| Vertical slice menu, binding e telemetria | ✅ | Selezione auto/pista, practice/quick race/time attack, risultati, rebind persistente, HUD danni e Control API. |
| Pioggia, aquaplaning e audio 3D | ✅ | Wet physics, particelle rain/spray opt-in, doppler/distanza e rolling per superficie. |
| Rendering delle mesh | ✅ | Bake KN5 → NMS2, finestre LOD/culling, texture DDS e materiali `materials.txt` raggiungono il frame (set 1) e sono verificati su contenuto di riferimento (2.2–2.5). Sprint S1 del rendering AI brief: mip runtime + anisotropia sul sampler albedo. Restano IBL e clear-coat come infrastruttura, non come deliverable. |
| UX racing completa | 🟡 | Menu, garage, risultati e piano pit esistono; va completato il flusso di gara con team, servizi box e contenuto selezionato. |
| Contenuto e presentazione | 🟡 | Parser per team/upgrades/livree/sound pack e cache texture esistono, ma non sono ancora risolti e applicati in una sessione di gara end-to-end. |

Riferimenti: [PARITY_STATUS.md](PARITY_STATUS.md),
[GAP_MATRIX.md](GAP_MATRIX.md), [ARCHITECTURE.md](ARCHITECTURE.md),
[MILLIKEN_KSENGINE.md](MILLIKEN_KSENGINE.md),
[CHECKLIST.md](CHECKLIST.md) (checklist operativa),
[WIREUP_GAPS.md](WIREUP_GAPS.md) (gap di cablaggio puntuali) e
[ksengine-vs-cryengine-gap.md](ksengine-vs-cryengine-gap.md).

---

## Fase 0 — Fondazioni racing

**Stato: ✅ chiusa.**

- Pneumatici da INI, load sensitivity, grip per superficie, setup e FFB.
- Replay, AI su racing line, penalità, PB, meteo e ciclo sessione.
- Pit/garage, danni, telemetria shared-memory/UDP/TCP e Control API.
- Multiplayer host/join LAN, interpolazione, lobby e auth a token.

**Guardrail:** la fisica e il trasporto di rete sono maturi; i lavori successivi
non devono sostituirli senza un problema misurato e un test di regressione.

---

## Fase 1 — Vertical slice guidabile

**Stato implementazione: ✅ chiusa. Stato rilascio: 🟡 da collaudare su build
pulito.**

| Deliverable | Stato | Nota |
|---|---|---|
| Audio motore, vento e cambiata | ✅ | `SimulatorAudio` riceve la telemetria di guida. |
| Caricamento stabile di una pista e un'auto | ✅ | Bake swap e placeholder solido proteggono la scena quando un asset manca. |
| Flusso menu auto → pista → sessione → risultati | ✅ | Practice, quick race e time attack sono vincolati alla selezione di auto e pista. |
| Binding volante/tastiera persistenti | ✅ | Rebind overlay e `KeyboardMapping`. |
| HUD danni e canali telemetrici | ✅ | HUD, shared memory, UDP e TCP. |
| Documentazione Control API | ✅ | [CONTROL_API.md](CONTROL_API.md). |

### Gate di uscita da eseguire

- [ ] Build/install pulito → 10 minuti di practice con audio e FFB reali.
- [ ] Host + un client LAN → cinque giri, risultato visibile e nessun desync
      grossolano.
- [ ] Riavvio del gioco → binding volante e selezione contenuti ancora validi.

I tre gate sono test di accettazione; non vanno marcati completi in assenza del
report della macchina che li ha eseguiti.

---

## Fase 2 — Da vertical slice a prodotto giocabile

La priorità non è aggiungere altri effetti isolati: è chiudere i percorsi già
iniziati fino a una gara ripetibile con contenuto reale. L'ordine riduce il
rischio: prima una scena corretta, poi il flusso gara che la usa, infine la
validazione.

### 2A — Contenuto e rendering runtime

| # | Deliverable | Stato | Criterio di accettazione |
|---|---|---|---|
| 2.1 | Mesh pista/auto con LOD | ✅ | Bake NMS2 conserva `lodIn`/`lodOut`; il renderer culla per distanza senza regressioni dei mesh legacy. |
| 2.2 | Texture DDS a runtime | ✅ | `TextureRuntime` è posseduto dal renderer, risolve le texture della cache baked (`<dir>/textures/`) e fa fallback bianco (e flat-normal per le normal map). Evidenza: fixture DDS in `test_renderer` (decode → upload → cache → fallback), bind del set 1 nel frame. |
| 2.3 | Materiali PBR per mesh | ✅ | Il runtime legge `materials.txt` (`MaterialCache`), associa albedo/normal/roughness/metalness per mesh e li passa a descriptor (set 1: albedo + normal + `MaterialUBO`) e shader (`native_forward.frag`, `gbuffer.frag` → roughness nella GBuffer). Evidenza: `material_cache_test` + `test_renderer`. |
| 2.4 | Un contenuto di riferimento | ✅ | Una pista e una vettura sono caricabili da cache, con texture e LOD verificati in una sessione windowed. Evidenza: bake sintetico commitato `content/baked/` e `content/cars/refcar/`. |
| 2.5 | Verifica GPU e degradazione | ✅ | Screenshot/regression test su scena textured, contatori culling e messaggio utile per asset o shader mancanti. |

### Gate di uscita da eseguire (2A)

- [x] Contenuto di riferimento: `test_renderer` verde + sessione windowed fotografata.
- [ ] Sessione windowed manuale con pista e vettura reali (cache AC):
      texture, LOD e materiali visibili, nessun fallback inatteso.

### 2B — Gara, box e identità della vettura

| # | Deliverable | Stato | Criterio di accettazione |
|---|---|---|---|
| 2.6 | Piano pit nella UI | ✅ | Fuel e lavori di riparazione sono modificabili e copiati in `PitRepairInput`; il piano non richiede automaticamente il servizio. |
| 2.7 | Servizio box end-to-end | 🟡 | Conferma piano → richiesta esplicita in box → tempi di servizio → stato, danni e fuel aggiornati nel HUD. Vedi [WIREUP_GAPS.md](WIREUP_GAPS.md). |
| 2.8 | Team, numero e box assegnati | 🟡 | `team.ini` determina roster, livrea/numero e garage/grid. `loadTeam` + `buildFieldFromTeam` + menu `onTeamChosen` sono cablati; resta collaudo end-to-end. |
| 2.9 | Upgrade, livrea e sound pack | 🟡 | Menu `upgradeRowLabels` / `onUpgradeRowCycled` → `cycleUpgradeRow` cablato; resta collaudo con contenuto reale. |
| 2.10 | Menu racing coerente | 🟡 | Garage, pit, risultati e multiplayer espongono lo stesso stato di sessione. |

I modelli dati di 2.8 e 2.9 sono già presenti (`TeamInfo`, `GarageSpawn`,
`VehicleAppearanceBundle`, `ApplyVehicleUpgrades`); il lavoro rimanente è il
wire-up residuale e il collaudo, non la loro riscrittura.

### 2C — Accettazione e qualità

| # | Deliverable | Stato | Criterio di accettazione |
|---|---|---|---|
| 2.11 | Gara asciutto/bagnato ripetibile | ⬜ | Gara 10–15 min, 8+ auto; grip, spray, audio e risultati coerenti. |
| 2.12 | Stabilità lunga | ⬜ | Run di un'ora senza NaN o recovery; log e telemetria ispezionabili. |
| 2.13 | Regressione automatica | 🟡 | CTest Qt-free e standalone restano verdi; aggiungere test per manifest materiali, fallback texture e bridge team/upgrades. |
| 2.14 | Profilo di frame reale | ⬜ | Misurare CPU fisica e GPU su hardware target con la scena di riferimento. |

### Ordine di esecuzione consigliato

| Blocco | Dipende da | Risultato osservabile |
|---|---|---|
| A. Texture + materiali (2.2–2.3) | Cache/baker già presenti | Auto e pista non sono più mesh monocromatiche. |
| B. Contenuto di riferimento (2.4–2.5) | A | Una scena windowed verificabile con LOD, fallback e screenshot. |
| C. Team + upgrade (2.8–2.9) | B | La scelta del player cambia spawn, numero, fisica, look e audio. |
| D. Servizio box e UX (2.7, 2.10) | C | Una gara completa passa dal garage al pit e ai risultati. |
| E. Soak/performance (2.11–2.14) | A–D | Gate di rilascio basati su misure, non su stime. |

---

## Fase 3 — Profondità pro, solo dopo la Fase 2

| # | Deliverable | Priorità | Nota |
|---|---|---|---|
| 3.1 | Validazione pneumatici contro telemetria reale | Alta | Campagna dati; modello Milliken come riferimento. |
| 3.2 | Secure-connect ksnet, reconnect e server dedicato | Media | Hardening dopo collaudo LAN. |
| 3.3 | Multi-layout senza reload completo | Media | Solo se i contenuti scelti lo richiedono. |
| 3.4 | Driver swap endurance | Media | Dopo pit strategy e sessione lunga. |
| 3.5 | VR OpenXR | Bassa | Solo con camera/render loop stabile. |
| 3.6 | Triple monitor | Bassa | Manca multi-view nel runtime. |
| 3.7 | Ranked / skill rating | Bassa | Infrastruttura server + anti-cheat. |
| 3.8 | Laser-scan track pipeline | Bassa | Tooling offline. |

---

## Metriche e gate di rilascio

| Milestone | Gate |
|---|---|
| Fase 1 | Practice giocabile; FFB/audio, bind persistenti e LAN 2-client verificati su build pulito. |
| Fase 2 | Gara 10–15 min in asciutto e bagnato, 8+ auto, una pista/auto textured con LOD. |
| Stabilità | Zero NaN in soak di un'ora; nessun crash o desync grave nel test LAN. |
| Prestazioni | Fisica entro budget; GPU solo dopo misura. |
| Qualità | Suite automatizzate verdi e bridge end-to-end coperto da test. |

---

## Decisioni esplicite

| Non fare ora | Motivo |
|---|---|
| Riscrivere fisica, sessione o `ksnet` | Fondazioni già funzionali. |
| Aggiungere moto/barca/aereo | Fuori ambito auto. |
| Ranked globale prima del loop offline/LAN | Serve infrastruttura e anti-cheat. |
| Promuovere IBL/clear-coat come “finiti” | Restano infrastruttura. |
| Riattivare l'editor Qt per compensare un gap runtime | La slice deve restare Qt-free. |

---

## Documenti operativi

- [CHECKLIST.md](CHECKLIST.md) — checklist operativa con gate e ordine di lavoro.
- [WIREUP_GAPS.md](WIREUP_GAPS.md) — gap di cablaggio puntuali (file + riga) per 2.7–2.10.
- [PARITY_STATUS.md](PARITY_STATUS.md) — stato puntuale delle funzionalità cablate.
- [GAP_MATRIX.md](GAP_MATRIX.md) — mappa storica P0–P3.
- [MILLIKEN_KSENGINE.md](MILLIKEN_KSENGINE.md) — dinamica veicolo.
- [CONTROL_API.md](CONTROL_API.md) — API esterna.
- [NATIVE_UI.md](NATIVE_UI.md), [VEHICLE_UPGRADES.md](VEHICLE_UPGRADES.md),
  [GARAGE_TEAM_AUDIO.md](GARAGE_TEAM_AUDIO.md) — contratti Fase 2.

*Aggiornare questa roadmap alla chiusura di ogni gate e includere nel commit il
test o la misura che giustifica il cambio di stato.*
