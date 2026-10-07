#pragma once

/**
 * @file PhysicsEngine.h
 * @brief Core physics engine with rigid body, soft body, and cloth simulation
 * @copyright KS Physics Engine
 */

#include "PhysicsTypes.h"
#include <QObject>
#include <QVector3D>
#include <QVector>
#include <QPair>
#include <QString>
#include <QMatrix4x4>

namespace ks {
namespace physics {

// ============================================================================
// Forward Declarations
// ============================================================================

class RigidBody;
class PhysicsWorld;
class CollisionShape;

// ============================================================================
// Rigid Body Data
// ============================================================================

/**
 * @brief Configuration data for a rigid body
 */
struct RigidBodyData {
    enum class Type {
        Static,     ///< Immovable body
        Dynamic,    ///< Fully simulated body
        Kinematic   ///< Controlled by kinematics
    };
    
    Type bodyType = Type::Dynamic;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.3f;
    float linearDamping = 0.01f;
    float angularDamping = 0.01f;
    bool collisionEnabled = true;
    bool deformationsEnabled = true;
    
    struct CollisionShape {
        // Uses CollisionShape::ShapeType from the forward declaration above
        // Values: Box, Sphere, Capsule, Cylinder, Cone, ConvexHull, Compound
        int shapeType = 1; // Default: Sphere
        QVector3D dimensions = {1.0f, 1.0f, 1.0f};
        float radius = 0.5f;
    } collisionShape;
    
    QVector3D linearVelocity;
    QVector3D angularVelocity;
    
    bool isActive() const { return bodyType != Type::Static; }
};

// ============================================================================
// Soft Body Configuration
// ============================================================================

/**
 * @brief Configuration for soft body simulation
 */
struct SoftBodyConfig {
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
// Cloth Configuration
// ============================================================================

/**
 * @brief Configuration for cloth simulation
 */
struct ClothConfig {
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
    float gravity[3] = {0.0f, -9.81f, 0.0f};
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
// Particle System
// ============================================================================

/**
 * @brief Particle system configuration
 */
struct ParticleSystemConfig {
    struct Particle {
        QVector3D position;
        QVector3D velocity;
        QVector3D acceleration;
        float lifetime = 0.0f;
        float age = 0.0f;
        float mass = 1.0f;
        float size = 0.1f;
        QVector4D color = {1.0f, 1.0f, 1.0f, 1.0f};
        
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
        float gravity[3] = {0.0f, -9.81f, 0.0f};
        float damping = 0.0f;
        float brownian = 0.0f;
        bool useWind = false;
        float wind[3] = {0.0f, 0.0f, 0.0f};
    };
    
    QVector<Particle> particles;
    Emitter emitter;
    Physics physics;
    int maxCount = 1000;
    int lifetime = 100;
    
    void emitParticles(int count);
    void update(float deltaTime);
    void clear();
    QVector4D colorRamp(float t) const;
};

// ============================================================================
// Fluid Particle
// ============================================================================

/**
 * @brief Fluid particle for SPH simulation
 */
struct FluidParticle {
    QVector3D position;
    QVector3D velocity;
    float density = 0.0f;
    float pressure = 0.0f;
};

// Type aliases for backward compatibility with PhysicsSimulations.h
using SoftBody = SoftBodyConfig;
using Cloth = ClothConfig;
using ParticleSystem = ParticleSystemConfig;

// ============================================================================
// Hair System
// ============================================================================

/**
 * @brief Hair strand for hair simulation
 */
struct HairStrandData {
    QVector<QVector3D> points;
    QVector<QVector3D> velocities;
    int rootVertex = 0;
    float curlFactor = 0.0f;
    float clumpFactor = 0.0f;
};

// ============================================================================
// Collision Shape
// ============================================================================

/**
 * @brief Collision shape for rigid bodies
 */
class CollisionShape : public QObject {
    Q_OBJECT
    
public:
    enum ShapeType {
        Box,
        Sphere,
        Capsule,
        Cylinder,
        Cone,
        ConvexHull,
        Compound
    };
    
    explicit CollisionShape(QObject* parent = nullptr);
    ~CollisionShape() override;
    
    void setType(ShapeType type) { m_type = type; }
    ShapeType type() const { return m_type; }
    
    void setDimensions(const QVector3D& dims) { m_dimensions = dims; }
    QVector3D dimensions() const { return m_dimensions; }
    
    void setMargin(float margin) { m_margin = margin; }
    float margin() const { return m_margin; }
    
    void setOffset(const QVector3D& offset) { m_offset = offset; }
    QVector3D offset() const { return m_offset; }
    
    float computeVolume() const;
    QVector3D computeInertia() const;
    
    bool intersects(const CollisionShape& other, const QMatrix4x4& transformA, 
                    const QMatrix4x4& transformB) const;
    
signals:
    void shapeModified();

private:
    ShapeType m_type = ShapeType::Box;
    QVector3D m_dimensions = {1.0f, 1.0f, 1.0f};
    QVector3D m_offset;
    float m_margin = 0.01f;
    
    bool intersectsBox(const CollisionShape& other, const QMatrix4x4& transformA,
                       const QMatrix4x4& transformB) const;
    bool intersectsSphere(const CollisionShape& other, const QMatrix4x4& transformA,
                          const QMatrix4x4& transformB) const;
};

// ============================================================================
// Rigid Body
// ============================================================================

/**
 * @brief Rigid body physics object
 */
class RigidBody : public QObject {
    Q_OBJECT
    
public:
    explicit RigidBody(QObject* parent = nullptr);
    ~RigidBody() override;
    
    // Mass and inertia
    void setMass(float mass) { m_mass = mass; }
    float mass() const { return m_mass; }
    
    void setInertia(const QVector3D& inertia) { m_inertia = inertia; }
    QVector3D inertia() const { return m_inertia; }
    
    // Transform
    void setPosition(const QVector3D& pos) { m_position = pos; emit positionChanged(); }
    QVector3D position() const { return m_position; }
    
    void setRotation(const QVector3D& euler) { m_rotation = euler; }
    QVector3D rotation() const { return m_rotation; }
    
    void setTransform(const QMatrix4x4& transform);
    QMatrix4x4 transform() const;
    
    // Velocity
    void setVelocity(const QVector3D& vel) { m_velocity = vel; emit velocityChanged(); }
    QVector3D velocity() const { return m_velocity; }
    
    void setAngularVelocity(const QVector3D& angVel) { m_angularVelocity = angVel; }
    QVector3D angularVelocity() const { return m_angularVelocity; }
    
    // Forces
    void applyForce(const QVector3D& force, const QVector3D& point = QVector3D());
    void applyImpulse(const QVector3D& impulse, const QVector3D& point = QVector3D());
    void applyTorque(const QVector3D& torque);
    
    void clearForces();
    
    // Integration
    void integrate(float dt);
    void integratePosition(float dt);
    void integrateVelocity(float dt);
    
    // Energy
    float kineticEnergy() const;
    float potentialEnergy(float gravity = Constants::GRAVITY) const;
    float totalEnergy(float gravity = Constants::GRAVITY) const;
    
    // Collision
    void setCollisionShape(CollisionShape* shape) { m_collisionShape = shape; }
    CollisionShape* collisionShape() const { return m_collisionShape; }
    
    void setRestitution(float restitution) { m_restitution = restitution; }
    float restitution() const { return m_restitution; }
    
    void setFriction(float friction) { m_friction = friction; }
    float friction() const { return m_friction; }
    
    // Flags
    void setStatic(bool isStatic) { m_isStatic = isStatic; }
    bool isStatic() const { return m_isStatic; }
    
    void setKinematic(bool isKinematic) { m_isKinematic = isKinematic; }
    bool isKinematic() const { return m_isKinematic; }
    
    void setActive(bool active) { m_isActive = active; }
    bool isActive() const { return m_isActive; }
    
signals:
    void positionChanged();
    void velocityChanged();
    void collisionDetected(RigidBody* other, const QVector3D& point, const QVector3D& normal);

private:
    float m_mass = 1.0f;
    QVector3D m_inertia = {1.0f, 1.0f, 1.0f};
    QVector3D m_position;
    QVector3D m_rotation;
    QVector3D m_velocity;
    QVector3D m_angularVelocity;
    QVector3D m_accumulatedForce;
    QVector3D m_accumulatedTorque;
    float m_restitution = 0.3f;
    float m_friction = 0.5f;
    bool m_isStatic = false;
    bool m_isKinematic = false;
    bool m_isActive = true;
    CollisionShape* m_collisionShape = nullptr;
};

// ============================================================================
// Physics World
// ============================================================================

/**
 * @brief Physics world managing all physics objects
 */
class PhysicsWorld : public QObject {
    Q_OBJECT
    
public:
    enum class BroadphaseType {
        Simple,   ///< Simple O(n²) broadphase
        SAP,      ///< Sweep and prune
        DBVT      ///< Dynamic bounding volume tree
    };
    
    explicit PhysicsWorld(QObject* parent = nullptr);
    ~PhysicsWorld() override;
    
    // World settings
    void setGravity(const QVector3D& gravity) { m_gravity = gravity; }
    QVector3D gravity() const { return m_gravity; }
    
    void setSolverIterations(int iterations) { m_solverIterations = iterations; }
    int solverIterations() const { return m_solverIterations; }
    
    void setFixedTimeStep(float dt) { m_fixedTimeStep = dt; }
    float fixedTimeStep() const { return m_fixedTimeStep; }
    
    void setBroadphase(BroadphaseType type) { m_broadphaseType = type; }
    BroadphaseType broadphase() const { return m_broadphaseType; }
    
    // Body management
    RigidBody* createBody(float mass = 1.0f);
    void addBody(RigidBody* body);
    void removeBody(RigidBody* body);
    QVector<RigidBody*> allBodies() const { return m_bodies; }
    void clearBodies();
    
    // Simulation
    void stepSimulation(float deltaTime);
    void stepSimulationFixed(float deltaTime);
    
    // Collision detection
    bool checkCollision(RigidBody* bodyA, RigidBody* bodyB, 
                       QVector3D& contactPoint, QVector3D& contactNormal);
    QVector<QPair<RigidBody*, RigidBody*>> getCollisionPairs();
    
    // Ray casting
    struct RaycastResult {
        bool hit = false;
        QVector3D point;
        QVector3D normal;
        float distance = 0.0f;
        RigidBody* body = nullptr;
    };
    RaycastResult raycast(const QVector3D& origin, const QVector3D& direction, float maxDistance = 1000.0f);
    
    // Debug
    void debugDraw();
    void setDebugMode(bool enabled) { m_debugMode = enabled; }
    bool debugMode() const { return m_debugMode; }
    
signals:
    void stepCompleted();
    void collisionDetected(RigidBody* bodyA, RigidBody* bodyB, const QVector3D& point, const QVector3D& normal);
    void bodyAdded(RigidBody* body);
    void bodyRemoved(RigidBody* body);

private:
    void solveConstraints();
    void broadphaseSAP();
    void broadphaseDBVT();
    void resolveCollision(RigidBody* bodyA, RigidBody* bodyB, const QVector3D& point, const QVector3D& normal);
    void updateBroadphase();
    
    QVector3D m_gravity = {0.0f, -Constants::GRAVITY, 0.0f};
    int m_solverIterations = 10;
    float m_fixedTimeStep = 0.001f;
    QVector<RigidBody*> m_bodies;
    BroadphaseType m_broadphaseType = BroadphaseType::SAP;
    bool m_debugMode = false;
    float m_accumulatedTime = 0.0f;
    
    // Broadphase structures
    struct BodyBounds {
        RigidBody* body;
        QVector3D min;
        QVector3D max;
    };
    QVector<BodyBounds> m_bounds;
};

// ============================================================================
// ISimulator Interface
// ============================================================================

/**
 * @brief Base interface for all physics simulators
 */
class ISimulator : public QObject {
    Q_OBJECT
    
public:
    explicit ISimulator(QObject* parent = nullptr) : QObject(parent) {}
    ~ISimulator() override = default;
    
    // Lifecycle
    virtual void startSimulation() = 0;
    virtual void stopSimulation() = 0;
    virtual void reset() = 0;
    virtual void updatePhysics(double dt) = 0;
    virtual bool isRunning() const = 0;
    virtual SimulationState getState() const = 0;
    
    // Configuration
    virtual void setTimeMultiplier(float multiplier) { m_timeMultiplier = multiplier; }
    virtual float timeMultiplier() const { return m_timeMultiplier; }
    
    // Debug
    virtual void setDebugMode(bool enabled) { m_debugMode = enabled; }
    virtual bool debugMode() const { return m_debugMode; }
    
signals:
    void stateUpdated(const SimulationState& state);
    void simulationStarted();
    void simulationStopped();
    void simulationReset();
    void errorOccurred(const QString& error);

protected:
    float m_timeMultiplier = 1.0f;
    bool m_debugMode = false;
};

} // namespace physics
} // namespace ks