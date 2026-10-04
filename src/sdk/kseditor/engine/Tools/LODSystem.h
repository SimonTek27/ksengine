#pragma once

#include <QString>
#include <QVector>
#include <QVector3D>
#include <QSet>
#include <QMap>
#include <QPair>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include "../mesh/MeshOperations.h"

namespace ks {

// ── LOD Level Config ───────────────────────────────────────────────────────

struct LODLevel {
    QString name;
    float screenSize = 0.0f;
    float distance = 0.0f;
    int targetTriangles = 0;
    float decimateRatio = 1.0f;
    bool useUnsubdivide = false;
    int unsubdivideSteps = 0;
    bool generateNormals = true;
    bool generateTangents = true;
    bool weldVertices = true;
    float weldThreshold = 0.001f;
};

struct LODResult {
    QVector<MeshData> levels;
    QVector<float> distances;
    int originalTriCount = 0;
    int originalVertCount = 0;
};

// ── Unified LOD System ─────────────────────────────────────────────────────

class LODSystem {
public:
    // ── LOD Generation ──
    static LODResult generateAutoLODs(const MeshData& mesh, int levelCount = 3);
    static LODResult generateCustomLODs(const MeshData& mesh, const QVector<LODLevel>& levels);

    static LODLevel makeLOD0();
    static LODLevel makeLOD1();
    static LODLevel makeLOD2();
    static LODLevel makeLOD3();

    // ── Mesh Simplification (QEM) ──
    static MeshData decimateMesh(const MeshData& mesh, float ratio);
    static MeshData simplifyMesh(const MeshData& source, float targetRatio);
    static MeshData simplifyMeshQuadric(const MeshData& mesh, int targetTriangles);
    static MeshData unsubdivideMesh(const MeshData& mesh, int steps);
    static MeshData reduceVertices(MeshData source, int targetCount);

    // ── Helpers ──
    static void optimizeMeshForLOD(MeshData& mesh);
    static float calculateMeshArea(const MeshData& mesh);
    static float calculateScreenSize(const MeshData& mesh, float distance, float fov);
    static QVector3D calculateBoundingSphere(const MeshData& mesh);
    static int countTriangles(const MeshData& mesh);
    static int estimateLODTriangles(int highPolyTris, int lodIndex, int totalLODs);
    static QVector<int> findBoundaryEdges(const MeshData& mesh);

    // ── Export ──
    static bool exportLODFiles(const MeshData& mesh, const QString& basePath,
                               const QVector<LODLevel>& levels);
    static QString getLODFileName(const QString& baseName, int lodIndex);

private:
    struct QEMQuadric {
        double a[10] = {};
        QEMQuadric& operator+=(const QEMQuadric& o) {
            for (int i = 0; i < 10; ++i) a[i] += o.a[i];
            return *this;
        }
        QEMQuadric operator+(const QEMQuadric& o) const {
            QEMQuadric r = *this; r += o; return r;
        }
        double error(double x, double y, double z) const {
            return a[0]*x*x + 2*a[1]*x*y + 2*a[2]*x*z + 2*a[3]*x
                 + a[4]*y*y + 2*a[5]*y*z + 2*a[6]*y
                 + a[7]*z*z + 2*a[8]*z + a[9];
        }
    };

    static QEMQuadric planeToQuadric(double nx, double ny, double nz, double d);
};

// ── Inline implementations ─────────────────────────────────────────────────

inline LODLevel LODSystem::makeLOD0() {
    LODLevel l;
    l.name = "LOD0_High";
    l.screenSize = 1.0f;
    l.distance = 0.0f;
    l.decimateRatio = 1.0f;
    return l;
}

inline LODLevel LODSystem::makeLOD1() {
    LODLevel l;
    l.name = "LOD1_Medium";
    l.screenSize = 0.5f;
    l.distance = 50.0f;
    l.decimateRatio = 0.5f;
    return l;
}

inline LODLevel LODSystem::makeLOD2() {
    LODLevel l;
    l.name = "LOD2_Low";
    l.screenSize = 0.25f;
    l.distance = 100.0f;
    l.decimateRatio = 0.25f;
    return l;
}

inline LODLevel LODSystem::makeLOD3() {
    LODLevel l;
    l.name = "LOD3_Impostor";
    l.screenSize = 0.1f;
    l.distance = 200.0f;
    l.decimateRatio = 0.1f;
    return l;
}

inline LODResult LODSystem::generateAutoLODs(const MeshData& mesh, int levelCount) {
    QVector<LODLevel> levels;
    switch (levelCount) {
        case 4: levels = {makeLOD0(), makeLOD1(), makeLOD2(), makeLOD3()}; break;
        case 3: levels = {makeLOD0(), makeLOD1(), makeLOD2()}; break;
        default: levels = {makeLOD0(), makeLOD1()}; break;
    }
    return generateCustomLODs(mesh, levels);
}

inline LODResult LODSystem::generateCustomLODs(const MeshData& mesh, const QVector<LODLevel>& levels) {
    LODResult result;
    result.originalTriCount = countTriangles(mesh);
    result.originalVertCount = mesh.vertices.size();

    for (int i = 0; i < levels.size(); ++i) {
        const LODLevel& lvl = levels[i];
        MeshData lodMesh;

        if (i == 0) {
            lodMesh = mesh;
            optimizeMeshForLOD(lodMesh);
        } else {
            MeshData prev = result.levels[i - 1];

            if (lvl.useUnsubdivide && lvl.unsubdivideSteps > 0) {
                lodMesh = unsubdivideMesh(prev, lvl.unsubdivideSteps);
            } else {
                float ratio = lvl.decimateRatio;
                if (lvl.targetTriangles > 0) {
                    int currentTris = countTriangles(prev);
                    ratio = (float)lvl.targetTriangles / (float)qMax(currentTris, 1);
                    ratio = qBound(0.01f, ratio, 0.99f);
                }
                lodMesh = decimateMesh(prev, ratio);
            }

            optimizeMeshForLOD(lodMesh);
        }

        result.levels.append(lodMesh);
        result.distances.append(lvl.distance);
    }

    return result;
}

inline int LODSystem::countTriangles(const MeshData& mesh) {
    return mesh.faces.size();
}

inline float LODSystem::calculateMeshArea(const MeshData& mesh) {
    float totalArea = 0.0f;
    for (const auto& face : mesh.faces) {
        if (face.indices.size() < 3) continue;
        const QVector3D& p0 = mesh.vertices[face.indices[0]].position;
        const QVector3D& p1 = mesh.vertices[face.indices[1]].position;
        const QVector3D& p2 = mesh.vertices[face.indices[2]].position;
        totalArea += QVector3D::crossProduct(p1 - p0, p2 - p0).length() * 0.5f;
    }
    return totalArea;
}

inline LODSystem::QEMQuadric LODSystem::planeToQuadric(double nx, double ny, double nz, double d) {
    QEMQuadric q;
    q.a[0] = nx * nx; q.a[1] = nx * ny; q.a[2] = nx * nz; q.a[3] = nx * d;
    q.a[4] = ny * ny; q.a[5] = ny * nz; q.a[6] = ny * d;
    q.a[7] = nz * nz; q.a[8] = nz * d;
    q.a[9] = d * d;
    return q;
}

inline MeshData LODSystem::decimateMesh(const MeshData& mesh, float ratio) {
    if (ratio >= 1.0f) return mesh;
    int nVerts = mesh.vertices.size();
    if (nVerts < 3) return mesh;
    int targetFaces = qMax(3, static_cast<int>(mesh.faces.size() * ratio));
    if (targetFaces >= mesh.faces.size()) return mesh;

    // Decimate on triangles: collapsing an edge shared by two triangles can
    // actually remove a face, whereas collapsing a quad edge only triangulates
    // it (still one face) so the face count would never drop.
    MeshData wm = mesh;
    {
        QVector<Face> triFaces;
        triFaces.reserve(mesh.faces.size() * 2);
        for (const Face& f : mesh.faces) {
            int n = f.indices.size();
            if (n <= 3) { triFaces.append(f); continue; }
            for (int i = 1; i < n - 1; ++i) {
                Face t;
                t.indices = {f.indices[0], f.indices[i], f.indices[i + 1]};
                if (f.uvIndices.size() == n)
                    t.uvIndices = {f.uvIndices[0], f.uvIndices[i], f.uvIndices[i + 1]};
                triFaces.append(t);
            }
        }
        wm.faces = triFaces;
    }

    QVector<QEMQuadric> Q(nVerts);
    for (const Face& face : wm.faces) {
        if (face.indices.size() < 3) continue;
        const QVector3D& p0 = wm.vertices[face.indices[0]].position;
        const QVector3D& p1 = wm.vertices[face.indices[1]].position;
        const QVector3D& p2 = wm.vertices[face.indices[2]].position;
        QVector3D n = QVector3D::crossProduct(p1 - p0, p2 - p0);
        double len = n.length();
        if (len < 1e-10) continue;
        n /= static_cast<float>(len);
        double d = -static_cast<double>(QVector3D::dotProduct(n, p0));
        QEMQuadric Qf = planeToQuadric(n.x(), n.y(), n.z(), d);
        for (int idx : face.indices) {
            if (idx >= 0 && idx < nVerts) Q[idx] += Qf;
        }
    }

    struct Collapse { int v0, v1; double cost; };
    QMap<QPair<int, int>, int> edgeMap;
    QVector<Collapse> collapses;
    collapses.reserve(mesh.faces.size() * 3);

    for (const Face& face : wm.faces) {
        int n = face.indices.size();
        for (int i = 0; i < n; ++i) {
            int va = face.indices[i];
            int vb = face.indices[(i + 1) % n];
            if (va == vb || va < 0 || vb < 0 || va >= nVerts || vb >= nVerts) continue;
            auto key = std::make_pair(qMin(va, vb), qMax(va, vb));
            if (edgeMap.contains(key)) continue;
            edgeMap[key] = collapses.size();
            QEMQuadric Qe = Q[va] + Q[vb];
            const QVector3D& pa = wm.vertices[va].position;
            const QVector3D& pb = wm.vertices[vb].position;
            double cost = Qe.error((pa.x() + pb.x()) * 0.5,
                                   (pa.y() + pb.y()) * 0.5,
                                   (pa.z() + pb.z()) * 0.5);
            collapses.append({va, vb, cost});
        }
    }

    std::sort(collapses.begin(), collapses.end(),
              [](const Collapse& a, const Collapse& b) { return a.cost < b.cost; });

    QVector<int> remapTable(nVerts);
    for (int i = 0; i < nVerts; ++i) remapTable[i] = i;

    auto follow = [&](int v) {
        while (remapTable[v] != v) v = remapTable[v];
        return v;
    };

    MeshData result;
    result.vertices = mesh.vertices;
    result.normals = mesh.normals;
    result.uvs = mesh.uvs;
    result.uv2s = mesh.uv2s;
    result.tangents = mesh.tangents;
    result.bitangents = mesh.bitangents;
    result.name = mesh.name;
    result.materialName = mesh.materialName;
    result.diffuseColor = mesh.diffuseColor;
    result.metallic = mesh.metallic;
    result.roughness = mesh.roughness;

    int removedFaces = 0;

    auto countResultFaces = [&]() -> int {
        int count = 0;
        for (const Face& face : wm.faces) {
            QSet<int> unique;
            for (int idx : face.indices) unique.insert(follow(idx));
            if (unique.size() >= 3) ++count;
        }
        return count;
    };

    for (const Collapse& c : collapses) {
        int va = follow(c.v0);
        int vb = follow(c.v1);
        if (va == vb) continue;
        // Tentatively apply the collapse to check the resulting face count
        int savedRemap = remapTable[vb];
        remapTable[vb] = va;
        int cnt = countResultFaces();
        if (cnt < targetFaces) {
            remapTable[vb] = savedRemap;  // overshoots target; try the next collapse
            continue;
        }
        result.vertices[va].position = (wm.vertices[c.v0].position + wm.vertices[c.v1].position) * 0.5f;
        Q[va] += Q[vb];
        removedFaces++;
        if (cnt <= targetFaces) break;   // reached the target face count
    }

    for (const Face& face : wm.faces) {
        Face newFace = face;
        newFace.indices.clear();
        newFace.uvIndices.clear();
        for (int idx : face.indices) {
            int mapped = follow(idx);
            if (newFace.indices.isEmpty() || newFace.indices.last() != mapped)
                newFace.indices.append(mapped);
        }
        QSet<int> unique(newFace.indices.begin(), newFace.indices.end());
        if (unique.size() < 3) continue;
        result.faces.append(newFace);
    }

    if (result.faces.size() > targetFaces)
        result.faces.resize(targetFaces);

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

inline MeshData LODSystem::simplifyMesh(const MeshData& source, float targetRatio) {
    return decimateMesh(source, targetRatio);
}

inline MeshData LODSystem::simplifyMeshQuadric(const MeshData& mesh, int targetTriangles) {
    if (targetTriangles <= 0) return mesh;
    float ratio = static_cast<float>(targetTriangles) / static_cast<float>(qMax(countTriangles(mesh), 1));
    return decimateMesh(mesh, qBound(0.01f, ratio, 0.99f));
}

inline MeshData LODSystem::unsubdivideMesh(const MeshData& mesh, int steps) {
    MeshData result = mesh;
    for (int s = 0; s < steps; ++s) {
        if (result.faces.size() < 2) break;
        QVector<Face> newFaces;
        for (int fi = 0; fi < result.faces.size(); fi += 2) {
            if (fi + 1 >= result.faces.size()) {
                newFaces.append(result.faces[fi]);
                continue;
            }
            const Face& f0 = result.faces[fi];
            const Face& f1 = result.faces[fi + 1];
            if (f0.indices.size() == 3 && f1.indices.size() == 3) {
                Face quad;
                quad.indices = {f0.indices[0], f0.indices[1], f1.indices[0], f1.indices[1]};
                quad.materialId = f0.materialId;
                newFaces.append(quad);
            } else {
                newFaces.append(f0);
                newFaces.append(f1);
            }
        }
        result.faces = newFaces;
    }
    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

inline MeshData LODSystem::reduceVertices(MeshData source, int targetCount) {
    if (targetCount >= source.vertices.size()) return source;

    MeshData result;
    result.vertices.reserve(targetCount);

    QVector<bool> usedVert(source.vertices.size(), false);
    QVector<int> vertMap(source.vertices.size(), -1);

    for (int i = 0; i < source.faces.size() && result.vertices.size() < targetCount; i++) {
        const Face& face = source.faces[i];
        for (int j = 0; j < face.indices.size() && result.vertices.size() < targetCount; j++) {
            int idx = face.indices[j];
            if (!usedVert[idx]) {
                usedVert[idx] = true;
                vertMap[idx] = result.vertices.size();
                result.vertices.append(source.vertices[idx]);
            }
        }
    }

    for (const Face& face : source.faces) {
        Face newFace;
        for (int idx : face.indices) {
            if (vertMap[idx] >= 0)
                newFace.indices.append(vertMap[idx]);
        }
        if (newFace.indices.size() >= 3)
            result.faces.append(newFace);
    }

    return result;
}

inline void LODSystem::optimizeMeshForLOD(MeshData& mesh) {
    if (mesh.vertices.isEmpty()) return;

    QVector<bool> vertUsed(mesh.vertices.size(), false);
    for (const auto& face : mesh.faces) {
        for (int idx : face.indices) {
            if (idx >= 0 && idx < vertUsed.size()) vertUsed[idx] = true;
        }
    }

    QVector<Vertex> newVerts;
    QVector<int> vertMap(mesh.vertices.size(), -1);
    for (int i = 0; i < mesh.vertices.size(); ++i) {
        if (vertUsed[i]) {
            vertMap[i] = newVerts.size();
            newVerts.append(mesh.vertices[i]);
        }
    }

    for (auto& face : mesh.faces) {
        for (int j = 0; j < face.indices.size(); ++j) {
            face.indices[j] = vertMap[face.indices[j]];
        }
    }

    mesh.vertices = newVerts;
}

inline float LODSystem::calculateScreenSize(const MeshData& mesh, float distance, float fov) {
    if (distance <= 0.001f) return 1e10f;
    float boundingRadius = calculateBoundingSphere(mesh).length();
    float screenHeight = 2.0f * boundingRadius * tanf(fov * 0.5f * M_PI / 180.0f);
    return (screenHeight * screenHeight) / (distance * distance);
}

inline QVector3D LODSystem::calculateBoundingSphere(const MeshData& mesh) {
    if (mesh.vertices.isEmpty()) return QVector3D();
    QVector3D center;
    for (const auto& v : mesh.vertices) center += v.position;
    center /= mesh.vertices.size();
    float maxRadius = 0.0f;
    for (const auto& v : mesh.vertices)
        maxRadius = qMax(maxRadius, (v.position - center).length());
    return QVector3D(0.0f, 0.0f, maxRadius);
}

inline int LODSystem::estimateLODTriangles(int highPolyTris, int lodIndex, int totalLODs) {
    if (lodIndex >= totalLODs || lodIndex < 0) return 0;
    if (lodIndex == 0) return highPolyTris;
    float ratio = 1.0f - ((float)lodIndex / totalLODs) * 0.9f;
    return (int)(highPolyTris * ratio);
}

inline QString LODSystem::getLODFileName(const QString& baseName, int lodIndex) {
    return baseName + "_lod" + QString::number(lodIndex);
}

inline QVector<int> LODSystem::findBoundaryEdges(const MeshData& mesh) {
    QMap<QPair<int, int>, int> edgeCount;
    for (const auto& face : mesh.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            int v1 = face.indices[i];
            int v2 = face.indices[(i + 1) % face.indices.size()];
            if (v1 > v2) qSwap(v1, v2);
            edgeCount[std::make_pair(v1, v2)]++;
        }
    }
    QVector<int> boundaries;
    for (auto it = edgeCount.begin(); it != edgeCount.end(); ++it) {
        if (it.value() == 1) {
            boundaries.append(it.key().first);
            boundaries.append(it.key().second);
        }
    }
    return boundaries;
}

inline bool LODSystem::exportLODFiles(const MeshData& mesh, const QString& basePath,
                                       const QVector<LODLevel>& levels) {
    LODResult lods = generateCustomLODs(mesh, levels);
    if (lods.levels.size() != levels.size()) return false;

    QFileInfo fi(basePath);
    QString dir = fi.absolutePath();
    QString baseName = fi.completeBaseName();
    QString ext = fi.suffix();
    if (ext.isEmpty()) ext = "obj";

    bool allOk = true;
    for (int i = 0; i < lods.levels.size(); ++i) {
        QString outPath = QString("%1/%2_LOD%3.%4")
            .arg(dir, baseName).arg(i).arg(ext);

        QFile file(outPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            allOk = false;
            continue;
        }

        QTextStream out(&file);
        const MeshData& lodMesh = lods.levels[i];

        for (int vi = 0; vi < lodMesh.vertices.size(); ++vi) {
            const auto& v = lodMesh.vertices[vi].position;
            out << "v " << v.x() << " " << v.y() << " " << v.z() << "\n";
        }

        for (int ti = 0; ti < lodMesh.uvs.size(); ++ti) {
            out << "vt " << lodMesh.uvs[ti].x() << " " << lodMesh.uvs[ti].y() << "\n";
        }

        for (const auto& face : lodMesh.faces) {
            out << "f";
            for (int j = 0; j < face.indices.size(); ++j) {
                int idx = face.indices[j] + 1;
                if (j < face.uvIndices.size() && face.uvIndices[j] >= 0)
                    out << " " << idx << "/" << (face.uvIndices[j] + 1);
                else
                    out << " " << idx;
            }
            out << "\n";
        }

        file.close();
    }

    return allOk;
}

// Backward compatibility alias
using LODGenerator = LODSystem;

} // namespace ks
