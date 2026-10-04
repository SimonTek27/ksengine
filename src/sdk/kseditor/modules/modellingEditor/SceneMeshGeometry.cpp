#include "SceneMeshGeometry.h"
#include "3DModelingQmlBridge.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include <cstring>

namespace ks {

ks::QmlSceneMeshGeometry::QmlSceneMeshGeometry(QQuick3DObject* parent)
    : QQuick3DGeometry(parent)
{
}

void ks::QmlSceneMeshGeometry::setObjectId(int id) {
    if (m_objectId != id) {
        m_objectId = id;
        emit objectIdChanged();
        rebuild();
    }
}

void ks::QmlSceneMeshGeometry::rebuild() {
    clear();
    if (m_objectId < 0) return;

    ks::SceneGraph* scene = KSModelerQml::instance().sceneGraph();
    if (!scene) return;

    SceneObject* obj = scene->findObjectById(m_objectId);
    if (!obj || !obj->mesh()) return;

    const SceneMesh* mesh = obj->mesh();
    const auto& verts = mesh->geometry().vertices;
    const auto& idxs = mesh->geometry().indices;

    if (verts.isEmpty()) return;

    // Stride: position(3) + normal(3) + uv(2) + color(4) = 12 floats = 48 bytes
    const int vertexStride = 48;
    QByteArray vertexData;
    vertexData.resize(verts.size() * vertexStride);
    float* vptr = reinterpret_cast<float*>(vertexData.data());

    for (const SceneVertex& sv : verts) {
        // Position
        vptr[0] = sv.position.x();
        vptr[1] = sv.position.y();
        vptr[2] = sv.position.z();
        // Normal
        vptr[3] = sv.normal.x();
        vptr[4] = sv.normal.y();
        vptr[5] = sv.normal.z();
        // UV
        vptr[6] = sv.uv.x();
        vptr[7] = sv.uv.y();
        // Color
        vptr[8] = sv.color.x();
        vptr[9] = sv.color.y();
        vptr[10] = sv.color.z();
        vptr[11] = sv.color.w();
        vptr += 12;
    }

    setVertexData(vertexData);
    setStride(vertexStride);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoordSemantic, 24, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::ColorSemantic, 32, QQuick3DGeometry::Attribute::F32Type);

    if (!idxs.isEmpty()) {
        QByteArray indexData;
        indexData.resize(idxs.size() * sizeof(uint32_t));
        std::memcpy(indexData.data(), idxs.constData(), idxs.size() * sizeof(uint32_t));
        setIndexData(indexData);
    }

    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
}

} // namespace ks
