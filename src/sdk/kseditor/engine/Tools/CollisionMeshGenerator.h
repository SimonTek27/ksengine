#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QMatrix4x4>
#include <QMap>
#include <QVariant>

namespace ks {
namespace tools {

// ─── Collision Mesh Generator ────────────────────────────────────────────────

struct CollisionMeshOptions {
    enum class Type {
        ConvexHull,
        Box,
        Sphere,
        Capsule,
        Cylinder,
        Compound,
        VHACD  // Convex decomposition
    };
    
    Type type = Type::ConvexHull;
    int maxHulls = 16;           // For VHACD
    int maxVerticesPerHull = 64; // For VHACD
    float concavity = 0.0025f;   // For VHACD
    float volumeThreshold = 0.0001f;
    bool shrinkWrap = false;
    float skinWidth = 0.01f;
    bool simplify = true;
    float simplifyThreshold = 0.001f;
};

struct CollisionHull {
    QVector<QVector3D> vertices;
    QVector<int> indices;        // Triangle indices
    QVector<QVector3D> normals;  // Face normals
    QMatrix4x4 localTransform;
    float volume = 0;
};

struct CollisionMeshResult {
    QVector<CollisionHull> hulls;
    float totalVolume = 0;
    int totalTriangles = 0;
    QString error;
    bool success = false;
};

class CollisionMeshGenerator {
    public:
    explicit CollisionMeshGenerator();
    ~CollisionMeshGenerator() = default;

    // Generate collision mesh from source vertices/indices
    CollisionMeshResult generate(const QVector<QVector3D>& vertices,
                                 const QVector<int>& indices,
                                 const CollisionMeshOptions& options = CollisionMeshOptions());

    // Generate from OBJ file
    CollisionMeshResult generateFromFile(const QString& filePath,
                                         const CollisionMeshOptions& options = CollisionMeshOptions());

    // Generate compound collision (multiple hulls from groups)
    CollisionMeshResult generateCompound(const QMap<QString, QPair<QVector<QVector3D>, QVector<int>>>& meshGroups,
                                          const CollisionMeshOptions& options = CollisionMeshOptions());

    // Utility: compute convex hull using quickhull
    static QVector<int> computeConvexHull(const QVector<QVector3D>& points);
    
    // Utility: simplify mesh (quadric decimation)
    static void simplifyMesh(QVector<QVector3D>& vertices,
                            QVector<int>& indices,
                            float threshold);

    // Utility: compute oriented bounding box
    static QMatrix4x4 computeOBB(const QVector<QVector3D>& vertices,
                                 const QVector<int>& indices);

private:
    CollisionMeshResult generateConvexHull(const QVector<QVector3D>& vertices,
                                           const QVector<int>& indices,
                                           const CollisionMeshOptions& options);
    
    CollisionMeshResult generateVHACD(const QVector<QVector3D>& vertices,
                                      const QVector<int>& indices,
                                      const CollisionMeshOptions& options);
    
    CollisionMeshResult generatePrimitive(const QVector<QVector3D>& vertices,
                                          const QVector<int>& indices,
                                          const CollisionMeshOptions& options);
    
    static QVector<QVector3D> computeHullVertices(const QVector<int>& hullIndices,
                                                  const QVector<QVector3D>& allVertices);
    static float computeHullVolume(const QVector<QVector3D>& hullVerts,
                                   const QVector<int>& hullIndices);
};

// ─── LOD Generator ──────────────────────────────────────────────────────────

struct LODOptions {
    enum class Method {
        QuadricDecimation,
        ScreenSpaceError,
        ProxyGeometry,
        Impostor
    };
    
    Method method = Method::QuadricDecimation;
    int targetCount = 4;           // Number of LOD levels (including LOD0)
    float reductionRatio = 0.5f;   // Reduction per level
    float minScreenSize = 1.0f;    // Screen-space error threshold
    float minVertexCount = 50;     // Don't reduce below this
    bool preserveBoundaries = true;
    bool preserveUVSeams = true;
    bool generateImpostors = false; // For last LOD
    int impostorResolution = 512;
    int maxLODDistance = 1000;     // Distance at which highest LOD used
};

struct LODLevel {
    int level = 0;
    QVector<QVector3D> vertices;
    QVector<int> indices;
    QVector<QVector3D> normals;
    QVector<QVector2D> uvs;
    float screenSize = 0;
    float error = 0;
    int triangleCount = 0;
    float reductionRatio = 1.0f;
};

struct LODResult {
    QVector<LODLevel> levels;
    QString error;
    bool success = false;
};

} // namespace tools
} // namespace ks