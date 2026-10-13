# Track data JSON nativo (ksim)

Manifest leggero in `<track>/data/data.json`, letto da
`TrackDataJsonLoader` in `loadTrackFolder` **prima** del fallback
`surfaces.ini`.

## Loader attuale

Header: `src/adapters/assetto_corsa/TrackDataJsonLoader.h`  
Parser minimal (string-scan, come `SetupJson.h`) — **nessuna** dipendenza JSON DOM.

Chiavi top-level usate oggi:

| Chiave | Tipo | Uso |
|--------|------|-----|
| `name` | string | Nome pista (menu / HUD) |
| `length` | number | Lunghezza giro in metri → `m_trackData.splineLength` |
| `width` | number | Larghezza pista (opzionale, riservata) |

Se `name` è vuoto **e** `length ≤ 1`, il loader fallisce e si usa `surfaces.ini`.

## Schema esteso (opzionale, non ancora consumato dal runtime)

Il file di esempio Mugello può portare anche superfici, sezioni, DRS, camere
(`length_m`, `surfaces`, `layout.sections`, …) per tool futuri. Il loader
corrente ignora le chiavi che non conosce.

Esempio minimo sufficiente:

```json
{
  "name": "Mugello",
  "length": 5245,
  "width": 12.0
}
```

Esempio di riferimento (esteso): `content/tracks/mugello/data/data.json`.

## Ordine in `loadTrackFolder`

1. AI spline (`fast_lane.ai`)
2. **`data/data.json`** se presente e valido
3. altrimenti **`data/surfaces.ini`**
4. `.kn5` + bake visuals

## Test

```text
KS_TRACK=content/tracks/mugello
```

Con il JSON presente: `m_trackData.name = "Mugello"`, `splineLength = 5245`.
