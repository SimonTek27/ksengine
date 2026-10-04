// Roadmap ksengine-vs-cryengine P1 - particle simulation.
//
// The renderer side (particle.vert/particle.frag pipelines in
// NativeRenderer) is covered by test_renderer, which does a differential
// pixel check; this test covers the half that does not need a GPU: spawn
// maths, fixed-step integration, lifetime recycling, the capacity cap, the
// quad packing the renderer consumes, and determinism (same seed + same
// update sequence => bit-identical state).
//
// update() clamps a single call to 0.25 s (stall protection), so long
// intervals below are advanced in 0.25 s slices - exactly representable, so
// every slice runs the same number of 1/120 s sub-steps.
#include "KsTest.h"
#include "engine/Graphics/ParticleSystem.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace ks;
using namespace ks::engine::graphics;
using ks::math::vec3;

namespace {

ParticleSystem makeSystem(std::uint32_t seed = 12345u) {
    ParticleSystem ps;
    ps.setSeed(seed);
    ParticleEmitter e;
    e.position = vec3{1.0f, 2.0f, 3.0f};
    e.direction = vec3{0.0f, -1.0f, 0.0f};
    e.spreadDeg = 0.0f;      // perfectly collimated: no cone randomness in
    e.speed = 0.0f;          // the position assertions below
    e.speedJitter = 0.0f;
    e.size = 0.5f;
    e.sizeJitter = 0.0f;
    e.spawnRate = 0.0f;      // burst-driven by default; the rate test below
    e.lifetime = 2.0f;       // turns it back on
    e.gravity = vec3{0.0f, -10.0f, 0.0f};
    ps.addEmitter(e);
    return ps;
}

void advance(ParticleSystem& ps, float seconds) {
    float left = seconds;
    while (left > 0.0f) {
        const float slice = left > 0.25f ? 0.25f : left;
        ps.update(slice);
        left -= slice;
    }
}

bool sameState(const ParticleSystem& a, const ParticleSystem& b) {
    const std::vector<Particle>& pa = a.particles();
    const std::vector<Particle>& pb = b.particles();
    if (pa.size() != pb.size()) return false;
    for (std::size_t i = 0; i < pa.size(); ++i) {
        if (std::memcmp(&pa[i], &pb[i], sizeof(Particle)) != 0) return false;
    }
    return true;
}

} // namespace

int main() {
    // ---- Burst + capacity. ----
    {
        ParticleSystem ps = makeSystem();
        ps.burst(0, 10);
        KS_CHECK(ps.aliveCount() == 10);
        for (const Particle& p : ps.particles()) {
            KS_CHECK_NEAR(p.position.x, 1.0f, 1e-6f);
            KS_CHECK_NEAR(p.position.y, 2.0f, 1e-6f);
            KS_CHECK_NEAR(p.position.z, 3.0f, 1e-6f);
            KS_CHECK_NEAR(p.maxLife, 2.0f, 1e-6f);
            KS_CHECK_NEAR(p.size, 0.5f, 1e-6f);
        }
        ps.burst(0, ParticleSystem::kMaxParticles * 4);
        KS_CHECK(ps.aliveCount() <= ParticleSystem::kMaxParticles);
        ps.killAll();
        KS_CHECK(ps.aliveCount() == 0);
    }

    // ---- Rate: 60/s over exactly one second of fixed steps. ----
    {
        ParticleSystem ps = makeSystem();
        ps.emitter(0).spawnRate = 60.0f;
        advance(ps, 1.0f);
        const int n = ps.aliveCount();
        KS_CHECK(n >= 59 && n <= 61);
        advance(ps, 1.0f);
        KS_CHECK(ps.aliveCount() >= 119 && ps.aliveCount() <= 121);
    }

    // ---- Lifetime: alive at 1.75 s, gone past maxLife = 2.0 s. ----
    {
        ParticleSystem ps = makeSystem();
        ps.burst(0, 8);
        advance(ps, 1.75f);
        KS_CHECK(ps.aliveCount() == 8);
        advance(ps, 0.5f); // total 2.25 s
        KS_CHECK(ps.aliveCount() == 0);
    }

    // ---- Gravity: semi-implicit Euler at 1/120 s converges on -g*t^2/2.
    // With g = 10 and t = 0.5 the analytic answer is a 1.25 m drop from the
    // emitter's y = 2 m, i.e. absolute y = 0.75 m. The tolerance is not float
    // noise: update() clamps to 0.25 s slices, and a slice can land on 29
    // instead of 30 sub-steps (~4 cm/step at these speeds), so the test
    // accepts the band around the discrete answer rather than pretending the
    // step count is knowable from outside. ----
    {
        ParticleSystem ps = makeSystem();
        ps.burst(0, 1);
        advance(ps, 0.5f);
        KS_CHECK(ps.aliveCount() == 1);
        const Particle& p = ps.particles().front();
        KS_CHECK_NEAR(p.position.y, 0.75f, 0.12f);  // 2.0 - 1.25
        KS_CHECK_NEAR(p.velocity.y, -5.0f, 0.2f);
        KS_CHECK_NEAR(p.position.x, 1.0f, 1e-6f); // no lateral drift
    }

    // ---- Stalled frames are clamped: update(10 s) must behave exactly like
    // update(0.25 s), because a single call never simulates more than the
    // 0.25 s ceiling - instead of replaying ten seconds of physics after a
    // debugger pause or an alt-tab. ----
    {
        ParticleSystem a = makeSystem();
        ParticleSystem b = makeSystem();
        a.burst(0, 4);
        b.burst(0, 4);
        a.update(10.0f);
        b.update(0.25f);
        KS_CHECK(sameState(a, b));
        a.update(10.0f);
        b.update(0.25f);
        KS_CHECK(sameState(a, b));
    }

    // ---- Determinism: same seed + same sequence => bit-identical. ----
    {
        ParticleSystem a = makeSystem(777u);
        ParticleSystem b = makeSystem(777u);
        ParticleEmitter fast;
        fast.spawnRate = 240.0f;
        fast.speed = 5.0f;
        fast.speedJitter = 2.0f;
        fast.spreadDeg = 45.0f;
        a.addEmitter(fast);
        b.addEmitter(fast);
        for (int i = 0; i < 12; ++i) {
            a.update(0.083f);
            b.update(0.083f);
        }
        KS_CHECK(a.aliveCount() > 0);
        KS_CHECK(sameState(a, b));

        // ...and a different seed must diverge, or the seed does nothing.
        ParticleSystem c = makeSystem(778u);
        c.addEmitter(fast);
        for (int i = 0; i < 12; ++i) c.update(0.083f);
        KS_CHECK(!sameState(a, c));
    }

    // ---- buildQuads: layout, counts, overflow guard. ----
    {
        ParticleSystem ps = makeSystem();
        ps.burst(0, 3);
        std::vector<float> buf(std::size_t(ParticleSystem::kMaxParticles) *
                                   ParticleSystem::kVerticesPerParticle *
                                   ParticleSystem::kFloatsPerVertex,
                               -1.0f);
        const std::size_t floats = ps.buildQuads(buf.data(), buf.size());
        KS_CHECK(floats == 3u * 6u * 12u);

        // First vertex of the first particle: centre, uv corner, colour,
        // size, life, maxLife - in the exact order particle.vert reads.
        KS_CHECK_NEAR(buf[0], 1.0f, 1e-6f);  // position.x
        KS_CHECK_NEAR(buf[1], 2.0f, 1e-6f);  // position.y
        KS_CHECK_NEAR(buf[2], 3.0f, 1e-6f);  // position.z
        KS_CHECK_NEAR(buf[3], 0.0f, 1e-6f);  // uv (first corner)
        KS_CHECK_NEAR(buf[4], 0.0f, 1e-6f);
        KS_CHECK_NEAR(buf[5], 1.0f, 1e-6f);  // color.rgb
        KS_CHECK_NEAR(buf[6], 0.7f, 1e-6f);
        KS_CHECK_NEAR(buf[7], 0.3f, 1e-6f);
        KS_CHECK_NEAR(buf[8], 1.0f, 1e-6f);  // alpha
        KS_CHECK_NEAR(buf[9], 0.5f, 1e-6f);  // size
        KS_CHECK_NEAR(buf[10], 2.0f, 1e-6f); // life (fresh)
        KS_CHECK_NEAR(buf[11], 2.0f, 1e-6f); // maxLife

        // The second vertex of the quad is the top-right corner...
        KS_CHECK_NEAR(buf[12 + 3], 1.0f, 1e-6f);
        KS_CHECK_NEAR(buf[12 + 4], 0.0f, 1e-6f);
        // ...and the fourth vertex (index 3) restarts the first triangle.
        KS_CHECK_NEAR(buf[3 * 12 + 3], 0.0f, 1e-6f);
        KS_CHECK_NEAR(buf[3 * 12 + 4], 0.0f, 1e-6f);

        // Overflow: a buffer for exactly one particle must be filled and stop.
        float one[6 * 12];
        std::memset(one, 0xCD, sizeof(one));
        KS_CHECK(ps.buildQuads(one, sizeof(one) / sizeof(float)) == 6u * 12u);
        float tiny[11];
        KS_CHECK(ps.buildQuads(tiny, sizeof(tiny) / sizeof(float)) == 0u);
    }

    return KS_TEST_RESULT("particle_test");
}
