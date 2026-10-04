#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QMatrix4x4>
#include <QJsonObject>
#include <QMap>

namespace ks {

struct MeshVertex {
    QVector3D position;
    QVector3D normal;
    QVector2D uv;
    QVector4D color;
    QVector<int> boneIndices;
    QVector<float> boneWeights;
};

struct MeshFace {
    int v1, v2, v3;
    int materialId;
};

struct VertexGroup {
    QString name;
    QVector<int> vertexIndices;
};

struct ModifierData {
    QString type;
    QJsonObject params;
};

class MeshObject {
public:
    QString id;
    QString name;
    QString meshType;
    QVector<MeshVertex> vertices;
    QVector<MeshFace> faces;
    QVector<VertexGroup> vertexGroups;
    QVector<ModifierData> modifiers;
    QMatrix4x4 transform;
    bool visible = true;

    QVector3D getCenter() const;
    void applyTransform();
};

class MeshModifier : public QObject {
    Q_OBJECT
public:
    explicit MeshModifier(QObject* parent = nullptr);
    ~MeshModifier();

    static MeshModifier* instance();

    QVector<int> findMeshesByName(const QString& infix);
    int findMeshByName(const QString& name);

    MeshObject* getMesh(int meshId);
    void setMesh(int meshId, MeshObject* mesh);

    bool createVertexGroup(int meshId, const QString& groupName, const QVector<int>& vertexIndices);
    bool removeVertexGroup(int meshId, const QString& groupName);
    QVector<int> getVerticesInGroup(int meshId, const QString& groupName);
    QVector<int> getVerticesInRadius(int meshId, const QVector3D& center, float radius);

    bool createBones(int meshId, int boneCount, float radius);
    bool addSkinModifier(int meshId);

    void translateVertices(int meshId, const QVector<int>& vertices, const QVector3D& delta);
    void rotateVertices(int meshId, const QVector<int>& vertices, const QVector3D& center, const QVector3D& rotation);
    void scaleVertices(int meshId, const QVector<int>& vertices, const QVector3D& center, const QVector3D& scale);

    void mirrorAlongAxis(int meshId, int axis, float threshold = 0.001f);

    void addModifier(int meshId, const QString& modifierType, const QJsonObject& params);
    bool removeModifier(int meshId, const QString& modifierType);
    bool applyModifiers(int meshId);

    QVector3D calculateMeshCenter(int meshId);
    QVector3D calculateMeshBounds(int meshId);

    int addMesh(const QString& name, const QString& type);
    void removeMesh(int meshId);

 signals:
    void meshModified(int meshId);
    void meshAdded(int meshId);
    void meshRemoved(int meshId);

private:
    static MeshModifier* s_instance;

    QMap<int, MeshObject*> m_meshes;
    int m_nextMeshId = 0;

    int findMeshIndex(int meshId);
};

} // namespace ks
