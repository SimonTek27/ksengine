#include "NetworkAuth.h"
NetworkServer::NetworkServer() {}

NetworkServer::~NetworkServer() {
    stop();
    if (m_hasPrivateKey) {
        std::memset(m_privateKey, 0, sizeof(m_privateKey));
        m_hasPrivateKey = false;
    }
}

void NetworkServer::setPrivateKey(const uint8_t* key, int keyLength) {
    if (!key || keyLength != 32) {
        clearPrivateKey();
        return;
    }
    std::memcpy(m_privateKey, key, 32);
    m_hasPrivateKey = true;
    if (m_server) m_server->SetPrivateKey(m_privateKey, 32);
}

void NetworkServer::clearPrivateKey() {
    std::memset(m_privateKey, 0, sizeof(m_privateKey));
    m_hasPrivateKey = false;
    if (m_server) m_server->ClearPrivateKey();
}

bool NetworkServer::start(uint16_t port, const std::string& serverName, const std::string& trackName) {
    if (m_running) stop();

    m_serverName = serverName.empty() ? "ksim Server" : serverName;
    m_trackName = trackName.empty() ? "Unknown" : trackName;

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

    if (m_hasPrivateKey) {
        if (!m_server->SetPrivateKey(m_privateKey, 32)) {
            m_server.reset();
            return false;
        }
    }

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
                        } else if (m_authRequired &&
                                   !tokenEquals(join->authToken, m_authToken.c_str())) {
                            if (m_server->CanSendMessage(ci, CHANNEL_RELIABLE)) {
                                auto* fail = (AuthFailedMessage*)m_server->CreateMessage(ci, MSG_AUTH_FAILED);
                                if (fail) {
                                    strncpy(fail->reason, "Invalid auth token", sizeof(fail->reason) - 1);
                                    m_server->SendMessage(ci, CHANNEL_RELIABLE, fail);
                                }
                            }
                            // Drop connection after auth failure
                            m_server->DisconnectClient(ci);
                        } else {
                            m_clients[ci].connected = true;
                            m_clients[ci].driverName = std::string(join->driverName);
                            m_clients[ci].carName = std::string(join->carName);

                            if (m_server->CanSendMessage(ci, CHANNEL_RELIABLE)) {
                                auto* welcome = (ServerWelcomeMessage*)m_server->CreateMessage(ci, MSG_SERVER_WELCOME);
                                if (welcome) {
                                    welcome->clientId = static_cast<uint32_t>(ci);
                                    strncpy(welcome->serverName, m_serverName.c_str(), sizeof(welcome->serverName) - 1);
                                    strncpy(welcome->trackName, m_trackName.c_str(), sizeof(welcome->trackName) - 1);
                                    m_server->SendMessage(ci, CHANNEL_RELIABLE, welcome);
                                }
                            }

                            spawnCarForClient(ci, m_clients[ci].driverName, m_clients[ci].carName);
                            if (onClientConnected)
                                onClientConnected(ci, static_cast<uint32_t>(ci), m_clients[ci].driverName);
                        }
                        break;
                    }
                    case MSG_PLAYER_INPUT: {
                        auto* input = (PlayerInputMessage*)msg;
                        if (m_simLoop) m_simLoop->applyRemoteInput(ci, input->data);
                        break;
                    }
                    case MSG_CHAT: {
                        auto* chat = (ChatMessage*)msg;
                        for (int dest = 0; dest < m_server->GetMaxClients(); ++dest) {
                            if (!m_server->IsClientConnected(dest)) continue;
                            if (!m_server->CanSendMessage(dest, CHANNEL_RELIABLE)) continue;
                            auto* echo = (ChatMessage*)m_server->CreateMessage(dest, MSG_CHAT);
                            if (echo) {
                                echo->senderId = static_cast<uint32_t>(ci);
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
                                *relay = *lap;
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

