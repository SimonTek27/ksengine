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
// FanatecFFB — DirectInput FFB for Fanatec wheels
// ============================================================================
// Supports: CSL DD, CSL Elite, ClubSport, Podium DD1, DD2, DD+
// Fanatec wheels on PC use DirectInput for FFB when the Fanatec Driver
// is NOT installed (generic mode). When the Fanatec driver is installed,
// the wheel appears as a DirectInput device with FFB support.
//
// Some Fanatec wheels also expose a proprietary USB interface for
// advanced FDB (Fanatec Driver Base) features like true 1:1 torque
// mapping, but DirectInput is the universal fallback.
// ============================================================================

class FanatecFFB {
public:
    FanatecFFB();
    ~FanatecFFB();

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
    void setRumble(float strongMotor, float weakMotor);  // Not used on DD wheels

    // Fanatec-specific
    enum class WheelModel { Unknown, CSLDD, CSLDDPlus, CSLElite, ClubSportV2,
                            ClubSportV3, PodiumDD1, PodiumDD2, PodiumDDPlus };
    WheelModel detectedModel() const { return m_model; }
    std::string modelName() const;
    float maxTorqueNm() const;

    // Fanatec VID
    static constexpr uint16_t FANATEC_VID = 0x0EB7;

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
    void* m_constantEffectRef = nullptr;
    DIEFFECT* m_damperEffect = nullptr;
    void* m_damperEffectRef = nullptr;

    // Known Fanatec PIDs
    struct DeviceInfo {
        uint16_t pid;
        WheelModel model;
        const char* name;
        float maxTorque;
    };
    static const std::vector<DeviceInfo>& knownDevices();
    static float maxTorqueForModel(WheelModel model);
};

} // namespace ks::device
