#pragma once
/**
 * MenuNetworkBridge — wire GameMenuOverlay ↔ NetworkManager (LAN + kslobby).
 *
 * Uses Matchmaking for server browser (UDP discovery + optional HTTP lobby).
 * Set KS_LOBBY_URL or call net.setLobbyBaseUrl("http://host:8080/api/v1").
 */
#include "GameMenuOverlay.h"
#include "NetworkManager.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ks {
namespace sim {

/** Apply lobby URL from environment if not already set. */
inline void applyLobbyUrlFromEnv(NetworkManager& net)
{
    if (!net.lobbyBaseUrl().empty()) return;
    if (const char* u = std::getenv("KS_LOBBY_URL"); u && u[0])
        net.setLobbyBaseUrl(u);
}

/**
 * Connect multiplayer menu actions to NetworkManager.
 * @param driverName used on join (defaults to "Player")
 * @param carName used on join (defaults to "gte3")
 * @param hostPort default host listen port
 */
inline void wireMenuToNetwork(GameMenuOverlay& menu, NetworkManager& net,
                              const std::string& driverName = "Player",
                              const std::string& carName = "gte3",
                              uint16_t hostPort = 40000)
{
    applyLobbyUrlFromEnv(net);

    menu.onHostServerRequested = [&menu, &net, hostPort]() {
        if (net.isHosting()) {
            std::fprintf(stderr, "MenuNet: already hosting\n");
            return;
        }
        applyLobbyUrlFromEnv(net);
        std::string track = menu.trackName();
        if (track.empty()) track = "Unknown";
        const std::string serverName = "ksim Server";
        if (net.hostServer(hostPort, 8, serverName, track)) {
            menu.setVisible(false);
            std::fprintf(stderr, "MenuNet: hosting on %u (lobby=%s)\n",
                         (unsigned)hostPort,
                         net.lobbyBaseUrl().empty() ? "off" : net.lobbyBaseUrl().c_str());
        } else {
            std::fprintf(stderr, "MenuNet: hostServer failed\n");
        }
    };

    menu.onDisconnectRequested = [&net]() {
        if (net.isHosting()) net.stopServer();
        else if (net.isConnected()) net.disconnectFromServer();
        std::fprintf(stderr, "MenuNet: session closed\n");
    };

    menu.onRefreshServerListRequested = [&menu, &net]() {
        applyLobbyUrlFromEnv(net);
        if (!net.isMatchmakingRunning())
            net.startMatchmaking(/*announceAsHost=*/false);
        net.refreshServerList();
        std::vector<BrowserServerEntry> rows;
        for (const auto& e : net.matchmakingServers()) {
            BrowserServerEntry b;
            b.name = e.name.empty() ? "server" : e.name;
            b.track = e.track;
            b.host = e.host.empty() ? "127.0.0.1" : e.host;
            b.port = e.port ? e.port : static_cast<uint16_t>(40000);
            b.players = e.players;
            b.maxPlayers = e.maxPlayers;
            b.sessionType = e.sessionType;
            b.ageSec = 0.f;
            rows.push_back(b);
        }
        menu.setServerList(rows);
        std::fprintf(stderr, "MenuNet: browser %zu servers (lobby=%s)\n",
                     rows.size(),
                     net.lobbyBaseUrl().empty() ? "off" : net.lobbyBaseUrl().c_str());
    };

    menu.onOpenServerBrowserRequested = [&menu]() {
        if (menu.onRefreshServerListRequested)
            menu.onRefreshServerListRequested();
    };

    menu.onJoinServerRequested = [&menu, &net, driverName, carName](const BrowserServerEntry& e) {
        if (net.isConnected() || net.isHosting()) {
            std::fprintf(stderr, "MenuNet: already in session\n");
            return;
        }
        applyLobbyUrlFromEnv(net);
        const std::string host = e.host.empty() ? "127.0.0.1" : e.host;
        const uint16_t port = e.port ? e.port : static_cast<uint16_t>(40000);
        std::string driver = driverName;
        if (!menu.profile().name.empty()) driver = menu.profile().name;
        if (net.joinServer(host, port, driver, carName)) {
            menu.setVisible(false);
            std::fprintf(stderr, "MenuNet: joining %s:%u\n", host.c_str(), (unsigned)port);
        } else {
            std::fprintf(stderr, "MenuNet: join failed %s:%u\n", host.c_str(), (unsigned)port);
        }
    };

    // Keep browser list in sync when matchmaking pushes updates.
    net.onServerListUpdated = [&menu](const std::vector<ServerListEntry>& list) {
        std::vector<BrowserServerEntry> rows;
        for (const auto& e : list) {
            BrowserServerEntry b;
            b.name = e.name.empty() ? "server" : e.name;
            b.track = e.track;
            b.host = e.host.empty() ? "127.0.0.1" : e.host;
            b.port = e.port ? e.port : static_cast<uint16_t>(40000);
            b.players = e.players;
            b.maxPlayers = e.maxPlayers;
            b.sessionType = e.sessionType;
            rows.push_back(b);
        }
        menu.setServerList(rows);
    };
}

} // namespace sim
} // namespace ks
