# Piano di chiusura dei gap dei cfg

Inventario dei gap (status `partial`/`missing`) nei documenti di `system/cfg/`.
Aggiornato dopo la chiusura di windscreenEffects (4 stage), mirror (stage 3 + side tiles)
e busMix (audio).

## Cluster A — Audio (1 gap rimasto: 0 dopo busMix)
- busMix → live (audio.json aggiornato).

## Cluster B — Video / grafica (7 gap)
- bloomAndFlare (partial)
- ssgi (partial)
- screenSpaceReflections (partial)
- coloredShadows (partial)
- localLights (partial)
- vrSupport (partial)
- tripleScreen (missing)

Note: ssgi/ssr/coloredShadows/localLights dipendono da pass dedicati nel
NativeRenderer (deferred path). VR/tripleScreen richiedono refactor render loop.

## Cluster C — Controlli / input (3 gap)
- wheelLeds (partial)
- headTracking (partial)
- vrInput (partial)

## Cluster D — Gameplay / sessione / rete (molti)
sessionSetup, collisionRules, vehicleSystems, gameplayTweaks, sessionRules,
spectatorMode, chatBindings, chatEmoji (missing), voiceChat (missing),
spectatorView, dedicatedServer, udpCommandSemantics, careerStats, upgrades,
garageScene, brakeGlow, walkMode, surfaceEffects, pitService, replayLimits,
carFilters, paintableBody, teamSuits, teamHelmets, driverMaterials (missing),
standings, photoMode, windowStartup, hudWidgets, headerBar, devModeRow,
driverSuits, helmets, showrooms, contentUnpacking, standardContent,
carOverrides, materialMapping, contentManager.

## Cluster E — UI / contenuti (overlap con D)
 splash, creditsList, coreSwitches, devModeRow, ecc.

## Priorità suggerite
1. Cluster B (grafica) — gap visibili e verificabili con screenshot.
2. Cluster C (input) — headTracking/vrInput dipendono da hardware.
3. Cluster D/E — feature di gioco, molte già parzialmente implementate.

## Prossimo passo
Auditare un cluster alla volta: verificare lo stato reale del codice,
aggiornare il JSON se il codice è già live, oppure implementare il gap
mancante con test + smoke.
