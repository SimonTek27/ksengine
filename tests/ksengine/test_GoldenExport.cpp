/**
 * Parity 1.2 - golden export round-trip (Qt-free).
 * Records a scripted lap through VehicleSimulator, exports it via
 * PhysicsGolden::saveCsv, reloads it and replays it open-loop.
 * Roadmap target: corr_speed > 0.95 on data matching the physics.
 */
#include "engine/physics/PhysicsGolden.h"
#include "engine/physics/VehicleSimulator.h"
#include <cmath>
#include <cstdio>

namespace {

struct Input {
    double t;
    float throttle, brake, steer;
};

Input scriptedInput(double t) {
    Input i{t, 0.0f, 0.0f, 0.0f};
    if (t < 5.0) {           // launch + straight accel
        i.throttle = 1.0f;
    } else if (t < 7.0) {    // brake into corner
        i.throttle = 0.2f;
        i.brake = 0.3f;
        i.steer = 0.15f;
    } else if (t < 9.0) {    // corner exit
        i.throttle = 0.7f;
        i.steer = -0.10f;
    } else {                 // full throttle again
        i.throttle = 1.0f;
    }
    return i;
}

} // namespace

int main() {
    const char* path = "test_golden_export_roundtrip.csv";
    const double duration = 12.0;
    const double dt = 0.001;
    const double sampleEvery = 0.05;

    ks::physics::PhysicsGolden recorder;
    ks::physics::VehicleSimulator veh;
    veh.reset();
    veh.startSimulation();

    double t = 0.0;
    double nextSample = 0.0;
    while (t < duration) {
        const Input in = scriptedInput(t);
        veh.setThrottle(in.throttle);
        veh.setBrake(in.brake);
        veh.setSteering(in.steer);
        veh.updatePhysics(dt);
        t += dt;
        if (t + 1e-9 >= nextSample) {
            const auto st = veh.getState();
            ks::physics::GoldenSample g;
            g.time = nextSample;
            g.speedMs = static_cast<float>(st.speed);
            g.rpm = static_cast<float>(veh.rpm());
            g.x = st.position.x;
            g.y = st.position.y;
            g.z = st.position.z;
            g.throttle = st.throttle;
            g.brake = st.brake;
            g.steer = st.steering;
            recorder.pushRef(g);
            nextSample += sampleEvery;
        }
    }

    if (recorder.refCount() < 100) {
        std::fprintf(stderr, "FAIL: recorded only %zu samples\n", recorder.refCount());
        return 2;
    }

    if (!recorder.saveCsv(path)) {
        std::fprintf(stderr, "FAIL: saveCsv(%s)\n", path);
        return 3;
    }

    ks::physics::PhysicsGolden golden;
    if (!golden.loadCsv(path)) {
        std::fprintf(stderr, "FAIL: reload %s\n", path);
        return 4;
    }
    if (golden.refCount() != recorder.refCount()) {
        std::fprintf(stderr, "FAIL: round-trip count %zu != %zu\n",
                     golden.refCount(), recorder.refCount());
        return 5;
    }

    // Replay the recorded inputs open-loop into a fresh simulator.
    veh.reset();
    veh.startSimulation();
    double rt = 0.0;
    auto metrics = golden.runOpenLoop([&](const ks::physics::GoldenSample& r) {
        veh.setThrottle(r.throttle);
        veh.setBrake(r.brake);
        veh.setSteering(r.steer);
        while (rt + 1e-4 < r.time) {
            veh.updatePhysics(dt);
            rt += dt;
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

    std::printf("GoldenExport samples=%d mae_speed=%.4f corr_speed=%.4f max_err=%.4f\n",
                metrics.samples, metrics.maeSpeed, metrics.corrSpeed,
                metrics.maxSpeedErr);

    std::remove(path);

    if (metrics.samples < 100) {
        std::fprintf(stderr, "FAIL: too few aligned samples (%d)\n", metrics.samples);
        return 6;
    }
    if (metrics.corrSpeed <= 0.95) {
        std::fprintf(stderr, "FAIL: corr_speed %.4f <= 0.95 (roadmap target)\n",
                     metrics.corrSpeed);
        return 7;
    }
    std::printf("PASS (export round-trip corr_speed > 0.95)\n");
    return 0;
}
