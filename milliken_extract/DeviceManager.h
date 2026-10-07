#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace ks::device {

class TripleMonitorManager;
class RacingInputManager;
class XrManager;

/**
 * Central hub for input/display devices — Qt-free.
 *
 * Coordinates triple monitor, VR, wheels, pedals, FFB.
 * Events use std::function callbacks (no QObject/signals).
 *
 * Usage:
 *   auto* dm = DeviceManager::instance();
 *   dm->initialize();
 *   dm->setDeviceStatusCallback([](const DeviceManager::DeviceStatus& s){ ... });
 */
class DeviceManager {
public:
    static DeviceManager* instance();
    static DeviceManager* createInstance();
    static void destroyInstance();

    DeviceManager();
    ~DeviceManager();

    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    // Lifecycle
    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    // Sub-managers (owned)
    TripleMonitorManager* tripleMonitor() const { return m_tripleMonitor.get(); }
    RacingInputManager* racingInput() const { return m_racingInput.get(); }
    XrManager* vr() const { return m_vr.get(); }

    enum class RenderMode : uint8_t {
        SingleMonitor = 0,
        TripleMonitor,
        VR
    };

    void setRenderMode(RenderMode mode);
    RenderMode renderMode() const;
    bool isVRActive() const;
    bool isTripleActive() const;

    struct DeviceStatus {
        bool tripleMonitorConnected = false;
        int monitorCount = 0;
        bool vrConnected = false;
        bool vrSessionRunning = false;
        bool steeringWheelConnected = false;
        std::string activeDeviceName;
    };

    DeviceStatus status() const;

    /** Poll and emit status if a callback is set (replaces QTimer). */
    void pollStatus();

    // Profiles (paths are filesystem UTF-8 / narrow strings)
    void loadProfiles(const std::string& basePath);
    void saveProfiles(const std::string& basePath) const;

    // Callbacks (replace Qt signals) — thread-safe setters
    void setRenderModeCallback(std::function<void(RenderMode)> cb);
    void setDeviceStatusCallback(std::function<void(const DeviceStatus&)> cb);
    void setErrorCallback(std::function<void(const std::string&)> cb);

private:
    void onMonitorChanged();
    void onVRSessionChanged(bool running);
    void onInputDeviceChanged(int index);
    void notifyStatus();
    void notifyError(const std::string& msg);

    std::unique_ptr<TripleMonitorManager> m_tripleMonitor;
    std::unique_ptr<RacingInputManager> m_racingInput;
    std::unique_ptr<XrManager> m_vr;

    RenderMode m_renderMode = RenderMode::SingleMonitor;
    bool m_initialized = false;

    mutable std::mutex m_mutex;
    std::function<void(RenderMode)> m_onRenderModeChanged;
    std::function<void(const DeviceStatus&)> m_onDeviceStatusChanged;
    std::function<void(const std::string&)> m_onError;

    static DeviceManager* s_instance;
};

} // namespace ks::device
