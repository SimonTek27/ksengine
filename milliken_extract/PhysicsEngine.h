#pragma once

/**
 * @file PhysicsEngine.h
 * @brief Core physics engine with rigid body, soft body, and cloth simulation
 * @copyright KS Physics Engine
 *
 * Qt-free. Uses PhysVec3 from PhysicsCoreTypes.h and std::vector / std::function.
 */

#include "PhysicsCoreTypes.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ks {
namespace physics {

// PhysVec3, Constants, SimulationState: PhysicsCoreTypes.h

struct PhysMat4 {
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    float& operator()(int row, int col) { return m[col * 4 + row]; }
    float operator()(int row, int col) const { return m[col * 4 + row]; }
    void setToIdentity() {
        for (int i = 0; i < 16; ++i) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
    PhysVec3 translation() const { return {m[12], m[13], m[14]}; }
    void setTranslation(const PhysVec3& t) { m[12] = t.x; m[13] = t.y; m[14] = t.z; }
};

struct PhysVec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    PhysVec4() = default;
    PhysVec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

// ============================================================================
// Forward Declarations
// ============================================================================

class RigidBody;
class PhysicsWorld;
class CollisionShape;

// ============================================================================
// Rigid Body Data
// ============================================================================

struct RigidBodyData {
    enum class Type {
        Static,
        Dynamic,
        Kinematic
    };

    Type bodyType = Type::Dynamic;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.3f;
    float linearDamping = 0.01f;
    float angularDamping = 0.01f;
    bool collisionEnabled = true;
    bool deformationsEnabled = true;

    struct CollisionShapeDesc {
        int shapeType = 1; // Sphere default (see CollisionShape::ShapeType)
        PhysVec3 dimensions{1.0f, 1.0f, 1.0f};
        float radius = 0.5f;
    } collisionShape;

    PhysVec3 linearVelocity;
    PhysVec3 angularVelocity;

    bool isActive() const { return bodyType != Type::Static; }
};

// ============================================================================
// Soft Body / Cloth / Particles
// ============================================================================

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

struct ClothConfig {
    struct Vertex {
        PhysVec3 position;
        PhysVec3 previousPosition;
        PhysVec3 acceleration;
        float mass = 1.0f;
        bool pinned = false;
    };

    struct Constraint {
        int vertex1 = 0;
        int vertex2 = 0;
        float restLength = 0.0f;
        float stiffness = 1.0f;
    };

    std::vector<Vertex> vertices;
    std::vector<Constraint> constraints;
    float gravity[3] = {0.0f, -9.81f, 0.0f};
    float structural = 1.0f;
    float shear = 1.0f;
    float bending = 0.1f;
    bool useDynamicMesh = true;
    float velocitySmooth = 0.9f;
    float damping = 0.01f;
    bool useCustomPhysics = false;
    int solverType = 0;
    PhysVec3 wind;
    float windNoise = 0.0f;
};

struct ParticleSystemConfig {
    struct Particle {
        PhysVec3 position;
        PhysVec3 velocity;
        PhysVec3 acceleration;
        float lifetime = 0.0f;
        float age = 0.0f;
        float mass = 1.0f;
        float size = 0.1f;
        PhysVec4 color{1.0f, 1.0f, 1.0f, 1.0f};

        bool isAlive() const { return age < lifetime; }
    };

    struct Emitter {
        PhysVec3 position;
        PhysVec3 direction;
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

    std::vector<Particle> particles;
    Emitter emitter;
    Physics physics;
    int maxCount = 1000;
    int lifetime = 100;

    void emitParticles(int count);
    void update(float deltaTime);
    void clear();
    PhysVec4 colorRamp(float t) const;
};

struct FluidParticle {
    PhysVec3 position;
    PhysVec3 velocity;
    float density = 0.0f;
    float pressure = 0.0f;
};

using SoftBody = SoftBodyConfig;
using Cloth = ClothConfig;
using ParticleSystem = ParticleSystemConfig;

struct HairStrandData {
    std::vector<PhysVec3> points;
    std::vector<PhysVec3> velocities;
    int rootVertex = 0;
    float curlFactor = 0.0f;
    float clumpFactor = 0.0f;
};

// ============================================================================
// Collision Shape
// ============================================================================

class CollisionShape {
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

    CollisionShape() = default;
    ~CollisionShape() = default;

    CollisionShape(const CollisionShape&) = delete;
    CollisionShape& operator=(const CollisionShape&) = delete;

    void setType(ShapeType type) { m_type = type; }
    ShapeType type() const { return m_type; }

    void setDimensions(const PhysVec3& dims) { m_dimensions = dims; }
    PhysVec3 dimensions() const { return m_dimensions; }

    void setMargin(float margin) { m_margin = margin; }
    float margin() const { return m_margin; }

    void setOffset(const PhysVec3& offset) { m_offset = offset; }
    PhysVec3 offset() const { return m_offset; }

    void setRadius(float radius) { m_radius = radius; }
    float radius() const { return m_radius; }

    void setHeight(float height) { m_height = height; }
    float height() const { return m_height; }

    void setLocalScaling(const PhysVec3& scale) { m_localScaling = scale; }
    PhysVec3 localScaling() const { return m_localScaling; }

    void setConvexHullPoints(const std::vector<PhysVec3>& points) { m_hullPoints = points; }
    const std::vector<PhysVec3>& convexHullPoints() const { return m_hullPoints; }

    float computeVolume() const;
    PhysVec3 computeInertia() const;

private:
    ShapeType m_type = Sphere;
    PhysVec3 m_dimensions{1.0f, 1.0f, 1.0f};
    PhysVec3 m_offset;
    PhysVec3 m_localScaling{1.0f, 1.0f, 1.0f};
    float m_margin = 0.04f;
    float m_radius = 0.5f;
    float m_height = 1.0f;
    std::vector<PhysVec3> m_hullPoints;
};

// ============================================================================
// Rigid Body
// ============================================================================

class RigidBody {
public:
    RigidBody() = default;
    ~RigidBody() = default;

    RigidBody(const RigidBody&) = delete;
    RigidBody& operator=(const RigidBody&) = delete;

    void setMass(float mass) { m_mass = mass; }
    float mass() const { return m_mass; }

    void setInertia(const PhysVec3& inertia) { m_inertia = inertia; }
    PhysVec3 inertia() const { return m_inertia; }

    void setPosition(const PhysVec3& pos) {
        m_position = pos;
        if (onPositionChanged) onPositionChanged();
    }
    PhysVec3 position() const { return m_position; }

    void setRotation(const PhysVec3& euler) { m_rotation = euler; }
    PhysVec3 rotation() const { return m_rotation; }

    void setTransform(const PhysMat4& transform);
    PhysMat4 transform() const;

    void setVelocity(const PhysVec3& vel) {
        m_velocity = vel;
        if (onVelocityChanged) onVelocityChanged();
    }
    PhysVec3 velocity() const { return m_velocity; }

    void setAngularVelocity(const PhysVec3& angVel) { m_angularVelocity = angVel; }
    PhysVec3 angularVelocity() const { return m_angularVelocity; }

    void applyForce(const PhysVec3& force, const PhysVec3& point = PhysVec3{});
    void applyImpulse(const PhysVec3& impulse, const PhysVec3& point = PhysVec3{});
    void applyTorque(const PhysVec3& torque);

    void clearForces();

    void integrate(float dt);
    void integratePosition(float dt);
    void integrateVelocity(float dt);

    float kineticEnergy() const;
    float potentialEnergy(float gravity = Constants::GRAVITY) const;
    float totalEnergy(float gravity = Constants::GRAVITY) const;

    void setCollisionShape(CollisionShape* shape) { m_collisionShape = shape; }
    CollisionShape* collisionShape() const { return m_collisionShape; }

    void setRestitution(float restitution) { m_restitution = restitution; }
    float restitution() const { return m_restitution; }

    void setFriction(float friction) { m_friction = friction; }
    float friction() const { return m_friction; }

    void setStatic(bool isStatic) { m_isStatic = isStatic; }
    bool isStatic() const { return m_isStatic; }

    void setKinematic(bool isKinematic) { m_isKinematic = isKinematic; }
    bool isKinematic() const { return m_isKinematic; }

    void setActive(bool active) { m_isActive = active; }
    bool isActive() const { return m_isActive; }

    // Callbacks (replace Qt signals)
    std::function<void()> onPositionChanged;
    std::function<void()> onVelocityChanged;
    std::function<void(RigidBody* other, const PhysVec3& point, const PhysVec3& normal)> onCollisionDetected;

private:
    float m_mass = 1.0f;
    PhysVec3 m_inertia{1.0f, 1.0f, 1.0f};
    PhysVec3 m_position;
    PhysVec3 m_rotation;
    PhysVec3 m_velocity;
    PhysVec3 m_angularVelocity;
    PhysVec3 m_accumulatedForce;
    PhysVec3 m_accumulatedTorque;
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

class PhysicsWorld {
public:
    enum class BroadphaseType {
        Simple,
        SAP,
        DBVT
    };

    PhysicsWorld() = default;
    ~PhysicsWorld() = default;

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void setGravity(const PhysVec3& gravity) { m_gravity = gravity; }
    PhysVec3 gravity() const { return m_gravity; }

    void setSolverIterations(int iterations) { m_solverIterations = iterations; }
    int solverIterations() const { return m_solverIterations; }

    void setFixedTimeStep(float dt) { m_fixedTimeStep = dt; }
    float fixedTimeStep() const { return m_fixedTimeStep; }

    void setBroadphase(BroadphaseType type) { m_broadphaseType = type; }
    BroadphaseType broadphase() const { return m_broadphaseType; }

    RigidBody* createBody(float mass = 1.0f);
    void addBody(RigidBody* body);
    void removeBody(RigidBody* body);
    std::vector<RigidBody*> allBodies() const { return m_bodies; }
    void clearBodies();

    void stepSimulation(float deltaTime);
    void stepSimulationFixed(float deltaTime);

    bool checkCollision(RigidBody* bodyA, RigidBody* bodyB,
                        PhysVec3& contactPoint, PhysVec3& contactNormal);
    std::vector<std::pair<RigidBody*, RigidBody*>> getCollisionPairs();

    struct RaycastResult {
        bool hit = false;
        PhysVec3 point;
        PhysVec3 normal;
        float distance = 0.0f;
        RigidBody* body = nullptr;
    };
    RaycastResult raycast(const PhysVec3& origin, const PhysVec3& direction,
                          float maxDistance = 1000.0f);

    void debugDraw();
    void setDebugMode(bool enabled) { m_debugMode = enabled; }
    bool debugMode() const { return m_debugMode; }

    // Callbacks (replace Qt signals)
    std::function<void()> onStepCompleted;
    std::function<void(RigidBody* bodyA, RigidBody* bodyB, const PhysVec3& point, const PhysVec3& normal)> onCollisionDetected;
    std::function<void(RigidBody* body)> onBodyAdded;
    std::function<void(RigidBody* body)> onBodyRemoved;

private:
    void solveConstraints();
    void broadphaseSAP();
    void broadphaseDBVT();
    void resolveCollision(RigidBody* bodyA, RigidBody* bodyB, const PhysVec3& point, const PhysVec3& normal);
    void updateBroadphase();

    PhysVec3 m_gravity{0.0f, -Constants::GRAVITY, 0.0f};
    int m_solverIterations = 10;
    float m_fixedTimeStep = 0.001f;
    std::vector<RigidBody*> m_bodies;
    BroadphaseType m_broadphaseType = BroadphaseType::SAP;
    bool m_debugMode = false;
    float m_accumulatedTime = 0.0f;

    struct BodyBounds {
        RigidBody* body = nullptr;
        PhysVec3 min;
        PhysVec3 max;
    };
    std::vector<BodyBounds> m_bounds;
};

// ============================================================================
// ISimulator Interface
// ============================================================================

class ISimulator {
public:
    ISimulator() = default;
    virtual ~ISimulator() = default;

    ISimulator(const ISimulator&) = delete;
    ISimulator& operator=(const ISimulator&) = delete;

    virtual void startSimulation() = 0;
    virtual void stopSimulation() = 0;
    virtual void reset() = 0;
    virtual void updatePhysics(double dt) = 0;
    virtual bool isRunning() const = 0;
    virtual SimulationState getState() const = 0;

    virtual void setTimeMultiplier(float multiplier) { m_timeMultiplier = multiplier; }
    virtual float timeMultiplier() const { return m_timeMultiplier; }

    virtual void setDebugMode(bool enabled) { m_debugMode = enabled; }
    virtual bool debugMode() const { return m_debugMode; }

    // Callbacks (replace Qt signals)
    std::function<void(const SimulationState& state)> onStateUpdated;
    std::function<void()> onSimulationStarted;
    std::function<void()> onSimulationStopped;
    std::function<void()> onSimulationReset;
    std::function<void(const std::string& error)> onErrorOccurred;

protected:
    float m_timeMultiplier = 1.0f;
    bool m_debugMode = false;
};

} // namespace physics
} // namespace ks
