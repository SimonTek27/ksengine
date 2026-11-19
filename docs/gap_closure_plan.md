# Piano di chiusura dei gap dei cfg

Inventario dei gap (status `partial`/`missing`) nei documenti di `system/cfg/`.
Aggiornato dopo la chiusura di windscreenEffects (4 stage), mirror (stage 3 + side tiles),
busMix (audio), ssgi, SSR e localLights (video).

## Stato attuale (aggiornato)

Cluster A — Audio: **chiuso** (busMix → live).

Cluster B — Video / grafica:
- bloomAndFlare (partial) — nessun codice; da implementare o marcare notApplicable.
- ssgi → **live** (gather+blur pipeline).
- screenSpaceReflections → **live** (ssrParams wired).
- coloredShadows (partial) — header presente, non cablato al runtime.
- localLights → **live** (NativeRenderer::setLocalLights + clustering).
- vrSupport (partial) — XrManager/XrInput presenti, da verificare con headset.
- tripleScreen (missing) — richiede refactor render loop.

Cluster C — Controlli / input:
- wheelLeds (partial) — HUD strip live, protocolli HW stub.
- headTracking (partial) — offset cockpit live, device input stub.
- vrInput (partial) — XrInput/VrInputAdapter cablati, da verificare con headset.

Cluster D — Gameplay / sessione / rete / UI / contenuti: ~40 gap (vedi ksengine.json).

## Priorità suggerite
1. Cluster B residuo: coloredShadows (cablare ColoredShadows al runtime) — piccolo, verificabile.
2. Cluster B: bloomAndFlare — implementare flare pass o marcare notApplicable.
3. Cluster C: wheelLeds protocolli HW (richiede device).
4. Cluster D: audit per feature (molti sono già live nel codice).

## Prossimo passo consigliato
Auditare un cluster alla volta: verificare lo stato reale del codice,
aggiornare il JSON se il codice è già live, oppure implementare il gap
mancante con test + smoke. Iniziare da coloredShadows (cluster B).
