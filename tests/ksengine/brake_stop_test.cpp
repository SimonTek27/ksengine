/**
 * 100-0 km/h braking distance with the Jaguar XJ220's own brake data:
 *  - the open-loop run really reaches 100 km/h and stops
 *  - the distance is printed, so a regression shows its number, not only a red
 *    check
 *  - the cold table fades the discs: the stop gets longer than with a flat,
 *    "already warm" table, so the curve is visible in metres
 *
 * The torque ceiling, the axle split and the table come from files shaped
 * exactly like the converted car's, so this measures the pipeline rather than a
 * hand-tuned vehicle. Set KS_BRAKE_TRACE=1 to print the stop every second.
 */
#include "KsTest.h"
#include "engine/physics/BrakeThermalModel.h"
#include "engine/physics/TrackSurface.h"
#include "engine/physics/VehicleSimulator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using ks::physics::BrakeThermalModel;
using ks::physics::TrackSurface;
using ks::physics::VehicleSimulator;

namespace {

constexpr double kTargetSpeedMs = 100.0 / 3.6; // 100 km/h
// Effectively stopped (2.9 km/h). A stopped car in gear does not stay put in
// this model: the negative coast torque keeps acting with no clutch, so it
// creeps backwards. The dip is the stop; the reversal after it is not the test's
// subject (see the guard in the loop).
constexpr double kStopSpeedMs = 0.8;

// Whatever the converter writes, both axles.
const char* kBrakeLut =
    "0|0.3409\r\n400|1\r\n900|0.9659\r\n1200|0.5909\r\n2000|0.3409\r\n";

const char* kBrakesIni =
    "[HEADER]\r\n"
    "VERSION=2\r\n"
    "\r\n"
    "[DATA]\r\n"
    "MAX_TORQUE=3250\r\n"
    "FRONT_SHARE=0.6\r\n"
    "HANDBRAKE_TORQUE=1000\r\n"
    "COCKPIT_ADJUSTABLE=0\r\n";

struct StopResult {
    double v0 = 0.0;
    double distance = 0.0;
    double stopTime = 0.0;
    double positionDistance = 0.0;
    float peakDiscTemp = 0.f;
    float endFade = 0.f;
    bool reached = false;
    bool stopped = false;
};

std::vector<std::pair<float, float>> flatCurve() {
    // "Already warm": no fade at any temperature. Only the curve differs from
    // the cold run, everything else is identical.
    return {{0.f, 1.f}, {2000.f, 1.f}};
}

StopResult stopFrom100(const std::vector<std::pair<float, float>>& curve,
                        const std::string& brakesIni)
{
    TrackSurface& surf = TrackSurface::instance();
    surf.configure();
    surf.setBaseGrip(1.0f);
    surf.setWetness(0.0f);

    VehicleSimulator v;
    v.reset();
    v.setMass(1372.0); // data.json specs.weight
    v.setWheelRadius(0.33);
    if (!brakesIni.empty()) v.loadBrakesFromIni(brakesIni);
    v.setBrakeTempCurves(curve, curve);
    v.startSimulation();

    const double dt = 0.001;
    StopResult r;
    for (int i = 0; i < 40000; ++i) {
        v.setThrottle(0.85);
        v.setBrake(0.0);
        v.setSteering(0.0);
        v.updatePhysics(dt);
        if (v.getState().speed >= kTargetSpeedMs) {
            r.reached = true;
            break;
        }
    }
    if (!r.reached) return r;

    r.v0 = v.getState().speed;
    const auto startPosition = v.getState().position;
    // Distance from the model's own speed, not from state().position: the
    // position is nudged by the damage cg shift on every step, so differencing
    // it accumulates a drift that has nothing to do with the stop.
    double travelled = 0.0;
    double previous = v.getState().speed;
    double minSpeed = previous;
    int step = 0;
    for (; step < 20000; ++step) {
        v.setThrottle(0.0);
        v.setBrake(1.0);
        v.setSteering(0.0);
        v.updatePhysics(dt);
        const auto st = v.getState();
        travelled += 0.5 * (previous + st.speed) * dt;
        previous = st.speed;
        for (int w = 0; w < 4; ++w)
            r.peakDiscTemp = std::max(r.peakDiscTemp, v.brakes().discTemp(w));
        if (std::getenv("KS_BRAKE_TRACE") && step % 1000 == 0) {
            std::printf("  t=%5.2f s speed=%7.3f pos=(%8.2f,%8.2f) brake=%.2f fade=%.3f"
                        " T=%5.0f gear=%d rpm=%6.0f travelled=%7.1f\n",
                        step * dt, st.speed, st.position.x, st.position.z, st.brake,
                        v.brakes().fade(0), v.brakes().discTemp(0), st.gear, st.rpm,
                        travelled);
        }
        minSpeed = std::min(minSpeed, static_cast<double>(st.speed));
        if (st.speed < kStopSpeedMs || (minSpeed < 2.0 && st.speed > minSpeed + 0.5)) {
            r.stopped = true;
            break;
        }
    }
    r.distance = travelled;
    {
        const auto endPosition = v.getState().position;
        const double dx = endPosition.x - startPosition.x;
        const double dz = endPosition.z - startPosition.z;
        r.positionDistance = std::sqrt(dx * dx + dz * dz);
    }
    r.stopTime = step * dt;
    r.endFade = v.brakes().fade(0);
    return r;
}

} // namespace

int main()
{
    const fs::path dir = fs::temp_directory_path() / "ks_test_brake_stop";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "data");
    {
        std::ofstream out(dir / "data" / "brakes.ini", std::ios::trunc);
        out << kBrakesIni;
    }
    std::vector<std::pair<float, float>> cold;
    {
        std::ofstream out(dir / "data" / "brake_temp_front.lut", std::ios::trunc);
        out << kBrakeLut;
    }
    KS_CHECK(BrakeThermalModel::loadPerfCurve(
        (dir / "data" / "brake_temp_front.lut").string(), cold));
    const std::string brakesIni = (dir / "data" / "brakes.ini").string();

    const StopResult coldRun = stopFrom100(cold, brakesIni);
    const StopResult warmRun = stopFrom100(flatCurve(), brakesIni);

    const double avgDecel = coldRun.distance > 0.0
                                ? (coldRun.v0 * coldRun.v0) / (2.0 * coldRun.distance)
                                : 0.0;
    std::printf("brake_stop: cold=%.1f m in %.2f s (v0=%.2f m/s, a=%.2f m/s2,"
                " Tmax=%.0f C, fade=%.2f) warm=%.1f m\n",
                coldRun.distance, coldRun.stopTime, coldRun.v0, avgDecel,
                coldRun.peakDiscTemp, coldRun.endFade, warmRun.distance);
    std::printf("brake_stop: reached=%d/%d stopped=%d/%d\n", coldRun.reached ? 1 : 0,
                warmRun.reached ? 1 : 0, coldRun.stopped ? 1 : 0, warmRun.stopped ? 1 : 0);

    std::printf("brake_stop: position-based distance: cold=%.1f m warm=%.1f m\n",
                coldRun.positionDistance, warmRun.positionDistance);

    // The script really drives the car: 100 km/h reached, then a stop.
    KS_CHECK(coldRun.reached);
    KS_CHECK(warmRun.reached);
    KS_CHECK(coldRun.v0 >= kTargetSpeedMs);
    KS_CHECK(coldRun.stopped);
    KS_CHECK(warmRun.stopped);
    KS_CHECK(coldRun.distance > 0.0);
    // state().position follows the stop again: the damage cg shift is folded in
    // as a delta, so differencing it no longer drifts away from the real path.
    KS_CHECK(std::fabs(coldRun.positionDistance - coldRun.distance) <
             std::max(1.0, 0.02 * coldRun.distance));

    // A 1992 hypercar with cold, fading brakes: no 30 m miracle, no 200 m slide.
    KS_CHECK(coldRun.distance > 30.0);
    KS_CHECK(coldRun.distance < 120.0);
    KS_CHECK(avgDecel > 4.0);
    KS_CHECK(avgDecel < 12.0);

    // The discs heat up during the stop...
    KS_CHECK(coldRun.peakDiscTemp > 90.0f);
    KS_CHECK(coldRun.endFade > 0.1f);
    // ... and that fade costs metres: the same car with a warm table stops
    // shorter. This is the curve reaching the tyres, measured.
    KS_CHECK(warmRun.distance > 0.0);
    KS_CHECK(coldRun.distance > warmRun.distance);
    KS_CHECK(coldRun.distance - warmRun.distance > 1.0);

    fs::remove_all(dir, ec);
    return KS_TEST_RESULT("brake_stop_test");
}
