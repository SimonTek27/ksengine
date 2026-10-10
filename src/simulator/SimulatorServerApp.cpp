/**
 * SimulatorServerApp — headless host (hardened)
 *   FeatureHub: discovery :20779, control :20780
 *   Optional game net: --game-port (HAS_KSNET)
 *   Defaults: server/kssimserver.ini, every flag overrides one of its keys.
 */
#include "SimulationLoop.h"
#include "FeatureHub.h"
#include "NetworkManager.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <cctype>
#include <algorithm>

// NB: the public include path contains src/engine/sys, whose Signal.h
// shadows the CRT's <signal.h> on a case-insensitive FS (same hazard already
// documented for vendored Lua in src/engine/CMakeLists.txt and handled in
// tools/ks_server/main.cpp). Use the Win32 console handler on Windows and
// <csignal> everywhere else.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#endif

namespace {
std::atomic<bool> g_run{true};
#ifdef _WIN32
BOOL WINAPI onConsoleCtrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        g_run.store(false);
        return TRUE;
    }
    return FALSE;
}
#else
void onSig(int) { g_run.store(false); }
#endif

bool validPort(int p) { return p > 0 && p <= 65535; }

std::string sanitizeName(const std::string& in) {
    std::string out;
    out.reserve(std::min(in.size(), size_t(64)));
    for (unsigned char c : in) {
        if (out.size() >= 64) break;
        if (std::isalnum(c) || c == '-' || c == '_' || c == ' ' || c == '.')
            out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.empty()) out = "ksengine-server";
    return out;
}

bool safePathArg(const std::string& p) {
    if (p.empty() || p.size() > 4096) return false;
    if (p.find('\0') != std::string::npos) return false;
    std::string n = p;
    for (char& c : n) if (c == '\\') c = '/';
    if (n.find("/../") != std::string::npos || n.rfind("../", 0) == 0 || n == "..")
        return false;
    return true;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lowerAscii(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

bool parseIntValue(const std::string& v, int& out) {
    if (v.empty()) return false;
    size_t used = 0;
    try {
        int n = std::stoi(v, &used);
        if (used != v.size()) return false;
        out = n;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseBoolValue(const std::string& v, bool& out) {
    const std::string s = lowerAscii(v);
    if (s == "1" || s == "true" || s == "yes" || s == "on")  { out = true;  return true; }
    if (s == "0" || s == "false" || s == "no" || s == "off") { out = false; return true; }
    return false;
}

/**
 * Startup configuration, read from server/kssimserver.ini (the file shipped
 * in the `server/` folder of the installed tree, see
 * cmake/KsInstallLayout.cmake). Every key has a command line equivalent and
 * the command line always wins.
 */
struct ServerConfig {
    std::string name      = "ksengine-server";
    bool announce         = false;
    std::string track;                       // empty = start without a track
    int ai                = 0;               // 0..32
    int gamePort          = 0;               // 0 = game network off
    int maxClients        = 24;              // 1..64
    int discoveryPort     = 20779;           // UDP, ks::sim::kDiscoveryPort
    int controlPort       = 20780;           // TCP, ExternalControlApi::kDefaultPort
};

/**
 * Fills `cfg` from `path`. A missing file is not an error (a build-tree run
 * is never staged): the defaults simply stay. Returns false only for a file
 * that exists and cannot be parsed, with the reason in `err`.
 */
bool loadServerConfig(const std::string& path, ServerConfig& cfg, std::string& err) {
    std::ifstream in(path);
    if (!in) return true;

    auto fail = [&err, &path](int lineNo, const std::string& what) {
        err = path + ":" + std::to_string(lineNo) + ": " + what;
        return false;
    };

    std::string raw;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            return fail(lineNo, "expected 'key = value'");

        const std::string key   = lowerAscii(trim(line.substr(0, eq)));
        const std::string value = trim(line.substr(eq + 1));

        int   iv = 0;
        bool  bv = false;
        if (key == "name") {
            cfg.name = value;
        } else if (key == "announce") {
            if (!parseBoolValue(value, bv)) return fail(lineNo, "announce: want true/false");
            cfg.announce = bv;
        } else if (key == "track") {
            if (!value.empty() && !safePathArg(value))
                return fail(lineNo, "track: rejected unsafe path");
            cfg.track = value;
        } else if (key == "ai") {
            if (!parseIntValue(value, iv)) return fail(lineNo, "ai: want a number");
            cfg.ai = std::clamp(iv, 0, 32);
        } else if (key == "game-port") {
            if (!parseIntValue(value, iv)) return fail(lineNo, "game-port: want a number");
            cfg.gamePort = iv;
        } else if (key == "max-clients") {
            if (!parseIntValue(value, iv)) return fail(lineNo, "max-clients: want a number");
            cfg.maxClients = std::clamp(iv, 1, 64);
        } else if (key == "discovery-port") {
            if (!parseIntValue(value, iv)) return fail(lineNo, "discovery-port: want a number");
            cfg.discoveryPort = iv;
        } else if (key == "control-port") {
            if (!parseIntValue(value, iv)) return fail(lineNo, "control-port: want a number");
            cfg.controlPort = iv;
        } else {
            std::fprintf(stderr, "SimulatorServer: %s:%d: ignoring unknown key '%s'\n",
                         path.c_str(), lineNo, key.c_str());
        }
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    // 1. --config is pre-scanned: the file supplies the defaults that every
    //    other flag then overrides, so it has to be read before them.
    std::string configPath = "server/kssimserver.ini";
    for (int i = 1; i < argc; ++i) {
        if (!argv[i] || std::strcmp(argv[i], "--config") != 0) continue;
        if (i + 1 >= argc || !argv[i + 1]) {
            std::fprintf(stderr, "SimulatorServer: --config needs a path\n");
            return 2;
        }
        configPath = argv[++i];
    }

    ServerConfig cfg;
    std::string cfgErr;
    if (!loadServerConfig(configPath, cfg, cfgErr)) {
        std::fprintf(stderr, "SimulatorServer: %s\n", cfgErr.c_str());
        return 2;
    }
    if (std::ifstream probe(configPath); probe.good())
        std::fprintf(stderr, "SimulatorServer: config %s\n", configPath.c_str());
    else
        std::fprintf(stderr, "SimulatorServer: no config at %s, using defaults\n",
                     configPath.c_str());

    bool announce = cfg.announce;
    int ai = cfg.ai;
    int gamePort = cfg.gamePort;
    int maxClients = cfg.maxClients;
    int discoveryPort = cfg.discoveryPort;
    int controlPort = cfg.controlPort;
    std::string trackDir = cfg.track;
    std::string hostName = cfg.name;

    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        if (!std::strcmp(argv[i], "--config")) {
            ++i; // value consumed by the pre-scan above
        }
        else if (!std::strcmp(argv[i], "--announce")) announce = true;
        else if (!std::strcmp(argv[i], "--ai") && i + 1 < argc) {
            ai = std::atoi(argv[++i]);
            ai = std::clamp(ai, 0, 32);
        }
        else if (!std::strcmp(argv[i], "--track") && i + 1 < argc) {
            std::string t = argv[++i];
            if (!safePathArg(t)) {
                std::fprintf(stderr, "SimulatorServer: rejected unsafe --track path\n");
                return 2;
            }
            trackDir = t;
        }
        else if (!std::strcmp(argv[i], "--name") && i + 1 < argc)
            hostName = sanitizeName(argv[++i]);
        else if (!std::strcmp(argv[i], "--game-port") && i + 1 < argc) {
            gamePort = std::atoi(argv[++i]);
            if (!validPort(gamePort) || gamePort < 1024) {
                std::fprintf(stderr, "SimulatorServer: invalid --game-port %d (need 1024-65535)\n", gamePort);
                return 2;
            }
        }
        else if (!std::strcmp(argv[i], "--max-clients") && i + 1 < argc)
            maxClients = std::clamp(std::atoi(argv[++i]), 1, 64);
        else if (!std::strcmp(argv[i], "--discovery-port") && i + 1 < argc)
            discoveryPort = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--control-port") && i + 1 < argc)
            controlPort = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--help")) {
            std::fprintf(stderr,
                "SimulatorServer [--config FILE] [--announce] [--track DIR] [--ai N]\n"
                "                [--name NAME] [--game-port PORT] [--max-clients N]\n"
                "                [--discovery-port PORT] [--control-port PORT]\n"
                "  discovery UDP %d  control TCP %d  game net optional (PORT>=1024)\n"
                "  defaults come from FILE (default server/kssimserver.ini);\n"
                "  flags above override it\n", ks::sim::kDiscoveryPort,
                ks::sim::ExternalControlApi::kDefaultPort);
            return 0;
        }
        else {
            std::fprintf(stderr, "SimulatorServer: unknown arg '%s' (use --help)\n", argv[i]);
            return 2;
        }
    }

    hostName = sanitizeName(hostName);

    // Values that can arrive from the config file get the same validation the
    // flags above already enforce.
    if (gamePort != 0 && (!validPort(gamePort) || gamePort < 1024)) {
        std::fprintf(stderr, "SimulatorServer: invalid game-port %d (need 1024-65535, 0 disables)\n",
                     gamePort);
        return 2;
    }
    if (!validPort(discoveryPort)) {
        std::fprintf(stderr, "SimulatorServer: invalid discovery-port %d\n", discoveryPort);
        return 2;
    }
    if (!validPort(controlPort)) {
        std::fprintf(stderr, "SimulatorServer: invalid control-port %d\n", controlPort);
        return 2;
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
#else
    std::signal(SIGINT, onSig);
    std::signal(SIGTERM, onSig);
    std::signal(SIGPIPE, SIG_IGN);
#endif

    ks::sim::SimulationLoop loop;
    if (!loop.initialize()) {
        std::fprintf(stderr, "SimulatorServer: initialize failed\n");
        return 1;
    }
    // Ports first: startFeatureServices()/announceHost() below are what bind
    // the sockets, and server/kssimserver.ini can move them.
    loop.features().setDiscoveryPort(static_cast<uint16_t>(discoveryPort));
    loop.features().setControlPort(static_cast<uint16_t>(controlPort));
    if (!trackDir.empty()) {
        if (!loop.loadTrackFolder(trackDir))
            std::fprintf(stderr, "SimulatorServer: warning: track load failed for '%s'\n", trackDir.c_str());
    }

    loop.features().setSessionMode(ks::sim::GameSessionMode::Practice);
    loop.startFeatureServices(announce);
    if (announce)
        loop.features().announceHost(hostName, trackDir.empty() ? "unknown" : trackDir,
                                     static_cast<uint16_t>(gamePort > 0 ? gamePort : 9600),
                                     1, maxClients);

    if (ai > 0)
        loop.setAiCarCount(ai);

    ks::sim::NetworkManager net(&loop);
    if (gamePort > 0) {
        if (net.hostServer(static_cast<uint16_t>(gamePort), maxClients, hostName, trackDir))
            std::fprintf(stderr, "SimulatorServer: game host on port %d\n", gamePort);
        else
            std::fprintf(stderr, "SimulatorServer: game host unavailable (build without HAS_KSNET?)\n");
    }

    // Our SimulationLoop opens the session from start(): it resets the
    // session state, staggers the AI grid (m_aiCarCount) and configures
    // RaceSessionManager. The incoming branch had a separate beginSession().
    loop.start();

    std::fprintf(stderr, "SimulatorServer: running (disc :%d, ctrl :%d%s)\n",
                 discoveryPort, controlPort,
                 gamePort > 0 ? ", game net on" : "");
    auto last = std::chrono::steady_clock::now();
    while (g_run.load()) {
        try {
            loop.tick();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "SimulatorServer: tick exception: %s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "SimulatorServer: tick unknown exception\n");
        }
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (dt < 1.0 / 120.0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (dt > 1.0)
            std::fprintf(stderr, "SimulatorServer: slow frame %.3fs\n", dt);
    }
    if (net.isHosting())
        net.stopServer();
    loop.stop();
    loop.features().stopServices();
    std::fprintf(stderr, "SimulatorServer: stopped\n");
    return 0;
}
