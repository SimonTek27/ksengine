#include "XInputDevice.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ks {
namespace device {

bool XInputDevice::initialize() {
#ifdef _WIN32
    m_supported = true;
    for (int i = 0; i < 4; ++i) {
        XINPUT_STATE st{};
        if (XInputGetState(i, &st) == ERROR_SUCCESS) {
            m_playerIndex = i;
            m_connected = true;
            std::fprintf(stderr, "XInputDevice: controller on index %d\n", i);
            return true;
        }
    }
    std::fprintf(stderr, "XInputDevice: no controller connected\n");
    return false;
#else
    m_supported = false;
    return false;
#endif
}

void XInputDevice::shutdown() {
    stopRumble();
    m_connected = false;
}

void XInputDevice::applyDeadZone(double& value, double deadZone) const {
    if (std::abs(value) < deadZone) {
        value = 0.0;
    } else {
        double sign = value > 0 ? 1.0 : -1.0;
        value = sign * (std::abs(value) - deadZone) / (1.0 - deadZone);
    }
}

void XInputDevice::applyStickCurve(double& value, double gamma) const {
    if (value == 0.0) return;
    double sign = value > 0 ? 1.0 : -1.0;
    value = sign * std::pow(std::abs(value), gamma);
}

void XInputDevice::update() {
#ifdef _WIN32
    if (!m_supported) return;
    XINPUT_STATE st{};
    DWORD r = XInputGetState(m_playerIndex, &st);
    if (r != ERROR_SUCCESS) {
        m_connected = false;
        m_throttle = m_brake = m_steer = m_clutch = 0;
        return;
    }
    m_connected = true;

    const auto& gp = st.Gamepad;

    // Sticks: -32768..32767
    m_leftStickX = gp.sThumbLX / 32767.0;
    m_leftStickY = gp.sThumbLY / 32767.0;
    m_rightStickX = gp.sThumbRX / 32767.0;
    m_rightStickY = gp.sThumbRY / 32767.0;
    applyDeadZone(m_leftStickX, m_deadZone);
    applyDeadZone(m_leftStickY, m_deadZone);
    applyDeadZone(m_rightStickX, m_deadZone);
    applyDeadZone(m_rightStickY, m_deadZone);

    // Triggers: 0..255
    m_brake = gp.bLeftTrigger / 255.0;
    m_throttle = gp.bRightTrigger / 255.0;
    if (m_brake < 0.05) m_brake = 0;
    if (m_throttle < 0.05) m_throttle = 0;

    m_steer = m_leftStickX;
    applyStickCurve(m_steer, m_steerGamma);
    m_steer = std::clamp(m_steer, -1.0, 1.0);

    m_buttonA = (gp.wButtons & XINPUT_GAMEPAD_A) != 0;
    m_buttonB = (gp.wButtons & XINPUT_GAMEPAD_B) != 0;
    m_buttonX = (gp.wButtons & XINPUT_GAMEPAD_X) != 0;
    m_buttonY = (gp.wButtons & XINPUT_GAMEPAD_Y) != 0;
    m_buttonLB = (gp.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    m_buttonRB = (gp.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    m_buttonBack = (gp.wButtons & XINPUT_GAMEPAD_BACK) != 0;
    m_buttonStart = (gp.wButtons & XINPUT_GAMEPAD_START) != 0;
    m_dpadUp = (gp.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    m_dpadDown = (gp.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    m_dpadLeft = (gp.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    m_dpadRight = (gp.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

    m_clutch = m_buttonA ? 1.0 : 0.0;

    // Apply pending rumble
    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);
        XINPUT_VIBRATION vib{};
        vib.wLeftMotorSpeed = static_cast<WORD>(std::clamp(m_rumbleLeft, 0.0, 1.0) * 65535.0);
        vib.wRightMotorSpeed = static_cast<WORD>(std::clamp(m_rumbleRight, 0.0, 1.0) * 65535.0);
        XInputSetState(m_playerIndex, &vib);
    }
#else
    (void)0;
#endif
}

void XInputDevice::setRumble(double leftMotor, double rightMotor) {
    std::lock_guard<std::mutex> lock(m_rumbleMutex);
    m_rumbleLeft = std::clamp(leftMotor, 0.0, 1.0);
    m_rumbleRight = std::clamp(rightMotor, 0.0, 1.0);
}

void XInputDevice::stopRumble() {
    setRumble(0, 0);
#ifdef _WIN32
    if (m_connected) {
        XINPUT_VIBRATION vib{};
        XInputSetState(m_playerIndex, &vib);
    }
#endif
}

void XInputDevice::applyFFB(float torqueNm, float speedKph) {
    float mag = std::min(1.0f, std::abs(torqueNm) / 8.0f) * static_cast<float>(m_ffbStrength);
    float road = std::min(0.3f, speedKph / 300.0f);
    setRumble(mag * 0.7 + road, mag * 0.4);
}

} // namespace device
} // namespace ks
