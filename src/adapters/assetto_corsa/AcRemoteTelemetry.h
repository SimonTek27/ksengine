#pragma once
/**
 * AC remote-telemetry server (UDP :9996) — header-only.
 *
 * Wire-compatible with the Assetto Corsa "remote telemetry" client that
 * external dash/overlay tools (rsTimer, Live, SimTools, ...) connect to.
 * Reverse-engineered from acs.exe's RemoteTelemetryUDP (bind 0x270c = 9996,
 * handler FUN_140146cc0 switch on the dword at recv offset 8) and cross-checked
 * against the public Kunos extended-format spec:
 *
 *   client -> server  Handshaker       12 B   identifier, version, operationId
 *   server -> client  HandshakerResp  408 B   carName, driverName, status,
 *                                             version, trackName, trackConfig
 *   server -> client  RTCarInfo       328 B   live car state, on subscription
 *   server -> client  RTLap           212 B   completed-lap event
 *   server -> client  CarList header    8 B   [sessionValue][carCount]
 *   server -> client  CarList record  208 B   driverName, carName, 2 int32
 *   server -> client  CarId response    4 B   car index (op 5)
 *
 * operationId: 0 = Connect, 1 = CarInfo, 2 = LapInfo, 3 = Disconnect,
 *              4 = CarList (enumerazione auto, header 8 B + record 0xd0),
 *              5 = CarId  (risposta 4 B).
 * Strings are fixed UTF-16LE[50] (100 B); numbers little-endian;
 * #pragma pack(4).
 *
 * Terminator note (acs.exe, FUN_140146cc0): Connect e car-list stringhe sono
 * terminate con L'%' (0x25), NON con L'0' — i client AC veri scansionano fino
 * a '%'. ksengine scrive '%' + zero-fill (AC lasciava byte non inizializzati).
 * RTLap qui resta NUL-terminated (open question: non verificato su acs.exe).
 *
 * This is the OUT direction only: kssim is the telemetry *source* (like
 * acs.exe), external tools are passive readers. It complements the KSIM-native
 * UdpTelemetryBridge (:20777) and does not replace it.
 */
#include "AcSharedMemory.h" // AcLiveInput

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <cstdio>

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
namespace ac {

// operationId values in the 12-byte Handshaker.
enum AcRemoteOp : uint32_t {
    AC_REMOTE_CONNECT = 0,
    AC_REMOTE_CARINFO = 1,
    AC_REMOTE_LAPINFO = 2,
    AC_REMOTE_DISCONNECT = 3,
    AC_REMOTE_CARLIST = 4, // reply: 8 B header + N * 208 B car records
    AC_REMOTE_CARID = 5,   // reply: 4 B car index
};

#pragma pack(push, 4)

/** client -> server, 12 B. */
struct AcRemoteHandshaker {
    uint32_t identifier = 1;  // device id (usually 1)
    uint32_t version = 1;     // protocol version (usually 1)
    uint32_t operationId = AC_REMOTE_CONNECT;
};
static_assert(sizeof(AcRemoteHandshaker) == 12, "Handshaker must be 12 B");

/** UTF-16LE fixed-width string (50 chars = 100 B), null-terminated. */
struct AcRemoteWStr {
    uint16_t ch[50] = {};
};
static_assert(sizeof(AcRemoteWStr) == 100, "UTF-16LE[50] must be 100 B");

/** server -> client, 408 B — reply to Connect. */
struct AcRemoteHandshakerResponse {
    AcRemoteWStr carName;
    AcRemoteWStr driverName;
    uint32_t identifier = 4242; // status code
    uint32_t version = 1;       // server version
    AcRemoteWStr trackName;
    AcRemoteWStr trackConfig;
};
static_assert(sizeof(AcRemoteHandshakerResponse) == 408, "HandshakerResponse must be 408 B");

/** server -> client, 328 B — live car state (RTCarInfo). */
struct AcRemoteCarInfo {
    char identifier[2] = {'A', 'C'}; // "AC"
    uint32_t size = 328;             // struct size
    float speedKmh = 0;
    float speedMph = 0;
    float speedMs = 0;
    uint8_t isAbsEnabled = 0;
    uint8_t isAbsInAction = 0;
    uint8_t isTcInAction = 0;
    uint8_t isTcEnabled = 0;
    uint8_t isInPit = 0;
    uint8_t isEngineLimiterOn = 0;
    uint8_t _pad0[2] = {0, 0}; // align accG to offset 28
    float accGVertical = 0;    // offset 28
    float accGHorizontal = 0;  // 32
    float accGFrontal = 0;     // 36
    uint32_t lapTime = 0;      // 40  ms
    uint32_t lastLap = 0;      // 44  ms
    uint32_t bestLap = 0;      // 48  ms
    uint32_t lapCount = 0;     // 52
    float gas = 0;             // 56
    float brake = 0;           // 60
    float clutch = 0;          // 64
    float engineRPM = 0;       // 68
    float steer = 0;           // 72
    uint32_t gear = 1;         // 76  0=R, 1=N, 2+=forward
    float cgHeight = 0;        // 80
    float wheelAngularSpeed[4] = {}; // 84
    float slipAngle[4] = {};         // 100
    float slipAngleContactPatch[4] = {}; // 116
    float slipRatio[4] = {};         // 132
    float tyreSlip[4] = {};          // 148
    float ndSlip[4] = {};            // 164
    float load[4] = {};              // 180
    float Dy[4] = {};                // 196
    float Mz[4] = {};                // 212
    float tyreDirtyLevel[4] = {};    // 228
    float camberRad[4] = {};         // 244
    float tyreRadius[4] = {};        // 260
    float tyreLoadedRadius[4] = {};  // 276
    float suspensionHeight[4] = {};  // 292
    float carPositionNormalized = 0; // 308
    float carSlope = 0;              // 312
    float carCoordinates[3] = {};    // 316
};
static_assert(sizeof(AcRemoteCarInfo) == 328, "RTCarInfo must be 328 B");

/** server -> client, 212 B — completed-lap event (RTLap). */
struct AcRemoteLap {
    uint32_t carIdentifierNumber = 0;
    uint32_t lap = 0;
    AcRemoteWStr driverName;
    AcRemoteWStr carName;
    uint32_t time = 0; // ms
};
static_assert(sizeof(AcRemoteLap) == 212, "RTLap must be 212 B");

/**
 * server -> client, 8 B — header of the op-4 (CarList) reply, sent once
 * before the per-car records. acs.exe (FUN_140146cc0 case 4):
 *   [int32 = *(session+0x1fc) — semantica ignota (open question)]
 *   [int32 = car count = (carsVector.end - begin) >> 3]
 * The count is the TOTAL number of cars; the filter (index 0 always sent,
 * the others only when visible/net — FUN_1400d8b40) applies to the records.
 */
struct AcRemoteCarListHeader {
    int32_t sessionValue = 0; // AC: *(int*)(session+0x1fc); kssim: 0
    int32_t carCount = 0;
};
static_assert(sizeof(AcRemoteCarListHeader) == 8, "CarList header must be 8 B");

/**
 * server -> client, 0xd0 = 208 B — one op-4 car record.
 * acs.exe (FUN_140146cc0 case 4) fills it as:
 *   [0..100)   driverName UTF-16LE[50] from car+0x10b8, '%' terminated
 *   [100..200) carName   UTF-16LE[50] from car+0x138,  '%' terminated
 *   [200..204) int32 leaderboard index (FUN_140048310(leaderboard, car),
 *              0xFFFFFFFF if the car is not in the leaderboard)
 *   [204..208) int32 car field +0x1158 (FUN_1400d3500, semantica ignota)
 * NOTE: driverName comes FIRST here — the Connect (op 0) response has
 * carName first. % terminators as in AC; ksengine zero-fills the tail.
 */
struct AcRemoteCarListRecord {
    AcRemoteWStr driverName; // 100 B, '%' terminated (AC wire)
    AcRemoteWStr carName;    // 100 B, '%' terminated (AC wire)
    int32_t leaderboardIndex = -1; // AC: FUN_140048310; -1 = not ranked
    int32_t carField1158 = 0;      // AC: *(car+0x1158); semantics open
};
static_assert(sizeof(AcRemoteCarListRecord) == 208, "CarList record must be 0xd0 = 208 B");

/** server -> client, 4 B — op-5 (CarId) reply: a car index. */
struct AcRemoteCarIdResponse {
    int32_t carIndex = 0; // AC: FUN_140048310; kssim: index of the player car
};
static_assert(sizeof(AcRemoteCarIdResponse) == 4, "CarId response must be 4 B");

#pragma pack(pop)

/**
 * Copy an ASCII/UTF-8 std::string into a fixed UTF-16LE[50] field.
 *
 * acs.exe terminates its remote-telemetry strings with L'%' (0x25), not with
 * L'\0' — verified on the Connect (op 0) and CarList (op 4) paths in
 * FUN_140146cc0: after the content it writes byte pair ('%','\0'), an empty
 * string is an immediate '%', and the rest of the field stays uninitialized
 * in AC. Real AC clients therefore scan up to '%'. Pass term='%' for those
 * fields (ksengine also zero-fills the tail, strictly safer than AC).
 * RTLap (op 2 broadcast) was NOT verified on acs.exe and keeps NUL (open
 * question), so the default term is 0.
 */
inline void acRemoteCopyW(AcRemoteWStr& dst, const std::string& src, uint16_t term = 0) {
    size_t n = src.size();
    if (n > 49) n = 49;
    for (size_t i = 0; i < n; ++i)
        dst.ch[i] = static_cast<uint16_t>(static_cast<unsigned char>(src[i]));
    dst.ch[n] = term;
}

/**
 * Fill an RTCarInfo snapshot from the shared AcLiveInput sample.
 * Channels AC exposes but ksengine does not yet source (Dy, Mz, tyreRadius,
 * loadedRadius, angularSpeed, slipRatio, ndSlip) are left at 0.
 */
inline void buildRemoteCarInfo(AcRemoteCarInfo& out, const AcLiveInput& in) {
    out.speedMs = in.speedMs;
    out.speedKmh = in.speedMs * 3.6f;
    out.speedMph = in.speedMs * 2.2369363f;
    out.gas = in.throttle;
    out.brake = in.brake;
    out.clutch = in.clutch;
    out.engineRPM = in.rpm;
    out.steer = in.steer;
    // AC gear convention: 0=R, 1=N, 2+=forward (matches AcSharedMemory fillPhysics).
    if (in.gear < 0) out.gear = 0;
    else if (in.gear == 0) out.gear = 1;
    else out.gear = static_cast<uint32_t>(in.gear) + 1u;
    out.isInPit = in.inPit ? 1 : 0;
    out.isEngineLimiterOn = in.pitLimiter ? 1 : 0;
    // Axis mapping (ksengine: x=forward, y=left, z=up) -> AC
    // [vertical, horizontal, frontal]. Verify against real AC capture.
    out.accGVertical = in.accG[2];
    out.accGHorizontal = in.accG[1];
    out.accGFrontal = in.accG[0];
    out.lapTime = static_cast<uint32_t>(in.iCurrentTimeMs > 0 ? in.iCurrentTimeMs : 0);
    out.lastLap = static_cast<uint32_t>(in.iLastTimeMs > 0 ? in.iLastTimeMs : 0);
    out.bestLap = static_cast<uint32_t>(in.iBestTimeMs > 0 ? in.iBestTimeMs : 0);
    out.lapCount = static_cast<uint32_t>(in.completedLaps > 0 ? in.completedLaps : 0);
    for (int i = 0; i < 4; ++i) {
        out.slipAngle[i] = in.wheelSlip[i];
        out.load[i] = in.wheelLoad[i];
        out.tyreDirtyLevel[i] = in.tyreWear[i];
        out.camberRad[i] = in.camber[i];
        out.suspensionHeight[i] = in.suspensionTravel[i];
    }
    out.carPositionNormalized = in.normalizedSpline;
    out.carSlope = in.pitch;
    out.carCoordinates[0] = in.carX;
    out.carCoordinates[1] = in.carY;
    out.carCoordinates[2] = in.carZ;
}

/**
 * One entry of the op-4/5 car roster (kssim-side, not a wire struct).
 * See AcRemoteTelemetryServer::setCarList().
 */
struct CarListEntry {
    std::string driver;
    std::string car;
    int32_t leaderboardIndex = -1; // -1 = not ranked (AC: 0xFFFFFFFF)
    int32_t carField1158 = 0;      // opaque, see AcRemoteCarListRecord
};

/**
 * UDP remote-telemetry server bound on a single port (default 9996).
 *
 * Usage:
 *   AcRemoteTelemetryServer srv;
 *   srv.setIdentity(carModel, driverName, trackName, trackConfig);
 *   srv.open(9996);
 *   // each tick:  srv.pump();  srv.publishCarInfo(in);
 *   // on lap:     srv.publishLap(in, lapNumber, lapTimeMs);
 *
 * Non-blocking; safe to pump from the sim thread. Registry capped at
 * kMaxClients so a rogue scanner cannot grow memory without bound.
 */
class AcRemoteTelemetryServer {
public:
    static constexpr size_t kMaxClients = 16;

    AcRemoteTelemetryServer() = default;
    ~AcRemoteTelemetryServer() { close(); }

    void setIdentity(const std::string& car, const std::string& driver,
                     const std::string& track, const std::string& config) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_car = car; m_driver = driver; m_track = track; m_config = config;
    }

    /**
     * Car roster served on op 4 (CarList) and op 5 (CarId).
     * kssim fills one entry per active car (single-player: the player car).
     * leaderboardIndex: rank of the car, -1 = not ranked (AC sends 0xFFFFFFFF).
     * carField1158: opaque int AC reads from car+0x1158; kssim uses the car
     * index in session (semantics open — see REPORT_AC_RE_ENGINEERING.md).
     * If never set, the roster falls back to the setIdentity() car/driver.
     */
    void setCarList(const std::vector<CarListEntry>& cars) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_carList = cars;
    }
    void clearCarList() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_carList.clear();
    }

    bool open(uint16_t port = 9996) {
        std::lock_guard<std::mutex> lock(m_mutex);
        closeUnlocked();
#ifdef _WIN32
        if (!m_wsa) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
            m_wsa = true;
        }
        m_sock = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (m_sock == INVALID_SOCKET) { m_sock = -1; return false; }
        u_long nb = 1;
        ioctlsocket(static_cast<SOCKET>(m_sock), FIONBIO, &nb);
#else
        m_sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (m_sock < 0) return false;
        int flags = fcntl(m_sock, F_GETFL, 0);
        if (flags >= 0) fcntl(m_sock, F_SETFL, flags | O_NONBLOCK);
#endif
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(m_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            std::fprintf(stderr, "AcRemoteTelemetry: bind :%u failed\n", unsigned(port));
            closeUnlocked();
            return false;
        }
        m_port = port;
        m_ok = true;
        std::fprintf(stderr, "AcRemoteTelemetry: server ready on UDP :%u\n", unsigned(port));
        return true;
    }

    void close() { std::lock_guard<std::mutex> lock(m_mutex); closeUnlocked(); }
    bool isOpen() const { return m_ok; }
    size_t clientCount() const { std::lock_guard<std::mutex> lock(m_mutex); return m_clients.size(); }

    /** Drain pending handshakers (Connect/CarInfo/LapInfo/Disconnect). */
    void pump() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_ok || m_sock < 0) return;
        for (int iter = 0; iter < 64; ++iter) { // bounded burst per pump
            char buf[512];
            sockaddr_in from{};
#ifdef _WIN32
            int flen = sizeof(from);
            int n = ::recvfrom(m_sock, buf, sizeof(buf), 0,
                               reinterpret_cast<sockaddr*>(&from), &flen);
#else
            socklen_t flen = sizeof(from);
            ssize_t n = ::recvfrom(m_sock, buf, sizeof(buf), 0,
                                   reinterpret_cast<sockaddr*>(&from), &flen);
#endif
            if (n < static_cast<int>(sizeof(AcRemoteHandshaker))) break;
            handleHandshaker(buf, static_cast<size_t>(n), from);
        }
    }

    /** Broadcast an RTCarInfo frame to every CarInfo subscriber. */
    void publishCarInfo(const AcLiveInput& in) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_ok || m_clients.empty()) return;
        AcRemoteCarInfo pkt;
        buildRemoteCarInfo(pkt, in);
        for (auto& c : m_clients)
            if (c.carInfo) sendTo(c.addr, &pkt, sizeof(pkt));
    }

    /** Send an RTLap event to every LapInfo subscriber. */
    void publishLap(const AcLiveInput& in, uint32_t lapNumber, uint32_t lapTimeMs) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_ok || m_clients.empty()) return;
        AcRemoteLap pkt;
        pkt.carIdentifierNumber = 0;
        pkt.lap = lapNumber;
        acRemoteCopyW(pkt.driverName, in.playerName.empty() ? "Driver" : in.playerName);
        acRemoteCopyW(pkt.carName, in.carModel);
        pkt.time = lapTimeMs;
        for (auto& c : m_clients)
            if (c.lapInfo) sendTo(c.addr, &pkt, sizeof(pkt));
    }

private:
    struct Client {
        sockaddr_in addr{};
        bool carInfo = false;
        bool lapInfo = false;
    };

    void handleHandshaker(const char* buf, size_t n, const sockaddr_in& from) {
        AcRemoteHandshaker hs;
        std::memcpy(&hs, buf, sizeof(hs));
        switch (hs.operationId) {
        case AC_REMOTE_CONNECT: {
            Client* c = findOrAdd(from);
            if (!c) return;
            AcRemoteHandshakerResponse resp;
            // '%' terminators: AC (FUN_140146cc0 case 0) terminates these
            // strings with L'%' not L'\0'. Zero-fill tail covers the rest.
            acRemoteCopyW(resp.carName, m_car, '%');
            acRemoteCopyW(resp.driverName, m_driver.empty() ? "Driver" : m_driver, '%');
            acRemoteCopyW(resp.trackName, m_track, '%');
            acRemoteCopyW(resp.trackConfig, m_config, '%');
            sendTo(from, &resp, sizeof(resp));
            break;
        }
        case AC_REMOTE_CARINFO: {
            Client* c = findOrAdd(from);
            if (c) c->carInfo = true;
            break;
        }
        case AC_REMOTE_LAPINFO: {
            Client* c = findOrAdd(from);
            if (c) c->lapInfo = true;
            break;
        }
        case AC_REMOTE_DISCONNECT:
            removeClient(from);
            break;
        case AC_REMOTE_CARLIST: {
            // Reply: 8 B header + one 208 B record per car (AC does NOT
            // register the requester as a subscriber here).
            AcRemoteCarListHeader hdr;
            // Roster: setCarList() if provided, else the identity car.
            std::vector<CarListEntry> list = m_carList;
            if (list.empty()) {
                CarListEntry e;
                e.driver = m_driver.empty() ? "Driver" : m_driver;
                e.car = m_car;
                list.push_back(e);
            }
            hdr.carCount = static_cast<int32_t>(list.size());
            sendTo(from, &hdr, sizeof(hdr));
            for (const auto& e : list) {
                AcRemoteCarListRecord rec;
                // Driver FIRST then car — reversed vs the Connect response.
                acRemoteCopyW(rec.driverName, e.driver, '%');
                acRemoteCopyW(rec.carName, e.car, '%');
                rec.leaderboardIndex = e.leaderboardIndex;
                rec.carField1158 = e.carField1158;
                sendTo(from, &rec, sizeof(rec));
            }
            break;
        }
        case AC_REMOTE_CARID: {
            // Reply 4 B: index of the player car (list entry 0).
            AcRemoteCarIdResponse r;
            r.carIndex = 0;
            sendTo(from, &r, sizeof(r));
            break;
        }
        default:
            break;
        }
    }

    Client* findOrAdd(const sockaddr_in& addr) {
        for (auto& c : m_clients)
            if (sameEndpoint(c.addr, addr)) return &c;
        if (m_clients.size() >= kMaxClients) return nullptr;
        Client nc;
        nc.addr = addr;
        m_clients.push_back(nc);
        return &m_clients.back();
    }

    void removeClient(const sockaddr_in& addr) {
        for (size_t i = 0; i < m_clients.size(); ++i) {
            if (sameEndpoint(m_clients[i].addr, addr)) {
                m_clients.erase(m_clients.begin() + static_cast<long>(i));
                return;
            }
        }
    }

    static bool sameEndpoint(const sockaddr_in& a, const sockaddr_in& b) {
        return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
    }

    void sendTo(const sockaddr_in& dst, const void* data, size_t n) {
#ifdef _WIN32
        ::sendto(m_sock, static_cast<const char*>(data), static_cast<int>(n), 0,
                 reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
#else
        ::sendto(m_sock, data, n, 0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
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
        m_clients.clear();
        m_ok = false;
    }

    mutable std::mutex m_mutex;
    bool m_ok = false;
    bool m_wsa = false;
    int m_sock = -1;
    uint16_t m_port = 9996;
    std::string m_car, m_driver, m_track, m_config;
    std::vector<CarListEntry> m_carList; // op 4/5 roster (empty = identity car)
    std::vector<Client> m_clients;
};

} // namespace ac
} // namespace ks
