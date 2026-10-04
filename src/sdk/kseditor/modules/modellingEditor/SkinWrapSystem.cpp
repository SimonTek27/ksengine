#include "SkinWrapSystem.h"

#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <limits>

#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"

namespace ks {

namespace {

// Closest point on triangle (Ericson, Real-Time Collision Detection) with
// exact barycentric coordinates of the closest point.
void closestPointOnTriangle(const QVector3D& p, const QVector3D& a, const QVector3D& b,
                            const QVector3D& c, QVector3D& outPoint, QVector3D& outBary)
{
    const QVector3D ab = b - a;
    const QVector3D ac = c - a;
    const QVector3D ap = p - a;
    const float d1 = QVector3D::dotProduct(ab, ap);
    const float d2 = QVector3D::dotProduct(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) { outPoint = a; outBary = QVector3D(1, 0, 0); return; }

    const QVector3D bp = p - b;
    const float d3 = QVector3D::dotProduct(ab, bp);
    const float d4 = QVector3D::dotProduct(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) { outPoint = b; outBary = QVector3D(0, 1, 0); return; }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        outPoint = a + v * ab;
        outBary = QVector3D(1.0f - v, v, 0);
        return;
    }

    const QVector3D cp = p - c;
    const float d5 = QVector3D::dotProduct(ab, cp);
    const float d6 = QVector3D::dotProduct(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) { outPoint = c; outBary = QVector3D(0, 0, 1); return; }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        outPoint = a + w * ac;
        outBary = QVector3D(1.0f - w, 0, w);
        return;
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        outPoint = b + w * (c - b);
        outBary = QVector3D(0, 1.0f - w, w);
        return;
    }

    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    outPoint = a + ab * v + ac * w;
    outBary = QVector3D(1.0f - v - w, v, w);
}

} // namespace

QVariant SkinWrapBinding::toVariant() const
{
    QJsonObject o;
    o["cageId"] = cageId;
    o["cageName"] = cageName;
    o["enabled"] = enabled;

    QJsonArray tri;
    for (int t : cageTri) tri.append(t);
    o["cageTri"] = tri;

    QJsonArray baryArr;
    for (const QVector3D& b : bary)
        baryArr.append(QJsonArray{b.x(), b.y(), b.z()});
    o["bary"] = baryArr;

    QJsonArray offArr;
    for (const QVector3D& oo : worldOffset)
        offArr.append(QJsonArray{oo.x(), oo.y(), oo.z()});
    o["worldOffset"] = offArr;
    return o;
}

void SkinWrapBinding::fromVariant(const QVariant& v)
{
    QJsonObject o = v.toJsonObject();
    cageId = o["cageId"].toInt();
    cageName = o["cageName"].toString();
    enabled = o["enabled"].toBool(true);

    cageTri.clear();
    const QJsonArray tri = o["cageTri"].toArray();
    for (const QJsonValue& t : tri) cageTri.append(t.toInt());

    bary.clear();
    const QJsonArray bArr = o["bary"].toArray();
    for (const QJsonValue& bv : bArr) {
        const QJsonArray q = bv.toArray();
        if (q.size() >= 3)
            bary.append(QVector3D(static_cast<float>(q.at(0).toDouble()),
                                  static_cast<float>(q.at(1).toDouble()),
                                  static_cast<float>(q.at(2).toDouble())));
    }

    worldOffset.clear();
    const QJsonArray oArr = o["worldOffset"].toArray();
    for (const QJsonValue& ov : oArr) {
        const QJsonArray q = ov.toArray();
        if (q.size() >= 3)
            worldOffset.append(QVector3D(static_cast<float>(q.at(0).toDouble()),
                                         static_cast<float>(q.at(1).toDouble()),
                                         static_cast<float>(q.at(2).toDouble())));
    }
}

bool SkinWrapSystem::captureGeometry(const QVector<QVector3D>& skinLocalVerts,
                                     const QMatrix4x4& skinWorld,
                                     const QVector<QVector3D>& cageLocalVerts,
                                     const QVector<uint32_t>& cageIndices,
                                     const QMatrix4x4& cageWorld,
                                     SkinWrapBinding& out, int cageId, const QString& cageName)
{
    if (skinLocalVerts.isEmpty() || cageLocalVerts.isEmpty() || cageIndices.size() < 3)
        return false;
    const int triCount = cageIndices.size() / 3;
    QVector<QVector<QVector3D>> cageTris;
    cageTris.reserve(triCount);
    for (int t = 0; t < triCount; ++t) {
        QVector<QVector3D> tri;
        tri.reserve(3);
        for (int k = 0; k < 3; ++k)
            tri.append(cageWorld * cageLocalVerts[cageIndices[t * 3 + k]]);
        cageTris.append(tri);
    }

    out.cageId = cageId;
    out.cageName = cageName;
    out.cageTri.clear();
    out.bary.clear();
    out.worldOffset.clear();

    for (const QVector3D& v : skinLocalVerts) {
        const QVector3D world = skinWorld * v;
        int bestT = -1;
        float bestDist = std::numeric_limits<float>::max();
        QVector3D bestPoint, bestBary;
        for (int t = 0; t < triCount; ++t) {
            QVector3D pt, b;
            closestPointOnTriangle(world, cageTris[t][0], cageTris[t][1], cageTris[t][2], pt, b);
            const float d = (world - pt).lengthSquared();
            if (d < bestDist) {
                bestDist = d;
                bestT = t;
                bestPoint = pt;
                bestBary = b;
            }
        }
        if (bestT < 0) return false;
        out.cageTri.append(bestT);
        out.bary.append(bestBary);
        out.worldOffset.append(world - bestPoint);
    }
    return true;
}

void SkinWrapSystem::applyGeometry(const QVector<QVector3D>& cageLocalVerts,
                                   const QVector<uint32_t>& cageIndices,
                                   const QMatrix4x4& cageWorld,
                                   const QMatrix4x4& skinWorldInv,
                                   const SkinWrapBinding& b,
                                   QVector<QVector3D>& outSkinLocalPositions)
{
    if (cageLocalVerts.isEmpty() || cageIndices.size() < 3) return;
    const int triCount = cageIndices.size() / 3;
    QVector<QVector<QVector3D>> cageTris(triCount);
    for (int t = 0; t < triCount; ++t)
        for (int k = 0; k < 3; ++k)
            cageTris[t].append(cageWorld * cageLocalVerts[cageIndices[t * 3 + k]]);

    const int n = std::min<int>(outSkinLocalPositions.size(), b.cageTri.size());
    for (int i = 0; i < n; ++i) {
        const int t = b.cageTri[i];
        if (t < 0 || t >= triCount) continue;
        const QVector3D& uvw = b.bary[i];
        const QVector<QVector3D>& tri = cageTris[t];
        QVector3D world = tri[0] * uvw.x() + tri[1] * uvw.y() + tri[2] * uvw.z();
        world += b.worldOffset[i];
        outSkinLocalPositions[i] = skinWorldInv * world;
    }
}

bool SkinWrapSystem::capture(SceneObject* skin, SceneObject* cage, SkinWrapBinding& outBinding)
{
    if (!skin || !cage || !skin->mesh() || !cage->mesh()) return false;
    const auto& skinGeo = skin->mesh()->geometry();
    const auto& cageGeo = cage->mesh()->geometry();
    if (skinGeo.vertices.isEmpty() || cageGeo.vertices.isEmpty() || cageGeo.indices.size() < 3)
        return false;

    skin->updateWorldTransform();
    cage->updateWorldTransform();

    QVector<QVector3D> skinVerts;
    skinVerts.reserve(skinGeo.vertices.size());
    for (const SceneVertex& sv : skinGeo.vertices)
        skinVerts.append(QVector3D(sv.position.x(), sv.position.y(), sv.position.z()));
    QVector<QVector3D> cageVerts;
    cageVerts.reserve(cageGeo.vertices.size());
    for (const SceneVertex& sv : cageGeo.vertices)
        cageVerts.append(QVector3D(sv.position.x(), sv.position.y(), sv.position.z()));

    return captureGeometry(skinVerts, skin->worldTransform(),
                           cageVerts, cageGeo.indices, cage->worldTransform(),
                           outBinding, cage->id(), cage->name());
}

bool SkinWrapSystem::add(int objectId, int cageId, const QString& cageName)
{
    if (cageId <= 0 || cageId == objectId) return false;
    // Appends a pending (not yet captured) binding; the first evaluate() or an
    // explicit rebind() performs the actual per-vertex capture.
    SkinWrapBinding b;
    b.cageId = cageId;
    b.cageName = cageName;
    m_bindings[objectId].append(b);
    return true;
}

bool SkinWrapSystem::remove(int objectId, int index)
{
    auto it = m_bindings.find(objectId);
    if (it == m_bindings.end() || index < 0 || index >= it->size()) return false;
    it->removeAt(index);
    if (it->isEmpty()) m_bindings.erase(it);
    return true;
}

bool SkinWrapSystem::setEnabled(int objectId, int index, bool on)
{
    auto it = m_bindings.find(objectId);
    if (it == m_bindings.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].enabled = on;
    return true;
}

bool SkinWrapSystem::rebind(int objectId, int index, SceneGraph* graph)
{
    if (!graph) return false;
    auto it = m_bindings.find(objectId);
    if (it == m_bindings.end() || index < 0 || index >= it->size()) return false;
    SceneObject* skin = graph->findObjectById(objectId);
    SceneObject* cage = graph->findObjectById((*it)[index].cageId);
    if (!skin || !cage) return false;
    SkinWrapBinding fresh;
    if (!capture(skin, cage, fresh)) return false;
    (*it)[index] = fresh;
    return true;
}

void SkinWrapSystem::clearObject(int objectId)
{
    m_bindings.remove(objectId);
}

void SkinWrapSystem::clearAll()
{
    m_bindings.clear();
}

void SkinWrapSystem::applyWrap(SceneObject* obj, SceneObject* cage, SkinWrapBinding& b,
                               int /*index*/, SceneGraph* /*graph*/)
{
    const auto& cageGeo = cage->mesh()->geometry();
    if (cageGeo.indices.size() < 3) return;

    cage->updateWorldTransform();
    obj->updateWorldTransform();

    QVector<QVector3D> cageVerts;
    cageVerts.reserve(cageGeo.vertices.size());
    for (const SceneVertex& sv : cageGeo.vertices)
        cageVerts.append(QVector3D(sv.position.x(), sv.position.y(), sv.position.z()));

    auto& verts = obj->mesh()->geometry().vertices;
    QVector<QVector3D> skinPos;
    skinPos.reserve(verts.size());
    for (const SceneVertex& sv : verts)
        skinPos.append(QVector3D(sv.position.x(), sv.position.y(), sv.position.z()));

    applyGeometry(cageVerts, cageGeo.indices, cage->worldTransform(),
                  obj->worldTransform().inverted(), b, skinPos);

    const int n = std::min<int>(verts.size(), skinPos.size());
    for (int i = 0; i < n; ++i)
        verts[i].position = skinPos[i];
}

int SkinWrapSystem::evaluate(SceneObject* obj, SceneGraph* graph)
{
    if (!obj || !graph || !obj->mesh()) return 0;
    auto it = m_bindings.find(obj->id());
    if (it == m_bindings.end()) return 0;

    int applied = 0;
    int index = 0;
    for (SkinWrapBinding& binding : *it) {
        if (binding.enabled) {
            SceneObject* cage = graph->findObjectById(binding.cageId);
            if (cage && cage->mesh()) {
                const int vcount = obj->mesh()->geometry().vertices.size();
                if (!binding.isValid(vcount))
                    rebind(obj->id(), index, graph);
                if (binding.isValid(vcount))
                    applyWrap(obj, cage, binding, index, graph);
                ++applied;
            }
        }
        ++index;
    }
    return applied;
}

} // namespace ks