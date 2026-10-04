#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>
#include <QMap>
#include <QVariant>
#include "MeshOperations.h"

namespace ks {

struct ShapeKey {
    QString name;
    QVector<QVector3D> deltas;
    float weight = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    bool mute = false;
    bool relative = true;
    int frameIndex = 0;
};

class ShapeKeyManager {
public:
    static constexpr int BASIS_INDEX = 0;

    static int addShapeKey(MeshData& mesh, const QString& name);
    static bool removeShapeKey(MeshData& mesh, int index);
    static bool renameShapeKey(MeshData& mesh, int index, const QString& newName);
    static void reorderShapeKey(MeshData& mesh, int fromIndex, int toIndex);

    static int captureShapeKey(MeshData& mesh, const QString& name);
    static void setShapeKeyWeight(MeshData& mesh, int index, float weight);
    static float getShapeKeyWeight(const MeshData& mesh, int index);

    static QStringList getShapeKeyNames(const MeshData& mesh);
    static int getShapeKeyCount(const MeshData& mesh);
    static int getShapeKeyIndexByName(const MeshData& mesh, const QString& name);

    static void applyAllShapeKeys(MeshData& mesh);
    static QVector<QVector3D> getBlendedPositions(const MeshData& mesh);
    static void resetToBasis(MeshData& mesh);

    static QVector3D getBasePosition(const MeshData& mesh, int vertexIndex);
    static QVector3D getDelta(const MeshData& mesh, int keyIndex, int vertexIndex);
    static void setDelta(MeshData& mesh, int keyIndex, int vertexIndex, const QVector3D& delta);

    static void muteShapeKey(MeshData& mesh, int index, bool mute);
    static void setRange(MeshData& mesh, int index, float min, float max);

    static QVariantList shapeKeysToVariant(const MeshData& mesh);
    static void shapeKeysFromVariant(MeshData& mesh, const QVariantList& data);

private:
    static void validateShapeKeyCount(MeshData& mesh);
    static void ensureDeltasSize(ShapeKey& key, int vertexCount);
};

inline int ShapeKeyManager::addShapeKey(MeshData& mesh, const QString& name) {
    if (mesh.shapeKeyNames.isEmpty()) {
        mesh.shapeKeyNames.append("Basis");
        QVector<QVector3D> basisDeltas(mesh.vertices.size(), QVector3D(0, 0, 0));
        mesh.shapeKeyDeltas.append(basisDeltas);
        mesh.shapeKeyWeights.append(0.0f);
        mesh.shapeKeyMute.append(false);
    }
    ShapeKey key;
    key.name = name.isEmpty() ? QString("Key %1").arg(mesh.shapeKeyNames.size()) : name;
    key.deltas.resize(mesh.vertices.size());
    key.deltas.fill(QVector3D(0, 0, 0));
    key.weight = 0.0f;
    mesh.shapeKeyNames.append(key.name);
    mesh.shapeKeyDeltas.append(key.deltas);
    mesh.shapeKeyWeights.append(0.0f);
    mesh.shapeKeyMute.append(false);
    return mesh.shapeKeyNames.size() - 1;
}

inline bool ShapeKeyManager::removeShapeKey(MeshData& mesh, int index) {
    if (index <= BASIS_INDEX || index >= mesh.shapeKeyNames.size()) return false;
    mesh.shapeKeyNames.removeAt(index);
    mesh.shapeKeyDeltas.removeAt(index);
    mesh.shapeKeyWeights.removeAt(index);
    mesh.shapeKeyMute.removeAt(index);
    applyAllShapeKeys(mesh);
    return true;
}

inline bool ShapeKeyManager::renameShapeKey(MeshData& mesh, int index, const QString& newName) {
    if (index < 0 || index >= mesh.shapeKeyNames.size()) return false;
    if (index == BASIS_INDEX) return false;
    mesh.shapeKeyNames[index] = newName;
    return true;
}

inline void ShapeKeyManager::reorderShapeKey(MeshData& mesh, int fromIndex, int toIndex) {
    if (fromIndex <= BASIS_INDEX || toIndex <= BASIS_INDEX) return;
    if (fromIndex >= mesh.shapeKeyNames.size() || toIndex >= mesh.shapeKeyNames.size()) return;
    auto n = mesh.shapeKeyNames.takeAt(fromIndex);
    mesh.shapeKeyNames.insert(toIndex, n);
    auto d = mesh.shapeKeyDeltas.takeAt(fromIndex);
    mesh.shapeKeyDeltas.insert(toIndex, d);
    auto w = mesh.shapeKeyWeights.takeAt(fromIndex);
    mesh.shapeKeyWeights.insert(toIndex, w);
    auto m = mesh.shapeKeyMute.takeAt(fromIndex);
    mesh.shapeKeyMute.insert(toIndex, m);
}

inline int ShapeKeyManager::captureShapeKey(MeshData& mesh, const QString& name) {
    if (mesh.vertices.isEmpty()) return -1;
    int idx = addShapeKey(mesh, name);
    if (idx < 0) return -1;
    for (int i = 0; i < mesh.vertices.size(); ++i) {
        QVector3D basePos = getBasePosition(mesh, i);
        mesh.shapeKeyDeltas[idx][i] = mesh.vertices[i].position - basePos;
    }
    return idx;
}

inline void ShapeKeyManager::setShapeKeyWeight(MeshData& mesh, int index, float weight) {
    if (index < 0 || index >= mesh.shapeKeyWeights.size()) return;
    mesh.shapeKeyWeights[index] = qBound(mesh.shapeKeyMin.value(index, 0.0f), weight, mesh.shapeKeyMax.value(index, 1.0f));
    applyAllShapeKeys(mesh);
}

inline float ShapeKeyManager::getShapeKeyWeight(const MeshData& mesh, int index) {
    if (index < 0 || index >= mesh.shapeKeyWeights.size()) return 0.0f;
    return mesh.shapeKeyWeights[index];
}

inline QStringList ShapeKeyManager::getShapeKeyNames(const MeshData& mesh) {
    return mesh.shapeKeyNames;
}

inline int ShapeKeyManager::getShapeKeyCount(const MeshData& mesh) {
    return mesh.shapeKeyNames.size();
}

inline int ShapeKeyManager::getShapeKeyIndexByName(const MeshData& mesh, const QString& name) {
    return mesh.shapeKeyNames.indexOf(name);
}

inline void ShapeKeyManager::applyAllShapeKeys(MeshData& mesh) {
    if (mesh.shapeKeyNames.isEmpty()) return;
    if (mesh.shapeKeyDeltas.isEmpty()) return;
    QVector<QVector3D> blended = getBlendedPositions(mesh);
    for (int i = 0; i < qMin(blended.size(), mesh.vertices.size()); ++i) {
        mesh.vertices[i].position = blended[i];
    }
    mesh.computeNormals();
    mesh.computeBoundingBox();
}

inline QVector<QVector3D> ShapeKeyManager::getBlendedPositions(const MeshData& mesh) {
    if (mesh.vertices.isEmpty()) return {};
    QVector<QVector3D> result(mesh.vertices.size());
    for (int i = 0; i < mesh.vertices.size(); ++i) {
        result[i] = getBasePosition(mesh, i);
    }
    for (int k = 1; k < mesh.shapeKeyNames.size(); ++k) {
        if (mesh.shapeKeyMute.value(k, false)) continue;
        float w = mesh.shapeKeyWeights.value(k, 0.0f);
        if (qFuzzyIsNull(w)) continue;
        if (k < mesh.shapeKeyDeltas.size()) {
            const auto& deltas = mesh.shapeKeyDeltas[k];
            int n = qMin(deltas.size(), result.size());
            for (int i = 0; i < n; ++i) {
                result[i] += deltas[i] * w;
            }
        }
    }
    return result;
}

inline void ShapeKeyManager::resetToBasis(MeshData& mesh) {
    int n = mesh.vertices.size();
    for (int i = 0; i < n; ++i) {
        mesh.vertices[i].position = getBasePosition(mesh, i);
    }
    for (int k = 1; k < mesh.shapeKeyWeights.size(); ++k) {
        mesh.shapeKeyWeights[k] = 0.0f;
    }
    mesh.computeNormals();
    mesh.computeBoundingBox();
}

inline QVector3D ShapeKeyManager::getBasePosition(const MeshData& mesh, int vertexIndex) {
    if (vertexIndex < 0 || vertexIndex >= mesh.vertices.size()) return QVector3D();
    QVector3D pos = mesh.vertices[vertexIndex].position;
    for (int k = 1; k < mesh.shapeKeyNames.size(); ++k) {
        if (k < mesh.shapeKeyDeltas.size() && vertexIndex < mesh.shapeKeyDeltas[k].size()) {
            pos -= mesh.shapeKeyDeltas[k][vertexIndex] * mesh.shapeKeyWeights.value(k, 0.0f);
        }
    }
    return pos;
}

inline QVector3D ShapeKeyManager::getDelta(const MeshData& mesh, int keyIndex, int vertexIndex) {
    if (keyIndex < 0 || keyIndex >= mesh.shapeKeyDeltas.size()) return QVector3D();
    if (vertexIndex < 0 || vertexIndex >= mesh.shapeKeyDeltas[keyIndex].size()) return QVector3D();
    return mesh.shapeKeyDeltas[keyIndex][vertexIndex];
}

inline void ShapeKeyManager::setDelta(MeshData& mesh, int keyIndex, int vertexIndex, const QVector3D& delta) {
    if (keyIndex < 0 || keyIndex >= mesh.shapeKeyDeltas.size()) return;
    if (keyIndex == BASIS_INDEX) return;
    if (vertexIndex < 0 || vertexIndex >= mesh.shapeKeyDeltas[keyIndex].size()) return;
    mesh.shapeKeyDeltas[keyIndex][vertexIndex] = delta;
}

inline void ShapeKeyManager::muteShapeKey(MeshData& mesh, int index, bool mute) {
    if (index < 0 || index >= mesh.shapeKeyMute.size()) return;
    if (index == BASIS_INDEX) return;
    mesh.shapeKeyMute[index] = mute;
    applyAllShapeKeys(mesh);
}

inline void ShapeKeyManager::setRange(MeshData& mesh, int index, float min, float max) {
    if (index < 0 || index >= mesh.shapeKeyNames.size()) return;
    mesh.shapeKeyMin[index] = min;
    mesh.shapeKeyMax[index] = max;
}

inline QVariantList ShapeKeyManager::shapeKeysToVariant(const MeshData& mesh) {
    QVariantList list;
    for (int k = 0; k < mesh.shapeKeyNames.size(); ++k) {
        QVariantMap entry;
        entry["name"] = mesh.shapeKeyNames[k];
        entry["weight"] = mesh.shapeKeyWeights.value(k, 0.0f);
        entry["mute"] = mesh.shapeKeyMute.value(k, false);
        entry["min"] = mesh.shapeKeyMin.value(k, 0.0f);
        entry["max"] = mesh.shapeKeyMax.value(k, 1.0f);
        QVariantList deltas;
        if (k < mesh.shapeKeyDeltas.size()) {
            for (const auto& d : mesh.shapeKeyDeltas[k]) {
                QVariantList v;
                v << d.x() << d.y() << d.z();
                deltas.append(v);
            }
        }
        entry["deltas"] = deltas;
        list.append(entry);
    }
    return list;
}

inline void ShapeKeyManager::shapeKeysFromVariant(MeshData& mesh, const QVariantList& data) {
    mesh.shapeKeyNames.clear();
    mesh.shapeKeyDeltas.clear();
    mesh.shapeKeyWeights.clear();
    mesh.shapeKeyMute.clear();
    mesh.shapeKeyMin.clear();
    mesh.shapeKeyMax.clear();
    for (const auto& entryVar : data) {
        QVariantMap entry = entryVar.toMap();
        mesh.shapeKeyNames.append(entry["name"].toString());
        mesh.shapeKeyWeights.append(entry["weight"].toFloat());
        mesh.shapeKeyMute.append(entry["mute"].toBool());
        mesh.shapeKeyMin.append(entry["min"].toFloat());
        mesh.shapeKeyMax.append(entry["max"].toFloat());
        QVector<QVector3D> deltas;
        for (const auto& dv : entry["deltas"].toList()) {
            QVariantList v = dv.toList();
            deltas.append(QVector3D(v.value(0).toFloat(), v.value(1).toFloat(), v.value(2).toFloat()));
        }
        mesh.shapeKeyDeltas.append(deltas);
    }
}

} // namespace ks
