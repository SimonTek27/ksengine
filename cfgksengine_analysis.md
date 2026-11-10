# Analisi KSEngine - Feature Partial e Missing

## Sommario Esecutivo
- **Schema**: 1
- **Total Lines**: 965
- **Feature Partial**: 39
- **Feature Missing**: 22

---

## FEATURE PARTIAL (39)

### VIDEO (12)
1. **bloomAndFlare**
   - Status: `partial`
   - Lands As: "bloom (bright pass + separable blur)"
   - Note: "Bloom is un real half-resolution pass - glare extract, blur H, blur V - feeding the display pass. Occlusion-driven lens flare is not implemented."

2. **ssgi**
   - Status: `partial`
   - Lands As: "half-res gather + gaussian denoise"
   - Note: "The bounce is gathered at half resolution by ssgiGather.frag into its own target and denoised with two gaussian axes before the deferred lighting pass samples it as binding 8 - that filter is what keeps a wide video.ssgiRadius smooth. video.ssgi turns it on (and switches the render path to deferred, because the forward path never runs that shader, off by default); video.ssgiRadius and video.ssgiIntensity tune footprint and strength. The two engine-side ssgi compute shaders build but nothing dispatches them."

3. **screenSpaceReflections**
   - Status: `partial`
   - Lands As: "ssr"
   - Note: "Screen-space reflections exist; planar and mirror reflections do not."

4. **mirrors**
   - Status: `partial`
   - Lands As: "mirrors, mirrorResolution"
   - Note: "video.mirrors turns on a rear-view tile rendered to its own colour+depth target every frame with the real forward draw list (the AC virtual mirror idea), and the tile draws inside whichever colour pass writes the swapchain — skipped on an HDR10 backbuffer, where its un-encoded SDR values would glow against the PQ image. video.mirrorResolution is the tile size, forced landscape ('512x128' here). The rear view queues its own draws: it has its own frustum (the main camera yawed 180 about its up axis) and its own occlusion grid, so a mesh the main eye..."

5. **colorGrading**
   - Status: `partial`
   - Lands As: "postProcessFilter"
   - Note: "The native display pass applies exposure, the tone curve and the ppfilter LUT; no external post-processing middleware is involved."

6. **headPhysics**
   - Status: `partial`
   - Lands As: "cockpit head spring"
   - Note: "Longitudinal and lateral acceleration sway the cockpit head through a spring-damper; vertical suspension travel is not fed in yet."

7. **freeCamera**
   - Status: `partial`
   - Lands As: "orbit camera"
   - Note: "A free orbit camera exists for the showroom and the menu backdrop."

8. **chaseCamera**
   - Status: `partial`
   - Lands As: "chase camera"
   - Note: "Chase and cockpit views exist, without spring or offset tuning."

9. **cameraViews**
   - Status: `partial`
   - Lands As: "chase / cockpit cameras"
   - Note: "Two views exist and switch; its per-camera bookmarks are not read."

10. **cockpitCamera**
	- Status: `partial`
	- Lands As: "cockpit view"
	- Note: "Onboard camera is fixed to the car's cockpit node; no FOV or offset tuning."

### AUDIO (1)
11. **channelLevels**
	- Status: `partial`
	- Lands As: "master"
	- Note: "Master is a real setting. Engine, wind, tyre and opponent levels are driven per frame by the session, so they are not exposed as preferences."

### CONTROLS (3)
12. **keyboardBindings**
	- Status: `partial`
	- Lands As: "keyboard rebind page"
	- Note: "Keyboard only: nine actions (throttle, brake, both steering directions, both shifts, handbrake, clutch, starter), each captured and swapped so two actions never share a key; the clutch is a pedal level and the starter is a rising edge, so holding the key does not crank again. Arrow keys stay reserved."

13. **gamepadInput**
	- Status: `partial`
	- Lands As: "DirectInput joystick, default gamepad mapping"
	- Note: "InputManager polls the DirectInput stick against the built-in gamepad mapping; there is no screen to remap it and the log reports when acquiring the device fails."

14. **actionProps**
	- Status: `partial`
	- Lands As: "action labels"
	- Note: "Partially implemented: see landsAs for the part that exists."

15. **hotkeyList**
	- Status: `partial`
	- Lands As: "global hotkeys"
	- Note: "Partially implemented: see landsAs for the part that exists."

### SINGLEPLAYER (3)
16. **sessionSetup**
	- Status: `partial`
	- Lands As: "session setup"
	- Note: "Car and track come from the chosen content; the MODE SETUP screen sets the laps, the AI cars, the session clock and whether the player starts in the garage. AI level and a fixed setup are not exposed."

17. **driverAids**
	- Status: `partial`
	- Lands As: "assist.tc, assist.abs"
	- Note: "Applied from the setup at start-up only."

18. **collisionRules**
	- Status: `partial`
	- Lands As: "physics flags"
	- Note: "Car collisions are always on; the collision radius is fixed."

19. **vehicleSystems**
	- Status: `partial`
	- Lands As: "vehicle model"
	- Note: "Gearbox, clutch and tyre model are the engine's own; the extra systems are not ported."

### MULTIPLAYER (2)
20. **sessionRules**
	- Status: `partial`
	- Lands As: "session rules"
	- Note: "No password, slot list or booking."

21. **chatBindings**
	- Status: `partial`
	- Lands As: "chat"
	- Note: "Partially implemented: see landsAs for the part that exists."

22. **dedicatedServer**
	- Status: `partial`
	- Lands As: "host row"
	- Note: "kssimserver.exe ships next to the simulator; there is no in-menu manager."

### PROFILE (1)
23. **careerStats**
	- Status: `partial`
	- Lands As: "PROFILE stats block"
	- Note: "Career totals are kept and written to stats.json; there is no championship progression."

### GARAGE (4)
24. **upgrades**
	- Status: `partial`
	- Lands As: "upgrade rows"
	- Note: "Upgrade packages cycle and apply; there is no per-parameter setup screen."

25. **garageScene**
	- Status: `partial`
	- Lands As: "garage view"
	- Note: "The showroom is a real scene (content/showrooms/industrial)."

26. **brakeGlow**
	- Status: `partial`
	- Lands As: "brake glow"
	- Note: "Discs glow with temperature from the brake thermal model. The car's own [BRAKEDISC_FX] sections scale it: GLOW_MULT multiplies the value and ACTIVE = 0 turns it off, both read from the model document at car load. Sparks: one emitter per wheel backs the glow - a disc past 600 C shedding under near-lock braking at road speed throws white-hot crumbs, rate scaled by how far past that anchor the disc is (RainEffects.h, same particle set as the skid marks)."

27. **walkMode**
	- Status: `partial`
	- Lands As: "leave garage / return to box"
	- Note: "Pit lane phases exist; the driver does not walk."

28. **surfaceEffects**
	- Status: `partial`
	- Lands As: "track surface grip"
	- Note: "Surface names map to friction; per-surface effects are not rendered."

### PIT STRATEGY (1)
29. **pitService**
	- Status: `partial`
	- Lands As: "REQUEST SERVICE"
	- Note: "The service runs when the car is stopped in the box; tyre compound choice is not implemented."

### REPLAY (1)
30. **replayLimits**
	- Status: `partial`
	- Lands As: "maxSizeMb, qualityLevel, autosave"
	- Note: "Size and sampling rate are read as documented defaults; autosave counts are not enforced per session type."

### CAR SELECT (3)
31. **carFilters**
	- Status: `partial`
	- Lands As: "CARS page"
	- Note: "Scrolling list with the car name; brand, class and year come from <car>/ui_car.json and each filter row exists only when at least one car declares that field. A folder with no metadata is listed by its folder name."

32. **paintableBody**
	- Status: `partial`
	- Lands As: "body material"
	- Note: "Bodywork maps to the CARBODY material; paintable regions are not declared per car."

33. **oemColorsModern**
	- Status: `live`
	- Lands As: "colours list of ui_car.json paints the bodywork"
	- Note: "colors[] in ui_car.json: the first is applied when the car loads, the CARS page cycles the rest. The tint multiplies the albedo of the meshes the presets classify as paint, so the texture detail stays. Channels above 1 are read as 0..255 and scaled."

### RESULTS (1)
34. **standings**
	- Status: `partial`
	- Lands As: "results rows"
	- Note: "No export, no championship points."

### PAUSE (1)
35. **photoMode**
	- Status: `partial`
	- Lands As: "photo mode"
	- Note: "A photo mode with a free camera exists; the display pass applies the preset's colour grading, its configurable bloom strength and the real depth of field from the depthOfField block (focal length, aperture, metering), and F12 toggles it unless KS_PHOTOMODE names the mode itself."

### HOME (3)
36. **windowStartup**
	- Status: `partial`
	- Lands As: "window title and startup section"
	- Note: "No launcher page: the simulator opens on the tiles."

37. **hudWidgets**
	- Status: `partial`
	- Lands As: "header bar"
	- Note: "The header bar (clock, notifications, quick actions) is the part with a counterpart; no widget list."

38. **headerBar**
	- Status: `partial`
	- Lands As: "header bar"
	- Note: "Partially implemented: see landsAs for the part that exists."

### DEV MODE (1)
39. **devModeRow**
	- Status: `partial`
	- Lands As: "DEV MODE row"
	- Note: "The row opens a confirmation screen; the mode itself only logs."

### CONTENT (2)
40. **showrooms**
	- Status: `partial`
	- Lands As: "garage scene"
	- Note: "One showroom is installed; its preview grid does not exist."

41. **standardContent**
	- Status: `partial`
	- Lands As: "content scan"
	- Note: "Partially implemented: see landsAs for the part that exists."

42. **carOverrides**
	- Status: `partial`
	- Lands As: "model.json and driver.json overrides"
	- Note: "The content's car-wide ext_config.ini, whether at the car root or in extension/, is folded into the car's documents by the converter: hidden_nodes and driver_model are applied, a skin's own file goes to skins/<id>/skin.json, and every other section is kept verbatim under sections. Of those sections the runtime reads one today, BRAKEDISC_FX (ACTIVE, GLOW_MULT); the rest describe a renderer this engine does not have and stay as content, not as guesses."

---

## FEATURE MISSING (22)

### VIDEO (2)
1. **vrSupport**
   - Status: `missing`
   - Lands As: `null`

2. **tripleScreen**
   - Status: `missing`
   - Lands As: `null`
   - Note: "One window, one swapchain: no triple-head splitting or side-angle correction."

### CONTROLS (3)
3. **wheelLeds**
   - Status: `missing`
   - Lands As: `null`

4. **headTracking**
   - Status: `missing`
   - Lands As: `null`

5. **vrInput**
   - Status: `missing`
   - Lands As: `null`

### SINGLEPLAYER (1)
6. **gameplayTweaks**
   - Status: `missing`
   - Lands As: `null`

### MULTIPLAYER (5)
7. **spectatorMode**
   - Status: `missing`
   - Lands As: `null`

8. **chatEmoji**
   - Status: `missing`
   - Lands As: `null`

9. **voiceChat**
   - Status: `missing`
   - Lands As: `null`

10. **spectatorView**
	- Status: `missing`
	- Lands As: `null`

### GARAGE (2)
11. **carInstruments**
	- Status: `missing`
	- Lands As: `null`

12. **setupCatalog**
	- Status: `missing`
	- Lands As: `null`

### CAR SELECT (3)
13. **oemColorsVintage**
	- Status: `missing`
	- Lands As: `null`
	- Note: "No separate vintage palette: the single colors[] list above is all the choice a car folder offers today."

14. **carData**
	- Status: `missing`
	- Lands As: `null`

### TEAM SELECT (3)
15. **teamSuits**
	- Status: `missing`
	- Lands As: `null`

16. **teamHelmets**
	- Status: `missing`
	- Lands As: `null`

17. **driverMaterials**
	- Status: `missing`
	- Lands As: `null`

### HOME (1)
18. **splash**
	- Status: `missing`
	- Lands As: `null`

### CREDITS (1)
19. **creditsList**
	- Status: `missing`
	- Lands As: `null`

### DEV MODE (6)
20. **coreSwitches**
	- Status: `missing`
	- Lands As: `null`
	- Note: "The runtime is its own engine: there are no core switches to inherit."

21. **scripting**
	- Status: `missing`
	- Lands As: `null`

22. **moduleLoader**
	- Status: `missing`
	- Lands As: `null`

23. **appPausePolicy**
	- Status: `missing`
	- Lands As: `null`

24. **virtualTyres**
	- Status: `missing`
	- Lands As: `null`

25. **timeLimitedSession**
	- Status: `missing`
	- Lands As: `null`

### CONTENT (5)
26. **driverSuits**
	- Status: `missing`
	- Lands As: `null`

27. **helmets**
	- Status: `missing`
	- Lands As: `null`

28. **skins**
	- Status: `missing`
	- Lands As: `null`

29. **contentUnpacking**
	- Status: `missing`
	- Lands As: `null`
	- Note: "Packaging::unpack is a stub that returns false: content is read in place from its folder (and from .zip reads done at load time), never extracted to disk."

30. **trackTweaks**
	- Status: `missing`
	- Lands As: `null`

31. **contentManifest**
	- Status: `missing`
	- Lands As: `null`

32. **materialMapping**
	- Status: `missing`
	- Lands As: `null`

---

## Raggruppamento per Area

### Video Graphics (14)
- **Partial**: bloomAndFlare, ssgi, screenSpaceReflections, mirrors, colorGrading, headPhysics, freeCamera, chaseCamera, cameraViews, cockpitCamera
- **Missing**: vrSupport, tripleScreen

### Audio & Input (4)
- **Partial**: channelLevels, keyboardBindings, gamepadInput, actionProps, hotkeyList
- **Missing**: wheelLeds, headTracking, vrInput

### Session Management (9)
- **Partial**: sessionSetup, driverAids, collisionRules, vehicleSystems, sessionRules, chatBindings, dedicatedServer, pitService
- **Missing**: gameplayTweaks, spectatorMode, chatEmoji, voiceChat, spectatorView

### Content & Customization (14)
- **Partial**: upgrades, garageScene, brakeGlow, walkMode, surfaceEffects, carFilters, paintableBody, oemColorsModern, standings, photoMode, showrooms, standardContent, carOverrides
- **Missing**: carInstruments, setupCatalog, oemColorsVintage, carData, teamSuits, teamHelmets, driverMaterials, driverSuits, helmets, skins, contentUnpacking, trackTweaks, contentManifest, materialMapping

### UI & Navigation (8)
- **Partial**: windowStartup, hudWidgets, headerBar, devModeRow, replayLimits
- **Missing**: splash, creditsList, coreSwitches, scripting, moduleLoader, appPausePolicy, virtualTyres, timeLimitedSession

### Profile & Stats (1)
- **Partial**: careerStats
- **Missing**: -

---

## Raccomandazioni per il Completamento

### Priorità Alta (Core Functionality)
1. **ssgi Compute Shaders** - Completa i compute shader SSGI (richiesti per la qualità grafica)
2. **Mirror Rendering** - Implementa mirror reflections in aggiunta a SSR
3. **Setup Catalog** - Sistema di setup personalizzati (importante per grigio garageGarage)
4. **Tyre Compound Choice** - Completare la scelta del composto in pit strategy

### Priorità Media (User Experience)
1. **VR Support** - Importante per pubblico entusiasta
2. **Head Tracking** - Migliora l'immersione
3. **Photo Mode Enhancements** - Completare con maggiori opzioni
4. **Content Unpacking** - Supporto archivi zip

### Priorità Bassa (Nice to Have)
1. **Triple Screen Support** - Support multi-monitor specializzato
2. **Team Customization** - Suit e helmet personalizzati
3. **Championship Progression** - Sistema campionato completo
4. **Voice Chat** - Comunicazione multiplayer

---

## Extra Considerations

- **Total Schema Version**: 1
- **Config Files Locations**:
  - System: `system/cfg/ksengine.json`
  - User Overrides: `user/<player>/settings.json`
  - Replay Storage: `user/<player>/replay`
  - Controls: `user/<player>/controls.json`
  - Stats: `user/<player>/stats.json`

---

*Analisi completata: 2024*
*File configurazione analizato: E:\Users\Simon\source\repos\ksengine\system\cfg\ksengine.json*
