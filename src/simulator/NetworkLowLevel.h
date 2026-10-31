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

namespace detail {

inline int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** Decode exactly outLen bytes from a lowercase/uppercase hex string. */
inline bool parseHexKey(const std::string& hex, uint8_t* out, int outLen) {
    if (static_cast<int>(hex.size()) != outLen * 2) return false;
    for (int i = 0; i < outLen; ++i) {
        const int hi = hexNibble(hex[static_cast<size_t>(i) * 2]);
        const int lo = hexNibble(hex[static_cast<size_t>(i) * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

inline bool isValidHexKey(const std::string& hex, int outLen) {
    if (static_cast<int>(hex.size()) != outLen * 2) return false;
    for (char c : hex) {
        if (hexNibble(c) < 0) return false;
    }
    return true;
}

} // namespace detail

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
     * Shared-secret transport key as 64 hex chars (32 bytes, ksnet::PRIVATE_KEY_BYTES).
     * Empty clears secure mode. Takes effect on the next connect().
     * Returns false (and stays insecure) when the hex string is malformed.
     */
    bool setPrivateKey(const std::string& hexKey);
    void clearPrivateKey();
    bool hasPrivateKey() const { return m_hasPrivateKey; }
    /** True once the transport handshake completed with a shared key. */
    bool isSecure() const;

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
    uint8_t m_privateKey[ksnet::PRIVATE_KEY_BYTES] = {};
    bool m_hasPrivateKey = false;

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
     * Shared-secret transport key as 64 hex chars (32 bytes, ksnet::PRIVATE_KEY_BYTES).
     * Call before start(); empty clears secure mode.
     * Returns false (and leaves the current mode untouched) on malformed input.
     */
    bool setPrivateKey(const std::string& hexKey);
    void clearPrivateKey();
    bool hasPrivateKey() const { return m_hasPrivateKey; }
    bool isSecure() const;

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
    uint8_t m_privateKey[ksnet::PRIVATE_KEY_BYTES] = {};
    bool m_hasPrivateKey = false;
};

} // namespace net
} // namespace ks::sim

#else

namespace ks::sim {
class SimulationLoop;
class MultiCarManager;
}

#endif
