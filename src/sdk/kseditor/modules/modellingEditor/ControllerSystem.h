#pragma once

#include <QString>
#include <QVector>
#include <QVector3D>
#include <QMatrix4x4>
#include <QMap>
#include <QPair>
#include <QVariant>

#include "sdk/kseditor/engine/Graphics/SceneObject.h"

namespace ks {

// Procedural animation controllers (3ds Max "controllers" style).
enum class ControllerType {
    Noise = 0,        // base + amplitude * valueNoise(time * frequency + phase)
    Spring = 1,       // damped oscillation around `base` after creation
    LookAt = 2,       // object +Z axis points toward the target every tick
    Attachment = 3    // object position follows target mesh vertex `vertexIndex` + offset
};

struct ControllerDef {
    int type = 0;
    int targetId = -1;
    QString targetName;
    QString channel = QStringLiteral("position.y");   // scalar target (Noise / Spring)
    float base = 0.0f;                                // captured at creation time
    float amplitude = 1.0f;                           // Noise
    float frequency = 1.0f;                           // Noise / Spring
    float phase = 0.0f;                               // Noise / Spring
    float stiffness = 50.0f;                          // Spring
    float damping = 2.0f;                             // Spring
    int vertexIndex = 0;                              // Attachment
    QVector3D offset = {0, 0, 0};                     // Attachment
    bool enabled = true;

    QVariant toVariant() const;
    void fromVariant(const QVariant& v);
};

class ControllerSystem
{
public:
    ControllerSystem() = default;

    void add(int objectId, int type, int targetId, const QString& targetName,
             const QString& channel, float base, float amplitude, float frequency,
             float phase, float stiffness, float damping);
    bool remove(int objectId, int index);
    bool setEnabled(int objectId, int index, bool on);
    bool setParams(int objectId, int index, float amplitude, float frequency,
                   float phase, float stiffness, float damping);
    bool setAttachment(int objectId, int index, int vertexIndex,
                       const QVector3D& offset);
    void clearObject(int objectId);
    void clearAll();

    QVector<ControllerDef> forObject(int objectId) const { return m_controllers.value(objectId); }
    bool hasAny() const { return !m_controllers.isEmpty(); }
    bool hasObject(int objectId) const { return m_controllers.contains(objectId); }
    int count(int objectId) const { return m_controllers.value(objectId).size(); }
    QVector<int> controlledObjectIds() const { return m_controllers.keys(); }

    // Applies all enabled controllers of `objectId` at simulation `time`.
    // `time` is a monotonic clock in seconds (advances while animating).
    int evaluate(SceneObject* obj, SceneGraph* graph, float time);

    // Deterministic 1D value noise in [0,1].
    static float valueNoise(float x);

    // Pure helper: world position of a local-space mesh vertex (testable without a SceneMesh).
    static bool vertexWorldPos(const QVector<QVector3D>& localVerts, const QMatrix4x4& world,
                               int vertexIndex, QVector3D& outWorldPos);

private:
    bool applyOne(SceneObject* obj, SceneObject* target, const ControllerDef& c, int index, float time);

    QMap<int, QVector<ControllerDef>> m_controllers;
    // Spring start times captured on first evaluation, keyed by (objectId, index).
    QMap<QPair<int, int>, float> m_startTimes;
};

} // namespace ks