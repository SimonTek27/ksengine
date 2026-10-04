#include "BoolOpQmlBridge.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include "3DModelingQmlBridge.h"

namespace ks::editor {

BoolOpQmlBridge::BoolOpQmlBridge(QObject* parent)
    : QObject(parent)
{
}

bool BoolOpQmlBridge::isAvailable() const {
    return geometry::BooleanOperations::canPerform();
}

geometry::GeoMeshData BoolOpQmlBridge::sceneMeshToGeometryMesh(SceneObject* obj) const
{
    geometry::GeoMeshData md;
    if (!obj || !obj->mesh()) return md;

    auto& verts = obj->mesh()->geometry().vertices;
    md.vertices.reserve(verts.size());
    md.normals.reserve(verts.size());
    for (const auto& sv : verts) {
        md.vertices.emplace_back(sv.position.x(), sv.position.y(), sv.position.z());
        md.normals.emplace_back(sv.normal.x(), sv.normal.y(), sv.normal.z());
    }

    auto& idxs = obj->mesh()->geometry().indices;
    md.faces.reserve(idxs.size() / 3);
    for (int i = 0; i + 2 < idxs.size(); i += 3)
        md.faces.emplace_back(idxs[i], idxs[i + 1], idxs[i + 2]);

    return md;
}

bool BoolOpQmlBridge::performOp(const QString& meshAId, const QString& meshBId,
                                 geometry::BooleanOperations::Operation op)
{
    emit operationStarted();
    emit progressUpdated(10);

    ks::SceneGraph* scene = m_scene ? m_scene : KSModelerQml::instance().sceneGraph();
    if (!scene) {
        m_lastResult.status = geometry::BoolOpResult::InvalidInput;
        m_lastResult.errorMessage = "No scene available";
        emit operationCompleted(false, "No scene available");
        return false;
    }

    bool okA = false, okB = false;
    int idA = meshAId.toInt(&okA);
    int idB = meshBId.toInt(&okB);
    if (!okA || !okB) {
        m_lastResult.status = geometry::BoolOpResult::InvalidInput;
        m_lastResult.errorMessage = "Invalid mesh ID format";
        emit operationCompleted(false, "Invalid mesh ID format");
        return false;
    }

    SceneObject* objA = scene->findObjectById(idA);
    SceneObject* objB = scene->findObjectById(idB);
    if (!objA || !objB || !objA->mesh() || !objB->mesh()) {
        m_lastResult.status = geometry::BoolOpResult::InvalidInput;
        m_lastResult.errorMessage = "One or both meshes not found";
        emit operationCompleted(false, "One or both meshes not found");
        return false;
    }

    emit progressUpdated(30);

    geometry::GeoMeshData meshA = sceneMeshToGeometryMesh(objA);
    geometry::GeoMeshData meshB = sceneMeshToGeometryMesh(objB);

    emit progressUpdated(50);

    m_lastResult = geometry::BooleanOperations::performOperation(meshA, meshB, op);

    emit progressUpdated(90);

    if (m_lastResult.isSuccess()) {
        SceneMesh* resultMesh = new SceneMesh();
        for (const auto& v : m_lastResult.result.vertices) {
            SceneVertex sv;
            sv.position = QVector3D((float)v.x, (float)v.y, (float)v.z);
            sv.color = QVector4D(1, 1, 1, 1);
            resultMesh->geometry().vertices.append(sv);
        }
        for (const auto& f : m_lastResult.result.faces) {
            resultMesh->geometry().indices.append(f.v0);
            resultMesh->geometry().indices.append(f.v1);
            resultMesh->geometry().indices.append(f.v2);
        }
        QString name = objA->name() + "_result";
        SceneObject* resultObj = scene->createObject(name, SceneObject::Type::Mesh);
        resultObj->setMesh(resultMesh);
        resultObj->setPosition(objA->position());

        scene->deleteObject(objB);
    }

    emit progressUpdated(100);

    bool ok = m_lastResult.isSuccess();
    emit operationCompleted(ok, QString::fromStdString(m_lastResult.errorMessage));
    return ok;
}

bool BoolOpQmlBridge::unionMeshes(const QString& meshAId, const QString& meshBId) {
    return performOp(meshAId, meshBId, geometry::BooleanOperations::Union);
}

bool BoolOpQmlBridge::differenceMeshes(const QString& meshAId, const QString& meshBId) {
    return performOp(meshAId, meshBId, geometry::BooleanOperations::Difference);
}

bool BoolOpQmlBridge::intersectMeshes(const QString& meshAId, const QString& meshBId) {
    return performOp(meshAId, meshBId, geometry::BooleanOperations::Intersection);
}

QString BoolOpQmlBridge::getLastResultInfo() const {
    return QString::fromStdString(m_lastResult.errorMessage);
}

int BoolOpQmlBridge::getResultVertexCount() const {
    return static_cast<int>(m_lastResult.result.vertices.size());
}

} // namespace ks::editor
