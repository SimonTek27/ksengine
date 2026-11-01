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
- Il mosaico di tile e il piano LOD del fondo sono il passo successivo del
  design doc (`docs/superpowers/specs/2026-11-01-geotrack-generation-design.md`).
