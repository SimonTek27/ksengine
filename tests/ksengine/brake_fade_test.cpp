/**
 * Brake data -> physics, on the Jaguar XJ220's own numbers:
 *  - data/brake_temp_*.lut parsing: interpolation between the points, held flat
 *    outside them, a missing file refused
 *  - the curve replaces the fade thresholds (cold discs really do fade)
 *  - per-wheel thermal config: the rear, with its smaller constant airflow term,
 *    cools more slowly than the front
 *  - data.json brakes.TEMPS_FRONT/TEMPS_REAR reach the vehicle per axle, and a
 *    car that declares nothing keeps the model's defaults
 *  - brakes.ini [DATA] MAX_TORQUE / FRONT_SHARE / HANDBRAKE_TORQUE /
 *    COCKPIT_ADJUSTABLE reach the vehicle
 */
#include "KsTest.h"
#include "engine/physics/BrakeThermalModel.h"
#include "engine/physics/CarPhysicsJson.h"
#include "engine/physics/VehicleSimulator.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using ks::physics::BrakeThermalConfig;
using ks::physics::BrakeThermalModel;
using ks::physics::CarPhysicsJson;
using ks::physics::VehicleSimulator;

namespace {

void writeFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::trunc);
    out << text;
}

// Exactly what the converter writes, both axles.
const char* kBrakeLut = "0|0.3409\r\n400|1\r\n900|0.9659\r\n1200|0.5909\r\n2000|0.3409\r\n";

// The jaguar's data.json brakes block, trimmed to the keys the readers consume.
const char* kManifest =
    "{\n"
    "  \"brakes\": {\n"
    "    \"DATA\": { \"MAX_TORQUE\": 3250, \"FRONT_SHARE\": 0.6,"
    " \"HANDBRAKE_TORQUE\": 1000, \"COCKPIT_ADJUSTABLE\": 0 },\n"
    "    \"TEMPS_FRONT\": { \"TRANSFER\": 0.4, \"COOL_TRANSFER\": 0.002, \"TORQUE_K\": 0.4,"
    " \"PERF_CURVE\": \"brake_temp_front.lut\", \"COOL_SPEED_FACTOR\": 0.06,"
    " \"COOL_SPEED_FACTOR_0\": 0.05 },\n"
    "    \"TEMPS_REAR\": { \"TRANSFER\": 0.4, \"COOL_TRANSFER\": 0.002, \"TORQUE_K\": 0.4,"
    " \"PERF_CURVE\": \"brake_temp_rear.lut\", \"COOL_SPEED_FACTOR\": 0.06,"
    " \"COOL_SPEED_FACTOR_0\": 0.04 }\n"
    "  }\n"
    "}\n";

// The converter's brakes.ini, comments and tabs left as they are written.
const char* kBrakesIni =
    "[HEADER]\n"
    "VERSION=2\n"
    "\n"
    "[DATA]\n"
    "MAX_TORQUE=3250\t\t\t; Maximum Brake torque in Nm\n"
    "FRONT_SHARE=0.6\t\t\t; Percentance of brake torque at front axis\n"
    "HANDBRAKE_TORQUE=1000\n"
    "COCKPIT_ADJUSTABLE=0   ; 0: no bias control from cockpit\n";

} // namespace

int main()
{
    const fs::path dir = fs::temp_directory_path() / "ks_test_brake_fade";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "data");
    writeFile(dir / "data" / "data.json", kManifest);
    writeFile(dir / "data" / "brake_temp_front.lut", kBrakeLut);
    writeFile(dir / "data" / "brake_temp_rear.lut", kBrakeLut);
    writeFile(dir / "data" / "brakes.ini", kBrakesIni);

    // --- the table parses and interpolates -------------------------------
    std::vector<std::pair<float, float>> curve;
    KS_CHECK(BrakeThermalModel::loadPerfCurve(
        (dir / "data" / "brake_temp_front.lut").string(), curve));
    KS_CHECK(curve.size() == 5);
    {
        std::vector<std::pair<float, float>> missing;
        KS_CHECK(!BrakeThermalModel::loadPerfCurve(
            (dir / "data" / "nope.lut").string(), missing));
        KS_CHECK(missing.empty());
    }

    BrakeThermalModel curved;
    for (int w = 0; w < 4; ++w) curved.setPerfCurve(w, curve);
    KS_CHECK_NEAR(curved.perfCurveMultiplier(0, 0.f), 0.3409, 1e-3);
    KS_CHECK_NEAR(curved.perfCurveMultiplier(0, 400.f), 1.0, 1e-3);
    KS_CHECK_NEAR(curved.perfCurveMultiplier(0, 900.f), 0.9659, 1e-3);
    KS_CHECK_NEAR(curved.perfCurveMultiplier(0, 1200.f), 0.5909, 1e-3);
    // Halfway between 0 and 400 C is the average of the two points.
    KS_CHECK_NEAR(curved.perfCurveMultiplier(0, 200.f), 0.5 * (0.3409 + 1.0), 2e-2);
    // Held flat outside the table, never extrapolated.
    KS_CHECK_NEAR(curved.perfCurveMultiplier(0, 2600.f), 0.3409, 1e-3);
    KS_CHECK_NEAR(curved.perfCurveMultiplier(0, -50.f), 0.3409, 1e-3);

    // --- the curve wins over the fade thresholds -------------------------
    {
        BrakeThermalModel plain;
        plain.update(0.01f, 0, 0.f, 0.f, 0.f);
        KS_CHECK_NEAR(plain.state(0).fade, 0.0, 1e-6); // 80 C is below fadeStartTemp

        curved.update(0.01f, 0, 0.f, 0.f, 0.f);
        KS_CHECK(curved.state(0).fade > 0.4f);
        KS_CHECK(curved.state(0).fade < 0.65f);
        const float faded = curved.effectiveTorque(0, 1000.f);
        KS_CHECK(faded > 400.f && faded < 500.f);
    }

    // --- the rear cools with its own numbers -----------------------------
    {
        BrakeThermalModel m;
        BrakeThermalConfig front;
        front.coolingBase = 1.0f;
        front.coolingSpeedFactor = 0.06f;
        BrakeThermalConfig rear = front;
        rear.coolingBase = 0.8f; // TEMPS_REAR: 0.04 / the reference 0.05
        m.setWheelConfig(0, front);
        m.setWheelConfig(2, rear);
        // Heat both corners identically until the clamp holds them at max...
        for (int i = 0; i < 600; ++i) {
            m.update(0.05f, 0, 5000.f, 100.f, 20.f);
            m.update(0.05f, 2, 5000.f, 100.f, 20.f);
        }
        KS_CHECK(m.discTemp(0) > 800.f && m.discTemp(2) > 800.f);
        // ... then let them cool: less airflow makes the rear stay hotter.
        for (int i = 0; i < 100; ++i) {
            m.update(0.05f, 0, 0.f, 0.f, 20.f);
            m.update(0.05f, 2, 0.f, 0.f, 20.f);
        }
        KS_CHECK(m.discTemp(2) > m.discTemp(0) + 1.0f);
    }

    // --- TEMPS_FRONT/TEMPS_REAR reach the vehicle, per axle --------------
    {
        VehicleSimulator v;
        std::string error;
        KS_CHECK(CarPhysicsJson::loadAcBrakeTempsForCarDir(dir.string(), v, &error));
        // Front: the reference profile, so the model's own defaults.
        KS_CHECK_NEAR(v.brakes().wheelConfig(0).coolingBase, 1.0, 1e-4);
        KS_CHECK_NEAR(v.brakes().wheelConfig(0).coolingSpeedFactor, 0.06, 1e-4);
        KS_CHECK_NEAR(v.brakes().wheelConfig(0).padCoupling, 0.15, 1e-4);
        KS_CHECK_NEAR(v.brakes().wheelConfig(0).coolingCoeff, 12.0, 1e-4);
        // Rear: the lower constant airflow term.
        KS_CHECK_NEAR(v.brakes().wheelConfig(2).coolingBase, 0.8, 1e-4);
        // Both wheels of each axle carry the same config.
        KS_CHECK_NEAR(v.brakes().wheelConfig(1).coolingBase, 1.0, 1e-4);
        KS_CHECK_NEAR(v.brakes().wheelConfig(3).coolingBase, 0.8, 1e-4);

        // --- brakes.ini [DATA] ------------------------------------------
        v.loadBrakesFromIni((dir / "data" / "brakes.ini").string());
        KS_CHECK_NEAR(v.brakeMaxTorque(), 3250.0, 1e-3);
        KS_CHECK_NEAR(v.brakeFrontShare(), 0.6, 1e-6);
        KS_CHECK_NEAR(v.handbrakeTorque(), 1000.0, 1e-3);
        KS_CHECK(!v.brakeBiasAdjustable());
    }

    // --- a car that declares nothing keeps the model's defaults ----------
    {
        const fs::path empty = fs::temp_directory_path() / "ks_test_brake_fade_empty";
        fs::remove_all(empty, ec);
        fs::create_directories(empty);
        VehicleSimulator v;
        KS_CHECK(!CarPhysicsJson::loadAcBrakeTempsForCarDir(empty.string(), v));
        KS_CHECK_NEAR(v.brakes().wheelConfig(0).coolingBase, 1.0, 1e-4);
        KS_CHECK_NEAR(v.brakes().wheelConfig(0).coolingSpeedFactor, 0.05, 1e-4);
        KS_CHECK_NEAR(v.brakeMaxTorque(), 0.0, 1e-6);
        KS_CHECK_NEAR(v.handbrakeTorque(), 0.0, 1e-6);
        fs::remove_all(empty, ec);
    }

    fs::remove_all(dir, ec);
    return KS_TEST_RESULT("brake_fade_test");
}
