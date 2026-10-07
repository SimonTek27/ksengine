#include "InputManager.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include "devices/xinput/XInputDevice.h"
#include "devices/DirectInputJoystick.h"
#endif

namespace ks {
namespace sim {

InputManager::InputManager() = default;
InputManager::~InputManager() = default;

bool InputManager::initialize() {
#ifdef _WIN32
    // DirectInput (wheels / generic sticks)
    m_dinput = std::make_unique<ks::device::DirectInputJoystick>();
    if (m_dinput->initialize()) {
        std::fprintf(stderr, "InputManager: DirectInput device ready\n");
        // Racing wheels: use wheel mapping by default
        setMapping(JoystickMapping::wheelDefault());
    } else {
        std::fprintf(stderr, "InputManager: no DirectInput game controller\n");
        m_dinput.reset();
    }

    // XInput (Xbox pads)
    m_xinput = std::make_unique<ks::device::XInputDevice>();
    if (m_xinput->initialize()) {
        std::fprintf(stderr, "InputManager: XInput pad ready\n");
        if (!m_dinput)
            setMapping(JoystickMapping::gamepadDefault());
    } else {
        m_xinput.reset();
    }
#endif
    std::fprintf(stderr, "InputManager: mapping steer=%d throttle=%d brake=%d\n",
                 m_map.steerAxis, m_map.throttleAxis, m_map.brakeAxis);
    return true;
}

double InputManager::curve(double v, double gamma, double deadZone, bool bipolar) {
    if (bipolar) {
        if (std::abs(v) < deadZone) return 0.0;
        double sign = v > 0 ? 1.0 : -1.0;
        double n = (std::abs(v) - deadZone) / (1.0 - deadZone);
        return sign * std::pow(std::clamp(n, 0.0, 1.0), gamma);
    }
    // unipolar 0..1
    if (v < deadZone) return 0.0;
    double n = (v - deadZone) / (1.0 - deadZone);
    return std::pow(std::clamp(n, 0.0, 1.0), gamma);
}

void InputManager::applyMapping(const double axes[AXIS_COUNT], unsigned buttons) {
    auto axis = [&](int idx) -> double {
        if (idx < 0 || idx >= AXIS_COUNT) return 0.0;
        return axes[idx];
    };

    // Steer
    double st = axis(m_map.steerAxis);
    st = curve(st, m_map.steerGamma, m_map.deadZone, true);
    if (m_map.invertSteer) st = -st;
    m_steer = std::clamp(st, -1.0, 1.0);

    // Pedals
    if (m_map.throttleIsCombined) {
        double a = axis(m_map.throttleAxis);
        if (a >= 0) {
            m_throttle = curve(a, m_map.throttleGamma, m_map.deadZone, false);
            m_brake = 0;
        } else {
            m_brake = curve(-a, m_map.brakeGamma, m_map.deadZone, false);
            m_throttle = 0;
        }
    } else {
        double th = axis(m_map.throttleAxis);
        double br = axis(m_map.brakeAxis);
        // Triggers may arrive as 0..1; sticks as -1..1 → fold to 0..1
        if (th < 0) th = 0;
        if (br < 0) br = 0;
        if (m_map.invertThrottle) th = 1.0 - th;
        if (m_map.invertBrake) br = 1.0 - br;
        m_throttle = curve(th, m_map.throttleGamma, 0.02, false);
        m_brake = curve(br, m_map.brakeGamma, 0.02, false);
    }

    // Clutch axis or button
    if (m_map.clutchAxis >= 0) {
        double c = axis(m_map.clutchAxis);
        if (c < 0) c = 0;
        m_clutch = curve(c, 1.0, 0.02, false);
    } else if (m_map.clutchButton >= 0) {
        m_clutch = (buttons & (1u << m_map.clutchButton)) ? 1.0 : 0.0;
    } else {
        m_clutch = 0;
    }

    // Shift edge detection
    bool up = m_map.shiftUpButton >= 0 && (buttons & (1u << m_map.shiftUpButton));
    bool dn = m_map.shiftDownButton >= 0 && (buttons & (1u << m_map.shiftDownButton));
    if (up && !m_prevShiftUpBtn) m_shiftUp = true;
    if (dn && !m_prevShiftDownBtn) m_shiftDown = true;
    m_prevShiftUpBtn = up;
    m_prevShiftDownBtn = dn;

    m_rawThrottle = m_throttle;
    m_rawBrake = m_brake;
    m_rawSteer = m_steer;
}

void InputManager::injectAxes(const double axes[AXIS_COUNT], unsigned buttons) {
    std::memcpy(m_axes, axes, sizeof(m_axes));
    m_buttons = buttons;
    m_hasInjected = true;
}

void InputManager::injectAxis(int index, double value) {
    if (index < 0 || index >= AXIS_COUNT) return;
    m_axes[index] = value;
    m_hasInjected = true;
}

void InputManager::injectButton(int index, bool pressed) {
    if (index < 0 || index >= 32) return;
    if (pressed) m_buttons |= (1u << index);
    else m_buttons &= ~(1u << index);
    m_hasInjected = true;
}

void InputManager::processInjected() {
    applyMapping(m_axes, m_buttons);
}

void InputManager::update() {
    m_shiftUp = false;
    m_shiftDown = false;

    // Priority: external inject > DirectInput wheel > XInput pad > keyboard
    if (m_hasInjected) {
        processInjected();
        m_hasInjected = false;
        return;
    }

#ifdef _WIN32
    if (m_preferDi && m_dinput && m_dinput->isConnected()) {
        m_dinput->update();
        injectAxes(m_dinput->axes(), m_dinput->buttons());
        processInjected();
        m_hasInjected = false;
        return;
    }
    if (m_xinput && m_xinput->isConnected()) {
        processXInput();
        return;
    }
    if (m_dinput && m_dinput->isConnected()) {
        m_dinput->update();
        injectAxes(m_dinput->axes(), m_dinput->buttons());
        processInjected();
        m_hasInjected = false;
        return;
    }
#endif
    processKeyboard();
}

bool InputManager::hasDirectInput() const {
#ifdef _WIN32
    return m_dinput != nullptr;
#else
    return false;
#endif
}

bool InputManager::isDirectInputConnected() const {
#ifdef _WIN32
    return m_dinput && m_dinput->isConnected();
#else
    return false;
#endif
}

void InputManager::processXInput() {
#ifdef _WIN32
    m_xinput->update();

    double axes[AXIS_COUNT] = {};
    axes[AXIS_LX] = m_xinput->leftStickX();
    axes[AXIS_LY] = m_xinput->leftStickY();
    axes[AXIS_RX] = m_xinput->rightStickX();
    axes[AXIS_RY] = m_xinput->rightStickY();
    axes[AXIS_LT] = m_xinput->leftTrigger();
    axes[AXIS_RT] = m_xinput->rightTrigger();

    unsigned buttons = 0;
    if (m_xinput->buttonA()) buttons |= (1u << 0);
    if (m_xinput->buttonB()) buttons |= (1u << 1);
    if (m_xinput->buttonX()) buttons |= (1u << 2);
    if (m_xinput->buttonY()) buttons |= (1u << 3);
    if (m_xinput->buttonLB()) buttons |= (1u << 4);
    if (m_xinput->buttonRB()) buttons |= (1u << 5);
    if (m_xinput->buttonBack()) buttons |= (1u << 6);
    if (m_xinput->buttonStart()) buttons |= (1u << 7);
    if (m_xinput->dpadUp()) buttons |= (1u << 8);
    if (m_xinput->dpadDown()) buttons |= (1u << 9);
    if (m_xinput->dpadLeft()) buttons |= (1u << 10);
    if (m_xinput->dpadRight()) buttons |= (1u << 11);

    applyMapping(axes, buttons);

    // Also dpad shift
    if (m_xinput->dpadUp()) m_shiftUp = true;
    if (m_xinput->dpadDown()) m_shiftDown = true;
#endif
}

bool InputManager::hasXInput() const {
#ifdef _WIN32
    return m_xinput != nullptr;
#else
    return false;
#endif
}

bool InputManager::isXInputConnected() const {
#ifdef _WIN32
    return m_xinput && m_xinput->isConnected();
#else
    return false;
#endif
}

void InputManager::processKeyboard() {
    m_throttle = 0;
    if (isKeyDown('W') || isKeyDown('w') || isKeyDown(KEY_UP))
        m_throttle = 1.0;

    m_brake = 0;
    if (isKeyDown('S') || isKeyDown('s') || isKeyDown(KEY_DOWN))
        m_brake = 1.0;

    m_steer = 0;
    if (isKeyDown('A') || isKeyDown('a') || isKeyDown(KEY_LEFT))
        m_steer -= 1.0;
    if (isKeyDown('D') || isKeyDown('d') || isKeyDown(KEY_RIGHT))
        m_steer += 1.0;

    if (m_map.invertSteer) m_steer = -m_steer;

    bool curE = isKeyDown('E') || isKeyDown('e');
    bool curQ = isKeyDown('Q') || isKeyDown('q');
    if (curE && !m_prevE) m_shiftUp = true;
    if (curQ && !m_prevQ) m_shiftDown = true;
    m_prevE = curE;
    m_prevQ = curQ;

    m_steer = curve(m_steer, m_map.steerGamma, m_map.deadZone, true);
    m_throttle = std::clamp(m_throttle, 0.0, 1.0);
    m_brake = std::clamp(m_brake, 0.0, 1.0);
    m_steer = std::clamp(m_steer, -1.0, 1.0);

    m_rawThrottle = m_throttle;
    m_rawBrake = m_brake;
    m_rawSteer = m_steer;
}

void InputManager::reset() {
    m_throttle = m_brake = m_steer = m_clutch = 0;
    m_rawThrottle = m_rawBrake = m_rawSteer = 0;
    m_shiftUp = m_shiftDown = false;
    m_keys.clear();
    m_buttons = 0;
    m_hasInjected = false;
    std::memset(m_axes, 0, sizeof(m_axes));
}

} // namespace sim
} // namespace ks
