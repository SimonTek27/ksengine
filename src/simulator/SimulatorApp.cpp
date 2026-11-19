#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include "simulator/SimulationLoop.h"
#include "simulator/CameraController.h"
#include "simulator/AppPausePolicy.h"
#include "simulator/DashboardOverlay.h"
#include "simulator/TelemetryOverlay.h"
#include "simulator/GameMenuOverlay.h"
#include "simulator/ShowroomConfig.h"
#include "simulator/PitStrategyBridge.h" // roadmap 2.7: menu pit plan -> loop state
#include "simulator/InputManager.h"
#include "simulator/HotkeyMap.h"
#include "simulator/RenderModes.h"
#include "simulator/SetupGarage.h"
#include "simulator/NetworkManager.h"
#include "simulator/ShadowSystem.h"
#include "simulator/NativeRenderer.h"
#include "simulator/FrameDigest.h" // KS_VISUAL_DIGEST: the capture's signature
#include "simulator/TrackMaterialInventory.h" // KS_MATERIALS_REPORT: what the track is made of
#include "simulator/LocationMaterialDocument.h" // ... and where its document lives
#include "simulator/ModuleLoader.h" // native modules: the ABI and its loader
#include "simulator/SimulatorAudio.h" // setMasterVolume (settings.json audio)
// Phase 3 — per-player data folder + the two JSON settings files
// (user/<player>/stats.json lives here too).
#include "simulator/UserStats.h"
#include "simulator/DriverProfileStore.h" // first-run driver.json
// Roadmap 2.4/2.5 — KS_SCREENSHOT writes the readback with this Qt-free
// encoder (same PNG writer the material module uses).
#include "engine/material/PngCodec.h"
#include "engine/physics/PhysicsProfiler.h" // roadmap 2.14: exit dump (KS_PROFILE=1)
#include "engine/physics/VehicleSimulator.h"
#include "engine/Config/PPFilterPreset.h" // KS_PPFILTER preset
#include "engine/assets/Paths.h"         // system/ppfilters/<id>.json
#include "engine/Config/EngineSettings.h"
#include "engine/Engine.h"
#include "engine/input/WheelLedControl.h" // shift flash rate from wheelLeds.shiftFlashHz
#include "engine/archive/ContentUnpacking.h" // CONTENT: installing a package
#include "engine/sim/GameplayTweaks.h"    // gameplay.fuelConsumption -> the car's burn rate
#include "GameplayOptionsBridge.h"       // GAMEPLAY page -> tweak set + settings
#include "engine/assets/UserData.h"
#include "devices/DeviceManager.h"
#include "devices/vr/XrManager.h"
#include <cstdio>
#include <filesystem>
#include <utility>
#include <algorithm>
#include "engine/Config/Json.h"
#include <iterator>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "LocalShadowAtlas.h"

static const char* AC_PATH = "F:/SteamLibrary/steamapps/common/assettocorsa";
// Installed layout: SPIR-V lives in system/shaders next to ksim.exe
// (cmake/KsInstallLayout.cmake).
// Directory of ksim.exe: the build puts the SPIR-V in system/shaders next to
// it, whatever the process working directory happens to be.
static std::string exeDirectory() {
    char buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return std::string();
    std::string path(buffer, length);
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

// The shader set this build produced sits next to the exe, and a run from the
// repository root has to find that one: an older scratch copy picked up from
// the working directory shipped a vertex shader that mirrored the whole
// overlay, and nothing said which set had been loaded. KS_SHADER_DIR still
// wins for a harness that keeps them elsewhere.
static std::string shaderDirectory() {
    static const std::string resolved = []() -> std::string {
        if (const char* env = std::getenv("KS_SHADER_DIR"); env != nullptr && env[0] != '\0')
            return std::string(env);
        const std::string exe = exeDirectory();
        if (!exe.empty() &&
            std::ifstream(exe + "/system/shaders/native_forward.vert.spv", std::ios::binary).good())
            return exe + "/system/shaders";
        if (std::ifstream("system/shaders/native_forward.vert.spv", std::ios::binary).good())
            return std::string("system/shaders");
        // Nothing found: report the installed path, which is the one an
        // incomplete install is missing.
        return std::string("system/shaders");
    }();
    return resolved;
}

static HINSTANCE g_hInstance = nullptr;
static HWND g_hWnd = nullptr;
static VkInstance g_vkInstance = VK_NULL_HANDLE;
static VkSurfaceKHR g_surface = VK_NULL_HANDLE;
static ks::sim::NativeRenderer* g_nativeRenderer = nullptr;
static ks::sim::CascadedShadowMap g_shadowMap;
// Local-light shadow layers (one per spot lamp, six per point light), rendered
// with the same ksShadow pipeline and sampled by the deferred lighting pass.
static ks::sim::LocalShadowAtlas g_localShadowAtlas;
static std::unique_ptr<ks::sim::SimulationLoop> g_simulation;
// --track <dir> from WinMain: same setting as KS_TRACK, see initSimulation().
static std::string g_cliTrack;

// Phase 3 — the per-player data folder (user/<player>/, created on the first
// start) and the settings merged from system/cfg/ksengine.json (install
// defaults) + user/<player>/settings.json (per-player overrides). File
// scope because the teardown path writes both back after the frame loop is
// gone; see engine/Config/EngineSettings.h and engine/assets/UserData.h.
static ks::engine::assets::UserLayout g_userLayout;
// user/<player>/driver.json: the first-run profile and its completion flag.
static std::string g_driverProfilePath;
static ks::engine::config::EngineSettings g_engineSettings;

static bool g_throttle = false, g_brake = false;
static bool g_steerLeft = false, g_steerRight = false;
static bool g_consoleToggle = false; // CTRL+INS toggle state
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
// KS_VISUAL_DIGEST: the frame's signature, written next to the screenshot so a
// capture run leaves something a test can compare instead of an image nobody
// looks at.
static std::string g_visualDigestPath;
// Native modules, loaded once at start-up and unloaded at exit (see WinMain).
static ks::sim::ModuleLoader g_nativeModules;
static bool g_shotRequested = false;
static int g_shotFrame = 0;
static double g_headlessElapsed = 0.0;
static bool g_photoMode = false;
// Global hotkeys from system/cfg/controls.json (controls.hotkeys). The
// defaults in the struct are the keys the handler used to hardcode.
static ks::sim::HotkeyMap g_hotkeys;

// Roadmap 4.x — photo mode: free camera, HUD hidden, screenshot on key.
// scopes and the GPU markers read) and dumped on the way out; KS_PROFILE_CSV
// additionally writes it as CSV.
static double g_profileSeconds = 0.0;
static double g_profileElapsed = 0.0;

static const wchar_t* WINDOW_CLASS = L"KsEditorSimWindow";

/** File stems of the input profiles in `dir`, sorted: the picker lists what
 *  InputManager::saveProfile() actually wrote, not a guessed list. A missing
 *  folder (no profile saved yet) is an empty list, not an error. */
static std::vector<std::string> listInputProfiles(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".json") continue;
        names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

static bool createVulkanInstance(const std::vector<std::string>& extraExtensions = {}) {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "KinetiKa Simulator";
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

    uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    if (availableCount)
        vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());
    for (const auto& extension : extraExtensions) {
        const bool supported = std::any_of(available.begin(), available.end(),
            [&extension](const VkExtensionProperties& candidate) {
                return extension == candidate.extensionName;
            });
        if (!supported) {
            fprintf(stderr, "Vulkan extension required by OpenXR is unavailable: %s\n",
                    extension.c_str());
            return false;
        }
        if (std::find_if(extNames.begin(), extNames.end(), [&extension](const char* name) {
                return extension == name;
            }) == extNames.end())
            extNames.push_back(extension.c_str());
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
    // Roadmap 4.x — photo mode toggle. controls.hotkeys.photoMode names the
    // key (F12 by default); KS_PHOTOMODE=<key> overrides it for a run.
    if (vk == g_hotkeys.photoMode) {
        // The orbit of camera/FreeCamera.h, seeded where the driver's view was
        // so the frame photo mode opens on is the frame before it.
        g_simulation->setPhotoOrbit(true);
        // The car stops while the camera flies: the orbit's pan keys are W/S/A/D,
        // which would otherwise throttle and brake the car under the shot. The
        // exit path already resumed the session, so this is the half that was
        // missing.
        if (g_simulation->isRunning()) g_simulation->stop();
        g_simulation->ui().dashboard().setVisible(false);
        g_simulation->ui().menu().setVisible(false);
        g_photoMode = !g_photoMode; // toggle: first press enters, second exits
        printf("[photo] %sphoto mode (key %s)\n", g_photoMode ? "entered " : "exited ",
               ks::sim::keyName(g_hotkeys.photoMode).c_str());
        if (!g_photoMode) {
            g_simulation->ui().dashboard().setVisible(true);
            g_simulation->ui().menu().setVisible(true);
            g_simulation->setPhotoOrbit(false); // back to the view it had
            g_simulation->start(); // resume simulation
            // Disabilita DOF quando si esce dalla photo mode, tornando allo
            // stato "senza DOF" (il renderer mantiene l'ultimo setDepthOfField
            // chiamato; qui gli passiamo false per coerenza).
            g_nativeRenderer->setDepthOfField(false, 0, 0.0f, 0.0f, -1.0f, 0.0f, -1, -1, 0.0f, 0.0f);
        }
        return;
    }
    // Roadmap 4.x — developer console toggle (CTRL+INSERT).
    // VK_CONTROL = 0x12, VK_INSERT = 0x2D
    if (vk == 0x2D) { // INSERT key
        bool ctrlDown = (GetAsyncKeyState(0x12) & 0x8000) != 0;
        if (ctrlDown && !g_consoleToggle) {
            g_consoleToggle = true;
            g_simulation->ui().console().setVisible(true);
            g_simulation->ui().console().startTyping();
            printf("[console] opened (CTRL+INSERT)\n");
        } else if (!ctrlDown) {
            g_consoleToggle = false;
            g_simulation->ui().console().setVisible(false);
            printf("[console] closed (CTRL released)\n");
        }
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

    // Global hotkeys, read from controls.hotkeys in system/cfg/controls.json.
    // Driving bindings returned above, so a player who binds an action to one
    // of these keys keeps the action; an empty or unknown name in the document
    // leaves the historic key in place.
    if (vk == g_hotkeys.cameraCycle) {
        if (g_simulation->camera()) {
            auto mode = g_simulation->camera()->mode();
            if (mode == ks::sim::CameraController::Mode::Cockpit)
                g_simulation->setCameraMode(ks::sim::CameraController::Mode::Chase);
            else
                g_simulation->setCameraMode(ks::sim::CameraController::Mode::Cockpit);
        }
        return;
    }
    if (vk == g_hotkeys.resetSession) { g_simulation->reset(); return; }
    if (vk == g_hotkeys.dashboard) {
        if (auto* d = g_simulation->dashboard()) d->setVisible(!d->isVisible());
        return;
    }
    if (vk == g_hotkeys.telemetry) {
        if (g_simulation->telemetry()) g_simulation->telemetry()->toggleVisible();
        return;
    }
    if (vk == g_hotkeys.pit) {
        // Roadmap 2.7: smart garage/pit toggle. Held in the box -> roll out
        // (pit limiter); on track -> head back to the box, where stopping
        // inside arms the box service. Elsewhere it is a no-op.
        if (g_simulation->isHeldInGarage()) g_simulation->requestLeaveGarage();
        else g_simulation->requestReturnToBox();
        return;
    }
    if (vk == g_hotkeys.replayRecord) {
        const std::string saved = g_simulation->toggleReplayRecording();
        if (!saved.empty()) printf("Replay saved: %s\n", saved.c_str());
        return;
    }
    if (vk == g_hotkeys.replayPause) {
        if (g_simulation->replayPlaying()) {
            g_simulation->pauseReplayPlayback();
            printf("Replay paused.\n");
        } else if (g_simulation->replayLoaded()) {
            g_simulation->resumeReplayPlayback();
            printf("Replay resumed.\n");
        }
        return;
    }
    if (vk == g_hotkeys.fullscreen) {
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
            // Leaving fullscreen restores the mode of the monitor this window
            // is on, not a literal; SWP_NOSIZE keeps the size it already had.
            MONITORINFO restore = { sizeof(restore) };
            GetMonitorInfoW(MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST), &restore);
            SetWindowPos(g_hWnd, nullptr, 100, 100,
                         restore.rcMonitor.right - restore.rcMonitor.left,
                         restore.rcMonitor.bottom - restore.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
        }
        return;
    }

    switch (vk) {
    case 'G':
        if (auto* sg = g_simulation->setupGarage()) {
            sg->toggleVisible();
            if (sg->isVisible() && g_simulation->dashboard())
                g_simulation->dashboard()->setVisible(false);
        }
        break;
    case VK_F5:
        if (shift) { g_simulation->stop(); printf("Stopped.\n"); }
        else       { g_simulation->start(); printf("Driving started!\n"); }
        break;
    // GAP_MATRIX P0.5 — replay keys. ',' and '.' scrub by two seconds and
    // '-'/'=' change the playback speed; the HUD line already reports where
    // the playhead is. All of them are no-ops until a recording has been
    // loaded, so they never shadow a driving key. F6/F7 are hotkeys above.
    case VK_OEM_COMMA:
        if (g_simulation->replayLoaded()) g_simulation->seekReplayBy(-2.0f);
        break;
    case VK_OEM_PERIOD:
        if (g_simulation->replayLoaded()) g_simulation->seekReplayBy(2.0f);
        break;
    case VK_OEM_MINUS:
        if (g_simulation->replayLoaded())
            g_simulation->setReplaySpeed(g_simulation->replaySpeed() - 0.5f);
        break;
    case VK_OEM_PLUS:
        if (g_simulation->replayLoaded())
            g_simulation->setReplaySpeed(g_simulation->replaySpeed() + 0.5f);
        break;
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

// Losing focus with a session running pauses it, so the car does not keep
// driving behind another window (AppPausePolicy.h carries the rule). A network
// session keeps running, and a menu that is already up is already a pause.
// KS_PAUSE_ON_FOCUS_LOSS=0 turns it off, for a capture or an unattended run.
static void applyFocusPause(bool windowActive) {
    if (!g_simulation) return;
    ks::sim::FocusPauseInput in;
    if (const char* env = std::getenv("KS_PAUSE_ON_FOCUS_LOSS"); env && env[0] == '0')
        in.enabled = false;
    in.multiplayer = g_simulation->networkManager() != nullptr &&
                     g_simulation->networkManager()->isConnected();
    in.sessionRunning = g_simulation->isRunning();
    auto& menu = g_simulation->ui().menu();
    in.menuOpen = menu.isVisible();
    in.windowActive = windowActive;
    if (ks::sim::shouldPauseOnFocusLoss(in)) {
        menu.openPauseMenu();
        std::printf("[app] window lost focus: session paused\n");
        return;
    }
    if (windowActive && ks::sim::shouldResumeOnFocusGain(in)) menu.setVisible(false);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Focus is the one message the app answers before anything else: it is not
    // input for the car, it is whether the car should be running at all.
    if (msg == WM_ACTIVATEAPP) {
        applyFocusPause(wParam != 0);
        return 0;
    }
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
        if (g_simulation) {
            const float mx = static_cast<float>((short)LOWORD(lParam));
            const float my = static_cast<float>((short)HIWORD(lParam));
            // The UI sees the pointer first; whatever it does not consume is a
            // cockpit look (freelook), not a hover.
            if (!g_simulation->handleUiMouseMove(mx, my))
                g_simulation->handleLookMouseMove(mx, my);
        }
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

// The window starts at the display mode the system reports, not at a guess:
// a 1920x1080 literal upscales on a 4K panel and does not fit a 1366x768
// laptop. EnumDisplaySettingsW(ENUM_CURRENT_SETTINGS) is the mode the desktop
// is actually running; GetSystemMetrics covers the cases where that call comes
// back empty. The 1920x1080 last resort only fires when the OS reports no
// screen at all (headless session), and says so on stderr.
static void queryDesktopResolution(int& width, int& height) {
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) &&
        mode.dmPelsWidth > 0 && mode.dmPelsHeight > 0) {
        width = static_cast<int>(mode.dmPelsWidth);
        height = static_cast<int>(mode.dmPelsHeight);
    }
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "[INIT] display: the system reported no resolution, "
                        "falling back to 1920x1080\n");
        width = 1920;
        height = 1080;
    }
}

// The part of that mode a *framed* window can occupy: a window sized to the
// whole mode hangs its border off the screen. Still read from the system (the
// monitor's work area), just the part a window can actually use.
static void queryWindowWorkArea(int& width, int& height) {
    queryDesktopResolution(width, height);
    MONITORINFO info = { sizeof(info) };
    if (GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info)) {
        width = static_cast<int>(info.rcWork.right - info.rcWork.left);
        height = static_cast<int>(info.rcWork.bottom - info.rcWork.top);
    }
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

    int windowW = 0, windowH = 0;
    queryDesktopResolution(windowW, windowH);
    // The menu's first entry is that same mode, so "current resolution" in the
    // options page is what the monitor is running, not a constant.
    ks::sim::GameMenuOverlay::seedSystemResolution(windowW, windowH);
    printf("[INIT] Display: system mode %dx%d\n", windowW, windowH);

    int clientW = 0, clientH = 0;
    queryWindowWorkArea(clientW, clientH);
    RECT framed{0, 0, clientW, clientH};
    AdjustWindowRect(&framed, WS_OVERLAPPEDWINDOW, FALSE);
    g_hWnd = CreateWindowExW(0, WINDOW_CLASS, L"KinetiKa Simulator",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             framed.right - framed.left, framed.bottom - framed.top,
                             nullptr, nullptr, g_hInstance, nullptr);
    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);
}

static void initSimulation();

// The pointer image the theme names, handed to the window class so every state
// that does not set a cursor of its own wears it. Only .cur/.ani load this way:
// a PNG pointer would need its alpha mask built by hand, and a file the system
// refuses leaves the arrow and says so rather than changing nothing quietly.
static void installThemeCursor() {
    if (g_headless || g_hWnd == nullptr) return;
    const std::string& path = ks::sim::ui::NativeUiHub::themeCursorImage;
    if (path.empty()) return;
    HCURSOR cursor = static_cast<HCURSOR>(LoadImageA(nullptr, path.c_str(), IMAGE_CURSOR, 0, 0,
                                                     LR_LOADFROMFILE | LR_DEFAULTSIZE));
    if (cursor == nullptr) {
        fprintf(stderr, "[ui] cursor '%s' not loaded (only .cur/.ani): the arrow stays\n",
                path.c_str());
        return;
    }
    SetClassLongPtrA(g_hWnd, GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(cursor));
    SetCursor(cursor);
    printf("[ui] cursor installed from %s\n", path.c_str());
}

static void initVulkanAndSimulation() {
    if (g_headless) {
        printf("[INIT] KS_HEADLESS=1 -> no window surface, no Vulkan, renderer = nullptr\n");
        initSimulation();
        return;
    }
    auto* xrManager = ks::device::XrManager::instance();
    bool xrBootstrap = false;
    if (const char* vr = std::getenv("KS_VR"); vr && std::strcmp(vr, "0") != 0) {
        xrBootstrap = xrManager->prepareVulkanBootstrap();
        if (!xrBootstrap) {
            xrManager->shutdown();
            printf("[INIT] OpenXR unavailable; continuing in desktop mode\n");
        }
    }
    printf("[INIT] Creating Vulkan instance...\n");
    const std::vector<std::string> emptyExtensions;
    if (!createVulkanInstance(xrBootstrap ? xrManager->requiredVulkanInstanceExtensions()
                                          : emptyExtensions)) {
        if (!xrBootstrap || !createVulkanInstance()) {
            printf("[INIT] FAILED: Vulkan instance\n");
            return;
        }
        xrManager->shutdown();
        xrBootstrap = false;
        printf("[INIT] OpenXR Vulkan extensions unavailable; continuing in desktop mode\n");
    }
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
    // deferredRequested also gates the brief P5 SSAO default below: AO is
    // part of the deferred look, never of the default forward image.
    bool deferredRequested = false;
    if (const char* mode = std::getenv("KS_RENDER")) {
        if (std::strcmp(mode, "deferred") == 0) {
            g_nativeRenderer->setDeferred(true);
            deferredRequested = true;
            printf("[INIT] Render path: deferred\n");
        } else if (std::strcmp(mode, "taa") == 0) {
            g_nativeRenderer->setDeferred(true);
            g_nativeRenderer->setTaa(true);
            deferredRequested = true;
            printf("[INIT] Render path: deferred + TAA\n");
        } else if (std::strcmp(mode, "forward") == 0) {
            printf("[INIT] Render path: forward (no antialiasing)\n");
        }
    } else {
        // No explicit request: the default image is the antialiased one. The
        // forward path stays one env var away, and it is the jagged one.
        g_nativeRenderer->setDeferred(true);
        g_nativeRenderer->setTaa(true);
        deferredRequested = true;
        printf("[INIT] Render path: deferred + TAA (default)\n");
    }
    // HDR10 output: 10-bit A2B10G10R10 swapchain in
    // VK_COLOR_SPACE_HDR10_ST2084_EXT, PQ/Rec.2020-encoded by the tonemap
    // display pass. Silently falls back to the SDR swapchain (with a logged
    // reason) when the driver or surface does not offer that mode.
    if (const char* hdr = std::getenv("KS_HDR")) {
        if (std::strcmp(hdr, "0") != 0) {
            g_nativeRenderer->setHdrOutput(true);
            g_nativeRenderer->setDeferred(true);   // HDR needs the display pass
            deferredRequested = true;
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

    // Post-processing filter preset (system/ppfilters/<id>.json, schema
    // "ksengine.ppfilter/1", see engine/Config/PPFilterPreset.h). Opt-in
    // like the switches around it: nothing changes unless KS_PPFILTER names
    // a preset id ("normal") or a path, so the default image stays the one
    // the other knobs produce. The preset display block wins over the
    // KS_EXPOSURE/KS_TONEMAP values set above.
    // The shipped "normal" preset is the baseline look: without KS_PPFILTER the
    // image was whatever the raw knobs produced, which is the flat picture the
    // showroom kept showing. KS_PPFILTER=none turns the grading off again, and
    // naming another preset still picks that one.
    const char* pfName = std::getenv("KS_PPFILTER");
    if (pfName == nullptr || pfName[0] == 0) pfName = "normal";
    if (std::string(pfName) != "none") {
        std::string pfPath = pfName;
        const bool hasSuffix = pfPath.size() >= 5 &&
            pfPath.compare(pfPath.size() - 5, 5, ".json") == 0;
        if (!hasSuffix) {
            pfPath = ks::engine::assets::Paths::ppFiltersFile(pfPath + ".json");
        }
        ks::engine::config::PPFilterPreset preset;
        std::string pfError;
        if (ks::engine::config::PPFilterPreset::loadFile(pfPath, preset, &pfError)) {
            ks::engine::config::PPFilterPreset::instance() = preset;
            // Colour grading only where the preset declared the block: the
            // struct default (saturation 0.95) is a look, not a neutral, and
            // must not leak into presets that never named one.
            if (preset.colorGradingDeclared) {
                g_nativeRenderer->setSaturation(preset.colorGrading.saturation);
                g_nativeRenderer->setContrast(preset.colorGrading.contrast);
                g_nativeRenderer->setGradeHue(preset.colorGrading.hue);
                g_nativeRenderer->setGradeSepia(preset.colorGrading.sepia);
            }
            const float brightness = preset.colorGradingDeclared
                                         ? (preset.colorGrading.brightness > 0.0f
                                                ? preset.colorGrading.brightness
                                                : 1.0f)
                                         : 1.0f;
            // A showroom carries its own ppeffects.ini and AC applies it in the
            // room: the baseline ppfilter must not stomp the exposure there.
            const float roomExposure =
                g_simulation ? g_simulation->showroomExposure() : 0.0f;
            if (preset.display.exposure > 0.0f && roomExposure <= 0.0f) {
                // Brightness is a pre-curve linear gain: the same place the
                // display pass applies exposure, so the two multiply here
                // rather than double-encoding.
                g_nativeRenderer->setExposure(preset.display.exposure * brightness);
            }
            if (preset.display.tonemapMode >= 0 && preset.display.tonemapMode <= 4) {
                g_nativeRenderer->setTonemapMode(preset.display.tonemapMode);
            }
            if (preset.display.bloomStrength > 0.0f) {
                g_nativeRenderer->setBloomStrength(preset.display.bloomStrength);
            }
            // Depth of field: only activate when the preset explicitly declared
            // the depthOfField block (the struct default f/12 cannot invent bokeh
            // for a preset that never asked for it). The renderer's setDepthOfField()
            // maps the block's aperture/sensor/focal into the push-constant block
            // of tonemap.frag.
            if (preset.depthOfFieldDeclared) {
                g_nativeRenderer->setDepthOfField(
                    preset.depthOfField.enabled,
                    preset.depthOfField.quality,
                    preset.depthOfField.apertureFNumber,
                    preset.depthOfField.imageSensorHeight,
                    /*focalLength=*/-1.0f,   // renderer derives from camera FOV
                    preset.depthOfField.apertureParameter,
                    preset.depthOfField.apertureFrontLevels,
                    preset.depthOfField.apertureBackLevels,
                    /*meteringWidth=*/100.0f,   // full frame by default
                    /*meteringHeight=*/100.0f);
            }
            // Exposure, tone curve and the grading strip all live in the
            // display pass, which only the deferred path runs: a preset asks
            // for it the way KS_HDR does instead of silently doing nothing.
            g_nativeRenderer->setDeferred(true);
            if (preset.colorLut.enabled && !preset.colorLut.file.empty()) {
                const std::string lutPath = preset.colorLutPath(pfPath);
                if (!g_nativeRenderer->loadColorLut(lutPath, preset.colorLut.strength)) {
                    printf("[INIT] PP preset LUT not loaded: %s\n", lutPath.c_str());
                }
            }
            printf("[INIT] Post-processing preset '%s': exposure %.3f, tonemap %d, "
                   "saturation %.2f, contrast %.2f, bloom %.2f, lut %s\n",
                   preset.name.c_str(),
                   static_cast<double>(preset.display.exposure),
                   preset.display.tonemapMode,
                   static_cast<double>(g_nativeRenderer->saturation()),
                   static_cast<double>(g_nativeRenderer->contrast()),
                   static_cast<double>(g_nativeRenderer->bloomStrength()),
                   preset.colorLut.file.empty() ? "none" : preset.colorLut.file.c_str());
        } else {
            printf("[INIT] KS_PPFILTER \'%s\' not loaded: %s\n", pfName, pfError.c_str());
        }
    }

    // Brief P5 — contact-friendly AO. SSAO lives in the deferred lighting
    // pass, so it rides along by DEFAULT whenever deferred was already
    // requested (KS_RENDER=deferred|taa or KS_HDR): the default forward
    // image stays untouched, per the KS_RENDER comment above. KS_SSAO=0
    // opts out of the deferred default; an explicit KS_SSAO=1 still forces
    // the deferred path on by itself, exactly like before.
    {
        const char* ssaoEnv = std::getenv("KS_SSAO");
        const bool ssaoOff = ssaoEnv && std::strcmp(ssaoEnv, "0") == 0;
        const bool ssaoForced = ssaoEnv && !ssaoOff;
        if ((ssaoForced || deferredRequested) && !ssaoOff) {
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
            if (ssaoForced && !deferredRequested) {
                g_nativeRenderer->setDeferred(true); // SSAO lives in the deferred pass
            }
            printf("[INIT] SSAO: on (radius %.2f, intensity %.2f)%s\n",
                   static_cast<double>(radius), static_cast<double>(intensity),
                   ssaoForced ? "" : " - deferred default (KS_SSAO=0 opts out)");
        } else if (ssaoOff) {
            printf("[INIT] SSAO: off (KS_SSAO=0)\n");
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
    VkPhysicalDevice xrPhysicalDevice = xrBootstrap
        ? xrManager->vulkanGraphicsDevice(g_vkInstance) : VK_NULL_HANDLE;
    const std::vector<std::string> xrDeviceExtensions = xrPhysicalDevice != VK_NULL_HANDLE
        ? xrManager->requiredVulkanDeviceExtensions(xrPhysicalDevice)
        : std::vector<std::string>{};
    bool deviceCreated = g_nativeRenderer->createDevice(
        g_vkInstance, g_surface, xrPhysicalDevice, xrDeviceExtensions);
    if (!deviceCreated && xrBootstrap) {
        xrManager->shutdown();
        xrBootstrap = false;
        deviceCreated = g_nativeRenderer->createDevice(g_vkInstance, g_surface);
        printf("[INIT] OpenXR Vulkan device setup failed; continuing in desktop mode\n");
    }
    if (deviceCreated) {
        if (xrBootstrap) {
            xrManager->setVulkanDevice(g_nativeRenderer->device(),
                g_nativeRenderer->physicalDevice(), g_nativeRenderer->instance(),
                g_nativeRenderer->graphicsQueueFamily(), 0);
            if (!xrManager->initialize("KinetiKa Simulator VR")) {
                xrManager->shutdown();
                xrBootstrap = false;
                printf("[INIT] OpenXR session setup failed; continuing in desktop mode\n");
            } else {
                printf("[INIT] OpenXR session initialized (render-to-headset not yet enabled)\n");
            }
        }
        RECT rc;
        GetClientRect(g_hWnd, &rc);
        printf("[INIT] Creating swap chain %dx%d...\n", rc.right - rc.left, rc.bottom - rc.top);
        g_nativeRenderer->createSwapChain(g_surface, rc.right - rc.left, rc.bottom - rc.top);
        printf("[INIT] Vulkan device + swap chain OK\n");

        // One directory for every shader load below (main pass and shadows):
        // the installed layout or the source tree, decided once.
        const std::string shaderDir = shaderDirectory();
        printf("[INIT] Shader dir: %s\n", shaderDir.c_str());
        // The main pass is what a frame cannot be recorded without. Running on
        // with no pipeline drew nothing and then handed the driver null
        // handles (the windowed SIGSEGV), so stop here and say why.
        if (!g_nativeRenderer->loadPipelines(shaderDir.c_str())) {
            printf("[INIT] Pipeline load FAILED (missing %s shaders?)\n", shaderDir.c_str());
            // Nothing below can produce a frame, and this function returns void
            // (the window and its Vulkan objects are process-lifetime): leaving
            // here with a non-zero status beats a silent black window.
            std::exit(1);
        }

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
                                   shaderDir.c_str())) {
            g_nativeRenderer->attachShadowMap(&g_shadowMap);
            printf("[INIT] Shadow map OK\n");
        } else {
            printf("[INIT] Shadow map init FAILED - continuing unshadowed\n");
        }
        // KS_LIGHT_SHADOWS=0 keeps the local lights unshadowed, which is the
        // A/B baseline for the layered shadow cost.
        const char* localShadowsEnv = std::getenv("KS_LIGHT_SHADOWS");
        const bool localShadowsEnabled =
            localShadowsEnv == nullptr || std::strcmp(localShadowsEnv, "0") != 0;
        if (!localShadowsEnabled) {
            printf("[INIT] Local light shadows: off (KS_LIGHT_SHADOWS=0)\n");
        } else if (g_localShadowAtlas.initialize(g_nativeRenderer->physicalDevice(),
                                                g_nativeRenderer->device(), shaderDir.c_str())) {
            g_nativeRenderer->attachLocalShadowAtlas(&g_localShadowAtlas);
            printf("[INIT] Local light shadows OK (%d layers)\n", g_localShadowAtlas.layerCount());
        } else {
            printf("[INIT] Local light shadows FAILED - local lights stay unshadowed\n");
        }
    } else {
        printf("[INIT] Vulkan device creation FAILED\n");
        delete g_nativeRenderer;
        g_nativeRenderer = nullptr;
        return;
    }

    initSimulation();
}

// The cockpit knobs the driver left last session (TC, ABS, brake bias). All
// three go through the light setters the console uses, never applySetup(), so
// this is safe to run after a car has been loaded - and it is, from
// applyRuntimeSettings() below, which follows every loadCar(). A stored value
// outside what the car can do stays the car's business: the setters clamp, and
// a level on an aid the car declares absent still drops to zero.
static void applyCockpitSettings() {
    if (!g_simulation) return;
    ks::sim::SimulationLoop::CockpitKnobs knobs;
    knobs.hasTc = g_engineSettings.hasCockpitTcLevel;
    knobs.hasAbs = g_engineSettings.hasCockpitAbsLevel;
    knobs.hasBias = g_engineSettings.hasCockpitBrakeBiasPct;
    knobs.tcLevel = g_engineSettings.cockpitTcLevel;
    knobs.absLevel = g_engineSettings.cockpitAbsLevel;
    knobs.brakeBiasPct = g_engineSettings.cockpitBrakeBiasPct;
    // The loop applies them to the car in the garage now, and again at the end
    // of every loadCar() - the one point where the car's own electronics, setup
    // and upgrades are final, so a stored level is not wiped by the load.
    g_simulation->setCockpitKnobs(knobs);
    auto* vehicle = g_simulation->vehicle();
    if (vehicle && (knobs.hasTc || knobs.hasAbs || knobs.hasBias)) {
        // What the car actually took: a level on an aid it declares absent comes
        // back as zero, which is the answer worth printing.
        printf("[INIT] Cockpit: TC %d ABS %d BB %d%% (settings)\n", vehicle->tcLevel(),
               vehicle->absLevel(),
               static_cast<int>(vehicle->brakeBias() * 100.f + 0.5f));
    }
}

// --- settings.json -> engine -----------------------------------------------
// Fixed step + master audio: safe to push at any time (idempotent, and audio
// may not exist yet — SimulationLoop builds it lazily on the first car load,
// which is why this runs again after every loadCar()).
static void applyRuntimeSettings() {
    if (!g_simulation) return;
    ks::Engine::instance().setFixedDt(g_engineSettings.physicsFixedDt);
    if (auto* audio = g_simulation->audio()) {
        audio->setMasterVolume(g_engineSettings.audioMaster);
        audio->setPlayerCarVolume(g_engineSettings.audioPlayerCar);
        audio->setOpponentCarVolume(g_engineSettings.audioOpponentCar);
        audio->setLocationVolume(g_engineSettings.audioLocation);
        audio->setSpotterVolume(g_engineSettings.audioSpotter);
        audio->setMusicVolume(g_engineSettings.audioMusic);
        audio->setUiVolume(g_engineSettings.audioUi);
    }
    applyCockpitSettings(); // only once the car exists, so no-op before a load
}

// Assist levels can only reach physics through VehicleSimulator::applySetup(),
// which rebuilds mass/aero/suspension out of the setup — running it after a
// car's own data files were loaded would throw those values away, so this
// happens once at start-up (nothing else in ksim calls applySetup). An
// untouched settings.json asks for the vehicle's own defaults and is skipped
// outright, so the shipped "{}" changes nothing.
static void applyAssistSettings() {
    if (!g_simulation) return;
    auto* vehicle = g_simulation->vehicle();
    if (!vehicle) return;
    const auto& current = vehicle->setupParams();
    if (current.tcLevel == g_engineSettings.assistTc &&
        current.absLevel == g_engineSettings.assistAbs)
        return;
    auto p = current;
    p.tcLevel = g_engineSettings.assistTc;
    p.absLevel = g_engineSettings.assistAbs;
    vehicle->applySetup(p);
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

    // --- Phase 3: per-player folder (first run) + settings ---------------
    // The install ships user/ empty: user/<player>/ is created here, named
    // after the driver profile, with its three JSON files ({} until the
    // runtime writes keys) and the screenshots/replay/telemetry folders.
    g_userLayout = ks::engine::assets::UserData::ensure(
        g_simulation->ui().menu().profile().name);
    if (g_userLayout.created)
        printf("[INIT] Created %s/ (controls.json, settings.json, stats.json, "
               "screenshots/, replay/, telemetry/)\n",
               g_userLayout.player.c_str());
    else if (!g_userLayout.ok)
        printf("[INIT] User folder not writable: %s\n", g_userLayout.player.c_str());

    // Settings, in precedence order: install-wide defaults from
    // system/cfg/ksengine.json, then the player's own overrides. Presence is
    // cleared in between so the write-back at exit only ever reports keys the
    // *player's* file carried (a system default must not leak into it).
    g_engineSettings.load(ks::engine::assets::Paths::systemCfgFile("ksengine.json"));
    const bool sysFixedDt = g_engineSettings.hasPhysicsFixedDt;
    const bool sysAudio = g_engineSettings.hasAudioMaster;
    const bool sysAssist = g_engineSettings.hasAssistTc || g_engineSettings.hasAssistAbs;
    g_engineSettings.clearPresence();
    g_engineSettings.load(g_userLayout.settings);
    applyRuntimeSettings();
    applyAssistSettings();
    // The session's telemetry lands beside the player's other dumps: one CSV per
    // session, in user/<player>/telemetry (see SessionTelemetryCsv.h).
    g_simulation->setTelemetryCsvDirectory(g_userLayout.telemetry);
    // telemetry.csvHz: the row rate the CSV is sampled at (default 20 Hz).
    g_simulation->setTelemetryCsvRateHz(g_engineSettings.telemetryCsvHz);
    // The two in-race panels open where the driver left them last time.
    if (g_engineSettings.hasUiPanelOffsets) {
        g_simulation->ui().setOverlayOffsets(
            {static_cast<float>(g_engineSettings.uiHdashX),
             static_cast<float>(g_engineSettings.uiHdashY),
             static_cast<float>(g_engineSettings.uiInputsX),
             static_cast<float>(g_engineSettings.uiInputsY)});
    }
    if (g_simulation) {
        g_simulation->features().replay.setMaxSizeMb(g_engineSettings.replayMaxSizeMb);
        g_simulation->features().replay.setQualityLevel(g_engineSettings.replayQualityLevel);
    }
    // The wheel LED strip's shift flash: the rate lives in the settings files,
    // the module holds it, so the HUD and a wheel flash together.
    ks::engine::input::WheelLedControl::instance().setShiftFlashFrequency(
        g_engineSettings.wheelLedsShiftFlashHz);
    // Gameplay tweaks: the set keeps its compiled defaults and takes the fuel
    // burn multiplier from the settings. SimulationLoop applies it to the car
    // (and logs it), so the file is the only place this value is chosen.
    {
        auto& tweaks = ks::engine::sim::GameplayTweaks::instance();
        ks::engine::sim::GameplayTweakSet set = tweaks.getTweakSet();
        set.fuelConsumption = static_cast<float>(g_engineSettings.gameplayFuelConsumption);
        set.tireDamageRate = static_cast<float>(g_engineSettings.gameplayTireDamageRate);
        set.racingRulesStrict = g_engineSettings.gameplayRacingRulesStrict;
        // devMode: KS_FUEL_CONSUMPTION=<x> drains the tank at x times the car's
        // rate. A session that starts on a full tank never gets near the low-fuel
        // warning in a capture; x200 does it in about a minute, without touching
        // settings.json. The car keeps the multiplier, so it is a whole-session
        // choice, not a one-off splash.
        if (const char* envFuel = std::getenv("KS_FUEL_CONSUMPTION");
            envFuel != nullptr && envFuel[0] != '\0') {
            const float demoFuel = std::strtof(envFuel, nullptr);
            if (demoFuel > 0.0f && demoFuel <= 1000.0f) { // a NaN fails both
                set.fuelConsumption = demoFuel;
                std::printf("[TWEAKS] fuel consumption x%.1f (KS_FUEL_CONSUMPTION)\n",
                            static_cast<double>(demoFuel));
            }
        }
        tweaks.setTweakSet(set);
    }
    const auto where = [](bool user, bool system) -> const char* {
        return user ? " (settings.json)" : (system ? " (system/cfg)" : "");
    };
    printf("[INIT] Settings: fixedDt=%.6f%s audio=%.2f%s assist TC=%d ABS=%d%s ledFlash=%.2fHz%s\n",
           g_engineSettings.physicsFixedDt,
           where(g_engineSettings.hasPhysicsFixedDt, sysFixedDt),
           static_cast<double>(g_engineSettings.audioMaster),
           where(g_engineSettings.hasAudioMaster, sysAudio), g_engineSettings.assistTc,
           g_engineSettings.assistAbs,
           where(g_engineSettings.hasAssistTc || g_engineSettings.hasAssistAbs, sysAssist),
           g_engineSettings.wheelLedsShiftFlashHz,
           where(g_engineSettings.hasWheelLedsShiftFlashHz, false));

    // Career stats (wins/poles/podiums/races/best lap + PB record count)
    // seed the profile; they are written back on change and at exit.
    {
        ks::sim::DriverStats stats;
        if (ks::sim::loadDriverStats(g_userLayout.stats, stats))
            ks::sim::applyDriverStats(stats, g_simulation->ui().menu().profile());
    }

    // Who the driver is, and whether the first run is behind us: driver.json
    // carries both. A folder without it opens the wizard instead of a menu
    // under an unnamed driver. Never in a capture or an autostart run, where
    // the menu is suppressed anyway.
    g_driverProfilePath = g_userLayout.player + "/driver.json";
    {
        ks::sim::StoredDriverProfile stored;
        const bool loaded = ks::sim::loadDriverProfile(g_driverProfilePath, stored);
        if (loaded) {
            g_simulation->ui().menu().profile() = stored.profile;
            g_simulation->ui().menu().setSetupComplete(stored.setupComplete);
            printf("[INIT] Driver: %s (%s) #%d%s\n", stored.profile.name.c_str(),
                   stored.profile.nationality.c_str(), stored.profile.raceNumber,
                   stored.setupComplete ? ", setup complete" : ", setup incomplete");
        }
        const bool suppressMenu = std::getenv("KS_HIDE_MENU") != nullptr ||
                                 std::getenv("KS_AUTOSTART") != nullptr;
        if (!loaded || !stored.setupComplete) {
            if (!suppressMenu) g_simulation->ui().menu().openFirstRun();
            else printf("[INIT] First run skipped (menu suppressed)\n");
        }
    }

    // The content the environment points at, loaded through the same paths
    // SELECT CAR, SELECT TRACK and TEAM use. Headless reads them too: a run
    // without a window drives the car and the track it was pointed at instead
    // of an empty default scene, which is what a pit-box start needs to have
    // anything to place.
    auto folderName = [](const char* path) {
        std::string text = path ? path : "";
        while (!text.empty() && (text.back() == '/' || text.back() == '\\')) text.pop_back();
        const std::string::size_type cut = text.find_last_of("/\\");
        return cut == std::string::npos ? text : text.substr(cut + 1);
    };
    auto loadEnvContent = [&folderName]() {
        // KS_CAR=<dir>: start with a real car instead of the placeholder box —
        // the committed reference bake content/cars/refcar turns this into the
        // scriptable way to photograph a cached car (roadmap 2.4).
        if (const char* car = std::getenv("KS_CAR"); car && car[0]) {
            if (g_simulation->loadCar(car)) {
                applyRuntimeSettings(); // audio is created with the car
                // The menu names the content it is driving: loaded from here
                // it never went through the CARS page, and a session card
                // reading "NOT CHOSEN" beside a running car is a lie.
                g_simulation->ui().menu().setCarName(folderName(car));
                g_simulation->ui().menu().setCarDir(car);
            } else {
                printf("[INIT] KS_CAR load failed: %s\n", car);
            }
        }
        // KS_TRACK=<dir> or --track <dir>: pick a track before the first frame
        // (a folder without a bake keeps the current scene, and the
        // ai/fast_lane.ai spline inside it feeds the grid). The command line
        // wins over the environment, so a shortcut can override it.
        std::string startTrack = g_cliTrack;
        if (startTrack.empty()) {
            if (const char* track = std::getenv("KS_TRACK"); track && track[0])
                startTrack = track;
        }
        if (!startTrack.empty()) {
            if (g_simulation->loadTrackFolder(startTrack)) {
                printf("[INIT] Track loaded: %s\n", startTrack.c_str());
                g_simulation->ui().menu().setTrackName(folderName(startTrack.c_str()));
            } else {
                printf("[INIT] track load failed: %s\n", startTrack.c_str());
            }
        }
        // KS_TEAM=<dir>: load a team roster (team.ini) so drivers, race numbers,
        // liveries, garage boxes and grid slots come from it (roadmap 2.8).
        if (const char* team = std::getenv("KS_TEAM"); team && team[0]) {
            if (!g_simulation->loadTeam(team))
                printf("[INIT] KS_TEAM load failed: %s\n", team);
        }
    };

    if (!g_headless) {
    const int bakedMeshes = g_simulation->loadBakedScene("content/baked");
    printf("[INIT] Loaded %d baked mesh(es) into %zu scene entit%s\n", bakedMeshes,
           g_simulation->scene().alive(), g_simulation->scene().alive() == 1 ? "y" : "ies");
    // Roadmap 1.2: a car visual exists from the first frame (solid box
    // placeholder until SELECT CAR loads baked meshes, if any).
    g_simulation->ensureCarVisual(std::string());
    loadEnvContent();

    // The game always opens in a showroom: content/showrooms/<id>/showroom.json
    // describes it (KS_SHOWROOM picks one, otherwise the first folder that has
    // one). A checkout with no showroom keeps the plain startup scene instead
    // of failing - the menu still opens.
    {
        ks::sim::ShowroomConfig showroom;
        std::string showroomError;
        // showroom.default (CONTENT page) picks the room; KS_SHOWROOM still
        // overrides it, and an empty choice keeps the first one on disk.
        const std::string preferredRoom =
            g_engineSettings.hasShowroomDefault ? g_engineSettings.showroomDefault
                                                : std::string();
        if (!preferredRoom.empty())
            printf("[INIT] Showroom default from settings: %s\n", preferredRoom.c_str());
        // Same root the CONTENT page lists, so the mark on the page is the room
        // that opens (libraryRoot resolves the install prefix for a build-folder
        // launch, which a bare "content/showrooms" did not).
        if (ks::sim::loadStartupShowroom(showroom, &showroomError, preferredRoom,
                                         ks::sim::showroomLibraryRoot())) {
            g_simulation->openShowroom(showroom.directory);
            printf("[INIT] Showroom '%s' (%s) camera %.1f m, %.0f deg/s\n",
                   showroom.name.c_str(), showroom.id.c_str(),
                   static_cast<double>(showroom.cameraDistance),
                   static_cast<double>(showroom.cameraRotationDegPerSec));
            if (showroom.hasAmbience() || showroom.hasMusic())
                printf("[INIT] Showroom audio: ambience %s, music %s\n",
                       showroom.hasAmbience() ? showroom.ambience.formatName() : "none",
                       showroom.hasMusic() ? showroom.music.formatName() : "none");
            for (const auto& warning : showroom.warnings)
                printf("[INIT] Showroom warning: %s\n", warning.c_str());
        } else {
            printf("[INIT] No showroom: %s\n", showroomError.c_str());
        }
    }

    ks::sim::GameMenuOverlay* uiMenu = &g_simulation->ui().menu();
    // singleplayer.modes: the SINGLE PLAYER rows, in the order the config lists
    // them. The install file is read first and the player's own settings.json
    // after it, so a player wins exactly like the other settings. A file that
    // names none keeps the built-in eight, and a name the parser does not know
    // is reported instead of quietly becoming a race.
    {
        std::vector<ks::sim::GameSessionMode> modes;
        bool configured = false;
        for (const std::string& file :
             {ks::engine::assets::Paths::systemCfgFile("ksengine.json"),
              g_userLayout.settings}) {
            std::ifstream in(file, std::ios::binary);
            if (!in) continue;
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            ks::engine::json::Value root;
            std::string error;
            if (!ks::engine::json::parse(text, root, &error)) {
                printf("[INIT] %s: %s\n", file.c_str(), error.c_str());
                continue;
            }
            const ks::engine::json::Value* block = root.find("singleplayer");
            // singleplayer.defaults: what MODE SETUP starts from, field by field.
            if (block) {
                if (const ks::engine::json::Value* defs = block->find("defaults");
                    defs && defs->isObject()) {
                    for (const std::string& name : ks::sim::applySessionDefaults(*defs))
                        printf("[INIT] %s singleplayer.defaults: unknown mode '%s', ignored\n",
                               file.c_str(), name.c_str());
                }
            }
            const ks::engine::json::Value* rows = block ? block->find("modes") : nullptr;
            if (!rows || !rows->isArray() || rows->size() == 0) continue;
            std::vector<ks::sim::GameSessionMode> parsed;
            for (size_t i = 0; i < rows->size(); ++i) {
                const ks::engine::json::Value* entry = rows->item(i);
                const std::string name = entry ? entry->asString() : std::string();
                ks::sim::GameSessionMode mode;
                if (!entry || !entry->isString()) {
                    printf("[INIT] %s singleplayer.modes[%zu]: not a name, ignored\n",
                           file.c_str(), i);
                } else if (!ks::sim::tryModeFromMenuEntry(name, &mode)) {
                    printf("[INIT] %s singleplayer.modes: unknown mode '%s', ignored\n",
                           file.c_str(), name.c_str());
                } else {
                    parsed.push_back(mode);
                }
            }
            if (parsed.empty()) {
                printf("[INIT] %s singleplayer.modes: no usable mode, keeping the "
                       "built-in eight\n", file.c_str());
                continue;
            }
            modes = parsed;
            configured = true;
        }
        if (configured) {
            std::string listed;
            for (size_t i = 0; i < modes.size(); ++i) {
                if (i) listed += ", ";
                listed += ks::sim::sessionModeName(modes[i]);
            }
            uiMenu->setSingleplayerModes(modes);
            printf("[INIT] Single player modes: %s\n", listed.c_str());
        }
    }
    // Keys the runtime never reads. The file documents far more than it drives,
    // so a typo used to be silent; this names the ones nobody consumes.
    {
        const std::vector<std::pair<std::string, std::vector<std::string>>> known = {
            {"physics", {"fixedDt", "note"}},
            {"audio", {"master", "note", "playerCar", "opponentCar", "location",
             "spotter", "music", "ui", "sources"}},
            {"assist", {"tc", "abs", "note"}},
            {"telemetry", {"csvHz", "note"}},
            {"video",
             {"note", "resolution", "resolutions", "fullscreen", "vsync", "frameLimit", "ssao",
              "ssr", "ssgi", "ssgiRadius", "ssgiIntensity", "motionBlur", "exposure",
              "saturation", "contrast", "renderMode", "renderModes",
              "gbufferOverrides",
              "tonemap", "tonemaps", "postProcessFilter", "hdrOutput", "msaa",
              "anisotropicFiltering", "shadowMapSize", "shadowCascades",
              "mirrors", "mirrorResolution", "particles", "headPhysics", "sources", "note"}},
            {"singleplayer", {"modes", "note", "sources", "defaults"}},
        };
        std::vector<std::string> files = {
            ks::engine::assets::Paths::systemCfgFile("ksengine.json"),
            g_userLayout.settings};
        // A section may point at its own document ("video":
        // "system/cfg/video.json", "audio": "system/cfg/audio.json"): the file
        // it names is checked here too, so a typo in it is reported exactly
        // like one in ksengine.json.
        for (size_t i = 0; i < files.size(); ++i) {
            const std::string file = files[i];
            std::ifstream in(file, std::ios::binary);
            if (!in) continue;
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            ks::engine::json::Value root;
            std::string error;
            if (!ks::engine::json::parse(text, root, &error)) continue; // reported above
            for (const char* member : {"video", "audio", "controls"}) {
                const ks::engine::json::Value* section = root.find(member);
                if (section && section->isString() && !section->asString().empty() &&
                    std::find(files.begin(), files.end(), section->asString()) == files.end())
                    files.push_back(section->asString());
            }
            std::string unread;
            size_t unreadCount = 0;
            for (const auto& section : known) {
                const ks::engine::json::Value* block = root.find(section.first);
                if (!block || !block->isObject()) continue;
                for (const std::string& key : block->keys()) {
                    if (std::find(section.second.begin(), section.second.end(), key) !=
                        section.second.end())
                        continue;
                    if (unreadCount++ > 0) unread += ", ";
                    unread += section.first + "." + key;
                }
            }
            if (unreadCount > 0)
                printf("[INIT] %s: %zu key(s) the engine does not read: %s\n",
                       file.c_str(), unreadCount, unread.c_str());
        }
    }
    uiMenu->setVisible(true);
    uiMenu->onExitRequested = []() {
        g_running = false;
        PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
    };
    uiMenu->onStartDrivingRequested = [](ks::sim::GameSessionMode mode, const ks::sim::SessionStartParams& params) {
        g_simulation->ui().menu().setVisible(false);
        // The sky the WEATHER page chose is what the session runs with: the
        // loop's own WeatherControl holds it, and the physics and the lighting
        // read that.
        auto& weather = g_simulation->features().weatherCtrl;
        weather.weather() = ks::sim::presetFor(params.weather);
        weather.setTimeOfDay(params.weather.hourOfDay);
        printf("Weather: %s (%s) %.1f C, rain %.2f, wetness %.2f\n",
               ks::sim::weatherKindName(params.weather.kind),
               ks::sim::weatherSourceName(params.weather.source), params.weather.ambientC,
               params.weather.rain, params.weather.wetness);
        g_simulation->startSession(mode, params);
        // The header's clipboard opens the session planner from here on: it
        // has a session to describe, and a start that failed has nothing.
        g_simulation->ui().menu().setSessionActive(g_simulation->isRunning());
        printf("Session started: %s\n", ks::sim::sessionModeName(mode));
    };
    uiMenu->onPaintChosen = [](float r, float g, float b) {
        if (g_simulation) g_simulation->setCarPaint(r, g, b);
    };
    uiMenu->onTrackChosen = [](const std::string& dir) {
        if (g_simulation->isRunning()) g_simulation->stop();
        g_simulation->ui().menu().setSessionActive(false);
        if (g_simulation->loadTrackFolder(dir))
            printf("Track loaded: %s\n", dir.c_str());
        else
            printf("Track load failed: %s\n", dir.c_str());
    };
    uiMenu->onCarChosen = [](const std::string& dir) {
        if (g_simulation->isRunning()) g_simulation->stop();
        g_simulation->ui().menu().setSessionActive(false);
        if (g_simulation->loadCar(dir)) {
            applyRuntimeSettings(); // audio only exists from the first car on
            g_simulation->ui().menu().setCarDir(dir);
            printf("Car loaded: %s\n", dir.c_str());
        } else {
            printf("Car load failed: %s\n", dir.c_str());
        }
    };
    // The session planner changed the car or the track of a session that is
    // up: the content is already loaded above, so the same session starts
    // again on it with the sky and the numbers the card shows.
    uiMenu->onSessionRestartRequested = [](const ks::sim::SessionStartParams& params) {
        if (!g_simulation) return;
        const ks::sim::GameSessionMode mode = g_simulation->features().sessionMode;
        auto& weather = g_simulation->features().weatherCtrl;
        weather.weather() = ks::sim::presetFor(params.weather);
        weather.setTimeOfDay(params.weather.hourOfDay);
        printf("[MENU] session planner: restarting %s\n", ks::sim::sessionModeName(mode));
        g_simulation->startSession(mode, params);
        auto& menu = g_simulation->ui().menu();
        menu.closeSessionPlanner();
        menu.setSessionActive(g_simulation->isRunning());
        menu.setVisible(false); // back into the car, like a restart from pause
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
    // CONTENT lists the loaded .kn5's own record (model.json's kn5 block): the
    // file it is built from and the skin it wears. Cycling writes the document
    // and rebuilds the car through the loop.
    uiMenu->carImportRowLabels = []() { return g_simulation->carImportRowLabels(); };
    uiMenu->onCarImportRowCycled = [](int row) { g_simulation->cycleCarImportRow(row); };
    // CONTENT -> SKINS: the texture sets of the loaded car, and the choice that
    // is recorded in its model.json and rebuilt on the spot.
    uiMenu->carSkinChoices = []() { return g_simulation->carSkinChoices(); };
    uiMenu->activeCarSkin = []() { return g_simulation->activeCarSkin(); };
    uiMenu->onCarSkinChosen = [](const std::string& id) { g_simulation->setCarSkin(id); };
    // Roadmap 2.7: PIT STRATEGY menu -> loop state. CONFIRM PLAN writes the
    // player's fuel/job choices into m_pitStrategy; the service itself only
    // runs when the player explicitly requests it (REQUEST SERVICE) while
    // stationary in the box — the plan alone queues nothing.
    ks::sim::wirePitStrategyMenu(*uiMenu, g_simulation->pitStrategy(), {},
        [sim = g_simulation.get()]() { sim->requestPitService(true); });
    // Roadmap 2.7: garage exit / pit entry — the same two intents as the P
    // key, exposed as GARAGE menu rows.
    uiMenu->onLeaveGarageRequested = [sim = g_simulation.get()]() {
        sim->requestLeaveGarage();
    };
    uiMenu->onReturnToBoxRequested = [sim = g_simulation.get()]() {
        sim->requestReturnToBox();
    };
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
    // First run finished: the wizard reports the driver, the app writes it
    // into the player's folder. The menu continues on its own to CARS.
    uiMenu->onFirstRunFinished = [](const ks::sim::DriverProfile& p) {
        g_driverProfilePath = g_userLayout.player + "/driver.json";
        if (ks::sim::saveDriverProfile(g_driverProfilePath, p, true))
            printf("[INIT] Driver saved: %s (%s) #%d (%s)\n", p.name.c_str(),
                   p.nationality.c_str(), p.raceNumber, g_driverProfilePath.c_str());
        else
            printf("[INIT] Driver save failed: %s\n", g_driverProfilePath.c_str());
        if (ks::sim::saveDriverStats(
                g_userLayout.stats,
                ks::sim::driverStatsFromProfile(
                    p, static_cast<int>(g_simulation->features().pb.size())))) {
            printf("[INIT] Stats written for %s\n", p.name.c_str());
        }
    };
    uiMenu->onNationalityInputRequested = []() {
        printf("Nationality picker requested\n");
    };
    // GAP_MATRIX P0.5: the REPLAY page lists the recordings on disk and plays
    // the chosen one; RECORD starts/stops a session recording, which the loop
    // names and saves under replay/.
    uiMenu->replayChoices = [sim = g_simulation.get()]() { return sim->replayChoices(); };
    uiMenu->onReplayFileChosen = [sim = g_simulation.get()](const std::string& path) {
        if (!sim->startReplayPlayback(path))
            printf("Replay load failed: %s\n", path.c_str());
    };
    uiMenu->replayRecordingState = [sim = g_simulation.get()]() { return sim->replayRecording(); };
    uiMenu->onRecordReplayRequested = [sim = g_simulation.get()]() {
        const std::string saved = sim->toggleReplayRecording();
        if (!saved.empty()) printf("Replay saved: %s\n", saved.c_str());
    };
    // CONTENT: the launcher's two moving parts. The page lists the ZIPs waiting
    // in <content>/packages, and installing one unpacks it into the tree the
    // loader scans, so the new cars/tracks are listable without a restart.
    uiMenu->contentPackages = []() {
        namespace fs = std::filesystem;
        std::vector<std::pair<std::string, std::string>> out;
        std::error_code ec;
        const fs::path inbox = fs::path(ks::sim::carLibraryRoot()).parent_path() / "packages";
        if (!fs::is_directory(inbox, ec)) return out;
        for (const auto& entry : fs::directory_iterator(inbox, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string lower = entry.path().filename().string();
            for (char& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.size() < 5 || lower.compare(lower.size() - 4, 4, ".zip") != 0) continue;
            out.push_back({entry.path().filename().string(), entry.path().string()});
        }
        std::sort(out.begin(), out.end());
        if (!out.empty()) printf("[CONTENT] %zu package(s) waiting in %s\n", out.size(), inbox.string().c_str());
        return out;
    };
    // CONTENT -> SHOWROOMS: the room the game opens in. Choosing one stores it
    // in the settings (written back on exit) and shows it at once while no
    // session is up, so the menu background is the answer.
    uiMenu->showroomDefault = []() {
        return g_engineSettings.hasShowroomDefault ? g_engineSettings.showroomDefault
                                                   : std::string();
    };
    uiMenu->onShowroomChosen = [](const std::string& id, const std::string& path) {
        g_engineSettings.showroomDefault = id;
        g_engineSettings.hasShowroomDefault = true;
        printf("[CONTENT] default showroom: %s\n", id.c_str());
        if (g_simulation && !g_simulation->isRunning()) {
            g_simulation->openShowroom(path);
            printf("[CONTENT] showroom opened: %s\n", path.c_str());
        }
    };
    uiMenu->onContentInstallRequested = [](const std::string& archive) -> std::string {
        namespace fs = std::filesystem;
        const fs::path contentRoot = fs::path(ks::sim::carLibraryRoot()).parent_path();
        const ks::engine::archive::ContentInstall result =
            ks::engine::archive::ContentUnpacking::instance().installPackage(archive,
                                                                            contentRoot.string());
        if (!result.ok) {
            printf("[CONTENT] install refused (%s): %s\n", archive.c_str(), result.error.c_str());
            return "REFUSED: " + result.error;
        }
        std::string line = result.library + ": ";
        for (size_t i = 0; i < result.items.size(); ++i) {
            if (i) line += ", ";
            line += result.items[i];
        }
        if (result.items.empty()) line += "files";
        if (result.files > 1) line += " (" + std::to_string(result.files) + " files)";
        printf("[CONTENT] installed %s -> %s\n", archive.c_str(), line.c_str());
        return line;
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
            // The export's own columns: a token instead of the sentence, the
            // lap in seconds instead of "best 92.480", and the lap count on its
            // own. DriverStanding::currentLap is the laps *finished*, not the
            // lap in progress: RaceSessionManager increments it as the car
            // crosses the timing line (the AI's AIController::lapCount does the
            // same behind a half-spline guard), so a driver who has not yet
            // completed one reads 0 - a real answer the writer keeps. A best lap
            // the driver has not set also stays 0, which the writer turns into
            // an empty cell rather than a fake time.
            r.status = s.disqualified ? "dsq" : (s.finished ? "finished" : "running");
            r.bestLapTime = s.bestLapTime < 1e8f ? s.bestLapTime : 0.0f;
            r.completedLaps = s.currentLap;
            rows.push_back(std::move(r));
        }
        g_simulation->ui().menu().showResults(std::move(rows));
    };
    // RESULTS -> EXPORT CLASSIFICATION: the rows the page is showing land in the
    // player's folder as a CSV (ResultsExport.h). The menu owns the rows, the
    // app owns the folder, so the page hands its rows over and prints the
    // answer it gets back - the path, or the reason nothing was written.
    uiMenu->onExportResults = [](const std::vector<ks::sim::ResultExportRow>& rows) -> std::string {
        std::string path;
        std::string error;
        if (!ks::sim::writeResultsCsv(g_userLayout.player + "/results", rows, path, error)) {
            printf("[RESULTS] export failed: %s\n", error.c_str());
            return "EXPORT FAILED: " + error;
        }
        printf("[RESULTS] exported %zu row(s) -> %s\n", rows.size(), path.c_str());
        return "Exported " + std::to_string(rows.size()) + " row(s): " + path;
    };
    uiMenu->onOpenSettingsPanelRequested = [](const std::string& panel) {
        if (panel == "keyboard") {
            g_simulation->ui().menu().setVisible(false);
            g_simulation->ui().keyRebind().setVisible(true);
            return;
        }
        if (panel == "pad") {
            auto* im = g_simulation->inputManager();
            if (!im) return;
            g_simulation->ui().menu().setVisible(false);
            im->takeCapturedButton(); // drop a press from before the panel opened
            g_simulation->ui().padRebind().setMapping(im->mapping());
            g_simulation->ui().padRebind().setVisible(true);
            return;
        }
        if (panel == "controller") {
            auto* im = g_simulation->inputManager();
            if (!im) return;
            g_simulation->ui().menu().setVisible(false);
            ks::sim::ui::ControllerSettingsOverlay::Fallbacks fallbacks;
            const ks::sim::JoystickMapping& map = im->mapping();
            fallbacks.steerDeadZone = map.deadZone;
            fallbacks.steerSensitivity = map.steerSensitivity;
            fallbacks.throttleDeadZone = map.throttleDeadZone;
            fallbacks.throttleSensitivity = map.throttleSensitivity;
            fallbacks.brakeDeadZone = map.brakeDeadZone;
            fallbacks.brakeSensitivity = map.brakeSensitivity;
            fallbacks.clutchDeadZone = map.clutchDeadZone;
            fallbacks.clutchSensitivity = map.clutchSensitivity;
            fallbacks.invertSteer = map.invertSteer;
            g_simulation->ui().controllerSettings().setSettings(im->controllerSettings(),
                                                                fallbacks);
            g_simulation->ui().controllerSettings().setVisible(true);
            return;
        }
        if (panel == "audio") {
            g_simulation->ui().menu().openOptionsAudio();
            return;
        }
        if (panel == "graphics") {
            g_simulation->ui().menu().openOptions();
            return;
        }
        if (panel == "devices") {
            auto* im = g_simulation->inputManager();
            if (!im) return;
            g_simulation->ui().menu().setVisible(false);
            std::vector<ks::sim::ui::DeviceInfoRow> rows;
            ks::sim::ui::DeviceInfoRow r1;
            r1.name = "DirectInput wheel/pad";
            r1.detail = im->isDirectInputConnected() ? "connected" : "not connected";
            r1.connected = im->isDirectInputConnected();
            rows.push_back(r1);
            ks::sim::ui::DeviceInfoRow r2;
            r2.name = "XInput pad";
            r2.detail = im->isXInputConnected() ? "connected" : "not connected";
            r2.connected = im->isXInputConnected();
            rows.push_back(r2);
            auto& dev = g_simulation->ui().devices();
            dev.setInputManager(im);
            dev.setDevices(std::move(rows));
            dev.onCalibrationBegin = [im](int axis) {
                im->calibrationWizardBegin(axis);
            };
            dev.onCalibrationAdvance = [im]() {
                im->calibrationWizardAdvance();
                if (!im->calibrating()) {
                    // Wizard finito: calibrazione (e mapping) nella cartella del
                    // giocatore, accanto a controls.json.
                    if (im->saveJoystickJson(g_userLayout.player + "/joystick.json"))
                        printf("Joystick mapping/calibration saved (%s/joystick.json)\n",
                               g_userLayout.player.c_str());
                }
            };
            dev.onCalibrationCancel = [im]() {
                im->calibrationWizardCancel();
            };
            dev.setVisible(true);
            return;
        }
        printf("Settings panel: %s\n", panel.c_str());
    };

    // --- the config drives the image ---------------------------------------
    // Read after the ppfilter preset so the file wins: system/cfg/ksengine.json
    // first, the player's settings.json second, its key wins - the same merge
    // EngineSettings already does for physics.fixedDt and audio.master.
    {
        ks::engine::json::Value videoConfig = ks::engine::json::Value::object();
        const auto readDocument = [](const std::string& path,
                                     ks::engine::json::Value& root) {
            std::string text;
            if (!ks::engine::assets::UserData::readFile(path, text)) return false;
            std::string error;
            if (!ks::engine::json::parse(text, root, &error)) {
                printf("[INIT] %s: %s\n", path.c_str(), error.c_str());
                return false;
            }
            return true;
        };
        const auto applyVideo = [](const ks::engine::json::Value& video,
                                   ks::engine::json::Value& out) {
            if (!video.isObject()) return;
            for (const std::string& key : video.keys()) {
                if (const ks::engine::json::Value* value = video.find(key))
                    out.set(key, *value);
            }
        };
        // The video section lives in its own document: ksengine.json carries
        // "video": "system/cfg/video.json" instead of the object. The player's
        // settings.json may still inline the object, so both forms are read.
        // A referenced document is either the video object itself or one that
        // wraps it in a "video" member.
        const auto mergeVideo = [&readDocument, &applyVideo](
                                    const std::string& path,
                                    ks::engine::json::Value& out) {
            ks::engine::json::Value root;
            if (!readDocument(path, root)) return;
            const ks::engine::json::Value* video = root.find("video");
            if (!video) return;
            if (video->isObject()) {
                applyVideo(*video, out);
                return;
            }
            if (!video->isString() || video->asString().empty()) return;
            const std::string reference = video->asString();
            ks::engine::json::Value referenced;
            if (!readDocument(reference, referenced)) {
                // A bare name resolves beside the file that named it.
                const size_t slash = path.find_last_of("/\\");
                const std::string sibling =
                    (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) +
                    reference;
                if (sibling == reference || !readDocument(sibling, referenced)) {
                    printf("[INIT] %s: video file '%s' not readable\n", path.c_str(),
                           reference.c_str());
                    return;
                }
            }
            if (const ks::engine::json::Value* inner = referenced.find("video");
                inner && inner->isObject()) {
                applyVideo(*inner, out);
                return;
            }
            applyVideo(referenced, out);
        };
        mergeVideo(ks::engine::assets::Paths::systemCfgFile("ksengine.json"), videoConfig);
        mergeVideo(g_userLayout.settings, videoConfig);
        const double exposure = videoConfig.numberAt("exposure", -1.0);
        const double saturation = videoConfig.numberAt("saturation", -1.0);
        const double contrast = videoConfig.numberAt("contrast", -1.0);
        const std::string tonemap = videoConfig.stringAt("tonemap");
        bool ssao = videoConfig.boolAt("ssao", false);
        bool ssr = videoConfig.boolAt("ssr", false);
        bool ssgi = videoConfig.boolAt("ssgi", false);
        const double ssgiRadius = videoConfig.numberAt("ssgiRadius", 0.05);
        const double ssgiIntensity = videoConfig.numberAt("ssgiIntensity", 1.0);
        bool motionBlur = videoConfig.boolAt("motionBlur", false);
        // video.renderMode is a named bundle over those switches ("forward",
        // "deferred", "deferred-full"); "auto" or an unknown name leaves the
        // individual keys as the authority, so older documents are unchanged.
        bool modeDeferred = false;
        std::vector<ks::sim::RenderMode> renderModes;
        const int customModes = ks::sim::loadRenderModes(videoConfig, renderModes);
        const std::string renderMode = videoConfig.stringAt("renderMode");
        if (ks::sim::applyRenderMode(renderModes, renderMode, modeDeferred, ssao, ssr,
                                     ssgi, motionBlur))
            printf("[INIT] Render mode: %s (%d mode(s) from video.renderModes)\n",
                   renderMode.c_str(), customModes);
        const int vsync = videoConfig.boolAt("vsync", true) ? 1 : 0;
        const bool headPhysics = videoConfig.boolAt("headPhysics", true);
        const int frameLimit = static_cast<int>(videoConfig.numberAt("frameLimit", 0.0));
        const int anisotropy =
            static_cast<int>(videoConfig.numberAt("anisotropicFiltering", 8.0));
        const bool particles = videoConfig.boolAt("particles", true);
        const int shadowMapSize =
            static_cast<int>(videoConfig.numberAt("shadowMapSize", 2048.0));
        const int shadowCascades =
            static_cast<int>(videoConfig.numberAt("shadowCascades", 3.0));
        const std::string resolution = videoConfig.stringAt("resolution");
        int width = 0, height = 0;
        if (std::sscanf(resolution.c_str(), "%dx%d", &width, &height) != 2) {
            width = 0;
            height = 0;
        }
        if (width <= 0 || height <= 0) {
            // No usable video.resolution in the config ("auto" included): the
            // window takes the monitor's work area, so it opens fitted to the
            // screen instead of a guessed 1920x1080. An explicit config value
            // still wins over it.
            queryWindowWorkArea(width, height);
        }
        if (auto* renderer = g_simulation->renderer()) {
            if (exposure > 0.0) renderer->setExposure(static_cast<float>(exposure));
            // Colour grading: the config wins over the preset read above (the
            // preset already set the renderer's members), so a negative or
            // missing value keeps what the preset left in place.
            if (saturation >= 0.0) renderer->setSaturation(static_cast<float>(saturation));
            if (contrast > 0.0) renderer->setContrast(static_cast<float>(contrast));
            for (int i = 0; i < ks::sim::GameMenuOverlay::kTonemapCount; ++i) {
                if (tonemap == ks::sim::GameMenuOverlay::kTonemapNames[i])
                    renderer->setTonemapMode(i);
            }
            // video.shadowMapSize / video.shadowCascades: the cascade stack
            // rebuilds its attachments once, here, before the first frame.
            if (g_shadowMap.isInitialized()) {
                if (!g_shadowMap.setCascadeCount(shadowCascades))
                    printf("[INIT] Shadow cascades: rebuild FAILED\n");
                if (!g_shadowMap.setResolution(
                        static_cast<uint32_t>(shadowMapSize)))
                    printf("[INIT] Shadow map size: rebuild FAILED\n");
                printf("[INIT] Shadow: %ux%u, %d cascades\n",
                       g_shadowMap.resolution(), g_shadowMap.resolution(),
                       g_shadowMap.cascadeCount());
            }

            // video.gbufferOverrides: per-material channel lists / opt-outs.
            // Only a disable changes the pass today (the shared G-buffer
            // pipeline cannot drop a single channel); the rest is carried for
            // the material-aware variants.
            ks::sim::GBufferLayoutManager gbufferLayouts;
            const int gbufferOverrides =
                ks::sim::loadGBufferOverrides(videoConfig, gbufferLayouts);
            if (gbufferOverrides > 0) {
                renderer->setGBufferLayouts(gbufferLayouts);
                printf("[INIT] G-buffer overrides: %d material(s)\n", gbufferOverrides);
            }
            // A named mode that asks for the deferred path opens it even when
            // no deferred-only effect is on, so "deferred" means what it says.
            if (modeDeferred) renderer->setDeferred(true);
            renderer->setSsao(ssao);
            renderer->setSsr(ssr);
            renderer->setSsgi(ssgi,
                              ssgiRadius > 0.0 ? static_cast<float>(ssgiRadius) : 0.05f,
                              ssgiIntensity > 0.0 ? static_cast<float>(ssgiIntensity) : 1.0f);
            if (ssgi) {
                // SSGI is gathered by the deferred lighting shader, which the
                // forward path never runs: asking for it means asking for that
                // path, exactly like the KS_SSAO / KS_SSR env switches above.
                renderer->setDeferred(true);
                printf("[INIT] Render path: deferred (video.ssgi)\n");
                printf("[INIT] SSGI: on (radius %.3f, intensity %.2f)\n",
                       ssgiRadius, ssgiIntensity);
            }

            renderer->setMotionBlur(motionBlur);
            if (motionBlur) renderer->setDeferred(true);
            renderer->setVsync(vsync != 0);
            if (auto* camera = g_simulation->camera())
                camera->setHeadPhysics(headPhysics);
            renderer->setAnisotropicFiltering(anisotropy);
            g_simulation->setFrameLimit(frameLimit);
            g_simulation->setParticlesEnabled(particles);
            // Mirrors, stage 1: the rear-view tile. The bool comes from
            // video.mirrors, the tile size from the pair video.mirrorResolution
            // has always documented ("128x512"), landscape forced; either one
            // can be overridden with KS_MIRROR for a quick test run.
            const int mirrorConfig = videoConfig.boolAt("mirrors", false) ? 1 : 0;
            int mirrorW = 0, mirrorH = 0;
            const std::string mirrorRes = videoConfig.stringAt("mirrorResolution");
            if (std::sscanf(mirrorRes.c_str(), "%dx%d", &mirrorW, &mirrorH) != 2) {
                mirrorW = 0;
                mirrorH = 0;
            }
            int mirrorState = mirrorConfig;
            if (const char* env = std::getenv("KS_MIRROR"); env && *env)
                mirrorState = std::atoi(env) != 0 ? 1 : 0;
            if (mirrorW > 0 && mirrorH > 0)
                renderer->setMirrorResolution(static_cast<uint32_t>(mirrorW),
                                              static_cast<uint32_t>(mirrorH));
            renderer->setVirtualMirror(mirrorState != 0);
            printf("[INIT] Mirrors: %s (tile %s)\n",
                   mirrorState != 0 ? "on" : "off",
                   mirrorW > 0 ? mirrorRes.c_str() : "default 512x128");
            if (width > 0 && height > 0 && g_hWnd) {
                const uint32_t currentW = renderer->extent().width;
                const uint32_t currentH = renderer->extent().height;
                if (currentW != static_cast<uint32_t>(width) ||
                    currentH != static_cast<uint32_t>(height)) {
                    RECT rc{0, 0, width, height};
                    const DWORD style = static_cast<DWORD>(GetWindowLongW(g_hWnd, GWL_STYLE));
                    AdjustWindowRect(&rc, style, FALSE);
                    SetWindowPos(g_hWnd, nullptr, 0, 0, rc.right - rc.left,
                                 rc.bottom - rc.top,
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
        }
        printf("[INIT] Config video: exposure %.2f, tonemap %s, ssao %d, ssr %d, ssgi %d, motion blur %d, head %d, vsync %d, %dx%d, aniso %dx, limit %d fps, particles %d\n",
               exposure > 0.0 ? exposure : 0.0, tonemap.c_str(), ssao, ssr, ssgi, motionBlur,
               headPhysics ? 1 : 0, vsync, width, height, anisotropy, frameLimit, particles ? 1 : 0);
    }
    // --- VIDEO: the page reads the renderer back and writes to it ----------
    // Resolution and fullscreen are window state, not renderer state: the app
    // owns the HWND, so they go through the two callbacks below.
    const auto readOptions = [](ks::sim::GameMenuOverlay::MenuOptions& options) {
        if (auto* renderer = g_simulation->renderer()) {
            options.resolutionIndex = ks::sim::GameMenuOverlay::resolutionIndexFor(
                static_cast<int>(renderer->extent().width),
                static_cast<int>(renderer->extent().height));
            // The row shows this, not the entry: the window's real client size.
            options.windowWidth = static_cast<int>(renderer->extent().width);
            options.windowHeight = static_cast<int>(renderer->extent().height);
            options.vsync = renderer->vsync();
            options.motionBlur = renderer->isMotionBlur();
            options.exposureIndex =
                ks::sim::GameMenuOverlay::exposureIndexFor(renderer->exposure());
            options.tonemap = renderer->tonemapMode();
            options.ssao = renderer->isSsao();
            options.ssr = renderer->isSsr();
            options.mirror = renderer->virtualMirror();
        }
        // The mixer only exists from the first car on. Until then the settings
        // are what the volume is, so a change on another row never invents an
        // audio.master the player did not choose.
        options.masterVolume = g_engineSettings.audioMaster;
        if (auto* audio = g_simulation->audio())
            options.masterVolume = audio->masterVolume();
        // VIDEO rows without a renderer getter come from the merged settings:
        // that is what the next start reads, so the page shows the value the
        // file holds instead of a compiled-in guess.
        {
            using menu = ks::sim::GameMenuOverlay;
            const auto& s = g_engineSettings;
            options.frameLimitIndex = menu::nearestIndex(
                menu::kFrameLimitValues, menu::kFrameLimitCount, s.videoFrameLimit);
            options.anisoIndex = menu::nearestIndex(
                menu::kAnisoValues, menu::kAnisoCount, s.videoAnisotropicFiltering);
            options.shadowSizeIndex = menu::nearestIndex(
                menu::kShadowSizeValues, menu::kShadowSizeCount, s.videoShadowMapSize);
            options.cascadeIndex = menu::nearestIndex(
                menu::kCascadeValues, menu::kCascadeCount, s.videoShadowCascades);
            options.saturationIndex = menu::nearestIndex(
                menu::kSaturationValues, menu::kSaturationCount, s.videoSaturation);
            options.contrastIndex = menu::nearestIndex(
                menu::kContrastValues, menu::kContrastCount, s.videoContrast);
            options.mirrorResIndex = 1; // 512x128, the config's own default pair
            for (int i = 0; i < menu::kMirrorResCount; ++i)
                if (s.videoMirrorResolution == menu::kMirrorResValues[i])
                    options.mirrorResIndex = i;
            options.ssgi = s.videoSsgi;
            options.particles = s.videoParticles;
            options.headPhysics = s.videoHeadPhysics;
        }
        // GAMEPLAY rows show what the session applies: the tweak set is the
        // live truth (a settings file seeded it at start-up).
        {
            const auto& gameplay = ks::engine::sim::GameplayTweaks::instance().getTweakSet();
            options.fuelIndex =
                ks::sim::GameMenuOverlay::gameplayIndexFor(gameplay.fuelConsumption);
            options.tireIndex =
                ks::sim::GameMenuOverlay::gameplayIndexFor(gameplay.tireDamageRate);
            options.strictRules = gameplay.racingRulesStrict;
        }
        // The window style is the truth for fullscreen: F11 changes it too.
        const DWORD style = static_cast<DWORD>(GetWindowLongW(g_hWnd, GWL_STYLE));
        options.fullscreen = (style & WS_OVERLAPPEDWINDOW) == 0;
    };
    {
        ks::sim::GameMenuOverlay::MenuOptions options =
            g_simulation->ui().menu().options();
        readOptions(options);
        g_simulation->ui().menu().setOptions(options);
        printf("[INIT] Video seeded: %dx%d, fullscreen %d, vsync %d, motion blur %d, exposure %.2f, tonemap %s, ssao %d, ssr %d, volume %.0f\n",
               options.windowWidth > 0
                   ? options.windowWidth
                   : ks::sim::GameMenuOverlay::resolutionWidth(options.resolutionIndex),
               options.windowHeight > 0
                   ? options.windowHeight
                   : ks::sim::GameMenuOverlay::resolutionHeight(options.resolutionIndex),
               options.fullscreen ? 1 : 0, options.vsync ? 1 : 0,
               options.motionBlur ? 1 : 0,
               static_cast<double>(
                   ks::sim::GameMenuOverlay::exposureValue(options.exposureIndex)),
               ks::sim::GameMenuOverlay::tonemapName(options.tonemap),
               options.ssao ? 1 : 0, options.ssr ? 1 : 0,
               static_cast<double>(options.masterVolume * 100.f));
    }
    uiMenu->onOptionsRefresh = readOptions;
    uiMenu->onResolutionRequested = [](int width, int height) {
        if (!g_hWnd) return;
        RECT rc{0, 0, width, height};
        const DWORD style = static_cast<DWORD>(GetWindowLongW(g_hWnd, GWL_STYLE));
        AdjustWindowRect(&rc, style, FALSE);
        SetWindowPos(g_hWnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        printf("[MENU] video: window %dx%d requested\n", width, height);
    };
    uiMenu->onOptionsChanged = [](const ks::sim::GameMenuOverlay::MenuOptions& options) {
        using menu = ks::sim::GameMenuOverlay;
        // The values the page shows, resolved once: the same numbers are
        // applied to the image and written into the settings file.
        const int frameLimit = menu::kFrameLimitValues[std::min(options.frameLimitIndex, menu::kFrameLimitCount - 1)];
        const int anisotropy = menu::kAnisoValues[std::min(options.anisoIndex, menu::kAnisoCount - 1)];
        const float saturation = menu::kSaturationValues[std::min(options.saturationIndex, menu::kSaturationCount - 1)];
        const float contrast = menu::kContrastValues[std::min(options.contrastIndex, menu::kContrastCount - 1)];
        const int shadowSize = menu::kShadowSizeValues[std::min(options.shadowSizeIndex, menu::kShadowSizeCount - 1)];
        const int shadowCascades = menu::kCascadeValues[std::min(options.cascadeIndex, menu::kCascadeCount - 1)];
        const char* mirrorRes = menu::kMirrorResValues[std::min(options.mirrorResIndex, menu::kMirrorResCount - 1)];
        const std::string tonemapName = menu::tonemapName(options.tonemap);
        const float exposure = menu::exposureValue(options.exposureIndex);
        if (auto* renderer = g_simulation->renderer()) {
            renderer->setExposure(exposure);
            renderer->setTonemapMode(options.tonemap);
            renderer->setSsao(options.ssao);
            renderer->setSsr(options.ssr);
            renderer->setSsgi(options.ssgi, g_engineSettings.videoSsgiRadius,
                              g_engineSettings.videoSsgiIntensity);
            if (options.ssgi) renderer->setDeferred(true);
            renderer->setSaturation(saturation);
            renderer->setContrast(contrast);
            renderer->setVirtualMirror(options.mirror);
            renderer->setMotionBlur(options.motionBlur);
            if (options.motionBlur) renderer->setDeferred(true); // resolve pass only
            renderer->setVsync(options.vsync);
            renderer->setAnisotropicFiltering(anisotropy);
        }
        g_simulation->setFrameLimit(frameLimit);
        g_simulation->setParticlesEnabled(options.particles);
        if (auto* camera = g_simulation->camera()) camera->setHeadPhysics(options.headPhysics);
        if (auto* audio = g_simulation->audio())
            audio->setMasterVolume(options.masterVolume);
        // Every key the page can change is written back, so the option the
        // player set is what the next start reads: the launcher's own settings
        // page behaves the same way. Shadows and the mirror tile build GPU
        // state once, so those three are stored and applied at next start.
        {
            auto& s = g_engineSettings;
            // Only the keys this change actually moved are written: a row the
            // player never touched stays out of the file, exactly like the
            // other settings, so a round trip invents nothing.
            const auto moved = [](auto before, auto after) { return before != after; };
            const auto keep = [&s](const char* key, bool changed) {
                if (changed) s.videoPresence.insert(key);
            };
            keep("exposure", moved(s.videoExposure, exposure));
            s.videoExposure = exposure;
            keep("saturation", moved(s.videoSaturation, saturation));
            s.videoSaturation = saturation;
            keep("contrast", moved(s.videoContrast, contrast));
            s.videoContrast = contrast;
            keep("tonemap", moved(s.videoTonemap, tonemapName));
            s.videoTonemap = tonemapName;
            keep("vsync", moved(s.videoVsync, options.vsync));
            s.videoVsync = options.vsync;
            keep("ssao", moved(s.videoSsao, options.ssao));
            s.videoSsao = options.ssao;
            keep("ssr", moved(s.videoSsr, options.ssr));
            s.videoSsr = options.ssr;
            keep("ssgi", moved(s.videoSsgi, options.ssgi));
            s.videoSsgi = options.ssgi;
            keep("mirrors", moved(s.videoMirrors, options.mirror));
            s.videoMirrors = options.mirror;
            keep("motionBlur", moved(s.videoMotionBlur, options.motionBlur));
            s.videoMotionBlur = options.motionBlur;
            keep("particles", moved(s.videoParticles, options.particles));
            s.videoParticles = options.particles;
            keep("headPhysics", moved(s.videoHeadPhysics, options.headPhysics));
            s.videoHeadPhysics = options.headPhysics;
            keep("frameLimit", moved(s.videoFrameLimit, frameLimit));
            s.videoFrameLimit = frameLimit;
            keep("anisotropicFiltering", moved(s.videoAnisotropicFiltering, anisotropy));
            s.videoAnisotropicFiltering = anisotropy;
            keep("shadowMapSize", moved(s.videoShadowMapSize, shadowSize));
            s.videoShadowMapSize = shadowSize;
            keep("shadowCascades", moved(s.videoShadowCascades, shadowCascades));
            s.videoShadowCascades = shadowCascades;
            keep("mirrorResolution", s.videoMirrorResolution != std::string(mirrorRes));
            s.videoMirrorResolution = mirrorRes;
            if (moved(s.audioMaster, options.masterVolume)) {
                s.audioMaster = options.masterVolume;
                s.hasAudioMaster = true;
            }
        }
        // GAMEPLAY: the tweak set is what the loop applies to the car, and the
        // settings carry the chosen values into the next start. The translation
        // lives in GameplayOptionsBridge.h so gameplay_settings_test drives the
        // same code this handler runs.
        {
            auto& tweaks = ks::engine::sim::GameplayTweaks::instance();
            if (ks::sim::applyGameplayOptionsFromMenu(options, tweaks, g_engineSettings)) {
                printf("[MENU] gameplay: fuel x%.1f, tyres x%.1f, racing rules %s\n",
                       static_cast<double>(ks::sim::GameMenuOverlay::gameplayValue(
                           options.fuelIndex)),
                       static_cast<double>(ks::sim::GameMenuOverlay::gameplayValue(
                           options.tireIndex)),
                       options.strictRules ? "strict" : "lenient");
            }
        }
        printf("[MENU] video: vsync %d, frame limit %d, aniso %dx, particles %d, head physics %d, "
               "motion blur %d, ssao %d, ssr %d, ssgi %d, mirror %d (%s), shadows %d/%d, "
               "exposure %.2f, saturation %.2f, contrast %.2f, tonemap %s, volume %.0f\n",
               options.vsync ? 1 : 0, frameLimit, anisotropy, options.particles ? 1 : 0,
               options.headPhysics ? 1 : 0, options.motionBlur ? 1 : 0, options.ssao ? 1 : 0,
               options.ssr ? 1 : 0, options.ssgi ? 1 : 0, options.mirror ? 1 : 0, mirrorRes,
               shadowSize, shadowCascades, static_cast<double>(exposure),
               static_cast<double>(saturation), static_cast<double>(contrast),
               tonemapName.c_str(), static_cast<double>(options.masterVolume * 100.f));
    };
    // The VIDEO page remembers which sections were folded: the layout is part
    // of ui.collapsedSections, so the page opens the way it was left.
    {
        for (const std::string& name : g_engineSettings.uiCollapsedSections) {
            const int index = ks::sim::GameMenuOverlay::sectionIndexFor(name);
            if (index >= 0) uiMenu->setSectionCollapsed(index, true);
        }
        if (g_engineSettings.hasUiCollapsedSections)
            printf("[INIT] VIDEO sections folded: %s\n",
                   g_engineSettings.uiCollapsedSections.empty()
                       ? "none"
                       : [&]() {
                             std::string listed;
                             for (const std::string& name : g_engineSettings.uiCollapsedSections) {
                                 if (!listed.empty()) listed += ", ";
                                 listed += name;
                             }
                             return listed;
                         }().c_str());
    }
    uiMenu->onSectionsChanged = []() {
        auto& settings = g_engineSettings;
        auto& menu = g_simulation->ui().menu();
        settings.uiCollapsedSections.clear();
        for (int i = 0; i < ks::sim::GameMenuOverlay::kSectionCount; ++i)
            if (menu.sectionCollapsed(i))
                settings.uiCollapsedSections.insert(ks::sim::GameMenuOverlay::sectionKey(i));
        settings.hasUiCollapsedSections = true;
    };
    uiMenu->onDevModeRequested = []() {
        printf("Dev mode request\n");
    };
    // Career stats follow the profile straight into stats.json. Session
    // results are written on the profile without a profile edit, so the exit
    // path writes them as well — this hook keeps the file current while the
    // menu is being used.
    uiMenu->onProfileChanged = [](const ks::sim::DriverProfile& p) {
        ks::sim::saveDriverStats(
            g_userLayout.stats,
            ks::sim::driverStatsFromProfile(
                p, static_cast<int>(g_simulation->features().pb.size())));
    };

    // --- Roadmap 1.4 / P2.7: keyboard bindings ---------------------------
    // controls.json in the player's own folder replaces the shared
    // user/keyboard.ini: load it, fall back to the legacy ini once (and
    // migrate what it held), and send every rebind to controls.json only.
    if (auto* im = g_simulation->inputManager()) {
        ks::sim::KeyboardMapping kb;
        // Install-wide bindings first: controls.vehicle in the document that
        // ksengine.json points at (system/cfg/controls.json) is the system
        // default, key by key. The player's own controls.json overrides only
        // the actions it actually carries, and the legacy user/keyboard.ini
        // migration runs after both.
        {
            std::ifstream in(ks::engine::assets::Paths::systemCfgFile("controls.json"),
                             std::ios::binary);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                ks::engine::json::Value root;
                std::string error;
                if (ks::engine::json::parse(ss.str(), root, &error) && root.isObject()) {
                    const ks::engine::json::Value* controls = root.find("controls");
                    if (controls && controls->isObject()) {
                        // The bindings live under controls.vehicle (AC-style). Fall
                        // back to controls.keyboard for older files.
                        const ks::engine::json::Value* kbj = controls->find("vehicle");
                        if (!kbj || !kbj->isObject()) kbj = controls->find("keyboard");
                        if (kbj && kbj->isObject() &&
                            ks::sim::applyKeyboardMappingObject(*kbj, kb) > 0)
                            printf("[INIT] keyboard: system defaults\n");
                    }
                }
            }
        }
        if (ks::sim::loadKeyboardMappingJson(g_userLayout.controls, kb)) {
            im->setKeyboardMapping(kb);
        } else if (ks::sim::loadKeyboardMapping("user/keyboard.ini", kb)) {
            im->setKeyboardMapping(kb);
            if (ks::sim::saveKeyboardMappingJson(g_userLayout.controls, kb))
                printf("Migrated user/keyboard.ini -> %s\n", g_userLayout.controls.c_str());
        } else {
            // System defaults (or hardcoded fallbacks) still apply.
            im->setKeyboardMapping(kb);
        }
        g_simulation->ui().keyRebind().setMapping(im->keyboardMapping());

        // Mapping + calibrazione del volante/pad: joystick.json vive nella
        // cartella del giocatore accanto a controls.json. Il wizard lo scrive a
        // fine calibrazione; senza questa lettura il risultato andava perso a
        // ogni avvio (l'asse restava pass-through).
        const std::string joystickPath = g_userLayout.player + "/joystick.json";
        if (im->loadJoystickJson(joystickPath) != 0)
            printf("[INIT] joystick.json: mapping + calibrazione (%s)\n", joystickPath.c_str());

        // --- profili input: mapping + calibrazione + tastiera in un file ---
        // Il pannello (F3) elenca user/<player>/profiles/; Load applica il
        // profilo, New scrive il setup corrente sotto il primo nome libero,
        // Delete rimuove il file.
        const std::string profileDir = g_userLayout.player + "/profiles";
        auto* profiles = &g_simulation->ui().profiles();
        profiles->setProfiles(listInputProfiles(profileDir));
        profiles->onLoad = [im](const std::string& name) {
            const std::string path = g_userLayout.player + "/profiles/" + name + ".json";
            if (!im->loadProfile(path)) {
                printf("Profilo input non applicato: %s\n", path.c_str());
                return;
            }
            g_simulation->ui().keyRebind().setMapping(im->keyboardMapping());
            printf("Profilo input caricato: %s\n", path.c_str());
        };
        profiles->onNew = [im]() {
            const std::string dir = g_userLayout.player + "/profiles";
            std::error_code ec;
            for (int i = 1; i < 1000; ++i) {
                const std::string candidate = "profile" + std::to_string(i);
                if (std::filesystem::exists(dir + "/" + candidate + ".json", ec)) continue;
                const std::string written = im->saveProfile(dir, candidate);
                if (written.empty())
                    printf("Profilo input non scrivibile in %s\n", dir.c_str());
                else
                    printf("Profilo input salvato: %s\n", written.c_str());
                g_simulation->ui().profiles().setProfiles(
                    listInputProfiles(g_userLayout.player + "/profiles"));
                return;
            }
        };
        profiles->onDelete = [](const std::string& name) {
            const std::string path = g_userLayout.player + "/profiles/" + name + ".json";
            std::error_code ec;
            if (!std::filesystem::remove(path, ec))
                printf("Profilo input non rimosso: %s\n", path.c_str());
            g_simulation->ui().profiles().setProfiles(
                listInputProfiles(g_userLayout.player + "/profiles"));
        };

        // Controller settings ride in the same files as the engine settings,
        // under a "controller" object: install defaults first, the player's
        // own second, later wins per key (the EngineSettings merge order).
        if (im->loadControllerSettings(
                ks::engine::assets::Paths::systemCfgFile("ksengine.json")))
            printf("[INIT] controller settings: system defaults\n");
        if (!g_userLayout.settings.empty() &&
            im->loadControllerSettings(g_userLayout.settings))
            printf("[INIT] controller settings: %s\n", g_userLayout.settings.c_str());
    }
    g_simulation->ui().keyRebind().onMappingChanged = [](const ks::sim::KeyboardMapping& m) {
        if (auto* im = g_simulation->inputManager()) im->setKeyboardMapping(m);
        if (ks::sim::saveKeyboardMappingJson(g_userLayout.controls, m))
            printf("Keyboard bindings saved (%s)\n", g_userLayout.controls.c_str());
    };
    g_simulation->ui().padRebind().onMappingChanged = [](const ks::sim::JoystickMapping& m) {
        auto* im = g_simulation->inputManager();
        if (!im) return;
        im->setMapping(m);
        if (im->saveJoystickJson(g_userLayout.player + "/joystick.json"))
            printf("Pad/wheel bindings saved (%s/joystick.json)\n",
                   g_userLayout.player.c_str());
    };
    // Controller panel: edits apply to the live mapping immediately, but only
    // Save writes settings.json; Revert/Esc restore the opening document.
    g_simulation->ui().controllerSettings().onChanged =
        [](const ks::engine::config::ControllerSettings& settings) {
            auto* im = g_simulation->inputManager();
            if (!im) return;
            im->controllerSettings() = settings;
            im->applyControllerSettings();
        };
    g_simulation->ui().controllerSettings().onSaved =
        [](const ks::engine::config::ControllerSettings& settings) {
            auto* im = g_simulation->inputManager();
            if (!im) return;
            im->controllerSettings() = settings;
            im->applyControllerSettings();
            if (im->saveControllerSettings(g_userLayout.settings))
                printf("Controller settings saved (%s)\n", g_userLayout.settings.c_str());
        };

    // In-car pause overlay: RESTART re-enters the mode the session was started
    // in (startSession() stops the running one first), so the player is back
    // in the pits with the same car, track and AI count.
    uiMenu->onRestartSessionRequested = []() {
        if (!g_simulation) return;
        const ks::sim::GameSessionMode mode = g_simulation->features().sessionMode;
        std::printf("[MENU] restart session: %s\n", ks::sim::sessionModeName(mode));
        g_simulation->startSession(mode);
        g_simulation->ui().menu().setSessionActive(g_simulation->isRunning());
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
            g_simulation->ui().notify("Hosting a session on port 40000");
        } else {
            g_simulation->ui().chat().addLine("[local] server start failed on 40000");
            g_simulation->ui().notify("Server start failed on port 40000",
                                      ks::sim::ui::NoticeKind::Warning);
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
        g_simulation->ui().notify("Network session closed");
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

    // KS_START_IN_GARAGE=1: the session starts with the car in its pit box
    // instead of on the grid - the start MODE SETUP asks for, and the one a
    // restart from the session planner takes. It needs a track that ships a
    // garage row; the loop says so when there is none.
    const bool startInGarageEnv = [] {
        const char* v = std::getenv("KS_START_IN_GARAGE");
        if (v == nullptr || v[0] == '\0') return false;
        return v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y';
    }();
    // The mode a bare session would run: KS_SESSION picks it, practice is what
    // start() does when nothing does.
    auto envSessionMode = []() {
        if (const char* sess = std::getenv("KS_SESSION"); sess && sess[0])
            return ks::sim::modeFromMenuEntry(sess);
        return ks::sim::GameSessionMode::Practice;
    };
    // The parameters of that session: the mode's own defaults, a race still
    // promising the grid startSession(mode) would have given it.
    auto envSessionParams = [](ks::sim::GameSessionMode mode) {
        ks::sim::SessionStartParams p = ks::sim::defaultsForMode(mode);
        if (mode == ks::sim::GameSessionMode::Race && p.aiCars == 0) p.aiCars = 5;
        return p;
    };

    // KS_HEADLESS runs the session with no menu and no renderer at all; the
    // windowed KS_AUTOSTART=1 path skips the menu but still renders.
    if (g_headless) {
        printf("[INIT] KS_HEADLESS -> starting session (no menu, no renderer)\n");
        loadEnvContent();
        if (startInGarageEnv) {
            const ks::sim::GameSessionMode mode = envSessionMode();
            ks::sim::SessionStartParams params = envSessionParams(mode);
            params.startInGarage = true;
            printf("[INIT] KS_START_IN_GARAGE=1 -> %s from the pit box\n",
                   ks::sim::sessionModeName(mode));
            g_simulation->ui().menu().adoptSessionConfig(mode, params);
            g_simulation->startSession(mode, params);
        } else {
            g_simulation->start();
        }
    } else if (const char* autostart = std::getenv("KS_AUTOSTART");
        autostart && autostart[0] == '1') {
        printf("[INIT] KS_AUTOSTART=1 -> starting drive session, menu suppressed\n");
        // KS_SESSION=<mode> picks the session type (race = grid placement,
        // roadmap 2.8 evidence); without it bare start() keeps the practice
        // default exactly like before.
        if (const char* sess = std::getenv("KS_SESSION"); sess && sess[0]) {
            const ks::sim::GameSessionMode mode = ks::sim::modeFromMenuEntry(sess);
            printf("[INIT] KS_SESSION=%s -> %s\n", sess, ks::sim::sessionModeName(mode));
            if (startInGarageEnv) {
                ks::sim::SessionStartParams params = envSessionParams(mode);
                params.startInGarage = true;
                printf("[INIT] KS_START_IN_GARAGE=1 -> from the pit box\n");
                g_simulation->ui().menu().adoptSessionConfig(mode, params);
                g_simulation->startSession(mode, params);
            } else {
                // The card reports what the app started, env paths included.
                g_simulation->ui().menu().adoptSessionConfig(mode, envSessionParams(mode));
                g_simulation->startSession(mode); // startSession() calls start()
            }
        } else if (startInGarageEnv) {
            const ks::sim::GameSessionMode mode = ks::sim::GameSessionMode::Practice;
            ks::sim::SessionStartParams params = envSessionParams(mode);
            params.startInGarage = true;
            printf("[INIT] KS_START_IN_GARAGE=1 -> practice from the pit box\n");
            g_simulation->ui().menu().adoptSessionConfig(mode, params);
            g_simulation->startSession(mode, params);
        } else {
            g_simulation->start();
        }
        g_simulation->ui().menu().setSessionActive(g_simulation->isRunning());
        g_simulation->ui().menu().setVisible(false);
    }

    // KS_MATERIALS_REPORT=<path>: what the track is made of, written before the
    // first frame. It runs for a headless session as well - that is the point:
    // with no renderer there is nothing to look at, and the report is the only
    // answer a run can give about the content's materials.
    if (const char* reportPath = std::getenv("KS_MATERIALS_REPORT");
        reportPath != nullptr && reportPath[0] != 0) {
        const char* trackEnv = std::getenv("KS_TRACK");
        const std::string source = (trackEnv != nullptr && trackEnv[0] != 0)
                                       ? std::string(trackEnv)
                                       : std::string("content");
        std::string reportError;
        // The document is looked for next to the binary and under the content
        // roots too, so a staged install reports like a repository launch.
        std::vector<std::string> tried;
        const std::string document =
            ks::sim::findLocationMaterialDocument(ks::sim::executableDirectory(), &tried);
        if (document.empty()) {
            fprintf(stderr, "[materials] no report for %s: %s\n", source.c_str(),
                    ks::sim::locationMaterialNotFoundMessage(tried).c_str());
        } else if (ks::sim::writeTrackMaterialReport(source, reportPath, &reportError, document)) {
            printf("[materials] report for %s written to %s (vocabulary from %s)\n",
                   source.c_str(), reportPath, document.c_str());
        } else {
            fprintf(stderr, "[materials] no report for %s: %s\n", source.c_str(),
                    reportError.c_str());
        }
    }

    // KS_CAM=<cockpit|chase|free> forces the camera the session starts in.
    // The key 'C' still toggles it afterwards; this env exists so an unattended
    // run (screenshot capture, perf logging) lands in the intended view with
    // no input plumbing — the windscreen rain gate, for one, only paints in
    // the cockpit view.
    if (const char* camEnv = std::getenv("KS_CAM"); camEnv && camEnv[0]) {
        ks::sim::CameraController::Mode cam =
            ks::sim::CameraController::Mode::Chase;
        const char* camName = "chase";
        if (!std::strcmp(camEnv, "cockpit")) {
            cam = ks::sim::CameraController::Mode::Cockpit;
            camName = "cockpit";
        } else if (!std::strcmp(camEnv, "free")) {
            cam = ks::sim::CameraController::Mode::Free;
            camName = "free";
        } else if (std::strcmp(camEnv, "chase") != 0) {
            printf("[INIT] KS_CAM=%s -> unknown view, keeping chase\n", camEnv);
        }
        g_simulation->setCameraMode(cam);
        if (std::strcmp(camName, "chase") != 0)
            printf("[INIT] KS_CAM=%s -> %s view\n", camEnv, camName);
    }

    // KS_MIRROR_MESH=<name> makes the rear-view tile sample that mesh's
    // geometry instead of the fixed screen quad (mirror stage 3). The mesh
    // must already exist in the renderer (e.g. a bake's "mirror" entry);
    // an unknown name silently keeps the quad.
    if (const char* mm = std::getenv("KS_MIRROR_MESH"); mm && mm[0]) {
        if (g_nativeRenderer) g_nativeRenderer->setMirrorGlassMesh(mm);
        printf("[INIT] KS_MIRROR_MESH=%s\n", mm);
    }
    if (const char* wg = std::getenv("KS_WINDSCREEN_GLASS"); wg && wg[0]) {
        if (g_nativeRenderer) g_nativeRenderer->setWindscreenGlassMesh(wg);
        printf("[INIT] KS_WINDSCREEN_GLASS=%s\n", wg);
    }

    // Gate 2.11/2.12 enabler: KS_WEATHER=<dry|damp|wet> applies the same
    // preset the menu offers, right after the optional session start so the
    // whole run (grip, spray, wetness decay) is reproducible without UI
    // interaction - the wet/dry race gate runs KS_WEATHER=wet.
    if (const char* wx = std::getenv("KS_WEATHER"); wx && wx[0]) {
        printf("[INIT] KS_WEATHER=%s\n", wx);
        g_simulation->setWeatherPresetByName(wx);
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
    // The same frame, reduced to a signature: the hash plus a 12x10 luma grid,
    // which is what the visual baseline compares.
    if (!g_visualDigestPath.empty()) {
        const ks::sim::FrameSignature sig =
            ks::sim::frameSignature(px.data(), int(ext.width), int(ext.height));
        std::string sigError;
        if (ks::sim::writeFrameSignature(g_visualDigestPath, sig, &sigError))
            printf("[shot] digest %s hash=%llu mean=%.2f\n", g_visualDigestPath.c_str(),
                   static_cast<unsigned long long>(sig.hash), sig.meanLuma);
        else
            printf("[shot] digest FAILED %s: %s\n", g_visualDigestPath.c_str(),
                   sigError.c_str());
    }
    return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
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
    if (const char* vd = std::getenv("KS_VISUAL_DIGEST"); vd && vd[0]) {
        g_visualDigestPath = vd;
        printf("[INIT] KS_VISUAL_DIGEST=%s\n", vd);
    }
    if (const char* sd = std::getenv("KS_SCREENSHOT_DELAY")) {
        const int d = std::atoi(sd);
        if (d >= 0) g_shotDelay = d;
    }
    // Native modules: every *.dll in system/modules (KS_MODULES_DIR overrides),
    // in file-name order. They speak KsModuleApi.h; one built for another ABI, or
    // missing an entry, is reported and skipped instead of being called. A
    // directory that does not exist is the normal case and says so once.
    {
        g_nativeModules.setLog([](const std::string& line) {
            printf("[modules] %s\n", line.c_str());
        });
        const char* modulesEnv = std::getenv("KS_MODULES_DIR");
        const std::string modulesDir = (modulesEnv != nullptr && modulesEnv[0] != 0)
                                           ? std::string(modulesEnv)
                                           : std::string("system/modules");
        const auto modules = g_nativeModules.loadDirectory(modulesDir);
        if (!modules.empty()) {
            int refused = 0;
            for (const auto& module : modules)
                if (!module.loaded) ++refused;
            printf("[modules] %s: %d loaded, %d refused\n", modulesDir.c_str(),
                   g_nativeModules.loadedCount(), refused);
        }
    }
    // Global hotkeys: controls.hotkeys in the install-wide document. The
    // struct's defaults are the keys the handler used to hardcode, so a
    // document without the block behaves exactly as before.
    {
        std::ifstream in(ks::engine::assets::Paths::systemCfgFile("controls.json"),
                         std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            ks::engine::json::Value root;
            std::string error;
            if (ks::engine::json::parse(ss.str(), root, &error) &&
                ks::sim::loadHotkeysFromControls(root, g_hotkeys) > 0)
                printf("[INIT] hotkeys: controls.hotkeys\n");
        }
    }
    // Roadmap 4.x — photo mode: free camera, HUD hidden, screenshot on key.
    if (const char* pm = std::getenv("KS_PHOTOMODE")) {
        // The env names the toggle key; the mode still starts off, so the
        // first press enters and the next leaves, like the document's key.
        const int vk = ks::sim::keyFromName(pm);
        if (vk > 0) g_hotkeys.photoMode = vk;
    }
    // Roadmap 2.14 — timed profile run (see globals).
    // --track <dir>: the KS_TRACK setting, from the command line. WinMain gets
    // the raw ANSI tail, so this is a minimal quoted-word split; anything it
    // does not recognise is ignored (shells add their own switches).
    if (lpCmdLine && lpCmdLine[0]) {
        const auto isSpace = [](char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        };
        const std::string cmd(lpCmdLine);
        size_t i = 0;
        const auto skipSpace = [&]() { while (i < cmd.size() && isSpace(cmd[i])) ++i; };
        const auto nextWord = [&]() {
            std::string w;
            skipSpace();
            if (i < cmd.size() && cmd[i] == '"') {
                for (++i; i < cmd.size() && cmd[i] != '"'; ++i) w += cmd[i];
                if (i < cmd.size()) ++i;
            } else {
                while (i < cmd.size() && !isSpace(cmd[i])) w += cmd[i++];
            }
            return w;
        };
        while (true) {
            const std::string flag = nextWord();
            if (flag.empty()) break;
            if (flag == "--track" || flag == "-t") {
                const std::string value = nextWord();
                if (value.empty()) printf("[MAIN] --track needs a directory\n");
                else g_cliTrack = value;
            }
        }
    }
    if (const char* ps = std::getenv("KS_PROFILE_SECONDS")) {
        const double s = std::strtod(ps, nullptr);
        if (s > 0.0) g_profileSeconds = s;
    }

    printf("[MAIN] Starting KinetiKa Simulator (Qt-free%s)...\n",
           g_headless ? ", headless" : "");

    if (!g_headless) initWindow();
    initVulkanAndSimulation();
    // After the UI exists: the theme is read while the hub is constructed, and
    // the pointer it names belongs on the window that was just made.
    installThemeCursor();

    // KS_MENU_SCREEN=<page>: open a menu page before the capture frames run, so
    // the screens behind the tiles can be judged without driving the window by
    // hand. An unknown name leaves rather than writing a capture of the wrong
    // page under the requested file name.
    if (const char* screenEnv = std::getenv("KS_MENU_SCREEN"); screenEnv && screenEnv[0]) {
        std::string name(screenEnv);
        for (char& c : name)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        struct Page { const char* name; ks::sim::MenuState state; };
        static const Page kPages[] = {
            {"MAIN", ks::sim::MenuState::Main},
            {"SINGLE PLAYER", ks::sim::MenuState::Singleplayer},
            {"SINGLEPLAYER", ks::sim::MenuState::Singleplayer},
            {"MULTIPLAYER", ks::sim::MenuState::Multiplayer},
            {"SETTINGS", ks::sim::MenuState::Settings},
            {"VIDEO", ks::sim::MenuState::Options},
            {"CONTENT", ks::sim::MenuState::ContentManager},
            // The showroom list under CONTENT, so a capture can show the mark
            // on the current default without driving the window by hand.
            {"SHOWROOMS", ks::sim::MenuState::ShowroomSelect},
            // The skin list of the loaded car, under CONTENT as well.
            {"SKINS", ks::sim::MenuState::Skins},
            // The two driver-gear libraries, listed from the content tree.
            {"SUITS", ks::sim::MenuState::Suits},
            {"HELMETS", ks::sim::MenuState::Helmets},
            // The page calls itself DRIVER, like its tile: both names work, so
            // a capture is never lost to the section's two spellings.
            {"DRIVER", ks::sim::MenuState::Profile},
            {"PROFILE", ks::sim::MenuState::Profile},
            {"CREDITS", ks::sim::MenuState::Credits},
            // The track list scans content/tracks on the way in, so it goes
            // through its own opener instead of a bare page switch.
            {"TRACKS", ks::sim::MenuState::TrackSelect},
            // The weather step is normally reached by choosing a track; the
            // name opens it on its dry default for a capture.
            {"WEATHER", ks::sim::MenuState::TrackWeather},
            // The in-car pause bar (ESC over the session), the screen that wears
            // the header bar as well as its own: a capture of it used to need a
            // hand on the keyboard mid-run.
            {"PAUSE", ks::sim::MenuState::Pause},
            // The walk's first step and the wizard it can follow: both build
            // their list as they open, so they go through their openers too.
            {"CARS", ks::sim::MenuState::CarSelect},
            {"CAR DATA", ks::sim::MenuState::CarData},
            {"FIRST RUN", ks::sim::MenuState::FirstRun},
        };
        bool opened = false;
        for (const Page& page : kPages) {
            if (name != page.name) continue;
            if (page.state == ks::sim::MenuState::TrackSelect)
                g_simulation->ui().menu().openTrackSelect();
            else if (page.state == ks::sim::MenuState::CarSelect)
                g_simulation->ui().menu().beginSessionSetup();
            else if (page.state == ks::sim::MenuState::FirstRun)
                g_simulation->ui().menu().openFirstRun();
            else
                g_simulation->ui().menu().openScreen(page.state);
            printf("[menu] KS_MENU_SCREEN=%s -> %s\n", screenEnv,
                   g_simulation->ui().menu().sectionTitle().c_str());
            opened = true;
            break;
        }
        if (!opened) {
            printf("[menu] KS_MENU_SCREEN=%s: unknown page\n", screenEnv);
            std::exit(2);
        }
    }
    // KS_MENU_WEATHER=<DRY|CUSTOM|TODAY|DATE>: the sky the WEATHER page shows
    // in a capture, on a track from the library so the real modes have
    // coordinates to ask about. KS_TRACK names the track; the first one stands
    // in when it does not.
    if (const char* skyEnv = std::getenv("KS_MENU_WEATHER"); skyEnv && skyEnv[0]) {
        std::string sky(skyEnv);
        for (char& c : sky)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        ks::sim::WeatherSource source = ks::sim::WeatherSource::Dry;
        int dayOffset = 0;
        bool known = true;
        if (sky == "DRY") source = ks::sim::WeatherSource::Dry;
        else if (sky == "CUSTOM") source = ks::sim::WeatherSource::Custom;
        else if (sky == "TODAY") source = ks::sim::WeatherSource::Today;
        else if (sky == "DATE") {
            source = ks::sim::WeatherSource::RealDate;
            dayOffset = -4; // a past day has a reading, not a forecast
        } else {
            known = false;
        }
        if (!known) {
            printf("[menu] KS_MENU_WEATHER=%s: unknown sky\n", skyEnv);
        } else {
            std::string trackName;
            if (const char* trackPath = std::getenv("KS_TRACK"); trackPath && trackPath[0]) {
                trackName = trackPath;
                const std::string::size_type cut = trackName.find_last_of("/\\");
                if (cut != std::string::npos) trackName = trackName.substr(cut + 1);
            }
            auto& menu = g_simulation->ui().menu();
            if (menu.openWeatherPreview(trackName, source, dayOffset))
                printf("[menu] KS_MENU_WEATHER=%s -> %s on %s\n", skyEnv,
                       ks::sim::weatherKindName(menu.weather().kind),
                       menu.trackName().c_str());
            else
                printf("[menu] KS_MENU_WEATHER=%s: no tracks in the library\n", skyEnv);
        }
    }
    // KS_MENU_PLANNER=1: open the session planner card in a capture. With
    // KS_AUTOSTART and KS_SESSION the card describes a session that is really
    // running; on its own it is the card of the setup the menu holds.
    if (const char* plannerEnv = std::getenv("KS_MENU_PLANNER");
        plannerEnv && plannerEnv[0] == '1') {
        auto& menu = g_simulation->ui().menu();
        menu.setVisible(true);
        menu.setSessionActive(true);
        if (!menu.sessionPlannerOpen()) menu.toggleSessionPlanner();
        printf("[menu] KS_MENU_PLANNER=1 -> session planner card\n");
    }

    // KS_MENU_PLAN_CYCLE=<CAR|TRACK>: press that row of the session planner
    // before the capture. A click there restarts the session, which is what
    // the log then shows without a hand on the mouse.
    if (const char* cycleEnv = std::getenv("KS_MENU_PLAN_CYCLE"); cycleEnv && cycleEnv[0]) {
        std::string what(cycleEnv);
        for (char& c : what)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        int field = -1;
        if (what == "CAR") field = ks::sim::GameMenuOverlay::PlanCar;
        else if (what == "TRACK") field = ks::sim::GameMenuOverlay::PlanTrack;
        if (field < 0) {
            printf("[menu] KS_MENU_PLAN_CYCLE=%s: unknown row\n", cycleEnv);
        } else {
            const bool cycled = g_simulation->ui().menu().cycleSessionPlanField(field);
            printf("[menu] KS_MENU_PLAN_CYCLE=%s -> %s\n", cycleEnv,
                   cycled ? "cycled" : "no content to cycle");
        }
    }

    // KS_PROFILE_NAME / KS_PROFILE_NATIONALITY: fill the driver page before the
    // capture, so a long name or a chosen country can be judged on screen - the
    // defaults never show either.
    if (const char* nameEnv = std::getenv("KS_PROFILE_NAME"); nameEnv && nameEnv[0])
        g_simulation->ui().menu().setProfileField("NAME", nameEnv);
    if (const char* natEnv = std::getenv("KS_PROFILE_NATIONALITY"); natEnv && natEnv[0])
        g_simulation->ui().menu().setProfileField("NATIONALITY", natEnv);
    // KS_MENU_SELECT=<row>: put the selection further down a page, so a capture
    // can show what a list does once it scrolls - the rows hidden above it and
    // the caption that says so.
    if (const char* selectEnv = std::getenv("KS_MENU_SELECT"); selectEnv && selectEnv[0]) {
        const int count = static_cast<int>(g_simulation->ui().menu().items().size());
        if (count <= 0) {
            printf("[menu] KS_MENU_SELECT=%s: the page has no rows\n", selectEnv);
        } else {
            int row = std::atoi(selectEnv);
            if (row < 0) row = 0;
            if (row > count - 1) row = count - 1;
            g_simulation->ui().menu().setSelectedIndex(row);
            printf("[menu] selected row %d of %d on %s\n", row, count,
                   g_simulation->ui().menu().sectionTitle().c_str());
        }
    }

    // KS_HIDE_MENU=1: leave the menu overlay out, so a capture shows the scene
    // (the showroom turntable and the car on it) instead of the tiles over it.
    if (const char* hide = std::getenv("KS_HIDE_MENU");
        hide != nullptr && hide[0] && std::strcmp(hide, "0") != 0) {
        g_simulation->ui().menu().setVisible(false);
        std::puts("[menu] overlay hidden by KS_HIDE_MENU");
    }

    // KS_CONSOLE=1: the showroom with an empty stage and the developer console
    // already up, where TEMPERATURE / BRIGHTNESS / CONTRAST tune the display
    // live. KS_CONSOLE_CMD="TEMPERATURE 4500; CONTRAST 1.15" runs lines at
    // start-up, so a captured frame can show a tuned room without the window
    // being driven by hand.
    if (const char* consoleEnv = std::getenv("KS_CONSOLE");
        consoleEnv != nullptr && consoleEnv[0] && consoleEnv[0] != '0') {
        g_simulation->ui().console().setVisible(true);
        g_simulation->ui().console().startTyping();
        std::puts("[console] open: TEMPERATURE / BRIGHTNESS / CONTRAST tune the display");
    }
    // KS_SETUP=1: the car's setup screen already up, so a capture shows it
    // without the window being driven by hand.
    if (const char* setupEnv = std::getenv("KS_SETUP");
        setupEnv != nullptr && setupEnv[0] && setupEnv[0] != '0') {
        if (auto* garage = g_simulation->setupGarage()) garage->setVisible(true);
        const std::string tab = setupEnv;
        if (tab != "1") g_simulation->setupGarage()->selectTabByName(tab);
        std::puts("[setup] screen open");
    }
    // KS_SETUP_SYMMETRIC=0: open with the checkbox off, so a capture can show
    // the per-corner mode as well as the symmetric one.
    if (const char* symEnv = std::getenv("KS_SETUP_SYMMETRIC");
        symEnv != nullptr && symEnv[0] && g_simulation->setupGarage() != nullptr) {
        g_simulation->setupGarage()->setSymmetric(std::strcmp(symEnv, "0") != 0);
    }
    // KS_SETUP_EDIT="ARB_FRONT=34000;FUEL=30": rows moved before the first
    // frame, so a capture shows the screen with its offsets from the car.
    if (const char* editEnv = std::getenv("KS_SETUP_EDIT");
        editEnv != nullptr && editEnv[0] && g_simulation->setupGarage() != nullptr) {
        const std::string lines(editEnv);
        std::size_t start = 0;
        while (start <= lines.size()) {
            const std::size_t end = lines.find(';', start);
            const std::string line =
                lines.substr(start, end == std::string::npos ? std::string::npos
                                                             : end - start);
            const std::size_t eq = line.find('=');
            if (eq != std::string::npos) {
                const std::string key = line.substr(0, eq);
                const float value = static_cast<float>(std::atof(line.c_str() + eq + 1));
                if (g_simulation->setupGarage()->setRowValue(key, value))
                    std::printf("[setup] %s = %.2f\n", key.c_str(),
                                static_cast<double>(value));
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    if (const char* cmdEnv = std::getenv("KS_CONSOLE_CMD"); cmdEnv != nullptr && cmdEnv[0]) {
        // The lines run whether or not the panel is up: KS_CONSOLE decides
        // that, so a capture of a tuned room is not covered by the overlay.
        const std::string lines(cmdEnv);
        std::size_t start = 0;
        while (start <= lines.size()) {
            const std::size_t end = lines.find(';', start);
            const std::string line =
                lines.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!line.empty()) g_simulation->ui().console().execute(line);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    printf("KinetiKa Simulator started (%s)\n",
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
        // Roadmap 4.x — photo mode screenshot: capture the current frame
        // with the active ppfilter LUT applied, at the window's resolution.
        if (g_photoMode && g_nativeRenderer && g_nativeRenderer->screenshotReady() && !g_shotRequested) {
            g_shotRequested = true;
            // Use a photo-mode specific path if KS_SCREENSHOT is also set,
            // otherwise just log and leave the file next to the exe.
            std::string photoPath = g_shotPath.empty() ?
                "screenshot_photo.png" : g_shotPath;
            // The readback is already in sRGB; we apply the preset's LUT
            // strength/exposure in the shader, but here we just save the
            // display-referred pixels as-is (the LUT was already applied
            // during the frame).  Save at window extents.
            ks::image::RawImage img;
            img.resize(g_nativeRenderer->extent().width,
                       g_nativeRenderer->extent().height);
            const auto& px = g_nativeRenderer->screenshotPixels();
            for (size_t i = 0; i + 3 < px.size(); i += 4) {
                img.rgba[i] = px[i + 2];   // R
                img.rgba[i + 1] = px[i + 1]; // G
                img.rgba[i + 2] = px[i];   // B
                img.rgba[i + 3] = 255;     // A
            }
            std::string err;
            if (ks::image::saveImageFile(photoPath, img, &err)) {
                printf("[photo] saved %s (%ux%u)\n", photoPath.c_str(),
                       img.width, img.height);
            } else {
                printf("[photo] save failed: %s\n", err.c_str());
            }
            // Exit photo mode after the shot
            g_photoMode = false;
            g_simulation->ui().dashboard().setVisible(true);
            g_simulation->ui().menu().setVisible(true);
            g_simulation->setPhotoOrbit(false); // back to the view it had
            g_simulation->start(); // resume simulation
        }

        if (g_headless && g_headlessSeconds > 0.0) {
            g_headlessElapsed += elapsed;
            if (g_headlessElapsed >= g_headlessSeconds) {
                printf("[MAIN] KS_HEADLESS_SECONDS=%.1f reached -> exiting\n",
                       g_headlessSeconds);
                g_running = false;
            }
        }

        // Roadmap 2.14 — wall time of the run (reported on exit) and the
        // timed profile exit: leave through the normal teardown so the dump
        // below sees the resolved GPU frame too.
        g_profileElapsed += elapsed;
        if (g_profileSeconds > 0.0 && g_profileElapsed >= g_profileSeconds) {
            printf("[MAIN] KS_PROFILE_SECONDS=%.1f reached -> exiting\n",
                   g_profileSeconds);
            g_running = false;
        }

        Sleep(1);
    }

    // Roadmap 2.14 — gate evidence on the way out: KS_PROFILE=1 (the same
    // switch that enables the tick scopes and the GPU markers) prints the
    // frame profile, KS_PROFILE_CSV=<path> additionally writes it as CSV.
    // Both read the one PhysicsProfiler instance behind ksengine.dll, so the
    // dump shows what the report/C API would show for this run.
    if (const char* sp = std::getenv("KS_PROFILE");
        sp && sp[0] && std::strcmp(sp, "0") != 0) {
        auto& prof = ks::physics::PhysicsProfiler::instance();
        std::fprintf(stderr, "[PROFILE] run wall time %.1f s\n", g_profileElapsed);
        prof.report(stderr);
        // KS_PROFILE_REPORT=<path>: same report in a file — stdout/stderr of
        // a WIN32-subsystem build are not reliably capturable by the parent.
        if (const char* rp = std::getenv("KS_PROFILE_REPORT"); rp && rp[0]) {
            if (FILE* rf = std::fopen(rp, "w")) {
                prof.report(rf);
                std::fclose(rf);
                printf("[PROFILE] report -> %s\n", rp);
            } else {
                printf("[PROFILE] report FAILED: %s\n", rp);
            }
        }
        if (const char* csv = std::getenv("KS_PROFILE_CSV"); csv && csv[0]) {
            if (prof.exportCsv(csv))
                printf("[PROFILE] csv -> %s\n", csv);
            else
                printf("[PROFILE] csv FAILED: %s\n", csv);
        }
    }

    // Deterministic teardown: destroy the simulation (and its borrowed
    // singletons: Engine, SceneModule, ScriptModule, Input/Render) while the
    // function-local statics they point into are still alive. If these ran
    // as CRT static destructors instead, Engine::instance() (constructed
    // later than g_simulation, hence destroyed earlier) would already be
    // gone when ~SimulationLoop touched m_modules -> access violation.
    // Close the discovery socket + control API before the loop goes away.
    if (g_simulation) g_simulation->features().stopServices();
    // Persist the per-player files while the simulation still owns the
    // profile and the PB store: stats.json (career record + PB record count)
    // and settings.json (only the keys the player's own file carried — see
    // EngineSettings::clearPresence() above).
    if (g_simulation) {
        ks::sim::saveDriverStats(
            g_userLayout.stats,
            ks::sim::driverStatsFromProfile(
                g_simulation->ui().menu().profile(),
                static_cast<int>(g_simulation->features().pb.size())));
        // What the driver left in the car and on screen follows him into the
        // next session: the cockpit knobs and the two panel positions.
        if (auto* vehicle = g_simulation->vehicle()) {
            g_engineSettings.cockpitTcLevel = vehicle->tcLevel();
            g_engineSettings.cockpitAbsLevel = vehicle->absLevel();
            g_engineSettings.cockpitBrakeBiasPct =
                static_cast<int>(vehicle->brakeBias() * 100.f + 0.5f);
            g_engineSettings.hasCockpitTcLevel = true;
            g_engineSettings.hasCockpitAbsLevel = true;
            g_engineSettings.hasCockpitBrakeBiasPct = true;
        }
        {
            const auto offsets = g_simulation->ui().overlayOffsets();
            g_engineSettings.uiHdashX = offsets.hdashX;
            g_engineSettings.uiHdashY = offsets.hdashY;
            g_engineSettings.uiInputsX = offsets.inputsX;
            g_engineSettings.uiInputsY = offsets.inputsY;
            g_engineSettings.hasUiPanelOffsets = true;
        }
        g_engineSettings.save(g_userLayout.settings);
        // driver.json follows the same rule: the profile the menu carries is
        // what the next launch reads, completion flag included.
        if (!g_driverProfilePath.empty()) {
            ks::sim::saveDriverProfile(g_driverProfilePath,
                                       g_simulation->ui().menu().profile(),
                                       g_simulation->ui().menu().setupComplete());
        }
    }
    g_simulation.reset();
    g_localShadowAtlas.shutdown();
    g_shadowMap.shutdown();
    delete g_nativeRenderer;
    g_nativeRenderer = nullptr;
    if (g_surface) vkDestroySurfaceKHR(g_vkInstance, g_surface, nullptr);
    if (g_vkInstance) vkDestroyInstance(g_vkInstance, nullptr);

    // Modules outlive the session on purpose (they load before it and unload
    // after it): their shutdown runs here, in reverse load order.
    g_nativeModules.unloadAll();

    return 0;
}
