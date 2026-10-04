#pragma once
/**
 * Apply CarStateSync to MultiCarManager (Sprint 7).
 */
#include "CarStateSync.h"
#include "MultiCarManager.h"
#include "engine/physics/VehicleSimulator.h"
#include <vector>

namespace ks {
namespace sim {

inline void carStateSyncPublishFromMultiCar(netsync::CarStateSync& sync,
                                            MultiCarManager& multi,
                                            double simTime,
                                            int localPlayerId = -1) {
    if (!sync.active()) return;
    std::vector<netsync::CarStatePacked> batch;
    if (sync.role() == netsync::CarStateSync::Role::Host) {
        for (const auto& ce : multi.cars()) {
            if (!ce || !ce->isActive || !ce->vehicle) continue;
            const auto st = ce->vehicle->getState();
            batch.push_back(netsync::packFromSim(
                static_cast<uint32_t>(ce->id),
                st.position.x, st.position.y, st.position.z,
                st.heading, st.speed, st.steering, st.throttle, st.brake,
                st.gear, ce->isPlayer, true));
        }
    } else if (sync.role() == netsync::CarStateSync::Role::Client) {
        // send local player only
        for (const auto& ce : multi.cars()) {
            if (!ce || !ce->isPlayer || !ce->vehicle) continue;
            if (localPlayerId >= 0 && ce->id != localPlayerId) continue;
            const auto st = ce->vehicle->getState();
            batch.push_back(netsync::packFromSim(
                static_cast<uint32_t>(ce->id),
                st.position.x, st.position.y, st.position.z,
                st.heading, st.speed, st.steering, st.throttle, st.brake,
                st.gear, true, true));
        }
        // also single vehicle without multi
    }
    if (!batch.empty())
        sync.publish(simTime, batch);
}

/** Sample remotes onto non-player cars (or spawn placeholders). */
inline void carStateSyncApplyToMultiCar(netsync::CarStateSync& sync, MultiCarManager& multi,
                                        int localPlayerId = -1) {
    if (!sync.active()) return;
    for (const auto& kv : sync.remotes()) {
        const uint32_t id = kv.first;
        if ((int)id == localPlayerId) continue;
        float x,y,z,h,spd,st,th,br; int gear;
        if (!sync.sample(id, x, y, z, h, spd, st, th, br, gear)) continue;
        auto* ce = multi.getCar(static_cast<int>(id));
        if (!ce) {
            // soft spawn remote ghost
            int nid = multi.addCar("remote", "Remote", vec3{x, y, z}, false);
            ce = multi.getCar(nid);
            // note: id may differ — for strict id match host should pre-assign
            if (!ce) continue;
        }
        if (ce->isPlayer) continue;
        if (ce->vehicle) {
            auto& s = ce->vehicle->state();
            s.position.x = x; s.position.y = y; s.position.z = z;
            s.heading = h;
            s.speed = spd;
            s.steering = st;
            s.throttle = th;
            s.brake = br;
            s.gear = gear;
            // freeze physics integrate for remote-driven cars optional:
            // ce->vehicle->setFrozen(true);
        }
        ce->transform = mat4();
        ce->transform(0, 3) = x;
        ce->transform(1, 3) = y;
        ce->transform(2, 3) = z;
    }
}

} // namespace sim
} // namespace ks
