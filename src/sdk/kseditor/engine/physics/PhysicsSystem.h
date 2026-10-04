#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QMatrix4x4>
#include <QVector3D>

namespace ks {
namespace physics {

// ============================================================================
// Rigid Body Physics - Data Structures
// ============================================================================

struct RigidBodyData {
    enum class Type { Static, Dynamic, Kinematic };
    Type bodyType = Type::Dynamic;

    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.3f;
    float linearDamping = 0.01f;
    float angularDamping = 0.01f;

    bool collisionEnabled = true;
    bool deformationsEnabled = true;

    struct CollisionShape {
        enum ShapeType { Sphere, Box, Capsule, Cylinder, ConvexHull, Mesh };
        ShapeType shapeType;
        QVector3D dimensions = {1, 1, 1};
        float radius = 0.5f;
    } collisionShape;

    QVector3D linearVelocity;
    QVector3D angularVelocity;

    bool isActive() const { return bodyType != Type::Static; }
};

// ============================================================================
// Soft Body Physics - Data Structures
// ============================================================================

struct SoftBody {
    enum class Config {
        Mass,
        Stiffness,
        Damping,
        Pressure,
        Volume
    };

    float mass = 1.0f;
    float stiffness = 0.5f;
    float damping = 0.01f;
    float pressure = 0.0f;
    float volume = 0.0f;

    bool useBendConstraints = true;
    bool useShapeConstraints = true;
    bool usePressure = false;

    int iterationCount = 5;
    float collisionMargin = 0.02f;
};

// ============================================================================
// Cloth Physics - Data Structures
// ============================================================================

struct Cloth {
    struct Vertex {
        QVector3D position;
        QVector3D previousPosition;
        QVector3D acceleration;
        float mass = 1.0f;
        bool pinned = false;
    };

    struct Constraint {
        int vertex1;
        int vertex2;
        float restLength;
        float stiffness = 1.0f;
    };

    QVector<Vertex> vertices;
    QVector<Constraint> constraints;

    float gravity[3] = {0, -9.81f, 0};
    float structural = 1.0f;
    float shear = 1.0f;
    float bending = 0.1f;

    bool useDynamicMesh = true;
    float velocitySmooth = 0.9f;
    float damping = 0.01f;

    bool useCustomPhysics = false;
    int solverType = 0;

    QVector3D wind;
    float windNoise = 0.0f;
};

// ============================================================================
// Particle System - Data Structures
// ============================================================================

struct ParticleSystem {
public:
    struct Particle {
        QVector3D position;
        QVector3D velocity;
        QVector3D acceleration;
        float lifetime;
        float age;
        float mass;
        float size;
        QVector4D color;

        bool isAlive() const { return age < lifetime; }
    };

    struct Emitter {
        QVector3D position;
        QVector3D direction;
        float angle = 0.0f;
        float velocity = 1.0f;
        float rate = 100.0f;

        enum class Shape { Point, Circle, Sphere, Plane };
        Shape shape = Shape::Point;
    };

    struct Physics {
        float gravity[3] = {0, -9.81f, 0};
        float damping = 0.0f;
        float brownian = 0.0f;

        bool useWind = false;
        float wind[3] = {0, 0, 0};
    };

    QVector<Particle> particles;
    Emitter emitter;
    Physics physics;

    int maxCount = 1000;
    int lifetime = 100;

    void emitParticles(int count);
    void update(float deltaTime);
    void clear();

    QVector4D colorRamp(float t);
};

// ============================================================================
// Fluid Physics - Data Structures
// ============================================================================

struct FluidParticle {
    QVector3D position;
    QVector3D velocity;
    float density;
    float pressure;
};

// ============================================================================
// Hair Physics - Data Structures
// ============================================================================

struct HairStrand {
    QVector<QVector3D> points;
    QVector<QVector3D> velocities;
    int rootVertex;
};

} // namespace physics
} // namespace ks