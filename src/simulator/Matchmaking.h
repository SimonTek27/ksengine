#pragma once
/**
 * Matchmaking: LAN discovery + optional HTTP lobby registry (P2).
 *
 * LAN: reuses ServerDiscovery (UDP 20779).
 * Cloud lobby: optional base URL, e.g. "http://lobby.example.com/api/v1"
 *   POST /servers  — register host (JSON body)
 *   GET  /servers  — list (JSON array)
 * Soft-fail when URL empty or HTTP unavailable (no hard dependency).
 */
#include "ServerDiscovery.h"
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#endif

namespace ks {
namespace sim {

struct MatchmakingConfig {
    std::string lobbyBaseUrl;
    double lanRefreshSec = 2.0;
    double lobbyRefreshSec = 10.0;
    bool announceAsHost = false;
};

class Matchmaking {
public:
    explicit Matchmaking(const MatchmakingConfig& cfg = {}) : m_cfg(cfg) {}

    void setConfig(const MatchmakingConfig& cfg) { m_cfg = cfg; }
    const MatchmakingConfig& config() const { return m_cfg; }

    bool start() {
        if (!m_discovery.start(m_cfg.announceAsHost)) return false;
        m_running = true;
        m_t0 = std::chrono::steady_clock::now();
        return true;
    }

    void stop() {
        m_running = false;
        m_discovery.stop();
        m_servers.clear();
    }

    void setHostInfo(const std::string& name, const std::string& track,
                     uint16_t port, int players, int maxPlayers, uint8_t sessionType) {
        m_host.name = name;
        m_host.track = track;
        m_host.port = port;
        m_host.players = players;
        m_host.maxPlayers = maxPlayers;
        m_host.sessionType = sessionType;
        m_discovery.setHostInfo(name, track, port, sessionType, players, maxPlayers);
    }

    void setAuthRequired(bool v) { m_authRequired = v; }
    bool authRequired() const { return m_authRequired; }

    void update(double dt) {
        if (!m_running) return;
        const double now = elapsed();
        m_discovery.tick(static_cast<float>(dt));

        if (m_cfg.announceAsHost && !m_cfg.lobbyBaseUrl.empty() &&
            now - m_lastLobbyRegister >= 5.0) {
            m_lastLobbyRegister = now;
            registerWithLobby();
        }

        if (now - m_lastLanRefresh >= m_cfg.lanRefreshSec) {
            m_lastLanRefresh = now;
            m_discovery.queryLan();
            mergeLanList();
        }

        if (!m_cfg.lobbyBaseUrl.empty() && now - m_lastLobbyRefresh >= m_cfg.lobbyRefreshSec) {
            m_lastLobbyRefresh = now;
            queryLobby();
        }
    }

    const std::vector<ServerListEntry>& servers() const { return m_servers; }
    std::vector<std::string> browserRows() const {
        std::vector<std::string> rows;
        for (const auto& e : m_servers) {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "%s | %s | %d/%d | %s:%u%s",
                e.name.c_str(), e.track.c_str(), e.players, e.maxPlayers,
                e.host.c_str(), (unsigned)e.port,
                m_authRequired ? " [auth]" : "");
            rows.emplace_back(buf);
        }
        if (rows.empty()) rows.emplace_back("No servers (LAN query / lobby)");
        return rows;
    }

    std::function<void(const std::vector<ServerListEntry>&)> onServerListUpdated;

private:
    double elapsed() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_t0).count();
    }

    void mergeLanList() {
        auto lan = m_discovery.servers();
        std::vector<ServerListEntry> merged = lan;
        for (const auto& s : m_lobbyCache) {
            bool found = false;
            for (const auto& m : merged) {
                if (m.host == s.host && m.port == s.port) { found = true; break; }
            }
            if (!found) merged.push_back(s);
        }
        m_servers = std::move(merged);
        if (onServerListUpdated) onServerListUpdated(m_servers);
    }

    static bool httpRequest(const std::string& url, const std::string& method,
                            const std::string& body, std::string& outResponse) {
        if (url.size() < 8 || url.substr(0, 7) != "http://") return false;
        std::string rest = url.substr(7);
        std::string host, path = "/";
        uint16_t port = 80;
        auto slash = rest.find('/');
        std::string hostPort = (slash == std::string::npos) ? rest : rest.substr(0, slash);
        if (slash != std::string::npos) path = rest.substr(slash);
        auto colon = hostPort.find(':');
        if (colon != std::string::npos) {
            host = hostPort.substr(0, colon);
            port = static_cast<uint16_t>(std::atoi(hostPort.substr(colon + 1).c_str()));
        } else {
            host = hostPort;
        }
        if (host.empty()) return false;

#ifdef _WIN32
        WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char portStr[16];
        std::snprintf(portStr, sizeof(portStr), "%u", port);
        if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) return false;

        int sock = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock < 0) { freeaddrinfo(res); return false; }

#ifdef _WIN32
        DWORD tv = 3000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
#else
        timeval tv{3, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        if (connect(sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            freeaddrinfo(res);
            return false;
        }
        freeaddrinfo(res);

        std::ostringstream req;
        req << method << " " << path << " HTTP/1.0\r\n"
            << "Host: " << host << "\r\n"
            << "User-Agent: ksim-matchmaking/1.0\r\n"
            << "Connection: close\r\n";
        if (!body.empty()) {
            req << "Content-Type: application/json\r\n"
                << "Content-Length: " << body.size() << "\r\n";
        }
        req << "\r\n";
        if (!body.empty()) req << body;
        std::string reqStr = req.str();
#ifdef _WIN32
        send(sock, reqStr.c_str(), (int)reqStr.size(), 0);
#else
        ::send(sock, reqStr.c_str(), reqStr.size(), 0);
#endif

        char buf[4096];
        outResponse.clear();
        for (;;) {
#ifdef _WIN32
            int n = recv(sock, buf, sizeof(buf), 0);
#else
            int n = (int)::recv(sock, buf, sizeof(buf), 0);
#endif
            if (n <= 0) break;
            outResponse.append(buf, buf + n);
            if (outResponse.size() > 256 * 1024) break;
        }
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return !outResponse.empty();
    }

    void registerWithLobby() {
        if (m_cfg.lobbyBaseUrl.empty()) return;
        std::ostringstream json;
        json << "{"
             << "\"name\":\"" << escapeJson(m_host.name) << "\","
             << "\"track\":\"" << escapeJson(m_host.track) << "\","
             << "\"port\":" << m_host.port << ","
             << "\"players\":" << m_host.players << ","
             << "\"maxPlayers\":" << m_host.maxPlayers << ","
             << "\"sessionType\":" << (int)m_host.sessionType << ","
             << "\"auth\":" << (m_authRequired ? "true" : "false")
             << "}";
        std::string resp;
        httpRequest(m_cfg.lobbyBaseUrl + "/servers", "POST", json.str(), resp);
    }

    void queryLobby() {
        if (m_cfg.lobbyBaseUrl.empty()) return;
        std::string resp;
        if (!httpRequest(m_cfg.lobbyBaseUrl + "/servers", "GET", "", resp)) return;
        auto pos = resp.find("\r\n\r\n");
        if (pos == std::string::npos) return;
        parseLobbyJson(resp.substr(pos + 4));
        mergeLanList();
    }

    static std::string escapeJson(const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') o.push_back('\\');
            o.push_back(c);
        }
        return o;
    }

    void parseLobbyJson(const std::string& body) {
        m_lobbyCache.clear();
        size_t i = 0;
        while (i < body.size()) {
            auto objStart = body.find('{', i);
            if (objStart == std::string::npos) break;
            auto objEnd = body.find('}', objStart);
            if (objEnd == std::string::npos) break;
            std::string obj = body.substr(objStart, objEnd - objStart + 1);
            ServerListEntry e;
            e.name = extractJsonString(obj, "name");
            e.host = extractJsonString(obj, "host");
            e.track = extractJsonString(obj, "track");
            e.port = static_cast<uint16_t>(extractJsonInt(obj, "port", 40000));
            e.players = extractJsonInt(obj, "players", 0);
            e.maxPlayers = extractJsonInt(obj, "maxPlayers", 16);
            e.sessionType = static_cast<uint8_t>(extractJsonInt(obj, "sessionType", 0));
            e.lastSeenSec = elapsed();
            if (!e.host.empty() || !e.name.empty()) {
                if (e.host.empty()) e.host = "0.0.0.0";
                m_lobbyCache.push_back(e);
            }
            i = objEnd + 1;
        }
    }

    static std::string extractJsonString(const std::string& obj, const char* key) {
        std::string pat = std::string("\"") + key + "\"";
        auto p = obj.find(pat);
        if (p == std::string::npos) return {};
        p = obj.find(':', p);
        if (p == std::string::npos) return {};
        p = obj.find('"', p);
        if (p == std::string::npos) return {};
        auto q = obj.find('"', p + 1);
        if (q == std::string::npos) return {};
        return obj.substr(p + 1, q - p - 1);
    }

    static int extractJsonInt(const std::string& obj, const char* key, int def) {
        std::string pat = std::string("\"") + key + "\"";
        auto p = obj.find(pat);
        if (p == std::string::npos) return def;
        p = obj.find(':', p);
        if (p == std::string::npos) return def;
        return std::atoi(obj.c_str() + p + 1);
    }

    MatchmakingConfig m_cfg;
    ServerDiscovery m_discovery;
    ServerListEntry m_host;
    std::vector<ServerListEntry> m_servers;
    std::vector<ServerListEntry> m_lobbyCache;
    bool m_running = false;
    bool m_authRequired = false;
    double m_lastLanRefresh = 0, m_lastLobbyRefresh = 0, m_lastLobbyRegister = 0;
    std::chrono::steady_clock::time_point m_t0;
};

} // namespace sim
} // namespace ks
