#pragma once

#include <QString>
#include <QVector>
#include <QMatrix4x4>
#include <QColor>

namespace ks {
namespace fileformat {

// Data transfer structs for a dependency-free USD ASCII (.usda) exporter.
// Meshes are already triangulated (indices describe a triangle list).

struct USDAExVertex {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float nx = 0.0f, ny = 0.0f, nz = 0.0f;
    float u = 0.0f, v = 0.0f;
};

struct USDAExMesh {
    QString name;
    QMatrix4x4 transform;
    QVector<USDAExVertex> vertices;
    QVector<uint32_t> indices;       // triangle list
    QString materialName;            // references a material prim
};

struct USDAExMaterial {
    QString name;
    QColor baseColor = QColor(200, 200, 200);
    float metallic = 0.0f;
    float roughness = 0.5f;
    QColor emissive = QColor(0, 0, 0);
    float opacity = 1.0f;
    QString baseColorTexture;
    QString normalTexture;
    QString roughnessTexture;
    QString metallicTexture;
};

// Writes a spec-compliant .usda stage containing an Xform per mesh, a triangle
// Mesh prim with per-vertex normals and st UVs, and UsdPreviewSurface materials
// (with optional UsdUVTexture maps) bound via material:binding rel.
bool exportUSDA(const QString& path,
                const QVector<USDAExMesh>& meshes,
                const QVector<USDAExMaterial>& materials,
                QString* error = nullptr);

} // namespace fileformat
} // namespace ks