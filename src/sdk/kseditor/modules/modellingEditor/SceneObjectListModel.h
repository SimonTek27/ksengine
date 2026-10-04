#pragma once

#include <QAbstractListModel>
#include <QVector3D>
#include <QVector>
#include <QColor>
#include <functional>
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"

namespace ks {

class SceneObjectListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalVertices READ totalVertices NOTIFY countChanged)
    Q_PROPERTY(int totalTriangles READ totalTriangles NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        ObjectIdRole,
        TypeRole,
        PositionRole,
        RotationRole,
        ScaleRole,
        SelectedRole,
        VertexCountRole,
        TriangleCountRole,
        HasMeshRole,
        VisibleRole,
        ObjectPtrRole,
        BaseColorRole,
        MetallicRole,
        RoughnessRole,
        OpacityRole,
        WorldPositionRole,
        WorldRotationRole,
        WorldScaleRole,
        DepthRole,
        ChildCountRole,
        DiffuseTextureRole,
        NormalTextureRole
    };

    explicit SceneObjectListModel(QObject* parent = nullptr);

    void setSceneGraph(SceneGraph* scene);
    SceneGraph* sceneGraph() const { return m_scene; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int objectIdAt(int row) const;
    Q_INVOKABLE QVector3D positionAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;

    int totalVertices() const;
    int totalTriangles() const;

    Q_INVOKABLE void refresh();

    // Texture path resolvers (set by the bridge for procedural fabrics).
    void setTextureResolvers(const std::function<QString(int objectId)>& diffuse,
                             const std::function<QString(int objectId)>& normal) {
        m_diffuseResolver = diffuse;
        m_normalResolver = normal;
    }

signals:
    void countChanged();

private slots:
    void onSceneChanged();

private:
    SceneGraph* m_scene = nullptr;
    QVector<SceneObject*> m_objects;
    std::function<QString(int)> m_diffuseResolver;
    std::function<QString(int)> m_normalResolver;
    void syncObjectList();
};

} // namespace ks
