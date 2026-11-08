// Engine start — the sequence the car declares, not a script beside it.
//
// data.json "start" says how the car is started (cranking speed and time, idle,
// minimum, antistall) and which hardware does it: a hybrid cranks on its
// electric motor and may restart on the move, a petrol or turbo engine needs the
// starter and a standstill. A car that declares no block keeps the old
// behaviour — ignition on, engine running — which is what this test pins first.

#include "KsTest.h"

#include "engine/physics/CarPhysicsJson.h"
#include "engine/physics/VehicleSimulator.h"

#include <filesystem>
#include <fstream>
#include <string>

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

void writeFile(const std::filesystem::path& p, const std::string& content) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::trunc);
    out << content;
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
        // The launch holds the declared AUTOCLUTCH band, so the engine is
        // several thousand rpm above its idle when the brake goes on: the
        // drivetrain needs time to drag it down to the antistall. Bounded, so a
        // regression that never reaches it still fails here.
        int settle = 0;
        while (!v.antistallActive() && settle < 600) {
            v.updatePhysics(0.01);
            ++settle;
        }
        KS_CHECK(v.antistallActive());
        KS_CHECK(settle < 600);
        run(v, 1400); // 14 s in total, past the 10 s the car declares
        KS_CHECK(v.engineRunState() == RunState::Stalled);
        KS_CHECK(!v.isEngineRunning());
        KS_CHECK(v.rpm() == 0.0);
    }

    // A car that declares only its idle — no [STARTER] block — is the shape the
    // other cars in the content get: engine.ini MINIMUM is the idle, the cranking
    // speed and time stay the model's own, and it declares no antistall.
    {
        ph::CarStartConfig t;
        t.declared = true;
        t.type = ph::CarStartConfig::Type::Turbo;
        t.idleRpm = 1050.0; // XJ220 [ENGINE_DATA] MINIMUM
        ph::VehicleSimulator v;
        v.setFuelL(50.f);
        v.setEngineStartConfig(t);
        v.setIgnition(true);
        v.startSimulation();
        KS_CHECK(v.engineRunState() == RunState::Off);
        KS_CHECK(v.pressStarter(0.0));
        run(v, 40);
        KS_CHECK(v.engineRunState() == RunState::Running);
        KS_CHECK(v.rpm() >= 1050.0);          // settles on the declared idle
        KS_CHECK(v.engineStartConfig().starterRpm == 200.0);
        KS_CHECK(v.engineStartConfig().crankTime == 0.15);
        KS_CHECK(v.engineStartConfig().antistallRpm == 0.0);
    }

    // A crew start belongs to the team, and the team is in the pit lane: the
    // key cannot fire it, the crew cannot fire it from out on track, and the
    // driver keeps the kill switch.
    {
        ph::CarStartConfig c = declaredStart(ph::CarStartConfig::Type::Petrol);
        c.startedByCrew = true;
        ph::VehicleSimulator v;
        v.setFuelL(50.f);
        v.setEngineStartConfig(c);
        v.setIgnition(true);
        v.startSimulation();
        KS_CHECK(!v.pressStarter(0.0));       // no starter button on this car
        KS_CHECK(!v.pressStarterByCrew(0.0)); // and the team is not here yet
        KS_CHECK(v.engineRunState() == RunState::Off);

        v.setCrewStarterAvailable(true); // in the pit lane
        KS_CHECK(!v.pressStarterByCrew(5.0)); // the crew cannot crank it moving
        KS_CHECK(v.pressStarterByCrew(0.0));
        run(v, 40);
        KS_CHECK(v.isEngineRunning());

        v.setCrewStarterAvailable(false); // back out on track
        KS_CHECK(!v.pressStarterByCrew(0.0)); // no crew restart out here
        KS_CHECK(v.isEngineRunning());        // the engine keeps running
        KS_CHECK(v.pressStarter(0.0));        // the kill is still the driver's
        KS_CHECK(!v.isEngineRunning());
        KS_CHECK(v.rpm() == 0.0);
    }

    // The garage exit fires the starter every step while the box waits for the
    // engine. A press while the crank is already turning must leave the
    // sequence alone: resetting the timer per call held the crank at its start
    // speed forever, so the engine never caught and the box never let the car
    // out. Covers both the crew's repeated fire and the driver's key.
    {
        ph::CarStartConfig c = declaredStart(ph::CarStartConfig::Type::Petrol);
        c.startedByCrew = true;
        ph::VehicleSimulator v;
        v.setFuelL(50.f);
        v.setEngineStartConfig(c);
        v.setIgnition(true);
        v.startSimulation();
        v.setCrewStarterAvailable(true);
        for (int i = 0; i < 100; ++i) { // 1.0 s of the garage firing per step
            // The garage fires only a dead engine: pressing one that is running
            // is the kill, is at a standstill, and would loop catch and off.
            if (!v.isEngineRunning()) KS_CHECK(v.pressStarterByCrew(0.0));
            v.updatePhysics(0.01);
        }
        KS_CHECK(v.engineRunState() == RunState::Running);
        KS_CHECK(v.rpm() >= 1800.0);
    }
    {
        ph::VehicleSimulator v;
        v.setFuelL(50.f);
        v.setEngineStartConfig(declaredStart(ph::CarStartConfig::Type::Petrol));
        v.setIgnition(true);
        v.startSimulation();
        for (int i = 0; i < 100; ++i) { // the key held in the same blind spot
            // Same order the driver's path reads: press only what is not
            // running, or the standstill kill undoes the start it just made.
            if (!v.isEngineRunning()) KS_CHECK(v.pressStarter(0.0));
            v.updatePhysics(0.01);
        }
        KS_CHECK(v.engineRunState() == RunState::Running);
        KS_CHECK(v.rpm() >= 1800.0);
    }

    // The manifest's start block may leave the idle out. The car's own engine
    // declaration is the number then: the manifest's "engine" block wins (the
    // copy the conversion made of engine.ini MINIMUM), the engine.ini the
    // folder still ships comes next, and only with neither source does the
    // model's standalone idle apply.
    {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                          "ks_test_engine_start";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        // The converted manifest: the same file the converter writes back.
        writeFile(dir / "data" / "data.json",
                  "{ \"start\": { \"TYPE\": \"PETROL\", \"BY\": \"DRIVER\" },\n"
                  "  \"engine\": { \"ENGINE_DATA\": { \"MINIMUM\": 1050 } } }");
        ph::CarStartConfig c1;
        KS_CHECK(ph::CarPhysicsJson::loadAcStart(dir.string(), c1));
        KS_CHECK(c1.declared);
        KS_CHECK(c1.idleRpm == 1050.0);
        // A folder that declares the block but ships the idle as engine.ini.
        writeFile(dir / "data" / "data.json",
                  "{ \"start\": { \"TYPE\": \"PETROL\", \"BY\": \"DRIVER\" } }");
        writeFile(dir / "data" / "engine.ini",
                  "[ENGINE_DATA]\nMINIMUM=1200\nPOWER=300\n");
        ph::CarStartConfig c2;
        KS_CHECK(ph::CarPhysicsJson::loadAcStart(dir.string(), c2));
        KS_CHECK(c2.idleRpm == 1200.0);
        // Neither source: the model's 900 idle is what is left to fall back on.
        writeFile(dir / "data" / "engine.ini", "[ENGINE_DATA]\nPOWER=300\n");
        ph::CarStartConfig c3;
        KS_CHECK(ph::CarPhysicsJson::loadAcStart(dir.string(), c3));
        KS_CHECK(c3.idleRpm == 900.0);
        std::filesystem::remove_all(dir, ec);
    }

    return ks::test::finish("engine_start_test");
}
