// Included from NetworkManager.cpp after setupClientSignals penalty block and after broadcastPenalty

// --- wire client damage/setup/collision (append inside setupClientSignals before closing brace) ---
#if 0
    m_client->onCarDamage = [this](const net::CarDamageMessage& msg) {
        if (onCarDamageReceived) onCarDamageReceived(msg);
    };
    m_client->onCarSetup = [this](const net::CarSetupMessage& msg) {
        if (onCarSetupReceived) onCarSetupReceived(msg);
    };
    m_client->onCarCollision = [this](const net::CarCollisionMessage& msg) {
        if (onCarCollisionReceived) onCarCollisionReceived(msg);
    };
#endif

void NetworkManager::broadcastCarDamage(const net::CarDamageMessage& msg) {
    if (m_hosting && m_server)
        m_server->broadcastCarDamage(msg);
}

void NetworkManager::broadcastCarSetup(const net::CarSetupMessage& msg) {
    if (m_hosting && m_server)
        m_server->broadcastCarSetup(msg);
}

void NetworkManager::broadcastCarCollision(const net::CarCollisionMessage& msg) {
    if (m_hosting && m_server)
        m_server->broadcastCarCollision(msg);
}

void NetworkManager::publishDamage(uint32_t carId, const ks::physics::DamageSystem& dmg) {
    if (!m_hosting || !m_server) return;
    net::CarDamageMessage msg;
    fillCarDamageMessage(msg, carId, dmg);
    m_server->broadcastCarDamage(msg);
}

void NetworkManager::publishSetup(uint32_t carId, const SetupData& setup) {
    if (!m_hosting || !m_server) return;
    net::CarSetupMessage msg;
    fillCarSetupMessage(msg, carId, setup);
    m_server->broadcastCarSetup(msg);
}
