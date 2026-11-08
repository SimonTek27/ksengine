# Specchietto virtuale — dai due stage al culling per occhio

`video.mirrors` accende un tile posteriore renderizzato in un target proprio
(`video.mirrorResolution`, forzato orizzontale: `512x128` di default).
`KS_MIRROR=1` lo accende per una prova senza toccare la configurazione.
Tutto vive in `src/simulator/NativeRenderer.cpp`.

## Stage 1 — il target

Il tile è una scena completa, non una copia dello schermo:

| pezzo | dove |
| --- | --- |
| colore + profondità del tile | `ensureMirrorResources()` |
| pass di scena dal retro | `recordMirrorScene()` |
| quad sullo schermo | `recordMirrorOverlay()` |

La camera è la stessa dell'occhio principale ruotata di 180° sull'asse verticale
del frame di vista: funziona anche con camera inclinata o rollata e, essendo una
rotazione, non cambia la mano del sistema (lo stato di culling delle pipeline
resta valido). Il quad applica il flip orizzontale, perché uno specchio scambia
davvero destra e sinistra.

Il tile viene saltato su un backbuffer HDR10: i suoi valori SDR, non codificati,
brillerebbero contro l'immagine PQ.

## Stage 2 — occhio, lista e frustum propri

Il tile non riusa più la lista scelta dalla camera principale.

- `beginFrame()` costruisce `m_mirrorViewFrame` / `m_mirrorProjFrame` e
  `m_mirrorFrustum` accanto a quelli principali. La focale verticale resta
  quella principale, l'orizzontale si riscala all'aspetto del tile
  (`focalY * h / w`): tile largo, vista larga.
- `drawMesh()` riempie `m_mirrorList` **prima** di qualsiasi scarto
  dell'occhio principale. Quello che la camera ha dietro le spalle è esattamente
  ciò che lo specchio deve mostrare.
- `recordMirrorScene()` disegna da quella coppia di matrici già testata, mai da
  un ricalcolo: ciò che è stato cullato è ciò che viene rasterizzato.
- La finestra LOD autoriale resta condivisa: misura distanza dall'occhio, e
  l'occhio del tile è lo stesso punto (pura rotazione).
- Le dimensioni del tile vengono da `resolvedMirrorSize()`, la stessa funzione
  che costruisce il target: un cambio di risoluzione in sessione non può
  lasciare il frustum su un aspetto e il pass su un altro.

## Occlusione — una griglia per occhio

La griglia principale è un max-pool 8×8 della profondità proiettato nello
schermo della camera principale. Stessa posizione dell'occhio, mappatura
completamente diversa: non dice nulla su cosa il tile possa saltare, quindi il
tile non la usa (né in positivo né in negativo).

Il tile ha la sua:

| pezzo | nota |
| --- | --- |
| target, framebuffer, descriptor set | propri; pass, pipeline, layout, sampler e pool sono condivisi |
| readback | 4 KB per un tile 512×128 (64×16 tile) |
| profondità del tile | il pass specchio la conserva (`STORE`); prima era `DONT_CARE` |
| proiezione del test | `m_prevMirrorViewProj`, accanto a `m_prevViewProj` |
| linearizzazione | riusa `m_occlR22/R23`: il tile differisce dalla principale solo in riga 0 |

Come per la griglia principale c'è un frame di latenza: la griglia copiata alla
fine di un frame si consuma nel `beginFrame()` successivo, e il test proietta con
le matrici con cui era stata rasterizzata.

Le due griglie sono indipendenti: se quella del tile non si costruisce, il
culling principale continua e il tile si limita al frustum.

## Cosa si vede nel log

```
[NativeRenderer] mirror: rear view target 512x128 built
[NativeRenderer] mirror occlusion grid 64x16 tiles @ 512x128 depth
[render] 165.4 fps submitted=2 drawn=2 culled=0 occluded=0 mirror=1 mirrorOccl=0
```

`mirror=` è la dimensione della lista che il pass posteriore ha davvero
riprodotto; `mirrorOccl=` quanti di quelli la griglia del tile ha scartato prima
di finire in lista. In una scena senza occludenti il secondo resta a zero: è il
caso corretto, non un guasto.

## Cosa non copre

- Il tile non campiona il vetro di una mesh specchio: lo stage 1 è un quad sullo
  schermo, non un riflesso agganciato all'oggetto.
- Nessun tile laterale o destro/sinistro separato.
- L'occlusione del tile ha la stessa latenza di un frame della principale, e la
  stessa griglia a 8×8 pixel: oggetti più stretti di un tile possono sfuggire.
- Il pass posteriore usa la pipeline forward anche quando il percorso principale
  è deferred, quindi vetri e superfici blend non ci arrivano traslucide.
