#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ks::device {

// ============================================================================
// Minimal math / geometry (Qt-free replacements for QRect / QMatrix4x4)
// ============================================================================

struct IntRect {
    int x = 0, y = 0, w = 0, h = 0;
    int width() const { return w; }
    int height() const { return h; }
    static IntRect fromXYWH(int x, int y, int w, int h) { return {x, y, w, h}; }
};

/** Column-major 4x4 matrix (OpenGL convention). */
struct Mat4 {
    float m[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    static Mat4 identity() { return Mat4{}; }

    static Mat4 perspective(float fovYDeg, float aspect, float zNear, float zFar) {
        Mat4 r{};
        const float f = 1.0f / std::tan(fovYDeg * 0.5f * 0.017453292519943295f);
        r.m[0]  = f / aspect;
        r.m[5]  = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        r.m[15] = 0.0f;
        return r;
    }

    /** Rotate around axis (ax,ay,az) by angleDeg. */
    Mat4& rotate(float angleDeg, float ax, float ay, float az) {
        const float rad = angleDeg * 0.017453292519943295f;
        const float c = std::cos(rad), s = std::sin(rad);
        const float len = std::sqrt(ax*ax + ay*ay + az*az);
        if (len < 1e-8f) return *this;
        ax /= len; ay /= len; az /= len;
        const float ic = 1.0f - c;
        Mat4 R{};
        R.m[0]  = c + ax*ax*ic;
        R.m[1]  = ay*ax*ic + az*s;
        R.m[2]  = az*ax*ic - ay*s;
        R.m[4]  = ax*ay*ic - az*s;
        R.m[5]  = c + ay*ay*ic;
        R.m[6]  = az*ay*ic + ax*s;
        R.m[8]  = ax*az*ic + ay*s;
        R.m[9]  = ay*az*ic - ax*s;
        R.m[10] = c + az*az*ic;
        *this = multiply(R, *this);
        return *this;
    }

    static Mat4 multiply(const Mat4& a, const Mat4& b) {
        Mat4 o{};
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                o.m[col*4 + row] =
                    a.m[0*4 + row] * b.m[col*4 + 0] +
                    a.m[1*4 + row] * b.m[col*4 + 1] +
                    a.m[2*4 + row] * b.m[col*4 + 2] +
                    a.m[3*4 + row] * b.m[col*4 + 3];
            }
        }
        return o;
    }

    Mat4 operator*(const Mat4& o) const { return multiply(*this, o); }
};

// ============================================================================
// Monitor Configuration
// ============================================================================

struct MonitorInfo {
    int index = 0;
    std::string name;
    IntRect geometry;
    IntRect availableGeometry;
    float dpi = 96.0f;
    bool isPrimary = false;
    float physicalWidthMm = 0;
    float physicalHeightMm = 0;
};

struct TripleMonitorConfig {
    bool enabled = false;
    int centerMonitorIndex = -1;
    float bezelCompensationMm = 20.0f;
    float fovHorizontal = 108.0f;
    float eyeDistance = 0.63f;
    bool verticalSync = true;
    int targetFps = 60;

    enum class Arrangement : uint8_t {
        Horizontal = 0,
        Landscape,
        PortraitTop,
        VerticalStack,
        Custom
    };
    Arrangement arrangement = Arrangement::Horizontal;

    void load(const std::string& path);
    void save(const std::string& path) const;
};

// ============================================================================
// TripleMonitorManager — Qt-free multi-monitor rendering helper
// ============================================================================

class TripleMonitorManager {
public:
    TripleMonitorManager();
    ~TripleMonitorManager();

    // Detection (Win32 EnumDisplayMonitors; other platforms: single virtual monitor)
    void detectMonitors();
    int monitorCount() const { return static_cast<int>(m_monitors.size()); }
    const std::vector<MonitorInfo>& monitors() const { return m_monitors; }
    const MonitorInfo& monitor(int index) const { return m_monitors[static_cast<size_t>(index)]; }

    void setConfig(const TripleMonitorConfig& config) {
        m_config = config;
        recalculate();
        if (onConfigChanged) onConfigChanged();
    }
    const TripleMonitorConfig& config() const { return m_config; }

    struct EyeViewport {
        Mat4 projection;
        Mat4 view;
        IntRect viewport;
        float fovOffsetDeg = 0.0f;
    };

    EyeViewport leftEye() const { return m_leftEye; }
    EyeViewport centerEye() const { return m_centerEye; }
    EyeViewport rightEye() const { return m_rightEye; }

    void recalculate();
    void updateView(const Mat4& baseView);

    IntRect combinedViewport() const { return m_combinedViewport; }

    // Callbacks (replace Qt signals)
    std::function<void()> onMonitorsChanged;
    std::function<void()> onConfigChanged;

private:
    void calculateEyeProjections();
    float calculatePhysicalFovDeg() const;
    float calculateBezelOffsetDeg(int monitorIndex) const;

    TripleMonitorConfig m_config;
    std::vector<MonitorInfo> m_monitors;

    EyeViewport m_leftEye;
    EyeViewport m_centerEye;
    EyeViewport m_rightEye;
    IntRect m_combinedViewport;

    Mat4 m_baseView;
};

// ============================================================================
// Racing Input Device Types
// ============================================================================

enum class InputDeviceType : uint8_t {
    Keyboard = 0,
    Gamepad,
    SteeringWheel,
    FlightStick,
    Pedals,
    Shifter,
    Handbrake,
    MotionPlatform,
    ButtKicker,
    Custom
};

enum class AxisType : uint8_t {
    None = 0,
    SteeringWheel,     // rotation
    Throttle,          // analog axis
    Brake,             // analog axis
    Clutch,            // analog axis
    Handbrake,         // analog axis
    AccelerometerX,    // gyro
    AccelerometerY,
    AccelerometerZ,
    ForceFeedbackX,    // FFB
    ForceFeedbackY,
    POV,               // hat switch
    Button
};

struct InputAxis {
    AxisType type = AxisType::None;
    int deviceAxisIndex = -1;
    float value = 0.0f;
    float rawValue = 0.0f;
    float minRange = -1.0f;
    float maxRange = 1.0f;
    float deadZone = 0.0f;
    float saturation = 1.0f;
    float gamma = 1.0f;
    bool inverted = false;
};

struct InputButton {
    int deviceButtonIndex = -1;
    bool pressed = false;
    bool justPressed = false;
    bool justReleased = false;
    bool toggled = false;

    enum class Function : uint8_t {
        None = 0,
        ShiftUp,
        ShiftDown,
        Handbrake,
        LookLeft,
        LookRight,
        LookBack,
        DRS,
        ERS,
        PitLimiter,
        Flash,
        Horn,
        Headlights,
        Wipers,
        Menu,
        Reset,
        ToggleFFB,
        ToggleMirror,
        ToggleHUD,
    };
    Function function = Function::None;
};

struct InputDeviceProfile {
    std::string name;
    std::string deviceName;
    std::string deviceGuid;
    InputDeviceType type = InputDeviceType::Gamepad;
    std::vector<InputAxis> axes;
    std::vector<InputButton> buttons;
    bool isGameController = false;
};

// ============================================================================
// ForceFeedbackEffect
// ============================================================================

struct ForceFeedbackEffect {
    enum class Type : uint8_t {
        Constant = 0,
        Spring,
        Damper,
        Friction,
        Sine,
        Square,
        Triangle,
        SawtoothUp,
        SawtoothDown,
        Ramp,
        Collision
    };

    Type type = Type::Constant;
    float magnitude = 0.0f;
    float direction = 0.0f;     // radians, 0 = forward
    float duration = 0.0f;      // seconds, 0 = infinite
    float period = 0.0f;        // seconds for periodic effects
    float attackLevel = 0.0f;
    float fadeLevel = 0.0f;
    float attackTime = 0.0f;
    float fadeTime = 0.0f;
    int conditionOffset = 0;
    int conditionSaturation = 0;
    int conditionCoefficient = 0;
    int conditionDeadband = 0;
    int conditionCenter = 0;
};

// ============================================================================
// RacingInputManager
// ============================================================================
// Detects and manages racing simulation input devices: steering wheels,
// pedals, shifters, handbrakes. Supports FFB, per-device profiles,
// dead zones, gamma curves, and force feedback effects.
//
// Qt-free (unlike TripleMonitorManager above, which stays Qt-based — it
// needs QScreen for real monitor enumeration, a genuinely bigger native
// rewrite than this class needed). Plain callbacks replace the Qt signals;
// profile save/load uses a small built-in INI reader/writer instead of
// QSettings (see SimpleIni in the .cpp) since the format used here is a
// flat key=value-under-[Group] file, not anything QSettings-specific.
// ============================================================================

class RacingInputManager {
public:
    explicit RacingInputManager() = default;
    ~RacingInputManager();

    // Detection
    bool initialize();
    void shutdown();
    void update(double dt);
    void scanDevices();

    // Device management
    int deviceCount() const { return static_cast<int>(m_devices.size()); }
    const InputDeviceProfile& device(int index) const { return m_devices[index]; }
    void selectDevice(int index);
    int selectedDevice() const { return m_selectedDevice; }

    // Axis access
    float getAxis(AxisType type) const;
    float getRawAxis(AxisType type) const;
    void setAxisDeadZone(AxisType type, float deadZone);
    void setAxisGamma(AxisType type, float gamma);
    void setAxisInverted(AxisType type, bool inverted);

    // Button access
    bool isButtonPressed(InputButton::Function function) const;
    bool isButtonJustPressed(InputButton::Function function) const;
    bool isButtonJustReleased(InputButton::Function function) const;

    // Force feedback
    bool isFFBSupported() const { return m_ffbSupported; }
    bool isFFBEnabled() const { return m_ffbEnabled; }
    void setFFBEnabled(bool enabled) { m_ffbEnabled = enabled; }
    void setFFBStrength(float strength) { m_ffbStrength = std::clamp(strength, 0.0f, 1.0f); }
    float ffbStrength() const { return m_ffbStrength; }
    void playFFBEffect(const ForceFeedbackEffect& effect);
    void stopAllFFB();
    void setConstantForce(float force);
    void setSpringForce(float center, float stiffness, float damping);
    void setDamperForce(float velocity, float coefficient);
    void updateFFBFromPhysics(float aligningTorqueNm);

    // Steering-specific
    void setSteeringRange(float degrees) { m_steeringRange = degrees; }
    float steeringRange() const { return m_steeringRange; }
    float steeringAngle() const;

    // Calibration
    void beginCalibration();
    void endCalibration();
    bool isCalibrating() const { return m_calibrating; }

    // Profiles
    void saveProfile(const std::string& path) const;
    void loadProfile(const std::string& path);

    // Plain-callback event API, replacing Qt signals.
    std::function<void(int index)> onDeviceConnected;
    std::function<void(int index)> onDeviceDisconnected;
    std::function<void(int index)> onDeviceSelected;
    std::function<void(float force)> onFFBUpdate;
    std::function<void(InputButton::Function function)> onButtonPressed;
    std::function<void()> onCalibrationComplete;

private:
    struct DeviceState {
        std::vector<float> axisValues;
        std::vector<bool> buttonStates;
        std::vector<bool> prevButtonStates;
    };

    void processAxes(int deviceIndex);
    void processButtons(int deviceIndex);
    void applyAxisFilters(InputAxis& axis);
    float applyDeadZone(float value, float deadZone) const;
    float applyGammaCurve(float value, float gamma) const;
    float mapAxisRange(float raw, float minRange, float maxRange) const;

    std::vector<InputDeviceProfile> m_devices;
    std::vector<DeviceState> m_deviceStates;
    int m_selectedDevice = -1;
    bool m_initialized = false;
    bool m_calibrating = false;

    // FFB
    bool m_ffbSupported = false;
    bool m_ffbEnabled = false;
    float m_ffbStrength = 0.75f;
    float m_steeringRange = 900.0f;

    double m_updateAccumulator = 0.0;
    static constexpr double INPUT_POLL_RATE = 1000.0; // Hz
};

} // namespace ks::device