/**
 * kslobby — hosted matchmaking lobby for ksengine / Matchmaking.h
 *
 * Standalone HTTP registry (no Qt, no external deps beyond OS sockets).
 *
 * Endpoints (any of these path prefixes work):
 *   GET  /servers | /api/v1/servers     → JSON array of live hosts
 *   POST /servers | /api/v1/servers     → register / refresh host
 *   DELETE /servers?host=&port=         → optional explicit deregister
 *   GET  /health | /api/v1/health       → {"ok":true,"servers":N}
 *
 * POST body (from Matchmaking::registerWithLobby):
 *   {"name","track","port","players","maxPlayers","sessionType","auth"}
 * Host IP is taken from the TCP peer if "host" is omitted (typical).
 *
 * Stale entries expire after --ttl seconds (default 30; clients re-POST every ~5s).
 *
 * Usage:
 *   kslobby [--port 8080] [--bind 0.0.0.0] [--ttl 30] [--max 512]
 * Client:
 *   networkManager.setLobbyBaseUrl("http://lobby.example.com:8080/api/v1");
 */
#include "LobbyWebEmbedded.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
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
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
using Sock = int;
constexpr Sock kInvalid = -1;
static void closeSock(Sock s) { if (s != kInvalid) ::close(s); }
#endif

namespace {

struct Config {
    std::string bind = "0.0.0.0";
    uint16_t port = 8080;
    double ttlSec = 30.0;
    size_t maxServers = 512;
};

struct Entry {
    std::string name;
    std::string host;
    std::string track;
    uint16_t port = 40000;
    int players = 0;
    int maxPlayers = 16;
    int sessionType = 0;
    bool auth = false;
    double lastSeen = 0.0; // steady_clock seconds
};

std::string keyOf(const std::string& host, uint16_t port)
{
    return host + ":" + std::to_string(port);
}

double nowSteady()
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

std::string escapeJson(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o.push_back('\\');
        if (c < 0x20) continue;
        o.push_back(static_cast<char>(c));
    }
    return o;
}

std::string extractJsonString(const std::string& obj, const char* key)
{
    const std::string pat = std::string("\"") + key + "\"";
    auto p = obj.find(pat);
    if (p == std::string::npos) return {};
    p = obj.find(':', p + pat.size());
    if (p == std::string::npos) return {};
    p = obj.find('"', p + 1);
    if (p == std::string::npos) return {};
    ++p;
    std::string out;
    while (p < obj.size() && obj[p] != '"') {
        if (obj[p] == '\\' && p + 1 < obj.size()) {
            out.push_back(obj[p + 1]);
            p += 2;
            continue;
        }
        out.push_back(obj[p++]);
    }
    return out;
}

int extractJsonInt(const std::string& obj, const char* key, int def)
{
    const std::string pat = std::string("\"") + key + "\"";
    auto p = obj.find(pat);
    if (p == std::string::npos) return def;
    p = obj.find(':', p + pat.size());
    if (p == std::string::npos) return def;
    ++p;
    while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) ++p;
    char* end = nullptr;
    long v = std::strtol(obj.c_str() + p, &end, 10);
    if (end == obj.c_str() + p) return def;
    return static_cast<int>(v);
}

bool extractJsonBool(const std::string& obj, const char* key, bool def)
{
    const std::string pat = std::string("\"") + key + "\"";
    auto p = obj.find(pat);
    if (p == std::string::npos) return def;
    p = obj.find(':', p + pat.size());
    if (p == std::string::npos) return def;
    ++p;
    while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) ++p;
    if (obj.compare(p, 4, "true") == 0) return true;
    if (obj.compare(p, 5, "false") == 0) return false;
    return def;
}

std::string peerIp(Sock sock)
{
    sockaddr_storage ss{};
#ifdef _WIN32
    int len = sizeof(ss);
#else
    socklen_t len = sizeof(ss);
#endif
    if (getpeername(sock, reinterpret_cast<sockaddr*>(&ss), &len) != 0) return "0.0.0.0";
    char buf[INET6_ADDRSTRLEN] = {};
    if (ss.ss_family == AF_INET) {
        auto* a = reinterpret_cast<sockaddr_in*>(&ss);
        inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
    } else if (ss.ss_family == AF_INET6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&ss);
        inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
    }
    return buf[0] ? std::string(buf) : "0.0.0.0";
}

bool pathIsServers(const std::string& path)
{
    // strip query
    std::string p = path;
    auto q = p.find('?');
    if (q != std::string::npos) p = p.substr(0, q);
    return p == "/servers" || p == "/api/v1/servers" || p == "/api/servers";
}

bool pathIsHealth(const std::string& path)
{
    std::string p = path;
    auto q = p.find('?');
    if (q != std::string::npos) p = p.substr(0, q);
    return p == "/health" || p == "/api/v1/health" || p == "/api/health";
}

bool pathIsWebUi(const std::string& path)
{
    std::string p = path;
    auto q = p.find('?');
    if (q != std::string::npos) p = p.substr(0, q);
    return p == "/" || p == "/index.html" || p == "/web" || p == "/web/" || p == "/browser";
}

std::string queryParam(const std::string& path, const char* key)
{
    auto q = path.find('?');
    if (q == std::string::npos) return {};
    std::string qs = path.substr(q + 1);
    const std::string k = std::string(key) + "=";
    size_t i = 0;
    while (i < qs.size()) {
        auto amp = qs.find('&', i);
        std::string part = qs.substr(i, amp == std::string::npos ? std::string::npos : amp - i);
        if (part.compare(0, k.size(), k) == 0) return part.substr(k.size());
        if (amp == std::string::npos) break;
        i = amp + 1;
    }
    return {};
}

class Registry {
public:
    explicit Registry(const Config& cfg) : m_cfg(cfg) {}

    void upsert(Entry e)
    {
        std::lock_guard<std::mutex> lock(m_mu);
        e.lastSeen = nowSteady();
        const std::string k = keyOf(e.host, e.port);
        auto it = m_map.find(k);
        if (it == m_map.end()) {
            if (m_map.size() >= m_cfg.maxServers) {
                // drop oldest
                auto oldest = m_map.begin();
                for (auto i = m_map.begin(); i != m_map.end(); ++i)
                    if (i->second.lastSeen < oldest->second.lastSeen) oldest = i;
                if (oldest != m_map.end()) m_map.erase(oldest);
            }
        }
        m_map[k] = std::move(e);
    }

    bool remove(const std::string& host, uint16_t port)
    {
        std::lock_guard<std::mutex> lock(m_mu);
        return m_map.erase(keyOf(host, port)) > 0;
    }

    std::vector<Entry> listLive()
    {
        std::lock_guard<std::mutex> lock(m_mu);
        const double now = nowSteady();
        std::vector<Entry> out;
        out.reserve(m_map.size());
        for (auto it = m_map.begin(); it != m_map.end();) {
            if (now - it->second.lastSeen > m_cfg.ttlSec) {
                it = m_map.erase(it);
            } else {
                out.push_back(it->second);
                ++it;
            }
        }
        return out;
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(m_mu);
        return m_map.size();
    }

private:
    Config m_cfg;
    std::mutex m_mu;
    std::unordered_map<std::string, Entry> m_map;
};

std::string jsonArray(const std::vector<Entry>& list)
{
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& e = list[i];
        if (i) os << ",";
        os << "{"
           << "\"name\":\"" << escapeJson(e.name) << "\","
           << "\"host\":\"" << escapeJson(e.host) << "\","
           << "\"track\":\"" << escapeJson(e.track) << "\","
           << "\"port\":" << e.port << ","
           << "\"players\":" << e.players << ","
           << "\"maxPlayers\":" << e.maxPlayers << ","
           << "\"sessionType\":" << e.sessionType << ","
           << "\"auth\":" << (e.auth ? "true" : "false")
           << "}";
    }
    os << "]";
    return os.str();
}

std::string httpResponse(int code, const char* status, const std::string& body,
                         const char* contentType = "application/json")
{
    std::ostringstream os;
    os << "HTTP/1.0 " << code << " " << status << "\r\n"
       << "Content-Type: " << contentType << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type\r\n"
       << "Connection: close\r\n"
       << "\r\n"
       << body;
    return os.str();
}

bool readRequest(Sock sock, std::string& method, std::string& path, std::string& body)
{
    std::string raw;
    char buf[2048];
    // Read until headers complete or buffer large
    for (;;) {
#ifdef _WIN32
        int n = recv(sock, buf, sizeof(buf), 0);
#else
        int n = (int)::recv(sock, buf, sizeof(buf), 0);
#endif
        if (n <= 0) break;
        raw.append(buf, buf + n);
        if (raw.find("\r\n\r\n") != std::string::npos) break;
        if (raw.size() > 64 * 1024) break;
    }
    if (raw.empty()) return false;

    auto lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos) return false;
    std::string reqLine = raw.substr(0, lineEnd);
    {
        std::istringstream is(reqLine);
        is >> method >> path;
    }
    if (method.empty() || path.empty()) return false;

    size_t contentLength = 0;
    auto cl = raw.find("Content-Length:");
    if (cl == std::string::npos) cl = raw.find("content-length:");
    if (cl != std::string::npos) {
        contentLength = static_cast<size_t>(std::strtoul(raw.c_str() + cl + 15, nullptr, 10));
    }

    auto hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return false;
    body = raw.substr(hdrEnd + 4);

    while (body.size() < contentLength && body.size() < 256 * 1024) {
#ifdef _WIN32
        int n = recv(sock, buf, sizeof(buf), 0);
#else
        int n = (int)::recv(sock, buf, sizeof(buf), 0);
#endif
        if (n <= 0) break;
        body.append(buf, buf + n);
    }
    if (contentLength > 0 && body.size() > contentLength)
        body.resize(contentLength);
    return true;
}

void handleClient(Sock sock, Registry& reg)
{
    std::string method, path, body;
    if (!readRequest(sock, method, path, body)) {
        closeSock(sock);
        return;
    }

    std::string response;

    if (method == "OPTIONS") {
        response = httpResponse(204, "No Content", "");
    } else if (pathIsWebUi(path) && method == "GET") {
        response = httpResponse(200, "OK", std::string(kslobby_web::kIndexHtml),
                                kslobby_web::kContentType);
    } else if (pathIsHealth(path) && method == "GET") {
        auto live = reg.listLive();
        std::ostringstream os;
        os << "{\"ok\":true,\"servers\":" << live.size() << "}";
        response = httpResponse(200, "OK", os.str());
    } else if (pathIsServers(path) && method == "GET") {
        response = httpResponse(200, "OK", jsonArray(reg.listLive()));
    } else if (pathIsServers(path) && method == "POST") {
        Entry e;
        e.name = extractJsonString(body, "name");
        e.host = extractJsonString(body, "host");
        e.track = extractJsonString(body, "track");
        e.port = static_cast<uint16_t>(extractJsonInt(body, "port", 40000));
        e.players = extractJsonInt(body, "players", 0);
        e.maxPlayers = extractJsonInt(body, "maxPlayers", 16);
        e.sessionType = extractJsonInt(body, "sessionType", 0);
        e.auth = extractJsonBool(body, "auth", false);
        if (e.host.empty() || e.host == "0.0.0.0" || e.host == "127.0.0.1") {
            // Prefer peer address for public listing (Matchmaking omits host).
            const std::string peer = peerIp(sock);
            if (!peer.empty() && peer != "127.0.0.1" && peer != "::1")
                e.host = peer;
            else if (e.host.empty())
                e.host = peer.empty() ? "0.0.0.0" : peer;
        }
        if (e.name.empty()) e.name = "ksim Server";
        reg.upsert(std::move(e));
        response = httpResponse(200, "OK", "{\"ok\":true}");
    } else if (pathIsServers(path) && method == "DELETE") {
        std::string host = queryParam(path, "host");
        std::string portStr = queryParam(path, "port");
        if (host.empty()) host = extractJsonString(body, "host");
        if (portStr.empty()) portStr = std::to_string(extractJsonInt(body, "port", 0));
        uint16_t port = static_cast<uint16_t>(std::atoi(portStr.c_str()));
        if (host.empty() || port == 0) {
            response = httpResponse(400, "Bad Request", "{\"ok\":false,\"error\":\"host and port required\"}");
        } else {
            const bool removed = reg.remove(host, port);
            response = httpResponse(removed ? 200 : 404, removed ? "OK" : "Not Found",
                                    removed ? "{\"ok\":true}" : "{\"ok\":false}");
        }
    } else {
        response = httpResponse(404, "Not Found",
                                "{\"ok\":false,\"error\":\"use GET/POST /servers or /api/v1/servers\"}");
    }

#ifdef _WIN32
    send(sock, response.c_str(), (int)response.size(), 0);
#else
    ::send(sock, response.c_str(), response.size(), 0);
#endif
    closeSock(sock);
}

void printUsage(const char* argv0)
{
    std::fprintf(stderr,
        "kslobby — ksengine hosted matchmaking lobby\n"
        "Usage: %s [--port N] [--bind ADDR] [--ttl SEC] [--max N]\n"
        "  --port  listen port (default 8080)\n"
        "  --bind  bind address (default 0.0.0.0)\n"
        "  --ttl   entry TTL seconds (default 30)\n"
        "  --max   max registered servers (default 512)\n"
        "Web UI:  http://HOST:PORT/  (or /index.html /web /browser)\n"
        "API:     setLobbyBaseUrl(\"http://HOST:PORT/api/v1\")\n",
        argv0);
}

} // namespace

int main(int argc, char** argv)
{
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> const char* {
            if (a == flag && i + 1 < argc) return argv[++i];
            return nullptr;
        };
        if (a == "-h" || a == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        if (const char* v = need("--port")) cfg.port = static_cast<uint16_t>(std::atoi(v));
        else if (const char* v = need("--bind")) cfg.bind = v;
        else if (const char* v = need("--ttl")) cfg.ttlSec = std::atof(v);
        else if (const char* v = need("--max")) cfg.maxServers = static_cast<size_t>(std::atoi(v));
        else {
            std::fprintf(stderr, "Unknown arg: %s\n", a.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }
    if (cfg.ttlSec < 5.0) cfg.ttlSec = 5.0;
    if (cfg.maxServers < 8) cfg.maxServers = 8;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    Sock listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock == kInvalid) {
        std::fprintf(stderr, "socket() failed\n");
        return 1;
    }
    int yes = 1;
#ifdef _WIN32
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
#else
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg.port);
    if (inet_pton(AF_INET, cfg.bind.c_str(), &addr.sin_addr) != 1) {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "bind(%s:%u) failed\n", cfg.bind.c_str(), (unsigned)cfg.port);
        closeSock(listenSock);
        return 1;
    }
    if (listen(listenSock, 64) != 0) {
        std::fprintf(stderr, "listen() failed\n");
        closeSock(listenSock);
        return 1;
    }

    Registry reg(cfg);
    std::fprintf(stdout,
        "kslobby listening on http://%s:%u\n"
        "  Web UI  http://%s:%u/  (browser client)\n"
        "  GET/POST/DELETE /api/v1/servers  |  GET /api/v1/health\n"
        "  TTL=%.0fs  max=%zu\n"
        "  setLobbyBaseUrl(\"http://<this-host>:%u/api/v1\")\n",
        cfg.bind.c_str(), (unsigned)cfg.port,
        cfg.bind.c_str(), (unsigned)cfg.port,
        cfg.ttlSec, cfg.maxServers, (unsigned)cfg.port);
    std::fflush(stdout);

    for (;;) {
        sockaddr_in client{};
#ifdef _WIN32
        int clen = sizeof(client);
#else
        socklen_t clen = sizeof(client);
#endif
        Sock cs = accept(listenSock, reinterpret_cast<sockaddr*>(&client), &clen);
        if (cs == kInvalid) continue;

        // Detach per-connection thread (low traffic registry; fine for lobby scale).
        std::thread([cs, &reg]() { handleClient(cs, reg); }).detach();
    }

    // unreachable
    closeSock(listenSock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
