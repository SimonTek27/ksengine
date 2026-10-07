#pragma once

/**
 * Qt-free generic DirectInput joystick / wheel (Windows).
 * Enumerates first attached game controller with axes and feeds
 * normalized values suitable for InputManager::injectAxes().
 */

#include <cstdint>
#include <string>
#include <vector>

namespace ks {
namespace device {

class DirectInputJoystick {
public:
    static constexpr int AXIS_COUNT = 8;

    struct DeviceInfo {
        std::string name;
        unsigned vendorId = 0;
        unsigned productId = 0;
        int numAxes = 0;
        int numButtons = 0;
        bool hasForceFeedback = false;
    };

    DirectInputJoystick() = default;
    ~DirectInputJoystick() { shutdown(); }

    /** Enumerate and acquire first (or preferred VID) device. */
    bool initialize(unsigned preferredVid = 0);
    void shutdown();
    void update();

    bool isConnected() const { return m_acquired; }
    const DeviceInfo& info() const { return m_info; }

    /** Normalized axes: sticks -1..1, pedals/triggers 0..1 when possible. */
    const double* axes() const { return m_axes; }
    double axis(int i) const { return (i >= 0 && i < AXIS_COUNT) ? m_axes[i] : 0.0; }
    unsigned buttons() const { return m_buttons; }
    bool button(int i) const { return (m_buttons & (1u << i)) != 0; }

    /** List all attached game controllers (call before or after init). */
    static std::vector<DeviceInfo> enumerate();

private:
    bool createDevice();
    void releaseDevice();

    DeviceInfo m_info;
    double m_axes[AXIS_COUNT] = {};
    unsigned m_buttons = 0;
    bool m_acquired = false;
    unsigned m_preferredVid = 0;

    void* m_dinput = nullptr;  // IDirectInput8*
    void* m_device = nullptr;  // IDirectInputDevice8*
};

} // namespace device
} // namespace ks
