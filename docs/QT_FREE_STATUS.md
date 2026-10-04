# KSEngine / ksimulator Qt-free

**Updated:** 2026-10-04 â€” **phase completed: `check_no_qt.ps1 -Strict` â†’ 0.**
Milestones completed: **ECS + `ks::Engine` used by the simulator**, **deferred/
volumetric/TAA renderer**, **Lua scripting**, **TrueType fonts** (`KS_FONT_TTF`,
`engine/FileFormat/TtfReader` + `TtfRasterizer`, no FreeType).

## Status

| Gate | Result |
| --- | --- |
| `ksengine` (`-DKSENGINE_QT_FREE=ON`) | **builds, 0 errors** |
| `SimulatorApp` (`-DKSIMULATOR_QT_FREE=ON`) | **builds + links, 0 errors** |
| `tools/check_no_qt.ps1 -Strict` | **0 of 858 files touch Qt** (exit 0) |
| ECS standalone build (`cmake -S src/engine`) | **builds, 0 errors** |
| CTest qt-free (`ctest --test-dir build_qtfree -C Debug`) | **40/40 passed** |
| CTest standalone (`cmake -S tests/ksengine -B build_ksengine_tests`) | **31/31 passed** |

## Build

```bash
cmake -S . -B build_qtfree -G "Visual Studio 18 2026" -A x64 \
      -DKSENGINE_QT_FREE=ON -DKSIMULATOR_QT_FREE=ON
cmake --build build_qtfree --config Debug            # everything
cmake --build build_qtfree --config Debug --target SimulatorApp -j 8
powershell -ExecutionPolicy Bypass -File tools/check_no_qt.ps1 -Strict
```

Notes:
- Re-run the `cmake -S . -B build_qtfree` configure step after adding or
  deleting sources: the `file(GLOB_RECURSE)` results are cached.
- The qt-free early-exit block lives in the root `CMakeLists.txt`
  (â‰ˆ line 176). Qt `find_package` calls are skipped behind `KS_QT_FREE_BUILD`.

## Engine

- `Engine.h` + `Engine.cpp` + `EngineModule.h` â€” fixed tick, `update(dt)`, no Qt.
  `Engine.cpp` now holds the real out-of-line bodies (`initialize`, `shutdown`,
  `createEntity`, `destroyEntity`) so a standalone `ksengine` build compiles and
  instantiates the whole ECS, not just parses the headers.
- `Engine` owns a `ecs::Registry` (`registry()`) and exposes
  `createEntity(name)` / `destroyEntity(e)` â€” the app-side equivalent of
  `pEntitySystem->SpawnEntity()`.
- `scene/Registry.h` â€” sparse-set ECS, dependency-free. Handles are
  `(generation << 20 | index)` so a stale handle can never alias a recycled
  slot. `emplace/remove/has/tryGet/get` are O(1); `each<A, B>()` iterates the
  smallest pool and is available in const and non-const form (up to 4
  component types).
- `scene/Components.h` â€” `Name`, `Tag`, `Transform` (position/rotation/scale,
  rotation as roll-pitch-yaw matching `ks::physics::MotionState`), plus
  renderer-agnostic `MeshInstance` and `worldMatrix(Transform)`.
- `scene/SceneModule.h` â€” `EngineModule` (`ks.scene`) owning an ordered list
  of named systems; `Engine::tick()` runs them against `Engine::registry()`
  at the fixed timestep.
- `KsQtFreeGuard.h` â€” hard fail if Qt headers leak in
- CMake AUTOMOC/UIC/RCC off, no Qt link
- Qt-only sources are excluded in `src/engine/CMakeLists.txt` via
  `list(FILTER ... EXCLUDE REGEX ...)` as a safety net (the Qt/QML bridges no
  longer live under `src/engine` at all).
- `engine/AI/AiFileReader` â€” standalone AC AI-line reader (binary `\0AI`
  magic + CSV fallback), replaces the Qt editor's `AiSplineEditor::parseAiBinary`.
- `engine/Audio/BankParserBridge` â€” `ks::audio::parseBankFile` stub, reports
  `valid=false` until a standalone FMOD bank reader exists.

## ks::Engine wiring (SimulatorApp)

- `SimulationLoop::initialize()` â†’ `Engine::initialize()`, registers
  `ks.input` (`devices::InputSystem`), `ks.render` (`graphics::RenderSystem`),
  `ks.scene` (`ecs::SceneModule`) and `ks.script` (`scripting::ScriptModule`)
  â€” **the first three were dead code before** (nothing in the repo ever called
  their `::instance()`).
- The three modules are Meyers singletons, so they are registered through an
  aliasing `shared_ptr` with a no-op deleter: `Engine` must not own/destroy
  them.
- `SimulationLoop::start()/stop()` drive `Engine::start()/stop()`;
  the destructor clears the systems, detaches the registry and shuts the
  engine down.
- `SimulationLoop::tick()` calls `Engine::tick(elapsed)` after its own 1 kHz
  physics pass, so ECS systems run at the engine's fixed 120 Hz step.
- `syncCarTransforms` is registered as a system: vehicle position â†’
  `Transform` of every `car_*` `MeshInstance` (replaces the old inline loop
  over `m_renderables`).
- `applyInput()` publishes `InputState` into `devices::InputSystem`, so engine
  code reads input from the module instead of reaching into the sim.
- `updateWeather()` is no longer a stub: time-of-day â†’ sun direction
  (12:00 reproduces the historical `{0.3,-0.8,0.2}` default exactly, so the
  frame is unchanged until `setTimeOfDay()` is called), cloud cover â†’ fog,
  `WeatherState` â†’ rain/wetness, forwarded to `NativeRenderer::setSun`.
- `render()` runs the `RenderSystem` frame lifecycle (Shadow â†’ Geometry â†’
  Post) and submits the scene **from the registry**: every
  `(Transform, MeshInstance)` entity becomes a `NativeRenderer::drawMesh`.
- `loadBakedScene(dir)` loads the `.nmsh` manifest through `NativeRenderer`
  and spawns one entity per mesh; `SimulatorApp` now calls this instead of
  reaching into the renderer directly.
- Dead field removed: `SimulationLoop::m_renderables` / `renderables()`
  (`std::vector<RenderableMesh>`, always empty, zero references anywhere in
  `src/`, `examples/`, `tests/` or `tools/`).

## Simulator

- `SimulationLoop` wires **NativeUiHub** each frame
- HUD from vehicle state; modal UI blocks driving input + FFB **and pauses
  physics** while still rendering (otherwise the menu could never be drawn)
- `handleUiKey(vk)` â€” Esc menu / F1 devices / F2 multiplayer / Y race HUD;
  `handleUiChar(c)` â€” WM_CHAR into `ChatOverlay`; `handleUiMouse*` â€” mouse
  into `UiInput`
- One `GameMenuOverlay` only: `SimulatorApp` wires its callbacks onto the
  hub's instance (it used to own a second, never-rendered copy, so the
  visible menu had no actions and the wired one was never drawn)
- `ChatOverlay` + `MultiplayerOverlay` bound to `NetworkManager`
  (`onHostServerRequested`/`onJoin`/`onChatMessageReceived`)
- Font atlas + UiRenderer batch ready for GPU upload â€” plus real TrueType
  fonts: `KS_FONT_TTF=/path/font.ttf` (+ `KS_FONT_PX`) makes `NativeUiHub`
  rasterize ASCII 32..126 into the atlas through `ui::loadTtfFont`; a rejected
  font (CFF/OTTO, truncated, â€¦) logs `[ui] KS_FONT_TTF ignored:` and keeps the
  baked 8Ã—8 set. See `docs/NATIVE_UI.md`.
- `NativeRenderer` + `ShadowSystem` are the full Vulkan implementations
  (the 124-byte include shims left over from the reset were removed).
- `MqttClient.cpp` is **not** in the qt-free source list: it unconditionally
  includes `<mosquitto.h>`, which is not on this machine.

## Renderer â€” deferred, volumetrici, TAA

- Three passes: **GBuffer** (MRT: RT0 albedo+AO `R8G8B8A8_UNORM`, RT1
  normal+roughness `R16G16B16A16_SFLOAT`, RT2 worldPos+coverage) â†’
  **lighting** (fullscreen triangle, PBR GGX + cascade shadow + height fog +
  12-tap volumetric raymarch with a Henyey-Greenstein phase = god rays) â†’
  **resolve** (TAA or plain blit into the swapchain).
- `NativeRenderer::setDeferred(bool)` / `setTaa(bool)`, **both default off**:
  the TAA path is opt-in because this environment has no visual verification,
  so the gate is "builds + `glslc` compiles every shader", not "looks right".
- Three P0 post effects, all **default off**, all deferred-only, and each one
  implemented inside a shader that is already bound to everything it needs, so
  not a single new image, pass or descriptor was added:
  - `NativeRenderer::setSsao(bool, radius, intensity, bias)` + env `KS_SSAO=1`
    (`KS_SSAO_RADIUS` / `KS_SSAO_INTENSITY` optional): 16-tap golden-angle
    hemisphere marched in `deferred_lighting.frag` against `gbufWorldPos`,
    radial-distance occlusion test, folded into the ambient term only.
    `ssao.comp` stays unwired - it targets `r32f` depth storage images this
    GBuffer does not expose.
  - `NativeRenderer::setSsr(bool, maxDistance, intensity, roughnessCutOff)` +
    env `KS_SSR=1` (`KS_SSR_DISTANCE` / `KS_SSR_INTENSITY`): world-space ray
    march in the same shader, the hit shaded from the hit pixel's
    albedo/normal + sun, added fresnel- and roughness-weighted on top of the
    specular term. `ssr.comp` stays unwired for the same reason.
  - `NativeRenderer::setMotionBlur(bool, strength, samples, maxLength)` + env
    `KS_MOTIONBLUR=1` (`KS_MOTIONBLUR_STRENGTH`): gathered inside the resolve
    pass (`taa.frag`) on the current frame *before* the temporal blend, from a
    motion vector reconstructed with `prevViewProj` + GBuffer world positions,
    so no velocity buffer is needed and history is never smeared twice.
    `motionblur.frag` stays unwired.
  - `FrameDataUBO` is now 480 bytes: the tail is `viewProj`, `prevViewProj`,
    `taaParams`, `fogColor`, `fogParams`, `aoParams` (432), `ssrParams` (448),
    `motionBlurParams` (464). Offsets checked against `spirv-dis` for
    `deferred_lighting.frag` and `taa.frag`; `native_forward.frag` declares
    the same tail, and any shader may stop early without moving the offsets
    of the members it does declare.
- `test_renderer` also pixel-checks one deferred frame with SSAO + SSR +
  motion blur all enabled (GBuffer -> lighting + SSAO + SSR -> resolve +
  motion blur -> display pass, default ACES curve): the quad must survive the
  round-trip, stay lit and stay clearly brighter than the background. A lone
  quad occludes nothing, reflects nothing and does not move, so the three
  effects must leave the image alone - which is also what keeps the assertion
  stable. That frame is the only pixel-level coverage the deferred chain has,
  since this environment cannot look at the output.
- `cmake/KsShaders.cmake` builds 28 `.spv` (was 15): the 12 shaders that used
  to be dead files (`ssao`, `ssr`, `motionblur`, `fxaa`, `particle`,
  `terrain`, `water`, `vegetation`) are registered so CI notices when they
  rot, and P1 added `particle_gbuffer.frag`. The empty `GPUParticleSystem`
  stub was deleted.
- P1 particle system (all std-only, no Qt): `engine/Graphics/
  ParticleSystem.{h,cpp}` - fixed 1/120 s integration steps clamped to 2 s
  (240 steps) per call, cone emitters, gravity + linear drag, 4096-particle
  cap, deterministic xorshift seeding, `buildQuads()` packing the
  interleaved 12-float sprite records. `SimulationLoop::updateAndDrawParticles`
  runs it from `render(dt)` behind opt-in `KS_PARTICLES=1` (default off - the
  default frame is unchanged), one dust emitter following the car while it
  moves. Two pipelines in `NativeRenderer` consume the vertex blob: forward
  (alpha blend, depth test, no depth write) and GBuffer (MRT coverage,
  `particle_gbuffer.frag`), both from `particle.vert`, camera-facing via the
  view-matrix basis pushed as constants. `particle_test` covers the sim
  (spawn/cap, gravity/drag, determinism, the 2 s stall clamp, `buildQuads`
  byte layout), and `test_renderer` renders a real sprite built by
  `buildQuads` twice per path (with/without the upload) and requires both the
  forward and the deferred frame to change and the centre pixel to turn red
  (readback is BGRA).
- P2 trackside terrain (std-only, opt-in `KS_TERRAIN=1`, default off):
  `src/engine/terrain` is now compiled into `ksengine` (it was dead source
  in no build target at all). `TerrainMesh.{h,cpp}` turns a heightmap into a
  grid mesh whose `TerrainVertex` is byte-compatible with `NativeVertex`
  (central-difference normals, winding CCW seen from +Y, a stride-2 LOD
  variant and an NMSH writer matching `loadMeshFromFile`), and the new
  `TerrainHeightmap.{h,cpp}` synthesizes that heightmap deterministically
  (value-noise fBm: seeded lattice hash, quintic smoothstep, octaves /
  lacunarity / gain) because the only real heightmaps live inside Qt's
  TrackTerrainEditor. `SimulationLoop::initTracksideTerrain()` builds a
  129x129 grid over 1600 m, presses it down to -0.8 m under the baked track
  within 200 m and blends into the hills outside, tints every vertex (the
  GBuffer takes albedo from vertex colour - there is no terrain texture to
  bind) and registers it as an ordinary ECS `MeshInstance`, so it rides the
  same draw list, shadow pass and frustum test as KN5 geometry: no new
  shader, no new descriptor set, no new pipeline. With the flag unset no
  heightmap is generated and no mesh exists, so the default image is
  untouched. `terrain_test` covers mesh generation (vertex/index counts,
  normals on a ramp, CCW winding, LOD sampling, byte-exact NMSH round-trip,
  malformed input rejected) and the noise (bit-identical per seed, differs
  across seeds, bounded by the amplitude, flat at zero octaves);
  `test_renderer` adds a differential terrain frame on the forward and the
  deferred path (~36% of the pixels move). The dead `terrain/water/
  vegetation.vert|frag` shaders stay unwired on purpose: they are
  single-attachment forward shaders with a 256/264/312-byte push-constant
  block (water and vegetation pass Vulkan's guaranteed 256) plus a `set = 1`
  block for textures this renderer cannot upload yet.
- P2 heightmap import (std-only, opt-in `KS_TERRAIN_PATH=<png>`, default
  off): `FileFormat/PngReader.{h,cpp}` is a grayscale PNG reader written
  against the engine's existing `Deflate.h::inflateZlib` - signature and
  chunk walk with its own CRC32, IHDR accepted for 8/16-bit non-interlaced
  colour type 0 (dims <= 16384), IDAT concatenated then inflated, scanlines
  unfiltered with Sub/Up/Average/Paeth (byte level, bpp 1 or 2), samples
  normalised to 0..65535 (8-bit expanded by 257). Ancillary chunks and
  `PLTE` are skipped, other critical chunks are rejected, and every failure
  path reports a message. This is the exact format `TrackTerrainEditor::
  saveTerrain` writes, so it is the hand-off from the Qt terrain tool to the
  Qt-free simulator: `SimulationLoop::initTracksideTerrain()` loads it when
  `KS_TERRAIN_PATH` is set (fallback to the fBm generator on any failure,
  with `KS_TERRAIN_MIN_H`/`KS_TERRAIN_MAX_H` in metres, defaults -2..48,
  and grids above 257 strided down through `generateTerrainMeshLOD`) and
  skips the press-to-`-0.8 m` basin, since an imported map was authored to
  fit its own track. `png_test` covers 8/16-bit round-trips, all five
  filters, a 1x1 image, extra chunks, and rejects bad signatures, truncated
  data, corrupt IHDR CRC, colour type 2, bit depth 4, interlacing, short
  IDAT, a missing IDAT/IEND, and garbage payload; `test_file_roundtrip`
  writes and re-reads a real file. Palette PNGs (colour type 3 - what GDI+
  and most 8-bit "grayscale-looking" exports emit) are refused: Qt writes
  colour type 0 for `Format_Grayscale8`, which is the format this reader is
  built for. Verified end to end against `SimulatorApp`: a generated 65x65
  bowl reports `65x65 heightmap, -5.0..60.0 m` and builds 4225 vertices, a
  1025x1025 map is strided down to 66049 vertices with the default -2..48 m
  range, a wrong CRC falls back to the fBm noise with a warning, and with
  `KS_TERRAIN` unset nothing is printed at all.
- `taa.frag` reprojects through the *unjittered* view-projection, clamps
  against the 3Ã—3 neighbourhood of the current frame and blends with the
  history at `taaParams.x` (0.9). Halton(2,3) jitter over 8 samples is applied
  to `proj(0,2)/(1,2)`. Two history images ping-pong; `m_historyValid` forces
  feedback 0 on the first frame after a swapchain rebuild.
- `FrameDataUBO` grew to 480 bytes (`viewProj`, `prevViewProj`,
  `taaParams` appended after `cameraPos`) â€” still layout-compatible with the
  shorter block declared by `taa.frag` (which stops at `taaParams`).
- New shaders in `src/simulator/shaders`: `gbuffer.{vert,frag}`,
  `deferred_lighting.{vert,frag}`, `taa.frag`; all five verified with `glslc`
  and registered in `_ksim_shader_sources`.

## Scripting â€” Lua 5.4.8

- Vendored from `lua.org` into `src/engine/external/lua` (60 files, `lua.c` /
  `luac.c` excluded), built as its **own static library `ksengine_lua`**.
  Separate target on purpose: the engine puts `src/engine/sys` on the include
  path, and on a case-insensitive filesystem `lstate.h`'s `#include
  <signal.h>` resolves to the engine's C++ `Signal.h` instead of the CRT's.
- The vendored copy **wins over** `find_package(Lua)`, and the `HAS_LUA`
  variable is shared between the root `CMakeLists.txt` and
  `src/engine/CMakeLists.txt`, so every TU sees the same value.
- `Scripting/LuaScriptHost.{h,cpp}` â€” owns the `lua_State`.
  `initialize/shutdown/eval/runFile/getNumber/setNumber/hasFunction/
  callFunction/lastError`. `eval()` first tries `return <code>` (so
  expressions work) and falls back to the verbatim chunk (so statements
  work); failures never throw, they record `lastError()` and return false.
  Lua's headers carry no `extern "C"` guard, so the include is wrapped.
- `Scripting/ScriptHost.{h,cpp}` â€” facade; the only scripting type the rest
  of the engine talks to.
- `Scripting/ScriptModule.h` â€” `EngineModule` with `moduleId "ks.script"`,
  `priority() 100`: runs the queued startup files at `initialize()`, then
  publishes `delta_time` and calls a global `on_update(dt)` each fixed tick.
- Wired in `SimulationLoop::initialize()` as `ks.script`; a missing Lua is
  reported on stderr instead of failing startup.
- `Engine::tick()` now `stable_sort`s the module snapshot by `priority()`.
  `priority()` was dead before (nothing overrode it), so existing module
  ordering is unchanged â€” it just makes "scripting runs last" real.
- Tests: `tests/ksengine/lua_test.cpp`, `tests/ksengine/script_module_test.cpp`.

## Qt removed from `src/engine` + `src/simulator`

- **Moved** (editor-only, still needed by `src/main.cpp` and
  `src/sdk/kseditor/main.cpp`) to `src/sdk/kseditor/qmlbridges/`:
  `assets/AssetsLibraryQmlBridge`, `Audio/{AudioQMLBridge,
  AudioWaveformBridge, NoiseReducer, PeakMeter, Studio}`,
  `mesh/MeshLoaderQML`, `Scripting/{ACEContentQMLBridge, ContentQMLBridge,
  CspConfigQmlBridge}` â€” includes updated in `src/main.cpp`,
  `src/sdk/kseditor/main.cpp`, `SoundEditorModule.cpp`.
- **Deleted** (tracked, zero references, superseded by the native overlays):
  `src/simulator/DeviceSettingsWidget.{h,cpp}`,
  `src/simulator/MultiplayerWidget.{h,cpp}`.

