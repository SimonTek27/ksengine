#include "DeviceManager.h"

#include "simracing/SimRacingDevices.h"
#include "vr/XrManager.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ks::device {

DeviceManager* DeviceManager::s_instance = nullptr;

DeviceManager* DeviceManager::instance()
{
    return s_instance;
}

DeviceManager* DeviceManager::createInstance()
{
    if (!s_instance)
        s_instance = new DeviceManager();
    return s_instance;
}

void DeviceManager::destroyInstance()
{
    delete s_instance;
    s_instance = nullptr;
}

DeviceManager::DeviceManager()
{
    if (!s_instance)
        s_instance = this;

    // TripleMonitor, RacingInput, XrManager: Qt-free.
    m_tripleMonitor = std::make_unique<TripleMonitorManager>();
    m_racingInput = std::make_unique<RacingInputManager>();
    m_vr = std::make_unique<XrManager>();

    m_racingInput->onDeviceSelected = [this](int index) {
        onInputDeviceChanged(index);
    };
    m_tripleMonitor->onMonitorsChanged = [this]() { onMonitorChanged(); };
    m_tripleMonitor->onConfigChanged = [this]() { notifyStatus(); };
    m_vr->onSessionRunningChanged = [this](bool running) { onVRSessionChanged(running); };
    m_vr->onError = [this](const std::string& msg) { notifyError(msg); };
}

DeviceManager::~DeviceManager()
{
    shutdown();
    if (s_instance == this)
        s_instance = nullptr;
}

bool DeviceManager::initialize()
{
    if (m_initialized)
        return true;

    std::fprintf(stderr, "DeviceManager: Initializing...\n");

    m_tripleMonitor->detectMonitors();
    m_racingInput->initialize();

    m_initialized = true;

    std::fprintf(stderr, "DeviceManager: Initialized - %d monitors, %d input devices\n",
                 m_tripleMonitor->monitorCount(),
                 m_racingInput->deviceCount());

    notifyStatus();
    return true;
}

void DeviceManager::shutdown()
{
    if (!m_initialized)
        return;

    m_racingInput->shutdown();

    if (m_vr->isInitialized())
        m_vr->shutdown();

    m_initialized = false;
    std::fprintf(stderr, "DeviceManager: Shutdown complete\n");
}

void DeviceManager::setRenderMode(RenderMode mode)
{
    std::function<void(RenderMode)> cb;
    {
        std::lock_guard lock(m_mutex);
        if (m_renderMode == mode)
            return;

        if (mode == RenderMode::VR && !m_vr->isInitialized()) {
            std::fprintf(stderr, "DeviceManager: VR not available, falling back to single monitor\n");
            mode = RenderMode::SingleMonitor;
            if (m_renderMode == mode)
                return;
        }

        m_renderMode = mode;
        cb = m_onRenderModeChanged;
    }

    if (cb)
        cb(mode);

    std::fprintf(stderr, "DeviceManager: Render mode changed to %d\n", static_cast<int>(mode));
    notifyStatus();
}

DeviceManager::RenderMode DeviceManager::renderMode() const
{
    std::lock_guard lock(m_mutex);
    return m_renderMode;
}

bool DeviceManager::isVRActive() const
{
    std::lock_guard lock(m_mutex);
    return m_renderMode == RenderMode::VR && m_vr->isSessionRunning();
}

bool DeviceManager::isTripleActive() const
{
    std::lock_guard lock(m_mutex);
    return m_renderMode == RenderMode::TripleMonitor &&
           m_tripleMonitor->monitorCount() >= 3;
}

DeviceManager::DeviceStatus DeviceManager::status() const
{
    DeviceStatus s;
    s.tripleMonitorConnected = m_tripleMonitor->monitorCount() >= 3;
    s.monitorCount = m_tripleMonitor->monitorCount();
    s.vrConnected = m_vr->isInitialized();
    s.vrSessionRunning = m_vr->isSessionRunning();
    s.steeringWheelConnected = m_racingInput->deviceCount() > 0;

    if (m_racingInput->selectedDevice() >= 0) {
        s.activeDeviceName = m_racingInput->device(m_racingInput->selectedDevice()).name;
    }

    return s;
}

void DeviceManager::pollStatus()
{
    notifyStatus();
}

void DeviceManager::loadProfiles(const std::string& basePath)
{
    std::error_code ec;
    fs::create_directories(basePath, ec);

    const fs::path dir(basePath);
    const fs::path inputProfile = dir / "input.ini";
    if (fs::exists(inputProfile)) {
        m_racingInput->loadProfile(inputProfile.string());
        std::fprintf(stderr, "DeviceManager: Loaded input profile from %s\n",
                     inputProfile.string().c_str());
    }

    const fs::path monitorProfile = dir / "monitors.ini";
    if (fs::exists(monitorProfile)) {
        TripleMonitorConfig config;
        config.load(monitorProfile.string());
        m_tripleMonitor->setConfig(config);
        std::fprintf(stderr, "DeviceManager: Loaded monitor profile from %s\n",
                     monitorProfile.string().c_str());
    }
}

void DeviceManager::saveProfiles(const std::string& basePath) const
{
    std::error_code ec;
    fs::create_directories(basePath, ec);

    const fs::path dir(basePath);
    m_racingInput->saveProfile((dir / "input.ini").string());
    m_tripleMonitor->config().save((dir / "monitors.ini").string());

    std::fprintf(stderr, "DeviceManager: Saved profiles to %s\n", basePath.c_str());
}

void DeviceManager::setRenderModeCallback(std::function<void(RenderMode)> cb)
{
    std::lock_guard lock(m_mutex);
    m_onRenderModeChanged = std::move(cb);
}

void DeviceManager::setDeviceStatusCallback(std::function<void(const DeviceStatus&)> cb)
{
    std::lock_guard lock(m_mutex);
    m_onDeviceStatusChanged = std::move(cb);
}

void DeviceManager::setErrorCallback(std::function<void(const std::string&)> cb)
{
    std::lock_guard lock(m_mutex);
    m_onError = std::move(cb);
}

void DeviceManager::onMonitorChanged()
{
    std::fprintf(stderr, "DeviceManager: Monitor configuration changed - %d monitors\n",
                 m_tripleMonitor->monitorCount());
    notifyStatus();
}

void DeviceManager::onVRSessionChanged(bool running)
{
    std::fprintf(stderr, "DeviceManager: VR session %s\n", running ? "started" : "stopped");
    notifyStatus();
}

void DeviceManager::onInputDeviceChanged(int index)
{
    if (index >= 0 && index < m_racingInput->deviceCount()) {
        std::fprintf(stderr, "DeviceManager: Input device changed to %s\n",
                     m_racingInput->device(index).name.c_str());
    }
    notifyStatus();
}

void DeviceManager::notifyStatus()
{
    std::function<void(const DeviceStatus&)> cb;
    DeviceStatus s;
    {
        std::lock_guard lock(m_mutex);
        cb = m_onDeviceStatusChanged;
    }
    s = status();
    if (cb)
        cb(s);
}

void DeviceManager::notifyError(const std::string& msg)
{
    std::function<void(const std::string&)> cb;
    {
        std::lock_guard lock(m_mutex);
        cb = m_onError;
    }
    if (cb)
        cb(msg);
    else
        std::fprintf(stderr, "DeviceManager error: %s\n", msg.c_str());
}

} // namespace ks::device
