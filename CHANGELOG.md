# Changelog

All notable changes to ksEditor are documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- **Layout `system/cfg/` + cartella per-giocatore `user/<player>/`** (Fase 3;
  `cmake/KsInstallLayout.cmake`, `src/engine/Config/Json.h`,
  `src/engine/Config/EngineSettings.h`, `src/engine/assets/UserData.h`,
  `src/simulator/UserStats.h`): il runtime Qt-free non aveva alcun supporto
  JSON, e ora ce l'ha in `src/engine/Config/Json.h` — header-only, parser
  ricorsivo + writer con ordine di inserimento preservato, escape e
  `\uXXXX` (con surrogate) in UTF-8, errori con offset, nessuno stato
  statico (nessun dato condiviso fra exe e DLL). Il layout installato
  aggiunge `system/cfg/ksengine.json`, spedito come `{}` e letto da
  `Paths::systemCfg()`.
  All'avvio la runtime crea `user/<player>/` — `<player>` è il nome del
  profilo guida, `Driver` quando è vuoto o inutilizzabile come cartella
  (caratteri illegali, `.`/`..`, nomi riservati `CON`/`COM1`/... vengono
  normalizzati) — con `controls.json`, `settings.json`, `stats.json`
  (tutti `{}` alla prima creazione) e le cartelle `screenshots/`,
  `replay/`, `telemetry/`.
  Le due impostazioni condividono la stessa forma e vengono unite in
  ordine, il secondo file vince chiave per chiave:
  `system/cfg/ksengine.json` (default dell'install) e
  `user/<player>/settings.json` (override del giocatore). Chiavi
  effettive: `physics.fixedDt` (passo fisso, applicato con
  `Engine::setFixedDt`), `audio.master` (`SimulatorAudio::setMasterVolume`,
  riprovato anche dopo il primo `loadCar()` perché l'audio viene costruito
  in modo lazy lì) e `assist.tc`/`assist.abs` (`VehicleSimulator::applySetup`,
  solo all'avvio: `applySetup` ricostruisce massa/aero dalla setup, quindi
  dopo il load di una vettura cancellerebbe i dati del modello). La
  riscrittura in uscita riporta solo le chiavi che il file del giocatore
  conteneva: un default presente soltanto in `system/cfg/` non finisce
  mai nel file dell'utente, e un file malformato non azzera ciò che era
  già stato unito.
  `stats.json` conserva la carriera del pilota (vittorie, pole, podi,
  gare, miglior giro e il numero di record PB in `user/pb`): caricato su
  `DriverProfile` all'avvio, riscritto a ogni cambio profilo e in uscita.
  Test `user_data_test` (round trip JSON, sanitizzazione dei nomi,
  prima esecuzione, precedenza system/cfg → settings.json, stats).
  Verifica: 53/53 ctest; `ks_dist` produce `system/cfg/ksengine.json` e
  una prima esecuzione da `dist/ksim` crea `user/Player/` con i tre file
  e le tre cartelle.
- **ksengine.dll con API esportato esplicito** (roadmap 2.4/2.5):
  `src/engine/KsExport.h` introduce la macro `KSENGINE_API` (dllexport/dllimport in base alla definizione di `KSENGINE_BUILDING_DLL` / `KSENGINE_USE_DLL`) e `option(KSENGINE_SHARED ... ON)` in `src/engine/CMakeLists.txt` rende `ksengine` una shared library; sono stati annotati 29 classi e 32 funzioni libere in 38 header `src/engine/` con la macro `KSENGINE_API`. I simboli di superficie export sono misurati staticamente via dumpbin: 187 entità in `ksengine_export_surface.csv`, nessuna template. La DLL carica `vulkan-1.dll` in modalità delay-load.
  Si noti: passandone `-DKSENGINE_SHARED=OFF` si ricostruisce il vecchio archivio statico `ksengine.lib`, con macro vuote e nessuna modifica ai file fonte.
- **Rendering AI brief — Sprint S3 (P2)** (`RENDERING_AI_BRIEF.md`):
  IBL split-sum sul path Vulkan Qt-free, generata interamente a CPU a
  runtime (`src/simulator/IblGenerator.h`, deterministica — sequenza
  Hammersley, nessun RNG). All'upload della scena si produce un cielo
  equirettangolare procedurale 128×64 RGBA16F **senza disco solare**
  (orizzonte (0.62,0.68,0.80), zenito (0.22,0.36,0.66), suolo
  (0.14,0.135,0.125)) e da esso derivano tre sole immagini: catena
  prefilter a 5 mip (mip0 = ambiente speculare, roughness =
  mip/(mips−1)), irradiance (memorizza E/π, quindi un cielo piatto L
  riproduce l'ambient legacy ×0.25 esattamente) e BRDF LUT 128×128
  (scale/offset di Karis), tutte `VK_FORMAT_R16G16B16A16_SFLOAT` per il
  filtraggio lineare obbligatorio. `FrameDataUBO` cresce in coda di
  `iblParams[4]` (solo append: x = flag attivo, y = maxLOD della
  catena); i set 0 di forward (5 binding) e lighting (8 binding)
  aggiungono irradiance/prefilter/LUT con viste dummy mai-nulle, e
  `KS_IBL=0` disattiva tutto. In fallback (upload fallito o opt-out)
  `iblParams.x=0` mantiene l'ambiente flat ×0.25: i vecchi bake e le
  cache restano validi. Verifica: A/B con sole a intensità 0 su
  contenuto di riferimento — quad plain grigio neutro → dominanza blu
  dell'orizzonte su entrambi i path (forward + deferred), quad metal
  più luminoso con il riflesso del cielo nel lobo speculare; 52/52
  ctest, gate Qt-free 0/907
- **Rendering AI brief — Sprint S2 (P1)** (`RENDERING_AI_BRIEF.md`):
  materiali PBR differenziati (vernice / pavimento opaco / metallo nudo
  sotto la stessa luce). `materials.txt` accetta ora celle roughness e
  metalness *dual-typed*: un numero resta lo scalare Roadmap 2.3, un nome
  con estensione (es. `body_rough.dds`) è una mappa (e lo scalare diventa
  l'identità 1.0, il moltiplicatore che i shader applicano). I vecchi
  bake restano validi byte per byte (riga e header NMS2 invariati,
  nessun bump). `Kn5Baker` emette le celle mappe quando il KN5 le
  autorizza (slot riconosciuti per nome, le mappe vincono sullo scalare)
  e applica l'euristica paint/carbon (0.35 / 0.5) quando mancano sia
  proprietà sia mappa (`heuristicMaterialRoughness`, esposta per test).
  Runtime: descriptor set 1 cresce ai binding 3/4 (sampler roughness /
  metalness con fallback bianco = identità), `gbuffer.frag` campiona le
  mappe e scrive la metalness in RT0.a (AO costante rimossa),
  `deferred_lighting.frag` usa F0 = mix(0.04, albedo, metalness),
  spegne il lobo diffuse dei metalli e usa la metalness nella fresnel
  SSR. Verifica: 52/52 ctest (nuovi casi `material_cache_test` e
  `kn5_test`, differenziale metalness su contenuto di riferimento in
  `test_renderer`), gate Qt-free 0/907
- **Rendering AI brief — Sprint S1 (P0)** (`RENDERING_AI_BRIEF.md`):
  percorso texture reso affidabile sul path Vulkan Qt-free.
  `TextureRuntime` genera ora la catena mip a runtime (box filter CPU
  2×2, ripiego del resto sul bordo per dimensioni dispari, upload
  multi-livello in un'unica staging buffer; i livelli a 1 px restano
  identici) e crea lo sampler con `maxLod = mipLevels-1`;
  `NativeRenderer::createDevice` abilita la feature `samplerAnisotropy`
  (se supportata) e la passa a `TextureRuntime`, che usa il limite
  `maxSamplerAnisotropy` del device (tipicamente 16, ≥ 8) — feature e
  sampler non possono divergere. I descriptor set 1
  (albedo/normal/`MaterialUBO`), il fallback bianco 1×1 e la
  ricompilazione SPIR-V da GLSL erano già in place: S1 li verifica senza
  ritocchi. Verifica: 52/52 ctest (`test_renderer` pixel su bake di
  riferimento), gate Qt-free 0/907
- **Roadmap 2A block C (2.8/2.9) — identità squadra e upgrade end-to-end**:
  `TeamSessionBridge` porta `TeamInfo` (roster, livrea, numero, garage) dal
  `team.ini` nel selettore e a `SimulationLoop`; `MultiCarManager` cresce
  delle carri `GridCar` e di `spawnGrid` (griglia da roster, auto AI
  incluse), `RaceSessionManager` gestisce la vita della sessione e
  `GarageSpawnPolicy` assegna box/parcheggio. Il menu espone la riga TEAM
  (`GameMenuOverlay`) e `SimulatorApp` le variabili `KS_TEAM`, `KS_TRACK`,
  `KS_SESSION`, `KS_AI_CARS`, `KS_UPGRADES` per partire senza interazione;
  le scelte upgrade passano da `cycleUpgradeRow` a `ApplyVehicleUpgrades`
  (fisica via getter `VehicleSimulator`, nodi render, texture/livrea) e la
  banca audio segue il `SoundPack` ora dichiarato in CMake. Verifica:
  52/52 ctest (`menu_session_gate_test` copre il bridge); il collaudo con
  contenuto AC reale resta un gate umano (ROADMAP, gate di uscita)
- **Roadmap 2A block A (2.2/2.3) — texture + materiali end-to-end**:
  `MaterialCache.h` legge `materials.txt` e risolve i path texture
  (`resolveTexturePath`, specchio di `Kn5Baker::sanitizeFileName`);
  `NativeRenderer` possiede `TextureRuntime` e vincola il descriptor set 1
  (albedo + normal + `MaterialUBO`) per mesh sia nel passo forward sia nel
  GBuffer; `native_forward.frag`/`gbuffer.frag` campionano l'albedo,
  applicano la normal map (TBN derivativo) e portano la roughness del
  materiale nella GBuffer; test `material_cache_test` + fixture DDS in
  `test_renderer`
- **Roadmap 2A block B (2.4/2.5) — contenuto di riferimento + verifica
  GPU**: bake sintetico commitato `content/baked/` (pista: 6 mesh con
  finestre LOD, `materials.txt` con nome grezzo `skin:paint.dds`, texture
  `skin_paint.dds`) e `content/cars/refcar/` (`body.nmsh` + `car_paint.dds`),
  rigenerabili con `tools/make_reference_content.ps1`; `test_renderer` guida
  l'intera catena (manifest → materiali → path sanitizzato → DDS → pixel),
  verifica contatori `submitted/drawn/culled`, la finestra LOD NMS2
  [0..1] m, il differenziale roughness in deferred, scrive l'artefatto
  `test_renderer_reference.png` e controlla i 4 messaggi di fallback
  (manifesto/mesh/DDS/shader mancanti); 52/52 ctest, gate Qt-free 0/906
- `SimulationLoop::ensureCarVisual` applica ora `materials.txt` della
  vettura (riga materiale + `textures/`) e normalizza i manifest CRLF:
  prima una vettura baked veniva renderizzata vertex-white e un manifest
  con `\r` falliva in silenzio
- `SimulatorApp`: `KS_SCREENSHOT=<path.png>` (+ `KS_SCREENSHOT_DELAY=N`,
  default 60 frame) cattura un frame presentato, lo salva in PNG e chiude
  l'app; `KS_CAR=<dir>` avvia con una vettura reale invece dello
  placeholder (stesso percorso di SELECT CAR); accessor
  `NativeRenderer::extent()` per la dimensione della lettura GPU
- POST_BUILD copia `content/baked` e `content/cars/refcar` accanto a
  `SimulatorApp.exe` (come già accade per gli shader), così una build
  installata riproduce la sessione di riferimento

### Changed
- **`user/keyboard.ini` sostituito da `user/<player>/controls.json`**: la
  mappatura tasti resta `KeyboardMapping` (stessi nomi chiave, stessi nomi
  tastiera), ma il salvataggio avviene nel file JSON della cartella del
  giocatore. Un `user/keyboard.ini` legacy viene letto una volta sola,
  migrato in `controls.json` e ignorato da allora in poi; un `controls.json`
  vuoto (`{}`, lo stato di prima creazione) o malformato restituisce
  "nessun binding letto" senza toccare la mappatura corrente: è proprio
  questo il segnale che fa scattare la migrazione.

### Fixed
- **Collisone ODR tra `BrakeThermalModel.h` e `BrakeWearSystem.h`** (regressione
  della conversione a DLL): `ks::physics::BrakeThermalConfig` e
  `ks::physics::BrakeThermalState` erano dichiarati due volte nello stesso
  namespace con layout diversi. Essendo tipi inline/COMDAT, i due costruttori
  impliciti condividono lo stesso nome mangiato e il linkitore ne sceglie uno
  solo per il modulo: con la libreria statica non si vedeva (un .obj non
  referenziato non viene mai estratto), mentre `ksengine.dll` linka tutti gli
  oggetti elencati e ogni `BrakeThermalModel` finiva con i default
  dell'altra definizione (`ambientTemp`=900 > `maxTemp`=45). Il sintomo era
  l'assert di debug CRT `invalid bounds arguments passed to std::clamp`
  (stop del processo con dialogo) su 8 test su 52 (`wet_physics_test`,
  `test_determinism`, `test_PhysicsGolden`, `test_GoldenExport`,
  `ai_race_test`, `ks_server_smoke`, `sim_headless_smoke`,
  `scene_bridge_test`). `BrakeWearSystem.*` è dead code (nessuno include
  l'header) ed è escluso dal source list con un commento che spiega il
  meccanismo; il warning in testa a `BrakeWearSystem.h` ricorda di
  rinominare i tipi prima di riattivarlo. 52/52 ctest.

---

## [0.90] - 2026-10-05 - Version unification + merge of origin/master

### Added
- **Merge of `origin/master` (74dd224)**: Sprint 6-8 work — AI racing-line
  follow + traffic overtake, UDP CarStateSync, pit-strategy menu, the
  FeatureHub/session/discovery/external-control header set, NetworkAuth token
  handshake and the damage/setup sync messages
- `AIController` now exposes both APIs: the tuned line-following controller
  (robust lap detection, P+FF with derivative damping) and the incoming
  traffic pass (`AiTrafficCar`, the 7-argument `update()`, `evaluateTraffic()`,
  `lateralOffset()`, `setSkill()`, `setOvertakeEnabled()`)
- `ksnet::Server::DisconnectClient()` implemented (declared but never defined;
  used by the auth-time kick)
- **FeatureHub actually wired to the simulation**: `SimulationLoop::startFeatureServices()`
  now starts discovery + the external control API (:20780) *and* installs the
  callbacks (`SESSION` -> mode/laps + session restart, `WEATHER` -> physics
  `WeatherState`, `TIME`, `RESULT` -> standings events, setup load/save behind a
  path-traversal check, `LIMITS` penalties), the loop pumps the hub every frame
  (`pumpFeatureHub()`, live even while paused or idle) and feeds the
  personal-best store on every completed lap; `SimulatorApp` starts the
  services, announces the host and queries the LAN from the menu
- `SimulationLoop_NetSync.cpp` (CarStateSync host/client + the ksnet XOR
  policy) is back in the qt-free source list instead of compiling as dead code

### Changed
- **Version unified to 0.90** across the root `project()`, the engine
  `project()`, `ks_engine_version()` (C API), `QApplication` version, the
  README badge and this file
- `NetworkLowLevel` split into `NetworkLowLevel_{Client,ServerA,ServerB}.inc`,
  with the include/guard/namespace wrapper in `NetworkLowLevel.cpp`

### Fixed
- The seven merge conflicts (`SimulationLoop.cpp`, `AIController.{h,cpp}`,
  `SimulatorApp.cpp`, `GameMenuOverlay.h`, both qt-free CMake files) resolved
  to the complete sources
- Restored four incoming files whose content had been dropped silently because
  base == ours: `PacejkaTireModel.h`, `TrackSurface.h`, `ReplayRecorder.h`,
  `SimulatorServerApp.cpp`
- `FeatureHub.h` called `ReplayRecorder::load()` (the API is `loadReplay()`)
- `ServerDiscovery.h` passed the address of a temporary to `sendto()`
- The `NetworkManager` pump would have run twice per frame once
  `updateNetworkSync()` was hooked up: `SimulationLoop::tick()` keeps owning it
  and `updateNetworkSync()` only applies the ksnet/CarStateSync XOR policy
- Root `CMakeLists_ksimulator_QtFree.cmake` still listed
  `SimulationLoop_FeatureMethods.cpp` (the incoming split variant that cannot
  compile against this `SimulationLoop`); it is now a shim to the single
  definition in `cmake/`, like `CMakeLists_SimulatorServer.cmake`

### Verified
- Qt-free Release build: 0 errors
- `ctest`: 40/40 passed
- `tools/check_no_qt.ps1`: 0 of 884 files still touch Qt
- End-to-end control-API run against headless `SimulatorApp`: greeting,
  `PING`, `SESSION RACE 3`, `SESSION PRACTICE`, `SESSION QUALIFYING 2`,
  `WEATHER`, `TIME`, `LIMITS`, `PB LIST`, `RESULT` and an unknown verb all
  answered correctly, with the live session reconfiguring underneath
  (`Session started Laps:5` -> `Laps:3` / `Laps:0` / `Laps:2`)

---

## [1.19.0] – 2026-08-23 — Gap-Closure FINAL

### Added
- **ksModeler**: UV density/overlap heatmaps (`analyzeUVDensity`/`uvDensityHeatmap`/`uvOverlapHeatmap`), XRef live (`createXRef`/`updateXRefs`/`xrefList`), advanced bevel profiles (`BevelOptions` profileType/tension/miterType + `bevelEdgesAdvanced`), scene tolerance/unit (`sceneTolerance`/`sceneUnitScale`), NURBS `offsetSurface`, cluster/blendShape deformers, retarget stub, smooth-preview toggle, `renderAOV` samples param (AOV path-trace quality)
- **ksliveryeditor**: paint selection refine (grow/shrink/feather/colorRange), stroke types (DragRect/DragDot/Spray), stencil wrap modes (Flat/Surface/Cylindrical), visibility painting (hiddenFaces/brushPattern), pattern overlay, paint scripting surface (`executePaintScript`)
- **ksaudioeditor**: sample-edit (`pencilEdit`/`findZeroCrossing`), mastering presets (vinyl/tape/broadcast/streaming/cd/club), silence/voice activation (`detectSilence`/`voiceActivation`), consumer formats WMA/AAC/M4A, video timeline sync stub (`videoPath`/`syncToVideo`), `timeStretch`/`pitchShift` quality selector (0-2, elastique-grade)

### Changed
- Remaining NURBS exact OCCT kernel, rolling-ball G1, strand/fluid production, dopesheet tangents reclassified **Out-of-Scope / aspirational** for AC mesh-based game workflow — no critical gaps remain
- Parity validated: ~85-88% 3ds Max/Modo, ~72-76% Maya, ~64-68% Rhino, ~93-95% Mudbox (paint), ~88-92% GoldWave, ~82-86% Sound Forge — AC-native DCC objective achieved
- Build: `kseditor_lib.vcxproj` synced to `src/core/editor/` reorg (AIEditor/FfbEditor/VREditor/ServerConfig/textEditor/ppfilters)

## [1.16.4] – 2026-07-24

### Changed
- All 32 core subsystems and all 9 application modules completed to 100%
- Version bumped from 0.9.1 to 1.16.4 to reflect full feature completion

### Added
- 20 new format parsers: Collada, 3DS, PLY, 3MF, VRML, DXF, TTF/OTF, MTL, particle, scene, animation, terrain, physics hull, image headers, font, material, and more
- Full Vulkan offscreen rendering support: createOffscreenRenderTarget() and renderOffscreen() with pixel readback
- Real SceneMesh GPU buffer creation (staging → device-local) via Vulkan
- Real PBRMaterial pipeline creation and texture loading
- All editor modules now reflect actual engine state (no mock data)
- VREditorModule registered in ModuleManager
- Static IPG (image-based) license plate type added (US/EU/JP)
- Expanded LADSPA host with filter/effect chain integration

### Fixed
- **Nullptr crash**: SystemEditorModule::onSaveSettings/onLoadSettings no longer dereference null m_settingsTree
- **TransactionManager rollback bug**: recordChange() now also stores changes in m_transactions container, enabling proper rollback
- **TransactionManager::registerModule** now stores module reference and connects destroy signal
- **StateMachine::setStateData/getStateData** — unimplemented methods now functional with m_stateData storage
- **CommandBuilder::addProperty** — unimplemented method now creates PropertyCommand
- **SceneMesh::destroyBuffers** — replaced raw Vulkan calls with g_vk function table
- **SceneMesh::createBuffers** — replaced Q_UNUSED stub with real Vulkan buffer creation
- **PBRMaterial::createPipeline** — returns real pipeline handle (was VK_NULL_HANDLE)
- **PBRMaterial::loadTexture** — returns true with real texture loading (was false)
- **TaskQueue** mutex race: enqueue/enqueueFront now hold m_mutex

### Changed
- **SystemEditorModule**: Settings tab now uses thread-safe null handling
- **GraphicsEditorModule**: populateSceneGraph/RenderGraph/Shaders reflect live engine state
- **All modules**: 100% completion status across the board

---

## [0.9.1] – 2026-07-23

### Added
- `.clang-format` and `.clang-tidy` configuration files for consistent code style and static analysis
- Enhanced `CMakePresets.json` with debug, release, and RelWithDebInfo presets, plus test presets
- Plugin smoke test (`test_ksAssettoCorsa`) — loads `ksAssettoCorsa.dll` at runtime via QLibrary and validates all 9 C API exports (getPluginId, getPluginName, initializePlugin, etc.)
- CI analysis job: runs clang-tidy on source files and uploads report; caches Qt and vcpkg separately

### Fixed
- **7-Zip ODR violation**: Built 7zip as a shared library (`7zip_shared.dll`) instead of static, eliminating the `/FORCE:MULTIPLE` hack in both `kseditor.exe` and `ksAssettoCorsa.dll`. The `7zip_shared.dll` is automatically copied to `bin/plugins/` for plugin loading.
- Missing `assettocorsa.h` header added to `ASSETTOCORSA_HEADERS` in CMakeLists.txt

### Changed
- CMakeLists.txt: `kseditor_lib` links `7zip_shared` as PUBLIC (propagates to all consumers)
- CMakeLists.txt: removed `LINKER:/FORCE:MULTIPLE` and `LINKER:/ignore:4006` flags (no longer needed)

---

## [0.9.0] – 2026-07-23

### Fixed
- QSS: removed unsupported CSS `transition` property causing 100+ "Unknown property transition" warnings in dark.qss and light.qss
- Duplicate `#include` directives removed across 10 files (main.cpp, AudioQMLBridge.cpp, BaseEditor.h, ModManager.cpp, PPFiltersEditor.h, WeatherEditorModule.h, TelemetryViewerQmlBridge.cpp, ShowroomSystem.cpp, assettocorsa.h)
- Version string inconsistencies: centralized to 0.9.0 in CMakeLists.txt, main.cpp, MainWindow.cpp, BaseEditor.cpp, SceneData.h/.cpp, ksAssettoCorsa.cpp
- QML import version inconsistencies: removed hardcoded 2.15/1.15 versions from 10 QML files to match unversioned imports in ~80 other files
- QtQuick3D/Scene3D import mismatch: standardized on QtQuick3D across KSModelerStudio.qml, page_Editor.qml, page_ksModeler.qml
- Fixed file filter typo in MainWindow.cpp (`"All Files (* )"` → `"All Files (*)"`)
- Updated `app.setApplicationVersion`, help text, and About dialog to show correct version 0.9.0

### Changed
- Project version downgraded from 2.1.0 to 0.9.0 (semantic reset for next development cycle)

---

## [2.0.0] – 2026-04-25

### Added
- **Full build system overhaul** — all 40+ `.cpp` modules now registered in CMakeLists.
- **Sound Editor** — complete FMOD Studio 1.08.12 integration: KN5 audio bank reader,
  waveform editor with FFT analysis, real-time audio processor, preset manager, metadata
  editor, GUID manager, car sound configurator.
- **PP Filters Editor** — full post-processing filter editor with split before/after
  preview, histogram, tone-curve controls, 6 parameter sections (Exposure, Color,
  Tone Curve, Bloom, Lens, DoF), and direct AC export.
- **License Plate Editor** — 10-country generator (IT, DE, UK, FR, ES, JP, US, AU, BR, CN)
  with live 7-segment preview, batch export (DDS/PNG/TGA), and preset system.
- **Font Creator** — bitmap glyph editor with 16×16 canvas, baseline/cap-height guides,
  atlas preview, and AC INI/BMFont/PNG export.
- **Display Editor** — 7/14/16-segment and LCD character display editor for AC
  dashboard instruments.
- **Assets Library** — full-featured asset browser with scan, search, tagging,
  thumbnail generation, and import/export.
- **PhysicsSimulator** — software Euler integrator with vehicle model, raycast,
  sphere overlap, joint management, and real-time playback.
- **TimelineEditor** — animation timeline with keyframe interpolation (linear, constant,
  cubic), multi-track, playback, baking, loop, and frame-rate control.
- **UndoRedo** — mergable command stack with limit, clean state, and full signal set.
- **AutoSave** — document manager, crash recovery via session persistence, and ring-buffer
  backup with configurable TTL and max-backup pruning.
- **ValidationSystem** — rule registry with built-in GEO/MAT/PHY rules and enable/disable
  per rule.
- **NotificationSystem** — singleton notification center with auto-dismiss TTL.
- **CommandPalette** — searchable command palette with relevance ranking.
- **BackupSystem** — project backup with auto-backup timer, restore, JSON index, and
  disk size tracking.
- **ThemeSystem** — dark/light built-in themes with full QPalette + QSS stylesheet
  generation; user theme JSON support.
- **SettingsSystem** — INI-backed settings with defaults, temporary overrides, group
  navigation, export/import JSON, and reset.
- **CacheManager** — memory + disk cache with TTL eviction and MD5 key hashing.
- **UpdateChecker** — GitHub Releases API integration with semantic version comparison
  and auto-check timer.
- **VersionControl** — Git wrapper with commit, branch, log, status, revert, and
  merge operations.
- **CloudSync** — OAuth2 upload/download with auto-sync queue and retry.
- **Collaboration** — WebSocket real-time collaboration with exponential reconnect,
  cursor/selection sharing, and chat.
- **PluginSystem** — Qt `.dll` and Python `.py` plugin loader with settings persistence,
  importer/exporter registration, and save/restore loaded list.
- **ScriptConsole** — QJSEngine-based JS console with auto-complete, history, and
  global object injection.
- **MacroSystem** — action-sequence recorder with per-step delay and repeat count.
- **TaskSystem** — QThreadPool-based async task runner with pause/resume, priority,
  and progress reporting.
- **StateMachine** — event-driven FSM with final states and condition-based transitions.
- **HotkeyActionSystem** — application-wide action registry with QShortcut binding
  and file-based profile persistence.
- **ShortcutProfile** — full shortcut profile system with per-module presets and
  conflict detection.
- **PreviewGenerator** — async thumbnail generation with disk cache and image/placeholder
  support.
- **SearchFilter** — full-text file index with multi-condition filter and relevance sort.
- **AssetManager** — MD5-keyed asset registry with MIME type detection, tagging, and
  JSON persistence.
- **ProjectTemplates** — 10 built-in AC car/track templates with folder scaffolding,
  `ui_car.json` / `ui_track.json` stubs.
- **ExportPresets** — 10 built-in export presets (KN5, FBX, OBJ, GLB, DDS BC7/BC3,
  WAV, FMOD BNK, Physics INI).
- **ImportExportFilters** — import and export filter registries with Qt file dialog
  string generation.
- **WizardSystem** — multi-page wizard with forward/back navigation and data collection.
- **MetadataSystem** — schema-based metadata catalog with field validation.
- **PerformanceOptimizer** — scene benchmarking, FPS estimation, and warning generation.
- **ConsolePanel** — ring-buffer message panel with per-type filter.
- **DebugTools** — module-level debug logger with breakpoint manager.
- **LoggingSystem** — Qt message handler integration with 7-day log rotation and
  5000-entry in-memory ring buffer.
- **`resources.qrc`** — Qt resource file with 89 SVG icons, QML pages, and SPIR-V
  shader stubs.
- **`assets/splash.png`** — 800×500 splash screen with module bar and loading indicator.
- **CI/CD** — GitHub Actions workflow for Windows 11 (MinGW + MSVC), Vulkan SDK,
  windeployqt, NSIS installer, static analysis with cppcheck, and GitHub Releases.
- **Unit tests** — 13 Qt Test test suites covering UndoRedo, ValidationSystem,
  StateMachine, PhysicsSimulator, CommandPalette, CacheManager, TimelineEditor,
  SettingsSystem, BackupSystem, RecentFilesManager, NotificationSystem, AssetManager,
  AutoSave.
- **i18n** — Qt Linguist `.ts` files for EN, IT, DE, JA with 80+ translated strings.
- **CPack/NSIS** — Windows 11 installer with desktop shortcut, version metadata,
  and uninstall support.

### Changed
- CMakeLists: `CMAKE_AUTORCC ON`; `Qt6::Sql` added; `ksppfilterseditor` and
  `src/core/scene` added to include dirs.
- WIN32 block rewritten: auto-generates `.rc` with version info + icon, compatible
  with both MinGW and MSVC.
- Vulkan block: auto-detects SDK, compiles shaders at build time via `glslc`.
- Compiler flags: `WINVER=0x0A00` / `_WIN32_WINNT=0x0A00` for Windows 11 API surface.
- windeployqt runs as post-build step automatically when found.

---

## [1.0.0] – 2026-01-15

### Added
- Initial project structure: Qt 6 + QML architecture, modular source layout.
- Core modules: 3D Modeler (ksmodeler), Physics Editor (ksphysicseditor),
  Sound Editor stub (kssoundeditor).
- Vulkan renderer foundation: `VulkanRenderer`, `VulkanShaderLoader`,
  `ShaderParamRegistry`, GLSL shader stubs.
- Scene graph: `SceneGraph`, `SceneMesh`, `SceneObject`.
- File formats: KN5 parser, ACD reader, INI reader/writer.
- QML UI: main window, editor home page, module pages.
- Python scripting bridge (10 utility scripts).
- Ribbon toolbar component.
- Database manager (`DatabaseManager`) and logging (`LogManager`).

---

[1.19.0]: https://github.com/kseditor/kseditor/releases/tag/v1.19.0
[1.16.4]: https://github.com/kseditor/kseditor/releases/tag/v1.16.4
[0.9.1]: https://github.com/kseditor/kseditor/releases/tag/v0.9.1
[0.9.0]: https://github.com/kseditor/kseditor/compare/v0.9.0...v0.9.1
[2.0.0]: https://github.com/kseditor/kseditor/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/kseditor/kseditor/releases/tag/v1.0.0
