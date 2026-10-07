#pragma once
#include <string>
#include "SimRacingDevices.h"
#include <vector>
#include <mutex>

namespace ks::device {

// ============================================================================
// MozaFFB — FFB for MOZA Racing direct drive wheels
// ============================================================================
// Supports: R5, R9, R12, R16, R21, R21F, MOZA Racing Wheel
// Uses MOZA USB HID protocol for force feedback.
// MOZA wheels expose a HID interface for FFB — no DirectInput needed.
// The MOZA SDK (Pit House) provides a proprietary API, but the generic
// USB HID approach works without installing the MOZA software.
// ============================================================================

class MozaFFB {
public:
    MozaFFB();
    ~MozaFFB();

    bool initialize();
    void shutdown();
    bool isSupported() const { return m_connected; }

    // Core FFB
    void updateFFB(float torqueNm);

    // Effect types
    void setConstantForce(float magnitude);   // -1.0 to 1.0
    void setSpringForce(float center, float stiffness, float damping);
    void setDamperForce(float velocity, float coefficient);
    void setFrictionForce(float coefficient);
    void setRumble(float strongMotor, float weakMotor);

    // MOZA-specific
    enum class WheelModel { Unknown, R5, R9, R12, R16, R21, R21F, MBoat, MBoatPro };
    WheelModel detectedModel() const { return m_model; }
    std::string modelName() const;
    float maxTorqueNm() const;

    // MOZA VID
    static constexpr uint16_t MOZA_VID = 0x346E;

    // Max torque per model (Nm)
    static float maxTorqueForModel(WheelModel model);

private:
    bool enumerateDevice();
    bool sendFFBCommand(const uint8_t* data, size_t len);
    void processHIDInput();

    bool m_connected = false;
    std::mutex m_mutex;
    float m_lastTorque = 0.0f;

    WheelModel m_model = WheelModel::Unknown;
    uint16_t m_productId = 0;

    // HID device handle (Windows HANDLE or platform equivalent)
    void* m_hidDevice = nullptr;

    // Known MOZA PIDs
    struct DeviceInfo {
        uint16_t pid;
        WheelModel model;
        const char* name;
        float maxTorque;
    };
    static const std::vector<DeviceInfo>& knownDevices();
};

} // namespace ks::device
