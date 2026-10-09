#pragma once

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
#include <memory>
#include "MathTypes.h"
#include "engine/Math/Frustum.h"
#include "engine/scene/LodWindow.h"

namespace ks::sim {

class CascadedShadowMap;
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
    std::string albedoTexture;   // resolved path, "" = none authored
    std::string normalTexture;   // resolved path, "" = none authored
    // P1 — resolved PBR map paths ("" = the materials.txt cell held a
    // scalar; the shader then multiplies by the white fallback = identity).
    std::string roughnessTexture;
    std::string metalnessTexture;
    float roughness = 0.35f; // identity 1.0 when roughnessTexture is set
    float metalness = 0.0f;  // identity 1.0 when metalnessTexture is set
    // Descriptor set 1 for this mesh + its 16-byte std140 material block.
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
    bool createDevice(VkInstance instance, VkSurfaceKHR surface);
    bool createSwapChain(VkSurfaceKHR surface, uint32_t width, uint32_t height);
    bool recreateSwapChain(uint32_t width, uint32_t height);
    // Depth-only shadow cascades are wired in here if a shadow map is
    // attached (see attachShadowMap()) — geometry submitted via drawMesh()
    // between beginFrame()/endFrame() is automatically also rendered into
    // each active cascade before the main color pass.
    void attachShadowMap(CascadedShadowMap* shadowMap) { m_shadowMap = shadowMap; }

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
    void setSun(const DirectionalLight& light) { m_sun = light; }

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

    // Display transform of the deferred path (the tonemap.frag pass that now
    // owns the backbuffer). Exposure and the tone-curve operator are the two
    // knobs the SDR and HDR10 encodings share; modes: 0 none, 1 Reinhard,
    // 2 ACES, 3 Uncharted2, 4 Filmic.
    void setExposure(float e) { m_exposure = e; }
    float exposure() const { return m_exposure; }
    void setTonemapMode(int mode) { m_tonemapMode = mode; }
    void setHdrWhiteNits(float nits) { if (nits > 0.0f) m_hdrWhiteNits = nits; }
    float hdrWhiteNits() const { return m_hdrWhiteNits; }

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

    // UI overlay GPU pass. SimulationLoop owns the pass (it builds the font
    // atlas + per-frame dynamic VB/IB); NativeRenderer keeps it alive and
    // pumps a frame through it while its render pass is open.
    void setUiGpuPass(std::shared_ptr<ui::UiGpuPass> pass) { m_uiPass = std::move(pass); }
    ui::UiGpuPass* uiGpuPass() { return m_uiPass.get(); }
    void drawUi(const ui::UiRenderer& ui);

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
    VkDevice device() const { return m_device; }
    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkCommandPool commandPool() const { return m_commandPool; }
    VkCommandBuffer currentCommandBuffer() const { return m_commandBuffer; }

    // Loads a baked DDS through the renderer-owned cache. The returned handles
    // remain valid until clearTextureCache(), shutdown(), or renderer
    // destruction; callers must not destroy them. This is the texture-loading
    // half of the material pipeline. Descriptor binding remains per-material.
    VkImageView textureView(const std::string& path, VkSampler& outSampler);
    std::size_t cachedTextureCount() const noexcept;
    void clearTextureCache();

private:
    bool createCommandPoolAndBuffer();
    bool createSyncObjects();
    bool createDepthResources(uint32_t width, uint32_t height);
    void destroySwapChain();
    void uploadMesh(NativeMesh& mesh);

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
                                    const std::string& metalnessPath);
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

    // --- Deferred path (opt-in, setDeferred / setTaa) ---
    bool ensureDeferredResources();
    void destroyDeferredResources();
    void writeLightingDescriptorSet(VkImageView shadowView, VkSampler shadowSampler);
    void writeResolveDescriptorSet();
    // Per-frame half of the display/bloom descriptors: both the bright-pass
    // extract and the tonemap pass read the history slot *this* frame's
    // resolve writes, so they are re-pointed every frame.
    void writeDisplayDescriptorSets(VkImageView resolvedView);

    // --- Occlusion culling: previous-frame max-depth grid ---
    // ensureOcclusionResources() builds the downsample pass + host readback
    // lazily on the first frame that needs them; recordOcclusionPass() runs
    // the downsample + copy after whichever geometry pass ran this frame;
    // destroyOcclusionResources() tears it all down with the swapchain.
    bool ensureOcclusionResources();
    void destroyOcclusionResources();
    void recordOcclusionPass(VkImage depthImage, VkImageView depthView);

    static constexpr int kGBufferCount = 3;
    static constexpr int kHistoryCount = 2;
    static constexpr int kJitterSamples = 8;
    static constexpr float kTaaFeedback = 0.9f;

    bool m_deferred = false;
    bool m_taa = false;
    bool m_deferredReady = false;
    bool m_deferredFailed = false;
    bool m_historyValid = false;
    bool m_prevViewProjValid = false;
    int m_jitterIndex = 0;
    int m_historyIndex = 0;
    mat4 m_prevViewProj;
    std::string m_shaderDir;

    // Display transform / HDR output (see setExposure/setHdrOutput).
    bool m_hdrRequested = false;        // caller asked for HDR10 ...
    bool m_hdrOutput = false;           // ... and the swapchain really is one
    bool m_swapchainColorSpaceExt = false; // VK_EXT_swapchain_colorspace (instance ext) present
    VkColorSpaceKHR m_swapChainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    float m_exposure = 1.0f;
    int m_tonemapMode = 2;              // 0 none, 1 Reinhard, 2 ACES, 3 Uncharted2, 4 Filmic
    static constexpr float kHdrPeakNits = 1000.0f; // highlight ceiling for the PQ shoulder
    // Nits given to the graded SDR-range signal (1.0) in the PQ encode. This
    // has to match the OS's SDR-white for HDR and SDR to look the same: Windows
    // composites SDR windows against its own SDR-white, measured at 80 nits
    // here (capture calibration against the SDR path), while BT.2408's 203
    // made the same frame ~2.5x brighter than SDR. Override per machine with
    // KS_HDR_WHITE_NITS.
    float m_hdrWhiteNits = 80.0f;

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
    VkPipeline m_gbufferPipeline = VK_NULL_HANDLE;
    VkPipeline m_lightingPipeline = VK_NULL_HANDLE;
    VkShaderModule m_gbufferVertModule = VK_NULL_HANDLE;
    VkShaderModule m_gbufferFragModule = VK_NULL_HANDLE;
    VkShaderModule m_lightingVertModule = VK_NULL_HANDLE;
    VkShaderModule m_lightingFragModule = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_resolveSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_resolvePool = VK_NULL_HANDLE;
    VkDescriptorSet m_resolveSet = VK_NULL_HANDLE;
    VkSampler m_hdrSampler = VK_NULL_HANDLE;
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
    // Everything that survived the frustum test: the shadow cascades render
    // this list (an object hidden from the camera can still cast a visible
    // shadow) ...
    std::vector<QueuedDraw> m_drawList;
    // ... while the camera passes render this one, which additionally
    // excludes what the previous-frame depth grid says is occluded.
    std::vector<QueuedDraw> m_mainDrawList;

    mat4 m_view;
    mat4 m_proj;
    vec3 m_camPosWS;
    float m_camNear = 0.5f;
    float m_camFar = 500.0f;
    DirectionalLight m_sun;
    CascadedShadowMap* m_shadowMap = nullptr;

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
    float m_ssrMaxDistance = 8.0f;
    float m_ssrIntensity = 1.0f;
    float m_ssrRoughnessThreshold = 0.6f;

    bool m_motionBlur = false;
    float m_mbStrength = 1.0f;
    int m_mbSamples = 8;
    float m_mbMaxLength = 0.03f;

    std::shared_ptr<ui::UiGpuPass> m_uiPass;
    int m_viewportW = 1280;
    int m_viewportH = 720;

    // --- Image-Based Lighting (brief P2 / S3) ---
    // 0 = GGX-prefiltered procedural sky (128x64 + 5 mips of roughness
    // LODs; mip 0 doubles as the mirror env, so no separate raw sky),
    // 1 = irradiance convolution (32x16, stores E/PI), 2 = split-sum BRDF
    // LUT (128x128, RG = scale/offset). All RGBA16F, all created in
    // createIblResources() (called from loadPipelines before the forward
    // set 0 is written) and destroyed with the device, not the swapchain.
    bool m_iblWanted = false; // setIblEnabled()
    bool m_iblReady = false;  // real maps uploaded (else 1x1 fallbacks)
    VkImage m_iblImages[3] = {};
    VkDeviceMemory m_iblMemory[3] = {};
    VkImageView m_iblViews[3] = {};
    VkSampler m_iblEnvSampler = VK_NULL_HANDLE; // repeat U / clamp V, mips
    VkSampler m_iblLutSampler = VK_NULL_HANDLE; // clamp, no mips

    bool createIblResources();
    void destroyIblResources();
    bool createIblTexture(uint32_t width, uint32_t height, uint32_t mipLevels,
                          const std::vector<uint16_t>& rgba16f,
                          VkImage& image, VkDeviceMemory& memory, VkImageView& view);

};

} // namespace ks::sim
