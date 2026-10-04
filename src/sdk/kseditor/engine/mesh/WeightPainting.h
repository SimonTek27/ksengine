#pragma once

#include <QVector>
#include <QVector3D>
#include <QMap>
#include <QColor>
#include <QImage>
#include <QtMath>

namespace ks {

struct WeightVertex {
    int vertexIndex;
    QMap<int, float> weights;
};

struct BrushSettings {
    float strength = 0.5f;
    float radius = 1.0f;
    float falloff = 1.0f;
    float weight = 1.0f;
    float pressureStrength = 1.0f;
    float pressureRadius = 0.0f;

    enum class FalloffType { Constant, Linear, Smooth, Sharp, Root };
    FalloffType falloffType = FalloffType::Smooth;

    bool useFalloff = true;
    bool useGradient = true;
    bool useNormal = true;
    bool useFrontfaces = true;
    float normalRange = qDegreesToRadians(90.0f);
};

class WeightPainter {
public:
    static void paintWeight(const QVector3D& center, float radius, int targetBone,
                           float strength, const QVector<QVector3D>& vertices,
                           QVector<WeightVertex>& weights);

    static void addWeight(const QVector3D& point, const QVector<QVector3D>& vertices,
                         const QVector<QVector<int>>& faces, const QVector<WeightVertex>& weights,
                         int targetBone, float strength, float radius);

    static void subtractWeight(const QVector3D& point, const QVector<QVector3D>& vertices,
                             const QVector<QVector<int>>& faces, QVector<WeightVertex>& weights,
                             int targetBone, float strength, float radius);

    static void normalizeWeights(const QVector<QVector3D>& vertices,
                                 QVector<WeightVertex>& weights);

    static void blendWeights(QVector<WeightVertex>& weights, float mixRatio);

    static void mirrorWeights(QVector<WeightVertex>& weights, const QVector<QVector3D>& vertices,
                              bool flipX, bool flipY, bool flipZ);

    static void cleanZeroWeights(QVector<WeightVertex>& weights, float threshold = 0.001f);

    static void transferWeights(const QVector<WeightVertex>& source,
                               const QVector<WeightVertex>& target,
                               const QMap<int, int>& vertexMap);

    static void limitWeights(QVector<WeightVertex>& weights, int maxCount = 4);

    static void clampWeights(QVector<WeightVertex>& weights, float minWeight = 0.0f, float maxWeight = 1.0f);

    static QImage renderWeightMap(const QVector<WeightVertex>& weights, int boneIndex,
                                  const QVector<QVector2D>& uvs, int width, int height);

    static QVector3D blendPosition(const QVector3D& v1, const QVector3D& v2, float t);

    static float falloffFunction(float distance, float radius, BrushSettings::FalloffType type);
};

class WeightOptimization {
public:
    static void pruneWeights(QVector<WeightVertex>& weights, const QVector<QVector3D>& vertices,
                           float threshold = 0.01f);

    static void normalizeAllWeights(QVector<WeightVertex>& weights);

    static void smoothWeights(const QVector<QVector3D>& vertices,
                             const QVector<QVector<int>>& faces,
                             QVector<WeightVertex>& weights,
                             float factor, int iterations);

    static void inflateWeights(const QVector<QVector3D>& vertices,
                          QVector<WeightVertex>& weights,
                          int boneIndex,
                          float minWeight, float maxWeight);

    static QMap<int, float> getBoneInfluences(const WeightVertex& wv);
};

class BoneTransformCalculator {
public:
    static QVector3D computeWeightedPosition(const QVector3D& position,
                                             const QMap<int, float>& weights,
                                             const QVector<QMatrix4x4>& boneMatrices);

    static QVector3D computeWeightedNormal(const QVector3D& normal,
                                         const QMap<int, float>& weights,
                                         const QVector<QMatrix4x4>& boneMatrices);

    static QMatrix4x4 computeSkinningMatrix(const QMap<int, float>& weights,
                                          const QVector<QMatrix4x4>& bindMatrices,
                                          const QVector<QMatrix4x4>& boneMatrices);
};

class AutoWeightCalculator {
public:
    enum class Method {
        NearestVertex,
        HeatDiffusion,
        BFS,
        DLA
    };

    static QVector<WeightVertex> calculateAutoWeights(const QVector<QVector3D>& meshVerts,
                                                        const QVector<QVector<int>>& meshFaces,
                                                        const QVector<QVector3D>& bonePositions,
                                                        Method method = Method::HeatDiffusion,
                                                        int iterations = 10);

    static QVector<WeightVertex> heatMethod(const QVector<QVector3D>& vertices,
                                          const QVector<QVector<int>>& faces,
                                          const QVector<QVector3D>& bonePositions,
                                          int iterations);

    static QVector<WeightVertex> nearestNeighborMethod(const QVector<QVector3D>& vertices,
                                                      const QVector<QVector3D>& bonePositions);

    static QVector<WeightVertex> bfsMethod(const QVector<QVector3D>& vertices,
                                          const QVector<QVector<int>>& faces,
                                          const QVector<QVector3D>& bonePositions,
                                          int maxDistance);
};

}