#pragma once

#include <QObject>
#include <QVector>
#include <QMap>
#include <QVector3D>
#include <QVector2D>
#include <QMatrix4x4>

namespace ks {
namespace tools {

struct LODGenerator {
    struct Options {
        int lodCount = 4;
        float reductionRatio = 0.5f;      // Target reduction per LOD
        float minTriangleRatio = 0.01f;   // Minimum triangles vs original
        bool useQuadricError = true;      // Use quadric error metrics
        bool preserveBoundaries = true;   // Preserve mesh boundaries
        bool preserveUVSeams = true;      // Preserve UV seam edges
        bool preserveSharpEdges = true;   // Preserve sharp edges (angle > threshold)
        float sharpEdgeAngle = 45.0f;     // Degrees
        bool generateCollision = false;   // Generate collision mesh for each LOD
        int maxTriangles = 10000;         // Cap triangles per LOD
    };

    struct LODLevel {
        int level = 0;
        QVector<QVector3D> vertices;
        QVector<int> indices;
        QVector<QVector3D> normals;
        QVector<QVector2D> uvs;
        QVector<QVector3D> tangents;
        int triangleCount = 0;
        float errorMetric = 0.0f;
        float screenSize = 0.0f;  // Screen size threshold for switching
    };

    struct Result {
        QVector<LODLevel> levels;
        QVector3D originalBoundsMin;
        QVector3D originalBoundsMax;
        float originalSurfaceArea = 0.0f;
        bool success = false;
        QString errorMessage;
    };

    // Main entry point
    static Result generate(const QVector<QVector3D>& vertices,
                           const QVector<int>& indices,
                           const QVector<QVector3D>& normals = {},
                           const QVector<QVector2D>& uvs = {},
                           const Options& options = Options());

    // Generate from existing mesh data
    static Result generateFromMeshData(const QVector<QVector3D>& vertices,
                                       const QVector<int>& indices,
                                       const QVector<QVector3D>& normals,
                                       const QVector<QVector2D>& uvs,
                                       const Options& options = Options());

private:
    // Quadric Error Metrics implementation
    struct Quadric {
        float a[10] = {0};  // Symmetric 4x4 matrix: [xx, xy, xz, xw, yy, yz, yw, zz, zw, ww]
        
        Quadric() = default;
        Quadric(const QVector3D& p, const QVector3D& n);  // Plane quadric
        
        Quadric operator+(const Quadric& other) const;
        Quadric& operator+=(const Quadric& other);
        
        float evaluate(const QVector3D& v) const;
        bool solve(QVector3D& out) const;  // Find minimum
    };

    struct EdgeCollapse {
        int v1, v2;
        QVector3D optimalPosition;
        float cost;
        int newVertexIndex;
        
        bool operator<(const EdgeCollapse& other) const { return cost > other.cost; }  // Min-heap
    };

    struct VertexData {
        QVector3D position;
        QVector3D normal;
        QVector2D uv;
        QVector<int> triangles;  // Adjacent triangles
        QVector<int> edges;      // Adjacent edges
        Quadric quadric;
        bool isBoundary = false;
        bool isUVSeam = false;
        bool isSharp = false;
        bool removed = false;
    };

    struct EdgeData {
        int v1, v2;
        int tri1, tri2;  // Adjacent triangles (-1 for boundary)
        bool isBoundary = false;
        bool isUVSeam = false;
        bool isSharp = false;
        float cost = 0;
        QVector3D optimalPosition;
    };

    // Build quadrics from mesh
    static void buildQuadrics(const QVector<QVector3D>& vertices,
                              const QVector<int>& indices,
                              const QVector<QVector3D>& normals,
                              QVector<VertexData>& vertexData,
                              QVector<EdgeData>& edges);

    // Identify feature edges
    static void identifyFeatureEdges(const QVector<QVector3D>& vertices,
                                     const QVector<int>& indices,
                                     const QVector<QVector3D>& normals,
                                     const QVector<QVector2D>& uvs,
                                     QVector<VertexData>& vertexData,
                                     QVector<EdgeData>& edges,
                                     float sharpAngle);

    // Compute edge collapse costs
    static void computeCollapseCosts(QVector<VertexData>& vertexData,
                                     QVector<EdgeData>& edges,
                                     QVector<EdgeCollapse>& collapseHeap);

    // Perform edge collapse
    static bool collapseEdge(QVector<VertexData>& vertexData,
                             QVector<EdgeData>& edges,
                             QVector<int>& indices,
                             QVector<QVector3D>& normals,
                             QVector<QVector2D>& uvs,
                             const EdgeCollapse& collapse);

    // Update mesh after collapse
    static void updateMeshTopology(QVector<VertexData>& vertexData,
                                   QVector<EdgeData>& edges,
                                   QVector<int>& indices,
                                   QVector<QVector3D>& normals,
                                   QVector<QVector2D>& uvs,
                                   int removedVertex);

    // Calculate screen size threshold
    static float calculateScreenSize(const LODLevel& lod, float fov, int screenHeight);

    // Generate collision mesh (convex hull)
    static LODLevel generateCollisionMesh(const LODLevel& lod);
    
    // Validate LOD quality
    static bool validateLOD(const LODLevel& original, const LODLevel& reduced, float maxError);
};

} // namespace tools
} // namespace ks