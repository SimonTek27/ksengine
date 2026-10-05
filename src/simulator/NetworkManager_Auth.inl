// Included from NetworkManager.cpp after setupClientSignals damage handlers
// and provides setAuthToken / setJoinToken implementations.

// Wire in setupClientSignals:
//   m_client->onAuthFailed = [this](const std::string& reason) {
//       m_connected = false;
//       printf("NetworkManager: AUTH FAILED — %s\n", reason.c_str());
//       if (onAuthFailed) onAuthFailed(reason);
//       if (onConnectionFailed) onConnectionFailed(std::string("Auth failed: ") + reason);
//   };

inline void NetworkManager_setAuthToken(ks::sim::net::NetworkServer* server, const std::string& token) {
    if (server) server->setAuthToken(token);
}
inline void NetworkManager_setJoinToken(ks::sim::net::NetworkClient* client, const std::string& token) {
    if (client) client->setJoinToken(token);
}
