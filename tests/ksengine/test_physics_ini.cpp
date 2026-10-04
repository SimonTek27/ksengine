/**
 * Parity 2.2 — physics data file loaders:
 * power.lut (rpm|torque) round-trip, EngineModel sibling-lut probe,
 * Pacejka A/B/C coefficient INI, TireModelManager sectioned round-trip,
 * VehicleSimulator tyres/drivetrain INI.
 */
#include "engine/physics/EngineModel.h"
#include "engine/physics/PacejkaTireModel.h"
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

    std::error_code ec;
    fs::remove_all(dir, ec);

    if (g_failures == 0) {
        std::printf("physics_ini: OK\n");
        return 0;
    }
    std::printf("physics_ini: %d FAILURES\n", g_failures);
    return 1;
}
