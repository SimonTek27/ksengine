#pragma once
/** LAN server discovery — UDP broadcast port 20779 */
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <cstdio>
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
#  include <unistd.h>
#  include <fcntl.h>
#endif

namespace ks {
namespace sim {

constexpr uint16_t kDiscoveryPort = 20779;
constexpr uint32_t kDiscoveryMagic = 0x4B534D31;

enum class DiscoveryMsg : uint8_t { Query = 1, Announce = 2, Reply = 3 };

struct ServerListEntry {
    std::string name, host, track;
    uint16_t port = 7777;
    uint8_t sessionType = 0;
    int players = 0, maxPlayers = 16;
    double lastSeenSec = 0.0;
};

#pragma pack(push, 1)
struct DiscoveryPacket {
    uint32_t magic = kDiscoveryMagic;
    uint8_t msg = 0, sessionType = 0;
    uint16_t port = 0, players = 0, maxPlayers = 0;
    char name[48]{}, track[48]{}, host[40]{};
};
#pragma pack(pop)

class ServerDiscovery {
public:
    bool start(bool announceAsHost = false) {
        if (m_sock >= 0) return true;
#ifdef _WIN32
        WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
        m_sock = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_sock < 0) return false;
        u_long nb = 1; ioctlsocket((SOCKET)m_sock, FIONBIO, &nb);
        BOOL yes = 1;
        setsockopt((SOCKET)m_sock, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));
        setsockopt((SOCKET)m_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
#else
        m_sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
        if (m_sock < 0) return false;
        int flags = fcntl(m_sock, F_GETFL, 0); fcntl(m_sock, F_SETFL, flags | O_NONBLOCK);
        int yes = 1;
        setsockopt(m_sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
        setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(kDiscoveryPort);
        addr.sin_addr.s_addr = INADDR_ANY;
#ifdef _WIN32
        if (bind((SOCKET)m_sock, (sockaddr*)&addr, sizeof(addr)) != 0) { closesocket((SOCKET)m_sock); m_sock = -1; return false; }
#else
        if (bind(m_sock, (sockaddr*)&addr, sizeof(addr)) != 0) { close(m_sock); m_sock = -1; return false; }
#endif
        m_hostMode = announceAsHost; m_t0 = std::chrono::steady_clock::now(); return true;
    }
    void stop() {
        if (m_sock < 0) return;
#ifdef _WIN32
        closesocket((SOCKET)m_sock);
#else
        close(m_sock);
#endif
        m_sock = -1;
    }
    void setHostInfo(const std::string& name, const std::string& track, uint16_t port,
                     uint8_t sessionType, int players, int maxPlayers) {
        m_hostName = name; m_hostTrack = track; m_hostPort = port;
        m_hostSession = sessionType; m_hostPlayers = players; m_hostMax = maxPlayers; m_hostMode = true;
    }
    void queryLan() {
        DiscoveryPacket p{}; p.magic = kDiscoveryMagic; p.msg = (uint8_t)DiscoveryMsg::Query;
        sendBroadcast(p);
    }
    void tick(float) {
        if (m_sock < 0) return;
        const double now = elapsed();
        if (m_hostMode && now - m_lastAnnounce > 2.0) { m_lastAnnounce = now; sendBroadcast(makeAnnounce()); }
        for (;;) {
            DiscoveryPacket p{}; sockaddr_in from{};
#ifdef _WIN32
            int flen = sizeof(from);
            int n = recvfrom((SOCKET)m_sock, (char*)&p, sizeof(p), 0, (sockaddr*)&from, &flen);
#else
            socklen_t flen = sizeof(from);
            int n = (int)recvfrom(m_sock, &p, sizeof(p), 0, (sockaddr*)&from, &flen);
#endif
            if (n < (int)sizeof(uint32_t)) break;
            if (p.magic != kDiscoveryMagic) continue;
            char ip[64];
#ifdef _WIN32
            InetNtopA(AF_INET, &from.sin_addr, ip, sizeof(ip));
#else
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
#endif
            if (p.msg == (uint8_t)DiscoveryMsg::Query && m_hostMode) sendTo(makeAnnounce(), from);
            else if (p.msg == (uint8_t)DiscoveryMsg::Announce || p.msg == (uint8_t)DiscoveryMsg::Reply) {
                ServerListEntry e;
                e.name = p.name; e.track = p.track; e.port = p.port; e.sessionType = p.sessionType;
                e.players = p.players; e.maxPlayers = p.maxPlayers; e.host = ip; e.lastSeenSec = now;
                upsert(e);
            }
        }
        m_servers.erase(std::remove_if(m_servers.begin(), m_servers.end(),
            [now](const ServerListEntry& e) { return now - e.lastSeenSec > 8.0; }), m_servers.end());
    }
    const std::vector<ServerListEntry>& servers() const { return m_servers; }
    std::vector<std::string> browserRows() const {
        std::vector<std::string> rows;
        for (const auto& e : m_servers) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s | %s | %d/%d | %s:%u",
                e.name.c_str(), e.track.c_str(), e.players, e.maxPlayers, e.host.c_str(), (unsigned)e.port);
            rows.emplace_back(buf);
        }
        if (rows.empty()) rows.emplace_back("No LAN servers (query with F5)");
        return rows;
    }
private:
    DiscoveryPacket makeAnnounce() const {
        DiscoveryPacket p{}; p.magic = kDiscoveryMagic; p.msg = (uint8_t)DiscoveryMsg::Announce;
        p.sessionType = m_hostSession; p.port = m_hostPort;
        p.players = (uint16_t)m_hostPlayers; p.maxPlayers = (uint16_t)m_hostMax;
        std::snprintf(p.name, sizeof(p.name), "%s", m_hostName.c_str());
        std::snprintf(p.track, sizeof(p.track), "%s", m_hostTrack.c_str());
        return p;
    }
    void sendBroadcast(const DiscoveryPacket& p) {
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(kDiscoveryPort);
        addr.sin_addr.s_addr = INADDR_BROADCAST; sendTo(p, addr);
    }
    void sendTo(const DiscoveryPacket& p, const sockaddr_in& addr) {
        if (m_sock < 0) return;
#ifdef _WIN32
        sendto((SOCKET)m_sock, (const char*)&p, sizeof(p), 0, (sockaddr*)&addr, sizeof(addr));
#else
        sendto(m_sock, &p, sizeof(p), 0, (sockaddr*)&addr, sizeof(addr));
#endif
    }
    void upsert(const ServerListEntry& e) {
        for (auto& s : m_servers) if (s.host == e.host && s.port == e.port) { s = e; return; }
        m_servers.push_back(e);
    }
    double elapsed() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_t0).count();
    }
    int m_sock = -1; bool m_hostMode = false;
    std::string m_hostName = "ksim Server", m_hostTrack;
    uint16_t m_hostPort = 7777; uint8_t m_hostSession = 2;
    int m_hostPlayers = 0, m_hostMax = 16;
    std::vector<ServerListEntry> m_servers;
    std::chrono::steady_clock::time_point m_t0{};
    double m_lastAnnounce = 0.0;
};

} // namespace sim
} // namespace ks
