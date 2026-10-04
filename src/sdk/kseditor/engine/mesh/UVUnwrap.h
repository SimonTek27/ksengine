#pragma once

#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QMap>
#include <QSet>
#include <QPair>
#include <QMatrix2x2>
#include <QtMath>

#if HAS_EIGEN
#include <Eigen/Sparse>
#include <Eigen/Dense>
#endif

namespace ks {

struct UVVertex {
    QVector2D uv;
    QVector3D position;
    int meshVertexIndex;
    int islandId;
    float packScale = 1.0f;
};

struct UVFace {
    QVector<int> indices;
    int islandId;
    float area;
    QVector2D minUV, maxUV;
};

struct UVIsland {
    int id;
    QVector<int> faceIndices;
    QVector<int> vertexIndices;
    QVector2D center;
    float area;
    QVector2D boundingBox;
    float rotation;
    bool isFlipped;
};

struct SeamEdge {
    int v1, v2;
    float weight;
    bool isSeam;
};

struct UVUnwrapConfig {
    bool useAngleBased = true;
    bool useAreaWeighting = true;
    bool useCorrectAspect = true;
    bool useSphereProjection = false;
    bool useCylinderProjection = false;
    bool useCubeProjection = false;
    float margin = 0.01f;
    float rotateThreshold = qDegreesToRadians(5.0f);
    int iterations = 100;
    float pinThreshold = 0.01f;
    QSet<int> pinnedVertices;
    QSet<QPair<int, int>> seams;
};

class LSCMUnwrapper {
public:
    static bool unwrap(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                       const QSet<QPair<int, int>>& seams, QVector<QVector2D>& uvs);

private:
#if !HAS_EIGEN
    struct SparseMatrix {
        QMap<int, float> data;
        int rows, cols;

        SparseMatrix(int r = 0, int c = 0) : rows(r), cols(c) {}

        float get(int row, int col) const {
            return data.value(row * cols + col, 0.0f);
        }

        void set(int row, int col, float value) {
            data[row * cols + col] = value;
        }

        void add(int row, int col, float value) {
            int key = row * cols + col;
            data[key] = data.value(key, 0.0f) + value;
        }
    };

    static void buildMatrix(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                           const QSet<QPair<int, int>>& seams, SparseMatrix& L, QVector<float>& b);
    static bool solveLSCM(SparseMatrix& L, QVector<float>& b, QVector<QVector2D>& uvs, int numFree);
#endif
    static void normalizeUVs(QVector<QVector2D>& uvs);
};

class ConformalUnwrapper {
public:
    static bool unwrap(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                       QVector<QVector2D>& uvs, const UVUnwrapConfig& config = UVUnwrapConfig());

private:
    static void computeCotanWeights(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                    QMap<QPair<int, int>, float>& weights);
    static bool solveHarmonicMap(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                const QMap<QPair<int, int>, float>& weights, QVector<QVector2D>& uvs);
    static void applyAreaPreservation(QVector<QVector2D>& uvs, const QVector<QVector3D>& vertices,
                                     const QVector<QVector<int>>& faces);
    static QVector3D computeFaceNormal(const QVector<QVector3D>& v, const QVector<int>& face, int idx);
};

class PackConfig {
public:
    float padding = 0.01f;
    float resolution = 1024.0f;
    bool rotate = true;
    bool mergeOverlapping = true;
    bool packOnlyConnected = false;
    int maxIterations = 100;
    QVector2D gridSpacing{0.01f, 0.01f};
};

class UVPacker {
public:
    static QVector<UVIsland> packIslands(const QVector<UVIsland>& islands, const PackConfig& config = PackConfig());
    static void rotateIsland(UVIsland& island, float angle);
    static void fitToBounds(UVIsland& island, const QVector2D& bounds);
    static bool wouldFit(const UVIsland& island, const QVector2D& position, const QVector2D& canvasSize);
    static QVector2D findBestPosition(const UVIsland& island, const QVector<UVIsland>& packed, const QVector2D& canvasSize);
    static float calculateArea(const QVector<QVector2D>& uvs, const QVector<int>& indices);
    static QVector2D calculateBounds(const QVector<QVector2D>& uvs);
};

class UVMapper {
public:
    static QVector<QVector2D> planarProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                            const QVector3D& direction);
    static QVector<QVector2D> sphericalProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces);
    static QVector<QVector2D> cylindricalProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces);
    static QVector<QVector2D> cubeProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces);
    static QVector<QVector2D> smartProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                          const UVUnwrapConfig& config = UVUnwrapConfig());
    static QVector<QVector2D> followActiveProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                                   const QVector2D& activeUV);

    static void correctAspect(QVector<QVector2D>& uvs, const QVector<QVector3D>& vertices,
                             const QVector<QVector<int>>& faces, bool useScale = true);
    static void scaleToFit(QVector<QVector2D>& uvs, float margin = 0.01f);
    static void centerUVs(QVector<QVector2D>& uvs);
    static void alignIslands(QVector<UVIsland>& islands);

private:
    static QVector3D computeFaceNormal(const QVector<QVector3D>& v, int i0, int i1, int i2);
    static float computeFaceArea(const QVector<QVector3D>& v, int i0, int i1, int i2);
};

class UVIslandDetector {
public:
    static QVector<UVIsland> findIslands(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                        const QSet<QPair<int, int>>& seams);
    static QVector<QPair<int, int>> findSeamsFromAngle(const QVector<QVector3D>& vertices,
                                                       const QVector<QVector<int>>& faces, float angleThreshold);
    static QVector<QPair<int, int>> findSeamsFromUV(const QVector<QVector2D>& uvs, float threshold);

    static QVector<QVector<int>> findConnectedFaces(const QVector<QVector<int>>& faces,
                                                    const QSet<QPair<int, int>>& seamEdges);
    static int findIslandId(const QVector<QVector<int>>& faceIslands, int faceIndex);

private:
    static bool areFacesConnected(const QVector<int>& f1, const QVector<int>& f2,
                                  const QSet<QPair<int, int>>& seamEdges);
    static bool isEdgeSeam(const QVector<int>& f1, const QVector<int>& f2,
                          const QSet<QPair<int, int>>& seamEdges);
};

class UVTransform {
public:
    static void translate(QVector<QVector2D>& uvs, const QVector2D& offset);
    static void scale(QVector<QVector2D>& uvs, const QVector2D& center, float scale);
    static void rotate(QVector<QVector2D>& uvs, const QVector2D& center, float angle);
    static void flip(QVector<QVector2D>& uvs, bool flipHorizontal, bool flipVertical);
    static void weld(QVector<QVector2D>& uvs, float threshold);
    static QVector2D snapToPixel(const QVector2D& uv, float resolution);
    static void alignToAxis(QVector<QVector2D>& uvs, float threshold);
};

class StitchConfig {
public:
    float threshold = 0.01f;
    bool mirror = false;
    int samples = 1;
};

class UVStitcher {
public:
    static bool stitchIslands(const UVIsland& island1, const UVIsland& island2,
                             const QVector<QVector2D>& uvs1, const QVector<QVector2D>& uvs2,
                             const StitchConfig& config = StitchConfig());
    static int findMatchingSeam(const UVIsland& island1, const UVIsland& island2,
                              const QVector<QVector2D>& uvs1, const QVector<QVector2D>& uvs2);
};

class MinStretchUnwrapper {
public:
    static bool minimizeStretch(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                               QVector<QVector2D>& uvs, int iterations = 50);
private:
    static float computeStretch(const QVector<QVector3D>& verts, const QVector<QVector<int>>& faces,
                               const QVector<QVector2D>& uvs, int faceIndex);
    static void smoothUVs(QVector<QVector2D>& uvs, float lambda);
};

class UVQualityChecker {
public:
    struct QualityReport {
        float distortion;
        float maxStretch;
        float avgStretch;
        float islandsOverlap;
        float uvToTexRatio;
        QVector<int> distortedFaces;
        QVector<QPair<int, int>> overlappingIslands;
    };

    static QualityReport analyze(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                 const QVector<QVector2D>& uvs, float texelSize = 0.001f);
    static float computeConformalDistortion(const QVector<QVector3D>& verts, const QVector<QVector2D>& uvs,
                                             int faceIndex);
    static float computeAreaDistortion(const QVector<QVector3D>& verts, const QVector<QVector2D>& uvs,
                                       int faceIndex);
    static bool checkOverlap(const QVector<UVIsland>& islands);
};

}