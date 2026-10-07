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
#include "engine/physics/VehicleSimulator.h"
#include "devices/DeviceManager.h"
#include <cstdio>
#include <memory>

static const char* AC_PATH = "F:/SteamLibrary/steamapps/common/assettocorsa";
static const char* SHADER_DIR = "shaders";

static HINSTANCE g_hInstance = nullptr;
static HWND g_hWnd = nullptr;
static VkInstance g_vkInstance = VK_NULL_HANDLE;
static VkSurfaceKHR g_surface = VK_NULL_HANDLE;
static ks::sim::NativeRenderer* g_nativeRenderer = nullptr;
static ks::sim::CascadedShadowMap g_shadowMap;
static std::unique_ptr<ks::sim::SimulationLoop> g_simulation;
static std::unique_ptr<ks::sim::GameMenuOverlay> g_menu;

static bool g_throttle = false, g_brake = false;
static bool g_steerLeft = false, g_steerRight = false;
static bool g_handbrake = false;
static bool g_running = true;

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

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;

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

static void handleKeyDown(int vk) {
    if (!g_simulation) return;
    if (g_menu && g_menu->isVisible()) {
        g_menu->handleKeyPress(vk);
        return;
    }
    if (auto* sg = g_simulation->setupGarage(); sg && sg->isVisible()) {
        if (sg->handleKeyPress(vk)) return;
    }

    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    switch (vk) {
    case VK_ESCAPE: if (g_menu) g_menu->toggleVisible(); break;
    case 'W': case VK_UP:
        g_throttle = true;
        if (g_simulation->inputManager()) g_simulation->inputManager()->setKeyDown('W');
        break;
    case 'S': case VK_DOWN:
        g_brake = true;
        if (g_simulation->inputManager()) g_simulation->inputManager()->setKeyDown('S');
        break;
    case 'A': case VK_LEFT:
        g_steerLeft = true;
        if (g_simulation->inputManager()) g_simulation->inputManager()->setKeyDown('A');
        break;
    case 'D': case VK_RIGHT:
        g_steerRight = true;
        if (g_simulation->inputManager()) g_simulation->inputManager()->setKeyDown('D');
        break;
    case VK_SPACE: g_handbrake = true; break;
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
    case 'E': if (g_simulation->inputManager()) g_simulation->inputManager()->setKeyDown('E'); break;
    case 'Q': if (g_simulation->inputManager()) g_simulation->inputManager()->setKeyDown('Q'); break;
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
    if (g_menu && g_menu->isVisible()) return;
    switch (vk) {
    case 'W': case VK_UP:
        g_throttle = false;
        if (g_simulation && g_simulation->inputManager()) g_simulation->inputManager()->setKeyUp('W');
        break;
    case 'S': case VK_DOWN:
        g_brake = false;
        if (g_simulation && g_simulation->inputManager()) g_simulation->inputManager()->setKeyUp('S');
        break;
    case 'A': case VK_LEFT:
        g_steerLeft = false;
        if (g_simulation && g_simulation->inputManager()) g_simulation->inputManager()->setKeyUp('A');
        break;
    case 'D': case VK_RIGHT:
        g_steerRight = false;
        if (g_simulation && g_simulation->inputManager()) g_simulation->inputManager()->setKeyUp('D');
        break;
    case VK_SPACE: g_handbrake = false; break;
    default: break;
    }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        handleKeyDown((int)wParam);
        return 0;
    case WM_KEYUP:
        handleKeyUp((int)wParam);
        return 0;
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

static void initVulkanAndSimulation() {
    printf("[INIT] Creating Vulkan instance...\n");
    if (!createVulkanInstance()) { printf("[INIT] FAILED: Vulkan instance\n"); return; }
    printf("[INIT] Creating Win32 surface...\n");
    if (!createWin32Surface()) { printf("[INIT] FAILED: Win32 surface\n"); return; }

    printf("[INIT] Creating NativeRenderer...\n");
    g_nativeRenderer = new ks::sim::NativeRenderer();
    printf("[INIT] Creating Vulkan device...\n");
    if (g_nativeRenderer->createDevice(g_vkInstance, g_surface)) {
        RECT rc;
        GetClientRect(g_hWnd, &rc);
        printf("[INIT] Creating swap chain %dx%d...\n", rc.right - rc.left, rc.bottom - rc.top);
        g_nativeRenderer->createSwapChain(g_surface, rc.right - rc.left, rc.bottom - rc.top);
        printf("[INIT] Vulkan device + swap chain OK\n");

        if (!g_nativeRenderer->loadPipelines(SHADER_DIR))
            printf("[INIT] Pipeline load FAILED (missing %s shaders?)\n", SHADER_DIR);

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

    g_simulation = std::make_unique<ks::sim::SimulationLoop>();
    g_simulation->setVulkanRenderer(g_nativeRenderer);
    g_simulation->initialize();
    g_simulation->startFeatureServices(false);

    const int bakedMeshes = g_simulation->loadBakedScene("content/baked");
    printf("[INIT] Loaded %d baked mesh(es) into %zu scene entit%s\n", bakedMeshes,
           g_simulation->scene().alive(), g_simulation->scene().alive() == 1 ? "y" : "ies");

    g_menu = std::make_unique<ks::sim::GameMenuOverlay>();
    g_menu->onExitRequested = []() {
        g_running = false;
        PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
    };
    g_menu->onStartDrivingRequested = []() {
        if (g_simulation) {
            g_simulation->beginSession(ks::sim::GameSessionMode::Race);
            g_simulation->start();
        }
        if (g_menu) g_menu->setVisible(false);
        printf("Driving started!\n");
    };
    g_menu->onStartSessionRequested = [](const std::string& modeLabel) {
        if (!g_simulation) return;
        auto mode = ks::sim::modeFromMenuEntry(modeLabel);
        g_simulation->beginSession(mode);
        g_simulation->start();
        if (g_menu) g_menu->setVisible(false);
        printf("Session started: %s\n", ks::sim::sessionModeName(mode));
    };
    g_menu->onLoadTrackRequested = []() {
        printf("Load track: no in-game content browser yet.\n");
    };
    g_menu->onLoadCarRequested = []() {
        printf("Load car: no in-game content browser yet.\n");
    };
    g_menu->onToggleFullscreenRequested = []() {
        SendMessageW(g_hWnd, WM_KEYDOWN, VK_F11, 0);
    };
    g_menu->onOpenSetupGarageRequested = []() {
        g_menu->setVisible(false);
        if (auto* sg = g_simulation->setupGarage()) sg->setVisible(true);
    };
    g_menu->onTextInputRequested = [](const std::string& field, const std::string&) {
        printf("Text input requested for %s\n", field.c_str());
    };
    g_menu->onNationalityInputRequested = []() {
        printf("Nationality picker requested\n");
    };
    g_menu->onLoadReplayRequested = []() {
        if (!g_simulation) return;
        const char* path = std::getenv("KS_REPLAY_FILE");
        std::string p = path && path[0] ? path : "replays/last.ksreplay";
        if (g_simulation->loadReplayFile(p)) {
            if (g_menu) g_menu->setVisible(false);
            g_simulation->start();
            printf("Replay loaded: %s\n", p.c_str());
        } else {
            printf("Load replay failed: %s (set KS_REPLAY_FILE)\n", p.c_str());
        }
    };
    g_menu->onOpenContentBrowserRequested = [](const std::string& type) {
        printf("Content browser: %s\n", type.c_str());
    };
    g_menu->onOpenSettingsPanelRequested = [](const std::string& panel) {
        printf("Settings panel: %s\n", panel.c_str());
    };
    g_menu->onDevModeRequested = []() {
        printf("Dev mode request\n");
    };
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    g_hInstance = hInstance;
    printf("[MAIN] Starting ksEditor Simulator (Qt-free)...\n");

    initWindow();
    initVulkanAndSimulation();

    printf("ksEditor Simulator started (Win32 + Vulkan, no Qt)\n");

    LARGE_INTEGER freq, lastTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;

        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        double elapsed = (double)(currentTime.QuadPart - lastTime.QuadPart) / (double)freq.QuadPart;
        lastTime = currentTime;
        if (elapsed < 0.001) elapsed = 0.001;
        if (elapsed > 0.1) elapsed = 0.1;

        pollInput();

        if (g_simulation && g_simulation->isRunning() && g_menu && !g_menu->isVisible())
            g_simulation->tick();

        Sleep(1);
    }

    g_shadowMap.shutdown();
    delete g_nativeRenderer;
    g_nativeRenderer = nullptr;
    if (g_surface) vkDestroySurfaceKHR(g_vkInstance, g_surface, nullptr);
    if (g_vkInstance) vkDestroyInstance(g_vkInstance, nullptr);

    return 0;
}
