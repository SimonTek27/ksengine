#include "NetworkLowLevel.h"
#include <cstring>

#if HAS_KSNET

#include "SimulationLoop.h"
#include "InputManager.h"
#include "MultiCarManager.h"
#include "engine/physics/VehiclePhysics.h"

#ifdef SendMessage
#undef SendMessage
#endif

namespace ks::sim::net {

NetworkClient::NetworkClient() {}

NetworkClient::~NetworkClient() {
    disconnect();
    if (m_hasPrivateKey) {
        std::memset(m_privateKey, 0, sizeof(m_privateKey));
        m_hasPrivateKey = false;
    }
}

void NetworkClient::setPrivateKey(const uint8_t* key, int keyLength) {
    if (!key || keyLength != 32) {
        clearPrivateKey();
        return;
    }
    std::memcpy(m_privateKey, key, 32);
    m_hasPrivateKey = true;
}

void NetworkClient::clearPrivateKey() {
    std::memset(m_privateKey, 0, sizeof(m_privateKey));
    m_hasPrivateKey = false;
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

    bool ok = false;
    if (m_hasPrivateKey) {
        ok = m_client->SecureConnect(m_privateKey, 32, serverAddress);
    } else {
        ok = m_client->InsecureConnect(nullptr, 0, serverAddress);
    }
    if (!ok) {
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

    if (state == ksnet::CLIENT_STATE_CONNECTED && !m_joinSent) {
        m_joinSent = true;
        auto* join = (ClientJoinMessage*)m_client->CreateMessage(MSG_CLIENT_JOIN);
        if (join) {
            strncpy(join->driverName, m_driverName.c_str(), sizeof(join->driverName) - 1);
            strncpy(join->carName, m_carName.c_str(), sizeof(join->carName) - 1);
            join->clientVersion = PROTOCOL_VERSION;
            strncpy(join->authToken, m_joinToken.c_str(), sizeof(join->authToken) - 1);
            m_client->SendMessage(CHANNEL_RELIABLE, join);
        }
    }

    m_sendAccumulator += dt;
    if (m_simLoop && m_sendAccumulator >= (1.0 / 60.0) && isConnected()) {
        m_sendAccumulator = 0.0;
        if (m_simLoop->inputManager()) {
            PlayerInputMessage* msg = (PlayerInputMessage*)m_client->CreateMessage(MSG_PLAYER_INPUT);
            if (msg) {
                msg->data.throttle = m_simLoop->inputManager()->throttle();
                msg->data.brake = m_simLoop->inputManager()->brake();
                msg->data.steering = m_simLoop->inputManager()->steer();
                msg->data.frameNumber = m_clientId;
                m_client->SendMessage(CHANNEL_UNRELIABLE, msg);
            }
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
                    if (onSessionState)
                        onSessionState(ss->type, ss->phase, ss->currentLap, ss->totalLaps, ss->timeRemaining);
                    break;
                }
                case MSG_RACE_COUNTDOWN: {
                    auto* cd = (RaceCountdownMessage*)msg;
                    if (onRaceCountdown) onRaceCountdown(cd->value);
                    break;
                }
                case MSG_LAP_TIME: {
                    auto* lt = (LapTimeMessage*)msg;
                    if (onLapTime)
                        onLapTime(lt->carId, lt->lapNumber, lt->lapTime,
                                  lt->sector1, lt->sector2, lt->sector3, lt->isValid);
                    break;
                }
                case MSG_PENALTY: {
                    auto* pen = (PenaltyMessage*)msg;
                    if (onPenalty)
                        onPenalty(pen->carId, pen->penaltyType, pen->value, std::string(pen->reason));
                    break;
                }
                case MSG_AUTH_FAILED: {
                    auto* af = (AuthFailedMessage*)msg;
                    if (onAuthFailed) onAuthFailed(std::string(af->reason));
                    m_connected = false;
                    break;
                }
                case MSG_CAR_DAMAGE: {
                    auto* dm = (CarDamageMessage*)msg;
                    if (onCarDamage) onCarDamage(*dm);
                    break;
                }
                case MSG_CAR_SETUP: {
                    auto* su = (CarSetupMessage*)msg;
                    if (onCarSetup) onCarSetup(*su);
                    break;
                }
                case MSG_CAR_COLLISION: {
                    auto* col = (CarCollisionMessage*)msg;
                    if (onCarCollision) onCarCollision(*col);
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
// NetworkServer (ctor/dtor live in ServerA)
// ============================================================================

} // namespace ks::sim::net

#endif
