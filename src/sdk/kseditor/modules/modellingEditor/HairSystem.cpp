#include "HairSystem.h"
#include "CurveSystem.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"

#include <QtGlobal>
#include <cmath>

namespace ks {

namespace {
inline unsigned hairRng(unsigned& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
inline float hairRnd(unsigned& s) { return (hairRng(s) & 0x00FFFFFF) / 16777216.0f; }

QVector3D hairPerp(const QVector3D& n)
{
    QVector3D u = std::abs(n.y()) < 0.999f
        ? QVector3D::crossProduct(n, QVector3D(0.0f, 1.0f, 0.0f)).normalized()
        : QVector3D(1.0f, 0.0f, 0.0f);
    return QVector3D::crossProduct(u, n).normalized();
}
} // namespace

HairSystem::HairSystem() = default;

bool HairSystem::addHair(int surfaceObjectId, SceneObject* obj, int strandCount, int segments, float length)
{
    if (!obj || !obj->mesh() || m_entries.contains(surfaceObjectId)) return false;
    const auto& verts = obj->mesh()->geometry().vertices;
    const auto& idxs = obj->mesh()->geometry().indices;
    if (verts.isEmpty() || idxs.size() < 3) return false;

    const int seg = qBound(2, segments, 64);
    const int count = qBound(1, strandCount, 20000);

    Entry* e = new Entry();
    e->surfaceObjectId = surfaceObjectId;
    e->segments = seg;
    e->length = qMax(0.01f, length);

    // Area-weighted cumulative distribution over triangles.
    const int triCount = idxs.size() / 3;
    QVector<double> cum(triCount);
    double total = 0.0;
    for (int t = 0; t < triCount; ++t) {
        const QVector3D a(verts[idxs[t * 3]].position.x(), verts[idxs[t * 3]].position.y(), verts[idxs[t * 3]].position.z());
        const QVector3D b(verts[idxs[t * 3 + 1]].position.x(), verts[idxs[t * 3 + 1]].position.y(), verts[idxs[t * 3 + 1]].position.z());
        const QVector3D c(verts[idxs[t * 3 + 2]].position.x(), verts[idxs[t * 3 + 2]].position.y(), verts[idxs[t * 3 + 2]].position.z());
        total += QVector3D::crossProduct(b - a, c - a).length() * 0.5;
        cum[t] = total;
    }
    if (total <= 0.0) { delete e; return false; }

    unsigned seed = unsigned(surfaceObjectId * 73856093u + 7u);
    const float segLen = e->length / float(seg);
    for (int s = 0; s < count; ++s) {
        // Pick a triangle by area.
        const double r = hairRnd(seed) * total;
        int t = 0;
        for (int i = 0; i < triCount; ++i) { if (cum[i] >= r) { t = i; break; } }

        // Random barycentric point.
        const float r1 = std::sqrt(hairRnd(seed));
        const float r2 = hairRnd(seed);
        const float u = 1.0f - r1;
        const float v = r1 * r2;
        const int ia = idxs[t * 3], ib = idxs[t * 3 + 1], ic = idxs[t * 3 + 2];
        const QVector3D pa(verts[ia].position.x(), verts[ia].position.y(), verts[ia].position.z());
        const QVector3D pb(verts[ib].position.x(), verts[ib].position.y(), verts[ib].position.z());
        const QVector3D pc(verts[ic].position.x(), verts[ic].position.y(), verts[ic].position.z());
        const QVector3D base = pa * u + pb * v + pc * (1.0f - u - v);

        const QVector3D na(verts[ia].normal.x(), verts[ia].normal.y(), verts[ia].normal.z());
        const QVector3D nb(verts[ib].normal.x(), verts[ib].normal.y(), verts[ib].normal.z());
        const QVector3D nc(verts[ic].normal.x(), verts[ic].normal.y(), verts[ic].normal.z());
        QVector3D n = (na * u + nb * v + nc * (1.0f - u - v)).normalized();
        if (n.lengthSquared() < 1e-6f) {
            n = QVector3D::crossProduct(pb - pa, pc - pa).normalized();
            if (n.lengthSquared() < 1e-6f) n = QVector3D(0, 1, 0);
        }

        // Jitter the strand direction a little.
        const QVector3D perp = hairPerp(n);
        const float spread = 0.15f;
        const float a1 = (hairRnd(seed) - 0.5f) * 2.0f * spread;
        const float a2 = (hairRnd(seed) - 0.5f) * 2.0f * spread;
        const QVector3D dir = (n + perp * a1 + QVector3D::crossProduct(n, perp) * a2).normalized();

        HairStrand st;
        st.rootNormal = n;
        for (int i = 0; i <= seg; ++i) {
            const QVector3D p = base + dir * (segLen * i);
            st.base.append(p);
            st.pos.append(p);
            st.prev.append(p);
        }
        e->strands.append(st);
    }

    m_entries.insert(surfaceObjectId, e);
    return true;
}

bool HairSystem::removeHair(int surfaceObjectId)
{
    auto it = m_entries.find(surfaceObjectId);
    if (it == m_entries.end()) return false;
    delete it.value();
    m_entries.erase(it);
    return true;
}

void HairSystem::removeAll()
{
    qDeleteAll(m_entries);
    m_entries.clear();
}

const QVector<HairSystem::HairStrand>& HairSystem::strands(int surfaceObjectId) const
{
    static const QVector<HairStrand> empty;
    auto it = m_entries.constFind(surfaceObjectId);
    return it == m_entries.constEnd() ? empty : it.value()->strands;
}

void HairSystem::setLength(int surfaceObjectId, float v)
{
    auto it = m_entries.find(surfaceObjectId);
    if (it == m_entries.end()) return;
    Entry& e = *it.value();
    e.length = qMax(0.01f, v);
    const float segLen = e.length / float(e.segments);
    for (auto& st : e.strands) {
        for (int i = 0; i < st.base.size(); ++i) {
            const QVector3D dir = (st.base.last() - st.base.first()).normalized();
            st.base[i] = st.base.first() + dir * (segLen * i);
            st.pos[i] = st.base[i];
            st.prev[i] = st.base[i];
        }
    }
}

float HairSystem::lengthOf(int surfaceObjectId) const
{
    auto it = m_entries.constFind(surfaceObjectId);
    return it == m_entries.constEnd() ? 0.0f : it.value()->length;
}

void HairSystem::setStiffness(int surfaceObjectId, float v)
{
    auto it = m_entries.find(surfaceObjectId);
    if (it != m_entries.end()) it.value()->stiffness = qBound(0.0f, v, 1.0f);
}

float HairSystem::stiffnessOf(int surfaceObjectId) const
{
    auto it = m_entries.constFind(surfaceObjectId);
    return it == m_entries.constEnd() ? 0.0f : it.value()->stiffness;
}

void HairSystem::setWind(int surfaceObjectId, float v)
{
    auto it = m_entries.find(surfaceObjectId);
    if (it != m_entries.end()) it.value()->wind = qBound(0.0f, v, 1.0f);
}

float HairSystem::windOf(int surfaceObjectId) const
{
    auto it = m_entries.constFind(surfaceObjectId);
    return it == m_entries.constEnd() ? 0.0f : it.value()->wind;
}

int HairSystem::strandCountOf(int surfaceObjectId) const
{
    auto it = m_entries.constFind(surfaceObjectId);
    return it == m_entries.constEnd() ? 0 : it.value()->strands.size();
}

int HairSystem::segmentsOf(int surfaceObjectId) const
{
    auto it = m_entries.constFind(surfaceObjectId);
    return it == m_entries.constEnd() ? 0 : it.value()->segments;
}

void HairSystem::step(float dt)
{
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    const float sub = 3.0f;
    const float h = dt / sub;

    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Entry& e = *it.value();
        if (!e.active) continue;
        const float stiff = e.stiffness * 0.5f;
        for (auto& st : e.strands) {
            for (int iter = 0; iter < sub; ++iter) {
                for (int i = 1; i < st.pos.size(); ++i) {
                    const QVector3D vel = st.pos[i] - st.prev[i];
                    st.prev[i] = st.pos[i];
                    QVector3D acc = m_gravity * 0.35f;
                    acc.setX(acc.x() + e.wind * 1.6f);
                    acc.setZ(acc.z() + e.wind * 1.1f);
                    QVector3D newPos = st.pos[i] + vel + acc * h * h;
                    // Pull toward the rest direction (stiffness).
                    newPos = newPos + (st.base[i] - newPos) * stiff;
                    // Keep the strand length bounded from the root.
                    const QVector3D root = st.base.first();
                    const QVector3D d = newPos - root;
                    const float maxLen = (st.base.last() - root).length() * 1.02f;
                    const float l = d.length();
                    if (l > maxLen) newPos = root + d * (maxLen / l);
                    st.pos[i] = newPos;
                }
                // Push points out of the surface slightly (avoid clipping).
                for (int i = 1; i < st.pos.size(); ++i) {
                    const QVector3D off = st.pos[i] - st.base.first();
                    const float along = QVector3D::dotProduct(off, st.rootNormal);
                    if (along < 0.001f)
                        st.pos[i] = st.pos[i] + st.rootNormal * (0.001f - along);
                }
            }
        }
    }
}

MeshData HairSystem::buildMesh(int surfaceObjectId) const
{
    MeshData out;
    auto it = m_entries.constFind(surfaceObjectId);
    if (it == m_entries.constEnd()) return out;

    const float width = 0.006f;
    for (const auto& st : it.value()->strands) {
        if (st.pos.size() < 2) continue;
        const CurveData curve = CurvePrimitives::polyline(st.pos);
        const MeshData ribbon = CurveSurfaces::curveRibbon(curve, width, int(st.pos.size()) - 1);
        if (ribbon.vertices.isEmpty()) continue;
        const int baseIdx = out.vertices.size();
        for (const auto& v : ribbon.vertices)
            out.vertices.append(v);
        for (const auto& f : ribbon.faces) {
            Face nf;
            for (const int idx : f.indices) nf.indices.append(idx + baseIdx);
            out.faces.append(nf);
        }
    }
    out.computeNormals();
    out.computeBoundingBox();
    return out;
}

} // namespace ks
