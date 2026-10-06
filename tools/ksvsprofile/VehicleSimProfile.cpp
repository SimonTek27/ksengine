/**
 * ksvsprofile — full VehicleSimulator::updatePhysics profiling (1 kHz step).
 *
 * Links VehicleSimulator + deps; drives throttle/steer patterns;
 * reports PhysicsProfiler section breakdown + wall-clock stats.
 *
 * Usage:
 *   ksvsprofile [--cars N] [--steps S] [--csv path]
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

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* f) -> const char* {
            if (a == f && i + 1 < argc) return argv[++i];
            return nullptr;
        };
        if (const char* v = need("--cars")) cars = std::atoi(v);
        else if (const char* v = need("--steps")) steps = std::atoi(v);
        else if (const char* v = need("--csv")) csvPath = v;
        else if (a == "-h" || a == "--help") {
            std::fprintf(stderr,
                "ksvsprofile — VehicleSimulator full-step benchmark\n"
                "  --cars N   concurrent vehicles (default 1)\n"
                "  --steps S  physics steps @ 1 ms (default 20000)\n"
                "  --csv PATH write wall-clock summary CSV\n");
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
        s.startSimulation();
        s.setThrottle(0.55);
        s.setBrake(0.0);
        s.setSteering(0.15);
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
        // mild input modulation
        const float t = static_cast<float>(i) * 0.001f;
        const double steer = 0.12 * std::sin(t * 1.7);
        const double throttle = 0.45 + 0.25 * std::sin(t * 0.9);
        for (auto& s : sims) {
            s.setSteering(steer);
            s.setThrottle(throttle);
        }

        const auto t0 = clock_type::now();
        for (auto& s : sims) s.updatePhysics(0.001);
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
