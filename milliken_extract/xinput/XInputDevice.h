#pragma once

/**
 * Qt-free Xbox controller via XInput (Windows).
 * No QObject / signals — polled API only.
 */

#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <XInput.h>
#pragma comment(lib, "xinput.lib")
#endif

namespace ks {
namespace device {

class XInputDevice {
public:
    XInputDevice() = default;
    ~XInputDevice() { shutdown(); }

    bool initialize();
    void shutdown();
    void update();

    bool isSupported() const { return m_supported; }
    bool isConnected() const { return m_connected; }
    int playerIndex() const { return m_playerIndex; }

    // Normalized 0..1 / -1..1
    double throttle() const { return m_throttle; }
    double brake() const { return m_brake; }
    double steer() const { return m_steer; }
    double clutch() const { return m_clutch; }

    double leftStickX() const { return m_leftStickX; }
    double leftStickY() const { return m_leftStickY; }
    double rightStickX() const { return m_rightStickX; }
    double rightStickY() const { return m_rightStickY; }
    double leftTrigger() const { return m_brake; }   // LT
    double rightTrigger() const { return m_throttle; } // RT

    bool buttonA() const { return m_buttonA; }
    bool buttonB() const { return m_buttonB; }
    bool buttonX() const { return m_buttonX; }
    bool buttonY() const { return m_buttonY; }
    bool buttonLB() const { return m_buttonLB; }
    bool buttonRB() const { return m_buttonRB; }
    bool buttonBack() const { return m_buttonBack; }
    bool buttonStart() const { return m_buttonStart; }
    bool dpadUp() const { return m_dpadUp; }
    bool dpadDown() const { return m_dpadDown; }
    bool dpadLeft() const { return m_dpadLeft; }
    bool dpadRight() const { return m_dpadRight; }

    void setRumble(double leftMotor, double rightMotor);
    void stopRumble();
    void applyFFB(float torqueNm, float speedKph);

    void setDeadZone(double dz) { m_deadZone = dz; }
    void setSteerGamma(double g) { m_steerGamma = g; }
    void setPlayerIndex(int i) { m_playerIndex = i; }

private:
    void applyDeadZone(double& value, double deadZone) const;
    void applyStickCurve(double& value, double gamma) const;

    bool m_supported = false;
    bool m_connected = false;
    int m_playerIndex = 0;

    double m_throttle = 0, m_brake = 0, m_steer = 0, m_clutch = 0;
    double m_leftStickX = 0, m_leftStickY = 0;
    double m_rightStickX = 0, m_rightStickY = 0;

    bool m_buttonA = false, m_buttonB = false, m_buttonX = false, m_buttonY = false;
    bool m_buttonLB = false, m_buttonRB = false;
    bool m_buttonBack = false, m_buttonStart = false;
    bool m_dpadUp = false, m_dpadDown = false, m_dpadLeft = false, m_dpadRight = false;

    double m_deadZone = 0.15;
    double m_steerGamma = 1.8;
    double m_ffbStrength = 1.0;

    std::mutex m_rumbleMutex;
    double m_rumbleLeft = 0, m_rumbleRight = 0;
};

} // namespace device
} // namespace ks
