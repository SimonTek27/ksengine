#include "SimRacingDevices.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <algorithm>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace ks::device {

// ============================================================================
// SimpleIni — shared by TripleMonitorConfig and RacingInputManager profiles
// ============================================================================

namespace {

class SimpleIni {
public:
    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        m_values.clear();
        std::string line, group;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']') {
                group = line.substr(1, line.size() - 2);
                continue;
            }
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = group.empty() ? line.substr(0, eq) : group + "/" + line.substr(0, eq);
            m_values[key] = line.substr(eq + 1);
        }
        return true;
    }

    bool save(const std::string& path) const {
        std::ofstream f(path, std::ios::trunc);
        if (!f.is_open()) return false;
        std::string currentGroup;
        // Stable order: group then key
        std::vector<std::pair<std::string, std::string>> items(m_values.begin(), m_values.end());
        std::sort(items.begin(), items.end());
        for (const auto& [key, value] : items) {
            size_t slash = key.find('/');
            std::string group = slash == std::string::npos ? "" : key.substr(0, slash);
            std::string name = slash == std::string::npos ? key : key.substr(slash + 1);
            if (group != currentGroup) {
                f << "[" << group << "]\n";
                currentGroup = group;
            }
            f << name << "=" << value << "\n";
        }
        return true;
    }

    void setValue(const std::string& group, const std::string& key, const std::string& v) {
        m_values[group.empty() ? key : group + "/" + key] = v;
    }
    void setValue(const std::string& group, const std::string& key, float v) {
        setValue(group, key, std::to_string(v));
    }
    void setValue(const std::string& group, const std::string& key, int v) {
        setValue(group, key, std::to_string(v));
    }
    void setValue(const std::string& group, const std::string& key, bool v) {
        setValue(group, key, v ? "true" : "false");
    }

    std::string value(const std::string& group, const std::string& key, const std::string& def = {}) const {
        const std::string k = group.empty() ? key : group + "/" + key;
        auto it = m_values.find(k);
        return it == m_values.end() ? def : it->second;
    }
    int valueInt(const std::string& group, const std::string& key, int def) const {
        auto s = value(group, key);
        if (s.empty()) return def;
        try { return std::stoi(s); } catch (...) { return def; }
    }
    float valueFloat(const std::string& group, const std::string& key, float def) const {
        auto s = value(group, key);
        if (s.empty()) return def;
        try { return std::stof(s); } catch (...) { return def; }
    }
    bool valueBool(const std::string& group, const std::string& key, bool def) const {
        auto s = value(group, key);
        if (s.empty()) return def;
        return s == "1" || s == "true" || s == "True" || s == "TRUE";
    }

private:
    std::map<std::string, std::string> m_values;
};

#ifdef _WIN32
struct EnumCtx {
    std::vector<MonitorInfo>* out;
    int index;
};

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return TRUE;

    MonitorInfo info;
    info.index = ctx->index++;

    // Device name
    char nameA[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, nameA, sizeof(nameA) - 1, nullptr, nullptr);
    info.name = nameA;

    info.geometry = IntRect::fromXYWH(
        mi.rcMonitor.left, mi.rcMonitor.top,
        mi.rcMonitor.right - mi.rcMonitor.left,
        mi.rcMonitor.bottom - mi.rcMonitor.top);
    info.availableGeometry = IntRect::fromXYWH(
        mi.rcWork.left, mi.rcWork.top,
        mi.rcWork.right - mi.rcWork.left,
        mi.rcWork.bottom - mi.rcWork.top);
    info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    // DPI
    UINT dpiX = 96, dpiY = 96;
    // GetDpiForMonitor requires shcore — fall back to 96 if unavailable
    info.dpi = static_cast<float>(dpiX);

    // Physical size estimate from mm via DEVMODE if possible
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
        // Approximate mm from assumed 96 dpi if not provided
        if (dm.dmPelsWidth > 0)
            info.physicalWidthMm = (info.geometry.w / info.dpi) * 25.4f;
        if (dm.dmPelsHeight > 0)
            info.physicalHeightMm = (info.geometry.h / info.dpi) * 25.4f;
    }

    ctx->out->push_back(info);
    std::fprintf(stderr, "Monitor %d: %s %dx%d%s\n",
                 info.index, info.name.c_str(),
                 info.geometry.w, info.geometry.h,
                 info.isPrimary ? " (primary)" : "");
    return TRUE;
}
#endif

} // anonymous namespace

// ============================================================================
// TripleMonitorConfig load/save
// ============================================================================

void TripleMonitorConfig::load(const std::string& path) {
    if (path.empty()) return;
    SimpleIni ini;
    if (!ini.load(path)) return;
    enabled = ini.valueBool("TripleMonitor", "enabled", enabled);
    centerMonitorIndex = ini.valueInt("TripleMonitor", "centerMonitorIndex", centerMonitorIndex);
    bezelCompensationMm = ini.valueFloat("TripleMonitor", "bezelCompensationMm", bezelCompensationMm);
    fovHorizontal = ini.valueFloat("TripleMonitor", "fovHorizontal", fovHorizontal);
    eyeDistance = ini.valueFloat("TripleMonitor", "eyeDistance", eyeDistance);
    verticalSync = ini.valueBool("TripleMonitor", "verticalSync", verticalSync);
    targetFps = ini.valueInt("TripleMonitor", "targetFps", targetFps);
    arrangement = static_cast<Arrangement>(ini.valueInt("TripleMonitor", "arrangement", 0));
}

void TripleMonitorConfig::save(const std::string& path) const {
    if (path.empty()) return;
    SimpleIni ini;
    ini.load(path); // preserve other groups if file exists
    ini.setValue("TripleMonitor", "enabled", enabled);
    ini.setValue("TripleMonitor", "centerMonitorIndex", centerMonitorIndex);
    ini.setValue("TripleMonitor", "bezelCompensationMm", bezelCompensationMm);
    ini.setValue("TripleMonitor", "fovHorizontal", fovHorizontal);
    ini.setValue("TripleMonitor", "eyeDistance", eyeDistance);
    ini.setValue("TripleMonitor", "verticalSync", verticalSync);
    ini.setValue("TripleMonitor", "targetFps", targetFps);
    ini.setValue("TripleMonitor", "arrangement", static_cast<int>(arrangement));
    ini.save(path);
}

// ============================================================================
// TripleMonitorManager
// ============================================================================

TripleMonitorManager::TripleMonitorManager() {
    detectMonitors();
}

TripleMonitorManager::~TripleMonitorManager() = default;

void TripleMonitorManager::detectMonitors()
{
    m_monitors.clear();

#ifdef _WIN32
    EnumCtx ctx{&m_monitors, 0};
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&ctx));
#else
    // Fallback: one virtual 1920x1080 primary monitor
    MonitorInfo info;
    info.index = 0;
    info.name = "Virtual-0";
    info.geometry = IntRect::fromXYWH(0, 0, 1920, 1080);
    info.availableGeometry = info.geometry;
    info.dpi = 96.0f;
    info.isPrimary = true;
    info.physicalWidthMm = 527.0f;
    info.physicalHeightMm = 296.0f;
    m_monitors.push_back(info);
    std::fprintf(stderr, "Monitor 0: Virtual-0 1920x1080 (primary)\n");
#endif

    if (onMonitorsChanged)
        onMonitorsChanged();
}

void TripleMonitorManager::recalculate()
{
    if (m_monitors.size() < 3) {
        std::fprintf(stderr, "TripleMonitor: Less than 3 monitors detected (%zu)\n", m_monitors.size());
        m_leftEye = m_centerEye = m_rightEye = {};
        return;
    }

    calculateEyeProjections();

    int center = m_config.centerMonitorIndex;
    const int n = static_cast<int>(m_monitors.size());
    if (center < 0 || center >= n) {
        center = -1;
        for (int i = 0; i < n; ++i) {
            if (m_monitors[static_cast<size_t>(i)].isPrimary) { center = i; break; }
        }
        if (center < 0) center = (n >= 3) ? 1 : 0;
    }

    auto clampIdx = [n](int i) { return std::max(0, std::min(n - 1, i)); };

    IntRect leftGeo, centerGeo, rightGeo;
    switch (m_config.arrangement) {
        case TripleMonitorConfig::Arrangement::Horizontal:
        default:
            leftGeo   = m_monitors[static_cast<size_t>(clampIdx(center - 1))].geometry;
            centerGeo = m_monitors[static_cast<size_t>(center)].geometry;
            rightGeo  = m_monitors[static_cast<size_t>(clampIdx(center + 1))].geometry;
            break;
    }

    const int minX = std::min(leftGeo.x, std::min(centerGeo.x, rightGeo.x));
    const int minY = std::min(leftGeo.y, std::min(centerGeo.y, rightGeo.y));
    const int totalW = leftGeo.w + centerGeo.w + rightGeo.w;
    const int maxH = std::max(leftGeo.h, std::max(centerGeo.h, rightGeo.h));
    m_combinedViewport = IntRect::fromXYWH(minX, minY, totalW, maxH);

    m_leftEye.viewport   = IntRect::fromXYWH(0, 0, leftGeo.w, leftGeo.h);
    m_centerEye.viewport = IntRect::fromXYWH(leftGeo.w, 0, centerGeo.w, centerGeo.h);
    m_rightEye.viewport  = IntRect::fromXYWH(leftGeo.w + centerGeo.w, 0, rightGeo.w, rightGeo.h);

    if (onConfigChanged)
        onConfigChanged();
}

void TripleMonitorManager::updateView(const Mat4& baseView)
{
    m_baseView = baseView;
    recalculate();
}

void TripleMonitorManager::calculateEyeProjections()
{
    const float totalFov = m_config.fovHorizontal;
    const float perEyeFov = totalFov / 3.0f;
    const float aspectPerMonitor = 16.0f / 9.0f;
    const float bezelOffsetDeg = calculateBezelOffsetDeg(0);

    m_centerEye.fovOffsetDeg = 0.0f;
    m_centerEye.projection = Mat4::perspective(perEyeFov, aspectPerMonitor, 0.1f, 1000.0f);
    m_centerEye.view = m_baseView;

    m_leftEye.fovOffsetDeg = -(perEyeFov + bezelOffsetDeg);
    Mat4 leftRot = Mat4::identity();
    leftRot.rotate(-(perEyeFov + bezelOffsetDeg), 0, 1, 0);
    m_leftEye.projection = Mat4::perspective(perEyeFov, aspectPerMonitor, 0.1f, 1000.0f);
    m_leftEye.view = leftRot * m_baseView;

    m_rightEye.fovOffsetDeg = (perEyeFov + bezelOffsetDeg);
    Mat4 rightRot = Mat4::identity();
    rightRot.rotate((perEyeFov + bezelOffsetDeg), 0, 1, 0);
    m_rightEye.projection = Mat4::perspective(perEyeFov, aspectPerMonitor, 0.1f, 1000.0f);
    m_rightEye.view = rightRot * m_baseView;
}

float TripleMonitorManager::calculatePhysicalFovDeg() const
{
    if (m_monitors.empty()) return 60.0f;

    int centerIdx = m_config.centerMonitorIndex;
    if (centerIdx < 0 || centerIdx >= static_cast<int>(m_monitors.size()))
        centerIdx = 0;
    const auto& center = m_monitors[static_cast<size_t>(centerIdx)];

    const float widthMm = center.physicalWidthMm;
    const float eyeDistMm = m_config.eyeDistance * 1000.0f;
    if (eyeDistMm < 1e-3f) return 60.0f;
    return 2.0f * (std::atan(widthMm / (2.0f * eyeDistMm)) * 57.29577951308232f);
}

float TripleMonitorManager::calculateBezelOffsetDeg(int /*monitorIndex*/) const
{
    const float eyeDistMm = m_config.eyeDistance * 1000.0f;
    if (eyeDistMm < 1e-3f) return 0.0f;
    const float halfBezel = m_config.bezelCompensationMm / 2.0f;
    return std::atan(halfBezel / eyeDistMm) * 57.29577951308232f;
}


// ============================================================================
// RacingInputManager
// ============================================================================

RacingInputManager::initialize()
{
    if (m_initialized) return true;

    scanDevices();
    m_initialized = true;
    fprintf(stderr, "[RacingInputManager] Initialized with %d devices\n", static_cast<int>(m_devices.size()));
    return true;
}

void RacingInputManager::shutdown()
{
    m_devices.clear();
    m_deviceStates.clear();
    m_selectedDevice = -1;
    m_initialized = false;
}

void RacingInputManager::update(double dt)
{
    if (!m_initialized) return;

    m_updateAccumulator += dt;
    double pollInterval = 1.0 / INPUT_POLL_RATE;
    if (m_updateAccumulator < pollInterval) return;
    m_updateAccumulator -= pollInterval;

    for (int i = 0; i < static_cast<int>(m_devices.size()); ++i) {
        processAxes(i);
        processButtons(i);
    }
}

void RacingInputManager::scanDevices()
{
    m_devices.clear();
    m_deviceStates.clear();

    InputDeviceProfile keyboard;
    keyboard.name = "Keyboard";
    keyboard.deviceName = "System Keyboard";
    keyboard.type = InputDeviceType::Keyboard;

    InputAxis steerAxis;
    steerAxis.type = AxisType::SteeringWheel;
    steerAxis.deviceAxisIndex = 0;
    steerAxis.minRange = -1.0f;
    steerAxis.maxRange = 1.0f;
    keyboard.axes.push_back(steerAxis);

    InputAxis throttleAxis;
    throttleAxis.type = AxisType::Throttle;
    throttleAxis.deviceAxisIndex = 1;
    throttleAxis.minRange = 0.0f;
    throttleAxis.maxRange = 1.0f;
    keyboard.axes.push_back(throttleAxis);

    InputAxis brakeAxis;
    brakeAxis.type = AxisType::Brake;
    brakeAxis.deviceAxisIndex = 2;
    brakeAxis.minRange = 0.0f;
    brakeAxis.maxRange = 1.0f;
    keyboard.axes.push_back(brakeAxis);

    InputButton shiftUp;
    shiftUp.deviceButtonIndex = 0;
    shiftUp.function = InputButton::Function::ShiftUp;
    keyboard.buttons.push_back(shiftUp);

    InputButton shiftDown;
    shiftDown.deviceButtonIndex = 1;
    shiftDown.function = InputButton::Function::ShiftDown;
    keyboard.buttons.push_back(shiftDown);

    keyboard.isGameController = false;
    m_devices.push_back(keyboard);

    DeviceState ks;
    ks.axisValues.resize(3);
    ks.buttonStates.resize(2);
    ks.prevButtonStates.resize(2);
    m_deviceStates.push_back(ks);

    fprintf(stderr, "[RacingInputManager] Scanned %d devices\n", static_cast<int>(m_devices.size()));
    for (size_t i = 0; i < m_devices.size(); ++i) {
        fprintf(stderr, "  %s - %d axes, %d buttons\n", m_devices[i].name.c_str(),
               static_cast<int>(m_devices[i].axes.size()), static_cast<int>(m_devices[i].buttons.size()));
    }
}

void RacingInputManager::selectDevice(int index)
{
    if (index < 0 || index >= static_cast<int>(m_devices.size())) return;
    m_selectedDevice = index;
    if (onDeviceSelected) onDeviceSelected(index);
    fprintf(stderr, "[RacingInputManager] Selected device %s\n", m_devices[index].name.c_str());
}

void RacingInputManager::processAxes(int deviceIndex)
{
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(m_devices.size())) return;
    auto& profile = m_devices[deviceIndex];
    auto& state = m_deviceStates[deviceIndex];

    for (size_t i = 0; i < profile.axes.size(); ++i) {
        auto& axis = profile.axes[i];
        float raw = i < state.axisValues.size() ? state.axisValues[i] : 0.0f;
        float mapped = mapAxisRange(raw, axis.minRange, axis.maxRange);
        applyAxisFilters(axis);
        axis.rawValue = raw;
        axis.value = mapped;
    }
}

void RacingInputManager::processButtons(int deviceIndex)
{
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(m_devices.size())) return;
    auto& profile = m_devices[deviceIndex];
    auto& state = m_deviceStates[deviceIndex];

    for (auto& btn : profile.buttons) {
        int idx = btn.deviceButtonIndex;
        if (idx < 0 || idx >= static_cast<int>(state.buttonStates.size())) continue;

        bool current = state.buttonStates[idx];
        bool previous = state.prevButtonStates[idx];
        btn.pressed = current;
        btn.justPressed = current && !previous;
        btn.justReleased = !current && previous;

        if (btn.justPressed) {
            if (onButtonPressed) onButtonPressed(btn.function);
        }
    }

    state.prevButtonStates = state.buttonStates;
}

void RacingInputManager::applyAxisFilters(InputAxis& axis)
{
    axis.value = applyDeadZone(axis.value, axis.deadZone);
    if (axis.inverted) axis.value = -axis.value;
    axis.value = std::clamp(axis.value, -1.0f, 1.0f);
}

float RacingInputManager::applyDeadZone(float value, float deadZone) const
{
    if (deadZone <= 0.0f) return value;
    if (std::abs(value) < deadZone) return 0.0f;
    float sign = (value > 0) ? 1.0f : -1.0f;
    return sign * (std::abs(value) - deadZone) / (1.0f - deadZone);
}

float RacingInputManager::applyGammaCurve(float value, float gamma) const
{
    if (gamma <= 0.0f || gamma >= 2.0f) return value;
    float sign = (value > 0) ? 1.0f : -1.0f;
    return sign * std::pow(std::abs(value), gamma);
}

float RacingInputManager::mapAxisRange(float raw, float minRange, float maxRange) const
{
    float range = maxRange - minRange;
    if (range <= 0.0f) return 0.0f;
    return (raw - minRange) / range * 2.0f - 1.0f;
}

float RacingInputManager::getAxis(AxisType type) const
{
    if (m_selectedDevice < 0 || m_selectedDevice >= static_cast<int>(m_devices.size())) return 0.0f;
    const auto& axes = m_devices[m_selectedDevice].axes;
    for (const auto& axis : axes) {
        if (axis.type == type) return axis.value;
    }
    return 0.0f;
}

float RacingInputManager::getRawAxis(AxisType type) const
{
    if (m_selectedDevice < 0 || m_selectedDevice >= static_cast<int>(m_devices.size())) return 0.0f;
    const auto& axes = m_devices[m_selectedDevice].axes;
    for (const auto& axis : axes) {
        if (axis.type == type) return axis.rawValue;
    }
    return 0.0f;
}

void RacingInputManager::setAxisDeadZone(AxisType type, float deadZone)
{
    if (m_selectedDevice < 0 || m_selectedDevice >= static_cast<int>(m_devices.size())) return;
    auto& axes = m_devices[m_selectedDevice].axes;
    for (auto& axis : axes) {
        if (axis.type == type) {
            axis.deadZone = std::clamp(deadZone, 0.0f, 0.5f);
            return;
        }
    }
}

void RacingInputManager::setAxisGamma(AxisType type, float gamma)
{
    if (m_selectedDevice < 0 || m_selectedDevice >= static_cast<int>(m_devices.size())) return;
    auto& axes = m_devices[m_selectedDevice].axes;
    for (auto& axis : axes) {
        if (axis.type == type) {
            axis.gamma = std::clamp(gamma, 0.1f, 3.0f);
            return;
        }
    }
}

void RacingInputManager::setAxisInverted(AxisType type, bool inverted)
{
    if (m_selectedDevice < 0 || m_selectedDevice >= static_cast<int>(m_devices.size())) return;
    auto& axes = m_devices[m_selectedDevice].axes;
    for (auto& axis : axes) {
        if (axis.type == type) {
            axis.inverted = inverted;
            return;
        }
    }
}

bool RacingInputManager::isButtonPressed(InputButton::Function function) const
{
    if (m_selectedDevice < 0 || m_selectedDevice >= static_cast<int>(m_devices.size())) return false;
    for (const auto& btn : m_devices[m_selectedDevice].buttons) {
        if (btn.function == function) return btn.pressed;
    }
    return false;
}

bool RacingInputManager::isButtonJustPressed(InputButton::Function function) const
{
    if (m_selectedDevice < 0 || m_selectedDevice >= static_cast<int>(m_devices.size())) return false;
    for (const auto& btn : m_devices[m_selectedDevice].buttons) {
        if (btn.function == function) return btn.justPressed;
    }
    return false;
}

bool RacingInputManager::isButtonJustReleased(InputButton::Function function) const
{
    if (m_selectedDevice < 0 || m_selectedDevice >= static_cast<int>(m_devices.size())) return false;
    for (const auto& btn : m_devices[m_selectedDevice].buttons) {
        if (btn.function == function) return btn.justReleased;
    }
    return false;
}

float RacingInputManager::steeringAngle() const
{
    float normalized = getAxis(AxisType::SteeringWheel);
    return normalized * (m_steeringRange / 2.0f);
}

void RacingInputManager::beginCalibration()
{
    m_calibrating = true;
    fprintf(stderr, "[RacingInputManager] Calibration started\n");
}

void RacingInputManager::endCalibration()
{
    m_calibrating = false;
    if (onCalibrationComplete) onCalibrationComplete();
    fprintf(stderr, "[RacingInputManager] Calibration completed\n");
}

void RacingInputManager::playFFBEffect(const ForceFeedbackEffect& effect)
{
    if (!m_ffbEnabled || !m_ffbSupported) return;
    if (onFFBUpdate) onFFBUpdate(effect.magnitude * m_ffbStrength);
}

void RacingInputManager::stopAllFFB()
{
    if (onFFBUpdate) onFFBUpdate(0.0f);
}

void RacingInputManager::setConstantForce(float force)
{
    if (!m_ffbEnabled) return;
    if (onFFBUpdate) onFFBUpdate(force * m_ffbStrength);
}

void RacingInputManager::setSpringForce(float center, float stiffness, float damping)
{
    if (!m_ffbEnabled) return;
    float angle = steeringAngle();
    float springTorque = -(angle - center) * stiffness * 0.01f - damping * 0.001f * angle;
    if (onFFBUpdate) onFFBUpdate(std::clamp(springTorque * m_ffbStrength, -1.0f, 1.0f));
}

void RacingInputManager::setDamperForce(float velocity, float coefficient)
{
    if (!m_ffbEnabled) return;
    if (onFFBUpdate) onFFBUpdate(std::clamp(-velocity * coefficient * 0.01f * m_ffbStrength, -1.0f, 1.0f));
}

void RacingInputManager::updateFFBFromPhysics(float aligningTorqueNm)
{
    if (!m_ffbEnabled) return;
    float norm = std::clamp(aligningTorqueNm / 12.0f, -1.0f, 1.0f);
    if (onFFBUpdate) onFFBUpdate(norm * m_ffbStrength);
}

void RacingInputManager::saveProfile(const std::string& path) const
{
    if (path.empty() || m_selectedDevice < 0) return;
    SimpleIni ini;
    ini.setValue("RacingInput", "selectedDevice", m_selectedDevice);
    ini.setValue("RacingInput", "steeringRange", m_steeringRange);
    ini.setValue("RacingInput", "ffbEnabled", m_ffbEnabled);
    ini.setValue("RacingInput", "ffbStrength", m_ffbStrength);

    const auto& device = m_devices[m_selectedDevice];
    ini.setValue("RacingInput/Device", "name", device.name);
    ini.setValue("RacingInput/Device", "type", static_cast<int>(device.type));

    for (const auto& axis : device.axes) {
        std::string key = "axis_" + std::to_string(static_cast<int>(axis.type));
        ini.setValue("RacingInput/Axes", key + "_deadZone", axis.deadZone);
        ini.setValue("RacingInput/Axes", key + "_gamma", axis.gamma);
        ini.setValue("RacingInput/Axes", key + "_inverted", axis.inverted);
    }

    ini.save(path);
}

void RacingInputManager::loadProfile(const std::string& path)
{
    if (path.empty()) return;
    SimpleIni ini;
    if (!ini.load(path)) return;

    m_steeringRange = ini.valueFloat("RacingInput", "steeringRange", 900.0f);
    m_ffbEnabled = ini.valueBool("RacingInput", "ffbEnabled", false);
    m_ffbStrength = ini.valueFloat("RacingInput", "ffbStrength", 0.75f);

    int devIdx = ini.valueInt("RacingInput", "selectedDevice", 0);
    if (devIdx >= 0 && devIdx < static_cast<int>(m_devices.size())) {
        selectDevice(devIdx);
    }

    if (m_selectedDevice >= 0 && m_selectedDevice < static_cast<int>(m_devices.size())) {
        auto& axes = m_devices[m_selectedDevice].axes;
        for (auto& axis : axes) {
            std::string key = "axis_" + std::to_string(static_cast<int>(axis.type));
            axis.deadZone = ini.valueFloat("RacingInput/Axes", key + "_deadZone", axis.deadZone);
            axis.gamma = ini.valueFloat("RacingInput/Axes", key + "_gamma", axis.gamma);
            axis.inverted = ini.valueBool("RacingInput/Axes", key + "_inverted", axis.inverted);
        }
    }
}

} // namespace ks::device