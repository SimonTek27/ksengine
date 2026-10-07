#pragma once
#include <string>
#include "SimRacingDevices.h"
#include <vector>
#include <mutex>

// Forward declarations for DirectInput types
struct IDirectInput8A;
struct IDirectInputDevice8A;
struct DIEFFECT;

namespace ks::device {

// ============================================================================
// LogitechFFB — DirectInput FFB for Logitech wheels
// ============================================================================
// Supports: G27, G29, G920, G Pro, MOMO Racing
// Uses DirectInput8 Force Feedback API for all Logitech wheels.
// Logitech has a proprietary SDK (LogitechSteeringWheel SDK) but the
// generic DirectInput approach works for all models and is preferred.
// ============================================================================

class LogitechFFB {
public:
    LogitechFFB();
    ~LogitechFFB();

    bool initialize();
    void shutdown();
    bool isSupported() const { return m_acquired; }

    // Core FFB
    void updateFFB(float torqueNm);

    // Effect types
    void setConstantForce(float magnitude);   // -1.0 to 1.0
    void setSpringForce(float center, float stiffness, float damping);
    void setDamperForce(float velocity, float coefficient);
    void setFrictionForce(float coefficient);
    void setRumble(float strongMotor, float weakMotor);  // 0.0 to 1.0

    // Logitech-specific
    enum class WheelModel { Unknown, G27, G29, G920, GPro, MOMO, DrivingForceGT };
    WheelModel detectedModel() const { return m_model; }
    std::string modelName() const;

    // Logitech VID
    static constexpr uint16_t LOGITECH_VID = 0x046D;

private:
    bool enumerateDevice();
    bool createFFBEffect();
    void updateConstantForce(float forcePercent);
    void processDIInput();

    bool m_acquired = false;
    bool m_fsbInitialized = false;
    std::mutex m_mutex;
    float m_lastTorque = 0.0f;

    WheelModel m_model = WheelModel::Unknown;
    uint16_t m_productId = 0;

    // DirectInput objects
    IDirectInput8A* m_dinput = nullptr;
    IDirectInputDevice8A* m_device = nullptr;

    // FFB effect handles
    DIEFFECT* m_constantEffect = nullptr;
    void* m_constantEffectRef = nullptr;   // IDirectInputEffect*
    DIEFFECT* m_damperEffect = nullptr;
    void* m_damperEffectRef = nullptr;

    // Known Logitech PIDs
    struct DeviceInfo {
        uint16_t pid;
        WheelModel model;
        const char* name;
    };
    static const std::vector<DeviceInfo>& knownDevices();
};

} // namespace ks::device
