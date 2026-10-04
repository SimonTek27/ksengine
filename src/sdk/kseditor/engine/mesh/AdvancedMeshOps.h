#pragma once

#include "MeshOperations.h"
#include <QMap>
#include <QSet>
#include <QQueue>
#include <QVector3D>

namespace ks {

struct BSPNode {
    QVector3D normal;
    float distance;
    int frontIndex;
    int backIndex;
    QVector<int> polygonIndices;
    bool isLeaf = false;
};

struct BooleanConfig {
    bool useSwap = false;
    bool useDissolve = true;
    float dissolveDistance = 0.0001f;
    bool deleteOriginal = true;
    bool preserveMaterials = false;
    bool snapToGrid = false;
    float gridSize = 0.01f;
    bool recalculateNormals = false;
    float sliceOffset = 0.0f;
};

class BooleanCsg {
public:
    static MeshData unite(const MeshData& a, const MeshData& b, const BooleanConfig& config = BooleanConfig());
    static MeshData intersect(const MeshData& a, const MeshData& b, const BooleanConfig& config = BooleanConfig());
    static MeshData subtract(const MeshData& a, const MeshData& b, const BooleanConfig& config = BooleanConfig());
    static MeshData slice(const MeshData& a, const MeshData& b, const BooleanConfig& config = BooleanConfig());

private:
    static MeshData booleanOperation(const MeshData& a, const MeshData& b, int operationType, const BooleanConfig& config);

    struct EdgePair {
        int v1a, v2a;
        int v1b, v2b;
        float paramA, paramB;
        QVector3D point;
        QVector3D normal;
    };

    struct Polygon {
        QVector<int> indices;
        QVector3D normal;
        int meshId;
        int material = 0;
        bool isCoplanar = false;
    };

    static QVector<Polygon> extractPolygons(const MeshData& mesh);
    static void clipPolygons(QVector<Polygon>& polygons, const QVector3D& planeNormal, float planeDistance, bool keepPositive);
    static void addPolygonToMesh(MeshData& result, const Polygon& poly, int meshId);
    static QVector3D computeCentroid(const QVector<QVector3D>& vertices);
    static bool isPointInsidePolygon(const QVector3D& point, const QVector<int>& indices, const QVector<QVector3D>& vertices);
    static QVector3D lineIntersection(const QVector3D& p1, const QVector3D& p2, const QVector3D& planeNormal, float planeDist);

    static QVector<EdgePair> findEdgeIntersections(const MeshData& a, const MeshData& b);
    static void splitPolygonAtEdge(const Polygon& poly, const EdgePair& edge, QVector<Polygon>& front, QVector<Polygon>& back);

public:
    static void mergeDuplicateVertices(MeshData& mesh, float tolerance);

private:

    static QVector3D polygonNormal(const Polygon& poly, const QVector<QVector3D>& vertices);
    static float planeDistance(const QVector3D& normal, const QVector3D& point);
};

class ConvexHull {
public:
    static MeshData compute(const QVector<QVector3D>& points);
    static MeshData compute(const MeshData& mesh);

private:
    struct Face {
        int v1, v2, v3;
        QVector3D normal;
        bool visible;
    };

    static QVector<Face> createHull(const QVector<QVector3D>& points);
    static bool isVisible(const Face& face, const QVector3D& point, const QVector<QVector3D>& vertices);
};

class Decimation {
public:
    static MeshData simplify(const MeshData& mesh, float targetRatio, float angleThreshold = 0.1f);
    static MeshData decimateCluster(const MeshData& mesh, float targetFaceCount);
    static MeshData decimateQuadric(const MeshData& mesh, float targetRatio);

private:
    struct Quadric {
        float a = 0, b = 0, c = 0, d = 0, e = 0;
        float f = 0, g = 0, h = 0, i = 0, j = 0;
    };

    struct MeshVertex {
        QVector3D position;
        Quadric q;
        int representative;
    };

    static Quadric computeQuadric(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3);
    static float quadricError(const Quadric& q, const QVector3D& v);
    static QVector3D optimalVertex(const Quadric& q1, const Quadric& q2);
    static void computePairing(QVector<MeshVertex>& vertices, const QVector<QPair<int, int>>& edges);
};

class Remeshing {
public:
    static MeshData quadRemesh(const MeshData& mesh, int targetCount = 1000);
    static MeshData triRemesh(const MeshData& mesh);
    static MeshData pentagonalRemesh(const MeshData& mesh);
    static MeshData isoSurface(const MeshData& mesh, float isovalue);

private:
    struct Voxel {
        QVector3D position;
        float distance;
        bool occupied;
    };

    static QVector<Voxel> voxelize(const MeshData& mesh, float resolution);
    static MeshData extractSurface(const QVector<Voxel>& voxels, float isovalue);
    static void refineVertices(MeshData& mesh, int iterations);
};

class PolygonOperations {
public:
    static MeshData fillHoles(const MeshData& mesh, int maxHoleSize = 100);
    static QVector<QVector<int>> findHoles(const MeshData& mesh);
    static MeshData triangulateQuads(const MeshData& mesh, bool beauty = true);
    static MeshData convertToQuads(const MeshData& mesh, float angleThreshold = qDegreesToRadians(30.0f));
    static MeshData splitNonPlanarFaces(const MeshData& mesh, float threshold = 0.001f);
    static MeshData planarFaces(const MeshData& mesh, float threshold = 0.001f);
    static MeshData mergeFaces(const MeshData& mesh, const QVector<int>& faceIndices);
    static MeshData separateFaces(const MeshData& mesh, const QVector<int>& faceIndices);
    static MeshData symmetricDifference(const MeshData& a, const MeshData& b);

    static QVector3D triangleArea(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3);
    static float triangleAspectRatio(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3);
    static float triangleQuality(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3);

    static int findSharedEdge(const Face& f1, const Face& f2);
    static bool shareEdge(const Face& f1, const Face& f2);
    static QVector<int> getAdjacentFaces(const MeshData& mesh, int faceIndex);
    static int getEdgeFaceCount(const MeshData& mesh, int v1, int v2);
};

class KnifeTool {
public:
    struct CutPoint {
        QVector3D position;
        int vertexIndex;
        bool isNewVertex;
        float t;
    };

    struct CutSegment {
        CutPoint start;
        CutPoint end;
        QVector<CutPoint> points;
    };

    static MeshData cut(const MeshData& mesh, const QVector3D& start, const QVector3D& end, bool snapToVertex = true);
    static MeshData cutFaces(const MeshData& mesh, const QVector<CutSegment>& segments);
    static QVector<CutPoint> intersectWithPlane(const MeshData& mesh, const QVector3D& point, const QVector3D& normal);
    static int splitEdge(MeshData& mesh, int v1, int v2, float t);
    static QVector<int> splitFace(MeshData& mesh, int faceIndex, const QVector3D& point);
};

class LoopCut {
public:
    static MeshData cut(const MeshData& mesh, int cuts, const QVector3D& center = QVector3D(), const QVector3D& normal = QVector3D(0, 0, 1));
    static QVector<QVector<int>> findEdgeLoops(const MeshData& mesh, int startEdge);
    static QVector<QVector<int>> findFaceLoops(const MeshData& mesh, const QVector<int>& edgeLoop);
};

class Bisect {
public:
    static MeshData cut(const MeshData& mesh, const QVector3D& planePoint, const QVector3D& planeNormal, bool cutCenter = true, bool clearOuter = false, bool clearInner = false);
    static QPair<MeshData, MeshData> split(const MeshData& mesh, const QVector3D& planePoint, const QVector3D& planeNormal);
    static QVector3D projectToPlane(const QVector3D& point, const QVector3D& planePoint, const QVector3D& planeNormal);
};

class VertexConnectivity {
public:
    static QVector<QVector<int>> findConnectedVertices(const MeshData& mesh, int startVertex);
    static QVector<QVector<int>> findConnectedEdges(const MeshData& mesh, const QVector<int>& vertices);
    static QVector<QVector<int>> findVertexRings(const MeshData& mesh, int vertexIndex);
    static int getVertexValence(const MeshData& mesh, int vertexIndex);
    static QVector<int> findFeatureVertices(const MeshData& mesh, float angleThreshold = qDegreesToRadians(30.0f));
};

}