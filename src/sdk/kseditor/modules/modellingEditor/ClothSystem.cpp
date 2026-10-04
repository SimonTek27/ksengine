#include "ClothSystem.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"

#include <QtGlobal>
#include <QHash>
#include <cmath>

namespace ks {

namespace {
// Closest point on a triangle (Ericson, Real-Time Collision Detection).
QVector3D closestPointOnTriangle(const QVector3D& p, const ClothColliderTri& t)
{
    const QVector3D& A = t.a;
    const QVector3D& B = t.b;
    const QVector3D& C = t.c;
    const QVector3D AB = B - A;
    const QVector3D AC = C - A;
    const QVector3D AP = p - A;
    const float d1 = QVector3D::dotProduct(AB, AP);
    const float d2 = QVector3D::dotProduct(AC, AP);
    if (d1 <= 0.0f && d2 <= 0.0f) return A;
    const QVector3D BP = p - B;
    const float d3 = QVector3D::dotProduct(AB, BP);
    const float d4 = QVector3D::dotProduct(AC, BP);
    if (d3 >= 0.0f && d4 <= d3) return B;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return A + AB * v;
    }
    const QVector3D CP = p - C;
    const float d5 = QVector3D::dotProduct(AB, CP);
    const float d6 = QVector3D::dotProduct(AC, CP);
    if (d6 >= 0.0f && d5 <= d6) return C;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return A + AC * w;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return B + (C - B) * w;
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return A + AB * v + AC * w;
}

// Uniform grid key for a position + cell size.
int gridKey(const QVector3D& p, float cell)
{
    const int ix = int(std::floor(p.x() / cell));
    const int iy = int(std::floor(p.y() / cell));
    const int iz = int(std::floor(p.z() / cell));
    return (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791);
}
} // namespace

ClothSystem::ClothSystem() = default;

bool ClothSystem::addCloth(int objectId, SceneObject* obj, int pinMode)
{
    if (!obj || !obj->mesh() || m_entries.contains(objectId)) return false;
    auto& verts = obj->mesh()->geometry().vertices;
    auto& idxs = obj->mesh()->geometry().indices;
    if (verts.isEmpty() || idxs.size() < 3) return false;

    Entry* e = new Entry();
    e->pinMode = pinMode;
    e->rest.resize(verts.size());
    e->pos.resize(verts.size());
    e->prev.resize(verts.size());
    e->pinned.resize(verts.size());
    for (int i = 0; i < verts.size(); ++i) {
        const QVector3D p = QVector3D(verts[i].position.x(), verts[i].position.y(), verts[i].position.z());
        e->rest[i] = p;
        e->pos[i] = p;
        e->prev[i] = p;
    }

    // Build unique edge list from triangle indices.
    QMap<QPair<int, int>, int> edgeSet;
    float edgeSum = 0.0f;
    int edgeCount = 0;
    for (int i = 0; i + 2 < idxs.size(); i += 3) {
        const int a = idxs[i], b = idxs[i + 1], c = idxs[i + 2];
        QPair<int, int> edges[3] = { qMakePair(a, b), qMakePair(b, c), qMakePair(c, a) };
        for (const auto& e0 : edges) {
            QPair<int, int> key = e0.first < e0.second ? e0 : qMakePair(e0.second, e0.first);
            if (!edgeSet.contains(key)) {
                edgeSet.insert(key, 1);
                e->springs.append(key);
                edgeSum += (e->rest[e0.first] - e->rest[e0.second]).length();
                ++edgeCount;
            }
        }
    }
    if (edgeCount > 0)
        e->collisionRadius = qBound(0.005f, 0.75f * edgeSum / float(edgeCount), 0.5f);

    // Pinning: highest vertices (pinMode 1) or all (pinMode 2).
    if (pinMode == 2) {
        e->pinned.fill(true);
    } else if (pinMode == 1) {
        float maxY = -1e30f;
        for (const auto& p : e->pos) maxY = std::max(maxY, p.y());
        if (maxY > -1e20f) {
            const float eps = 0.02f;
            for (int i = 0; i < e->pos.size(); ++i)
                e->pinned[i] = e->pos[i].y() > maxY - eps;
        }
    }

    m_entries.insert(objectId, e);
    return true;
}

bool ClothSystem::removeCloth(int objectId)
{
    auto it = m_entries.find(objectId);
    if (it == m_entries.end()) return false;
    delete it.value();
    m_entries.erase(it);
    return true;
}

void ClothSystem::removeAll()
{
    qDeleteAll(m_entries);
    m_entries.clear();
}

int ClothSystem::pinModeOf(int objectId) const
{
    auto it = m_entries.constFind(objectId);
    return it == m_entries.constEnd() ? 0 : it.value()->pinMode;
}

int ClothSystem::springCount(int objectId) const
{
    auto it = m_entries.constFind(objectId);
    return it == m_entries.constEnd() ? 0 : it.value()->springs.size();
}

void ClothSystem::setGravity(const QVector3D& g) { m_gravity = g; }

void ClothSystem::setStiffness(int objectId, float v)
{
    auto it = m_entries.find(objectId);
    if (it != m_entries.end()) it.value()->stiffness = qBound(0.0f, v, 1.0f);
}

void ClothSystem::setDamping(int objectId, float v)
{
    auto it = m_entries.find(objectId);
    if (it != m_entries.end()) it.value()->damping = qBound(0.0f, v, 1.0f);
}

void ClothSystem::setWind(int objectId, float v)
{
    auto it = m_entries.find(objectId);
    if (it != m_entries.end()) it.value()->wind = qBound(0.0f, v, 1.0f);
}

void ClothSystem::setColliders(const QVector<ClothColliderTri>& tris) { m_colliders = tris; }

void ClothSystem::setCollisionEnabled(int objectId, bool enabled)
{
    auto it = m_entries.find(objectId);
    if (it != m_entries.end()) it.value()->collide = enabled;
}

bool ClothSystem::collisionEnabled(int objectId) const
{
    auto it = m_entries.constFind(objectId);
    return it == m_entries.constEnd() ? true : it.value()->collide;
}

void ClothSystem::setSelfCollision(int objectId, bool enabled)
{
    auto it = m_entries.find(objectId);
    if (it != m_entries.end()) it.value()->selfCollide = enabled;
}

bool ClothSystem::selfCollision(int objectId) const
{
    auto it = m_entries.constFind(objectId);
    return it == m_entries.constEnd() ? true : it.value()->selfCollide;
}

void ClothSystem::step(float dt, const QMap<int, SceneObject*>& objects)
{
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    const float sub = 4.0f;
    const float h = dt / sub;

    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Entry& e = *it.value();
        if (!e.active) continue;
        SceneObject* obj = objects.value(it.key());
        if (!obj || !obj->mesh()) continue;
        auto& verts = obj->mesh()->geometry().vertices;
        if (verts.size() != e.pos.size()) continue;

        for (int iter = 0; iter < sub; ++iter) {
            const float gravityScale = std::min(1.0f, (2.0f + e.stiffness) / 3.0f);
            // Verlet integration with gravity + wind.
            for (int i = 0; i < e.pos.size(); ++i) {
                if (e.pinned[i]) continue;
                const QVector3D vel = e.pos[i] - e.prev[i];
                e.prev[i] = e.pos[i];
                QVector3D acc = m_gravity * gravityScale;
                acc.setX(acc.x() + e.wind * 2.0f);
                acc.setZ(acc.z() + e.wind * 1.4f);
                QVector3D newPos = e.pos[i] + vel * (1.0f - e.damping * 0.05f) + acc * h * h;
                // Floor collision.
                if (newPos.y() < 0.0f) newPos.setY(0.0f);
                e.pos[i] = newPos;
            }

            // Distance constraints (position projection).
            const float invIters = 1.0f;
            const float k = 0.7f * e.stiffness + 0.3f;
            for (const auto& sp : e.springs) {
                const int a = sp.first, b = sp.second;
                if (e.pinned[a] && e.pinned[b]) continue;
                const QVector3D delta = e.pos[b] - e.pos[a];
                const float dist = delta.length();
                if (dist < 1e-6f) continue;
                const float restLen = (e.rest[a] - e.rest[b]).length();
                const float diff = (dist - restLen) / dist;
                if (e.pinned[a] && !e.pinned[b]) {
                    e.pos[b] -= delta * (diff * k);
                } else if (!e.pinned[a] && e.pinned[b]) {
                    e.pos[a] += delta * (diff * k);
                } else {
                    const QVector3D corr = delta * (diff * k * 0.5f);
                    e.pos[a] += corr;
                    e.pos[b] -= corr;
                }
            }
            Q_UNUSED(invIters);

            // Object collision: push particles out of the registered solid
            // mesh triangles (broadphase = uniform grid over the triangles).
            if (e.collide && !m_colliders.isEmpty()) {
                const float cell = e.collisionRadius * 2.0f;
                QHash<int, QVector<int>> triCells;
                for (int ti = 0; ti < m_colliders.size(); ++ti) {
                    const ClothColliderTri& t = m_colliders[ti];
                    triCells[gridKey(t.a, cell)].append(ti);
                    triCells[gridKey(t.b, cell)].append(ti);
                    triCells[gridKey(t.c, cell)].append(ti);
                }
                for (int i = 0; i < e.pos.size(); ++i) {
                    if (e.pinned[i]) continue;
                    const QVector3D p = e.pos[i];
                    QHash<int, bool> visited;
                    QVector3D corr;
                    float bestPush = 0.0f;
                    bool any = false;
                    for (int gx = 0; gx < 3; ++gx)
                        for (int gy = 0; gy < 3; ++gy)
                            for (int gz = 0; gz < 3; ++gz) {
                                const QVector3D c = p + QVector3D((gx - 1) * cell, (gy - 1) * cell, (gz - 1) * cell);
                                const int key = gridKey(c, cell);
                                if (visited.contains(key)) continue;
                                visited.insert(key, true);
                                for (const int ti : triCells.value(key)) {
                                    const ClothColliderTri& t = m_colliders[ti];
                                    const QVector3D cp = closestPointOnTriangle(p, t);
                                    const QVector3D v = p - cp;
                                    const float dist = v.length();
                                    const float signedD = QVector3D::dotProduct(v, t.n);
                                    if (dist < e.collisionRadius && signedD < e.collisionRadius) {
                                        const float push = e.collisionRadius - dist;
                                        if (push > bestPush) { bestPush = push; corr = v; any = true; }
                                    }
                                }
                            }
                    if (any) {
                        const float len = corr.length();
                        if (len > 1e-6f) e.pos[i] += corr * (bestPush / len);
                    }
                }
            }

            // Self-collision: pairwise separation of nearby particles via grid.
            if (e.selfCollide) {
                const float cell = e.collisionRadius * 2.0f;
                QHash<int, QVector<int>> cells;
                for (int i = 0; i < e.pos.size(); ++i)
                    cells[gridKey(e.pos[i], cell)].append(i);
                const float minDist2 = e.collisionRadius * e.collisionRadius;
                for (int i = 0; i < e.pos.size(); ++i) {
                    if (e.pinned[i]) continue;
                    for (int gx = -1; gx <= 1; ++gx)
                        for (int gy = -1; gy <= 1; ++gy)
                            for (int gz = -1; gz <= 1; ++gz) {
                                const QVector3D c = e.pos[i] + QVector3D(gx * cell, gy * cell, gz * cell);
                                const int key = gridKey(c, cell);
                                const auto it = cells.constFind(key);
                                if (it == cells.constEnd()) continue;
                                for (const int j : it.value()) {
                                    if (j <= i) continue;
                                    if (e.pinned[j]) continue;
                                    const QVector3D d = e.pos[j] - e.pos[i];
                                    const float d2 = d.lengthSquared();
                                    if (d2 >= minDist2 || d2 < 1e-10f) continue;
                                    const float dd = std::sqrt(d2);
                                    const QVector3D sep = d * (0.5f * (e.collisionRadius - dd) / dd);
                                    e.pos[i] -= sep;
                                    e.pos[j] += sep;
                                }
                            }
                }
            }

            // Write back each sub-step so the mesh deforms continuously.
            for (int i = 0; i < e.pos.size(); ++i)
                verts[i].position = QVector3D(e.pos[i].x(), e.pos[i].y(), e.pos[i].z());
        }

        writeBack(e, obj);
    }
}

void ClothSystem::reset(const QMap<int, SceneObject*>& objects)
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Entry& e = *it.value();
        SceneObject* obj = objects.value(it.key());
        if (!obj || !obj->mesh()) continue;
        auto& verts = obj->mesh()->geometry().vertices;
        if (verts.size() != e.pos.size()) continue;
        e.pos = e.rest;
        e.prev = e.rest;
        for (int i = 0; i < verts.size(); ++i)
            verts[i].position = QVector3D(e.rest[i].x(), e.rest[i].y(), e.rest[i].z());
        writeBack(e, obj);
    }
}

void ClothSystem::clearAll()
{
    removeAll();
}

void ClothSystem::writeBack(Entry& e, SceneObject* obj)
{
    if (!obj || !obj->mesh()) return;
    auto& verts = obj->mesh()->geometry().vertices;
    auto& idxs = obj->mesh()->geometry().indices;
    const int vc = verts.size();
    QVector<QVector3D> normals(vc);
    for (int i = 0; i + 2 < idxs.size(); i += 3) {
        const int a = idxs[i], b = idxs[i + 1], c = idxs[i + 2];
        if (a >= vc || b >= vc || c >= vc) continue;
        const QVector3D& A = verts[a].position;
        const QVector3D& B = verts[b].position;
        const QVector3D& C = verts[c].position;
        const QVector3D n = QVector3D::crossProduct(B - A, C - A);
        normals[a] += n;
        normals[b] += n;
        normals[c] += n;
    }
    for (int i = 0; i < vc; ++i)
        verts[i].normal = normals[i].normalized();
}

} // namespace ks
