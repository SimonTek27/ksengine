/**
 * SimulationLoop — network sync (CarStateSync UDP + full ksnet path).
 * Compile as SimulationLoop_NetSync.cpp alongside SimulationLoop.cpp.
 */
#include "SimulationLoop.h"
#include <cstdio>

namespace ks {
namespace sim {

bool SimulationLoop::startCarStateHost(uint16_t port) {
    if (!m_carSync.startHost(port)) {
        std::fprintf(stderr, "SimulationLoop: CarState host failed on %u\n", (unsigned)port);
        return false;
    }
    m_carSync.setSendHz(20.f);
    return true;
}

bool SimulationLoop::startCarStateClient(const std::string& host, uint16_t port) {
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
    // Path A: full ksnet reliable-UDP (HAS_KSNET=1).
    // NetworkManager owns host car-state @ 20 Hz; session/penalty/countdown
    // are bridged from FeatureTick (RaceSession hooks).
#if HAS_KSNET
    if (m_network) {
        m_network->update(static_cast<double>(dt));
    }
#endif

    // Path B: lightweight CarStateSync UDP (works without ksnet).
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
