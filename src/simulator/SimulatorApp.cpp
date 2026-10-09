#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include "simulator/SimulationLoop.h"
#include "simulator/CameraController.h"
#include "simulator/DashboardOverlay.h"
#include "simulator/TelemetryOverlay.h"
#include "simulator/GameMenuOverlay.h"
#include "simulator/InputManager.h"
#include "simulator/SetupGarage.h"
#include "simulator/NetworkManager.h"
#include "simulator/ShadowSystem.h"
#include "simulator/NativeRenderer.h"
// Roadmap 2.4/2.5 — KS_SCREENSHOT writes the readback with this Qt-free
// encoder (same PNG writer the material module uses).
#include "engine/material/PngCodec.h"
#include "engine/physics/VehicleSimulator.h"
#include "devices/DeviceManager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static const char* AC_PATH = "F:/SteamLibrary/steamapps/common/assettocorsa";
// Installed layout: SPIR-V lives in system/shaders next to ksim.exe
// (cmake/KsInstallLayout.cmake).
static const char* SHADER_DIR = "system/shaders";

static HINSTANCE g_hInstance = nullptr;
static HWND g_hWnd = nullptr;
static VkInstance g_vkInstance = VK_NULL_HANDLE;
static VkSurfaceKHR g_surface = VK_NULL_HANDLE;
static ks::sim::NativeRenderer* g_nativeRenderer = nullptr;
static ks::sim::CascadedShadowMap g_shadowMap;
static std::unique_ptr<ks::sim::SimulationLoop> g_simulation;

static bool g_throttle = false, g_brake = false;
static bool g_steerLeft = false, g_steerRight = false;
static bool g_handbrake = false;
static bool g_running = true;

// Roadmap 4.2 — headless no-Vulkan: KS_HEADLESS=1 skips the Win32 window,
// the Vulkan instance/surface/device/swapchain and the renderer entirely;
// SimulationLoop runs with a nullptr renderer (every path already null-checks
// it). KS_HEADLESS_SECONDS=N auto-exits after N seconds (CI smoke).
static bool g_headless = false;
static double g_headlessSeconds = 0.0;

// Roadmap 2.4/2.5 — KS_SCREENSHOT=<path.png> captures one presented frame
// KS_SCREENSHOT_DELAY frames in (default 60) and exits, so a windowed
// session leaves verifiable image evidence behind. Ignored when headless
// (there is no renderer to read back from).
static std::string g_shotPath;
static int g_shotDelay = 60;
static bool g_shotRequested = false;
static int g_shotFrame = 0;
static double g_headlessElapsed = 0.0;

static const wchar_t* WINDOW_CLASS = L"KsEditorSimWindow";

static bool createVulkanInstance() {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ksEditor Simulator";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "ksEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    // VK_EXT_swapchain_colorspace is an *instance* extension: enabling it is
    // what makes color spaces such as VK_COLOR_SPACE_HDR10_ST2084_EXT legal
    // in vkCreateSwapchainKHR (see NativeRenderer::createSwapChain, KS_HDR).
    // Appended only when the loader actually exposes it, so a machine
    // without it still gets the same instance it always did.
    std::vector<const char*> extNames{extensions[0], extensions[1]};
    {
        uint32_t count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> avail(count);
        if (count) vkEnumerateInstanceExtensionProperties(nullptr, &count, avail.data());
        for (const auto& e : avail) {
            if (std::strcmp(e.extensionName, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME) == 0) {
                extNames.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
                break;
            }
        }
    }

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extNames.size());
    createInfo.ppEnabledExtensionNames = extNames.data();

    if (vkCreateInstance(&createInfo, nullptr, &g_vkInstance) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan instance\n");
        return false;
    }
    return true;
}

static bool createWin32Surface() {
    VkWin32SurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = g_hInstance;
    createInfo.hwnd = g_hWnd;

    if (vkCreateWin32SurfaceKHR(g_vkInstance, &createInfo, nullptr, &g_surface) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan surface\n");
        return false;
    }
    return true;
}

static void pollInput() {
    if (!g_simulation || !g_simulation->isRunning()) return;
    auto* v = g_simulation->vehicle();
    if (!v) return;
    v->setThrottle(g_throttle ? 1.0 : 0.0);
    v->setBrake(g_brake ? 1.0 : 0.0);
    double steer = 0;
    if (g_steerLeft) steer -= 1.0;
    if (g_steerRight) steer += 1.0;
    v->setSteering(steer);
}

// The menu is the one inside SimulationLoop's NativeUiHub (roadmap 3.1): it
// used to be a second, never-rendered GameMenuOverlay, so the visible menu
// had no callbacks and the wired one was never drawn.

static void handleKeyDown(int vk) {
    if (!g_simulation) return;
    ks::sim::ui::NativeUiHub& hub = g_simulation->ui();
    if (hub.menu().isVisible()) {
        hub.menu().handleKeyPress(vk);
        return;
    }
    if (auto* sg = g_simulation->setupGarage(); sg && sg->isVisible()) {
        if (sg->handleKeyPress(vk)) return;
    }
    // F1 devices / F2 server browser / chat composer / Esc opens the menu.
    if (g_simulation->handleUiKey(vk)) return;

    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    // Roadmap 1.4 / P2.7: driving bindings come from the rebindable
    // KeyboardMapping (primary key + fixed arrow alternate). The exact key
    // that was pressed is forwarded so release stays 1:1 even when two
    // physical keys drive the same action.
    auto* im = g_simulation->inputManager();
    const ks::sim::KeyboardMapping kb = im ? im->keyboardMapping() : ks::sim::KeyboardMapping{};
    if (vk == kb.throttle || vk == ks::sim::KeyboardMapping::AltThrottle) {
        g_throttle = true;
        if (im) im->setKeyDown(vk);
        return;
    }
    if (vk == kb.brake || vk == ks::sim::KeyboardMapping::AltBrake) {
        g_brake = true;
        if (im) im->setKeyDown(vk);
        return;
    }
    if (vk == kb.steerLeft || vk == ks::sim::KeyboardMapping::AltSteerLeft) {
        g_steerLeft = true;
        if (im) im->setKeyDown(vk);
        return;
    }
    if (vk == kb.steerRight || vk == ks::sim::KeyboardMapping::AltSteerRight) {
        g_steerRight = true;
        if (im) im->setKeyDown(vk);
        return;
    }
    if (vk == kb.shiftUp) { if (im) im->setKeyDown(vk); return; }
    if (vk == kb.shiftDown) { if (im) im->setKeyDown(vk); return; }
    if (vk == kb.handbrake) { g_handbrake = true; return; }

    switch (vk) {
    case 'C': {
        if (!g_simulation->camera()) break;
        auto mode = g_simulation->camera()->mode();
        if (mode == ks::sim::CameraController::Mode::Cockpit)
            g_simulation->setCameraMode(ks::sim::CameraController::Mode::Chase);
        else
            g_simulation->setCameraMode(ks::sim::CameraController::Mode::Cockpit);
        break;
    }
    case 'R': g_simulation->reset(); break;
    case 'H':
        if (auto* d = g_simulation->dashboard()) d->setVisible(!d->isVisible());
        break;
    case 'G':
        if (auto* sg = g_simulation->setupGarage()) {
            sg->toggleVisible();
            if (sg->isVisible() && g_simulation->dashboard())
                g_simulation->dashboard()->setVisible(false);
        }
        break;
    case 'T':
        if (g_simulation->telemetry()) g_simulation->telemetry()->toggleVisible();
        break;
    case VK_F5:
        if (shift) { g_simulation->stop(); printf("Stopped.\n"); }
        else       { g_simulation->start(); printf("Driving started!\n"); }
        break;
    case VK_F11: {
        DWORD style = GetWindowLongW(g_hWnd, GWL_STYLE);
        if (style & WS_OVERLAPPEDWINDOW) {
            SetWindowLongW(g_hWnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfoW(MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST), &mi);
            SetWindowPos(g_hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_FRAMECHANGED);
        } else {
            SetWindowLongW(g_hWnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
            SetWindowPos(g_hWnd, nullptr, 100, 100, 1920, 1080,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
        }
        break;
    }
    default: break;
    }
}

static void handleKeyUp(int vk) {
    // Always clear: swallowing the release while an overlay was open left
    // the driving key latched on once the overlay closed. Shift keys (E/Q)
    // were never released at all before, so keyboard shifting died after the
    // first shift until the next reset().
    auto* im = g_simulation ? g_simulation->inputManager() : nullptr;
    const ks::sim::KeyboardMapping kb = im ? im->keyboardMapping() : ks::sim::KeyboardMapping{};
    if (vk == kb.throttle || vk == ks::sim::KeyboardMapping::AltThrottle) {
        g_throttle = false;
        if (im) im->setKeyUp(vk);
        return;
    }
    if (vk == kb.brake || vk == ks::sim::KeyboardMapping::AltBrake) {
        g_brake = false;
        if (im) im->setKeyUp(vk);
        return;
    }
    if (vk == kb.steerLeft || vk == ks::sim::KeyboardMapping::AltSteerLeft) {
        g_steerLeft = false;
        if (im) im->setKeyUp(vk);
        return;
    }
    if (vk == kb.steerRight || vk == ks::sim::KeyboardMapping::AltSteerRight) {
        g_steerRight = false;
        if (im) im->setKeyUp(vk);
        return;
    }
    if (vk == kb.shiftUp || vk == kb.shiftDown) {
        if (im) im->setKeyUp(vk);
        return;
    }
    if (vk == kb.handbrake) g_handbrake = false;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        handleKeyDown((int)wParam);
        return 0;
    case WM_KEYUP:
        handleKeyUp((int)wParam);
        return 0;
    case WM_CHAR:
        // Translated character: the chat composer needs punctuation that
        // virtual-key codes do not carry (':' '.' ...).
        if (g_simulation) g_simulation->handleUiChar((int)wParam);
        return 0;
    case WM_MOUSEMOVE:
        if (g_simulation)
            g_simulation->handleUiMouseMove(static_cast<float>((short)LOWORD(lParam)),
                                            static_cast<float>((short)HIWORD(lParam)));
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        if (g_simulation)
            g_simulation->handleUiMouseButton(ks::sim::ui::MouseButton::Left,
                                              msg == WM_LBUTTONDOWN,
                                              static_cast<float>((short)LOWORD(lParam)),
                                              static_cast<float>((short)HIWORD(lParam)));
        return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        if (g_simulation)
            g_simulation->handleUiMouseButton(ks::sim::ui::MouseButton::Right,
                                              msg == WM_RBUTTONDOWN,
                                              static_cast<float>((short)LOWORD(lParam)),
                                              static_cast<float>((short)HIWORD(lParam)));
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt{(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
        ScreenToClient(hWnd, &pt);
        if (g_simulation)
            g_simulation->handleUiMouseWheel(static_cast<float>((short)HIWORD(wParam)),
                                             static_cast<float>(pt.x),
                                             static_cast<float>(pt.y));
        return 0;
    }
    case WM_SIZE: {
        if (g_nativeRenderer && g_nativeRenderer->isInitialized() && g_surface) {
            int w = LOWORD(lParam), h = HIWORD(lParam);
            if (w > 0 && h > 0) g_nativeRenderer->createSwapChain(g_surface, w, h);
            if (g_simulation && g_simulation->camera() && h > 0)
                g_simulation->camera()->setAspectRatio(float(w) / float(h));
        }
        return 0;
    }
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void initWindow() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS;
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(0, WINDOW_CLASS, L"ksEditor Simulator",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1920, 1080,
                             nullptr, nullptr, g_hInstance, nullptr);
    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);
}

static void initSimulation();

static void initVulkanAndSimulation() {
    if (g_headless) {
        printf("[INIT] KS_HEADLESS=1 -> no window surface, no Vulkan, renderer = nullptr\n");
        initSimulation();
        return;
    }
    printf("[INIT] Creating Vulkan instance...\n");
    if (!createVulkanInstance()) { printf("[INIT] FAILED: Vulkan instance\n"); return; }
    printf("[INIT] Creating Win32 surface...\n");
    if (!createWin32Surface()) { printf("[INIT] FAILED: Win32 surface\n"); return; }

    printf("[INIT] Creating NativeRenderer...\n");
    g_nativeRenderer = new ks::sim::NativeRenderer();

    // Rendering-path and display settings are parsed *before* the swapchain
    // is created, because KS_HDR picks the surface format and needs the
    // deferred path to exist (the forward path writes the backbuffer directly
    // and has no display pass to encode into). The deferred GBuffer + TAA
    // pipelines are fully built by NativeRenderer but unreachable unless
    // something calls setDeferred/setTaa, so they could rot without anyone
    // noticing — KS_RENDER=deferred|taa makes them opt-in without changing
    // the default forward image.
    if (const char* mode = std::getenv("KS_RENDER")) {
        if (std::strcmp(mode, "deferred") == 0) {
            g_nativeRenderer->setDeferred(true);
            printf("[INIT] Render path: deferred\n");
        } else if (std::strcmp(mode, "taa") == 0) {
            g_nativeRenderer->setDeferred(true);
            g_nativeRenderer->setTaa(true);
            printf("[INIT] Render path: deferred + TAA\n");
        }
    }
    // HDR10 output: 10-bit A2B10G10R10 swapchain in
    // VK_COLOR_SPACE_HDR10_ST2084_EXT, PQ/Rec.2020-encoded by the tonemap
    // display pass. Silently falls back to the SDR swapchain (with a logged
    // reason) when the driver or surface does not offer that mode.
    if (const char* hdr = std::getenv("KS_HDR")) {
        if (std::strcmp(hdr, "0") != 0) {
            g_nativeRenderer->setHdrOutput(true);
            g_nativeRenderer->setDeferred(true);   // HDR needs the display pass
            printf("[INIT] HDR10 output: requested (PQ/Rec.2020, SDR fallback logged if unsupported)\n");
        }
    }
    if (const char* exp = std::getenv("KS_EXPOSURE")) {
        const float e = std::strtof(exp, nullptr);
        if (e > 0.0f) {
            g_nativeRenderer->setExposure(e);
            printf("[INIT] Display exposure: %.3f\n", static_cast<double>(e));
        }
    }
    if (const char* wn = std::getenv("KS_HDR_WHITE_NITS")) {
        const float n = std::strtof(wn, nullptr);
        if (n > 0.0f) {
            g_nativeRenderer->setHdrWhiteNits(n);
            printf("[INIT] HDR10 reference white: %.0f nits\n", static_cast<double>(n));
        }
    }
    if (const char* tm = std::getenv("KS_TONEMAP")) {
        int mode = -1;
        if (std::strcmp(tm, "none") == 0) mode = 0;
        else if (std::strcmp(tm, "reinhard") == 0) mode = 1;
        else if (std::strcmp(tm, "aces") == 0) mode = 2;
        else if (std::strcmp(tm, "uncharted2") == 0) mode = 3;
        else if (std::strcmp(tm, "filmic") == 0) mode = 4;
        if (mode >= 0) {
            g_nativeRenderer->setTonemapMode(mode);
            printf("[INIT] Tonemap: %s\n", tm);
        } else {
            printf("[INIT] KS_TONEMAP '%s' unknown (none|reinhard|aces|uncharted2|filmic), keeping default\n", tm);
        }
    }

    if (const char* ssao = std::getenv("KS_SSAO")) {
        if (std::strcmp(ssao, "0") != 0) {
            float radius = 0.5f, intensity = 1.0f;
            if (const char* r = std::getenv("KS_SSAO_RADIUS")) {
                const float v = std::strtof(r, nullptr);
                if (v > 0.0f) radius = v;
            }
            if (const char* i = std::getenv("KS_SSAO_INTENSITY")) {
                const float v = std::strtof(i, nullptr);
                if (v > 0.0f) intensity = v;
            }
            g_nativeRenderer->setSsao(true, radius, intensity);
            g_nativeRenderer->setDeferred(true);   // SSAO lives in the deferred pass
            printf("[INIT] SSAO: on (radius %.2f, intensity %.2f)\n",
                   static_cast<double>(radius), static_cast<double>(intensity));
        }
    }

    if (const char* ssr = std::getenv("KS_SSR")) {
        if (std::strcmp(ssr, "0") != 0) {
            float dist = 8.0f, intensity = 1.0f;
            if (const char* d = std::getenv("KS_SSR_DISTANCE")) {
                const float v = std::strtof(d, nullptr);
                if (v > 0.0f) dist = v;
            }
            if (const char* i = std::getenv("KS_SSR_INTENSITY")) {
                const float v = std::strtof(i, nullptr);
                if (v > 0.0f) intensity = v;
            }
            g_nativeRenderer->setSsr(true, dist, intensity);
            g_nativeRenderer->setDeferred(true);    // SSR lives in the deferred pass
            printf("[INIT] SSR: on (max distance %.1f, intensity %.2f)\n",
                   static_cast<double>(dist), static_cast<double>(intensity));
        }
    }
    if (const char* mb = std::getenv("KS_MOTIONBLUR")) {
        if (std::strcmp(mb, "0") != 0) {
            float strength = 1.0f;
            if (const char* s = std::getenv("KS_MOTIONBLUR_STRENGTH")) {
                const float v = std::strtof(s, nullptr);
                if (v > 0.0f) strength = v;
            }
            g_nativeRenderer->setMotionBlur(true, strength);
            g_nativeRenderer->setDeferred(true);    // resolve pass only exists deferred
            printf("[INIT] Motion blur: on (strength %.2f)\n", static_cast<double>(strength));
        }
    }

    // Brief P2 — image-based lighting is on by default in the windowed app
    // (the sky/horizon fill on paint and track); KS_IBL=0 opts out and the
    // shaders fall back to the exact legacy flat ambient. Works on both the
    // forward and deferred paths, so no setDeferred() here.
    {
        const char* ibl = std::getenv("KS_IBL");
        const bool iblOn = !ibl || std::strcmp(ibl, "0") != 0;
        g_nativeRenderer->setIblEnabled(iblOn);
        if (!iblOn) printf("[INIT] IBL: off (KS_IBL=0)\n");
    }

    printf("[INIT] Creating Vulkan device...\n");
    if (g_nativeRenderer->createDevice(g_vkInstance, g_surface)) {
        RECT rc;
        GetClientRect(g_hWnd, &rc);
        printf("[INIT] Creating swap chain %dx%d...\n", rc.right - rc.left, rc.bottom - rc.top);
        g_nativeRenderer->createSwapChain(g_surface, rc.right - rc.left, rc.bottom - rc.top);
        printf("[INIT] Vulkan device + swap chain OK\n");

        if (!g_nativeRenderer->loadPipelines(SHADER_DIR))
            printf("[INIT] Pipeline load FAILED (missing %s shaders?)\n", SHADER_DIR);

        // Culling is on by default; KS_FRUSTUM_CULL=0 turns it off so its
        // effect can be A/B compared from the frame stats it reports.
        if (const char* cull = std::getenv("KS_FRUSTUM_CULL")) {
            if (std::strcmp(cull, "0") == 0) {
                g_nativeRenderer->setFrustumCulling(false);
                printf("[INIT] Frustum culling: off\n");
            }
        }
        // Same for previous-frame occlusion culling: KS_OCCLUSION_CULL=0
        // keeps the depth grid from being built at all, so an image or stats
        // difference between the two runs is attributable to this test alone.
        if (const char* occ = std::getenv("KS_OCCLUSION_CULL")) {
            if (std::strcmp(occ, "0") == 0) {
                g_nativeRenderer->setOcclusionCulling(false);
                printf("[INIT] Occlusion culling: off\n");
            }
        }

        if (g_shadowMap.initialize(g_nativeRenderer->physicalDevice(), g_nativeRenderer->device(),
                                   g_nativeRenderer->commandPool(), g_nativeRenderer->graphicsQueue(),
                                   SHADER_DIR)) {
            g_nativeRenderer->attachShadowMap(&g_shadowMap);
            printf("[INIT] Shadow map OK\n");
        } else {
            printf("[INIT] Shadow map init FAILED - continuing unshadowed\n");
        }
    } else {
        printf("[INIT] Vulkan device creation FAILED\n");
        delete g_nativeRenderer;
        g_nativeRenderer = nullptr;
        return;
    }

    initSimulation();
}

// Shared tail of init: SimulationLoop + menu + AI grid + autostart. Runs in
// both modes; g_nativeRenderer is nullptr when headless.
static void initSimulation() {
    g_simulation = std::make_unique<ks::sim::SimulationLoop>();
    g_simulation->setVulkanRenderer(g_nativeRenderer);
    g_simulation->initialize();
    // Discovery + external control API (port 20780). The FeatureHub callbacks
    // are wired inside startFeatureServices(), see SimulationLoop.cpp.
    g_simulation->startFeatureServices(false);

    if (!g_headless) {
    const int bakedMeshes = g_simulation->loadBakedScene("content/baked");
    printf("[INIT] Loaded %d baked mesh(es) into %zu scene entit%s\n", bakedMeshes,
           g_simulation->scene().alive(), g_simulation->scene().alive() == 1 ? "y" : "ies");
    // Roadmap 1.2: a car visual exists from the first frame (solid box
    // placeholder until SELECT CAR loads baked meshes, if any).
    g_simulation->ensureCarVisual(std::string());
    // KS_CAR=<dir>: start with a real car instead of the placeholder box —
    // the committed reference bake content/cars/refcar turns this into the
    // scriptable way to photograph a cached car (roadmap 2.4). Same loadCar()
    // path SELECT CAR uses; headless mode never reaches this block.
    if (const char* car = std::getenv("KS_CAR"); car && car[0]) {
        if (!g_simulation->loadCar(car))
            printf("[INIT] KS_CAR load failed: %s\n", car);
    }
    // KS_TRACK=<dir>: pick a track before the first frame (same path SELECT
    // TRACK uses; a folder without a bake keeps the current scene, and the
    // ai/fast_lane.ai spline inside it feeds the grid).
    if (const char* track = std::getenv("KS_TRACK"); track && track[0]) {
        if (!g_simulation->loadTrackFolder(track))
            printf("[INIT] KS_TRACK load failed: %s\n", track);
    }
    // KS_TEAM=<dir>: load a team roster (team.ini) so drivers, race numbers,
    // liveries, garage boxes and grid slots come from it (roadmap 2.8).
    if (const char* team = std::getenv("KS_TEAM"); team && team[0]) {
        if (!g_simulation->loadTeam(team))
            printf("[INIT] KS_TEAM load failed: %s\n", team);
    }

    ks::sim::GameMenuOverlay* uiMenu = &g_simulation->ui().menu();
    uiMenu->setVisible(true);
    uiMenu->onExitRequested = []() {
        g_running = false;
        PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
    };
    uiMenu->onStartDrivingRequested = [](ks::sim::GameSessionMode mode) {
        g_simulation->ui().menu().setVisible(false);
        g_simulation->startSession(mode);
        printf("Session started: %s\n", ks::sim::sessionModeName(mode));
    };
    uiMenu->onTrackChosen = [](const std::string& dir) {
        if (g_simulation->isRunning()) g_simulation->stop();
        if (g_simulation->loadTrackFolder(dir))
            printf("Track loaded: %s\n", dir.c_str());
        else
            printf("Track load failed: %s\n", dir.c_str());
    };
    uiMenu->onCarChosen = [](const std::string& dir) {
        if (g_simulation->isRunning()) g_simulation->stop();
        if (g_simulation->loadCar(dir))
            printf("Car loaded: %s\n", dir.c_str());
        else
            printf("Car load failed: %s\n", dir.c_str());
    };
    // Roadmap 2.8: TEAM row -> load the roster (applies immediately; the next
    // beginRaceSession builds the field from it).
    uiMenu->onTeamChosen = [](const std::string& dir) {
        if (g_simulation->loadTeam(dir))
            printf("Team loaded: %s\n", dir.c_str());
        else
            printf("Team load failed: %s\n", dir.c_str());
    };
    // Roadmap 2.9: GARAGE lists the car's upgrade packages; cycling a row
    // selects the next level and re-applies physics/nodes/livery/sound.
    uiMenu->upgradeRowLabels = []() { return g_simulation->upgradeRowLabels(); };
    uiMenu->onUpgradeRowCycled = [](int row) { g_simulation->cycleUpgradeRow(row); };
    uiMenu->onToggleFullscreenRequested = []() {
        SendMessageW(g_hWnd, WM_KEYDOWN, VK_F11, 0);
    };
    uiMenu->onOpenSetupGarageRequested = []() {
        g_simulation->ui().menu().setVisible(false);
        if (auto* sg = g_simulation->setupGarage()) sg->setVisible(true);
    };
    uiMenu->onTextInputRequested = [](const std::string& field, const std::string&) {
        printf("Text input requested for %s\n", field.c_str());
    };
    uiMenu->onNationalityInputRequested = []() {
        printf("Nationality picker requested\n");
    };
    uiMenu->onLoadReplayRequested = []() {
        printf("Load replay: not implemented yet.\n");
    };
    // Roadmap 1.3: RESULTS renders the live standings as menu rows.
    uiMenu->onShowResultsRequested = []() {
        std::vector<ks::sim::ResultsRow> rows;
        for (const auto& s : g_simulation->raceSession().standings()) {
            ks::sim::ResultsRow r;
            r.position = std::to_string(s.position > 0 ? s.position
                                                       : static_cast<int>(rows.size()) + 1);
            r.driver = s.driverName.empty() ? s.carName : s.driverName;
            if (s.disqualified) r.detail = "DSQ";
            else if (s.finished) r.detail = "finished";
            else if (s.bestLapTime < 1e8f) {
                char buf[40];
                std::snprintf(buf, sizeof(buf), "best %.3f", s.bestLapTime);
                r.detail = buf;
            } else {
                r.detail = "lap " + std::to_string(s.currentLap);
            }
            rows.push_back(std::move(r));
        }
        g_simulation->ui().menu().showResults(std::move(rows));
    };
    uiMenu->onOpenSettingsPanelRequested = [](const std::string& panel) {
        if (panel == "keyboard") {
            g_simulation->ui().menu().setVisible(false);
            g_simulation->ui().keyRebind().setVisible(true);
            return;
        }
        printf("Settings panel: %s\n", panel.c_str());
    };
    uiMenu->onDevModeRequested = []() {
        printf("Dev mode request\n");
    };

    // --- Roadmap 1.4 / P2.7: keyboard bindings ---------------------------
    // Load the persisted mapping (Key=Value, same user/ pattern as user/pb)
    // and wire the rebind panel: every capture is applied immediately and
    // written back.
    if (auto* im = g_simulation->inputManager()) {
        ks::sim::KeyboardMapping kb;
        if (ks::sim::loadKeyboardMapping("user/keyboard.ini", kb))
            im->setKeyboardMapping(kb);
        g_simulation->ui().keyRebind().setMapping(im->keyboardMapping());
    }
    g_simulation->ui().keyRebind().onMappingChanged = [](const ks::sim::KeyboardMapping& m) {
        if (auto* im = g_simulation->inputManager()) im->setKeyboardMapping(m);
        if (ks::sim::saveKeyboardMapping("user/keyboard.ini", m))
            printf("Keyboard bindings saved (user/keyboard.ini)\n");
    };

    // --- Roadmap 3.1: menu <-> transport ---------------------------------
    uiMenu->onHostServerRequested = []() {
        auto* net = g_simulation->networkManager();
        if (!net) return;
        if (net->isHosting()) { printf("Already hosting.\n"); return; }
        std::string track = g_simulation->ui().menu().trackName();
        if (track.empty()) track = "Unknown";
        if (net->hostServer(40000, 8, "ksEditor Server", track)) {
            g_simulation->features().announceHost("ksEditor Server", track, 40000, 1, 8);
            g_simulation->startFeatureServices(true);
            g_simulation->ui().menu().setVisible(false);
            printf("Hosting on port 40000\n");
        } else {
            g_simulation->ui().chat().addLine("[local] server start failed on 40000");
        }
    };
    uiMenu->onOpenServerBrowserRequested = []() {
        if (!g_simulation) return;
        if (!g_simulation->features().discoveryStarted)
            g_simulation->startFeatureServices(false);
        g_simulation->features().discovery.queryLan();
        g_simulation->ui().menu().setVisible(false);
        g_simulation->ui().multiplayer().setVisible(true);
    };
    uiMenu->onDisconnectRequested = []() {
        auto* net = g_simulation->networkManager();
        if (!net) return;
        net->disconnectFromServer();
        net->stopServer();
        printf("Network session closed.\n");
    };

    // --- Roadmap 3.1: F2 browser + chat ----------------------------------
    g_simulation->ui().multiplayer().setServers({{"ksim local", "127.0.0.1", 0, 8, 0}});
    g_simulation->ui().multiplayer().onJoin = [](const std::string& address) {
        auto* net = g_simulation->networkManager();
        if (!net) return;
        std::string host = address;
        uint16_t port = 40000;
        const std::string::size_type colon = address.rfind(':');
        if (colon != std::string::npos) {
            port = static_cast<uint16_t>(std::atoi(address.c_str() + colon + 1));
            host = address.substr(0, colon);
        }
        if (net->joinServer(host, port, g_simulation->ui().menu().profile().name, "gte3")) {
            g_simulation->ui().multiplayer().setVisible(false);
            printf("Joining %s:%u\n", host.c_str(), port);
        } else {
            g_simulation->ui().chat().addLine("[local] join failed: " + host);
        }
    };
    g_simulation->ui().chat().onSend = [](const std::string& msg) {
        auto* net = g_simulation->networkManager();
        if (net && (net->isConnected() || net->isHosting())) net->sendChatMessage(msg);
        else g_simulation->ui().chat().addLine("[local] " + msg);
    };
    } // !g_headless (baked scene + menu)

    // AI grid size (roadmap 3.5): N AI cars spawn on the track's
    // ai/fast_lane.ai line when the race session starts. Default 0.
    if (const char* aiCars = std::getenv("KS_AI_CARS");
        aiCars && aiCars[0]) {
        g_simulation->setAiCarCount(std::atoi(aiCars));
        printf("[INIT] KS_AI_CARS=%s -> %d AI cars\n",
               aiCars, g_simulation->aiCarCount());
    }

    // KS_HEADLESS runs the session with no menu and no renderer at all; the
    // windowed KS_AUTOSTART=1 path skips the menu but still renders.
    if (g_headless) {
        printf("[INIT] KS_HEADLESS -> starting session (no menu, no renderer)\n");
        g_simulation->start();
    } else if (const char* autostart = std::getenv("KS_AUTOSTART");
        autostart && autostart[0] == '1') {
        printf("[INIT] KS_AUTOSTART=1 -> starting drive session, menu suppressed\n");
        // KS_SESSION=<mode> picks the session type (race = grid placement,
        // roadmap 2.8 evidence); without it bare start() keeps the practice
        // default exactly like before.
        if (const char* sess = std::getenv("KS_SESSION"); sess && sess[0]) {
            const ks::sim::GameSessionMode mode = ks::sim::modeFromMenuEntry(sess);
            printf("[INIT] KS_SESSION=%s -> %s\n", sess, ks::sim::sessionModeName(mode));
            g_simulation->startSession(mode); // startSession() calls start()
        } else {
            g_simulation->start();
        }
        g_simulation->ui().menu().setVisible(false);
    }
}

// Roadmap 2.4/2.5 — KS_SCREENSHOT writer: NativeRenderer readback rows are
// tightly packed BGRA (B8G8R8A8 swapchain, origin top-left); PngCodec wants
// RGBA with straight alpha. Every mismatch prints its reason, so a failed
// capture is never silent.
static bool saveScreenshotToFile(const std::string& path) {
    const std::vector<unsigned char>& px = g_nativeRenderer->screenshotPixels();
    const VkExtent2D ext = g_nativeRenderer->extent();
    if (ext.width == 0 || ext.height == 0 ||
        px.size() != size_t(ext.width) * size_t(ext.height) * 4) {
        printf("[shot] unexpected readback: %zu bytes for %ux%u\n",
               px.size(), ext.width, ext.height);
        return false;
    }
    ks::image::RawImage img;
    img.resize(int(ext.width), int(ext.height));
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
        img.rgba[i] = px[i + 2];
        img.rgba[i + 1] = px[i + 1];
        img.rgba[i + 2] = px[i];
        img.rgba[i + 3] = 255; // swapchain alpha is not meaningful
    }
    std::string err;
    if (!ks::image::saveImageFile(path, img, &err)) {
        printf("[shot] encode failed: %s\n", err.c_str());
        return false;
    }
    return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    g_hInstance = hInstance;

    // Unbuffered stdout: diagnostics must survive a hard crash (the headless
    // smoke relies on seeing the last [INIT] line when something faults).
    setvbuf(stdout, nullptr, _IONBF, 0);
    // Roadmap 4.2 — headless flags (see globals). KS_HEADLESS=1 boots the
    // session without a window or Vulkan; KS_HEADLESS_SECONDS=N makes it
    // exit on its own (used by the sim_headless_smoke CTest).
    if (const char* hl = std::getenv("KS_HEADLESS");
        hl && hl[0] && std::strcmp(hl, "0") != 0) {
        g_headless = true;
    }
    if (const char* hs = std::getenv("KS_HEADLESS_SECONDS")) {
        const double s = std::strtod(hs, nullptr);
        if (s > 0.0) g_headlessSeconds = s;
    }
    // Roadmap 2.4/2.5 — screenshot capture (see globals). Parsed regardless
    // of mode; the state machine below is gated on the renderer, so a
    // headless run with KS_SCREENSHOT set simply never fires.
    if (const char* ss = std::getenv("KS_SCREENSHOT"); ss && ss[0]) {
        g_shotPath = ss;
    }
    if (const char* sd = std::getenv("KS_SCREENSHOT_DELAY")) {
        const int d = std::atoi(sd);
        if (d >= 0) g_shotDelay = d;
    }

    printf("[MAIN] Starting ksEditor Simulator (Qt-free%s)...\n",
           g_headless ? ", headless" : "");

    if (!g_headless) initWindow();
    initVulkanAndSimulation();

    printf("ksEditor Simulator started (%s)\n",
           g_headless ? "headless, no window/Vulkan" : "Win32 + Vulkan, no Qt");

    LARGE_INTEGER freq, lastTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    while (g_running) {
        if (!g_headless) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { g_running = false; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!g_running) break;
        }

        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        double elapsed = (double)(currentTime.QuadPart - lastTime.QuadPart) / (double)freq.QuadPart;
        lastTime = currentTime;
        if (elapsed < 0.001) elapsed = 0.001;
        if (elapsed > 0.1) elapsed = 0.1;

        if (!g_headless) pollInput();

        // Always tick: SimulationLoop::tick() runs a render-only path while
        // the session is stopped and pauses physics while a modal overlay is
        // up, so gating here would keep the menu from ever being drawn.
        if (g_simulation) g_simulation->tick();

        // Roadmap 2.4/2.5 — KS_SCREENSHOT: request after the delay, save as
        // soon as endFrame() has produced the readback, then leave through
        // the same path the menu's EXIT uses (deterministic teardown).
        if (g_nativeRenderer && !g_shotPath.empty()) {
            if (!g_shotRequested) {
                if (++g_shotFrame > g_shotDelay) {
                    g_nativeRenderer->requestScreenshot();
                    g_shotRequested = true;
                }
            } else if (g_nativeRenderer->screenshotReady()) {
                if (saveScreenshotToFile(g_shotPath))
                    printf("[shot] saved %s (%ux%u)\n", g_shotPath.c_str(),
                           g_nativeRenderer->extent().width,
                           g_nativeRenderer->extent().height);
                else
                    printf("[shot] FAILED %s\n", g_shotPath.c_str());
                g_running = false;
                PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
            }
        }

        if (g_headless && g_headlessSeconds > 0.0) {
            g_headlessElapsed += elapsed;
            if (g_headlessElapsed >= g_headlessSeconds) {
                printf("[MAIN] KS_HEADLESS_SECONDS=%.1f reached -> exiting\n",
                       g_headlessSeconds);
                g_running = false;
            }
        }

        Sleep(1);
    }

    // Deterministic teardown: destroy the simulation (and its borrowed
    // singletons: Engine, SceneModule, ScriptModule, Input/Render) while the
    // function-local statics they point into are still alive. If these ran
    // as CRT static destructors instead, Engine::instance() (constructed
    // later than g_simulation, hence destroyed earlier) would already be
    // gone when ~SimulationLoop touched m_modules -> access violation.
    // Close the discovery socket + control API before the loop goes away.
    if (g_simulation) g_simulation->features().stopServices();
    g_simulation.reset();
    g_shadowMap.shutdown();
    delete g_nativeRenderer;
    g_nativeRenderer = nullptr;
    if (g_surface) vkDestroySurfaceKHR(g_vkInstance, g_surface, nullptr);
    if (g_vkInstance) vkDestroyInstance(g_vkInstance, nullptr);

    return 0;
}
