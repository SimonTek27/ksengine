#include "MeshModifierData.h"
#include "sdk/kseditor/engine/mesh/MeshOperations.h"
#include "AdditionalModifiers.h"
#include <algorithm>
#include <QSharedPointer>
#include <QJsonArray>

namespace ks {

QVector3D MeshObject::getCenter() const
{
    if (vertices.isEmpty()) return QVector3D(0, 0, 0);

    QVector3D sum(0, 0, 0);
    for (const auto& v : vertices) {
        sum += v.position;
    }
    return sum / vertices.size();
}

void MeshObject::applyTransform()
{
    for (auto& v : vertices) {
        QVector4D pos(v.position, 1.0f);
        pos = transform * pos;
        v.position = pos.toVector3D();

        QVector4D norm(v.normal, 0.0f);
        norm = transform * norm;
        v.normal = norm.toVector3D().normalized();
    }
}

// ---------------------------------------------------------------------------
// MeshModifier Implementation
// ---------------------------------------------------------------------------
MeshModifier* MeshModifier::s_instance = nullptr;

MeshModifier::MeshModifier(QObject* parent) : QObject(parent) {}
MeshModifier::~MeshModifier() {
    for (auto* m : m_meshes) delete m;
    m_meshes.clear();
}

MeshModifier* MeshModifier::instance() {
    if (!s_instance) s_instance = new MeshModifier();
    return s_instance;
}

int MeshModifier::findMeshIndex(int meshId) {
    return m_meshes.contains(meshId) ? meshId : -1;
}

QVector<int> MeshModifier::findMeshesByName(const QString& infix) {
    QVector<int> results;
    for (auto it = m_meshes.constBegin(); it != m_meshes.constEnd(); ++it) {
        if (it.value()->name.contains(infix, Qt::CaseInsensitive))
            results.append(it.key());
    }
    return results;
}

int MeshModifier::findMeshByName(const QString& name) {
    for (auto it = m_meshes.constBegin(); it != m_meshes.constEnd(); ++it) {
        if (it.value()->name == name) return it.key();
    }
    return -1;
}

MeshObject* MeshModifier::getMesh(int meshId) {
    return m_meshes.value(meshId);
}

void MeshModifier::setMesh(int meshId, MeshObject* mesh) {
    if (m_meshes.contains(meshId)) {
        delete m_meshes[meshId];
        m_meshes[meshId] = mesh;
    }
}

int MeshModifier::addMesh(const QString& name, const QString& type) {
    auto* mesh = new MeshObject();
    mesh->id = QString::number(m_nextMeshId);
    mesh->name = name.isEmpty() ? "Mesh_" + QString::number(m_nextMeshId) : name;
    mesh->meshType = type.isEmpty() ? "TriMesh" : type;
    int id = m_nextMeshId++;
    m_meshes[id] = mesh;
    emit meshAdded(id);
    return id;
}

void MeshModifier::removeMesh(int meshId) {
    if (m_meshes.contains(meshId)) {
        delete m_meshes.take(meshId);
        emit meshRemoved(meshId);
    }
}

bool MeshModifier::createVertexGroup(int meshId, const QString& groupName, const QVector<int>& vertexIndices) {
    if (!m_meshes.contains(meshId) || groupName.isEmpty()) return false;
    VertexGroup vg;
    vg.name = groupName;
    vg.vertexIndices = vertexIndices;
    m_meshes[meshId]->vertexGroups.append(vg);
    return true;
}

bool MeshModifier::removeVertexGroup(int meshId, const QString& groupName) {
    if (!m_meshes.contains(meshId)) return false;
    auto& groups = m_meshes[meshId]->vertexGroups;
    for (int i = 0; i < groups.size(); ++i) {
        if (groups[i].name == groupName) {
            groups.removeAt(i);
            return true;
        }
    }
    return false;
}

QVector<int> MeshModifier::getVerticesInGroup(int meshId, const QString& groupName) {
    if (!m_meshes.contains(meshId)) return {};
    for (const auto& vg : m_meshes[meshId]->vertexGroups) {
        if (vg.name == groupName) return vg.vertexIndices;
    }
    return {};
}

QVector<int> MeshModifier::getVerticesInRadius(int meshId, const QVector3D& center, float radius) {
    QVector<int> result;
    if (!m_meshes.contains(meshId)) return result;
    float r2 = radius * radius;
    const auto& verts = m_meshes[meshId]->vertices;
    for (int i = 0; i < verts.size(); ++i) {
        if ((verts[i].position - center).lengthSquared() <= r2)
            result.append(i);
    }
    return result;
}

bool MeshModifier::createBones(int meshId, int boneCount, float radius) {
    if (!m_meshes.contains(meshId) || boneCount < 1) return false;
    auto& verts = m_meshes[meshId]->vertices;
    if (verts.isEmpty()) return false;

    QVector3D center = calculateMeshCenter(meshId);
    QVector3D bounds = calculateMeshBounds(meshId);
    float maxDim = qMax(bounds.x(), qMax(bounds.y(), bounds.z()));

    for (int i = 0; i < boneCount; ++i) {
        float angle = 2.0f * 3.14159f * i / boneCount;
        float x = radius * cosf(angle);
        float z = radius * sinf(angle);
        QVector3D bonePos = center + QVector3D(x, maxDim * 0.5f, z);

        for (auto& v : verts) {
            float dist = v.position.distanceToPoint(bonePos);
            if (dist < maxDim) {
                float weight = 1.0f - dist / maxDim;
                if (weight > 0.01f) {
                    v.boneIndices.append(i);
                    v.boneWeights.append(weight);
                }
            }
        }
    }

    return true;
}

bool MeshModifier::addSkinModifier(int meshId) {
    if (!m_meshes.contains(meshId)) return false;
    ModifierData md;
    md.type = "Skin";
    md.params["bones"] = QJsonValue(4);
    m_meshes[meshId]->modifiers.append(md);
    return true;
}

void MeshModifier::translateVertices(int meshId, const QVector<int>& vertices, const QVector3D& delta) {
    if (!m_meshes.contains(meshId)) return;
    auto& verts = m_meshes[meshId]->vertices;
    for (int idx : vertices) {
        if (idx >= 0 && idx < verts.size())
            verts[idx].position += delta;
    }
    emit meshModified(meshId);
}

void MeshModifier::rotateVertices(int meshId, const QVector<int>& vertices, const QVector3D& center, const QVector3D& rotation) {
    if (!m_meshes.contains(meshId)) return;
    auto& verts = m_meshes[meshId]->vertices;
    QMatrix4x4 rotMat;
    rotMat.translate(center);
    rotMat.rotate(QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, rotation.x()));
    rotMat.rotate(QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, rotation.y()));
    rotMat.rotate(QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, rotation.z()));
    rotMat.translate(-center);

    for (int idx : vertices) {
        if (idx >= 0 && idx < verts.size()) {
            QVector4D p(verts[idx].position, 1.0f);
            p = rotMat * p;
            verts[idx].position = p.toVector3D();
        }
    }
    emit meshModified(meshId);
}

void MeshModifier::scaleVertices(int meshId, const QVector<int>& vertices, const QVector3D& center, const QVector3D& scale) {
    if (!m_meshes.contains(meshId)) return;
    auto& verts = m_meshes[meshId]->vertices;
    for (int idx : vertices) {
        if (idx >= 0 && idx < verts.size())
            verts[idx].position = center + (verts[idx].position - center) * scale;
    }
    emit meshModified(meshId);
}

void MeshModifier::mirrorAlongAxis(int meshId, int axis, float threshold) {
    if (!m_meshes.contains(meshId)) return;
    auto& verts = m_meshes[meshId]->vertices;
    int count = verts.size();

    for (int i = 0; i < count; ++i) {
        MeshVertex v = verts[i];
        float val = (axis == 0) ? v.position.x() : (axis == 1) ? v.position.y() : v.position.z();
        if (qAbs(val) < threshold) continue;
        MeshVertex mv = v;
        if (axis == 0) { mv.position.setX(-v.position.x()); mv.normal.setX(-v.normal.x()); }
        else if (axis == 1) { mv.position.setY(-v.position.y()); mv.normal.setY(-v.normal.y()); }
        else { mv.position.setZ(-v.position.z()); mv.normal.setZ(-v.normal.z()); }
        verts.append(mv);
    }

    int newCount = verts.size();
    int oldFaceCount = m_meshes[meshId]->faces.size();
    for (int i = 0; i < oldFaceCount; ++i) {
        MeshFace f = m_meshes[meshId]->faces[i];
        MeshFace mf;
        mf.v1 = f.v3 + (count);
        mf.v2 = f.v2 + (count);
        mf.v3 = f.v1 + (count);
        mf.materialId = f.materialId;
        m_meshes[meshId]->faces.append(mf);
    }

    emit meshModified(meshId);
}

void MeshModifier::addModifier(int meshId, const QString& modifierType, const QJsonObject& params) {
    if (!m_meshes.contains(meshId)) return;
    ModifierData md;
    md.type = modifierType;
    md.params = params;
    m_meshes[meshId]->modifiers.append(md);
}

bool MeshModifier::removeModifier(int meshId, const QString& modifierType) {
    if (!m_meshes.contains(meshId)) return false;
    auto& mods = m_meshes[meshId]->modifiers;
    for (int i = 0; i < mods.size(); ++i) {
        if (mods[i].type == modifierType) {
            mods.removeAt(i);
            return true;
        }
    }
    return false;
}

QVector3D MeshModifier::calculateMeshCenter(int meshId) {
    if (!m_meshes.contains(meshId)) return QVector3D();
    return m_meshes[meshId]->getCenter();
}

QVector3D MeshModifier::calculateMeshBounds(int meshId) {
    if (!m_meshes.contains(meshId)) return QVector3D();
    const auto& verts = m_meshes[meshId]->vertices;
    if (verts.isEmpty()) return QVector3D();

    QVector3D min = verts[0].position, max = verts[0].position;
    for (const auto& v : verts) {
        min.setX(qMin(min.x(), v.position.x()));
        min.setY(qMin(min.y(), v.position.y()));
        min.setZ(qMin(min.z(), v.position.z()));
        max.setX(qMax(max.x(), v.position.x()));
        max.setY(qMax(max.y(), v.position.y()));
        max.setZ(qMax(max.z(), v.position.z()));
    }
    return max - min;
}

// ── MeshObject ↔ MeshData conversion helpers ──
namespace {
static MeshData meshObjectToMeshData(const MeshObject& obj) {
    MeshData md;
    md.vertices.reserve(obj.vertices.size());
    for (const auto& mv : obj.vertices) {
        Vertex v;
        v.position = mv.position;
        v.normal = mv.normal;
        v.uv = mv.uv;
        md.vertices.append(v);
    }
    md.faces.reserve(obj.faces.size());
    for (const auto& mf : obj.faces) {
        Face f;
        f.indices = { mf.v1, mf.v2, mf.v3 };
        f.materialId = mf.materialId;
        md.faces.append(f);
    }
    return md;
}
static MeshObject meshDataToMeshObject(const MeshData& md, const MeshObject& templateObj) {
    MeshObject obj = templateObj;
    obj.vertices.clear();
    obj.vertices.reserve(md.vertices.size());
    for (const auto& v : md.vertices) {
        MeshVertex mv;
        mv.position = v.position;
        mv.normal = v.normal;
        mv.uv = v.uv;
        obj.vertices.append(mv);
    }
    obj.faces.clear();
    obj.faces.reserve(md.faces.size());
    for (const auto& f : md.faces) {
        if (f.indices.size() >= 3) {
            MeshFace mf;
            mf.v1 = f.indices[0];
            mf.v2 = f.indices[1];
            mf.v3 = f.indices[2];
            mf.materialId = f.materialId;
            obj.faces.append(mf);
        }
    }
    return obj;
}
static ModifierPtr createModifierFromType(const QString& type) {
    if (type == "Mirror")    return QSharedPointer<MirrorModifier>::create();
    if (type == "Array")     return QSharedPointer<ArrayModifier>::create();
    if (type == "Bevel")     return QSharedPointer<BevelModifier>::create();
    if (type == "Solidify")  return QSharedPointer<SolidifyModifier>::create();
    if (type == "Subdivision") return QSharedPointer<SubdivisionModifier>::create();
    if (type == "Decimate")  return QSharedPointer<DecimateModifier>::create();
    if (type == "Displace")  return QSharedPointer<DisplaceModifier>::create();
    if (type == "Smooth")    return QSharedPointer<SmoothModifier>::create();
    if (type == "Cast")      return QSharedPointer<CastModifier>::create();
    if (type == "Triangulate") return QSharedPointer<TriangulateModifier>::create();
    if (type == "Wireframe") return QSharedPointer<WireframeModifier>::create();
    if (type == "Remesh")    return QSharedPointer<RemeshModifier>::create();
    if (type == "Skin")      return QSharedPointer<SkinModifier>::create();
    if (type == "Shrinkwrap") return QSharedPointer<ShrinkwrapModifier>::create();
    if (type == "CageDeform") return QSharedPointer<CageDeformModifier>::create();
    if (type == "LatticeEx")  return QSharedPointer<LatticeExModifier>::create();
    if (type == "SimpleDeform") return QSharedPointer<SimpleDeformModifier>::create();
    if (type == "Curve")      return QSharedPointer<CurveModifier>::create();
    if (type == "CorrectiveSmooth") return QSharedPointer<CorrectiveSmoothModifier>::create();
    if (type == "UVProject") return QSharedPointer<UVProjectModifier>::create();
    if (type == "Weld") return QSharedPointer<WeldModifier>::create();
    if (type == "LaplacianSmooth") return QSharedPointer<LaplacianSmoothModifier>::create();
    if (type == "SurfaceSmooth") return QSharedPointer<SurfaceSmoothModifier>::create();
    if (type == "VolumeSmooth") return QSharedPointer<VolumeSmoothModifier>::create();
    if (type == "Taper") return QSharedPointer<TaperModifier>::create();
    if (type == "Ripple") return QSharedPointer<RippleModifier>::create();
    if (type == "Noise") return QSharedPointer<NoiseModifier>::create();
    if (type == "Push") return QSharedPointer<PushModifier>::create();
    if (type == "Relax") return QSharedPointer<RelaxModifier>::create();
    if (type == "Melt") return QSharedPointer<MeltModifier>::create();
    if (type == "Lathe") return QSharedPointer<LatheModifier>::create();
    if (type == "Wave") return QSharedPointer<WaveModifierEx>::create();
    return nullptr;
}
} // anonymous namespace

bool MeshModifier::applyModifiers(int meshId) {
    if (!m_meshes.contains(meshId)) return false;
    MeshObject* obj = m_meshes[meshId];
    if (obj->vertices.isEmpty()) return true;

    MeshData data = meshObjectToMeshData(*obj);

    for (const auto& md : obj->modifiers) {
        ModifierPtr mod = createModifierFromType(md.type);
        if (!mod) continue;

        QMap<QString, QVariant> params;
        for (auto it = md.params.begin(); it != md.params.end(); ++it)
            params[it.key()] = it.value().toVariant();
        mod->readParameters(params);

        if (mod->canApply(data))
            data = mod->apply(data);
    }

    *obj = meshDataToMeshObject(data, *obj);
    emit meshModified(meshId);
    return true;
}

} // namespace ks
