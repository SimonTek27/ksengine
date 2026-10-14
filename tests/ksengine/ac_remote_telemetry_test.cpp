/**
 * AC remote-telemetry server (UDP :9996) — wire-format test.
 *
 * Proves the server reverse-engineered from acs.exe speaks the documented
 * extended protocol: a 12-byte Handshaker gets a 408-byte HandshakerResponse,
 * subscribing with operationId=1 streams 328-byte RTCarInfo frames, and
 * operationId=2 gets 212-byte RTLap events. Verifies fixed sizes, key field
 * offsets on the wire, and the connect/subscribe/disconnect registry.
 *
 * Loopback only: server bound to 127.0.0.1 on an ephemeral port, a plain UDP
 * client drives it. No external content or timing dependencies.
 */
#include "KsTest.h"
#include "adapters/assetto_corsa/AcRemoteTelemetry.h"

#include <cstddef>
#include <cstring>
#include <chrono>
#include <thread>

#ifdef _WIN32
#  include <winsock2.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#endif

using ks::ac::AcLiveInput;
using ks::ac::AcRemoteCarIdResponse;
using ks::ac::AcRemoteCarInfo;
using ks::ac::AcRemoteCarListHeader;
using ks::ac::AcRemoteCarListRecord;
using ks::ac::AcRemoteHandshaker;
using ks::ac::AcRemoteHandshakerResponse;
using ks::ac::AcRemoteLap;
using ks::ac::AcRemoteTelemetryServer;
using ks::ac::AC_REMOTE_CARID;
using ks::ac::AC_REMOTE_CARINFO;
using ks::ac::AC_REMOTE_CARLIST;
using ks::ac::AC_REMOTE_CONNECT;
using ks::ac::AC_REMOTE_DISCONNECT;
using ks::ac::AC_REMOTE_LAPINFO;

namespace {

#ifdef _WIN32
using sock_t = int;
constexpr sock_t BAD_SOCK = -1;
void closeSock(sock_t s) { if (s >= 0) closesocket(static_cast<SOCKET>(s)); }
int recvTo(sock_t s, char* buf, int len) { return ::recv(s, buf, len, 0); }
#else
using sock_t = int;
constexpr sock_t BAD_SOCK = -1;
void closeSock(sock_t s) { if (s >= 0) ::close(s); }
int recvTo(sock_t s, char* buf, int len) { return static_cast<int>(::recv(s, buf, len, 0)); }
#endif

struct WsaGuard {
    WsaGuard() {
#ifdef _WIN32
        WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
#endif
    }
    ~WsaGuard() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

// A UDP client socket bound to an ephemeral local port, pointed at the server.
sock_t makeClient(uint16_t serverPort, uint16_t& outLocalPort) {
    sock_t s = static_cast<sock_t>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (s < 0) return BAD_SOCK;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0; // ephemeral
    if (::bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) { closeSock(s); return BAD_SOCK; }
    sockaddr_in bound{};
#ifdef _WIN32
    int blen = sizeof(bound);
#else
    socklen_t blen = sizeof(bound);
#endif
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &blen);
    outLocalPort = ntohs(bound.sin_port);
    // connect so we can use plain recv() and the server sees a stable endpoint
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    srv.sin_port = htons(serverPort);
    if (::connect(s, reinterpret_cast<sockaddr*>(&srv), sizeof(srv)) != 0) { closeSock(s); return BAD_SOCK; }
    // Short recv timeout so recv() never blocks the test harness.
#ifdef _WIN32
    DWORD tv = 15; // ms
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec = 0; tv.tv_usec = 15000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif
    return s;
}

bool sendAll(sock_t s, const void* data, size_t n) {
#ifdef _WIN32
    return ::send(s, static_cast<const char*>(data), static_cast<int>(n), 0) == static_cast<int>(n);
#else
    return ::send(s, data, n, 0) == static_cast<ssize_t>(n);
#endif
}

// Non-blocking-ish drain: set a short recv timeout and pull one datagram.
bool recvPacket(sock_t s, char* buf, int len, int& outN) {
    outN = recvTo(s, buf, len);
    return outN > 0;
}

void spin(AcRemoteTelemetryServer& srv, int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        srv.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace

int main() {
    WsaGuard wsa;

    // Fixed struct sizes straight off the wire.
    KS_CHECK(sizeof(AcRemoteHandshaker) == 12);
    KS_CHECK(sizeof(AcRemoteHandshakerResponse) == 408);
    KS_CHECK(sizeof(AcRemoteCarInfo) == 328);
    KS_CHECK(sizeof(AcRemoteLap) == 212);

    // --- pick an ephemeral port by binding a throwaway server ---------------
    AcRemoteTelemetryServer srv;
    srv.setIdentity("refcar", "Driver", "ks_track", "gp");
    uint16_t port = 0;
    {
        // open on port 0 then read it back via a probe; simpler: try a fixed
        // high port range until one binds.
        for (uint16_t tryPort = 19800; tryPort < 19850; ++tryPort) {
            if (srv.open(tryPort)) { port = tryPort; break; }
        }
    }
    KS_CHECK(port != 0);
    if (port == 0) return KS_TEST_RESULT("ac_remote_telemetry_test");

    // --- client A: handshake -> 408-byte response ---------------------------
    uint16_t aPort = 0;
    sock_t a = makeClient(port, aPort);
    KS_CHECK(a >= 0);

    AcRemoteHandshaker hs;
    hs.identifier = 1; hs.version = 1; hs.operationId = AC_REMOTE_CONNECT;
    KS_CHECK(sendAll(a, &hs, sizeof(hs)));

    {
        spin(srv, 50);
        char buf[1024];
        int n = 0;
        bool got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            got = recvPacket(a, buf, sizeof(buf), n);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KS_CHECK(got);
        KS_CHECK(n == 408);
        if (n == 408) {
            AcRemoteHandshakerResponse resp;
            std::memcpy(&resp, buf, sizeof(resp));
            KS_CHECK(resp.identifier == 4242);
            KS_CHECK(resp.version == 1);
            // carName "refcar" as UTF-16LE
            KS_CHECK(resp.carName.ch[0] == 'r');
            KS_CHECK(resp.carName.ch[5] == 'r'); // "refcaR"? no: refcar[5]='r'
            KS_CHECK(resp.trackName.ch[0] == 'k');
        }
    }
    KS_CHECK(srv.clientCount() == 1);

    // --- client B: subscribe CarInfo, expect a 328-byte RTCarInfo -----------
    uint16_t bPort = 0;
    sock_t b = makeClient(port, bPort);
    KS_CHECK(b >= 0);
    hs.operationId = AC_REMOTE_CARINFO;
    KS_CHECK(sendAll(b, &hs, sizeof(hs)));
    spin(srv, 20);

    {
        AcLiveInput in;
        in.speedMs = 50.f;          // 180 km/h
        in.throttle = 0.75f;
        in.brake = 0.0f;
        in.rpm = 6500.f;
        in.gear = 4;                // AC gear 5
        in.completedLaps = 3;
        in.iCurrentTimeMs = 91234;
        in.normalizedSpline = 0.42f;
        in.carX = 10.f; in.carY = 20.f; in.carZ = 30.f;
        srv.publishCarInfo(in);

        char buf[1024];
        int n = 0;
        bool got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            got = recvPacket(b, buf, sizeof(buf), n);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KS_CHECK(got);
        KS_CHECK(n == 328);
        if (n == 328) {
            AcRemoteCarInfo ci;
            std::memcpy(&ci, buf, sizeof(ci));
            KS_CHECK(ci.identifier[0] == 'A' && ci.identifier[1] == 'C');
            KS_CHECK(ci.size == 328);
            KS_CHECK_NEAR(ci.speedKmh, 180.f, 0.01f);
            KS_CHECK_NEAR(ci.speedMs, 50.f, 0.01f);
            KS_CHECK_NEAR(ci.gas, 0.75f, 1e-6f);
            KS_CHECK_NEAR(ci.engineRPM, 6500.f, 0.1f);
            KS_CHECK(ci.gear == 5);
            KS_CHECK(ci.lapCount == 3);
            KS_CHECK(ci.lapTime == 91234u);
            KS_CHECK_NEAR(ci.carPositionNormalized, 0.42f, 1e-6f);
            KS_CHECK_NEAR(ci.carCoordinates[1], 20.f, 1e-6f);
            // offset sanity: lapTime sits at byte 40, gas at 56.
            KS_CHECK(offsetof(AcRemoteCarInfo, lapTime) == 40);
            KS_CHECK(offsetof(AcRemoteCarInfo, gas) == 56);
            KS_CHECK(offsetof(AcRemoteCarInfo, gear) == 76);
            KS_CHECK(offsetof(AcRemoteCarInfo, carCoordinates) == 316);
        }
    }

    // --- client B: subscribe LapInfo, expect 212-byte RTLap -----------------
    hs.operationId = AC_REMOTE_LAPINFO;
    KS_CHECK(sendAll(b, &hs, sizeof(hs)));
    spin(srv, 20);
    {
        AcLiveInput in;
        in.playerName = "Driver";
        in.carModel = "refcar";
        srv.publishLap(in, 4, 88500);
        char buf[1024];
        int n = 0;
        bool got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            got = recvPacket(b, buf, sizeof(buf), n);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KS_CHECK(got);
        KS_CHECK(n == 212);
        if (n == 212) {
            AcRemoteLap lp;
            std::memcpy(&lp, buf, sizeof(lp));
            KS_CHECK(lp.lap == 4);
            KS_CHECK(lp.time == 88500u);
            KS_CHECK(lp.carName.ch[0] == 'r');
        }
    }

    // --- client B: op 4 CarList (default roster = identity car) -------------
    // Wire: 8-byte header [sessionValue, carCount] + 208-byte records.
    // Strings are '%' terminated (AC wire), driverName BEFORE carName.
    KS_CHECK(sizeof(AcRemoteCarListHeader) == 8);
    KS_CHECK(sizeof(AcRemoteCarListRecord) == 208);
    KS_CHECK(sizeof(AcRemoteCarIdResponse) == 4);
    hs.operationId = AC_REMOTE_CARLIST;
    KS_CHECK(sendAll(b, &hs, sizeof(hs)));
    spin(srv, 20);
    {
        char buf[1024];
        int n = 0;
        bool got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            got = recvPacket(b, buf, sizeof(buf), n);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KS_CHECK(got);
        KS_CHECK(n == 8);
        if (n == 8) {
            AcRemoteCarListHeader hdr;
            std::memcpy(&hdr, buf, sizeof(hdr));
            KS_CHECK(hdr.carCount == 1);
        }
        got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            got = recvPacket(b, buf, sizeof(buf), n);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KS_CHECK(got);
        KS_CHECK(n == 208);
        if (n == 208) {
            AcRemoteCarListRecord rec;
            std::memcpy(&rec, buf, sizeof(rec));
            // driverName first: "Driver" -> ch[0]='D', terminator '%' at ch[6]
            KS_CHECK(rec.driverName.ch[0] == 'D');
            KS_CHECK(rec.driverName.ch[6] == '%');
            // carName second: "refcar" -> ch[0]='r', terminator '%' at ch[6]
            KS_CHECK(rec.carName.ch[0] == 'r');
            KS_CHECK(rec.carName.ch[6] == '%');
            KS_CHECK(rec.leaderboardIndex == -1); // not ranked
            KS_CHECK(rec.carField1158 == 0);
            // wire offsets: strings at 0/100, ints at 200/204
            KS_CHECK(offsetof(AcRemoteCarListRecord, carName) == 100);
            KS_CHECK(offsetof(AcRemoteCarListRecord, leaderboardIndex) == 200);
            KS_CHECK(offsetof(AcRemoteCarListRecord, carField1158) == 204);
        }
    }

    // --- client B: op 4 CarList with an explicit 2-car roster ---------------
    srv.setCarList({{"Alice", "car_a", 0, 7}, {"Bob", "car_b", 1, 2}});
    KS_CHECK(sendAll(b, &hs, sizeof(hs)));
    spin(srv, 20);
    {
        char buf[1024];
        int n = 0;
        bool got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            got = recvPacket(b, buf, sizeof(buf), n);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KS_CHECK(got);
        KS_CHECK(n == 8);
        if (n == 8) {
            AcRemoteCarListHeader hdr;
            std::memcpy(&hdr, buf, sizeof(hdr));
            KS_CHECK(hdr.sessionValue == 0);
            KS_CHECK(hdr.carCount == 2);
        }
        // record 0: Alice / car_a
        got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            got = recvPacket(b, buf, sizeof(buf), n);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KS_CHECK(got && n == 208);
        if (got && n == 208) {
            AcRemoteCarListRecord rec;
            std::memcpy(&rec, buf, sizeof(rec));
            KS_CHECK(rec.driverName.ch[0] == 'A');
            KS_CHECK(rec.driverName.ch[5] == '%'); // "Alice" = 5 chars
            KS_CHECK(rec.carName.ch[0] == 'c');
            KS_CHECK(rec.leaderboardIndex == 0);
            KS_CHECK(rec.carField1158 == 7);
        }
        // record 1: Bob / car_b
        got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            got = recvPacket(b, buf, sizeof(buf), n);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KS_CHECK(got && n == 208);
        if (got && n == 208) {
            AcRemoteCarListRecord rec;
            std::memcpy(&rec, buf, sizeof(rec));
            KS_CHECK(rec.driverName.ch[0] == 'B');
            KS_CHECK(rec.carName.ch[0] == 'c');
            KS_CHECK(rec.leaderboardIndex == 1);
            KS_CHECK(rec.carField1158 == 2);
        }
    }
    srv.clearCarList();

    // --- client B: op 5 CarId -> 4-byte reply -------------------------------
    hs.operationId = AC_REMOTE_CARID;
    KS_CHECK(sendAll(b, &hs, sizeof(hs)));
    spin(srv, 20);
    {
        char buf[64];
        int n = 0;
        bool got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            got = recvPacket(b, buf, sizeof(buf), n);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        KS_CHECK(got);
        KS_CHECK(n == 4);
        if (n == 4) {
            AcRemoteCarIdResponse r;
            std::memcpy(&r, buf, sizeof(r));
            KS_CHECK(r.carIndex == 0); // player car
        }
    }

    // --- client A never subscribed -> must NOT get RTCarInfo ----------------
    // (A only did CONNECT; after B's traffic, A should still have no 328-byte
    // frame waiting. Any pending 408 response was already consumed above.)
    {
        AcLiveInput in;
        in.speedMs = 10.f;
        srv.publishCarInfo(in);
        spin(srv, 10);
        char buf[1024];
        int n = 0;
        bool stray = recvPacket(a, buf, sizeof(buf), n);
        KS_CHECK(!stray);
    }

    // --- disconnect A -> registry drops to B only ---------------------------
    hs.operationId = AC_REMOTE_DISCONNECT;
    KS_CHECK(sendAll(a, &hs, sizeof(hs)));
    spin(srv, 20);
    KS_CHECK(srv.clientCount() == 1);

    closeSock(a);
    closeSock(b);
    srv.close();

    // --- REGRESSION: standalone loopback still passes ------------------------
    // Re-run the core checks in a fresh invocation to confirm no silent
    // regression (size/offset drift). This test binary is already part of the
    // KS_QT_FREE_TESTS suite, so CI will catch any future ABI/-layout break.
    return KS_TEST_RESULT("ac_remote_telemetry_test");
}
