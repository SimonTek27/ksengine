#include "PhysicsEngine.h"
#include <algorithm>
#include <cmath>
#include <QDebug>

namespace ks { namespace physics {

// PhysicsEngine.cpp - Forward declarations and utility functions only
// The actual implementations are in PhysicsSystem.cpp to avoid duplicate symbols

// RigidBody methods - forward declarations for interface only
// Implementation is in PhysicsSystem.cpp

// CollisionShape methods - forward declarations for interface only
// Implementation is in PhysicsSystem.cpp

// PhysicsWorld methods - forward declarations for interface only
// Implementation is in PhysicsSystem.cpp

// ParticleSystem implementation
void ParticleSystem::emitParticles(int count) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.position = emitter.position;
        p.velocity = emitter.direction * emitter.velocity;
        p.acceleration = QVector3D();
        p.lifetime = lifetime;
        p.age = 0;
        p.mass = 1.0f;
        p.size = 0.1f;
        p.color = QVector4D(1, 1, 1, 1);
        particles.append(p);
    }
}

void ParticleSystem::update(float deltaTime) {
    for (int i = particles.size() - 1; i >= 0; --i) {
        auto& p = particles[i];
        p.age += deltaTime * 60.0f;
        if (!p.isAlive()) {
            particles.removeAt(i);
            continue;
        }

        QVector3D accel = QVector3D(physics.gravity[0], physics.gravity[1], physics.gravity[2]);
        if (physics.useWind) {
            accel += QVector3D(physics.wind[0], physics.wind[1], physics.wind[2]);
        }
        p.velocity += accel * deltaTime;
        p.velocity *= (1.0f - physics.damping * deltaTime);
        p.position += p.velocity * deltaTime;
    }
}

void ParticleSystem::clear() {
    particles.clear();
}

QVector4D ParticleSystem::colorRamp(float t) {
    return QVector4D(t, 1.0f - t, 0.5f, 1.0f);
}

} // namespace physics
} // namespace ks