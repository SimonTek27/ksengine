# AI Brief — Improve ksengine realistic rendering

Purpose: Give this document to an AI coding agent so it can improve the Qt-free Vulkan path of ksengine (NativeRenderer + bake pipeline) toward photorealistic trackside / onboard look, without rewriting the engine.

Project: ksengine (SimonTek27/ksengine) — racing simulator, physics already advanced (KsTireModel, Milliken G1–G18).
Scope of this brief: rendering / materials / lighting / post only. Do not change vehicle dynamics unless a visual debug overlay is required.

Language: Code and comments in English. User-facing docs may be Italian.

## 1. Current baseline (do not regress)

| Component | Location (repo layout) | Status |
|---|---|---|
| Native Vulkan renderer | `src/simulator/NativeRenderer.{h,cpp}` | Forward + deferred + CSM shadows + fog + partial TAA |
| Mesh cache | `.nmsh` / NMS2 (lodIn/lodOut + verts) | LOD distance culling via `src/engine/scene/LodWindow.h` |
| KN5 bake | `src/engine/FileFormat/Kn5Baker.*, Kn5Reader.*` | Writes manifest.txt, *.nmsh, textures/*, materials.txt |
| Texture runtime | `src/simulator/TextureRuntime.{h,cpp}` | DDS (BC1/2/3 → RGBA8), TextureCache, white 1×1 fallback |
| Materials map | `materials.txt` lines: `meshName\talbedoFile` | Loaded by `loadTexturesFromBakeDir` |
| Shaders (GLSL source) | `src/simulator/shaders/native_forward.{vert,frag}`, `gbuffer.{vert,frag}`, deferred, TAA, HiZ | Albedo `sampler2D` at `set = 1, binding = 0`; UV passed as `fragUV` |
| Shader build | `cmake/KsShaders.cmake` | Compiles GLSL → .spv next to sources |
| CMake target | `cmake/CMakeLists_ksimulator_QtFree.cmake` | Must list `TextureRuntime.cpp` |

### Hard constraints

- Stay Qt-free on the simulator path (no QImage / QVulkan).
- Keep NMS2 backward compatible; optional extensions must fail-open for old caches.
- Do not remove LOD window culling or shadow cascades.
- Prefer incremental PRs: materials → IBL → clear-coat → post → track materials.
- Real-time budget: target stable 60 FPS with 1 player car + track LOD (physics is ~1.5 µs/car; GPU is the limiter).

## 2. Goal look (reference)

Visual reference: trackside + onboard footage of historic F1 at Mugello (e.g. Ferrari 412 T1B / Corse Clienti style videos — natural outdoor grading, not cinematic teal-orange).

| Trait | Target |
|---|---|
| Grading | Neutral documentary; mild cool sky, warm body reflections |
| Ferrari / race red | Recognisable Rosso-like, not pure (1,0,0). Starting albedo linear ~(0.82, 0.28, 0.19) then layer clear-coat |
| Contrast | High dynamic range under sun; specular peaks near white; deep shadow under wings |
| Asphalt | Cool grey ~0.45–0.50 linear, visible roughness variation, not plastic |
| Avoid | Oversaturated Instagram LUT, huge bloom, pure mirror chrome everywhere |

## 3. Priority backlog (implement in order)

### P0 — Make existing texture path reliable

- Ensure `native_forward` and `gbuffer` SPIR-V are rebuilt from updated GLSL (`fragUV` + `albedoMap`).
- Pipeline layout must use two descriptor sets: `set0` = frame UBO + shadows; `set1` = albedo.
- `bindAlbedoForMesh` must run every draw; missing texture → white 1×1.
- Mip generation or at least `VK_SAMPLER_MIPMAP_MODE_LINEAR` with correct mip count if images gain mips.
- Anisotropic filtering on albedo sampler (max available, at least 8).

Done when: baked KN5 car/track shows correct diffuse textures in both forward and deferred, no pink/black missing binds.

### P1 — Full PBR material maps

Extend bake + runtime beyond single albedo:

| Map | Purpose |
|---|---|
| Albedo / base color | Already |
| Roughness | Micro-surface (paint ~0.25–0.4 clear-coat under; carbon higher) |
| Metalness | 0 paint, ~1 bare metal / some foils |
| Normal | Tangent-space detail (carbon weave, panel gaps) |
| Optional AO | Multiplied into diffuse |

Suggested file convention (next to `textures/`):

```
materials.txt          # mesh \t albedo \t roughness \t metalness \t normal
# or separate:
# materials.json
```

Update `Kn5Baker` to emit roughness/metalness defaults from KN5 shader property heuristics when maps are absent (paint: rough 0.35 metal 0; carbon: rough 0.5 metal 0).

Shader: sample maps in gbuffer write (RT0 albedo, RT1 normal+roughness packing, metalness in a channel or RT2).

Done when: body paint vs matte floors vs metallic exhaust read as different materials under the same light.

### P2 — Image-based lighting (IBL)

- Load one HDR (or RGBM) environment (start with a procedural sky + ground albedo if HDR pipeline is heavy).
- Prefilter specular GGX mips + irradiance (or SH) offline or at load.
- Ambient diffuse from irradiance; specular from prefiltered env × BRDF LUT (split-sum).
- Keep directional sun + CSM as dominant hard light; IBL fills shadows and reflections.

Done when: car body shows sky/horizon reflection, not only flat ambient * 0.25.

### P3 — Clear-coat (race paint)

Approximate multi-layer paint:

- Base: albedo * (1−F) diffuse + specular with roughness_base
- Coat: specular lobe roughness ~0.05–0.1, F0 ~0.04, energy weakly coupled

Can be a simplified second specular term in deferred lighting and forward.

Done when: red body has sharp specular highlight distinct from broader base reflection (Mugello sun tick on nose/engine cover).

### P4 — Post-process stack

Order suggestion:

1. HDR colour from lighting
2. Optional light bloom (threshold high, small radius — avoid foggy glow)
3. ACES or Filmic tonemap
4. Exposure / EV bias (user cvar)
5. Mild vignette optional
6. TAA (already partial) + light sharpen

Match reference: natural outdoor, not showroom.

Done when: no raw HDR washed blacks; speculars roll off cleanly.

### P5 — Track / world materials

- Asphalt: albedo + normal + roughness (puddle-free default).
- Kerbs Mugello-style: high contrast paint, not emissive.
- Grass/gravel: low specular.
- Contact-friendly AO: screen-space AO or cheap ground contact shadow under chassis.

Done when: car sits in the scene instead of floating on uniform grey.

### P6 — Polish (optional)

- Tyre sidewall marks, brake glow (link to `brakeDiscTemp` telemetry if available)
- Soft heat haze on long focal length
- Exhaust heat (very subtle)
- Shadow contact fix for thin wings

## 4. Architecture guidance for the AI implementer

### Descriptor sets (target layout)

```
set = 0  (per frame)
  binding 0  UBO FrameData (sun, cascades, camera, fog, …)
  binding 1  sampler2DArray shadowCascades

set = 1  (per draw / per material)
  binding 0  albedo
  binding 1  roughness / metalness (packed or separate)
  binding 2  normal
  binding 3  optional AO

set = 2  (per frame, IBL)  [add in P2]
  binding 0  irradiance cube / SH
  binding 1  prefiltered env
  binding 2  BRDF LUT
```

### FrameData UBO

Do not reorder existing members used by current shaders without updating all of: `native_forward.frag`, `deferred_lighting.frag`, `taa.frag`, CPU `FrameDataUBO`. Append new fields at the end.

### NativeMesh extensions

```cpp
std::string albedoTexture;
std::string roughnessTexture;  // optional
std::string metalnessTexture;  // optional
std::string normalTexture;     // optional
// or single materialId into a MaterialTable
```

### Performance rules

- No per-draw pipeline switch if avoidable; use material texture binds.
- Atlas or bindless later; start with update-set + single albedo/roughness/normal.
- LOD already culls meshes — do not draw off-window KN5 props.
- Prefer one HDR env shared for the whole session.

## 5. Testing checklist

- Load bake dir with `manifest.txt` + `materials.txt` + `textures/`.
- Forward path: textured car under sun + shadows.
- Deferred path: same, no black GBuffer.
- Missing texture → white, no validation spam.
- Old NMSH (no LOD pair) still loads.
- Toggle IBL intensity; scene remains stable.
- 60 FPS target on mid GPU with one car + track LOD.

## 6. Explicit non-goals

- Do not port Qt editor material graph into the simulator runtime.
- Do not require RTX hardware.
- Do not rewrite physics or networking.
- Do not add motorcycle / boat / aircraft render paths.
- Do not depend on online asset download at runtime.

## 7. Deliverables expected from the implementing AI

- Code in-tree under `src/simulator/`, `src/engine/FileFormat/`, `src/simulator/shaders/`, CMake updates.
- Rebuild SPIR-V via existing `KsShaders.cmake` (or document exact glslc commands).
- Short CHANGELOG section: what was added, how to bake, how to verify.
- If format changes: document materials.txt / NMS version bump and fail-open behaviour.
- Optional: one debug view mode (albedo / roughness / normals / metalness) toggled by cvar or key.

## 8. Suggested implementation order (sprints)

| Sprint | Focus | Exit criteria |
|---|---|---|
| S1 | P0 texture bind + mip/aniso + SPV | Textured car on track |
| S2 | P1 roughness/metal/normal bake+sample | Material differentiation |
| S3 | P2 IBL | Believable reflections |
| S4 | P3 clear-coat + P4 tonemap | Paint + exposure look |
| S5 | P5 track materials + AO | Grounded scene |

## 9. File touch list (starting points)

- `src/simulator/NativeRenderer.h`
- `src/simulator/NativeRenderer.cpp`
- `src/simulator/TextureRuntime.h`
- `src/simulator/TextureRuntime.cpp`
- `src/simulator/shaders/native_forward.vert`
- `src/simulator/shaders/native_forward.frag`
- `src/simulator/shaders/gbuffer.vert`
- `src/simulator/shaders/gbuffer.frag`
- `src/simulator/shaders/deferred_lighting.frag`
- `src/engine/FileFormat/Kn5Baker.h`
- `src/engine/FileFormat/Kn5Baker.cpp`
- `cmake/CMakeLists_ksimulator_QtFree.cmake`
- `cmake/KsShaders.cmake`
- `docs/ROADMAP.md` — mark render milestones

## 10. Prompt snippet (paste to coding AI)

> You are improving ksengine's Qt-free Vulkan renderer for realistic racing visuals.
> Read `RENDERING_AI_BRIEF.md` in full and implement Sprint S1 (P0) first unless asked otherwise.
> Follow hard constraints: no Qt on simulator path, keep NMS2/LOD, fail-open materials, append UBO fields only at end.
> Match the Mugello trackside reference look: natural grading, believable red paint, no heavy cinematic LUT.
> After each sprint, list files changed and how to verify.

## 11. Context for physics coupling (optional later)

Telemetry already available for future visual polish (do not block P0–P4):

- `brakeDiscTemp[]`, `brakeFade[]` — brake glow
- tyre wear / temp — sidewall dirt / thermal colour
- aeroDownforce, speed — optional deformation is out of scope
