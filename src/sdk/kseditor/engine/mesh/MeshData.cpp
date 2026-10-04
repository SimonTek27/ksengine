#include "MeshOperations.h"
#include <QVector3D>
#include <QVector2D>
#include <algorithm>
#include <limits>

namespace ks {

void MeshData::clear() {
    vertices.clear();
    faces.clear();
    edges.clear();
    normals.clear();
    uvs.clear();
    uv2s.clear();
    tangents.clear();
    bitangents.clear();
    materials.clear();
    vertexGroups.clear();
    shapeKeyNames.clear();
    shapeKeyDeltas.clear();
    shapeKeyWeights.clear();
    shapeKeyMute.clear();
    shapeKeyMin.clear();
    shapeKeyMax.clear();
    boundingBoxMin = boundingBoxMax = QVector3D();
    boundingRadius = 0.0f;
}

void MeshData::computeBoundingBox() {
    if (vertices.isEmpty()) {
        boundingBoxMin = boundingBoxMax = QVector3D();
        boundingRadius = 0.0f;
        return;
    }
    
    boundingBoxMin = boundingBoxMax = vertices[0].position;
    
    for (const auto& vertex : vertices) {
        boundingBoxMin.setX(qMin(boundingBoxMin.x(), vertex.position.x()));
        boundingBoxMin.setY(qMin(boundingBoxMin.y(), vertex.position.y()));
        boundingBoxMin.setZ(qMin(boundingBoxMin.z(), vertex.position.z()));
        boundingBoxMax.setX(qMax(boundingBoxMax.x(), vertex.position.x()));
        boundingBoxMax.setY(qMax(boundingBoxMax.y(), vertex.position.y()));
        boundingBoxMax.setZ(qMax(boundingBoxMax.z(), vertex.position.z()));
    }
    
    QVector3D size = boundingBoxMax - boundingBoxMin;
    boundingRadius = size.length() * 0.5f;
}

void MeshData::computeNormals() {
    if (vertices.isEmpty() || faces.isEmpty()) return;
    
    normals.resize(vertices.size());
    normals.fill(QVector3D(0, 0, 0));
    
    for (const auto& face : faces) {
        if (face.indices.size() < 3) continue;
        
        QVector3D v0 = vertices[face.indices[0]].position;
        QVector3D v1 = vertices[face.indices[1]].position;
        QVector3D v2 = vertices[face.indices[2]].position;
        
        QVector3D normal = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();
        
        for (int idx : face.indices) {
            if (idx >= 0 && idx < normals.size()) {
                normals[idx] += normal;
            }
        }
    }
    
    for (auto& normal : normals) {
        if (normal.lengthSquared() > 0) {
            normal.normalize();
        }
    }
    
    // Update vertex normals
    for (size_t i = 0; i < vertices.size() && i < normals.size(); ++i) {
        vertices[i].normal = normals[i];
    }
}

void MeshData::computeTangents() {
    if (vertices.isEmpty() || faces.isEmpty() || uvs.isEmpty()) return;
    
    tangents.resize(vertices.size());
    bitangents.resize(vertices.size());
    tangents.fill(QVector3D());
    bitangents.fill(QVector3D());
    
    QVector<QVector3D> tan1(vertices.size()), tan2(vertices.size());
    
    for (const auto& face : faces) {
        if (face.indices.size() < 3) continue;
        
        int i0 = face.indices[0];
        int i1 = face.indices[1];
        int i2 = face.indices[2];
        
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;
        if (i0 >= uvs.size() || i1 >= uvs.size() || i2 >= uvs.size()) continue;
        
        QVector3D v0 = vertices[i0].position;
        QVector3D v1 = vertices[i1].position;
        QVector3D v2 = vertices[i2].position;
        
        QVector2D uv0 = uvs[i0];
        QVector2D uv1 = uvs[i1];
        QVector2D uv2 = uvs[i2];
        
        QVector3D edge1 = v1 - v0;
        QVector3D edge2 = v2 - v0;
        QVector2D deltaUV1 = uv1 - uv0;
        QVector2D deltaUV2 = uv2 - uv0;
        
        float f = 1.0f / (deltaUV1.x() * deltaUV2.y() - deltaUV2.x() * deltaUV1.y());
        
        QVector3D tangent(
            f * (deltaUV2.y() * edge1.x() - deltaUV1.y() * edge2.x()),
            f * (deltaUV2.y() * edge1.y() - deltaUV1.y() * edge2.y()),
            f * (deltaUV2.y() * edge1.z() - deltaUV1.y() * edge2.z())
        );
        
        QVector3D bitangent(
            f * (-deltaUV2.x() * edge1.x() + deltaUV1.x() * edge2.x()),
            f * (-deltaUV2.x() * edge1.y() + deltaUV1.x() * edge2.y()),
            f * (-deltaUV2.x() * edge1.z() + deltaUV1.x() * edge2.z())
        );
        
        tan1[i0] += tangent;
        tan1[i1] += tangent;
        tan1[i2] += tangent;
        
        tan2[i0] += bitangent;
        tan2[i1] += bitangent;
        tan2[i2] += bitangent;
    }
    
    for (size_t i = 0; i < vertices.size(); ++i) {
        QVector3D n = vertices[i].normal;
        QVector3D t = tan1[i];
        
        // Gram-Schmidt orthogonalize
        tangents[i] = (t - n * QVector3D::dotProduct(n, t)).normalized();
        
        // Calculate handedness
        float handedness = (QVector3D::dotProduct(QVector3D::crossProduct(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
        bitangents[i] = QVector3D::crossProduct(n, tangents[i]) * handedness;
    }
}

void MeshData::flipFaces() {
    for (auto& face : faces) {
        std::reverse(face.indices.begin(), face.indices.end());
    }
    computeNormals();
}

void MeshData::triangulate() {
    QVector<Face> newFaces;
    for (const auto& face : faces) {
        if (face.indices.size() <= 3) {
            newFaces.append(face);
            continue;
        }
        
        // Fan triangulation
        for (size_t i = 1; i < face.indices.size() - 1; ++i) {
            Face tri;
            tri.indices = {face.indices[0], face.indices[i], face.indices[i + 1]};
            tri.materialId = face.materialId;
            newFaces.append(tri);
        }
    }
    faces = newFaces;
}

int MeshData::getTriangleCount() const {
    return faces.size();
}

int MeshData::getVertexCount() const {
    return vertices.size();
}

geometry::GeoMeshData MeshData::toGeoMesh() const {
    geometry::GeoMeshData geo;
    geo.vertices.resize(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        geo.vertices[i] = geometry::GeoVertex(vertices[i].position.x(), vertices[i].position.y(), vertices[i].position.z());
    }
    
    for (const auto& face : faces) {
        if (face.indices.size() >= 3) {
            geo.faces.emplace_back(face.indices[0], face.indices[1], face.indices[2]);
        }
    }
    return geo;
}

MeshData MeshData::fromGeoMesh(const geometry::GeoMeshData& geo) {
    MeshData mesh;
    mesh.vertices.resize(geo.vertices.size());
    for (size_t i = 0; i < geo.vertices.size(); ++i) {
        mesh.vertices[i].position = QVector3D((float)geo.vertices[i].x, (float)geo.vertices[i].y, (float)geo.vertices[i].z);
    }
    
    for (const auto& face : geo.faces) {
        mesh.faces.append(Face({(int)face.v0, (int)face.v1, (int)face.v2}));
    }
    return mesh;
}

} // namespace ks