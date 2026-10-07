#pragma once

#include "PhysicsEngine.h"
#include <QVector3D>
#include <QVector>
#include <QPair>
#include <QRandomGenerator>
#include <QFile>
#include <QTextStream>
#include <unordered_map>

namespace ks {
namespace physics {

// ============================================================================
// Soft Body Physics Implementation
// ============================================================================

struct SoftBodySimulator : public QObject {
    Q_OBJECT

public:
    explicit SoftBodySimulator(QObject* parent = nullptr);
    ~SoftBodySimulator();

    void setMesh(const QVector<QVector3D>& vertices, const QVector<int>& faces);
    void setConfig(const ks::physics::SoftBody& config);

    void simulate(int frameStep);

    QVector<QVector3D> getPositions() const { return m_positions; }

    void addForce(const QVector3D& force);
    void setGravity(const QVector3D& gravity);
    void setWind(const QVector3D& wind, float noise = 0.0f);

    void pinVertex(int index);
    void unpinVertex(int index);
    void pinVertexGroup(const QVector<int>& indices);

    void reset();

signals:
    void simulationStep(int step);

private:
    void buildConstraints();
    void satisfyConstraints(int iterations);
    void applyForces(float deltaTime);
    void resolveCollisions();

    QVector<QVector3D> m_positions;
    QVector<QVector3D> m_velocities;
    QVector<int> m_faces;
    QVector<QPair<int, int>> m_edges;
    QVector<float> m_restLengths;
    QVector<int> m_pinnedVertices;

    ks::physics::SoftBody m_config;
    QVector3D m_externalForce;
    QVector3D m_wind;
    QVector3D m_gravity = QVector3D(0, -9.81f, 0);
};

// ============================================================================
// Cloth Physics Implementation
// ============================================================================

struct ClothSimulator : public QObject {
    Q_OBJECT

public:
    explicit ClothSimulator(QObject* parent = nullptr);
    ~ClothSimulator();

    void setCloth(ks::physics::Cloth* cloth);
    void simulate(float deltaTime);

    void addCollisionSphere(const QVector3D& center, float radius);
    void addCollisionBox(const QVector<QVector3D>& corners);
    void clearCollisions();

    void setPinnedVertices(const QVector<int>& indices);

signals:
    void clothUpdated();

private:
    void integrateVerlet(float deltaTime);
    void satisfyConstraints(float deltaTime);
    void satisfyCollisionConstraints();

    ks::physics::Cloth* m_cloth = nullptr;
    QVector<QVector3D> m_positions;
    QVector<QVector3D> m_previousPositions;

    struct CollisionSphere {
        QVector3D center;
        float radius;
    };
    QVector<CollisionSphere> m_collisionSpheres;

    struct CollisionBox {
        QVector<QVector3D> corners;
        QVector3D min;
        QVector3D max;
    };
    QVector<CollisionBox> m_collisionBoxes;
};

// ============================================================================
// Particle System Implementation
// ============================================================================

struct ParticleSystemSimulator : public QObject {
    Q_OBJECT

public:
    explicit ParticleSystemSimulator(QObject* parent = nullptr);
    ~ParticleSystemSimulator();

    void setEmitter(const ks::physics::ParticleSystem::Emitter& emitter);
    void setPhysics(const ks::physics::ParticleSystem::Physics& physics);
    void setMaxCount(int count);
    void setLifetime(int frames);

    void simulate(float deltaTime);

    const QVector<ks::physics::ParticleSystem::Particle>& particles() const { return m_particles; }

signals:
    void particlesUpdated();

private:
    QVector<ks::physics::ParticleSystem::Particle> m_particles;
    ks::physics::ParticleSystem::Emitter m_emitter;
    ks::physics::ParticleSystem::Physics m_physics;
    int m_maxCount = 1000;
    int m_lifetime = 100;
    int m_framesSinceEmit = 0;
};

// ============================================================================
// Fluid Physics Implementation
// ============================================================================

struct FluidSimulator : public QObject {
    Q_OBJECT

public:
    FluidSimulator(QObject* parent = nullptr);
    ~FluidSimulator();

    enum class SimulationMode { Off, Particles, Grid };
    SimulationMode mode = SimulationMode::Particles;

    struct FluidParticle {
        QVector3D position;
        QVector3D velocity;
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

    QVector<FluidParticle> fluidParticles;

    void addParticles(const QVector<QVector3D>& positions);
    void simulate(float deltaTime);

    void setBoundaries(const QVector<QVector3D>& boundaries);

    float getParticleDensity(int index) const;
    float getParticlePressure(int index) const;

    // Export particle data as volumetric grid (VDB-compatible)
    // Returns density field as flat array with grid metadata
    struct VolumeGrid {
        QVector<float> densityField;
        QVector<float> velocityFieldX;
        QVector<float> velocityFieldY;
        QVector<float> velocityFieldZ;
        QVector3D gridMin;
        QVector3D gridMax;
        QVector3D voxelSize;
        int resolutionX, resolutionY, resolutionZ;
    };

    VolumeGrid exportVolumeGrid(int resolution = 64) const;
    bool exportVDB(const QString& path, int resolution = 64) const;

signals:
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
        std::unordered_map<CellKey, QVector<int>, CellKeyHash> cells;

        void build(const QVector<FluidParticle>& particles, float radius);
        QVector<int> query(const QVector3D& position, float radius) const;
        void clear();
    };

    SpatialHashGrid m_spatialGrid;

    float computeDensity(const QVector3D& position);
    QVector3D computeDensityGradient(const QVector3D& position);
    QVector3D computeSurfaceTension(const QVector3D& position, const QVector3D& normal);
    void computePressures();

    QVector<QVector3D> m_boundaries;
};

// ============================================================================
// Hair Physics Implementation
// ============================================================================

struct HairSystem : public QObject {
    Q_OBJECT

public:
    HairSystem(QObject* parent = nullptr);
    ~HairSystem();

    struct HairStrand {
        QVector<QVector3D> points;
        QVector<QVector3D> velocities;
        QVector<QVector3D> restPositions;  // Rest pose for clumping
        QVector<float> thickness;         // Per-vertex thickness
        QVector<float> ages;              // Per-vertex age for ICE
        int rootVertex;
        float curlFactor;                 // Per-strand curl amount
        float clumpFactor;                // Per-strand clumping amount
    };

    QVector<HairStrand> strands;

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
        QVector4D sample(float u, float v) const;
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

    QVector<QVector3D> getCompletedStrands() const;

signals:
    void hairUpdated();

private:
    void simulateStrand(HairStrand& strand, float deltaTime);
    void applyHairHairCollision();
    void applyClumping();
    void applyCurling();
    void applyGuideFollow();
    
    float sampleTextureChannel(const TextureMap& tex, float u, float v, int channel) const;

    QVector<int> m_rootVertices;

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
        std::unordered_map<StrandCellKey, QVector<QPair<int,int>>, StrandCellKeyHash> cells; // strand index, point index

        void build(const QVector<HairStrand>& strands, float radius);
        QVector<QPair<int,int>> query(const QVector3D& position, float radius) const;
        void clear();
    };

    StrandSpatialHash m_strandHash;
};

} // namespace physics
} // namespace ks