#pragma once

#include <QString>
#include <QVector>
#include <QVector3D>
#include <QMatrix4x4>
#include <QMap>
#include <QVariant>

#include "sdk/kseditor/engine/Graphics/SceneObject.h"

namespace ks {

// Skin Wrap (3ds Max): deforms a skin mesh so it follows a low/high-poly cage
// mesh. Each skin vertex is bound to the nearest cage triangle (barycentric
// coordinates + a world-space offset vector). When the cage is deformed/moved
// the skin vertex follows it.
struct SkinWrapBinding {
    int cageId = -1;
    QString cageName;

    // Captured per skin vertex (index-aligned with the skin mesh vertices).
    QVector<int> cageTri;              // cage triangle index (into indices/3)
    QVector<QVector3D> bary;           // barycentric coords (u, v, w)
    QVector<QVector3D> worldOffset;    // offset from the triangle point to the vertex

    bool enabled = true;

    bool isValid(int vertexCount) const
    {
        return cageTri.size() == vertexCount
            && bary.size() == vertexCount
            && worldOffset.size() == vertexCount;
    }

    QVariant toVariant() const;
    void fromVariant(const QVariant& v);
};

class SkinWrapSystem
{
public:
    SkinWrapSystem() = default;

    // Captures the current pose of `cage` against the skin mesh of `objectId`.
    bool add(int objectId, int cageId, const QString& cageName);
    bool remove(int objectId, int index);
    bool setEnabled(int objectId, int index, bool on);
    bool rebind(int objectId, int index, SceneGraph* graph);
    void clearObject(int objectId);
    void clearAll();

    QVector<SkinWrapBinding> forObject(int objectId) const { return m_bindings.value(objectId); }
    bool hasAny() const { return !m_bindings.isEmpty(); }
    bool hasObject(int objectId) const { return m_bindings.contains(objectId); }
    int count(int objectId) const { return m_bindings.value(objectId).size(); }
    QVector<int> wrappedObjectIds() const { return m_bindings.keys(); }

    // Deforms `obj` using its bindings (cage mesh read from the graph).
    // Returns the number of bindings applied.
    int evaluate(SceneObject* obj, SceneGraph* graph);

    // Pure geometry capture: binds `skinLocalVerts` (skin-space) to the
    // triangles of a cage whose local vertices + indices are given, both
    // transformed to world space by `skinWorld` / `cageWorld`.
    static bool captureGeometry(const QVector<QVector3D>& skinLocalVerts,
                                const QMatrix4x4& skinWorld,
                                const QVector<QVector3D>& cageLocalVerts,
                                const QVector<uint32_t>& cageIndices,
                                const QMatrix4x4& cageWorld,
                                SkinWrapBinding& out, int cageId, const QString& cageName);

    // Pure geometry evaluation: computes the deformed skin positions from the
    // current cage vertices / world transform.
    static void applyGeometry(const QVector<QVector3D>& cageLocalVerts,
                              const QVector<uint32_t>& cageIndices,
                              const QMatrix4x4& cageWorld,
                              const QMatrix4x4& skinWorldInv,
                              const SkinWrapBinding& b,
                              QVector<QVector3D>& outSkinLocalPositions);

private:
    // Deforms the skin vertices of `obj` according to `b`.
    void applyWrap(SceneObject* obj, SceneObject* cage, SkinWrapBinding& b,
                   int index, SceneGraph* graph);
    static bool capture(SceneObject* skin, SceneObject* cage, SkinWrapBinding& out);

    QMap<int, QVector<SkinWrapBinding>> m_bindings;
};

} // namespace ks