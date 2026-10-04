#include "LODGenerator.h"
#include "../FileFormat/MeshData.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <cmath>

namespace ks {
namespace tools {

struct Quadric {
    double a[10] = {0};

    Quadric() = default;
    Quadric(const QVector3D& p, const QVector3D& n) {
        double nx = n.x(), ny = n.y(), nz = n.z();
        double d = -QVector3D::dotProduct(n, p);
        a[0] = nx * nx;  a[1] = nx * ny;  a[2] = nx * nz;  a[3] = nx * d;
        a[4] = ny * ny;  a[5] = ny * nz;  a[6] = ny * d;
        a[7] = nz * nz;  a[8] = nz * d;
        a[9] = d * d;
    }

    Quadric operator+(const Quadric& other) const {
        Quadric r;
        for (int i = 0; i < 10; ++i) r.a[i] = a[i] + other.a[i];
        return r;
    }

    Quadric& operator+=(const Quadric& other) {
        for (int i = 0; i < 10; ++i) a[i] += other.a[i];
        return *this;
    }

    float evaluate(const QVector3D& v) const {
        double x = v.x(), y = v.y(), z = v.z();
        return static_cast<float>(a[0]*x*x + 2*a[1]*x*y + 2*a[2]*x*z + 2*a[3]*x
                 + a[4]*y*y + 2*a[5]*y*z + 2*a[6]*y
                 + a[7]*z*z + 2*a[8]*z + a[9]);
    }

    bool solve(QVector3D& out) const {
        double A[3][3] = {
            {a[0], a[1], a[2]},
            {a[1], a[4], a[5]},
            {a[2], a[5], a[7]}
        };
        double b[3] = {-a[3], -a[6], -a[8]};
        double det = A[0][0] * (A[1][1]*A[2][2] - A[1][2]*A[2][1])
                   - A[0][1] * (A[1][0]*A[2][2] - A[1][2]*A[2][0])
                   + A[0][2] * (A[1][0]*A[2][1] - A[1][1]*A[2][0]);
        if (std::abs(det) < 1e-10) return false;
        double invDet = 1.0 / det;
        double x = invDet * (b[0] * (A[1][1]*A[2][2] - A[1][2]*A[2][1])
                           - A[0][1] * (b[1]*A[2][2] - A[1][2]*b[2])
                           + A[0][2] * (b[1]*A[2][1] - A[1][1]*b[2]));
        double y = invDet * (A[0][0] * (b[1]*A[2][2] - A[1][2]*b[2])
                           - b[0] * (A[1][0]*A[2][2] - A[1][2]*A[2][0])
                           + A[0][2] * (A[1][0]*b[2] - b[1]*A[2][0]));
        double z = invDet * (A[0][0] * (A[1][1]*b[2] - b[1]*A[2][1])
                           - A[0][1] * (A[1][0]*b[2] - b[1]*A[2][0])
                           + b[0] * (A[1][0]*A[2][1] - A[1][1]*A[2][0]));
        out = QVector3D(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        return true;
    }
};

struct EdgeCollapse {
    int v1, v2;
    QVector3D optimalPosition;
    float cost;
    int newVertexIndex;

    bool operator<(const EdgeCollapse& other) const { return cost > other.cost; }
};

struct VertexData {
    QVector3D position;
    QVector3D normal;
    QVector2D uv;
    QVector<int> triangles;
    QVector<int> edges;
    Quadric quadric;
    bool isBoundary = false;
    bool isUVSeam = false;
    bool isSharp = false;
    bool removed = false;
};

struct EdgeData {
    int v1, v2;
    int tri1, tri2;
    bool isBoundary = false;
    bool isUVSeam = false;
    bool isSharp = false;
    float cost = 0;
    QVector3D optimalPosition;
};

static void buildQuadrics(const QVector<QVector3D>& vertices,
                          const QVector<int>& indices,
                          const QVector<QVector3D>& normals,
                          QVector<VertexData>& vertexData,
                          QVector<EdgeData>& edges)
{
    int nVerts = vertices.size();
    vertexData.resize(nVerts);
    for (int i = 0; i < nVerts; ++i) {
        vertexData[i].position = vertices[i];
        if (i < normals.size()) vertexData[i].normal = normals[i];
    }

    QMap<QPair<int, int>, int> edgeMap;
    for (int i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size()) break;
        int idx[3] = {indices[i], indices[i+1], indices[i+2]};
        QVector3D p0 = vertices[idx[0]];
        QVector3D p1 = vertices[idx[1]];
        QVector3D p2 = vertices[idx[2]];
        QVector3D n = QVector3D::crossProduct(p1 - p0, p2 - p0);
        float len = n.length();
        if (len < 1e-8f) continue;
        n /= len;

        Quadric Qf(p0, n);
        for (int j = 0; j < 3; ++j) {
            if (idx[j] < nVerts) vertexData[idx[j]].quadric += Qf;
            vertexData[idx[j]].triangles.append(i / 3);
        }

        for (int e = 0; e < 3; ++e) {
            int a = idx[e], b = idx[(e+1)%3];
            if (a > b) std::swap(a, b);
            QPair<int, int> key(a, b);
            if (!edgeMap.contains(key)) {
                edgeMap[key] = edges.size();
                edges.append({a, b, i/3, -1});
            } else {
                edges[edgeMap[key]].tri2 = i/3;
            }
        }
    }

    for (auto& edge : edges) {
        edge.isBoundary = (edge.tri2 == -1);
    }
}

static void identifyFeatureEdges(const QVector<QVector3D>& vertices,
                                 const QVector<int>& indices,
                                 const QVector<QVector3D>& normals,
                                 const QVector<QVector2D>& uvs,
                                 QVector<VertexData>& vertexData,
                                 QVector<EdgeData>& edges,
                                 float sharpAngle)
{
    float cosThreshold = qCos(qDegreesToRadians(sharpAngle));

    for (auto& edge : edges) {
        if (!edge.isBoundary) {
            QVector3D n1 = vertexData[edge.v1].normal;
            QVector3D n2 = vertexData[edge.v2].normal;
            if (QVector3D::dotProduct(n1, n2) < cosThreshold) {
                edge.isSharp = true;
                vertexData[edge.v1].isSharp = true;
                vertexData[edge.v2].isSharp = true;
            }
        }

        if (!uvs.isEmpty()) {
            QVector2D uv1 = uvs[edge.v1];
            QVector2D uv2 = uvs[edge.v2];
            if ((uv1 - uv2).length() > 0.1f) {
                edge.isUVSeam = true;
                vertexData[edge.v1].isUVSeam = true;
                vertexData[edge.v2].isUVSeam = true;
            }
        }
    }
}

static void computeCollapseCosts(QVector<VertexData>& vertexData,
                                 QVector<EdgeData>& edges,
                                 QVector<EdgeCollapse>& collapseHeap)
{
    collapseHeap.clear();
    collapseHeap.reserve(edges.size());

    for (int i = 0; i < edges.size(); ++i) {
        EdgeData& edge = edges[i];
        if (edge.isBoundary || edge.isSharp || edge.isUVSeam) {
            edge.cost = std::numeric_limits<float>::max();
            continue;
        }

        Quadric Qe = vertexData[edge.v1].quadric + vertexData[edge.v2].quadric;
        QVector3D optPos;
        if (!Qe.solve(optPos)) {
            optPos = (vertexData[edge.v1].position + vertexData[edge.v2].position) * 0.5f;
        }
        float cost = Qe.evaluate(optPos);

        edge.cost = cost;
        edge.optimalPosition = optPos;
        collapseHeap.append({edge.v1, edge.v2, optPos, cost, -1});
    }

    std::sort(collapseHeap.begin(), collapseHeap.end());
}

static bool collapseEdge(QVector<VertexData>& vertexData,
                         QVector<EdgeData>& edges,
                         QVector<int>& indices,
                         QVector<QVector3D>& normals,
                         QVector<QVector2D>& uvs,
                         const EdgeCollapse& collapse)
{
    int v1 = collapse.v1;
    int v2 = collapse.v2;
    if (v1 == v2 || vertexData[v1].removed || vertexData[v2].removed) return false;

    QVector3D newPos = collapse.optimalPosition;
    Quadric newQuadric = vertexData[v1].quadric + vertexData[v2].quadric;

    QVector<int> newTriangles;
    for (int triIdx : vertexData[v2].triangles) {
        if (!vertexData[v1].triangles.contains(triIdx)) {
            newTriangles.append(triIdx);
        }
    }

    for (int edgeIdx : vertexData[v2].edges) {
        EdgeData& edge = edges[edgeIdx];
        if (edge.v1 == v2) edge.v1 = v1;
        else if (edge.v2 == v2) edge.v2 = v1;
        if (edge.v1 == edge.v1 && edge.v2 == edge.v2) continue;
        if (edge.v1 > edge.v2) std::swap(edge.v1, edge.v2);
    }

    vertexData[v1].position = newPos;
    vertexData[v1].quadric = newQuadric;
    vertexData[v1].triangles.append(newTriangles);
    vertexData[v2].removed = true;

    return true;
}

static void updateMeshTopology(QVector<VertexData>& vertexData,
                               QVector<EdgeData>& edges,
                               QVector<int>& indices,
                               QVector<QVector3D>& normals,
                               QVector<QVector2D>& uvs,
                               int removedVertex)
{
    Q_UNUSED(removedVertex);
    QVector<int> remap(vertexData.size(), -1);
    int newIdx = 0;
    for (int i = 0; i < vertexData.size(); ++i) {
        if (!vertexData[i].removed) {
            remap[i] = newIdx++;
        }
    }

    QVector<VertexData> newVertexData(newIdx);
    for (int i = 0; i < vertexData.size(); ++i) {
        if (!vertexData[i].removed) {
            newVertexData[remap[i]] = vertexData[i];
        }
    }
    vertexData = newVertexData;

    QVector<int> newIndices;
    newIndices.reserve(indices.size());
    for (int i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size()) break;
        int ni[3] = {remap[indices[i]], remap[indices[i+1]], remap[indices[i+2]]};
        if (ni[0] != ni[1] && ni[1] != ni[2] && ni[2] != ni[0]) {
            newIndices.append(ni[0]);
            newIndices.append(ni[1]);
            newIndices.append(ni[2]);
        }
    }
    indices = newIndices;
}

static LODGenerator::LODLevel simplifyWithQEM(const LODGenerator::LODLevel& source, int targetTriangles, const LODGenerator::Options& options)
{
    LODGenerator::LODLevel result = source;
    if (result.triangleCount <= targetTriangles) return result;

    QVector<VertexData> vertexData;
    QVector<EdgeData> edges;
    buildQuadrics(result.vertices, result.indices, result.normals, vertexData, edges);
    identifyFeatureEdges(result.vertices, result.indices, result.normals, result.uvs, vertexData, edges, options.sharpEdgeAngle);

    QVector<EdgeCollapse> collapseHeap;
    int currentTris = result.triangleCount;
    int removedCount = 0;

    while (currentTris > targetTriangles && removedCount < result.vertices.size() / 2) {
        computeCollapseCosts(vertexData, edges, collapseHeap);
        if (collapseHeap.isEmpty() || collapseHeap[0].cost >= std::numeric_limits<float>::max() / 2) break;

        if (!collapseEdge(vertexData, edges, result.indices, result.normals, result.uvs, collapseHeap[0])) {
            break;
        }

        updateMeshTopology(vertexData, edges, result.indices, result.normals, result.uvs, collapseHeap[0].v2);
        currentTris = result.indices.size() / 3;
        removedCount++;
    }

    result.vertices.resize(vertexData.size());
    for (int i = 0; i < vertexData.size(); ++i) {
        result.vertices[i] = vertexData[i].position;
        if (i < result.normals.size()) result.normals[i] = vertexData[i].normal;
        if (i < result.uvs.size()) result.uvs[i] = vertexData[i].uv;
    }
    result.triangleCount = currentTris;

    return result;
}

static LODGenerator::LODLevel simplifyUniform(const LODGenerator::LODLevel& source, int targetTriangles)
{
    LODGenerator::LODLevel result = source;
    if (result.triangleCount <= targetTriangles) return result;

    float ratio = static_cast<float>(targetTriangles) / result.triangleCount;
    int keepVerts = qMax(4, static_cast<int>(result.vertices.size() * ratio));

    QVector<int> keepIndices;
    for (int i = 0; i < qMin(keepVerts, result.vertices.size()); ++i) {
        keepIndices.append(i);
    }

    QVector<int> remap(result.vertices.size(), -1);
    for (int i = 0; i < keepIndices.size(); ++i) {
        remap[keepIndices[i]] = i;
    }

    result.vertices = QVector<QVector3D>(keepIndices.size());
    for (int i = 0; i < keepIndices.size(); ++i) {
        result.vertices[i] = source.vertices[keepIndices[i]];
    }
    if (!source.normals.isEmpty()) {
        result.normals.resize(keepIndices.size());
        for (int i = 0; i < keepIndices.size(); ++i) {
            result.normals[i] = source.normals[keepIndices[i]];
        }
    }
    if (!source.uvs.isEmpty()) {
        result.uvs.resize(keepIndices.size());
        for (int i = 0; i < keepIndices.size(); ++i) {
            result.uvs[i] = source.uvs[keepIndices[i]];
        }
    }

    QVector<int> newIndices;
    for (int idx : source.indices) {
        if (remap[idx] >= 0) newIndices.append(remap[idx]);
    }
    result.indices = newIndices;
    result.triangleCount = newIndices.size() / 3;

    return result;
}

static LODGenerator::LODLevel preserveFeatures(const LODGenerator::LODLevel& original, const LODGenerator::LODLevel& simplified, const LODGenerator::Options& options)
{
    LODGenerator::LODLevel result = simplified;
    if (!options.preserveBoundaries && !options.preserveUVSeams && !options.preserveSharpEdges)
        return result;

    // Build a spatial lookup to find which simplified vertices correspond to original ones
    QMap<int, int> origToSimp;  // original index -> simplified index
    float snapDist = 1e-3f;
    for (int si = 0; si < result.vertices.size(); ++si) {
        const QVector3D& sv = result.vertices[si];
        for (int oi = 0; oi < original.vertices.size(); ++oi) {
            if ((original.vertices[oi] - sv).lengthSquared() < snapDist) {
                origToSimp[oi] = si;
                break;
            }
        }
    }

    // Identify sharp edges in the original mesh
    QSet<QPair<int, int>> featureEdges;
    if (options.preserveSharpEdges) {
        float cosAngle = std::cos(qDegreesToRadians(options.sharpEdgeAngle));
        for (int i = 0; i < original.indices.size(); i += 3) {
            if (i + 2 >= original.indices.size()) break;
            int i0 = original.indices[i], i1 = original.indices[i+1], i2 = original.indices[i+2];
            QVector3D e1 = original.vertices[i1] - original.vertices[i0];
            QVector3D e2 = original.vertices[i2] - original.vertices[i0];
            QVector3D n = QVector3D::crossProduct(e1, e2);
            float len = n.length();
            if (len < 1e-8f) continue;
            n /= len;

            // For each edge, check adjacent triangle normals
            struct Edge { int a, b; };
            Edge edges[3] = {{i0, i1}, {i1, i2}, {i2, i0}};
            for (const auto& e : edges) {
                int a = e.a, b = e.b;
                if (a > b) std::swap(a, b);
                QPair<int, int> key(a, b);
                auto it = featureEdges.find(key);
                if (it != featureEdges.end()) continue;
                // Check if this edge exists in another triangle with different normal
                for (int j = 0; j < original.indices.size(); j += 3) {
                    if (j == i) continue;
                    if (j + 2 >= original.indices.size()) break;
                    int j0 = original.indices[j], j1 = original.indices[j+1], j2 = original.indices[j+2];
                    if ((j0 == a && j1 == b) || (j0 == a && j2 == b) ||
                        (j1 == a && j2 == b) || (j1 == a && j0 == b) ||
                        (j2 == a && j0 == b) || (j2 == a && j1 == b)) {
                        QVector3D fe1 = original.vertices[j1] - original.vertices[j0];
                        QVector3D fe2 = original.vertices[j2] - original.vertices[j0];
                        QVector3D fn = QVector3D::crossProduct(fe1, fe2);
                        float flen = fn.length();
                        if (flen < 1e-8f) continue;
                        fn /= flen;
                        if (std::abs(QVector3D::dotProduct(n, fn)) < cosAngle) {
                            featureEdges.insert(key);
                        }
                        break;
                    }
                }
            }
        }
    }

    // If we need to restore vertices near feature edges, add them back
    if (!featureEdges.isEmpty() || options.preserveBoundaries) {
        QSet<int> vertsToRestore;
        // Collect vertex indices from feature edges
        for (const auto& edge : featureEdges) {
            if (origToSimp.contains(edge.first)) vertsToRestore.insert(edge.first);
            if (origToSimp.contains(edge.second)) vertsToRestore.insert(edge.second);
        }

        // Find boundary vertices (edges that appear in only one triangle)
        if (options.preserveBoundaries) {
            QMap<QPair<int, int>, int> edgeCount;
            for (int i = 0; i < original.indices.size(); i += 3) {
                if (i + 2 >= original.indices.size()) break;
                int tri[3] = {original.indices[i], original.indices[i+1], original.indices[i+2]};
                for (int e = 0; e < 3; ++e) {
                    int a = tri[e], b = tri[(e+1)%3];
                    if (a > b) std::swap(a, b);
                    edgeCount[QPair<int, int>(a, b)]++;
                }
            }
            for (auto it = edgeCount.begin(); it != edgeCount.end(); ++it) {
                if (it.value() == 1) {
                    vertsToRestore.insert(it.key().first);
                    vertsToRestore.insert(it.key().second);
                }
            }
        }

        // Add back original vertices near feature edges if they're missing from simplified
        QVector<QVector3D> newVerts = result.vertices;
        QMap<int, int> extraMap; // orig idx -> new simplified idx
        for (int oi : vertsToRestore) {
            if (origToSimp.contains(oi)) continue;
            if (oi < 0 || oi >= original.vertices.size()) continue;
            int newIdx = newVerts.size();
            newVerts.append(original.vertices[oi]);
            extraMap[oi] = newIdx;
            origToSimp[oi] = newIdx;
        }

        // Remap indices, adding triangles around restored vertices
        QVector<int> newIndices = result.indices;
        QSet<int> restoredSet = vertsToRestore;
        for (int i = 0; i < original.indices.size(); i += 3) {
            if (i + 2 >= original.indices.size()) break;
            int oi0 = original.indices[i], oi1 = original.indices[i+1], oi2 = original.indices[i+2];
            // Restore any triangle that has at least one feature vertex
            if (restoredSet.contains(oi0) || restoredSet.contains(oi1) || restoredSet.contains(oi2)) {
                if (origToSimp.contains(oi0) && origToSimp.contains(oi1) && origToSimp.contains(oi2)) {
                    newIndices.append(origToSimp[oi0]);
                    newIndices.append(origToSimp[oi1]);
                    newIndices.append(origToSimp[oi2]);
                }
            }
        }

        result.vertices = newVerts;
        result.indices = newIndices;
    }

    result.triangleCount = result.indices.size() / 3;
    return result;
}

LODGenerator::LODLevel LODGenerator::generateCollisionMesh(const LODGenerator::LODLevel& lod)
{
    LODGenerator::LODLevel result = lod;
    result.vertices.clear();
    result.indices.clear();
    result.normals.clear();
    result.uvs.clear();

    if (lod.vertices.isEmpty()) return result;

    QVector3D minBounds = lod.vertices[0], maxBounds = lod.vertices[0];
    for (const auto& v : lod.vertices) {
        minBounds = QVector3D(qMin(minBounds.x(), v.x()), qMin(minBounds.y(), v.y()), qMin(minBounds.z(), v.z()));
        maxBounds = QVector3D(qMax(maxBounds.x(), v.x()), qMax(maxBounds.y(), v.y()), qMax(maxBounds.z(), v.z()));
    }

    QVector3D center = (minBounds + maxBounds) * 0.5f;
    QVector3D halfExtents = (maxBounds - minBounds) * 0.5f;

    result.vertices = {
        QVector3D(-halfExtents.x(), -halfExtents.y(), -halfExtents.z()) + center,
        QVector3D( halfExtents.x(), -halfExtents.y(), -halfExtents.z()) + center,
        QVector3D( halfExtents.x(),  halfExtents.y(), -halfExtents.z()) + center,
        QVector3D(-halfExtents.x(),  halfExtents.y(), -halfExtents.z()) + center,
        QVector3D(-halfExtents.x(), -halfExtents.y(),  halfExtents.z()) + center,
        QVector3D( halfExtents.x(), -halfExtents.y(),  halfExtents.z()) + center,
        QVector3D( halfExtents.x(),  halfExtents.y(),  halfExtents.z()) + center,
        QVector3D(-halfExtents.x(),  halfExtents.y(),  halfExtents.z()) + center
    };

    result.indices = {
        0,1,2, 2,3,0,  4,5,6, 6,7,4,
        0,1,5, 5,4,0,  2,3,7, 7,6,2,
        0,3,7, 7,4,0,  1,2,6, 6,5,1
    };
    result.triangleCount = 12;
    result.normals.resize(8);
    result.uvs.resize(8);

    return result;
}

float LODGenerator::calculateScreenSize(const LODGenerator::LODLevel& lod,
                                         float fov,
                                         int screenHeight)
{
    if (lod.vertices.isEmpty()) return 0.0f;

    QVector3D minV = lod.vertices[0], maxV = lod.vertices[0];
    for (const auto& v : lod.vertices) {
        minV = QVector3D(qMin(minV.x(), v.x()), qMin(minV.y(), v.y()), qMin(minV.z(), v.z()));
        maxV = QVector3D(qMax(maxV.x(), v.x()), qMax(maxV.y(), v.y()), qMax(maxV.z(), v.z()));
    }
    float size = (maxV - minV).length();
    float distance = size * 10.0f;
    if (distance <= 0) return 1e10f;
    float screenSize = 2.0f * size * screenHeight / (2.0f * distance * qTan(qDegreesToRadians(fov * 0.5f)));
    return screenSize;
}

LODGenerator::Result LODGenerator::generate(const QVector<QVector3D>& vertices,
                                            const QVector<int>& indices,
                                            const QVector<QVector3D>& normals,
                                            const QVector<QVector2D>& uvs,
                                            const Options& options)
{
    Result result;
    result.success = false;

    if (vertices.isEmpty() || indices.size() < 3) {
        result.errorMessage = "Invalid input mesh";
        return result;
    }

    LODLevel currentLOD;
    currentLOD.level = 0;
    currentLOD.vertices = vertices;
    currentLOD.indices = indices;
    currentLOD.normals = normals;
    currentLOD.uvs = uvs;
    currentLOD.triangleCount = indices.size() / 3;

    result.levels.append(currentLOD);
    result.originalBoundsMin = QVector3D(std::numeric_limits<float>::max(),
                                         std::numeric_limits<float>::max(),
                                         std::numeric_limits<float>::max());
    result.originalBoundsMax = QVector3D(std::numeric_limits<float>::lowest(),
                                         std::numeric_limits<float>::lowest(),
                                         std::numeric_limits<float>::lowest());

    for (const auto& v : vertices) {
        result.originalBoundsMin = QVector3D(qMin(result.originalBoundsMin.x(), v.x()),
                                             qMin(result.originalBoundsMin.y(), v.y()),
                                             qMin(result.originalBoundsMin.z(), v.z()));
        result.originalBoundsMax = QVector3D(qMax(result.originalBoundsMax.x(), v.x()),
                                             qMax(result.originalBoundsMax.y(), v.y()),
                                             qMax(result.originalBoundsMax.z(), v.z()));
    }

    QVector3D size = result.originalBoundsMax - result.originalBoundsMin;
    result.originalSurfaceArea = 2 * (size.x() * size.y() + size.y() * size.z() + size.z() * size.x());

    for (int level = 1; level < options.lodCount; ++level) {
        float ratio = std::pow(options.reductionRatio, level);
        int targetTris = qMax(static_cast<int>(currentLOD.triangleCount * ratio), 
                              static_cast<int>(currentLOD.triangleCount * options.minTriangleRatio));
        targetTris = qMin(targetTris, options.maxTriangles);

        if (targetTris >= currentLOD.triangleCount) {
            break;
        }

        LODLevel nextLOD;
        nextLOD.level = level;

        if (options.useQuadricError) {
            nextLOD = simplifyWithQEM(currentLOD, targetTris, options);
        } else {
            nextLOD = simplifyUniform(currentLOD, targetTris);
        }

        if (nextLOD.triangleCount < 4) {
            break;
        }

        nextLOD.screenSize = calculateScreenSize(nextLOD, 60.0f, 1080);

        if (options.preserveBoundaries || options.preserveUVSeams || options.preserveSharpEdges) {
            nextLOD = preserveFeatures(currentLOD, nextLOD, options);
        }

        if (options.generateCollision && level == options.lodCount - 1) {
            nextLOD = generateCollisionMesh(nextLOD);
        }

        result.levels.append(nextLOD);
        currentLOD = nextLOD;
    }

    result.success = true;
    return result;
}

LODGenerator::Result LODGenerator::generateFromMeshData(const QVector<QVector3D>& vertices,
                                                         const QVector<int>& indices,
                                                         const QVector<QVector3D>& normals,
                                                         const QVector<QVector2D>& uvs,
                                                         const Options& options)
{
    return generate(vertices, indices, normals, uvs, options);
}

static LODGenerator::LODLevel generateImpostor(const QVector<QVector3D>& vertices,
                                               const QVector<int>& indices,
                                               int resolution)
{
    LODGenerator::LODLevel result;
    result.level = -1;

    if (vertices.isEmpty()) {
        result.triangleCount = 2;
        result.vertices = {
            QVector3D(-1, -1, 0), QVector3D(1, -1, 0),
            QVector3D(1, 1, 0), QVector3D(-1, 1, 0)
        };
        result.indices = {0, 1, 2, 0, 2, 3};
        result.uvs = {QVector2D(0, 0), QVector2D(1, 0), QVector2D(1, 1), QVector2D(0, 1)};
        result.normals = {QVector3D(0, 0, 1), QVector3D(0, 0, 1), QVector3D(0, 0, 1), QVector3D(0, 0, 1)};
        return result;
    }

    QVector3D minV = vertices[0], maxV = vertices[0];
    for (int i = 1; i < vertices.size(); ++i) {
        const QVector3D& v = vertices[i];
        minV = QVector3D(qMin(minV.x(), v.x()), qMin(minV.y(), v.y()), qMin(minV.z(), v.z()));
        maxV = QVector3D(qMax(maxV.x(), v.x()), qMax(maxV.y(), v.y()), qMax(maxV.z(), v.z()));
    }

    QVector3D center = (minV + maxV) * 0.5f;
    QVector3D extents = (maxV - minV) * 0.5f;
    float maxExtent = qMax(qMax(extents.x(), extents.y()), extents.z());

    result.triangleCount = 2;
    result.vertices = {
        center + QVector3D(-maxExtent, -maxExtent, 0),
        center + QVector3D( maxExtent, -maxExtent, 0),
        center + QVector3D( maxExtent,  maxExtent, 0),
        center + QVector3D(-maxExtent,  maxExtent, 0)
    };
    result.indices = {0, 1, 2, 0, 2, 3};
    result.uvs = {QVector2D(0, 0), QVector2D(1, 0), QVector2D(1, 1), QVector2D(0, 1)};
    result.normals = {QVector3D(0, 0, 1), QVector3D(0, 0, 1), QVector3D(0, 0, 1), QVector3D(0, 0, 1)};

    return result;
}

static float calculateScreenError(const LODGenerator::LODLevel& lod,
                                   float distance,
                                   float fov,
                                   int viewportHeight)
{
    if (distance <= 0) return std::numeric_limits<float>::max();
    float size = (lod.vertices.isEmpty()) ? 1.0f : 
        (lod.vertices.first() - lod.vertices.last()).length();
    float projectedSize = size * viewportHeight / (2.0f * distance * qTan(qDegreesToRadians(fov * 0.5f)));
    return projectedSize;
}

} // namespace tools
} // namespace ks