// Parity 2.1 — renderer validation ("Renderer validato").
//
// Drives the real Qt-free Vulkan renderer (src/simulator/NativeRenderer)
// end to end on a hidden window:
//
//   instance -> Win32 surface -> device -> swapchain -> forward pipeline ->
//   two frames of a known quad -> requestScreenshot() readback.
//
// What is asserted on the actual pixels:
//   - clear colour == setFog() colour, exact within rounding (fog density 0
//     so the clear value reaches the framebuffer untouched);
//   - a quad with one winding order rasterises (>12% coverage) and the same
//     quad with the reversed winding does not (<2%): VK_CULL_MODE_BACK_BIT
//     + VK_FRONT_FACE_COUNTER_CLOCKWISE are both effective;
//   - the lit centre pixel is the expected white (vertex white, sun on,
//     ambient + diffuse, fog off) — validates the FrameData UBO (sun, fog)
//     and the push-constant MVP;
//   - per-frame culling stats: submitted/drawn == 1, culled == 0.
//
// SKIPs (exit 0) when no Vulkan instance extensions / no GPU: the test must
// not fail a machine that simply cannot run Vulkan.

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "KsTest.h"
#include "NativeRenderer.h"
// Roadmap P1: the sprite quads fed to the renderer below come from the real
// CPU particle system, so this test also pins the buildQuads() byte layout
// to what particle.vert actually reads.
#include "engine/Graphics/ParticleSystem.h"
// Roadmap P2: the terrain frame below is built from the real mesh generator
// and heightmap synthesis, so this test also pins their vertex layout to what
// the shared NativeVertex pipeline reads.
#include "engine/terrain/TerrainMesh.h"
#include "engine/terrain/TerrainHeightmap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

// Requested size; the surface may hand back a different actual extent
// (Win32 window sizing / DPI), which we adopt after createSwapChain().
uint32_t kW = 320;
uint32_t kH = 240;
constexpr float kPi = 3.14159265f;

using ks::sim::DirectionalLight;
using ks::sim::mat4;
using ks::sim::NativeMesh;
using ks::sim::NativeRenderer;
using ks::sim::NativeVertex;
using ks::sim::vec3;

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcW(h, m, w, l);
}

NativeMesh makeQuad(bool reversed)
{
    NativeMesh mesh;
    NativeVertex v;
    v.nx = 0; v.ny = 0; v.nz = 1; // faces +z, camera sits at +z looking down -z
    v.u = 0; v.v = 0;
    v.r = 1; v.g = 1; v.b = 1; v.a = 1;
    v.px = -1; v.py = -1; v.pz = 0; mesh.vertices.push_back(v);
    v.px = 1;  v.py = -1;           mesh.vertices.push_back(v);
    v.px = 1;  v.py = 1;            mesh.vertices.push_back(v);
    v.px = -1; v.py = 1;            mesh.vertices.push_back(v);
    if (reversed) mesh.indices = {2, 1, 0, 3, 2, 0};
    else          mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

void appendU32(std::vector<unsigned char>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<unsigned char>(value & 0xFFu));
    bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
    bytes.push_back(static_cast<unsigned char>((value >> 16) & 0xFFu));
    bytes.push_back(static_cast<unsigned char>((value >> 24) & 0xFFu));
}

// A minimal 4x4 uncompressed RGBA DDS. TextureRuntime intentionally uses the
// shared DdsReader instead of a second parser, so this exercises its complete
// path: DDS decode -> staging buffer -> device-local image -> sampled view.
bool writeTextureFixture(const std::filesystem::path& path)
{
    constexpr std::uint32_t kWidth = 4;
    constexpr std::uint32_t kHeight = 4;
    constexpr std::uint32_t kFlags = 0x0000100Fu; // caps, height, width, pitch, pixel format
    constexpr std::uint32_t kRgbAlpha = 0x00000042u;
    std::vector<unsigned char> bytes;
    bytes.reserve(128 + kWidth * kHeight * 4);
    bytes.insert(bytes.end(), {'D', 'D', 'S', ' '});
    appendU32(bytes, 124);
    appendU32(bytes, kFlags);
    appendU32(bytes, kHeight);
    appendU32(bytes, kWidth);
    appendU32(bytes, kWidth * 4);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    for (int index = 0; index < 11; ++index) appendU32(bytes, 0);
    appendU32(bytes, 32);
    appendU32(bytes, kRgbAlpha);
    appendU32(bytes, 0);
    appendU32(bytes, 32);
    appendU32(bytes, 0x000000FFu);
    appendU32(bytes, 0x0000FF00u);
    appendU32(bytes, 0x00FF0000u);
    appendU32(bytes, 0xFF000000u);
    for (int index = 0; index < 5; ++index) appendU32(bytes, 0);
    for (std::uint32_t index = 0; index < kWidth * kHeight; ++index) {
        bytes.push_back(220);
        bytes.push_back(80);
        bytes.push_back(30);
        bytes.push_back(255);
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

// Clear colour is setFog({0, 0.5, 0}) -> BGRA bytes (0, 128, 0, 255);
// 0.5 * 255 = 127.5 rounds to 127 or 128 depending on the driver.
bool isClearPixel(const unsigned char* px)
{
    return std::abs(int(px[0]) - 0) <= 4 && std::abs(int(px[1]) - 128) <= 4 &&
           std::abs(int(px[2]) - 0) <= 4;
}

struct FrameOut {
    float coverage = 0.0f;      // fraction of pixels that differ from clear
    bool cornerClear[2] = {false, false};
    bool centerClear = false;
    bool centerWhite = false;
    int centerMax = 0;          // brightest channel at the centre pixel
    int cornerMax = 0;          // brightest channel at the top-left pixel
    NativeRenderer::FrameStats stats;
};

bool renderFrame(NativeRenderer& r, const char* meshName, FrameOut& out,
                 const float* quads = nullptr, size_t quadsFloats = 0,
                 std::vector<unsigned char>* pixelsOut = nullptr,
                 const char* extraMesh = nullptr)
{
    r.requestScreenshot();
    if (!r.beginFrame()) {
        std::printf("test_renderer: beginFrame failed\n");
        return false;
    }
    r.drawMesh(meshName, mat4()); // identity model matrix
    if (extraMesh) r.drawMesh(extraMesh, mat4());
    // Always set, never "leave whatever was there": a null/0 upload clears
    // the sprite, so every capture below is explicitly with or without it.
    r.setParticleVertices(quads, quadsFloats);
    r.endFrame();
    if (!r.screenshotReady()) {
        std::printf("test_renderer: screenshot not ready after endFrame\n");
        return false;
    }

    const std::vector<unsigned char>& px = r.screenshotPixels();
    const size_t expected = size_t(kW) * kH * 4;
    if (px.size() != expected) {
        std::printf("test_renderer: screenshot size %zu != %zu\n", px.size(), expected);
        return false;
    }

    out.cornerClear[0] = isClearPixel(px.data());
    out.cornerClear[1] = isClearPixel(px.data() + (size_t(kW) * (kH - 1) + (kW - 1)) * 4);
    out.cornerMax = std::max<int>(px[0], std::max<int>(px[1], px[2]));

    const size_t center = ((size_t(kH) / 2) * kW + kW / 2) * 4;
    out.centerClear = isClearPixel(px.data() + center);
    out.centerWhite = px[center] >= 245 && px[center + 1] >= 245 && px[center + 2] >= 245;
    out.centerMax = std::max<int>(px[center], std::max<int>(px[center + 1], px[center + 2]));

    size_t differing = 0;
    for (size_t i = 0; i < expected; i += 4) {
        if (!isClearPixel(px.data() + i))
            ++differing;
    }
    out.coverage = float(differing) / float(size_t(kW) * kH);

    out.stats = r.lastFrameStats();
    if (pixelsOut)
        *pixelsOut = px;
    return true;
}

// Pixels whose colour differs between two captures of the same scene — the
// only colour-space-independent way to say "that feature changed the image"
// when a tonemap sits between the geometry pass and the readback.
size_t countDifferingPixels(const std::vector<unsigned char>& a,
                            const std::vector<unsigned char>& b)
{
    if (a.size() != b.size() || a.empty()) return 0;
    size_t n = 0;
    for (size_t i = 0; i + 2 < a.size(); i += 4)
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ++n;
    return n;
}

// One sprite, straight out of the CPU particle system: a bright red disc
// held between the camera (0,0,3) and the white quad (z=0), so it lands on
// the centre pixel of the frame in both the forward and the deferred pass.
std::vector<float> makeParticleSprite()
{
    namespace ps = ks::engine::graphics;
    ps::ParticleSystem system;
    ps::ParticleEmitter emitter;
    emitter.position = {0.0f, 0.0f, 1.5f}; // 1.5 m in front of the camera
    emitter.direction = {0.0f, 0.0f, -1.0f};
    emitter.spreadDeg = 0.0f;
    emitter.speed = 0.0f;
    emitter.speedJitter = 0.0f;
    emitter.spawnRate = 0.0f;              // burst-driven, nothing respawns
    emitter.lifetime = 10.0f;
    emitter.size = 0.6f;                   // ~70% of the frame height here
    emitter.sizeJitter = 0.0f;
    emitter.color = {1.0f, 0.0f, 0.0f};    // unmistakably not the white quad
    emitter.alpha = 1.0f;
    emitter.gravity = {0.0f, 0.0f, 0.0f};
    system.addEmitter(emitter);
    system.burst(0, 1);

    static constexpr size_t kMaxFloats =
        size_t(ps::ParticleSystem::kMaxParticles) * ps::ParticleSystem::kVerticesPerParticle *
        ps::ParticleSystem::kFloatsPerVertex;
    std::vector<float> quads(kMaxFloats);
    const size_t floats = system.buildQuads(quads.data(), quads.size());
    quads.resize(floats);
    return quads;
}

// Roadmap P2: the same trackside ground SimulationLoop builds behind
// KS_TERRAIN, scaled to the test camera (160 m across, sits inside the 100 m
// far plane) and centred on the origin.
NativeMesh makeTerrain()
{
    namespace terrain = ks::engine::terrain;
    constexpr int kGrid = 65;
    constexpr float kWorld = 160.0f;
    terrain::FbmHeightmapParams params;
    params.seed = 7u;
    params.baseHeight = -6.0f;
    params.amplitude = 8.0f;
    params.frequency = 3.0f;
    const std::vector<float> heights = terrain::generateFbmHeightmap(kGrid, kGrid, params);
    terrain::TerrainMeshData data =
        terrain::generateTerrainMesh(heights, kGrid, kGrid, kWorld, kWorld, 25.0f);

    NativeMesh mesh;
    const float half = kWorld * 0.5f;
    mesh.vertices.reserve(data.vertices.size());
    for (const terrain::TerrainVertex& v : data.vertices) {
        NativeVertex nv;
        nv.px = v.px - half; nv.py = v.py; nv.pz = v.pz - half;
        nv.nx = v.nx; nv.ny = v.ny; nv.nz = v.nz;
        nv.u = v.u; nv.v = v.v;
        nv.r = 0.35f; nv.g = 0.45f; nv.b = 0.25f; nv.a = 1.0f;
        mesh.vertices.push_back(nv);
    }
    mesh.indices = std::move(data.indices);
    return mesh;
}

} // namespace

int main()
{
    // ---- Instance (only when the platform really exposes Vulkan). ----
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    if (extCount)
        vkEnumerateInstanceExtensionProperties(nullptr, &extCount, exts.data());
    bool hasSurface = false, hasWin32 = false;
    for (const VkExtensionProperties& e : exts) {
        if (std::strcmp(e.extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0) hasSurface = true;
        if (std::strcmp(e.extensionName, VK_KHR_WIN32_SURFACE_EXTENSION_NAME) == 0) hasWin32 = true;
    }
    if (!hasSurface || !hasWin32) {
        std::printf("test_renderer: SKIP - surface/win32 Vulkan extensions unavailable\n");
        return 0;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "kseditor test_renderer";
    app.apiVersion = VK_API_VERSION_1_0;

    const char* instExt[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = instExt;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
        std::printf("test_renderer: SKIP - vkCreateInstance failed\n");
        return 0;
    }
    struct InstanceCleanup {
        VkInstance i;
        ~InstanceCleanup() { if (i) vkDestroyInstance(i, nullptr); }
    } instanceCleanup{instance};

    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
    if (gpuCount == 0) {
        std::printf("test_renderer: SKIP - no Vulkan physical device\n");
        return 0;
    }

    // ---- Hidden window + surface. ----
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"KsRendererTestWindow";
    RegisterClassW(&wc); // idempotent enough: failure just means already registered

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"renderer test", WS_OVERLAPPED, 0, 0,
                                int(kW), int(kH), nullptr, nullptr, hInst, nullptr);
    KS_CHECK(hwnd != nullptr);
    if (!hwnd)
        return KS_TEST_RESULT("test_renderer");
    struct WndCleanup {
        HWND h;
        HINSTANCE inst;
        ~WndCleanup() { if (h) DestroyWindow(h); }
    } wndCleanup{hwnd, hInst};

    VkWin32SurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = hInst;
    sci.hwnd = hwnd;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    KS_CHECK(vkCreateWin32SurfaceKHR(instance, &sci, nullptr, &surface) == VK_SUCCESS);
    if (surface == VK_NULL_HANDLE)
        return KS_TEST_RESULT("test_renderer");
    struct SurfaceCleanup {
        VkSurfaceKHR s;
        ~SurfaceCleanup() { if (s) vkDestroySurfaceKHR(cleanupInstance, s, nullptr); }
        VkInstance cleanupInstance;
    } surfaceCleanup{surface, instance};

    // Declared last: destroyed first (device before surface before instance).
    NativeRenderer renderer;

    // ---- Renderer bring-up: after this point failures are real bugs on a
    // machine that has a GPU (we checked), so they FAIL rather than SKIP. ----
    KS_CHECK(renderer.createDevice(instance, surface));
    if (!renderer.isInitialized())
        return KS_TEST_RESULT("test_renderer");

    const bool swapOk = renderer.createSwapChain(surface, kW, kH);
    KS_CHECK(swapOk);
    KS_CHECK(renderer.loadPipelines(KS_TEST_SHADER_DIR));
    if (!swapOk)
        return KS_TEST_RESULT("test_renderer");

    // ---- Runtime DDS cache (roadmap 2A): a real DDS is decoded by the
    // shared reader, uploaded via the renderer's transfer queue, cached by
    // normalized path and safely falls back for a missing file. The material
    // descriptor binding comes in the next slice; this pins the resource
    // lifetime and upload contract before that wiring is added. ----
    const std::filesystem::path texturePath =
        std::filesystem::temp_directory_path() /
        ("ksengine_texture_runtime_" + std::to_string(GetCurrentProcessId()) + ".dds");
    KS_CHECK(writeTextureFixture(texturePath));
    VkSampler textureSampler = VK_NULL_HANDLE;
    const VkImageView textureView = renderer.textureView(texturePath.string(), textureSampler);
    KS_CHECK(textureView != VK_NULL_HANDLE);
    KS_CHECK(textureSampler != VK_NULL_HANDLE);
    KS_CHECK(renderer.cachedTextureCount() == 1);
    VkSampler cachedSampler = VK_NULL_HANDLE;
    KS_CHECK(renderer.textureView(texturePath.string(), cachedSampler) == textureView);
    KS_CHECK(cachedSampler == textureSampler);
    VkSampler fallbackSampler = VK_NULL_HANDLE;
    const VkImageView fallbackView =
        renderer.textureView((texturePath.parent_path() / "missing_texture.dds").string(),
                             fallbackSampler);
    KS_CHECK(fallbackView != VK_NULL_HANDLE);
    KS_CHECK(fallbackSampler != VK_NULL_HANDLE);
    KS_CHECK(renderer.cachedTextureCount() == 1);
    renderer.clearTextureCache();
    KS_CHECK(renderer.cachedTextureCount() == 0);
    std::error_code removeError;
    std::filesystem::remove(texturePath, removeError);

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer.physicalDevice(), surface, &caps);
    if (caps.currentExtent.width != 0xFFFFFFFF && caps.currentExtent.width > 0) {
        kW = caps.currentExtent.width;
        kH = caps.currentExtent.height;
    }
    std::printf("test_renderer: swapchain extent %u x %u\n", kW, kH);

    // ---- Known scene: white quad, white sun straight at it, fog off. ----
    DirectionalLight sun;
    sun.direction = vec3(0, 0, -1); // shader: L = -direction -> (0,0,1)
    sun.color = vec3(1, 1, 1);
    sun.intensity = 1.0f;
    renderer.setSun(sun);
    renderer.setFog(vec3(0.0f, 0.5f, 0.0f), 0.0f, 0.0f);
    renderer.setOcclusionCulling(false); // occlusion culling has occlusion_test

    const mat4 view = mat4::lookAt(vec3(0, 0, 3), vec3(0, 0, 0), vec3(0, 1, 0));
    const mat4 proj =
        mat4::perspective(60.0f * kPi / 180.0f, float(kW) / float(kH), 0.1f, 100.0f);
    renderer.setCamera(view, proj, 0.1f, 100.0f);

    renderer.setMesh("quadA", makeQuad(false));
    renderer.setMesh("quadB", makeQuad(true));

    FrameOut a, b;
    const bool okA = renderFrame(renderer, "quadA", a);
    const bool okB = renderFrame(renderer, "quadB", b);
    KS_CHECK(okA);
    KS_CHECK(okB);
    if (okA && okB) {
        // Culling stats: everything queued reached the GPU-side pipeline
        // (frustum culling on, both quads in view).
        KS_CHECK(a.stats.submitted == 1);
        KS_CHECK(a.stats.drawn == 1);
        KS_CHECK(a.stats.culled == 0);
        KS_CHECK(b.stats.submitted == 1);
        KS_CHECK(b.stats.drawn == 1);
        KS_CHECK(b.stats.culled == 0);

        // Exact clear colour in the corners on both frames.
        KS_CHECK(a.cornerClear[0]);
        KS_CHECK(a.cornerClear[1]);
        KS_CHECK(b.cornerClear[0]);
        KS_CHECK(b.cornerClear[1]);

        // Back-face culling: exactly one winding rasterises.
        const bool aVisible = a.coverage > 0.12f;
        const bool bVisible = b.coverage > 0.12f;
        KS_CHECK(aVisible != bVisible);
        const FrameOut& vis = aVisible ? a : b;
        const FrameOut& hid = aVisible ? b : a;
        KS_CHECK(vis.coverage > 0.12f);  // ~25% of a 320x240 frame
        KS_CHECK(hid.coverage < 0.02f);  // fully rejected by the rasteriser
        KS_CHECK(hid.centerClear);

        // Lit quad centre = white (ambient 0.25 + diffuse 1.0 saturates);
        // validates FrameData UBO (sun + fog) and the MVP push constants.
        KS_CHECK(vis.centerWhite);
        KS_CHECK(!vis.centerClear);
    }

    // ---- Deferred path with inline SSAO + SSR + motion blur (roadmap
    // ksengine-vs-cryengine P0). This is the only pixel-level coverage the
    // deferred chain gets: the FrameData block gained aoParams/ssrParams/
    // motionBlurParams after fogParams and the lighting/resolve shaders now
    // fold those terms in. A lone quad in empty space legitimately occludes
    // nothing, reflects nothing and does not move, so all three must leave the
    // image alone - the assertions are that the whole chain (GBuffer ->
    // lighting+SSAO+SSR -> resolve+motion blur -> display) still produces the
    // lit quad and does not black it out. The display pass runs the default
    // ACES curve, so exact 255 whites are not expected here, only "lit".
    renderer.setDeferred(true);
    renderer.setSsao(true, 0.5f, 1.0f);
    renderer.setSsr(true, 8.0f, 1.0f, 0.6f);
    renderer.setMotionBlur(true, 1.0f, 8, 0.03f);
    FrameOut d;
    const bool okD = renderFrame(renderer, "quadA", d);
    KS_CHECK(okD);
    if (okD) {
        KS_CHECK(d.stats.submitted == 1);
        KS_CHECK(d.stats.drawn == 1);
        KS_CHECK(d.stats.culled == 0);
        KS_CHECK(d.coverage > 0.12f);   // quad survived the GBuffer round-trip
        KS_CHECK(!d.centerClear);       // centre is lit geometry, not background
        KS_CHECK(d.centerMax >= 180);   // SSAO/gamma must not crush it to black
        // The quad must be clearly brighter than the background *in the same
        // image*: the deferred display pass recolours both, so comparing the
        // two pixels against each other is the only colour-independent way
        // to say "this frame really contains the lit quad".
        KS_CHECK(d.centerMax > d.cornerMax + 20);
        std::printf("test_renderer: deferred+SSAO+SSR+motionblur centre max %d corner max %d\n",
                    d.centerMax, d.cornerMax);
    }
    renderer.setDeferred(false);
    renderer.setSsao(false);
    renderer.setSsr(false);
    renderer.setMotionBlur(false);

    // ---- Particle sprite (roadmap ksengine-vs-cryengine P1): a
    // differential frame on *both* paths. Absolute colours are not asserted
    // — the sprite leaves the geometry pass in linear light and the deferred
    // path tone-maps it — so the test captures the same scene twice, with
    // and without the upload, requires the sprite to have moved the image at
    // all, and checks only the relative fact that the centre pixel turned
    // red (readback is BGRA: red is byte 2). The sprite itself is built by
    // the CPU particle system, which pins buildQuads()'s byte layout to what
    // particle.vert reads. ----
    const std::vector<float> sprite = makeParticleSprite();
    KS_CHECK(sprite.size() == 6u * 12u); // one particle = 6 vertices x 12 floats
    const size_t framePx = size_t(kW) * kH;
    const size_t centre = ((size_t(kH) / 2) * kW + kW / 2) * 4;

    {
        std::vector<unsigned char> without, withp;
        FrameOut fo, fw;
        KS_CHECK(renderFrame(renderer, "quadA", fo, nullptr, 0, &without));
        KS_CHECK(renderFrame(renderer, "quadA", fw, sprite.data(), sprite.size(), &withp));
        const size_t changed = countDifferingPixels(without, withp);
        KS_CHECK(changed > framePx / 100);                  // >1% of the frame
        // Readback is BGRA, so blue sits at [0] and red at [2]: the centre
        // pixel has to have gone from the white quad to the red sprite.
        KS_CHECK(withp[centre + 2] > withp[centre] + 30);
        std::printf("test_renderer: forward sprite changed %zu of %zu px\n", changed, framePx);
    }

    renderer.setDeferred(true);
    {
        std::vector<unsigned char> without, withp;
        FrameOut fo, fw;
        KS_CHECK(renderFrame(renderer, "quadA", fo, nullptr, 0, &without));
        KS_CHECK(renderFrame(renderer, "quadA", fw, sprite.data(), sprite.size(), &withp));
        const size_t changed = countDifferingPixels(without, withp);
        KS_CHECK(changed > framePx / 100);                  // GBuffer sprite drew
        KS_CHECK(withp[centre + 2] > withp[centre] + 30);    // and stayed red
        std::printf("test_renderer: deferred sprite changed %zu of %zu px\n", changed, framePx);
    }
    renderer.setParticleVertices(nullptr, 0); // no sprite in any later frame
    renderer.setDeferred(false);

    // ---- Trackside terrain (roadmap ksengine-vs-cryengine P2): a real
    // TerrainMesh drawn through the ordinary mesh path, so the differential
    // frame is the whole proof - same scene with and without the extra
    // drawMesh() call. Checked on the forward path and inside the GBuffer
    // pass, plus the culling stats saying one more draw reached the GPU. ----
    renderer.setMesh("terrain", makeTerrain());
    {
        std::vector<unsigned char> without, withp;
        FrameOut fo, fw;
        KS_CHECK(renderFrame(renderer, "quadA", fo, nullptr, 0, &without));
        KS_CHECK(renderFrame(renderer, "quadA", fw, nullptr, 0, &withp, "terrain"));
        const size_t changed = countDifferingPixels(without, withp);
        KS_CHECK(changed > framePx / 100);            // ground moved the image
        KS_CHECK(fw.stats.drawn >= fo.stats.drawn + 1);
        std::printf("test_renderer: forward terrain changed %zu of %zu px\n", changed, framePx);
    }
    renderer.setDeferred(true);
    {
        std::vector<unsigned char> without, withp;
        FrameOut fo, fw;
        KS_CHECK(renderFrame(renderer, "quadA", fo, nullptr, 0, &without));
        KS_CHECK(renderFrame(renderer, "quadA", fw, nullptr, 0, &withp, "terrain"));
        const size_t changed = countDifferingPixels(without, withp);
        KS_CHECK(changed > framePx / 100);            // terrain reached the GBuffer
        KS_CHECK(fw.stats.drawn >= fo.stats.drawn + 1);
        std::printf("test_renderer: deferred terrain changed %zu of %zu px\n", changed, framePx);
    }
    renderer.setDeferred(false);

    renderer.shutdown();
    return KS_TEST_RESULT("test_renderer");
}
