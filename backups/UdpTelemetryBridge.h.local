#pragma once
/**
 * UDP telemetry bridge — publish ksim live state to localhost/LAN.
 * Qt-free. Complements shared-memory for tools that prefer sockets.
 *
 * Default: UDP 127.0.0.1:20777  (listener side remains free for external AC streams)
 * Packet: little-endian binary header "KSIM" + UdpTelemPacket v1
 * Optional: JSON line mode for scripts (newline-terminated UTF-8)
 */
#include <cstdint>
#include <cstring>
#include <string>
#include <mutex>
#include <cstdio>
#include <cmath>

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
#endif

namespace ks {
namespace sim {

#pragma pack(push, 1)
/** Fixed binary payload (v1) — keep fields append-only for future versions. */
struct UdpTelemPacket {
    char magic[4];          // 'K','S','I','M'
    uint16_t version;       // 1
    uint16_t size;          // sizeof(UdpTelemPacket)
    uint32_t sequence;
    double timeSec;

    float speedMs;
    float rpm;
    float throttle;
    float brake;
    float steer;
    int32_t gear;
    float fuelL;

    float posX, posY, posZ;
    float velX, velY, velZ;
    float accGX, accGY, accGZ;
    float heading;

    float tyreTemp[4];
    float tyreWear[4];
    float tyrePressure[4];

    int32_t completedLaps;
    int32_t currentSector;
    int32_t currentTimeMs;
    int32_t lastTimeMs;
    int32_t bestTimeMs;
    int32_t position;
    int32_t sessionType;
    int32_t status;         // 0 off 2 live

    float normalizedSpline;
    float surfaceGrip;
    float airTemp;
    float roadTemp;
    uint8_t inPit;
    uint8_t pitLimiter;
    uint8_t _pad[2];
};
#pragma pack(pop)

static_assert(sizeof(UdpTelemPacket) < 1500, "UDP packet should fit one datagram");

struct UdpTelemSample {
    double timeSec = 0;
    float speedMs = 0, rpm = 0;
    float throttle = 0, brake = 0, steer = 0;
    int gear = 1;
    float fuelL = 0;
    float posX = 0, posY = 0, posZ = 0;
    float velX = 0, velY = 0, velZ = 0;
    float accGX = 0, accGY = 0, accGZ = 0;
    float heading = 0;
    float tyreTemp[4] = {80, 80, 80, 80};
    float tyreWear[4] = {};
    float tyrePressure[4] = {2.2f, 2.2f, 2.0f, 2.0f};
    int completedLaps = 0;
    int currentSector = 0;
    int currentTimeMs = 0;
    int lastTimeMs = 0;
    int bestTimeMs = 0;
    int position = 1;
    int sessionType = 2;
    int status = 2;
    float normalizedSpline = 0;
    float surfaceGrip = 1.f;
    float airTemp = 25.f;
    float roadTemp = 30.f;
    bool inPit = false;
    bool pitLimiter = false;
};

class UdpTelemetryBridge {
public:
    UdpTelemetryBridge() = default;
    ~UdpTelemetryBridge() { close(); }

    /** Bind local ephemeral port and set destination. */
    bool open(const char* host = "127.0.0.1", uint16_t port = 20777) {
        std::lock_guard<std::mutex> lock(m_mutex);
        closeUnlocked();

#ifdef _WIN32
        if (!m_wsa) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                std::fprintf(stderr, "UdpTelemetryBridge: WSAStartup failed\n");
                return false;
            }
            m_wsa = true;
        }
        m_sock = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (m_sock == INVALID_SOCKET) {
            m_sock = -1;
            return false;
        }
#else
        m_sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (m_sock < 0) return false;
        int flags = fcntl(m_sock, F_GETFL, 0);
        if (flags >= 0) fcntl(m_sock, F_SETFL, flags | O_NONBLOCK);
#endif

        std::memset(&m_dest, 0, sizeof(m_dest));
        m_dest.sin_family = AF_INET;
        m_dest.sin_port = htons(port);
        if (::inet_pton(AF_INET, host, &m_dest.sin_addr) != 1) {
            std::fprintf(stderr, "UdpTelemetryBridge: bad host %s\n", host);
            closeUnlocked();
            return false;
        }

        m_host = host;
        m_port = port;
        m_ok = true;
        std::fprintf(stderr, "UdpTelemetryBridge: → %s:%u\n", host, static_cast<unsigned>(port));
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        closeUnlocked();
    }

    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const { return m_enabled; }
    bool isOpen() const { return m_ok; }

    void setJsonMode(bool j) { m_json = j; }
    bool jsonMode() const { return m_json; }

    /** Send one sample (binary or JSON). Thread-safe. */
    bool publish(const UdpTelemSample& s) {
        if (!m_enabled) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_ok || m_sock < 0) return false;

        m_seq++;
        if (m_json)
            return sendJson(s);
        return sendBinary(s);
    }

    uint32_t sequence() const { return m_seq; }

private:
    bool sendBinary(const UdpTelemSample& s) {
        UdpTelemPacket p{};
        p.magic[0] = 'K'; p.magic[1] = 'S'; p.magic[2] = 'I'; p.magic[3] = 'M';
        p.version = 1;
        p.size = static_cast<uint16_t>(sizeof(UdpTelemPacket));
        p.sequence = m_seq;
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

        return sendBytes(reinterpret_cast<const char*>(&p), sizeof(p));
    }

    bool sendJson(const UdpTelemSample& s) {
        char buf[1024];
        int n = std::snprintf(buf, sizeof(buf),
            "{\"seq\":%u,\"t\":%.3f,\"v\":%.2f,\"rpm\":%.0f,\"gear\":%d,"
            "\"thr\":%.3f,\"brk\":%.3f,\"str\":%.3f,\"fuel\":%.2f,"
            "\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,"
            "\"lap\":%d,\"sec\":%d,\"ct\":%d,\"bt\":%d,\"pos\":%d,\"spline\":%.4f}\n",
            m_seq, s.timeSec, s.speedMs * 3.6, s.rpm, s.gear,
            s.throttle, s.brake, s.steer, s.fuelL,
            s.posX, s.posY, s.posZ,
            s.completedLaps, s.currentSector, s.currentTimeMs, s.bestTimeMs,
            s.position, s.normalizedSpline);
        if (n <= 0) return false;
        return sendBytes(buf, static_cast<size_t>(n));
    }

    bool sendBytes(const char* data, size_t n) {
#ifdef _WIN32
        int sent = ::sendto(m_sock, data, static_cast<int>(n), 0,
                            reinterpret_cast<sockaddr*>(&m_dest), sizeof(m_dest));
        return sent == static_cast<int>(n);
#else
        ssize_t sent = ::sendto(m_sock, data, n, 0,
                                reinterpret_cast<sockaddr*>(&m_dest), sizeof(m_dest));
        return sent == static_cast<ssize_t>(n);
#endif
    }

    void closeUnlocked() {
        if (m_sock >= 0) {
#ifdef _WIN32
            closesocket(static_cast<SOCKET>(m_sock));
#else
            ::close(m_sock);
#endif
            m_sock = -1;
        }
        m_ok = false;
    }

    std::mutex m_mutex;
    bool m_ok = false;
    bool m_enabled = true;
    bool m_json = false;
    bool m_wsa = false;
    int m_sock = -1;
    sockaddr_in m_dest{};
    std::string m_host;
    uint16_t m_port = 20777;
    uint32_t m_seq = 0;
};

} // namespace sim
} // namespace ks
