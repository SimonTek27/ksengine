# CHECKLIST OPERATIVA — ksengine / ksim
## Stato attuale al 2026-10-08

Analisi basata su esame sorgenti e documentazione. Build Qt-free pulita confermata.

---

## A. Gate di accettazione (obbligatori)

| Voce | Stato | Note |
|------|-------|------|
| Build/install pulito: `cmake -B build -DKSENGINE_QT_FREE=ON` + `cmake --build build -j` | ✅ | Build Debug completata senza errori |
| Practice ~10 min con audio reale e FFB reale | 🟡 | Audio e FFB wired; serve test con contenuto AC reale |
| Host + 1 client LAN → cinque giri, risultati visibili, nessun desync grossolano | 🟡 | Infrastructure ksnet presente; test end-to-end pendente |
| Riavvio del gioco → binding volante e selezione auto/pista ancora validi | 🟡 | Il binding persistente è implementato; serve验证 con sessione reale |
| Sessione windowed con pista e auto reali (cache AC) | 🟡 | Bake sintetico e refcar disponibili; gate umano AC ancora richiesto |

> **Nota:** I gate umani con contenuto AC reale restano obbligatori (vedi roadmap).

---

## B. Fase 2B — Gara, box e identità vettura

| # | Task | Stato | Criterio accettazione | Implementazione |
|---|------|--------|----------------------|-----------------|
| [ ] | **2.7 Servizio box end-to-end** | 🟡 | Conferma piano → richiesta esplicita in box → tempi servizio → stato, danni e fuel aggiornati HUD | `PitLaneRepair` infrastructure presente; wire-up selezione → box non completamente end-to-end verificato |
| [ ] | **2.8 Team, numero e box assegnati** | 🟡 | `team.ini` determina roster, livrea/numero e garage/grid player e AI all'avvio | `TeamInfoLoader::loadIni()` fully implemented in `SimulationLoop::loadTeam()`; garage assignment e grid slot resolti |
| [ ] | **2.9 Upgrade, livrea e sound pack** | 🟡 | Selezione applica `VehicleUpgradeSystem` a fisica, nodi render, texture/livrea e banca audio; fallback = contenuto base | `VehicleUpgradeSystem`, `VehicleAppearanceBundle`, `resolveAppearance()`, `applyUpgradePhysics()`, `applyUpgradeAudio()` tutti presenti e funzionanti |
| [ ] | **2.10 Menu racing coerente** | 🟡 | Garage, pit, risultati e multiplayer espongono lo stesso stato sessione, senza callback o overlay duplicati | `FeatureHub` route gli eventi; `SimulationLoop::startFeatureServices()` collega API events al loop; pendente verifica "stesso stato" senza duplicati |

> **Nota dai documenti:** modelli dati esistono già (`TeamInfo`, `GarageSpawn`, `VehicleAppearanceBundle`, `ApplyVehicleUpgrades`); lavoro rimanente è il *wire-up* nel selettore e in `SimulationLoop`, non la loro riscrittura.

---

## C. Fase 2C — Accettazione e qualità

| # | Task | Stato | Criterio accettazione |
|---|------|--------|----------------------|
| [ ] | **2.11 Gara asciutto/bagnato ripetibile** | ⬜ | Gara 10–15 min, 8+ auto; grip, spray, audio e risultati coerenti |
| [ ] | **2.12 Stabilità lunga** | ⬜ | Run di un'ora senza NaN o recovery; log e telemetria ispezionabili |
| [ ] | **2.13 Regressione automatica** | 🟡 | CTest Qt-free e standalone verdi; mancano test per manifest materiali, fallback texture e bridge team/upgrades |
| [ ] | **2.14 Profilo di frame reale** | ⬜ | Misurare CPU fisica e GPU su hardware target con scena di riferimento; nessun KPI GPU dichiarato prima della misura |

---

## D. Ordine di esecuzione consigliato

| Blocco | Dipende da | Risultato osservabile | Stato |
|--------|------------|----------------------|-------|
| A. Texture + materiali (2.2–2.3) | Cache/baker già presenti | Auto e pista non più mesh monocromatiche | ✅ [x] |
| B. Contenuto di riferimento (2.4–2.5) | A | Scene windowed verificabile con LOD, fallback e screenshot | ✅ [x] |
| C. Team + upgrade (2.8–2.9) | B | La scelta player cambia spawn, numero, fisica, look e audio | 🟡 [ ] |
| D. Servizio box e UX (2.7, 2.10) | C | Gara completa passa dal garage al pit e ai risultati | 🟡 [ ] |
| E. Soak/performance (2.11–2.14) | A–D | Gate di rilascio basati su misure, non stime | ⬜ [ ] |

> **Nota critica:** C e D non dovrebbero iniziare prima che B renda visibile la vettura selezionata. B è già ✅.

---

## E. Cose da non fare ora

| Voce | Motivo |
|------|--------|
| Riscrivere fisica, sessione o `ksnet` | Sono fondazioni già funzionali |
| Aggiungere domini moto/barca/aereo | Deviano dall'obiettivo auto |
| Fare ranked globale prima del loop offline/LAN | Serve infrastruttura, moderazione e anti-cheat |
| Promuovere IBL/clear-coat come "finiti" | Restano infrastruttura (nessun upload/binding/test immagine end-to-end) |
| Riattivare l'editor Qt per compensare un gap runtime | La slice deve restare eseguibile Qt-free |

---

## F. Verifica tecnica — Cosa è stato implementato

### Dati e infrastruttura (già presenti)

- **TeamInfo loading** (`src/simulator/TeamInfo.h`): `loadIni()` legge `team.ini` con sezione `[Team]` e `[Car_N]`, auto-assigns garage indices, race numbers, driver names, livery IDs, player flags. ✅
- **GarageSpawn** (`src/simulator/GarageSpawn.h`): Policy per row layout, assignment, grid placement. ✅
- **VehicleAppearanceBundle** (`src/engine/vehicle/VehicleAppearanceBundle.h`): Struttura con components (RaceComponentConfig), livery, sound, enable/disable nodes. ✅
- **resolveAppearance()** (`src/engine/vehicle/VehicleAppearanceBundle.h`): Unisce componenti INI + upgrade selections per physics/nodes/livery/sound. ✅
- **ApplyVehicleUpgrades** (referenziato in `SimulationLoop`): `loadUpgradesForCar()` carica `upgrades.ini` o fallback built-in; `refreshUpgradeApplication()` re-applies tutto. ✅
- **MaterialCache** (`MaterialCache.h`): Legge `materials.txt` da bake, associa albedo/normal/roughness/metalness. ✅
- **Baked scene loading** (`SimulationLoop::loadBakedScene`, `spawnSceneEntities`): Carica manifest e mesh da directory baked. ✅
- **ensureCarVisual()** (`SimulationLoop.cpp`): Applica texture/livrea dai bump di upgrade/team, fallback a placeholder box solid. ✅
- **Track visuals** (`applyTrackVisuals`): Carica track bake con LOD e fallback. ✅

### Wire-up in SimulationLoop (parzialmente completato)

- `loadTeam()` chiamato dal menu/control API → popola `m_team` ✅
- `loadCar()` → chiama `loadUpgradesForCar()` + `applyUpgradePhysics()` + `ensureCarVisual()` ✅
- `beginRaceSession()` → `buildFieldFromTeam(m_team, m_aiCarCount)` imposta identità driver, numeri, grid ✅
- `placePlayerForSession()` → calcola posa player da garage/grid/linea basata su `m_sessionType` ✅
- `spawnSessionAiVisuals()` → visual AI dalla bake + appearance del carico corrente ✅
- `refreshUpgradeApplication()` → re-resolve appearance + physics + visual + audio al cambiare selezione ✅
- `cycleUpgradeRow()` → ciclo livello upgrade + refresh totale ✅

### Mancanti per completamento 2B

- **End-to-end box service**: Il piano pit (2.6 ✅) esiste ma il servizio box end-to-end (2.7 🟡) non è stato verificato completamente. Manca la conferma "richiesta esplicita in box → tempi di servizio → stato, danni e fuel aggiornati nel HUD".
- **Menu racing coerente (2.10)**: Gli overlay espongono stato sessione ma serve verifica che non ci siano callback o overlay duplicati.
- **Team/upgrade selection flow**: La sequenza "seleziona auto → carica team → applica upgrade → visualizza vettura" deve essere testata end-to-end.

### Mancanti per completamento 2C

- **Automated regression tests (2.13)**: Il CTest suite è Qt-free verde (52/52 per gate 2A secondo roadmap), ma servono test aggiuntivi per:
  - Manifest materiali
  - Fallback texture
  - Bridge team/upgrades
- **Soak stability (2.12)**: Run un'ora senza NaN non è stato eseguito.
- **Performance profiling (2.14)**: Misurazioni CPU/GPU sulla scena di riferimento non sono state eseguite.

---

## G. Build e test confermati

```
cmake -B build -DKSENGINE_QT_FREE=ON  # ✅ Configured OK
cmake --build build -j                 # ✅ Debug build completa senza errori
```

Test Qt-free status dai documenti roadmap: **52/52 ctest verdi**, gate 2A passaggio confermato.

---

## H. Prossimi passi consigliati

1. **Verifica end-to-end 2B**: Eseguire una sessione completa dall'selezione auto/team fino alla gara, confermando che:
   - team.ini determina roster, numeri, liverie ✅ (già implementato)
   - upgrade selection cambia physics, nodes, livery, audio ✅ (già implementato)
   - garage/grid assignment funziona per player e AI ✅ (già implementato)
   - Il gap restante è la convalida con contenuto AC reale e il servizio pit

2. **Aggiungere test automatizzati 2.13**: Aggiungere test CTest per:
   - Manifest materiali presenti/assenti
   - Fallback texture when materials.txt missing
   - Bridge team/upgrades end-to-end

3. **Soak test 2.12**: Eseguire run fisica di un'ora verificando zero NaN.

4. **Profilo frame 2.14**: Misurare CPU fisica e GPU sulla scena `content/baked` + `content/cars/refcar`.

5. **Gate umano AC**: Eseguire sessione windowed con contenuto AC reale per confermare texture, LOD e materiali visibili senza fallback inaspettati.