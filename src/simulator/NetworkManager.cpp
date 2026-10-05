#include "NetworkManager.h"
#include <cstdio>
#include <cstring>
#include <chrono>

#if HAS_KSNET
#include "SimulationLoop.h"
#include "MultiCarManager.h"
#endif

namespace ks::sim {

#if HAS_KSNET

NetworkManager::NetworkManager(SimulationLoop* simLoop)
    : m_simLoop(simLoop)
{
    m_client = std::make_unique<net::NetworkClient>();
    m_server = std::make_unique<net::NetworkServer>();

    m_client->setSimulationLoop(simLoop);
    m_server->setSimulationLoop(simLoop);
    rebindMultiCar();

    setupClientSignals();
    setupServerSignals();

    m_lastStatsPoll = std::chrono::steady_clock::now();
}

NetworkManager::~NetworkManager() {
    disconnectFromServer();
    stopServer();
}

void NetworkManager::setInterpolationDelay(double sec) {
    m_interp.setDelay(sec);
}

double NetworkManager::interpolationDelay() const {
    return m_interp.delay();
}

void NetworkManager::rebindMultiCar() {
    if (!m_simLoop) return;
    MultiCarManager* mc = m_simLoop->multiCarManager();
    if (!mc) return;
    if (m_client) m_client->setMultiCarManager(mc);
    if (m_server) m_server->setMultiCarManager(mc);
}

void NetworkManager::setupClientSignals() {
    m_client->onConnected = [this](uint32_t clientId) {
        m_connected = true;
        m_localClientId = clientId;
        printf("NetworkManager: Connected as client, id=%u\n", clientId);
        if (onClientConnectedToServer) onClientConnectedToServer(m_hosting ? "localhost" : "", m_port);
    };

    m_client->onDisconnected = [this](const std::string& reason) {
        m_connected = false;
        m_localClientId = 0;
        m_interp.clear();
        printf("NetworkManager: Disconnected - %s\n", reason.c_str());
        if (onDisconnectedFromServer) onDisconnectedFromServer(reason);
    };

    m_client->onChatReceived = [this](uint32_t senderId, const std::string& name, const std::string& msg) {
        if (onChatMessageReceived) onChatMessageReceived(senderId, name, msg);
    };

    m_client->onCarSpawned = [this](uint32_t carId, const std::string& name, uint32_t clientId) {
        if (onRemoteCarSpawned) onRemoteCarSpawned(carId, name, clientId);
    };

    m_client->onCarDespawned = [this](uint32_t carId) {
        m_interp.remove(carId);
        if (onRemoteCarDespawned) onRemoteCarDespawned(carId);
    };

    // Raw network ticks → snapshot buffer (delivery is interpolated in update).
    m_client->onCarStateReceived = [this](uint32_t carId, const net::CarStateData& state) {
        m_interp.push(carId, state, m_clock);
    };

    m_client->onSessionState = [this](uint8_t type, uint8_t phase, int cur, int total, double rem) {
        if (onSessionStateReceived) onSessionStateReceived(type, phase, cur, total, rem);
        if (m_simLoop && m_simLoop->onSessionStateChanged)
            m_simLoop->onSessionStateChanged(type, phase, cur, total, rem);
    };

    m_client->onRaceCountdown = [this](int seconds) {
        if (onRaceCountdownReceived) onRaceCountdownReceived(seconds);
        printf("NetworkManager: countdown %d\n", seconds);
    };

    m_client->onLapTime = [this](uint32_t carId, uint32_t lap, double t, double s1, double s2, double s3, bool valid) {
        if (onLapTimeReceived) onLapTimeReceived(carId, lap, t, s1, s2, s3, valid);
    };

    m_client->onPenalty = [this](uint32_t carId, uint8_t type, float value, const std::string& reason) {
        if (onPenaltyReceived) onPenaltyReceived(carId, type, value, reason);
        printf("NetworkManager: penalty car=%u type=%u value=%.1f (%s)\n",
               carId, (unsigned)type, value, reason.c_str());
    };
}

void NetworkManager::setupServerSignals() {
    m_server->onClientConnected = [this](int clientIndex, uint32_t clientId, const std::string& name) {
        printf("NetworkManager: Client %d joined - %s\n", clientIndex, name.c_str());
        if (onRemoteClientJoined) onRemoteClientJoined(clientIndex, clientId, name);
        updatePlayerList();
    };

    m_server->onClientDisconnected = [this](int clientIndex, const std::string& reason) {
        printf("NetworkManager: Client %d left - %s\n", clientIndex, reason.c_str());
        if (onRemoteClientLeft) onRemoteClientLeft(clientIndex, reason);
        updatePlayerList();
    };
}

bool NetworkManager::hostServer(uint16_t port, int /*maxClients*/,
                                 const std::string& serverName, const std::string& trackName) {
    if (m_hosting || m_connected) {
        printf("NetworkManager: Already connected/hosting\n");
        return false;
    }

    m_serverName = serverName;
    m_trackName = trackName;
    m_port = port;
    m_driverName = "Host";
    m_carName = "gte3";

    rebindMultiCar();

    if (!m_server->start(port, serverName, trackName)) {
        if (onConnectionFailed) onConnectionFailed("Failed to start server");
        return false;
    }

    m_hosting = true;
    m_connected = true;

    m_client->setSimulationLoop(m_simLoop);
    rebindMultiCar();

    if (!m_client->connect("127.0.0.1", port, "Host", "gte3")) {
        printf("NetworkManager: Failed to connect as local client\n");
    }

    m_lastStatsPoll = std::chrono::steady_clock::now();
    m_stateAccum = 0.0;
    m_interp.clear();
    if (onServerStarted) onServerStarted(port);

    printf("NetworkManager: Server started on port %u (car-state @ %.0f Hz, interp delay %.0f ms)\n",
           port, STATE_SEND_HZ, m_interp.delay() * 1000.0);
    updatePlayerList();
    return true;
}

void NetworkManager::stopServer() {
    if (!m_hosting) return;
    m_client->disconnect();
    m_server->stop();
    m_hosting = false;
    m_connected = false;
    m_interp.clear();
    if (onServerStopped) onServerStopped();
    printf("NetworkManager: Server stopped\n");
}

bool NetworkManager::joinServer(const std::string& host, uint16_t port,
                                 const std::string& driverName, const std::string& carName) {
    if (m_connected) {
        printf("NetworkManager: Already connected\n");
        return false;
    }

    m_driverName = driverName;
    m_carName = carName;
    m_port = port;

    m_client->setSimulationLoop(m_simLoop);
    rebindMultiCar();
    m_interp.clear();

    if (!m_client->connect(host, port, driverName, carName)) {
        if (onConnectionFailed) onConnectionFailed("Failed to connect to " + host);
        return false;
    }

    printf("NetworkManager: Connecting to %s:%u (interp delay %.0f ms)\n",
           host.c_str(), port, m_interp.delay() * 1000.0);
    return true;
}

void NetworkManager::disconnectFromServer() {
    if (!m_connected) return;
    if (m_hosting) return;
    m_client->disconnect();
    m_connected = false;
    m_interp.clear();
    if (onDisconnectedFromServer) onDisconnectedFromServer("Disconnected");
    printf("NetworkManager: Disconnected\n");
}

int NetworkManager::clientCount() const {
    if (m_hosting) return m_server->getClientCount();
    return m_connected ? 1 : 0;
}

std::string NetworkManager::clientName(int index) const {
    if (m_hosting) return m_server->getClientName(index);
    if (index == 0) return m_driverName;
    return {};
}

void NetworkManager::sendChatMessage(const std::string& message) {
    if (!m_connected || !m_client) return;
    auto* chat = (net::ChatMessage*)m_client->createMessage(net::MSG_CHAT);
    if (!chat) return;
    chat->senderId = 0;
    strncpy(chat->senderName, m_driverName.c_str(), sizeof(chat->senderName) - 1);
    strncpy(chat->message, message.c_str(), sizeof(chat->message) - 1);
    m_client->sendMessage(net::CHANNEL_RELIABLE, chat);
}

void NetworkManager::broadcastSessionState(uint8_t type, uint8_t phase, int currentLap, int totalLaps, double timeRemaining) {
    if (m_hosting && m_server)
        m_server->broadcastSessionState(type, phase, currentLap, totalLaps, timeRemaining);
}

void NetworkManager::broadcastRaceCountdown(int seconds) {
    if (m_hosting && m_server)
        m_server->broadcastRaceCountdown(seconds);
}

void NetworkManager::broadcastLapTime(uint32_t carId, int lapNumber, double lapTime, double s1, double s2, double s3, bool valid) {
    if (m_hosting && m_server)
        m_server->broadcastLapTime(carId, lapNumber, lapTime, s1, s2, s3, valid);
}

void NetworkManager::broadcastPenalty(uint32_t carId, uint8_t penaltyType, float value, const std::string& reason) {
    if (m_hosting && m_server)
        m_server->broadcastPenalty(carId, penaltyType, value, reason);
}

void NetworkManager::hostBroadcastCarStates() {
    if (!m_hosting || !m_server || !m_simLoop) return;
    MultiCarManager* mc = m_simLoop->multiCarManager();
    if (!mc) return;
    if (clientCount() <= 1) return;

    net::CarStateData wire{};
    for (const auto& entry : mc->cars()) {
        if (!entry || !entry->isActive || !entry->vehicle) continue;
        const auto s = entry->vehicle->getState();
        wire.carId = static_cast<uint32_t>(entry->id);
        wire.posX = static_cast<float>(s.position.x);
        wire.posY = static_cast<float>(s.position.y);
        wire.posZ = static_cast<float>(s.position.z);
        wire.rotX = static_cast<float>(s.rotation.x);
        wire.rotY = static_cast<float>(s.rotation.y);
        wire.rotZ = static_cast<float>(s.rotation.z);
        wire.velX = static_cast<float>(s.velocity.x);
        wire.velY = static_cast<float>(s.velocity.y);
        wire.velZ = static_cast<float>(s.velocity.z);
        wire.speed = static_cast<float>(s.speed);
        wire.rpm = static_cast<float>(s.rpm);
        wire.gear = s.gear;
        wire.throttle = s.throttle;
        wire.brake = s.brake;
        wire.steering = s.steering;
        m_server->broadcastCarState(wire.carId, wire);
    }
}

void NetworkManager::deliverInterpolatedStates() {
    if (!onRemoteCarStateReceived) return;
    // Clients always sample; host loopback also samples remote cars so the
    // same apply path (handleRemoteCarState) stays consistent.
    if (!m_connected) return;
    m_interp.sampleAll(m_clock, [this](uint32_t carId, const net::CarStateData& state) {
        onRemoteCarStateReceived(carId, state);
    });
}

void NetworkManager::update(double dt) {
    rebindMultiCar();
    m_clock += dt;

    if (m_server) m_server->update(dt);
    if (m_client) m_client->update(dt);

    if (m_hosting) {
        m_stateAccum += dt;
        const double period = 1.0 / STATE_SEND_HZ;
        if (m_stateAccum >= period) {
            m_stateAccum = 0.0;
            hostBroadcastCarStates();
        }
    }

    deliverInterpolatedStates();

    auto now = std::chrono::steady_clock::now();
    if (m_connected &&
        std::chrono::duration<double>(now - m_lastStatsPoll).count() >= STATS_POLL_INTERVAL) {
        m_lastStatsPoll = now;
        onStatsTimer();
    }
}

void NetworkManager::onStatsTimer() {
    if (m_connected && m_client) {
        m_stats = m_client->getStats();
        if (onStatsUpdated) onStatsUpdated(m_stats);
    }
}

void NetworkManager::updatePlayerList() {
    std::vector<std::string> players;
    if (m_hosting) {
        players.push_back("Host (You)");
        for (int i = 0; i < m_server->getClientCount(); ++i)
            players.push_back(m_server->getClientName(i));
    } else if (m_connected) {
        players.push_back(m_driverName);
    }
    if (onPlayerListUpdated) onPlayerListUpdated(players);
}

#else // !HAS_KSNET

#endif

} // namespace ks::sim
