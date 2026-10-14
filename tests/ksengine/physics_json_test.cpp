/**
 * Unified car physics file (data/physics.json):
 * parse/apply of every section, precedence over the .ini set, .ini fallback
 * when the JSON is absent, invalid input rejected, save() round-trip and
 * convertFromIni() migration of a legacy .ini car folder.
 */
#include "engine/physics/CarPhysicsJson.h"
#include "engine/physics/VehicleSimulator.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

using ks::physics::CarPhysicsData;
using ks::physics::CarPhysicsJson;
using ks::physics::VehicleSimulator;

static bool near(double a, double b, double eps = 1e-3) {
    return std::fabs(a - b) < eps;
}

static void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::trunc);
    out << content;
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / "ks_test_physics_json";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "data");

    // --- parse + apply: every section reaches the vehicle ----------------
    const std::string physicsJson =
        "{\n"
        "  \"version\": 1,\n"
        "  \"car\": { \"massKg\": 980 },\n"
        "  \"engine\": { \"powerKw\": 480, \"limiter\": 8200, \"fuelCapacityL\": 64 },\n"
        "  \"tyres\": { \"radius\": 0.34, \"coefficients\": { \"a3\": 1200 } },\n"
        "  \"drivetrain\": { \"finalDrive\": 4.1, \"gears\": [3.6, 2.4, 1.7, 1.3, 1.0] },\n"
        "  \"aero\": { \"cd\": 0.31, \"area\": 1.85 },\n"
        "  \"suspension\": { \"wheelbase\": 2.45, \"track\": 1.58 }\n"
        "}\n";
    writeFile(dir / "data" / "physics.json", physicsJson);

    {
        VehicleSimulator vs;
        CHECK(CarPhysicsJson::loadForCarDir(dir.string(), vs));
        // physics.json massKg is the DRY mass (see VehicleSimulator::baseMass);
        // mass() is that plus the fuel/ballast on board (default tank = 100 L).
        CHECK(near(vs.baseMass(), 980.0));
        CHECK(near(vs.mass(), vs.baseMass() + vs.state().fuel * 0.74, 1e-2));
        CHECK(near(vs.enginePowerKw(), 480.0));
        CHECK(near(vs.maxRpm(), 8200.0));
        CHECK(near(vs.fuelTankCapacityL(), 64.0, 1e-4));
        CHECK(near(vs.wheelRadius(), 0.34, 1e-4));
        CHECK(near(vs.finalDrive(), 4.1, 1e-4));
        CHECK(vs.gearRatios().size() == 5);
        if (vs.gearRatios().size() == 5)
            CHECK(near(vs.gearRatios()[0], 3.6, 1e-4) &&
                  near(vs.gearRatios()[4], 1.0, 1e-4));
        CHECK(near(vs.dragCoeff(), 0.31, 1e-4));
        CHECK(near(vs.frontalArea(), 1.85, 1e-4));
        CHECK(near(vs.wheelBase(), 2.45, 1e-4));
        CHECK(near(vs.trackWidth(), 1.58, 1e-4));
        CHECK(near(vs.tires().getCoefficients().a3, 1200.0, 1e-3));
    }

    // --- precedence: physics.json wins over conflicting .ini files -------
    {
        writeFile(dir / "data" / "engine.ini", "[ENGINE]\nPOWER=999\nLIMITER=5000\n");
        writeFile(dir / "data" / "aero.ini", "CD=0.9\nAREA=3.5\n");
        VehicleSimulator vs;
        vs.loadVehicleParams(dir.string());
        CHECK(near(vs.enginePowerKw(), 480.0)); // JSON, not engine.ini
        CHECK(near(vs.maxRpm(), 8200.0));
        CHECK(near(vs.dragCoeff(), 0.31, 1e-4)); // JSON, not aero.ini
        fs::remove(dir / "data" / "engine.ini");
        fs::remove(dir / "data" / "aero.ini");
    }

    // --- fallback: an .ini-only car folder still loads the old way -------
    {
        const fs::path legacy = dir / "legacy";
        writeFile(legacy / "data" / "engine.ini", "[ENGINE]\nPOWER=300\nLIMITER=7000\n");
        writeFile(legacy / "data" / "drivetrain.ini",
                  "[GEARS]\nGEAR_1=3.2\nGEAR_2=2.0\n[DRIVETRAIN]\nFINAL_DRIVE=4.4\n");
        writeFile(legacy / "data" / "aero.ini", "CD=0.29\nAREA=2.0\n");
        VehicleSimulator vs;
        vs.loadVehicleParams(legacy.string());
        CHECK(near(vs.enginePowerKw(), 300.0));
        CHECK(near(vs.maxRpm(), 7000.0));
        CHECK(near(vs.finalDrive(), 4.4, 1e-4));
        CHECK(vs.gearRatios().size() == 2);
        CHECK(near(vs.dragCoeff(), 0.29, 1e-4));
        CHECK(near(vs.frontalArea(), 2.0, 1e-4));
        // A car folder in <car>/physics.json (no data/ dir) resolves too.
        const fs::path flat = dir / "flat";
        writeFile(flat / "physics.json",
                  "{\"engine\": {\"powerKw\": 210, \"limiter\": 6500}}");
        VehicleSimulator flat_vs;
        flat_vs.loadVehicleParams(flat.string());
        CHECK(near(flat_vs.enginePowerKw(), 210.0));
        CHECK(near(flat_vs.maxRpm(), 6500.0));
    }

    // --- reject: syntax error, no physics sections, bad tyre coefficients
    {
        VehicleSimulator vs;
        std::string error;
        writeFile(dir / "data" / "physics.json", "{ not json ");
        error.clear();
        CHECK(!CarPhysicsJson::loadForCarDir(dir.string(), vs, &error));
        CHECK(!error.empty());
        CHECK(near(vs.enginePowerKw(), 260.0)); // untouched: still the default

        writeFile(dir / "data" / "physics.json", "{\"model\": \"some_model\"}");
        error.clear();
        CHECK(!CarPhysicsJson::loadForCarDir(dir.string(), vs, &error));
        CHECK(!error.empty());

        writeFile(dir / "data" / "physics.json",
                  "{\"tyres\": {\"radius\": 0.33, \"coefficients\": {\"a2\": -5}}}");
        error.clear();
        CHECK(!CarPhysicsJson::loadForCarDir(dir.string(), vs, &error));
        CHECK(error.find("coefficient") != std::string::npos);
    }

    // --- unit tolerance: horsepower in, kW out ---------------------------
    {
        CarPhysicsData d;
        CHECK(CarPhysicsJson::parse("{\"engine\": {\"horsepower\": 750}}", d));
        CHECK(d.hasPowerKw && near(d.powerKw, 750.0 * 0.745699872, 1e-3));
    }

    // --- save() round-trip: what is written reads back identically -------
    {
        VehicleSimulator vs;
        vs.setMass(1050);
        vs.setEnginePower(412.5);
        vs.setMaxRpm(7600);
        vs.setFuelTankCapacity(72.f);
        vs.setWheelRadius(0.325);
        vs.setFinalDrive(3.7);
        vs.setGearRatios({3.4, 2.2, 1.6, 1.2, 0.95});
        vs.setDragCoeff(0.305);
        vs.setFrontalArea(1.92);
        vs.setWheelBase(2.51);
        vs.setTrackWidth(1.61);

        const std::string text = CarPhysicsJson::save(vs);
        CarPhysicsData d;
        std::string error;
        CHECK(CarPhysicsJson::parse(text, d, &error));

        VehicleSimulator back;
        CarPhysicsJson::apply(back, d);
        CHECK(near(back.mass(), vs.mass()));
        CHECK(near(back.enginePowerKw(), vs.enginePowerKw()));
        CHECK(near(back.maxRpm(), vs.maxRpm()));
        CHECK(near(back.fuelTankCapacityL(), vs.fuelTankCapacityL(), 1e-4));
        CHECK(near(back.wheelRadius(), vs.wheelRadius(), 1e-4));
        CHECK(near(back.finalDrive(), vs.finalDrive(), 1e-4));
        CHECK(back.gearRatios().size() == vs.gearRatios().size());
        CHECK(near(back.dragCoeff(), vs.dragCoeff(), 1e-4));
        CHECK(near(back.frontalArea(), vs.frontalArea(), 1e-4));
        CHECK(near(back.wheelBase(), vs.wheelBase(), 1e-4));
        CHECK(near(back.trackWidth(), vs.trackWidth(), 1e-4));
    }

    // --- convertFromIni(): a legacy folder becomes a unified one ---------
    {
        const fs::path legacy = dir / "migrate";
        writeFile(legacy / "data" / "engine.ini",
                  "[ENGINE]\nPOWER=340\nLIMITER=7400\nFUEL_CAPACITY=55\n");
        writeFile(legacy / "data" / "tyres.ini", "RADIUS=0.31\n");
        writeFile(legacy / "data" / "drivetrain.ini",
                  "[GEARS]\nGEAR_1=3.1\nGEAR_2=2.1\nGEAR_3=1.5\n[DRIVETRAIN]\nFINAL_DRIVE=4.0\n");
        writeFile(legacy / "data" / "aero.ini", "CD=0.33\nAREA=2.1\n");
        writeFile(legacy / "data" / "suspension.ini", "WHEELBASE=2.7\nTRACK=1.55\n");

        VehicleSimulator before;
        before.loadVehicleParamsFromIni(legacy.string());

        std::string error;
        CHECK(CarPhysicsJson::convertFromIni(legacy.string(), &error));
        CHECK(fs::exists(legacy / "data" / "physics.json"));

        VehicleSimulator after;
        after.loadVehicleParams(legacy.string()); // unified path now
        CHECK(near(after.enginePowerKw(), before.enginePowerKw()));
        CHECK(near(after.maxRpm(), before.maxRpm()));
        CHECK(near(after.fuelTankCapacityL(), before.fuelTankCapacityL(), 1e-4));
        CHECK(near(after.wheelRadius(), before.wheelRadius(), 1e-4));
        CHECK(near(after.finalDrive(), before.finalDrive(), 1e-4));
        CHECK(after.gearRatios() == before.gearRatios());
        CHECK(near(after.dragCoeff(), before.dragCoeff(), 1e-4));
        CHECK(near(after.frontalArea(), before.frontalArea(), 1e-4));
        CHECK(near(after.wheelBase(), before.wheelBase(), 1e-4));
        CHECK(near(after.trackWidth(), before.trackWidth(), 1e-4));
        // The .ini files are kept as the fallback, they are not deleted.
        CHECK(fs::exists(legacy / "data" / "engine.ini"));

        // A missing car directory is an error, not a crash.
        error.clear();
        CHECK(!CarPhysicsJson::convertFromIni((dir / "no_such_car").string(), &error));
        CHECK(!error.empty());
    }

    fs::remove_all(dir, ec);

    if (g_failures == 0) {
        std::printf("physics_json: OK\n");
        return 0;
    }
    std::printf("physics_json: %d FAILURES\n", g_failures);
    return 1;
}
