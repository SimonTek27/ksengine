#include "PhysicsCollisionSystem.h"
#include <QElapsedTimer>
#include <QDebug>
#include <cmath>

#if HAS_VHACD
#include <VHACD.h>
#endif

namespace ks {

// ── Helpers ────────────────────────────────────────────────────────────────

static QVector<QVector3D> pcmPositions(const MeshData& mesh) {
    QVector<QVector3D> verts;
    verts.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices)
        verts.append(v.position);
    return verts;
}

static QVector<QVector<int>> pcmTriangles(const MeshData& mesh) {
    QVector<QVector<int>> tris;
    tris.reserve(mesh.faces.size());
    for (const auto& f : mesh.faces) {
        if (f.indices.size() >= 3) {
            tris.append({f.indices[0], f.indices[1], f.indices[2]});
        }
    }
    return tris;
}

// ── Public API ─────────────────────────────────────────────────────────────

PhysicsResult PhysicsCollisionSystem::generate(const MeshData& mesh, const CollisionConfig& config) {
    PhysicsResult result;
    MeshData workMesh = mesh;
    if (config.weldVertices) {
        workMesh = MeshOperations::weldVertices(workMesh, config.weldThreshold);
    }

    switch (config.shapeType) {
    case CollisionConfig::ShapeType::ConvexHull: {
        auto hull = generateConvexHull(workMesh, config.outputName);
        if (!hull.vertices.isEmpty()) result.hulls.append(hull);
        break;
    }
    case CollisionConfig::ShapeType::Box:
        result.hulls.append(generateBoundingBox(workMesh, config.outputName));
        break;
    case CollisionConfig::ShapeType::Sphere:
        result.hulls.append(generateBoundingSphere(workMesh, config.outputName));
        break;
    case CollisionConfig::ShapeType::Capsule:
        result.hulls.append(generateCapsule(workMesh, config.outputName));
        break;
    case CollisionConfig::ShapeType::Cylinder:
        result.hulls.append(generateCylinder(workMesh, config.outputName));
        break;
    case CollisionConfig::ShapeType::MeshStripped:
        result.hulls.append(generateStripMesh(workMesh, config.outputName, config.simplificationRatio));
        break;
    case CollisionConfig::ShapeType::VHACD: {
        VHACDParams p;
        p.maxNumVerticesPerCH = config.maxVerticesPerHull;
        p.maxConvexHulls = config.maxHulls;
        auto vhacdResult = generateVHACD(workMesh, p);
        result.hulls = vhacdResult.hulls;
        result.executionTimeMs = vhacdResult.executionTimeMs;
        break;
    }
    }

    result.debugMesh = hullsToDebugMesh(result.hulls);
    for (const auto& h : result.hulls) {
        result.totalVertices += h.vertices.size();
        for (const auto& tri : h.triangles)
            result.totalFaces += tri.size() / 3;
        result.totalVolume += h.volume;
    }
    result.success = true;
    return result;
}

PhysicsResult PhysicsCollisionSystem::generateVHACD(const MeshData& mesh, const VHACDParams& params) {
    PhysicsResult result;
    QElapsedTimer timer;
    timer.start();

    auto vertices = pcmPositions(mesh);
    auto triangles = pcmTriangles(mesh);

    if (vertices.isEmpty() || triangles.isEmpty()) {
        result.success = false;
        result.errorMessage = "Empty mesh";
        return result;
    }

#if HAS_VHACD
    try {
        VHACD::IVHACD* vhacd = VHACD::CreateVHACD();
        if (!vhacd) {
            result.success = false;
            result.errorMessage = "Failed to create VHACD instance";
            return result;
        }

        VHACD::IVHACD::Parameters p;
        p.m_resolution = static_cast<unsigned int>(params.resolution);
        p.m_concavity = params.concavity;
        p.m_planeDownsampling = static_cast<int>(params.planeDownsampling);
        p.m_convexhullDownsampling = static_cast<int>(params.convexhullDownsampling);
        p.m_alpha = params.alpha;
        p.m_beta = params.beta;
        p.m_gamma = params.gamma;
        p.m_minVolumePerCH = params.minVolumePerCH;
        p.m_maxNumVerticesPerCH = params.maxNumVerticesPerCH;
        p.m_depth = params.depth;
        p.m_pca = params.pca ? 1 : 0;
        p.m_mode = params.mode ? 1 : 0;
        p.m_convexhullApproximation = params.convexhullApproximation ? 1 : 0;
        p.m_oclAcceleration = params.oclAcceleration ? 1 : 0;

        QVector<float> points;
        points.reserve(vertices.size() * 3);
        for (const auto& v : vertices) {
            points.append(v.x());
            points.append(v.y());
            points.append(v.z());
        }

        QVector<int> triIndices;
        triIndices.reserve(triangles.size() * 3);
        for (const auto& tri : triangles) {
            triIndices.append(tri[0]);
            triIndices.append(tri[1]);
            triIndices.append(tri[2]);
        }

        bool ok = vhacd->Compute(points.constData(), 3, vertices.size(),
                                  triIndices.constData(), 3, triangles.size(), p);
        if (!ok) {
            result.success = false;
            result.errorMessage = "VHACD computation failed";
            vhacd->Release();
            return result;
        }

        unsigned int hullCount = vhacd->GetNConvexHulls();
        if (hullCount > static_cast<unsigned int>(params.maxConvexHulls))
            hullCount = params.maxConvexHulls;

        for (unsigned int i = 0; i < hullCount; ++i) {
            VHACD::IVHACD::ConvexHull ch;
            vhacd->GetConvexHull(i, ch);

            CollisionHull hull;
            hull.name = QString("vhacd_%1").arg(i);
            hull.vertices.reserve(ch.m_nPoints);
            for (unsigned int j = 0; j < ch.m_nPoints; ++j) {
                hull.vertices.append(QVector3D(
                    ch.m_points[j * 3 + 0],
                    ch.m_points[j * 3 + 1],
                    ch.m_points[j * 3 + 2]));
            }

            hull.triangles.reserve(ch.m_nTriangles);
            for (unsigned int j = 0; j < ch.m_nTriangles; ++j) {
                hull.triangles.append({static_cast<int>(ch.m_triangles[j * 3 + 0]),
                                       static_cast<int>(ch.m_triangles[j * 3 + 1]),
                                       static_cast<int>(ch.m_triangles[j * 3 + 2])});
            }

            hull.volume = computeHullVolume(hull);
            hull.center = computeCentroid(hull.vertices);
            result.totalVolume += hull.volume;
            result.hulls.append(hull);
        }

        vhacd->Release();
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = QString("VHACD exception: %1").arg(e.what());
    } catch (...) {
        result.success = false;
        result.errorMessage = "Unknown VHACD exception";
    }
#else
    // Fallback: return original mesh as a single hull
    CollisionHull hull;
    hull.name = "fallback";
    hull.vertices = vertices;
    for (const auto& tri : triangles)
        hull.triangles.append(tri);
    hull.volume = computeHullVolume(hull);
    hull.center = computeCentroid(hull.vertices);
    result.hulls.append(hull);
    result.success = true;
    result.errorMessage = "VHACD not available - using original mesh";
#endif

    result.executionTimeMs = timer.elapsed();
    result.debugMesh = hullsToDebugMesh(result.hulls);
    for (const auto& h : result.hulls) {
        result.totalVertices += h.vertices.size();
        for (const auto& tri : h.triangles)
            result.totalFaces += tri.size();
    }
    return result;
}

// ── Convex Hull ────────────────────────────────────────────────────────────

CollisionHull PhysicsCollisionSystem::generateConvexHull(const MeshData& mesh, const QString& name) {
    CollisionHull hull;
    hull.name = name;

    QVector<QVector3D> allPoints;
    allPoints.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices)
        allPoints.append(v.position);

    computeConvexHullIndexed(allPoints, hull.vertices, hull.triangles);
    hull.center = computeCentroid(hull.vertices);
    hull.volume = computeHullVolume(hull);
    return hull;
}

CollisionHull PhysicsCollisionSystem::generateSingleConvexHull(const MeshData& mesh, const QString& name) {
    return generateConvexHull(mesh, name);
}

// ── Primitive Shapes ───────────────────────────────────────────────────────

CollisionHull PhysicsCollisionSystem::generateBoundingBox(const MeshData& mesh, const QString& name) {
    CollisionHull hull;
    hull.name = name;

    QVector3D mn(1e18f, 1e18f, 1e18f), mx(-1e18f, -1e18f, -1e18f);
    for (const auto& v : mesh.vertices) {
        mn = QVector3D(qMin(mn.x(), v.position.x()), qMin(mn.y(), v.position.y()), qMin(mn.z(), v.position.z()));
        mx = QVector3D(qMax(mx.x(), v.position.x()), qMax(mx.y(), v.position.y()), qMax(mx.z(), v.position.z()));
    }

    hull.vertices = {
        {mn.x(), mn.y(), mn.z()}, {mx.x(), mn.y(), mn.z()},
        {mx.x(), mx.y(), mn.z()}, {mn.x(), mx.y(), mn.z()},
        {mn.x(), mn.y(), mx.z()}, {mx.x(), mn.y(), mx.z()},
        {mx.x(), mx.y(), mx.z()}, {mn.x(), mx.y(), mx.z()}
    };

    hull.triangles = {
        {0,1,2}, {0,2,3}, {1,5,6}, {1,6,2},
        {5,4,7}, {5,7,6}, {4,0,3}, {4,3,7},
        {4,5,1}, {4,1,0}, {3,2,6}, {3,6,7}
    };

    hull.center = (mn + mx) * 0.5f;
    hull.volume = (mx.x() - mn.x()) * (mx.y() - mn.y()) * (mx.z() - mn.z());
    return hull;
}

CollisionHull PhysicsCollisionSystem::generateBoundingSphere(const MeshData& mesh, const QString& name) {
    CollisionHull hull;
    hull.name = name;

    QVector3D center;
    for (const auto& v : mesh.vertices) center += v.position;
    center /= qMax(mesh.vertices.size(), 1);

    float maxRadius = 0.0f;
    for (const auto& v : mesh.vertices)
        maxRadius = qMax(maxRadius, center.distanceToPoint(v.position));

    int segments = 16;
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * M_PI;
        for (int j = 0; j <= segments; ++j) {
            float phi = (float)j / segments * 2.0f * M_PI;
            float x = center.x() + maxRadius * sinf(theta) * cosf(phi);
            float y = center.y() + maxRadius * cosf(theta);
            float z = center.z() + maxRadius * sinf(theta) * sinf(phi);
            hull.vertices.append(QVector3D(x, y, z));
        }
    }

    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < segments; ++j) {
            int a = i * (segments + 1) + j;
            int b = a + 1;
            int c = (i + 1) * (segments + 1) + j;
            int d = c + 1;
            hull.triangles.append({a, c, b});
            hull.triangles.append({b, c, d});
        }
    }

    hull.center = center;
    hull.volume = (4.0f / 3.0f) * M_PI * maxRadius * maxRadius * maxRadius;
    return hull;
}

CollisionHull PhysicsCollisionSystem::generateCapsule(const MeshData& mesh, const QString& name) {
    CollisionHull hull;
    hull.name = name;

    QVector3D mn(1e18f, 1e18f, 1e18f), mx(-1e18f, -1e18f, -1e18f);
    for (const auto& v : mesh.vertices) {
        mn = QVector3D(qMin(mn.x(), v.position.x()), qMin(mn.y(), v.position.y()), qMin(mn.z(), v.position.z()));
        mx = QVector3D(qMax(mx.x(), v.position.x()), qMax(mx.y(), v.position.y()), qMax(mx.z(), v.position.z()));
    }

    float height = mx.y() - mn.y();
    float radius = qMax((mx.x() - mn.x()) * 0.5f, (mx.z() - mn.z()) * 0.5f);
    QVector3D center = (mn + mx) * 0.5f;
    float halfHeight = qMax(0.0f, height * 0.5f - radius);

    int segments = 12;
    for (int i = 0; i <= segments; ++i) {
        float theta = (float)i / segments * M_PI;
        for (int j = 0; j <= segments; ++j) {
            float phi = (float)j / segments * 2.0f * M_PI;
            float r = radius * sinf(theta);
            float x = center.x() + r * cosf(phi);
            float y = center.y() + (theta <= static_cast<float>(M_PI_2) ? halfHeight : -halfHeight) + radius * cosf(theta);
            float z = center.z() + r * sinf(phi);
            hull.vertices.append(QVector3D(x, y, z));
        }
    }

    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < segments; ++j) {
            int a = i * (segments + 1) + j;
            int b = a + 1;
            int c = (i + 1) * (segments + 1) + j;
            int d = c + 1;
            hull.triangles.append({a, c, b});
            hull.triangles.append({b, c, d});
        }
    }

    hull.center = center;
    hull.volume = M_PI * radius * radius * (2.0f * radius + 2.0f * halfHeight);
    return hull;
}

CollisionHull PhysicsCollisionSystem::generateCylinder(const MeshData& mesh, const QString& name) {
    CollisionHull hull;
    hull.name = name;

    QVector3D mn(1e18f, 1e18f, 1e18f), mx(-1e18f, -1e18f, -1e18f);
    for (const auto& v : mesh.vertices) {
        mn = QVector3D(qMin(mn.x(), v.position.x()), qMin(mn.y(), v.position.y()), qMin(mn.z(), v.position.z()));
        mx = QVector3D(qMax(mx.x(), v.position.x()), qMax(mx.y(), v.position.y()), qMax(mx.z(), v.position.z()));
    }

    float radius = qMax((mx.x() - mn.x()) * 0.5f, (mx.z() - mn.z()) * 0.5f);
    float height = mx.y() - mn.y();
    QVector3D center = (mn + mx) * 0.5f;

    int segments = 16;
    for (int i = 0; i <= segments; ++i) {
        float phi = (float)i / segments * 2.0f * M_PI;
        float x = center.x() + radius * cosf(phi);
        float z = center.z() + radius * sinf(phi);
        hull.vertices.append(QVector3D(x, mn.y(), z));
        hull.vertices.append(QVector3D(x, mx.y(), z));
    }

    for (int i = 0; i < segments; ++i) {
        int a = i * 2, b = a + 1, c = (i + 1) * 2, d = c + 1;
        hull.triangles.append({a, c, b});
        hull.triangles.append({b, c, d});
    }

    hull.center = center;
    hull.volume = M_PI * radius * radius * height;
    return hull;
}

CollisionHull PhysicsCollisionSystem::generateStripMesh(const MeshData& mesh, const QString& name, float simplification) {
    CollisionHull hull;
    hull.name = name;

    QVector<QVector3D> points;
    points.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) points.append(v.position);

    hull.vertices = simplifyPointCloud(points, qMax(3, (int)(points.size() * simplification)));

    for (const auto& face : mesh.faces) {
        if (face.indices.size() >= 3) {
            for (int i = 2; i < face.indices.size(); ++i)
                hull.triangles.append({face.indices[0], face.indices[i - 1], face.indices[i]});
        }
    }

    hull.center = computeCentroid(hull.vertices);
    hull.volume = computeHullVolume(hull);
    return hull;
}

// ── Parametric Collision Shapes ────────────────────────────────────────────

MeshData PhysicsCollisionSystem::createBoxCollision(const QVector3D& halfExtents) {
    return MeshOperations::createBox(halfExtents.x() * 2, halfExtents.y() * 2, halfExtents.z() * 2);
}

MeshData PhysicsCollisionSystem::createSphereCollision(float radius, int segments) {
    return MeshOperations::createSphere(radius, segments, segments / 2);
}

MeshData PhysicsCollisionSystem::createCapsuleCollision(float radius, float height, int segments) {
    MeshData result;
    float halfH = height * 0.5f;
    int rings = segments / 2;

    for (int i = 0; i <= rings; ++i) {
        float v = (float)i / rings;
        float theta = v * M_PI;
        float sinT = sinf(theta);
        float cosT = cosf(theta);
        for (int j = 0; j <= segments; ++j) {
            float u = (float)j / segments;
            float phi = u * 2.0f * M_PI;
            float yPos = (v < 0.5f) ? (-halfH - cosT * radius) : (halfH - cosT * radius);
            Vertex vert;
            vert.position = QVector3D(sinT * cosf(phi) * radius, yPos, sinT * sinf(phi) * radius);
            result.vertices.append(vert);
        }
    }

    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            int a = i * (segments + 1) + j;
            int b = a + segments + 1;
            result.faces.append({a, b, a + 1});
            result.faces.append({a + 1, b, b + 1});
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData PhysicsCollisionSystem::createCylinderCollision(float radius, float height, int segments) {
    return MeshOperations::createCylinder(radius, height, segments);
}

// ── Decomposition ──────────────────────────────────────────────────────────

QVector<CollisionHull> PhysicsCollisionSystem::decomposeConvex(const MeshData& mesh, int maxHulls, int maxVertsPerHull) {
    QVector<CollisionHull> hulls;
    if (mesh.vertices.isEmpty() || mesh.faces.isEmpty()) return hulls;

    int totalVerts = mesh.vertices.size();
    int vertsPerHull = qMin(maxVertsPerHull, totalVerts / qMax(1, maxHulls));

    for (int h = 0; h < maxHulls && h * vertsPerHull < totalVerts; ++h) {
        int start = h * vertsPerHull;
        int end = qMin(start + vertsPerHull, totalVerts);

        MeshData chunk;
        for (int i = start; i < end; ++i)
            chunk.vertices.append(mesh.vertices[i]);

        for (const auto& face : mesh.faces) {
            bool allInRange = true;
            for (int idx : face.indices) {
                if (idx < start || idx >= end) { allInRange = false; break; }
            }
            if (allInRange) {
                Face f;
                for (int idx : face.indices) f.indices.append(idx - start);
                chunk.faces.append(f);
            }
        }

        hulls.append(generateConvexHull(chunk, QString("hull_%1").arg(h)));
    }

    return hulls;
}

// ── Conversion Utilities ───────────────────────────────────────────────────

MeshData PhysicsCollisionSystem::hullToMeshData(const CollisionHull& hull) {
    MeshData mesh;
    for (const auto& v : hull.vertices) {
        Vertex vert;
        vert.position = v;
        mesh.vertices.append(vert);
    }
    for (const auto& tri : hull.triangles) {
        if (tri.size() >= 3) {
            Face f;
            f.indices = {tri[0], tri[1], tri[2]};
            mesh.faces.append(f);
        }
    }
    mesh.computeNormals();
    mesh.computeBoundingBox();
    return mesh;
}

MeshData PhysicsCollisionSystem::hullsToSingleMesh(const QVector<CollisionHull>& hulls) {
    MeshData combined;
    int baseIndex = 0;
    for (const auto& hull : hulls) {
        for (const auto& v : hull.vertices) {
            Vertex vert;
            vert.position = v;
            combined.vertices.append(vert);
        }
        for (const auto& tri : hull.triangles) {
            if (tri.size() >= 3) {
                Face f;
                f.indices = {tri[0] + baseIndex, tri[1] + baseIndex, tri[2] + baseIndex};
                combined.faces.append(f);
            }
        }
        baseIndex += hull.vertices.size();
    }
    combined.computeNormals();
    combined.computeBoundingBox();
    return combined;
}

MeshData PhysicsCollisionSystem::optimizeCollisionMesh(const MeshData& mesh, int maxVertices, float tolerance) {
    if (mesh.vertices.size() <= maxVertices)
        return mesh;

    VHACDParams p;
    auto decomp = generateVHACD(mesh, p);
    if (decomp.success && !decomp.hulls.isEmpty()) {
        for (auto& hull : decomp.hulls) {
            while (hull.vertices.size() > maxVertices)
                hull.vertices.resize(hull.vertices.size() / 2);
        }
        return hullsToSingleMesh(decomp.hulls);
    }

    MeshData result = mesh;
    int stride = qMax(1, result.vertices.size() / maxVertices);
    QVector<Vertex> optimized;
    for (int i = 0; i < result.vertices.size(); i += stride)
        optimized.append(result.vertices[i]);
    result.vertices = optimized;

    QVector<Face> optFaces;
    for (const auto& f : result.faces) {
        Face nf;
        for (int idx : f.indices) {
            int newIdx = idx / stride;
            if (!nf.indices.contains(newIdx))
                nf.indices.append(newIdx);
        }
        if (nf.indices.size() >= 3)
            optFaces.append(nf);
    }
    result.faces = optFaces;
    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

// ── Validation & Math ──────────────────────────────────────────────────────

bool PhysicsCollisionSystem::validate(const CollisionHull& hull) {
    if (hull.vertices.size() < 4) return false;
    if (hull.triangles.isEmpty()) return false;
    for (const auto& tri : hull.triangles) {
        if (tri.size() < 3) return false;
    }
    return true;
}

bool PhysicsCollisionSystem::validateCollisionMesh(const MeshData& mesh, QString* error) {
    if (mesh.vertices.isEmpty()) {
        if (error) *error = "Mesh has no vertices";
        return false;
    }
    if (mesh.faces.isEmpty()) {
        if (error) *error = "Mesh has no faces";
        return false;
    }
    for (int i = 0; i < mesh.faces.size(); ++i) {
        for (int j = 0; j < mesh.faces[i].indices.size(); ++j) {
            int idx = mesh.faces[i].indices[j];
            if (idx < 0 || idx >= mesh.vertices.size()) {
                if (error) *error = QString("Face %1 has invalid vertex index %2").arg(i).arg(idx);
                return false;
            }
        }
    }
    return true;
}

float PhysicsCollisionSystem::computeHullVolume(const CollisionHull& hull) {
    if (hull.vertices.size() < 4) return 0.0f;
    float volume = 0.0f;
    for (const auto& tri : hull.triangles) {
        if (tri.size() < 3) continue;
        const auto& a = hull.vertices[tri[0]];
        const auto& b = hull.vertices[tri[1]];
        const auto& c = hull.vertices[tri[2]];
        volume += QVector3D::dotProduct(a, QVector3D::crossProduct(b, c)) / 6.0f;
    }
    return qAbs(volume);
}

float PhysicsCollisionSystem::computeMeshVolume(const MeshData& mesh) {
    if (mesh.faces.isEmpty()) return 0.0f;
    double volume = 0.0;
    for (const auto& face : mesh.faces) {
        if (face.indices.size() < 3) continue;
        const auto& a = mesh.vertices[face.indices[0]].position;
        const auto& b = mesh.vertices[face.indices[1]].position;
        const auto& c = mesh.vertices[face.indices[2]].position;
        volume += QVector3D::dotProduct(a, QVector3D::crossProduct(b, c));
    }
    return static_cast<float>(std::abs(volume) / 6.0);
}

QVector3D PhysicsCollisionSystem::computeMeshCenter(const MeshData& mesh) {
    if (mesh.vertices.isEmpty()) return QVector3D(0, 0, 0);
    QVector3D center;
    for (const auto& v : mesh.vertices) center += v.position;
    return center / mesh.vertices.size();
}

// ── Debug ──────────────────────────────────────────────────────────────────

MeshData PhysicsCollisionSystem::hullsToDebugMesh(const QVector<CollisionHull>& hulls) {
    MeshData debug;
    int vertOffset = 0;
    for (const auto& hull : hulls) {
        for (const auto& v : hull.vertices) {
            Vertex vert;
            vert.position = v;
            vert.color = QVector4D(0, 1, 0, 0.3f);
            debug.vertices.append(vert);
        }
        for (const auto& tri : hull.triangles) {
            if (tri.size() >= 3) {
                Face f;
                f.indices = {vertOffset + tri[0], vertOffset + tri[1], vertOffset + tri[2]};
                debug.faces.append(f);
            }
        }
        vertOffset += hull.vertices.size();
    }
    debug.computeNormals();
    debug.computeBoundingBox();
    return debug;
}

// ── Quickhull 3D Convex Hull ──────────────────────────────────────────────

PhysicsCollisionSystem::HullFace::HullFace(int a, int b, int c, const QVector<QVector3D>& pts)
    : removed(false) {
    indices[0] = a;
    indices[1] = b;
    indices[2] = c;
    updateNormal(pts);
}

void PhysicsCollisionSystem::HullFace::updateNormal(const QVector<QVector3D>& pts) {
    normal = computePlaneNormal(pts[indices[0]], pts[indices[1]], pts[indices[2]]);
    planeDist = -QVector3D::dotProduct(normal, pts[indices[0]]);
}

bool PhysicsCollisionSystem::HullFace::isPointAbove(const QVector3D& p) const {
    return QVector3D::dotProduct(normal, p) + planeDist > 1e-6f;
}

int PhysicsCollisionSystem::findExtremePoint(const QVector<QVector3D>& pts, const QVector3D& dir) {
    int best = 0;
    float bestDot = QVector3D::dotProduct(pts[0], dir);
    for (int i = 1; i < pts.size(); ++i) {
        float d = QVector3D::dotProduct(pts[i], dir);
        if (d > bestDot) {
            bestDot = d;
            best = i;
        }
    }
    return best;
}

QVector3D PhysicsCollisionSystem::computePlaneNormal(const QVector3D& a, const QVector3D& b, const QVector3D& c) {
    QVector3D n = QVector3D::crossProduct(b - a, c - a);
    float len = n.length();
    if (len < 1e-12f) return QVector3D(0, 1, 0);
    return n / len;
}

float PhysicsCollisionSystem::pointToPlaneDist(const QVector3D& p, const QVector3D& n, float d) {
    return QVector3D::dotProduct(n, p) + d;
}

bool PhysicsCollisionSystem::edgeOnHorizon(const QVector<QPair<int,int>>& horizon, int a, int b) {
    for (const auto& e : horizon) {
        if ((e.first == a && e.second == b) || (e.first == b && e.second == a))
            return true;
    }
    return false;
}

void PhysicsCollisionSystem::computeConvexHullIndexed(const QVector<QVector3D>& points,
                                                       QVector<QVector3D>& outVerts,
                                                       QVector<QVector<int>>& outTris) {
    if (points.size() < 4) {
        outVerts = points;
        return;
    }

    QVector<QVector3D> pts;
    pts.reserve(points.size());
    for (const auto& p : points) {
        bool dup = false;
        for (const auto& q : pts) {
            if ((p - q).lengthSquared() < 1e-10f) { dup = true; break; }
        }
        if (!dup) pts.append(p);
    }

    if (pts.size() < 4) {
        outVerts = pts;
        return;
    }

    int i0 = findExtremePoint(pts, QVector3D(1, 0, 0));
    int i1 = findExtremePoint(pts, QVector3D(-1, 0, 0));

    QVector3D dir2 = QVector3D::crossProduct(pts[i1] - pts[i0], QVector3D(0, 1, 0));
    if (dir2.lengthSquared() < 1e-12f)
        dir2 = QVector3D::crossProduct(pts[i1] - pts[i0], QVector3D(0, 0, 1));
    int i2 = findExtremePoint(pts, dir2);

    QVector3D normal012 = computePlaneNormal(pts[i0], pts[i1], pts[i2]);
    int i3 = findExtremePoint(pts, normal012);
    if (i3 == i0 || i3 == i1 || i3 == i2) {
        QVector3D mn(1e18f, 1e18f, 1e18f), mx(-1e18f, -1e18f, -1e18f);
        for (const auto& p : pts) {
            mn = QVector3D(qMin(mn.x(), p.x()), qMin(mn.y(), p.y()), qMin(mn.z(), p.z()));
            mx = QVector3D(qMax(mx.x(), p.x()), qMax(mx.y(), p.y()), qMax(mx.z(), p.z()));
        }
        outVerts = {mn,
                    QVector3D(mx.x(), mn.y(), mn.z()),
                    QVector3D(mx.x(), mx.y(), mn.z()),
                    QVector3D(mn.x(), mx.y(), mn.z()),
                    QVector3D(mn.x(), mn.y(), mx.z()),
                    QVector3D(mx.x(), mn.y(), mx.z()),
                    mx,
                    QVector3D(mn.x(), mx.y(), mx.z())};
        outTris = {{0,1,2},{0,2,3},{4,6,5},{4,7,6},
                   {1,5,6},{1,6,2},{0,3,7},{0,7,4},
                   {0,4,5},{0,5,1},{2,6,7},{2,7,3}};
        return;
    }

    QVector3D n012 = computePlaneNormal(pts[i0], pts[i1], pts[i2]);
    if (QVector3D::dotProduct(n012, pts[i3] - pts[i0]) > 0) {
        qSwap(i1, i2);
    }

    QVector<HullFace> faces;
    faces.append(HullFace(i0, i1, i2, pts));
    faces.append(HullFace(i0, i3, i1, pts));
    faces.append(HullFace(i1, i3, i2, pts));
    faces.append(HullFace(i2, i3, i0, pts));

    QVector3D centroid = (pts[i0] + pts[i1] + pts[i2] + pts[i3]) * 0.25f;
    for (auto& f : faces) {
        QVector3D faceCenter = (pts[f.indices[0]] + pts[f.indices[1]] + pts[f.indices[2]]) / 3.0f;
        if (QVector3D::dotProduct(f.normal, faceCenter - centroid) < 0) {
            qSwap(f.indices[1], f.indices[2]);
            f.updateNormal(pts);
        }
    }

    QSet<int> usedPts;
    usedPts.insert(i0);
    usedPts.insert(i1);
    usedPts.insert(i2);
    usedPts.insert(i3);

    for (int pi = 0; pi < pts.size(); ++pi) {
        if (usedPts.contains(pi)) continue;
        const QVector3D& pt = pts[pi];

        QVector<int> visibleFaces;
        for (int fi = 0; fi < faces.size(); ++fi) {
            if (!faces[fi].removed && faces[fi].isPointAbove(pt))
                visibleFaces.append(fi);
        }

        if (visibleFaces.isEmpty()) continue;

        QVector<QPair<int,int>> horizonEdges;
        for (int vi : visibleFaces) {
            for (int e = 0; e < 3; ++e) {
                int a = faces[vi].indices[e];
                int b = faces[vi].indices[(e + 1) % 3];

                bool sharedVisible = false;
                for (int vj : visibleFaces) {
                    if (vj == vi) continue;
                    for (int f = 0; f < 3; ++f) {
                        int ca = faces[vj].indices[f];
                        int cb = faces[vj].indices[(f + 1) % 3];
                        if (ca == b && cb == a) { sharedVisible = true; break; }
                    }
                    if (sharedVisible) break;
                }

                if (!sharedVisible) {
                    bool found = false;
                    for (auto& he : horizonEdges) {
                        if (he.first == b && he.second == a) { found = true; break; }
                    }
                    if (!found)
                        horizonEdges.append(std::make_pair(a, b));
                }
            }
        }

        for (int vi : visibleFaces)
            faces[vi].removed = true;

        for (const auto& he : horizonEdges) {
            faces.append(HullFace(he.first, he.second, pi, pts));
            auto& nf = faces.last();
            QVector3D faceCenter = (pts[nf.indices[0]] + pts[nf.indices[1]] + pts[nf.indices[2]]) / 3.0f;
            if (QVector3D::dotProduct(nf.normal, faceCenter - centroid) < 0) {
                qSwap(nf.indices[1], nf.indices[2]);
                nf.updateNormal(pts);
            }
        }

        usedPts.insert(pi);
    }

    QMap<int, int> indexMap;
    for (const auto& f : faces) {
        if (f.removed) continue;
        for (int k = 0; k < 3; ++k) {
            int orig = f.indices[k];
            if (!indexMap.contains(orig)) {
                indexMap[orig] = outVerts.size();
                outVerts.append(pts[orig]);
            }
        }
        outTris.append({indexMap[f.indices[0]], indexMap[f.indices[1]], indexMap[f.indices[2]]});
    }
}

QVector3D PhysicsCollisionSystem::computeCentroid(const QVector<QVector3D>& points) {
    if (points.isEmpty()) return QVector3D();
    QVector3D sum;
    for (const auto& p : points) sum += p;
    return sum / (float)points.size();
}

QVector<QVector3D> PhysicsCollisionSystem::simplifyPointCloud(const QVector<QVector3D>& points, int maxPoints) {
    if (points.size() <= maxPoints) return points;
    QVector<QVector3D> result;
    float step = (float)points.size() / maxPoints;
    for (int i = 0; i < maxPoints; ++i) {
        int idx = qMin((int)(i * step), points.size() - 1);
        result.append(points[idx]);
    }
    return result;
}

} // namespace ks
