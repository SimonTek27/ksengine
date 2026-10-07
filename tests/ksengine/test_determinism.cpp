/**
 * Parity 1.4 — same inputs + fixed dt → identical trajectory (deterministic).
 * Qt-free. Two independent VehicleSimulator runs must match bit-for-bit on key state.
 */
#include "engine/physics/TrackSurface.h"
#include "engine/physics/VehicleSimulator.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

struct Snapshot {
    float x, y, z;
    float speed;
    float rpm;
    int gear;
};

Snapshot capture(const ks::physics::VehicleSimulator& v) {
    const auto st = v.getState();
    Snapshot s;
    s.x = static_cast<float>(st.position.x);
    s.y = static_cast<float>(st.position.y);
    s.z = static_cast<float>(st.position.z);
    s.speed = static_cast<float>(st.speed);
    s.rpm = static_cast<float>(v.rpm());
    s.gear = v.currentGear();
    return s;
}

uint64_t hashSnap(const Snapshot& s) {
    // FNV-1a over raw bytes
    uint64_t h = 14695981039346656037ull;
    const auto* p = reinterpret_cast<const unsigned char*>(&s);
    for (size_t i = 0; i < sizeof(s); ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

bool nearlyEq(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

bool same(const Snapshot& a, const Snapshot& b) {
    return nearlyEq(a.x, b.x) && nearlyEq(a.y, b.y) && nearlyEq(a.z, b.z)
        && nearlyEq(a.speed, b.speed) && nearlyEq(a.rpm, b.rpm)
        && a.gear == b.gear;
}

void runScript(ks::physics::VehicleSimulator& v, int steps) {
    v.reset();
    // TrackSurface is a mutable shared singleton (rubber trails deposit into
    // it while driving): give each run the same fresh environment, otherwise
    // run A's deposits shift run B's sampled grip and the runs legitimately
    // diverge.
    ks::physics::TrackSurface::instance().configure();
    v.startSimulation();
    const double dt = 0.001;
    for (int i = 0; i < steps; ++i) {
        // Deterministic open-loop profile (no RNG)
        const float t = static_cast<float>(i) * static_cast<float>(dt);
        float thr = 0.f, brk = 0.f, str = 0.f;
        if (t < 2.0f) thr = 0.8f;
        else if (t < 3.0f) { thr = 0.2f; brk = 0.4f; str = 0.15f; }
        else if (t < 5.0f) { thr = 0.9f; str = -0.1f; }
        else thr = 0.5f;
        v.setThrottle(thr);
        v.setBrake(brk);
        v.setSteering(str);
        v.updatePhysics(dt);
    }
}

} // namespace

int main() {
    constexpr int kSteps = 5000; // 5 s @ 1 kHz

    ks::physics::VehicleSimulator a, b;
    runScript(a, kSteps);
    runScript(b, kSteps);

    const Snapshot sa = capture(a);
    const Snapshot sb = capture(b);
    const uint64_t ha = hashSnap(sa);
    const uint64_t hb = hashSnap(sb);

    std::printf("determinism: hash_a=%016llx hash_b=%016llx speed=%.4f gear=%d\n",
                static_cast<unsigned long long>(ha),
                static_cast<unsigned long long>(hb),
                sa.speed, sa.gear);

    if (ha != hb || !same(sa, sb)) {
        std::fprintf(stderr,
            "FAIL: non-deterministic\n"
            "  A: x=%.6f y=%.6f z=%.6f spd=%.6f rpm=%.2f gear=%d\n"
            "  B: x=%.6f y=%.6f z=%.6f spd=%.6f rpm=%.2f gear=%d\n",
            sa.x, sa.y, sa.z, sa.speed, sa.rpm, sa.gear,
            sb.x, sb.y, sb.z, sb.speed, sb.rpm, sb.gear);
        return 1;
    }

    std::printf("PASS: two runs identical after %d steps\n", kSteps);
    return 0;
}
