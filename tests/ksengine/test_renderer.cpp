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
// Roadmap 2.4/2.5 — reference content (content/baked, committed; regenerate
// with tools/make_reference_content.ps1) is then driven end to end on the
// same device: manifest -> materials.txt (raw "skin:paint.dds" name,
// sanitised into textures/) -> DDS upload -> descriptor set 1 -> pixels,
// the NMS2 [0..1] m LOD window through the culled/drawn counters, the
// deferred GGX highlight for roughness 0.05 vs 0.95, the brief P1
// metalness pair (scalar dielectric vs metalness *map*, metal_mask.dds ->
// GBuffer RT0.a -> metallic F0), the reference car bake
// through loadMeshFromFile(), and the "useful message" contract for a
// missing manifest / mesh / texture / shader (captured stderr).
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
// Roadmap 2.4/2.5: reference content (materials.txt pairing) + the PNG
// artifact writer for the screenshot regression.
#include "MaterialCache.h"
#include "engine/material/PngCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
// StderrCapture swaps fd 2 at the fd level (roadmap 2.5 message checks).
#include <fcntl.h>
#include <io.h>

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

// Roadmap 2.5 — fraction of pixels that differ from the fog-clear colour,
// the same definition renderFrame() uses, for the reference captures below.
float coverageOf(const std::vector<unsigned char>& px)
{
    if (px.empty()) return 0.0f;
    size_t differing = 0;
    for (size_t i = 0; i + 3 < px.size(); i += 4)
        if (!isClearPixel(px.data() + i)) ++differing;
    return float(differing) / float(px.size() / 4);
}

// Roadmap 2.4/2.5 — minimal NMS2 writer, the exact byte layout
// NativeRenderer::loadMeshFromFile() reads, for the broken-content fixtures
// (the committed reference bakes come from tools/make_reference_content.ps1).
void writeNmsh(const std::filesystem::path& path, const NativeMesh& mesh,
               float lodIn = 0.0f, float lodOut = 1.0e9f)
{
    std::ofstream f(path, std::ios::binary);
    f.write("NMS2", 4);
    const std::uint32_t v = static_cast<std::uint32_t>(mesh.vertices.size());
    const std::uint32_t i = static_cast<std::uint32_t>(mesh.indices.size());
    f.write(reinterpret_cast<const char*>(&v), 4);
    f.write(reinterpret_cast<const char*>(&i), 4);
    f.write(reinterpret_cast<const char*>(&lodIn), 4);
    f.write(reinterpret_cast<const char*>(&lodOut), 4);
    f.write(reinterpret_cast<const char*>(mesh.vertices.data()),
            static_cast<std::streamsize>(sizeof(NativeVertex) * v));
    f.write(reinterpret_cast<const char*>(mesh.indices.data()),
            static_cast<std::streamsize>(sizeof(std::uint32_t) * i));
}

// Roadmap 2.5 — captures every byte written to stderr while alive, so the
// "useful message for a missing asset/shader" contract can be asserted. The
// swap happens at fd level because fprintf(stderr) is unbuffered and lands
// in the file immediately; stop() restores fd 2 and returns the text.
class StderrCapture
{
public:
    StderrCapture()
    {
        static int s_next = 0;
        std::fflush(stderr);
        m_saved = _dup(_fileno(stderr));
        m_path = std::filesystem::temp_directory_path() /
                 ("ksengine_stderr_" + std::to_string(GetCurrentProcessId()) + "_" +
                  std::to_string(s_next++) + ".log");
        { std::ofstream touch(m_path); } // create, then reopen by fd
        const int fd = _open(m_path.string().c_str(), _O_WRONLY | _O_TRUNC);
        if (fd >= 0 && m_saved >= 0) _dup2(fd, _fileno(stderr));
        if (fd >= 0) _close(fd);
    }
    StderrCapture(const StderrCapture&) = delete;
    StderrCapture& operator=(const StderrCapture&) = delete;

    std::string stop()
    {
        if (m_saved < 0) return {};
        std::fflush(stderr);
        _dup2(m_saved, _fileno(stderr));
        _close(m_saved);
        m_saved = -1;
        std::ifstream in(m_path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
        return text;
    }
    ~StderrCapture() { (void)stop(); }

private:
    int m_saved = -1;
    std::filesystem::path m_path;
};

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

    // ---- Roadmap 2.4/2.5 — reference content, end to end --------------------
    // content/baked (committed; regenerate with tools/make_reference_content.ps1)
    // is the same bake the windowed app loads at startup, here driven through
    // the whole runtime chain: manifest -> materials.txt (raw "skin:paint.dds"
    // name sanitised into textures/) -> DDS upload -> descriptor set 1 ->
    // pixels, plus the NMS2 [0..1] m LOD window, the deferred GGX roughness
    // path and the reference car bake. Readback is BGRA, every frame draws
    // exactly the mesh it names, so the culling counters attribute cleanly.
    const std::string refBake = KS_TEST_REFERENCE_BAKE;
    const int refLoaded = renderer.loadMeshesFromManifest(refBake);
    std::printf("test_renderer: reference bake loaded %d mesh(es) from %s\n",
                refLoaded, refBake.c_str());
    KS_CHECK(refLoaded >= 7); // plain sign smooth rough coated metal lodable asphalt asphalt_tilt ground
    KS_CHECK(renderer.cachedTextureCount() == 5);   // skin_paint + metal_mask + asphalt + 2 normals

    const mat4 projRef =
        mat4::perspective(60.0f * kPi / 180.0f, float(kW) / float(kH), 0.1f, 100.0f);
    auto aimAt = [&](float ex, float ey, float ez, float tx, float ty, float tz, vec3 up) {
        renderer.setCamera(mat4::lookAt(vec3(ex, ey, ez), vec3(tx, ty, tz), up),
                           projRef, 0.1f, 100.0f);
    };
    DirectionalLight sunRef;
    sunRef.color = vec3(1, 1, 1);
    sunRef.intensity = 1.0f;
    sunRef.direction = vec3(0, 0, 1); // L = -direction = (0,0,-1): head-on for the -z quads
    renderer.setSun(sunRef);

    struct RefShot {
        bool ok = false;
        std::vector<unsigned char> px;
        NativeRenderer::FrameStats stats{};
    };
    auto capture = [&](const char* mesh, const mat4& model, RefShot& out) {
        renderer.requestScreenshot();
        if (!renderer.beginFrame()) {
            std::printf("test_renderer: beginFrame failed\n");
            return;
        }
        renderer.drawMesh(mesh, model);
        renderer.endFrame();
        if (!renderer.screenshotReady()) {
            std::printf("test_renderer: no screenshot readback\n");
            return;
        }
        out.px = renderer.screenshotPixels();
        out.stats = renderer.lastFrameStats();
        out.ok = out.px.size() == size_t(kW) * kH * 4;
    };
    auto centrePx = [](const RefShot& s) -> const unsigned char* {
        return s.px.data() + ((size_t(kH) / 2) * kW + kW / 2) * 4;
    };
    // Fractional (0..1) pixel probe — used off-centre where the GGX spike
    // of a 0.05-roughness surface cannot saturate the comparison (P1).
    auto pxAt = [](const RefShot& s, float fx, float fy) -> const unsigned char* {
        const size_t x = std::min(static_cast<size_t>(float(kW) * fx), size_t(kW) - 1);
        const size_t y = std::min(static_cast<size_t>(float(kH) * fy), size_t(kH) - 1);
        return s.px.data() + (y * size_t(kW) + x) * 4;
    };
    auto redDominant = [](const unsigned char* c) {
        return int(c[2]) > int(c[1]) + 40 && int(c[2]) > int(c[0]) + 40;
    };
    auto greenDominant = [](const unsigned char* c) {
        return int(c[1]) > int(c[2]) + 40 && int(c[1]) > int(c[0]) + 40;
    };

    // (a) No materials.txt row: defaults + white albedo fallback -> the lit
    //     centre is exact white (the plain-quad pin), corners stay clear.
    aimAt(0, 3, -3, 0, 3, 0, vec3(0, 1, 0));
    RefShot plainShot;
    capture("plain", mat4(), plainShot);
    KS_CHECK(plainShot.ok);
    if (plainShot.ok) {
        const unsigned char* c = centrePx(plainShot);
        KS_CHECK(c[0] >= 245 && c[1] >= 245 && c[2] >= 245); // BGRA all white
        KS_CHECK(isClearPixel(plainShot.px.data()));          // corners = fog clear
        KS_CHECK(plainShot.stats.submitted == 1);
        KS_CHECK(plainShot.stats.drawn == 1);
        KS_CHECK(plainShot.stats.culled == 0);
    }

    // (b) Textured: the row's raw KN5 name must sanitise to
    //     textures/skin_paint.dds and the orange albedo must reach the frame.
    RefShot signShot;
    capture("sign", mat4(), signShot);
    KS_CHECK(signShot.ok);
    if (signShot.ok) {
        KS_CHECK(coverageOf(signShot.px) > 0.12f);
        const unsigned char* c = centrePx(signShot);
        KS_CHECK(!isClearPixel(c));
        KS_CHECK(redDominant(c)); // orange paint: not white, not clear
        KS_CHECK(signShot.stats.drawn == 1);
    }

    // (c) Ground plane from above: winding + texture on the big plane the
    //     windowed session looks down at (lit from straight up).
    sunRef.direction = vec3(0, -1, 0);
    renderer.setSun(sunRef);
    aimAt(0, 10, 0, 0, 0, 0, vec3(0, 0, -1));
    RefShot groundShot;
    capture("ground", mat4(), groundShot);
    KS_CHECK(groundShot.ok);
    if (groundShot.ok) {
        KS_CHECK(coverageOf(groundShot.px) > 0.5f);
        KS_CHECK(redDominant(centrePx(groundShot)));
        KS_CHECK(groundShot.stats.drawn == 1);
    }

    // (d) NMS2 LOD window [0..1] m: past the window at ~3 m (culled counter,
    //     centre clear), inside it at ~0.4 m (drawn).
    sunRef.direction = vec3(0, 0, 1);
    renderer.setSun(sunRef);
    aimAt(0, 3, -3, 0, 3, 0, vec3(0, 1, 0));
    RefShot lodFar;
    capture("lodable", mat4(), lodFar);
    KS_CHECK(lodFar.ok);
    if (lodFar.ok) {
        KS_CHECK(lodFar.stats.submitted == 1);
        KS_CHECK(lodFar.stats.culled == 1);
        KS_CHECK(lodFar.stats.drawn == 0);
        KS_CHECK(isClearPixel(centrePx(lodFar)));
    }
    aimAt(0, 3, -0.5f, 0, 3, 0, vec3(0, 1, 0));
    RefShot lodNear;
    capture("lodable", mat4(), lodNear);
    KS_CHECK(lodNear.ok);
    if (lodNear.ok) {
        KS_CHECK(lodNear.stats.drawn == 1);
        KS_CHECK(lodNear.stats.culled == 0);
        KS_CHECK(!isClearPixel(centrePx(lodNear)));
    }

    // (e) Deferred: identical geometry and albedo, materials.txt roughness
    //     0.05 vs 0.95 -> only the smooth surface gets the GGX highlight, so
    //     its centre blue channel lifts far above the rough one (GBuffer
    //     RT1.w = material.roughness, deferred_lighting.frag applies GGX).
    renderer.setDeferred(true);
    aimAt(0, 3, -3, 0, 3, 0, vec3(0, 1, 0));
    RefShot smoothShot, roughShot;
    capture("smooth", mat4(), smoothShot);
    capture("rough", mat4(), roughShot);
    KS_CHECK(smoothShot.ok && roughShot.ok);
    if (smoothShot.ok && roughShot.ok) {
        KS_CHECK(smoothShot.stats.drawn == 1 && roughShot.stats.drawn == 1);
        const unsigned char* sm = centrePx(smoothShot);
        const unsigned char* rg = centrePx(roughShot);
        KS_CHECK(int(sm[0]) > int(rg[0]) + 40); // blue: specular highlight
        KS_CHECK(std::max<int>(rg[0], std::max<int>(rg[1], rg[2])) >= 180);
        std::printf("test_renderer: deferred roughness blue smooth=%u rough=%u\n",
                    static_cast<unsigned>(sm[0]), static_cast<unsigned>(rg[0]));
    }

    // (e2) Brief P1 metalness: "metal" is the same quad, same albedo and
    //     same roughness 0.05 as "smooth", but its materials.txt metalness
    //     cell is a *texture* (metal_mask.dds, red = mask 1.0) — so this
    //     proves the whole map chain: dual-typed cell -> resolved DDS ->
    //     descriptor binding 3/4 -> shader sample -> GBuffer RT0.a ->
    //     metallic F0. Under the identical light the dielectric keeps
    //     ambient + diffuse while the metal loses the diffuse lobe, so off
    //     the highlight centre the metal quad must read clearly darker.
    //     Probe at (0.40, 0.35): inside the 2x2 m quad (which spans about
    //     x 0.31..0.69, y 0.21..0.79 at this camera) and ~20 px off the
    //     sub-pixel GGX spike at screen centre.
    RefShot metalShot;
    capture("metal", mat4(), metalShot);
    KS_CHECK(metalShot.ok);
    if (metalShot.ok) {
        KS_CHECK(metalShot.stats.drawn == 1);
        const unsigned char* m = pxAt(metalShot, 0.40f, 0.35f);
        const unsigned char* d = pxAt(smoothShot, 0.40f, 0.35f);
        const int mSum = int(m[0]) + int(m[1]) + int(m[2]);
        const int dSum = int(d[0]) + int(d[1]) + int(d[2]);
        std::printf("test_renderer: deferred metalness off-lobe BGRA metal=%u,%u,%u "
                    "dielectric=%u,%u,%u\n",
                    static_cast<unsigned>(m[0]), static_cast<unsigned>(m[1]),
                    static_cast<unsigned>(m[2]), static_cast<unsigned>(d[0]),
                    static_cast<unsigned>(d[1]), static_cast<unsigned>(d[2]));
        KS_CHECK(!isClearPixel(m));                 // probe really on the quad
        KS_CHECK(dSum > mSum + 60);                 // dielectric lit, metal darker
        KS_CHECK(int(d[2]) > int(m[2]) + 20);       // red: diffuse present vs gone
    }
    renderer.setDeferred(false);

    // (e3) Brief P2 — IBL A/B. Sun intensity 0 makes the frame ambient-only,
    //     so the two captures differ by exactly the ambient model: the legacy
    //     flat albedo * 0.25 is neutral grey, the split-sum IBL picks up the
    //     procedural sky's horizon (blue-dominant on the -z-facing plain
    //     quad, whose reflection vector R = (0,0,-1) sits on the horizon
    //     band). The metal quad then proves the specular lobe: with no sun
    //     and no diffuse, its only light source is the prefiltered sky, so
    //     IBL on must be clearly brighter than the flat 0.25 fallback.
    //     Runs both paths (forward + deferred) — the two shaders carry
    //     independent IBL branches.
    {
        DirectionalLight sunAmb = sunRef;
        sunAmb.intensity = 0.0f; // ambient only: the A/B isolates IBL
        renderer.setSun(sunAmb);
        aimAt(0, 3, -3, 0, 3, 0, vec3(0, 1, 0));

        auto iblPair = [&](const char* mesh, RefShot& off, RefShot& on) {
            renderer.setIblEnabled(false);
            capture(mesh, mat4(), off);
            renderer.setIblEnabled(true);
            capture(mesh, mat4(), on);
        };

        // Forward path: plain white quad + metal quad.
        RefShot fOff, fOn, fmOff, fmOn;
        iblPair("plain", fOff, fOn);
        iblPair("metal", fmOff, fmOn);
        KS_CHECK(fOff.ok && fOn.ok && fmOff.ok && fmOn.ok);
        if (fOff.ok && fOn.ok) {
            const unsigned char* o = centrePx(fOff);
            const unsigned char* n = centrePx(fOn);
            const int rOff = int(o[2]), bOff = int(o[0]); // BGRA
            const int rOn = int(n[2]), bOn = int(n[0]);
            std::printf("test_renderer: IBL forward plain off RB=%d,%d on RB=%d,%d\n",
                        rOff, bOff, rOn, bOn);
            KS_CHECK(!isClearPixel(o) && !isClearPixel(n)); // both on the quad
            KS_CHECK(std::abs(bOff - rOff) <= 8);  // flat ambient = neutral grey
            KS_CHECK(bOn > rOn + 8);               // sky horizon: blue dominant
        }
        if (fmOff.ok && fmOn.ok) {
            const unsigned char* o = pxAt(fmOff, 0.40f, 0.35f);
            const unsigned char* n = pxAt(fmOn, 0.40f, 0.35f);
            const int sOff = int(o[0]) + int(o[1]) + int(o[2]);
            const int sOn = int(n[0]) + int(n[1]) + int(n[2]);
            std::printf("test_renderer: IBL forward metal sum off=%d on=%d\n", sOff, sOn);
            KS_CHECK(!isClearPixel(o) && !isClearPixel(n));
            // Observed +45 (off 82 -> on 127): the prefiltered horizon sky is
            // clearly above the flat 0.25 fallback without demanding double.
            KS_CHECK(sOn > sOff + 30); // prefiltered sky >> flat 0.25 ambient
        }

        // Deferred path: same plain-quad pin through the GBuffer + the
        // deferred lighting shader's own IBL branch.
        renderer.setDeferred(true);
        RefShot dOff, dOn;
        iblPair("plain", dOff, dOn);
        KS_CHECK(dOff.ok && dOn.ok);
        if (dOff.ok && dOn.ok) {
            const unsigned char* o = centrePx(dOff);
            const unsigned char* n = centrePx(dOn);
            const int rOff = int(o[2]), bOff = int(o[0]);
            const int rOn = int(n[2]), bOn = int(n[0]);
            std::printf("test_renderer: IBL deferred plain off RB=%d,%d on RB=%d,%d\n",
                        rOff, bOff, rOn, bOn);
            KS_CHECK(!isClearPixel(o) && !isClearPixel(n));
            KS_CHECK(std::abs(bOff - rOff) <= 8);
            // Deferred goes through tonemap.frag's sRGB OETF, so the linear
            // blue-vs-red skew is gamma-compressed (observed 224 vs 231):
            // +4 still discriminates from the neutral off capture (diff 0).
            KS_CHECK(bOn > rOn + 4);
        }
        renderer.setDeferred(false);
        renderer.setIblEnabled(false);
        renderer.setSun(sunRef); // restore the lit sun for (f)
    }

    // (e4) Brief P3 — clear-coat A/B. "coated" shares "rough"'s geometry,
    //     albedo (skin_paint) and base roughness 0.95: the ONLY difference
    //     in materials.txt is the optional 6th cell (1 vs absent). Under
    //     the head-on sun both get the identical broad base lobe, so the
    //     centre delta is purely the coat's sharp 0.07 GGX spike, and a
    //     probe ~12° off the highlight — where a 0.07 lobe has already
    //     collapsed to nothing — must show both quads sitting on the same
    //     broad base. That is the P3 done-when: "sharp specular highlight
    //     distinct from the broader base reflection". Both paths: forward
    //     reads the flag from MaterialData, deferred decodes it from
    //     GBuffer RT1.w's sign bit.
    {
        aimAt(0, 3, -3, 0, 3, 0, vec3(0, 1, 0));
        auto coatSum = [](const unsigned char* p) {
            return int(p[0]) + int(p[1]) + int(p[2]); // BGRA readback
        };
        auto coatPair = [&](RefShot& base, RefShot& coat) {
            capture("rough", mat4(), base);
            capture("coated", mat4(), coat);
            KS_CHECK(base.ok && coat.ok);
            if (!base.ok || !coat.ok) return;
            KS_CHECK(base.stats.drawn == 1 && coat.stats.drawn == 1);
            const unsigned char* cCentre = centrePx(coat);
            const unsigned char* bCentre = centrePx(base);
            const unsigned char* cOff = pxAt(coat, 0.40f, 0.35f);
            const unsigned char* bOff = pxAt(base, 0.40f, 0.35f);
            const int cSum = coatSum(cCentre), bSum = coatSum(bCentre);
            const int cOffSum = coatSum(cOff), bOffSum = coatSum(bOff);
            std::printf("test_renderer: clear-coat centre base=%d coat=%d "
                        "off-probe base=%d coat=%d\n",
                        bSum, cSum, bOffSum, cOffSum);
            KS_CHECK(!isClearPixel(cCentre) && !isClearPixel(bCentre));
            KS_CHECK(!isClearPixel(cOff) && !isClearPixel(bOff));
            // Centre: the coat spike saturates the highlight (observed
            // margin ~+230 deferred / ~+370 forward), so +100 leaves
            // room for tonemap roll-off without going soft.
            KS_CHECK(cSum > bSum + 100);
            // Off-centre: coat lobe gone, base shared -> deltas are
            // rounding/AA only (observed ~0).
            KS_CHECK(cOffSum <= bOffSum + 12);
        };

        renderer.setDeferred(true); // flag rides GBuffer RT1.w's sign here
        RefShot dBase, dCoat;
        coatPair(dBase, dCoat);
        renderer.setDeferred(false);
        RefShot fBase, fCoat; // flag rides MaterialData.clearcoat here
        coatPair(fBase, fCoat);
    }

    // (e5) Brief P4 — exposure + ACES roll-off. Exposure is a display-pass
    //     multiplier (setExposure -> TonemapPC.exposure; the app wires
    //     KS_EXPOSURE to it) applied before the tone curve. At 3x the rough
    //     quad's broad highlight sits well above 1.0 linear in red/green:
    //     mode=0 (no curve) clips those channels to 255, while the default
    //     ACES shoulder (mode 2) rolls them off cleanly below clip — the
    //     P4 done-when ("speculars roll off cleanly"). Blue of the red
    //     paint stays under 1.0 linear even at 3x, so GREEN is the
    //     discriminating channel. TAA is off here, which also pins
    //     sharpenAmount = 0: none of the other deferred pins move.
    {
        aimAt(0, 3, -3, 0, 3, 0, vec3(0, 1, 0));
        renderer.setDeferred(true);
        RefShot e1, e3clip, e3aces;
        capture("rough", mat4(), e1);
        renderer.setExposure(3.0f);
        renderer.setTonemapMode(0); // no curve: highlight clips at white
        capture("rough", mat4(), e3clip);
        renderer.setTonemapMode(2); // ACES: shoulder rolls off below clip
        capture("rough", mat4(), e3aces);
        renderer.setExposure(1.0f); // restore the defaults for the rest
        renderer.setDeferred(false);
        renderer.setTonemapMode(2);
        KS_CHECK(e1.ok && e3clip.ok && e3aces.ok);
        if (e1.ok && e3clip.ok && e3aces.ok) {
            const unsigned char* p1 = centrePx(e1);
            const unsigned char* pClip = centrePx(e3clip);
            const unsigned char* pAces = centrePx(e3aces);
            auto g8 = [](const unsigned char* p) { return int(p[1]); }; // BGRA
            std::printf("test_renderer: P4 exposure rough centre G 1x=%d "
                        "3x-clip=%d 3x-aces=%d\n",
                        g8(p1), g8(pClip), g8(pAces));
            KS_CHECK(!isClearPixel(p1) && !isClearPixel(pClip) &&
                     !isClearPixel(pAces));
            KS_CHECK(g8(pClip) > g8(p1));  // exposure reaches the image
            KS_CHECK(g8(pAces) > g8(p1));  // ACES keeps the lift
            KS_CHECK(g8(pClip) == 255);    // the clip is real at 3x
            // Observed gap ~15 (255 vs ~240): ACES stays under clip.
            KS_CHECK(g8(pAces) < g8(pClip) - 8);
        }
    }

    // (e6) Brief P5 — track materials + AO. "asphalt" is a cool-grey quad
    //     whose materials.txt row is the first reference row to use cell 5
    //     (normal tex): the centre must read as desaturated asphalt grey
    //     (blue >= red, tiny spread — every paint quad sits at R >> B),
    //     which pins row -> normalScale 1 -> descriptor -> sampled normal
    //     on committed content. "asphalt_tilt" shares albedo and
    //     roughness; only its normal texel differs — (255,128,128) is a
    //     tangent-space +x normal, i.e. N _|_ sun — so NdotL collapses
    //     and the frame falls back to the ambient term: pixel-level proof
    //     the normal map really drives the lighting (a decorative bind
    //     would leave both quads identical). AO side of P5 is the SSAO
    //     default in SimulatorApp (deferred-only) — here SSAO stays off,
    //     the flat quads carry no creases for it anyway.
    {
        aimAt(0, 3, -3, 0, 3, 0, vec3(0, 1, 0));
        renderer.setDeferred(true);
        RefShot asph, tilt;
        capture("asphalt", mat4(), asph);
        capture("asphalt_tilt", mat4(), tilt);
        renderer.setDeferred(false);
        KS_CHECK(asph.ok && tilt.ok);
        if (asph.ok && tilt.ok) {
            const unsigned char* a = centrePx(asph);
            const unsigned char* t = centrePx(tilt);
            const int aSum = int(a[0]) + int(a[1]) + int(a[2]);
            const int tSum = int(t[0]) + int(t[1]) + int(t[2]);
            std::printf("test_renderer: P5 asphalt BGRA=%d,%d,%d tilt sum=%d\n",
                        a[0], a[1], a[2], tSum);
            KS_CHECK(!isClearPixel(a) && !isClearPixel(t));
            KS_CHECK(int(a[0]) >= int(a[2]));        // cool grey, not red paint
            KS_CHECK(std::abs(int(a[0]) - int(a[2])) <= 12);
            KS_CHECK(std::max<int>(a[0], std::max<int>(a[1], a[2])) >= 120);
            // Tilted texel kills the sun lobe (observed ~600 vs ~270).
            KS_CHECK(aSum > tSum + 40);
        }
    }

    // (f) Reference car bake: the loadMeshFromFile() call shape
    //     SimulationLoop::ensureCarVisual() now makes (materials.txt row +
    //     textureDir) — green paint on the -z face, authored [0..1000] window.
    ks::sim::MaterialCache carMaterials;
    const std::string refCar = KS_TEST_REFERENCE_CAR;
    const bool carHasMaterials = carMaterials.load(refCar + "/materials.txt");
    KS_CHECK(carHasMaterials);
    const ks::sim::MeshMaterial* carMaterial =
        carHasMaterials ? carMaterials.find("body") : nullptr;
    KS_CHECK(carMaterial != nullptr);
    if (carMaterial) {
        KS_CHECK(renderer.loadMeshFromFile("car_body", refCar + "/body.nmsh",
                                           carMaterial, refCar + "/textures"));
        KS_CHECK(renderer.cachedTextureCount() == 6); // track five + car_paint.dds
        aimAt(0, 0.3f, -4.5f, 0, 0.3f, 0, vec3(0, 1, 0));
        RefShot carShot;
        capture("car_body", mat4(), carShot);
        KS_CHECK(carShot.ok);
        if (carShot.ok) {
            KS_CHECK(coverageOf(carShot.px) > 0.12f);
            const unsigned char* c = centrePx(carShot);
            KS_CHECK(!isClearPixel(c));
            KS_CHECK(greenDominant(c));
            KS_CHECK(carShot.stats.drawn == 1);
        }
    }

    // (g) Screenshot regression artifact (roadmap 2.5): the textured frame
    //     written next to the build for eyeballing / CI triage (BGRA -> RGBA).
    {
        ks::image::RawImage img;
        img.resize(int(kW), int(kH));
        for (size_t i = 0; i + 3 < signShot.px.size(); i += 4) {
            img.rgba[i] = signShot.px[i + 2];
            img.rgba[i + 1] = signShot.px[i + 1];
            img.rgba[i + 2] = signShot.px[i];
            img.rgba[i + 3] = 255;
        }
        const std::string shotPath =
            std::string(KS_TEST_SHADER_DIR) + "/../test_renderer_reference.png";
        std::string pngErr;
        if (!ks::image::saveImageFile(shotPath, img, &pngErr))
            std::printf("test_renderer: png save failed: %s\n", pngErr.c_str());
        KS_CHECK(std::filesystem::exists(shotPath));
        std::printf("test_renderer: reference screenshot -> %s\n", shotPath.c_str());
    }

    // (h) Roadmap 2.5 — the useful message for every missing piece: no
    //     manifest, a manifest row without its .nmsh, a materials.txt row
    //     without its DDS, shaders that are not there. stderr is captured
    //     around each single call (fd-level swap; stderr is unbuffered, so
    //     fprintf lands in the file at once).
    const std::filesystem::path broken =
        std::filesystem::temp_directory_path() /
        ("ksengine_broken_" + std::to_string(GetCurrentProcessId()));
    std::error_code brokenEc;
    std::filesystem::remove_all(broken, brokenEc);

    { // directory with no manifest.txt at all
        StderrCapture cap;
        const int n = renderer.loadMeshesFromManifest((broken / "empty").string());
        const std::string log = cap.stop();
        KS_CHECK(n == 0);
        const bool said = log.find("no manifest.txt in") != std::string::npos;
        if (!said) std::printf("test_renderer: missing-manifest stderr:\n%s\n", log.c_str());
        KS_CHECK(said);
    }
    { // manifest.txt names a mesh whose .nmsh is missing
        const std::filesystem::path dir = broken / "ghost";
        std::filesystem::create_directories(dir);
        {
            std::ofstream m(dir / "manifest.txt");
            m << "ghost\n";
        }
        StderrCapture cap;
        const int n = renderer.loadMeshesFromManifest(dir.string());
        const std::string log = cap.stop();
        KS_CHECK(n == 0);
        bool said = log.find("cannot open mesh cache") != std::string::npos &&
                    log.find("ghost.nmsh") != std::string::npos;
        if (!said) std::printf("test_renderer: missing-mesh stderr:\n%s\n", log.c_str());
        KS_CHECK(said);
    }
    { // materials.txt rows pointing at textures that do not exist — albedo
        // and (brief P1) a roughness *map* cell, both must name themselves
        // on stderr and still load the mesh.
        const std::filesystem::path dir = broken / "notex";
        std::filesystem::create_directories(dir);
        {
            std::ofstream m(dir / "manifest.txt");
            m << "t\n";
            std::ofstream mat(dir / "materials.txt");
            mat << "t\tgone.dds\tgone_rough.dds\t0.00\t\n";
        }
        writeNmsh(dir / "t.nmsh", makeQuad(false));
        StderrCapture cap;
        const int n = renderer.loadMeshesFromManifest(dir.string());
        const std::string log = cap.stop();
        KS_CHECK(n == 1);
        bool said = log.find("cannot open texture") != std::string::npos &&
                    log.find("gone.dds") != std::string::npos &&
                    log.find("gone_rough.dds") != std::string::npos;
        if (!said) std::printf("test_renderer: missing-texture stderr:\n%s\n", log.c_str());
        KS_CHECK(said);
    }
    { // shader directory that does not exist — the last renderer call, so a
      // null module pair left behind cannot disturb anything that follows.
        StderrCapture cap;
        const bool pipes =
            renderer.loadPipelines(std::string(KS_TEST_SHADER_DIR) + "/no_such_dir");
        const std::string log = cap.stop();
        KS_CHECK(!pipes);
        const bool said = log.find("failed to load native_forward") != std::string::npos;
        if (!said) std::printf("test_renderer: missing-shader stderr:\n%s\n", log.c_str());
        KS_CHECK(said);
    }
    std::filesystem::remove_all(broken, brokenEc);

    renderer.shutdown();
    return KS_TEST_RESULT("test_renderer");
}
