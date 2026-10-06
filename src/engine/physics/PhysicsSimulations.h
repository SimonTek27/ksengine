#include <string>
#include "PhysicsCoreTypes.h"
#pragma once

#include "PhysicsEngine.h"
#include <vector>
#include <utility>
#include <random>
#include <fstream>
#include <unordered_map>

namespace ks {
namespace physics {

// ============================================================================
// Soft Body Physics Implementation
// ============================================================================

struct SoftBodySimulator {
    public:
    explicit SoftBodySimulator();
    ~SoftBodySimulator();

    void setMesh(const std::vector<PhysVec3>& vertices, const std::vector<int>& faces);
    void setConfig(const ks::physics::SoftBody& config);

    void simulate(int frameStep);

    std::vector<PhysVec3> getPositions() const { return m_positions; }

    void addForce(const PhysVec3& force);
    void setGravity(const PhysVec3& gravity);
    void setWind(const PhysVec3& wind, float noise = 0.0f);

    void pinVertex(int index);
    void unpinVertex(int index);
    void pinVertexGroup(const std::vector<int>& indices);

    void reset();

public:
    void simulationStep(int step);

private:
    void buildConstraints();
    void satisfyConstraints(int iterations);
    void applyForces(float deltaTime);
    void resolveCollisions();

    std::vector<PhysVec3> m_positions;
    std::vector<PhysVec3> m_velocities;
    std::vector<int> m_faces;
    std::vector<std::pair<int, int>> m_edges;
    std::vector<float> m_restLengths;
    std::vector<int> m_pinnedVertices;

    ks::physics::SoftBody m_config;
    PhysVec3 m_externalForce;
    PhysVec3 m_wind;
    PhysVec3 m_gravity = PhysVec3(0, -9.81f, 0);
};

// ============================================================================
// Cloth Physics Implementation
// ============================================================================

struct ClothSimulator {
    public:
    explicit ClothSimulator();
    ~ClothSimulator();

    void setCloth(ks::physics::Cloth* cloth);
    void simulate(float deltaTime);

    void addCollisionSphere(const PhysVec3& center, float radius);
    void addCollisionBox(const std::vector<PhysVec3>& corners);
    void clearCollisions();

    void setPinnedVertices(const std::vector<int>& indices);

public:
    void clothUpdated();

private:
    void integrateVerlet(float deltaTime);
    void satisfyConstraints(float deltaTime);
    void satisfyCollisionConstraints();

    ks::physics::Cloth* m_cloth = nullptr;
    std::vector<PhysVec3> m_positions;
    std::vector<PhysVec3> m_previousPositions;

    struct CollisionSphere {
        PhysVec3 center;
        float radius;
    };
    std::vector<CollisionSphere> m_collisionSpheres;

    struct CollisionBox {
        std::vector<PhysVec3> corners;
        PhysVec3 min;
        PhysVec3 max;
    };
    std::vector<CollisionBox> m_collisionBoxes;
};

// ============================================================================
// Particle System Implementation
// ============================================================================

struct ParticleSystemSimulator {
    public:
    explicit ParticleSystemSimulator();
    ~ParticleSystemSimulator();

    void setEmitter(const ks::physics::ParticleSystem::Emitter& emitter);
    void setPhysics(const ks::physics::ParticleSystem::Physics& physics);
    void setMaxCount(int count);
    void setLifetime(int frames);

    void simulate(float deltaTime);

    const std::vector<ks::physics::ParticleSystem::Particle>& particles() const { return m_particles; }

public:
    void particlesUpdated();

private:
    std::vector<ks::physics::ParticleSystem::Particle> m_particles;
    ks::physics::ParticleSystem::Emitter m_emitter;
    ks::physics::ParticleSystem::Physics m_physics;
    int m_maxCount = 1000;
    int m_lifetime = 100;
    int m_framesSinceEmit = 0;
};

// ============================================================================
// Fluid Physics Implementation
// ============================================================================

struct FluidSimulator {
    public:
    FluidSimulator();
    ~FluidSimulator();

    enum class SimulationMode { Off, Particles, Grid };
    SimulationMode mode = SimulationMode::Particles;

    struct FluidParticle {
        PhysVec3 position;
        PhysVec3 velocity;
        float density;
        float pressure;
    };

    float gravity[3] = {0, -9.81f, 0};
    float viscosity = 0.0f;
    float stiffness = 0.0f;
    float restDensity = 1000.0f;

    float smoothingRadius = 0.2f;
    int targetNumDensity = 64;

    float timeScale = 1.0f;
    int iterations = 2;

    // Surface tension parameters
    float surfaceTension = 0.0728f;
    float surfaceThreshold = 0.5f;

    std::vector<FluidParticle> fluidParticles;

    void addParticles(const std::vector<PhysVec3>& positions);
    void simulate(float deltaTime);

    void setBoundaries(const std::vector<PhysVec3>& boundaries);

    float getParticleDensity(int index) const;
    float getParticlePressure(int index) const;

    // Export particle data as volumetric grid (VDB-compatible)
    // Returns density field as flat array with grid metadata
    struct VolumeGrid {
        std::vector<float> densityField;
        std::vector<float> velocityFieldX;
        std::vector<float> velocityFieldY;
        std::vector<float> velocityFieldZ;
        PhysVec3 gridMin;
        PhysVec3 gridMax;
        PhysVec3 voxelSize;
        int resolutionX, resolutionY, resolutionZ;
    };

    VolumeGrid exportVolumeGrid(int resolution = 64) const;
    bool exportVDB(const std::string& path, int resolution = 64) const;

public:
    void fluidUpdated();

private:
    // Spatial hash grid for O(n) neighbor search
    struct CellKey {
        int x, y, z;
        bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    
    struct CellKeyHash {
        size_t operator()(const CellKey& k) const {
            size_t h = 0;
            h ^= std::hash<int>()(k.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    
    struct SpatialHashGrid {
        float cellSize;
        std::unordered_map<CellKey, std::vector<int>, CellKeyHash> cells;

        void build(const std::vector<FluidParticle>& particles, float radius);
        std::vector<int> query(const PhysVec3& position, float radius) const;
        void clear();
    };

    SpatialHashGrid m_spatialGrid;

    float computeDensity(const PhysVec3& position);
    PhysVec3 computeDensityGradient(const PhysVec3& position);
    PhysVec3 computeSurfaceTension(const PhysVec3& position, const PhysVec3& normal);
    void computePressures();

    std::vector<PhysVec3> m_boundaries;
};

// ============================================================================
// Hair Physics Implementation
// ============================================================================

struct HairSystem {
    public:
    HairSystem();
    ~HairSystem();

    struct HairStrand {
        std::vector<PhysVec3> points;
        std::vector<PhysVec3> velocities;
        std::vector<PhysVec3> restPositions;  // Rest pose for clumping
        std::vector<float> thickness;         // Per-vertex thickness
        std::vector<float> ages;              // Per-vertex age for ICE
        int rootVertex;
        float curlFactor;                 // Per-strand curl amount
        float clumpFactor;                // Per-strand clumping amount
    };

    std::vector<HairStrand> strands;

    float segmentLength = 0.1f;
    int segments = 5;

    float gravity[3] = {0, 0, 0};
    float dynamics = 0.5f;
    float damping = 0.1f;

    // Collision parameters
    bool useCollision = true;
    float collisionRadius = 0.02f;

    // Hair-hair collision
    bool useHairCollision = true;
    float hairHairRadius = 0.01f;
    float hairHairStiffness = 10.0f;

    // Clumping parameters
    bool useClumping = false;
    float clumpStrength = 0.0f;
    float clumpRadius = 0.1f;

    // Curl parameters
    bool useCurling = false;
    float curlRadius = 0.0f;
    float curlFrequency = 1.0f;

    // Guide-follow parameters
    bool useGuideFollow = false;
    int guideStrandCount = 0;  // Number of guide strands at start
    float guideInfluence = 0.5f;

    // Texture-driven hair parameters
    struct TextureMap {
        QByteArray data;        // Raw texture data (RGBA)
        int width = 0;
        int height = 0;
        int channels = 4;
        
        // Sample texture at UV coordinates
        PhysVec4 sample(float u, float v) const;
    };
    
    TextureMap densityTexture;    // Controls hair density
    TextureMap stiffnessTexture;  // Controls hair stiffness
    TextureMap curlTexture;       // Controls curl amount
    TextureMap thicknessTexture;  // Controls hair thickness
    
    bool useTextureDensity = false;
    bool useTextureStiffness = false;
    bool useTextureCurl = false;
    bool useTextureThickness = false;

    void addStrand(int rootVertex, int count);
    void removeStrand(int index);

    void simulate(float deltaTime);
    
    // Texture-driven parameter application
    void applyTextureParameters();

    std::vector<PhysVec3> getCompletedStrands() const;

public:
    void hairUpdated();

private:
    void simulateStrand(HairStrand& strand, float deltaTime);
    void applyHairHairCollision();
    void applyClumping();
    void applyCurling();
    void applyGuideFollow();
    
    float sampleTextureChannel(const TextureMap& tex, float u, float v, int channel) const;

    std::vector<int> m_rootVertices;

    // Spatial hash for hair-hair collision
    struct StrandCellKey {
        int x, y, z;
        bool operator==(const StrandCellKey& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    
    struct StrandCellKeyHash {
        size_t operator()(const StrandCellKey& k) const {
            size_t h = 0;
            h ^= std::hash<int>()(k.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    
    struct StrandSpatialHash {
        float cellSize;
        std::unordered_map<StrandCellKey, std::vector<std::pair<int,int>>, StrandCellKeyHash> cells; // strand index, point index

        void build(const std::vector<HairStrand>& strands, float radius);
        std::vector<std::pair<int,int>> query(const PhysVec3& position, float radius) const;
        void clear();
    };

    StrandSpatialHash m_strandHash;
};

} // namespace physics
} // namespace ks