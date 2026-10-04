#include "SymmetryQmlBridge.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include "3DModelingQmlBridge.h"

namespace ks {

SymmetryQmlBridge::SymmetryQmlBridge(QObject* parent)
    : QObject(parent)
{
}

void SymmetryQmlBridge::setAxis(int a) {
    SymmetryManager::Axis newAxis = static_cast<SymmetryManager::Axis>(
        qBound(0, a, 2));
    if (m_axis != newAxis) {
        m_axis = newAxis;
        emit axisChanged();
    }
}

void SymmetryQmlBridge::setOffset(float o) {
    if (!qFuzzyCompare(m_offset, o)) {
        m_offset = o;
        emit offsetChanged();
    }
}

void SymmetryQmlBridge::setWeldThreshold(float t) {
    float clamped = qMax(0.0f, t);
    if (!qFuzzyCompare(m_weldThreshold, clamped)) {
        m_weldThreshold = clamped;
        emit weldThresholdChanged();
    }
}

void SymmetryQmlBridge::setClipMode(int m) {
    auto newMode = static_cast<SymmetryManager::ClipMode>(
        qBound(0, m, 2));
    if (m_clipMode != newMode) {
        m_clipMode = newMode;
        emit clipModeChanged();
    }
}

void SymmetryQmlBridge::setMergeMode(int m) {
    auto newMode = static_cast<SymmetryManager::MergeMode>(
        qBound(0, m, 2));
    if (m_mergeMode != newMode) {
        m_mergeMode = newMode;
        emit mergeModeChanged();
    }
}

void SymmetryQmlBridge::setPreviewVisible(bool v) {
    if (m_previewVisible != v) {
        m_previewVisible = v;
        emit previewChanged();
        if (!v) clearPreview();
    }
}

SceneObject* SymmetryQmlBridge::getSelectedMeshObject() {
    ks::SceneGraph* scene = m_scene ? m_scene : KSModelerQml::instance().sceneGraph();
    if (!scene) return nullptr;
    auto allObjs = scene->allObjects();
    for (SceneObject* obj : allObjs) {
        if (obj->isSelected() && obj->type() == SceneObject::Type::Mesh && obj->mesh()) {
            return obj;
        }
    }
    return nullptr;
}

MeshData SymmetryQmlBridge::sceneMeshToMeshData(SceneObject* obj) {
    MeshData md;
    if (!obj || !obj->mesh()) return md;
    auto& verts = obj->mesh()->geometry().vertices;
    for (const auto& sv : verts) {
        Vertex v;
        v.position = QVector3D(sv.position.x(), sv.position.y(), sv.position.z());
        v.color = QVector4D(sv.color.x(), sv.color.y(), sv.color.z(), sv.color.w());
        md.vertices.append(v);
    }
    auto& idxs = obj->mesh()->geometry().indices;
    for (int i = 0; i + 2 < idxs.size(); i += 3)
        md.faces.append(Face({ (int)idxs[i], (int)idxs[i+1], (int)idxs[i+2] }));
    md.computeNormals();
    md.computeBoundingBox();
    return md;
}

bool SymmetryQmlBridge::meshDataToScene(MeshData& md, const QString& name) {
    ks::SceneGraph* scene = m_scene ? m_scene : KSModelerQml::instance().sceneGraph();
    if (!scene || md.vertices.isEmpty()) return false;
    SceneObject* obj = scene->createObject(name, SceneObject::Type::Mesh);
    if (!obj) return false;
    SceneMesh* sm = new SceneMesh();
    for (const auto& v : md.vertices) {
        SceneVertex sv;
        sv.position = QVector3D(v.position.x(), v.position.y(), v.position.z());
        sv.color = QVector4D(v.color.x(), v.color.y(), v.color.z(), v.color.w());
        sm->geometry().vertices.append(sv);
    }
    for (const auto& f : md.faces) {
        for (int idx : f.indices)
            sm->geometry().indices.append((uint32_t)idx);
    }
    obj->setMesh(sm);
    return true;
}

void SymmetryQmlBridge::updateSelection() {
    bool hadSelection = m_hasSelection;
    SceneObject* obj = getSelectedMeshObject();
    m_hasSelection = (obj != nullptr);
    if (hadSelection != m_hasSelection)
        emit selectionChanged();
}

void SymmetryQmlBridge::applySymmetry() {
    SceneObject* obj = getSelectedMeshObject();
    if (!obj) {
        m_statusText = "No mesh selected";
        emit statusChanged();
        emit symmetryApplied(false, m_statusText);
        return;
    }

    MeshData input = sceneMeshToMeshData(obj);
    if (input.vertices.isEmpty()) {
        m_statusText = "Selected object has no geometry";
        emit statusChanged();
        emit symmetryApplied(false, m_statusText);
        return;
    }

    QString objName = obj->name();
    SymmetryResult result = SymmetryManager::mirrorMesh(
        input, m_axis, m_offset, m_clipMode, m_mergeMode, m_weldThreshold);

    if (!result.success) {
        m_statusText = "Symmetry failed: " + result.errorMessage;
        emit statusChanged();
        emit symmetryApplied(false, m_statusText);
        return;
    }

    ks::SceneGraph* scene = m_scene ? m_scene : KSModelerQml::instance().sceneGraph();

    if (m_mergeMode == SymmetryManager::MergeMode::NewObject) {
        QString newName = objName + "_sym";
        if (!meshDataToScene(result.result, newName)) {
            m_statusText = "Failed to create symmetry object";
            emit statusChanged();
            emit symmetryApplied(false, m_statusText);
            return;
        }
    } else {
        SceneMesh* sm = new SceneMesh();
        for (const auto& v : result.result.vertices) {
            SceneVertex sv;
            sv.position = QVector3D(v.position.x(), v.position.y(), v.position.z());
            sv.color = QVector4D(v.color.x(), v.color.y(), v.color.z(), v.color.w());
            sm->geometry().vertices.append(sv);
        }
        for (const auto& f : result.result.faces) {
            for (int idx : f.indices)
                sm->geometry().indices.append((uint32_t)idx);
        }
        obj->setMesh(sm);
    }

    emit KSModelerQml::instance().sceneChanged();

    m_statusText = QString("Symmetry applied (%1 axis, %2 verts, %3 welded)")
        .arg(SymmetryManager::axisToString(m_axis))
        .arg(result.resultVertexCount)
        .arg(result.weldedVertices);
    emit statusChanged();
    emit symmetryApplied(true, m_statusText);
}

void SymmetryQmlBridge::previewSymmetry() {
    SceneObject* obj = getSelectedMeshObject();
    if (!obj) {
        m_statusText = "No mesh selected for preview";
        emit statusChanged();
        return;
    }

    MeshData input = sceneMeshToMeshData(obj);
    if (input.vertices.isEmpty()) return;

    SymmetryResult previewResult = SymmetryManager::mirrorMesh(
        input, m_axis, m_offset,
        SymmetryManager::ClipMode::None,
        SymmetryManager::MergeMode::NewObject,
        0.0f);

    if (!previewResult.success) return;

    ks::SceneGraph* scene = m_scene ? m_scene : KSModelerQml::instance().sceneGraph();
    if (!scene) return;

    clearPreview();

    previewResult.result.name = "__symmetry_preview__";
    SceneObject* prevObj = scene->createObject("__symmetry_preview__", SceneObject::Type::Mesh);
    if (!prevObj) return;

    SceneMesh* sm = new SceneMesh();
    for (const auto& v : previewResult.result.vertices) {
        SceneVertex sv;
        sv.position = QVector3D(v.position.x(), v.position.y(), v.position.z());
        sv.color = QVector4D(0, 1, 0, 1);
        sm->geometry().vertices.append(sv);
    }
    for (const auto& f : previewResult.result.faces) {
        for (int idx : f.indices)
            sm->geometry().indices.append((uint32_t)idx);
    }
    prevObj->setMesh(sm);
    m_previewObjectId = prevObj->id();

    emit KSModelerQml::instance().sceneChanged();

    m_previewVisible = true;
    emit previewChanged();
    m_statusText = "Preview active (green)";
    emit statusChanged();
}

void SymmetryQmlBridge::clearPreview() {
    ks::SceneGraph* scene = m_scene ? m_scene : KSModelerQml::instance().sceneGraph();
    if (!scene) return;

    if (m_previewObjectId >= 0) {
        SceneObject* prevObj = scene->findObjectById(m_previewObjectId);
        if (prevObj) {
            scene->deleteObject(prevObj);
            emit KSModelerQml::instance().sceneChanged();
        }
        m_previewObjectId = -1;
    }
    m_previewVisible = false;
    emit previewChanged();
}

}
