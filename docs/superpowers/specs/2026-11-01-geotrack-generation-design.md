# Generazione piste da satellite + dati vettoriali — design

Data: 2026-11-01
Stato: proposta, in attesa di approvazione.

## Intento e perché

Generare in autonomia una pista giocabile partendo da una posizione geografica, senza
modellare a mano e senza dipendere da un asset Kunos. La scena richiesta è:

1. un **quadrato di terreno** delle dimensioni dell'ingombro della pista, texturizzato con
   imagery satellitare;
2. al centro il **layer pista**, composto da pit lane, griglia di partenza, tracciato, vie
   di fuga ed elementi dello scenario.

Il satellite e il vettoriale fanno lavori diversi: il vettoriale dà la **topologia** (dove
passa la pista, dove sta la corsia box, l'ordine delle curve), il satellite dà il **reale**
(larghezza dell'asfalto, dove finisce il tracciato e comincia ghiaia o erba, ingombro degli
apron). Nessuna delle due sorgenti da sola produce una pista corretta.

## Obiettivi

- Un comando headless che, dato un tracciato e una cache di tile, produce una cartella pista
  caricabile dal simulatore: layer in `.nmsh` + `baked/manifest.txt` + `data/data.json`
  + `scene.json`.
- Larghezza del tracciato **misurata**, non stimata: oggi `width_m` esce a 30–39 m contro i
  ~14 reali perché l'apron dei box viene classificato `road`.
- Pit lane, griglia, vie di fuga e scenario derivati da regole riproducibili, non disegnati.
- Tutto deterministico e testabile headless, in CI, senza Qt e senza GPU.

## Non-obiettivi (per ora)

- Rilievo altimetrico: il terreno della fase 1 e 2 è **piatto**, come da specifica ("un
  quadrato"). DEM/SRTM è una fase successiva e non entra in questo documento.
- Editor visuale: nessuna UI. Il kseditor resta com'è.
- Traffico, AI line definitiva, camere, suoni: un tracciato generato riceve una
  `fast_lane.ai` di bootstrap, non una racing line ottimizzata.
- Asset dello scenario nuovi: si usano quelli che esistono (mesh commissario condivisa).

## Anti-obiettivi

- Una pista che si carica ma con la larghezza sbagliata è un fallimento, non un passo avanti.
- Il generatore non deve fare rete a runtime di build: niente chiamate HTTP dentro il
  binario C++.
- Nessun passaggio manuale nel kseditor dentro la pipeline.

## Vincoli trovati nel codice (verificati)

- `ks::sim::findBakedManifestDir` (`src/simulator/SceneAssets.h`) cerca
  `<track>/baked/manifest.txt` e, in mancanza, `<track>/manifest.txt`. I layer vanno in
  `baked/`.
- `NativeRenderer::loadMeshesFromManifest` carica ogni `.nmsh` elencato nel manifest come
  mesh separata: **un layer = un mesh**.
- `SimulationLoop::loadTrack(dir)` **tollera l'assenza del `.kn5`**: senza kn5 segna
  comunque `m_trackData.valid = true`. Il blocco del `.kn5` sta solo in
  `TrackLoader::load`, che non è il percorso usato per le cartelle pista. Quindi una pista
  manifest-only è già possibile: **non serve toccare `TrackLoader`** (correzione rispetto a
  quanto detto in conversazione).
- `TrackDataJsonLoader` legge da `data/data.json` solo `name`, `length`, `width`.
  `marshal_posts` e `safety_car` sono letti da `TracksidePosts`; i garage dal loader del pit.
  Il generatore deve scrivere tutte queste chiavi.
- Nessun decoder JPEG in `src/`: c'è `PngReader`. Le tile vanno normalizzate in PNG.
- Lo skeleton dell'asfalto esiste già in `tools/trackmodules/TrackModulesCore.cpp`
  (`thinMask` Zhang-Suen, `stitchLap`, `tryLap`, `walkLap`) ma è file-local: per riusarlo va
  dichiarato in un header.
- `ks::engine::terrain::writeTerrainNMSH` (`src/engine/terrain/TerrainMesh.h`) scrive già il
  formato che il renderer legge: il quadrato e le strisce possono passare da lì.
- Nel catalogo servizi non c'è nessun provider di tile satellitari: l'immagine richiede una
  chiave dell'utente (Google, Maxar, ESRI, Sentinel-2). Il generatore resta
  provider-agnostico: consuma una cache locale.

## Approcci considerati

**A — Generatore headless a due sorgenti (scelto).** Tool Qt-free `tools/geotrack` che legge
vettoriale e cache tile, misura l'asfalto riusando raster + skeleton esistenti, emette layer
e manifest. Lo scarico (vettoriale e tile) è uno script Python che riempie una cache; il C++
legge solo file locali.
*Trade-off*: due componenti e un piano tile/LOD da progettare, in cambio di determinismo,
testabilità e riuso del codice di thinning già scritto.

**B — Solo vettoriale, satellite come sfondo.** Più rapido, ma la larghezza e le vie di fuga
restano stimate: non soddisfa la richiesta e lascia in piedi il bug della larghezza.
Scartato come destinazione, **tenuto come fase 1** di A.

**C — Dentro il kseditor.** `RoadBuilder` + `TrackExporter` esportano già una cartella AC
completa. Scartato: Qt, manuale, non autonomo, non eseguibile in questa VM. Resta la
possibilità di riusarne gli algoritmi più avanti, copiandoli.

## Decisioni (mie, da contestare se non convincono)

1. **Due fasi.** Fase 1 vettoriale + fondo texturizzato (pipeline end-to-end, pista
   caricabile). Fase 2 misura dell'asfalto dal satellite. Così qualcosa di guidabile esiste
   presto e la parte di visione non blocca tutto.
2. **Scarico fuori dal binario.** `tools/geotrack/fetch_cache.py` (Python, `urllib`) scarica
   vettoriale (Overpass) e tile, e le normalizza in PNG + un `cache.json` con i bounds. Il
   binario C++ non fa rete.
3. **Tile in PNG, non JPEG**, perché il motore sa leggere solo PNG.
4. **Tile 2048 px a 0,5 m/px = 1024 m per tile**, LOD per distanza: un quadrato di 5 km sono
   ~25 tile. Con mip e un budget di memoria dichiarato in `scene.json`.
5. **Sistema di coordinate ENU locale**: origine nel centroide del tracciato, +X est, +Z nord,
   proiezione equirettangolare con `cos(lat)`. Errore atteso ben sotto lo 0,1% su pochi km,
   verificato in fase 0 con due punti noti.
6. **Il quadrato è il bbox del tracciato espanso di 250 m**, reso quadrato sul lato maggiore.
   Le UV del quadrato derivano dai bounds geografici, così qualunque immagine scaricata si
   allinea senza aggiustamenti.
7. **Un layer = un `.nmsh`**: `ground`, `track`, `kerbs`, `runoff`, `pitlane`, `griddot`.
   `scene.json` elenca layer, materiale, tile e posizionamenti dello scenario.
8. **Pit lane**: offset parallelo al tracciato su una finestra di stazioni dichiarata in
   configurazione, con rampa di rientro a entrambi i capi; griglia: N cubetti sfalsati a
   partire dalla linea di partenza.
9. **Scenario fase 1**: solo i commissari (4 per curva, mesh condivisa
   `marshall_A_HI_010` già estratta) e la piazzola safety car. Altri props quando esiste un
   catalogo.

## Come è fatto (fase 1)

```
tools/geotrack/
  GeoProjection.h      WGS84 -> ENU locale, bounds, quadrato, UV
  GeoCenterline.h      lettura GeoJSON LineString / GPX / CSV lat,lon
  TrackSceneCore.h/cpp proiezione, strisce, pit lane, griglia, vie di fuga, writer
  main.cpp             CLI: geotrack <centrolinea> <outdir> [--width 12] [--pit ...]
  fetch_cache.py       Overpass + tile -> cache locale (PNG + cache.json)
```

Uscita:

```
<outdir>/
  baked/manifest.txt          ground, track, kerbs, runoff, pitlane, griddot
  baked/<layer>.nmsh
  baked/textures/<layer>.png  (fase 2: tile del terreno)
  data/data.json              name, length, width, pit_pads, garages, corner_count,
                              marshal_posts (mesh HI), safety_car
  scene.json                  bounds geografici, quadrato, tile, layer, scenario
  ai/fast_lane.ai             bootstrap dalla centerline (AiFileWriter)
```

Verifiche di accettazione:

- proiezione: distanza fra due punti noti entro lo 0,1% su ~5 km, e andata/ritorno coerenti;
- geometria: nessun NaN, normali unitarie, indici in range, nessun triangolo degenere,
  winding coerente, larghezza della striscia = `--width` entro 1 cm;
- quadrato: lato = bbox espanso, UV dell'angolo NW = (0,1), inversa UV→mondo coerente;
- pit lane: lunghezza entro il 2% della finestra richiesta, nessuna auto-intersezione;
- griglia: N slot, passo costante, dentro la carreggiata;
- `data.json`: `name`/`length`/`width` letti da `TrackDataJsonLoader`; `marshal_posts` e
  `safety_car` letti da `TracksidePosts` (37 check già esistenti restano verdi);
- cartella caricabile: `data/data.json` + `baked/manifest.txt`, nessun `.kn5`, e
  `SimulationLoop::loadTrack` non fallisce.

## Fase 2 — misura dal satellite

- `fetch_cache.py` scarica le tile attorno al bbox a zoom massimo disponibile.
- Il C++ rasterizza la centerline vettoriale sulla griglia delle tile, classifica i pixel per
  colore (asfalto / ghiaia / erba / muro) e misura la **mezza larghezza per stazione**
  perpendicolare alla centerline; la mediana per bin sostituisce il valore stimato.
- Lo skeleton esistente (`thinMask`, `stitchLap`) viene spostato in un header e usato per
  correggere la centerline dove il vettoriale è approssimato.
- Le vie di fuga si generano dalla fascia non-asfalto adiacente, con larghezza dalla
  classificazione; il pit apron viene escluso dalla misura del tracciato (è la causa dei
  30–39 m).
- Accettazione: la larghezza misurata su Vallelunga deve cadere entro 12–16 m e i posti
  commissario devono stare fuori dall'asfalto invece che a metà carreggiata.

## Cosa resta al builder

- Scelta del provider imagery e della chiave (utente), e licenza d'uso delle tile.
- Budget di memoria texture sul target reale: non misurabile in questa VM.
- Catalogo degli asset di scenario oltre al commissario.
- Se la fase 2 deve produrre anche il DEM; fuori scope qui.
- Conferma dei campi garage di `data.json` leggendo il loader del pit durante l'implementazione.

## Stato di implementazione (2026-11-01)

**Fase 1 completa** (`tools/geotrack`, commit `b5fe99a`): la cartella pista esce con
terreno, tracciato, kerb, vie di fuga, corsia box, griglia, quattro commissari per curva,
piazzola safety car, linea AI di bootstrap, `data/data.json` e `scene.json`. Quattro test
verdi (`geotrack_geo`, `_centerline`, `_strips`, `_generate`).

**Fase 2, prima metà**: `--ortho <png>` misura la larghezza dell'asfalto stazione per
stazione sull'immagine georeferenziata sul quadrato; la mediana sostituisce `--width` in
geometria e in `data.json`, salvo `--keep-width`. La prova end-to-end misura 14 m con un
apron dei box da 30 m presente, cioè proprio il caso che gonfiava il valore a 30–39 m.

**Fase 2, mosaico**: `--tiles cache/cache.json` legge la cache di `fetch_cache.py`,
georeferenzia ogni tile slippy-map sul piano ENU e la usa per entrambi i lavori — la
misura dell'asfalto e il fondo del terreno a chunk (`ground_<x>_<z>`, una mesh e un
albedo per chunk, una riga di `materials.txt` cadauno, `--chunk` per il lato). Un albedo
per mesh e' quello che il renderer legge oggi, quindi le tile arrivano a schermo senza
toccare il motore; il numero di chunk e' il primo livello di LOD.

**Resta**: LOD dei chunk a runtime nel motore (oggi il chunk piu' distante costa come
quello sotto la macchina) e correzione della centerline vettoriale con lo skeleton
dell'asfalto (`thinMask`, `stitchLap` sono ancora file-local in
`tools/trackmodules/TrackModulesCore.cpp`).
