#pragma once
// CPU particle simulation (roadmap ksengine-vs-cryengine P1 - the old
// GPUParticleSystem was an empty stub and has been deleted).
//
// Split on purpose: this class owns *simulation only* (spawn, integrate,
// reap, deterministic and unit-testable), while NativeRenderer owns the GPU
// side (particle.vert/particle.frag pipelines). The hand-off is a flat
// interleaved vertex array produced by buildQuads(), so nothing in here needs
// Vulkan, a camera or a scene.
//
// Determinism: fixed 1/120 s sub-steps driven by an accumulator and a private
// xorshift PRNG seeded through setSeed(). Same seed + same update() sequence
// == bit-identical particle states, which is what particle_test asserts.
#include "../Math/MathTypesFree.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ks {
namespace engine {
namespace graphics {

struct ParticleEmitter {
    ks::math::vec3 position;
    ks::math::vec3 direction{0.0f, -1.0f, 0.0f};  // main exit axis of the cone
    float spreadDeg = 25.0f;                      // half-angle of the cone
    float spawnRate = 60.0f;                      // particles per second
    float lifetime = 1.5f;                        // seconds
    float speed = 4.0f;                           // mean exit speed (m/s)
    float speedJitter = 1.0f;                     // +/- range around speed
    float size = 0.2f;                            // half-extent of the quad
    float sizeJitter = 0.05f;
    ks::math::vec3 color{1.0f, 0.7f, 0.3f};
    float alpha = 1.0f;
    ks::math::vec3 gravity{0.0f, -9.8f, 0.0f};
    float drag = 0.0f;                            // velocity damping per second
    bool enabled = true;
};

struct Particle {
    ks::math::vec3 position;
    ks::math::vec3 velocity;
    ks::math::vec3 gravity;
    ks::math::vec3 color;
    float drag = 0.0f;
    float alpha = 1.0f;
    float life = 0.0f;
    float maxLife = 1.0f;
    float size = 0.1f;
};

class ParticleSystem {
public:
    static constexpr int kMaxParticles = 4096;
    static constexpr float kFixedStep = 1.0f / 120.0f;
    // Vertex layout shared with particle.vert: pos(3) uv(2) color(4)
    // size(1) life(1) maxLife(1).
    static constexpr int kFloatsPerVertex = 12;
    // Two triangles, so the GPU draws a plain vkCmdDraw with no index buffer.
    static constexpr int kVerticesPerParticle = 6;

    void clear();

    int addEmitter(const ParticleEmitter& emitter);
    int emitterCount() const { return static_cast<int>(m_emitters.size()); }
    ParticleEmitter& emitter(int index) { return m_emitters[static_cast<std::size_t>(index)]; }
    const ParticleEmitter& emitter(int index) const { return m_emitters[static_cast<std::size_t>(index)]; }

    void setSeed(std::uint32_t seed) { m_rng = seed ? seed : 0x9E3779B9u; }

    // Advances the simulation. dt is wall time; it is chopped into fixed
    // 1/120 s steps (a single call simulates at most 2 s — a stall is
    // clamped, not replayed — and the leftover stays in the accumulator for
    // the next call) and emitters spawn from a per-emitter fractional
    // accumulator, so a 60/s emitter over exactly 1 s spawns 60.
    void update(float dt);

    // One-shot spawn (explosions, test setup) bypassing the rate accumulator.
    void burst(int emitterIndex, int count);

    void killAll() { m_particles.clear(); }

    int aliveCount() const { return static_cast<int>(m_particles.size()); }
    const std::vector<Particle>& particles() const { return m_particles; }

    // Packs one screen-facing quad per alive particle into `out` as
    // kVerticesPerParticle * kFloatsPerVertex floats (6 vertices of 12
    // floats: position, uv, rgba, size, life, maxLife). Returns the number
    // of floats written; `maxFloats` caps it and drops the overflow
    // particles rather than writing past the end.
    std::size_t buildQuads(float* out, std::size_t maxFloats) const;

private:
    void step(float dt);
    void spawn(int emitterIndex);
    float rand01();

    std::vector<ParticleEmitter> m_emitters;
    std::vector<float> m_spawnAcc;
    std::vector<Particle> m_particles;
    std::uint32_t m_rng = 0x9E3779B9u;
    float m_accum = 0.0f;
};

} // namespace graphics
} // namespace engine
} // namespace ks
