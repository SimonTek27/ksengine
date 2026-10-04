                const float wz = (static_cast<float>(z) / static_cast<float>(gridH - 1)) * kWorld - half;
                const float d = std::sqrt(wx * wx + wz * wz);
                float t = (d - 200.0f) / 400.0f;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                t = t * t * (3.0f - 2.0f * t);
                float& h = heights[static_cast<std::size_t>(z) * static_cast<std::size_t>(gridW) + static_cast<std::size_t>(x)];
                h = h * t - 0.8f * (1.0f - t);
            }
        }
    }

    int stride = 1;
    const int largest = std::max(gridW, gridH);
    if (largest > kMaxGrid)
        stride = (largest - 1 + (kMaxGrid - 2)) / (kMaxGrid - 1);
    terrain::TerrainMeshData data =
        stride > 1 ? terrain::generateTerrainMeshLOD(heights, gridW, gridH, kWorld, kWorld, stride, 30.0f)
                   : terrain::generateTerrainMesh(heights, gridW, gridH, kWorld, kWorld, 30.0f);
    if (data.vertices.empty()) return;

    NativeMesh mesh;
    mesh.vertices.reserve(data.vertices.size());
    for (terrain::TerrainVertex& v : data.vertices) {
        v.px -= half;
        v.pz -= half;
        const float rock = std::clamp((1.0f - v.ny) * 5.0f, 0.0f, 1.0f);
        const float snow = std::clamp((v.py - 26.0f) / 10.0f, 0.0f, 1.0f);
        float r = (1.0f - rock) * 0.31f + rock * 0.44f;
        float g = (1.0f - rock) * 0.40f + rock * 0.41f;
        float b = (1.0f - rock) * 0.22f + rock * 0.37f;
        r = (1.0f - snow) * r + snow * 0.72f;
        g = (1.0f - snow) * g + snow * 0.73f;
        b = (1.0f - snow) * b + snow * 0.75f;
        NativeVertex nv;
        nv.px = v.px; nv.py = v.py; nv.pz = v.pz;
        nv.nx = v.nx; nv.ny = v.ny; nv.nz = v.nz;
        nv.u = v.u; nv.v = v.v;
        nv.r = r; nv.g = g; nv.b = b; nv.a = 1.0f;
        mesh.vertices.push_back(nv);
    }
    mesh.indices = std::move(data.indices);
    m_vulkanRenderer->setMesh(kTerrainMeshName, mesh);

    const ks::ecs::Entity e = scene().create();
    if (e != ks::ecs::kNullEntity) {
        scene().emplace<ks::ecs::Name>(e, ks::ecs::Name{kTerrainMeshName});
        scene().emplace<ks::ecs::Transform>(e);
        scene().emplace<ks::ecs::MeshInstance>(e, ks::ecs::MeshInstance{kTerrainMeshName});
    }
    std::fprintf(stderr, "[terrain] KS_TERRAIN=1: %zu vertices of ground over %.0f m\n",
                 mesh.vertices.size(), kWorld);
}

// Roadmap P1 (KS_PARTICLES=1): one dust emitter riding the car, stepped with
// the frame's real dt and handed to the renderer as flat quads. Everything
// here is behind the env flag - with it unset nothing is created, no emitter
// exists and not a single float reaches setParticleVertices(), so the
// default image is untouched.
void SimulationLoop::updateAndDrawParticles(float dt) {
    using ks::engine::graphics::ParticleEmitter;
    using ks::engine::graphics::ParticleSystem;

    if (!m_particlesInit) {
        m_particlesInit = true;
        const char* flag = std::getenv("KS_PARTICLES");
        m_particlesWanted = flag && *flag && std::atoi(flag) != 0;
        if (m_particlesWanted) {
            m_particles.setSeed(20251004u);
            ParticleEmitter dust;
            dust.position = {0.0f, 0.2f, 0.0f};   // re-anchored to the car below
            dust.direction = {0.0f, 1.0f, 0.0f};  // kicked up, not backwards
            dust.spreadDeg = 45.0f;
            dust.spawnRate = 90.0f;
            dust.lifetime = 1.6f;
            dust.speed = 2.2f;
            dust.speedJitter = 1.1f;
            dust.size = 0.30f;
            dust.sizeJitter = 0.15f;
            dust.color = {0.62f, 0.57f, 0.48f};   // road dust, not smoke
            dust.alpha = 0.55f;
            dust.gravity = {0.0f, -1.6f, 0.0f};   // hangs, then settles
            dust.drag = 1.4f;
            m_particles.addEmitter(dust);
            std::fprintf(stderr, "[particles] KS_PARTICLES=1: dust emitter up to %d sprites\n",
                         ParticleSystem::kMaxParticles);
        }
    }
    if (!m_particlesWanted || !m_vulkanRenderer) return;

#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        const auto st = m_vehicle->getState();
        ks::engine::graphics::ParticleEmitter& em = m_particles.emitter(0);
        em.position = {st.position.x, st.position.y + 0.15f, st.position.z};
        // Dust is kicked up by rolling tyres: stationary, there is none.
        em.enabled = std::fabs(st.speed) > 2.0f;
    }
#endif

    m_particles.update(dt);

    static constexpr std::size_t kMaxFloats =
        static_cast<std::size_t>(ParticleSystem::kMaxParticles) *
        ParticleSystem::kVerticesPerParticle * ParticleSystem::kFloatsPerVertex;
    m_particleQuads.resize(kMaxFloats);
    const std::size_t floats = m_particles.buildQuads(m_particleQuads.data(), m_particleQuads.size());
    m_vulkanRenderer->setParticleVertices(floats ? m_particleQuads.data() : nullptr, floats);
}

} // namespace ks::sim
