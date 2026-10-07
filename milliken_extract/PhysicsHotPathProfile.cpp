/**
 * ksphprofile — CPU hot-path benchmark for ksengine tire / vehicle physics.
 *
 * Measures:
 *   1) KsTireModel::calculateForces (cold vs warm Fz cache)
 *   2) KsTireModel::calculateCombinedSlipBatch (4 wheels)
 *   3) TireSimulator::update (1 car and N cars)
 *   4) Scaled estimate for 1 kHz sim budget
 *
 * Usage:
 *   ksphprofile [--cars N] [--steps S] [--warmup W] [--csv path]
 * Defaults: cars=16 steps=10000 warmup=500
 */
#include "KsTireModel.h"
#include "TireSimulator.h"
#include "PhysicsProfiler.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <fstream>

using clock_type = std::chrono::steady_clock;

static double nsSince(clock_type::time_point t0)
{
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now() - t0).count());
}

struct BenchResult {
    const char* name = "";
    int iterations = 0;
    double totalNs = 0;
    double minNs = 0;
    double maxNs = 0;
    // optional payload to defeat DCE
    double sink = 0;
};

static void printResult(const BenchResult& r)
{
    const double avgNs = r.iterations > 0 ? r.totalNs / r.iterations : 0.0;
    const double avgUs = avgNs / 1e3;
    const double totalMs = r.totalNs / 1e6;
    std::printf("  %-36s  iters=%7d  total=%8.2f ms  avg=%8.3f us  min=%7.2f ns  max=%7.2f ns\n",
                r.name, r.iterations, totalMs, avgUs, r.minNs, r.maxNs);
}

static BenchResult benchForcesWarm(ks::physics::KsTireModel& model, int steps)
{
    ks::physics::KsTireModel::TireState s;
    s.normalForce = 4000.f;
    s.frictionCoefficient = 1.f;
    s.tireTemp = 80.f;
    s.tirePressure = 26.f;
    // warm cache
    s.slipAngle = 0.05f;
    (void)model.calculateForces(s);

    BenchResult r;
    r.name = "KsTire.calculateForces (warm cache)";
    r.iterations = steps;
    r.minNs = 1e300;
    r.maxNs = 0;
    double sink = 0;
    for (int i = 0; i < steps; ++i) {
        s.slipAngle = 0.02f + 0.001f * static_cast<float>(i % 50);
        s.slipRatio = 0.01f + 0.0005f * static_cast<float>(i % 40);
        const auto t0 = clock_type::now();
        auto f = model.calculateForces(s);
        const double ns = nsSince(t0);
        r.totalNs += ns;
        if (ns < r.minNs) r.minNs = ns;
        if (ns > r.maxNs) r.maxNs = ns;
        sink += f.lateralForce + f.longitudinalForce;
    }
    r.sink = sink;
    return r;
}

static BenchResult benchForcesCold(ks::physics::KsTireModel& model, int steps)
{
    BenchResult r;
    r.name = "KsTire.calculateForces (cold Fz)";
    r.iterations = steps;
    r.minNs = 1e300;
    r.maxNs = 0;
    double sink = 0;
    ks::physics::KsTireModel::TireState s;
    s.frictionCoefficient = 1.f;
    s.tireTemp = 80.f;
    s.tirePressure = 26.f;
    for (int i = 0; i < steps; ++i) {
        // Force cache miss every call
        s.normalForce = 2000.f + static_cast<float>((i * 97) % 5000);
        s.slipAngle = 0.05f;
        s.slipRatio = 0.03f;
        model.invalidateCache();
        const auto t0 = clock_type::now();
        auto f = model.calculateForces(s);
        const double ns = nsSince(t0);
        r.totalNs += ns;
        if (ns < r.minNs) r.minNs = ns;
        if (ns > r.maxNs) r.maxNs = ns;
        sink += f.lateralForce;
    }
    r.sink = sink;
    return r;
}

static BenchResult benchBatch(ks::physics::KsTireModel& model, int steps)
{
    BenchResult r;
    r.name = "KsTire.calculateCombinedSlipBatch×4";
    r.iterations = steps;
    r.minNs = 1e300;
    r.maxNs = 0;
    float sa[4] = {0.08f, 0.08f, 0.04f, 0.04f};
    float sr[4] = {0.02f, 0.02f, 0.05f, 0.05f};
    float load[4] = {3500.f, 3500.f, 4500.f, 4500.f};
    float cam[4] = {-0.03f, -0.03f, -0.02f, -0.02f};
    ks::physics::KsTireModel::TireForces out[4];
    double sink = 0;
    for (int i = 0; i < steps; ++i) {
        sa[0] = 0.05f + 0.0001f * static_cast<float>(i % 100);
        const auto t0 = clock_type::now();
        model.calculateCombinedSlipBatch(sa, sr, load, cam, 1.0f, 26.f, 80.f, out);
        const double ns = nsSince(t0);
        r.totalNs += ns;
        if (ns < r.minNs) r.minNs = ns;
        if (ns > r.maxNs) r.maxNs = ns;
        sink += out[0].lateralForce + out[3].longitudinalForce;
    }
    r.sink = sink;
    return r;
}

static BenchResult benchTireSim(int cars, int steps, std::string& nameStorage)
{
    std::vector<ks::physics::TireSimulator> sims(static_cast<size_t>(cars));
    std::array<float, 4> loads = {3500.f, 3500.f, 4500.f, 4500.f};
    BenchResult r;
    nameStorage = "TireSimulator::update ×" + std::to_string(cars) + " cars";
    r.name = nameStorage.c_str();
    r.iterations = steps;
    r.minNs = 1e300;
    r.maxNs = 0;
    double sink = 0;
    for (int i = 0; i < steps; ++i) {
        const float speed = 20.f + 0.01f * static_cast<float>(i % 200);
        const auto t0 = clock_type::now();
        for (int c = 0; c < cars; ++c) {
            sims[static_cast<size_t>(c)].update(
                0.001f, speed, 0.1f, 0.05f, 0.4f, 0.1f, loads, 1.0f, 800.f, 200.f);
        }
        const double ns = nsSince(t0);
        r.totalNs += ns;
        if (ns < r.minNs) r.minNs = ns;
        if (ns > r.maxNs) r.maxNs = ns;
        sink += sims[0].totalLateralForce();
    }
    r.sink = sink;
    return r;
}

static void writeCsv(const std::string& path, const std::vector<BenchResult>& results)
{
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr, "Cannot write CSV %s\n", path.c_str());
        return;
    }
    out << "name,iterations,total_ms,avg_us,min_ns,max_ns\n";
    for (const auto& r : results) {
        const double avgNs = r.iterations > 0 ? r.totalNs / r.iterations : 0.0;
        out << '"' << r.name << '"' << ','
            << r.iterations << ','
            << (r.totalNs / 1e6) << ','
            << (avgNs / 1e3) << ','
            << r.minNs << ','
            << r.maxNs << '\n';
    }
    std::printf("CSV written: %s\n", path.c_str());
}

int main(int argc, char** argv)
{
    int cars = 16;
    int steps = 10000;
    int warmup = 500;
    std::string csvPath;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> const char* {
            if (a == flag && i + 1 < argc) return argv[++i];
            return nullptr;
        };
        if (const char* v = need("--cars")) cars = std::atoi(v);
        else if (const char* v = need("--steps")) steps = std::atoi(v);
        else if (const char* v = need("--warmup")) warmup = std::atoi(v);
        else if (const char* v = need("--csv")) csvPath = v;
        else if (a == "-h" || a == "--help") {
            std::fprintf(stderr,
                "ksphprofile — physics hot-path benchmark\n"
                "  --cars N     concurrent cars for TireSimulator (default 16)\n"
                "  --steps S    timed iterations (default 10000)\n"
                "  --warmup W   warmup iterations (default 500)\n"
                "  --csv PATH   write results CSV\n");
            return 0;
        }
    }
    if (cars < 1) cars = 1;
    if (steps < 100) steps = 100;

    std::printf("ksphprofile  cars=%d  steps=%d  warmup=%d\n", cars, steps, warmup);
    std::printf("budget @ 1 kHz: 1000 us/frame total; tire share target << 200 us/car\n\n");

    auto& prof = ks::physics::PhysicsProfiler::instance();
    prof.setEnabled(true);
    prof.reset();

    ks::physics::KsTireModel model;

    // Warmup
    {
        auto w1 = benchForcesWarm(model, warmup);
        auto w2 = benchBatch(model, warmup);
        std::string nw;
        auto w3 = benchTireSim(1, warmup / 10 > 0 ? warmup / 10 : 50, nw);
        (void)w1;
        (void)w2;
        (void)w3;
    }
    prof.reset();

    std::vector<BenchResult> results;
    std::string name1, nameN;
    results.push_back(benchForcesWarm(model, steps));
    results.push_back(benchForcesCold(model, steps));
    results.push_back(benchBatch(model, steps));
    results.push_back(benchTireSim(1, steps, name1));
    results.push_back(benchTireSim(cars, steps, nameN));

    // Profiled TireSimulator path via PhysicsProfiler sections
    {
        ks::physics::TireSimulator sim;
        std::array<float, 4> loads = {3500.f, 3500.f, 4500.f, 4500.f};
        for (int i = 0; i < steps; ++i) {
            PROFILE_FRAME();
            {
                PROFILE_SUBSYSTEM(ks::physics::PhysicsProfiler::Tires);
                PROFILE_SECTION("TireSimulator.update");
                sim.update(0.001f, 25.f, 0.12f, 0.04f, 0.5f, 0.0f, loads, 1.0f, 900.f, 0.f);
            }
            PROFILE_END_FRAME();
        }
    }

    std::printf("-- Microbenchmarks --\n");
    for (const auto& r : results) printResult(r);

    // Budget analysis
    std::printf("\n-- 1 kHz budget analysis --\n");
    for (const auto& r : results) {
        if (std::strstr(r.name, "TireSimulator") == nullptr) continue;
        const double avgUs = r.iterations > 0 ? (r.totalNs / r.iterations) / 1e3 : 0.0;
        const double pct = avgUs / 1000.0 * 100.0; // vs 1 ms frame
        std::printf("  %-36s  avg %.3f us/step  → %.1f%% of 1 ms frame\n",
                    r.name, avgUs, pct);
    }

    // Batch cost per car equivalent
    for (const auto& r : results) {
        if (std::strstr(r.name, "Batch") != nullptr) {
            const double avgUs = r.iterations > 0 ? (r.totalNs / r.iterations) / 1e3 : 0.0;
            std::printf("  tire MF batch×4 only             avg %.3f us  (excl. slip/chassis)\n", avgUs);
        }
    }

    std::printf("\n");
    prof.report(stdout);

    if (!csvPath.empty()) writeCsv(csvPath, results);

    // Prevent DCE of sinks
    double sink = 0;
    for (const auto& r : results) sink += r.sink;
    if (sink == 1e300) std::printf("%f\n", sink);

    return 0;
}
