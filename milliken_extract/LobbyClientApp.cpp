/**
 * kslobby-cli — client for the ksengine hosted matchmaking lobby (kslobby).
 *
 * Commands:
 *   kslobby-cli list   [--url URL]
 *   kslobby-cli health [--url URL]
 *   kslobby-cli register --name N --track T --port P [--players N] [--max M]
 *                        [--session S] [--auth] [--host H] [--url URL]
 *   kslobby-cli delete --port P [--host H] [--url URL]
 *
 * Default URL: env KS_LOBBY_URL, else http://127.0.0.1:8080/api/v1
 * Compatible with LobbyServerApp (GET/POST/DELETE /servers).
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
using Sock = SOCKET;
constexpr Sock kInvalid = INVALID_SOCKET;
static void closeSock(Sock s) { if (s != kInvalid) closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
using Sock = int;
constexpr Sock kInvalid = -1;
static void closeSock(Sock s) { if (s != kInvalid) ::close(s); }
#endif

namespace {

std::string defaultUrl()
{
    if (const char* e = std::getenv("KS_LOBBY_URL"); e && e[0]) return e;
    return "http://127.0.0.1:8080/api/v1";
}

std::string escapeJson(const std::string& s)
{
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o.push_back('\\');
        if (c < 0x20) continue;
        o.push_back(static_cast<char>(c));
    }
    return o;
}

bool httpRequest(const std::string& url, const std::string& method,
                 const std::string& body, std::string& outResponse, int* outCode = nullptr)
{
    if (url.size() < 8 || url.substr(0, 7) != "http://") {
        std::fprintf(stderr, "Only http:// URLs supported (got: %s)\n", url.c_str());
        return false;
    }
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
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%u", port);
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) {
        std::fprintf(stderr, "DNS/resolve failed for %s:%u\n", host.c_str(), port);
        return false;
    }
    Sock sock = (Sock)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == kInvalid) {
        freeaddrinfo(res);
        return false;
    }
#ifdef _WIN32
    DWORD tv = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
#else
    timeval tv{5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
        std::fprintf(stderr, "connect failed to %s:%u\n", host.c_str(), port);
        closeSock(sock);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.0\r\n"
        << "Host: " << host << "\r\n"
        << "User-Agent: kslobby-cli/1.0\r\n"
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
        if (outResponse.size() > 512 * 1024) break;
    }
    closeSock(sock);

    if (outCode) {
        *outCode = 0;
        if (outResponse.size() >= 12 && outResponse.compare(0, 5, "HTTP/") == 0) {
            auto sp = outResponse.find(' ');
            if (sp != std::string::npos)
                *outCode = std::atoi(outResponse.c_str() + sp + 1);
        }
    }
    return !outResponse.empty();
}

std::string bodyOnly(const std::string& resp)
{
    auto pos = resp.find("\r\n\r\n");
    if (pos == std::string::npos) return resp;
    return resp.substr(pos + 4);
}

void printUsage(const char* argv0)
{
    std::fprintf(stderr,
        "kslobby-cli — client for kslobby matchmaking registry\n"
        "Usage:\n"
        "  %s list     [--url URL]\n"
        "  %s health   [--url URL]\n"
        "  %s register --name N --track T --port P [options]\n"
        "  %s delete   --port P [--host H] [--url URL]\n"
        "Options:\n"
        "  --url URL       base URL (default: $KS_LOBBY_URL or http://127.0.0.1:8080/api/v1)\n"
        "  --name NAME     server display name\n"
        "  --track TRACK   track name\n"
        "  --port N        game port (required for register/delete)\n"
        "  --host H        public host IP (optional; lobby uses peer IP if omitted)\n"
        "  --players N     current players (default 0)\n"
        "  --max N         max players (default 16)\n"
        "  --session N     session type int (default 0)\n"
        "  --auth          mark auth required\n",
        argv0, argv0, argv0, argv0);
}

std::string joinUrl(const std::string& base, const char* suffix)
{
    if (base.empty()) return suffix;
    if (base.back() == '/') return base.substr(0, base.size() - 1) + suffix;
    return base + suffix;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    const std::string cmd = argv[1];
    std::string url = defaultUrl();
    std::string name = "ksim Server";
    std::string track = "Unknown";
    std::string host;
    uint16_t port = 0;
    int players = 0, maxPlayers = 16, sessionType = 0;
    bool auth = false;

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> const char* {
            if (a == flag && i + 1 < argc) return argv[++i];
            return nullptr;
        };
        if (const char* v = need("--url")) url = v;
        else if (const char* v = need("--name")) name = v;
        else if (const char* v = need("--track")) track = v;
        else if (const char* v = need("--host")) host = v;
        else if (const char* v = need("--port")) port = static_cast<uint16_t>(std::atoi(v));
        else if (const char* v = need("--players")) players = std::atoi(v);
        else if (const char* v = need("--max")) maxPlayers = std::atoi(v);
        else if (const char* v = need("--session")) sessionType = std::atoi(v);
        else if (a == "--auth") auth = true;
        else if (a == "-h" || a == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "Unknown arg: %s\n", a.c_str());
            return 1;
        }
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    std::string resp;
    int code = 0;

    if (cmd == "list") {
        if (!httpRequest(joinUrl(url, "/servers"), "GET", "", resp, &code)) return 2;
        std::printf("%s\n", bodyOnly(resp).c_str());
        return (code >= 200 && code < 300) ? 0 : 3;
    }
    if (cmd == "health") {
        if (!httpRequest(joinUrl(url, "/health"), "GET", "", resp, &code)) return 2;
        std::printf("%s\n", bodyOnly(resp).c_str());
        return (code >= 200 && code < 300) ? 0 : 3;
    }
    if (cmd == "register") {
        if (port == 0) {
            std::fprintf(stderr, "register requires --port\n");
            return 1;
        }
        std::ostringstream json;
        json << "{"
             << "\"name\":\"" << escapeJson(name) << "\","
             << "\"track\":\"" << escapeJson(track) << "\",";
        if (!host.empty())
            json << "\"host\":\"" << escapeJson(host) << "\",";
        json << "\"port\":" << port << ","
             << "\"players\":" << players << ","
             << "\"maxPlayers\":" << maxPlayers << ","
             << "\"sessionType\":" << sessionType << ","
             << "\"auth\":" << (auth ? "true" : "false")
             << "}";
        if (!httpRequest(joinUrl(url, "/servers"), "POST", json.str(), resp, &code)) return 2;
        std::printf("%s\n", bodyOnly(resp).c_str());
        return (code >= 200 && code < 300) ? 0 : 3;
    }
    if (cmd == "delete") {
        if (port == 0) {
            std::fprintf(stderr, "delete requires --port\n");
            return 1;
        }
        std::ostringstream path;
        path << joinUrl(url, "/servers") << "?port=" << port;
        if (!host.empty()) path << "&host=" << host;
        if (!httpRequest(path.str(), "DELETE", "", resp, &code)) return 2;
        std::printf("%s\n", bodyOnly(resp).c_str());
        return (code >= 200 && code < 300) ? 0 : 3;
    }

    std::fprintf(stderr, "Unknown command: %s\n", cmd.c_str());
    printUsage(argv[0]);
    return 1;
}
