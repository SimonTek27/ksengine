#pragma once

#include <QVector3D>
#include <QMap>
#include <QVector>
#include "sdk/kseditor/engine/mesh/MeshOperations.h"

namespace ks {

class SceneObject;

// XSI-style hair/fur: strands grown from the surface of a mesh (area-weighted
// stratified sampling of the triangles), simulated as pinned Verlet chains
// with stiffness toward the rest direction, and rendered as thin ribbon cards.
class HairSystem {
public:
    struct HairStrand {
        QVector<QVector3D> base; // rest positions
        QVector<QVector3D> pos;  // current
        QVector<QVector3D> prev; // previous (verlet)
        QVector3D rootNormal;
    };

    HairSystem();
    ~HairSystem() = default;

    // Grow `strandCount` strands of `segments` points and length `length`
    // (world units) from the surface of `obj`.
    bool addHair(int surfaceObjectId, SceneObject* obj, int strandCount, int segments, float length);
    bool removeHair(int surfaceObjectId);
    void removeAll();
    bool hasHair(int surfaceObjectId) const { return m_entries.contains(surfaceObjectId); }
    int count() const { return m_entries.size(); }
    QVector<int> hairIds() const { return m_entries.keys(); }
    const QVector<HairStrand>& strands(int surfaceObjectId) const;

    void setLength(int surfaceObjectId, float v);
    float lengthOf(int surfaceObjectId) const;
    void setStiffness(int surfaceObjectId, float v);
    float stiffnessOf(int surfaceObjectId) const;
    void setWind(int surfaceObjectId, float v);
    float windOf(int surfaceObjectId) const;
    void setGravity(const QVector3D& g) { m_gravity = g; }
    QVector3D gravity() const { return m_gravity; }
    int strandCountOf(int surfaceObjectId) const;
    int segmentsOf(int surfaceObjectId) const;

    void step(float dt);

    // Builds a merged ribbon-card mesh for all strands (world space).
    MeshData buildMesh(int surfaceObjectId) const;

private:
    struct Entry {
        int surfaceObjectId = -1;
        QVector<HairStrand> strands;
        int segments = 6;
        float length = 0.4f;
        float stiffness = 0.7f;
        float wind = 0.0f;
        bool active = true;
    };

    QMap<int, Entry*> m_entries;
    QVector3D m_gravity = QVector3D(0.0f, -9.8f, 0.0f);
};

} // namespace ks
