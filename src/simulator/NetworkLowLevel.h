#pragma once

#include "NetworkConfig.h"

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

    void update(double dt);
    void sendInput(const InputData& input);
    void sendMessage(int channel, ksnet::Message* msg);
    ksnet::Message* createMessage(int type);

    NetworkStats getStats() const { return m_stats; }

    // Callbacks (replacing Qt signals)
    std::function<void(uint32_t)> onConnected;
    std::function<void(const std::string&)> onDisconnected;
    std::function<void(uint32_t, const CarStateData&)> onCarStateReceived;
    std::function<void(uint32_t, const std::string&, uint32_t)> onCarSpawned;
    std::function<void(uint32_t)> onCarDespawned;
    std::function<void(uint32_t, const std::string&, const std::string&)> onChatReceived;

private:
    void processMessages();

    std::unique_ptr<ksnet::Client> m_client;
    GameAdapter m_adapter;

    bool m_connected = false;
    uint32_t m_clientId = 0;
    double m_sendAccumulator = 0;
    double m_timeSinceLastPacket = 0;
    std::string m_driverName;
    std::string m_carName;
    bool m_joinSent = false;

    SimulationLoop* m_simLoop = nullptr;
    MultiCarManager* m_multiCar = nullptr;
    NetworkStats m_stats;
};

class NetworkServer {
public:
    explicit NetworkServer();
    ~NetworkServer();

    bool start(uint16_t port, const std::string& serverName, const std::string& trackName);
    void stop();
    bool isRunning() const { return m_running; }

    void setSimulationLoop(SimulationLoop* loop) { m_simLoop = loop; }
    void setMultiCarManager(MultiCarManager* m) { m_multiCar = m; }

    void update(double dt);
    void broadcastCarState(uint32_t carId, const CarStateData& state);
    void broadcastSessionState(uint8_t sessionType, uint8_t phase, int currentLap, int totalLaps, double timeRemaining);
    void broadcastLapTime(uint32_t carId, int lapNumber, double lapTime, double s1, double s2, double s3, bool valid);
    void broadcastPenalty(uint32_t carId, uint8_t penaltyType, float value, const std::string& reason);
    void sendMessage(int clientIndex, int channel, ksnet::Message* msg);
    ksnet::Message* createMessage(int clientIndex, int type);

    int getClientCount() const;
    std::string getClientName(int clientIndex) const;
    uint32_t getClientCarId(int clientIndex) const;

    // Callbacks (replacing Qt signals)
    std::function<void(int, uint32_t, const std::string&)> onClientConnected;
    std::function<void(int, const std::string&)> onClientDisconnected;

private:
    void processMessages();
    void spawnCarForClient(int clientIndex, const std::string& driverName, const std::string& carName);

    struct ClientSlot {
        bool connected = false;
        uint32_t carId = 0;
        std::string driverName;
        std::string carName;
    };

    std::unique_ptr<ksnet::Server> m_server;
    GameAdapter m_adapter;

    bool m_running = false;
    ClientSlot m_clients[MAX_CLIENTS];
    SimulationLoop* m_simLoop = nullptr;
    MultiCarManager* m_multiCar = nullptr;
};

} // namespace net
} // namespace ks::sim

#else

namespace ks::sim {
class SimulationLoop;
class MultiCarManager;
}

#endif
