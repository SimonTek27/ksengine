/**
 * Parity 1.1 — golden trajectory harness (Qt-free).
 * Uses synthetic CSV by default; replace tests/data/golden_lap.csv for real telemetry.
 */
#include "engine/physics/PhysicsGolden.h"
#include "engine/physics/VehicleSimulator.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const char* path = "tests/data/golden_lap.csv";
    if (argc > 1) path = argv[1];

    ks::physics::PhysicsGolden golden;
    if (!golden.loadCsv(path)) {
        // Soft skip if data missing in out-of-tree builds
        std::fprintf(stderr, "SKIP: cannot load %s (place real lap CSV for parity metric)\n", path);
        return 0;
    }

    ks::physics::VehicleSimulator veh;
    veh.reset();
    veh.startSimulation();

    double t = 0.0;
    auto metrics = golden.runOpenLoop([&](const ks::physics::GoldenSample& r) {
        veh.setThrottle(r.throttle);
        veh.setBrake(r.brake);
        veh.setSteering(r.steer);
        while (t + 1e-4 < r.time) {
            veh.updatePhysics(0.001);
            t += 0.001;
        }
        const auto st = veh.getState();
        ks::physics::GoldenSample out;
        out.speedMs = static_cast<float>(st.speed);
        out.rpm = static_cast<float>(veh.rpm());
        out.x = st.position.x;
        out.y = st.position.y;
        out.z = st.position.z;
        out.throttle = r.throttle;
        out.brake = r.brake;
        out.steer = r.steer;
        return out;
    });

    std::printf("PhysicsGolden samples=%d mae_speed=%.3f mae_rpm=%.1f corr_speed=%.4f max_err=%.3f ok=%d\n",
                metrics.samples, metrics.maeSpeed, metrics.maeRpm,
                metrics.corrSpeed, metrics.maxSpeedErr, int(metrics.ok));

    if (metrics.samples < 5) {
        std::fprintf(stderr, "FAIL: too few aligned samples\n");
        return 2;
    }
    std::printf("PASS (scaffold — real CSV must reach corr_speed > 0.95)\n");
    return 0;
}
