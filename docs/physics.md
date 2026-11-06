# physics.md — come una macchina AC diventa fisica in ksim

Questo documento descrive **cosa il motore legge davvero** da una cartella auto
proveniente da AC, e in che ordine. Serve a rispondere a due domande ricorrenti:
«questo dato viene usato?» e «se cambio questa chiave, cosa si muove?».

Il percorso ha tre stadi: la cartella AC originale, la conversione, il runtime.

```
content/cars/<car>/data/*.ini        (contenuto AC: car.ini, engine.ini, ...)
        │
        │  AcCarManifestConverter (in ksim: al primo caricamento, o via ks_cartool)
        ▼
content/cars/<car>/data/data.json    + 6 documenti  + verify.json  + brake_temp_*.lut
        │
        │  VehicleSimulator / SimulationLoop
        ▼
fisica del veicolo (coppia, serbatoio, gomme, aero, sospensioni, freni)
```

## 1. Quando scatta la conversione

`AcCarManifestConverter::needsConversion(carDir)` converte la cartella quando
**non** ha il set completo di documenti e c'è ancora un impianto `.ini` da
leggere:

- `hasManifest()` è vero solo con **tutti e sette** i documenti
  (`data.json`, `driver.json`, `camera.json`, `setup.json`, `model.json`,
  `sound.json`, `ai.json`). Una cartella con la sola fisica non è «pronta».
- La conversione parte se manca il set e trovi `data/car.ini` (o `car.ini`), oppure
  un `data.json`/`physics.json` legacy da spezzare.

Il convertitore non cancella niente: le `.ini` restano dove sono e la cartella
resta caricabile anche se un documento manca.

## 2. `data.json`: il documento della fisica

È il primo dei sette documenti. La struttura è **a sezioni con la grafia delle
ini**: la chiave di primo livello è lo stem del file `.ini` da cui viene il dato,
e dentro si trovano le sue sezioni originali.

```json
{
  "fuel":   { "MAX_FUEL": 90 },
  "brakes": {
    "DATA": { "MAX_TORQUE": 3250, "FRONT_SHARE": 0.6,
              "HANDBRAKE_TORQUE": 1000, "COCKPIT_ADJUSTABLE": 0 },
    "TEMPS_FRONT": { "TRANSFER": 0.4, "COOL_TRANSFER": 0.002,
                     "PERF_CURVE": "brake_temp_front.lut",
                     "COOL_SPEED_FACTOR": 0.06, "COOL_SPEED_FACTOR_0": 0.05 },
    "TEMPS_REAR":  { "...": "idem, con COOL_SPEED_FACTOR_0 0.04" }
  }
}
```

Cosa il motore legge **oggi** da `data.json` (tutto il resto resta nel file come
dato di contenuto, pronto per essere collegato):

| Sezione / chiave | Letta da | Effetto |
|---|---|---|
| `fuel.MAX_FUEL` (o `maxFuel`, `fuelCapacityL`) | `CarPhysicsJson::loadAcFuelCapacity` | capacità serbatoio in litri, prima delle `.ini` |
| `brakes.TEMPS_FRONT` | `CarPhysicsJson::loadAcBrakeTempsForCarDir` | profilo termico asse anteriore |
| `brakes.TEMPS_REAR` | idem | profilo termico asse posteriore |

`TEMPS_*` alimenta `BrakeThermalModel` per asse (ruote 0/1 davanti, 2/3 dietro) con
tre numeri:

- `COOL_SPEED_FACTOR` → `coolingSpeedFactor`, **1:1** (per m/s: stesso significato);
- `COOL_SPEED_FACTOR_0` → `coolingBase`, normalizzato su `0.05` (il riferimento
  del convertitore), così un'auto con i numeri di riferimento si comporta come i
  default del modello;
- `COOL_TRANSFER` → scala di `coolingCoeff` (riferimento `0.002`);
- `TRANSFER` → `padCoupling` disco→pasticca (riferimento `0.4`).

`TORQUE_K` e `CORE_TRANSFER_IN/OUT/AIR` **non** vengono consumati: appartengono al
modello di temperatura del *core* del disco, che questo motore non ha. Fingerli
sarebbe invenzione, quindi restano dichiarati e inutilizzati.

### Le tabelle `.lut` dei freni

`PERF_CURVE` punta a un file nella stessa cartella, non a una stringa inline:

```
data/brake_temp_front.lut      # temperatura °C | moltiplicatore di coppia
0|0.3409
400|1
900|0.9659
1200|0.5909
2000|0.3409
```

Formato: righe `x|y`, `|` obbligatorio, `#` commenta, tabella riordinata per
temperatura al caricamento, interpolazione lineare e **valori tenuti piatti fuori
dai punti** (nessuna estrapolazione). Sotto le due righe il file viene rifiutato e
il modello resta sulle soglie `fadeStartTemp`/`fadeFullTemp`.

Conseguenza da conoscere: a 80 °C (dischi freddi) il moltiplicatore è ~0.47, quindi
la frenata è più lunga a freddo e si accorcia scaldandosi. Misurato sul jaguar
convertito: **84.6 m** da 100 km/h a freddo, **65.4 m** con tabella «già calda».

## 3. Le `.ini` lette a runtime

Ogni loader cerca il file in `<car>/data/<nome>` e poi in `<car>/<nome>`.
`parseIni` registra sia `SEZIONE/CHIAVE` sia la chiave nuda, e taglia i commenti
con `;`, quindi valgono entrambe le grafie.

| File | Chiavi lette | Destinazione |
|---|---|---|
| `car.ini` | `[FUEL] MAX_FUEL` | capacità serbatoio (L) |
| `powertrain.ini` | `[FUEL] MAX_FUEL` | capacità serbatoio (per i mod che la tengono qui) |
| `engine.ini` | `POWER`, `LIMITER`, `FUEL_CAPACITY` | potenza kW, limitatore rpm, override serbatoio |
| `tyres.ini` / `tires.ini` | `RADIUS` (o `FRONT/RADIUS`) + coefficienti del modello gomma | raggio ruota; `KsTireModel::loadFromIni` legge i propri coefficienti (p.es. `NOMINAL_LOAD`/`FZ0`, `LOAD_SENSITIVITY`, `PRESSURE_OPT`, `TEMP_OPT`) |
| `drivetrain.ini` | `FINAL`, `FINAL_DRIVE`, `GEAR_1`…`GEAR_10` | rapporto finale e marce (contigue da 1) |
| `aero.ini` | `CD`, `AREA` | resistenza e superficie frontale |
| `suspension.ini` | `WHEELBASE`, `TRACK` | passo e carreggiata |
| `brakes.ini` | `[DATA] MAX_TORQUE`, `FRONT_SHARE`, `HANDBRAKE_TORQUE`, `COCKPIT_ADJUSTABLE` | tetto di coppia, ripartizione, freno a mano, bias regolabile |

Note sui freni, perché sono i più «dichiarativi»:

- `MAX_TORQUE` limita la coppia totale comandata (prima il motore la stimava dalla
  massa); `FRONT_SHARE` decide la ripartizione **solo** se
  `COCKPIT_ADJUSTABLE=0` — un'auto che dichiara di poter regolare il bias usa il
  valore del setup.
- `HANDBRAKE_TORQUE` è la coppia del freno a mano; senza quella voce il motore usa
  una stima legata alla massa.

## 4. Precedenza

1. Se esiste `<car>/data/physics.json` (o `<car>/physics.json`), `CarPhysicsJson`
   lo applica e **le `.ini` non vengono lette affatto**: è il formato unificato
   delle auto native ksengine.
2. Altrimenti si legge il manifest (`data.json`, solo serbatoio e freni termici) e
   poi l'impianto `.ini` con i loader della tabella sopra.

Due punti d'ingresso distinti, da tenere presenti quando si cerca «chi legge cosa»:

- `VehicleSimulator::loadVehicleParamsFromIni` — `car.ini`, `powertrain.ini`,
  `engine.ini`, `tyres.ini`/`tires.ini`, `drivetrain.ini`, `aero.ini`,
  `suspension.ini`, più il serbatoio dal manifest.
- `SimulationLoop::loadCar` — `tyres.ini`, `engine.ini`, `drivetrain.ini`,
  `aero.ini`, `suspension.ini`, `brakes.ini`, le due tabelle `.lut` e il blocco
  `TEMPS_*` del manifest.

## 5. Cosa non viene letto (e perché)

- `TEMPS_*`: `TORQUE_K`, `CORE_TRANSFER_IN/OUT/AIR` — richiedono un modello di
  core del disco che non esiste.
- `aero.ini`: le tabelle delle ali oltre `CD`/`AREA` (`loadAeroFromIni` non le
  legge).
- `suspension.ini`: la geometria oltre passo e carreggiata.
- `brakes.ini`: `[TEMPS_*]` non è in quel file (AC lo tiene nel manifest),
  `[DISCS_GRAPHICS]` è grafica.
- `blurred_objects.ini`, `cameras.ini`/`camera.json`, `sounds.ini`/`sound.json`,
  `ai.ini`/`ai.json`, `setup.ini`/`setup.json`: non sono fisica del veicolo
  (rendering, audio, AI, UI del setup).
- I metadati dell'auto (nome, classe, anno, colori) **non** vengono da
  `data.json`: `ContentLibrary` li legge da `ui/ui_car.json`.

## 6. Come verificare una conversione

- Log a runtime: `[brakes] DATA: max=… Nm front_share=… handbrake=… Nm adjustable=…`,
  `[brakes] TEMPS: front=1 rear=1 cool_base=… cool_speed=…`,
  `[brakes] brake temp curves: front=N rear=M points`.
- `ks_cartool convert|verify|info|apply|prune|pack <car-dir>`: `convert` scrive i
  sette documenti, `verify.json` raccoglie le asserzioni (oggi `unchecked`).
- Test headless: `brake_fade_test` (curve, profilo per asse, dati `[DATA]`),
  `brake_stop_test` (spazio di frenata 100→0 km/h e mantenimento al minimo).

## 7. Regole di contenuto (dettami)

- Ogni voce obbligatoria del convertitore deve esserci: la regola è «every
  required entry is there», e le voci che AC tiene nel proprio codice si aggiungono
  **nel convertitore**, mai per singola auto.
- Nessun riferimento a AC / Content Manager / CSP nei JSON prodotti.
- Le tabelle (`power.lut`, `aero_*.lut`, `brake_temp_*.lut`) sono **file**, non
  stringhe inline, così restano ispezionabili e confrontabili.
