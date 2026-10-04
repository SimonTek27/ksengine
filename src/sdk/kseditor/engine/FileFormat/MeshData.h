#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include <QVariant>
#include <QMatrix4x4>

namespace ks {
namespace fileformat {

struct MeshVertex {
    float px = 0, py = 0, pz = 0;
    float nx = 0, ny = 0, nz = 0;
    float u0 = 0, v0 = 0;
    float u1 = 0, v1 = 0;
    float tx = 0, ty = 0, tz = 0;
    float btx = 0, bty = 0, btz = 0;
    float boneWeights[4] = {};
    uint32_t boneIndex = 0;
};

struct MeshSubmesh {
    QString materialName;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
};

struct MeshMaterial {
    QString name;
    QString shaderName;
    QMap<QString, QString> textures;
    QMap<QString, QVariant> params;
};

struct MeshBone {
    QString name;
    int parentIndex = -1;
    float matrix[16] = {};
};

struct MeshData {
    QString name;
    QVector<MeshVertex> vertices;
    QVector<uint32_t> indices;
    QVector<MeshSubmesh> submeshes;
    QVector<MeshMaterial> materials;
    QVector<MeshBone> bones;

    bool isValid() const { return !vertices.isEmpty() || !indices.isEmpty(); }
    uint32_t vertexCount() const { return vertices.size(); }
    uint32_t indexCount() const { return indices.size(); }
    uint32_t triangleCount() const { return indices.size() / 3; }
};

} // namespace fileformat
} // namespace ks