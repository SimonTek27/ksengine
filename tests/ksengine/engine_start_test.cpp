// Engine start — the sequence the car declares, not a script beside it.
//
// data.json "start" says how the car is started (cranking speed and time, idle,
// minimum, antistall) and which hardware does it: a hybrid cranks on its
// electric motor and may restart on the move, a petrol or turbo engine needs the
// starter and a standstill. A car that declares no block keeps the old
// behaviour — ignition on, engine running — which is what this test pins first.

#include "KsTest.h"

#include "engine/physics/VehicleSimulator.h"

namespace ph = ks::physics;
using RunState = ph::VehicleSimulator::EngineRunState;

namespace {

/** The block the content pipeline writes for the 499P (script__car_params). */
ph::CarStartConfig declaredStart(ph::CarStartConfig::Type type) {
    ph::CarStartConfig c;
    c.declared = true;
    c.type = type;
    c.starterRpm = 200.0;
    c.crankTime = 0.15;
    c.idleRpm = 1800.0;
    c.minimumRpm = 500.0;
    c.antistallRpm = 900.0;
    c.antistallTime = 10.0;
    c.pitstopRpm = 4500.0;
    return c;
}

void run(ph::VehicleSimulator& v, int steps) {
    for (int i = 0; i < steps; ++i) v.updatePhysics(0.01);
}

} // namespace

int main() {
    // No start block: the car behaves exactly as before the block existed.
    {
        ph::VehicleSimulator v;
        v.startSimulation();
        KS_CHECK(!v.engineStartConfig().declared);
        KS_CHECK(v.isEngineRunning());
        KS_CHECK(v.ignitionOn());
    }

    // A petrol car: ignition off until asked, cranks at the declared speed for
    // the declared time, catches on its idle, cannot be cranked on the move and
    // can only be killed at a standstill.
    {
        ph::VehicleSimulator v;
        v.setFuelL(50.f);
        v.setEngineStartConfig(declaredStart(ph::CarStartConfig::Type::Petrol));
        v.startSimulation();
        KS_CHECK(v.engineRunState() == RunState::Off);
        KS_CHECK(!v.isEngineRunning());
        KS_CHECK(v.rpm() == 0.0);
        KS_CHECK(!v.pressStarter(0.0)); // the ignition is off

        v.setIgnition(true);
        KS_CHECK(!v.pressStarter(10.0)); // a moving car will not crank
        KS_CHECK(v.engineRunState() == RunState::Off);

        KS_CHECK(v.pressStarter(0.0));
        KS_CHECK(v.engineRunState() == RunState::Cranking);
        run(v, 40); // 0.4 s: cranked and caught
        KS_CHECK(v.engineRunState() == RunState::Running);
        KS_CHECK(v.rpm() >= 1800.0);

        KS_CHECK(!v.pressStarter(20.0)); // no kill on the move
        KS_CHECK(v.isEngineRunning());
        KS_CHECK(v.pressStarter(1.0)); // standstill kill
        KS_CHECK(v.engineRunState() == RunState::Off);
        KS_CHECK(v.rpm() == 0.0);
    }

    // A hybrid cranks on its electric motor: the same press works on the move.
    {
        ph::VehicleSimulator v;
        v.setFuelL(50.f);
        v.setEngineStartConfig(declaredStart(ph::CarStartConfig::Type::Hybrid));
        v.setIgnition(true);
        v.startSimulation();
        KS_CHECK(v.pressStarter(15.0));
        KS_CHECK(v.engineRunState() == RunState::Cranking);
    }

    // Throttle from a standstill starts a declared car by itself, the rule the
    // 499P's own pitlane launch follows.
    {
        ph::VehicleSimulator v;
        v.setFuelL(50.f);
        v.setEngineStartConfig(declaredStart(ph::CarStartConfig::Type::Petrol));
        v.setIgnition(true);
        v.startSimulation();
        v.setThrottle(1.0);
        run(v, 40);
        KS_CHECK(v.isEngineRunning());
    }

    // Switching the ignition off stops the engine.
    {
        ph::VehicleSimulator v;
        v.setFuelL(50.f);
        v.setEngineStartConfig(declaredStart(ph::CarStartConfig::Type::Petrol));
        v.setIgnition(true);
        v.startSimulation();
        KS_CHECK(v.pressStarter(0.0));
        run(v, 40);
        KS_CHECK(v.isEngineRunning());
        v.setIgnition(false);
        KS_CHECK(!v.isEngineRunning());
        KS_CHECK(v.rpm() == 0.0);
    }

    // The clutch: a stopped car sits on an open clutch and idles, asking for
    // torque closes it and the car pulls away, and a pressed pedal keeps it open
    // so no drive reaches the wheels.
    {
        ph::VehicleSimulator v;
        v.setFuelL(50.f);
        v.setEngineStartConfig(declaredStart(ph::CarStartConfig::Type::Petrol));
        ph::CarClutchConfig clutch;
        clutch.declared = true;
        clutch.maxTorqueNm = 700.0;
        clutch.autoClutchMinRpm = 1000.0;
        clutch.autoClutchMaxRpm = 5800.0;
        v.setClutchConfig(clutch);
        v.setIgnition(true);
        v.startSimulation();
        KS_CHECK(v.pressStarter(0.0));
        run(v, 60);
        KS_CHECK(v.isEngineRunning());
        KS_CHECK(v.rpm() >= 1800.0);            // idling at rest
        KS_CHECK(v.clutchEngagement(0.0) < 0.01); // the autoclutch holds it open
        KS_CHECK(!v.antistallActive());

        v.setThrottle(1.0);
        run(v, 100);
        KS_CHECK(v.clutchEngagement(static_cast<double>(v.getState().speed)) > 0.99);
        KS_CHECK(v.getState().speed > 1.0); // it pulls away

        v.setClutchPedal(1.0);
        KS_CHECK(v.clutchEngagement(static_cast<double>(v.getState().speed)) < 0.01);
        const float before = v.getState().speed;
        run(v, 50);
        KS_CHECK(v.getState().speed <= before + 0.2f); // no drive, no creep
        v.setClutchPedal(0.0);

        // Lugging it in gear with the brakes on and the throttle barely open is
        // the load the idle cannot carry: the declared antistall holds for its
        // ten seconds and then the engine stalls.
        v.setBrake(1.0);
        v.setThrottle(0.1);
        run(v, 60);
        KS_CHECK(v.antistallActive());
        run(v, 1400); // 14 s in total, past the 10 s the car declares
        KS_CHECK(v.engineRunState() == RunState::Stalled);
        KS_CHECK(!v.isEngineRunning());
        KS_CHECK(v.rpm() == 0.0);
    }

    return ks::test::finish("engine_start_test");
}
