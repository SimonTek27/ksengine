#pragma once

#include <memory>
#include <string>
#include <unordered_set>

#ifdef _WIN32
namespace ks { namespace device {
class XInputDevice;
class DirectInputJoystick;
} }
#endif

namespace ks {
namespace sim {

/**
 * Configurable mapping from raw joystick axes/buttons to vehicle controls.
 * Axis indices: 0=LX, 1=LY, 2=RX, 3=RY, 4=LT, 5=RT (XInput default layout).
 * For external devices, feed axes via injectAxes().
 */
struct JoystickMapping {
    int steerAxis = 0;       // left stick X
    int throttleAxis = 5;    // right trigger
    int brakeAxis = 4;       // left trigger
    int clutchAxis = -1;     // optional

    bool invertSteer = false;
    bool invertThrottle = false;
    bool invertBrake = false;
    bool throttleIsCombined = false; // single axis: +throttle / -brake

    int shiftUpButton = 5;   // RB
    int shiftDownButton = 4; // LB
    int clutchButton = 0;    // A

    double deadZone = 0.12;
    double steerGamma = 1.6;
    double throttleGamma = 1.0;
    double brakeGamma = 1.0;

    static JoystickMapping gamepadDefault() { return JoystickMapping{}; }

    static JoystickMapping wheelDefault() {
        JoystickMapping m;
        m.steerAxis = 0;
        m.throttleAxis = 1; // often Y or separate pedal axis
        m.brakeAxis = 2;
        m.clutchAxis = 3;
        m.steerGamma = 1.2;
        m.deadZone = 0.02;
        m.shiftUpButton = 4;
        m.shiftDownButton = 5;
        return m;
    }

    static JoystickMapping combinedPedal() {
        JoystickMapping m;
        m.throttleIsCombined = true;
        m.throttleAxis = 1; // + = throttle, - = brake
        m.brakeAxis = -1;
        return m;
    }
};

/** Qt-free input: keyboard + XInput + generic axis injection. */
class InputManager {
public:
    static constexpr int KEY_UP    = 0x26;
    static constexpr int KEY_DOWN  = 0x28;
    static constexpr int KEY_LEFT  = 0x25;
    static constexpr int KEY_RIGHT = 0x27;

    static constexpr int AXIS_LX = 0;
    static constexpr int AXIS_LY = 1;
    static constexpr int AXIS_RX = 2;
    static constexpr int AXIS_RY = 3;
    static constexpr int AXIS_LT = 4;
    static constexpr int AXIS_RT = 5;
    static constexpr int AXIS_COUNT = 8;

    InputManager();
    ~InputManager();

    bool initialize();
    void update();

    double throttle() const { return m_throttle; }
    double brake() const { return m_brake; }
    double steer() const { return m_steer; }
    double clutch() const { return m_clutch; }

    double rawThrottle() const { return m_rawThrottle; }
    double rawBrake() const { return m_rawBrake; }
    double rawSteer() const { return m_rawSteer; }

    bool shiftUp() const { return m_shiftUp; }
    bool shiftDown() const { return m_shiftDown; }

    bool hasXInput() const;
    bool isXInputConnected() const;
    bool hasDirectInput() const;
    bool isDirectInputConnected() const;
    /** Prefer DI wheel over XInput when both present. */
    void setPreferDirectInput(bool v) { m_preferDi = v; }
    bool preferDirectInput() const { return m_preferDi; }

    void setKeyDown(int key) { m_keys.insert(key); }
    void setKeyUp(int key) { m_keys.erase(key); }
    bool isKeyDown(int key) const { return m_keys.count(key) > 0; }

    /** Mapping presets / live config */
    void setMapping(const JoystickMapping& m) { m_map = m; }
    const JoystickMapping& mapping() const { return m_map; }
    void setSteerGamma(double g) { m_map.steerGamma = g; }
    void setDeadZone(double dz) { m_map.deadZone = dz; }
    void setInvertSteer(bool i) { m_map.invertSteer = i; }

    /**
     * Inject raw axes from DirectInput / HID / custom driver.
     * values: typically -1..1 for sticks, 0..1 for triggers.
     * buttons: bit flags, bit i = button i pressed.
     */
    void injectAxes(const double axes[AXIS_COUNT], unsigned buttons);
    void injectAxis(int index, double value);
    void injectButton(int index, bool pressed);

    void reset();

private:
    void processKeyboard();
    void processXInput();
    void processInjected();
    void applyMapping(const double axes[AXIS_COUNT], unsigned buttons);
    static double curve(double v, double gamma, double deadZone, bool bipolar);

    double m_throttle = 0, m_brake = 0, m_steer = 0, m_clutch = 0;
    double m_rawThrottle = 0, m_rawBrake = 0, m_rawSteer = 0;

    bool m_shiftUp = false, m_shiftDown = false;
    bool m_prevE = false, m_prevQ = false;
    bool m_prevShiftUpBtn = false, m_prevShiftDownBtn = false;

    JoystickMapping m_map = JoystickMapping::gamepadDefault();

    double m_axes[AXIS_COUNT] = {};
    unsigned m_buttons = 0;
    bool m_hasInjected = false;

    std::unordered_set<int> m_keys;

#ifdef _WIN32
    std::unique_ptr<ks::device::XInputDevice> m_xinput;
    std::unique_ptr<ks::device::DirectInputJoystick> m_dinput;
    bool m_preferDi = true;
#endif
};

} // namespace sim
} // namespace ks
