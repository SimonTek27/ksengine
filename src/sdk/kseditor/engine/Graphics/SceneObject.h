#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QMatrix4x4>
#include <QMatrix3x3>
#include <QVector3D>
#include <QQuaternion>
#include <QColor>
#include <QUuid>
#include <QJsonObject>
#include <functional>
#include <cstring>

#include "Math/MathCore.h"

namespace ks {

class SceneGraph;
class SceneMesh;
class Material;

class SceneObject : public QObject
{
    Q_OBJECT

public:
    enum class Type {
        Empty,
        Node,
        Mesh,
        Light,
        Camera,
        Decal,
        Volume,
        Spline,
        ParticleSystem,
        Bone,
        Unknown = -1
    };

    SceneObject(int id, const QString& name = "Object", Type type = Type::Empty);
    explicit SceneObject(SceneGraph* graph, const QString& name = "Object");
    ~SceneObject() override;

    int id() const { return m_id; }
    QString name() const { return m_name; }
    Type type() const { return m_type; }

    SceneObject* parent() const { return m_parent; }
    const QVector<SceneObject*>& children() const { return m_children; }
    void addChild(SceneObject* child);
    void removeChild(SceneObject* child);
    SceneObject* findChild(const QString& name);
    QVector<SceneObject*> findChildren(const QString& name, bool recursive = true) const;

    SceneGraph* sceneGraph() const { return m_sceneGraph; }
    void setSceneGraph(SceneGraph* graph) { m_sceneGraph = graph; }

    QVector3D position() const;
    void setPosition(const QVector3D& pos);
    void setTranslation(const QVector3D& pos) { setPosition(pos); }
    QVector3D translation() const { return position(); }
    QVector3D rotationEuler() const;
    void setRotationEuler(const QVector3D& euler);
    QVector3D scale() const;
    void setScale(const QVector3D& s);

    // World-space transform accessors (recompute the cached world matrix if dirty).
    QVector3D worldPosition() const;
    QVector3D worldRotationEuler() const;
    QVector3D worldScale() const;

    QMatrix4x4 transform() const {
        QMatrix4x4 mat;
        memcpy(mat.data(), m_transform.m, sizeof(float) * 16);
        return mat;
    }
    QMatrix4x4 worldTransform() const {
        QMatrix4x4 mat;
        memcpy(mat.data(), m_worldTransform.m, sizeof(float) * 16);
        return mat;
    }

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);

    QVector3D boundingBoxMin() const { return m_boundingMin; }
    QVector3D boundingBoxMax() const { return m_boundingMax; }
    float boundingRadius() const { return m_boundingRadius; }

    bool isSelected() const { return m_selected; }
    void setSelected(bool selected) { m_selected = selected; }

    bool hasMesh() const { return m_mesh != nullptr; }
    SceneMesh* mesh() const { return m_mesh; }
    void setMesh(SceneMesh* mesh);
    Material* material() const { return m_material; }
    void setMaterial(Material* material);

QColor baseColor() const { return m_baseColor; }
    float metallic() const { return m_metallic; }
    float roughness() const { return m_roughness; }
    float opacity() const { return m_opacity; }

    void setBaseColor(const QColor& color) { m_baseColor = color; }
    void setMetallic(float value) { m_metallic = value; }
    void setRoughness(float value) { m_roughness = value; }
    void setOpacity(float value) { m_opacity = qBound(0.0f, value, 1.0f); }

    void updateWorldTransform(bool force = false) const;

    SceneObject* findById(int id) const;
    SceneObject* findByName(const QString& name, bool recursive = true) const;
    QVector<SceneObject*> findByType(Type type, bool recursive = true) const;

    void traverse(const std::function<void(SceneObject*)>& visitor) const;
    int descendantCount() const;

    QJsonObject serialize() const;
    void deserialize(const QJsonObject& json);
    static SceneObject* fromJson(const QJsonObject& obj, int& nextId);

    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

    void setName(const QString& name) { m_name = name; }

signals:
    void transformChanged();
    void visibilityChanged(bool visible);
    void meshChanged();

private:
    int m_id = 0;
    QString m_name;
    Type m_type = Type::Empty;

    SceneGraph* m_sceneGraph = nullptr;
    SceneObject* m_parent = nullptr;
    QVector<SceneObject*> m_children;

    mutable Matrix4 m_transform = Matrix4::Identity();
    mutable Matrix4 m_worldTransform = Matrix4::Identity();

    bool m_visible = true;
    mutable bool m_dirty = true;
    bool m_selected = false;

    QColor m_baseColor = QColor(200, 200, 200, 255);
    float m_metallic = 0.0f;
    float m_roughness = 0.5f;
    float m_opacity = 1.0f;

    QVector3D m_boundingMin = {0, 0, 0};
    QVector3D m_boundingMax = {0, 0, 0};
    float m_boundingRadius = 0.0f;

    SceneMesh* m_mesh = nullptr;
    Material* m_material = nullptr;

    void* m_physicsBody = nullptr;
};

} // namespace ks
