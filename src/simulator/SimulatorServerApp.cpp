/**
 * SimulatorServerApp — headless host (hardened)
 *   FeatureHub: discovery :20779, control :20780
 *   Optional game net: --game-port (HAS_KSNET)
 */
#include "SimulationLoop.h"
#include "FeatureHub.h"
#include "NetworkManager.h"
#include <cstdio>
#include <cstring>
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
} // namespace

int main(int argc, char** argv) {
    bool announce = false;
    int ai = 0;
    int gamePort = 0;
    std::string trackDir;
    std::string hostName = "ksengine-server";

    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        if (!std::strcmp(argv[i], "--announce")) announce = true;
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
        else if (!std::strcmp(argv[i], "--help")) {
            std::fprintf(stderr,
                "SimulatorServer [--announce] [--track DIR] [--ai N] [--name NAME] [--game-port PORT]\n"
                "  discovery UDP :20779  control TCP :20780  game net optional (PORT>=1024)\n");
            return 0;
        }
        else {
            std::fprintf(stderr, "SimulatorServer: unknown arg '%s' (use --help)\n", argv[i]);
            return 2;
        }
    }

    hostName = sanitizeName(hostName);

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
    if (!trackDir.empty()) {
        if (!loop.loadTrackFolder(trackDir))
            std::fprintf(stderr, "SimulatorServer: warning: track load failed for '%s'\n", trackDir.c_str());
    }

    loop.features().setSessionMode(ks::sim::GameSessionMode::Practice);
    loop.startFeatureServices(announce);
    if (announce)
        loop.features().announceHost(hostName, trackDir.empty() ? "unknown" : trackDir, 9600, 1, 24);

    if (ai > 0)
        loop.setAiCarCount(ai);

    ks::sim::NetworkManager net(&loop);
    if (gamePort > 0) {
        if (net.hostServer(static_cast<uint16_t>(gamePort), 24, hostName, trackDir))
            std::fprintf(stderr, "SimulatorServer: game host on port %d\n", gamePort);
        else
            std::fprintf(stderr, "SimulatorServer: game host unavailable (build without HAS_KSNET?)\n");
    }

    // Our SimulationLoop opens the session from start(): it resets the
    // session state, staggers the AI grid (m_aiCarCount) and configures
    // RaceSessionManager. The incoming branch had a separate beginSession().
    loop.start();

    std::fprintf(stderr, "SimulatorServer: running (disc :20779, ctrl :20780%s)\n",
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
