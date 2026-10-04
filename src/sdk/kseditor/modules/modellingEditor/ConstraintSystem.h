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

// Rigging constraint types (world-space solvers).
enum class ConstraintType {
    Point = 0,          // object position follows target position (+ offset)
    Orientation = 1,    // object rotation follows target rotation (+ offset rotation)
    Aim = 2,            // object +Z axis points toward the target
    Parent = 3,         // object transform = target world transform * offset
    Path = 4,           // object position slides along a path; `path` = target-local
                        //               samples, `param` = T in [0,1], `follow` aligns to tangent
    Attachment = 5,     // object position snaps to vertex `param` of `path` (target-local)
    Link = 6,           // object keeps its initial world transform relative to the target
    Spring = 7          // springy follow toward target position (stiffness / damping)
};

struct ConstraintDef {
    int type = 0;
    int targetId = -1;
    QString targetName;
    QVector3D offset = {0, 0, 0};       // world offset (Point / Parent / Path / Attachment / Spring)
    QVector3D offsetRot = {0, 0, 0};    // euler offset for Orientation / Parent
    QVector<QVector3D> path;            // target-local space samples (Path / Attachment)
    float param = 0.0f;                 // Path: T in [0,1]; Attachment: target vertex index
    float stiffness = 50.0f;            // Spring
    float damping = 2.0f;               // Spring
    bool follow = false;                // Path: align object orientation to the path tangent
    bool enabled = true;

    QVariant toVariant() const;
    void fromVariant(const QVariant& v);
};

class ConstraintSystem
{
public:
    ConstraintSystem() = default;

    void add(int objectId, int type, int targetId, const QString& targetName,
             const QVector3D& offset = QVector3D(), const QVector3D& offsetRot = QVector3D());
    bool remove(int objectId, int index);
    bool setEnabled(int objectId, int index, bool on);
    bool setOffset(int objectId, int index, const QVector3D& offset);
    void clearObject(int objectId);
    void clearAll();

    // Path / Attachment / Spring specific parameters.
    bool setParam(int objectId, int index, float param);
    bool setFollow(int objectId, int index, bool follow);
    bool setSpringParams(int objectId, int index, float stiffness, float damping);
    bool setPath(int objectId, int index, const QVector<QVector3D>& path);

    QVector<ConstraintDef> forObject(int objectId) const { return m_constraints.value(objectId); }
    bool hasAny() const { return !m_constraints.isEmpty(); }
    bool hasObject(int objectId) const { return m_constraints.contains(objectId); }
    int count(int objectId) const { return m_constraints.value(objectId).size(); }
    QVector<int> constrainedObjectIds() const { return m_constraints.keys(); }

    // Applies all enabled constraints of `objectId` in world space.
    // Requires the graph transforms to be up to date; calls setPosition/setRotationEuler on the object.
    // Returns the number of constraints applied.
    int evaluate(SceneObject* obj, SceneGraph* graph);

    // Helper: decompose a matrix into translation and ZYX euler rotation.
    static void decompose(const QMatrix4x4& m, QVector3D& outPos, QVector3D& outEuler);

private:
    void applyOne(SceneObject* obj, SceneObject* target, const ConstraintDef& c, int index);

    QMap<int, QVector<ConstraintDef>> m_constraints;

    // Mutable solver state (spring velocities / Link captured offsets), keyed by
    // (objectId, constraint index) so multiple constraints never collide.
    QMap<QPair<int, int>, QVector3D> m_velocities;
    QMap<QPair<int, int>, QMatrix4x4> m_lockedOffsets;
};

} // namespace ks
