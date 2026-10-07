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
    /**
     * Optional 32-byte shared private key for ksnet SecureConnect.
     * Empty = InsecureConnect (LAN default). Must match host SetPrivateKey.
     */
    void setPrivateKey(const uint8_t* key, int keyLength);
    void clearPrivateKey();
    bool hasPrivateKey() const { return m_hasPrivateKey; }

    void update(double dt);
    void sendInput(const InputData& input);
    void sendMessage(int channel, ksnet::Message* msg);
    ksnet::Message* createMessage(int type);

    NetworkStats getStats() const { return m_stats; }
    uint32_t clientId() const { return m_clientId; }

    // Callbacks (replacing Qt signals)
    std::function<void(uint32_t)> onConnected;
    std::function<void(const std::string&)> onDisconnected;
    std::function<void(uint32_t, const CarStateData&)> onCarStateReceived;
    std::function<void(uint32_t, const std::string&, uint32_t)> onCarSpawned;
    std::function<void(uint32_t)> onCarDespawned;
    std::function<void(uint32_t, const std::string&, const std::string&)> onChatReceived;
    std::function<void(uint8_t /*type*/, uint8_t /*phase*/, int /*curLap*/, int /*totalLaps*/, double /*timeRem*/)> onSessionState;
    std::function<void(int /*seconds*/)> onRaceCountdown;
    std::function<void(uint32_t /*carId*/, uint32_t /*lap*/, double /*time*/, double s1, double s2, double s3, bool valid)> onLapTime;
    std::function<void(uint32_t /*carId*/, uint8_t /*type*/, float /*value*/, const std::string& /*reason*/)> onPenalty;
    std::function<void(const CarDamageMessage&)> onCarDamage;
    std::function<void(const CarSetupMessage&)> onCarSetup;
    std::function<void(const CarCollisionMessage&)> onCarCollision;
    std::function<void(const std::string& /*reason*/)> onAuthFailed;

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
    std::string m_joinToken;
    bool m_joinSent = false;
    bool m_hasPrivateKey = false;
    uint8_t m_privateKey[32] = {};

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
    /** Empty = open; non-empty required on ClientJoin (constant-time compare). */
    void setAuthToken(const std::string& token) { m_authToken = token; m_authRequired = !token.empty(); }
    bool authRequired() const { return m_authRequired; }
    /**
     * Optional 32-byte shared private key for ksnet secure mode.
     * Call before start(). Empty/null = InsecureConnect clients only.
     */
    void setPrivateKey(const uint8_t* key, int keyLength);
    void clearPrivateKey();
    bool hasPrivateKey() const { return m_hasPrivateKey; }

    void update(double dt);
    void broadcastCarState(uint32_t carId, const CarStateData& state);
    void broadcastSessionState(uint8_t sessionType, uint8_t phase, int currentLap, int totalLaps, double timeRemaining);
    void broadcastRaceCountdown(int seconds);
    void broadcastLapTime(uint32_t carId, int lapNumber, double lapTime, double s1, double s2, double s3, bool valid);
    void broadcastPenalty(uint32_t carId, uint8_t penaltyType, float value, const std::string& reason);
    void broadcastCarDamage(const CarDamageMessage& msg);
    void broadcastCarSetup(const CarSetupMessage& msg);
    void broadcastCarCollision(const CarCollisionMessage& msg);
    void sendMessage(int clientIndex, int channel, ksnet::Message* msg);
    ksnet::Message* createMessage(int clientIndex, int type);

    int getClientCount() const;
    std::string getClientName(int clientIndex) const;
    uint32_t getClientCarId(int clientIndex) const;
    const std::string& serverName() const { return m_serverName; }
    const std::string& trackName() const { return m_trackName; }

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
    std::string m_serverName;
    std::string m_trackName;
    std::string m_authToken;
    bool m_authRequired = false;
    bool m_hasPrivateKey = false;
    uint8_t m_privateKey[32] = {};
};

} // namespace net
} // namespace ks::sim

#else

namespace ks::sim {
class SimulationLoop;
class MultiCarManager;
}

#endif
