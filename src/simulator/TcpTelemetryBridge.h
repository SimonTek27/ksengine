#pragma once
/**
 * TCP telemetry bridge — reliable stream of ksim samples to one client.
 * Qt-free. Complements UDP (best-effort) and shared-memory (local).
 *
 * Default listen: 0.0.0.0:20778
 * Framing: uint32 LE length + payload (UdpTelemPacket binary or JSON)
 * Rate-limited (default 60 Hz) so 1 kHz physics does not flood TCP.
 */
#include "UdpTelemetryBridge.h" // UdpTelemSample / UdpTelemPacket
#include <cstdint>
#include <cstring>
#include <string>
#include <mutex>
#include <cstdio>
#include <chrono>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  static const socket_t kInvalidSock = INVALID_SOCKET;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
  using socket_t = int;
  static const socket_t kInvalidSock = -1;
#endif

namespace ks {
namespace sim {

class TcpTelemetryBridge {
public:
    TcpTelemetryBridge() = default;
    ~TcpTelemetryBridge() { close(); }

    /** Listen for one client. host "0.0.0.0" = all interfaces. */
    bool listen(const char* host = "0.0.0.0", uint16_t port = 20778) {
        std::lock_guard<std::mutex> lock(m_mutex);
        closeUnlocked();

#ifdef _WIN32
        if (!m_wsa) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
            m_wsa = true;
        }
#endif
        m_listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listen == kInvalidSock) return false;

        int yes = 1;
#ifdef _WIN32
        setsockopt(m_listen, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
        setsockopt(m_listen, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
            closeUnlocked();
            return false;
        }
        if (::bind(m_listen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            std::fprintf(stderr, "TcpTelemetryBridge: bind %s:%u failed\n", host, unsigned(port));
            closeUnlocked();
            return false;
        }
        if (::listen(m_listen, 1) != 0) {
            closeUnlocked();
            return false;
        }
        setNonBlocking(m_listen);
        m_port = port;
        m_ok = true;
        std::fprintf(stderr, "TcpTelemetryBridge: listen %s:%u\n", host, unsigned(port));
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        closeUnlocked();
    }

    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const { return m_enabled; }
    bool isListening() const { return m_ok; }
    bool hasClient() const { return m_client != kInvalidSock; }

    void setJsonMode(bool j) { m_json = j; }
    void setMinIntervalMs(int ms) { m_minIntervalMs = ms > 0 ? ms : 1; }

    /** Accept pending client (non-blocking). Call each tick. */
    void pollAccept() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_ok || m_listen == kInvalidSock) return;
        if (m_client != kInvalidSock) return;

        sockaddr_in peer{};
#ifdef _WIN32
        int len = sizeof(peer);
#else
        socklen_t len = sizeof(peer);
#endif
        socket_t c = ::accept(m_listen, reinterpret_cast<sockaddr*>(&peer), &len);
        if (c == kInvalidSock) return;

        int flag = 1;
#ifdef _WIN32
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
#else
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
#endif
        setNonBlocking(c);
        m_client = c;
        char ip[64] = {};
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        std::fprintf(stderr, "TcpTelemetryBridge: client %s:%u\n",
                     ip, unsigned(ntohs(peer.sin_port)));
    }

    /** Publish sample if client connected and rate allows. */
    bool publish(const UdpTelemSample& s) {
        if (!m_enabled) return false;
        pollAccept();

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_client == kInvalidSock) return false;

        const auto now = std::chrono::steady_clock::now();
        if (m_lastSend.time_since_epoch().count() != 0) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSend).count();
            if (ms < m_minIntervalMs) return false;
        }

        std::vector<char> payload;
        if (m_json) {
            char buf[1024];
            int n = std::snprintf(buf, sizeof(buf),
                "{\"t\":%.3f,\"v\":%.2f,\"rpm\":%.0f,\"gear\":%d,\"thr\":%.3f,\"brk\":%.3f,"
                "\"fuel\":%.2f,\"lap\":%d,\"ct\":%d,\"bt\":%d,\"spline\":%.4f,"
                "\"dmg\":%.3f,\"engH\":%.3f,\"pwr\":%.3f,\"warn\":%d,\"seized\":%d}\n",
                s.timeSec, s.speedMs * 3.6, s.rpm, s.gear, s.throttle, s.brake,
                s.fuelL, s.completedLaps, s.currentTimeMs, s.bestTimeMs, s.normalizedSpline,
                s.damageOverall, s.engineHealth, s.powerMult, s.damageWarning,
                static_cast<int>(s.engineSeized));
            if (n <= 0) return false;
            payload.assign(buf, buf + n);
        } else {
            UdpTelemPacket p{};
            p.magic[0] = 'K'; p.magic[1] = 'S'; p.magic[2] = 'I'; p.magic[3] = 'M';
            p.version = 2; // v2 = damage channels (docs/DAMAGE_TELEMETRY.md)
            p.size = static_cast<uint16_t>(sizeof(UdpTelemPacket));
            p.sequence = ++m_seq;
            p.timeSec = s.timeSec;
            p.speedMs = s.speedMs;
            p.rpm = s.rpm;
            p.throttle = s.throttle;
            p.brake = s.brake;
            p.steer = s.steer;
            p.gear = s.gear;
            p.fuelL = s.fuelL;
            p.posX = s.posX; p.posY = s.posY; p.posZ = s.posZ;
            p.velX = s.velX; p.velY = s.velY; p.velZ = s.velZ;
            p.accGX = s.accGX; p.accGY = s.accGY; p.accGZ = s.accGZ;
            p.heading = s.heading;
            for (int i = 0; i < 4; ++i) {
                p.tyreTemp[i] = s.tyreTemp[i];
                p.tyreWear[i] = s.tyreWear[i];
                p.tyrePressure[i] = s.tyrePressure[i];
                p.suspIntegrity[i] = s.suspIntegrity[i];
            }
            p.completedLaps = s.completedLaps;
            p.currentSector = s.currentSector;
            p.currentTimeMs = s.currentTimeMs;
            p.lastTimeMs = s.lastTimeMs;
            p.bestTimeMs = s.bestTimeMs;
            p.position = s.position;
            p.sessionType = s.sessionType;
            p.status = s.status;
            p.normalizedSpline = s.normalizedSpline;
            p.surfaceGrip = s.surfaceGrip;
            p.airTemp = s.airTemp;
            p.roadTemp = s.roadTemp;
            p.inPit = s.inPit ? 1 : 0;
            p.pitLimiter = s.pitLimiter ? 1 : 0;
            p.damageWarning = static_cast<uint8_t>(s.damageWarning);
            p.engineSeized = s.engineSeized ? 1 : 0;
            p.damageOverall = s.damageOverall;
            p.engineHealth = s.engineHealth;
            p.powerMult = s.powerMult;
            p.dragMult = s.dragMult;
            p.downforceMult = s.downforceMult;
            for (int i = 0; i < 5; ++i) p.carDamage[i] = s.carDamage[i];
            const char* raw = reinterpret_cast<const char*>(&p);
            payload.assign(raw, raw + sizeof(p));
        }

        uint32_t len = static_cast<uint32_t>(payload.size());
        char hdr[4];
        std::memcpy(hdr, &len, 4); // LE on little-endian hosts (Win/Linux x64)

        if (!sendAll(hdr, 4) || !sendAll(payload.data(), payload.size())) {
            dropClient();
            return false;
        }
        m_lastSend = now;
        return true;
    }

private:
    void setNonBlocking(socket_t s) {
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(s, FIONBIO, &mode);
#else
        int flags = fcntl(s, F_GETFL, 0);
        if (flags >= 0) fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
    }

    bool sendAll(const char* data, size_t n) {
        size_t off = 0;
        while (off < n) {
#ifdef _WIN32
            int r = ::send(m_client, data + off, static_cast<int>(n - off), 0);
            if (r == SOCKET_ERROR) {
                const int e = WSAGetLastError();
                if (e == WSAEWOULDBLOCK) continue;
                return false;
            }
#else
            ssize_t r = ::send(m_client, data + off, n - off, MSG_NOSIGNAL);
            if (r < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                return false;
            }
#endif
            if (r == 0) return false;
            off += static_cast<size_t>(r);
        }
        return true;
    }

    void dropClient() {
        if (m_client != kInvalidSock) {
#ifdef _WIN32
            closesocket(m_client);
#else
            ::close(m_client);
#endif
            m_client = kInvalidSock;
            std::fprintf(stderr, "TcpTelemetryBridge: client dropped\n");
        }
    }

    void closeUnlocked() {
        dropClient();
        if (m_listen != kInvalidSock) {
#ifdef _WIN32
            closesocket(m_listen);
#else
            ::close(m_listen);
#endif
            m_listen = kInvalidSock;
        }
        m_ok = false;
    }

    std::mutex m_mutex;
    bool m_ok = false;
    bool m_enabled = true;
    bool m_json = false;
    bool m_wsa = false;
    socket_t m_listen = kInvalidSock;
    socket_t m_client = kInvalidSock;
    uint16_t m_port = 20778;
    int m_minIntervalMs = 16; // ~60 Hz
    uint32_t m_seq = 0;
    std::chrono::steady_clock::time_point m_lastSend{};
};

} // namespace sim
} // namespace ks
