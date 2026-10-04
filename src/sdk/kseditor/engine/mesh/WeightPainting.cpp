#include "WeightPainting.h"
#include <algorithm>
#include <queue>
#include <QMatrix4x4>

namespace ks {

float WeightPainter::falloffFunction(float distance, float radius, BrushSettings::FalloffType type) {
    float t = distance / radius;
    if (t >= 1.0f) return 0.0f;

    switch (type) {
        case BrushSettings::FalloffType::Constant:
            return 1.0f;
        case BrushSettings::FalloffType::Linear:
            return 1.0f - t;
        case BrushSettings::FalloffType::Smooth:
            t = 1.0f - t;
            return t * t * (3.0f - 2.0f * t);
        case BrushSettings::FalloffType::Sharp:
            return pow(1.0f - t, 2.0f);
        case BrushSettings::FalloffType::Root:
            return sqrt(1.0f - t);
        default:
            return 1.0f - t;
    }
}

void WeightPainter::paintWeight(const QVector3D& center, float radius, int targetBone,
                                float strength, const QVector<QVector3D>& vertices,
                                QVector<WeightVertex>& weights) {
    if (vertices.isEmpty() || weights.isEmpty() || targetBone < 0) return;
    
    // Ensure weights array matches vertices size
    if (weights.size() != vertices.size()) {
        weights.resize(vertices.size());
        for (int i = 0; i < vertices.size(); ++i) {
            weights[i].vertexIndex = i;
            // Initialize all bone weights to 0
            for (int b = 0; b < weights[i].weights.size(); ++b) {
                weights[i].weights[b] = 0.0f;
            }
        }
    }
    
    // Paint weights within radius
    for (int i = 0; i < vertices.size(); ++i) {
        float distance = (vertices[i] - center).length();
        if (distance <= radius) {
            float influence = 1.0f - (distance / radius); // Linear falloff
            influence = qPow(influence, 2.0f); // Square for smoother falloff
            influence *= strength;
            
            // Add to target bone weight, ensuring we don't exceed 1.0
            float& currentWeight = weights[i].weights[targetBone];
            currentWeight = qMin(1.0f, currentWeight + influence);
            
            // Normalize weights to sum to 1.0 (optional, depends on weighting system)
            float totalWeight = 0.0f;
            for (float w : weights[i].weights) {
                totalWeight += w;
            }
            if (totalWeight > 0.0f) {
                for (int b = 0; b < weights[i].weights.size(); ++b) {
                    weights[i].weights[b] /= totalWeight;
                }
            }
        }
    }
}

void WeightPainter::addWeight(const QVector3D& point, const QVector<QVector3D>& vertices,
                               const QVector<QVector<int>>& faces, const QVector<WeightVertex>& weights,
                               int targetBone, float strength, float radius) {
    // Delegate to paintWeight with additive behavior
    paintWeight(point, radius, targetBone, strength, vertices, 
                const_cast<QVector<WeightVertex>&>(weights));
}

void WeightPainter::subtractWeight(const QVector3D& point, const QVector<QVector3D>& vertices,
                                   const QVector<QVector<int>>& faces, QVector<WeightVertex>& weights,
                                   int targetBone, float strength, float radius) {
    // Subtract weight (negative strength)
    paintWeight(point, radius, targetBone, -strength, vertices, weights);
}

void WeightPainter::normalizeWeights(const QVector<QVector3D>& vertices,
                                     QVector<WeightVertex>& weights) {
    // Ensure weights array matches vertices size
    if (weights.size() != vertices.size()) {
        weights.resize(vertices.size());
        for (int i = 0; i < vertices.size(); ++i) {
            weights[i].vertexIndex = i;
            // Initialize all bone weights to 0
            for (int b = 0; b < weights[i].weights.size(); ++b) {
                weights[i].weights[b] = 0.0f;
            }
        }
    }
    
    for (auto& wv : weights) {
        float total = 0;
        for (auto it = wv.weights.constBegin(); it != wv.weights.constEnd(); ++it) {
            total += it.value();
        }
        if (total > 0.0001f) {
            QMap<int, float> normalized;
            for (auto it = wv.weights.constBegin(); it != wv.weights.constEnd(); ++it) {
                normalized[it.key()] = it.value() / total;
            }
            wv.weights = normalized;
        }
    }
}

void WeightPainter::blendWeights(QVector<WeightVertex>& weights, float mixRatio) {
    // Blend weights towards zero based on mixRatio (0 = no change, 1 = full blend to zero)
    float blendFactor = qBound(0.0f, mixRatio, 1.0f);
    
    for (auto& wv : weights) {
        for (auto it = wv.weights.begin(); it != wv.weights.end(); ++it) {
            it.value() *= (1.0f - blendFactor);
        }
    }
}

void WeightPainter::mirrorWeights(QVector<WeightVertex>& weights, const QVector<QVector3D>& vertices,
                                  bool flipX, bool flipY, bool flipZ) {
    if (vertices.isEmpty() || weights.isEmpty() || weights.size() != vertices.size()) return;
    
    for (int i = 0; i < vertices.size(); ++i) {
        QVector3D mirrored = vertices[i];
        if (flipX) mirrored.setX(-mirrored.x());
        if (flipY) mirrored.setY(-mirrored.y());
        if (flipZ) mirrored.setZ(-mirrored.z());
        
        // Find mirror vertex
        for (int j = 0; j < vertices.size(); ++j) {
            if ((mirrored - vertices[j]).length() < 0.001f) { // Close enough
                // Swap weights
                if (i != j && i < weights.size() && j < weights.size()) {
                    weights[i].weights.swap(weights[j].weights);
                }
                break;
            }
        }
    }
}

void WeightPainter::cleanZeroWeights(QVector<WeightVertex>& weights, float threshold) {
    for (auto& wv : weights) {
        QList<int> toRemove;
        for (auto it = wv.weights.constBegin(); it != wv.weights.constEnd(); ++it) {
            if (it.value() < threshold) {
                toRemove.append(it.key());
            }
        }
        for (int key : toRemove) {
            wv.weights.remove(key);
        }
    }
}

void WeightPainter::transferWeights(const QVector<WeightVertex>& source,
                                    const QVector<WeightVertex>& target,
                                    const QMap<int, int>& vertexMap) {
    if (source.isEmpty() || target.isEmpty() || vertexMap.isEmpty()) return;
    
    // Transfer weights from source to target based on vertex mapping
    for (auto it = vertexMap.constBegin(); it != vertexMap.constEnd(); ++it) {
        int sourceIndex = it.key();
        int targetIndex = it.value();
        
        if (sourceIndex < source.size() && targetIndex < target.size()) {
            // Copy weights from source vertex to target vertex
            const QVector<WeightVertex>& srcWeights = source;
            QVector<WeightVertex>& tgtWeights = const_cast<QVector<WeightVertex>&>(target);
            
            if (srcWeights[sourceIndex].weights.size() == tgtWeights[targetIndex].weights.size()) {
                tgtWeights[targetIndex].weights = srcWeights[sourceIndex].weights;
            }
        }
    }
}

void WeightPainter::limitWeights(QVector<WeightVertex>& weights, int maxCount) {
    for (auto& wv : weights) {
        if (wv.weights.size() <= maxCount) continue;

        QList<QPair<int, float>> sorted;
        for (auto it = wv.weights.constBegin(); it != wv.weights.constEnd(); ++it) {
            sorted.append(std::make_pair(it.key(), it.value()));
        }
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        wv.weights.clear();
        // Keep only the top maxCount weights
        for (int i = 0; i < qMin(maxCount, sorted.size()); ++i) {
            wv.weights[sorted[i].first] = sorted[i].second;
        }
    }
}

void WeightPainter::clampWeights(QVector<WeightVertex>& weights, float minWeight, float maxWeight) {
    for (auto& wv : weights) {
        QMap<int, float> clamped;
        for (auto it = wv.weights.constBegin(); it != wv.weights.constEnd(); ++it) {
            float w = qBound(minWeight, it.value(), maxWeight);
            if (w > minWeight) {
                clamped[it.key()] = w;
            }
        }
        wv.weights = clamped;
    }
}

QImage WeightPainter::renderWeightMap(const QVector<WeightVertex>& weights, int boneIndex,
                                     const QVector<QVector2D>& uvs, int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::black);

    QVector<QVector<int>> uvToVerts(width, QVector<int>());

    for (int vi = 0; vi < weights.size() && vi < uvs.size(); ++vi) {
        const auto& wv = weights[vi];
        float weight = wv.weights.value(boneIndex, 0.0f);
        if (weight > 0.001f) {
            int u = int(uvs[vi].x() * width) % width;
            int v = int(uvs[vi].y() * height) % height;
            QRgb color = qRgb(int(weight * 255), int(weight * 255), int(weight * 255));
            image.setPixel(u, v, color);
        }
    }

    return image;
}

QVector3D WeightPainter::blendPosition(const QVector3D& v1, const QVector3D& v2, float t) {
    return v1 + (v2 - v1) * t;
}

void WeightOptimization::pruneWeights(QVector<WeightVertex>& weights, const QVector<QVector3D>& vertices,
                                     float threshold) {
    WeightPainter::cleanZeroWeights(weights, threshold);
}

void WeightOptimization::normalizeAllWeights(QVector<WeightVertex>& weights) {
    WeightPainter::normalizeWeights(QVector<QVector3D>(), weights);
}

void WeightOptimization::smoothWeights(const QVector<QVector3D>& vertices,
                                       const QVector<QVector<int>>& faces,
                                       QVector<WeightVertex>& weights,
                                       float factor, int iterations) {
    for (int iter = 0; iter < iterations; ++iter) {
        QVector<QMap<int, float>> newWeights;
        newWeights.resize(weights.size());

        for (int vi = 0; vi < vertices.size(); ++vi) {
            QMap<int, float> sumWeights;
            float sumFactor = 0;

            for (const auto& face : faces) {
                for (int i = 0; i < face.size(); ++i) {
                    if (face[i] == vi) {
                        int prev = face[(i - 1 + face.size()) % face.size()];
                        int next = face[(i + 1) % face.size()];

                        for (auto it = weights[prev].weights.constBegin(); it != weights[prev].weights.constEnd(); ++it) {
                            sumWeights[it.key()] += it.value();
                            sumFactor += it.value();
                        }
                        for (auto it = weights[next].weights.constBegin(); it != weights[next].weights.constEnd(); ++it) {
                            sumWeights[it.key()] += it.value();
                            sumFactor += it.value();
                        }
                        for (auto it = weights[vi].weights.constBegin(); it != weights[vi].weights.constEnd(); ++it) {
                            sumWeights[it.key()] += it.value();
                            sumFactor += it.value();
                        }
                    }
                }
            }

            if (sumFactor > 0.0001f) {
                for (auto it = sumWeights.constBegin(); it != sumWeights.constEnd(); ++it) {
                    newWeights[vi][it.key()] = it.value() / sumFactor;
                }
            }
        }

        for (int vi = 0; vi < weights.size(); ++vi) {
            for (auto it = newWeights[vi].constBegin(); it != newWeights[vi].constEnd(); ++it) {
                float oldWeight = weights[vi].weights.value(it.key(), 0.0f);
                weights[vi].weights[it.key()] = oldWeight * (1 - factor) + it.value() * factor;
            }
        }
    }

    WeightPainter::normalizeWeights(vertices, weights);
}

void WeightOptimization::inflateWeights(const QVector<QVector3D>& vertices,
                                      QVector<WeightVertex>& weights,
                                      int boneIndex,
                                       float minWeight, float maxWeight) {
    for (auto& wv : weights) {
        if (wv.weights.value(boneIndex, 0.0f) >= minWeight && wv.weights.value(boneIndex, 0.0f) < maxWeight) {
            wv.weights[boneIndex] = maxWeight;
        }
    }
}

QMap<int, float> WeightOptimization::getBoneInfluences(const WeightVertex& wv) {
    return wv.weights;
}

QVector3D BoneTransformCalculator::computeWeightedPosition(const QVector3D& position,
                                                          const QMap<int, float>& weights,
                                                          const QVector<QMatrix4x4>& boneMatrices) {
    QVector3D result(0, 0, 0);

    for (auto it = weights.constBegin(); it != weights.constEnd(); ++it) {
        int boneIdx = it.key();
        float weight = it.value();

        if (boneIdx >= 0 && boneIdx < boneMatrices.size()) {
            QVector3D transformed = boneMatrices[boneIdx].map(position);
            result += transformed * weight;
        }
    }

    return result;
}

QVector3D BoneTransformCalculator::computeWeightedNormal(const QVector3D& normal,
                                                         const QMap<int, float>& weights,
                                                         const QVector<QMatrix4x4>& boneMatrices) {
    QVector3D result(0, 0, 0);

    for (auto it = weights.constBegin(); it != weights.constEnd(); ++it) {
        int boneIdx = it.key();
        float weight = it.value();

        if (boneIdx >= 0 && boneIdx < boneMatrices.size()) {
            QVector3D transformed = boneMatrices[boneIdx].mapVector(normal);
            result += transformed * weight;
        }
    }

    if (result.length() > 0.0001f) result.normalize();
    return result;
}

QMatrix4x4 BoneTransformCalculator::computeSkinningMatrix(const QMap<int, float>& weights,
                                                          const QVector<QMatrix4x4>& bindMatrices,
                                                          const QVector<QMatrix4x4>& boneMatrices) {
    QMatrix4x4 result;
    result.setToIdentity();

    for (auto it = weights.constBegin(); it != weights.constEnd(); ++it) {
        int boneIdx = it.key();
        float weight = it.value();

        if (boneIdx >= 0 && boneIdx < boneMatrices.size()) {
            QMatrix4x4 boneMat = boneMatrices[boneIdx];
            if (boneIdx < bindMatrices.size()) {
                QMatrix4x4 invBind = bindMatrices[boneIdx].inverted();
                result += boneMat * invBind * weight;
            }
        }
    }

    return result;
}

QVector<WeightVertex> AutoWeightCalculator::calculateAutoWeights(const QVector<QVector3D>& meshVerts,
                                                                   const QVector<QVector<int>>& meshFaces,
                                                                   const QVector<QVector3D>& bonePositions,
                                                                   Method method,
                                                                   int iterations) {
    switch (method) {
        case Method::HeatDiffusion:
            return heatMethod(meshVerts, meshFaces, bonePositions, iterations);
        case Method::NearestVertex:
            return nearestNeighborMethod(meshVerts, bonePositions);
        case Method::BFS:
            return bfsMethod(meshVerts, meshFaces, bonePositions, iterations);
        default:
            return nearestNeighborMethod(meshVerts, bonePositions);
    }
}

QVector<WeightVertex> AutoWeightCalculator::nearestNeighborMethod(const QVector<QVector3D>& vertices,
                                                                     const QVector<QVector3D>& bonePositions) {
    QVector<WeightVertex> weights;
    weights.resize(vertices.size());

    for (int vi = 0; vi < vertices.size(); ++vi) {
        weights[vi].vertexIndex = vi;

        float minDist = std::numeric_limits<float>::max();
        int nearestBone = 0;

        for (int bi = 0; bi < bonePositions.size(); ++bi) {
            float dist = (vertices[vi] - bonePositions[bi]).length();
            if (dist < minDist) {
                minDist = dist;
                nearestBone = bi;
            }
        }

        weights[vi].weights[nearestBone] = 1.0f;
    }

    return weights;
}

QVector<WeightVertex> AutoWeightCalculator::heatMethod(const QVector<QVector3D>& vertices,
                                                        const QVector<QVector<int>>& faces,
                                                        const QVector<QVector3D>& bonePositions,
                                                        int iterations) {
    QVector<WeightVertex> weights;
    weights.resize(vertices.size());

    for (int bi = 0; bi < bonePositions.size(); ++bi) {
        int nearestVert = 0;
        float minDist = std::numeric_limits<float>::max();

        for (int vi = 0; vi < vertices.size(); ++vi) {
            float dist = (vertices[vi] - bonePositions[bi]).length();
            if (dist < minDist) {
                minDist = dist;
                nearestVert = vi;
            }
        }

        if (nearestVert < weights.size()) {
            weights[nearestVert].weights[bi] = 1.0f;
        }
    }

    return weights;
}

QVector<WeightVertex> AutoWeightCalculator::bfsMethod(const QVector<QVector3D>& vertices,
                                                      const QVector<QVector<int>>& faces,
                                                      const QVector<QVector3D>& bonePositions,
                                                      int maxDistance) {
    QVector<WeightVertex> weights;
    weights.resize(vertices.size());

    for (int vi = 0; vi < weights.size(); ++vi) {
        weights[vi].vertexIndex = vi;
    }

    for (int bi = 0; bi < bonePositions.size(); ++bi) {
        int startVert = 0;
        float minDist = std::numeric_limits<float>::max();

        for (int vi = 0; vi < vertices.size(); ++vi) {
            float dist = (vertices[vi] - bonePositions[bi]).length();
            if (dist < minDist) {
                minDist = dist;
                startVert = vi;
            }
        }

        if (startVert < weights.size()) {
            weights[startVert].weights[bi] = 1.0f;
        }
    }

    return weights;
}

}