#pragma once
/** Bidirectional control API (InSim-style) TCP default :20780 */
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cctype>
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

struct ControlCommand {
    std::string raw, verb;
    std::vector<std::string> args;
};

inline ControlCommand parseControlLine(const std::string& line) {
    ControlCommand c; c.raw = line;
    std::istringstream ss(line); ss >> c.verb;
    for (char& ch : c.verb) ch = (char)toupper((unsigned char)ch);
    std::string a; while (ss >> a) c.args.push_back(a);
    return c;
}

class ExternalControlApi {
public:
    static constexpr uint16_t kDefaultPort = 20780;

    /** Sprint 4 / P1.2 — optional shared token. Empty = open LAN (legacy). */
    void setAuthToken(const std::string& token) {
        m_token = token;
        m_authRequired = !token.empty();
    }
    void setAuthRequired(bool on) { m_authRequired = on; }
    bool isAuthenticated() const { return !m_authRequired || m_authenticated; }
    void clearAuth() { m_authenticated = false; }
    std::function<void(const ControlCommand&)> onCommand;
    std::function<void(const std::string&)> onClientConnected;
    std::function<void()> onClientDisconnected;

    bool start(uint16_t port = kDefaultPort) {
        if (m_listen >= 0) return true;
#ifdef _WIN32
        WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
        m_listen = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listen < 0) return false;
        BOOL yes = 1; setsockopt((SOCKET)m_listen, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        u_long nb = 1; ioctlsocket((SOCKET)m_listen, FIONBIO, &nb);
#else
        m_listen = (int)socket(AF_INET, SOCK_STREAM, 0);
        if (m_listen < 0) return false;
        int yes = 1; setsockopt(m_listen, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        int flags = fcntl(m_listen, F_GETFL, 0); fcntl(m_listen, F_SETFL, flags | O_NONBLOCK);
#endif
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port); addr.sin_addr.s_addr = INADDR_ANY;
#ifdef _WIN32
        if (bind((SOCKET)m_listen, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
        if (listen((SOCKET)m_listen, 4) != 0) return false;
#else
        if (bind(m_listen, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
        if (listen(m_listen, 4) != 0) return false;
#endif
        m_port = port;
        std::fprintf(stderr, "ExternalControlApi: listening on %u\n", (unsigned)port);
        return true;
    }
    void stop() {
        closeClient();
        if (m_listen >= 0) {
#ifdef _WIN32
            closesocket((SOCKET)m_listen);
#else
            close(m_listen);
#endif
            m_listen = -1;
        }
    }
    void poll() {
        acceptIfNeeded();
        if (m_client < 0) return;
        char buf[512];
#ifdef _WIN32
        int n = recv((SOCKET)m_client, buf, sizeof(buf) - 1, 0);
#else
        int n = (int)recv(m_client, buf, sizeof(buf) - 1, 0);
#endif
        if (n == 0) { closeClient(); return; }
        if (n < 0) return;
        buf[n] = 0; m_rx.append(buf, n);
        for (;;) {
            auto pos = m_rx.find('\n');
            if (pos == std::string::npos) break;
            std::string line = m_rx.substr(0, pos); m_rx.erase(0, pos + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;
            auto cmd = parseControlLine(line);
            if (cmd.verb == "PING") { sendLine("PONG"); continue; }
            if (cmd.verb == "AUTH") {
                const std::string provided = cmd.args.empty() ? "" : cmd.args[0];
                if (!m_authRequired || provided == m_token) {
                    m_authenticated = true;
                    sendLine("OK AUTH");
                } else {
                    m_authenticated = false;
                    sendLine("ERR AUTH");
                }
                continue;
            }
            if (m_authRequired && !m_authenticated) {
                sendLine("ERR UNAUTH — send AUTH <token>");
                continue;
            }
            if (onCommand) onCommand(cmd);
            else sendLine("ERR unknown handler");
        }
    }
    void sendLine(const std::string& line) {
        if (m_client < 0) return;
        std::string msg = line; if (msg.empty() || msg.back() != '\n') msg.push_back('\n');
#ifdef _WIN32
        send((SOCKET)m_client, msg.c_str(), (int)msg.size(), 0);
#else
        send(m_client, msg.c_str(), msg.size(), 0);
#endif
    }
    void emitEvent(const std::string& evt) { sendLine(std::string("EVT ") + evt); }
    bool hasClient() const { return m_client >= 0; }
    uint16_t port() const { return m_port; }
private:
    void acceptIfNeeded() {
        if (m_listen < 0 || m_client >= 0) return;
        sockaddr_in from{};
#ifdef _WIN32
        int flen = sizeof(from);
        int c = (int)accept((SOCKET)m_listen, (sockaddr*)&from, &flen);
        if (c < 0) return;
        u_long nb = 1; ioctlsocket((SOCKET)c, FIONBIO, &nb);
#else
        socklen_t flen = sizeof(from);
        int c = (int)accept(m_listen, (sockaddr*)&from, &flen);
        if (c < 0) return;
        int flags = fcntl(c, F_GETFL, 0); fcntl(c, F_SETFL, flags | O_NONBLOCK);
#endif
        m_client = c;
        m_authenticated = !m_authRequired;
        if (m_authRequired)
            sendLine("OK ksim control API v1 AUTH_REQUIRED");
        else
            sendLine("OK ksim control API v1");
        if (onClientConnected) {
            char ip[64];
#ifdef _WIN32
            InetNtopA(AF_INET, &from.sin_addr, ip, sizeof(ip));
#else
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
#endif
            onClientConnected(ip);
        }
    }
    void closeClient() {
        if (m_client < 0) return;
#ifdef _WIN32
        closesocket((SOCKET)m_client);
#else
        close(m_client);
#endif
        m_client = -1; m_rx.clear(); m_authenticated = false;
        if (onClientDisconnected) onClientDisconnected();
    }
    int m_listen = -1, m_client = -1;
    uint16_t m_port = kDefaultPort;
    std::string m_rx;
    std::string m_token;
    bool m_authRequired = false;
    bool m_authenticated = false;
};

} // namespace sim
} // namespace ks
