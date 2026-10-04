#include "PhysicsEngine.h"
#include <QtMath>
#include <algorithm>

namespace ks { namespace physics {

// ============================================================================
// RigidBody implementation
// ============================================================================

void RigidBody::applyForce(const QVector3D& force, const QVector3D& point) {
    m_accumulatedForce += force;
    if (!point.isNull()) {
        QVector3D r = point - m_position;
        m_accumulatedTorque += QVector3D::crossProduct(r, force);
    }
}

void RigidBody::applyImpulse(const QVector3D& impulse, const QVector3D& point) {
    m_velocity += impulse / m_mass;
    if (!point.isNull()) {
        QVector3D r = point - m_position;
        m_angularVelocity += QVector3D::crossProduct(r, impulse) / (m_mass * 0.1f);
    }
}

void RigidBody::integrate(float dt) {
    if (m_mass <= 0) return;

    QVector3D acceleration = m_accumulatedForce / m_mass;
    m_velocity += acceleration * dt;
    m_velocity += m_gravity * dt;
    m_position += m_velocity * dt;

    m_angularVelocity += m_accumulatedTorque * dt / m_mass;
    m_rotation += m_angularVelocity * dt;

    m_accumulatedForce = QVector3D();
    m_accumulatedTorque = QVector3D();
}

float RigidBody::kineticEnergy() const {
    float linearKe = 0.5f * m_mass * m_velocity.lengthSquared();
    float angularKe = 0.5f * m_mass * m_angularVelocity.lengthSquared() * 0.1f;
    return linearKe + angularKe;
}

float RigidBody::potentialEnergy(float gravity) const {
    return m_mass * gravity * m_position.y();
}

// ============================================================================
// CollisionShape implementation
// ============================================================================

float CollisionShape::computeVolume() const {
    float vol = 1.0f;
    switch (m_type) {
    case Box:
        vol = m_dimensions.x() * m_dimensions.y() * m_dimensions.z();
        break;
    case Sphere:
        vol = (4.0f / 3.0f) * M_PI * m_dimensions.x() * m_dimensions.x() * m_dimensions.x();
        break;
    case Capsule:
        vol = M_PI * m_dimensions.x() * m_dimensions.x() * (m_dimensions.y() + (4.0f / 3.0f) * m_dimensions.x());
        break;
    case Cylinder:
        vol = M_PI * m_dimensions.x() * m_dimensions.x() * m_dimensions.y();
        break;
    case Cone:
        vol = (1.0f / 3.0f) * M_PI * m_dimensions.x() * m_dimensions.x() * m_dimensions.y();
        break;
    default:
        vol = m_dimensions.x() * m_dimensions.y() * m_dimensions.z();
        break;
    }
    return qMax(vol, 0.001f);
}

QVector3D CollisionShape::computeInertia() const {
    QVector3D inertia;
    float vol = computeVolume();
    float density = 1.0f / vol;

    switch (m_type) {
    case Box: {
        float x = m_dimensions.x(), y = m_dimensions.y(), z = m_dimensions.z();
        inertia = QVector3D((1.0f / 12.0f) * density * (y * y + z * z),
                            (1.0f / 12.0f) * density * (x * x + z * z),
                            (1.0f / 12.0f) * density * (x * x + y * y));
        break;
    }
    case Sphere: {
        float r = m_dimensions.x();
        inertia = QVector3D(0.4f * density * r * r,
                            0.4f * density * r * r,
                            0.4f * density * r * r);
        break;
    }
    case Capsule: {
        float r = m_dimensions.x(), h = m_dimensions.y();
        float ih = 0.0833f * density * (4.0f * r * r + h * h) + 0.5f * density * r * r;
        float ip = 0.5f * density * r * r;
        inertia = QVector3D(ih, ip, ih);
        break;
    }
    case Cylinder: {
        float r = m_dimensions.x(), h = m_dimensions.y();
        inertia = QVector3D((1.0f / 12.0f) * density * (3.0f * r * r + h * h),
                            0.5f * density * r * r,
                            (1.0f / 12.0f) * density * (3.0f * r * r + h * h));
        break;
    }
    default:
        inertia = QVector3D(1, 1, 1);
        break;
    }
    return inertia;
}

// ============================================================================
// PhysicsWorld implementation
// ============================================================================

RigidBody* PhysicsWorld::createBody(float mass) {
    auto* body = new RigidBody(this);
    body->setMass(mass);
    m_bodies.append(body);
    return body;
}

void PhysicsWorld::addBody(RigidBody* body) {
    if (body && !m_bodies.contains(body)) {
        m_bodies.append(body);
    }
}

void PhysicsWorld::removeBody(RigidBody* body) {
    m_bodies.removeAll(body);
    delete body;
}

void PhysicsWorld::stepSimulation(float deltaTime) {
    float dt = qMin(deltaTime, 0.05f);
    int steps = qMax(1, static_cast<int>(dt / m_fixedTimeStep));
    float subDt = dt / steps;

    for (int i = 0; i < steps; ++i) {
        solveConstraints();
        for (auto* body : m_bodies) {
            if (body) {
                body->integrate(subDt);
            }
        }
    }

    emit stepCompleted();
}

void PhysicsWorld::debugDraw() {
}

void PhysicsWorld::solveConstraints() {
    for (int iter = 0; iter < m_solverIterations; ++iter) {
        for (int i = 0; i < m_bodies.size(); ++i) {
            for (int j = i + 1; j < m_bodies.size(); ++j) {
                auto* a = m_bodies[i];
                auto* b = m_bodies[j];
                if (!a || !b) continue;

                QVector3D diff = b->position() - a->position();
                float dist = diff.length();
                float minDist = 0.1f;

                if (dist < minDist && dist > 0.001f) {
                    QVector3D dir = diff / dist;
                    float overlap = minDist - dist;
                    QVector3D correction = dir * overlap * 0.5f;
                    a->setPosition(a->position() - correction);
                    b->setPosition(b->position() + correction);
                }
            }
        }
    }
}

} // namespace physics
} // namespace ks
