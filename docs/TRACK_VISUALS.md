# Piste senza geometria: cosa guarda il caricatore

Una pista può essere **solo dati** — AI, superficie, metadati — e va benissimo:
il gioco la carica, la sessione parte, i tempi si misurano. Quello che manca è
solo il disegno. Questo documento dice cosa cerca `applyTrackVisuals()`, quali
righe di log corrispondono ai tre casi, e perché una scena vuota non è un
errore.

## Ordine delle ricerche

Per una cartella pista il caricatore prova, in quest'ordine:

1. **Una cache cotta** (`findBakedManifestDir`, `src/simulator/SceneAssets.h`):
   `<cartella>/baked/manifest.txt`, altrimenti `<cartella>/manifest.txt`.
   Il primo formato è preferito perché `kn5baker` sceglie la cartella di
   uscita; entrambi si trovano in giro.
2. **Un `.kn5`**, solo per sapere se c'è qualcosa da cuocere.

Quando la cartella pista viene aperta (non un `.kn5` singolo), la scelta del
modello è: il file **con lo stesso nome della cartella**, altrimenti il più
grande (una pista AC contiene anche decorazioni e sotto-layout, e l'ordine di
directory aveva già scelto un deco da 7 MB al posto della pista da 406 MB).
Se la cartella radice non ne ha, si usa il `.kn5` del layout selezionato.

## Le tre righe di log

| situazione | riga |
| --- | --- |
| cache cotta presente | `[scene] anchored at the track spawn (...)` e i mesh della pista in scena |
| `.kn5` presente, nessuna cache | `[scene] track has .kn5 but no bake; run: kn5baker "<file>" <out>/baked` |
| né `.kn5` né cache | `[scene] <cartella> has no .kn5 and no baked cache: nothing to load, the current scene stays on screen` |

La seconda riga è un'istruzione, non un errore: c'è la geometria, manca la
conversione. La terza dice che non c'è nulla da convertire.

## Perché la scena non si svuota

Nei due casi senza cache il caricatore **non** azzera la scena: quella
precedente resta a schermo. È una garanzia di stabilità — cambiare pista non
deve mai produrre un frame nero — ma fino a ieri era silenziosa, e una vista
immobile senza spiegazione si diagnostica male. Le due righe sopra esistono
per questo.

## Il caso concreto: Mugello

`content/tracks/mugello` contiene:

```
ai/fast_lane.ai
data/data.json
layouts.json
```

Nessun `.kn5`, nessuna cache: la riga stampata all'avvio è

```
[scene] ...\content\tracks\mugello has no .kn5 and no baked cache: nothing to load,
        the current scene stays on screen
```

Il resto funziona: `[INIT] Track loaded: ...` riporta lunghezza 5245.0 m e un
layout, la sessione parte dalla linea AI (`[scene] no AC marker, spawn from the
AI line` quando mancano i marker AC) e l'auto viene posata lì. Per vedere la
pista serve il suo `.kn5`; per avere l'occlusione dello specchio (e ogni altra
prova visiva) serve la cache cotta, perché i mesh si disegnano solo da lì.

## Come si produce la cache

```
kn5baker "<pista>.kn5" <cartella>/baked
```

L'uscita va in `<cartella>/baked` (o dove si preferisce, purché il
`manifest.txt` finisca nella cartella o in una sua sottocartella `baked`).
