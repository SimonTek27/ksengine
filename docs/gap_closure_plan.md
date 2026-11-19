# Piano di chiusura dei gap dei cfg

Inventario dei gap (status `partial`/`missing`) nei documenti di `system/cfg/`.
Aggiornato dopo la chiusura di windscreenEffects (4 stage), mirror (stage 3 + side tiles),
busMix, ssgi, SSR, localLights, headPhysics, brakeGlow, surfaceEffects, upgrades, pitService,
camera (free/chase/cockpit/cameraViews), coloredShadows, materialMapping, gameplayTweaks
(mechanicalFailures + damageAffectsDriving), bloomAndFlare (lens flare), settings.entries (ui),
picture (profile), merge rain/weatherShaders->weather, merge grass->terrain (poi separato).

## Stato attuale

Cluster A — Audio: **chiuso** (busMix → live).

Cluster B — Video / grafica:
- bloomAndFlare → **live** (ghosts + anamorphic streak).
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
materialMapping, spectatorMode, photoMode, carFilters) ma marcati partial per dettagli mancanti.
Gli altri richiedono implementazione ex novo.

## Gap chiusi in questa sessione (doc audit + implementazione)
headPhysics, brakeGlow, surfaceEffects, upgrades, pitService, camera (free/chase/cockpit/cameraViews),
busMix, ssgi, SSR, localLights, windscreenEffects, mirrors, coloredShadows, materialMapping,
gameplayTweaks (mechanicalFailures + damageAffectsDriving), bloomAndFlare (lens flare),
settings.entries (ui), profile.picture, merge weather/terrain.

## Priorità suggerite
1. Cluster B: vrSupport/tripleScreen (richiede hardware/refactor render loop).
2. Cluster C: wheelLeds protocolli HW (richiede device fisico).
3. Cluster D: audit feature-per-feature per i ~33 gap rimanenti.

## Nota
I gap rimanenti richiedono implementazione hardware (VR, triple screen, wheel LEDs)
o audit manuale feature-per-feature. Non sono chiudibili con sola documentazione.
Il prossimo step più piccolo e verificabile è implementare il supporto VR (render-to-headset).
