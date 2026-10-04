#include "SceneObjectListModel.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include <QUrl>

namespace ks {

SceneObjectListModel::SceneObjectListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void SceneObjectListModel::setSceneGraph(ks::SceneGraph* scene) {
    m_scene = scene;
    syncObjectList();
    emit countChanged();
}

void SceneObjectListModel::syncObjectList() {
    beginResetModel();
    m_objects.clear();
    if (m_scene) {
        auto all = m_scene->allObjects();
        for (SceneObject* obj : all) {
            if (obj->type() == SceneObject::Type::Mesh && obj->hasMesh()) {
                m_objects.append(obj);
            } else if (obj->type() == SceneObject::Type::Spline && obj->hasMesh()) {
                // Curves render through their ribbon mesh preview.
                m_objects.append(obj);
            }
        }
    }
    endResetModel();
}

int SceneObjectListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_objects.size();
}

QVariant SceneObjectListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_objects.size())
        return QVariant();

    SceneObject* obj = m_objects[index.row()];
    if (!obj) return QVariant();

    switch (role) {
    case NameRole:
        return obj->name();
    case ObjectIdRole:
        return obj->id();
    case TypeRole: {
        switch (obj->type()) {
        case SceneObject::Type::Node: return "Node";
        case SceneObject::Type::Mesh: return "Mesh";
        case SceneObject::Type::Spline: return "Spline";
        case SceneObject::Type::Light: return "Light";
        case SceneObject::Type::Camera: return "Camera";
        case SceneObject::Type::Bone: return "Bone";
        default: return "Unknown";
        }
    }
    case PositionRole: {
        return obj->position();
    }
    case RotationRole: {
        return obj->rotationEuler();
    }
    case ScaleRole: {
        return obj->scale();
    }
    case SelectedRole:
        return obj->isSelected();
    case VertexCountRole: {
        if (obj->mesh()) return (int)obj->mesh()->geometry().vertices.size();
        return 0;
    }
    case TriangleCountRole: {
        if (obj->mesh()) return (int)(obj->mesh()->geometry().indices.size() / 3);
        return 0;
    }
    case HasMeshRole:
        return obj->hasMesh();
    case VisibleRole:
        return obj->isVisible();
    case ObjectPtrRole:
        return QVariant::fromValue(reinterpret_cast<quintptr>(obj));
    case BaseColorRole:
        return QVariant::fromValue(obj->baseColor());
    case MetallicRole:
        return obj->metallic();
    case RoughnessRole:
        return obj->roughness();
    case OpacityRole:
        return obj->opacity();
    case WorldPositionRole:
        return obj->worldPosition();
    case WorldRotationRole:
        return obj->worldRotationEuler();
    case WorldScaleRole:
        return obj->worldScale();
    case DepthRole: {
        int depth = 0;
        for (SceneObject* p = obj->parent(); p; p = p->parent())
            ++depth;
        return depth;
    }
    case ChildCountRole:
        return (int)obj->children().size();
    case DiffuseTextureRole:
        if (m_diffuseResolver) {
            const QString p = m_diffuseResolver(obj->id());
            if (!p.isEmpty()) return QUrl::fromLocalFile(p).toString();
        }
        return QString();
    case NormalTextureRole:
        if (m_normalResolver) {
            const QString p = m_normalResolver(obj->id());
            if (!p.isEmpty()) return QUrl::fromLocalFile(p).toString();
        }
        return QString();
    }
    return QVariant();
}

QHash<int, QByteArray> SceneObjectListModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[NameRole] = "objectName";
    roles[ObjectIdRole] = "objectId";
    roles[TypeRole] = "objectType";
    roles[PositionRole] = "objectPosition";
    roles[RotationRole] = "objectRotation";
    roles[ScaleRole] = "objectScale";
    roles[SelectedRole] = "objectSelected";
    roles[VertexCountRole] = "vertexCount";
    roles[TriangleCountRole] = "triangleCount";
    roles[HasMeshRole] = "hasMesh";
    roles[VisibleRole] = "objectVisible";
    roles[BaseColorRole] = "baseColor";
    roles[MetallicRole] = "metallic";
    roles[RoughnessRole] = "roughness";
    roles[OpacityRole] = "opacity";
    roles[WorldPositionRole] = "objectWorldPosition";
    roles[WorldRotationRole] = "objectWorldRotation";
    roles[WorldScaleRole] = "objectWorldScale";
    roles[DepthRole] = "objectDepth";
    roles[ChildCountRole] = "childCount";
    roles[DiffuseTextureRole] = "diffuseTexture";
    roles[NormalTextureRole] = "normalTexture";
    return roles;
}

int SceneObjectListModel::objectIdAt(int row) const {
    if (row < 0 || row >= m_objects.size()) return -1;
    return m_objects[row] ? m_objects[row]->id() : -1;
}

QVector3D SceneObjectListModel::positionAt(int row) const {
    if (row < 0 || row >= m_objects.size()) return QVector3D();
    return m_objects[row]->position();
}

QString SceneObjectListModel::nameAt(int row) const {
    if (row < 0 || row >= m_objects.size()) return QString();
    return m_objects[row]->name();
}

void SceneObjectListModel::refresh() {
    syncObjectList();
    emit countChanged();
}

void SceneObjectListModel::onSceneChanged() {
    refresh();
}

int SceneObjectListModel::totalVertices() const {
    int count = 0;
    for (SceneObject* obj : m_objects) {
        if (obj && obj->mesh())
            count += obj->mesh()->geometry().vertices.size();
    }
    return count;
}

int SceneObjectListModel::totalTriangles() const {
    int count = 0;
    for (SceneObject* obj : m_objects) {
        if (obj && obj->mesh())
            count += obj->mesh()->geometry().indices.size() / 3;
    }
    return count;
}

} // namespace ks
