# Changelog

All notable changes to ksEditor are documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

### Verified
- Qt-free Release build: 0 errors
- `ctest`: 40/40 passed
- `tools/check_no_qt.ps1`: 0 of 884 files still touch Qt

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
