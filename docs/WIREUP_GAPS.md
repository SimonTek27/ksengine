# Wire-up gaps — Fase 2B

**Aggiornata:** 2026-10-20  
Analisi puntuale del tree su `master`. Obiettivo: non riscrivere modelli già presenti; chiudere i collegamenti mancanti.

---

## Sintesi (aggiornata 2026-10-20)

| ID | Stato codice | Cosa resta | Priorità |
|----|--------------|------------|----------|
| **2.7** | ✅ cablato end-to-end | Gate umano (tempi, HUD, esperienza box) | **Alta (collaudo)** |
| **2.8** | ✅ team/roster | Collaudo con `team.ini` AC reale | Media (collaudo) |
| **2.9** | ✅ upgrade menu → physics/audio | Collaudo con `upgrades.ini` reale | Media (collaudo) |
| **2.10** | ✅ standings/HUD unificati | Verifica manuale path menu | Media (collaudo) |
| **2.11** | — | ✅ gara 10–15 min dry/wet 8+ auto (evidence) | chiuso |
| **2.12** | — | ✅ soak 1 h (evidence) | chiuso |
| **2.14** | — | ✅ profilo frame (evidence) | chiuso |

Piano operativo gate: [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md).

---

## 2.7 — Servizio box end-to-end

### Codice (chiuso 2026-10-11+)

| Pezzo | Dove | Stato |
|-------|------|-------|
| UI piano pit | `GameMenuOverlay` PitStrategy | ✅ |
| REQUEST SERVICE | distinto da CONFIRM PLAN | ✅ |
| Uscita/rientro box | `updateGarageExit`, tasto P, menu GARAGE | ✅ |
| `PitStrategyBridge` → `PitRepairInput` | header + loop | ✅ |
| `updatePitRepair` a 1 kHz | `SimulationLoop_Pit.cpp` | ✅ |
| Fuel / tyres / capacity | `setFuelL`, `resetTyreWear`, `fuelTankCapacityL` | ✅ |
| Coda multi-auto | `PitLaneQueue` | ✅ |
| Test | `pit_strategy_menu_test` | ✅ |
| **Gate umano** | tempi, HUD, esperienza | ⬜ |

### Fuori scope codice (opzionale)

- **Serbatoio nel JSON (percorso reale)**: il contenuto convertito non ha
  `.ini`, ha un solo `data/data.json` con `"fuel": { "MAX_FUEL": 90 }`.
  `CarPhysicsJson::loadAcFuelCapacity` lo legge prima degli ini; `FUEL`
  resta il carico attuale e non viene mai preso come capacita'.
  Aperto lato contenuto: `data/data.json` e'
  JSON non valido (643 `{` contro 644 `}`, radice chiusa a riga 297,
  sezioni `BASIC`/`GRAPHICS`/`PIT_STOP` finite in radice e `"car"`
  vuoto), quindi il tank resta il default finche' il converter non lo
  rigenera.
- **Converter contenuto AC**: `AcCarManifestConverter`
  (`src/adapters/assetto_corsa/AcCarManifestConverter.h`) porta una
  cartella `.ini` stock ai sei documenti `data/data.json`, `driver.json`,
  `camera.json`, `setup.json`, `model.json`, `sound.json`, `ai.json` (stessa forma: una
  chiave di root per stem di file, smistata per natura del dato; `model.json`
  contiene la tabella dei nodi `proview_nodes`, i `lods`, i `colliders` e la sezione `GRAPHICS_OFFSETS`; `sound.json` tiene
  `sounds.ini` per intero; `ai.json` raccoglie tutto cio' che riguarda l'IA,
  cioe' `ai.ini` e anche `ai_tyres.ini` (il degrado chilometrico delle mescole),
  che prima restava fuori. Non vengono convertiti `escmode.ini` (preset di
  aiuti elettronici che nessuno legge) e `ac_tyres.ini` (ripete la geometria
  delle gomme gia' in `tyres.ini`), che restano nella cartella) con la forma
  del formato attuale (una chiave di root per stem di file, `car` da
  `car.ini`, `fuel` ricomposto da `[FUEL]`/`[FUELTANK]`/`fuel_cons.ini`).
  `VehicleSimulator::loadVehicleParams` lo invoca quando la vettura non
  e' pronta; CLI: `ks_cartool convert <src> <dst>` e `ks_cartool split <car>`
  per completare una cartella che ha solo il vecchio `data.json` (senza ini).
  Aggiunte di tipo, non inventate per vettura: `fuel.KG_PER_LITER`
  (0.76, densita' che AC non serializza), `car.RULES.MIN_HEIGHT`
  (default AC), `SHORT_NAME` dal brand. Nessun blocco turbo/ERS se gli
  ini non lo dichiarano.
- **Voci obbligatorie**: qualunque vettura, di qualunque tipologia, deve
  portare le chiavi elencate in `AcCarManifestConverter::requiredEntries()`
  (`dampers`, `bumpstops`, `car.GRAPHICS`, `brakes.TEMPS_*`,
  `engine.HEADER.POWER_CURVE_STANDARD`/`HUMIDITY_REF`,
  `engine.ENGINE_DATA.AMBIENT_EFFECT`, `ai.TYRES`/`WING_VARIATION`/`LOOKAHEAD`,
  `electronics.ABS_V2`/`EDL`,
  `drivetrain.UPSHIFT_PROFILE`/`DAMAGE`, `specs.range`). La conversione le
  verifica e la CLI stampa quelle mancanti; `dampers`/`bumpstops` sono
  proiettati dalle chiavi classiche inline di `suspensions`. `car.GRAPHICS`
  non resta nel documento fisico: ogni sua voce va al documento del suo
  soggetto (`GRAPHICS.DRIVEREYES`/`ON_BOARD_PITCH_ANGLE`/`ONBOARD_EXPOSURE` a
  `driver.json`, le chiavi `*_CAMERA_*`, `OUTBOARD_EXPOSURE` e `SHAKE_MUL` a
  `camera.json`, `GRAPHICS_OFFSET`/`GRAPHICS_PITCH_ROTATION`/
  `USE_ANIMATED_SUSPENSIONS` a `model.json`, `MIRROR_POSITION` va nella
  sezione `mirrors` di `data.json` (quella di `mirrors.ini`) perche' lo
  specchio e' geometria della carrozzeria, non del modello. Il `HEADER` che
  ogni sezione si portava dietro perde il `VERSION` (la versione del formato
  non e' un dato della vettura) e, se non teneva altro, sparisce del tutto;
  dove invece il `HEADER` ha valori veri (motore: `POWER_CURVE_STANDARD`,
  `HUMIDITY_REF`) resta con quelli;
  `FUEL_LIGHT_MIN_LITERS` non e' grafica, finisce nella zona `fuel` di
  Nei freni la sezione dei dischi tiene solo le mesh (`DISCS_COMPONENTS`):
  `FRONT_MAX_GLOW`, `REAR_MAX_GLOW`, `LAG_HOT` e `LAG_COOL` sono comportamento,
  quindi salgono in `brakes.DATA` anche quando l'ini li scrive li' dentro.
  `data.json`). Le spie di acqua e olio seguono la stessa idea dentro
  `engine`: `WATER_LIGHT_MAX_C` (110) e `OIL_LIGHT_MIN_BAR` (1.0), costanti in
  un unico punto del converter, non valori per vettura.
- **Verifica online dei dati**: dopo la conversione il converter scrive
  `data/verify.json`, l'elenco delle affermazioni da controllare (potenza,
  coppia, massa, limitatore, marce, gomme, serbatoio, picchi delle curve) con
  una query di ricerca gia' pronta per ciascuna. Ogni affermazione porta
  `status` (`unchecked`/`match`/`mismatch`/`unknown`), `source` (URL) e `note`;
  la conversione non e' "finita" finche' `coverage.status` non vale `complete`.
  La ricerca non puo' girare dentro il motore (nessun client HTTP, nessuna
  chiave): la esegue il toolchain e l'esito si registra nel file.
  `ks_cartool verify <car>` aggiorna la lista, stampa le query e riporta la
  copertura; `convert` e `split` la producono da soli. Le correzioni passano
  solo per proposta e accettazione: ogni voce puo' portare `proposal` e
  `accepted`, e `ks_cartool apply <car>` riscrive il dato **solo** dove
  `accepted` vale `true`; tutto il resto resta com'e'.
- **Flusso in quattro passi**: 1) `convert` con la regola delle voci obbligatorie
  (obbligatoria), 2) `verify` + `apply` con confronto sui dati reali e
  proposta/accettazione (facoltativa), 3) `prune` che rimuove i `.ini` ormai
  rappresentati dai documenti (`.lut`, `.rto`, `.kn5`, texture e suoni
  restano). `prune` **rifiuta** se la regola del primo passo non e' soddisfatta,
  e va usato solo quando i lettori del motore leggono davvero i documenti:
  finche' motore, aero, gomme e sospensioni passano dagli ini, cancellarli
  azzera quei valori. 4) `pack` rigenera `data.acd` dal contenuto attuale di
  `data/` (documenti, curve, preset), cifrato con la chiave della cartella
  vettura: `prune` lo fa da solo appena ha finito, cosi' archivio e cartella
  raccontano la stessa cosa. `pack` da solo pretende i sette documenti; la
  regola del primo passo la impone `prune`, il punto in cui l'insieme e'
  considerato completo.
- AI `requestEnter` in corsia — **non fatto di proposito**: l'AI non ha un
  path verso il box (`AIController` segue solo la linea di gara), quindi
  chiedere l'ingresso occuperebbe uno slot in coda senza mai entrare.
  Serve prima il path AI box -> box -> rientro.
- ~~Parser `powertrain.ini` per capacità~~ - fatto: `[FUEL] MAX_FUEL` da
  `car.ini` / `powertrain.ini` (`VehicleSimulator::loadFuelCapacityFromIni`),
  con `FUEL_CAPACITY` in `engine.ini` che resta l'override esplicito.
  Coperto da `test_physics_ini`.

---

## 2.8 / 2.9 / 2.10

Codice consegnato e coperto da `menu_session_gate_test`. Resta solo collaudo con contenuto AC reale e verifica path menu (vedi GATE_PLAN_WINDOWS G2/G3).

---

## Riferimenti

- [CHECKLIST.md](CHECKLIST.md)
- [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md)
- [ROADMAP.md](ROADMAP.md)
- [GATE_2_11_WET_DRY_RACE.md](GATE_2_11_WET_DRY_RACE.md)
- [GATE_2_12_STABILITY.md](GATE_2_12_STABILITY.md)
- [GATE_2_14_FRAME_PROFILE.md](GATE_2_14_FRAME_PROFILE.md)
