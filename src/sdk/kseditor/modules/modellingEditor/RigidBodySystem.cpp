#include "RigidBodySystem.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include <QtMath>
#include <QQuaternion>

#if HAS_BULLET
#include "btBulletDynamicsCommon.h"
#include "BulletCollision/CollisionShapes/btBoxShape.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"
#include "BulletCollision/CollisionShapes/btConvexHullShape.h"
#endif

namespace ks {

struct RigidBodySystem::Entry {
    void* body = nullptr;    // btRigidBody*
    void* shape = nullptr;   // btCollisionShape*
    void* motion = nullptr;  // btDefaultMotionState*
    int shapeType = RigidBodySystem::Box;
    float mass = 1.0f;
    bool kinematic = false;
    bool dynamic = false;
};

RigidBodySystem::RigidBodySystem() {
#if HAS_BULLET
    btDefaultCollisionConfiguration* cfg = new btDefaultCollisionConfiguration();
    btCollisionDispatcher* dispatcher = new btCollisionDispatcher(cfg);
    btBroadphaseInterface* broadphase = new btDbvtBroadphase();
    btConstraintSolver* solver = new btSequentialImpulseConstraintSolver();
    btDiscreteDynamicsWorld* world =
        new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, cfg);
    world->setGravity(btVector3(0, -9.81f, 0));
    m_world = world;
#endif
}

RigidBodySystem::~RigidBodySystem() {
    clearAll();
#if HAS_BULLET
    if (m_world) {
        delete reinterpret_cast<btDynamicsWorld*>(m_world);
        m_world = nullptr;
    }
#endif
}

void RigidBodySystem::setGravity(const QVector3D& g) {
#if HAS_BULLET
    if (m_world)
        reinterpret_cast<btDynamicsWorld*>(m_world)->setGravity(btVector3(g.x(), g.y(), g.z()));
#endif
}

QVector3D RigidBodySystem::gravity() const {
#if HAS_BULLET
    if (m_world) {
        const btVector3 g = reinterpret_cast<btDynamicsWorld*>(m_world)->getGravity();
        return QVector3D(g.x(), g.y(), g.z());
    }
#endif
    return QVector3D(0, -9.81f, 0);
}

bool RigidBodySystem::addBody(int objectId, SceneObject* obj, int shapeType, float mass, bool kinematic) {
#if !HAS_BULLET
    Q_UNUSED(objectId); Q_UNUSED(obj); Q_UNUSED(shapeType); Q_UNUSED(mass); Q_UNUSED(kinematic);
    return false;
#else
    if (!obj || !m_world || m_entries.contains(objectId)) return false;

    QVector3D scale = obj->scale();
    if (scale.x() < 1e-4f) scale.setX(1.0f);
    if (scale.y() < 1e-4f) scale.setY(1.0f);
    if (scale.z() < 1e-4f) scale.setZ(1.0f);

    btCollisionShape* shape = nullptr;
    if (shapeType == Box) {
        const QVector3D half = (obj->boundingBoxMax() - obj->boundingBoxMin()) * 0.5f;
        shape = new btBoxShape(btVector3(half.x() * scale.x(),
                                         half.y() * scale.y(),
                                         half.z() * scale.z()));
    } else if (shapeType == Sphere) {
        const float r = obj->boundingRadius() * qMax(scale.x(), qMax(scale.y(), scale.z()));
        shape = new btSphereShape(r > 1e-3f ? r : 0.5f);
    } else { // ConvexHull
        btConvexHullShape* hull = new btConvexHullShape();
        if (obj->hasMesh()) {
            const auto& geom = obj->mesh()->geometry();
            for (const SceneVertex& v : geom.vertices) {
                const QVector3D p = QVector3D(v.position.x() * scale.x(),
                                              v.position.y() * scale.y(),
                                              v.position.z() * scale.z());
                hull->addPoint(btVector3(p.x(), p.y(), p.z()), false);
            }
        }
        if (hull->getNumPoints() < 4) {
            delete hull;
            const QVector3D half = (obj->boundingBoxMax() - obj->boundingBoxMin()) * 0.5f;
            shape = new btBoxShape(btVector3(qMax(half.x() * scale.x(), 0.5f),
                                             qMax(half.y() * scale.y(), 0.5f),
                                             qMax(half.z() * scale.z(), 0.5f)));
        } else {
            hull->recalcLocalAabb();
            shape = hull;
        }
    }
    if (!shape) return false;

    Entry* e = new Entry;
    e->shapeType = shapeType;
    e->mass = kinematic ? 0.0f : qMax(0.0f, mass);
    e->kinematic = kinematic;
    e->dynamic = e->mass > 0.0f && !kinematic;
    e->shape = shape;

    btTransform t;
    t.setIdentity();
    t.setOrigin(btVector3(obj->position().x(), obj->position().y(), obj->position().z()));
    const QQuaternion q = QQuaternion::fromEulerAngles(obj->rotationEuler());
    t.setRotation(btQuaternion(q.x(), q.y(), q.z(), q.scalar()));

    btVector3 localInertia(0, 0, 0);
    if (e->dynamic) shape->calculateLocalInertia(e->mass, localInertia);

    btDefaultMotionState* motion = new btDefaultMotionState(t);
    btRigidBody::btRigidBodyConstructionInfo ci(e->mass, motion, shape, localInertia);
    btRigidBody* body = new btRigidBody(ci);
    body->setActivationState(ISLAND_SLEEPING);
    if (kinematic) {
        body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        body->setActivationState(DISABLE_DEACTIVATION);
    }
    reinterpret_cast<btDynamicsWorld*>(m_world)->addRigidBody(body);

    e->body = body;
    e->motion = motion;
    m_entries.insert(objectId, e);
    return true;
#endif
}

bool RigidBodySystem::removeBody(int objectId) {
#if !HAS_BULLET
    Q_UNUSED(objectId);
    return false;
#else
    auto it = m_entries.find(objectId);
    if (it == m_entries.end()) return false;
    Entry* e = it.value();
    if (e->body)
        reinterpret_cast<btDynamicsWorld*>(m_world)->removeRigidBody(reinterpret_cast<btRigidBody*>(e->body));
    if (e->body)    delete reinterpret_cast<btRigidBody*>(e->body);
    if (e->motion)  delete reinterpret_cast<btDefaultMotionState*>(e->motion);
    if (e->shape)   delete reinterpret_cast<btCollisionShape*>(e->shape);
    delete e;
    m_entries.erase(it);
    return true;
#endif
}

void RigidBodySystem::removeAll() {
    for (int id : m_entries.keys()) removeBody(id);
}

void RigidBodySystem::clearAll() {
    removeAll();
}

int RigidBodySystem::shapeTypeOf(int objectId) const {
    auto it = m_entries.constFind(objectId);
    return it == m_entries.constEnd() ? -1 : it.value()->shapeType;
}

void RigidBodySystem::setBodyKinematic(int objectId, bool kinematic) {
#if HAS_BULLET
    auto it = m_entries.find(objectId);
    if (it == m_entries.end()) return;
    Entry* e = it.value();
    btRigidBody* body = reinterpret_cast<btRigidBody*>(e->body);
    if (!body) return;
    e->kinematic = kinematic;
    if (kinematic) {
        e->mass = 0.0f;
        e->dynamic = false;
        body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        body->setActivationState(DISABLE_DEACTIVATION);
    } else {
        body->setCollisionFlags(body->getCollisionFlags() & ~btCollisionObject::CF_KINEMATIC_OBJECT);
        e->dynamic = e->mass > 0.0f;
    }
#endif
}

bool RigidBodySystem::setBodyMass(int objectId, float mass) {
#if !HAS_BULLET
    Q_UNUSED(objectId); Q_UNUSED(mass);
    return false;
#else
    auto it = m_entries.find(objectId);
    if (it == m_entries.end()) return false;
    Entry* e = it.value();
    btRigidBody* body = reinterpret_cast<btRigidBody*>(e->body);
    if (!body || e->kinematic) return false;
    e->mass = qMax(0.0f, mass);
    e->dynamic = e->mass > 0.0f;
    btVector3 inertia(0, 0, 0);
    btCollisionShape* shape = reinterpret_cast<btCollisionShape*>(e->shape);
    if (e->dynamic && shape) shape->calculateLocalInertia(e->mass, inertia);
    body->setMassProps(e->mass, inertia);
    return true;
#endif
}

// Push object poses into bodies (kinematic/static follow the object).
void RigidBodySystem::sync(const QMap<int, SceneObject*>& objects) {
#if HAS_BULLET
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        auto eIt = m_entries.constFind(it.key());
        if (eIt == m_entries.constEnd()) continue;
        Entry* e = eIt.value();
        if (!e->kinematic && e->mass > 0.0f) continue; // dynamic simulated
        btRigidBody* body = reinterpret_cast<btRigidBody*>(e->body);
        if (!body) continue;
        btTransform t;
        t.setIdentity();
        t.setOrigin(btVector3(it.value()->position().x(),
                              it.value()->position().y(),
                              it.value()->position().z()));
        const QQuaternion q = QQuaternion::fromEulerAngles(it.value()->rotationEuler());
        t.setRotation(btQuaternion(q.x(), q.y(), q.z(), q.scalar()));
        body->setWorldTransform(t);
        body->setLinearVelocity(btVector3(0, 0, 0));
        body->setAngularVelocity(btVector3(0, 0, 0));
    }
#endif
}

// Advance the simulation and write simulated transforms back to the objects.
void RigidBodySystem::step(float dt, const QMap<int, SceneObject*>& objects) {
#if HAS_BULLET
    if (!m_world || m_entries.isEmpty()) return;
    sync(objects);
    reinterpret_cast<btDynamicsWorld*>(m_world)->stepSimulation(dt, 8, qMax(dt / 8.0f, 1.0f / 240.0f));
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        auto eIt = m_entries.constFind(it.key());
        if (eIt == m_entries.constEnd()) continue;
        Entry* e = eIt.value();
        if (!e->dynamic) continue;
        btRigidBody* body = reinterpret_cast<btRigidBody*>(e->body);
        if (!body) continue;
        btTransform t;
        if (body->getMotionState()) body->getMotionState()->getWorldTransform(t);
        else                       t = body->getCenterOfMassTransform();
        SceneObject* obj = it.value();
        obj->setPosition(QVector3D(t.getOrigin().x(), t.getOrigin().y(), t.getOrigin().z()));
        const btQuaternion bq = t.getRotation();
        const QQuaternion q = QQuaternion(bq.w(), bq.x(), bq.y(), bq.z());
        obj->setRotationEuler(q.toEulerAngles());
    }
#endif
}

// Teleport all bodies back to their objects' poses and zero velocities.
void RigidBodySystem::reset(const QMap<int, SceneObject*>& objects) {
#if HAS_BULLET
    if (!m_world) return;
    sync(objects); // sets static/kinematic transforms
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        auto eIt = m_entries.constFind(it.key());
        if (eIt == m_entries.constEnd()) continue;
        btRigidBody* body = reinterpret_cast<btRigidBody*>(eIt.value()->body);
        if (!body) continue;
        btTransform t;
        t.setIdentity();
        t.setOrigin(btVector3(it.value()->position().x(),
                              it.value()->position().y(),
                              it.value()->position().z()));
        const QQuaternion q = QQuaternion::fromEulerAngles(it.value()->rotationEuler());
        t.setRotation(btQuaternion(q.x(), q.y(), q.z(), q.scalar()));
        body->setWorldTransform(t);
        body->setCenterOfMassTransform(t);
        body->setLinearVelocity(btVector3(0, 0, 0));
        body->setAngularVelocity(btVector3(0, 0, 0));
        body->activate();
    }
#endif
}

} // namespace ks
