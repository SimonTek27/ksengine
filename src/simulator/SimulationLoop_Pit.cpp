/**
 * SimulationLoop_Pit.cpp — roadmap 2.7: box service end-to-end (Qt-free).
 *
 * The pit *plan* lives in the menu (GameMenuOverlay PIT STRATEGY screen) and
 * is stored on the loop by PitStrategyBridge::wirePitStrategyMenu. This TU
 * turns that plan into an actual stop: it feeds PitLaneRepair with the
 * player's confirmed jobs, gates the start on an explicit REQUEST SERVICE
 * while the car is stopped inside its own box, and pushes the resulting tank
 * level back into the vehicle so HUD, shared memory and telemetry follow the
 * refuel as it happens.
 *
 * It is a separate translation unit only because SimulationLoop.cpp is
 * already very large; the CMake source list (cmake/CMakeLists_ksimulator_
 * QtFree.cmake) compiles it for both SimulatorApp and SimulatorServer.
 *
 * The garage-exit / pit-lane state machine (GarageExit.h) is NOT driven here:
 * leaving the box is a separate roadmap item. "In box" is therefore a
 * footprint test around the box bound by placePlayerForSession().
 */
#include "SimulationLoop.h"

#ifndef HAS_VEHICLE_SIM
#define HAS_VEHICLE_SIM 1
#endif

#if HAS_VEHICLE_SIM
#include "engine/physics/VehicleSimulator.h"
#endif

#include <cmath>
#include <cstdio>

namespace ks {
namespace sim {

void SimulationLoop::updatePitRepair(float dt) {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;

    const auto st = m_vehicle->getState();
    PitRepairInput in;
    in.speedMs = static_cast<float>(st.speed);
    in.fuelL = static_cast<float>(st.fuel);
    // TODO(2.7 follow-up): read the car's real tank capacity (setup / data
    // file) instead of the class default. 100 L matches PitRepairInput and
    // the fuel capacity applySetup() clamps to.
    in.fuelCapacityL = 100.f;

    // WHAT to do is the confirmed menu plan — never the old "fill everything"
    // defaults. A plan alone still queues nothing.
    applyStrategyToRepairInput(m_pitStrategy, in);

    // WHERE: inside the bound garage box. Until a session has placed the car
    // there is no box to serve in, so no service can start.
    bool inBox = false;
    if (m_hasPlayerBox) {
        const float dx = static_cast<float>(st.position.x) - m_playerBox.x;
        const float dz = static_cast<float>(st.position.z) - m_playerBox.z;
        inBox = (dx * dx + dz * dz) <= m_pitBoxRadiusM * m_pitBoxRadiusM;
    }
    in.inGarageBox = inBox;
    // STOP-Go penalties set the same flag (SimulationLoop.cpp onPenalty).
    in.requestService = m_requestPitService && inBox;

    const GarageExitPhase phase = inBox ? GarageExitPhase::InGarage
                                        : GarageExitPhase::OnTrack;
    // PitLaneRepair reports completedThisFrame per *job*; the stop is only
    // over when the whole queue drains (busy -> idle), which is when the
    // request must be consumed.
    const bool busyBefore = m_pitRepair.isBusy();
    PitRepairOutput out = m_pitRepair.update(dt, in, m_vehicle->damage(), phase);

    if (out.holdCar) {
        m_vehicle->setThrottle(0);
        m_vehicle->setBrake(1.0);
        m_vehicle->setSteering(0);
    }

    // Refuel: PitLaneRepair only counts litres in its own accumulator. Push
    // them into the vehicle so the fuel gauge, the shared memory block and
    // the UDP/TCP telemetry all move during the stop, not just at the end.
    if ((out.serviceActive || out.completedThisFrame) &&
        std::fabs(out.fuelL - in.fuelL) > 0.01f)
        m_vehicle->setFuelL(out.fuelL);

    if (m_pitStatus != out.statusText) {
        m_pitStatus = out.statusText;
        std::fprintf(stderr, "SimulationLoop: pit service %s\n",
                     m_pitStatus.c_str());
    }

    if (busyBefore && !m_pitRepair.isBusy()) {
        m_requestPitService = false; // the ask is consumed: next stop is a
                                     // new REQUEST SERVICE, not a repeat
        std::fprintf(stderr,
                     "SimulationLoop: pit service COMPLETE (%d/%d jobs, "
                     "fuel %.1f L)\n",
                     out.jobsDone, out.jobsTotal, out.fuelL);
    }
#else
    (void)dt;
#endif
}

} // namespace sim
} // namespace ks
