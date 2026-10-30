/**
 * Parity 2.2 — physics data file loaders:
 * power.lut (rpm|torque) round-trip, EngineModel sibling-lut probe,
 * Pacejka A/B/C coefficient INI, TireModelManager sectioned round-trip,
 * VehicleSimulator tyres/drivetrain INI.
 */
#include "engine/physics/EngineModel.h"
#include "engine/physics/PacejkaTireModel.h"
#include "engine/physics/VehicleSimulator.h"
#include "adapters/assetto_corsa/AcCarManifestConverter.h"
#include "engine/FileFormat/AcdReader.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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

using ks::physics::EngineModel;
using ks::physics::PacejkaTireModel;
using ks::physics::TireModelManager;
using ks::physics::VehicleSimulator;

static bool near(double a, double b, double eps = 1e-3) {
    return std::fabs(a - b) < eps;
}

static void writeFile(const fs::path& p, const std::string& content) {
    std::ofstream out(p, std::ios::trunc);
    out << content;
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / "ks_test_physics_ini";
    fs::create_directories(dir);

    // --- power.lut: rpm|torque parse, comments skipped, junk rejected ---
    {
        const fs::path lut = dir / "plain.lut";
        writeFile(lut, "; Power LUT - RPM|Torque(Nm)\n1000|150\n3000|250\n");
        auto curve = EngineModel::loadPowerLut(lut.string());
        CHECK(curve.size() == 2);
        if (curve.size() == 2) {
            CHECK(near(curve[0].rpm, 1000) && near(curve[0].torque, 150));
            CHECK(near(curve[1].rpm, 3000) && near(curve[1].torque, 250));
            const double expected = 150.0 * 1000.0 * 2.0 * 3.14159265358979323846 / 60.0 / 1000.0;
            CHECK(near(curve[0].power, expected, 1e-2));
        }

        const fs::path bad = dir / "bad.lut";
        writeFile(bad, "INVALID_LUT_FILE\n");
        CHECK(EngineModel::loadPowerLut(bad.string()).empty());

        // save -> load round-trip
        const fs::path rt = dir / "roundtrip.lut";
        CHECK(EngineModel::savePowerLut(curve, rt.string()));
        auto back = EngineModel::loadPowerLut(rt.string());
        CHECK(back.size() == curve.size());
        if (back.size() == curve.size())
            for (size_t i = 0; i < back.size(); ++i) {
                CHECK(near(back[i].rpm, curve[i].rpm));
                CHECK(near(back[i].torque, curve[i].torque, 0.01));
            }

        // interpolateCurve resamples between endpoints
        auto resampled = EngineModel::interpolateCurve(curve, 5);
        CHECK(resampled.size() == 5);
        if (resampled.size() == 5) {
            CHECK(near(resampled.front().rpm, curve.front().rpm, 1.0));
            CHECK(near(resampled.back().rpm, curve.back().rpm, 1.0));
            CHECK(resampled[2].rpm > resampled[1].rpm);
        }
    }

    // --- EngineModel::loadFromIni probes sibling power.lut ---
    {
        writeFile(dir / "engine.ini",
                  "[ENGINE_DATA]\nLIMITER=7200\nPOWER=220\nIDLE=900\n");
        writeFile(dir / "power.lut", "1000|150\n4000|300\n7000|260\n");
        EngineModel em;
        em.loadFromIni((dir / "engine.ini").string());
        const auto cfg = em.getConfig();
        CHECK(near(cfg.maxRPM, 7200));
        CHECK(near(cfg.peakPower, 220));
        CHECK(near(cfg.idleRPM, 900));
        CHECK(cfg.torqueCurve.size() == 3);
    }

    // --- PacejkaTireModel::loadFromIni: flat A/B/C, invalid rejected ---
    {
        writeFile(dir / "pac.ini", "A2=1234\nB2=567\nC7=0.5\n");
        PacejkaTireModel t;
        const auto before = t.getCoefficients();
        t.loadFromIni((dir / "pac.ini").string());
        const auto c = t.getCoefficients();
        CHECK(near(c.a2, 1234));
        CHECK(near(c.b2, 567));
        CHECK(near(c.c7, 0.5));
        CHECK(near(c.a1, before.a1)); // untouched key keeps its value

        writeFile(dir / "pac_bad.ini", "A2=-5\n"); // fails validation (a2 <= 0)
        PacejkaTireModel t2;
        const auto before2 = t2.getCoefficients();
        t2.loadFromIni((dir / "pac_bad.ini").string());
        CHECK(near(t2.getCoefficients().a2, before2.a2));
    }

    // --- TireModelManager: axle + per-wheel sections, save/load round-trip ---
    {
        writeFile(dir / "tyres_sections.ini",
                  "[FRONT]\nA3=42\n[FL]\nA1=-10\n[RR]\nA1=-20\nPRESSURE=27.5\n");
        TireModelManager mgr;
        mgr.loadFromIni((dir / "tyres_sections.ini").string());
        CHECK(near(mgr.getModel(0).getCoefficients().a3, 42)); // FRONT axle
        CHECK(near(mgr.getModel(1).getCoefficients().a3, 42));
        CHECK(near(mgr.getModel(0).getCoefficients().a1, -10)); // FL override
        CHECK(near(mgr.getModel(1).getCoefficients().a1,
                   PacejkaTireModel::getSlickTireCoefficients().a1)); // untouched
        CHECK(near(mgr.getModel(3).getCoefficients().a1, -20)); // RR

        const fs::path rt = dir / "tyres_roundtrip.ini";
        mgr.saveToIni(rt.string());
        TireModelManager mgr2;
        mgr2.loadFromIni(rt.string());
        for (int w = 0; w < 4; ++w) {
            CHECK(near(mgr2.getModel(w).getCoefficients().a1,
                       mgr.getModel(w).getCoefficients().a1));
            CHECK(near(mgr2.getModel(w).getCoefficients().a3,
                       mgr.getModel(w).getCoefficients().a3));
        }
    }

    // --- VehicleSimulator: tyres radius + drivetrain gears/final ---
    {
        writeFile(dir / "tyres.ini", "RADIUS=0.35\n");
        VehicleSimulator vs;
        vs.loadTyresFromIni((dir / "tyres.ini").string());
        CHECK(near(vs.wheelRadius(), 0.35, 1e-4));

        writeFile(dir / "drivetrain.ini",
                  "[GEARS]\nGEAR_1=3.5\nGEAR_2=2.2\nGEAR_3=1.5\nGEAR_4=1.1\n"
                  "[DRIVETRAIN]\nFINAL_DRIVE=4.2\n");
        vs.loadDrivetrainFromIni((dir / "drivetrain.ini").string());
        CHECK(vs.gearRatios().size() == 4);
        if (vs.gearRatios().size() == 4)
            CHECK(near(vs.gearRatios()[0], 3.5, 1e-4) &&
                  near(vs.gearRatios()[3], 1.1, 1e-4));
        CHECK(near(vs.finalDrive(), 4.2, 1e-4));
    }

    // --- AC tank size: [FUEL] MAX_FUEL from car.ini / powertrain.ini -------
    {
        VehicleSimulator vs;
        writeFile(dir / "car_tank.ini", "[FUEL]\nFUEL=20\nMAX_FUEL=71\n");
        vs.loadFuelCapacityFromIni((dir / "car_tank.ini").string());
        CHECK(near(vs.fuelTankCapacityL(), 71.0, 1e-4));

        // Mods that keep the tank in powertrain.ini load the same way.
        VehicleSimulator vs2;
        writeFile(dir / "powertrain_tank.ini",
                  "[FUEL]\nCONSUMPTION=1\nMAX_FUEL=45\n");
        vs2.loadFuelCapacityFromIni((dir / "powertrain_tank.ini").string());
        CHECK(near(vs2.fuelTankCapacityL(), 45.0, 1e-4));

        // No MAX_FUEL in the file: the tank is left as it was.
        VehicleSimulator vs3;
        writeFile(dir / "car_nofuel.ini", "[BASIC]\nTOTALMASS=1200\n");
        vs3.loadFuelCapacityFromIni((dir / "car_nofuel.ini").string());
        CHECK(near(vs3.fuelTankCapacityL(), 150.0, 1e-4));

        // Load order on a real car folder: the AC content sets the tank, and
        // engine.ini's FUEL_CAPACITY still wins when both are present.
        const fs::path carDir = dir / "car_order";
        fs::create_directories(carDir / "data");
        writeFile(carDir / "data" / "car.ini", "[FUEL]\nMAX_FUEL=71\n");
        writeFile(carDir / "data" / "engine.ini", "[ENGINE]\nPOWER=300\n");
        VehicleSimulator v4;
        v4.loadVehicleParamsFromIni(carDir.string());
        CHECK(near(v4.fuelTankCapacityL(), 71.0, 1e-4));

        writeFile(carDir / "data" / "engine.ini",
                  "[ENGINE]\nPOWER=300\nFUEL_CAPACITY=90\n");
        VehicleSimulator v5;
        v5.loadVehicleParamsFromIni(carDir.string());
        CHECK(near(v5.fuelTankCapacityL(), 90.0, 1e-4));

        // Converted content: no .ini at all, one data/data.json whose "fuel"
        // section keeps the AC spelling (this is what the pipeline writes).
        const fs::path jsonDir = dir / "car_json";
        fs::create_directories(jsonDir / "data");
        writeFile(jsonDir / "data" / "data.json",
                  "{\"name\":\"Test\",\"fuel\":{\"FUEL\":50,\"MAX_FUEL\":90}}");
        VehicleSimulator j1;
        j1.loadVehicleParamsFromIni(jsonDir.string());
        CHECK(near(j1.fuelTankCapacityL(), 90.0, 1e-4));

        // "FUEL" is the current load, not the tank.
        writeFile(jsonDir / "data" / "data.json", "{\"fuel\":{\"FUEL\":50}}");
        VehicleSimulator j2;
        j2.loadVehicleParamsFromIni(jsonDir.string());
        CHECK(near(j2.fuelTankCapacityL(), 150.0, 1e-4));

        // A broken manifest is ignored, not guessed at.
        writeFile(jsonDir / "data" / "data.json", "{\"fuel\":{\"MAX_FUEL\":90}");
        VehicleSimulator j3;
        j3.loadVehicleParamsFromIni(jsonDir.string());
        CHECK(near(j3.fuelTankCapacityL(), 150.0, 1e-4));

        // A folder that still ships only the .ini set is not ready to be
        // initialised: loadVehicleParams converts it first (writes
        // data/data.json) and reads the tank from that manifest.
        const fs::path convDir = dir / "car_convert";
        fs::create_directories(convDir / "data");
        writeFile(convDir / "data" / "car.ini",
                  "[BASIC]\nTOTALMASS=735\nGRAPHICS_OFFSET=0,-0.32,-0.28\n"
                  "[GRAPHICS]\nMIRROR_POSITION=0,0.7,0.5\n"
                  "[FUEL]\nFUEL=20\nMAX_FUEL=200\n"
                  "[FUELTANK]\nPOSITION=0,-0.05,0.1\n");
        // Classic inline damper and bump stop data: the rule wants them as
        // their own roots, and the graphics offset must move to car.GRAPHICS.
        writeFile(convDir / "data" / "suspensions.ini",
                  "[FRONT]\nDAMP_BUMP=16000\nDAMP_REBOUND=8000\n"
                  "BUMPSTOP_UP=0.055\nBUMP_STOP_RATE=150000\n"
                  "[GRAPHICS_OFFSETS]\nWHEEL_LF=-0.01\nSUSP_LF=0.0\n");
        writeFile(convDir / "data" / "brakes.ini",
                  "[DATA]\nMAX_TORQUE=2700\nFRONT_SHARE=0.5\n"
                  "[DISCS_GRAPHICS]\nDISC_LF=g_Disk_LF\nFRONT_MAX_GLOW=12\n"
                  "LAG_HOT=0.995\n");
        writeFile(convDir / "data" / "colliders.ini",
                  "[COLLIDER_0]\nMESH=body\n");
        writeFile(convDir / "data" / "lods.ini",
                  "[HEADER]\nVERSION=2\n[LOD_0]\nDISTANCE=10\n[LOD_1]\nDISTANCE=200\n");
        writeFile(convDir / "data" / "fuel_cons.ini",
                  "[FUEL_EVAL]\nKM_PER_LITER=1.179665\n");
        // The fixture lives in the temp folder and survives between runs: the
        // produced documents have to go, or needsConversion() sees a ready car
        // and skips the conversion this test is about.
        for (const std::string& doc : {"data.json", "driver.json", "camera.json",
                                       "setup.json", "model.json", "sound.json", "ai.json"})
            fs::remove(convDir / "data" / doc);
        VehicleSimulator c1;
        c1.loadVehicleParams(convDir.string());
        // Seven documents: physics, driver, cameras, setup, model, sound, ai.
        for (const std::string& doc : {"data.json", "driver.json", "camera.json",
                                       "setup.json", "model.json", "sound.json", "ai.json"})
            CHECK(fs::exists(convDir / "data" / doc));
        // The rule: entries every car must carry, whatever its type.
        std::ifstream dataIn(convDir / "data" / "data.json", std::ios::binary);
        const std::string dataText((std::istreambuf_iterator<char>(dataIn)),
                                   std::istreambuf_iterator<char>());
        CHECK(dataText.find("\"dampers\"") != std::string::npos);
        CHECK(dataText.find("\"DAMP_BUMP\"") != std::string::npos);
        CHECK(dataText.find("\"bumpstops\"") != std::string::npos);
        CHECK(dataText.find("\"BUMPSTOP_UP\"") != std::string::npos);
        // The graphics offset leaves BASIC and lands in model.json, not in the
        // physics document.
        std::ifstream modelIn(convDir / "data" / "model.json", std::ios::binary);
        const std::string modelText((std::istreambuf_iterator<char>(modelIn)),
                                    std::istreambuf_iterator<char>());
        CHECK(modelText.find("\"GRAPHICS_OFFSET\"") != std::string::npos);
        // The mirror is body geometry, not model geometry: it stays in the
        // physics document, in the mirrors section.
        CHECK(dataText.find("\"MIRROR_POSITION\"") != std::string::npos);
        CHECK(dataText.find("\"mirrors\"") != std::string::npos);
        // LODs are part of the model, not of the car data.
        CHECK(modelText.find("\"lods\"") != std::string::npos);
        CHECK(dataText.find("\"lods\"") == std::string::npos);
        // Colliders describe the shape of the model.
        // Wheel and suspension graphical offsets are model coordinates.
        CHECK(modelText.find("\"GRAPHICS_OFFSETS\"") != std::string::npos);
        CHECK(dataText.find("\"GRAPHICS_OFFSETS\"") == std::string::npos);
        // The disc section keeps the meshes; glow and lag are brake behaviour.
        CHECK(dataText.find("\"DISCS_COMPONENTS\"") != std::string::npos);
        CHECK(dataText.find("\"DISCS_GRAPHICS\"") == std::string::npos);
        CHECK(dataText.find("\"LAG_HOT\"") != std::string::npos);
        CHECK(modelText.find("\"colliders\"") != std::string::npos);
        CHECK(dataText.find("\"colliders\"") == std::string::npos);
        // A section that only repeated the format version keeps nothing.
        CHECK(modelText.find("\"VERSION\"") == std::string::npos);
        CHECK(modelText.find("\"MIRROR_POSITION\"") == std::string::npos);
        // The conversion also leaves the verification checklist behind.
        CHECK(fs::exists(convDir / "data" / "verify.json"));
        CHECK(dataText.find("\"GRAPHICS_OFFSET\"") == std::string::npos);
        CHECK(near(c1.fuelTankCapacityL(), 200.0, 1e-4));
        // Final step: data.acd is rebuilt from the folder and reads back with the
        // key of the car folder name.
        const ks::assetto_corsa::AcCarManifestConverter::Result packed =
            ks::assetto_corsa::AcCarManifestConverter::packAcd(convDir);
        CHECK(packed.packDone);
        CHECK(packed.packed > 0);
        CHECK(fs::exists(convDir / "data.acd"));
        std::ifstream acdIn(convDir / "data.acd", std::ios::binary);
        const std::string acdBytes((std::istreambuf_iterator<char>(acdIn)),
                                   std::istreambuf_iterator<char>());
        const auto archive = ks::engine::fileformat::parseAcd(
            acdBytes, convDir.filename().string());
        CHECK(archive.ok());
        CHECK(archive.find("data.json") != nullptr);
        CHECK(archive.find("suspensions.ini") != nullptr);
    }

    // --- roadmap 2.7 follow-up: real tank capacity + fitted tyres ----------
    {
        VehicleSimulator vs;
        // Default keeps the old 150 L clamp: setFuelL past it is trimmed.
        CHECK(near(vs.fuelTankCapacityL(), 150.0, 1e-4));
        vs.setFuelL(200.f);
        CHECK(near(vs.state().fuel, 150.0, 1e-3));

        // FUEL_CAPACITY in the car's engine.ini becomes the real tank size...
        writeFile(dir / "engine_tank.ini",
                  "[ENGINE]\nPOWER=300\nFUEL_CAPACITY=64\n");
        vs.loadEngineFromIni((dir / "engine_tank.ini").string());
        CHECK(near(vs.fuelTankCapacityL(), 64.0, 1e-4));
        // ...and a smaller tank trims what is already on board.
        CHECK(near(vs.state().fuel, 64.0, 1e-3));
        CHECK(near(vs.setupParams().fuelL, 64.0, 1e-3));
        // The pit refuel therefore stops at the real tank, not at 100 L.
        vs.setFuelL(80.f);
        CHECK(near(vs.state().fuel, 64.0, 1e-3));

        // Fitted tyres: only the wear model is reset, so the stop gives full
        // forces again; temperature/pressure belong to the tyre model and are
        // left to come back up on their own.
        auto& s = vs.state();
        s.tyreWear[0] = 0.8;
        s.tyreWear[3] = 0.2;
        s.tyreTemp[1] = 95.0;
        vs.resetTyreWear();
        for (int i = 0; i < 4; ++i) CHECK(near(s.tyreWear[i], 0.0, 1e-9));
        CHECK(near(s.tyreTemp[1], 95.0, 1e-6));
    }

    std::error_code ec;
    fs::remove_all(dir, ec);

    if (g_failures == 0) {
        std::printf("physics_ini: OK\n");
        return 0;
    }
    std::printf("physics_ini: %d FAILURES\n", g_failures);
    return 1;
}
