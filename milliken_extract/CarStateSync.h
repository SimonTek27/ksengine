#pragma once
/**
 * CarStateSync — Sprint 7 / P1.1
 * Lightweight UDP CarState broadcast ≥20 Hz (no ksnet required).
 * Host: broadcasts all active cars. Client: sends local, applies remote.
 */
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <cstddef>
#include <chrono>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#endif

namespace ks {
namespace sim {
namespace netsync {

constexpr uint32_t kCarStateMagic = 0x4B534353u; // 'KSCS'
constexpr uint16_t kDefaultSyncPort = 40001;
constexpr int kMaxCarsPerPacket = 16;
constexpr float kDefaultHz = 20.f;

#pragma pack(push, 1)
struct CarStatePacked {
    uint32_t carId = 0;
    float posX = 0, posY = 0, posZ = 0;
    float heading = 0;
    float speed = 0;
    float steer = 0;
    float throttle = 0;
    float brake = 0;
    uint8_t gear = 0;
    uint8_t flags = 0; // bit0=active, bit1=player
};

struct CarStatePacket {
    uint32_t magic = kCarStateMagic;
    uint16_t seq = 0;
    uint16_t count = 0;
    double simTime = 0;
    CarStatePacked cars[kMaxCarsPerPacket];
};
#pragma pack(pop)

struct RemoteCarSnapshot {
    CarStatePacked state{};
    double recvTime = 0;
    uint16_t lastSeq = 0;
    bool valid = false;
    // interpolation
    float prevX = 0, prevZ = 0, prevH = 0;
    float nextX = 0, nextZ = 0, nextH = 0;
    double prevT = 0, nextT = 0;
};

inline CarStatePacked packFromSim(uint32_t id, float x, float y, float z,
                                  float heading, float speed,
                                  float steer, float throttle, float brake,
                                  int gear, bool player, bool active) {
    CarStatePacked p;
    p.carId = id;
    p.posX = x; p.posY = y; p.posZ = z;
    p.heading = heading;
    p.speed = speed;
    p.steer = steer;
    p.throttle = throttle;
    p.brake = brake;
    p.gear = static_cast<uint8_t>(std::clamp(gear, 0, 8));
    p.flags = (active ? 1u : 0u) | (player ? 2u : 0u);
    return p;
}

class CarStateSync {
public:
    enum class Role { None, Host, Client };

    ~CarStateSync() { stop(); }

    bool startHost(uint16_t port = kDefaultSyncPort) {
        stop();
        if (!openSocket(port, true)) return false;
        m_role = Role::Host;
        m_port = port;
        m_seq = 0;
        std::fprintf(stderr, "CarStateSync: HOST on :%u\n", (unsigned)port);
        return true;
    }

    bool startClient(const std::string& host, uint16_t port = kDefaultSyncPort) {
        stop();
        if (!openSocket(0, false)) return false;
        m_role = Role::Client;
        m_port = port;
        m_peerHost = host;
        std::memset(&m_peerAddr, 0, sizeof(m_peerAddr));
        m_peerAddr.sin_family = AF_INET;
        m_peerAddr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &m_peerAddr.sin_addr) != 1) {
            std::fprintf(stderr, "CarStateSync: bad host %s\n", host.c_str());
            stop();
            return false;
        }
        std::fprintf(stderr, "CarStateSync: CLIENT → %s:%u\n", host.c_str(), (unsigned)port);
        return true;
    }

    void stop() {
        if (m_sock >= 0) {
#ifdef _WIN32
            closesocket((SOCKET)m_sock);
#else
            close(m_sock);
#endif
            m_sock = -1;
        }
        m_role = Role::None;
        m_remotes.clear();
    }

    Role role() const { return m_role; }
    bool active() const { return m_role != Role::None && m_sock >= 0; }
    void setSendHz(float hz) { m_sendHz = std::clamp(hz, 5.f, 60.f); }
    float sendHz() const { return m_sendHz; }

    /** Host: publish full list. Client: publish local car only. */
    void publish(double simTime, const std::vector<CarStatePacked>& cars) {
        if (!active()) return;
        m_sendAcc += 1.0; // caller passes after dt accumulate externally
        // use internal clock
        const double now = nowSec();
        if (now - m_lastSend < 1.0 / m_sendHz) return;
        m_lastSend = now;

        CarStatePacket pkt{};
        pkt.magic = kCarStateMagic;
        pkt.seq = ++m_seq;
        pkt.simTime = simTime;
        pkt.count = static_cast<uint16_t>(std::min(cars.size(), size_t(kMaxCarsPerPacket)));
        for (uint16_t i = 0; i < pkt.count; ++i)
            pkt.cars[i] = cars[i];

        const size_t bytes = sizeof(uint32_t) * 1 + sizeof(uint16_t) * 2 + sizeof(double)
            + sizeof(CarStatePacked) * pkt.count;
        // safer full struct size for simplicity
        const size_t sendBytes = offsetof(CarStatePacket, cars) + sizeof(CarStatePacked) * pkt.count;

        if (m_role == Role::Host) {
            // Broadcast to all known peers + subnet broadcast
            for (auto& peer : m_clientAddrs)
                sendto(m_sock, reinterpret_cast<const char*>(&pkt), (int)sendBytes, 0,
                       (sockaddr*)&peer, sizeof(peer));
            // Also LAN broadcast
            sockaddr_in bcast{};
            bcast.sin_family = AF_INET;
            bcast.sin_port = htons(m_port);
            bcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            sendto(m_sock, reinterpret_cast<const char*>(&pkt), (int)sendBytes, 0,
                   (sockaddr*)&bcast, sizeof(bcast));
        } else if (m_role == Role::Client) {
            sendto(m_sock, reinterpret_cast<const char*>(&pkt), (int)sendBytes, 0,
                   (sockaddr*)&m_peerAddr, sizeof(m_peerAddr));
        }
        m_stats.sent += 1;
        m_stats.bytesOut += sendBytes;
    }

    /** Poll incoming packets; updates remote snapshots. */
    void poll() {
        if (!active()) return;
        for (;;) {
            CarStatePacket pkt{};
            sockaddr_in from{};
#ifdef _WIN32
            int flen = sizeof(from);
#else
            socklen_t flen = sizeof(from);
#endif
            int n = recvfrom(m_sock, reinterpret_cast<char*>(&pkt), sizeof(pkt), 0,
                             (sockaddr*)&from, &flen);
            if (n < (int)(offsetof(CarStatePacket, cars))) break;
            if (pkt.magic != kCarStateMagic) continue;
            if (pkt.count > kMaxCarsPerPacket) continue;

            if (m_role == Role::Host) {
                // remember client address
                bool known = false;
                for (auto& a : m_clientAddrs) {
                    if (a.sin_addr.s_addr == from.sin_addr.s_addr && a.sin_port == from.sin_port) {
                        known = true; break;
                    }
                }
                if (!known && m_clientAddrs.size() < 32)
                    m_clientAddrs.push_back(from);
            }

            const double t = nowSec();
            for (uint16_t i = 0; i < pkt.count; ++i) {
                const auto& c = pkt.cars[i];
                auto& snap = m_remotes[c.carId];
                // sequence drop
                if (snap.valid && (uint16_t)(pkt.seq - snap.lastSeq) > 60000 && pkt.seq < snap.lastSeq)
                    continue;
                snap.prevX = snap.nextX; snap.prevZ = snap.nextZ; snap.prevH = snap.nextH;
                snap.prevT = snap.nextT;
                snap.nextX = c.posX; snap.nextZ = c.posZ; snap.nextH = c.heading;
                snap.nextT = t;
                snap.state = c;
                snap.lastSeq = pkt.seq;
                snap.recvTime = t;
                snap.valid = true;
            }
            m_stats.recv += 1;
            m_stats.bytesIn += (size_t)n;
            if (onPacket) onPacket(pkt);
        }
    }

    /** Interpolated pose for rendering / ghost cars. */
    bool sample(uint32_t carId, float& x, float& y, float& z, float& heading,
                float& speed, float& steer, float& throttle, float& brake, int& gear) const {
        auto it = m_remotes.find(carId);
        if (it == m_remotes.end() || !it->second.valid) return false;
        const auto& s = it->second;
        const double t = nowSec();
        float u = 0.f;
        if (s.nextT > s.prevT + 1e-4)
            u = float((t - s.prevT) / (s.nextT - s.prevT));
        u = std::clamp(u, 0.f, 1.25f); // slight extrapolate
        x = s.prevX + (s.nextX - s.prevX) * u;
        z = s.prevZ + (s.nextZ - s.prevZ) * u;
        y = s.state.posY;
        // shortest angle lerp
        float dh = s.nextH - s.prevH;
        while (dh > 3.14159f) dh -= 6.28318f;
        while (dh < -3.14159f) dh += 6.28318f;
        heading = s.prevH + dh * std::min(u, 1.f);
        speed = s.state.speed;
        steer = s.state.steer;
        throttle = s.state.throttle;
        brake = s.state.brake;
        gear = s.state.gear;
        // stale?
        if (t - s.recvTime > 1.5) return false;
        return true;
    }

    const std::unordered_map<uint32_t, RemoteCarSnapshot>& remotes() const { return m_remotes; }

    struct Stats {
        uint64_t sent = 0, recv = 0, bytesIn = 0, bytesOut = 0;
    };
    Stats stats() const { return m_stats; }

    std::function<void(const CarStatePacket&)> onPacket;

private:
    bool openSocket(uint16_t bindPort, bool broadcast) {
#ifdef _WIN32
        static bool wsa = false;
        if (!wsa) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); wsa = true; }
#endif
        m_sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
        if (m_sock < 0) return false;
        int yes = 1;
        setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        if (broadcast)
            setsockopt(m_sock, SOL_SOCKET, SO_BROADCAST, (char*)&yes, sizeof(yes));
#ifdef _WIN32
        u_long nb = 1; ioctlsocket((SOCKET)m_sock, FIONBIO, &nb);
#else
        fcntl(m_sock, F_SETFL, O_NONBLOCK);
#endif
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(bindPort);
        if (bind(m_sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
            closesocket((SOCKET)m_sock);
#else
            close(m_sock);
#endif
            m_sock = -1;
            return false;
        }
        return true;
    }

    static double nowSec() {
        using clock = std::chrono::steady_clock;
        static const auto t0 = clock::now();
        return std::chrono::duration<double>(clock::now() - t0).count();
    }

    Role m_role = Role::None;
    int m_sock = -1;
    uint16_t m_port = kDefaultSyncPort;
    std::string m_peerHost;
    sockaddr_in m_peerAddr{};
    std::vector<sockaddr_in> m_clientAddrs;
    uint16_t m_seq = 0;
    float m_sendHz = kDefaultHz;
    double m_lastSend = 0;
    double m_sendAcc = 0;
    std::unordered_map<uint32_t, RemoteCarSnapshot> m_remotes;
    Stats m_stats;
};

} // namespace netsync
} // namespace sim
} // namespace ks
