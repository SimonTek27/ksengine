#include "CollisionMeshGenerator.h"
#include "../FileFormat/MeshData.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <map>
#include <QTextStream>
#include <cstring>
#include <algorithm>
#include <limits>
#include <cmath>
#if HAS_VHACD
#include <VHACD.h>
#endif

namespace ks {
namespace tools {

CollisionMeshGenerator::CollisionMeshGenerator()
{
}

CollisionMeshResult CollisionMeshGenerator::generate(const QVector<QVector3D>& vertices,
                                                      const QVector<int>& indices,
                                                      const CollisionMeshOptions& options)
{
    CollisionMeshResult result;

    if (vertices.isEmpty() || indices.isEmpty()) {
        result.error = "Invalid input: empty vertices or indices";
        result.success = false;
        return result;
    }

    switch (options.type) {
        case CollisionMeshOptions::Type::ConvexHull:
            result = generateConvexHull(vertices, indices, options);
            break;
        case CollisionMeshOptions::Type::VHACD:
            result = generateVHACD(vertices, indices, options);
            break;
        case CollisionMeshOptions::Type::Box:
        case CollisionMeshOptions::Type::Sphere:
        case CollisionMeshOptions::Type::Capsule:
        case CollisionMeshOptions::Type::Cylinder:
            result = generatePrimitive(vertices, indices, options);
            break;
        case CollisionMeshOptions::Type::Compound:
            result.error = "Compound generation requires generateCompound()";
            result.success = false;
            break;
    }

    if (result.success && options.simplify) {
        for (auto& hull : result.hulls) {
            simplifyMesh(hull.vertices, hull.indices, options.simplifyThreshold);
        }
    }

    return result;
}

static bool parseOBJ(const QByteArray& data, QVector<QVector3D>& outVerts, QVector<int>& outIndices)
{
    QTextStream in(data);
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> texcoords;
    QVector<QVector<int>> faceVerts;  // {pos/norm/tex indices}

    auto parseTriplet = [](const QString& s) -> QVector<int> {
        QStringList parts = s.split('/');
        QVector<int> res;
        for (const auto& p : parts) {
            bool ok = false;
            int v = p.toInt(&ok);
            res.append(ok ? v : 0);
        }
        while (res.size() < 3) res.append(0);
        return res;
    };

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        if (line.startsWith("v ")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 4) {
                positions.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
            }
        } else if (line.startsWith("f ")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() < 4) continue;  // Need at least 3 vertices
            parts.removeFirst();
            // Triangulate: fan triangulation for n-gons
            QVector<QVector<int>> verts;
            for (const auto& p : parts) {
                verts.append(parseTriplet(p));
            }
            for (int i = 1; i < verts.size() - 1; ++i) {
                faceVerts.append(verts[0]);
                faceVerts.append(verts[i]);
                faceVerts.append(verts[i+1]);
            }
        }
    }

    if (positions.isEmpty()) return false;

    // Build unique vertex list
    struct VertKey { int pos, norm, tex; };
    struct VertKeyLess {
        bool operator()(const VertKey& a, const VertKey& b) const {
            if (a.pos != b.pos) return a.pos < b.pos;
            if (a.norm != b.norm) return a.norm < b.norm;
            return a.tex < b.tex;
        }
    };
    std::map<VertKey, int, VertKeyLess> vertMap;

    for (const auto& fv : faceVerts) {
        VertKey key{fv[0] - 1, fv[2] - 1, fv[1] - 1};  // OBJ: pos/tex/norm
        if (key.pos < 0 || key.pos >= positions.size()) continue;
        auto it = vertMap.find(key);
        if (it == vertMap.end()) {
            int idx = outVerts.size();
            vertMap[key] = idx;
            outVerts.append(positions[key.pos]);
            outIndices.append(idx);
        } else {
            outIndices.append(it->second);
        }
    }

    if (outIndices.size() % 3 != 0) outIndices.resize(outIndices.size() - (outIndices.size() % 3));
    return !outVerts.isEmpty() && !outIndices.isEmpty();
}

static bool parseSTL(const QByteArray& data, QVector<QVector3D>& outVerts, QVector<int>& outIndices)
{
    // Check for ASCII STL (starts with "solid")
    QString header = QString::fromUtf8(data.left(80)).trimmed().toLower();
    if (header.startsWith("solid ")) {
        // ASCII STL
        QTextStream in(data);
        QVector<QVector3D> positions;
        QVector<int> tris;
        bool readingFacet = false;

        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;

            if (line.startsWith("facet normal ")) {
                readingFacet = true;
            } else if (line.startsWith("vertex ") && readingFacet) {
                QStringList parts = line.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 4) {
                    positions.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
                }
            } else if (line.startsWith("endfacet")) {
                readingFacet = false;
                int n = positions.size();
                if (n >= 3) {
                    // Deduplicate by position
                    struct Vec3Key { float x, y, z; };
                    auto vecLess = [](const Vec3Key& a, const Vec3Key& b) {
                        if (a.x != b.x) return a.x < b.x;
                        if (a.y != b.y) return a.y < b.y;
                        return a.z < b.z;
                    };
                    struct Vec3Compare {
                        bool operator()(const Vec3Key& a, const Vec3Key& b) const {
                            if (a.x != b.x) return a.x < b.x;
                            if (a.y != b.y) return a.y < b.y;
                            return a.z < b.z;
                        }
                    };
                    std::map<Vec3Key, int, Vec3Compare> vertMap;
                    for (int i = n - 3; i < n; ++i) {
                        const QVector3D& p = positions[i];
                        Vec3Key key{p.x(), p.y(), p.z()};
                        auto it = vertMap.find(key);
                        if (it == vertMap.end()) {
                            int idx = outVerts.size();
                            vertMap[key] = idx;
                            outVerts.append(p);
                            outIndices.append(idx);
                        } else {
                            outIndices.append(it->second);
                        }
                    }
                    tris.append(1);
                }
            }
        }
    } else {
        // Binary STL: 80-byte header, 4-byte count, then 50-byte triangles
        const unsigned char* buf = reinterpret_cast<const unsigned char*>(data.constData());
        int dataSize = data.size();
        if (dataSize < 84) return false;
        unsigned int triCount;
        std::memcpy(&triCount, buf + 80, 4);

        int offset = 84;
        struct Vec3Key { float x, y, z; };
        struct Vec3Compare {
            bool operator()(const Vec3Key& a, const Vec3Key& b) const {
                if (a.x != b.x) return a.x < b.x;
                if (a.y != b.y) return a.y < b.y;
                return a.z < b.z;
            }
        };
        std::map<Vec3Key, int, Vec3Compare> vertMap;
        for (unsigned int t = 0; t < triCount && offset + 50 <= dataSize; ++t) {
            offset += 12;  // Skip normal
            for (int v = 0; v < 3; ++v) {
                float x, y, z;
                std::memcpy(&x, buf + offset, 4); offset += 4;
                std::memcpy(&y, buf + offset, 4); offset += 4;
                std::memcpy(&z, buf + offset, 4); offset += 4;
                Vec3Key key{x, y, z};
                auto it = vertMap.find(key);
                if (it == vertMap.end()) {
                    int idx = outVerts.size();
                    vertMap[key] = idx;
                    outVerts.append(QVector3D(x, y, z));
                    outIndices.append(idx);
                } else {
                    outIndices.append(it->second);
                }
            }
            offset += 2;  // Skip attribute byte count
        }
    }

    if (outIndices.size() % 3 != 0) outIndices.resize(outIndices.size() - (outIndices.size() % 3));
    return !outVerts.isEmpty() && !outIndices.isEmpty();
}

CollisionMeshResult CollisionMeshGenerator::generateFromFile(const QString& filePath,
                                                               const CollisionMeshOptions& options)
{
    CollisionMeshResult result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = "Failed to open file: " + filePath;
        result.success = false;
        return result;
    }
    QByteArray data = file.readAll();
    file.close();

    QVector<QVector3D> verts;
    QVector<int> indices;
    QString ext = QFileInfo(filePath).suffix().toLower();

    bool parsed = false;
    if (ext == "obj") {
        parsed = parseOBJ(data, verts, indices);
    } else if (ext == "stl") {
        parsed = parseSTL(data, verts, indices);
    } else {
        result.error = "Unsupported mesh format: " + ext + " (supported: .obj, .stl)";
        result.success = false;
        return result;
    }

    if (!parsed) {
        result.error = "Failed to parse " + ext + " file: " + filePath;
        result.success = false;
        return result;
    }

    result = generate(verts, indices, options);
    if (!result.success)
        result.error = "File parsed (" + QString::number(verts.size()) + " verts, " +
                       QString::number(indices.size() / 3) + " tris) but collision generation failed: " + result.error;
    return result;
}

CollisionMeshResult CollisionMeshGenerator::generateCompound(const QMap<QString, QPair<QVector<QVector3D>, QVector<int>>>& meshGroups,
                                                              const CollisionMeshOptions& options)
{
    CollisionMeshResult result;
    result.success = true;

    for (auto it = meshGroups.begin(); it != meshGroups.end(); ++it) {
        CollisionMeshResult groupResult = generate(it.value().first, it.value().second, options);
        if (groupResult.success) {
            for (const auto& hull : groupResult.hulls) {
                result.hulls.append(hull);
                result.totalVolume += hull.volume;
                result.totalTriangles += hull.indices.size() / 3;
            }
        } else {
            result.success = false;
            result.error = "Failed for group " + it.key() + ": " + groupResult.error;
            break;
        }
    }

    return result;
}

QVector<int> CollisionMeshGenerator::computeConvexHull(const QVector<QVector3D>& points)
{
    QVector<int> hull;
    if (points.size() < 4) {
        for (int i = 0; i < points.size(); ++i) hull.append(i);
        return hull;
    }

    struct Face {
        int a, b, c;
        QVector3D normal;
        float offset;
        bool valid = true;
    };

    auto cross = [](const QVector3D& a, const QVector3D& b) -> QVector3D {
        return QVector3D::crossProduct(a, b);
    };

    auto dot = [](const QVector3D& a, const QVector3D& b) -> float {
        return QVector3D::dotProduct(a, b);
    };

    QVector<Face> faces;

    int i0 = 0;
    int i1 = -1;
    float maxDist = 0;
    for (int i = 1; i < points.size(); ++i) {
        float d = (points[i] - points[i0]).lengthSquared();
        if (d > maxDist) {
            maxDist = d;
            i1 = i;
        }
    }
    if (i1 == -1) return hull;

    int i2 = -1;
    maxDist = 0;
    QVector3D dir1 = points[i1] - points[i0];
    for (int i = 0; i < points.size(); ++i) {
        if (i == i0 || i == i1) continue;
        QVector3D dir2 = points[i] - points[i0];
        float d = cross(dir1, dir2).lengthSquared();
        if (d > maxDist) {
            maxDist = d;
            i2 = i;
        }
    }
    if (i2 == -1) return hull;

    int i3 = -1;
    maxDist = 0;
    QVector3D normal = cross(points[i1] - points[i0], points[i2] - points[i0]).normalized();
    for (int i = 0; i < points.size(); ++i) {
        if (i == i0 || i == i1 || i == i2) continue;
        float d = std::abs(dot(points[i] - points[i0], normal));
        if (d > maxDist) {
            maxDist = d;
            i3 = i;
        }
    }
    if (i3 == -1) return hull;

    if (dot(points[i3] - points[i0], normal) > 0) {
        std::swap(i2, i3);
        normal = -normal;
    }

    auto addFace = [&](int a, int b, int c) {
        QVector3D n = cross(points[b] - points[a], points[c] - points[a]).normalized();
        faces.append({a, b, c, n, -dot(n, points[a])});
    };

    addFace(i0, i1, i2);
    addFace(i0, i3, i1);
    addFace(i0, i2, i3);
    addFace(i1, i3, i2);

    for (int i = 0; i < points.size(); ++i) {
        if (i == i0 || i == i1 || i == i2 || i == i3) continue;

        QVector<int> visibleFaces;
        for (int f = 0; f < faces.size(); ++f) {
            if (faces[f].valid && dot(faces[f].normal, points[i]) + faces[f].offset > 1e-6f) {
                visibleFaces.append(f);
            }
        }

        if (visibleFaces.isEmpty()) continue;

        QMap<QPair<int, int>, int> edgeCount;
        for (int fIdx : visibleFaces) {
            Face& face = faces[fIdx];
            QPair<int, int> edges[3] = {{face.a, face.b}, {face.b, face.c}, {face.c, face.a}};
            for (auto& e : edges) {
                if (e.first > e.second) std::swap(e.first, e.second);
                edgeCount[e]++;
            }
            face.valid = false;
        }

        for (auto it = edgeCount.begin(); it != edgeCount.end(); ++it) {
            if (it.value() == 1) {
                int a = it.key().first;
                int b = it.key().second;
                addFace(a, b, i);
            }
        }
    }

    for (const auto& face : faces) {
        if (face.valid) {
            hull.append(face.a);
            hull.append(face.b);
            hull.append(face.c);
        }
    }

    return hull;
}

void CollisionMeshGenerator::simplifyMesh(QVector<QVector3D>& vertices,
                                           QVector<int>& indices,
                                           float threshold)
{
    if (vertices.size() < 4 || indices.size() < 3) return;

    struct Quadric {
        double a[10] = {0};
        Quadric& add(const Quadric& o) {
            for (int i = 0; i < 10; ++i) a[i] += o.a[i];
            return *this;
        }
        double error(const QVector3D& v) const {
            double x = v.x(), y = v.y(), z = v.z();
            return a[0]*x*x + 2*a[1]*x*y + 2*a[2]*x*z + 2*a[3]*x
                 + a[4]*y*y + 2*a[5]*y*z + 2*a[6]*y
                 + a[7]*z*z + 2*a[8]*z + a[9];
        }
    };

    int nVerts = vertices.size();
    QVector<Quadric> Q(nVerts);

    for (int i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size()) break;
        int ia = indices[i], ib = indices[i+1], ic = indices[i+2];
        if (ia >= nVerts || ib >= nVerts || ic >= nVerts) continue;

        QVector3D p0 = vertices[ia];
        QVector3D p1 = vertices[ib];
        QVector3D p2 = vertices[ic];
        QVector3D n = QVector3D::crossProduct(p1 - p0, p2 - p0);
        float len = n.length();
        if (len < 1e-8f) continue;
        n /= len;
        double d = -QVector3D::dotProduct(n, p0);

        Quadric Qf;
        Qf.a[0] = n.x() * n.x();
        Qf.a[1] = n.x() * n.y();
        Qf.a[2] = n.x() * n.z();
        Qf.a[3] = n.x() * d;
        Qf.a[4] = n.y() * n.y();
        Qf.a[5] = n.y() * n.z();
        Qf.a[6] = n.y() * d;
        Qf.a[7] = n.z() * n.z();
        Qf.a[8] = n.z() * d;
        Qf.a[9] = d * d;

        Q[ia].add(Qf);
        Q[ib].add(Qf);
        Q[ic].add(Qf);
    }

    struct EdgeCollapse { int v0, v1; double cost; QVector3D pos; };
    QVector<EdgeCollapse> collapses;
    QMap<QPair<int, int>, int> edgeMap;

    for (int i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size()) break;
        int idx[3] = {indices[i], indices[i+1], indices[i+2]};
        for (int e = 0; e < 3; ++e) {
            int a = idx[e], b = idx[(e+1)%3];
            if (a > b) std::swap(a, b);
            QPair<int, int> key(a, b);
            if (edgeMap.contains(key)) continue;
            edgeMap[key] = collapses.size();

            Quadric Qe = Q[a];
            Qe.add(Q[b]);
            QVector3D pa = vertices[a], pb = vertices[b];
            QVector3D mid = (pa + pb) * 0.5f;
            double cost = Qe.error(mid);
            collapses.append({a, b, cost, mid});
        }
    }

    std::sort(collapses.begin(), collapses.end(),
              [](const EdgeCollapse& a, const EdgeCollapse& b) { return a.cost < b.cost; });

    QVector<int> parent(nVerts);
    for (int i = 0; i < nVerts; ++i) parent[i] = i;

    auto find = [&](auto&& self, int v) -> int {
        while (parent[v] != v) v = parent[v] = parent[parent[v]];
        return v;
    };

    int targetVerts = qMax(4, static_cast<int>(nVerts * threshold));
    int removed = 0;

    for (const auto& c : collapses) {
        if (nVerts - removed <= targetVerts) break;
        int a = find(find, c.v0);
        int b = find(find, c.v1);
        if (a == b) continue;
        vertices[a] = c.pos;
        parent[b] = a;
        Q[a].add(Q[b]);
        removed++;
    }

    QVector<int> remap(nVerts, -1);
    int newIdx = 0;
    for (int i = 0; i < nVerts; ++i) {
        int root = find(find, i);
        if (remap[root] == -1) remap[root] = newIdx++;
    }

    QVector<QVector3D> newVerts(newIdx);
    for (int i = 0; i < nVerts; ++i) {
        int root = find(find, i);
        if (remap[root] >= 0 && remap[root] < newIdx) {
            newVerts[remap[root]] = vertices[i];
        }
    }

    QVector<int> newIndices;
    newIndices.reserve(indices.size());
    for (int idx : indices) {
        int root = find(find, idx);
        if (remap[root] >= 0) newIndices.append(remap[root]);
    }

    vertices = newVerts;
    indices = newIndices;
}

QMatrix4x4 CollisionMeshGenerator::computeOBB(const QVector<QVector3D>& vertices,
                                               const QVector<int>& indices)
{
    Q_UNUSED(indices);
    QMatrix4x4 obb;
    if (vertices.isEmpty()) return obb;

    QVector3D center;
    for (const auto& v : vertices) center += v;
    center /= vertices.size();

    double cov[3][3] = {{0}};
    for (const auto& v : vertices) {
        QVector3D d = v - center;
        cov[0][0] += d.x() * d.x();
        cov[0][1] += d.x() * d.y();
        cov[0][2] += d.x() * d.z();
        cov[1][0] += d.y() * d.x();
        cov[1][1] += d.y() * d.y();
        cov[1][2] += d.y() * d.z();
        cov[2][0] += d.z() * d.x();
        cov[2][1] += d.z() * d.y();
        cov[2][2] += d.z() * d.z();
    }

    double eval[3] = {0}, evec[3][3] = {{0}};
    evec[0][0] = evec[1][1] = evec[2][2] = 1.0;

    for (int iter = 0; iter < 50; ++iter) {
        int p = 0, q = 1;
        double maxVal = std::abs(cov[0][1]);
        for (int i = 0; i < 3; ++i) {
            for (int j = i + 1; j < 3; ++j) {
                if (std::abs(cov[i][j]) > maxVal) {
                    maxVal = std::abs(cov[i][j]);
                    p = i; q = j;
                }
            }
        }
        if (maxVal < 1e-10) break;

        double tau = (cov[q][q] - cov[p][p]) / (2 * cov[p][q]);
        double t = std::copysign(1.0 / (std::abs(tau) + std::sqrt(1 + tau * tau)), tau);
        double c = 1 / std::sqrt(1 + t * t);
        double s = t * c;

        for (int i = 0; i < 3; ++i) {
            if (i != p && i != q) {
                double aip = cov[i][p], aiq = cov[i][q];
                cov[i][p] = cov[p][i] = c * aip - s * aiq;
                cov[i][q] = cov[q][i] = s * aip + c * aiq;
            }
        }
        double app = cov[p][p], aqq = cov[q][q], apq = cov[p][q];
        cov[p][p] = c * c * app - 2 * c * s * apq + s * s * aqq;
        cov[q][q] = s * s * app + 2 * c * s * apq + c * c * aqq;
        cov[p][q] = cov[q][p] = 0;

        for (int i = 0; i < 3; ++i) {
            double eip = evec[i][p], eiq = evec[i][q];
            evec[i][p] = c * eip - s * eiq;
            evec[i][q] = s * eip + c * eiq;
        }
    }

    for (int i = 0; i < 3; ++i) eval[i] = cov[i][i];

    QVector3D axes[3];
    for (int i = 0; i < 3; ++i) {
        axes[i] = QVector3D(evec[0][i], evec[1][i], evec[2][i]).normalized();
    }

    float minProj[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float maxProj[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

    for (const auto& v : vertices) {
        QVector3D d = v - center;
        for (int i = 0; i < 3; ++i) {
            float proj = QVector3D::dotProduct(d, axes[i]);
            minProj[i] = qMin(minProj[i], proj);
            maxProj[i] = qMax(maxProj[i], proj);
        }
    }

    QVector3D halfExtents = QVector3D(
        (maxProj[0] - minProj[0]) * 0.5f,
        (maxProj[1] - minProj[1]) * 0.5f,
        (maxProj[2] - minProj[2]) * 0.5f
    );

    QVector3D obbCenter = center;
    for (int i = 0; i < 3; ++i) {
        obbCenter += axes[i] * ((maxProj[i] + minProj[i]) * 0.5f);
    }

    obb.setToIdentity();
    for (int i = 0; i < 3; ++i) {
        obb.setColumn(i, QVector4D(axes[i].x() * halfExtents[i], axes[i].y() * halfExtents[i], axes[i].z() * halfExtents[i], 0));
    }
    obb.setColumn(3, QVector4D(obbCenter.x(), obbCenter.y(), obbCenter.z(), 1));

    return obb;
}

CollisionMeshResult CollisionMeshGenerator::generateConvexHull(const QVector<QVector3D>& vertices,
                                                                const QVector<int>& indices,
                                                                const CollisionMeshOptions& options)
{
    Q_UNUSED(options);
    CollisionMeshResult result;
    result.success = true;

    QVector<QVector3D> uniqueVerts = vertices;
    QVector<int> hullIndices = computeConvexHull(uniqueVerts);

    if (hullIndices.size() < 3) {
        result.success = false;
        result.error = "Convex hull generation failed - insufficient vertices";
        return result;
    }

    CollisionHull hull;
    hull.vertices = uniqueVerts;
    hull.indices = hullIndices;

    QVector<QVector3D> normals;
    normals.reserve(hullIndices.size() / 3);
    for (int i = 0; i < hullIndices.size(); i += 3) {
        if (i + 2 >= hullIndices.size()) break;
        QVector3D p0 = uniqueVerts[hullIndices[i]];
        QVector3D p1 = uniqueVerts[hullIndices[i+1]];
        QVector3D p2 = uniqueVerts[hullIndices[i+2]];
        QVector3D n = QVector3D::crossProduct(p1 - p0, p2 - p0).normalized();
        normals.append(n);
    }
    hull.normals = normals;

    hull.volume = computeHullVolume(hull.vertices, hull.indices);
    hull.localTransform.setToIdentity();

    result.hulls.append(hull);
    result.totalVolume = hull.volume;
    result.totalTriangles = hull.indices.size() / 3;

    return result;
}

CollisionMeshResult CollisionMeshGenerator::generateVHACD(const QVector<QVector3D>& vertices,
                                                           const QVector<int>& indices,
                                                           const CollisionMeshOptions& options)
{
#if HAS_VHACD
    // VHACD library is available — perform convex decomposition
    VHACD::IVHACD::Parameters params;
    params.m_resolution = (options.maxHulls > 0) ? 100000 * options.maxHulls : 100000;
    params.m_maxNumVerticesPerCH = static_cast<unsigned int>(options.maxVerticesPerHull);
    params.m_concavity = options.concavity;
    params.m_minVolumePerCH = options.volumeThreshold;

    VHACD::IVHACD* vhacd = VHACD::CreateVHACD();
    QVector<double> doubleVerts;
    doubleVerts.reserve(vertices.size() * 3);
    for (const auto& v : vertices) {
        doubleVerts << static_cast<double>(v.x()) << static_cast<double>(v.y()) << static_cast<double>(v.z());
    }
    bool computeOk = vhacd->Compute(
        doubleVerts.constData(),
        sizeof(double),
        static_cast<unsigned int>(vertices.size()),
        indices.constData(),
        sizeof(int),
        static_cast<unsigned int>(indices.size()) / 3,
        params);

    CollisionMeshResult result;
    result.success = computeOk;

    if (computeOk) {
        unsigned int nHulls = vhacd->GetNConvexHulls();
        for (unsigned int h = 0; h < nHulls; ++h) {
            VHACD::IVHACD::ConvexHull ch;
            vhacd->GetConvexHull(h, ch);
            CollisionHull hull;
            int nPts = static_cast<int>(ch.m_nPoints);
            hull.vertices.resize(nPts);
            for (int i = 0; i < nPts; ++i) {
                hull.vertices[i] = QVector3D(
                    static_cast<float>(ch.m_points[i * 3]),
                    static_cast<float>(ch.m_points[i * 3 + 1]),
                    static_cast<float>(ch.m_points[i * 3 + 2]));
            }
            int nTris = static_cast<int>(ch.m_nTriangles);
            hull.indices.reserve(nTris * 3);
            for (int i = 0; i < nTris * 3; ++i) {
                hull.indices.append(ch.m_triangles[i]);
            }
            hull.volume = computeHullVolume(hull.vertices, hull.indices);
            hull.localTransform.setToIdentity();
            result.hulls.append(hull);
            result.totalVolume += hull.volume;
            result.totalTriangles += hull.indices.size() / 3;
        }
    } else {
        result.error = "VHACD decomposition failed";
    }

    vhacd->Release();
    return result;
#else
    CollisionMeshResult result;
    result.success = false;
    result.error = "VHACD not available - requires Bullet Physics with VHACD";
    return result;
#endif
}

CollisionMeshResult CollisionMeshGenerator::generatePrimitive(const QVector<QVector3D>& vertices,
                                                               const QVector<int>& indices,
                                                               const CollisionMeshOptions& options)
{
    Q_UNUSED(indices);
    CollisionMeshResult result;
    result.success = true;

    QVector3D minBounds(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D maxBounds(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

    for (const auto& v : vertices) {
        minBounds = QVector3D(qMin(minBounds.x(), v.x()), qMin(minBounds.y(), v.y()), qMin(minBounds.z(), v.z()));
        maxBounds = QVector3D(qMax(maxBounds.x(), v.x()), qMax(maxBounds.y(), v.y()), qMax(maxBounds.z(), v.z()));
    }

    QVector3D center = (minBounds + maxBounds) * 0.5f;
    QVector3D halfExtents = (maxBounds - minBounds) * 0.5f;
    float maxExtent = qMax(qMax(halfExtents.x(), halfExtents.y()), halfExtents.z());

    CollisionHull hull;
    hull.localTransform.setToIdentity();
    hull.localTransform.translate(center);

    switch (options.type) {
        case CollisionMeshOptions::Type::Box: {
            hull.vertices = {
                QVector3D(-halfExtents.x(), -halfExtents.y(), -halfExtents.z()),
                QVector3D( halfExtents.x(), -halfExtents.y(), -halfExtents.z()),
                QVector3D( halfExtents.x(),  halfExtents.y(), -halfExtents.z()),
                QVector3D(-halfExtents.x(),  halfExtents.y(), -halfExtents.z()),
                QVector3D(-halfExtents.x(), -halfExtents.y(),  halfExtents.z()),
                QVector3D( halfExtents.x(), -halfExtents.y(),  halfExtents.z()),
                QVector3D( halfExtents.x(),  halfExtents.y(),  halfExtents.z()),
                QVector3D(-halfExtents.x(),  halfExtents.y(),  halfExtents.z())
            };
            hull.indices = {
                0,1,2, 2,3,0,  4,5,6, 6,7,4,
                0,1,5, 5,4,0,  2,3,7, 7,6,2,
                0,3,7, 7,4,0,  1,2,6, 6,5,1
            };
            for (auto& v : hull.vertices) v = hull.localTransform.map(v);
            break;
        }
        case CollisionMeshOptions::Type::Sphere: {
            int segments = 16;
            int rings = 8;
            float radius = maxExtent;
            for (int r = 0; r <= rings; ++r) {
                float phi = M_PI * r / rings;
                for (int s = 0; s <= segments; ++s) {
                    float theta = 2 * M_PI * s / segments;
                    hull.vertices.append(QVector3D(
                        radius * qSin(phi) * qCos(theta),
                        radius * qCos(phi),
                        radius * qSin(phi) * qSin(theta)
                    ));
                }
            }
            for (int r = 0; r < rings; ++r) {
                for (int s = 0; s < segments; ++s) {
                    int i0 = r * (segments + 1) + s;
                    int i1 = i0 + 1;
                    int i2 = (r + 1) * (segments + 1) + s;
                    int i3 = i2 + 1;
                    hull.indices.append(i0); hull.indices.append(i2); hull.indices.append(i1);
                    hull.indices.append(i1); hull.indices.append(i2); hull.indices.append(i3);
                }
            }
            for (auto& v : hull.vertices) v = hull.localTransform.map(v);
            break;
        }
        case CollisionMeshOptions::Type::Capsule: {
            float radius = qMax(halfExtents.x(), halfExtents.z());
            float halfHeight = halfExtents.y();
            int segments = 16;
            QVector3D offsets[2] = {QVector3D(0, halfHeight, 0), QVector3D(0, -halfHeight, 0)};
            for (int hemi = 0; hemi < 2; ++hemi) {
                int baseIdx = hull.vertices.size();
                for (int s = 0; s <= segments; ++s) {
                    float theta = 2 * M_PI * s / segments;
                    for (int r = 0; r <= segments / 2; ++r) {
                        float phi = M_PI * r / (segments / 2);
                        if (hemi == 1) phi = M_PI - phi;
                        hull.vertices.append(QVector3D(
                            radius * qSin(phi) * qCos(theta),
                            radius * qCos(phi),
                            radius * qSin(phi) * qSin(theta)
                        ) + offsets[hemi]);
                    }
                }
            }
            break;
        }
        case CollisionMeshOptions::Type::Cylinder: {
            float radius = qMax(halfExtents.x(), halfExtents.z());
            float halfHeight = halfExtents.y();
            int segments = 16;
            int baseIdx = 0;
            for (int cap = 0; cap < 2; ++cap) {
                float y = (cap == 0) ? -halfHeight : halfHeight;
                int centerIdx = hull.vertices.size();
                hull.vertices.append(QVector3D(0, y, 0));
                for (int s = 0; s <= segments; ++s) {
                    float theta = 2 * M_PI * s / segments;
                    hull.vertices.append(QVector3D(radius * qCos(theta), y, radius * qSin(theta)));
                }
            }
            for (int s = 0; s < segments; ++s) {
                int b0 = s + 1, b1 = (s + 1) % segments + 1;
                int t0 = segments + 2 + s + 1, t1 = segments + 2 + (s + 1) % segments + 1;
                hull.indices.append(b0); hull.indices.append(t0); hull.indices.append(b1);
                hull.indices.append(b1); hull.indices.append(t0); hull.indices.append(t1);
            }
            for (auto& v : hull.vertices) v = hull.localTransform.map(v);
            break;
        }
        default:
            result = generateConvexHull(vertices, indices, options);
            return result;
    }

    hull.volume = computeHullVolume(hull.vertices, hull.indices);
    hull.localTransform.setToIdentity();
    hull.localTransform.translate(center);

    result.hulls.append(hull);
    result.totalVolume = hull.volume;
    result.totalTriangles = hull.indices.size() / 3;

    return result;
}

QVector<QVector3D> CollisionMeshGenerator::computeHullVertices(const QVector<int>& hullIndices,
                                                                const QVector<QVector3D>& allVertices)
{
    QVector<QVector3D> result;
    QSet<int> uniqueIndices;
    for (int idx : hullIndices) {
        if (idx >= 0 && idx < allVertices.size() && !uniqueIndices.contains(idx)) {
            uniqueIndices.insert(idx);
            result.append(allVertices[idx]);
        }
    }
    return result;
}

float CollisionMeshGenerator::computeHullVolume(const QVector<QVector3D>& hullVerts,
                                                 const QVector<int>& hullIndices)
{
    float volume = 0.0f;
    if (hullVerts.size() < 4 || hullIndices.size() < 3) return 0.0f;

    QVector3D ref(0, 0, 0);
    for (const auto& v : hullVerts) ref += v;
    ref /= hullVerts.size();

    for (int i = 0; i < hullIndices.size(); i += 3) {
        if (i + 2 >= hullIndices.size()) break;
        QVector3D a = hullVerts[hullIndices[i]];
        QVector3D b = hullVerts[hullIndices[i+1]];
        QVector3D c = hullVerts[hullIndices[i+2]];
        float vol = QVector3D::dotProduct(a - ref, QVector3D::crossProduct(b - ref, c - ref)) / 6.0f;
        volume += vol;
    }

    return std::abs(volume);
}

} // namespace tools
} // namespace ks