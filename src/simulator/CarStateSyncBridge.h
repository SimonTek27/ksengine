#pragma once
/**
 * Apply CarStateSync to MultiCarManager (Sprint 7).
 */
#include "CarStateSync.h"
#include "MultiCarManager.h"
#include <vector>

namespace ks {
namespace sim {

inline void carStateSyncPublishFromMultiCar(netsync::CarStateSync& sync,
                                            MultiCarManager& multi,
                                            double simTime,
                                            int localPlayerId = -1) {
    if (!sync.active()) return;
    std::vector<netsync::CarStatePacked> batch;
    batch.reserve(multi.cars().size());
    for (const auto& e : multi.cars()) {
        if (!e) continue;
        const auto& st = e->state;
        const bool isLocal = (localPlayerId >= 0 && e->id == localPlayerId);
        batch.push_back(netsync::packFromSim(
            static_cast<uint16_t>(e->id & 0xFFFF),
            st.position.x, st.position.y, st.position.z,
            st.heading, st.speed, st.steering, st.throttle, st.brake,
            st.gear, isLocal, true));
    }
    if (!batch.empty())
        sync.publish(simTime, batch);
}

inline void carStateSyncApplyToMultiCar(netsync::CarStateSync& sync,
                                        MultiCarManager& multi,
                                        int localPlayerId = -1) {
    if (!sync.active()) return;
    for (const auto& p : sync.latest()) {
        const int id = static_cast<int>(p.carId);
        if (localPlayerId >= 0 && id == localPlayerId) continue;
        auto* e = multi.getCar(id);
        if (!e) continue;
        // externally driven remote
        e->externallyDriven = true;
        e->state.position.x = p.px;
        e->state.position.y = p.py;
        e->state.position.z = p.pz;
        e->state.heading = p.heading;
        e->state.speed = p.speed;
        e->state.steering = p.steer;
        e->state.throttle = p.throttle;
        e->state.brake = p.brake;
        e->state.gear = p.gear;
    }
}

} // namespace sim
} // namespace ks
