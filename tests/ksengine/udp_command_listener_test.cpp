/**
 * AC UDP command listener (UDP :9666) — parity test (P3).
 *
 * Proves the ksengine listener mirrors acs.exe's `UDPCommandListener`
 * (reverse-engineered via REA/Ghidra: socket(AF_INET, SOCK_DGRAM, 0),
 * bind(INADDR_ANY, htons(0x25c2)), non-blocking pump) and dispatches what
 * arrives:
 *
 *   1. constants — default port 9666 (0x25c2), command range 'B'..'Q';
 *   2. bind      — start() on an ephemeral port resolves the real port;
 *   3. command   — a datagram whose first byte is in AC's switch range
 *                  ('B'..'Q') fires onCommand(cmd, payload, ...) AND
 *                  onDatagram (raw); replyTo() delivers an ack back to the
 *                  sender's endpoint;
 *   4. unknown   — a first byte outside 'B'..'Q' fires ONLY onDatagram
 *                  (acs.exe switch default: silently ignored);
 *   5. endpoints — sender ip/port reported are the client's, not ours.
 *
 * Loopback only: listener on an ephemeral port, a plain UDP client drives it.
 * No external content or timing dependencies.
 */
#include "KsTest.h"
#include "adapters/assetto_corsa/UdpCommandListener.h"

#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#endif

using ks::ac::UdpCommandListener;

namespace {

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

void closeSock(int s) {
    if (s < 0) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(s));
#else
    ::close(s);
#endif
}

// A UDP client bound to an ephemeral local port on loopback.
int makeClient(uint16_t& outLocalPort) {
    int s = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (s < 0) return -1;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;
    if (::bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        closeSock(s);
        return -1;
    }
    sockaddr_in got{};
#ifdef _WIN32
    int glen = sizeof(got);
#else
    socklen_t glen = sizeof(got);
#endif
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&got), &glen) != 0) {
        closeSock(s);
        return -1;
    }
    outLocalPort = ntohs(got.sin_port);
    return s;
}

void sendTo(int s, uint16_t port, const void* data, size_t n) {
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::sendto(s, static_cast<const char*>(data), static_cast<int>(n), 0,
             reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
}

void sleepMs(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

// Receive with a short bounded wait (loopback is immediate; keep it tight).
int recvWait(int s, char* buf, int len, int tries = 200) {
    for (int i = 0; i < tries; ++i) {
        int n = ::recv(s, buf, len, 0);
        if (n > 0) return n;
        sleepMs(1);
    }
    return -1;
}

// Pump the listener for `ms` milliseconds so slow deliveries still land.
void pumpFor(UdpCommandListener& l, int ms) {
    for (int i = 0; i < ms; ++i) {
        l.pump();
        sleepMs(1);
    }
}

} // namespace

int main() {
    WsaGuard g;

    // 1. Default port matches acs.exe htons(0x25c2); command range matches
    //    the switch in FUN_1401d1570 ('B'..'Q').
    KS_CHECK(UdpCommandListener::kDefaultPort == 9666);
    KS_CHECK(UdpCommandListener::kDefaultPort == 0x25c2);
    KS_CHECK(UdpCommandListener::kCmdFirst == 'B');
    KS_CHECK(UdpCommandListener::kCmdLast == 'Q');

    // 2. bind + ephemeral port resolution + stop.
    UdpCommandListener l;
    KS_CHECK(l.start(0));
    KS_CHECK(l.isRunning());
    KS_CHECK(l.port() != 0);
    const uint16_t lport = l.port();

    // 3. Known command byte: dispatch to onCommand(cmd, payload) +
    //    onDatagram (raw), and replyTo() reaches the sender.
    char seenCmd = 0;
    std::string seenPayload;
    uint32_t seenIp = 0;
    uint16_t seenPort = 0;
    std::vector<std::string> raws;
    l.onCommand = [&](char cmd, const char* payload, size_t n, uint32_t ip,
                      uint16_t p) {
        seenCmd = cmd;
        seenPayload.assign(payload, n);
        seenIp = ip;
        seenPort = p;
        const std::string ack = "OK";
        l.replyTo(ip, p, ack.data(), ack.size());
    };
    l.onDatagram = [&](const char* d, size_t n, uint32_t, uint16_t) {
        raws.emplace_back(d, n);
    };

    uint16_t cport = 0;
    int c = makeClient(cport);
    KS_CHECK(c >= 0);
    if (c < 0) return KS_TEST_RESULT("udp_command_listener_test");

    const char payload[] = {'K', 'P', 'I', 'T'}; // command byte + payload
    sendTo(c, lport, payload, sizeof(payload));
    pumpFor(l, 50);

    KS_CHECK(raws.size() == 1);
    KS_CHECK(seenCmd == 'K');
    KS_CHECK(seenPayload == "PIT"); // first byte consumed as the command
    KS_CHECK(seenIp == 0x7f000001u); // sender is loopback, not the listener
    KS_CHECK(seenPort == cport);

    char reply[64] = {};
    int rn = recvWait(c, reply, sizeof(reply));
    KS_CHECK(rn > 0);
    if (rn > 0) KS_CHECK(std::string(reply, reply + rn) == "OK");

    // 4. First byte outside 'B'..'Q': only onDatagram fires (AC's switch
    //    default silently ignores it).
    seenCmd = 0;
    seenPayload.clear();
    raws.clear();
    const unsigned char unk[] = {'T', 'I', 'M', 'E'};
    sendTo(c, lport, unk, sizeof(unk));
    pumpFor(l, 50);
    KS_CHECK(raws.size() == 1);
    KS_CHECK(seenCmd == 0);
    KS_CHECK(seenPayload.empty());
    if (!raws.empty()) KS_CHECK(raws[0].size() == sizeof(unk));

    closeSock(c);
    l.stop();
    KS_CHECK(!l.isRunning());

    return KS_TEST_RESULT("udp_command_listener_test");
}
