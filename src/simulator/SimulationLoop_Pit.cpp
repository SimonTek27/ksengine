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
 * It also drives the garage-exit state machine (GarageExit.h): sessions that
 * spawn in a garage box start held there (IN GARAGE), leave on an explicit
 * request with the pit limiter on, and go back in when the car is driven
 * slowly into its box — which is exactly the state the service above needs.
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

void SimulationLoop::updateGarageExit(float dt) {
#if HAS_VEHICLE_SIM
    if (!m_vehicle || !m_garageExitEnabled) return;

    const auto st = m_vehicle->getState();
    GarageExitInput in;
    // One-shot intents: the state machine consumed them this step.
    in.requestLeave = m_requestLeaveGarage;
    in.requestReturn = m_requestReturnBox;
    m_requestLeaveGarage = false;
    m_requestReturnBox = false;
    // ksim has no ignition state yet — the engine is always considered
    // running, so GarageExit's engine-start branch is a timed hop.
    in.engineRunning = true;
    in.ignitionOn = true;
    in.speedMs = static_cast<float>(st.speed);
    in.throttle = st.throttle;
    in.brake = st.brake;
    in.steer = st.steering;
    in.posX = static_cast<float>(st.position.x);
    in.posY = static_cast<float>(st.position.y);
    in.posZ = static_cast<float>(st.position.z);
    in.heading = st.heading;
    // TODO(2.7 follow-up): a real race pit window — RaceSessionManager owns
    // it; for now the lane is always open, which matches Practice/Qualify.
    in.pitLaneOpen = true;
    static constexpr uint8_t kQualifying = 1;  // SESSION_QUALIFYING
    static constexpr uint8_t kRace = 2;        // SESSION_RACE
    static constexpr uint8_t kCountdown = 1;   // PHASE_COUNTDOWN
    in.session = (m_sessionType == kQualifying) ? SessionType::Qualify
                 : (m_sessionType == kRace)     ? SessionType::Race
                 : (m_sessionType > kRace)      ? SessionType::Hotlap
                                                : SessionType::Practice;
    in.sessionPhase = (m_sessionPhase == kCountdown) ? SessionPhase::Countdown
                                                     : SessionPhase::Green;

    // Coming back needs no key: driving slowly into the bound box is the
    // return request, which is also what arms the box service.
    if (m_garageExit.phase() == GarageExitPhase::OnTrack && m_hasPlayerBox) {
        const float dx = in.posX - m_playerBox.x;
        const float dz = in.posZ - m_playerBox.z;
        if ((dx * dx + dz * dz) <= m_pitBoxRadiusM * m_pitBoxRadiusM &&
            in.speedMs < 1.5f)
            in.requestReturn = true;
    }

    const GarageExitPhase before = m_garageExit.phase();
    const GarageExitOutput out = m_garageExit.update(dt, in);

    // Held in the garage: keep the car parked on its box instead of letting
    // it creep while the controls are dead.
    if (out.snapToBox && m_hasPlayerBox) {
        auto& s = m_vehicle->state();
        s.position = {m_playerBox.x, m_playerBox.y, m_playerBox.z};
        s.heading = m_playerBox.heading;
        s.velocity = {0.f, 0.f, 0.f};
        s.angularVelocity = {0.f, 0.f, 0.f};
    }
    if (out.holdControls) {
        m_vehicle->setThrottle(0);
        m_vehicle->setBrake(1.0);
        m_vehicle->setSteering(0);
    } else if (out.pitLimiterActive) {
        // Rolling out / pit lane: the driver keeps the pedals, the limiter
        // caps them (GarageExitController::applyPitLimiter).
        const float limited = GarageExitController::applyPitLimiter(
            in.speedMs, st.throttle, out.pitLimiterMaxMs);
        m_vehicle->setThrottle(limited);
        if (in.speedMs > out.pitLimiterMaxMs) m_vehicle->setBrake(0.4);
    }

    if (out.phase != before)
        std::fprintf(stderr, "SimulationLoop: garage %s -> %s\n",
                     GarageExitController::phaseLabel(before),
                     GarageExitController::phaseLabel(out.phase));
    m_garageStatus = out.statusText;
#else
    (void)dt;
#endif
}

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
