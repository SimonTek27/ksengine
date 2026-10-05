#pragma once

#include "NetworkConfig.h"
#include "NetworkAuth.h"

#if HAS_KSNET

#include <memory>
#include <string>
#include <functional>

namespace ks::sim {

class SimulationLoop;
class MultiCarManager;

namespace net {

class NetworkClient {
public:
    explicit NetworkClient();
    ~NetworkClient();

    bool connect(const std::string& address, uint16_t port,
                 const std::string& driverName, const std::string& carName);
    void disconnect();
    bool isConnected() const;

    void setSimulationLoop(SimulationLoop* loop) { m_simLoop = loop; }
    void setMultiCarManager(MultiCarManager* m) { m_multiCar = m; }
    void setJoinToken(const std::string& token) { m_joinToken = token; }
    const std::string& joinToken() const { return m_joinToken; }

    void update(double dt);
    void sendInput(const InputData& input);
    void sendMessage(int channel, ksnet::Message* msg);
    ksnet::Message* createMessage(int type);

    NetworkStats getStats() const { return m_stats; }
    uint32_t clientId() const { return m_clientId; }

    std::function<void(uint32_t)> onConnected;
    std::function<void(const std::string&)> onDisconnected;
    std::function<void(uint32_t, const CarStateData&)> onCarStateReceived;
    std::function<void(uint32_t, the std::string&, uint32_t)> onCarSpawned;
