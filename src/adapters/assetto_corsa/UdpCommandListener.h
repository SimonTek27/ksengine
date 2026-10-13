#pragma once
/**
 * AC UDP command listener (UDP :9666) — header-only.
 *
 * Wire-compatible with acs.exe's `UDPCommandListener` (RTTI
 * `.?AVUDPCommandListener@@`), reverse-engineered via REA/Ghidra:
 *
 *   FUN_1401d0f30 (constructor):
 *     socket(AF_INET, SOCK_DGRAM, 0)
 *     bind(INADDR_ANY, htons(0x25c2))    // 9666
 *     // on failure: "ERROR: BINDING LISTENER SOCKET UdpCommandListener"
 *     // socket handle stored at this+0x60, name "UDPCOMMAND_LISTENER"
 *
 * Separate from remote telemetry (:9996): external tools (Content Manager,
 * apps) send small UDP datagrams here to drive the running game. ksengine
 * previously had NOTHING on 9666, so those tools could not talk to kssim.
 *
 * Command wire format (FUN_1401d1570, the pump — REA round 7):
 *
 *   recvfrom(sock, buf, 0xfbc, 0, &from, ...)   // 4028 B, one datagram
 *   switch (buf[0]) {                          // FIRST BYTE = command
 *     case 'B': ... case 'Q': ...              // 16 known commands
 *     default:  -- ignored, no reply           // sender address unused
 *   }
 *
 * AC never replies and ignores the sender endpoint (fire-and-forget); the
 * pump makes the socket non-blocking (ioctlsocket FIONBIO) each call and
 * prints "Socket error UDP Command listener %d" on recv errors.
 *
 * ksengine exposes every datagram via `onDatagram` (raw bytes + sender) and,
 * when the first byte is one of AC's known commands 'B'..'Q', via
 * `onCommand` (command char + remaining payload) — consumers decide the
 * semantics (FeatureHub). Unknown first bytes are silently ignored, exactly
 * like acs.exe's switch default. Unlike AC's one-recv-per-frame, pump() drains
 * a bounded burst so a burst of commands cannot overflow the socket buffer.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

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

/**
 * UDP command listener bound on a single port (default 9666, as AC).
 * Non-blocking; pump() from the sim thread.
 */
class UdpCommandListener {
public:
    static constexpr uint16_t kDefaultPort = 9666; // acs.exe: htons(0x25c2)

    /** First/last command byte handled by acs.exe's switch (FUN_1401d1570). */
    static constexpr char kCmdFirst = 'B';
    static constexpr char kCmdLast = 'Q';

    /** Raw datagram callback: payload bytes + sender endpoint. */
    std::function<void(const char* data, size_t len,
                       uint32_t senderIp, uint16_t senderPort)> onDatagram;
    /**
     * Command callback: fires only for AC's known command bytes 'B'..'Q'.
     * `cmd` is the first payload byte, `payload`/`len` are the rest
     * (len may be 0). AC ignores the sender; ksengine reports it so
     * consumers may reply if they choose (parity keeps AC fire-and-forget).
     */
    std::function<void(char cmd, const char* payload, size_t len,
                       uint32_t senderIp, uint16_t senderPort)> onCommand;

    UdpCommandListener() = default;
    ~UdpCommandListener() { stop(); }

    bool start(uint16_t port = kDefaultPort) {
        stop();
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
            // Same message acs.exe prints on bind failure.
            std::fprintf(stderr,
                         "ERROR: BINDING LISTENER SOCKET UdpCommandListener\n");
            stop();
            return false;
        }
        m_port = port;
        // Resolves ephemeral port 0 to the port the OS actually gave us.
        sockaddr_in bound{};
        socklen_t blen = sizeof(bound);
        if (::getsockname(m_sock, reinterpret_cast<sockaddr*>(&bound), &blen) == 0)
            m_port = ntohs(bound.sin_port);
        m_ok = true;
        std::fprintf(stderr, "UdpCommandListener: UDP :%u\n", unsigned(m_port));
        return true;
    }

    void stop() {
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

    bool isRunning() const { return m_ok; }
    uint16_t port() const { return m_port; }

    /** Drain pending datagrams (bounded burst per call; AC does one/frame). */
    void pump() {
        if (!m_ok || m_sock < 0) return;
        for (int iter = 0; iter < 64; ++iter) {
            char buf[0xfbc]; // acs.exe recvfrom length: 4028 bytes
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
            if (n <= 0) break;
            handle(buf, static_cast<size_t>(n), from);
        }
    }

    /** Send a small text/binary reply back to a command's sender. */
    void replyTo(uint32_t senderIp, uint16_t senderPort, const void* data, size_t n) {
        if (!m_ok || m_sock < 0 || !data || n == 0) return;
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(senderPort);
        dst.sin_addr.s_addr = htonl(senderIp);
#ifdef _WIN32
        ::sendto(m_sock, static_cast<const char*>(data), static_cast<int>(n), 0,
                 reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
#else
        ::sendto(m_sock, data, n, 0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
#endif
    }

private:
    void handle(const char* buf, size_t n, const sockaddr_in& from) {
        const uint32_t ip = ntohl(from.sin_addr.s_addr);
        const uint16_t sport = ntohs(from.sin_port);
        if (onDatagram) onDatagram(buf, n, ip, sport);
        if (!onCommand || n == 0) return;
        // acs.exe: switch(payload[0]) over 'B'..'Q', everything else ignored.
        const char cmd = buf[0];
        if (cmd < kCmdFirst || cmd > kCmdLast) return;
        onCommand(cmd, buf + 1, n - 1, ip, sport);
    }

    bool m_ok = false;
    bool m_wsa = false;
    int m_sock = -1;
    uint16_t m_port = kDefaultPort;
};

} // namespace ac
} // namespace ks
