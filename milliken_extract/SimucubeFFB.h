#pragma once
#include "SimRacingDevices.h"
#include <memory>
#include <mutex>
#include <string>
#include <cstdint>

namespace ks::device {

class SimucubeFFB {
public:
    SimucubeFFB();
    ~SimucubeFFB();

    bool initialize();
    void shutdown();
    bool isSupported() const { return m_connected; }

    void updateFFB(float torqueNm);
    void setConstantForce(float magnitude);
    void setSpringForce(float center, float stiffness, float damping);
    void setDamperForce(float velocity, float coefficient);
    void setFrictionForce(float coefficient);
    void setRumble(float strongMotor, float weakMotor);

    enum class WheelModel { Unknown, Simucube1, Simucube2Pro, Simucube2Ultimate };
    WheelModel detectedModel() const { return m_model; }
    std::string modelName() const;
    float maxTorqueNm() const;

    void setTargetIP(const std::string& ip) { m_targetIP = ip; }
    const std::string& targetIP() const { return m_targetIP; }

private:
    bool discoverDevice();
    bool sendTrueDriveCommand(const uint8_t* data, size_t len);
    bool sendTorqueCommand(float torqueNm);
    void processUDPResponse();

    bool m_connected = false;
    std::mutex m_mutex;
    float m_lastTorque = 0.0f;
    WheelModel m_model = WheelModel::Unknown;
    std::string m_targetIP;
    uint16_t m_targetPort = 0;
    void* m_udpSocket = nullptr;

    static constexpr uint16_t TRUEDRIVE_DEFAULT_PORT = 1234;
    static constexpr uint8_t CMD_TORQUE = 0x01;
    static constexpr uint8_t CMD_QUERY = 0x10;
    static constexpr uint8_t CMD_SET_MAX_TORQUE = 0x20;
    static float maxTorqueForModel(WheelModel model);
};

} // namespace ks::device
