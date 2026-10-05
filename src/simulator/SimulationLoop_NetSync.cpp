/**
 * SimulationLoop — network sync.
 * Policy: when ksnet is actively hosting or connected, it owns multiplayer
 * state (XOR CarStateSync). CarStateSync UDP is the zero-dep fallback.
 */
#include "SimulationLoop.h"
#include "NetworkManager.h"
#include <cstdio>

namespace ks {
namespace sim {

bool SimulationLoop::startCarStateHost(uint16_t port) {
#if HAS_KSNET
    if (m_network && (m_network->isHosting() || m_network->isConnected())) {
        std::fprintf(stderr, "SimulationLoop: CarStateSync host skipped (ksnet active)\n");
        return false;
    }
#endif
    if (!m_carSync.startHost(port)) {
        std::fprintf(stderr, "SimulationLoop: CarState host failed on %u\n", (unsigned)port);
        return false;
    }
    m_carSync.setSendHz(20.f);
    return true;
}

bool SimulationLoop::startCarStateClient(const std::string& host, uint16_t port) {
#if HAS_KSNET
    if (m_network && (m_network->isHosting() || m_network->isConnected())) {
        std::fprintf(stderr, "SimulationLoop: CarStateSync client skipped (ksnet active)\n");
        return false;
    }
#endif
    if (!m_carSync.startClient(host, port)) {
        std::fprintf(stderr, "SimulationLoop: CarState client failed → %s:%u\n",
                     host.c_str(), (unsigned)port);
        return false;
    }
    m_carSync.setSendHz(20.f);
    return true;
}

void SimulationLoop::stopCarStateSync() {
    m_carSync.stop();
}

void SimulationLoop::updateNetworkSync(float dt) {
    (void)dt;
#if HAS_KSNET
    if (m_network) {
        // SimulationLoop::tick() owns the NetworkManager pump (one update()
        // per frame): driving it from here too would advance every network
        // clock twice per frame - broadcasts at 2x, timeouts at half time.
        // Single-path: ksnet owns the wire while in session.
        if (m_network->isHosting() || m_network->isConnected()) {
            if (m_carSync.active())
                m_carSync.stop();
            return;
        }
    }
#endif

    // Fallback: CarStateSync UDP (no ksnet / offline).
    if (!m_carSync.active()) return;
    m_carSync.poll();
    if (m_multiCar) {
        const int playerId = m_multiCar->playerCarId();
        carStateSyncPublishFromMultiCar(m_carSync, *m_multiCar, m_simTimeSec, playerId);
        carStateSyncApplyToMultiCar(m_carSync, *m_multiCar, playerId);
    } else if (m_vehicle) {
        auto st = m_vehicle->getState();
        std::vector<netsync::CarStatePacked> batch;
        batch.push_back(netsync::packFromSim(
            1u, st.position.x, st.position.y, st.position.z,
            st.heading, st.speed, st.steering, st.throttle, st.brake,
            st.gear, true, true));
        m_carSync.publish(m_simTimeSec, batch);
    }
}

} // namespace sim
} // namespace ks
