#include "ParticleSystem.h"

#include <algorithm>
#include <cmath>

namespace ks {
namespace engine {
namespace graphics {

namespace {
constexpr float kPi = 3.14159265f;
// Spiral-of-death guard: 2 s of simulated time per call. Fixed steps cost
// microseconds each, so the cap is only there to stop a debugger pause (or
// an alt-tab) from replaying minutes of particle physics in one frame.
constexpr int kMaxStepsPerUpdate = 240;
constexpr float kMaxDt = 0.25f; // a stall longer than this is clamped, not replayed
} // namespace

void ParticleSystem::clear() {
    m_emitters.clear();
    m_spawnAcc.clear();
    m_particles.clear();
    m_accum = 0.0f;
    m_rng = 0x9E3779B9u;
}

int ParticleSystem::addEmitter(const ParticleEmitter& emitter) {
    m_emitters.push_back(emitter);
    m_spawnAcc.push_back(0.0f);
    return static_cast<int>(m_emitters.size()) - 1;
}

float ParticleSystem::rand01() {
    // xorshift32 -> 24 bits of mantissa, good enough for effects and - the
    // point of it - identical on every run and every platform.
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>((m_rng >> 8) & 0x00FFFFFFu) / 16777216.0f;
}

void ParticleSystem::update(float dt) {
    if (dt <= 0.0f) return;
    if (dt > kMaxDt) dt = kMaxDt;
    m_accum += dt;
    int steps = 0;
    while (m_accum >= kFixedStep && steps < kMaxStepsPerUpdate) {
        step(kFixedStep);
        m_accum -= kFixedStep;
        ++steps;
    }
    if (steps == kMaxStepsPerUpdate) m_accum = 0.0f;
}

void ParticleSystem::burst(int emitterIndex, int count) {
    if (emitterIndex < 0 || emitterIndex >= emitterCount() || count <= 0) return;
    for (int i = 0; i < count; ++i) spawn(emitterIndex);
}

void ParticleSystem::spawn(int emitterIndex) {
    if (static_cast<int>(m_particles.size()) >= kMaxParticles) return;
    const ParticleEmitter& e = m_emitters[static_cast<std::size_t>(emitterIndex)];

    // Uniform direction inside the emission cone: cos of the angle is drawn
    // uniformly in [cos(spread), 1], which is uniform over the spherical cap,
    // then rotated around the axis by a random azimuth.
    ks::math::vec3 axis = e.direction.normalized();
    if (axis.length() <= 0.0f) axis = ks::math::vec3{0.0f, -1.0f, 0.0f};
    const ks::math::vec3 up =
        std::fabs(axis.y) < 0.99f ? ks::math::vec3{0.0f, 1.0f, 0.0f} : ks::math::vec3{1.0f, 0.0f, 0.0f};
    const ks::math::vec3 tangent = up.cross(axis).normalized();
    const ks::math::vec3 bitangent = axis.cross(tangent);

    const float spreadRad = e.spreadDeg * kPi / 180.0f;
    const float cosMax = std::cos(spreadRad);
    const float cosA = 1.0f - (1.0f - cosMax) * rand01();
    const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
    const float phi = 2.0f * kPi * rand01();
    const ks::math::vec3 dir = (tangent * std::cos(phi) + bitangent * std::sin(phi)) * sinA + axis * cosA;

    Particle p;
    p.position = e.position;
    p.velocity = dir * (e.speed + (rand01() * 2.0f - 1.0f) * e.speedJitter);
    p.gravity = e.gravity;
    p.drag = e.drag;
    p.color = e.color;
    p.alpha = e.alpha;
    p.maxLife = e.lifetime;
    p.life = e.lifetime;
    p.size = std::max(0.0f, e.size + (rand01() * 2.0f - 1.0f) * e.sizeJitter);
    m_particles.push_back(p);
}

void ParticleSystem::step(float dt) {
    // Emission first, so a particle spawned this step is also integrated
    // this step (its first position is exactly the emitter position only
    // when spawnRate pushes it through the same code path - see particle_test).
    for (std::size_t i = 0; i < m_emitters.size(); ++i) {
        const ParticleEmitter& e = m_emitters[i];
        if (!e.enabled || e.spawnRate <= 0.0f) continue;
        m_spawnAcc[i] += e.spawnRate * dt;
        while (m_spawnAcc[i] >= 1.0f) {
            if (static_cast<int>(m_particles.size()) >= kMaxParticles) {
                m_spawnAcc[i] = 0.0f;
                break;
            }
            spawn(static_cast<int>(i));
            m_spawnAcc[i] -= 1.0f;
        }
    }

    // Integrate + reap in one backwards pass with swap-pop: order does not
    // matter for the physics (particles never interact) and it keeps the
    // alive set contiguous.
    for (int i = static_cast<int>(m_particles.size()) - 1; i >= 0; --i) {
        Particle& p = m_particles[static_cast<std::size_t>(i)];
        p.life -= dt;
        if (p.life <= 0.0f) {
            m_particles[static_cast<std::size_t>(i)] = m_particles.back();
            m_particles.pop_back();
            continue;
        }
        p.velocity += p.gravity * dt;
        if (p.drag > 0.0f) {
            const float k = 1.0f - p.drag * dt;
            p.velocity = p.velocity * (k > 0.0f ? k : 0.0f);
        }
        p.position += p.velocity * dt;
    }
}

std::size_t ParticleSystem::buildQuads(float* out, std::size_t maxFloats) const {
    // Corner order: TL, TR, BR, TL, BR, BL - two counter-clockwise triangles
    // over the uv square; the vertex shader turns uv into a billboard offset
    // around the particle centre.
    static const float kCorners[kVerticesPerParticle][2] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
        {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };

    std::size_t written = 0;
    for (const Particle& p : m_particles) {
        if (written + kVerticesPerParticle * kFloatsPerVertex > maxFloats) break;
        for (int v = 0; v < kVerticesPerParticle; ++v) {
            float* f = out + written;
            f[0] = p.position.x;
            f[1] = p.position.y;
            f[2] = p.position.z;
            f[3] = kCorners[v][0];
            f[4] = kCorners[v][1];
            f[5] = p.color.x;
            f[6] = p.color.y;
            f[7] = p.color.z;
            f[8] = p.alpha;
            f[9] = p.size;
            f[10] = p.life;
            f[11] = p.maxLife;
            written += kFloatsPerVertex;
        }
    }
    return written;
}

} // namespace graphics
} // namespace engine
} // namespace ks
