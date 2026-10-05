// Parity 3.1 — multiplayer: the real ksnet transport over loopback UDP.
//
// Exercises the full app-level session end to end with two NetworkManager
// instances (host with its implicit local client, plus one remote guest):
//   1. transport handshake (CONNECT/CONNECT_ACK) + MSG_CLIENT_JOIN ->
//      MSG_SERVER_WELCOME round trip,
//   2. chat message relayed guest -> server -> host,
//   3. server-side car state broadcast -> remote client,
//   4. clean client disconnect detected by the server.
//
// SimulationLoop.cpp is not part of this binary: NetworkLowLevel.cpp
// references two of its members, so the same empty stubs the real TU ships
// are provided here (the MultiCar / remote-input paths are never armed in
// this test: both managers are constructed with a null SimulationLoop).

#include "simulator/NetworkManager.h"
#include "simulator/SimulationLoop.h"
#include "simulator/MultiCarManager.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>

using namespace ks::sim;

namespace {

int g_failures = 0;

void check(bool cond, const char* what)
{
    if (cond) {
        std::printf("[ok] %s\n", what);
    } else {
        std::printf("[FAIL] %s\n", what);
        ++g_failures;
    }
}

} // namespace

void SimulationLoop::applyRemoteInput(int, const net::InputData&) {}

int MultiCarManager::addCar(const std::string&, const std::string&, const vec3&, bool) { return 0; }

void MultiCarManager::removeCar(int) {}

bool MultiCarManager::setCarClientIndex(int, int) { return true; }

int main()
{
    NetworkManager host(nullptr);
    NetworkManager guest(nullptr);

    bool serving = false;
    bool hostLocalIn = false;
    bool guestIn = false;
    bool guestJoined = false;
    std::string guestName;
    int guestIdx = -1;
    bool chatSeen = false;
    std::string chatName, chatText;
    bool stateSeen = false;
    uint32_t stateCar = 0;
    float stateX = 0.0f;
    bool guestLeft = false;

    host.onServerStarted = [&](uint16_t) { serving = true; };
    host.onClientConnectedToServer = [&](const std::string&, uint16_t) { hostLocalIn = true; };
    host.onRemoteClientJoined = [&](int idx, uint32_t, const std::string& name) {
        guestIdx = idx;
        guestName = name;
        guestJoined = true;
    };
    host.onRemoteClientLeft = [&](int, const std::string&) { guestLeft = true; };
    host.onChatMessageReceived = [&](uint32_t, const std::string& name, const std::string& text) {
        chatName = name;
        chatText = text;
        chatSeen = true;
    };

    guest.onClientConnectedToServer = [&](const std::string&, uint16_t) { guestIn = true; };
    guest.onRemoteCarStateReceived = [&](uint32_t id, const net::CarStateData& s) {
        stateCar = id;
        stateX = s.posX;
        stateSeen = true;
    };

    // --- 1. host server on a free loopback port --------------------------
    uint16_t port = 0;
    for (uint16_t p = 47391; p < 47400; ++p) {
        if (host.hostServer(p, 8, "TestServer", "loop")) {
            port = p;
            break;
        }
    }
    check(port != 0, "host server starts on a free port");
    if (!port) return 1;

    auto spin = [&](const std::function<bool()>& pred, double timeoutSec) {
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(timeoutSec));
        while (!pred() && std::chrono::steady_clock::now() < deadline) {
            host.update(0.01);
            guest.update(0.01);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return pred();
    };

    // --- 2. host local client: connect + join + welcome ------------------
    check(spin([&] { return serving && hostLocalIn; }, 5.0),
          "host local client gets MSG_SERVER_WELCOME (transport + join round trip)");
    check(host.clientCount() == 1, "server sees 1 connected client (the host)");

    // --- 3. remote guest joins -------------------------------------------
    check(guest.joinServer("127.0.0.1", port, "Alice", "gte3"),
          "guest joinServer opens the client transport");
    check(spin([&] { return guestIn && guestJoined; }, 5.0),
          "remote client welcomed and server observed MSG_CLIENT_JOIN");
    check(guestName == "Alice", "server records the joining driver name");
    check(guestIdx == 1, "remote client got the second server slot");
    check(host.clientCount() == 2, "server sees 2 connected clients");

    // --- 4. chat relay: guest -> server -> host ---------------------------
    guest.sendChatMessage("ciao dal test");
    check(spin([&] { return chatSeen; }, 5.0),
          "chat relayed through the server to the other client");
    check(chatText == "ciao dal test" && chatName == "Alice",
          "relayed chat keeps sender name and payload");

    // --- 5. server car state broadcast -> remote client -------------------
    net::CarStateData cs;
    cs.carId = 42;
    cs.posX = 12.5f;
    cs.speed = 137.0f;
    host.server()->broadcastCarState(42, cs);
    check(spin([&] { return stateSeen; }, 5.0),
          "server broadcasts car state to remote clients");
    check(stateCar == 42 && std::fabs(stateX - 12.5f) < 0.1f,
          "car state payload survives serialization");

    // --- 6. clean disconnect ----------------------------------------------
    guest.disconnectFromServer();
    check(spin([&] { return guestLeft; }, 5.0),
          "server detects the disconnect and fires onRemoteClientLeft");
    check(host.clientCount() == 1, "client count drops back to 1");

    host.stopServer();
    check(!host.isHosting(), "host stops cleanly");

    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("all multiplayer checks passed\n");
    return 0;
}
