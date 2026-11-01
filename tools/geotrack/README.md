# geotrack

Genera una cartella pista giocabile da un tracciato geografico: quadrato di
terreno delle dimensioni dell'ingombro, tracciato, kerb, vie di fuga, corsia
box, griglia, commissari e piazzola safety car.

## Uso

```bash
# 1) centerline: .csv "lat,lon" per riga, .geojson LineString o .gpx
ks_geotrack giro.csv out/miapista --name "Mia Pista" --width 12 \
             --pit 200,600,10,4

# con un fondo gia' preparato (PNG, copre il quadrato una volta)
ks_geotrack giro.csv out/miapista --ground-texture satellite.png

# fase 2: misura la larghezza reale dell'asfalto dall'ortofoto e la usa
ks_geotrack giro.csv out/miapista --ortho satellite.png

# misura ma tieni la larghezza dichiarata (--keep-width)
ks_geotrack giro.csv out/miapista --ortho satellite.png --width 12 --keep-width

# mosaico di tile gia' in cache: misura + fondo a chunk
ks_geotrack giro.csv out/miapista --tiles cache/cache.json --chunk 300

# riallinea la centerline sul centro dell'asfalto (raggio di ricerca 8 m)
ks_geotrack giro.csv out/miapista --tiles cache/cache.json --correct 8

# 2) cache di vettoriale e tile (opzionale, richiede rete)
python3 tools/geotrack/fetch_cache.py cache --bounds 50.32,6.93,50.35,6.98 \
        --overpass --tile-url "https://<provider>/{z}/{x}/{y}.png" --zoom 18
```

`fetch_cache.py` non ha provider cablati: l'URL delle tile e la chiave, se serve,
li mette l'utente. Il binario C++ non fa rete.

## Cosa produce

```
<miapista>/
  baked/manifest.txt          una riga per layer (ground, track, kerbs,
                              runoff, pitlane, griddot, a seconda delle opzioni)
  baked/<layer>.nmsh          mesh nel formato del renderer
  baked/materials.txt         solo con --ground-texture
  baked/textures/ground.png   solo con --ground-texture
  data/data.json              name, length, width, pitboxes, corner_count,
                              marshal_posts (mesh marshall_A_HI_010), safety_car
  ai/fast_lane.ai             linea AI di bootstrap (non una racing line)
  scene.json                  bounds geografici, lato del quadrato, layer
```

La cartella si carica senza `.kn5`: `SimulationLoop::loadTrack` accetta una
pista manifest-only. I livelli sono a quote diverse (terreno 0, runoff 0.01,
asfalto 0.02, kerb 0.03, griglia 0.06) per evitare lo z-fighting.

## Note

- Il quadrato e' il bounding box del tracciato piu' `--pad` metri per lato, reso
  quadrato sul lato maggiore.
- Senza `--ground-texture` il fondo resta al materiale di default: la pista e'
  completa e guidabile, manca solo l'immagine.
- `--ortho` vuole un'immagine che copra lo stesso quadrato del terreno
  (riga 0 = nord). La misura e' perpendicolare alla centerline, stazione per
  stazione, e il valore che finisce in `data.json` e' la **mediana**: l'apron dei
  box puo' allargare qualche campione senza spostare la larghezza del tracciato.
  `scene.json` riporta `measured_width_m`, `width_samples` e il file usato.
- I chunk del fondo sono scritti in **NMS2** con la finestra di distanza
  (`--chunk-view`, default 1400 m): il renderer la legge già (`LodWindow`) e non
  disegna i chunk oltre il limite, quindi il LOD non richiede modifiche al motore
  né un formato nuovo. Gli altri layer restano NMSH, che non ha la finestra.
  `--chunk-view 0` disattiva la finestra.
- `--tiles` legge `cache.json` (zoom + elenco tile) e georeferenzia ogni tile sul
  piano locale: la stessa immagine serve a misurare l'asfalto e a texturizzare il
  fondo. Il fondo esce a chunk (`ground_<x>_<z>`), una mesh e un albedo per chunk,
  perche' il renderer ha un albedo per mesh: `--chunk` decide il lato e quindi il
  primo livello di LOD.
- `--correct [M]` rasterizza l'asfalto a 1 m/px sul quadrato, lo assottiglia a
  scheletro (Zhang-Suen) e sposta ogni stazione sul punto di scheletro più vicino
  entro il raggio (default 6 m). Il numero di stazioni non cambia: si corregge la
  posizione, non la topologia. Lo spostamento medio finisce in
  `scene.json` (`centerline_shift_m`) e a video.
- Resta da fare, del design doc
  (`docs/superpowers/specs/2026-11-01-geotrack-generation-design.md`): LOD dei
  chunk a runtime nel motore.
