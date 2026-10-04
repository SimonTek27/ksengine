#pragma once

#include <QObject>
#include <QVector3D>
#include <QVector>
#include <QPair>

#include "PhysicsSystem.h"

namespace ks {
namespace physics {

// ============================================================================
// Rigid Body Physics - Core Engine Classes
// ============================================================================

class RigidBody : public QObject
{
    Q_OBJECT
public:
    explicit RigidBody(QObject* parent = nullptr) : QObject(parent) {}
    ~RigidBody() {}

    void setMass(float mass) { m_mass = mass; }
    float mass() const { return m_mass; }

    void setPosition(const QVector3D& pos) { m_position = pos; }
    QVector3D position() const { return m_position; }

    void setRotation(const QVector3D& euler) { m_rotation = euler; }
    QVector3D rotation() const { return m_rotation; }

    void setVelocity(const QVector3D& vel) { m_velocity = vel; }
    QVector3D velocity() const { return m_velocity; }

    void setAngularVelocity(const QVector3D& angVel) { m_angularVelocity = angVel; }
    QVector3D angularVelocity() const { return m_angularVelocity; }

    void applyForce(const QVector3D& force, const QVector3D& point = QVector3D());
    void applyImpulse(const QVector3D& impulse, const QVector3D& point = QVector3D());

    void integrate(float dt);

    float kineticEnergy() const;
    float potentialEnergy(float gravity = 9.81f) const;

signals:
    void positionChanged();
    void velocityChanged();

private:
    float m_mass = 1.0f;
    QVector3D m_position;
    QVector3D m_rotation;
    QVector3D m_velocity;
    QVector3D m_angularVelocity;
    QVector3D m_accumulatedForce;
    QVector3D m_accumulatedTorque;
    QVector3D m_gravity = QVector3D(0, -9.81f, 0);
};

class CollisionShape : public QObject
{
    Q_OBJECT
public:
    explicit CollisionShape(QObject* parent = nullptr) : QObject(parent) {}
    ~CollisionShape() {}

    enum ShapeType { Box, Sphere, Capsule, Cylinder, Cone, ConvexHull, Compound };

    void setType(ShapeType type) { m_type = type; }
    ShapeType type() const { return m_type; }

    void setDimensions(const QVector3D& dims) { m_dimensions = dims; }
    QVector3D dimensions() const { return m_dimensions; }

    void setMargin(float margin) { m_margin = margin; }
    float margin() const { return m_margin; }

    float computeVolume() const;
    QVector3D computeInertia() const;

signals:
    void shapeModified();

private:
    ShapeType m_type = Box;
    QVector3D m_dimensions = {1, 1, 1};
    float m_margin = 0.01f;
};

class PhysicsWorld : public QObject
{
    Q_OBJECT
public:
    explicit PhysicsWorld(QObject* parent = nullptr) : QObject(parent) {}
    ~PhysicsWorld() {}

    void setGravity(const QVector3D& g) { m_gravity = g; }
    QVector3D gravity() const { return m_gravity; }

    void setSolverIterations(int iterations) { m_solverIterations = iterations; }
    int solverIterations() const { return m_solverIterations; }

    void setFixedTimeStep(float dt) { m_fixedTimeStep = dt; }
    float fixedTimeStep() const { return m_fixedTimeStep; }

    RigidBody* createBody(float mass);
    void addBody(RigidBody* body);
    void removeBody(RigidBody* body);
    QVector<RigidBody*> allBodies() const { return m_bodies; }

    void stepSimulation(float deltaTime);
    void debugDraw();

    enum BroadphaseType { Simple, SAP, DBVT };

    void setBroadphase(BroadphaseType type) { m_broadphase = type; }

signals:
    void stepCompleted();

private:
    void solveConstraints();

    QVector3D m_gravity = {0, -9.81f, 0};
    int m_solverIterations = 10;
    float m_fixedTimeStep = 1.0f / 60.0f;
    QVector<RigidBody*> m_bodies;
    BroadphaseType m_broadphase = SAP;
};

} // namespace physics
} // namespace ks