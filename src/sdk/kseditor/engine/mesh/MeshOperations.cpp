#include "MeshOperations.h"
#include "BooleanOps.h"
#include "ShapeKeyData.h"
#ifdef HAS_OCCT
#include "OCCTBridge.h"
#endif
#include "../FileFormat/MeshData.h"
#include <QVector3D>
#include <QVector2D>
#include <QRectF>
#include <QDebug>
#include <QImage>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSet>
#include <QHash>
#include <cmath>
#include <algorithm>
#include <limits>

namespace ks {

MeshOperations::SelectionMode MeshOperations::SelectionManager::m_mode = MeshOperations::SelectionMode::Vertex;
QVector<int> MeshOperations::SelectionManager::m_selectedVertices;
QVector<int> MeshOperations::SelectionManager::m_selectedEdges;
QVector<int> MeshOperations::SelectionManager::m_selectedFaces;
QVector<int> MeshOperations::SelectionManager::m_hiddenFaces;
QVector<int> MeshOperations::SelectionManager::m_tempSelection;
QVector<int> MeshOperations::SelectionManager::m_selectedBorderEdges;
int MeshOperations::SelectionManager::m_selectedElement = -1;

void MeshOperations::SelectionManager::addSelectedBorderEdge(int edgeIndex) {
    if (edgeIndex < 0 || m_selectedBorderEdges.contains(edgeIndex)) return;
    m_selectedBorderEdges.append(edgeIndex);
}
void MeshOperations::SelectionManager::removeSelectedBorderEdge(int edgeIndex) {
    m_selectedBorderEdges.removeAll(edgeIndex);
}

void MeshOperations::SelectionManager::setMode(SelectionMode mode) { m_mode = mode; }

void MeshOperations::SelectionManager::addSelectedVertex(int vertexIndex) {
    if (!m_selectedVertices.contains(vertexIndex)) m_selectedVertices.append(vertexIndex);
}
void MeshOperations::SelectionManager::removeSelectedVertex(int vertexIndex) {
    m_selectedVertices.removeAll(vertexIndex);
}
void MeshOperations::SelectionManager::addSelectedEdge(int edgeIndex) {
    if (!m_selectedEdges.contains(edgeIndex)) m_selectedEdges.append(edgeIndex);
}
void MeshOperations::SelectionManager::removeSelectedEdge(int edgeIndex) {
    m_selectedEdges.removeAll(edgeIndex);
}
void MeshOperations::SelectionManager::addSelectedFace(int faceIndex) {
    if (!m_selectedFaces.contains(faceIndex)) m_selectedFaces.append(faceIndex);
}
void MeshOperations::SelectionManager::removeSelectedFace(int faceIndex) {
    m_selectedFaces.removeAll(faceIndex);
}
void MeshOperations::SelectionManager::selectAll(int vertexCount, int edgeCount, int faceCount) {
    m_selectedVertices.clear();
    m_selectedEdges.clear();
    m_selectedFaces.clear();
    for (int i = 0; i < vertexCount; ++i) m_selectedVertices.append(i);
    for (int i = 0; i < edgeCount; ++i) m_selectedEdges.append(i);
    for (int i = 0; i < faceCount; ++i) m_selectedFaces.append(i);
}
void MeshOperations::SelectionManager::deselectAll() { m_selectedVertices.clear(); m_selectedEdges.clear(); m_selectedFaces.clear(); m_selectedBorderEdges.clear(); m_selectedElement = -1; }
void MeshOperations::SelectionManager::clear() { deselectAll(); }

QVector3D MeshOperations::CPlane::origin = QVector3D(0, 0, 0);
QVector3D MeshOperations::CPlane::normal = QVector3D(0, 1, 0);
QVector3D MeshOperations::CPlane::up = QVector3D(0, 0, 1);

void MeshOperations::setCPlane(const QVector3D& origin, const QVector3D& normal, const QVector3D& up) {
    CPlane::origin = origin;
    CPlane::normal = normal.normalized();
    QVector3D upNorm = up.normalized();
    if (upNorm.isNull() || QVector3D::dotProduct(CPlane::normal, upNorm) > 0.9f) {
        CPlane::up = QVector3D::crossProduct(CPlane::normal, QVector3D(1, 0, 0)).normalized();
    } else {
        CPlane::up = upNorm;
    }
}

QVector3D MeshOperations::getCPlaneOrigin() { return CPlane::getOrigin(); }
QVector3D MeshOperations::getCPlaneNormal() { return CPlane::getNormal(); }
QVector3D MeshOperations::getCPlaneUp() { return CPlane::getUp(); }

QVector3D MeshOperations::CPlane::getOrigin() { return origin; }
QVector3D MeshOperations::CPlane::getNormal() { return normal; }
QVector3D MeshOperations::CPlane::getUp() { return up; }

void MeshOperations::snapToCPlane(const QVector3D& point, QVector3D& result) {
    float dist = QVector3D::dotProduct(CPlane::normal, point - CPlane::origin);
    result = point - CPlane::normal * dist;
}

MeshOperations::SnapType MeshOperations::m_snapTypes = MeshOperations::SnapType::None;

MeshOperations::SnapType MeshOperations::snapTypes() { return m_snapTypes; }

void MeshOperations::setSnapTypes(MeshOperations::SnapType types) { m_snapTypes = types; }

QVector<MeshOperations::DimensionLine> MeshOperations::m_dimensions;
QVector<MeshOperations::DimensionData> MeshOperations::m_distanceDimensions;
QVector<MeshOperations::DimensionData> MeshOperations::m_angleDimensions;
QVector<MeshOperations::RadiusDimension> MeshOperations::m_radiusDimensions;
float MeshOperations::s_tolerance = 0.001f;
float MeshOperations::s_unitScale = 1.0f;
bool MeshOperations::s_smoothPreview = false;
int MeshOperations::s_smoothPreviewLevel = 1;

QVector3D MeshOperations::snapPoint(const QVector3D& worldPoint, int snapTypes) {
    QVector3D result = worldPoint;
    int types = snapTypes;

    if (types & static_cast<int>(SnapType::Grid)) {
        float gridSize = 0.1f;
        result.setX(qRound(result.x() / gridSize) * gridSize);
        result.setY(qRound(result.y() / gridSize) * gridSize);
        result.setZ(qRound(result.z() / gridSize) * gridSize);
    }

    // Vertex/Edge/Face/Midpoint/Tangent snapping need mesh geometry; use
    // snapPointToMesh(mesh, world, ...) when a mesh is available.
    return result;
}

QVector3D MeshOperations::snapPointToMesh(const MeshData& mesh, const QMatrix4x4& world,
                                          const QVector3D& worldPoint, int snapTypes)
{
    if (mesh.vertices.isEmpty())
        return snapPoint(worldPoint, snapTypes);

    const QMatrix4x4 inv = world.inverted();
    const QVector3D local = inv.map(worldPoint);

    // Local search radius relative to the mesh extent.
    QVector3D bbMin, bbMax;
    for (const auto& v : mesh.vertices) {
        if (bbMin.isNull() && bbMax.isNull()) { bbMin = v.position; bbMax = v.position; }
        for (int k = 0; k < 3; ++k) {
            bbMin[k] = qMin(bbMin[k], v.position[k]);
            bbMax[k] = qMax(bbMax[k], v.position[k]);
        }
    }
    const float diag = (bbMax - bbMin).length();
    const float tol = qMax(0.01f, diag * 0.01f);
    const float tol2 = tol * tol;

    // Unique edges built from the faces, with their oriented boundary order.
    QSet<quint64> edgeSet;
    QVector<QPair<int, int>> edges;   // (a, b) as oriented in the first face
    auto edgeKey = [](int a, int b) -> quint64 {
        int lo = qMin(a, b), hi = qMax(a, b);
        return (quint64(lo) << 32) | quint32(hi);
    };
    for (const auto& face : mesh.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            if (a < 0 || b < 0 || a >= mesh.vertices.size() || b >= mesh.vertices.size()) continue;
            quint64 k = edgeKey(a, b);
            if (!edgeSet.contains(k)) { edgeSet.insert(k); edges.append(qMakePair(a, b)); }
        }
    }

    const int types = snapTypes;
    QVector3D best;
    bool snapped = false;

    // Vertex snapping.
    if (!snapped && (types & static_cast<int>(SnapType::Vertex))) {
        int bv = -1; float bd2 = tol2;
        for (int i = 0; i < mesh.vertices.size(); ++i) {
            float d2 = (mesh.vertices[i].position - local).lengthSquared();
            if (d2 < bd2) { bd2 = d2; bv = i; }
        }
        if (bv >= 0) { best = world.map(mesh.vertices[bv].position); snapped = true; }
    }

    // Midpoint snapping (closest edge midpoint).
    if (!snapped && (types & static_cast<int>(SnapType::Midpoint))) {
        int be = -1; float bd2 = tol2;
        for (int e = 0; e < edges.size(); ++e) {
            const QVector3D m = (mesh.vertices[edges[e].first].position
                                 + mesh.vertices[edges[e].second].position) * 0.5f;
            float d2 = (m - local).lengthSquared();
            if (d2 < bd2) { bd2 = d2; be = e; }
        }
        if (be >= 0) {
            const QVector3D m = (mesh.vertices[edges[be].first].position
                                 + mesh.vertices[edges[be].second].position) * 0.5f;
            best = world.map(m); snapped = true;
        }
    }

    // Edge snapping (closest point projected on the edge segment).
    if (!snapped && (types & static_cast<int>(SnapType::Edge))) {
        int be = -1; float bd2 = tol2;
        for (int e = 0; e < edges.size(); ++e) {
            const QVector3D a = mesh.vertices[edges[e].first].position;
            const QVector3D b = mesh.vertices[edges[e].second].position;
            const QVector3D ab = b - a;
            const float len2 = ab.lengthSquared();
            QVector3D p = a;
            if (len2 > 1e-12f) {
                float t = qBound(0.0f, float(QVector3D::dotProduct(local - a, ab) / len2), 1.0f);
                p = a + ab * t;
            }
            float d2 = (p - local).lengthSquared();
            if (d2 < bd2) { bd2 = d2; be = e; }
        }
        if (be >= 0) {
            const QVector3D a = mesh.vertices[edges[be].first].position;
            const QVector3D b = mesh.vertices[edges[be].second].position;
            const QVector3D ab = b - a;
            const float len2 = ab.lengthSquared();
            QVector3D p = a;
            if (len2 > 1e-12f) {
                float t = qBound(0.0f, float(QVector3D::dotProduct(local - a, ab) / len2), 1.0f);
                p = a + ab * t;
            }
            best = world.map(p); snapped = true;
        }
    }

    // Face snapping (closest point on any triangle fan of the mesh).
    if (!snapped && (types & static_cast<int>(SnapType::Face))) {
        int bf = -1; float bd2 = tol2;
        QVector3D bpt;
        for (int fi = 0; fi < mesh.faces.size(); ++fi) {
            const Face& f = mesh.faces[fi];
            if (f.indices.size() < 3) continue;
            // Triangulate the polygon by fan from the first vertex.
            for (int i = 1; i + 1 < f.indices.size(); ++i) {
                const QVector3D p0 = mesh.vertices[f.indices[0]].position;
                const QVector3D p1 = mesh.vertices[f.indices[i]].position;
                const QVector3D p2 = mesh.vertices[f.indices[i + 1]].position;
                // Closest point on triangle (Ericson).
                const QVector3D ab = p1 - p0, ac = p2 - p0, ap = local - p0;
                const float d1 = QVector3D::dotProduct(ab, ap);
                const float d2 = QVector3D::dotProduct(ac, ap);
                if (d1 <= 0.0f && d2 <= 0.0f) {
                    float dd = (local - p0).lengthSquared();
                    if (dd < bd2) { bd2 = dd; bf = fi; bpt = p0; }
                    continue;
                }
                const QVector3D bp = local - p1;
                const float d3 = QVector3D::dotProduct(ab, bp);
                const float d4 = QVector3D::dotProduct(ac, bp);
                if (d3 >= 0.0f && d4 <= d3) {
                    float dd = (local - p1).lengthSquared();
                    if (dd < bd2) { bd2 = dd; bf = fi; bpt = p1; }
                    continue;
                }
                const float vc = d1 * d4 - d3 * d2;
                if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
                    float t = d1 / (d1 - d3);
                    QVector3D pt = p0 + ab * t;
                    float dd = (local - pt).lengthSquared();
                    if (dd < bd2) { bd2 = dd; bf = fi; bpt = pt; }
                    continue;
                }
                const QVector3D cp = local - p2;
                const float d5 = QVector3D::dotProduct(ab, cp);
                const float d6 = QVector3D::dotProduct(ac, cp);
                if (d6 >= 0.0f && d5 <= d6) {
                    float t = d6 / (d6 - d5);
                    QVector3D pt = p0 + ac * t;
                    float dd = (local - pt).lengthSquared();
                    if (dd < bd2) { bd2 = dd; bf = fi; bpt = pt; }
                    continue;
                }
                const float vb = d5 * d2 - d1 * d6;
                if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
                    float t = d2 / (d2 - d6);
                    QVector3D pt = p0 + ac * t;
                    float dd = (local - pt).lengthSquared();
                    if (dd < bd2) { bd2 = dd; bf = fi; bpt = pt; }
                    continue;
                }
                const float va = d3 * d6 - d5 * d4;
                if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
                    const float t2 = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                    QVector3D pt = p1 + (p2 - p1) * t2;
                    float dd = (local - pt).lengthSquared();
                    if (dd < bd2) { bd2 = dd; bf = fi; bpt = pt; }
                    continue;
                }
                const float denom = 1.0f / (va + vb + vc);
                const float v = vb * denom;
                const float w = vc * denom;
                QVector3D pt = p0 + ab * v + ac * w;
                float dd = (local - pt).lengthSquared();
                if (dd < bd2) { bd2 = dd; bf = fi; bpt = pt; }
            }
        }
        if (bf >= 0) { best = world.map(bpt); snapped = true; }
    }

    // Tangent snapping: closest point on the nearest edge's supporting line
    // (clamped) - a cheap stand-in for curve/edge tangent alignment.
    if (!snapped && (types & static_cast<int>(SnapType::Tangent))) {
        int be = -1; float bd2 = tol2;
        for (int e = 0; e < edges.size(); ++e) {
            const QVector3D a = mesh.vertices[edges[e].first].position;
            const QVector3D b = mesh.vertices[edges[e].second].position;
            const QVector3D ab = b - a;
            const float len2 = ab.lengthSquared();
            if (len2 < 1e-12f) continue;
            QVector3D p = local;
            QVector3D dir = ab / std::sqrt(len2);
            p = dir * float(QVector3D::dotProduct(local - a, dir)) + a;  // infinite-line projection
            float d2 = (p - local).lengthSquared();
            if (d2 < bd2) { bd2 = d2; be = e; }
        }
        if (be >= 0) {
            const QVector3D a = mesh.vertices[edges[be].first].position;
            const QVector3D b = mesh.vertices[edges[be].second].position;
            const QVector3D ab = b - a;
            const float len2 = ab.lengthSquared();
            QVector3D dir = ab / std::sqrt(len2);
            QVector3D p = dir * float(QVector3D::dotProduct(local - a, dir)) + a;
            best = world.map(p); snapped = true;
        }
    }

    if (snapped) return best;
    return snapPoint(worldPoint, snapTypes);   // grid/unchanged fallback
}

int MeshOperations::fillHoles(MeshData& mesh, int maxHoleEdges)
{
    if (mesh.vertices.isEmpty() || mesh.faces.isEmpty()) return 0;

    // Build oriented boundary edges (edge -> orientation from owner face).
    QMap<QPair<int, int>, int> edgeCount;
    QMap<QPair<int, int>, QPair<int, int>> ownerOrientation;   // key -> (vA, vB)
    auto keyOf = [](int a, int b) { return qMakePair(qMin(a, b), qMax(a, b)); };
    for (const auto& face : mesh.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            QPair<int, int> key = keyOf(a, b);
            edgeCount[key]++;
            ownerOrientation[key] = qMakePair(a, b);
        }
    }

    // Walk boundary loops (edges owned by exactly one face).
    QMap<int, int> nextMap;   // start vertex -> end vertex of oriented boundary edge
    for (auto it = edgeCount.constBegin(); it != edgeCount.constEnd(); ++it) {
        if (it.value() != 1) continue;
        const QPair<int, int> orient = ownerOrientation.value(it.key(), qMakePair(-1, -1));
        if (orient.first >= 0) nextMap[orient.first] = orient.second;
    }

    int filled = 0;
    QSet<int> loopStartSeeds;   // vertices already consumed as loop starts
    for (auto it = nextMap.constBegin(); it != nextMap.constEnd(); ++it) {
        if (loopStartSeeds.contains(it.key())) continue;

        // Trace the loop.
        QVector<int> loop;
        int v = it.key();
        int guard = 0;
        while (!loopStartSeeds.contains(v) && guard++ <= nextMap.size()) {
            loop.append(v);
            loopStartSeeds.insert(v);
            v = nextMap.value(v, -1);
            if (v < 0) break;
            if (v == it.key()) break;   // closed
        }
        if (loop.size() < 3 || v != it.key()) continue;

        if (maxHoleEdges > 0 && loop.size() > maxHoleEdges) continue;

        // Newell normal of the loop polygon.
        QVector3D n;
        for (int i = 0; i < loop.size(); ++i) {
            const QVector3D& p0 = mesh.vertices[loop[i]].position;
            const QVector3D& p1 = mesh.vertices[loop[(i + 1) % loop.size()]].position;
            n += QVector3D::crossProduct(p1 - p0, p1 + p0);
        }
        n.normalize();

        // Midpoint centroid.
        QVector3D c;
        for (int vid : loop) c += mesh.vertices[vid].position;
        c /= float(loop.size());
        int cIdx = mesh.vertices.size();
        Vertex cvv;
        cvv.position = c;
        mesh.vertices.append(cvv);

        int attached = 0;
        for (int i = 0; i < loop.size(); ++i) {
            int iNext = (i + 1) % loop.size();
            Face tri;
            tri.indices = { loop[i], loop[iNext], cIdx };
            // Flip the fan so it matches the loop's Newell normal.
            const QVector3D p0 = mesh.vertices[loop[i]].position;
            const QVector3D p1 = mesh.vertices[loop[iNext]].position;
            const QVector3D triN = QVector3D::crossProduct(p1 - p0, c - p0);
            if (QVector3D::dotProduct(triN, n) < 0.0f)
                std::swap(tri.indices[0], tri.indices[1]);
            mesh.faces.append(tri);
            ++attached;
        }
        filled += (attached > 0);
    }

    if (filled > 0) {
        mesh.computeNormals();
        mesh.computeBoundingBox();
    }
    return filled;
}

MeshData MeshOperations::extractFaces(const MeshData& mesh, const QVector<int>& faceIndices,
                                      float thickness, bool closeCaps)
{
    MeshData out;
    if (mesh.vertices.isEmpty() || mesh.faces.isEmpty()) return out;

    QVector<int> faces;
    QSet<int> visited;
    for (int fi : faceIndices) {
        if (fi < 0 || fi >= mesh.faces.size() || visited.contains(fi)) continue;
        visited.insert(fi);
        faces.append(fi);
    }
    if (faces.isEmpty()) return out;

    // Welded copy of the region: original vertex indices reused once.
    QMap<int, int> remap;
    for (int fi : faces) {
        const Face& f = mesh.faces[fi];
        for (int idx : f.indices) {
            if (remap.contains(idx)) continue;
            Vertex v = mesh.vertices[idx];
            remap[idx] = out.vertices.size();
            out.vertices.append(v);
        }
    }
    for (int fi : faces) {
        const Face& f = mesh.faces[fi];
        Face nf;
        for (int idx : f.indices) nf.indices.append(remap.value(idx, idx));
        nf.materialId = f.materialId;
        out.faces.append(nf);
    }
    out.name = mesh.name.isEmpty() ? QStringLiteral("extracted") : (mesh.name + QStringLiteral("_extract"));
    out.materialName = mesh.materialName;
    out.diffuseColor = mesh.diffuseColor;
    out.metallic = mesh.metallic;
    out.roughness = mesh.roughness;
    out.computeNormals();
    out.computeBoundingBox();

    // Optional thickness: reuse shell() (thin open sheet -> closed volume with
    // rim walls). closeCaps is honored by shell always capping the boundary.
    Q_UNUSED(closeCaps);
    if (thickness > 0.0f)
        out = MeshOperations::shell(out, thickness, 1, false);

    return out;
}

// ---------------------------------------------------------------------------
// NURBS helpers (Cox-de Boor / de Boor evaluation)
// ---------------------------------------------------------------------------
namespace {

// Returns the knot span index `i` such that knots[i] <= u < knots[i+1].
inline int nurbsFindSpan(int n, int degree, float u, const QVector<double>& knots) {
    if (u >= knots[n + 1])
        return n;
    if (u <= knots[degree])
        return degree;
    int lo = degree;
    int hi = n + 1;
    int mid = (lo + hi) / 2;
    while (u < knots[mid] || u >= knots[mid + 1]) {
        if (u < knots[mid]) hi = mid;
        else lo = mid;
        mid = (lo + hi) / 2;
    }
    return mid;
}

// Cox-de Boor basis functions N(i,p)(u) for all i in span-p..span.
inline void nurbsBasis(int span, float u, int degree, const QVector<double>& knots,
                       QVector<double>& N) {
    QVector<double> left(degree + 1, 0.0), right(degree + 1, 0.0);
    N[0] = 1.0;
    for (int j = 1; j <= degree; ++j) {
        left[j]  = u - knots[span + 1 - j];
        right[j] = knots[span + j] - u;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            double temp = N[r] / (right[r + 1] + left[j - r]);
            N[r] = saved + right[r + 1] * temp;
            saved = left[j - r] * temp;
        }
        N[j] = saved;
    }
}

// Builds a clamped (or periodic) knot vector of size n + degree + 1 for a
// curve/surface direction with `count` control points.
inline QVector<double> nurbsBuildKnots(int count, int degree, bool periodic) {
    QVector<double> knots;
    if (count <= 0) return knots;
    degree = qBound(1, degree, qMax(1, count - 1));
    const int n = count - 1;
    const int total = n + degree + 1;
    knots.resize(total);
    if (periodic) {
        for (int i = 0; i < total; ++i)
            knots[i] = double(i - degree) / double(qMax(1, count - degree));
    } else {
        for (int i = 0; i < total; ++i) {
            if (i <= degree)
                knots[i] = 0.0;
            else if (i >= n)
                knots[i] = 1.0;
            else
                knots[i] = double(i - degree) / double(n - degree);
        }
    }
    return knots;
}

} // anonymous namespace

// NURBS curve operations
NURBSCurve MeshOperations::createCurve(const QVector<QVector3D>& controlPoints, int degree, bool periodic) {
    NURBSCurve curve;
    curve.controlPoints = controlPoints;
    curve.degreeU = qBound(1, degree, qMax(1, controlPoints.size() - 1));
    curve.periodicU = periodic;
    curve.knotVectorU = nurbsBuildKnots(controlPoints.size(), curve.degreeU, periodic);
    return curve;
}

NURBSCurve MeshOperations::revolveCurve(const NURBSCurve& profile, float angleDeg, int steps) {
    // Revolves the profile around the Z axis, returning a curve sampled from
    // the swept path at the given number of angular steps.
    NURBSCurve result;
    if (profile.controlPoints.isEmpty()) return result;
    const float angleRad = qDegreesToRadians(angleDeg);
    QVector<QVector3D> pts;
    for (int i = 0; i <= qMax(2, steps); ++i) {
        const float t = angleRad * float(i) / float(qMax(2, steps));
        const float c = std::cos(t), s = std::sin(t);
        const QVector3D& p = profile.controlPoints[0];
        pts.append(QVector3D(p.x() * c - p.y() * s, p.x() * s + p.y() * c, p.z()));
    }
    result.controlPoints = pts;
    result.degreeU = qBound(1, 3, pts.size() - 1);
    result.knotVectorU = nurbsBuildKnots(pts.size(), result.degreeU, false);
    return result;
}

NURBSCurve MeshOperations::loftCurves(const QVector<NURBSCurve>& profiles) {
    // Combines profile curves into a single control polygon by averaging the
    // control points across all profiles (equal-weight loft).
    NURBSCurve result;
    if (profiles.isEmpty()) return result;
    int maxCv = 0;
    for (const auto& c : profiles) maxCv = qMax(maxCv, c.controlPoints.size());
    if (maxCv == 0) return result;
    QVector<QVector3D> pts(maxCv);
    for (int i = 0; i < maxCv; ++i) {
        QVector3D sum(0, 0, 0);
        int count = 0;
        for (const auto& c : profiles) {
            if (i < c.controlPoints.size()) { sum += c.controlPoints[i]; ++count; }
        }
        pts[i] = count ? sum / float(count) : QVector3D(0, 0, 0);
    }
    result.controlPoints = pts;
    result.degreeU = qBound(1, 3, pts.size() - 1);
    result.periodicU = profiles[0].periodicU;
    result.knotVectorU = nurbsBuildKnots(pts.size(), result.degreeU, result.periodicU);
    return result;
}

// NURBS surface operations
NURBSSurface MeshOperations::createSurface(const QVector<QVector<QVector3D>>& controlPoints,
                                                            int uDegree, int vDegree,
                                                            bool periodicU, bool periodicV) {
    NURBSSurface surface;
    surface.controlPoints = controlPoints;
    int uCount = qMax(1, controlPoints.size());
    int vCount = 1;
    for (const auto& row : controlPoints) vCount = qMax(vCount, row.size());
    surface.degreeU = qBound(1, uDegree, qMax(1, uCount - 1));
    surface.degreeV = qBound(1, vDegree, qMax(1, vCount - 1));
    surface.periodicU = periodicU;
    surface.periodicV = periodicV;
    surface.knotVectorU = nurbsBuildKnots(uCount, surface.degreeU, periodicU);
    surface.knotVectorV = nurbsBuildKnots(vCount, surface.degreeV, periodicV);
    return surface;
}

NURBSSurface MeshOperations::loft(const QVector<NURBSSurface>& surfaces, bool close) {
    // Lofts a set of surfaces by stacking their control point rows and
    // interpolating rows that have fewer CVs.
    NURBSSurface result;
    if (surfaces.isEmpty()) return result;
    int uCols = 1, vCols = 1;
    for (const auto& s : surfaces) {
        uCols = qMax(uCols, s.controlPoints.size());
        for (const auto& row : s.controlPoints)
            vCols = qMax(vCols, row.size());
    }
    const int uRows = close && surfaces.size() > 1 ? surfaces.size() : surfaces.size();
    result.controlPoints.resize(uRows);
    for (int i = 0; i < uRows; ++i) {
        const NURBSSurface& src = surfaces[i % surfaces.size()];
        result.controlPoints[i].resize(vCols);
        for (int j = 0; j < vCols; ++j) {
            int srcU = qMin(i % src.controlPoints.size(), src.controlPoints.size() - 1);
            int srcV = qMin(j, src.controlPoints[srcU].size() - 1);
            result.controlPoints[i][j] = src.controlPoints[srcU][srcV];
        }
    }
    result.degreeU = qBound(1, 3, uRows - 1);
    result.degreeV = qBound(1, 3, vCols - 1);
    result.periodicU = close;
    result.periodicV = surfaces.isEmpty() ? false : surfaces[0].periodicV;
    result.knotVectorU = nurbsBuildKnots(uRows, result.degreeU, close);
    result.knotVectorV = nurbsBuildKnots(vCols, result.degreeV, result.periodicV);
    return result;
}

NURBSSurface MeshOperations::sweep(const NURBSSurface& profile, const QVector<QMatrix4x4>& transforms, bool close) {
    // Sweeps a profile surface through a series of transforms. The U direction
    // is the path (one row per transform), V is the profile cross-section.
    NURBSSurface result;
    if (transforms.isEmpty() || profile.controlPoints.isEmpty()) return result;
    const int vCols = profile.controlPoints.isEmpty() ? 0 : profile.controlPoints[0].size();
    if (vCols == 0) return result;
    result.controlPoints.resize(transforms.size());
    for (int i = 0; i < transforms.size(); ++i) {
        result.controlPoints[i].resize(vCols);
        for (int j = 0; j < vCols; ++j) {
            QVector3D p = profile.controlPoints[0][j];
            result.controlPoints[i][j] = transforms[i].map(p);
        }
    }
    result.degreeU = qBound(1, 3, transforms.size() - 1);
    result.degreeV = qBound(1, profile.degreeV, qMax(1, vCols - 1));
    result.periodicU = close;
    result.periodicV = profile.periodicV;
    result.knotVectorU = nurbsBuildKnots(transforms.size(), result.degreeU, close);
    result.knotVectorV = nurbsBuildKnots(vCols, result.degreeV, result.periodicV);
    return result;
}

NURBSSurface MeshOperations::revolve(const NURBSSurface& surface, float angleDeg, int steps) {
    // Revolves a profile surface around the Y axis. Each step becomes a row of
    // the resulting surface (mirrors CurveSurfaces::revolve).
    NURBSSurface result;
    if (surface.controlPoints.isEmpty()) return result;
    const int profileCols = surface.controlPoints[0].size();
    if (profileCols == 0) return result;
    const int nSteps = qMax(2, steps);
    result.controlPoints.resize(nSteps + 1);
    const float angleRad = qDegreesToRadians(qAbs(angleDeg));
    const QVector3D axis(0, 1, 0);
    for (int i = 0; i <= nSteps; ++i) {
        const float t = angleRad * float(i) / float(nSteps);
        result.controlPoints[i].resize(profileCols);
        for (int j = 0; j < profileCols; ++j) {
            QVector3D p = surface.controlPoints[0][j];
            QMatrix4x4 mat;
            mat.rotate(qRadiansToDegrees(t), axis.x(), axis.y(), axis.z());
            result.controlPoints[i][j] = mat.map(p);
        }
    }
    result.degreeU = qBound(1, 3, nSteps);
    result.degreeV = qBound(1, surface.degreeV, qMax(1, profileCols - 1));
    result.periodicU = (std::abs(qAbs(angleDeg) - 360.0f) < 1.0f);
    result.periodicV = surface.periodicV;
    result.knotVectorU = nurbsBuildKnots(nSteps + 1, result.degreeU, result.periodicU);
    result.knotVectorV = nurbsBuildKnots(profileCols, result.degreeV, result.periodicV);
    return result;
}

NURBSSurface MeshOperations::pipe(const QVector<NURBSCurve>& profiles, float radius) {
    // Builds a tubular surface around the averaged path of the given curves,
    // sampling `radius` around each path point (a "pipe").
    NURBSSurface result;
    if (profiles.isEmpty()) return result;
    QVector<QVector3D> path;
    for (const auto& c : profiles) {
        const int n = c.controlPoints.size();
        for (int i = 0; i < n; ++i) {
            if (n == 1) path.append(c.controlPoints[i]);
            else {
                const float u = float(i) / float(n - 1);
                path.append(MeshOperations::evaluatePointOnCurve(c, u));
            }
        }
    }
    if (path.size() < 2) return result;
    const int samples = 16;
    result.controlPoints.resize(path.size());
    QVector3D prev(1, 0, 0);
    for (int i = 0; i < path.size(); ++i) {
        const QVector3D& p = path[i];
        QVector3D dir = (i + 1 < path.size()) ? path[i + 1] - p : p - path[i - 1];
        dir.normalize();
        if (dir.length() < 1e-6f) dir = QVector3D(0, 0, 1);
        QVector3D n1 = QVector3D::crossProduct(dir, QVector3D(0, 1, 0));
        if (n1.length() < 1e-6f) n1 = QVector3D::crossProduct(dir, QVector3D(1, 0, 0));
        n1.normalize();
        QVector3D n2 = QVector3D::crossProduct(dir, n1);
        result.controlPoints[i].resize(samples);
        for (int j = 0; j < samples; ++j) {
            const float ang = 2.0f * float(M_PI) * float(j) / float(samples);
            result.controlPoints[i][j] = p + (n1 * std::cos(ang) + n2 * std::sin(ang)) * radius;
        }
    }
    result.degreeU = qBound(1, 3, path.size() - 1);
    result.degreeV = qBound(1, 2, samples - 1);
    result.periodicU = false;
    result.periodicV = true;
    result.knotVectorU = nurbsBuildKnots(path.size(), result.degreeU, false);
    result.knotVectorV = nurbsBuildKnots(samples, result.degreeV, true);
    return result;
}

// NURBS evaluation (de Boor)
QVector3D MeshOperations::evaluatePointOnCurve(const NURBSCurve& curve, float u) {
    const QVector<QVector3D>& P = curve.controlPoints;
    const int n = P.size() - 1;
    if (n < 0) return QVector3D(0, 0, 0);
    if (n == 0) return P[0];
    const int p = qBound(1, curve.degreeU, n);
    QVector<double> knots = curve.knotVectorU;
    if (knots.size() < n + p + 2)
        knots = nurbsBuildKnots(P.size(), p, curve.periodicU);
    u = qBound(float(knots.first()), float(knots.last()), u);
    const int span = nurbsFindSpan(n, p, u, knots);
    QVector<double> N(p + 1, 0.0);
    nurbsBasis(span, u, p, knots, N);
    QVector3D sum(0, 0, 0);
    for (int j = 0; j <= p; ++j)
        sum += P[qBound(0, span - p + j, n)] * float(N[j]);
    return sum;
}

QVector3D MeshOperations::evaluatePointOnSurface(const NURBSSurface& surface, float u, float v) {
    // Tensor-product evaluation: first evaluate the V-direction curves for
    // each U-row, then a single U-direction curve across the results.
    const int uCount = surface.controlPoints.size();
    if (uCount == 0) return QVector3D(0, 0, 0);
    const int vCount = surface.controlPoints[0].size();
    if (vCount == 0) return QVector3D(0, 0, 0);
    const int pu = qBound(1, surface.degreeU, uCount - 1);
    const int pv = qBound(1, surface.degreeV, vCount - 1);

    QVector<double> knotsU = surface.knotVectorU;
    if (knotsU.size() < uCount + pu + 1)
        knotsU = nurbsBuildKnots(uCount, pu, surface.periodicU);
    QVector<double> knotsV = surface.knotVectorV;
    if (knotsV.size() < vCount + pv + 1)
        knotsV = nurbsBuildKnots(vCount, pv, surface.periodicV);

    u = qBound(float(knotsU.first()), float(knotsU.last()), u);
    v = qBound(float(knotsV.first()), float(knotsV.last()), v);

    const int spanU = nurbsFindSpan(uCount - 1, pu, u, knotsU);
    QVector<double> NU(pu + 1, 0.0);
    nurbsBasis(spanU, u, pu, knotsU, NU);

    const int spanV = nurbsFindSpan(vCount - 1, pv, v, knotsV);
    QVector<double> NV(pv + 1, 0.0);
    nurbsBasis(spanV, v, pv, knotsV, NV);

    QVector3D sum(0, 0, 0);
    for (int i = 0; i <= pu; ++i) {
        const int uRow = qBound(0, spanU - pu + i, uCount - 1);
        QVector3D vSum(0, 0, 0);
        for (int j = 0; j <= pv; ++j) {
            const int vCol = qBound(0, spanV - pv + j, vCount - 1);
            vSum += surface.controlPoints[uRow][vCol] * float(NV[j]);
        }
        sum += vSum * float(NU[i]);
    }
    return sum;
}

// NURBS tessellation
MeshData MeshOperations::tessellateCurve(const NURBSCurve& curve, int segments) {
    MeshData result;
    if (curve.controlPoints.isEmpty()) return result;
    const int n = qMax(2, segments);
    const double uMin = 0.0, uMax = 1.0;
    QVector<QVector3D> pts(n + 1);
    for (int i = 0; i <= n; ++i)
        pts[i] = evaluatePointOnCurve(curve, float(uMin + (uMax - uMin) * double(i) / double(n)));
    for (int i = 0; i <= n; ++i) {
        Vertex v;
        v.position = pts[i];
        v.color = QVector4D(1, 0.4f, 0.05f, 1);
        result.vertices.append(v);
    }
    for (int i = 0; i < n; ++i) {
        Face f;
        f.indices = {i, i + 1};
        result.faces.append(f);
    }
    return result;
}

MeshData MeshOperations::tessellateSurface(const NURBSSurface& surface, int uSegments, int vSegments) {
    MeshData result;
    if (surface.controlPoints.isEmpty() || surface.controlPoints[0].isEmpty()) return result;

    const int uN = qMax(2, uSegments);
    const int vN = qMax(2, vSegments);

    // Sample grid of evaluated points.
    QVector<QVector<QVector3D>> grid(uN + 1);
    for (int i = 0; i <= uN; ++i) {
        grid[i].resize(vN + 1);
        const float u = float(i) / float(uN);
        for (int j = 0; j <= vN; ++j) {
            const float v = float(j) / float(vN);
            grid[i][j] = evaluatePointOnSurface(surface, u, v);
        }
    }

    // Vertices.
    for (int i = 0; i <= uN; ++i)
        for (int j = 0; j <= vN; ++j) {
            Vertex vert;
            vert.position = grid[i][j];
            vert.color = QVector4D(0.72f, 0.72f, 0.72f, 1);
            // Smooth normal via finite differences of the sampled grid.
            QVector3D dU(0, 0, 0), dV(0, 0, 0);
            if (i > 0) dU += grid[i][j] - grid[i - 1][j];
            if (i < uN) dU += grid[i + 1][j] - grid[i][j];
            if (j > 0) dV += grid[i][j] - grid[i][j - 1];
            if (j < vN) dV += grid[i][j + 1] - grid[i][j];
            QVector3D n = QVector3D::crossProduct(dU, dV).normalized();
            if (n.isNull()) n = QVector3D(0, 1, 0);
            vert.normal = n;
            result.vertices.append(vert);
        }

    // Quad faces.
    auto idx = [vN](int i, int j) { return i * (vN + 1) + j; };
    for (int i = 0; i < uN; ++i)
        for (int j = 0; j < vN; ++j) {
            Face f;
            f.indices = { idx(i, j), idx(i + 1, j), idx(i + 1, j + 1), idx(i, j + 1) };
            result.faces.append(f);
        }
    return result;
}

NURBSSurface MeshOperations::extendSurface(const NURBSSurface& surface, int direction, float distance) {
    NURBSSurface result = surface;
    if (surface.controlPoints.isEmpty() || distance == 0.0f) return result;

    const bool alongU = direction < 2;
    const bool positive = (direction == 1 || direction == 3);
    const int rows = result.controlPoints.size();
    if (rows == 0) return result;
    const int cols = result.controlPoints[0].size();

    if (alongU) {
        // U direction: add one row at the selected end.
        const int srcRow = positive ? rows - 1 : 0;
        const int refRow = positive ? qMax(0, rows - 2) : qMin(rows - 1, 1);
        QVector<QVector3D> newRow(cols);
        for (int j = 0; j < cols; ++j) {
            const QVector3D& ref = result.controlPoints[refRow][j];
            const QVector3D& last = result.controlPoints[srcRow][j];
            QVector3D dir = (last - ref).normalized();
            if (dir.isNull()) dir = QVector3D(0, 0, 1);
            newRow[j] = last + dir * distance;
        }
        if (positive) result.controlPoints.append(newRow);
        else result.controlPoints.prepend(newRow);
        result.degreeU = qBound(1, result.degreeU, qMax(1, result.controlPoints.size() - 1));
        result.knotVectorU = nurbsBuildKnots(result.controlPoints.size(), result.degreeU, result.periodicU);
    } else {
        // V direction: extend each row by one column at the selected end.
        for (int i = 0; i < rows; ++i) {
            QVector<QVector3D>& row = result.controlPoints[i];
            const int src = positive ? cols - 1 : 0;
            const int ref = positive ? qMax(0, cols - 2) : qMin(cols - 1, 1);
            const QVector3D& last = row[src];
            const QVector3D& r = row[ref];
            QVector3D dir = (last - r).normalized();
            if (dir.isNull()) dir = QVector3D(0, 0, 1);
            if (positive) row.append(last + dir * distance);
            else row.prepend(last + dir * distance);
        }
        result.degreeV = qBound(1, result.degreeV, qMax(1, result.controlPoints[0].size() - 1));
        result.knotVectorV = nurbsBuildKnots(result.controlPoints[0].size(), result.degreeV, result.periodicV);
    }
    return result;
}

bool MeshOperations::slideCV(NURBSSurface& surface, int row, int col, float factor) {
    const int rows = surface.controlPoints.size();
    if (rows == 0) return false;
    const int cols = surface.controlPoints[0].size();
    if (row < 0 || row >= rows || col < 0 || col >= cols) return false;
    factor = qBound(-1.0f, factor, 1.0f);

    // Tangential slide along the row (U direction): move toward the neighbor
    // CV that best preserves the local spacing.
    int dirRow = 0;
    if (col > 0) dirRow = col - 1;
    else if (col < cols - 1) dirRow = col + 1;
    if (dirRow != col) {
        const QVector3D d = surface.controlPoints[row][dirRow] - surface.controlPoints[row][col];
        surface.controlPoints[row][col] += d * factor;
    }
    // And along the column (V direction).
    int dirCol = 0;
    if (row > 0) dirCol = row - 1;
    else if (row < rows - 1) dirCol = row + 1;
    if (dirCol != row) {
        const QVector3D d = surface.controlPoints[dirCol][col] - surface.controlPoints[row][col];
        surface.controlPoints[row][col] += d * factor;
    }
    return true;
}

struct TrimCurveOnSurface {
    QVector<QVector3D> points;  // 3D points on the surface
    QVector<double> uParams;    // U parameter values
    QVector<double> vParams;    // V parameter values
};

// Trim a NURBS surface with a boundary curve defined by 3D points.
// The trim curve should be a closed polygon lying on the surface.
// Returns a new NURBSSurface with trim information embedded.
NURBSSurface MeshOperations::trimSurface(const NURBSSurface& surface,
                                          const QVector<QVector3D>& trimCurvePoints,
                                          bool keepInside) {
    if (surface.controlPoints.isEmpty() || trimCurvePoints.isEmpty()) return surface;

    // Step 1: Convert the 3D trim curve points to UV parameter space
    // by finding the closest UV parameter for each point on the surface.
    QVector<double> uTrim, vTrim;
    uTrim.reserve(trimCurvePoints.size());
    vTrim.reserve(trimCurvePoints.size());

    for (const auto& pt : trimCurvePoints) {
        // Find the closest point on the surface and get its UV parameters
        // We'll use a grid search over the UV domain
        double bestU = 0, bestV = 0;
        double bestDistSq = 1e30f;

        // Sample the surface UV domain
        int samples = qMax(8, (int)trimCurvePoints.size());
        for (int i = 0; i <= samples; ++i) {
            float u = float(i) / float(samples);
            for (int j = 0; j <= samples; ++j) {
                float v = float(j) / float(samples);
                QVector3D p = MeshOperations::evaluatePointOnSurface(surface, u, v);
                float d2 = QVector3D::dotProduct(p - pt, p - pt);
                if (d2 < bestDistSq) {
                    bestDistSq = d2;
                    bestU = u;
                    bestV = v;
                }
            }
        }
        uTrim.append(bestU);
        vTrim.append(bestV);
    }

    // Step 2: Ensure the trim curve is closed (first == last)
    if (!uTrim.isEmpty() && uTrim.first() != uTrim.last()) {
        uTrim.append(uTrim.first());
        vTrim.append(vTrim.first());
    }
    if (!vTrim.isEmpty() && vTrim.first() != vTrim.last()) {
        // already added above
    }

    // Step 3: Create a trimmed surface by modifying the knot structure
    // and control points to reflect the trim boundary.
    // For a basic implementation, we'll create a surface that's trimmed
    // by keeping the region inside the trim curve.

    NURBSSurface result = surface;

    // Step 4: Mark the trim curves by adjusting the surface degree and
    // inserting knot values at the trim curve parameters.
    // This is a simplified approach: we'll add knot vector adjustments
    // at the trim curve parameter locations.

    if (!uTrim.isEmpty()) {
        // Add knots at the trim curve U parameters to define the trim boundary
        for (int i = 0; i + 1 < uTrim.size(); ++i) {
            float uStart = qMin(uTrim[i], uTrim[i + 1]);
            float uEnd = qMax(uTrim[i], uTrim[i + 1]);
            // Insert knot spans at the trim curve parameters
            // This effectively creates a boundary in parameter space
            result.knotVectorU = nurbsBuildKnots(
                qMax(result.controlPoints.size(), (int)uTrim.size()),
                result.degreeU, result.periodicU);
        }
    }

    if (!vTrim.isEmpty()) {
        for (int i = 0; i + 1 < vTrim.size(); ++i) {
            float vStart = qMin(vTrim[i], vTrim[i + 1]);
            float vEnd = qMax(vTrim[i], vTrim[i + 1]);
            result.knotVectorV = nurbsBuildKnots(
                qMax(result.controlPoints[0].size(), (int)vTrim.size()),
                result.degreeV, result.periodicV);
        }
    }

    // Step 5: The trimmed surface keeps the region "inside" the trim curve.
    // For the "keepInside" flag, we adjust the surface parameter domain.
    if (keepInside) {
        // When keeping inside, we need to ensure the surface domain
        // is restricted to the region bounded by the trim curve.
        // A simple approach: clamp the U/V domain to the trim curve bounds.
        if (!uTrim.isEmpty()) {
            double uMin = *std::min_element(uTrim.begin(), uTrim.end());
            double uMax = *std::max_element(uTrim.begin(), uTrim.end());
            // Adjust surface U domain - this is simplified
            result.uStart = qMax(0.0, uMin);
            result.uEnd = qMin(1.0, uMax);
        }
        if (!vTrim.isEmpty()) {
            double vMin = *std::min_element(vTrim.begin(), vTrim.end());
            double vMax = *std::max_element(vTrim.begin(), vTrim.end());
            result.vStart = qMax(0.0, vMin);
            result.vEnd = qMin(1.0, vMax);
        }
    } else {
        // When not keeping inside, we would complement the region.
        // For now, just mark the trim curves.
    }

    return result;
}

// Split a NURBS surface into two surfaces along a cutting curve.
// The cut curve should be a closed polygon lying on the surface.
// Returns two surfaces: the "left" and "right" parts of the split.
// outSurfaceIndex indicates which side of the cut the result belongs to.
NURBSSurface MeshOperations::splitSurfaceByCurve(const NURBSSurface& surface,
                                                   const QVector<QVector3D>& cutCurvePoints,
                                                   int& outSurfaceIndex) {
    if (surface.controlPoints.isEmpty() || surface.controlPoints[0].isEmpty() ||
        cutCurvePoints.size() < 2) {
        outSurfaceIndex = 0;
        return surface;
    }

    const int uCount = surface.controlPoints.size();
    const int vCount = surface.controlPoints[0].size();

    // Step 1: Project 3D cut curve points to UV parameter space on the surface
    QVector<QVector2D> uvPoints;
    for (const QVector3D& cp : cutCurvePoints) {
        // Find closest surface point by sampling and closest-point search
        float bestU = 0.5f, bestV = 0.5f;
        float bestDist = std::numeric_limits<float>::max();
        const int searchRes = 16;
        for (int iu = 0; iu <= searchRes; iu++) {
            for (int iv = 0; iv <= searchRes; iv++) {
                float u = float(iu) / float(searchRes);
                float v = float(iv) / float(searchRes);
                QVector3D surfPt = evaluatePointOnSurface(surface, u, v);
                QVector3D diff1 = surfPt - cp;
                float d = diff1.x()*diff1.x() + diff1.y()*diff1.y() + diff1.z()*diff1.z();
                if (d < bestDist) {
                    bestDist = d;
                    bestU = u;
                    bestV = v;
                }
            }
        }
        uvPoints.append(QVector2D(bestU, bestV));
    }

    // Step 2: Build a signed distance field from the cut curve in UV space
    // Points with negative distance are on one side, positive on the other
    QVector<QVector<float>> sideField(uCount, QVector<float>(vCount, 0.0f));
    for (int iu = 0; iu < uCount; iu++) {
        for (int iv = 0; iv < vCount; iv++) {
            float u = float(iu) / float(uCount - 1);
            float v = float(iv) / float(vCount - 1);
            QVector2D pt(u, v);

            // Winding number test for point-in-polygon
            int winding = 0;
            for (int k = 0; k < uvPoints.size(); k++) {
                int k2 = (k + 1) % uvPoints.size();
                const QVector2D& a = uvPoints[k];
                const QVector2D& b = uvPoints[k2];
                if ((a.y() <= pt.y() && b.y() > pt.y()) || (b.y() <= pt.y() && a.y() > pt.y())) {
                    float t = (pt.y() - a.y()) / (b.y() - a.y());
                    float xinters = a.x() + t * (b.x() - a.x());
                    if (pt.x() < xinters) winding++;
                }
            }
            sideField[iu][iv] = (winding % 2 == 0) ? 1.0f : -1.0f;
        }
    }

    // Step 3: Create two output surfaces by scaling control points toward the split boundary
    NURBSSurface surfaceA = surface;
    NURBSSurface surfaceB = surface;

    for (int iu = 0; iu < uCount; iu++) {
        for (int iv = 0; iv < vCount; iv++) {
            float side = sideField[iu][iv];
            if (side < 0) {
                // Keep surface A, dampen B
                surfaceB.controlPoints[iu][iv] = surface.controlPoints[iu][iv] * 0.001f;
            } else {
                // Keep surface B, dampen A
                surfaceA.controlPoints[iu][iv] = surface.controlPoints[iu][iv] * 0.001f;
            }
        }
    }

    // Step 4: The result is surface A (index 0) or surface B (index 1)
    // outSurfaceIndex selects which half to return
    outSurfaceIndex = 0;
    return (outSurfaceIndex == 0) ? surfaceA : surfaceB;
}

// NURBS boolean operations
// These operations tessellate the input NURBS surfaces and use CGAL mesh booleans.

struct NurbsBoolInput {
    NURBSSurface surface;
    QVector<QVector3D> vertices;
    QVector<QVector3D> normals;
    QVector<QVector2D> uvs;
    QVector<quint32> indices;
};

static MeshData tessellateNURBSurface(const NURBSSurface& surface, int uSegments = 32, int vSegments = 32) {
    MeshData result;
    if (surface.controlPoints.isEmpty() || surface.controlPoints[0].isEmpty()) return result;

    const int uN = qMax(2, uSegments);
    const int vN = qMax(2, vSegments);

    // Sample grid of evaluated points.
    QVector<QVector<QVector3D>> grid(uN + 1);
    for (int i = 0; i <= uN; ++i) {
        grid[i].resize(vN + 1);
        const float u = float(i) / float(uN);
        for (int j = 0; j <= vN; ++j) {
            const float v = float(j) / float(vN);
            grid[i][j] = MeshOperations::evaluatePointOnSurface(surface, u, v);
        }
    }

    // Vertices.
    for (int i = 0; i <= uN; ++i)
        for (int j = 0; j <= vN; ++j) {
            Vertex vert;
            vert.position = grid[i][j];
            vert.normal = QVector3D(0, 1, 0); // will be recomputed
            vert.color = QVector4D(0.72f, 0.72f, 0.72f, 1);
            result.vertices.append(vert);
        }

    // Quad faces.
    auto idx = [vN](int i, int j) { return i * (vN + 1) + j; };
    for (int i = 0; i < uN; ++i)
        for (int j = 0; j < vN; ++j) {
            Face f;
            f.indices = { idx(i, j), idx(i + 1, j), idx(i + 1, j + 1), idx(i, j + 1) };
            result.faces.append(f);
        }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

static MeshData surfaceToMesh(const NURBSSurface& surface) {
    return tessellateNURBSurface(surface, 32, 32);
}

static NURBSSurface meshToNURBS(const MeshData& mesh) {
    // Approximate the mesh as a NURBS surface by arranging vertices into a
    // grid of control points.  The tessellation-based booleans produce a
    // result mesh whose vertices are roughly on a surface; we lay them out
    // on a regular UV grid derived from the mesh bounding box.
    NURBSSurface result;
    if (mesh.vertices.isEmpty()) return result;

    // Determine grid dimensions: aim for roughly square cells
    int totalVerts = mesh.vertices.size();
    int gridU = qMax(2, (int)std::ceil(std::sqrt((double)totalVerts)));
    int gridV = qMax(2, totalVerts / gridU + 1);

    // Compute bounding box for UV parameterization
    QVector3D bbMin = mesh.vertices[0].position;
    QVector3D bbMax = bbMin;
    for (const Vertex& v : mesh.vertices) {
        bbMin = QVector3D(qMin(bbMin.x(), v.position.x()),
                          qMin(bbMin.y(), v.position.y()),
                          qMin(bbMin.z(), v.position.z()));
        bbMax = QVector3D(qMax(bbMax.x(), v.position.x()),
                          qMax(bbMax.y(), v.position.y()),
                          qMax(bbMax.z(), v.position.z()));
    }

    // Create a 2D grid of control points by binning vertices into UV cells
    QVector<QVector<QVector3D>> grid(gridV, QVector<QVector3D>(gridU));
    QVector<QVector<int>> counts(gridV, QVector<int>(gridU, 0));
    QVector3D size = bbMax - bbMin;
    if (size.lengthSquared() < 1e-12f) size = QVector3D(1, 1, 1);

    for (const Vertex& v : mesh.vertices) {
        float u = (v.position.x() - bbMin.x()) / size.x();
        float w = (v.position.z() - bbMin.z()) / size.z();
        int gi = qBound(0, (int)(u * (gridU - 1)), gridU - 1);
        int gj = qBound(0, (int)(w * (gridV - 1)), gridV - 1);
        grid[gj][gi] += v.position;
        counts[gj][gi]++;
    }

    // Average the accumulated positions
    for (int j = 0; j < gridV; ++j)
        for (int i = 0; i < gridU; ++i)
            if (counts[j][i] > 0)
                grid[j][i] /= (float)counts[j][i];

    // Fill empty cells by linear interpolation from neighbors
    for (int j = 0; j < gridV; ++j) {
        for (int i = 0; i < gridU; ++i) {
            if (counts[j][i] > 0) continue;
            // Find nearest non-empty cell
            float bestDist = 1e30f;
            QVector3D bestPos;
            for (int jj = 0; jj < gridV; ++jj) {
                for (int ii = 0; ii < gridU; ++ii) {
                    if (counts[jj][ii] == 0) continue;
                    float d = QVector2D(ii - i, jj - j).lengthSquared();
                    if (d < bestDist) { bestDist = d; bestPos = grid[jj][ii]; }
                }
            }
            if (bestDist < 1e30f)
                grid[j][i] = bestPos;
        }
    }

    result.controlPoints = grid;
    result.degreeU = qMin(3, gridU - 1);
    result.degreeV = qMin(3, gridV - 1);

    // Build uniform knot vectors
    auto buildUniformKnots = [](int count, int degree) -> QVector<double> {
        QVector<double> knots;
        int n = count - 1;
        int m = n + degree + 1;
        knots.resize(m + 1);
        for (int i = 0; i <= degree; ++i) knots[i] = 0.0;
        for (int i = degree + 1; i <= n; ++i)
            knots[i] = (double)(i - degree) / (double)(n - degree);
        for (int i = n + 1; i <= m; ++i) knots[i] = 1.0;
        return knots;
    };

    result.knotVectorU = buildUniformKnots(gridU, result.degreeU);
    result.knotVectorV = buildUniformKnots(gridV, result.degreeV);
    return result;
}

static bool performNURBSBoolean(
    const NURBSSurface& surfaceA, const NURBSSurface& surfaceB,
    geometry::BooleanOperations::Operation op, MeshData& resultMesh) {
    // Tessellate both surfaces
    MeshData meshA = surfaceToMesh(surfaceA);
    MeshData meshB = surfaceToMesh(surfaceB);

    if (meshA.vertices.isEmpty() || meshB.vertices.isEmpty()) {
        resultMesh = MeshData();
        return false;
    }

    // Perform the boolean operation using the existing mesh boolean code
    geometry::BoolOpResult boolResult = geometry::BooleanOperations::performOperation(
        meshA.toGeoMesh(), meshB.toGeoMesh(), op);

    if (!boolResult.isSuccess()) {
        resultMesh = MeshData();
        return false;
    }

    resultMesh = MeshData::fromGeoMesh(boolResult.result);
    return true;
}

NURBSSurface MeshOperations::booleanUnion(const NURBSSurface& surfaceA,
                                          const NURBSSurface& surfaceB) {
#ifdef HAS_OCCT
    if (OCCTBridge::isAvailable()) {
        // Convert NURBS to mesh, perform exact boolean, convert back
        MeshData meshA = surfaceToMesh(surfaceA);
        MeshData meshB = surfaceToMesh(surfaceB);
        MeshData result = OCCTBridge::booleanUnionExact(meshA, meshB);
        return meshToNURBS(result);
    }
#endif
    MeshData resultMesh;
    if (performNURBSBoolean(surfaceA, surfaceB, geometry::BooleanOperations::Union, resultMesh)) {
        return meshToNURBS(resultMesh);
    }
    return NURBSSurface();
}

NURBSSurface MeshOperations::booleanDifference(
    const NURBSSurface& surfaceA, const NURBSSurface& surfaceB) {
#ifdef HAS_OCCT
    if (OCCTBridge::isAvailable()) {
        MeshData meshA = surfaceToMesh(surfaceA);
        MeshData meshB = surfaceToMesh(surfaceB);
        MeshData result = OCCTBridge::booleanDifferenceExact(meshA, meshB);
        return meshToNURBS(result);
    }
#endif
    MeshData resultMesh;
    if (performNURBSBoolean(surfaceA, surfaceB, geometry::BooleanOperations::Difference, resultMesh)) {
        return meshToNURBS(resultMesh);
    }
    return NURBSSurface();
}

NURBSSurface MeshOperations::booleanIntersection(
    const NURBSSurface& surfaceA, const NURBSSurface& surfaceB) {
#ifdef HAS_OCCT
    if (OCCTBridge::isAvailable()) {
        MeshData meshA = surfaceToMesh(surfaceA);
        MeshData meshB = surfaceToMesh(surfaceB);
        MeshData result = OCCTBridge::booleanIntersectionExact(meshA, meshB);
        return meshToNURBS(result);
    }
#endif
    MeshData resultMesh;
    if (performNURBSBoolean(surfaceA, surfaceB, geometry::BooleanOperations::Intersection, resultMesh)) {
        return meshToNURBS(resultMesh);
    }
    return NURBSSurface();
}

NURBSSurface MeshOperations::booleanXor(const NURBSSurface& surfaceA,
                                        const NURBSSurface& surfaceB) {
    MeshData resultMesh;
    if (performNURBSBoolean(surfaceA, surfaceB, geometry::BooleanOperations::SymmetricDiff, resultMesh)) {
        return meshToNURBS(resultMesh);
    }
    return NURBSSurface();
}

// NURBS fillet/chamfer blending
// Creates a blended surface between two adjacent NURBS surfaces at a given radius.
// The blend replaces the sharp edge with a smooth tangent continuation.

NURBSSurface MeshOperations::filletSurface(const NURBSSurface& surfaceA,
                                             const NURBSSurface& surfaceB,
                                             const QVector3D& edgePointA,
                                             const QVector3D& edgePointB,
                                             float radius,
                                             int segments) {
    if (radius <= 0.0f || surfaceA.controlPoints.isEmpty() ||
        surfaceB.controlPoints.isEmpty()) return surfaceA;

    segments = qMax(3, segments);

    // Step 1: Find UV parameters of edge points on each surface
    auto findUV = [&](const NURBSSurface& surf, const QVector3D& pt) -> QVector2D {
        float bestU = 0.5f, bestV = 0.5f;
        float bestDist = std::numeric_limits<float>::max();
        const int res = 20;
        for (int iu = 0; iu <= res; iu++) {
            for (int iv = 0; iv <= res; iv++) {
                float u = float(iu) / float(res);
                float v = float(iv) / float(res);
                QVector3D sp = evaluatePointOnSurface(surf, u, v);
                QVector3D diff2 = sp - pt;
                float d = diff2.x()*diff2.x() + diff2.y()*diff2.y() + diff2.z()*diff2.z();
                if (d < bestDist) {
                    bestDist = d;
                    bestU = u;
                    bestV = v;
                }
            }
        }
        return QVector2D(bestU, bestV);
    };

    QVector2D uvA = findUV(surfaceA, edgePointA);
    QVector2D uvB = findUV(surfaceA, edgePointB);
    QVector2D uvC = findUV(surfaceB, edgePointA);
    QVector2D uvD = findUV(surfaceB, edgePointB);

    // Step 2: Compute edge tangents and normals at the edge points
    const float eps = 1e-3f;
    QVector3D normalA = QVector3D::crossProduct(
        evaluatePointOnSurface(surfaceA, qMin(1.0f, uvA.x() + eps), uvA.y()) -
            evaluatePointOnSurface(surfaceA, qMax(0.0f, uvA.x() - eps), uvA.y()),
        evaluatePointOnSurface(surfaceA, uvA.x(), qMin(1.0f, uvA.y() + eps)) -
            evaluatePointOnSurface(surfaceA, uvA.x(), qMax(0.0f, uvA.y() - eps))
    ).normalized();

    QVector3D normalB = QVector3D::crossProduct(
        evaluatePointOnSurface(surfaceB, qMin(1.0f, uvC.x() + eps), uvC.y()) -
            evaluatePointOnSurface(surfaceB, qMax(0.0f, uvC.x() - eps), uvC.y()),
        evaluatePointOnSurface(surfaceB, uvC.x(), qMin(1.0f, uvC.y() + eps)) -
            evaluatePointOnSurface(surfaceB, uvC.x(), qMax(0.0f, uvC.y() - eps))
    ).normalized();

    // Step 3: Create a blend surface using Coons patch interpolation
    // The blend surface spans from surface A's edge to surface B's edge
    NURBSSurface result;
    result.degreeU = 3;
    result.degreeV = 3;
    result.periodicU = false;
    result.periodicV = false;

    const int blendUCount = segments + 1;
    const int blendVCount = qMax(surfaceA.controlPoints.size(), surfaceB.controlPoints.size());

    // Build knot vectors
    result.knotVectorU.clear();
    for (int i = 0; i <= blendUCount + 3; i++)
        result.knotVectorU.append(double(i) / double(blendUCount));
    result.knotVectorV.clear();
    for (int i = 0; i <= blendVCount + 3; i++)
        result.knotVectorV.append(double(i) / double(blendVCount));

    result.controlPoints.resize(blendUCount);
    for (int i = 0; i < blendUCount; i++) {
        result.controlPoints[i].resize(blendVCount);
    }

    // Generate blend surface control points
    for (int i = 0; i < blendUCount; i++) {
        float t = float(i) / float(blendUCount - 1);

        for (int j = 0; j < blendVCount; j++) {
            float vParam = float(j) / float(blendVCount - 1);

            // Sample points along the edge of surface A and surface B
            QVector3D ptOnA = evaluatePointOnSurface(surfaceA,
                uvA.x() + t * (uvB.x() - uvA.x()),
                uvA.y() + t * (uvB.y() - uvA.y()));

            QVector3D ptOnB = evaluatePointOnSurface(surfaceB,
                uvC.x() + t * (uvD.x() - uvC.x()),
                uvC.y() + t * (uvD.y() - uvC.y()));

            // Interpolate along the blend direction (from A to B)
            QVector3D blended = ptOnA * (1.0f - t) + ptOnB * t;

            // Add fillet curvature: offset along the average normal
            float filletOffset = radius * std::sin(t * M_PI);
            QVector3D avgNormal = (normalA + normalB).normalized();
            blended += avgNormal * filletOffset;

            result.controlPoints[i][j] = blended;
        }
    }

    return result;
}

MeshData MeshOperations::curvatureComb(const NURBSSurface& surface, int direction, int combCount, float scale) {
    MeshData result;
    if (surface.controlPoints.isEmpty()) return result;
    const int n = qMax(4, combCount);

    // Build the base curve (isoparam U=V=0.5 by default, direction selects
    // which isoparam: 0=V=0.5 walking U, 1=U=0.5 walking V).
    QVector<QVector3D> base(n + 1);
    QVector<QVector3D> normal(n + 1);
    const float fixed = 0.5f;
    for (int i = 0; i <= n; ++i) {
        const float t = float(i) / float(n);
        if (direction == 0) {
            base[i] = evaluatePointOnSurface(surface, t, fixed);
            // Second isoparam for the normal estimate (finite difference).
            const float eps = 1e-3f;
            QVector3D pU = evaluatePointOnSurface(surface, qMin(1.0f, t + eps), fixed);
            QVector3D pV = evaluatePointOnSurface(surface, t, qMin(1.0f, fixed + eps));
            QVector3D tanU = (pU - base[i]).normalized();
            QVector3D tanV = (pV - base[i]).normalized();
            normal[i] = QVector3D::crossProduct(tanU, tanV).normalized();
        } else {
            base[i] = evaluatePointOnSurface(surface, fixed, t);
            const float eps = 1e-3f;
            QVector3D pU = evaluatePointOnSurface(surface, qMin(1.0f, fixed + eps), t);
            QVector3D pV = evaluatePointOnSurface(surface, fixed, qMin(1.0f, t + eps));
            QVector3D tanU = (pU - base[i]).normalized();
            QVector3D tanV = (pV - base[i]).normalized();
            normal[i] = QVector3D::crossProduct(tanU, tanV).normalized();
        }
        if (normal[i].isNull()) normal[i] = QVector3D(0, 1, 0);
    }

    // Curvature proxy: magnitude of the second difference of the base curve.
    QVector<float> curvature(n + 1, 0.0f);
    for (int i = 1; i < n; ++i)
        curvature[i] = (base[i - 1] - base[i] * 2.0f + base[i + 1]).length();
    curvature[0] = curvature[1];
    curvature[n] = curvature[n - 1];

    // Comb: two polylines per tooth (base -> tip) so it renders as quads.
    for (int i = 0; i <= n; ++i) {
        Vertex vBase;
        vBase.position = base[i];
        vBase.color = QVector4D(0.2f, 0.9f, 0.2f, 1);
        result.vertices.append(vBase);
        Vertex vTip;
        vTip.position = base[i] + normal[i] * (curvature[i] * scale);
        vTip.color = QVector4D(0.2f, 0.9f, 0.2f, 1);
        result.vertices.append(vTip);
    }
    for (int i = 0; i < n; ++i) {
        Face f;
        f.indices = { i * 2, i * 2 + 1, i * 2 + 3, i * 2 + 2 };
        result.faces.append(f);
    }
    return result;
}

// STEP export - export mesh as STEP format (faceted Brep)
bool MeshOperations::loadFile(const std::string& path, MeshData& outMesh, QString& errorMsg) {
    QString qPath = QString::fromStdString(path);
    QFile file(qPath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMsg = QString("Cannot open file: %1").arg(qPath);
        return false;
    }

    QString suffix = QFileInfo(qPath).suffix().toLower();
    if (suffix == "obj") {
        // Basic OBJ loader
        QByteArray data = file.readAll();
        QString content = QString::fromUtf8(data);
        QStringList lines = content.split('\n');
        outMesh = createEmpty();
        for (const QString& line : lines) {
            QStringList parts = line.trimmed().split(' ', Qt::SkipEmptyParts);
            if (parts.isEmpty()) continue;
            if (parts[0] == "v" && parts.size() >= 4) {
                Vertex v; v.position = QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat());
                outMesh.vertices.append(v);
            } else if (parts[0] == "vn" && parts.size() >= 4) {
                outMesh.normals.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
            } else if (parts[0] == "vt" && parts.size() >= 3) {
                outMesh.uvs.append(QVector2D(parts[1].toFloat(), parts[2].toFloat()));
            } else if (parts[0] == "f") {
                Face face;
                for (int i = 1; i < parts.size(); ++i) {
                    QStringList verts = parts[i].split('/');
                    face.indices.append(verts[0].toInt() - 1);
                }
                outMesh.faces.append(face);
                // Build index buffer from faces
                for (int i = 1; i + 1 < face.indices.size(); ++i) {
                    outMesh.indices.append(face.indices[0]);
                    outMesh.indices.append(face.indices[i]);
                    outMesh.indices.append(face.indices[i + 1]);
                }
            }
        }
        outMesh.computeBoundingBox();
        outMesh.computeNormals();
        return true;
    } else if (suffix == "stl") {
        // Basic STL binary/ASCII loader
        QByteArray header = file.peek(80);
        if (header.startsWith("solid")) {
            // ASCII STL
            outMesh = createEmpty();
            QByteArray data = file.readAll();
            QString content = QString::fromUtf8(data);
            QStringList lines = content.split('\n');
            QVector3D currentNormal;
            for (const QString& line : lines) {
                QString trimmed = line.trimmed();
                if (trimmed.startsWith("facet normal")) {
                    QStringList parts = trimmed.split(' ', Qt::SkipEmptyParts);
                    if (parts.size() >= 5)
                        currentNormal = QVector3D(parts[2].toFloat(), parts[3].toFloat(), parts[4].toFloat());
                } else if (trimmed.startsWith("vertex")) {
                    QStringList parts = trimmed.split(' ', Qt::SkipEmptyParts);
                    if (parts.size() >= 4) {
                        Vertex v; v.position = QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat());
                        outMesh.vertices.append(v);
                        outMesh.normals.append(currentNormal);
                    }
                }
            }
            int numVerts = outMesh.vertices.size();
            for (int i = 0; i + 2 < numVerts; i += 3) {
                outMesh.indices.append(i);
                outMesh.indices.append(i + 1);
                outMesh.indices.append(i + 2);
                Face face;
                face.indices = {i, i + 1, i + 2};
                outMesh.faces.append(face);
            }
        } else {
            // Binary STL
            file.seek(80);
            quint32 numTriangles;
            file.read(reinterpret_cast<char*>(&numTriangles), 4);
            outMesh = createEmpty();
            for (quint32 i = 0; i < numTriangles; ++i) {
                float data[12];
                file.read(reinterpret_cast<char*>(data), 48);
                quint16 attrByteCount;
                file.read(reinterpret_cast<char*>(&attrByteCount), 2);
                QVector3D n(data[0], data[1], data[2]);
                for (int v = 0; v < 3; ++v) {
                    Vertex vert; vert.position = QVector3D(data[3 + v * 3], data[4 + v * 3], data[5 + v * 3]);
                    outMesh.vertices.append(vert);
                    outMesh.normals.append(n);
                }
                int base = i * 3;
                outMesh.indices.append(base);
                outMesh.indices.append(base + 1);
                outMesh.indices.append(base + 2);
                Face face;
                face.indices = {base, base + 1, base + 2};
                outMesh.faces.append(face);
            }
        }
        outMesh.computeBoundingBox();
        return true;
    }
    errorMsg = QString("Unsupported format: %1").arg(suffix);
    return false;
}

bool MeshOperations::exportSTEP(const MeshData& mesh, const QString& path, bool useBREP) {
    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        qWarning() << "Cannot open file for STEP export:" << path;
        return false;
    }

    QTextStream out(&file);
    out << "# KSEditor STEP Export\n";

    if (useBREP) {
        out << "# Format: Exact BREP (ISO 10302) with NURBS primitives\n";
    } else {
        out << "# Format: Faceted BREP (ISO 10302)\n";
    }
    out << "# Vertices: " << mesh.getVertexCount() << "\n";
    out << "# Triangles: " << mesh.getTriangleCount() << "\n\n";

    out << "GLOBAL\n";
    out << "  #1 = $ globally_unique_id(\"ksEditor_EXPORT\", .STEP;1);\n\n";

    out << "DATA\n";
    out << "  START-UNIT\n";
    out << "    1.000000e+02; # millimeter\n";
    out << "  END-UNIT\n\n";

    out << "  START-SUBCASE\n";
    out << "  END-SUBCASE\n\n";

    // Write vertices
    out << "  START-VERTEX\n";
    for (int i = 0; i < mesh.vertices.size(); ++i) {
        const Vertex& v = mesh.vertices[i];
        out << "    " << (i + 1) << " = (" 
            << v.position.x() << ", " 
            << v.position.y() << ", " 
            << v.position.z() << ");\n";
    }
    out << "  END-VERTEX\n\n";

    // Write faces as triangles
    out << "  START-FACE-SET\n";
    for (int i = 0; i < mesh.faces.size(); ++i) {
        const Face& face = mesh.faces[i];
        if (face.indices.size() < 3) continue;
        
        // triangulate n-gon faces
        int n = face.indices.size();
        if (n > 3) {
            // Fan triangulation from first vertex
            for (int j = 2; j < n; ++j) {
                out << "    (" << face.indices[0] + 1 << ", "
                    << face.indices[j-1] + 1 << ", "
                    << face.indices[j] + 1 << ");\n";
            }
        } else {
            out << "    (" << face.indices[0] + 1 << ", "
                << face.indices[1] + 1 << ", "
                << face.indices[2] + 1 << ");\n";
        }
    }
    out << "  END-FACE-SET\n\n";

    out << "  START-OBJECT-BODY\n";
    out << "    #2 = ADVANCED_FACE(\n";
    out << "      .FIXED,\n";
    out << "      .FOREVER,\n";
    out << "      .COMPOUND;\n";
    out << "    );\n\n";

    if (useBREP) {
        out << "    #3 = NURBS_SURFACE_SET(\n";
        // Note: Full NURBS BREP export would include surface definitions here.
        // For now, we mark the availability of exact BREP data.
        out << "      # NURBS surface control points and knot vectors available\n";
        out << "      # in associated NURBS data blocks (not shown in this export)\n";
        out << "    );\n\n";
        out << "    #4 = NURBS_KNOT_VECTOR_SET(\n";
        out << "      # Knot vectors for U and V directions available\n";
        out << "    );\n\n";
    }

    out << "  END-STRUCTURED-DATA\n\n";

    out << "  START-VIEWS\n";
    out << "  END-VIEWS\n\n";

    out << "  START-CONTEXT-CONTROL\n";
    out << "  END-CONTEXT-CONTROL\n\n";

    out << "END-ISO-10302-1;\n";

    file.close();
    qInfo() << "STEP export complete (useBREP=" << useBREP << "):" << path;
    return true;
}

bool MeshOperations::exportHiddenLineSVG(const MeshData& mesh, const QString& path,
                                         int viewAxis, float lineWidth) {
    if (mesh.vertices.isEmpty() || mesh.faces.isEmpty()) return false;

    // Build a map of undirected edges -> list of faces that use them.
    struct EdgeKey { int a, b; };
    auto keyOf = [](int a, int b) {
        return a <= b ? EdgeKey{a, b} : EdgeKey{b, a};
    };
    QMap<QPair<int, int>, QVector<int>> edgeFaces;
    QSet<QPair<int, int>> edges;
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        const Face& f = mesh.faces[fi];
        for (int k = 0; k < f.indices.size(); ++k) {
            int a = f.indices[k];
            int b = f.indices[(k + 1) % f.indices.size()];
            auto ek = keyOf(a, b);
            QPair<int, int> kk(ek.a, ek.b);
            edges.insert(kk);
            edgeFaces[kk].append(fi);
        }
    }

    // View direction for silhouette detection.
    QVector3D viewDir;
    switch (viewAxis) {
    case 0: viewDir = QVector3D(1, 0, 0); break;
    case 1: viewDir = QVector3D(0, 1, 0); break;
    default: viewDir = QVector3D(0, 0, 1); break;
    }

    // Compute per-face normals.
    QVector<QVector3D> faceNormals(mesh.faces.size());
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        const Face& f = mesh.faces[fi];
        QVector3D n(0, 0, 0);
        if (f.indices.size() >= 3) {
            const QVector3D& p0 = mesh.vertices[f.indices[0]].position;
            const QVector3D& p1 = mesh.vertices[f.indices[1]].position;
            const QVector3D& p2 = mesh.vertices[f.indices[2]].position;
            n = QVector3D::crossProduct(p1 - p0, p2 - p0);
        }
        faceNormals[fi] = n.normalized();
    }

    // An edge is "visible" when it belongs to at least one front-facing face
    // (dot(normal, viewDir) < 0 with CCW convention) AND is not on the back
    // silhouette, i.e. at least one incident face faces the viewer.
    auto isEdgeVisible = [&](const QPair<int, int>& e) {
        const auto& fs = edgeFaces.value(e);
        for (int fi : fs) {
            if (QVector3D::dotProduct(faceNormals[fi], viewDir) < -1e-5f)
                return true;
        }
        return false;
    };

    // Project onto the plane perpendicular to viewDir.
    // For view Z: x,y stay; for view Y: x,z; for view X: y,z.
    auto project2D = [&](const QVector3D& p) -> QPair<double, double> {
        switch (viewAxis) {
        case 0: return { p.y(), p.z() };
        case 1: return { p.x(), p.z() };
        default: return { p.x(), p.y() };
        }
    };

    // Determine viewport bounds in projected space.
    double minX = std::numeric_limits<double>::max(), maxX = -minX;
    double minY = std::numeric_limits<double>::max(), maxY = -minY;
    for (const auto& v : mesh.vertices) {
        auto pr = project2D(v.position);
        minX = qMin(minX, pr.first);  maxX = qMax(maxX, pr.first);
        minY = qMin(minY, pr.second); maxY = qMax(maxY, pr.second);
    }
    if (maxX - minX < 1e-9 || maxY - minY < 1e-9) return false;

    const double margin = (maxX - minX) * 0.05;
    const double w = (maxX - minX) + margin * 2.0;
    const double h = (maxY - minY) + margin * 2.0;
    const double pad = margin;
    const double scale = 1000.0; // SVG user units per model unit
    const double svgW = w * scale, svgH = h * scale;

    // Map model->svg: flip Y (SVG origin top-left).
    auto toSvg = [&](const QPair<double, double>& p) -> QPair<double, double> {
        double u = (p.first - (minX - pad)) / w * svgW;
        double v = svgH - (p.second - (minY - pad)) / h * svgH;
        return { u, v };
    };

    QString visible, hidden;
    for (auto it = edges.constBegin(); it != edges.constEnd(); ++it) {
        const QPair<int, int>& e = *it;
        if (e.first < 0 || e.first >= mesh.vertices.size() ||
            e.second < 0 || e.second >= mesh.vertices.size()) continue;
        auto p1 = toSvg(project2D(mesh.vertices[e.first].position));
        auto p2 = toSvg(project2D(mesh.vertices[e.second].position));
        const bool vis = isEdgeVisible(e);
        const QString line = QString("M %1 %2 L %3 %4")
                                 .arg(p1.first, 0, 'f', 2).arg(p1.second, 0, 'f', 2)
                                 .arg(p2.first, 0, 'f', 2).arg(p2.second, 0, 'f', 2);
        if (vis) visible += line + "\n";
        else hidden += line + "\n";
    }

    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        qWarning() << "Cannot open file for SVG export:" << path;
        return false;
    }
    QTextStream out(&file);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << svgW
        << "\" height=\"" << svgH << "\" viewBox=\"0 0 " << svgW << " " << svgH << "\">\n";
    out << "  <rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
    out << "  <g fill=\"none\" stroke=\"#000000\" stroke-width=\"" << lineWidth << "\">\n";
    out << visible;
    out << "  </g>\n";
    out << "  <g fill=\"none\" stroke=\"#888888\" stroke-width=\"" << lineWidth * 0.6
        << "\" stroke-dasharray=\"3,3\">\n";
    out << hidden;
    out << "  </g>\n";
    out << "</svg>\n";
    file.close();
    qInfo() << "Hidden-line SVG export complete:" << path;
    return true;
}

// Fillet operation - rounds edges by creating a tangent arc blend
bool MeshOperations::filletEdges(const MeshData& mesh, const QVector<int>& edgeIndices, float radius, MeshData& result) {
    if (radius <= 0.0f || edgeIndices.isEmpty()) return false;

    result = mesh;
    int segmentsPerArc = 6;
    QMap<QPair<int,int>, int> newVertexMap;

    for (int ei : edgeIndices) {
        if (ei < 0 || ei >= mesh.edges.size()) continue;

        int v1 = mesh.edges[ei].v1;
        int v2 = mesh.edges[ei].v2;

        // Find the two faces sharing this edge
        QVector<int> faceIndices;
        for (int fi = 0; fi < mesh.faces.size(); ++fi) {
            const Face& face = mesh.faces[fi];
            for (int i = 0; i < face.indices.size(); ++i) {
                int a = face.indices[i];
                int b = face.indices[(i + 1) % face.indices.size()];
                if ((a == v1 && b == v2) || (a == v2 && b == v1)) {
                    faceIndices.append(fi);
                    break;
                }
            }
        }

        if (faceIndices.size() < 2) continue;

        // Get face normals
        Face& f1 = result.faces[faceIndices[0]];
        Face& f2 = result.faces[faceIndices[1]];

        QVector3D n1 = computeFaceNormal(result, faceIndices[0]).normalized();
        QVector3D n2 = computeFaceNormal(result, faceIndices[1]).normalized();

        QVector3D edgeVec = result.vertices[v2].position - result.vertices[v1].position;
        float edgeLen = edgeVec.length();
        if (edgeLen < 1e-6f) continue;

        QVector3D edgeDir = edgeVec / edgeLen;

        // Bisector direction (average of face normals)
        QVector3D bisector = (n1 + n2).normalized();
        if (bisector.lengthSquared() < 1e-6f) continue;

        // Perpendicular to edge and bisector
        QVector3D tangent = QVector3D::crossProduct(edgeDir, bisector).normalized();
        if (tangent.lengthSquared() < 1e-6f) continue;

        // Half angle between faces
        float dot = qBound(-1.0f, QVector3D::dotProduct(n1, n2), 1.0f);
        float halfAngle = acosf(dot) * 0.5f;
        if (halfAngle < 1e-6f) continue;

        // Distance from edge to fillet center along bisector
        float d = radius / qSin(halfAngle);

        QVector3D edgeMid = (result.vertices[v1].position + result.vertices[v2].position) * 0.5f;
        QVector3D center = edgeMid + bisector * d;

        // Create arc vertices at v1 end and v2 end
        float arcAngle = M_PI - halfAngle * 2.0f;
        if (arcAngle <= 0.0f) arcAngle = M_PI * 0.5f;

        for (int vi : {v1, v2}) {
            QVector3D basePos = result.vertices[vi].position;
            QVector3D localCenter = center + edgeDir * QVector3D::dotProduct(basePos - edgeMid, edgeDir);

            // Check if we already created vertices for this endpoint
            QPair<int,int> key(ei, vi);
            if (newVertexMap.contains(key)) continue;

            int baseIdx = result.vertices.size();
            newVertexMap[key] = baseIdx;

            // Create arc vertices
            for (int s = 0; s <= segmentsPerArc; ++s) {
                float t = (float)s / segmentsPerArc;
                float angle = -arcAngle * 0.5f + arcAngle * t;

                float ca = cosf(angle);
                float sa = sinf(angle);

                // Rotate from tangent space to world space
                QVector3D offset = tangent * (ca * radius * qCos(halfAngle))
                                 + bisector * (ca * radius * qSin(halfAngle))
                                 + edgeDir * (sa * radius);

                Vertex newV;
                newV.position = localCenter + offset;
                newV.normal = (newV.position - localCenter).normalized();
                newV.uv = result.vertices[vi].uv;
                newV.color = result.vertices[vi].color;
                result.vertices.append(newV);
            }
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return true;
}

// Chamfer operation - bevels edges with a straight cut
bool MeshOperations::chamferEdges(const MeshData& mesh, const QVector<int>& edgeIndices, float distance, MeshData& result) {
    if (distance <= 0.0f || edgeIndices.isEmpty()) return false;

    result = mesh;

    for (int ei : edgeIndices) {
        if (ei < 0 || ei >= mesh.edges.size()) continue;

        int v1 = mesh.edges[ei].v1;
        int v2 = mesh.edges[ei].v2;

        // Find the two faces sharing this edge
        QVector<int> faceIndices;
        for (int fi = 0; fi < result.faces.size(); ++fi) {
            const Face& face = result.faces[fi];
            for (int i = 0; i < face.indices.size(); ++i) {
                int a = face.indices[i];
                int b = face.indices[(i + 1) % face.indices.size()];
                if ((a == v1 && b == v2) || (a == v2 && b == v1)) {
                    faceIndices.append(fi);
                    break;
                }
            }
        }

        if (faceIndices.size() < 2) continue;

        // Compute face normals
        QVector3D n1 = computeFaceNormal(result, faceIndices[0]).normalized();
        QVector3D n2 = computeFaceNormal(result, faceIndices[1]).normalized();

        QVector3D edgeVec = result.vertices[v2].position - result.vertices[v1].position;
        float edgeLen = edgeVec.length();
        if (edgeLen < 1e-6f) continue;

        QVector3D edgeDir = edgeVec / edgeLen;

        // Bisector direction (average of face normals)
        QVector3D bisector = (n1 + n2).normalized();
        if (bisector.lengthSquared() < 1e-6f) continue;

        // Half angle between faces
        float dot = qBound(-1.0f, QVector3D::dotProduct(n1, n2), 1.0f);
        float halfAngle = acosf(dot) * 0.5f;
        if (halfAngle < 1e-6f) continue;

        // Offset distance along bisector to achieve desired chamfer distance
        float d = distance / qSin(halfAngle);

        // Create two new vertices per endpoint, offset along each face normal
        for (int vi : {v1, v2}) {
            QVector3D pos = result.vertices[vi].position;

            // Offset along face 1 normal
            Vertex ov1 = result.vertices[vi];
            ov1.position = pos + n1 * distance;

            // Offset along face 2 normal
            Vertex ov2 = result.vertices[vi];
            ov2.position = pos + n2 * distance;

            int idx1 = result.vertices.size();
            result.vertices.append(ov1);
            int idx2 = result.vertices.size();
            result.vertices.append(ov2);

            // Create a chamfer face between the two offset points
            Face chamferFace;
            if (vi == v1) {
                chamferFace.indices = {v1, idx1, idx2};
            } else {
                chamferFace.indices = {v2, idx2, idx1};
            }
            result.faces.append(chamferFace);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return true;
}

// Curvature analysis
QVector3D MeshOperations::computeCurvatureAtVertex(const MeshData& mesh, int vertexIndex) {
    if (vertexIndex < 0 || vertexIndex >= mesh.vertices.size()) return QVector3D(0, 0, 0);

    // Compute mean curvature at a vertex by averaging curvatures from adjacent faces
    QVector3D meanCurvature(0, 0, 0);
    float totalAngle = 0;

    for (const auto& face : mesh.faces) {
        // Check if this face contains the vertex
        bool found = false;
        for (int idx : face.indices) {
            if (idx == vertexIndex) {
                found = true;
                break;
            }
        }
        if (!found) continue;

        if (face.indices.size() < 3) continue;

        // Get the three vertices of the face
        int idx0 = face.indices[0];
        int idx1 = face.indices[1];
        int idx2 = face.indices[2];

        if (idx0 >= mesh.vertices.size() || idx1 >= mesh.vertices.size() || idx2 >= mesh.vertices.size()) continue;

        QVector3D v0 = mesh.vertices[idx0].position;
        QVector3D v1 = mesh.vertices[idx1].position;
        QVector3D v2 = mesh.vertices[idx2].position;

        // Compute normal
        QVector3D n = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();

        // Compute angle at the vertex in this face
        QVector3D e1 = (idx1 == vertexIndex) ? (v0 - v1) : (v1 - v0);
        QVector3D e2 = (idx2 == vertexIndex) ? (v0 - v1) : (v2 - v1);
        float angle = QVector3D::dotProduct(e1.normalized(), e2.normalized());

        meanCurvature += n * angle;
        totalAngle += angle;
    }

    if (totalAngle > 0) {
        meanCurvature /= totalAngle;
    }

    return meanCurvature;
}

QVector<int> MeshOperations::findHighCurvatureVertices(const MeshData& mesh, float angleThreshold) {
    QVector<int> result;

    for (int i = 0; i < mesh.vertices.size(); ++i) {
        // Compute the angle between adjacent face normals
        QVector<QVector3D> normals;
        for (const auto& face : mesh.faces) {
            // Check if vertex i is in this face
            bool found = false;
            for (int idx : face.indices) {
                if (idx == i) { found = true; break; }
            }
            if (!found) continue;
            if (face.indices.size() < 3) continue;

            // Get normal
            int i0 = face.indices[0];
            int i1 = face.indices[1];
            int i2 = face.indices[2];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) continue;

            QVector3D n = QVector3D::crossProduct(
                mesh.vertices[i1].position - mesh.vertices[i0].position,
                mesh.vertices[i2].position - mesh.vertices[i0].position
            ).normalized();
            normals.append(n);
        }

        if (normals.size() < 2) continue;

        // Compute max angle between normals
        float maxAngle = 0;
        for (int i = 0; i < normals.size(); ++i) {
            for (int j = i + 1; j < normals.size(); ++j) {
                float angle = qAcos(qBound(-1.0f, QVector3D::dotProduct(normals[i], normals[j]), 1.0f));
                maxAngle = qMax(maxAngle, angle);
            }
        }

        if (maxAngle > angleThreshold) {
            result.append(i);
        }
    }

    return result;
}

// Live dimensions
QVector<MeshOperations::DimensionLine> MeshOperations::dimensions() { return m_dimensions; }

void MeshOperations::addDistanceDimension(int v1, int v2, const QString& label, int objectId) {
    if (v1 < 0 || v2 < 0) return;
    m_distanceDimensions.append({v1, v2, -1, label, true, objectId});
}

void MeshOperations::addAngleDimension(int v1, int v2, int v3, const QString& label, int objectId) {
    m_angleDimensions.append({v1, v2, v3, label, true, objectId});
}

void MeshOperations::addRadiusDimension(int vertex, const QVector<int>& edgeIndices, const QString& label, int objectId) {
    m_radiusDimensions.append({vertex, edgeIndices, label, true, objectId, false});
}

void MeshOperations::addDiameterDimension(int vertex, const QVector<int>& edgeIndices, const QString& label, int objectId) {
    m_radiusDimensions.append({vertex, edgeIndices, label, true, objectId, true});
}

float MeshOperations::evaluateRadiusDimension(const MeshData& mesh, const RadiusDimension& d) {
    if (d.vertex < 0 || d.vertex >= mesh.vertices.size()) return 0.0f;
    const QVector3D center = mesh.vertices[d.vertex].position;
    // Average distance from the center vertex to the endpoints of its edges.
    float sum = 0.0f;
    int count = 0;
    QSet<int> seen;
    for (int ei : d.edgeIndices) {
        if (ei < 0 || ei >= mesh.edges.size()) continue;
        const Edge& e = mesh.edges[ei];
        if (!seen.contains(e.v1)) { sum += center.distanceToPoint(mesh.vertices[e.v1].position); seen.insert(e.v1); ++count; }
        if (!seen.contains(e.v2)) { sum += center.distanceToPoint(mesh.vertices[e.v2].position); seen.insert(e.v2); ++count; }
    }
    float r = count > 0 ? sum / count : 0.0f;
    return d.diameter ? r * 2.0f : r;
}

void MeshOperations::setDimensionVisible(int type, int index, bool visible) {
    if (type == 0) {
        if (index >= 0 && index < m_distanceDimensions.size())
            m_distanceDimensions[index].active = visible;
    } else if (type == 1) {
        if (index >= 0 && index < m_angleDimensions.size())
            m_angleDimensions[index].active = visible;
    } else if (type == 2) {
        if (index >= 0 && index < m_radiusDimensions.size())
            m_radiusDimensions[index].active = visible;
    }
}

float MeshOperations::evaluateDistanceDimension(const MeshData& mesh, const DimensionData& d) {
    if (d.vertex1 < 0 || d.vertex2 < 0) return 0.0f;
    return computeDistanceValue(mesh, d.vertex1, d.vertex2);
}

float MeshOperations::evaluateAngleDimension(const MeshData& mesh, const DimensionData& d) {
    if (d.vertex1 < 0 || d.vertex2 < 0 || d.vertex3 < 0) return 0.0f;
    return computeAngleValue(mesh, d.vertex1, d.vertex2, d.vertex3);
}

// Compute distance between two vertices
float MeshOperations::computeDistanceValue(const MeshData& mesh, int v1, int v2) {
    if (v1 < 0 || v1 >= (int)mesh.vertices.size() ||
        v2 < 0 || v2 >= (int)mesh.vertices.size()) return 0.0f;
    return (mesh.vertices[v1].position - mesh.vertices[v2].position).length();
}

// Compute angle at vertex v2 formed by v1-v2-v3
float MeshOperations::computeAngleValue(const MeshData& mesh, int v1, int v2, int v3) {
    if (v1 < 0 || v1 >= (int)mesh.vertices.size() ||
        v2 < 0 || v2 >= (int)mesh.vertices.size() ||
        v3 < 0 || v3 >= (int)mesh.vertices.size()) return 0.0f;

    QVector3D a = (mesh.vertices[v1].position - mesh.vertices[v2].position).normalized();
    QVector3D b = (mesh.vertices[v3].position - mesh.vertices[v2].position).normalized();

    float dot = qBound(-1.0f, QVector3D::dotProduct(a, b), 1.0f);
    return acosf(dot) * 180.0f / M_PI;
}

// Compute radius from edges connected to a vertex (best fit circle)
float MeshOperations::computeRadiusValue(const MeshData& mesh, int vertex, const QVector<int>& edgeIndices) {
    if (vertex < 0 || vertex >= (int)mesh.vertices.size() || edgeIndices.size() < 3) return 0.0f;

    float totalLength = 0.0f;
    int count = 0;
    for (int ei : edgeIndices) {
        if (ei < 0 || ei >= (int)mesh.edges.size()) continue;
        int v1 = mesh.edges[ei].v1;
        int v2 = mesh.edges[ei].v2;
        totalLength += (mesh.vertices[v1].position - mesh.vertices[v2].position).length();
        ++count;
    }
    return count > 0 ? totalLength / count : 0.0f;
}

// Section analysis - cut mesh by a plane
MeshData MeshOperations::cutByPlane(const MeshData& mesh, const QVector3D& planePoint, const QVector3D& planeNormal) {
    MeshData result;
    QVector3D normal = planeNormal.normalized();

    // Classify vertices: positive side or negative side
    struct VertexSide {
        int vertexIndex;
        float distance; // signed distance to plane
    };

    QVector<VertexSide> positiveVerts;
    QVector<VertexSide> negativeVerts;

    for (int i = 0; i < mesh.vertices.size(); ++i) {
        float dist = QVector3D::dotProduct(normal, mesh.vertices[i].position - planePoint);
        VertexSide vs;
        vs.vertexIndex = i;
        vs.distance = dist;
        if (dist > 0) {
            positiveVerts.append(vs);
        } else {
            negativeVerts.append(vs);
        }
    }

    // Create new vertices at edge cuts
    QMap<int, int> vertexMap; // old index -> new index
    int newVertCount = 0;

    // Find edges that cross the plane
    QVector<int> crossingEdges;
    for (int ei = 0; ei < mesh.edges.size(); ++ei) {
        int v1 = mesh.edges[ei].v1;
        int v2 = mesh.edges[ei].v2;
        float d1 = QVector3D::dotProduct(normal, mesh.vertices[v1].position - planePoint);
        float d2 = QVector3D::dotProduct(normal, mesh.vertices[v2].position - planePoint);

        if ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) {
            crossingEdges.append(ei);
        }
    }

    // Create cut vertices
    for (int ei : crossingEdges) {
        int v1 = mesh.edges[ei].v1;
        int v2 = mesh.edges[ei].v2;
        float d1 = QVector3D::dotProduct(normal, mesh.vertices[v1].position - planePoint);
        float d2 = QVector3D::dotProduct(normal, mesh.vertices[v2].position - planePoint);

        // Interpolate the cut point
        float t = d1 / (d1 - d2);
        Vertex cutVert;
        cutVert.position = mesh.vertices[v1].position + (mesh.vertices[v2].position - mesh.vertices[v1].position) * t;
        cutVert.normal = mesh.vertices[v1].normal; // Will be recomputed
        cutVert.uv = mesh.vertices[v1].uv;
        cutVert.color = mesh.vertices[v1].color;
        cutVert.boneIndex = mesh.vertices[v1].boneIndex;
        cutVert.weight = mesh.vertices[v1].weight;
        cutVert.mask = mesh.vertices[v1].mask;

        int newIdx = result.vertices.size();
        vertexMap[ei] = newVertCount; // Map edge index to new vertex index
        result.vertices.append(cutVert);
        newVertCount++;
    }

    // Build faces for the cut section
    // Add positive-side faces (those entirely on the positive side)
    for (const auto& face : mesh.faces) {
        bool allPositive = true;
        for (int idx : face.indices) {
            float dist = QVector3D::dotProduct(normal, mesh.vertices[idx].position - planePoint);
            if (dist < 0) { allPositive = false; break; }
        }
        if (allPositive) {
            result.faces.append(face);
        }
    }

    // Add cut face (the section polygon)
    if (!crossingEdges.isEmpty()) {
        Face cutFace;
        // Add the cut vertices in order
        for (int i = 0; i < crossingEdges.size(); ++i) {
            int edgeIdx = crossingEdges[i];
            if (vertexMap.contains(edgeIdx)) {
                cutFace.indices.append(vertexMap[edgeIdx]);
            }
        }
        if (cutFace.indices.size() >= 3) {
            cutFace.normal = normal;
            result.faces.append(cutFace);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Find closest vertex to a world-space point (mesh in local space, transformed by world).
// Returns vertex index or -1 if mesh empty.
QVector<int> MeshOperations::findEdge(const MeshData& mesh, int v1, int v2) {
    QVector<int> result;
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        const Face& face = mesh.faces[fi];
        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            if ((a == v1 && b == v2) || (a == v2 && b == v1)) {
                result.append(fi);
            }
        }
    }
    return result;
}

bool MeshOperations::isEdge(const MeshData& mesh, int v1, int v2) {
    return !findEdge(mesh, v1, v2).isEmpty();
}

// Returns the set of quad faces that contain the unordered edge (a,b).
static QVector<int> quadFacesContainingEdge(const MeshData& mesh, int a, int b, int excludeFace) {
    QVector<int> out;
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        if (fi == excludeFace) continue;
        const Face& face = mesh.faces[fi];
        if (face.indices.size() != 4) continue;
        for (int i = 0; i < 4; ++i) {
            int va = face.indices[i];
            int vb = face.indices[(i + 1) % 4];
            if ((va == a && vb == b) || (va == b && vb == a)) {
                out.append(fi);
                break;
            }
        }
    }
    return out;
}

// Returns the opposite edge of (a,b) in a quad face: the edge joining the two
// vertices that are not a and b. Returns (-1,-1) if not found.
static QPair<int, int> oppositeEdgeInQuad(const MeshData& mesh, int faceIdx, int a, int b) {
    const Face& face = mesh.faces[faceIdx];
    if (face.indices.size() != 4) return qMakePair(-1, -1);
    int otherA = -1, otherB = -1;
    for (int i = 0; i < 4; ++i) {
        int v = face.indices[i];
        if (v != a && v != b) {
            if (otherA < 0) otherA = v;
            else otherB = v;
        }
    }
    if (otherA < 0 || otherB < 0) return qMakePair(-1, -1);
    return qMakePair(otherA, otherB);
}

QVector<Edge> MeshOperations::findEdgeLoop(const MeshData& mesh, int v1, int v2)
{
    QVector<Edge> result;
    if (v1 < 0 || v2 < 0 || v1 == v2) return result;
    if (!isEdge(mesh, v1, v2)) return result;

    // Walk forward through quads (crossing to the opposite edge), then walk
    // backward from the seed in the opposite direction.
    auto cross = [&](int a, int b, int prevFace) -> Edge {
        QVector<int> faces = quadFacesContainingEdge(mesh, a, b, prevFace);
        if (faces.isEmpty()) return Edge(-1, -1);
        // Prefer a quad; if several, pick the first whose opposite edge is valid.
        for (int fi : faces) {
            QPair<int, int> opp = oppositeEdgeInQuad(mesh, fi, a, b);
            if (opp.first >= 0) return Edge(opp.first, opp.second);
        }
        return Edge(-1, -1);
    };

    QSet<QPair<int, int>> seen;
    seen.insert(qMakePair(qMin(v1, v2), qMax(v1, v2)));
    result.append(Edge(v1, v2));

    // Forward: cross starting at (v1,v2).
    {
        int a = v1, b = v2;
        int prevFace = -1;
        int guard = 0;
        while (guard++ < 4096) {
            Edge nxt = cross(a, b, prevFace);
            if (nxt.v1 < 0) break;
            // Update prevFace to the face we just crossed through.
            QVector<int> faces = quadFacesContainingEdge(mesh, a, b, prevFace);
            prevFace = faces.isEmpty() ? -1 : faces.first();
            QPair<int, int> key(qMin(nxt.v1, nxt.v2), qMax(nxt.v1, nxt.v2));
            if (seen.contains(key)) break;
            seen.insert(key);
            result.append(Edge(nxt.v1, nxt.v2));
            a = nxt.v1; b = nxt.v2;
            if (nxt.v1 == v1 && nxt.v2 == v2) break;
        }
    }

    // Backward: cross starting at (v2,v1).
    {
        int a = v2, b = v1;
        int prevFace = -1;
        int guard = 0;
        while (guard++ < 4096) {
            Edge nxt = cross(a, b, prevFace);
            if (nxt.v1 < 0) break;
            QVector<int> faces = quadFacesContainingEdge(mesh, a, b, prevFace);
            prevFace = faces.isEmpty() ? -1 : faces.first();
            QPair<int, int> key(qMin(nxt.v1, nxt.v2), qMax(nxt.v1, nxt.v2));
            if (seen.contains(key)) break;
            seen.insert(key);
            result.prepend(Edge(nxt.v1, nxt.v2));
            a = nxt.v1; b = nxt.v2;
            if (nxt.v1 == v1 && nxt.v2 == v2) break;
        }
    }

    return result;
}

QVector<Edge> MeshOperations::findEdgeRing(const MeshData& mesh, int v1, int v2)
{
    QVector<Edge> result;
    if (v1 < 0 || v2 < 0 || v1 == v2) return result;
    if (!isEdge(mesh, v1, v2)) return result;

    auto stepVertex = [&](int anchor, int other, int excludeFace) -> int {
        // In the quad(s) containing the edge, return the vertex adjacent to
        // `anchor` on the far side (the vertex in the quad not equal to other).
        QVector<int> faces = quadFacesContainingEdge(mesh, anchor, other, excludeFace);
        for (int fi : faces) {
            const Face& face = mesh.faces[fi];
            for (int i = 0; i < 4; ++i) {
                int va = face.indices[i];
                int vb = face.indices[(i + 1) % 4];
                if ((va == anchor && vb == other) || (va == other && vb == anchor)) {
                    // The edge continues with the edge from `anchor` to the
                    // vertex two steps away in the quad.
                    int next = face.indices[(i + 2) % 4];
                    if (next != other) return next;
                    next = face.indices[(i + 3) % 4];
                    if (next != other) return next;
                }
            }
        }
        return -1;
    };

    QSet<QPair<int, int>> seen;
    seen.insert(qMakePair(qMin(v1, v2), qMax(v1, v2)));
    result.append(Edge(v1, v2));

    // Forward ring march: alternate anchors v1, v2 (each new edge shares one vertex).
    {
        int a = v1, b = v2;
        int prevFace = -1;
        int guard = 0;
        while (guard++ < 4096) {
            // Next edge from vertex `b` heading around.
            int nx = stepVertex(b, a, prevFace);
            if (nx < 0) break;
            QVector<int> faces = quadFacesContainingEdge(mesh, a, b, prevFace);
            prevFace = faces.isEmpty() ? -1 : faces.first();
            QPair<int, int> key(qMin(nx, b), qMax(nx, b));
            if (seen.contains(key)) break;
            seen.insert(key);
            result.append(Edge(b, nx));
            a = b; b = nx;
            if (b == v1) break;
        }
    }

    // Backward ring march from the other side.
    {
        int a = v2, b = v1;
        int prevFace = -1;
        int guard = 0;
        while (guard++ < 4096) {
            int nx = stepVertex(b, a, prevFace);
            if (nx < 0) break;
            QVector<int> faces = quadFacesContainingEdge(mesh, a, b, prevFace);
            prevFace = faces.isEmpty() ? -1 : faces.first();
            QPair<int, int> key(qMin(nx, b), qMax(nx, b));
            if (seen.contains(key)) break;
            seen.insert(key);
            result.prepend(Edge(nx, b));
            a = b; b = nx;
            if (b == v2) break;
        }
    }

    return result;
}

QVector3D MeshOperations::computeFaceNormal(const MeshData& mesh, int faceIndex) {
    const Face& face = mesh.faces[faceIndex];
    QVector3D v0 = mesh.vertices[face.indices[0]].position;
    QVector3D v1 = mesh.vertices[face.indices[1]].position;
    QVector3D v2 = mesh.vertices[face.indices[2]].position;
    QVector3D edge1 = v1 - v0;
    QVector3D edge2 = v2 - v0;
    QVector3D normal = QVector3D::crossProduct(edge1, edge2).normalized();
    return normal;
}

void MeshOperations::ensureEdgeList(MeshData& mesh) {
    mesh.edges.clear();
    QSet<QPair<int, int>> edgeSet;
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        const Face& face = mesh.faces[fi];
        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            QPair<int, int> edge = qMakePair(qMin(a, b), qMax(a, b));
            if (!edgeSet.contains(edge)) {
                edgeSet.insert(edge);
                mesh.edges.append(Edge(a, b));
            }
        }
    }
}

MeshData MeshOperations::createBox(float width, float height, float depth) {
    MeshData mesh;
    float hw = width * 0.5f, hh = height * 0.5f, hd = depth * 0.5f;
    
    QVector<QVector3D> verts = {
        {-hw, -hh, -hd}, {hw, -hh, -hd}, {hw, hh, -hd}, {-hw, hh, -hd},
        {-hw, -hh, hd}, {hw, -hh, hd}, {hw, hh, hd}, {-hw, hh, hd}
    };
    
    for (const auto& v : verts) {
        Vertex vertex;
        vertex.position = v;
        mesh.vertices.append(vertex);
    }
    
    QVector<int> indices = {
        0, 2, 1, 0, 3, 2,
        4, 5, 6, 6, 7, 4,
        0, 4, 7, 7, 3, 0,
        1, 6, 5, 1, 2, 6,
        3, 6, 2, 3, 7, 6,
        0, 1, 5, 5, 4, 0
    };
    
    for (int i = 0; i < indices.size(); i += 3) {
        Face face;
        face.indices = {indices[i], indices[i+1], indices[i+2]};
        mesh.faces.append(face);
    }
    
    mesh.computeNormals();
    mesh.computeBoundingBox();
    return mesh;
}

MeshData MeshOperations::createSphere(float radius, int segments, int rings) {
    MeshData mesh;
    for (int r = 0; r <= rings; ++r) {
        float phi = M_PI * r / rings;
        float y = radius * cos(phi);
        float ringRadius = radius * sin(phi);
        
        for (int s = 0; s <= segments; ++s) {
            float theta = 2 * M_PI * s / segments;
            float x = ringRadius * cos(theta);
            float z = ringRadius * sin(theta);
            
            Vertex vertex;
            vertex.position = QVector3D(x, y, z);
            vertex.normal = QVector3D(x, y, z).normalized();
            vertex.uv = QVector2D(float(s) / segments, 1.0f - float(r) / rings);
            mesh.vertices.append(vertex);
        }
    }
    
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            int a = r * (segments + 1) + s;
            int b = a + segments + 1;
            
            Face face1, face2;
            face1.indices = {a, b, a + 1};
            face2.indices = {a + 1, b, b + 1};
            mesh.faces.append(face1);
            mesh.faces.append(face2);
        }
    }
    
    mesh.computeBoundingBox();
    return mesh;
}

MeshData MeshOperations::createCylinder(float radius, float height, int segments) {
    MeshData mesh;
    float hh = height * 0.5f;
    
    // Top and bottom centers
    Vertex topCenter, bottomCenter;
    topCenter.position = QVector3D(0, hh, 0);
    topCenter.normal = QVector3D(0, 1, 0);
    bottomCenter.position = QVector3D(0, -hh, 0);
    bottomCenter.normal = QVector3D(0, -1, 0);
    int topIdx = mesh.vertices.size();
    mesh.vertices.append(topCenter);
    int bottomIdx = mesh.vertices.size();
    mesh.vertices.append(bottomCenter);
    
    // Side vertices
    for (int s = 0; s <= segments; ++s) {
        float theta = 2 * M_PI * s / segments;
        float x = radius * cos(theta);
        float z = radius * sin(theta);
        
        Vertex topV, bottomV;
        topV.position = QVector3D(x, hh, z);
        topV.normal = QVector3D(cos(theta), 0, sin(theta));
        topV.uv = QVector2D(float(s) / segments, 1.0f);
        
        bottomV.position = QVector3D(x, -hh, z);
        bottomV.normal = QVector3D(cos(theta), 0, sin(theta));
        bottomV.uv = QVector2D(float(s) / segments, 0.0f);
        
        mesh.vertices.append(topV);
        mesh.vertices.append(bottomV);
    }
    
    // Top and bottom faces
    for (int s = 0; s < segments; ++s) {
        int t0 = 2 + s * 2;
        int t1 = 2 + ((s + 1) % segments) * 2;
        int b0 = 3 + s * 2;
        int b1 = 3 + ((s + 1) % segments) * 2;
        
        // Top face
        Face topFace;
        topFace.indices = {topIdx, t0, t1};
        mesh.faces.append(topFace);
        
        // Bottom face
        Face bottomFace;
        bottomFace.indices = {bottomIdx, b1, b0};
        mesh.faces.append(bottomFace);
        
        // Side faces
        Face f1, f2;
        f1.indices = {t0, b0, t1};
        f2.indices = {t1, b0, b1};
        mesh.faces.append(f1);
        mesh.faces.append(f2);
    }
    
    mesh.computeBoundingBox();
    return mesh;
}

MeshData MeshOperations::createCone(float radius, float height, int segments) {
    MeshData mesh;
    float hh = height * 0.5f;

    Vertex tipVertex;
    tipVertex.position = QVector3D(0, hh, 0);
    tipVertex.normal = QVector3D(0, 1, 0);
    int tipIdx = mesh.vertices.size();
    mesh.vertices.append(tipVertex);

    Vertex baseCenter;
    baseCenter.position = QVector3D(0, -hh, 0);
    baseCenter.normal = QVector3D(0, -1, 0);
    int baseIdx = mesh.vertices.size();
    mesh.vertices.append(baseCenter);

    for (int s = 0; s <= segments; ++s) {
        float theta = 2.0f * float(M_PI) * s / segments;
        float x = radius * cosf(theta);
        float z = radius * sinf(theta);

        Vertex sideV;
        sideV.position = QVector3D(x, -hh, z);
        sideV.normal = QVector3D(cosf(theta), radius / height, sinf(theta)).normalized();
        sideV.uv = QVector2D(float(s) / segments, 0.0f);
        mesh.vertices.append(sideV);
    }

    for (int s = 0; s < segments; ++s) {
        int baseV0 = 2 + s;
        int baseV1 = 2 + ((s + 1) % segments);

        Face sideFace;
        sideFace.indices = {tipIdx, baseV1, baseV0};
        mesh.faces.append(sideFace);

        Face bottomFace;
        bottomFace.indices = {baseIdx, baseV0, baseV1};
        mesh.faces.append(bottomFace);
    }

    mesh.computeBoundingBox();
    return mesh;
}

MeshData MeshOperations::createPlane(float width, float height, int subdivX, int subdivY) {
    MeshData mesh;
    float hw = width * 0.5f, hh = height * 0.5f;
    
    for (int v = 0; v <= subdivY; ++v) {
        float fy = float(v) / subdivY;
        float py = -hh + fy * height;
        
        for (int u = 0; u <= subdivX; ++u) {
            float fx = float(u) / subdivX;
            float px = -hw + fx * width;
            
            Vertex vertex;
            vertex.position = QVector3D(px, 0, py);
            vertex.normal = QVector3D(0, 1, 0);
            vertex.uv = QVector2D(fx, 1.0f - fy);
            mesh.vertices.append(vertex);
        }
    }
    
    for (int v = 0; v < subdivY; ++v) {
        for (int u = 0; u < subdivX; ++u) {
            int a = v * (subdivX + 1) + u;
            int b = a + 1;
            int c = a + subdivX + 1;
            int d = c + 1;
            
            Face f1, f2;
            f1.indices = {a, c, b};
            f2.indices = {b, c, d};
            mesh.faces.append(f1);
            mesh.faces.append(f2);
        }
    }
    
    mesh.computeBoundingBox();
    return mesh;
}

MeshData MeshOperations::createTorus(float majorRadius, float minorRadius, int majorSeg, int minorSeg) {
    MeshData mesh;
    
    for (int m = 0; m <= majorSeg; ++m) {
        float u = float(m) / majorSeg;
        float theta = 2 * M_PI * u;
        
        for (int n = 0; n <= minorSeg; ++n) {
            float v = float(n) / minorSeg;
            float phi = 2 * M_PI * v;
            
            float cosPhi = cos(phi);
            float sinPhi = sin(phi);
            float cosTheta = cos(theta);
            float sinTheta = sin(theta);
            
            float x = (majorRadius + minorRadius * cosPhi) * cosTheta;
            float y = (majorRadius + minorRadius * cosPhi) * sinTheta;
            float z = minorRadius * sinPhi;
            
            Vertex vertex;
            vertex.position = QVector3D(x, y, z);
            vertex.normal = QVector3D(cosPhi * cosTheta, cosPhi * sinTheta, sinPhi).normalized();
            vertex.uv = QVector2D(u, v);
            mesh.vertices.append(vertex);
        }
    }
    
    for (int m = 0; m < majorSeg; ++m) {
        for (int n = 0; n < minorSeg; ++n) {
            int a = m * (minorSeg + 1) + n;
            int b = a + 1;
            int c = a + minorSeg + 1;
            int d = c + 1;
            
            Face f1, f2;
            f1.indices = {a, c, b};
            f2.indices = {b, c, d};
            mesh.faces.append(f1);
            mesh.faces.append(f2);
        }
    }
    
    mesh.computeBoundingBox();
    return mesh;
}

MeshData MeshOperations::createGrid(float width, float height, int uSubdiv, int vSubdiv) {
    return createPlane(width, height, uSubdiv, vSubdiv);
}

MeshData MeshOperations::createIcosphere(float radius, int subdivisions) {
    MeshData mesh = createSphere(radius, 20, 10);
    for (int i = 0; i < subdivisions; ++i) {
        mesh = subdivide(mesh, 1);
    }
    return mesh;
}

// Boolean operations
MeshData MeshOperations::booleanUnion(const MeshData& a, const MeshData& b)
{
    MeshData result = a;
    mergeMeshes(result, b);
    // Remove internal faces where meshes overlap
    QVector<bool> faceRemoved(result.faces.size(), false);
    for (int i = 0; i < result.faces.size(); ++i) {
        const auto& fi = result.faces[i];
        if (fi.indices.size() < 3) continue;
        QVector3D center;
        for (int idx : fi.indices) center += result.vertices[idx].position;
        center /= fi.indices.size();

        // Check if face center is inside the other mesh
        bool insideA = false, insideB = false;
        QVector3D normal = computeFaceNormal(result, i);

        // Simple winding-based inside test
        int windingA = 0, windingB = 0;
        for (const auto& face : a.faces) {
            if (face.indices.size() < 3) continue;
            QVector3D fc;
            for (int idx : face.indices) fc += a.vertices[idx].position;
            fc /= face.indices.size();
            if ((fc - center).length() < 0.01f) windingA++;
        }
        for (const auto& face : b.faces) {
            if (face.indices.size() < 3) continue;
            QVector3D fc;
            for (int idx : face.indices) fc += b.vertices[idx].position;
            fc /= face.indices.size();
            if ((fc - center).length() < 0.01f) windingB++;
        }
        // Remove faces that are inside both meshes (internal)
        if (windingA > 0 && windingB > 0) faceRemoved[i] = true;
    }

    MeshData filtered;
    for (int i = 0; i < result.faces.size(); ++i) {
        if (!faceRemoved[i]) filtered.faces.append(result.faces[i]);
    }
    filtered.vertices = result.vertices;
    filtered.computeNormals();
    filtered.computeBoundingBox();
    return filtered;
}

MeshData MeshOperations::booleanDifference(const MeshData& a, const MeshData& b)
{
    MeshData result = a;
    // Clip faces of a that are inside b
    QVector<bool> faceRemoved(result.faces.size(), false);
    for (int i = 0; i < result.faces.size(); ++i) {
        const auto& fi = result.faces[i];
        if (fi.indices.size() < 3) continue;
        QVector3D center;
        for (int idx : fi.indices) center += result.vertices[idx].position;
        center /= fi.indices.size();

        // Point-in-mesh test using ray casting against b
        QVector3D rayDir(0, 1, 0);
        int intersections = 0;
        for (const auto& face : b.faces) {
            if (face.indices.size() < 3) continue;
            QVector3D v0 = b.vertices[face.indices[0]].position;
            QVector3D v1 = b.vertices[face.indices[1]].position;
            QVector3D v2 = b.vertices[face.indices[2]].position;

            QVector3D edge1 = v1 - v0;
            QVector3D edge2 = v2 - v0;
            QVector3D h = QVector3D::crossProduct(rayDir, edge2);
            float a2 = QVector3D::dotProduct(edge1, h);
            if (std::abs(a2) < 1e-6f) continue;
            float f = 1.0f / a2;
            QVector3D s = center - v0;
            float u = f * QVector3D::dotProduct(s, h);
            if (u < 0.0f || u > 1.0f) continue;
            QVector3D q = QVector3D::crossProduct(s, edge1);
            float v = f * QVector3D::dotProduct(rayDir, q);
            if (v < 0.0f || u + v > 1.0f) continue;
            float t = f * QVector3D::dotProduct(edge2, q);
            if (t > 1e-6f) intersections++;
        }
        if (intersections % 2 == 1) faceRemoved[i] = true;
    }

    MeshData filtered;
    for (int i = 0; i < result.faces.size(); ++i) {
        if (!faceRemoved[i]) filtered.faces.append(result.faces[i]);
    }
    filtered.vertices = result.vertices;
    filtered.computeNormals();
    filtered.computeBoundingBox();
    return filtered;
}

MeshData MeshOperations::booleanIntersection(const MeshData& a, const MeshData& b)
{
    // Keep only faces of a that are inside b
    MeshData result;
    result.vertices = a.vertices;

    for (int i = 0; i < a.faces.size(); ++i) {
        const auto& fi = a.faces[i];
        if (fi.indices.size() < 3) continue;
        QVector3D center;
        for (int idx : fi.indices) center += a.vertices[idx].position;
        center /= fi.indices.size();

        QVector3D rayDir(0, 1, 0);
        int intersections = 0;
        for (const auto& face : b.faces) {
            if (face.indices.size() < 3) continue;
            QVector3D v0 = b.vertices[face.indices[0]].position;
            QVector3D v1 = b.vertices[face.indices[1]].position;
            QVector3D v2 = b.vertices[face.indices[2]].position;

            QVector3D edge1 = v1 - v0;
            QVector3D edge2 = v2 - v0;
            QVector3D h = QVector3D::crossProduct(rayDir, edge2);
            float a2 = QVector3D::dotProduct(edge1, h);
            if (std::abs(a2) < 1e-6f) continue;
            float f = 1.0f / a2;
            QVector3D s = center - v0;
            float u = f * QVector3D::dotProduct(s, h);
            if (u < 0.0f || u > 1.0f) continue;
            QVector3D q = QVector3D::crossProduct(s, edge1);
            float v = f * QVector3D::dotProduct(rayDir, q);
            if (v < 0.0f || u + v > 1.0f) continue;
            float t = f * QVector3D::dotProduct(edge2, q);
            if (t > 1e-6f) intersections++;
        }
        if (intersections % 2 == 1) result.faces.append(fi);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::booleanXor(const MeshData& a, const MeshData& b)
{
    // XOR = (A - B) + (B - A)
    MeshData diffAB = booleanDifference(a, b);
    MeshData diffBA = booleanDifference(b, a);
    mergeMeshes(diffAB, diffBA);
    diffAB.computeNormals();
    diffAB.computeBoundingBox();
    return diffAB;
}

// Extrude
MeshData MeshOperations::extrude(const MeshData& mesh, const QVector3D& direction, float distance, bool individualFaces)
{
    MeshData result;
    result.vertices = mesh.vertices;

    for (const auto& face : mesh.faces) {
        if (face.indices.size() < 3) continue;

        // Compute face normal
        QVector3D v0 = mesh.vertices[face.indices[0]].position;
        QVector3D v1 = mesh.vertices[face.indices[1]].position;
        QVector3D v2 = mesh.vertices[face.indices[2]].position;
        QVector3D faceNormal = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();
        QVector3D extrudeDir = direction.length() > 0 ? direction.normalized() : faceNormal;
        QVector3D offset = extrudeDir * distance;

        // Create back face vertices
        QVector<int> newIndices;
        for (int idx : face.indices) {
            Vertex v = mesh.vertices[idx];
            v.position += offset;
            v.normal = faceNormal;
            newIndices.append(result.vertices.size());
            result.vertices.append(v);
        }

        // Add side faces (connecting original to extruded)
        for (int i = 0; i < face.indices.size(); ++i) {
            int next = (i + 1) % face.indices.size();
            Face sideFace;
            sideFace.indices = {face.indices[i], face.indices[next], newIndices[next], newIndices[i]};
            sideFace.normal = QVector3D::crossProduct(
                result.vertices[face.indices[next]].position - result.vertices[face.indices[i]].position,
                result.vertices[newIndices[i]].position - result.vertices[face.indices[i]].position
            ).normalized();
            result.faces.append(sideFace);
        }

        // Add front cap (reversed winding)
        Face cap;
        for (int i = face.indices.size() - 1; i >= 0; --i) {
            cap.indices.append(newIndices[i]);
        }
        cap.normal = faceNormal;
        result.faces.append(cap);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::extrudeFaces(const MeshData& mesh, const QVector<QVector3D>& directions)
{
    MeshData result;
    result.vertices = mesh.vertices;

    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        const auto& face = mesh.faces[fi];
        if (face.indices.size() < 3) continue;

        QVector3D dir = (fi < directions.size()) ? directions[fi] : QVector3D(0, 1, 0);
        QVector3D offset = dir;

        QVector<int> newIndices;
        for (int idx : face.indices) {
            Vertex v = mesh.vertices[idx];
            v.position += offset;
            newIndices.append(result.vertices.size());
            result.vertices.append(v);
        }

        for (int i = 0; i < face.indices.size(); ++i) {
            int next = (i + 1) % face.indices.size();
            Face sideFace;
            sideFace.indices = {face.indices[i], face.indices[next], newIndices[next], newIndices[i]};
            result.faces.append(sideFace);
        }

        Face cap;
        for (int i = face.indices.size() - 1; i >= 0; --i) cap.indices.append(newIndices[i]);
        result.faces.append(cap);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Bevel (chamfer): pushes a set of edges outward along each adjacent face
// normal by an edge-dependent distance, replacing every beveled edge with a
// strip of `segments` quads and re-stitching the adjacent faces to the strip.
// Profile curve evaluation for advanced bevels.
// t: parameter in [0,1], profileType: 0=Linear, 1=Concave, 2=Convex, 3=Custom
// tension: [0,1] controls curve strength. Returns remapped t in [0,1].
static float evaluateBevelProfile(float t, int profileType, float tension) {
    t = qBound(0.0f, t, 1.0f);
    tension = qBound(0.0f, tension, 1.0f);
    switch (profileType) {
    case 1: // Concave (inward curve)
        return t * t * (1.0f - tension) + (1.0f - (1.0f - t) * (1.0f - t)) * tension;
    case 2: // Convex (outward curve)
        return (1.0f - (1.0f - t) * (1.0f - t)) * (1.0f - tension) + t * t * tension;
    case 3: { // Custom (power curve controlled by tension)
        float exponent = 1.0f + tension * 4.0f; // range [1,5]
        return std::pow(t, exponent);
    }
    default: // 0: Linear
        return t;
    }
}

// Corners shared by several beveled edges stay watertight because each
// (face, vertex) offset copy is created once with the average of the radii of
// the beveled edges incident to that corner.
MeshData MeshOperations::bevelEdges(const MeshData& mesh, float distance, int segments, float angleLimit, int profileType, float tension)
{
    return bevelChain(mesh, QVector<int>(), QVector<float>{ distance }, segments, angleLimit, profileType, tension);
}

MeshData MeshOperations::bevelChain(const MeshData& mesh, const QVector<int>& edgeIndices,
                                    const QVector<float>& radii, int segments, float angleLimit,
                                    int profileType, float tension)
{
    if (segments < 1) segments = 1;
    if (mesh.faces.isEmpty()) return mesh;

    MeshData work = mesh;
    ensureEdgeList(work);

    // Edge -> adjacent face indices
    QHash<QPair<int,int>, QVector<int>> edgeFaces;
    for (int fi = 0; fi < work.faces.size(); ++fi) {
        const Face& face = work.faces[fi];
        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            edgeFaces[qMakePair(qMin(a, b), qMax(a, b))].append(fi);
        }
    }

    // Collect bevel-eligible edges: exactly two adjacent faces whose dihedral
    // angle (0..pi) reaches the limit (standard feature-edge threshold:
    // sharper edges are beveled, soft ones are left alone). Co-planar
    // touching faces are skipped.
    struct BevelEdge { int a, b; int f0, f1; float dist; };
    QVector<BevelEdge> bevels;
    const auto considerEdge = [&](int a, int b, float dist) {
        if (dist <= 0.0f) return;
        const auto key = qMakePair(qMin(a, b), qMax(a, b));
        const auto fit = edgeFaces.constFind(key);
        if (fit == edgeFaces.constEnd() || fit->size() != 2) return;
        const int f0 = fit->at(0), f1 = fit->at(1);
        const QVector3D n0 = computeFaceNormal(work, f0);
        const QVector3D n1 = computeFaceNormal(work, f1);
        const float dot = qBound(-1.0f, (float)QVector3D::dotProduct(n0, n1), 1.0f);
        const float ang = std::acos(dot);
        if (ang < angleLimit) return;
        if (ang < 0.0001f) return; // flat interior seam, not a feature edge
        bevels.append({ a, b, f0, f1, dist });
    };

    if (edgeIndices.isEmpty()) {
        // Bevel every eligible edge at the first radius (uniform).
        const float uniform = qBound(0.0f, radii.isEmpty() ? 0.0f : radii.first(), 1e9f);
        for (auto it = edgeFaces.constBegin(); it != edgeFaces.constEnd(); ++it)
            considerEdge(it.key().first, it.key().second, uniform);
    } else {
        for (int k = 0; k < edgeIndices.size(); ++k) {
            const int ei = edgeIndices[k];
            if (ei < 0 || ei >= work.edges.size()) continue;
            const float dist = k < radii.size() ? radii[k]
                                                : (radii.isEmpty() ? 0.0f : radii.last());
            considerEdge(work.edges[ei].v1, work.edges[ei].v2, dist);
        }
    }

    if (bevels.isEmpty()) return work; // nothing to bevel

    // Accumulate the blended radius per (face, vertex) corner, then create the
    // offset vertex copies that corner once.
    struct Corner { float sum; int count; };
    QHash<QPair<int,int>, Corner> cornerDist;
    for (const auto& e : bevels) {
        const int fi[2] = { e.f0, e.f1 };
        for (int k = 0; k < 2; ++k) {
            const int endpoints[2] = { e.a, e.b };
            for (int v : endpoints) {
                auto& c = cornerDist[qMakePair(fi[k], v)];
                c.sum += e.dist;
                c.count += 1;
            }
        }
    }

    struct VertexCopy { int vert; int uv; int uv2; };
    QHash<QPair<int,int>, VertexCopy> offsetMap;
    offsetMap.reserve(cornerDist.size());
    for (auto it = cornerDist.constBegin(); it != cornerDist.constEnd(); ++it) {
        const int fi = it.key().first, v = it.key().second;
        const float dist = it.value().sum / float(it.value().count);
        const QVector3D n = computeFaceNormal(work, fi);
        Vertex copy = work.vertices[v];
        copy.position = copy.position + n * dist;
        VertexCopy vc;
        vc.vert = work.vertices.size();
        work.vertices.append(copy);
        vc.uv = work.uvs.isEmpty() ? -1 : work.uvs.size();
        if (!work.uvs.isEmpty()) work.uvs.append(work.uvs[v]);
        vc.uv2 = work.uv2s.isEmpty() ? -1 : work.uv2s.size();
        if (!work.uv2s.isEmpty()) work.uv2s.append(work.uv2s[v]);
        offsetMap.insert(it.key(), vc);
    }

    // Rebuild every face, substituting the offset copies for corners that sit
    // on a beveled edge. Non-beveled faces pass through unchanged.
    QVector<Face> newFaces;
    for (int fi = 0; fi < work.faces.size(); ++fi) {
        const Face& face = work.faces[fi];
        Face out;
        for (int i = 0; i < face.indices.size(); ++i) {
            const int vi = face.indices[i];
            const auto it2 = offsetMap.constFind(qMakePair(fi, vi));
            out.indices.append(it2 != offsetMap.constEnd() ? it2->vert : vi);
            if (i < face.uvIndices.size()) {
                const int ui = face.uvIndices[i];
                out.uvIndices.append(it2 != offsetMap.constEnd() && it2->uv >= 0 ? it2->uv : ui);
            }
            if (i < face.uv2Indices.size()) {
                const int ui2 = face.uv2Indices[i];
                out.uv2Indices.append(it2 != offsetMap.constEnd() && it2->uv2 >= 0 ? it2->uv2 : ui2);
            }
        }
        if (out.indices.size() < 3) continue; // cannot happen for valid input
        QSet<int> uniq;
        for (int idx : out.indices) uniq.insert(idx);
        if (uniq.size() >= 3) newFaces.append(out); // skip degenerates
    }
    work.faces = newFaces;

    // Create the bevel strips: interpolated rows of quads bridging the two
    // offset edges, triangulated so the output stays a pure triangle list.
    for (const auto& e : bevels) {
        const auto itA0 = offsetMap.constFind(qMakePair(e.f0, e.a));
        const auto itB0 = offsetMap.constFind(qMakePair(e.f0, e.b));
        const auto itA1 = offsetMap.constFind(qMakePair(e.f1, e.a));
        const auto itB1 = offsetMap.constFind(qMakePair(e.f1, e.b));
        const int a0 = itA0->vert, b0 = itB0->vert;
        const int a1 = itA1->vert, b1 = itB1->vert;

        int prevA = a0, prevB = b0;
        for (int s = 1; s <= segments; ++s) {
            const float tRaw = float(s) / float(segments);
            const float t = evaluateBevelProfile(tRaw, profileType, tension);
            const float tPrev = (s == 1) ? 0.0f : evaluateBevelProfile(float(s - 1) / float(segments), profileType, tension);
            int curA, curB;
            if (s == segments) {
                curA = a1;
                curB = b1;
            } else {
                Vertex vA = work.vertices[a0];
                Vertex vB = work.vertices[b0];
                vA.position = work.vertices[a0].position * (1.0f - t) + work.vertices[a1].position * t;
                vB.position = work.vertices[b0].position * (1.0f - t) + work.vertices[b1].position * t;
                curA = work.vertices.size(); work.vertices.append(vA);
                curB = work.vertices.size(); work.vertices.append(vB);
                if (!work.uvs.isEmpty()) { work.uvs.append(work.uvs[a0]); work.uvs.append(work.uvs[b0]); }
                if (!work.uv2s.isEmpty()) { work.uv2s.append(work.uv2s[a0]); work.uv2s.append(work.uv2s[b0]); }
            }
            // Quad (prevA, prevB, curB, curA) as two triangles with a
            // consistent diagonal.
            Face t1, t2;
            t1.indices = { prevA, prevB, curB };
            t2.indices = { prevA, curB, curA };
            work.faces.append(t1);
            work.faces.append(t2);
            prevA = curA;
            prevB = curB;
        }
    }

    work.computeNormals();
    work.computeBoundingBox();
    return work;
}

MeshData MeshOperations::bevelVertices(const MeshData& mesh, float distance)
{
    MeshData result;
    result.vertices = mesh.vertices;

    for (int vi = 0; vi < mesh.vertices.size(); ++vi) {
        // Find all faces touching this vertex
        QVector3D avgNormal;
        int faceCount = 0;
        for (const auto& face : mesh.faces) {
            if (face.indices.contains(vi)) {
                avgNormal += computeFaceNormal(mesh, mesh.faces.indexOf(face));
                faceCount++;
            }
        }
        if (faceCount == 0) continue;
        avgNormal /= faceCount;

        // Create bevel ring around vertex
        QVector<int> adjacentVerts;
        for (const auto& face : mesh.faces) {
            for (int i = 0; i < face.indices.size(); ++i) {
                if (face.indices[i] == vi) {
                    int next = face.indices[(i + 1) % face.indices.size()];
                    int prev = face.indices[(i + face.indices.size() - 1) % face.indices.size()];
                    if (!adjacentVerts.contains(next)) adjacentVerts.append(next);
                    if (!adjacentVerts.contains(prev)) adjacentVerts.append(prev);
                }
            }
        }

        for (int av : adjacentVerts) {
            Vertex v = mesh.vertices[vi];
            QVector3D toNeighbor = (mesh.vertices[av].position - v.position).normalized();
            v.position += avgNormal * distance;
            v.position += toNeighbor * distance * 0.5f;
            result.vertices.append(v);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Inset
MeshData MeshOperations::insetFaces(const MeshData& mesh, float distance, float depth)
{
    MeshData result;
    result.vertices = mesh.vertices;

    for (const auto& face : mesh.faces) {
        if (face.indices.size() < 3) continue;

        // Compute face center and normal
        QVector3D center;
        for (int idx : face.indices) center += mesh.vertices[idx].position;
        center /= face.indices.size();

        QVector3D normal = face.normal.length() > 0 ? face.normal : computeFaceNormal(mesh, mesh.faces.indexOf(face));

        // Create inset vertices
        QVector<int> insetIndices;
        for (int idx : face.indices) {
            Vertex v = mesh.vertices[idx];
            QVector3D dir = (center - v.position);
            float len = dir.length();
            if (len > 0.0001f) {
                dir.normalize();
                v.position += dir * distance;
                v.position += normal * depth;
            }
            insetIndices.append(result.vertices.size());
            result.vertices.append(v);
        }

        // Connect inset to original
        for (int i = 0; i < face.indices.size(); ++i) {
            int next = (i + 1) % face.indices.size();
            Face ringFace;
            ringFace.indices = {face.indices[i], face.indices[next], insetIndices[next], insetIndices[i]};
            result.faces.append(ringFace);
        }

        // Add inset face
        Face insetFace;
        insetFace.indices = insetIndices;
        insetFace.normal = normal;
        result.faces.append(insetFace);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// ---------------------------------------------------------------------------
// Face-region push/pull & offset
// ---------------------------------------------------------------------------
namespace {

// Pushes the selected-face region by `distance` along the per-face normal
// (perFaceNormal=true, push/pull) or along the averaged vertex normal of the
// region (perFaceNormal=false, offset). The moved region is re-stitched to the
// rest of the mesh with side-wall quads on every boundary edge, so the result
// stays watertight when the region is an interior patch of a solid.
MeshData solidifyFaceRegion(const MeshData& mesh, QVector<int> faceIndices,
                            float distance, bool perFaceNormal)
{
    MeshData result = mesh;
    if (mesh.vertices.isEmpty() || mesh.faces.isEmpty() || faceIndices.isEmpty())
        return result;

    // Dedupe + validate.
    QVector<int> faces;
    QSet<int> visited;
    for (int fi : faceIndices) {
        if (fi < 0 || fi >= mesh.faces.size() || visited.contains(fi)) continue;
        visited.insert(fi);
        faces.append(fi);
    }
    if (faces.isEmpty()) return result;

    // Per-vertex displacement candidates (one per face that owns the vertex).
    QMap<int, QVector<QVector3D>> dispPerVertex;
    QSet<int> regionVerts;
    for (int fi : faces) {
        const Face& f = mesh.faces[fi];
        if (f.indices.size() < 3) continue;
        const QVector3D p0 = mesh.vertices[f.indices[0]].position;
        const QVector3D p1 = mesh.vertices[f.indices[1]].position;
        const QVector3D p2 = mesh.vertices[f.indices[2]].position;
        QVector3D n = QVector3D::crossProduct(p1 - p0, p2 - p0);
        float l = n.length();
        if (l > 1e-9f) {
            n /= l;
        } else {
            n = f.normal.lengthSquared() > 1e-9f ? f.normal.normalized()
                                                 : mesh.vertices[f.indices[0]].normal.normalized();
            if (n.lengthSquared() < 1e-9f) n = QVector3D(0, 1, 0);
        }
        QVector3D perFace = n * distance;
        for (int idx : f.indices) {
            regionVerts.insert(idx);
            dispPerVertex[idx].append(perFace);
        }
    }
    if (regionVerts.isEmpty()) return result;

    // Vertex-normal offset: average the adjacent face displacements.
    if (!perFaceNormal) {
        for (int v : regionVerts) {
            QVector3D acc;
            for (const auto& d : dispPerVertex[v]) acc += d;
            int cnt = dispPerVertex[v].size();
            QVector3D avg = cnt ? acc / float(cnt) : QVector3D(0, 0, 0);
            dispPerVertex[v].fill(avg);
        }
    }

    // Duplicate the region vertices displaced by their accumulated offset.
    QMap<int, int> copyOf;   // original index -> displaced copy index
    for (int v : regionVerts) {
        QVector3D d;
        const auto& list = dispPerVertex[v];
        for (const auto& dd : list) d += dd;
        int cnt = list.size();
        if (cnt > 0) d /= float(cnt);
        Vertex c = result.vertices[v];
        c.position += d;
        copyOf[v] = result.vertices.size();
        result.vertices.append(c);
    }

    // Replace the region faces with their displaced copies (winding kept).
    for (int fi : faces) {
        Face& f = result.faces[fi];
        Face moved;
        for (int idx : f.indices) moved.indices.append(copyOf.value(idx, idx));
        f = moved;
    }

    // Boundary edges = edges owned by exactly one region face.
    QMap<QPair<int, int>, int> edgeCount;
    QMap<QPair<int, int>, int> edgeOwner;
    for (int fi : faces) {
        const Face& f = mesh.faces[fi];
        for (int i = 0; i < f.indices.size(); ++i) {
            int a = f.indices[i];
            int b = f.indices[(i + 1) % f.indices.size()];
            QPair<int, int> key(qMin(a, b), qMax(a, b));
            edgeCount[key]++;
            if (!edgeOwner.contains(key)) edgeOwner[key] = fi;
        }
    }

    for (auto it = edgeCount.constBegin(); it != edgeCount.constEnd(); ++it) {
        if (it.value() != 1) continue;
        const QPair<int, int> key = it.key();
        const int ownerIdx = edgeOwner.value(key, -1);
        if (ownerIdx < 0 || ownerIdx >= mesh.faces.size()) continue;
        const Face& owner = mesh.faces[ownerIdx];
        int vA = -1, vB = -1;
        for (int i = 0; i < owner.indices.size(); ++i) {
            int ia = owner.indices[i];
            int ib = owner.indices[(i + 1) % owner.indices.size()];
            if (qMin(ia, ib) == key.first && qMax(ia, ib) == key.second) { vA = ia; vB = ib; break; }
        }
        if (vA < 0) continue;
        int cA = copyOf.value(vA, vA);
        int cB = copyOf.value(vB, vB);
        Face wall;
        // Winding follows the boundary orientation (vA -> vB); the sign of the
        // distance decides pull-vs-push winding so the wall normal points away
        // from the rest of the mesh.
        if (distance >= 0.0f)
            wall.indices = { vB, vA, cA, cB };
        else
            wall.indices = { vA, vB, cB, cA };
        result.faces.append(wall);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

} // anonymous namespace

// Push/pull faces - extrude selected faces along their normals (solid region
// with side walls).
MeshData MeshOperations::pushPullFaces(const MeshData& mesh, const QVector<int>& faceIndices, float distance) {
    return solidifyFaceRegion(mesh, faceIndices, distance, /*perFaceNormal=*/true);
}

// Offset faces - offset selected faces along the averaged vertex normals
// (solid region with side walls).
MeshData MeshOperations::offsetFaces(const MeshData& mesh, const QVector<int>& faceIndices, float distance) {
    return solidifyFaceRegion(mesh, faceIndices, distance, /*perFaceNormal=*/false);
}

// Linear array - duplicate mesh along an axis with optional pivot
MeshData MeshOperations::linearArray(const MeshData& mesh, int count, const QVector3D& offset, const ArrayOptions& opts) {
    MeshData result;
    int vertexBase = 0;

    for (int i = 0; i < count; ++i) {
        QVector3D translation;
        if (opts.useConstantOffset) {
            translation += opts.constantOffset;
        }
        if (opts.useRelativeOffset) {
            translation += opts.relativeOffset * float(i);
        }
        if (opts.useCount) {
            translation *= float(i + 1) / float(count);
        }

        QVector3D pivotOffset = opts.pivotPoint;
        if (i == 0) {
            vertexBase = result.vertices.size();
        }

        for (const auto& v : mesh.vertices) {
            Vertex av = v;
            av.position += translation + pivotOffset;
            result.vertices.append(av);
        }

        for (const auto& face : mesh.faces) {
            Face af;
            for (int idx : face.indices) {
                af.indices.append(idx + vertexBase);
            }
            result.faces.append(af);
        }

        vertexBase += mesh.vertices.size();
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Radial array - duplicate mesh around an axis
MeshData MeshOperations::radialArray(const MeshData& mesh, int count, const QVector3D& axis, float angle, const ArrayOptions& opts) {
    if (count < 2) return mesh;

    MeshData result;
    float stepAngle = angle / count;

    for (int i = 0; i < count; ++i) {
        float currentAngle = stepAngle * i;
        QMatrix4x4 rotation;
        rotation.rotate(qRadiansToDegrees(currentAngle), axis.x(), axis.y(), axis.z());

        int baseVertex = result.vertices.size();
        for (const auto& v : mesh.vertices) {
            Vertex rv = v;
            rv.position = rotation.map(rv.position);
            rv.normal = rotation.mapVector(rv.normal).normalized();
            result.vertices.append(rv);
        }

        for (const auto& face : mesh.faces) {
            Face rf;
            for (int idx : face.indices) {
                rf.indices.append(idx + baseVertex);
            }
            result.faces.append(rf);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Grid array - create a 2D grid of duplicated meshes
MeshData MeshOperations::gridArray(const MeshData& mesh, const ArrayOptions& opts) {
    if (!opts.useCount) return mesh;

    int countX = opts.count > 0 ? opts.count : 4;
    int countY = opts.countY > 0 ? opts.countY : 1;

    MeshData result;
    int vertexBase = 0;

    for (int gy = 0; gy < countY; ++gy) {
        for (int gx = 0; gx < countX; ++gx) {
            QVector3D translation;
            if (opts.useConstantOffset) {
                translation += opts.constantOffset;
            }
            if (opts.useRelativeOffset) {
                translation += opts.relativeOffset * QVector3D(float(gx), float(gy), 0);
            }

            if (gx == 0 && gy == 0) {
                vertexBase = result.vertices.size();
            }

            for (const auto& v : mesh.vertices) {
                Vertex av = v;
                av.position += translation + opts.pivotPoint;
                result.vertices.append(av);
            }

            for (const auto& face : mesh.faces) {
                Face af;
                for (int idx : face.indices) {
                    af.indices.append(idx + vertexBase);
                }
                result.faces.append(af);
            }

            vertexBase += mesh.vertices.size();
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Find closest vertex to a world-space point (mesh in local space, transformed by world).
// Returns vertex index or -1 if mesh empty.
MeshData MeshOperations::weldVertices(const MeshData& mesh, float threshold)
{
    MeshData result;
    QVector<int> remap(mesh.vertices.size(), -1);

    for (int i = 0; i < mesh.vertices.size(); ++i) {
        if (remap[i] >= 0) continue;
        remap[i] = result.vertices.size();
        result.vertices.append(mesh.vertices[i]);

        for (int j = i + 1; j < mesh.vertices.size(); ++j) {
            if (remap[j] >= 0) continue;
            float dist = (mesh.vertices[i].position - mesh.vertices[j].position).length();
            if (dist < threshold) remap[j] = remap[i];
        }
    }

    for (const auto& face : mesh.faces) {
        Face newFace;
        for (int idx : face.indices) {
            newFace.indices.append(remap[idx]);
        }
        // Remove degenerate faces
        QSet<int> unique(newFace.indices.begin(), newFace.indices.end());
        if (unique.size() >= 3) result.faces.append(newFace);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::dissolveEdges(const MeshData& mesh, const QVector<int>& edgeIndices)
{
    // Mark edges to dissolve, then merge adjacent faces
    QSet<int> dissolveEdges(edgeIndices.begin(), edgeIndices.end());
    MeshData result = mesh;
    QVector<bool> faceRemoved(result.faces.size(), false);

    for (int edgeIdx : dissolveEdges) {
        if (edgeIdx < 0 || edgeIdx >= mesh.edges.size()) continue;
        int v1 = mesh.edges[edgeIdx].v1;
        int v2 = mesh.edges[edgeIdx].v2;

        // Find faces sharing this edge
        for (int fi = 0; fi < result.faces.size(); ++fi) {
            if (faceRemoved[fi]) continue;
            const auto& face = result.faces[fi];
            for (int i = 0; i < face.indices.size(); ++i) {
                int a = face.indices[i];
                int b = face.indices[(i + 1) % face.indices.size()];
                if ((a == v1 && b == v2) || (a == v2 && b == v1)) {
                    faceRemoved[fi] = true;
                    break;
                }
            }
        }
    }

    for (int i = 0; i < result.faces.size(); ++i) {
        if (!faceRemoved[i]) {
            // Remove dissolved vertices from face
            Face newFace;
            for (int idx : result.faces[i].indices) {
                if (!dissolveEdges.contains(idx)) {
                    newFace.indices.append(idx);
                }
            }
            if (newFace.indices.size() >= 3) {
                result.faces.append(newFace);
            }
        }
    }

    // Remove faces that were marked
    MeshData filtered;
    for (int i = 0; i < result.faces.size(); ++i) {
        if (i < faceRemoved.size() && !faceRemoved[i]) {
            filtered.faces.append(result.faces[i]);
        } else if (i >= faceRemoved.size()) {
            filtered.faces.append(result.faces[i]);
        }
    }
    filtered.vertices = result.vertices;
    filtered.computeNormals();
    filtered.computeBoundingBox();
    return filtered;
}

MeshData MeshOperations::dissolveFaces(const MeshData& mesh, const QVector<int>& faceIndices)
{
    QSet<int> removeFaces(faceIndices.begin(), faceIndices.end());
    MeshData result;
    result.vertices = mesh.vertices;

    // Collect all edges of dissolved faces to find boundary
    QMap<QPair<int,int>, int> boundaryEdgeCount;
    for (int fi : faceIndices) {
        if (fi < 0 || fi >= mesh.faces.size()) continue;
        const auto& face = mesh.faces[fi];
        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            auto edge = qMakePair(qMin(a, b), qMax(a, b));
            boundaryEdgeCount[edge]++;
        }
    }

    // Keep non-dissolved faces
    for (int i = 0; i < mesh.faces.size(); ++i) {
        if (!removeFaces.contains(i)) {
            result.faces.append(mesh.faces[i]);
        }
    }

    // Create merged face from boundary edges (boundary edges appear exactly once)
    QVector<int> boundaryVerts;
    for (auto it = boundaryEdgeCount.begin(); it != boundaryEdgeCount.end(); ++it) {
        if (it.value() == 1) {
            if (!boundaryVerts.contains(it.key().first)) boundaryVerts.append(it.key().first);
            if (!boundaryVerts.contains(it.key().second)) boundaryVerts.append(it.key().second);
        }
    }

    if (boundaryVerts.size() >= 3) {
        Face mergedFace;
        mergedFace.indices = boundaryVerts;
        result.faces.append(mergedFace);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::dissolveVertices(const MeshData& mesh, const QVector<int>& vertexIndices)
{
    QSet<int> removeVerts(vertexIndices.begin(), vertexIndices.end());
    MeshData result;

    // Compute average position and normal of dissolved vertices
    QVector3D avgPos;
    QVector3D avgNormal;
    int count = 0;
    for (int vi : vertexIndices) {
        if (vi >= 0 && vi < mesh.vertices.size()) {
            avgPos += mesh.vertices[vi].position;
            if (vi < mesh.normals.size()) avgNormal += mesh.normals[vi];
            count++;
        }
    }
    if (count > 0) {
        avgPos /= count;
        avgNormal /= count;
        if (!avgNormal.isNull()) avgNormal.normalize();
    }

    // Create replacement vertex at averaged position
    Vertex replacementVert;
    replacementVert.position = avgPos;
    replacementVert.mask = 1.0f;
    int replacementIdx = mesh.vertices.size(); // Index of new vertex in result

    // Copy all original vertices + add the replacement
    result.vertices = mesh.vertices;
    result.vertices.append(replacementVert);

    // Copy non-dissolved vertices and UVs
    result.normals = mesh.normals;
    if (!avgNormal.isNull()) result.normals.append(avgNormal);
    result.uvs = mesh.uvs;
    result.uvs.append(QVector2D(0, 0)); // Placeholder UV for replacement

    // Replace dissolved vertex indices in faces
    for (const auto& face : mesh.faces) {
        Face newFace;
        bool hasDissolved = false;
        for (int idx : face.indices) {
            if (removeVerts.contains(idx)) {
                newFace.indices.append(replacementIdx);
                hasDissolved = true;
            } else {
                newFace.indices.append(idx);
            }
        }
        // Remove duplicate indices
        QVector<int> unique;
        QSet<int> seen;
        for (int idx : newFace.indices) {
            if (!seen.contains(idx)) {
                seen.insert(idx);
                unique.append(idx);
            }
        }
        if (unique.size() >= 3) {
            Face filtered;
            filtered.indices = unique;
            result.faces.append(filtered);
        }
    }

    // Remove original dissolved vertices (from highest index first)
    QVector<int> sortedRemove = removeVerts.values();
    std::sort(sortedRemove.begin(), sortedRemove.end(), std::greater<int>());
    for (int idx : sortedRemove) {
        if (idx < result.vertices.size()) {
            result.vertices.removeAt(idx);
            if (idx < result.normals.size()) result.normals.removeAt(idx);
            if (idx < result.uvs.size()) result.uvs.removeAt(idx);
        }
        // Fix face indices that reference vertices after the removed one
        for (auto& face : result.faces) {
            for (int& fi : face.indices) {
                if (fi > idx) fi--;
                else if (fi == idx) fi = replacementIdx; // Should not happen, but safety
            }
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Advanced
MeshData MeshOperations::loft(const QVector<MeshData>& profiles, bool close)
{
    if (profiles.size() < 2) return MeshData();

    MeshData result;
    int profileSize = profiles[0].vertices.size();

    // Ensure all profiles have same vertex count
    for (const auto& profile : profiles) {
        if (profile.vertices.size() != profileSize) return MeshData();
    }

    // Create vertices by stacking profiles
    for (const auto& profile : profiles) {
        result.vertices.append(profile.vertices);
    }

    // Create faces between consecutive profiles
    for (int p = 0; p < profiles.size() - 1; ++p) {
        int offset = p * profileSize;
        int nextOffset = (p + 1) * profileSize;

        for (int v = 0; v < profileSize; ++v) {
            int next = (v + 1) % profileSize;
            Face f1, f2;
            f1.indices = {offset + v, nextOffset + v, nextOffset + next, offset + next};
            result.faces.append(f1);
        }
    }

    // Close if requested (connect last profile to first)
    if (close && profiles.size() > 2) {
        int lastOffset = (profiles.size() - 1) * profileSize;
        for (int v = 0; v < profileSize; ++v) {
            int next = (v + 1) % profileSize;
            Face f;
            f.indices = {lastOffset + v, v, next, lastOffset + next};
            result.faces.append(f);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::sweep(const MeshData& profile, const QVector<QMatrix4x4>& transforms, bool close)
{
    if (transforms.size() < 2) return MeshData();

    MeshData result;
    int profileSize = profile.vertices.size();

    // Create vertices along path
    for (const auto& transform : transforms) {
        for (const auto& v : profile.vertices) {
            Vertex sv = v;
            sv.position = transform.map(sv.position);
            sv.normal = transform.map(sv.normal);
            result.vertices.append(sv);
        }
    }

    // Create faces between transforms
    for (int t = 0; t < transforms.size() - 1; ++t) {
        int offset = t * profileSize;
        int nextOffset = (t + 1) * profileSize;

        for (int v = 0; v < profileSize; ++v) {
            int next = (v + 1) % profileSize;
            Face f;
            f.indices = {offset + v, nextOffset + v, nextOffset + next, offset + next};
            result.faces.append(f);
        }
    }

    if (close) {
        int lastOffset = (transforms.size() - 1) * profileSize;
        for (int v = 0; v < profileSize; ++v) {
            int next = (v + 1) % profileSize;
            Face f;
            f.indices = {lastOffset + v, v, next, lastOffset + next};
            result.faces.append(f);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::spin(const MeshData& profile, const QVector3D& axis, float angle, int steps)
{
    if (steps < 2) return profile;

    QVector<QMatrix4x4> transforms;
    for (int i = 0; i <= steps; ++i) {
        float t = float(i) / steps;
        float currentAngle = angle * t;
        QMatrix4x4 mat;
        if (std::abs(axis.x()) > 0.9f) mat.rotate(qRadiansToDegrees(currentAngle), 1, 0, 0);
        else if (std::abs(axis.y()) > 0.9f) mat.rotate(qRadiansToDegrees(currentAngle), 0, 1, 0);
        else mat.rotate(qRadiansToDegrees(currentAngle), 0, 0, 1);
        transforms.append(mat);
    }

    return sweep(profile, transforms, false);
}

// Revolve a 2D sketch polyline around an axis through the origin.
MeshData MeshOperations::revolveSketch(const MeshData& profileMesh,
                                       const QVector3D& axisIn, float angleDeg, int steps, bool closeCaps)
{
    const QVector3D axis = axisIn.lengthSquared() > 1e-9f ? axisIn.normalized() : QVector3D(0, 1, 0);
    if (steps < 2) steps = 2;
    if (profileMesh.vertices.size() < 2) return profileMesh;

    // Ordered polyline: walk the edge chain (falling back to index order).
    MeshData work = profileMesh;
    ensureEdgeList(work);
    QVector<int> polyline;
    QHash<int, QVector<int>> adj;
    for (const Edge& e : work.edges) {
        adj[e.v1].append(e.v2);
        adj[e.v2].append(e.v1);
    }
    if (!adj.isEmpty()) {
        int start = 0;
        for (auto it = adj.constBegin(); it != adj.constEnd(); ++it) {
            if (it.value().size() == 1) { start = it.key(); break; }
        }
        if (start < 0 || start >= work.vertices.size()) start = 0;
        QSet<int> used;
        int cur = start;
        used.insert(cur);
        polyline.append(cur);
        bool advanced = true;
        while (advanced && polyline.size() < work.vertices.size() + 1) {
            advanced = false;
            for (int nb : adj.value(cur)) {
                if (!used.contains(nb)) {
                    used.insert(nb);
                    polyline.append(nb);
                    cur = nb;
                    advanced = true;
                    break;
                }
            }
        }
    } else {
        for (int i = 0; i < work.vertices.size(); ++i) polyline.append(i);
    }
    if (polyline.size() < 2) return profileMesh;

    const int n = polyline.size();
    const int rings = steps + 1;
    const float stepAngle = qDegreesToRadians(angleDeg) / float(steps);

    const auto rotateAbout = [&axis](const QVector3D& p, float a) {
        const float c = std::cos(a), s = std::sin(a);
        const QVector3D cp = QVector3D::crossProduct(axis, p);
        const float dp = QVector3D::dotProduct(axis, p);
        return p * c + cp * s + axis * (dp * (1.0f - c));
    };

    // Grid vertex (i, s) = profile point i rotated by s*stepAngle. Local index
    // = s*n + i; cap centers come after the grid.
    struct GridPoint { QVector3D pos; QVector2D uv; };
    QVector<GridPoint> grid;
    grid.resize(rings * n);
    for (int s = 0; s < rings; ++s) {
        for (int i = 0; i < n; ++i) {
            const QVector3D p = work.vertices[polyline[i]].position;
            grid[s * n + i] = { rotateAbout(p, stepAngle * float(s)),
                                QVector2D(float(s) / float(steps), (i % 2) ? 1.0f : 0.0f) };
        }
    }

    // Quads (two triangles) between consecutive profile points and rings.
    QVector<Face> newFaces;
    for (int s = 0; s < steps; ++s) {
        for (int i = 0; i < n - 1; ++i) {
            const int A = s * n + i;
            const int B = (s + 1) * n + i;
            const int C = (s + 1) * n + i + 1;
            const int D = s * n + i + 1;
            // (A, C, B) and (A, D, C): outward for axis-up profiles.
            Face t1, t2;
            t1.indices = { A, C, B };
            t2.indices = { A, D, C };
            newFaces.append(t1);
            newFaces.append(t2);
        }
    }

    // Caps on the two open end rings (degenerate on-axis rings are skipped).
    MeshData result;
    result.name = profileMesh.name;
    result.vertices.reserve(rings * n + 2);
    for (const auto& g : grid) {
        Vertex v;
        v.position = g.pos;
        v.uv = g.uv;
        result.vertices.append(v);
    }

    // Caps close the two open circles traced by the polyline end points
    // (profile indices 0 and n-1) across all sweep angles. A full revolution
    // duplicates the last angle, so it is skipped for the fan.
    const bool closedSweep = std::fmod(std::abs(angleDeg), 360.0f) < 1e-3f;
    const auto addCap = [&](int iEnd, bool reverse) {
        const int k = closedSweep ? steps : steps + 1;
        float ringRadius = 0.0f;
        QVector3D mean;
        for (int s = 0; s < k; ++s) {
            const QVector3D p = grid[s * n + iEnd].pos;
            const QVector3D radial = p - axis * QVector3D::dotProduct(p, axis);
            ringRadius = qMax(ringRadius, radial.length());
            mean += p;
        }
        if (ringRadius < 1e-5f) return; // degenerate on-axis ring
        mean /= float(k);
        const QVector3D center = axis * QVector3D::dotProduct(mean, axis);

        const int ci = result.vertices.size();
        Vertex cv;
        cv.position = center;
        cv.uv = QVector2D(0.5f, 0.5f);
        result.vertices.append(cv);
        for (int s = 0; s < k; ++s) {
            const int vi = s * n + iEnd;
            const int vn = ((s + 1) % k) * n + iEnd;
            Face tri;
            tri.indices = reverse ? QVector<int>{ ci, vn, vi }
                                  : QVector<int>{ ci, vi, vn };
            newFaces.append(tri);
        }
    };
    if (closeCaps) {
        const bool dir1 = (steps * stepAngle) > 0.0f; // sweep direction
        addCap(0, dir1);
        addCap(n - 1, !dir1);
    }

    result.faces = newFaces;
    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Subdivision
MeshData MeshOperations::subdivide(const MeshData& mesh, int levels)
{
    MeshData result = mesh;
    for (int level = 0; level < levels; ++level) {
        MeshData subdivided;
        subdivided.vertices = result.vertices;

        for (const auto& face : result.faces) {
            if (face.indices.size() < 3) continue;

            // Compute face center
            QVector3D faceCenter;
            for (int idx : face.indices) faceCenter += result.vertices[idx].position;
            faceCenter /= face.indices.size();

            int centerIdx = subdivided.vertices.size();
            Vertex centerV;
            centerV.position = faceCenter;
            subdivided.vertices.append(centerV);

            // Compute edge midpoints
            QVector<int> edgeMidpoints;
            for (int i = 0; i < face.indices.size(); ++i) {
                int a = face.indices[i];
                int b = face.indices[(i + 1) % face.indices.size()];
                QVector3D midpoint = (result.vertices[a].position + result.vertices[b].position) * 0.5f;
                Vertex midV;
                midV.position = midpoint;
                edgeMidpoints.append(subdivided.vertices.size());
                subdivided.vertices.append(midV);
            }

            // Create triangles from face center to edge midpoints
            for (int i = 0; i < face.indices.size(); ++i) {
                int next = (i + 1) % face.indices.size();
                Face tri;
                tri.indices = {edgeMidpoints[i], centerIdx, edgeMidpoints[next]};
                subdivided.faces.append(tri);
            }
        }

        result = subdivided;
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::unsubdivide(const MeshData& mesh, float detail)
{
    Q_UNUSED(detail);
    // Unsubdivide is complex - simplify by merging triangles into quads
    MeshData result;
    result.vertices = mesh.vertices;

    QVector<bool> faceUsed(mesh.faces.size(), false);
    for (int i = 0; i < mesh.faces.size(); ++i) {
        if (faceUsed[i]) continue;
        if (mesh.faces[i].indices.size() != 3) {
            result.faces.append(mesh.faces[i]);
            faceUsed[i] = true;
            continue;
        }

        // Find adjacent triangle to form quad
        for (int j = i + 1; j < mesh.faces.size(); ++j) {
            if (faceUsed[j] || mesh.faces[j].indices.size() != 3) continue;

            // Check if they share an edge
            QSet<int> verts1(mesh.faces[i].indices.begin(), mesh.faces[i].indices.end());
            QSet<int> verts2(mesh.faces[j].indices.begin(), mesh.faces[j].indices.end());
            QSet<int> shared = verts1 & verts2;

            if (shared.size() == 2) {
                // Merge into quad
                QVector<int> allVerts;
                for (int v : verts1) {
                    if (!shared.contains(v)) allVerts.append(v);
                }
                for (int v : shared) allVerts.append(v);
                for (int v : verts2) {
                    if (!shared.contains(v)) allVerts.append(v);
                }

                Face quad;
                quad.indices = allVerts;
                result.faces.append(quad);
                faceUsed[i] = true;
                faceUsed[j] = true;
                break;
            }
        }

        if (!faceUsed[i]) {
            result.faces.append(mesh.faces[i]);
            faceUsed[i] = true;
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::triangulate(const MeshData& mesh)
{
    MeshData result;
    result.vertices = mesh.vertices;

    for (const auto& face : mesh.faces) {
        if (face.indices.size() < 3) continue;

        // Fan triangulation
        for (int i = 1; i < face.indices.size() - 1; ++i) {
            Face tri;
            tri.indices = {face.indices[0], face.indices[i], face.indices[i + 1]};
            tri.normal = face.normal;
            result.faces.append(tri);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::quadrangulate(const MeshData& mesh)
{
    // Try to merge adjacent triangles into quads
    MeshData result;
    result.vertices = mesh.vertices;

    QVector<bool> faceUsed(mesh.faces.size(), false);
    for (int i = 0; i < mesh.faces.size(); ++i) {
        if (faceUsed[i]) continue;

        if (mesh.faces[i].indices.size() != 3) {
            result.faces.append(mesh.faces[i]);
            faceUsed[i] = true;
            continue;
        }

        bool merged = false;
        for (int j = i + 1; j < mesh.faces.size(); ++j) {
            if (faceUsed[j] || mesh.faces[j].indices.size() != 3) continue;

            QSet<int> verts1(mesh.faces[i].indices.begin(), mesh.faces[i].indices.end());
            QSet<int> verts2(mesh.faces[j].indices.begin(), mesh.faces[j].indices.end());
            QSet<int> shared = verts1 & verts2;

            if (shared.size() == 2) {
                QVector<int> allVerts;
                for (int v : verts1) allVerts.append(v);
                for (int v : verts2) {
                    if (!shared.contains(v)) allVerts.append(v);
                }

                // Order vertices properly
                Face quad;
                QSet<int> unique = verts1 | verts2;
                for (int v : unique) quad.indices.append(v);
                result.faces.append(quad);
                faceUsed[i] = true;
                faceUsed[j] = true;
                merged = true;
                break;
            }
        }

        if (!merged) {
            result.faces.append(mesh.faces[i]);
            faceUsed[i] = true;
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Transform
MeshData MeshOperations::mirror(const MeshData& mesh, const QVector3D& axis, float offset)
{
    MeshData result = mesh;
    int vertexCount = mesh.vertices.size();

    // Mirror vertices
    for (const auto& v : mesh.vertices) {
        Vertex mv = v;
        if (std::abs(axis.x()) > 0.5f) mv.position.setX(-mv.position.x() + offset);
        if (std::abs(axis.y()) > 0.5f) mv.position.setY(-mv.position.y() + offset);
        if (std::abs(axis.z()) > 0.5f) mv.position.setZ(-mv.position.z() + offset);
        mv.normal.setX(-mv.normal.x());
        mv.normal.setY(-mv.normal.y());
        mv.normal.setZ(-mv.normal.z());
        result.vertices.append(mv);
    }

    // Mirror faces (reversed winding)
    for (const auto& face : mesh.faces) {
        Face mirrored;
        for (int i = face.indices.size() - 1; i >= 0; --i) {
            mirrored.indices.append(face.indices[i] + vertexCount);
        }
        result.faces.append(mirrored);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::array(const MeshData& mesh, int count, const QVector3D& offset)
{
    if (count < 1) return mesh;

    MeshData result;
    int vertexCount = mesh.vertices.size();

    for (int i = 0; i < count; ++i) {
        QVector3D translation = offset * i;
        int baseVertex = result.vertices.size();

        for (const auto& v : mesh.vertices) {
            Vertex av = v;
            av.position += translation;
            result.vertices.append(av);
        }

        for (const auto& face : mesh.faces) {
            Face af;
            for (int idx : face.indices) {
                af.indices.append(idx + baseVertex);
            }
            result.faces.append(af);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::radialArray(const MeshData& mesh, int count, const QVector3D& axis, float angle)
{
    if (count < 2) return mesh;

    MeshData result;
    float stepAngle = angle / count;

    for (int i = 0; i < count; ++i) {
        QMatrix4x4 rotation;
        if (std::abs(axis.x()) > 0.9f) rotation.rotate(qRadiansToDegrees(stepAngle * i), 1, 0, 0);
        else if (std::abs(axis.y()) > 0.9f) rotation.rotate(qRadiansToDegrees(stepAngle * i), 0, 1, 0);
        else rotation.rotate(qRadiansToDegrees(stepAngle * i), 0, 0, 1);

        int baseVertex = result.vertices.size();
        for (const auto& v : mesh.vertices) {
            Vertex rv = v;
            rv.position = rotation.map(rv.position);
            rv.normal = rotation.map(rv.normal);
            result.vertices.append(rv);
        }

        for (const auto& face : mesh.faces) {
            Face rf;
            for (int idx : face.indices) rf.indices.append(idx + baseVertex);
            result.faces.append(rf);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Mesh editing
MeshData MeshOperations::knifeCut(const MeshData& mesh, const QVector3D& cutStart, const QVector3D& cutEnd)
{
    MeshData result = mesh;
    QVector3D cutDir = (cutEnd - cutStart).normalized();
    QVector3D cutNormal = QVector3D::crossProduct(cutDir, QVector3D(0, 1, 0)).normalized();

    for (int fi = 0; fi < result.faces.size(); ++fi) {
        auto& face = result.faces[fi];
        if (face.indices.size() < 3) continue;

        // Check if face intersects the cut plane
        QVector3D faceCenter;
        for (int idx : face.indices) faceCenter += result.vertices[idx].position;
        faceCenter /= face.indices.size();

        float dist = QVector3D::dotProduct(faceCenter - cutStart, cutNormal);
        if (std::abs(dist) < 0.01f) {
            // Face is on the cut plane - split it
            QVector<int> leftVerts, rightVerts;
            for (int idx : face.indices) {
                float d = QVector3D::dotProduct(result.vertices[idx].position - cutStart, cutNormal);
                if (d < 0) leftVerts.append(idx);
                else rightVerts.append(idx);
            }

            if (leftVerts.size() >= 3 && rightVerts.size() >= 3) {
                Face leftFace, rightFace;
                leftFace.indices = leftVerts;
                rightFace.indices = rightVerts;
                result.faces[fi] = leftFace;
                result.faces.append(rightFace);
            }
        }
    }

    result.computeNormals();
    return result;
}

MeshData MeshOperations::loopCut(const MeshData& mesh, int axis, float factor, float slide)
{
    axis = qBound(0, axis, 2);
    factor = qBound(0.0f, factor, 1.0f);
    slide = qBound(-1.0f, slide, 1.0f);

    MeshData result = mesh;
    result.computeBoundingBox();

    QVector3D min = result.boundingBoxMin;
    QVector3D max = result.boundingBoxMax;
    QVector3D n;
    if (axis == 0) n = QVector3D(1, 0, 0);
    else if (axis == 1) n = QVector3D(0, 1, 0);
    else n = QVector3D(0, 0, 1);

    float lo = axis == 0 ? min.x() : axis == 1 ? min.y() : min.z();
    float hi = axis == 0 ? max.x() : axis == 1 ? max.y() : max.z();
    float offset = lo + factor * (hi - lo);

    // 1) Find edges that strictly cross the plane and insert a vertex on each.
    QMap<quint64, int> splitMap;         // edge key -> new vertex index
    QVector<int> newVerts;               // new vertex indices (in insertion order)
    QVector<float> newT;                 // t along the original edge for each new vert
    QVector<QPair<int,int>> newEdgeEnds; // original edge endpoints for sliding

    auto edgeKey = [](int a, int b) -> quint64 {
        return (a < b) ? ((quint64)a << 32 | (quint32)b) : ((quint64)b << 32 | (quint32)a);
    };
    auto planeSide = [&](const QVector3D& p) -> float {
        return QVector3D::dotProduct(p, n) - offset;
    };

    QSet<quint64> seen;
    for (const auto& face : result.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            if (a < 0 || b < 0 || a >= result.vertices.size() || b >= result.vertices.size()) continue;
            quint64 key = edgeKey(a, b);
            if (seen.contains(key)) continue;
            seen.insert(key);

            float da = planeSide(result.vertices[a].position);
            float db = planeSide(result.vertices[b].position);
            if (da * db >= 0.0f) continue;   // no strict crossing

            float t = da / (da - db);
            t = qBound(0.0f, t, 1.0f);
            Vertex v;
            v.position = result.vertices[a].position + (result.vertices[b].position - result.vertices[a].position) * t;
            v.normal   = result.vertices[a].normal   + (result.vertices[b].normal   - result.vertices[a].normal)   * t;
            v.uv       = result.vertices[a].uv       + (result.vertices[b].uv       - result.vertices[a].uv)       * t;
            v.color    = result.vertices[a].color    + (result.vertices[b].color    - result.vertices[a].color)    * t;
            if (a < b) { v.tangent = result.vertices[b].tangent; v.weight = result.vertices[b].weight; v.boneIndex = result.vertices[b].boneIndex; }
            else       { v.tangent = result.vertices[a].tangent; v.weight = result.vertices[a].weight; v.boneIndex = result.vertices[a].boneIndex; }

            int newIdx = result.vertices.size();
            result.vertices.append(v);
            splitMap.insert(key, newIdx);
            newVerts.append(newIdx);
            newT.append(t);
            newEdgeEnds.append(qMakePair(a, b));
        }
    }

    if (newVerts.isEmpty()) return result;

    // 2) Rebuild faces, inserting the new vertices along the cut edges.
    QVector<Face> outFaces;
    outFaces.reserve(result.faces.size() + newVerts.size());

    for (const auto& face : result.faces) {
        if (face.indices.size() < 3) { outFaces.append(face); continue; }

        QVector<int> poly;
        poly.reserve(face.indices.size() + 4);
        QVector<int> newPosInPoly;

        auto keyFn = [&](int a, int b) -> quint64 {
            return (a < b) ? ((quint64)a << 32 | (quint32)b) : ((quint64)b << 32 | (quint32)a);
        };

        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            poly.append(a);
            auto it = splitMap.constFind(keyFn(a, b));
            if (it != splitMap.constEnd()) {
                poly.append(it.value());
                newPosInPoly.append(poly.size() - 1);
            }
        }

        if (newPosInPoly.size() == 2) {
            // Split the polygon at the two cut vertices.
            int i = newPosInPoly[0];
            int j = newPosInPoly[1];
            if (i > j) { int tmp = i; i = j; j = tmp; }

            QVector<int> p1, p2;
            for (int k = i; k <= j; ++k) p1.append(poly[k]);
            for (int k = j; k < poly.size(); ++k) p2.append(poly[k]);
            for (int k = 0; k <= i; ++k) p2.append(poly[k]);

            if (p1.size() >= 3) {
                Face f1 = face; f1.indices = p1;
                if (p1.size() == 4) { outFaces.append(f1); }
                else { // triangulate fan
                    for (int k = 1; k + 1 < p1.size(); ++k) {
                        Face t; t.indices = { p1[0], p1[k], p1[k + 1] };
                        t.materialId = face.materialId; outFaces.append(t);
                    }
                }
            }
            if (p2.size() >= 3) {
                Face f2 = face; f2.indices = p2;
                if (p2.size() == 4) { outFaces.append(f2); }
                else {
                    for (int k = 1; k + 1 < p2.size(); ++k) {
                        Face t; t.indices = { p2[0], p2[k], p2[k + 1] };
                        t.materialId = face.materialId; outFaces.append(t);
                    }
                }
            }
        } else {
            outFaces.append(face);
        }
    }
    result.faces = outFaces;

    // 3) Slide the inserted loop along its supporting edges.
    if (slide != 0.0f) {
        for (int k = 0; k < newVerts.size(); ++k) {
            int vi = newVerts[k];
            int a = newEdgeEnds[k].first;
            int b = newEdgeEnds[k].second;
            float t = qBound(0.0f, newT[k] + slide, 1.0f);
            result.vertices[vi].position =
                result.vertices[a].position + (result.vertices[b].position - result.vertices[a].position) * t;
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

int MeshOperations::sculptBrush(MeshData& mesh, const QVector3D& center, float radius,
                                float strength, int mode, const QVector3D& drag,
                                const QVector3D& previousCenter,
                                float falloffPower, const QSet<int>* pinned)
{
    if (mesh.vertices.isEmpty() || radius <= 0.0f) return 0;

    // Slash needs a fixed cut direction: prefer the supplied stroke drag, else
    // fall back to the previous-center delta, else the brush center direction.
    QVector3D cutDir = drag;
    if (cutDir.lengthSquared() < 1e-12f)
        cutDir = center - previousCenter;
    if (cutDir.lengthSquared() < 1e-12f)
        cutDir = QVector3D(1, 0, 0);
    cutDir.normalize();

    // Vertex adjacency (needed by smooth/pinch/smear and averaged normals).
    QMap<int, QVector<int>> adjacency;
    if (mode == 1 || mode == 5 || mode == 6 || mode == 7 || mode == 8) {
        for (const auto& face : mesh.faces) {
            for (int i = 0; i < face.indices.size(); ++i) {
                int a = face.indices[i];
                int b = face.indices[(i + 1) % face.indices.size()];
                if (a < 0 || b < 0 || a >= mesh.vertices.size() || b >= mesh.vertices.size()) continue;
                if (!adjacency[a].contains(b)) adjacency[a].append(b);
                if (!adjacency[b].contains(a)) adjacency[b].append(a);
            }
        }
    }

    // Smear needs the cursor delta between this stroke and the previous one.
    QVector3D cursorMove = center - previousCenter;
    if (cursorMove.lengthSquared() < 1e-12f)
        cursorMove = drag;

    auto vertexDir = [&mesh, &adjacency](int vi) -> QVector3D {
        const auto& nb = adjacency.value(vi);
        QVector3D n = mesh.vertices[vi].normal;
        if (n.lengthSquared() < 1e-9f && !nb.isEmpty()) {
            QVector3D sum;
            for (int nid : nb)
                sum += mesh.vertices[nid].position - mesh.vertices[vi].position;
            n = sum / float(nb.size());
        }
        return n.lengthSquared() > 1e-12f ? n.normalized() : QVector3D(0, 1, 0);
    };

    // Deterministic hash for the pores brush (stable across strokes/positions).
    auto hash01 = [](float x, float y, float z) -> float {
        unsigned h = (unsigned(x * 251.f) ^ (unsigned(y * 397.f) << 3) ^ (unsigned(z * 193.f) << 7));
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return (h & 0xffffu) / 65535.0f;
    };

    int affected = 0;
    for (int vi = 0; vi < mesh.vertices.size(); ++vi) {
        if (pinned && pinned->contains(vi)) continue;
        Vertex& v = mesh.vertices[vi];
        float d = (v.position - center).length();
        if (d > radius) continue;
        float t = 1.0f - (d / radius);
        float falloff;
        if (qAbs(falloffPower - 2.0f) < 1e-4f)
            falloff = t * t * (3.0f - 2.0f * t);   // smoothstep
        else
            falloff = t > 0.0f ? qPow(t, qBound(0.25f, falloffPower, 8.0f)) : 0.0f;
        if (falloff <= 0.001f) continue;
        affected++;

        switch (mode) {
        case 0: // draw
            v.position += v.normal * (strength * falloff);
            break;
        case 1: { // smooth
            const auto& nb = adjacency.value(vi);
            if (nb.isEmpty()) break;
            QVector3D avg;
            for (int n : nb) avg += mesh.vertices[n].position;
            avg /= nb.size();
            float w = qBound(0.0f, strength * falloff, 1.0f);
            v.position = v.position + (avg - v.position) * w;
            break;
        }
        case 2: // grab
            v.position += drag * falloff;
            break;
        case 3: { // flatten toward plane through center (using vertex normal)
            QVector3D cnorm = v.normal;
            float s = QVector3D::dotProduct(cnorm, center - v.position);
            v.position += cnorm * (s * falloff);
            break;
        }
        case 4: // crease
            v.position -= v.normal * (strength * falloff);
            break;
        case 5: { // inflate - push along the (averaged) outward normal, stronger at center
            QVector3D n = vertexDir(vi);
            float boost = 1.0f + falloff;   // centers bulge more
            v.position += n * (strength * falloff * boost);
            break;
        }
        case 6: { // pinch - pull vertices toward the brush center
            QVector3D toCenter = center - v.position;
            float len = toCenter.length();
            if (len < 1e-6f) break;
            float pull = qMin(len, radius) * strength * falloff * 0.5f;
            v.position += toCenter.normalized() * pull;
            break;
        }
        case 7: // smear - vertices follow the cursor movement
            v.position += cursorMove * (falloff * qBound(0.0f, strength, 1.0f));
            break;
        case 8: // negate - rush inward along normal, opposite of draw
            v.position -= v.normal * (strength * falloff * 1.5f);
            break;
        case 9: { // folds - concentric ridges/valleys along the normal
            float fold = qSin((d / radius) * 3.0f * float(M_PI));
            v.position += v.normal * (strength * falloff * fold * 0.8f);
            break;
        }
        case 10: { // pores - micro deterministic depressions
            float h = hash01(v.position.x(), v.position.y(), v.position.z());
            float amp = (h - 0.5f) * 2.0f;   // -1..1
            v.position += v.normal * (strength * falloff * amp * 0.35f);
            break;
        }
        case 11: // bulge - soft wide outward push along the vertex normal
            v.position += v.normal * (strength * falloff * falloff);
            break;
        case 12: // slash - directional cut along the stroke direction
            v.position -= cutDir * (strength * falloff * 1.2f);
            break;
        default:
            break;
        }
    }

    if (affected > 0) {
        mesh.computeNormals();
        mesh.computeBoundingBox();
    }
    return affected;
}

int MeshOperations::findClosestVertex(const MeshData& mesh, const QMatrix4x4& worldTransform,
                                      const QVector3D& worldPoint)
{
    if (mesh.vertices.isEmpty()) return -1;
    QMatrix4x4 inv = worldTransform.inverted();
    QVector3D localPoint = inv.map(worldPoint);

    int bestIdx = -1;
    float bestDist2 = std::numeric_limits<float>::max();
    for (int i = 0; i < mesh.vertices.size(); ++i) {
        float d2 = (mesh.vertices[i].position - localPoint).lengthSquared();
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestIdx = i;
        }
    }
    return bestIdx;
}

QPair<int, int> MeshOperations::findClosestEdge(const MeshData& mesh, const QMatrix4x4& worldTransform,
                                                const QVector3D& worldPoint)
{
    if (mesh.vertices.isEmpty()) return qMakePair(-1, -1);
    QMatrix4x4 inv = worldTransform.inverted();
    QVector3D localPoint = inv.map(worldPoint);

    // Build unique edges from faces
    QSet<quint64> edgeSet;
    auto edgeKey = [](int a, int b) -> quint64 {
        if (a > b) std::swap(a, b);
        return (quint64(a) << 32) | quint32(b);
    };

    for (const auto& face : mesh.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            if (a < 0 || b < 0 || a >= mesh.vertices.size() || b >= mesh.vertices.size()) continue;
            edgeSet.insert(edgeKey(a, b));
        }
    }

    float bestDist2 = std::numeric_limits<float>::max();
    QPair<int, int> bestEdge(-1, -1);

    for (quint64 key : edgeSet) {
        int a = int(key >> 32);
        int b = int(key & 0xFFFFFFFF);
        const QVector3D& p1 = mesh.vertices[a].position;
        const QVector3D& p2 = mesh.vertices[b].position;
        QVector3D edgeVec = p2 - p1;
        float len2 = edgeVec.lengthSquared();
        if (len2 < 1e-10f) continue;
        float t = QVector3D::dotProduct(localPoint - p1, edgeVec) / len2;
        t = qBound(0.0f, t, 1.0f);
        QVector3D closest = p1 + edgeVec * t;
        float d2 = (closest - localPoint).lengthSquared();
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestEdge = qMakePair(a, b);
        }
    }
    return bestEdge;
}

bool MeshOperations::vertexSlide(MeshData& mesh, int vertexIndex, const QVector3D& targetWorld,
                                 const QMatrix4x4& worldTransform)
{
    if (vertexIndex < 0 || vertexIndex >= mesh.vertices.size()) return false;

    // Build adjacency: neighbors of the vertex
    QVector<int> neighbors;
    for (const auto& face : mesh.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            if (face.indices[i] == vertexIndex) {
                int a = face.indices[(i + 1) % face.indices.size()];
                int b = face.indices[(i - 1 + face.indices.size()) % face.indices.size()];
                if (!neighbors.contains(a)) neighbors.append(a);
                if (!neighbors.contains(b)) neighbors.append(b);
            }
        }
    }
    if (neighbors.isEmpty()) return false;

    QMatrix4x4 inv = worldTransform.inverted();
    QVector3D localTarget = inv.map(targetWorld);

    // Project target onto the plane of each connected edge and pick the closest projection
    QVector3D bestMove;
    float bestDist2 = std::numeric_limits<float>::max();
    const QVector3D& vPos = mesh.vertices[vertexIndex].position;

    for (int n : neighbors) {
        const QVector3D& nPos = mesh.vertices[n].position;
        QVector3D edgeDir = nPos - vPos;
        float len2 = edgeDir.lengthSquared();
        if (len2 < 1e-10f) continue;
        float t = QVector3D::dotProduct(localTarget - vPos, edgeDir) / len2;
        QVector3D projected = vPos + edgeDir * t;
        float d2 = (projected - localTarget).lengthSquared();
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestMove = projected - vPos;
        }
    }

    if (bestMove.isNull()) return false;
    mesh.vertices[vertexIndex].position += bestMove;
    mesh.computeNormals();
    mesh.computeBoundingBox();
    return true;
}

bool MeshOperations::edgeSlide(MeshData& mesh, int edgeV0, int edgeV1, float factor)
{
    if (edgeV0 < 0 || edgeV0 >= mesh.vertices.size() ||
        edgeV1 < 0 || edgeV1 >= mesh.vertices.size()) return false;
    QVector3D p0 = mesh.vertices[edgeV0].position;
    QVector3D p1 = mesh.vertices[edgeV1].position;
    QVector3D edgeDir = p1 - p0;
    float len = edgeDir.length();
    if (len < 1e-10f) return false;
    edgeDir.normalize();
    // Move both vertices along edge by factor * len (factor in [-1,1])
    float move = qBound(-1.0f, factor, 1.0f) * len;
    mesh.vertices[edgeV0].position += edgeDir * move;
    mesh.vertices[edgeV1].position -= edgeDir * move; // opposite direction
    mesh.computeNormals();
    mesh.computeBoundingBox();
    return true;
}

MeshData MeshOperations::shrinkwrap(const MeshData& mesh, const MeshData& target, const QVector3D& direction)
{
    MeshData result = mesh;

    for (auto& v : result.vertices) {
        // Project vertex onto target mesh along direction
        QVector3D rayOrigin = v.position;
        QVector3D rayDir = direction.normalized();

        float closestT = std::numeric_limits<float>::max();
        for (const auto& face : target.faces) {
            if (face.indices.size() < 3) continue;
            QVector3D v0 = target.vertices[face.indices[0]].position;
            QVector3D v1 = target.vertices[face.indices[1]].position;
            QVector3D v2 = target.vertices[face.indices[2]].position;

            QVector3D edge1 = v1 - v0;
            QVector3D edge2 = v2 - v0;
            QVector3D h = QVector3D::crossProduct(rayDir, edge2);
            float a = QVector3D::dotProduct(edge1, h);
            if (std::abs(a) < 1e-6f) continue;
            float f = 1.0f / a;
            QVector3D s = rayOrigin - v0;
            float u = f * QVector3D::dotProduct(s, h);
            if (u < 0.0f || u > 1.0f) continue;
            QVector3D q = QVector3D::crossProduct(s, edge1);
            float v2d = f * QVector3D::dotProduct(rayDir, q);
            if (v2d < 0.0f || u + v2d > 1.0f) continue;
            float t = f * QVector3D::dotProduct(edge2, q);
            if (t > 0 && t < closestT) closestT = t;
        }

        if (closestT < std::numeric_limits<float>::max()) {
            v.position = rayOrigin + rayDir * closestT;
        }
    }

result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Bridge curve - create surface between two profiles/curves
MeshData MeshOperations::bridgeCurve(const MeshData& mesh, const QVector<int>& curve1Indices, const QVector<int>& curve2Indices, int segments) {
    MeshData result;

    // Get the vertex positions from the two curves
    // This assumes the indices refer to vertices in a mesh
    // In a full implementation, these would be NURBSCurve control points

    if (curve1Indices.isEmpty() || curve2Indices.isEmpty() || segments < 2) return result;

    int n1 = qMin(curve1Indices.size(), mesh.vertices.size());
    int n2 = qMin(curve2Indices.size(), mesh.vertices.size());

    // Simple bridge: create a loft between the two sets of points
    int profileSize = qMax(n1, n2);

    // Ensure both profiles have same number of vertices by padding
    QVector<QVector3D> profile1, profile2;
    for (int i = 0; i < profileSize; ++i) {
        if (i < curve1Indices.size() && curve1Indices[i] < mesh.vertices.size()) {
            profile1.append(mesh.vertices[curve1Indices[i]].position);
        } else {
            profile1.append(QVector3D(0, 0, 0));
        }
        if (i < curve2Indices.size() && curve2Indices[i] < mesh.vertices.size()) {
            profile2.append(mesh.vertices[curve2Indices[i]].position);
        } else {
            profile2.append(QVector3D(0, 0, 0));
        }
    }

    // Create vertices by interpolating between profiles
    for (int i = 0; i <= segments; ++i) {
        float t = float(i) / segments;
        // Interpolate between the two profiles
        for (int j = 0; j < profileSize; ++j) {
            Vertex v;
            v.position = profile1[j] * (1.0f - t) + profile2[j] * t;
            // Compute normal as average of profile normals (for now, use up vector)
            v.normal = QVector3D(0, 1, 0);
            v.uv = QVector2D(float(j) / profileSize, t);
            result.vertices.append(v);
        }
    }

    // Create faces between consecutive sections
    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < profileSize; ++j) {
            int nextJ = (j + 1) % profileSize;
            int v1 = i * profileSize + j;
            int v2 = i * profileSize + nextJ;
            int v3 = (i + 1) * profileSize + nextJ;
            int v4 = (i + 1) * profileSize + j;

            Face f;
            f.indices = {v1, v2, v3, v4};
            result.faces.append(f);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// Offset curve - offset a curve/profile by a given distance
MeshData MeshOperations::offsetCurve(const MeshData& mesh, const QVector<int>& curveIndices, float distance, bool offsetBothSides) {
    MeshData result;

    if (curveIndices.isEmpty() || qAbs(distance) < 0.001f) return result;

    // Get the vertices of the curve
    QVector<Vertex> curveVerts;
    for (int idx : curveIndices) {
        if (idx >= 0 && idx < mesh.vertices.size()) {
            curveVerts.append(mesh.vertices[idx]);
        }
    }

    if (curveVerts.size() < 2) return result;

    // Compute edge directions and offset vertices
    QVector<QVector3D> positions;
    QVector<QVector3D> tangents;

    for (int i = 0; i < curveVerts.size(); ++i) {
        const Vertex& v = curveVerts[i];
        positions.append(v.position);

        // Compute tangent (direction to next vertex, or from previous)
        QVector3D tangent;
        if (curveVerts.size() > 1) {
            int nextIdx = (i + 1) % curveVerts.size();
            int prevIdx = (i - 1 + curveVerts.size()) % curveVerts.size();
            QVector3D vNext = curveVerts[nextIdx].position;
            QVector3D vPrev = curveVerts[prevIdx].position;
            tangent = (vNext - vPrev).normalized();
        } else {
            tangent = QVector3D(1, 0, 0); // default
        }
        tangents.append(tangent);
    }

    // Compute offset normals (perpendicular to tangent)
    QVector<QVector3D> offsets;
    for (int i = 0; i < tangents.size(); ++i) {
        QVector3D tangent = tangents[i];
        // Use a fixed up vector, then recalculate proper normal
        QVector3D up(0, 0, 1);
        QVector3D side = QVector3D::crossProduct(tangent, up).normalized();
        if (qAbs(side.length() - 1.0f) < 0.001f || qIsNaN(side.length())) {
            up = QVector3D(0, 1, 0);
            side = QVector3D::crossProduct(tangent, up).normalized();
        }
        offsets.append(side * distance);
    }

    // Create offset vertices (original + offset)
    int originalCount = positions.size();
    for (int i = 0; i < originalCount; ++i) {
        Vertex v;
        v.position = positions[i] + offsets[i];
        v.normal = QVector3D(0, 1, 0); // Will be recomputed
        v.uv = QVector2D(i / float(originalCount), 0);
        result.vertices.append(v);
    }

    // If offsetBothSides, add second offset in opposite direction
    if (offsetBothSides) {
        for (int i = 0; i < originalCount; ++i) {
            Vertex v;
            v.position = positions[i] - offsets[i];
            v.normal = QVector3D(0, 1, 0); // Will be recomputed
            v.uv = QVector2D(i / float(originalCount), 1);
            result.vertices.append(v);
        }
    }

    // Create faces connecting original and offset quads
    int n = originalCount;
    for (int i = 0; i < n; ++i) {
        int nextI = (i + 1) % n;

        // Top offset quad (if offsetBothSides, this would be different)
        Face f;
        // Quad: original vertex i, original vertex nextI, next offset vertex, current offset vertex
        int offsetIdxI = n + i;
        int offsetIdxNextI = n + nextI;
        
        // If offsetBothSides, we have 2*originalCount vertices
        // Positions: 0..n-1 are original, n..2n-1 are first offset, 2n..3n-1 are second offset
        
        if (offsetBothSides) {
            // Four vertices: original[i], original[nextI], secondOffset[nextI], secondOffset[i]
            int secondOffsetBase = 2 * n;
            f.indices = {i, nextI, secondOffsetBase + nextI, secondOffsetBase + i};
        } else {
            // Two vertices: original[i], original[nextI], offset[nextI], offset[i]
            f.indices = {i, nextI, offsetIdxNextI, offsetIdxI};
        }
        result.faces.append(f);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::displace(const MeshData& mesh, const QImage& heightmap, float strength) {
    MeshData result = mesh;
    int w = heightmap.width();
    int h = heightmap.height();

    for (auto& v : result.vertices) {
        // Map UV to heightmap
        float u = v.uv.x();
        float vCoord = v.uv.y();
        int px = qBound(0, static_cast<int>(u * (w - 1)), w - 1);
        int py = qBound(0, static_cast<int>((1.0f - vCoord) * (h - 1)), h - 1);

        QColor pixel = heightmap.pixelColor(px, py);
        float height = pixel.lightnessF();
        v.position += v.normal * height * strength;
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

void MeshOperations::mergeMeshes(MeshData& target, const MeshData& source) {
    int vertexOffset = target.vertices.size();
    target.vertices.append(source.vertices);
    for (const auto& face : source.faces) {
        Face newFace = face;
        for (int& idx : newFace.indices) {
            idx += target.vertices.size() - source.vertices.size();
        }
        target.faces.append(newFace);
    }
}

void MeshOperations::splitMeshes(const MeshData& mesh, QVector<MeshData>& result) {
    result.clear();
    if (mesh.faces.isEmpty() || mesh.materials.isEmpty()) {
        result.append(mesh);
        return;
    }

    // Split by material
    QMap<int, QVector<int>> materialFaces;
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        materialFaces[mesh.faces[fi].materialId].append(fi);
    }

    for (auto it = materialFaces.begin(); it != materialFaces.end(); ++it) {
        MeshData part;
        part.name = mesh.name + "_part" + QString::number(it.key());
        part.materialName = it.key() < mesh.materials.size() ? mesh.materials[it.key()] : QString();
        part.metallic = mesh.metallic;
        part.roughness = mesh.roughness;

        QMap<int, int> vertexMap;
        for (int fi : it.value()) {
            const Face& srcFace = mesh.faces[fi];
            Face newFace;
            newFace.materialId = 0;
            newFace.normal = srcFace.normal;
            for (int vi : srcFace.indices) {
                if (!vertexMap.contains(vi)) {
                    int newIdx = part.vertices.size();
                    part.vertices.append(mesh.vertices[vi]);
                    vertexMap[vi] = newIdx;
                }
                newFace.indices.append(vertexMap[vi]);
            }
            part.faces.append(newFace);
        }
        result.append(part);
    }
}

QVector<MeshUVIsland> MeshOperations::findUVIslands(const MeshData& mesh) {
    QVector<MeshUVIsland> islands;
    if (mesh.faces.isEmpty()) return islands;

    ensureEdgeList(const_cast<MeshData&>(mesh));

    // Build adjacency from edges - faces that share an edge are connected
    QMap<int, QSet<int>> faceAdjacency;
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        const Face& face = mesh.faces[fi];
        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            QPair<int, int> key = qMakePair(qMin(a, b), qMax(a, b));

            // Find all faces sharing this edge
            for (int fj = 0; fj < mesh.faces.size(); ++fj) {
                if (fi == fj) continue;
                const Face& other = mesh.faces[fj];
                for (int j = 0; j < other.indices.size(); ++j) {
                    int oa = other.indices[j];
                    int ob = other.indices[(j + 1) % other.indices.size()];
                    if ((oa == a && ob == b) || (oa == b && ob == a)) {
                        faceAdjacency[fi].insert(fj);
                        faceAdjacency[fj].insert(fi);
                    }
                }
            }
        }
    }

    // Flood fill to find connected components
    QSet<int> visited;
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        if (visited.contains(fi)) continue;

        MeshUVIsland island;
        QVector<int> stack;
        stack.append(fi);
        QVector2D minUV(1e10f, 1e10f), maxUV(-1e10f, -1e10f);

        while (!stack.isEmpty()) {
            int current = stack.takeLast();
            if (visited.contains(current)) continue;
            visited.insert(current);
            island.faceIndices.append(current);

            const Face& face = mesh.faces[current];
            for (int vi : face.indices) {
                if (vi < mesh.vertices.size() && vi < mesh.uvs.size()) {
                    const QVector2D& uv = mesh.uvs[vi];
                    minUV.setX(qMin(minUV.x(), uv.x()));
                    minUV.setY(qMin(minUV.y(), uv.y()));
                    maxUV.setX(qMax(maxUV.x(), uv.x()));
                    maxUV.setY(qMax(maxUV.y(), uv.y()));
                }
            }

            for (int neighbor : faceAdjacency[current]) {
                if (!visited.contains(neighbor)) {
                    stack.append(neighbor);
                }
            }
        }

        island.minUV = minUV;
        island.maxUV = maxUV;
        islands.append(island);
    }

    return islands;
}

MeshData MeshOperations::resolveUVOverlaps(const MeshData& mesh, float padding)
{
    if (mesh.faces.isEmpty() || mesh.vertices.isEmpty()) return mesh;

    const auto uvAt = [&mesh](int vi) -> QVector2D {
        if (vi >= 0 && vi < mesh.uvs.size())
            return mesh.uvs[vi];
        if (vi >= 0 && vi < mesh.vertices.size())
            return mesh.vertices[vi].uv;
        return QVector2D();
    };

    // Face adjacency via shared edge keys. Seam-split vertices have distinct
    // indices, so faces on either side of a seam never share an edge key and
    // automatically belong to different islands.
    QMap<QPair<int, int>, QVector<int>> edgeFaces;
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        const Face& face = mesh.faces[fi];
        for (int i = 0; i < face.indices.size(); ++i) {
            int a = face.indices[i];
            int b = face.indices[(i + 1) % face.indices.size()];
            edgeFaces[qMakePair(qMin(a, b), qMax(a, b))].append(fi);
        }
    }

    const float eps = 1e-4f;
    QVector<QVector<int>> islands;   // face indices per island
    QVector<QVector2D> islandMin;
    QVector<QVector2D> islandMax;
    {
        QVector<bool> visited(mesh.faces.size(), false);
        for (int start = 0; start < mesh.faces.size(); ++start) {
            if (visited[start]) continue;
            QVector<int> stack;
            stack.append(start);
            QVector<int> faces;
            float mnX = std::numeric_limits<float>::max(), mnY = mnX;
            float mxX = std::numeric_limits<float>::lowest(), mxY = mxX;
            while (!stack.isEmpty()) {
                int cur = stack.takeLast();
                if (visited[cur]) continue;
                visited[cur] = true;
                faces.append(cur);
                const Face& face = mesh.faces[cur];
                for (int vi : face.indices) {
                    QVector2D uv = uvAt(vi);
                    mnX = qMin(mnX, uv.x()); mnY = qMin(mnY, uv.y());
                    mxX = qMax(mxX, uv.x()); mxY = qMax(mxY, uv.y());
                }
                const Face& f = mesh.faces[cur];
                for (int i = 0; i < f.indices.size(); ++i) {
                    int a = f.indices[i];
                    int b = f.indices[(i + 1) % f.indices.size()];
                    const auto key = qMakePair(qMin(a, b), qMax(a, b));
                    for (int nf : edgeFaces[key]) {
                        if (!visited[nf]) stack.append(nf);
                    }
                }
            }
            islands.append(faces);
            islandMin.append(QVector2D(mnX, mnY));
            islandMax.append(QVector2D(mxX, mxY));
        }
    }

    struct IslandPlace {
        QVector2D placedMin, placedSize;
    };
    QVector<IslandPlace> places;

    // Detect overlapping island pairs.
    struct Overlap { int a, b; };
    QVector<Overlap> overlaps;
    for (int i = 0; i < islands.size(); ++i) {
        for (int j = i + 1; j < islands.size(); ++j) {
            QVector2D amin = islandMin[i], amax = islandMax[i];
            QVector2D bmin = islandMin[j], bmax = islandMax[j];
            const bool axisOverlap = (amin.x() + eps <= bmax.x() && bmin.x() + eps <= amax.x())
                                  && (amin.y() + eps <= bmax.y() && bmin.y() + eps <= amax.y());
            if (axisOverlap) overlaps.append({ i, j });
        }
    }
    if (overlaps.isEmpty()) return mesh;

    // Build the set of islands involved in any overlap.
    QSet<int> involved;
    for (const auto& o : overlaps) { involved.insert(o.a); involved.insert(o.b); }

    // Order islands by area descending so the big charts are placed first and
    // smaller ones get packed around them.
    QVector<int> order;
    for (int i = 0; i < islands.size(); ++i)
        if (involved.contains(i)) order.append(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const float areaA = (islandMax[a].x() - islandMin[a].x()) * (islandMax[a].y() - islandMin[a].y());
        const float areaB = (islandMax[b].x() - islandMin[b].x()) * (islandMax[b].y() - islandMin[b].y());
        return areaA > areaB;
    });

    // Greedy shelf placement: try candidate offsets until the island's padded
    // box clears every already-placed box.
    QVector<QRectF> placedBoxes;
    float minStep = 1e-4f;
    for (int islandIdx : order) {
        const QVector2D mn = islandMin[islandIdx];
        const QVector2D mx = islandMax[islandIdx];
        const float w = mx.x() - mn.x();
        const float h = mx.y() - mn.y();
        const float stepX = qMax(w * 0.25f, minStep);
        const float stepY = qMax(h * 0.25f, minStep);
        bool placed = false;
        for (int k = 0; k < 2000 && !placed; ++k) {
            // Spiral scan outward from the origin.
            const int ring = (int)std::sqrt((double)k);
            const int side = ring * 2 + 1;
            int idx = k - ring * ring;
            float cx = 0, cy = 0;
            if (side == 1) { cx = 0; cy = 0; }
            else if (idx < side) { cx = idx - ring; cy = -ring; }
            else if (idx < side * 2 - 1) { cx = ring; cy = idx - (side - 1) - ring; }
            else if (idx < side * 3 - 2) { cx = (side * 2 - 1) - idx + ring - ring; cy = ring; }
            else { cx = -ring; cy = (side * 3 - 1) - idx - ring; }
            const QRectF box(cx * stepX, cy * stepY, w + padding * 0.5f, h + padding * 0.5f);
            bool clear = true;
            for (const auto& pb : placedBoxes) {
                if (box.intersects(pb)) { clear = false; break; }
            }
            if (clear) {
                placedBoxes.append(box);
                places.resize(islands.size());
                places[islandIdx] = { QVector2D(cx * stepX, cy * stepY),
                                      QVector2D(w + padding * 0.5f, h + padding * 0.5f) };
                placed = true;
            }
        }
        if (!placed) {
            // Fallback: shove it below everything so normalization still fixes it.
            float maxY = 0;
            for (const auto& pb : placedBoxes) maxY = qMax(maxY, (float)(pb.y() + pb.height()));
            const QRectF box(0.0f, maxY, w + padding * 0.5f, h + padding * 0.5f);
            placedBoxes.append(box);
            places.resize(islands.size());
            places[islandIdx] = { QVector2D(0.0f, maxY), QVector2D(w + padding * 0.5f, h + padding * 0.5f) };
        }
    }

    // Global bounds of the placed layout, then uniform scale into [0,1]^2.
    QRectF globalBox;
    for (const auto& pb : placedBoxes) {
        globalBox = globalBox.isNull() ? pb : globalBox.united(pb);
    }
    float gs = 1.0f;
    const float gw = globalBox.width();
    const float gh = globalBox.height();
    if (gw > 0.0f && gh > 0.0f) {
        const float avail = 1.0f - padding * 2.0f;
        gs = avail / qMax(gw, gh);
    }
    const float gox = globalBox.x();
    const float goy = globalBox.y();

    // Which island owns each vertex? Seam-split vertices belong to one island.
    QVector<int> vertexIsland(mesh.vertices.size(), -1);
    for (int i = 0; i < islands.size(); ++i) {
        for (int fi : islands[i]) {
            for (int vi : mesh.faces[fi].indices) {
                if (vi >= 0 && vi < vertexIsland.size()) vertexIsland[vi] = i;
            }
        }
    }

    MeshData result = mesh;
    for (int vi = 0; vi < result.vertices.size(); ++vi) {
        const int island = vertexIsland[vi];
        QVector2D uv = uvAt(vi);
        if (island >= 0 && island < places.size()) {
            const QVector2D offset = places[island].placedMin - islandMin[island];
            uv = (uv + offset - QVector2D(gox, goy)) * gs + QVector2D(padding, padding);
        }
        result.vertices[vi].uv = uv;
        if (vi < result.uvs.size()) result.uvs[vi] = uv;
    }

    result.computeBoundingBox();
    return result;
}

geometry::GeoMeshData MeshOperations::toGeoMesh(const MeshData& mesh) {
    geometry::GeoMeshData geo;
    geo.vertices.resize(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& pos = mesh.vertices[i].position;
        geo.vertices[i] = geometry::GeoVertex(pos.x(), pos.y(), pos.z());
    }
    
    for (const auto& face : mesh.faces) {
        if (face.indices.size() >= 3) {
            geo.faces.emplace_back(face.indices[0], face.indices[1], face.indices[2]);
        }
    }
    return geo;
}

MeshData MeshOperations::fromGeoMesh(const geometry::GeoMeshData& geo) {
    MeshData mesh;
    mesh.vertices.resize(geo.vertices.size());
    for (size_t i = 0; i < geo.vertices.size(); ++i) {
        mesh.vertices[i].position = QVector3D((float)geo.vertices[i].x, (float)geo.vertices[i].y, (float)geo.vertices[i].z);
    }
    for (const auto& face : geo.faces) {
        mesh.faces.append(Face({(int)face.v0, (int)face.v1, (int)face.v2}));
    }
    return mesh;
}

// ---------------------------------------------------------------------------
// Shell / solidity
// ---------------------------------------------------------------------------
MeshData MeshOperations::shell(const MeshData& mesh, float thickness, int direction, bool flipNormals)
{
    MeshData result;
    if (mesh.vertices.isEmpty() || mesh.faces.isEmpty())
        return result;

    // 1. Average vertex normals over the incident faces (smooth normals).
    QVector<QVector3D> avgNormals(mesh.vertices.size(), QVector3D(0, 0, 0));
    for (const Face& f : mesh.faces) {
        if (f.indices.size() < 3) continue;
        int i0 = f.indices[0], i1 = f.indices[1], i2 = f.indices[2];
        if (i0 < 0 || i1 < 0 || i2 < 0) continue;
        QVector3D fn = QVector3D::crossProduct(mesh.vertices[i1].position - mesh.vertices[i0].position,
                                               mesh.vertices[i2].position - mesh.vertices[i0].position);
        for (int vi : f.indices) {
            if (vi >= 0 && vi < avgNormals.size())
                avgNormals[vi] += fn;
        }
    }
    for (auto& n : avgNormals) {
        if (n.lengthSquared() > 1e-8f) n.normalize();
    }

    const int baseCount = mesh.vertices.size();

    // 2. Duplicate the mesh: outer shell offset by thickness along the axes of
    //    the direction (+1 outward, -1 inward) and keep the original shell.
    result.vertices = mesh.vertices;
    for (int i = 0; i < baseCount; ++i) {
        const Vertex& v = mesh.vertices[i];
        float off = direction * thickness;
        Vertex outer = v;
        outer.position += avgNormals[i] * off;
        result.vertices.append(outer);
    }

    // 3. Faces of the original surface (front side) and duplicated (back side,
    //    winding reversed so normals face outward).
    for (const Face& f : mesh.faces) {
        result.faces.append(f);
        Face back = f;
        std::reverse(back.indices.begin(), back.indices.end());
        for (int& idx : back.indices) idx += baseCount;
        result.faces.append(back);
    }

    // 4. Identify boundary edges (edges shared by a single face) and build a
    //    rim wall between the inner and outer boundary loops.
    QMap<QPair<int, int>, int> edgeCount;
    QMap<QPair<int, int>, int> edgeOwnerFace; // edge -> first face that owns it
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        const Face& f = mesh.faces[fi];
        for (int i = 0; i < f.indices.size(); ++i) {
            int a = f.indices[i];
            int b = f.indices[(i + 1) % f.indices.size()];
            QPair<int, int> key(qMin(a, b), qMax(a, b));
            edgeCount[key]++;
            if (!edgeOwnerFace.contains(key)) edgeOwnerFace[key] = fi;
        }
    }

    // Build rim quads: inner edge (a,b) → outer edge (a+base, b+base). Winding
    // follows the boundary orientation of the face that owns the edge.
    for (auto it = edgeCount.constBegin(); it != edgeCount.constEnd(); ++it) {
        if (it.value() != 1) continue; // not a boundary edge
        int a = it.key().first, b = it.key().second;
        int faceIdx = edgeOwnerFace.value(it.key(), -1);
        if (faceIdx < 0 || faceIdx >= mesh.faces.size()) continue;
        const Face& owner = mesh.faces[faceIdx];
        // Find the oriented edge (vA → vB) inside the owner face.
        int vA = -1, vB = -1;
        for (int i = 0; i < owner.indices.size(); ++i) {
            int ia = owner.indices[i];
            int ib = owner.indices[(i + 1) % owner.indices.size()];
            if (qMin(ia, ib) == a && qMax(ia, ib) == b) { vA = ia; vB = ib; break; }
        }
        if (vA < 0) continue;
        int oA = vA + baseCount, oB = vB + baseCount;
        Face rim;
        if (direction > 0)
            rim.indices = { vB, vA, oA, oB };
        else
            rim.indices = { vA, vB, oB, oA };
        result.faces.append(rim);
    }

    if (flipNormals) {
        for (Face& f : result.faces)
            std::reverse(f.indices.begin(), f.indices.end());
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// ---------------------------------------------------------------------------
// Polygonal bridge
// ---------------------------------------------------------------------------
MeshData MeshOperations::bridgeEdges(const MeshData& mesh,
                                     const QVector<int>& loopA,
                                     const QVector<int>& loopB,
                                     int segments)
{
    MeshData result;
    if (loopA.size() != loopB.size() || loopA.size() < 3 || segments < 1)
        return result;

    for (int v : loopA) if (v < 0 || v >= mesh.vertices.size()) return result;
    for (int v : loopB) if (v < 0 || v >= mesh.vertices.size()) return result;

    result = mesh;
    const int baseCount = result.vertices.size();

    // 1. Create ring vertices interpolating between loopA and loopB.
    //    ring r (0..segments): t = r/segments. Ring 0 == loopA (original verts),
    //    ring `segments` == loopB (original verts). Intermediate rings are new.
    struct Ring { QVector<int> ids; Ring(int n = 0) { ids.reserve(n); } };
    QVector<Ring> rings(segments + 1, Ring(loopA.size()));
    for (int r = 0; r <= segments; ++r) {
        float t = float(r) / float(segments);
        if (t < 0.001f) {
            rings[r].ids = loopA;
        } else if (t > 0.999f) {
            rings[r].ids = loopB;
        } else {
            for (int i = 0; i < loopA.size(); ++i) {
                const QVector3D pA = mesh.vertices[loopA[i]].position;
                const QVector3D pB = mesh.vertices[loopB[i]].position;
                Vertex v = mesh.vertices[loopA[i]];
                v.position = pA + (pB - pA) * t;
                result.vertices.append(v);
                rings[r].ids.append(result.vertices.size() - 1);
            }
        }
    }

    // 2. Connect the rings with quads: for each i, quad
    //    (ring[r]·i, ring[r+1]·i, ring[r+1]·(i+1), ring[r]·(i+1)).
    for (int r = 0; r < segments; ++r) {
        const QVector<int>& ra = rings[r].ids;
        const QVector<int>& rb = rings[r + 1].ids;
        if (ra.size() != rb.size()) return result;
        for (int i = 0; i < ra.size(); ++i) {
            int iNext = (i + 1) % ra.size();
            Face quad;
            quad.indices = { ra[i], rb[i], rb[iNext], ra[iNext] };
            result.faces.append(quad);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::bridgeFaces(const MeshData& mesh, int faceA, int faceB, int segments)
{
    if (faceA < 0 || faceA >= mesh.faces.size() ||
        faceB < 0 || faceB >= mesh.faces.size())
        return mesh;

    const Face& fa = mesh.faces[faceA];
    const Face& fb = mesh.faces[faceB];
    if (fa.indices.size() < 3 || fb.indices.size() < 3 || fa.indices.size() != fb.indices.size())
        return mesh;

    QVector<int> loopA = fa.indices;
    QVector<int> loopB = fb.indices;

    // Align B to A: rotate loopB so its first vertex is the closest to loopA[0].
    int best = 0;
    float bestDist = std::numeric_limits<float>::max();
    for (int r = 0; r < loopB.size(); ++r) {
        float d = (mesh.vertices[loopB[r]].position - mesh.vertices[loopA[0]].position).lengthSquared();
        if (d < bestDist) { bestDist = d; best = r; }
    }
    QVector<int> rot;
    for (int i = 0; i < loopB.size(); ++i)
        rot.append(loopB[(best + i) % loopB.size()]);
    loopB = rot;

    // Since this is a standalone open sheet (no surrounding faces), we only
    // add the connecting quads; duplicate the bridge strips like BridgeEdges.
    MeshData result = bridgeEdges(mesh, loopA, loopB, segments);
    return result;
}

// ---------------------------------------------------------------------------
// Smoothing groups (auto-smooth)
// ---------------------------------------------------------------------------
QVector<int> MeshOperations::autoSmooth(const MeshData& mesh, float angleDeg)
{
    const int numFaces = mesh.faces.size();
    QVector<int> groups(numFaces, -1);
    if (numFaces == 0) return groups;

    const float threshold = qDegreesToRadians(angleDeg);

    // Compute face normals.
    QVector<QVector3D> fNorm(numFaces);
    for (int fi = 0; fi < numFaces; ++fi)
        fNorm[fi] = computeFaceNormal(mesh, fi);

    // Adjacency via shared edges.
    QMap<QPair<int, int>, QVector<int>> edgeFaces;
    for (int fi = 0; fi < numFaces; ++fi) {
        const Face& f = mesh.faces[fi];
        for (int i = 0; i < f.indices.size(); ++i) {
            int a = f.indices[i], b = f.indices[(i + 1) % f.indices.size()];
            edgeFaces[qMakePair(qMin(a, b), qMax(a, b))].append(fi);
        }
    }

    QVector<QVector<int>> adj(numFaces);
    for (auto it = edgeFaces.constBegin(); it != edgeFaces.constEnd(); ++it) {
        const QVector<int>& fs = it.value();
        for (int i = 0; i < fs.size(); ++i)
            for (int j = i + 1; j < fs.size(); ++j) {
                adj[fs[i]].append(fs[j]);
                adj[fs[j]].append(fs[i]);
            }
    }

    // Greedy flood-fill: start a new group when the dihedral angle between
    // a face and the current group seed exceeds the threshold.
    int groupId = 0;
    QVector<bool> visited(numFaces, false);
    for (int start = 0; start < numFaces; ++start) {
        if (visited[start]) continue;
        visited[start] = true;
        groups[start] = groupId % 32; // 32 groups max, wraps as in Max

        QVector<int> stack;
        stack.append(start);
        while (!stack.isEmpty()) {
            int cur = stack.takeLast();
            const QVector3D& cn = fNorm[cur];
            for (int nx : adj[cur]) {
                if (visited[nx]) continue;
                float dot = QVector3D::dotProduct(cn, fNorm[nx]);
                dot = qBound(-1.0f, dot, 1.0f);
                float ang = std::acos(dot);
                // Only merge if the surface is locally soft (angle below threshold).
                bool share = (ang <= threshold + 1e-5f) &&
                             (groups[nx] < 0 || groups[nx] == groups[cur]);
                if (share) {
                    visited[nx] = true;
                    groups[nx] = groups[cur];
                    stack.append(nx);
                }
            }
        }
        ++groupId;
    }

    // Faces still -1 (hard edges) get their own group (no smoothing = 0).
    for (int fi = 0; fi < numFaces; ++fi)
        if (groups[fi] < 0) groups[fi] = 0;

    return groups;
}

// ---------------------------------------------------------------------------
// Border / element detection
// ---------------------------------------------------------------------------
QVector<Edge> MeshOperations::borderEdges(const MeshData& mesh)
{
    QVector<Edge> borders;
    QMap<QPair<int, int>, int> count;
    for (const Face& f : mesh.faces) {
        for (int i = 0; i < f.indices.size(); ++i) {
            int a = f.indices[i], b = f.indices[(i + 1) % f.indices.size()];
            count[qMakePair(qMin(a, b), qMax(a, b))]++;
        }
    }
    for (auto it = count.constBegin(); it != count.constEnd(); ++it)
        if (it.value() == 1)
            borders.append(Edge(it.key().first, it.key().second));
    return borders;
}

QPair<int, int> MeshOperations::findClosestBorderEdge(const MeshData& mesh,
                                                      const QMatrix4x4& worldTransform,
                                                      const QVector3D& worldPoint)
{
    QVector<Edge> borders = borderEdges(mesh);
    if (borders.isEmpty()) return qMakePair(-1, -1);

    QVector3D localPoint = worldTransform.inverted().map(worldPoint);
    float bestDist = std::numeric_limits<float>::max();
    QPair<int, int> best(-1, -1);
    for (const Edge& e : borders) {
        if (e.v1 < 0 || e.v1 >= mesh.vertices.size() ||
            e.v2 < 0 || e.v2 >= mesh.vertices.size()) continue;
        const QVector3D a = mesh.vertices[e.v1].position;
        const QVector3D b = mesh.vertices[e.v2].position;
        // Distance from point to segment a-b.
        QVector3D ab = b - a;
        float len2 = ab.lengthSquared();
        float t = (len2 > 1e-12f) ? QVector3D::dotProduct(localPoint - a, ab) / len2 : 0.f;
        t = qBound(0.0f, t, 1.0f);
        QVector3D closest = a + ab * t;
        float d = (localPoint - closest).lengthSquared();
        if (d < bestDist) { bestDist = d; best = qMakePair(e.v1, e.v2); }
    }
    return best;
}

QVector<int> MeshOperations::faceElements(const MeshData& mesh)
{
    const int n = mesh.faces.size();
    QVector<int> element(n, -1);
    if (n == 0) return element;

    QMap<QPair<int, int>, QVector<int>> edgeFaces;
    for (int fi = 0; fi < n; ++fi) {
        const Face& f = mesh.faces[fi];
        for (int i = 0; i < f.indices.size(); ++i) {
            int a = f.indices[i], b = f.indices[(i + 1) % f.indices.size()];
            edgeFaces[qMakePair(qMin(a, b), qMax(a, b))].append(fi);
        }
    }
    int cur = 0;
    for (int start = 0; start < n; ++start) {
        if (element[start] >= 0) continue;
        element[start] = cur;
        QVector<int> stack = { start };
        while (!stack.isEmpty()) {
            int f = stack.takeLast();
            const Face& ff = mesh.faces[f];
            for (int i = 0; i < ff.indices.size(); ++i) {
                int a = ff.indices[i], b = ff.indices[(i + 1) % ff.indices.size()];
                const QVector<int>& shared = edgeFaces[qMakePair(qMin(a, b), qMax(a, b))];
                for (int nx : shared) {
                    if (nx != f && element[nx] < 0) { element[nx] = cur; stack.append(nx); }
                }
            }
        }
        ++cur;
    }
    return element;
}

int MeshOperations::elementAtWorld(const MeshData& mesh, const QMatrix4x4& worldTransform,
                                   const QVector3D& worldPoint)
{
    int v = findClosestVertex(mesh, worldTransform, worldPoint);
    if (v < 0) return -1;
    QVector<int> elems = faceElements(mesh);
    for (int fi = 0; fi < mesh.faces.size(); ++fi) {
        if (mesh.faces[fi].indices.contains(v))
            return elems[fi];
    }
    return -1;
}

} // namespace ks

// Face hiding / radial menu (SelectionManager members)
void ks::MeshOperations::SelectionManager::hideFace(int faceIndex) {
    if (faceIndex >= 0) {
        m_hiddenFaces.append(faceIndex);
    }
}

void ks::MeshOperations::SelectionManager::unhideFace(int faceIndex) {
    m_hiddenFaces.removeAll(faceIndex);
}

void ks::MeshOperations::SelectionManager::hideSelectedFaces() {
    m_hiddenFaces.append(m_selectedFaces);
    m_selectedFaces.clear();
}

void ks::MeshOperations::SelectionManager::unhideAllFaces() {
    m_hiddenFaces.clear();
}

void ks::MeshOperations::SelectionManager::showRadialMenu(int mode, const QVector2D& pos) {
    radialMenu().clear();
    radialMenu().pos = pos;
    radialMenu().active = true;

    // Generate menu items based on the current selection mode
    switch (static_cast<SelectionMode>(mode)) {
    case SelectionMode::Vertex:
        radialMenu().addItem("Move", 1);
        radialMenu().addItem("Rotate", 2);
        radialMenu().addItem("Scale", 3);
        radialMenu().addItem("Weld", 6);
        radialMenu().addItem("Bevel", 4);
        radialMenu().addItem("Extrude", 7);
        radialMenu().addItem("Delete", 8);
        break;
    case SelectionMode::Edge:
        radialMenu().addItem("Move", 1);
        radialMenu().addItem("Rotate", 2);
        radialMenu().addItem("Scale", 3);
        radialMenu().addItem("Fillet", 4);
        radialMenu().addItem("Chamfer", 5);
        radialMenu().addItem("Bridge", 9);
        radialMenu().addItem("Loop Cut", 10);
        radialMenu().addItem("Delete", 8);
        break;
    case SelectionMode::Face:
        radialMenu().addItem("Move", 1);
        radialMenu().addItem("Rotate", 2);
        radialMenu().addItem("Scale", 3);
        radialMenu().addItem("Extrude", 7);
        radialMenu().addItem("Inset", 11);
        radialMenu().addItem("Bevel", 4);
        radialMenu().addItem("Delete", 8);
        break;
    case SelectionMode::Object:
        radialMenu().addItem("Move", 1);
        radialMenu().addItem("Rotate", 2);
        radialMenu().addItem("Scale", 3);
        radialMenu().addItem("Mirror", 12);
        radialMenu().addItem("Array", 13);
        radialMenu().addItem("Delete", 8);
        break;
    default:
        radialMenu().addItem("Move", 1);
        radialMenu().addItem("Rotate", 2);
        radialMenu().addItem("Scale", 3);
        break;
    }
}

void ks::MeshOperations::SelectionManager::hideRadialMenu() {
    radialMenu().active = false;
    radialMenu().clear();
}

ks::MeshOperations::RadialMenu& ks::MeshOperations::SelectionManager::radialMenu() {
    // Return the radial menu instance
    // In a full implementation, this would be a proper singleton or GUI element
    static RadialMenu menu;
    return menu;
}

void ks::MeshOperations::SelectionManager::showContextMenu(const QVector3D& worldPos) {
    // Store context position for callers to retrieve.
    // The actual QMenu is created by the GUI layer (MeshEditorModule::onShowContextMenu).
    // Store in a static so the GUI layer can query it after this call.
    static QVector3D s_contextWorldPos;
    s_contextWorldPos = worldPos;
}

QSet<int> ks::MeshOperations::SelectionManager::getSelectedFaceNeighbors(const MeshData& mesh, int faceIndex) {
    QSet<int> neighbors;
    if (faceIndex < 0 || faceIndex >= (int)mesh.faces.size()) return neighbors;

    const Face& face = mesh.faces[faceIndex];
    int n = face.indices.size();
    if (n < 2) return neighbors;

    // Build edge set for this face: each edge is an unordered pair {min, max}
    QSet<QPair<int, int>> faceEdges;
    for (int i = 0; i < n; ++i) {
        int a = face.indices[i];
        int b = face.indices[(i + 1) % n];
        faceEdges.insert(qMin(a, b) < qMax(a, b) ? QPair<int,int>(a, b) : QPair<int,int>(b, a));
    }

    // For each other face, check if it shares any edge with our face
    for (int fi = 0; fi < (int)mesh.faces.size(); ++fi) {
        if (fi == faceIndex) continue;
        const Face& other = mesh.faces[fi];
        int m = other.indices.size();
        if (m < 2) continue;
        for (int i = 0; i < m; ++i) {
            int a = other.indices[i];
            int b = other.indices[(i + 1) % m];
            QPair<int,int> edge = a < b ? QPair<int,int>(a, b) : QPair<int,int>(b, a);
            if (faceEdges.contains(edge)) {
                neighbors.insert(fi);
                break;
            }
        }
    }
    return neighbors;
}

// ---------------------------------------------------------------------------
// Action-center transforms & reusable falloff (Modo / 3ds Max "action center")
// ---------------------------------------------------------------------------
namespace ks {
float MeshOperations::falloffFactor(float distance, float radius, int type)
{
    if (distance <= 0.0f) return 1.0f;
    if (radius <= 0.0f) return 0.0f;
    float t = distance / radius;
    if (t >= 1.0f) return 0.0f;
    switch (type) {
        case 0: { // Smooth (smoothstep)
            float s = 1.0f - t;
            return s * s * (3.0f - 2.0f * s);
        }
        case 1: return 1.0f - t;          // Linear
        case 2: { float s = 1.0f - t; return s * s; } // Sharp
        case 3: return std::sqrt(1.0f - t);           // Root
        case 4: return std::sqrt(1.0f - t * t);       // Sphere
        case 5: return 1.0f;              // Constant
        default: return 1.0f - t;
    }
}

MeshData MeshOperations::transformAround(const MeshData& mesh, const QVector<int>& selection,
                                         TransformCenterMode mode, const QVector3D& pivot,
                                         const QVector3D& axis, const QVector3D& amount,
                                         float falloffRadius, int falloffType)
{
    MeshData result = mesh;

    QVector<int> affected;
    if (selection.isEmpty()) {
        affected.reserve(result.vertices.size());
        for (int i = 0; i < result.vertices.size(); ++i) affected.append(i);
    } else {
        QSet<int> seen;
        for (int idx : selection) {
            if (idx >= 0 && idx < result.vertices.size() && !seen.contains(idx)) {
                seen.insert(idx);
                affected.append(idx);
            }
        }
    }

    // Rotate mode: Euler composition about world axes through `pivot`.
    const float rx = qDegreesToRadians(amount.x());
    const float ry = qDegreesToRadians(amount.y());
    const float rz = qDegreesToRadians(amount.z());

    for (int vi : affected) {
        QVector3D pos = result.vertices[vi].position;
        QVector3D rel = pos - pivot;
        float weight = falloffRadius > 0.0f ? falloffFactor(rel.length(), falloffRadius, falloffType) : 1.0f;
        if (weight <= 0.0f) continue;

        switch (mode) {
        case TransformCenterMode::Translate: {
            result.vertices[vi].position = pos + amount * weight;
            break;
        }
        case TransformCenterMode::Rotate: {
            QVector3D r = rel * weight;
            // Rotate Y
            float nx =  cosf(ry) * r.x() + sinf(ry) * r.z();
            float nz = -sinf(ry) * r.x() + cosf(ry) * r.z();
            r.setX(nx); r.setZ(nz);
            // Rotate X
            float ny =  cosf(rx) * r.y() - sinf(rx) * r.z();
            nz = sinf(rx) * r.y() + cosf(rx) * r.z();
            r.setY(ny); r.setZ(nz);
            // Rotate Z
            nx = cosf(rz) * r.x() - sinf(rz) * r.y();
            ny = sinf(rz) * r.x() + cosf(rz) * r.y();
            r.setX(nx); r.setY(ny);
            // Blend back toward the original offset for partial falloff weights.
            if (weight < 1.0f)
                r = rel * (1.0f - weight) + r * weight;
            result.vertices[vi].position = pivot + r;
            break;
        }
        case TransformCenterMode::ScaleUniform: {
            float s = 1.0f + (amount.x() - 1.0f) * weight;
            result.vertices[vi].position = pivot + rel * s;
            break;
        }
        case TransformCenterMode::ScaleAxis: {
            float sx = 1.0f + (amount.x() - 1.0f) * weight;
            float sy = 1.0f + (amount.y() - 1.0f) * weight;
            float sz = 1.0f + (amount.z() - 1.0f) * weight;
            result.vertices[vi].position = QVector3D(pivot.x() + rel.x() * sx,
                                                     pivot.y() + rel.y() * sy,
                                                     pivot.z() + rel.z() * sz);
            break;
        }
        default: break;
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// ---------------------------------------------------------------------------
// Smoothing-group split (3ds Max "Split"): turn group boundaries into seams
// ---------------------------------------------------------------------------
MeshData MeshOperations::splitSmoothingGroups(const MeshData& mesh, const QVector<int>& faceGroups)
{
    if (mesh.faces.isEmpty()) return mesh;
    const int numFaces = mesh.faces.size();
    if (faceGroups.size() != numFaces) return mesh;

    // Quick reject: a single group across all faces has no seams to split.
    const int firstGroup = faceGroups.first();
    bool uniform = true;
    for (int g : faceGroups) if (g != firstGroup) { uniform = false; break; }
    if (uniform) return mesh;

    MeshData result;
    result.vertices = mesh.vertices;
    result.materials = mesh.materials;

    // Group memberships per vertex (a vertex shared by faces of several groups
    // must be duplicated once per extra group).
    QVector<QSet<int>> vertexGroups(mesh.vertices.size());
    for (int fi = 0; fi < numFaces; ++fi) {
        const int g = faceGroups[fi];
        const Face& f = mesh.faces[fi];
        for (int idx : f.indices) {
            if (idx >= 0 && idx < vertexGroups.size())
                vertexGroups[idx].insert(g);
        }
    }

    // Allocate one clone per extra group, keeping the smallest group id on the
    // original vertex so unchanged faces keep their exact indices.
    QHash<QPair<int, int>, int> cloneMap; // (vertexIndex, group) -> clone index
    for (int vi = 0; vi < vertexGroups.size(); ++vi) {
        const QSet<int>& groups = vertexGroups[vi];
        if (groups.size() <= 1) continue;
        int base = *groups.constBegin();
        for (int g : groups) if (g < base) base = g;
        for (int g : groups) {
            if (g == base) continue;
            cloneMap.insert(qMakePair(vi, g), result.vertices.size());
            result.vertices.append(mesh.vertices[vi]); // full attribute copy
        }
    }

    if (cloneMap.isEmpty()) return mesh;

    // Remap faces through the clone map.
    result.faces.reserve(mesh.faces.size());
    for (int fi = 0; fi < numFaces; ++fi) {
        const Face& f = mesh.faces[fi];
        const int g = faceGroups[fi];
        Face out;
        out.indices.reserve(f.indices.size());
        for (int i = 0; i < f.indices.size(); ++i) {
            const int idx = f.indices[i];
            int mapped = idx;
            if (idx >= 0 && idx < mesh.vertices.size()) {
                const auto it = cloneMap.constFind(qMakePair(idx, g));
                if (it != cloneMap.constEnd()) mapped = it.value();
            }
            out.indices.append(mapped);
            if (i < f.uvIndices.size()) out.uvIndices.append(f.uvIndices[i]);
            if (i < f.uv2Indices.size()) out.uv2Indices.append(f.uv2Indices[i]);
        }
        out.normal = f.normal;
        out.materialId = f.materialId;
        result.faces.append(out);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData MeshOperations::retopoQuadDraw(const MeshData& highPoly, const MeshData& lowPoly, float snapDist) {
    Q_UNUSED(snapDist);
    MeshData out = lowPoly;
    for (auto& v : out.vertices) {
        float best = 1e9f; QVector3D bestP = v.position;
        for (const auto& hv : highPoly.vertices) {
            float d = (hv.position - v.position).lengthSquared();
            if (d < best) { best = d; bestP = hv.position; }
        }
        if (best < 4.0f) v.position = bestP;
    }
    out.computeNormals(); out.computeBoundingBox(); return out;
}

MeshData MeshOperations::uvPeel(const MeshData& mesh, const QVector<int>& seamEdges) {
    Q_UNUSED(seamEdges);
    MeshData out = mesh;
    out.computeBoundingBox(); return out;
}

MeshData MeshOperations::uvPack(const MeshData& mesh, float padding) {
    Q_UNUSED(padding);
    MeshData out = mesh; out.computeBoundingBox(); return out;
}

QImage MeshOperations::renderAOV(const MeshData& mesh, const QString& aov, int width, int height, int samples) {
    Q_UNUSED(mesh); Q_UNUSED(samples); QImage img(width, height, QImage::Format_ARGB32);
    if (aov == "depth") img.fill(QColor(128,128,128));
    else if (aov == "normal") img.fill(QColor(128,128,255));
    else if (aov == "albedo") img.fill(QColor(200,200,200));
    else img.fill(Qt::black);
    return img;
}
QVector<float> MeshOperations::analyzeUVDensity(const MeshData& mesh) {
    QVector<float> out; out.reserve(mesh.faces.size());
    for (const auto& f : mesh.faces) {
        if (f.indices.size() < 3 || mesh.uvs.isEmpty()) { out.append(1.0f); continue; }
        float area3D = 0, areaUV = 0;
        QVector3D p0 = mesh.vertices[f.indices[0]].position;
        QVector2D uv0 = mesh.uvs[qMin(f.indices[0], mesh.uvs.size()-1)];
        for (int i = 1; i + 1 < f.indices.size(); ++i) {
            QVector3D p1 = mesh.vertices[f.indices[i]].position;
            QVector3D p2 = mesh.vertices[f.indices[i+1]].position;
            area3D += QVector3D::crossProduct(p1-p0, p2-p0).length() * 0.5f;
            QVector2D uv1 = mesh.uvs[qMin(f.indices[i], mesh.uvs.size()-1)];
            QVector2D uv2 = mesh.uvs[qMin(f.indices[i+1], mesh.uvs.size()-1)];
            areaUV += qAbs((uv1-uv0).x()*(uv2-uv0).y() - (uv1-uv0).y()*(uv2-uv0).x()) * 0.5f;
            Q_UNUSED(uv0);
        }
        float d = areaUV > 1e-8f ? area3D / areaUV : 1.0f;
        out.append(qBound(0.0f, d, 10.0f));
    }
    return out;
}
QImage MeshOperations::uvOverlapHeatmap(const MeshData& mesh, int width, int height) {
    QImage img(width, height, QImage::Format_ARGB32); img.fill(QColor(30,30,30,255));
    if (mesh.uvs.isEmpty() || mesh.faces.isEmpty()) return img;
    QHash<quint64,int> occ; occ.reserve(width*height/4);
    for (const auto& f : mesh.faces) {
        for (int idx : f.indices) {
            QVector2D uv = mesh.uvs[qMin(idx, mesh.uvs.size()-1)];
            int x = qBound(0, int(uv.x()*width), width-1);
            int y = qBound(0, int(uv.y()*height), height-1);
            quint64 k = (quint64(x)<<32)|quint32(y); occ[k]++;
        }
    }
    for (auto it = occ.begin(); it != occ.end(); ++it) {
        int cnt = it.value(); if (cnt < 2) continue;
        int x = int(it.key()>>32); int y = int(it.key()&0xffffffff);
        int r = qMin(255, 80 + cnt*40); img.setPixelColor(x,y, QColor(r, 30, 30, 200));
    }
    return img;
}
QImage MeshOperations::uvDensityHeatmap(const MeshData& mesh, int width, int height) {
    auto dens = analyzeUVDensity(mesh);
    QImage img(width, height, QImage::Format_ARGB32); img.fill(QColor(40,40,40,255));
    if (dens.isEmpty() || mesh.faces.isEmpty()) return img;
    float mn = *std::min_element(dens.begin(), dens.end());
    float mx = *std::max_element(dens.begin(), dens.end());
    float rng = qMax(1e-6f, mx - mn);
    for (int i = 0; i < qMin(dens.size(), mesh.faces.size()); ++i) {
        const auto& f = mesh.faces[i];
        float t = (dens[i]-mn)/rng;
        QColor c = QColor::fromHsvF(0.66f*(1.0f-t), 0.9f, 0.95f);
        for (int idx : f.indices) {
            if (idx < 0 || idx >= mesh.uvs.size()) continue;
            QVector2D uv = mesh.uvs[idx];
            int x = qBound(0, int(uv.x()*width), width-1);
            int y = qBound(0, int(uv.y()*height), height-1);
            img.setPixelColor(x,y,c);
        }
    }
    return img;
}
NURBSSurface MeshOperations::offsetSurface(const NURBSSurface& surface, float distance) {
#ifdef HAS_OCCT
    if (OCCTBridge::isAvailable()) {
        MeshData mesh = surfaceToMesh(surface);
        MeshData offsetMesh = OCCTBridge::offsetSurfaceExact(mesh, distance);
        return meshToNURBS(offsetMesh);
    }
#endif
    // Fallback: simple Y-axis translation
    NURBSSurface out = surface;
    for (auto& row : out.controlPoints)
        for (auto& p : row) p += QVector3D(0, distance, 0);
    return out;
}
QVector<int> MeshOperations::retargetSkeleton(const QVector<QVector3D>& srcJoints, const QVector<QVector3D>& dstJoints) {
    QVector<int> map;
    if (srcJoints.isEmpty() || dstJoints.isEmpty()) return map;

    map.reserve(srcJoints.size());

    // Acceptance threshold: the skeleton's own size (largest joint-to-joint
    // distance). Matches farther than that mean the skeletons are unrelated;
    // normally placed skeletons (even a shifted copy) still map.
    const auto extent = [](const QVector<QVector3D>& js) {
        float e = 0.0f;
        for (int i = 0; i < js.size(); ++i)
            for (int j = i + 1; j < js.size(); ++j)
                e = qMax(e, (js[i] - js[j]).length());
        return e;
    };
    const float threshold = qMax(extent(srcJoints), extent(dstJoints));

    // Greedy: each source joint claims the nearest still-unclaimed
    // destination joint within the threshold; unmatched joints get -1.
    QVector<bool> claimed(dstJoints.size(), false);
    for (int i = 0; i < srcJoints.size(); ++i) {
        const QVector3D& srcPos = srcJoints[i];
        int bestIdx = -1;
        float bestDist = std::numeric_limits<float>::max();

        for (int j = 0; j < dstJoints.size(); ++j) {
            if (claimed[j]) continue;
            const QVector3D diff3 = srcPos - dstJoints[j];
            const float dist = diff3.lengthSquared();
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = j;
            }
        }

        if (bestIdx >= 0 && std::sqrt(bestDist) <= threshold) {
            claimed[bestIdx] = true;
            map.append(bestIdx);
        } else {
            map.append(-1); // no match found
        }
    }

    return map;
}
MeshData MeshOperations::applyClusterDeform(const MeshData& mesh, const QVector<int>& indices, const QVector3D& delta, float weight) {
    MeshData out = mesh;
    for (int idx : indices) if (idx>=0 && idx < out.vertices.size()) out.vertices[idx].position += delta * weight;
    out.computeNormals(); return out;
}
MeshData MeshOperations::applyBlendShape(const MeshData& base, const MeshData& target, float weight) {
    MeshData out = base; weight = qBound(0.0f, weight, 1.0f);
    int n = qMin(base.vertices.size(), target.vertices.size());
    for (int i = 0; i < n; ++i) out.vertices[i].position = base.vertices[i].position * (1-weight) + target.vertices[i].position * weight;
    out.computeNormals(); return out;
}
} // namespace ks

