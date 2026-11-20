#pragma once
// LocalLight and the tile table the deferred lighting pass reads.
#include "LocalLights.h"
// Per-material G-buffer overrides (video.gbufferOverrides).
#include "GBufferLayout.h"
// The frame-generation / super-resolution input contract this renderer
// publishes every frame (see frameGenInputs()).
#include "FrameGenInputs.h"

// Qt-free native Vulkan renderer for the SimulatorApp runtime.
//
// Why this exists: ks::VulkanRenderer (src/engine/Graphics/VulkanRenderer.h)
// is a QObject that uses QVulkanWindow/QVulkanInstance/QString/QMap/QImage
// throughout, and ks::engine::graphics::RenderSystem (the class that used to
// stand in for "the frame loop" in SimulationLoop::tick()) is a QObject too
// — and was, on inspection, an empty stub: its beginFrame()/endFrame() never
// recorded or submitted a single Vulkan command. Both of those are real
// blockers for "SimulatorApp must start without Qt", independent of any
// feature work (shadows, bloom, ...) built on top of them.
//
// NativeRenderer is a from-scratch replacement for the *subset* SimulatorApp
// actually needs: device/swapchain management (device/surface are created by
// SimulatorApp.cpp itself already, with raw vkCreateInstance/vkCreateWin32SurfaceKHR),
// a real per-frame command buffer with an actual render pass, and mesh
// storage using std::string/std::vector instead of QString/QVector/QMap.
//
// What this does NOT yet solve: SimulationLoop::loadTrack()/loadCar() build
// their scene meshes by parsing .kn5 files through KN5Parser, which returns
// Qt-typed data (QVector<SceneVertex> etc.) and uses QString::fromStdString
// internally. That is a second, separate Qt dependency in the content
// pipeline, not the renderer — rewriting KN5Parser itself is out of scope
// here (it's shared with the Qt editor and touching it blind, without a way
// to compile-test, risks breaking real AC content loading). The intended
// shape of the fix is an offline "bake" step: the Qt-based editor converts
// .kn5 -> a simple Qt-free mesh cache (see NativeMesh::loadFromFile below),
// and SimulatorApp only ever reads the baked format at runtime, never KN5Parser
// or QString. NativeRenderer::loadMeshFromFile() already speaks that baked format.
//
// A second producer of the same .nmsh format exists: ks::engine::terrain
// (src/engine/terrain/TerrainMesh.h) generates real triangle meshes from
// TrackTerrainEditor's heightmap data and can write them straight to .nmsh
// via TrackTerrainEditor::exportForSimulator() — so hand-edited terrain
// reaches SimulatorApp through this exact same loading path as baked KN5
// content, no separate runtime format needed.

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <memory>
#include "MathTypes.h"
#include "engine/Math/Frustum.h"
#include "engine/scene/LodWindow.h"
// Roadmap E9 / gate 2.14: VK_QUERY_TYPE_TIMESTAMP pools written along the
// endFrame() record path (see gpuProfiler() below).
#include "engine/physics/GpuProfiler.h"
#include "ui/VulkanUiPipeline.h"

namespace ks::sim {

class CascadedShadowMap;
class LocalShadowAtlas;
class TextureRuntime;
// Defined in MaterialCache.h (materials.txt sidecar reader); only ever used
// through a pointer here so the header stays free of that dependency.
struct MeshMaterial;

namespace ui {
class UiGpuPass;
class UiRenderer;
}

// Matches ks::VulkanRenderer::Vertex's layout (position, normal, uv, color =
// 3+3+2+4 floats) so baked meshes and shadow-pass vertex binding stay
// compatible with the existing shader set (pbr.vert, ksShadow.vert).
struct NativeVertex {
    float px = 0, py = 0, pz = 0;
    float nx = 0, ny = 0, nz = 0;
    float u = 0, v = 0;
    float r = 1, g = 1, b = 1, a = 1;
};
static_assert(sizeof(NativeVertex) == sizeof(float) * 12, "NativeVertex must stay tightly packed to match the shadow pipeline's vertex stride");

struct NativeMesh {
    // A backdrop must not cast: a closed sky dome would put its whole interior in
    // shadow, and then nothing inside it could cast a shadow of its own. It still
    // receives, so a car standing in the room does darken the floor.
    bool castShadow = true;
    std::vector<NativeVertex> vertices;
    std::vector<uint32_t> indices;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    // Local-space AABB, computed once when the mesh is uploaded. Empty meshes
    // leave hasBounds false and are never culled (there is nothing to test).
    vec3 boundsMin{};
    vec3 boundsMax{};
    bool hasBounds = false;
    // KN5 authored distance window (NMS2 caches): the instance is queued only
    // while the camera sits within [in, out] metres — see LodWindow.h.
    // Defaults = no window, so runtime-generated meshes (solids, terrain,
    // placeholders) and legacy NMSH caches render exactly as before.
    ks::scene::LodWindow lod;
    // Roadmap 2.3 — per-mesh PBR material resolved from materials.txt at
    // load time (defaults when no row exists). The renderer creates
    // descriptor set 1 (albedo/normal/roughness/metalness samplers +
    // MaterialUBO) from these in setMesh(); empty texture paths bind
    // TextureRuntime's fallbacks.
    // What the mesh was built from, kept for the showroom inspector: the KN5
    // material name (several meshes can share one) and the shader the content
    // gave it. Empty for runtime-generated meshes (solids, placeholders).
    std::string materialName;
    std::string shaderName;
    std::string albedoTexture;   // resolved path, "" = none authored
    std::string normalTexture;   // resolved path, "" = none authored
    // P1 — resolved PBR map paths ("" = the materials.txt cell held a
    // scalar; the shader then multiplies by the white fallback = identity).
    std::string roughnessTexture;
    std::string metalnessTexture;
    float roughness = 0.35f; // identity 1.0 when roughnessTexture is set
    float metalness = 0.0f;  // identity 1.0 when metalnessTexture is set
    // P3 — clear-coat flag from materials.txt cell 6 (>= 0.5 = coated).
    // Static per mesh, rides the same std140 block as the scalars above.
    float clearcoat = 0.0f;
    // KN5 alpha test (material.alpha_tested + ksAlphaRef): fragments under the
    // reference are discarded, so decals, grilles and light covers stop
    // rendering as opaque cards.
    bool alphaTested = false;
    float alphaRef = 0.5f;
    // ksEmissive, added unlit on top of the shaded result.
    float emissive = 0.0f;
    // A photographic surface (a showroom hall that is one image of a place):
    // its texture already holds the light of the room it shows, so the shader
    // displays it as it is instead of shading the photograph again.
    float unlit = 0.0f;
    // Glass: opacity scales the albedo alpha, glassiness (0..1) switches the
    // shading to the pane model (reflection instead of diffuse).
    float opacity = 1.0f;
    float glassiness = 0.0f;
    // Authored KN5 surface response the presets cannot know: ksSpecular
    // scales the dielectric F0 (0.5 = the legacy 0.04), and the fresnel
    // triplet is what AC's reflection shaders use to blend a pane in at
    // grazing angles (fresnelMaxLevel 0 = the file authored none).
    float specular = 0.5f;
    float fresnelC = 0.0f;
    float fresnelExp = 5.0f;
    float fresnelMax = 0.0f;
    // ksAmbient: the multiplier AC's shader puts on the ambient light (see
    // MaterialResponse.h). The legacy 0.25 neutral keeps un-authored content
    // exactly as it rendered before this field existed.
    float ksAmbient = 0.25f;
    // material.blend != 0 (glass, windscreen, decals): drawn after the
    // opaque pass, blended over what is already there, no depth write.
    bool blended = false;
    // The grass field's blades: drawn by their own pipeline after the blended
    // surfaces, with the wind evaluated in the vertex shader.
    bool grass = false;
    // Bodywork paint tint (CARS colour): multiplies the albedo in both the
    // forward and the GBuffer shader. 1,1,1 = as baked.
    float tint[3] = {1.0f, 1.0f, 1.0f};
    // 1 = tint replaces the albedo (an explicit paint over a livery), 0 = the
    // tint multiplies it (painting a body that shows its own texture).
    float paintSolid = 0.0f;
    // Detail map (KN5 txDetail): multiplies the albedo, tiled. Empty or
    // detailScale 0 leaves the base map untouched.
    std::string detailTexture;
    float detailScale = 0.0f;
    // Multiplier for the detail map: 2.0 neutralises AC's 0.5 grain, 1.0 is
    // used when the detail slot carries an occlusion bake instead.
    float detailGain = 2.0f;
    // AC txEmissive: the colour a self-lit surface emits ("" = none, which
    // leaves the ksEmissive term exactly as the file authored it).
    std::string emissiveTexture;
    // Brief P6 — brake-disc flag, derived from the mesh NAME in setMesh()
    // ("disc" appears, case-insensitive; fail-open: no match = no glow).
    // Static per mesh, same std140 block; the per-frame heat intensity is
    // FrameData.brakeGlow (setBrakeGlow), so old bakes simply never match
    // and render exactly as before.
    float brakeDisc = 0.0f;
    // Descriptor set 1 for this mesh + its 32-byte std140 material block
    // (grew 16 -> 32 when the P6 brake-disc flag joined the scalars).
    // Null until createMaterialDescriptor() succeeds (or after it fails);
    // the draw path then binds the default material set instead.
    VkDescriptorSet materialSet = VK_NULL_HANDLE;
    VkBuffer materialUbo = VK_NULL_HANDLE;
    VkDeviceMemory materialUboMemory = VK_NULL_HANDLE;
};

struct DirectionalLight {
    vec3 direction{0.3f, -0.8f, 0.2f};
    vec3 color{1.0f, 0.95f, 0.9f};
    float intensity = 1.0f;
};

class NativeRenderer {
public:
    ~NativeRenderer();
    // Declared here, defaulted in the .cpp: m_textureRuntime is a
    // unique_ptr to a forward-declared type, and an inline defaulted
    // constructor needs the complete type (unwind cleanup) — TUs that
    // include this header without TextureRuntime.h would not compile.
    NativeRenderer();
    NativeRenderer(const NativeRenderer&) = delete;
    NativeRenderer& operator=(const NativeRenderer&) = delete;

    // instance/surface are created by the caller (SimulatorApp.cpp already
    // does this with raw vkCreateInstance/vkCreateWin32SurfaceKHR).
    bool createDevice(VkInstance instance, VkSurfaceKHR surface,
                      VkPhysicalDevice preferredPhysicalDevice = VK_NULL_HANDLE,
                      const std::vector<std::string>& extraDeviceExtensions = {});
    bool createSwapChain(VkSurfaceKHR surface, uint32_t width, uint32_t height);
    bool recreateSwapChain(uint32_t width, uint32_t height);
    // Depth-only shadow cascades are wired in here if a shadow map is
    // attached (see attachShadowMap()) — geometry submitted via drawMesh()
    // between beginFrame()/endFrame() is automatically also rendered into
    // each active cascade before the main color pass.
    void attachShadowMap(CascadedShadowMap* shadowMap) { m_shadowMap = shadowMap; }
    // Local lights (point/spot) get the same treatment from their own atlas:
    // one layer per shadowed face, rendered with the same ksShadow pipeline
    // before the GBuffer pass, and sampled by the deferred lighting shader
    // through the light records.
    void attachLocalShadowAtlas(LocalShadowAtlas* atlas) { m_localShadowAtlas = atlas; }

    bool isInitialized() const { return m_device != VK_NULL_HANDLE; }
    void shutdown();

    // shaderDir must contain precompiled pbr.vert.spv/pbr.frag.spv (or a
    // simpler forward-lit fallback — see NativeRenderer.cpp) and
    // ksShadow.vert.spv/ksShadow.frag.spv if a shadow map is attached.
    bool loadPipelines(const std::string& shaderDir);

    // Loads a pre-baked, Qt-free mesh cache (see the file note above for why
    // this exists instead of parsing .kn5 directly). Format: a tiny custom
    // binary — 4-byte magic + uint32 vertexCount + uint32 indexCount, then
    // the raw NativeVertex array, then the raw uint32 index array. Magic
    // "NMS2" (kn5baker output) inserts the KN5 distance window — f32 lodIn,
    // f32 lodOut — between the counts and the vertices; magic "NMSH" is the
    // legacy layout (no window, mesh always queued).
    //
    // material/textureDir (Roadmap 2.3): when a materials.txt row is passed,
    // its texture references are resolved against textureDir (the bake's
    // textures/ directory) and its PBR values are stored on the mesh before
    // upload, so setMesh() creates the right material descriptor. Both
    // default to "no authored material" — pure geometry loads unchanged.
    bool loadMeshFromFile(const std::string& name, const std::string& path,
                          const MeshMaterial* material = nullptr,
                          const std::string& textureDir = {});
    // Reads <dir>/manifest.txt (one mesh name per line, as written by the
    // kn5baker tool) and calls loadMeshFromFile(name, dir+"/"+name+".nmsh")
    // for each, pairing every mesh with its optional <dir>/materials.txt row
    // (roughness/metalness/albedo/normal, see MaterialCache.h) and resolved
    // <dir>/textures/ paths. Returns the number of meshes successfully
    // loaded. Meshes
    // loaded this way are remembered as "static scene" meshes — see
    // drawStaticScene() — since baked track/prop geometry is already in
    // world space (kn5.worldMatrix was applied at bake time) and should be
    // drawn at identity every frame, unlike instanced meshes (e.g. a car)
    // that need a per-frame model matrix from drawMesh().
    int loadMeshesFromManifest(const std::string& dir);
    // Same as setMesh() but also registers the mesh as static-scene geometry,
    // so drawStaticScene() submits it every frame at the identity transform.
    // Used by SimulationLoop when it swaps in a freshly parsed .kn5 track.
    void setStaticMesh(const std::string& name, const NativeMesh& mesh);
    // Destroys every static-scene mesh (their GPU buffers included) and
    // forgets them. Called before uploading a newly loaded track so the old
    // layout does not linger on top of the new one. Instanced meshes such as
    // "player_car" are left untouched.
    void clearStaticScene();
    // Queues every mesh loaded via loadMeshesFromManifest() at the identity
    // transform. Call once per frame, before or after any drawMesh() calls
    // for instanced objects.
    void drawStaticScene();
    void setMesh(const std::string& name, const NativeMesh& mesh);
    void destroyMesh(const std::string& name);
    /** The mesh as it was uploaded, CPU side: geometry, bounds and the material
     *  numbers. The showroom inspector reads it for the pick and for the
     *  property sheet; null when no mesh of that name is registered. */
    const NativeMesh* findMesh(const std::string& name) const {
        const auto it = m_meshes.find(name);
        return it == m_meshes.end() ? nullptr : &it->second;
    }
    /** The projection the current frame is drawn with (jitter excluded), for
     *  screen-space picking. */
    mat4 viewProjection() const { return m_proj * m_view; }
    /** Re-tints one mesh in place: the per-mesh material block is host-visible,
     *  so this rewrites its bytes instead of replacing the mesh's buffers, and
     *  a frame already submitted keeps reading valid memory. */
    bool setMeshTint(const std::string& name, const float tint[3], float paintSolid);

    // near/far are forwarded to the shadow cascade split computation so the
    // cascades cover exactly the depth range the camera renders.
    void setCamera(const mat4& view, const mat4& proj,
                   float nearPlane = 0.5f, float farPlane = 500.0f) {
        m_view = view;
        m_proj = proj;
        m_camNear = nearPlane;
        m_camFar = farPlane;
        mat4 invView = view.inverse();
        m_camPosWS = vec3(invView(0, 3), invView(1, 3), invView(2, 3));
        // Marks the camera matrices as authoritative. Frustum culling stays
        // disabled until this happens — an identity view/projection would
        // otherwise yield a frustum that culls nearly the whole scene.
        m_cameraValid = true;
    }
    // The sun lights the frame and, since PhysicalSky.h, also *makes* the sky:
    // the IBL environment is baked from the same atmosphere with the same sun,
    // so a material change of direction re-bakes it (see refreshSkyIbl()).
    void setSun(const DirectionalLight& light);
    // A room owns the environment while it is open. The menu runs the weather
    // branch even with a showroom on screen, and its sun is the track's, not the
    // room's: freezing the maps here stops that sun from re-baking them, and
    // bakeSkyForSun() is how the room's own light is baked once, before the
    // freeze.
    void setSkyIblFrozen(bool frozen) {
        if (m_iblFrozen != frozen)
            std::fprintf(stderr, "[ibl] sky maps %s\n",
                         frozen ? "frozen: the room's sun owns them" : "live with the sun again");
        m_iblFrozen = frozen;
    }
    // False only while the maps do not exist yet (the caller retries).
    bool bakeSkyForSun(const DirectionalLight& light);
    // The room's reflection probe was the environment while the hall was on
    // screen, and setSun() refuses to re-bake over it: the track that replaces
    // the hall has to get the sky back. Clearing the bake stamp lifts the rate
    // limit too, so the first sun the track sets bakes at once instead of
    // leaving the track under the room's probe for ~120 frames.
    void releaseProbeEnvironment() {
        if (!m_iblFromProbe) return;
        std::fprintf(stderr, "[ibl] the room's probe released: the sky is the environment again");
        std::fputc(10, stderr);
        m_iblFromProbe = false;
        m_iblSkyBakedAt = 0;
        // The probe's maps are what the textures hold, and setSun() skips a
        // bake when the sun matches the last one baked (a track started on the
        // default hour does): no direction matches this, so the next sun the
        // track sets really rebuilds them.
        m_iblSunToward = vec3{0.0f, 0.0f, 0.0f};
    }

    // --- Native local lights (the engine's own Lighting FX) ---------------
    // Point and spot lights beyond the sun and the IBL. Every frame the renderer
    // bins them into screen tiles on the CPU (LocalLights.h) and the deferred
    // lighting pass walks only its own tile's list, so the per-pixel cost is the
    // lights that can reach the pixel instead of every light in the scene. An
    // empty list is a frame lit by the sun and the IBL alone, which is exactly
    // how every frame was lit before this existed. The forward path
    // (KS_RENDER=forward) still ignores them: only the deferred lighting pass
    // reads the table.
    void setLocalLights(const std::vector<ks::sim::LocalLight>& lights);
    std::size_t localLightCount() const { return m_localLights.size(); }
    int localLightTiles() const { return m_lightTilesX * m_lightTilesY; }

    // Linear height fog, applied by both the forward and the deferred path
    // (the deferred path previously hard-coded these constants in GLSL, so a
    // weather change could not reach the shader at all).
    void setFog(const vec3& color, float density, float heightFalloff) {
        m_fogColor = color;
        m_fogDensity = density;
        m_fogHeightFalloff = heightFalloff;
    }

    // Per-frame culling counters, mostly there so the win/loss of culling can
    // be observed without a profiler: submitted = drawMesh() calls received,
    // drawn = what reached the command buffer, culled = rejected by the
    // frustum test, occluded = rejected by the previous-frame depth grid.
    struct FrameStats {
        uint32_t submitted = 0;
        uint32_t drawn = 0;
        uint32_t culled = 0;
        uint32_t occluded = 0;
        // The mirror tile is its own eye (stage 2): the size of the list the
        // rear-view scene pass actually replayed last frame.
        uint32_t mirrorDrawn = 0;
        // ... and how many the tile's own grid rejected before its list.
        uint32_t mirrorOccluded = 0;
    };
    void setFrustumCulling(bool enabled) { m_frustumCulling = enabled; }
    bool frustumCulling() const { return m_frustumCulling; }
    // Previous-frame depth occlusion culling (see engine/Math/OcclusionTest.h
    // for the predicate). On by default; KS_OCCLUSION_CULL=0 turns it off,
    // and it disables itself if the downsample pass or the readback cannot
    // be built (missing shader, no host-visible memory, ...).
    void setOcclusionCulling(bool enabled) { m_occlusionCulling = enabled; }
    bool occlusionCulling() const { return m_occlusionCulling; }
    const FrameStats& lastFrameStats() const { return m_stats; }

    /** What a frame-generation or super-resolution provider is handed for the
     *  frame just drawn (FrameGenInputs.h): camera and clip matrices, the
     *  sub-pixel jitter, sizes, and which input textures exist. Filled in
     *  endFrame() on both render paths; a provider adapter reads this, never
     *  the renderer's private state, and asks framegen::ready() whether the
     *  frame can be reconstructed at all. */
    const ks::sim::framegen::Inputs& frameGenInputs() const { return m_frameGenInputs; }

    /** How far the camera jumped between the last two frames, and whether the
     *  last frame was published as a cut (frameGenInputs().reset). */
    ks::sim::framegen::PoseJump lastCameraJump() const {
        ks::sim::framegen::PoseJump jump;
        jump.translation = m_lastPoseJumpMetres;
        jump.rotationDegrees = m_lastPoseJumpDegrees;
        return jump;
    }

    /** The GBuffer's velocity target (RG16F, render pixels, y down): the
     *  `motion` role of the contract, sampleable while
     *  frameGenInputs().motionAvailable is true. Null on the forward path. */
    VkImageView velocityView() const { return m_velocityView; }
    // Names already reported as "queued but never uploaded", so the warning
    // is emitted once per mesh rather than once per frame.
    std::set<std::string> m_missingBufferLogged;

    // Deferred lighting path. Off by default: endFrame() keeps running the
    // exact forward pipeline it always has until this is turned on, so
    // enabling it is a pure opt-in and cannot regress the default image.
    // When on, the draw list goes through a GBuffer MRT (albedo / normal /
    // world position) and a fullscreen lighting + volumetric fog pass
    // instead. Resources are built lazily on the first frame that needs
    // them and torn down with the swapchain on resize.
    void setDeferred(bool enabled) { m_deferred = enabled; }
    bool isDeferred() const { return m_deferred; }

    // Temporal AA. Implies the deferred path (the resolve pass needs the
    // GBuffer world positions to reproject), so setTaa(true) alone is enough
    // to get deferred + TAA: the main-pass projection is jittered by a
    // Halton(2,3) sub-pixel offset and the lighting result is blended
    // against the reprojected previous frame.
    void setTaa(bool enabled) { m_taa = enabled; }
    bool isTaa() const { return m_taa; }

    // Screen-space ambient occlusion (roadmap ksengine-vs-cryengine P0).
    // Computed inline by the deferred lighting pass out of the GBuffer it
    // already reads, so it needs no extra image or pass; like TAA it is
    // deferred-only, and setSsao(true) alone is not enough - setDeferred(true)
    // has to be called too (done for you by SimulatorApp when KS_SSAO=1).
    // radius is in world units, intensity 0..1 scales how much of the
    // computed occlusion reaches the ambient term, bias rejects self-hits on
    // the occluding surface itself.
    void setSsao(bool enabled, float radius = 0.5f, float intensity = 1.0f,
                 float bias = 0.02f) {
        m_ssao = enabled;
        m_ssaoRadius = radius;
        m_ssaoIntensity = intensity;
        m_ssaoBias = bias;
    }
    bool isSsao() const { return m_ssao; }

    // Screen-space reflections (roadmap ksengine-vs-cryengine P0). Marched
    // inline by the deferred lighting pass against the GBuffer world
    // positions, so - like SSAO - no extra image, pass or descriptor: the
    // hit is shaded from the hit pixel's albedo/normal with the sun, which
    // is an approximation of a lit reflection. maxDistance is in world units,
    // roughnessThreshold is the cut-off above which reflections fade out
    // (there is no mip chain to blur them with).
    void setSsr(bool enabled, float maxDistance = 8.0f, float intensity = 1.0f,
                float roughnessThreshold = 0.6f) {
        m_ssr = enabled;
        m_ssrMaxDistance = maxDistance;
        m_ssrIntensity = intensity;
        m_ssrRoughnessThreshold = roughnessThreshold;
    }
    bool isSsr() const { return m_ssr; }
    // Screen-space indirect bounce (SSGI). Gathered inline by the deferred
    // lighting pass from the neighbouring GBuffer pixels, exactly like SSAO and
    // SSR, so it costs no target and no extra pass; radius is in screen UV.
    void setSsgi(bool enabled, float radius = 0.05f, float intensity = 1.0f) {
        m_ssgi = enabled;
        m_ssgiRadius = radius;
        m_ssgiIntensity = intensity;
    }
    bool isSsgi() const { return m_ssgi; }

    /** Per-material G-buffer overrides loaded from video.gbufferOverrides.
     *  A material with disableGBuffer is not drawn into the opaque targets;
     *  the transparent pass still draws it over the lit image. */
    void setGBufferLayouts(const GBufferLayoutManager& layouts) {
        m_gbufferLayouts = layouts;
    }
    const GBufferLayoutManager& gbufferLayouts() const { return m_gbufferLayouts; }

    // Motion blur (roadmap ksengine-vs-cryengine P0), gathered inside the
    // deferred resolve pass from a motion vector reconstructed out of the
    // world positions and the previous view-projection, so no velocity buffer
    // is needed. strength scales the raw screen-space motion, maxLength caps
    // the gather radius as a fraction of the screen (0.03 = 3%).
    void setMotionBlur(bool enabled, float strength = 1.0f, int samples = 8,
                       float maxLength = 0.03f) {
        m_motionBlur = enabled;
        m_mbStrength = strength;
        m_mbSamples = samples > 0 ? samples : 1;
        m_mbMaxLength = maxLength;
    }
    bool isMotionBlur() const { return m_motionBlur; }

    // Image-based lighting (brief P2 / S3). The three precomputed maps
    // (procedural sky prefilter chain, irradiance, BRDF LUT — see
    // IblGenerator.h) are uploaded once when the device comes up, and the
    // split-sum ambient replaces the flat `albedo * 0.25` in both the
    // forward and the deferred lighting shader when this is on. Fail-open:
    // if generation or upload fails the renderer keeps valid 1x1 textures
    // bound and iblParams.x stays 0, so the shader falls back to the exact
    // legacy ambient. The sun/CSM path is untouched — IBL only fills.
    void setIblEnabled(bool enabled) { m_iblWanted = enabled; }
    bool isIblEnabled() const { return m_iblWanted; }

    /** Rebuilds the IBL from the car's own reflection probe — AC ships it as an
     *  equirect *_Ref map — instead of the procedural sky, so the body and the
     *  chrome reflect the environment the car reflects in AC. Called once after
     *  the car is loaded and before the showroom's first frame: it re-uploads
     *  into the existing images, so every descriptor set written at pipeline
     *  creation keeps pointing at a live view. False leaves the sky in place. */
    bool setCarReflectionEnvironment(const std::vector<unsigned char>& rgba8, int width,
                                     int height);

    // Display transform of the deferred path (the tonemap.frag pass that now
    // owns the backbuffer). Exposure and the tone-curve operator are the two
    // knobs the SDR and HDR10 encodings share; modes: 0 none, 1 Reinhard,
    // 2 ACES, 3 Uncharted2, 4 Filmic.
    void setExposure(float e) { m_exposure = e; }
    float exposure() const { return m_exposure; }

    // Brief P6 — brake glow. [0..1] radiance multiplier for meshes flagged
    // as brake discs (NativeMesh::brakeDisc, name heuristic in setMesh()):
    // SimulationLoop maps the hottest disc temperature onto this range
    // (the physics 400..800 C fade anchor), the shaders add a fixed
    // blackbody-ish emissive scaled by it. 0 = truly off (cold discs,
    // no vehicle, headless builds); clamped so a bad caller can't push
    // HDR values through this signal.
    void setBrakeGlow(float g) { m_brakeGlow = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g); }
    float brakeGlow() const { return m_brakeGlow; }
    // Track wetness 0..1 (SimulationLoop mirrors the weather state here every
    // frame). FrameData.wetParams.x, read by both scene shaders, which apply
    // what WetLook.h describes: darker, glossier, with a sky sheen on the
    // surfaces that face up. 0 leaves every surface byte-identical, so a dry
    // or paused-weather frame is exactly the image it was before.
    void setWetness(float wetness) {
        m_wetness = !(wetness > 0.0f) ? 0.0f : (wetness > 1.0f ? 1.0f : wetness);
    }
    float wetness() const { return m_wetness; }

    // --- Mirrors, stage 1: one real-time rear view, rendered to its own
    // target every frame with the same forward draw list, and shown as a
    // screen overlay the way AC's own "virtual mirror" tile is (the mirror
    // meshes' own glass sampling is the follow-up). The target builds
    // lazily on the first enabled frame, so a session that never turns it
    // on pays nothing.
    void setVirtualMirror(bool enabled) { m_mirrorEnabled = enabled; }
    bool virtualMirror() const { return m_mirrorEnabled; }
    /** Target size for the next (re)build, clamped 64..1024 and forced
     *  landscape (the rear view is wider than it is tall); 0/0 keeps the
     *  built-in default. Applied where video.mirrorResolution says. */
    void setMirrorResolution(uint32_t width, uint32_t height) {
        m_mirrorWantedW = width;
        m_mirrorWantedH = height;
    }
    // Opt-in mesh that samples the rear-view tile instead of the fixed
    // screen quad (mirror stage 3). Empty keeps the legacy quad.
    void setMirrorGlassMesh(const std::string& name) { m_mirrorGlassMeshName = name; }
    void setTonemapMode(int mode) { m_tonemapMode = mode; }
    /** The curve in use: the settings page reads it back instead of guessing. */
    int tonemapMode() const { return m_tonemapMode; }
    void setHdrWhiteNits(float nits) { if (nits > 0.0f) m_hdrWhiteNits = nits; }
    float hdrWhiteNits() const { return m_hdrWhiteNits; }

    // --- Rain on the windscreen (display pass, tonemap.frag binding 4) ---
    // A 4-float UBO the session fills once per frame: coverage 0..1, the
    // animation clock, the in-cockpit flag (the layer is screen-space: no
    // glass mesh is sampled, so it only paints where the driver's window is)
    // and the viewport aspect. No push room was left (TonemapPC ends at
    // exactly the 128-byte guarantee), so the effect rides its own descriptor.
    void setWindscreen(float coverage, bool inCockpit, float clockSeconds) {
        m_windscreenCoverage =
            coverage < 0.0f ? 0.0f : (coverage > 1.0f ? 1.0f : coverage);
        m_windscreenCockpit = inCockpit;
        m_windscreenClock = clockSeconds;
    }
    // Opt-in mesh whose screen-space AABB restricts the rain to the real glass
    // (windscreen stage 3). Empty keeps the stage-1 screen band.
    void setWindscreenGlassMesh(const std::string& name) { m_windscreenGlassMeshName = name; }
    bool ensureWindscreenMaskResources();

    // Colour grading of the display pass: the preset's colorGrading block, the
    // video config's saturation/contrast and a room's [COLOR] block ride these.
    // Saturation and contrast are applied to the display-referred image (after
    // the transfer function, mid-grey 0.5); the colour filter below is a light
    // gain applied before the tone curve. Defaults keep the neutral image.
    void setSaturation(float s) {
        m_saturation = s < 0.0f ? 0.0f : (s > 3.0f ? 3.0f : s);
    }
    float saturation() const { return m_saturation; }
    void setContrast(float c) {
        m_contrast = c < 0.01f ? 0.01f : (c > 3.0f ? 3.0f : c);
    }
    float contrast() const { return m_contrast; }
    /** The display pass multiplies the graded image with this filter: the
     *  [COLOR] block's BRIGHTNESS and its white-balance gain, solved on the
     *  CPU (simulator/ShowroomColor.h) so a grey stays grey. 1,1,1 = off. */
    void setColorFilter(const float rgb[3]) {
        for (int i = 0; i < 3; ++i)
            m_colorFilter[i] = rgb[i] < 0.0f ? 0.0f : (rgb[i] > 8.0f ? 8.0f : rgb[i]);
    }
    const float* colorFilter() const { return m_colorFilter; }
    /** [COLOR] HUE (degrees) and SEPIA (0..1): the room's own grading block.
     *  Both at 0 keeps the shader's two branches skipped, i.e. the image a
     *  room without a [COLOR] block draws. */
    void setGradeHue(float degrees) { m_gradeHue = degrees; }
    float gradeHue() const { return m_gradeHue; }
    void setGradeSepia(float amount) {
        m_gradeSepia = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
    }
    float gradeSepia() const { return m_gradeSepia; }
    // Fold-in weight of the half-res bloom in the display pass (the shader's
    // old hardcoded 0.3). Clamped so a bad caller cannot dim or blow up the
    // frame from the config file.
    void setBloomStrength(float b) {
        m_bloomStrength = b < 0.0f ? 0.0f : (b > 2.0f ? 2.0f : b);
    }
    float bloomStrength() const { return m_bloomStrength; }

    // ---- Depth of field of the display pass (the preset's depthOfField
    // block; photo mode's "camera"). Values map 1:1 onto the push-constant
    // block in tonemap.frag; doBarriers in endFrame() handle the depth image
    // layout. focal in metres; a negative focal makes the record-time code
    // derive it from the camera's actual vertical FOV (f = 0.5*sensor*proj(1,1)).
    void setDepthOfField(bool enabled, int quality, float fNumber, float sensorHeight,
                         float focalLength, float apertureParameter,
                         int frontLevels, int backLevels,
                         float meteringWidth, float meteringHeight) {
        m_dofEnabled = enabled;
        m_dofQuality = quality < 0 ? 0 : (quality > 5 ? 5 : quality);
        m_dofFNumber = fNumber > 0.0f ? fNumber : 2.8f;
        m_dofSensorH = sensorHeight > 0.0f ? sensorHeight : 0.025f;
        m_dofFocal = focalLength;
        m_dofApertureParam = apertureParameter > 0.0f ? apertureParameter : 1.0f;
        m_dofFrontLevels = frontLevels;
        m_dofBackLevels = backLevels;
        // meteringWidth/Height arrive in percent, as the (shared) autoExposure
        // metering values do; the shader wants the half-size in UV fractions.
        m_dofMeterHalfX = meteringWidth > 0.0f ? meteringWidth / 200.0f : 0.5f;
        m_dofMeterHalfY = meteringHeight > 0.0f ? meteringHeight / 200.0f : 0.5f;
    }
    bool depthOfFieldEnabled() const { return m_dofEnabled; }

    // HDR10 output. Requests a VK_COLOR_SPACE_HDR10_ST2084_EXT swapchain
    // (10-bit A2B10G10R10, PQ/Rec.2020) and makes the display pass encode
    // for it; falls back to the 8-bit sRGB swapchain (with an explicit log)
    // when the driver, the surface or the format is not there. Needs the
    // deferred path — the forward path writes the backbuffer directly and
    // has no display pass to run the encoding in — so setHdrOutput(true)
    // alone is not enough: setDeferred(true) has to be called too (done for
    // you by SimulatorApp when KS_HDR=1).
    void setHdrOutput(bool enabled) { m_hdrRequested = enabled; }
    bool hdrOutput() const { return m_hdrOutput; }
    bool hdrRequested() const { return m_hdrRequested; }

    // Anisotropic filtering from the config (video.anisotropicFiltering),
    // capped by the device limit, applied to every sampler built after this
    // call. 1 means isotropic.
    void setAnisotropicFiltering(int samples);
    int anisotropicFiltering() const { return m_anisotropicFiltering; }

    // Real frame lifecycle. beginFrame() acquires a swapchain image and
    // resets the per-frame draw list; drawMesh() *queues* a (mesh, model
    // matrix) pair rather than drawing immediately, because the shadow
    // cascades and the main color pass both need to render the exact same
    // set of instances — recording them once and replaying the list twice
    // (once per shadow cascade, once for the main pass) is what endFrame()
    // does. Submission/present happens at the end of endFrame().
    bool beginFrame();
    void drawMesh(const std::string& name, const mat4& modelMatrix);
    void endFrame();

    // One-shot readback of the backbuffer (forward path, or the deferred
    // path's display pass). Call requestScreenshot() before endFrame(); once
    // endFrame() returns, screenshotReady() is true and screenshotPixels()
    // holds tightly packed BGRA8 rows (VK_FORMAT_B8G8R8A8_UNORM swapchain
    // extent, origin top-left as stored). One frame per request; the wait
    // for the copy happens inside endFrame(), so this stalls — it is a
    // validation/screenshot hook, not a per-frame feature.
    // Used by tests/ksengine/test_renderer.cpp (roadmap 2.1) and by
    // SimulatorApp's KS_SCREENSHOT capture (roadmap 2.4/2.5).
    void requestScreenshot() { m_screenshotPending = true; m_screenshotReady = false; }
    bool screenshotReady() const { return m_screenshotReady; }
    const std::vector<unsigned char>& screenshotPixels() const { return m_screenshotPixels; }
    // Pixel size of the swapchain the readback above came from (width *
    // height * 4 == screenshotPixels().size()): turns those bytes into an
    // image without guessing the row pitch. Roadmap 2.4/2.5.
    const VkExtent2D& extent() const { return m_swapChainExtent; }
    /** Present mode is fixed when the swapchain is created: FIFO when on,
     *  IMMEDIATE when off and the surface offers it. Switching rebuilds the
     *  swapchain instead of pretending a flag changes anything. */
    void setVsync(bool enabled);
    bool vsync() const { return m_vsync; }

    /** Re-reads every .spv this renderer owns and rebuilds the pipelines that
     *  use them, in place: the forward pair now, the deferred block on the
     *  next frame (it is torn down here and rebuilt lazily). Shaders are
     *  compiled at every start - there is no on-disk pipeline cache - so the
     *  only thing a driver update can leave behind is a stale running session;
     *  this is the same code path the options page's reload row uses.
     *  Waits for the device to go idle; call it between frames. */
    bool reloadShaders();
    /** "device | vendor | device id | driver version | api version" of the
     *  physical device. Part of the stamp that decides whether the driver
     *  changed since the last run. */
    std::string driverStamp() const;

    // UI overlay GPU pass. SimulationLoop owns the pass (it builds the font
    // atlas + per-frame dynamic VB/IB); NativeRenderer keeps it alive and
    // pumps a frame through it while its render pass is open.
    void setUiGpuPass(std::shared_ptr<ui::UiGpuPass> pass) { m_uiPass = std::move(pass); }
    ui::UiGpuPass* uiGpuPass() { return m_uiPass.get(); }
    void drawUi(const ui::UiRenderer& ui);

    // GPU frame profile (roadmap E9 / gate 2.14). Timestamps are written in
    // endFrame() between the FrameBegin and FrameEnd markers and resolved two
    // frames later (the ring only reuses a pool once the single in-flight
    // fence has signalled); the resolved value is published to
    // PhysicsProfiler from endFrame() so the CPU report, the C API and the
    // CSV all describe the same frame. No-op — and frameTimeMs() stays 0 —
    // when the device or the queue reports no timestamp support.
    GpuProfiler& gpuProfiler() { return m_gpuProfiler; }
    double gpuFrameTimeMs() const { return m_gpuProfiler.frameTimeMs(); }
    double gpuAvgFrameTimeMs() const { return m_gpuProfiler.avgFrameTimeMs(); }
    bool gpuProfilerActive() const { return m_gpuProfiler.isEnabled(); }

    // --- Particles (roadmap ksengine-vs-cryengine P1) ---
    // Uploads this frame's billboard quads and nothing else: floatCount is a
    // count of *floats* in the flat record ParticleSystem::buildQuads packs
    // (12 floats per vertex, 6 vertices per particle). Call it before
    // endFrame(); an empty count skips the draw entirely, so a scene with no
    // particles costs one branch. One frame in flight (beginFrame() waits on
    // the only fence) means the host-visible buffer may be rewritten in
    // place every frame.
    void setParticleVertices(const float* data, size_t floatCount);
    size_t particleVertexCount() const { return m_particleVerts.size(); }
    // Pass-wide alpha multiplier for the sprite quad (params.x of the push
    // constants); 0 hides every particle without dropping the upload.
    void setParticleAlpha(float a) { m_particleAlpha = a; }

    // Viewport hint from SimulationLoop::setViewportSize(). The swapchain
    // itself is recreated by SimulatorApp's WM_SIZE handler.
    void resize(int width, int height) {
        if (width > 0) m_viewportW = width;
        if (height > 0) m_viewportH = height;
    }
    int width() const { return m_viewportW; }
    int height() const { return m_viewportH; }

    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkInstance instance() const { return m_instance; }
    VkDevice device() const { return m_device; }
    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    uint32_t graphicsQueueFamily() const { return m_graphicsQueueFamily; }
    VkCommandPool commandPool() const { return m_commandPool; }
    VkCommandBuffer currentCommandBuffer() const { return m_commandBuffer; }

    // Loads a baked DDS through the renderer-owned cache. The returned handles
    // remain valid until clearTextureCache(), shutdown(), or renderer
    // destruction; callers must not destroy them. This is the texture-loading
    // half of the material pipeline. Descriptor binding remains per-material.
    VkImageView textureView(const std::string& path, VkSampler& outSampler);
    // A material texture that never touches the disk: rgba is width*height*4.
    bool addTextureFromMemory(const std::string& key,
                              const std::vector<unsigned char>& rgba, int width, int height);
    // Post-processing preset grading strip (system/ppfilters/<id>.lut.png:
    // a 32^3 cube as a 1024x32 PNG). Applied by the display pass, which needs
    // the deferred path, exactly like setExposure().
    bool loadColorLut(const std::string& path, float strength = 1.0f);
    /** Uploads the strip recorded by loadColorLut(); called from the
     *  first recorded frame, once the texture runtime exists. */
    bool uploadPendingColorLut();
    void setColorLutStrength(float strength) { m_lutStrength = strength; }
    bool colorLutReady() const { return m_lutReady; }
    std::size_t cachedTextureCount() const noexcept;
    void clearTextureCache();

private:
    // Forward pipeline pieces, split out of loadPipelines() so reloadShaders()
    // can rebuild them without recreating the descriptors and IBL maps.
    bool loadForwardShaderModules();
    bool createForwardPipelineObjects();
    /** FNV digest of every .spv in m_shaderDir (name + size + mtime). */
    std::string shaderDigest() const;
    /** Logs and records a driver or shader-set change since the last run: the
     *  pipelines a previous session compiled were built by another driver. */
    void warnIfDriverOrShadersChanged();
    bool createCommandPoolAndBuffer();
    bool createSyncObjects();
    bool createDepthResources(uint32_t width, uint32_t height);
    void destroySwapChain();
    void uploadMesh(NativeMesh& mesh);

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    std::unique_ptr<TextureRuntime> m_textureRuntime;

    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    VkFormat m_swapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D m_swapChainExtent{};
    // A capture copies the presented image into a buffer, which the swapchain
    // image can only do when the surface offered TRANSFER_SRC.
    bool m_swapChainTransferSrc = false;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE; // kept so setVsync can rebuild
    bool m_vsync = true;
    std::vector<VkImage> m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;
    std::vector<VkFramebuffer> m_swapChainFramebuffers;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;

    VkSemaphore m_imageAvailable = VK_NULL_HANDLE;
    // One render-finished semaphore per swapchain image: a queued present on
    // image i may still be waiting on it when the next frame re-signals for
    // image i (VUID-vkQueueSubmit-pSignalSemaphores-00067). Keyed on the
    // acquired index, image i's previous present has completed by the time
    // the swapchain hands image i back to us. Filled in createSwapChain().
    std::vector<VkSemaphore> m_renderFinished;
    VkFence m_inFlightFence = VK_NULL_HANDLE;
    uint32_t m_currentImageIndex = 0;

    VkShaderModule m_vertModule = VK_NULL_HANDLE;
    VkShaderModule m_fragModule = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    // Same shaders, alpha blend on and depth write off (see-through pass).
    VkPipeline m_transparentPipeline = VK_NULL_HANDLE;

    // Descriptor set 0: FrameData UBO (sun + cascade matrices) + shadow
    // cascade array sampler — matches native_forward.frag exactly. Written
    // once per frame in endFrame() before the main draw list, since the
    // cascade matrices are only known after CascadedShadowMap::update() runs.
    VkDescriptorSetLayout m_frameSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_frameSet = VK_NULL_HANDLE;
    VkBuffer m_frameUBO = VK_NULL_HANDLE;
    VkDeviceMemory m_frameUBOMemory = VK_NULL_HANDLE;
    void* m_frameUBOMapped = nullptr;

    // Descriptor set 1 (Roadmap 2.3): per-mesh albedo + normal samplers and
    // the std140 MaterialUBO (roughness, metalness, normalScale) — plus the
    // two PBR map samplers (roughness, metalness) the rendering brief P1
    // added at bindings 3/4. Bound inside recordDrawList() for every mesh of
    // both the forward and the GBuffer pass — they share m_pipelineLayout,
    // which therefore carries both set layouts (the forward shader only
    // declares bindings 0..2, a layout superset is legal). One pool serves
    // every mesh set plus the default set bound when a mesh has none
    // (created without FREE bit would prevent per-mesh frees on track
    // switch, hence the flag).
    VkDescriptorSetLayout m_materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_materialPool = VK_NULL_HANDLE;
    VkDescriptorSet m_defaultMaterialSet = VK_NULL_HANDLE;
    bool createMaterialDescriptorResources();
    void destroyMaterialDescriptorResources();
    bool createMaterialDescriptor(NativeMesh& mesh);
    void destroyMaterialDescriptor(NativeMesh& mesh);
    // (Re)writes a set's four texture bindings from the given paths,
    // resolving them through TextureRuntime (white fallbacks when a path is
    // empty or missing — the missing-map case must degrade to the authored
    // scalar, i.e. multiply by 1.0). The MaterialUBO binding is written by
    // createMaterialDescriptor(), not here.
    bool writeMaterialDescriptorSet(VkDescriptorSet set,
                                    const std::string& albedoPath,
                                    const std::string& normalPath,
                                    const std::string& roughnessPath,
                                    const std::string& metalnessPath,
                                    const std::string& detailPath,
                                    const std::string& emissivePath = std::string());
    // After clearTextureCache() drops images that live descriptor sets still
    // reference: re-resolve every set (lazy DDS reload on next use).
    void rewriteMaterialDescriptors();

    // 1x1 white shadow-cascade-array stand-in, bound when no CascadedShadowMap
    // is attached, so the descriptor set is always valid to bind (the shader
    // always declares the binding; this just makes every fragment "lit").
    VkImage m_dummyShadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_dummyShadowMemory = VK_NULL_HANDLE;
    VkImageView m_dummyShadowView = VK_NULL_HANDLE;
    VkSampler m_dummyShadowSampler = VK_NULL_HANDLE;

    bool createFrameDescriptorResources();
    bool createDummyShadowTexture();
    void writeFrameDescriptorSet(VkImageView shadowView, VkSampler shadowSampler);

    // --- Mirrors (stage 1): target + overlay, all lazily built ---
    bool ensureMirrorResources();
    void destroyMirrorResources();
    void destroyMirrorOverlay();
    // Draws the rear-view tile inside the open colour pass (forward main or
    // deferred display). Pipeline is built per pass (formats differ).
    bool recordMirrorOverlay(VkRenderPass pass);
    // video.mirrorResolution straightened out: landscape enforced, clamped,
    // defaults filled in — the single source of truth for the tile size, so
    // the frustum tested at queue time matches the target that gets built.
    void resolvedMirrorSize(uint32_t& width, uint32_t& height) const;
    // The tile's camera + frustum, built in beginFrame() next to the main
    // one so drawMesh() can test both eyes; recordMirrorScene() renders
    // from exactly these, never a recomputation.
    mat4 m_mirrorViewFrame;
    mat4 m_mirrorProjFrame;
    ks::math::Frustum m_mirrorFrustum;
    bool m_mirrorFrameValid = false;
    VkImage m_mirrorImage = VK_NULL_HANDLE;
    VkDeviceMemory m_mirrorMemory = VK_NULL_HANDLE;
    VkImageView m_mirrorView = VK_NULL_HANDLE;
    VkImage m_mirrorDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_mirrorDepthMemory = VK_NULL_HANDLE;
    VkImageView m_mirrorDepthView = VK_NULL_HANDLE;
    VkRenderPass m_mirrorRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_mirrorFramebuffer = VK_NULL_HANDLE;
    VkSampler m_mirrorSampler = VK_NULL_HANDLE;
    // Side tiles (left/right mirrors): own targets, cameras, frustums and
    // draw lists. Render pass is shared (same format/extent as the rear tile).
    mat4 m_mirrorLeftViewFrame, m_mirrorLeftProjFrame;
    ks::math::Frustum m_mirrorLeftFrustum;
    bool m_mirrorLeftFrameValid = false;
    uint32_t m_mirrorLeftW = 0, m_mirrorLeftH = 0;
    VkImage m_mirrorLeftImage = VK_NULL_HANDLE;
    VkDeviceMemory m_mirrorLeftMemory = VK_NULL_HANDLE;
    VkImageView m_mirrorLeftView = VK_NULL_HANDLE;
    VkImage m_mirrorLeftDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_mirrorLeftDepthMemory = VK_NULL_HANDLE;
    VkImageView m_mirrorLeftDepthView = VK_NULL_HANDLE;
    VkFramebuffer m_mirrorLeftFramebuffer = VK_NULL_HANDLE;
    mat4 m_mirrorRightViewFrame, m_mirrorRightProjFrame;
    ks::math::Frustum m_mirrorRightFrustum;
    bool m_mirrorRightFrameValid = false;
    uint32_t m_mirrorRightW = 0, m_mirrorRightH = 0;
    VkImage m_mirrorRightImage = VK_NULL_HANDLE;
    VkDeviceMemory m_mirrorRightMemory = VK_NULL_HANDLE;
    VkImageView m_mirrorRightView = VK_NULL_HANDLE;
    VkImage m_mirrorRightDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_mirrorRightDepthMemory = VK_NULL_HANDLE;
    VkImageView m_mirrorRightDepthView = VK_NULL_HANDLE;
    VkFramebuffer m_mirrorRightFramebuffer = VK_NULL_HANDLE;
    bool m_mirrorSideEnabled = true;
    VkBuffer m_mirrorUBO = VK_NULL_HANDLE;
    VkDeviceMemory m_mirrorUBOMemory = VK_NULL_HANDLE;
    void* m_mirrorUBOMapped = nullptr;
    VkDescriptorSet m_mirrorFrameSet = VK_NULL_HANDLE;
    bool m_mirrorEnabled = false;
    uint32_t m_mirrorWantedW = 0, m_mirrorWantedH = 0;
    std::string m_mirrorGlassMeshName;
    mat4 m_mirrorGlassModel{};
    bool m_mirrorGlassValid = false;
    uint32_t m_mirrorW = 0, m_mirrorH = 0;   // live target extent
    bool m_mirrorFailed = false;
    // Overlay half: layout/set shared by both pass pipelines.
    VkPipelineLayout m_mirrorOverlayLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mirrorOverlaySetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_mirrorOverlayPool = VK_NULL_HANDLE;
    VkDescriptorSet m_mirrorOverlaySet = VK_NULL_HANDLE;
    VkShaderModule m_mirrorOverlayVert = VK_NULL_HANDLE;
    VkShaderModule m_mirrorOverlayFrag = VK_NULL_HANDLE;
    VkPipeline m_mirrorOverlayForward = VK_NULL_HANDLE;    // built vs m_renderPass
    VkPipeline m_mirrorOverlayDisplay = VK_NULL_HANDLE;    // built vs display pass
    VkFormat m_mirrorOverlayDisplayFormat = VK_FORMAT_UNDEFINED;
    bool m_mirrorOverlayFailed = false;

    // Mirror stage 3 — optional glass mesh that samples the rear-view tile.
    VkShaderModule m_mirrorGlassVert = VK_NULL_HANDLE;
    VkShaderModule m_mirrorGlassFrag = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mirrorGlassSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_mirrorGlassPool = VK_NULL_HANDLE;
    VkDescriptorSet m_mirrorGlassSet = VK_NULL_HANDLE;
    VkPipelineLayout m_mirrorGlassLayout = VK_NULL_HANDLE;
    VkPipeline m_mirrorGlassPipeline = VK_NULL_HANDLE;
    bool m_mirrorGlassFailed = false;

    // --- Deferred path (opt-in, setDeferred / setTaa) ---
    bool ensureDeferredResources();
    void destroyDeferredResources();
    void writeLightingDescriptorSet(VkImageView shadowView, VkSampler shadowSampler);
    void writeResolveDescriptorSet();
    // Per-frame half of the display/bloom descriptors: both the bright-pass
    // extract and the tonemap pass read the history slot *this* frame's
    // resolve writes, so they are re-pointed every frame.
    void writeDisplayDescriptorSets(VkImageView resolvedView, VkImageView depthView);

    // --- Occlusion culling: previous-frame max-depth grid ---
    // ensureOcclusionResources() builds the downsample pass + host readback
    // lazily on the first frame that needs them; recordOcclusionPass() runs
    // the downsample + copy after whichever geometry pass ran this frame;
    // destroyOcclusionResources() tears it all down with the swapchain.
    bool ensureOcclusionResources();
    void destroyOcclusionResources();
    // What the grid pass needs to know about the eye it is pooling for: the
    // target, the descriptor set naming that eye's depth image, and the
    // readback the copy lands in. Pass, pipeline, layout, sampler and pool are
    // shared by both eyes.
    struct OcclTarget {
        VkImage image = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkBuffer readback = VK_NULL_HANDLE;
        int gridW = 0;
        int gridH = 0;
        bool* pending = nullptr;   // set once the copy is recorded
    };
    void recordOcclusionPass(VkImage depthImage, VkImageView depthView,
                             const OcclTarget& target);
    // The rear-view tile is a separate eye: the main grid's tiles describe a
    // screen mapping it cannot use, so it gets its own grid, target and
    // readback. Independent of the main one — a failure here only costs the
    // tile its occlusion test.
    bool ensureMirrorOcclusionResources();
    void destroyMirrorOcclusionResources();

    static constexpr int kGBufferCount = 3;
    static constexpr int kHistoryCount = 2;
    static constexpr int kJitterSamples = 8;
    static constexpr float kTaaFeedback = 0.9f;

    bool m_deferred = false;
    GBufferLayoutManager m_gbufferLayouts;
    bool m_taa = false;
    bool m_deferredReady = false;
    bool m_deferredFailed = false;
    bool m_historyValid = false;
    bool m_prevViewProjValid = false;
    int m_jitterIndex = 0;
    int m_historyIndex = 0;
    mat4 m_prevViewProj;
    // The previous frame's view matrix: only the cut test needs the pose itself
    // (see framegen::poseJump), the passes use m_prevViewProj.
    mat4 m_prevView;
    // The previous frame's projection (unjittered): the showroom and the garage
    // do not share a lens, and leaving the menu rescales the image with the pose
    // hardly moving - a cut the view matrices alone cannot see.
    mat4 m_prevProj;
    // Render size of the frame that produced m_prevViewProj: a resize makes the
    // pair incomparable even when the pose did not move.
    int m_prevFrameGenWidth = 0;
    int m_prevFrameGenHeight = 0;
    // The seam's published state, refreshed every endFrame().
    ks::sim::framegen::Inputs m_frameGenInputs;
    std::string m_shaderDir;

    // Display transform / HDR output (see setExposure/setHdrOutput).
    bool m_hdrRequested = false;        // caller asked for HDR10 ...
    bool m_hdrOutput = false;           // ... and the swapchain really is one
    int m_anisotropicFiltering = 1;     // samples the material samplers ask for
    bool m_swapchainColorSpaceExt = false; // VK_EXT_swapchain_colorspace (instance ext) present
    VkColorSpaceKHR m_swapChainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    float m_exposure = 1.0f;
    float m_brakeGlow = 0.0f;              // brief P6: hottest-disc heat [0..1]
    float m_wetness = 0.0f;                // videoSources.weatherShaders: track wetness [0..1]
    int m_tonemapMode = 2;              // 0 none, 1 Reinhard, 2 ACES, 3 Uncharted2, 4 Filmic
    static constexpr float kHdrPeakNits = 1000.0f; // highlight ceiling for the PQ shoulder
    // Nits given to the graded SDR-range signal (1.0) in the PQ encode. This
    // has to match the OS's SDR-white for HDR and SDR to look the same: Windows
    // composites SDR windows against its own SDR-white, measured at 80 nits
    // here (capture calibration against the SDR path), while BT.2408's 203
    // made the same frame ~2.5x brighter than SDR. Override per machine with
    // KS_HDR_WHITE_NITS.
    float m_hdrWhiteNits = 80.0f;
    // Colour grading of the display pass (see setSaturation/setContrast).
    float m_saturation = 1.0f;
    float m_contrast = 1.0f;
    float m_colorFilter[3] = {1.0f, 1.0f, 1.0f}; // [COLOR] brightness x white balance
    float m_gradeHue = 0.0f;                     // [COLOR] HUE, degrees
    float m_gradeSepia = 0.0f;                   // [COLOR] SEPIA, 0..1
    // Bloom fold-in weight of the display pass (was the shader's hardcoded
    // 0.3 — see setBloomStrength).
    float m_bloomStrength = 0.3f;
    // Depth of field of the display pass (see setDepthOfField).
    bool m_dofEnabled = false;
    int m_dofQuality = 2;
    float m_dofFNumber = 2.8f;
    float m_dofSensorH = 0.025f;   // metres
    float m_dofFocal = -1.0f;      // <= 0: derived from the camera FOV at record
    float m_dofApertureParam = 1.0f;
    int m_dofFrontLevels = -1;
    int m_dofBackLevels = -1;
    float m_dofMeterHalfX = 0.5f;  // metering half-size, UV fractions
    float m_dofMeterHalfY = 0.5f;

    VkBuffer m_windscreenUBO = VK_NULL_HANDLE;
    VkDeviceMemory m_windscreenMemory = VK_NULL_HANDLE;
    void* m_windscreenMapped = nullptr;
    VkBuffer m_windscreenRectUBO = VK_NULL_HANDLE;
    VkDeviceMemory m_windscreenRectMemory = VK_NULL_HANDLE;
    void* m_windscreenRectMapped = nullptr;

    // Windscreen stage 4: per-pixel glass mask (R8), rendered before the
    // display pass from the glass mesh so the rain only lands on real glass.
    VkImage m_windscreenMaskImage = VK_NULL_HANDLE;
    VkDeviceMemory m_windscreenMaskMemory = VK_NULL_HANDLE;
    VkImageView m_windscreenMaskView = VK_NULL_HANDLE;
    VkSampler m_windscreenMaskSampler = VK_NULL_HANDLE;
    VkRenderPass m_windscreenMaskRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_windscreenMaskFramebuffer = VK_NULL_HANDLE;
    VkPipeline m_windscreenMaskPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_windscreenMaskLayout = VK_NULL_HANDLE;
    VkShaderModule m_windscreenMaskVert = VK_NULL_HANDLE;
    VkShaderModule m_windscreenMaskFrag = VK_NULL_HANDLE;
    bool m_windscreenMaskFailed = false;
    float m_windscreenCoverage = 0.0f;
    bool m_windscreenCockpit = false;
    float m_windscreenClock = 0.0f;
    std::string m_windscreenGlassMeshName;
    mat4 m_windscreenGlassModel{};
    bool m_windscreenGlassValid = false;
    float m_windscreenGlassRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    bool m_windscreenGlassHasRect = false;

    VkFormat m_gbufferFormats[kGBufferCount] = {
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
    };
    VkImage m_gbufferImages[kGBufferCount]{};
    VkDeviceMemory m_gbufferMemory[kGBufferCount]{};
    VkImageView m_gbufferViews[kGBufferCount]{};
    VkImage m_gbufferDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_gbufferDepthMemory = VK_NULL_HANDLE;
    VkImageView m_gbufferDepthView = VK_NULL_HANDLE;

    // --- Motion vectors (FrameGenInputs.h, videoSources.frameGeneration) ---
    // The GBuffer's fourth colour target: RG16F, current pixel minus previous
    // pixel in render pixels, y down. Written by the main deferred eye only,
    // cleared to zero elsewhere; handed to a provider through frameGenInputs()
    // and velocityView().
    VkImage m_velocityImage = VK_NULL_HANDLE;
    VkDeviceMemory m_velocityImageMemory = VK_NULL_HANDLE;
    VkImageView m_velocityView = VK_NULL_HANDLE;
    // Set 2: one 256-byte slot per draw holding this frame's unjittered
    // local->clip, last frame's, and the render size. A dynamic offset, not the
    // per-mesh material block, because two instances of one mesh (the AI cars
    // share a body) need different matrices in the same frame.
    static constexpr int kVelocitySlots = 8192;
    static constexpr uint32_t kVelocitySlotBytes = 256;
    VkDescriptorSetLayout m_velocitySetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_velocityPool = VK_NULL_HANDLE;
    VkDescriptorSet m_velocitySet = VK_NULL_HANDLE;
    VkBuffer m_velocityUbo = VK_NULL_HANDLE;
    VkDeviceMemory m_velocityUboMemory = VK_NULL_HANDLE;
    void* m_velocityMapped = nullptr;
    int m_velocitySlot = 0;
    // Diagnostic (KS_FG_MOTION_DUMP): the velocity target copied back once and
    // summarised on stdout, so the vectors can be checked on a real GPU.
    bool m_velocityDumpRequested = false;
    bool m_velocityDumpCopied = false;
    VkBuffer m_velocityReadback = VK_NULL_HANDLE;
    VkDeviceMemory m_velocityReadbackMemory = VK_NULL_HANDLE;
    VkDeviceSize m_velocityReadbackBytes = 0;
    // Three dumps, sixty frames apart: the first frame with history is usually
    // a camera cut (menu -> garage), which says nothing about the steady state.
    int m_velocityDumpFrame = 0;
    // The pose jump of the last frame, kept for the dump line.
    float m_lastPoseJumpMetres = 0.0f;
    float m_lastPoseJumpDegrees = 0.0f;
    float m_lastLensJumpDegrees = 0.0f;

    // Lighting output, sampled by the resolve pass, plus two ping-ponged
    // temporal history targets (RGBA16F so accumulated light survives).
    VkImage m_hdrImage = VK_NULL_HANDLE;
    VkDeviceMemory m_hdrMemory = VK_NULL_HANDLE;
    VkImageView m_hdrView = VK_NULL_HANDLE;
    VkImage m_historyImages[kHistoryCount]{};
    VkDeviceMemory m_historyMemory[kHistoryCount]{};
    VkImageView m_historyViews[kHistoryCount]{};

    VkRenderPass m_gbufferRenderPass = VK_NULL_HANDLE;
    VkRenderPass m_lightingRenderPass = VK_NULL_HANDLE;
    VkRenderPass m_resolveRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_gbufferFramebuffers;
    VkFramebuffer m_lightingFramebuffer = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_resolveFramebuffers;

    VkDescriptorSetLayout m_lightingSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_lightingPool = VK_NULL_HANDLE;
    VkDescriptorSet m_lightingSet = VK_NULL_HANDLE;
    VkSampler m_gbufferSampler = VK_NULL_HANDLE;
    VkPipelineLayout m_lightingLayout = VK_NULL_HANDLE;

    // --- Native local lights ---------------------------------------------
    // The list as the running session set it, and the table this frame's
    // camera binned it into. The buffer is host-visible and coherent: the table
    // depends on the camera, so it is rebuilt and rewritten every frame rather
    // than staged.
    void uploadLocalLights(const float* viewProj, int pixelWidth, int pixelHeight,
                           float lightParams[4]);
    bool ensureLocalLightBuffer(VkDeviceSize bytes);
    std::vector<ks::sim::LocalLight> m_localLights;
    std::size_t m_localLightLogged = 0;
    int m_lightTilesX = 0;
    int m_lightTilesY = 0;
    int m_lightBase = 0;
    VkBuffer m_lightBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_lightMemory = VK_NULL_HANDLE;
    VkDeviceSize m_lightCapacity = 0;
    void* m_lightMapped = nullptr;

    /** The mirror tiles shade the same lamps but bin them against their own
     *  camera and their much smaller extent, so each mirror view needs a table
     *  of its own: a tile index covers a different piece of the world for every
     *  camera. Same record layout and the same doubling rule as the main table.
     *  Index 0 = rear view, 1 = left, 2 = right. */
    struct MirrorLightTable {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize capacity = 0;
        void* mapped = nullptr;
    };
    static constexpr int kMirrorLightViews = 3;
    MirrorLightTable m_mirrorLightTables[kMirrorLightViews];
    // Last tile grid reported per view, so the log speaks once per change.
    int m_mirrorLightSignature[kMirrorLightViews] = {-1, -1, -1};

    // Builds one mirror view's light table and copies it into that view's
    // buffer; fills lightParams[4] for the tile's own frame UBO.
    void uploadMirrorLocalLights(int mirrorSlot, const float* viewProj, int pixelWidth,
                                 int pixelHeight, float lightParams[4]);
    bool ensureMirrorLightBuffer(int mirrorSlot, VkDeviceSize bytes);
    // Writes the atlas layer indices, near/far and matrices into a freshly built
    // table's records: the main table and the mirror tables all need this.
    void applyShadowRecords(ks::sim::LightClusterTable& table) const;
    /** The buffer the lighting set's binding 9 currently points at. Written
     *  only when the allocation changes, not every frame: the table's size
     *  follows the camera, and rewriting a descriptor a submitted frame may
     *  still be reading is churn with no upside. */
    VkBuffer m_lightBoundBuffer = VK_NULL_HANDLE;
    VkPipeline m_gbufferPipeline = VK_NULL_HANDLE;
    VkPipeline m_lightingPipeline = VK_NULL_HANDLE;
    // Deferred transparent pass: blended surfaces (glass, decals) drawn with
    // the forward shader over the lit HDR image, depth-tested against the
    // GBuffer depth and never writing it.
    VkRenderPass m_transparentSceneRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_transparentSceneFramebuffer = VK_NULL_HANDLE;
    VkPipeline m_transparentScenePipeline = VK_NULL_HANDLE;
    // Grass blades: same render pass and vertex layout as the scene, its own
    // layout (mvp + sun + clock in the push constants), no descriptors.
    VkPipeline m_grassPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_grassLayout = VK_NULL_HANDLE;
    VkShaderModule m_grassVertModule = VK_NULL_HANDLE;
    VkShaderModule m_grassFragModule = VK_NULL_HANDLE;
    VkShaderModule m_gbufferVertModule = VK_NULL_HANDLE;
    VkShaderModule m_gbufferFragModule = VK_NULL_HANDLE;
    VkShaderModule m_lightingVertModule = VK_NULL_HANDLE;
    VkShaderModule m_lightingFragModule = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_resolveSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_resolvePool = VK_NULL_HANDLE;
    VkDescriptorSet m_resolveSet = VK_NULL_HANDLE;
    VkSampler m_hdrSampler = VK_NULL_HANDLE;
    VkImageView m_lutView = VK_NULL_HANDLE;  // grading strip, owned by the texture cache
    VkSampler m_lutSampler = VK_NULL_HANDLE;
    bool m_lutReady = false;
    std::string m_lutPendingPath;  // requested before the texture runtime existed
    float m_lutStrength = 0.0f;
    VkPipelineLayout m_resolveLayout = VK_NULL_HANDLE;
    VkPipeline m_resolvePipeline = VK_NULL_HANDLE;
    VkShaderModule m_resolveFragModule = VK_NULL_HANDLE;

    // --- Bloom (glareExtract.frag bright pass + bloomBlur.frag separable
    // blur, half resolution): input/blur targets ping-pong so the blur can
    // read A and write B, then read B and write A back for the display pass.
    VkImage m_bloomImages[2]{};
    VkDeviceMemory m_bloomMemory[2]{};
    VkImageView m_bloomViews[2]{};
    VkExtent2D m_bloomExtent{};
    VkRenderPass m_bloomRenderPass = VK_NULL_HANDLE;  // shared by extract + both blurs
    VkFramebuffer m_bloomFramebuffers[2]{};
    VkDescriptorSetLayout m_bloomInputSetLayout = VK_NULL_HANDLE; // 1 sampler, shared
    VkDescriptorPool m_bloomPool = VK_NULL_HANDLE;
    VkDescriptorSet m_bloomExtractSet = VK_NULL_HANDLE; // per-frame: resolved frame
    VkDescriptorSet m_bloomBlurSets[2]{};               // static: A, then B
    VkPipelineLayout m_bloomLayout = VK_NULL_HANDLE;    // shared: set0 + 16B push
    VkPipeline m_bloomExtractPipeline = VK_NULL_HANDLE;
    VkPipeline m_bloomBlurPipeline = VK_NULL_HANDLE;
    VkShaderModule m_glareFragModule = VK_NULL_HANDLE;
    VkShaderModule m_blurFragModule = VK_NULL_HANDLE;

    // SSGI (video.ssgi): a half-res gather plus one scratch target, filtered by
    // two gaussian passes and sampled by the lighting pass as binding 8. The
    // gather pipeline is its own; the blur reuses bloomBlur.frag.
    VkImage m_ssgiImages[2]{};
    VkDeviceMemory m_ssgiMemory[2]{};
    VkImageView m_ssgiViews[2]{};
    VkFramebuffer m_ssgiFramebuffers[2]{};
    VkSampler m_ssgiSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_ssgiGatherSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_ssgiBlurSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_ssgiPool = VK_NULL_HANDLE;
    VkDescriptorSet m_ssgiGatherSet = VK_NULL_HANDLE;
    VkDescriptorSet m_ssgiBlurSets[2]{};
    VkPipelineLayout m_ssgiGatherLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_ssgiBlurLayout = VK_NULL_HANDLE;
    VkPipeline m_ssgiGatherPipeline = VK_NULL_HANDLE;
    VkPipeline m_ssgiBlurPipeline = VK_NULL_HANDLE;
    VkShaderModule m_ssgiGatherFragModule = VK_NULL_HANDLE;

    // --- Display pass: the only writer of the backbuffer on the deferred
    // path — tonemap.frag turns the resolved linear-HDR frame into sRGB
    // (SDR) or PQ/Rec.2020 (HDR10) pixels. ---
    VkRenderPass m_displayRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_displayFramebuffers;
    VkDescriptorSetLayout m_displaySetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_displayPool = VK_NULL_HANDLE;
    VkDescriptorSet m_displaySet = VK_NULL_HANDLE;
    VkPipelineLayout m_displayLayout = VK_NULL_HANDLE;
    VkPipeline m_displayPipeline = VK_NULL_HANDLE;
    VkShaderModule m_tonemapFragModule = VK_NULL_HANDLE;

    std::unordered_map<std::string, NativeMesh> m_meshes;
    std::vector<std::string> m_staticSceneMeshNames;

    struct QueuedDraw { std::string meshName; mat4 model; };
    // drawList replays one of the queued lists from the camera it is given —
    // endFrame() passes its local recordDrawList lambda down here and the
    // mirror scene pass is its only other caller. Declared next to QueuedDraw
    // because the signature needs it complete.
    void recordMirrorScene(const mat4& view, const mat4& proj,
                          VkImage colorImage, VkImageView colorView,
                          VkFramebuffer framebuffer, uint32_t width, uint32_t height,
                          const std::vector<QueuedDraw>& drawListVec,
                          const std::function<void(const std::vector<QueuedDraw>&, const mat4&, const mat4&)>& drawFunc,
                          bool withOcclusion);
    // Everything that survived the frustum test: the shadow cascades render
    // this list (an object hidden from the camera can still cast a visible
    // shadow) ...
    std::vector<QueuedDraw> m_drawList;
    // ... while the camera passes render this one, which additionally
    // excludes what the previous-frame depth grid says is occluded.
    std::vector<QueuedDraw> m_mainDrawList;
    // Last frame's main list, index-aligned and copied in endFrame(): the
    // velocity of each draw is measured against the model at the same index, so
    // a moving car or wheel needs no instance id and a shared mesh still gets
    // one matrix per draw.
    std::vector<QueuedDraw> m_prevMainDrawList;
    // Last frame's draws grouped by mesh name, built in beginFrame(): a draw
    // continues the nearest unused instance of its own name (see
    // framegen::isSameInstance), so two AI cars sharing a body still get one
    // matrix each and a scene swap matches nobody instead of matching walls.
    struct PrevInstance {
        mat4 model;
        bool used = false;
    };
    std::unordered_map<std::string, std::vector<PrevInstance>> m_prevInstances;
    int m_velocityDraws = 0;
    int m_velocityMatched = 0;
    // The diagnostic's readback is read one frame after the pass that wrote it,
    // so the printout has to describe *that* frame: its classification is kept
    // here as the frame ends, otherwise a 95 px vector would be labelled with
    // the jump of the calm frame after it.
    struct FrameMotionClass {
        bool reset = false;
        float metres = 0.0f;
        float degrees = 0.0f;
        float lensDegrees = 0.0f;
        int matched = 0;
        int draws = 0;
    };
    FrameMotionClass m_dumpedMotionClass;
    // and the rear-view tile renders this one: its own frustum picks what
    // its eye can see, including meshes the main camera turned its back on
    // (both lists can hold the same instance, a wall to the side says).
    std::vector<QueuedDraw> m_mirrorList;
    std::vector<QueuedDraw> m_mirrorLeftList;
    std::vector<QueuedDraw> m_mirrorRightList;

    mat4 m_view;
    mat4 m_proj;
    vec3 m_camPosWS;
    float m_camNear = 0.5f;
    float m_camFar = 500.0f;
    DirectionalLight m_sun;
    CascadedShadowMap* m_shadowMap = nullptr;
    LocalShadowAtlas* m_localShadowAtlas = nullptr;

    // Culling state. The frustum is rebuilt once per frame in beginFrame()
    // from whatever setCamera() last supplied.
    bool m_cameraValid = false;
    bool m_frustumCulling = true;
    ks::math::Frustum m_frustum;
    FrameStats m_stats;

    // Previous-frame occlusion grid (see engine/Math/OcclusionTest.h).
    // Pipeline: endFrame() max-pools the depth buffer and copies it into
    // m_occlReadback; the *next* beginFrame() linearises that copy into
    // m_occlGrid; drawMesh() tests against m_occlGrid while projecting with
    // m_prevViewProj + (m_occlR22, m_occlR23) — all three describe the same
    // frame, so the grid and the projection can never drift apart.
    bool m_occlusionCulling = true;
    bool m_occlusionFailed = false;   // sticky: build failed, stop trying
    bool m_occlusionReady = false;
    bool m_occlPending = false;       // readback copy recorded, not consumed
    bool m_occlGridValid = false;     // m_occlGrid usable for tests
    int m_occlGridW = 0;
    int m_occlGridH = 0;
    int m_occlFullW = 0;              // depth image extent the grid maps to
    int m_occlFullH = 0;
    float m_occlR22 = 0.0f;           // projection row 2 of the grid's frame
    float m_occlR23 = 0.0f;
    std::vector<float> m_occlGrid;    // linear distance (m) per tile

    VkImage m_hizImage = VK_NULL_HANDLE;
    VkDeviceMemory m_hizMemory = VK_NULL_HANDLE;
    VkImageView m_hizView = VK_NULL_HANDLE;
    VkRenderPass m_hizRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_hizFramebuffer = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_hizSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_hizPool = VK_NULL_HANDLE;
    VkDescriptorSet m_hizSet = VK_NULL_HANDLE;
    VkSampler m_hizSampler = VK_NULL_HANDLE;
    VkPipelineLayout m_hizLayout = VK_NULL_HANDLE;
    VkPipeline m_hizPipeline = VK_NULL_HANDLE;
    VkShaderModule m_hizVertModule = VK_NULL_HANDLE;
    VkShaderModule m_hizFragModule = VK_NULL_HANDLE;
    VkBuffer m_occlReadback = VK_NULL_HANDLE;
    VkDeviceMemory m_occlReadbackMemory = VK_NULL_HANDLE;
    void* m_occlReadbackMapped = nullptr;

    // Mirror-eye grid: same pass and pipeline as the main one, its own target,
    // descriptor set and readback (a 512x128 tile needs 4 KB). Consumed the
    // same way — copied at the end of a frame, linearised in beginFrame(),
    // tested with the mirror eye's previous view-projection.
    bool m_mirrorOcclReady = false;
    bool m_mirrorOcclFailed = false;      // sticky: build failed, tile stops testing
    bool m_mirrorOcclPending = false;
    bool m_mirrorOcclGridValid = false;
    int m_mirrorOcclGridW = 0;
    int m_mirrorOcclGridH = 0;
    std::vector<float> m_mirrorOcclGrid;
    mat4 m_prevMirrorViewProj;
    bool m_prevMirrorViewProjValid = false;
    VkImage m_mirrorOcclImage = VK_NULL_HANDLE;
    VkDeviceMemory m_mirrorOcclMemory = VK_NULL_HANDLE;
    VkImageView m_mirrorOcclView = VK_NULL_HANDLE;
    VkFramebuffer m_mirrorOcclFramebuffer = VK_NULL_HANDLE;
    VkDescriptorSet m_mirrorOcclSet = VK_NULL_HANDLE;
    VkBuffer m_mirrorOcclReadback = VK_NULL_HANDLE;
    VkDeviceMemory m_mirrorOcclReadbackMemory = VK_NULL_HANDLE;
    void* m_mirrorOcclReadbackMapped = nullptr;

    // --- Particles (roadmap P1): sprite pipelines + a host-visible quad
    // buffer rewritten every frame. Built lazily on the first frame that
    // actually has particles to draw, so a scene without them never creates
    // a pipeline, a buffer or a shader module. The forward pipeline is
    // created against m_renderPass and the deferred one against
    // m_gbufferRenderPass, so they are torn down in two different places
    // (shutdown / destroyDeferredResources respectively).
    bool ensureParticlePipelines(bool deferred);
    bool ensureParticleBuffer(VkDeviceSize bytes);
    void recordParticles(const mat4& viewProjRender, bool deferred);
    void destroyParticlePipelines();
    void destroyParticleGbufPipeline();
    void destroyParticleBuffer();

    std::vector<float> m_particleVerts;   // this frame's quads, CPU side
    float m_particleAlpha = 1.0f;
    VkBuffer m_particleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_particleMemory = VK_NULL_HANDLE;
    void* m_particleMapped = nullptr;     // persistent map (one frame in flight)
    VkDeviceSize m_particleCapacity = 0;
    VkPipelineLayout m_particleLayout = VK_NULL_HANDLE;
    VkPipeline m_particleFwdPipeline = VK_NULL_HANDLE;
    VkPipeline m_particleGbufPipeline = VK_NULL_HANDLE;
    VkShaderModule m_particleVertModule = VK_NULL_HANDLE;
    VkShaderModule m_particleFragModule = VK_NULL_HANDLE;
    VkShaderModule m_particleGbufFragModule = VK_NULL_HANDLE;
    bool m_particleShadersFailed = false; // missing .spv: stop trying, once
    bool m_particleGbufFailed = false;    // ...and same, GBuffer variant only

    // --- Screenshot readback (requestScreenshot() / endFrame()) ---    // Host-visible copy target for the swapchain image, built lazily on the
    // first request and destroyed with the swapchain (extent-bound).
    bool ensureScreenshotBuffer();
    void recordScreenshotCopy();
    void destroyScreenshotResources();
    bool m_screenshotPending = false;   // caller asked for this frame
    bool m_screenshotRecorded = false;  // copy recorded in the current endFrame
    bool m_screenshotReady = false;     // m_screenshotPixels holds valid rows
    VkBuffer m_shotBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_shotMemory = VK_NULL_HANDLE;
    void* m_shotMapped = nullptr;
    VkDeviceSize m_shotSize = 0;
    std::vector<unsigned char> m_screenshotPixels;

    vec3 m_fogColor{0.35f, 0.55f, 0.75f};
    float m_fogDensity = 0.0028f;
    float m_fogHeightFalloff = 0.018f;

    bool m_ssao = false;
    float m_ssaoRadius = 0.5f;
    float m_ssaoIntensity = 1.0f;
    float m_ssaoBias = 0.02f;

    bool m_ssr = false;
    bool m_ssgi = false;
    float m_ssgiRadius = 0.05f;
    float m_ssgiIntensity = 1.0f;
    float m_ssrMaxDistance = 8.0f;
    float m_ssrIntensity = 1.0f;
    float m_ssrRoughnessThreshold = 0.6f;

    bool m_motionBlur = false;
    float m_mbStrength = 1.0f;
    int m_mbSamples = 8;
    float m_mbMaxLength = 0.03f;

    std::shared_ptr<ui::UiGpuPass> m_uiPass;
    // The overlay's real Vulkan pipeline, one per render pass it can land in:
    // the forward colour pass, or the display pass of the deferred path (the
    // only writer of the swapchain there). Nothing bound these hooks before,
    // so UiGpuPass stayed a CPU mirror and no interface ever reached the
    // screen — see recordUiOverlay().
    ui::VulkanUiPipeline* uiPipelineFor(VkRenderPass pass);
    /** The same pass, with the texture-quad shaders: theme images draw through
     *  it, and it is null when the SPIR-V pair was not built. */
    ui::VulkanUiPipeline* uiImagePipelineFor(VkRenderPass pass);
    void recordUiOverlay(VkRenderPass pass, VkCommandBuffer cmd);
    std::unique_ptr<ui::VulkanUiPipeline> m_uiForward;
    std::unique_ptr<ui::VulkanUiPipeline> m_uiDisplay;
    std::unique_ptr<ui::VulkanUiPipeline> m_uiForwardImage;
    std::unique_ptr<ui::VulkanUiPipeline> m_uiDisplayImage;
    bool m_uiForwardFailed = false;
    bool m_uiDisplayFailed = false;
    bool m_uiForwardImageFailed = false;
    bool m_uiDisplayImageFailed = false;
    void* m_uiAtlasPass = nullptr;   // pipeline the font atlas was uploaded to
    /** What the two image pipelines already hold: the store's generation, one
     *  entry per pass (0 = display, 1 = forward). A decode bumps it, and only
     *  then is a texture copied to the GPU again. */
    uint32_t m_uiImagesGeneration[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
    const ui::UiImageStore* m_uiImageStore = nullptr;
    // Pixel space the current UI batch was laid out in (logical window px). The
    // swapchain may be larger on a HiDPI window; recordUiOverlay() maps this
    // space onto the whole framebuffer.
    float m_uiLayoutW = 0.f;
    float m_uiLayoutH = 0.f;
    int m_viewportW = 1280;
    int m_viewportH = 720;

    // GPU timestamp profiler (E9 / 2.14). Owns the query pools; init() runs
    // from createDevice(), shutdown() before the device goes away.
    GpuProfiler m_gpuProfiler;

    // --- Image-Based Lighting (brief P2 / S3) ---
    // 0 = GGX-prefiltered procedural sky (128x64 + 5 mips of roughness
    // LODs; mip 0 doubles as the mirror env, so no separate raw sky),
    // 1 = irradiance convolution (32x16, stores E/PI), 2 = split-sum BRDF
    // LUT (128x128, RG = scale/offset). All RGBA16F, all created in
    // createIblResources() (called from loadPipelines before the forward
    // set 0 is written) and destroyed with the device, not the swapchain.
    // On by default: the prefilter/irradiance/BRDF chain is always built, and
    // leaving it off fell back to a flat ambient that made every surface look
    // like paper. setIblEnabled(false) still turns it off.
    bool m_iblWanted = true; // setIblEnabled()
    bool m_iblReady = false;  // real maps uploaded (else 1x1 fallbacks)
    VkImage m_iblImages[3] = {};
    VkDeviceMemory m_iblMemory[3] = {};
    VkImageView m_iblViews[3] = {};
    VkSampler m_iblEnvSampler = VK_NULL_HANDLE; // repeat U / clamp V, mips
    VkSampler m_iblLutSampler = VK_NULL_HANDLE; // clamp, no mips
    // The sun the maps above were baked for (unit vector towards the sun), how
    // many setSun() calls have been seen, at which of them the maps were baked,
    // and whether a car's reflection probe owns the environment instead (the
    // showroom: its probe must not be overwritten by a sky re-bake).
    vec3 m_iblSunToward{0.3f, 0.8f, 0.2f};
    uint32_t m_sunCalls = 0;
    uint32_t m_iblSkyBakedAt = 0;
    bool m_iblFromProbe = false;
    // True while a showroom owns the light: setSun() then leaves the maps alone.
    bool m_iblFrozen = false;

    bool createIblResources();
    void destroyIblResources();
    // Re-bakes the environment maps for `light`'s sun (environment, prefilter
    // chain, irradiance) and uploads them into the images created by
    // createIblResources(). Used when the time of day moves the sun far enough
    // that the old sky no longer matches the light.
    void refreshSkyIbl(const DirectionalLight& light);
    bool createIblTexture(uint32_t width, uint32_t height, uint32_t mipLevels,
                          const std::vector<uint16_t>& rgba16f,
                          VkImage& image, VkDeviceMemory& memory, VkImageView& view);
    bool uploadIblPixels(VkImage image, uint32_t width, uint32_t height, uint32_t mipLevels,
                         const std::vector<uint16_t>& rgba16f, VkImageLayout oldLayout);

};

} // namespace ks::sim
