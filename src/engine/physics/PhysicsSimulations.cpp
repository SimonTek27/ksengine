#include "PhysicsSimulations.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace ks { namespace physics {

// ============================================================================
// SoftBodySimulator implementation
// ============================================================================


namespace {

template <class C, class T> inline bool ksContains(const C& c, const T& v) {
    return std::find(c.begin(), c.end(), v) != c.end();
}
template <class C, class T> inline void ksRemoveAll(C& c, const T& v) {
    c.erase(std::remove(c.begin(), c.end(), v), c.end());
}

inline float ksRandomUnit() {
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng);
}
inline float ksDot(const ks::physics::PhysVec3& a, const ks::physics::PhysVec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline ks::physics::PhysVec3 ksCross(const ks::physics::PhysVec3& a, const ks::physics::PhysVec3& b) {
    return ks::physics::PhysVec3(a.y * b.z - a.z * b.y,
                                 a.z * b.x - a.x * b.z,
                                 a.x * b.y - a.y * b.x);
}
} // namespace

SoftBodySimulator::SoftBodySimulator()
{}

SoftBodySimulator::~SoftBodySimulator() {}

void SoftBodySimulator::setMesh(const std::vector<PhysVec3>& vertices, const std::vector<int>& faces) {
    m_positions = vertices;
    m_faces = faces;
    m_velocities.resize(vertices.size());
    buildConstraints();
}

void SoftBodySimulator::setConfig(const ks::physics::SoftBody& config) {
    m_config = config;
}

void SoftBodySimulator::simulate(int frameStep) {
    applyForces(1.0f / 60.0f);
    satisfyConstraints(m_config.iterationCount);
    simulationStep(frameStep);
}

void SoftBodySimulator::addForce(const PhysVec3& force) {
    m_externalForce += force;
}

void SoftBodySimulator::setGravity(const PhysVec3& gravity) {
    m_gravity = gravity;
}

void SoftBodySimulator::setWind(const PhysVec3& wind, float noise) {
    m_wind = wind;
}

void SoftBodySimulator::pinVertex(int index) {
    if (index >= 0 && index < m_pinnedVertices.size()) return;
    m_pinnedVertices.push_back(index);
}

void SoftBodySimulator::unpinVertex(int index) {
    ksRemoveAll(m_pinnedVertices, index);
}

void SoftBodySimulator::pinVertexGroup(const std::vector<int>& indices) {
    for (int idx : indices) pinVertex(idx);
}

void SoftBodySimulator::reset() {
    std::fill(m_velocities.begin(), m_velocities.end(), PhysVec3());
}

void SoftBodySimulator::buildConstraints() {
    m_edges.clear();
    m_restLengths.clear();

    for (int i = 0; i < m_faces.size(); i += 3) {
        int a = m_faces[i];
        int b = m_faces[i + 1];
        int c = m_faces[i + 2];

        auto addEdge = [this](int v1, int v2) {
            if (v1 > v2) std::swap(v1, v2);
            for (const auto& e : m_edges) {
                if (e.first == v1 && e.second == v2) return;
            }
            m_edges.push_back(std::make_pair(v1, v2));
            float len = (m_positions[v1] - m_positions[v2]).length();
            m_restLengths.push_back(len);
        };

        addEdge(a, b);
        addEdge(b, c);
        addEdge(c, a);
    }
}

void SoftBodySimulator::satisfyConstraints(int iterations) {
    for (int iter = 0; iter < iterations; ++iter) {
        for (int i = 0; i < m_edges.size(); ++i) {
            int v1 = m_edges[i].first;
            int v2 = m_edges[i].second;
            float restLen = m_restLengths[i];

            PhysVec3 delta = m_positions[v2] - m_positions[v1];
            float dist = delta.length();
            if (dist > 0.001f) {
                PhysVec3 correction = delta * (0.5f * (dist - restLen) / dist);
                if (!ksContains(m_pinnedVertices, v1))
                    m_positions[v1] += correction;
                if (!ksContains(m_pinnedVertices, v2))
                    m_positions[v2] -= correction;
            }
        }
    }
}

void SoftBodySimulator::applyForces(float deltaTime) {
    for (int i = 0; i < m_positions.size(); ++i) {
        if (ksContains(m_pinnedVertices, i)) continue;

        PhysVec3 force = m_externalForce + m_wind;
        force += m_gravity * m_config.mass;

        m_velocities[i] += force * (deltaTime / m_config.mass);
        m_velocities[i] *= (1.0f - m_config.damping * deltaTime);
        m_positions[i] += m_velocities[i] * deltaTime;
    }

    m_externalForce = PhysVec3();
}

void SoftBodySimulator::resolveCollisions() {
    // Ground collision
    for (int i = 0; i < m_positions.size(); ++i) {
        if (ksContains(m_pinnedVertices, i)) continue;

        if (m_positions[i].y < m_config.collisionMargin) {
            m_positions[i].y = m_config.collisionMargin;
            if (m_velocities[i].y < 0) {
                m_velocities[i].y = -m_velocities[i].y * 0.3f;
            }
        }
    }
}

// ============================================================================
// ClothSimulator implementation
// ============================================================================

ClothSimulator::ClothSimulator()
    : m_cloth(nullptr)
{}

ClothSimulator::~ClothSimulator() {}

void ClothSimulator::setCloth(ks::physics::Cloth* cloth) {
    m_cloth = cloth;
    if (cloth) {
        m_positions.resize(cloth->vertices.size());
        m_previousPositions.resize(cloth->vertices.size());
        for (int i = 0; i < cloth->vertices.size(); ++i) {
            m_positions[i] = cloth->vertices[i].position;
            m_previousPositions[i] = cloth->vertices[i].previousPosition;
        }
    }
}

void ClothSimulator::simulate(float deltaTime) {
    if (!m_cloth) return;

    integrateVerlet(deltaTime);
    satisfyConstraints(deltaTime);
    satisfyCollisionConstraints();

    for (int i = 0; i < m_cloth->vertices.size(); ++i) {
        m_cloth->vertices[i].position = m_positions[i];
        m_cloth->vertices[i].previousPosition = m_previousPositions[i];
    }

    clothUpdated();
}

void ClothSimulator::integrateVerlet(float deltaTime) {
    if (!m_cloth) return;

    PhysVec3 gravityVec(m_cloth->gravity[0], m_cloth->gravity[1], m_cloth->gravity[2]);
    PhysVec3 windVec = m_cloth->wind;

    for (int i = 0; i < m_cloth->vertices.size(); ++i) {
        if (m_cloth->vertices[i].pinned) continue;

        PhysVec3 vel = m_positions[i] - m_previousPositions[i];
        PhysVec3 accel = m_cloth->vertices[i].acceleration;

        accel += gravityVec;
        if (m_cloth->useDynamicMesh && windVec.lengthSquared() > 0.0001f)
            accel += windVec * (1.0f + m_cloth->windNoise * (2.0f * ksRandomUnit() - 1.0f));

        m_previousPositions[i] = m_positions[i];
        m_positions[i] += vel * m_cloth->velocitySmooth + accel * deltaTime * deltaTime;
    }
}

void ClothSimulator::satisfyConstraints(float deltaTime) {
    if (!m_cloth) return;

    for (int iter = 0; iter < 5; ++iter) {
        for (const auto& c : m_cloth->constraints) {
            PhysVec3 p1 = m_positions[c.vertex1];
            PhysVec3 p2 = m_positions[c.vertex2];
            PhysVec3 delta = p2 - p1;
            float dist = delta.length();

            if (dist > 0.001f) {
                float diff = (dist - c.restLength) / dist;
                PhysVec3 correction = delta * 0.5f * diff * c.stiffness;

                if (!m_cloth->vertices[c.vertex1].pinned)
                    m_positions[c.vertex1] += correction;
                if (!m_cloth->vertices[c.vertex2].pinned)
                    m_positions[c.vertex2] -= correction;
            }
        }
    }
}

void ClothSimulator::satisfyCollisionConstraints() {
    for (int i = 0; i < m_positions.size(); ++i) {
        if (m_cloth && m_cloth->vertices[i].pinned) continue;

        // Ground collision
        if (m_positions[i].y < 0.0f) {
            m_positions[i].y = 0.0f;
        }

        // Sphere collisions
        for (const auto& sphere : m_collisionSpheres) {
            PhysVec3 delta = m_positions[i] - sphere.center;
            float dist = delta.length();
            if (dist < sphere.radius && dist > 0.0001f) {
                m_positions[i] = sphere.center + delta / dist * sphere.radius;
            }
        }

        // Box collisions (AABB)
        for (const auto& box : m_collisionBoxes) {
            if (m_positions[i].x >= box.min.x && m_positions[i].x <= box.max.x &&
                m_positions[i].y >= box.min.y && m_positions[i].y <= box.max.y &&
                m_positions[i].z >= box.min.z && m_positions[i].z <= box.max.z) {
                // Push out to nearest face
                float dx = std::min(m_positions[i].x - box.min.x, box.max.x - m_positions[i].x);
                float dy = std::min(m_positions[i].y - box.min.y, box.max.y - m_positions[i].y);
                float dz = std::min(m_positions[i].z - box.min.z, box.max.z - m_positions[i].z);
                if (dx <= dy && dx <= dz) {
                    m_positions[i].x = (m_positions[i].x < (box.min.x + box.max.x) * 0.5f
                        ? box.min.x : box.max.x);
                } else if (dy <= dz) {
                    m_positions[i].y = (m_positions[i].y < (box.min.y + box.max.y) * 0.5f
                        ? box.min.y : box.max.y);
                } else {
                    m_positions[i].z = (m_positions[i].z < (box.min.z + box.max.z) * 0.5f
                        ? box.min.z : box.max.z);
                }
            }
        }
    }
}

void ClothSimulator::addCollisionSphere(const PhysVec3& center, float radius) {
    m_collisionSpheres.push_back({center, radius});
}

void ClothSimulator::addCollisionBox(const std::vector<PhysVec3>& corners) {
    PhysVec3 min = corners[0], max = corners[0];
    for (const auto& c : corners) {
        min = PhysVec3(std::min(min.x, c.x), std::min(min.y, c.y), std::min(min.z, c.z));
        max = PhysVec3(std::max(max.x, c.x), std::max(max.y, c.y), std::max(max.z, c.z));
    }
    m_collisionBoxes.push_back({corners, min, max});
}

void ClothSimulator::clearCollisions() {
    m_collisionSpheres.clear();
    m_collisionBoxes.clear();
}

void ClothSimulator::setPinnedVertices(const std::vector<int>& indices) {
    if (!m_cloth) return;
    for (int i = 0; i < m_cloth->vertices.size(); ++i) {
        m_cloth->vertices[i].pinned = false;
    }
    for (int idx : indices) {
        if (idx >= 0 && idx < m_cloth->vertices.size()) {
            m_cloth->vertices[idx].pinned = true;
        }
    }
}

// ============================================================================
// ParticleSystemSimulator implementation
// ============================================================================

ParticleSystemSimulator::ParticleSystemSimulator()
{}

ParticleSystemSimulator::~ParticleSystemSimulator() {}

void ParticleSystemSimulator::setEmitter(const ParticleSystem::Emitter& emitter) {
    m_emitter = emitter;
}

void ParticleSystemSimulator::setPhysics(const ParticleSystem::Physics& physics) {
    m_physics = physics;
}

void ParticleSystemSimulator::setMaxCount(int count) {
    m_maxCount = count;
}

void ParticleSystemSimulator::setLifetime(int frames) {
    m_lifetime = frames;
}

void ParticleSystemSimulator::simulate(float deltaTime) {
    // Emit new particles
    if (m_emitter.rate > 0 && m_particles.size() < m_maxCount) {
        int toEmit = std::min<int>(static_cast<int>(m_emitter.rate * deltaTime), m_maxCount - static_cast<int>(m_particles.size()));
        for (int i = 0; i < toEmit; ++i) {
            ParticleSystem::Particle p;
            p.position = m_emitter.position;
            p.velocity = m_emitter.direction * m_emitter.velocity;
            p.acceleration = PhysVec3();
            p.lifetime = m_lifetime;
            p.age = 0;
            p.mass = 1.0f;
            p.size = 0.1f;
            p.color = PhysVec4(1, 1, 1, 1);
            m_particles.push_back(p);
        }
    }

    // Update particles
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        auto& p = m_particles[i];
        p.age += deltaTime * 60.0f;
        if (!p.isAlive()) {
            m_particles.erase(m_particles.begin() + (i));
            continue;
        }

        PhysVec3 accel = PhysVec3(m_physics.gravity[0], m_physics.gravity[1], m_physics.gravity[2]);
        if (m_physics.useWind) {
            accel += PhysVec3(m_physics.wind[0], m_physics.wind[1], m_physics.wind[2]);
        }
        p.velocity += accel * deltaTime;
        p.velocity *= (1.0f - m_physics.damping * deltaTime);
        p.position += p.velocity * deltaTime;
    }

    particlesUpdated();
}

// ============================================================================
// FluidSimulator implementation
// ============================================================================

FluidSimulator::FluidSimulator()
{}

FluidSimulator::~FluidSimulator() {}

void FluidSimulator::addParticles(const std::vector<PhysVec3>& positions) {
    for (const auto& pos : positions) {
        FluidParticle p;
        p.position = pos;
        p.velocity = PhysVec3();
        p.density = restDensity;
        p.pressure = 0;
        fluidParticles.push_back(p);
    }
}

void FluidSimulator::simulate(float deltaTime) {
    deltaTime *= timeScale;

    // Build spatial hash grid for neighbor search
    m_spatialGrid.build(fluidParticles, smoothingRadius);

    // Compute densities using spatial hash
    for (int i = 0; i < fluidParticles.size(); ++i) {
        fluidParticles[i].density = computeDensity(fluidParticles[i].position);
    }

    // Compute pressures
    computePressures();

    // Apply forces
    for (int i = 0; i < fluidParticles.size(); ++i) {
        auto& p = fluidParticles[i];
        PhysVec3 force = PhysVec3(gravity[0], gravity[1], gravity[2]) * p.density;

        // Pressure gradient
        PhysVec3 grad = computeDensityGradient(p.position);
        force -= grad * stiffness;

        // Surface tension (CSF model)
        PhysVec3 normal = computeDensityGradient(p.position).normalized();
        PhysVec3 stForce = computeSurfaceTension(p.position, normal);
        force += stForce * surfaceTension;

        // Viscosity (simplified)
        force -= p.velocity * viscosity;

        // Adaptive time stepping based on CFL condition
        float CFL = 0.4f;
        float maxVel = p.velocity.length();
        if (maxVel > 0.001f) {
            float dtCFL = CFL * smoothingRadius / maxVel;
            deltaTime = std::min(deltaTime, dtCFL);
        }

        p.velocity += force * (deltaTime / p.density);
        p.position += p.velocity * deltaTime;
    }

    // Boundary constraints
    for (auto& p : fluidParticles) {
        for (const auto& b : m_boundaries) {
            if (p.position.y < b.y) {
                p.position.y = b.y;
                p.velocity.y = -p.velocity.y * 0.3f; // Bounce with damping
            }
        }
    }

    fluidUpdated();
}

void FluidSimulator::setBoundaries(const std::vector<PhysVec3>& boundaries) {
    m_boundaries = boundaries;
}

float FluidSimulator::getParticleDensity(int index) const {
    if (index >= 0 && index < fluidParticles.size())
        return fluidParticles[index].density;
    return 0;
}

float FluidSimulator::getParticlePressure(int index) const {
    if (index >= 0 && index < fluidParticles.size())
        return fluidParticles[index].pressure;
    return 0;
}

float FluidSimulator::computeDensity(const PhysVec3& position) {
    float density = 0;
    float h = smoothingRadius;
    float h2 = h * h;

    // Use spatial hash for neighbor search
    std::vector<int> neighbors = m_spatialGrid.query(position, h);
    for (int idx : neighbors) {
        const auto& p = fluidParticles[idx];
        float r2 = (p.position - position).lengthSquared();
        if (r2 < h2) {
            float w = (h2 - r2) / h2;
            density += w * w * w; // SPH poly6 kernel
        }
    }
    return density * restDensity;
}

PhysVec3 FluidSimulator::computeDensityGradient(const PhysVec3& position) {
    PhysVec3 grad;
    float h = smoothingRadius;
    float h2 = h * h;

    std::vector<int> neighbors = m_spatialGrid.query(position, h);
    for (int idx : neighbors) {
        const auto& p = fluidParticles[idx];
        PhysVec3 diff = position - p.position;
        float r2 = diff.lengthSquared();
        if (r2 < h2 && r2 > 0.0001f) {
            float r = std::sqrt(r2);
            float w = 6.0f * (h - r) / (3.14159f * h2 * h2 * r); // spiky kernel gradient
            grad += diff * w;
        }
    }
    return grad;
}

PhysVec3 FluidSimulator::computeSurfaceTension(const PhysVec3& position, const PhysVec3& normal) {
    if (normal.length() < 0.001f) return PhysVec3();

    // CSF (Continuum Surface Force) model
    float curvature = 0.0f;
    std::vector<int> neighbors = m_spatialGrid.query(position, smoothingRadius);
    for (int idx : neighbors) {
        const auto& p = fluidParticles[idx];
        PhysVec3 diff = position - p.position;
        float r2 = diff.lengthSquared();
        if (r2 < smoothingRadius * smoothingRadius && r2 > 0.0001f) {
            PhysVec3 neighborNormal = computeDensityGradient(p.position).normalized();
            curvature += (normal - neighborNormal).length();
        }
    }

    return normal * curvature;
}

void FluidSimulator::computePressures() {
    for (auto& p : fluidParticles) {
        // Tait equation of state
        float rhoRatio = p.density / restDensity;
        p.pressure = stiffness * (std::pow(rhoRatio, 7.0f) - 1.0f);
    }
}

// ============================================================================
// SpatialHashGrid implementation
// ============================================================================

void FluidSimulator::SpatialHashGrid::build(const std::vector<FluidParticle>& particles, float radius) {
    cells.clear();
    cellSize = radius * 2.0f;

    for (int i = 0; i < particles.size(); ++i) {
        const auto& pos = particles[i].position;
        int cx = static_cast<int>(std::floor(pos.x / cellSize));
        int cy = static_cast<int>(std::floor(pos.y / cellSize));
        int cz = static_cast<int>(std::floor(pos.z / cellSize));
        cells[{cx, cy, cz}].push_back(i);
    }
}

std::vector<int> FluidSimulator::SpatialHashGrid::query(const PhysVec3& position, float radius) const {
    std::vector<int> result;
    int cx = static_cast<int>(std::floor(position.x / cellSize));
    int cy = static_cast<int>(std::floor(position.y / cellSize));
    int cz = static_cast<int>(std::floor(position.z / cellSize));

    // Check 3x3x3 neighborhood
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                auto key = CellKey{cx + dx, cy + dy, cz + dz};
                auto it = cells.find(key);
                if (it != cells.end()) {
                    result.insert(result.end(), it->second.begin(), it->second.end());
                }
            }
        }
    }
    return result;
}

void FluidSimulator::SpatialHashGrid::clear() {
    cells.clear();
}

// ============================================================================
// HairSystem implementation
// ============================================================================

HairSystem::HairSystem()
{}

HairSystem::~HairSystem() {}

void HairSystem::addStrand(int rootVertex, int count) {
    HairStrand strand;
    strand.rootVertex = rootVertex;
    strand.points.resize(segments + 1);
    strand.velocities.resize(segments + 1);
    strand.restPositions.resize(segments + 1);
    strand.thickness.resize(segments + 1, 1.0f);
    strand.ages.resize(segments + 1, 0.0f);
    strand.curlFactor = 0.0f;
    strand.clumpFactor = 0.0f;

    PhysVec3 base(0, 0, 0);
    for (int i = 0; i <= segments; ++i) {
        strand.points[i] = base + PhysVec3(0, -i * segmentLength, 0);
        strand.restPositions[i] = strand.points[i];
        strand.velocities[i] = PhysVec3();
    }

    strands.push_back(strand);
}

void HairSystem::removeStrand(int index) {
    if (index >= 0 && index < strands.size())
        strands.erase(strands.begin() + (index));
}

void HairSystem::simulate(float deltaTime) {
    // Apply texture-driven parameters
    applyTextureParameters();
    
    // Build spatial hash for hair-hair collision
    if (useHairCollision) {
        m_strandHash.build(strands, hairHairRadius);
    }

    for (auto& strand : strands) {
        simulateStrand(strand, deltaTime);
    }

    // Apply global effects
    if (useHairCollision) {
        applyHairHairCollision();
    }
    if (useClumping) {
        applyClumping();
    }
    if (useCurling) {
        applyCurling();
    }
    if (useGuideFollow) {
        applyGuideFollow();
    }
}

void HairSystem::simulateStrand(HairStrand& strand, float deltaTime) {
    // Fixed root
    if (strand.points.empty()) return;
    PhysVec3 rootPos = strand.points[0];

    // Verlet integration
    for (int i = 1; i < strand.points.size(); ++i) {
        PhysVec3 vel = strand.points[i] - strand.velocities[i];
        strand.velocities[i] = strand.points[i];

        PhysVec3 gravityVec(gravity[0], gravity[1], gravity[2]);
        PhysVec3 accel = gravityVec * dynamics;
        accel -= vel * damping;

        strand.points[i] += vel + accel * deltaTime * deltaTime;

        // Update age
        strand.ages[i] += deltaTime;
    }

    // Constraint projection
    for (int iter = 0; iter < 4; ++iter) {
        strand.points[0] = rootPos;
        for (int i = 1; i < strand.points.size(); ++i) {
            PhysVec3 dir = strand.points[i] - strand.points[i-1];
            float dist = dir.length();
            if (dist > 0.001f) {
                dir /= dist;
                strand.points[i] = strand.points[i-1] + dir * segmentLength;
            }
        }
    }
}

void HairSystem::applyHairHairCollision() {
    float pushForce = hairHairStiffness;

    for (int si = 0; si < strands.size(); ++si) {
        auto& strandA = strands[si];
        for (int pi = 1; pi < strandA.points.size(); ++pi) {
            PhysVec3 posA = strandA.points[pi];
            auto nearby = m_strandHash.query(posA, hairHairRadius);

            for (const auto& [sj, pj] : nearby) {
                if (sj == si) continue; // Same strand

                PhysVec3 posB = strands[sj].points[pj];
                PhysVec3 diff = posA - posB;
                float dist = diff.length();

                if (dist < hairHairRadius && dist > 0.0001f) {
                    float penetration = hairHairRadius - dist;
                    PhysVec3 normal = diff / dist;
                    PhysVec3 correction = normal * penetration * pushForce * 0.5f;

                    strandA.points[pi] += correction * 0.5f;
                    strands[sj].points[pj] -= correction * 0.5f;
                }
            }
        }
    }
}

void HairSystem::applyClumping() {
    if (strands.empty()) return;

    // Simple clumping: pull nearby strands toward a common center
    PhysVec3 center;
    for (const auto& strand : strands) {
        if (!strand.points.empty()) {
            center += strand.points[0]; // Use root positions
        }
    }
    center /= strands.size();

    for (auto& strand : strands) {
        float influence = strand.clumpFactor * clumpStrength;
        for (int i = 1; i < strand.points.size(); ++i) {
            float t = static_cast<float>(i) / strand.points.size();
            PhysVec3 toCenter = center - strand.points[i];
            float dist = toCenter.length();
            if (dist > 0.001f) {
                toCenter /= dist;
                float strength = influence * t * clumpRadius;
                strand.points[i] += toCenter * std::min(strength, dist);
            }
        }
    }
}

void HairSystem::applyCurling() {
    for (auto& strand : strands) {
        float curl = strand.curlFactor * curlRadius;
        if (curl < 0.0001f) continue;

        for (int i = 1; i < strand.points.size(); ++i) {
            float t = static_cast<float>(i) * curlFrequency;
            float angle = t * M_PI * 2.0f;

            // Compute curl direction (perpendicular to strand direction)
            PhysVec3 strandDir;
            if (i < strand.points.size() - 1) {
                strandDir = (strand.points[i + 1] - strand.points[i]).normalized();
            } else {
                strandDir = (strand.points[i] - strand.points[i - 1]).normalized();
            }

            // Create curl in two perpendicular directions
            PhysVec3 perp1, perp2;
            if (std::fabs(strandDir.x) < 0.9f) {
                perp1 = ksCross(strandDir, PhysVec3(1, 0, 0)).normalized();
            } else {
                perp1 = ksCross(strandDir, PhysVec3(0, 1, 0)).normalized();
            }
            perp2 = ksCross(strandDir, perp1).normalized();

            PhysVec3 curlOffset = (perp1 * std::cos(angle) + perp2 * std::sin(angle)) * curl;
            strand.points[i] += curlOffset * 0.01f;
        }
    }
}

void HairSystem::applyGuideFollow() {
    if (guideStrandCount <= 0 || strands.size() <= guideStrandCount) return;

    // Guide strands are the first guideStrandCount strands
    // Other strands follow the nearest guide
    for (int i = guideStrandCount; i < strands.size(); ++i) {
        auto& strand = strands[i];

        // Find nearest guide strand
        int nearestGuide = 0;
        float minDist = std::numeric_limits<float>::max();

        PhysVec3 myRoot = strand.points[0];
        for (int g = 0; g < guideStrandCount; ++g) {
            PhysVec3 guideRoot = strands[g].points[0];
            float dist = (myRoot - guideRoot).lengthSquared();
            if (dist < minDist) {
                minDist = dist;
                nearestGuide = g;
            }
        }

        // Blend toward guide shape
        const auto& guide = strands[nearestGuide];
        for (int j = 1; j < strand.points.size() && j < guide.points.size(); ++j) {
            float t = static_cast<float>(j) / strand.points.size();
            float influence = guideInfluence * t;
            strand.points[j] = strand.points[j] * (1.0f - influence) + guide.points[j] * influence;
        }
    }
}

// ============================================================================
// HairSystem Texture-Driven Parameters
// ============================================================================

PhysVec4 HairSystem::TextureMap::sample(float u, float v) const {
    if (data.empty() || width == 0 || height == 0) {
        return PhysVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    
    // Clamp UV coordinates
    u = std::clamp( u, 0.0f,  1.0f);
    v = std::clamp( v, 0.0f,  1.0f);
    
    // Convert to pixel coordinates
    int x = static_cast<int>(u * (width - 1));
    int y = static_cast<int>(v * (height - 1));
    
    int idx = (y * width + x) * channels;
    if (idx + 3 >= data.size()) {
        return PhysVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    
    float r = static_cast<unsigned char>(data[idx]) / 255.0f;
    float g = static_cast<unsigned char>(data[idx + 1]) / 255.0f;
    float b = static_cast<unsigned char>(data[idx + 2]) / 255.0f;
    float a = (channels >= 4) ? static_cast<unsigned char>(data[idx + 3]) / 255.0f : 1.0f;
    
    return PhysVec4(r, g, b, a);
}

float HairSystem::sampleTextureChannel(const TextureMap& tex, float u, float v, int channel) const {
    PhysVec4 color = tex.sample(u, v);
    switch (channel) {
        case 0: return color.x;
        case 1: return color.y;
        case 2: return color.z;
        case 3: return color.w;
        default: return 1.0f;
    }
}

void HairSystem::applyTextureParameters() {
    for (auto& strand : strands) {
        if (strand.points.empty()) continue;
        
        for (int i = 0; i < strand.points.size(); ++i) {
            // Compute UV coordinates based on strand position
            // Simple projection: use XZ for UV, Y for density variation
            float u = (strand.points[i].x + 1.0f) * 0.5f;
            float v = (strand.points[i].z + 1.0f) * 0.5f;
            
            // Apply density texture (affects whether strand exists at this point)
            if (useTextureDensity && !densityTexture.data.empty()) {
                float density = sampleTextureChannel(densityTexture, u, v, 0);
                // Skip points with low density (simulate hair thinning)
                if (density < 0.1f && i > 0) {
                    strand.thickness[i] = 0.0f;
                } else {
                    strand.thickness[i] *= density;
                }
            }
            
            // Apply stiffness texture (affects dynamics multiplier)
            if (useTextureStiffness && !stiffnessTexture.data.empty()) {
                float stiffness = sampleTextureChannel(stiffnessTexture, u, v, 0);
                // Store stiffness in velocity magnitude for now
                // Actual application happens in simulateStrand
                strand.velocities[i] *= (0.5f + stiffness * 0.5f);
            }
            
            // Apply curl texture
            if (useTextureCurl && !curlTexture.data.empty()) {
                float curl = sampleTextureChannel(curlTexture, u, v, 0);
                strand.curlFactor = curl;
            }
            
            // Apply thickness texture
            if (useTextureThickness && !thicknessTexture.data.empty()) {
                float thick = sampleTextureChannel(thicknessTexture, u, v, 0);
                strand.thickness[i] *= thick;
            }
        }
    }
}

std::vector<PhysVec3> HairSystem::getCompletedStrands() const {
    std::vector<PhysVec3> result;
    for (const auto& strand : strands) {
        for (const auto& p : strand.points) {
            result.push_back(p);
        }
    }
    return result;
}

// ============================================================================
// HairSystem StrandSpatialHash implementation
// ============================================================================

void HairSystem::StrandSpatialHash::build(const std::vector<HairStrand>& strands, float radius) {
    cells.clear();
    cellSize = radius * 2.0f;

    for (int si = 0; si < strands.size(); ++si) {
        const auto& strand = strands[si];
        for (int pi = 0; pi < strand.points.size(); ++pi) {
            const auto& pos = strand.points[pi];
            int cx = static_cast<int>(std::floor(pos.x / cellSize));
            int cy = static_cast<int>(std::floor(pos.y / cellSize));
            int cz = static_cast<int>(std::floor(pos.z / cellSize));
            cells[{cx, cy, cz}].push_back({si, pi});
        }
    }
}

std::vector<std::pair<int,int>> HairSystem::StrandSpatialHash::query(const PhysVec3& position, float radius) const {
    std::vector<std::pair<int,int>> result;
    int cx = static_cast<int>(std::floor(position.x / cellSize));
    int cy = static_cast<int>(std::floor(position.y / cellSize));
    int cz = static_cast<int>(std::floor(position.z / cellSize));

    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                auto key = StrandCellKey{cx + dx, cy + dy, cz + dz};
                auto it = cells.find(key);
                if (it != cells.end()) {
                    result.insert(result.end(), it->second.begin(), it->second.end());
                }
            }
        }
    }
    return result;
}

void HairSystem::StrandSpatialHash::clear() {
    cells.clear();
}

// ============================================================================
// FluidSimulator Volume Export
// ============================================================================

FluidSimulator::VolumeGrid FluidSimulator::exportVolumeGrid(int resolution) const {
    VolumeGrid grid;
    grid.resolutionX = resolution;
    grid.resolutionY = resolution;
    grid.resolutionZ = resolution;

    if (fluidParticles.empty()) {
        grid.densityField.resize(resolution * resolution * resolution, 0.0f);
        grid.velocityFieldX.resize(resolution * resolution * resolution, 0.0f);
        grid.velocityFieldY.resize(resolution * resolution * resolution, 0.0f);
        grid.velocityFieldZ.resize(resolution * resolution * resolution, 0.0f);
        return grid;
    }

    // Compute bounding box
    grid.gridMin = fluidParticles[0].position;
    grid.gridMax = fluidParticles[0].position;
    for (const auto& p : fluidParticles) {
        grid.gridMin = PhysVec3(std::min(grid.gridMin.x, p.position.x),
                                  std::min(grid.gridMin.y, p.position.y),
                                  std::min(grid.gridMin.z, p.position.z));
        grid.gridMax = PhysVec3(std::max(grid.gridMax.x, p.position.x),
                                  std::max(grid.gridMax.y, p.position.y),
                                  std::max(grid.gridMax.z, p.position.z));
    }

    // Add padding
    PhysVec3 padding = (grid.gridMax - grid.gridMin) * 0.1f;
    grid.gridMin -= padding;
    grid.gridMax += padding;

    PhysVec3 gridSize = grid.gridMax - grid.gridMin;
    grid.voxelSize = PhysVec3(gridSize.x / resolution,
                                gridSize.y / resolution,
                                gridSize.z / resolution);

    int totalVoxels = resolution * resolution * resolution;
    grid.densityField.resize(totalVoxels, 0.0f);
    grid.velocityFieldX.resize(totalVoxels, 0.0f);
    grid.velocityFieldY.resize(totalVoxels, 0.0f);
    grid.velocityFieldZ.resize(totalVoxels, 0.0f);

    // Splat particles onto grid
    float h = smoothingRadius;
    float h2 = h * h;
    for (const auto& p : fluidParticles) {
        // Find grid cells within smoothing radius
        int minCx = std::max(0, static_cast<int>((p.position.x - h - grid.gridMin.x) / grid.voxelSize.x));
        int maxCx = std::min(resolution - 1, static_cast<int>((p.position.x + h - grid.gridMin.x) / grid.voxelSize.x));
        int minCy = std::max(0, static_cast<int>((p.position.y - h - grid.gridMin.y) / grid.voxelSize.y));
        int maxCy = std::min(resolution - 1, static_cast<int>((p.position.y + h - grid.gridMin.y) / grid.voxelSize.y));
        int minCz = std::max(0, static_cast<int>((p.position.z - h - grid.gridMin.z) / grid.voxelSize.z));
        int maxCz = std::min(resolution - 1, static_cast<int>((p.position.z + h - grid.gridMin.z) / grid.voxelSize.z));

        for (int cx = minCx; cx <= maxCx; ++cx) {
            for (int cy = minCy; cy <= maxCy; ++cy) {
                for (int cz = minCz; cz <= maxCz; ++cz) {
                    PhysVec3 voxelCenter = grid.gridMin + PhysVec3(
                        (cx + 0.5f) * grid.voxelSize.x,
                        (cy + 0.5f) * grid.voxelSize.y,
                        (cz + 0.5f) * grid.voxelSize.z
                    );

                    float r2 = (p.position - voxelCenter).lengthSquared();
                    if (r2 < h2) {
                        float w = (h2 - r2) / h2;
                        float weight = w * w * w;
                        int idx = cz * resolution * resolution + cy * resolution + cx;
                        grid.densityField[idx] += weight * p.density;
                        grid.velocityFieldX[idx] += weight * p.velocity.x;
                        grid.velocityFieldY[idx] += weight * p.velocity.y;
                        grid.velocityFieldZ[idx] += weight * p.velocity.z;
                    }
                }
            }
        }
    }

    return grid;
}

bool FluidSimulator::exportVDB(const std::string& path, int resolution) const {
    VolumeGrid grid = exportVolumeGrid(resolution);

#ifdef HAS_OPENVDB
    // Use OpenVDB if available
    openvdb::initialize();

    openvdb::FloatGrid::Ptr densityGrid = openvdb::FloatGrid::create(0.0f);
    openvdb::FloatGrid::Ptr velXGrid = openvdb::FloatGrid::create(0.0f);
    openvdb::FloatGrid::Ptr velYGrid = openvdb::FloatGrid::create(0.0f);
    openvdb::FloatGrid::Ptr velZGrid = openvdb::FloatGrid::create(0.0f);

    densityGrid->setTransform(openvdb::math::Transform::createLinearTransform(
        grid.voxelSize.x));
    densityGrid->setName("density");
    velXGrid->setName("velocity_x");
    velYGrid->setName("velocity_y");
    velZGrid->setName("velocity_z");

    openvdb::FloatGrid::Accessor densityAcc = densityGrid->getAccessor();
    openvdb::FloatGrid::Accessor velXAcc = velXGrid->getAccessor();
    openvdb::FloatGrid::Accessor velYAcc = velYGrid->getAccessor();
    openvdb::FloatGrid::Accessor velZAcc = velZGrid->getAccessor();

    for (int cx = 0; cx < grid.resolutionX; ++cx) {
        for (int cy = 0; cy < grid.resolutionY; ++cy) {
            for (int cz = 0; cz < grid.resolutionZ; ++cz) {
                int idx = cz * grid.resolutionX * grid.resolutionY + cy * grid.resolutionX + cx;
                float d = grid.densityField[idx];
                if (d > 0.01f) {
                    openvdb::Coord coord(cx, cy, cz);
                    densityAcc.setValue(coord, d);
                    velXAcc.setValue(coord, grid.velocityFieldX[idx]);
                    velYAcc.setValue(coord, grid.velocityFieldY[idx]);
                    velZAcc.setValue(coord, grid.velocityFieldZ[idx]);
                }
            }
        }
    }

    openvdb::io::File file(path);
    openvdb::GridPtrVec grids;
    grids.push_back(densityGrid);
    grids.push_back(velXGrid);
    grids.push_back(velYGrid);
    grids.push_back(velZGrid);
    file.write(grids);
    file.close();
    return true;
#else
    // Fallback: export as raw binary volume
    std::ofstream outFile(path, std::ios::binary);
    if (!outFile) return false;

    // Write header
    outFile.write("KSVOL", 5);
    std::int32_t res = grid.resolutionX;
    outFile.write(reinterpret_cast<const char*>(&res), sizeof(std::int32_t));
    outFile.write(reinterpret_cast<const char*>(&grid.gridMin), sizeof(PhysVec3));
    outFile.write(reinterpret_cast<const char*>(&grid.gridMax), sizeof(PhysVec3));

    // Write density field
    std::vector<float> densities = grid.densityField;
    outFile.write(reinterpret_cast<const char*>(densities.data()),
                  densities.size() * sizeof(float));

    outFile.close();
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Notifications that used to be Qt signals (emit simulationStep(...) and
// friends). In the Qt-free build nobody is connected to them, so they are
// explicit no-ops rather than undefined symbols waiting to break a link.
// ---------------------------------------------------------------------------
void SoftBodySimulator::simulationStep(int) {}
void ClothSimulator::clothUpdated() {}
void ParticleSystemSimulator::particlesUpdated() {}
void FluidSimulator::fluidUpdated() {}
void HairSystem::hairUpdated() {}

} // namespace physics
} // namespace ks
