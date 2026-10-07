#pragma once
#include <string>
#include "SimRacingDevices.h"
#include <vector>
#include <mutex>

// Forward declarations for DirectInput types
struct IDirectInput8A;
struct IDirectInputDevice8A;
struct DIEFFECT;
struct DIDEVICEOBJECTDATA;

namespace ks::device {

// ============================================================================
// ThrustmasterFFB — DirectInput FFB for Thrustmaster wheels
// ============================================================================
// Supports: T300 RS, T150, TS-PC Racer, T-GT, T80, TMX
// Uses DirectInput8 Force Feedback API for all Thrustmaster wheels.
// ============================================================================

class ThrustmasterFFB {
public:
    ThrustmasterFFB();
    ~ThrustmasterFFB();

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

    // Thrustmaster-specific
    void setMotorTemperature(float tempC);  // Temperature compensation
    void setBoostLevel(float level);        // Turbo boost
    enum class WheelModel { Unknown, T300, T150, TSPC, TGT, T80, TMX };
    WheelModel detectedModel() const { return m_model; }
    std::string modelName() const;

    // Thrustmaster VID
    static constexpr uint16_t THRUSTMASTER_VID = 0x044F;

private:
    bool enumerateDevice();
    bool createFFBEffect();
    void updateConstantForce(float forcePercent);
    void updatePeriodicEffect(float magnitude, float frequency);
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
    DIEFFECT* m_periodicEffect = nullptr;
    void* m_periodicEffectRef = nullptr;
    DIEFFECT* m_damperEffect = nullptr;
    void* m_damperEffectRef = nullptr;

    // Known Thrustmaster PIDs
    struct DeviceInfo {
        uint16_t pid;
        WheelModel model;
        const char* name;
    };
    static const std::vector<DeviceInfo>& knownDevices();
};

} // namespace ks::device
