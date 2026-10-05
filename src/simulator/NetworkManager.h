#pragma once

#include "NetworkLowLevel.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>

#if HAS_KSNET
#include "RemoteCarInterpolator.h"
#include "NetworkDamageSetupBridge.h"
#endif

namespace ks::sim {

class SimulationLoop;

#if HAS_KSNET

class NetworkManager {
public:
    explicit NetworkManager(SimulationLoop* simLoop);
    ~NetworkManager();

    void update(double dt);

    bool hostServer(uint16_t port = 40000, int maxClients = 8,
                    const std::string& serverName = "ksim Server",
                    const std::string& trackName = "Unknown");
    void stopServer();
    bool isHosting() const { return m_hosting; }
    int clientCount() const;
    std::string clientName(int index) const;

    /** Host: empty = open LAN; non-empty required on join (constant-time). */
    void setAuthToken(const std::string& token);
    bool authRequired() const;

    bool joinServer(const std::string& host, uint16_t port,
                    const std::string& driverName = "Player",
                    const std::string& carName = "gte3");
    /** Client: token sent in ClientJoinMessage. */
    void setJoinToken(const std::string& token);
    void disconnectFromServer();
    bool isConnected() const { return m_connected; }
    bool isClient() const { return m_connected && !m_hosting; }

    void sendChatMessage(const std::string& message);

    void broadcastSessionState(uint8_t type, uint8_t phase, int currentLap, int totalLaps, double timeRemaining);
    void broadcastRaceCountdown(int seconds);
    void broadcastLapTime(uint32_t carId, int lapNumber, double lapTime, double s1, double s2, double s3, bool valid);
    void broadcastPenalty(uint32_t carId, uint8_t penaltyType, float value, const std::string& reason);
    void broadcastCarDamage(const net::CarDamageMessage& msg);
    void broadcastCarSetup(const net::CarSetupMessage& msg);
    void broadcastCarCollision(const net::CarCollisionMessage& msg);
    void publishDamage(uint32_t carId, const ks::physics::DamageSystem& dmg);
    void publishSetup(uint32_t carId, const SetupData& setup);

    void setInterpolationDelay(double sec);
    double interpolationDelay() const;
    RemoteCarInterpolator& interpolator() { return m_interp; }
    const RemoteCarInterpolator& interpolator() const { return m_interp; }

    std::string serverName() const { return m_serverName; }
    std::string trackName() const { return m_trackName; }
    std::string localDriverName() const { return m_driverName; }
    std::string localCarName() const { return m_carName; }
    uint32_t localClientId() const { return m_localClientId; }
    net::NetworkStats stats() const { return m_stats; }

    net::NetworkClient* client() { return m_client.get(); }
    net::NetworkServer* server() { return m_server.get(); }

    std::function<void(uint16_t)> onServerStarted;
    std::function<void()> onServerStopped;
    std::function<void(const std::string&, uint16_t)> onClientConnectedToServer;
    std::function<void(const std::string&)> onDisconnectedFromServer;
    std::function<void(const std::string&)> onConnectionFailed;
    std::function<void(const std::string&)> onAuthFailed;

    std::function<void(int, uint32_t, const std::string&)> onRemoteClientJoined;
    std::function<void(int, const std::string&)> onRemoteClientLeft;

    std::function<void(uint32_t, const std::string&, const std::string&)> onChatMessageReceived;

    std::function<void(uint32_t, const std::string&, uint32_t)> onRemoteCarSpawned;
    std::function<void(uint32_t)> onRemoteCarDespawned;
    std::function<void(uint32_t, const net::CarStateData&)> onRemoteCarStateReceived;

    std::function<void(uint8_t, uint8_t, int, int, double)> onSessionStateReceived;
    std::function<void(int)> onRaceCountdownReceived;
    std::function<void(uint32_t, uint32_t, double, double, double, double, bool)> onLapTimeReceived;
    std::function<void(uint32_t, uint8_t, float, const std::string&)> onPenaltyReceived;
    std::function<void(const net::CarDamageMessage&)> onCarDamageReceived;
    std::function<void(const net::CarSetupMessage&)> onCarSetupReceived;
    std::function<void(const net::CarCollisionMessage&)> onCarCollisionReceived;

    std::function<void(const net::NetworkStats&)> onStatsUpdated;
    std::function<void(const std::vector<std::string>&)> onPlayerListUpdated;

private:
    void onStatsTimer();
    void setupClientSignals();
    void setupServerSignals();
    void updatePlayerList();
    void rebindMultiCar();
    void hostBroadcastCarStates();
    void deliverInterpolatedStates();

    SimulationLoop* m_simLoop = nullptr;
    std::unique_ptr<net::NetworkClient> m_client;
    std::unique_ptr<net::NetworkServer> m_server;

    bool m_hosting = false;
    bool m_connected = false;

    std::string m_serverName;
    std::string m_trackName;
    std::string m_driverName;
    std::string m_carName;
    uint16_t m_port = 0;
    uint32_t m_localClientId = 0;

    net::NetworkStats m_stats;

    std::chrono::steady_clock::time_point m_lastStatsPoll;
    static constexpr double STATS_POLL_INTERVAL = 0.5;

    double m_stateAccum = 0.0;
    static constexpr double STATE_SEND_HZ = 20.0;

    RemoteCarInterpolator m_interp;
    double m_clock = 0.0;
};

#else // !HAS_KSNET

namespace net {
struct NetworkStats { float rtt = 0; float packetLoss = 0; float sendBandwidth = 0; float recvBandwidth = 0; };
struct CarStateData { float posX=0,posY=0,posZ=0,rotX=0,rotY=0,rotZ=0,velX=0,velY=0,velZ=0,speed=0,rpm=0; int gear=0; float throttle=0,brake=0,steering=0; };
class NetworkClient {};
class NetworkServer {};
}

class NetworkManager {
public:
    explicit NetworkManager(SimulationLoop*) {}
    ~NetworkManager() = default;
    void update(double) {}
    bool hostServer(uint16_t = 40000, int = 8, const std::string& = {}, const std::string& = {}) { return false; }
    void stopServer() {}
    bool isHosting() const { return false; }
    int clientCount() const { return 0; }
    std::string clientName(int) const { return {}; }
    void setAuthToken(const std::string&) {}
    bool authRequired() const { return false; }
    bool joinServer(const std::string&, uint16_t, const std::string& = {}, const std::string& = {}) { return false; }
    void setJoinToken(const std::string&) {}
    void disconnectFromServer() {}
    bool isConnected() const { return false; }
    bool isClient() const { return false; }
    void sendChatMessage(const std::string&) {}
    void broadcastSessionState(uint8_t, uint8_t, int, int, double) {}
    void broadcastRaceCountdown(int) {}
    void broadcastLapTime(uint32_t, int, double, double, double, double, bool) {}
    void broadcastPenalty(uint32_t, uint8_t, float, const std::string&) {}
    void setInterpolationDelay(double) {}
    double interpolationDelay() const { return 0.1; }
    std::string serverName() const { return {}; }
    std::string trackName() const { return {}; }
    std::string localDriverName() const { return {}; }
    std::string localCarName() const { return {}; }
    uint32_t localClientId() const { return 0; }
    net::NetworkStats stats() const { return {}; }
    net::NetworkClient* client() { return nullptr; }
    net::NetworkServer* server() { return nullptr; }
    std::function<void(uint16_t)> onServerStarted;
    std::function<void()> onServerStopped;
    std::function<void(const std::string&, uint16_t)> onClientConnectedToServer;
    std::function<void(const std::string&)> onDisconnectedFromServer;
    std::function<void(const std::string&)> onConnectionFailed;
    std::function<void(const std::string&)> onAuthFailed;
    std::function<void(int, uint32_t, const std::string&)> onRemoteClientJoined;
    std::function<void(int, const std::string&)> onRemoteClientLeft;
    std::function<void(uint32_t, const std::string&, const std::string&)> onChatMessageReceived;
    std::function<void(uint32_t, const std::string&, uint32_t)> onRemoteCarSpawned;
    std::function<void(uint32_t)> onRemoteCarDespawned;
    std::function<void(uint32_t, const net::CarStateData&)> onRemoteCarStateReceived;
    std::function<void(uint8_t, uint8_t, int, int, double)> onSessionStateReceived;
    std::function<void(int)> onRaceCountdownReceived;
    std::function<void(uint32_t, uint32_t, double, double, double, double, bool)> onLapTimeReceived;
    std::function<void(uint32_t, uint8_t, float, const std::string&)> onPenaltyReceived;
    std::function<void(const net::NetworkStats&)> onStatsUpdated;
    std::function<void(const std::vector<std::string>&)> onPlayerListUpdated;
private:
    net::NetworkStats m_stats;
};

#endif

} // namespace ks::sim
