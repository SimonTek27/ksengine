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

void NetworkServer::broadcastRaceCountdown(int seconds) {
    if (!m_running || !m_server) return;
    for (int ci = 0; ci < m_server->GetMaxClients(); ++ci) {
        if (!m_server->IsClientConnected(ci)) continue;
        if (!m_server->CanSendMessage(ci, CHANNEL_RELIABLE)) continue;
        auto* msg = (RaceCountdownMessage*)m_server->CreateMessage(ci, MSG_RACE_COUNTDOWN);
        if (msg) {
            msg->value = seconds;
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
            msg->lapNumber = static_cast<uint32_t>(lapNumber);
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

void NetworkServer::broadcastCarDamage(const CarDamageMessage& src) {
    if (!m_running || !m_server) return;
    for (int ci = 0; ci < m_server->GetMaxClients(); ++ci) {
        if (!m_server->IsClientConnected(ci)) continue;
        if (!m_server->CanSendMessage(ci, CHANNEL_RELIABLE)) continue;
        auto* msg = (CarDamageMessage*)m_server->CreateMessage(ci, MSG_CAR_DAMAGE);
        if (!msg) continue;
        *msg = src;
        m_server->SendMessage(ci, CHANNEL_RELIABLE, msg);
    }
}

void NetworkServer::broadcastCarSetup(const CarSetupMessage& src) {
    if (!m_running || !m_server) return;
    for (int ci = 0; ci < m_server->GetMaxClients(); ++ci) {
        if (!m_server->IsClientConnected(ci)) continue;
        if (!m_server->CanSendMessage(ci, CHANNEL_RELIABLE)) continue;
        auto* msg = (CarSetupMessage*)m_server->CreateMessage(ci, MSG_CAR_SETUP);
        if (!msg) continue;
        *msg = src;
        m_server->SendMessage(ci, CHANNEL_RELIABLE, msg);
    }
}

void NetworkServer::broadcastCarCollision(const CarCollisionMessage& src) {
    if (!m_running || !m_server) return;
    for (int ci = 0; ci < m_server->GetMaxClients(); ++ci) {
        if (!m_server->IsClientConnected(ci)) continue;
        if (!m_server->CanSendMessage(ci, CHANNEL_UNRELIABLE)) continue;
        auto* msg = (CarCollisionMessage*)m_server->CreateMessage(ci, MSG_CAR_COLLISION);
        if (!msg) continue;
        *msg = src;
        m_server->SendMessage(ci, CHANNEL_UNRELIABLE, msg);
    }
}

void NetworkServer::spawnCarForClient(int clientIndex, const std::string& driverName, const std::string& carName) {
    if (!m_multiCar) return;

    float spawnZ = -clientIndex * 5.0f;
    int nid = m_multiCar->addCar(carName, driverName, vec3(0, 0.5f, spawnZ), false);
    if (nid < 0) return;
    uint32_t carId = static_cast<uint32_t>(nid);
    m_clients[clientIndex].carId = carId;
    m_multiCar->setCarClientIndex(nid, clientIndex);

    for (int dest = 0; dest < m_server->GetMaxClients(); ++dest) {
        if (!m_server->IsClientConnected(dest)) continue;
        if (!m_server->CanSendMessage(dest, CHANNEL_RELIABLE)) continue;
        CarSpawnMessage* msg = (CarSpawnMessage*)m_server->CreateMessage(dest, MSG_CAR_SPAWN);
        if (msg) {
            msg->carId = carId;
            msg->clientId = static_cast<uint32_t>(clientIndex);
            strncpy(msg->driverName, driverName.c_str(), sizeof(msg->driverName) - 1);
            strncpy(msg->carName, carName.c_str(), sizeof(msg->carName) - 1);
            msg->posX = 0;
            msg->posY = 0.5f;
            msg->posZ = spawnZ;
            m_server->SendMessage(dest, CHANNEL_RELIABLE, msg);
        }
    }

    // Replay existing grid to late joiner
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
        msg->clientId = static_cast<uint32_t>(other);
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
