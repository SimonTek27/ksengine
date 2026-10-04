# ksengine vs CRYENGINE - Feature Gap Analysis

> **Comparer:** ksengine (sim core + qt-free simulator, `src/engine` + `src/simulator`) vs CRYENGINE 5.7 LTS (Crytek)
> **Date:** 2026-10-04
> **Version:** ksengine (post model-importers + TrueType fonts + P0: inline SSAO/SSR/motion blur + P1: particle system + P2 slice: trackside terrain + grayscale heightmap import, suites qt-free 40/40, standalone 31/31, Qt 51/51)
> **Purpose:** Measure how far the in-house engine is from a full general-purpose AAA engine, so the roadmap can be steered deliberately instead of by accident.

---

## 1. Executive Summary

**CRYENGINE** is a complete AAA general-purpose engine: Sandbox level editor, Flowgraph/Schematyc visual scripting, TrackView cinematics, character/animation stack, audio middleware (ACE + HRTF), terrain/vegetation/water, destruction physics, DX11/12 + Vulkan backends, consoles, profiling (Statoscope).

**ksengine** is a *racing-simulator* engine: 52,240 LOC of engine + 18,497 LOC of qt-free simulator, plus a 307,648-LOC Qt asset suite (`kseditor`). It is deep where AC requires depth (vehicle physics, telemetry, race logic, content formats) and thin everywhere a general-purpose engine must be broad (terrain, characters, cinematics, visual scripting, tooling feedback).

**Overall parity:** roughly **25-30% of CRYENGINE's general-purpose feature surface**. Split by area:

| Area | Parity | One-line reason |
|------|--------|-----------------|
| Visuals | ~45% | deferred + CSM + volumetric fog + TAA + bloom/HDR are real; SSAO + SSR + motion blur ship as opt-in effects, P1 added a real particle system (CPU sim + billboard sprites in both the forward and the deferred pass, `KS_PARTICLES`) and P2 added trackside terrain (`KS_TERRAIN`, generated heightmap -> `TerrainMesh` -> the ordinary mesh path) plus the heightmap import that makes it usable with edited terrain (a new std-only grayscale PNG reader feeding `KS_TERRAIN_PATH`, the exact format `TrackTerrainEditor::saveTerrain` writes); vegetation/water shaders are built but still unconsumed |
| Sandbox / tools | ~30% | kseditor is huge but is an *asset* suite, not a level editor: 0% Flowgraph, TrackView, WYSIWYP, resource compiler, streaming |
| AI & Animation | ~15% | racing line AI + basic NavMesh + shape-key/timeline animation; no behaviour trees, no skeletal skinning path in the sim renderer |
| Audio | ~40% | real FMOD bank reading (FEV/STDT/FSB5) + WASAPI out + mixer; no HRTF, occlusion, DRS, dynamic music |
| Physics | ~35% breadth / **>100% in-vehicle** | Pacejka MF + thermal + damage/ERS vs CryPhysics' destruction/ropes/buoyancy/characters - orthogonal, not inferior |
| Performance | ~30% | 1 kHz sim + 120 Hz ECS + HiZ occlusion + golden-lap determinism; no profiler/Statoscope/thread tooling |

**Where ksengine is *ahead*:** AC content formats (KN5, ACD, FBX, OBJ, DDS, KsAnim, TTF, deflate), shared memory + UDP/TCP telemetry, race sessions/flags/penalties, replay + determinism tests, headless C API, dedicated server (`ks_server`), and a Qt-free CI gate (`check_no_qt.ps1 -Strict` = 0, 122 test runs across 3 suites). CRYENGINE has none of these - it would take a full project to write them.

---

## 2. Context & Methodology

**Online check (2026-10-04):** the CRYENGINE source is no longer publicly browsable. `CRYTEK/CRYENGINE_ReadMe` on GitHub is a landing repository only - since May 2022 versions 5.0-5.6.7 were removed from GitHub and the Launcher, and 5.7 LTS source ships through a **private** repo granted per-account. So no line-level diff is possible; this comparison uses the official feature list (`cryengine.com/features`), the public documentation, and Wikipedia's engine capability list.

**Local inventory:** `src/engine` = 52,240 LOC across 22 modules (physics 89 files, Audio 96, FileFormat 121, devices 64, Graphics 60, mesh 59, Tools 54, network 22, Scripting 25, material 25, assets 31, sys 32...); `src/simulator` = 18,497 LOC; `src/sdk` (kseditor) = 307,648 LOC. Shaders compiled by CMake: 28 (`ksShadow`, `native_forward`, `gbuffer`, `deferred_lighting`, `taa`, `hiz_downsample`, `tonemap`, `glareExtract`, `bloomBlur`, `ui.*`, plus the 12 previously-dead ones registered by P0: `ssao`, `ssr`, `motionblur`, `fxaa`, `particle`, `terrain`, `water`, `vegetation`, and `particle_gbuffer` added by P1).

Dead assets found during the audit (P0/P1 status in brackets): `ssao.comp`, `ssr.comp`, `ssgi*.glsl`, `motionblur.frag`, `terrain.{vert,frag}`, `water.{vert,frag}`, `vegetation.{vert,frag}` in `src/engine/Graphics/shaders/` were referenced by **no** CMake target and **no** source file - P0 registered 12 of them in `cmake/KsShaders.cmake` so they now compile every build (`ssao.comp`/`ssr.comp` still have no runtime consumer, since they are written against `r32f` depth storage images our GBuffer does not expose); `particle.{vert,frag}` were in that list and are **consumed since P1** (forward + GBuffer sprite pipelines in `NativeRenderer`); `terrain.{vert,frag}`, `water.{vert,frag}` and `vegetation.{vert,frag}` still have no consumer - P2 renders terrain through the **existing** mesh/GBuffer path instead, because those three shaders are single-attachment forward shaders with a 256/264/312-byte push-constant block (water and vegetation exceed Vulkan's guaranteed 256) and a `set = 1` descriptor block for textures the simulator cannot upload yet; `GPUParticleSystem` (`src/engine/Graphics/GPUParticleSystem.h`) was an empty unreferenced stub and has been **deleted**; `terrain/TerrainMesh` used to be dead source (in no CMake target, only mentioned in a `NativeRenderer.h` comment) and is since P2 compiled into `ksengine` and called by `SimulationLoop`; `FileFormat/PngReader.{h,cpp}` is P2's new std-only grayscale PNG reader (8/16-bit, non-interlaced, filters 0-4, own CRC32 over `inflateZlib`) so heightmap import needs no Qt image library.

Rating scale: **full** = functional equivalent / **partial** = functional but weaker / **missing** = absent / **n/a** = out of engine scope.

---

## 3. Feature Matrix (by CRYENGINE's own categories)

### 3.1 Visuals

| CRYENGINE feature | ksengine | Notes |
|---|---|---|
| Physically Based Rendering | partial | `pbr.frag`, `ksPerPixel`, `ksMultilayer` (AC multilayer map) - single GGX path, no layered clearcoat presets |
| Deferred lighting | full | GBuffer MRT + fullscreen PBR + cascade shadows + height fog |
| Volumetric fog shadows / god rays | full | 12-tap raymarch + Henyey-Greenstein phase in `deferred_lighting.frag` |
| Efficient anti-aliasing | full | TAA (Halton jitter, neighbourhood clamp) + FXAA shader on disk |
| HDR filmic tone mapping | full | `tonemap.frag` + `glareExtract`/`glareBlur`/`bloomBlur` bloom chain |
| Per-object shadow maps | partial | CSM (`csm.frag`) only, no per-object/spot cookie shadows |
| Real-time local reflections (SSR) | partial | SSR marched inline in `deferred_lighting.frag` (`KS_SSR=1`, default off): world-space march against `gbufWorldPos` as the depth oracle, hit shaded from the hit pixel's albedo/normal + sun, fresnel- and roughness-weighted. Missing vs a real SSR: no roughness mip blur, no sky/environment fallback, no separate AO/reflection target. `ssr.comp` still compiles but stays unwired (it wants `r32f` depth storage images this GBuffer does not expose) |
| Screen-space ambient/directional occlusion | partial | SSAO shipped inline in `deferred_lighting.frag` (`KS_SSAO=1` default off, world-space 16-tap hemisphere folded into the ambient term, no extra image/pass) and pixel-checked by `test_renderer`; HiZ occlusion *culling* (`OcclusionTest.h`, `hiz_downsample.frag`) also present. Still missing: separate AO target, blur, bent normals, AO in the GBuffer |
| Voxel GI / SSGI / IBL | missing | `ssgi*.glsl` dead; no probe/IBL path (sky ambient only) |
| Particle effect system | partial | P1 shipped the real thing at CPU scope: `engine/Graphics/ParticleSystem.{h,cpp}` (fixed 1/120 s steps, cone emitters, gravity/drag, 4096-particle cap, deterministic xorshift seed, `buildQuads()` packing) driven by `SimulationLoop` behind `KS_PARTICLES=1` (default off, dust emitter riding the car), drawn by two `particle.vert` pipelines - alpha blended in the forward pass, written into the GBuffer so deferred lighting lights it. `particle_test` (sim) + `test_renderer` (differential pixel frames on both paths). Still absent vs CRYENGINE: GPU/indirect draw, soft particles (no depth fade), sort-by-depth transparency, forces/fields, decal/sprite emitters |
| Motion blur / depth of field | partial | Motion blur gathered inside the deferred resolve (`taa.frag`, `KS_MOTIONBLUR=1`, default off) from a motion vector reconstructed with `prevViewProj` + GBuffer world positions - no velocity buffer needed. DoF still absent; `motionblur.frag` compiles but no pass consumes it |
| Area lights / lens flares / tessellation / POM | missing | absent |
| Vegetation / terrain / water rendering | partial (terrain) | terrain renders: `src/engine/terrain` (`TerrainMesh` + a new deterministic fBm heightmap generator) is now built and driven by `SimulationLoop` behind `KS_TERRAIN=1` (default off) - 129x129 grid over 1600 m, pressed down under the baked track near the origin, per-vertex tint (the GBuffer takes albedo from vertex colour), registered as a normal ECS `MeshInstance` so it shares the draw list, shadow pass and frustum test. `terrain_test` + `test_renderer` differential frames on both paths. Edited terrain is imported too: a new `FileFormat/PngReader.{h,cpp}` (std-only, 8/16-bit grayscale, filters 0-4, own CRC32 + `inflateZlib`) reads the PNG `TrackTerrainEditor::saveTerrain` writes, and `KS_TERRAIN_PATH=<png>` (+ `KS_TERRAIN_MIN_H`/`KS_TERRAIN_MAX_H` metres, grids above 257 strided down through the LOD path) feeds it straight into the same mesh path. Still missing: `terrain/water/vegetation.vert|frag` remain unwired (interface mismatch, see the dead-assets note), no water plane, no instanced vegetation |
| DX12 / OpenGL / console backends | missing | Vulkan only, Windows only |

### 3.2 Sandbox

| CRYENGINE feature | ksengine / kseditor | Notes |
|---|---|---|
| Sandbox level editor | missing | kseditor is an asset/modding suite (paint, physics, showroom, modelling, sequencer, track builder...) - no free-roam level editing with in-editor play |
| WYSIWYP / hot-update | missing | closest is `KS_HEADLESS` smoke + live sim window, not editor round-trip |
| Material editor | full | shader graph + material library (`materialEditor/ShaderGraphWidget`) |
| FBX support | full | `FbxReader` import + `FBXExporter`; also OBJ/ACD/KN5/DDS/KsAnim/TTF |
| Trackview cinematic editor | partial | `sequencerEditor` timeline exists, no camera/actor tracks bound to the sim |
| Flowgraph / Schematyc visual scripting | missing | scripting is Lua (`Scripting/`, mod SDK `ks.on/dispatch`) + Python in editor - textual only |
| Substance integration | missing | - |
| Resource compiler / asset database streaming | partial | asset manager + dependency graph + LOD/collision generators, no cook/streaming pipeline |

### 3.3 AI & Animation

| CRYENGINE feature | ksengine | Notes |
|---|---|---|
| Advanced AI system | partial | domain-specific: racing line following, curvature feedforward, friction circle, flag limiter (`ai_race_test`); no behaviour trees / tactical point system in the engine (editor has `AIEditor` behaviour trees) |
| Multi-layer navigation mesh | partial | `AI/NavMesh` exists + test, single layer, no auto-generation |
| Parametric skeletal animation | missing in engine | editor has skeletal/IK/blend trees; engine side has shape keys + timeline (`AnimationSystem`, `ShapeKeyAnimDriver`) |
| Skinned rendering path | missing | no bone/`inverseBind` code anywhere in `NativeRenderer`/`RenderSystem`/gbuffer - the sim renders static meshes only |
| Geometry cache / character customization | missing | - |
| Procedural motion warping / IK | partial | IK in kseditor only |

### 3.4 Audio

| CRYENGINE feature | ksengine | Notes |
|---|---|---|
| Audio abstraction / components | full | `SimulatorAudio`, `AudioMixer`, `TrackAudioManager`, `VehicleAudioHook`, `SoundsIniParser` |
| Data-driven sound system | full | AC `sounds.ini` + FMOD bank/FEV/STDT/FSB5 parsing (`BankParserBridge`, `bank_test`) |
| Audio controls editor (ACE) | full | kseditor sound editor + `ksAudioEditor` SDK |
| HRTF spatialization / occlusion / DRS | missing | mixer has panning; no HRTF, no geometric occlusion, no dynamic response system |
| Dynamic music / sound moods | partial | event-driven per-track audio, no music engine |

### 3.5 Physics

| CRYENGINE feature | ksengine | Notes |
|---|---|---|
| Built-in general physics | partial | rigid/raycast vehicle stack, 1 kHz deterministic loop; no broad-phase world with arbitrary rigid bodies, no ragdolls |
| Vehicle physics | **full (beyond CRYENGINE)** | Pacejka MF combined-slip, tyre thermal, suspension, aero, hybrid/ERS, mechanical wear, damage (`test_PhysicsGolden`, `test_determinism`) |
| Destruction / deformables / ropes | missing | damage models are vehicle-internal (`DamageSystem`, `Rf2DamageModel`), no environment destruction |
| Buoyancy / water simulation | missing | - |
| Vegetation touch bending | missing | - |

### 3.6 Performance & Platform

| CRYENGINE feature | ksengine | Notes |
|---|---|---|
| In-game profiling (Statoscope) | missing | counters/logs only |
| Data-driven thread management | partial | fixed 1 kHz sim + 120 Hz ECS tick, deterministic ordering by `priority()` |
| Multi-core render job system | missing | single-threaded submission |
| Multi-platform / consoles / VR | partial | Windows + Vulkan; OpenXR exists in kseditor (VR viewport), not in the sim runtime |
| Determinism / replay / headless CI | **full (beyond CRYENGINE)** | `test_determinism`, `test_PhysicsGolden`, replay binary, `ksengine_c.h` C API, `KS_HEADLESS` |

---

## 4. Where ksengine Wins

- **Domain depth:** telemetry (shared memory, UDP `:20777`, TCP `:20778`), race logic (flags, penalties, pit lane, standings), replay determinism, AC content formats - CRYENGINE has zero of it.
- **Legality/licence:** MIT-style freedom vs CRYENGINE's account-gated private source since 5.7 LTS.
- **CI discipline:** qt-free gate + 122 automated test runs; CRYENGINE ships no comparable open harness.
- **Size/maintainability:** 52k LOC engine is auditable in a day; CRYENGINE is orders of magnitude larger.

## 5. Where ksengine Loses

1. No level-editing layer (Sandbox + Flowgraph + TrackView + WYSIWYP).
2. No general visuals package (GI/full post stack) - SSAO, SSR, motion blur and now particles are in as opt-in effects, and the rest of it is already on disk as shaders that compile but nothing runs.
3. No terrain of its own by default (only what KN5 carries) - `KS_TERRAIN=1` now generates a heightmap-based ground, `KS_TERRAIN_PATH` imports an edited one, but there is no water and no vegetation.
4. No character pipeline (skinning absent from the sim renderer).
5. No profiling/telemetry tooling for the engine itself.

---

## 6. Recommended Roadmap (CRYENGINE-relevant)

| Phase | Focus | Items | Cost |
|---|---|---|---|
| **P0 - Cheap wins** | Ship what already exists | **DONE** SSAO + SSR inline in `deferred_lighting.frag`, motion blur in the resolve, all opt-in (`KS_SSAO`/`KS_SSR`/`KS_MOTIONBLUR`, default off), 12 dead shaders registered in `KsShaders.cmake`, `GPUParticleSystem` stub deleted, `test_renderer` pixel-checks the deferred chain with all three on. The original compute/standalone shaders stay unwired because they assume a GBuffer we do not have | small |
| **P1 - Particles** | FX parity | **DONE** - `engine/Graphics/ParticleSystem.{h,cpp}` CPU sim (fixed-step emitter/cone/gravity/drag/xorshift, `buildQuads()` interleaved sprite packing), `SimulationLoop` drives one car-riding dust emitter behind `KS_PARTICLES` (default off), two `particle.vert` pipelines in `NativeRenderer` (forward alpha blend + GBuffer MRT contribution), `particle_gbuffer.frag` registered, `particle_test` (qt-free) + `test_renderer` differential sprite frames on both paths. Not in scope here: GPU/indirect draw, soft particles, transparency sorting |
| **P2 - Trackside world** | Environment parity | **PARTIAL** - the `TerrainMesh` + heightmap-import halves are done: `src/engine/terrain` (grid -> triangles + a new deterministic fBm heightmap generator) is built and drawn by `SimulationLoop` behind `KS_TERRAIN=1` (default off) through the existing mesh/GBuffer path, and an edited heightmap is imported through the new std-only `FileFormat/PngReader` (`KS_TERRAIN_PATH`, grayscale PNG, the format `TrackTerrainEditor` saves; grids over 257 strided down via `generateTerrainMeshLOD`), with `terrain_test` + `png_test` + `test_renderer` differential frames. Still open: a water plane, instanced vegetation (needs per-instance buffers and a vegetation classifier - `Kn5Baker` drops material metadata), and the three dead forward shaders, which do not match this pipeline | large |
| **P3 - Characters** | Animation parity | skeletal skinning in `gbuffer.vert` (bone palette UBO), reuse kseditor's existing skeleton/IK data | large |
| **P4 - Feedback** | Profiling parity | frame stats overlay + capture (Statoscope-lite), thread queue timings | medium |
| **Not recommended** | Visual scripting, consoles, resource-compiler cook pipeline | out of the racing-sim identity; Lua events already cover the need | - |

---

## 7. Verdict

ksengine is **not trying to be CRYENGINE** and should not be measured as a failing copy of it. Measured on the general-purpose axis it sits at ~25-30% parity: a solid deferred Vulkan renderer, real vehicle physics, real networking and a real test harness, next to a missing world/character/tooling layer.

Measured on the axis that matters - *run an AC-quality race offline, headless, deterministically, with open tools* - ksengine already exceeds CRYENGINE, because CRYENGINE ships none of the telemetry, format, session or determinism machinery.

**Recommendation:** P0 and P1 are done (SSAO, SSR, motion blur, dead shaders under CI, particle system) and P2 has its terrain slice (generated heightmap -> `TerrainMesh` -> existing mesh path, opt-in, plus grayscale heightmap import from the Qt terrain editor). Next: finish P2 (water plane, instanced vegetation) or take P3 (skinning), and explicitly skip visual scripting and console backends - they dilute the racing-sim identity without closing any real gap.
