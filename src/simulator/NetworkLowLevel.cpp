#include "NetworkLowLevel.h"
#include <cstring>

#if HAS_KSNET

#include "SimulationLoop.h"
#include "InputManager.h"
#include "MultiCarManager.h"
#include "engine/physics/VehiclePhysics.h"

// windows.h (via the includes above) renames SendMessage to SendMessageA;
// our ksnet calls use the real method name.
#ifdef SendMessage
#undef SendMessage
#endif

namespace ks::sim::net {

// ============================================================================
// NetworkClient
// ============================================================================

NetworkClient::NetworkClient() {}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const std::string& address, uint16_t port,
                             const std::string& driverName, const std::string& carName) {
    if (m_connected) disconnect();

    ksnet::Address serverAddress(address.c_str(), port);

    ksnet::ClientServerConfig config;
    config.protocolId = PROTOCOL_ID;
    config.numChannels = 2;
    config.channel[0].type = ksnet::CHANNEL_TYPE_UNRELIABLE_UNORDERED;
    config.channel[0].packetBudget = 256;
    config.channel[1].type = ksnet::CHANNEL_TYPE_RELIABLE_ORDERED;
    config.channel[1].packetBudget = 1024;

    ksnet::DefaultAllocator allocator;
    ksnet::Address clientAddress("0.0.0.0", 0);
    m_client = std::make_unique<ksnet::Client>(allocator, clientAddress,
                                                  config, m_adapter, 0.0);
    if (!m_client) return false;

    if (!m_client->InsecureConnect(nullptr, 0, serverAddress)) {
        m_client.reset();
        return false;
    }

    m_driverName = driverName;
    m_carName = carName;
    m_joinSent = false;
    m_connected = true;
    m_sendAccumulator = 0;
    m_timeSinceLastPacket = 0;

    return true;
}

void NetworkClient::disconnect() {
    if (m_client) {
        m_client->Disconnect();
        m_client.reset();
    }
    m_connected = false;
    m_clientId = 0;
}

bool NetworkClient::isConnected() const {
    return m_connected && m_client &&
           m_client->GetClientState() == ksnet::CLIENT_STATE_CONNECTED;
}

void NetworkClient::update(double dt) {
    if (!m_connected || !m_client) return;

    m_client->SendPackets();
    m_client->ReceivePackets();

    processMessages();

    ksnet::ClientState state = m_client->GetClientState();
    if (state == ksnet::CLIENT_STATE_DISCONNECTED ||
        state == ksnet::CLIENT_STATE_ERROR) {
        m_connected = false;
        if (onDisconnected) onDisconnected("Disconnected from server");
        return;
    }

    // The transport handshake alone is not enough: the server only answers
    // with MSG_SERVER_WELCOME after an explicit MSG_CLIENT_JOIN, so send it
    // the first time we observe the CONNECTED state (and on every reconnect,
    // since connect() clears m_joinSent).
    if (state == ksnet::CLIENT_STATE_CONNECTED && !m_joinSent) {
        m_joinSent = true;
        auto* join = (ClientJoinMessage*)m_client->CreateMessage(MSG_CLIENT_JOIN);
        if (join) {
            strncpy(join->driverName, m_driverName.c_str(), sizeof(join->driverName) - 1);
            strncpy(join->carName, m_carName.c_str(), sizeof(join->carName) - 1);
            join->clientVersion = PROTOCOL_VERSION;
            m_client->SendMessage(CHANNEL_RELIABLE, join);
        }
    }

    m_sendAccumulator += dt;
    if (m_simLoop && m_sendAccumulator >= (1.0 / 60.0) && isConnected()) {
        m_sendAccumulator = 0.0;

        PlayerInputMessage* msg = (PlayerInputMessage*)m_client->CreateMessage(MSG_PLAYER_INPUT);
        if (msg) {
            msg->data.throttle = m_simLoop->inputManager()->throttle();
            msg->data.brake = m_simLoop->inputManager()->brake();
            msg->data.steering = m_simLoop->inputManager()->steer();
            msg->data.frameNumber = m_clientId;
            m_client->SendMessage(CHANNEL_UNRELIABLE, msg);
        }
    }

    m_timeSinceLastPacket += dt;

    ksnet::NetworkInfo info;
    m_client->GetNetworkInfo(info);
    m_stats.rtt = info.RTT;
    m_stats.packetLoss = info.packetLoss;
    m_stats.sendBandwidth = info.sentBandwidth;
    m_stats.recvBandwidth = info.receivedBandwidth;
}

void NetworkClient::processMessages() {
    for (int channel = 0; channel < 2; ++channel) {
        ksnet::Message* msg = nullptr;
        while ((msg = m_client->ReceiveMessage(channel)) != nullptr) {
            m_timeSinceLastPacket = 0;
            int type = msg->GetType();

            switch (type) {
                case MSG_SERVER_WELCOME: {
                    auto* welcome = (ServerWelcomeMessage*)msg;
                    m_clientId = welcome->clientId;
                    if (onConnected) onConnected(m_clientId);
                    break;
                }
                case MSG_SERVER_FULL:
                    if (onDisconnected) onDisconnected("Server is full");
                    break;
                case MSG_PROTOCOL_MISMATCH:
                    if (onDisconnected) onDisconnected("Protocol mismatch");
                    break;
                case MSG_CAR_STATE: {
                    auto* cs = (CarStateMessage*)msg;
                    if (onCarStateReceived) onCarStateReceived(cs->data.carId, cs->data);
                    break;
                }
                case MSG_CAR_SPAWN: {
                    auto* sp = (CarSpawnMessage*)msg;
                    if (onCarSpawned) onCarSpawned(sp->carId, std::string(sp->driverName), sp->clientId);
                    break;
                }
                case MSG_CAR_DESPAWN: {
                    auto* dp = (CarDespawnMessage*)msg;
                    if (onCarDespawned) onCarDespawned(dp->carId);
                    break;
                }
                case MSG_CHAT: {
                    auto* ch = (ChatMessage*)msg;
                    if (onChatReceived) onChatReceived(ch->senderId,
                                     std::string(ch->senderName),
                                     std::string(ch->message));
                    break;
                }
                case MSG_SESSION_STATE: {
                    auto* ss = (SessionStateMessage*)msg;
                    break;
                }
                case MSG_RACE_COUNTDOWN: {
                    auto* cd = (RaceCountdownMessage*)msg;
                    (void)cd;
                    break;
                }
                case MSG_LAP_TIME: {
                    auto* lt = (LapTimeMessage*)msg;
                    (void)lt;
                    break;
                }
                case MSG_PENALTY: {
                    auto* pen = (PenaltyMessage*)msg;
                    (void)pen;
                    break;
                }
                default: break;
            }

            m_client->ReleaseMessage(msg);
        }
    }
}

void NetworkClient::sendInput(const InputData& input) {
    if (!isConnected()) return;
    PlayerInputMessage* msg = (PlayerInputMessage*)m_client->CreateMessage(MSG_PLAYER_INPUT);
    if (msg) {
        msg->data = input;
        m_client->SendMessage(CHANNEL_UNRELIABLE, msg);
    }
}

void NetworkClient::sendMessage(int channel, ksnet::Message* msg) {
    if (!isConnected() || !msg) return;
    m_client->SendMessage(channel, msg);
}

ksnet::Message* NetworkClient::createMessage(int type) {
    if (!m_client) return nullptr;
    return m_client->CreateMessage(type);
}

// ============================================================================
// NetworkServer
// ============================================================================

NetworkServer::NetworkServer() {}

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start(uint16_t port, const std::string& serverName, const std::string& trackName) {
    if (m_running) stop();

    ksnet::Address serverAddress("0.0.0.0", port);

    ksnet::ClientServerConfig config;
    config.protocolId = PROTOCOL_ID;
    config.numChannels = 2;
    config.channel[0].type = ksnet::CHANNEL_TYPE_UNRELIABLE_UNORDERED;
    config.channel[0].packetBudget = 256;
    config.channel[1].type = ksnet::CHANNEL_TYPE_RELIABLE_ORDERED;
    config.channel[1].packetBudget = 1024;

    ksnet::DefaultAllocator allocator;
    m_server = std::make_unique<ksnet::Server>(allocator, nullptr, serverAddress,
                                                  config, m_adapter, 0.0);
    if (!m_server) return false;

    if (!m_server->Start(MAX_CLIENTS)) {
        m_server.reset();
        return false;
    }

    m_running = true;
    return true;
}

void NetworkServer::stop() {
    if (m_server) {
        m_server->Stop();
        m_server.reset();
    }
    m_running = false;
    for (auto& c : m_clients) c = ClientSlot{};
}

void NetworkServer::update(double dt) {
    if (!m_running || !m_server) return;

    m_server->SendPackets();
    m_server->ReceivePackets();
    processMessages();

    // Transport-level drops (client timeout or explicit DISCONNECT) surface
    // here: the slot is gone but the app-level join bookkeeping still holds.
    // Notify, despawn the car and free the slot.
    for (int ci = 0; ci < m_server->GetMaxClients(); ++ci) {
        if (!m_clients[ci].connected) continue;
        if (m_server->IsClientConnected(ci)) continue;

        const uint32_t carId = m_clients[ci].carId;
        m_clients[ci] = ClientSlot{};
        if (m_multiCar && carId) m_multiCar->removeCar(static_cast<int>(carId));

        if (carId) {
            for (int dest = 0; dest < m_server->GetMaxClients(); ++dest) {
                if (!m_server->IsClientConnected(dest)) continue;
                if (!m_server->CanSendMessage(dest, CHANNEL_RELIABLE)) continue;
                auto* dp = (CarDespawnMessage*)m_server->CreateMessage(dest, MSG_CAR_DESPAWN);
                if (dp) {
                    dp->carId = carId;
                    m_server->SendMessage(dest, CHANNEL_RELIABLE, dp);
                }
            }
        }

        if (onClientDisconnected) onClientDisconnected(ci, "Disconnected");
    }
    (void)dt;
}

void NetworkServer::processMessages() {
    for (int ci = 0; ci < m_server->GetMaxClients(); ++ci) {
        if (!m_server->IsClientConnected(ci)) continue;

        for (int ch = 0; ch < 2; ++ch) {
            ksnet::Message* msg = nullptr;
            while ((msg = m_server->ReceiveMessage(ci, ch)) != nullptr) {
                int type = msg->GetType();

                switch (type) {
                    case MSG_CLIENT_JOIN: {
                        auto* join = (ClientJoinMessage*)msg;
                        if (join->clientVersion != PROTOCOL_VERSION) {
                            if (m_server->CanSendMessage(ci, CHANNEL_RELIABLE)) {
                                auto* reply = (ProtocolMismatchMessage*)m_server->CreateMessage(ci, MSG_PROTOCOL_MISMATCH);
                                if (reply) m_server->SendMessage(ci, CHANNEL_RELIABLE, reply);
                            }
                        } else {
                            m_clients[ci].connected = true;
                            m_clients[ci].driverName = std::string(join->driverName);
                            m_clients[ci].carName = std::string(join->carName);

                            if (m_server->CanSendMessage(ci, CHANNEL_RELIABLE)) {
                                auto* welcome = (ServerWelcomeMessage*)m_server->CreateMessage(ci, MSG_SERVER_WELCOME);
                                if (welcome) {
                                    welcome->clientId = ci;
                                    strncpy(welcome->serverName, "ksEditor Server", sizeof(welcome->serverName) - 1);
                                    strncpy(welcome->trackName, "Unknown", sizeof(welcome->trackName) - 1);
                                    m_server->SendMessage(ci, CHANNEL_RELIABLE, welcome);
                                }
                            }

                            spawnCarForClient(ci, m_clients[ci].driverName, m_clients[ci].carName);
                            if (onClientConnected) onClientConnected(ci, ci, m_clients[ci].driverName);
                        }
                        break;
                    }
                    case MSG_PLAYER_INPUT: {
                        auto* input = (PlayerInputMessage*)msg;
                        if (m_simLoop) {
                            m_simLoop->applyRemoteInput(ci, input->data);
                        }
                        break;
                    }
                    case MSG_CHAT: {
                        auto* chat = (ChatMessage*)msg;
                        for (int dest = 0; dest < m_server->GetMaxClients(); ++dest) {
                            if (!m_server->IsClientConnected(dest)) continue;
                            if (!m_server->CanSendMessage(dest, CHANNEL_RELIABLE)) continue;
                            auto* echo = (ChatMessage*)m_server->CreateMessage(dest, MSG_CHAT);
                            if (echo) {
                                echo->senderId = ci;
                                strncpy(echo->senderName, m_clients[ci].driverName.c_str(), sizeof(echo->senderName) - 1);
                                strncpy(echo->message, chat->message, sizeof(echo->message) - 1);
                                m_server->SendMessage(dest, CHANNEL_RELIABLE, echo);
                            }
                        }
                        break;
                    }
                    case MSG_LAP_TIME: {
                        auto* lap = (LapTimeMessage*)msg;
                        for (int dest = 0; dest < m_server->GetMaxClients(); ++dest) {
                            if (dest == ci) continue;
                            if (!m_server->IsClientConnected(dest)) continue;
                            if (!m_server->CanSendMessage(dest, CHANNEL_RELIABLE)) continue;
                            auto* relay = (LapTimeMessage*)m_server->CreateMessage(dest, MSG_LAP_TIME);
                            if (relay) {
                                relay->carId = lap->carId;
                                relay->lapNumber = lap->lapNumber;
                                relay->lapTime = lap->lapTime;
                                relay->sector1 = lap->sector1;
                                relay->sector2 = lap->sector2;
                                relay->sector3 = lap->sector3;
                                relay->isValid = lap->isValid;
                                m_server->SendMessage(dest, CHANNEL_RELIABLE, relay);
                            }
                        }
                        break;
                    }
                    default: break;
                }

                m_server->ReleaseMessage(ci, msg);
            }
        }
    }
}

void NetworkServer::broadcastCarState(uint32_t carId, const CarStateData& state) {
    if (!m_running || !m_server) return;

    for (int ci = 0; ci < m_server->GetMaxClients(); ++ci) {
        if (!m_server->IsClientConnected(ci)) continue;
        if (!m_server->CanSendMessage(ci, CHANNEL_UNRELIABLE)) continue;

        CarStateMessage* msg = (CarStateMessage*)m_server->CreateMessage(ci, MSG_CAR_STATE);
        if (msg) {
            msg->data = state;
            msg->data.carId = carId;
            m_server->SendMessage(ci, CHANNEL_UNRELIABLE, msg);
        }
    }
}

void NetworkServer::broadcastSessionState(uint8_t sessionType, uint8_t phase, int currentLap, int totalLaps, double timeRemaining) {
    if (!m_running || !m_server) return;

    for (int ci = 0; ci < m_server->GetMaxClients(); ++ci) {
        if (!m_server->IsClientConnected(ci)) continue;
        if (!m_server->CanSendMessage(ci, CHANNEL_RELIABLE)) continue;

        auto* msg = (SessionStateMessage*)m_server->CreateMessage(ci, MSG_SESSION_STATE);
        if (msg) {
            msg->type = sessionType;
            msg->phase = phase;
            msg->currentLap = currentLap;
            msg->totalLaps = totalLaps;
            msg->timeRemaining = timeRemaining;
            m_server->SendMessage(ci, CHANNEL_RELIABLE, msg);
        }
    }
}

void NetworkServer::broadcastLapTime(uint32_t carId, int lapNumber, double lapTime, double s1, double s2, double s3, bool valid) {
    if (!m_running || !m_server) return;

    for (int ci = 0; ci < m_server->GetMaxClients(); ++ci) {
        if (!m_server->IsClientConnected(ci)) continue;
        if (!m_server->CanSendMessage(ci, CHANNEL_RELIABLE)) continue;

        auto* msg = (LapTimeMessage*)m_server->CreateMessage(ci, MSG_LAP_TIME);
        if (msg) {
            msg->carId = carId;
            msg->lapNumber = lapNumber;
            msg->lapTime = lapTime;
            msg->sector1 = s1;
            msg->sector2 = s2;
            msg->sector3 = s3;
            msg->isValid = valid;
            m_server->SendMessage(ci, CHANNEL_RELIABLE, msg);
        }
    }
}

void NetworkServer::broadcastPenalty(uint32_t carId, uint8_t penaltyType, float value, const std::string& reason) {
    if (!m_running || !m_server) return;

    for (int ci = 0; ci < m_server->GetMaxClients(); ++ci) {
        if (!m_server->IsClientConnected(ci)) continue;
        if (!m_server->CanSendMessage(ci, CHANNEL_RELIABLE)) continue;

        auto* msg = (PenaltyMessage*)m_server->CreateMessage(ci, MSG_PENALTY);
        if (msg) {
            msg->carId = carId;
            msg->penaltyType = penaltyType;
            msg->value = value;
            strncpy(msg->reason, reason.c_str(), sizeof(msg->reason) - 1);
            m_server->SendMessage(ci, CHANNEL_RELIABLE, msg);
        }
    }
}

void NetworkServer::spawnCarForClient(int clientIndex, const std::string& driverName, const std::string& carName) {
    if (!m_multiCar) return;

    float spawnZ = -clientIndex * 5.0f;
    uint32_t carId = m_multiCar->addCar(carName, driverName, vec3(0, 0.5f, spawnZ), false);
    m_clients[clientIndex].carId = carId;

    for (int dest = 0; dest < m_server->GetMaxClients(); ++dest) {
        if (!m_server->IsClientConnected(dest)) continue;
        if (!m_server->CanSendMessage(dest, CHANNEL_RELIABLE)) continue;
        CarSpawnMessage* msg = (CarSpawnMessage*)m_server->CreateMessage(dest, MSG_CAR_SPAWN);
        if (msg) {
            msg->carId = carId;
            msg->clientId = clientIndex;
            strncpy(msg->driverName, driverName.c_str(), sizeof(msg->driverName) - 1);
            strncpy(msg->carName, carName.c_str(), sizeof(msg->carName) - 1);
            msg->posX = 0;
            msg->posY = 0.5f;
            msg->posZ = spawnZ;
            m_server->SendMessage(dest, CHANNEL_RELIABLE, msg);
        }
    }

    // A client only hears about cars that join *after* it: replay the grid
    // that is already on track straight to the newcomer, otherwise a late
    // joiner would race an empty session.
    for (int other = 0; other < m_server->GetMaxClients(); ++other) {
        if (other == clientIndex) continue;
        if (!m_server->IsClientConnected(other)) continue;
        const uint32_t otherCar = m_clients[other].carId;
        if (otherCar == 0) continue;
        if (!m_server->CanSendMessage(clientIndex, CHANNEL_RELIABLE)) break;
        CarSpawnMessage* msg =
            (CarSpawnMessage*)m_server->CreateMessage(clientIndex, MSG_CAR_SPAWN);
        if (!msg) continue;
        msg->carId = otherCar;
        msg->clientId = other;
        strncpy(msg->driverName, m_clients[other].driverName.c_str(),
                sizeof(msg->driverName) - 1);
        strncpy(msg->carName, m_clients[other].carName.c_str(), sizeof(msg->carName) - 1);
        msg->posX = 0;
        msg->posY = 0.5f;
        msg->posZ = -other * 5.0f;
        m_server->SendMessage(clientIndex, CHANNEL_RELIABLE, msg);
    }
}

int NetworkServer::getClientCount() const {
    if (!m_server) return 0;
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (m_server->IsClientConnected(i)) count++;
    }
    return count;
}

std::string NetworkServer::getClientName(int clientIndex) const {
    if (clientIndex >= 0 && clientIndex < MAX_CLIENTS)
        return m_clients[clientIndex].driverName;
    return {};
}

uint32_t NetworkServer::getClientCarId(int clientIndex) const {
    if (clientIndex >= 0 && clientIndex < MAX_CLIENTS)
        return m_clients[clientIndex].carId;
    return 0;
}

void NetworkServer::sendMessage(int clientIndex, int channel, ksnet::Message* msg) {
    if (!m_running || !m_server || !msg) return;
    if (!m_server->IsClientConnected(clientIndex)) return;
    m_server->SendMessage(clientIndex, channel, msg);
}

ksnet::Message* NetworkServer::createMessage(int clientIndex, int type) {
    if (!m_server) return nullptr;
    return m_server->CreateMessage(clientIndex, type);
}

} // namespace ks::sim::net

#endif
