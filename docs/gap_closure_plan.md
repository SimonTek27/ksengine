# Piano di chiusura dei gap dei cfg

Inventario dei gap (status `partial`/`missing`) nei documenti di `system/cfg/`.
Aggiornato dopo la chiusura di windscreenEffects (4 stage), mirror (stage 3 + side tiles),
busMix, ssgi, SSR, localLights, headPhysics, brakeGlow, surfaceEffects, upgrades, pitService,
camera (free/chase/cockpit/cameraViews), coloredShadows, materialMapping, gameplayTweaks
(mechanicalFailures + damageAffectsDriving).

## Stato attuale

Cluster A — Audio: **chiuso** (busMix → live).

Cluster B — Video / grafica:
- bloomAndFlare (partial) — bloom live, lens flare non implementato.
- ssgi → **live**.
- screenSpaceReflections → **live**.
- coloredShadows → **live**.
- localLights → **live**.
- vrSupport (partial) — codice presente, da verificare con headset.
- tripleScreen (missing) — richiede refactor render loop.

Cluster C — Controlli / input:
- wheelLeds (partial) — HUD strip live, protocolli HW stub.
- headTracking (partial) — offset cockpit live, device input stub.
- vrInput (partial) — XrInput/VrInputAdapter cablati, da verificare con headset.

Cluster D — Gameplay / sessione / rete / UI / contenuti: ~33 gap.
Molti sono già implementati nel codice (gameplayTweaks, surfaceEffects, standings, showrooms,
materialMapping, spectatorMode, photoMode) ma marcati partial per dettagli mancanti.
Gli altri richiedono implementazione ex novo.

## Gap chiusi in questa sessione (doc audit + implementazione)
headPhysics, brakeGlow, surfaceEffects, upgrades, pitService, camera (free/chase/cockpit/cameraViews),
busMix, ssgi, SSR, localLights, windscreenEffects, mirrors, coloredShadows, materialMapping,
gameplayTweaks (mechanicalFailures + damageAffectsDriving), settings.entries (aggiunto "ui").

## Priorità suggerite
1. Cluster B: bloomAndFlare lens flare (implementare flare pass) — medio, verificabile con screenshot.
2. Cluster D: audit feature-per-feature per i ~33 gap rimanenti.
3. Cluster C: wheelLeds protocolli HW (richiede device fisico).
4. Cluster B: vrSupport/tripleScreen (richiede hardware/refactor).

## Nota
I gap rimanenti richiedono implementazione (lens flare, mechanicalFailures done, VR runtime, ecc.)
o audit manuale feature-per-feature. Non sono chiudibili con sola documentazione.
Il prossimo step più piccolo e verificabile è implementare il lens flare (bloomAndFlare).
