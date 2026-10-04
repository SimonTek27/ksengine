#pragma once

#include "MeshOperations.h"
#include <QVector>
#include <QVector3D>
#include <QString>
#include <QMap>
#include <QSet>
#include <QPair>
#include <vector>

namespace ks {

// ── Configuration ──────────────────────────────────────────────────────────

struct CollisionConfig {
    enum class ShapeType { ConvexHull, Box, Sphere, Capsule, Cylinder, MeshStripped, VHACD };
    enum class CollisionQuality { Fast, Medium, High };

    ShapeType shapeType = ShapeType::ConvexHull;
    CollisionQuality quality = CollisionQuality::Medium;

    int maxVerticesPerHull = 64;
    int maxHulls = 16;
    float mergeThreshold = 0.01f;
    float simplificationRatio = 0.3f;

    bool weldVertices = true;
    float weldThreshold = 0.001f;
    bool fillHoles = true;
    int maxHoleSize = 100;
    bool removeInterior = true;

    QString outputName = "collision";
};

struct VHACDParams {
    double resolution = 100000.0;
    double concavity = 0.001;
    double planeDownsampling = 4.0;
    double convexhullDownsampling = 4.0;
    double alpha = 0.05;
    double beta = 0.05;
    double gamma = 0.0005;
    double minVolumePerCH = 0.0001;
    int maxNumVerticesPerCH = 64;
    int depth = 20;
    int maxConvexHulls = 1024;
    bool pca = false;
    bool mode = false;
    bool convexhullApproximation = true;
    bool oclAcceleration = false;
};

// ── Data Structures ────────────────────────────────────────────────────────

struct CollisionHull {
    QString name;
    QVector<QVector3D> vertices;
    QVector<QVector<int>> triangles;
    QVector3D center;
    float volume = 0.0f;
};

struct PhysicsResult {
    QVector<CollisionHull> hulls;
    MeshData debugMesh;
    int totalVertices = 0;
    int totalFaces = 0;
    float totalVolume = 0.0f;
    double executionTimeMs = 0.0;
    bool success = false;
    QString errorMessage;
};

// ── Unified Physics/Collision System ───────────────────────────────────────

class PhysicsCollisionSystem {
public:
    // ── High-level generation ──
    static PhysicsResult generate(const MeshData& mesh, const CollisionConfig& config = CollisionConfig());
    static PhysicsResult generateVHACD(const MeshData& mesh, const VHACDParams& params = VHACDParams());

    // ── Convex hull ──
    static CollisionHull generateConvexHull(const MeshData& mesh, const QString& name = "hull");
    static CollisionHull generateSingleConvexHull(const MeshData& mesh, const QString& name = "hull");

    // ── Primitive shapes ──
    static CollisionHull generateBoundingBox(const MeshData& mesh, const QString& name = "box");
    static CollisionHull generateBoundingSphere(const MeshData& mesh, const QString& name = "sphere");
    static CollisionHull generateCapsule(const MeshData& mesh, const QString& name = "capsule");
    static CollisionHull generateCylinder(const MeshData& mesh, const QString& name = "cylinder");
    static CollisionHull generateStripMesh(const MeshData& mesh, const QString& name = "strip", float simplification = 0.3f);

    // ── Parametric collision shapes ──
    static MeshData createBoxCollision(const QVector3D& halfExtents);
    static MeshData createSphereCollision(float radius, int segments = 16);
    static MeshData createCapsuleCollision(float radius, float height, int segments = 16);
    static MeshData createCylinderCollision(float radius, float height, int segments = 16);

    // ── Decomposition ──
    static QVector<CollisionHull> decomposeConvex(const MeshData& mesh, int maxHulls = 16, int maxVertsPerHull = 64);

    // ── Conversion utilities ──
    static MeshData hullToMeshData(const CollisionHull& hull);
    static MeshData hullsToSingleMesh(const QVector<CollisionHull>& hulls);
    static MeshData optimizeCollisionMesh(const MeshData& mesh, int maxVertices = 256, float tolerance = 0.01f);

    // ── Validation ──
    static bool validate(const CollisionHull& hull);
    static bool validateCollisionMesh(const MeshData& mesh, QString* error = nullptr);
    static float computeHullVolume(const CollisionHull& hull);
    static float computeMeshVolume(const MeshData& mesh);
    static QVector3D computeMeshCenter(const MeshData& mesh);

    // ── Debug ──
    static MeshData hullsToDebugMesh(const QVector<CollisionHull>& hulls);

private:
    // Quickhull 3D convex hull
    struct HullFace {
        int indices[3];
        QVector3D normal;
        float planeDist;
        bool removed = false;

        HullFace() = default;
        HullFace(int a, int b, int c, const QVector<QVector3D>& pts);
        void updateNormal(const QVector<QVector3D>& pts);
        bool isPointAbove(const QVector3D& p) const;
    };

    static QVector3D computeCentroid(const QVector<QVector3D>& points);
    static QVector<QVector3D> simplifyPointCloud(const QVector<QVector3D>& points, int maxPoints);
    static int findExtremePoint(const QVector<QVector3D>& pts, const QVector3D& dir);
    static QVector3D computePlaneNormal(const QVector3D& a, const QVector3D& b, const QVector3D& c);
    static float pointToPlaneDist(const QVector3D& p, const QVector3D& n, float d);
    static bool edgeOnHorizon(const QVector<QPair<int,int>>& horizon, int a, int b);
    static void computeConvexHullIndexed(const QVector<QVector3D>& points,
                                          QVector<QVector3D>& outVerts,
                                          QVector<QVector<int>>& outTris);
};

// Backward compatibility aliases
using CollisionResult = PhysicsResult;

} // namespace ks

Q_DECLARE_METATYPE(ks::CollisionConfig)
Q_DECLARE_METATYPE(ks::VHACDParams)
Q_DECLARE_METATYPE(ks::PhysicsResult)

