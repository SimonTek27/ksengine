/**
 * ksvsprofile — full VehicleSimulator::updatePhysics profiling (1 kHz step).
 *
 * Links VehicleSimulator + deps; drives throttle/steer patterns;
 * reports PhysicsProfiler section breakdown + wall-clock stats.
 *
 * Usage:
 *   ksvsprofile [--cars N] [--steps S] [--csv path] [--car content/cars/<car>]
 *
 * --car loads a real car folder through the same VehicleSimulator::loadVehicleParams
 * the sim uses, then starts it, so the step cost and the result are the car's own.
 */
#include "VehicleSimulator.h"
#include "PhysicsProfiler.h"
#include "TrackSurface.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using clock_type = std::chrono::steady_clock;

static double nsSince(clock_type::time_point t0)
{
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now() - t0).count());
}

int main(int argc, char** argv)
{
    int cars = 1;
    int steps = 20000;
    std::string csvPath;
    std::string carPath;
    bool flatOut = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* f) -> const char* {
            if (a == f && i + 1 < argc) return argv[++i];
            return nullptr;
        };
        if (const char* v = need("--cars")) cars = std::atoi(v);
        else if (const char* v = need("--steps")) steps = std::atoi(v);
        else if (const char* v = need("--csv")) csvPath = v;
        else if (const char* v = need("--car")) carPath = v;
        else if (a == "--flat-out") flatOut = true;
        else if (a == "-h" || a == "--help") {
            std::fprintf(stderr,
                "ksvsprofile — VehicleSimulator full-step benchmark\n"
                "  --cars N   concurrent vehicles (default 1)\n"
                "  --steps S  physics steps @ 1 ms (default 20000)\n"
                "  --csv PATH write wall-clock summary CSV\n"
                "  --car DIR  load a real car folder before the run\n"
                "  --flat-out full throttle, no steering (engine comparison)\n");
            return 0;
        }
    }
    if (cars < 1) cars = 1;
    if (steps < 100) steps = 100;

    std::printf("ksvsprofile  cars=%d  steps=%d  dt=0.001\n", cars, steps);
    std::printf("budget @ 1 kHz: 1000 us/frame\n\n");

    ks::physics::TrackSurface::instance().configure(128, 2000.f);
    ks::physics::TrackSurface::instance().setBaseGrip(1.0f);

    std::vector<ks::physics::VehicleSimulator> sims(static_cast<size_t>(cars));
    for (auto& s : sims) {
        // Load first: startSimulation() resets the state, so a car started before
        // it would be switched off again.
        if (!carPath.empty()) s.loadVehicleParams(carPath);
        s.startSimulation();
        s.setThrottle(0.55);
        s.setBrake(0.0);
        s.setSteering(0.15);
    }

    // Who fires it comes from the car: an F1 declares a crew start (no onboard
    // starter), so the tool asks the team in the box, not the key.
    const bool crewStart = !carPath.empty() && sims[0].engineStartConfig().startedByCrew;
    if (!carPath.empty()) {
        for (auto& s : sims) {
            s.setIgnition(true);
            if (crewStart) s.pressStarterByCrew(0.0);
            else s.pressStarter(0.0);
        }
        // Cranking is real time: step a couple of seconds so the numbers below
        // are a running engine's, not a starter's.
        for (int i = 0; i < 2000; ++i)
            for (auto& s : sims) s.updatePhysics(0.001);
        std::printf("car: %s\n", carPath.c_str());
        std::printf("  gears=%d finalDrive=%.2f wheelRadius=%.3f running=%s\n",
                    static_cast<int>(sims[0].gearRatios().size()), sims[0].finalDrive(),
                    sims[0].wheelRadius(), sims[0].isEngineRunning() ? "yes" : "no");
        std::printf("  started by: %s\n", crewStart ? "crew (box)" : "driver (key)");
    }

    auto& prof = ks::physics::PhysicsProfiler::instance();
    prof.setEnabled(true);
    prof.reset();

    // Warmup
    for (int i = 0; i < 200; ++i) {
        for (auto& s : sims) s.updatePhysics(0.001);
    }
    prof.reset();

    double totalNs = 0;
    double minNs = 1e300, maxNs = 0;
    double sink = 0;

    for (int i = 0; i < steps; ++i) {
        // mild input modulation; --flat-out is the engine comparison: full
        // throttle, no steering, so the number is the drivetrain's own.
        const float t = static_cast<float>(i) * 0.001f;
        const double steer = flatOut ? 0.0 : 0.12 * std::sin(t * 1.7);
        const double throttle = flatOut ? 1.0 : 0.45 + 0.25 * std::sin(t * 0.9);
        for (auto& s : sims) {
            s.setSteering(steer);
            s.setThrottle(throttle);
        }

        // The profiler frame here is the STEP (this tool's "frame"): the
        // per-step pair used to live inside VehicleSimulator::updatePhysics,
        // which would corrupt the sim frame clock at runtime (gate 2.14).
        PROFILE_FRAME();
        const auto t0 = clock_type::now();
        for (auto& s : sims) s.updatePhysics(0.001);
        PROFILE_END_FRAME();
        const double ns = nsSince(t0);
        totalNs += ns;
        if (ns < minNs) minNs = ns;
        if (ns > maxNs) maxNs = ns;
        sink += sims[0].getState().speed;
    }

    const double avgUs = (totalNs / steps) / 1e3;
    const double avgUsPerCar = avgUs / static_cast<double>(cars);
    const double pctFrame = avgUs / 1000.0 * 100.0;

    std::printf("-- Wall clock --\n");
    std::printf("  VehicleSimulator::updatePhysics ×%d cars\n", cars);
    std::printf("  steps=%d  total=%.2f ms  avg=%.3f us/step  (%.3f us/car)\n",
                steps, totalNs / 1e6, avgUs, avgUsPerCar);
    std::printf("  min=%.0f ns  max=%.0f ns\n", minNs, maxNs);
    std::printf("  share of 1 ms frame: %.2f%%\n", pctFrame);
    std::printf("  sink speed=%.2f m/s (DCE guard)\n\n", sims[0].getState().speed);
    if (!carPath.empty()) {
        const auto& st = sims[0].getState();
        std::printf("  car result: speed=%.2f m/s (%.1f km/h) rpm=%.0f gear=%d\n\n",
                    st.speed, st.speed * 3.6, sims[0].rpm(), sims[0].currentGear());
        // What the engine actually delivers at that rpm, and whether the rear
        // axle is spinning: a flat-out run measures the engine only when the
        // tyres are not the limit.
        std::printf("  torque at rpm=%.0f Nm  rear slip=%.3f\n",
                    sims[0].torqueAtRpm(sims[0].rpm()), sims[0].slipRatio(2));
    }

    // Scale estimate
    for (int n : {1, 4, 8, 16, 32}) {
        const double est = avgUsPerCar * n;
        std::printf("  estimate %2d cars: %.2f us/step (%.1f%% of 1 ms)\n",
                    n, est, est / 10.0);
    }

    std::printf("\n");
    prof.report(stdout);

    if (!csvPath.empty()) {
        std::ofstream out(csvPath);
        out << "cars,steps,avg_us_step,avg_us_car,min_ns,max_ns,pct_1ms_frame\n";
        out << cars << ',' << steps << ',' << avgUs << ',' << avgUsPerCar << ','
            << minNs << ',' << maxNs << ',' << pctFrame << '\n';
        // section rows
        for (const auto& s : prof.sectionsSorted()) {
            out << "section," << s.name << ',' << s.avgUs << ',' << s.totalMs << ','
                << s.hits << ",,\n";
        }
        std::printf("CSV written: %s\n", csvPath.c_str());
    }

    if (sink == 1e300) std::printf("%f\n", sink);
    return 0;
}
