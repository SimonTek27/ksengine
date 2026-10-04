#pragma once
#include <QObject>
#include <QQuickItem>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QMatrix4x4>
#include <QVector>
#include <QString>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>

struct MeshVertex {
    QVector3D position;
    QVector3D normal;
    QVector2D texCoord;
    QVector4D color;
};

class MeshRenderer : public QObject
{
    Q_OBJECT

public:
    explicit MeshRenderer(QObject *parent = nullptr);
    ~MeshRenderer();

    bool loadFromKN5(const QString &filePath);
    bool loadFromOBJ(const QString &filePath);
    bool loadFromGLTF(const QString &filePath);

    void setPosition(const QVector3D &pos) { m_position = pos; }
    void setRotation(const QVector3D &rot) { m_rotation = rot; }
    void setScale(const QVector3D &scale) { m_scale = scale; }

    QVector3D getPosition() const { return m_position; }
    QVector3D getRotation() const { return m_rotation; }
    QVector3D getScale() const { return m_scale; }

    Q_INVOKABLE int getVertexCount() const { return m_vertices.size(); }
    Q_INVOKABLE int getFaceCount() const { return m_indices.size() / 3; }
    Q_INVOKABLE QString getName() const { return m_meshName; }

    const QVector<MeshVertex>& getVertices() const { return m_vertices; }
    const QVector<quint32>& getIndices() const { return m_indices; }

    QMatrix4x4 getTransformMatrix() const;

signals:
    void meshLoaded(const QString &name, int vertexCount, int faceCount);
    void loadError(const QString &error);

private:
    struct KN5Header {
        char magic[4];
        quint32 version;
        quint32 flags;
        quint32 headerSize;
    };

    bool parseKN5Header(QDataStream &stream, KN5Header &header);
    bool parseKN5Meshes(QDataStream &stream, const KN5Header &header);
    bool parseKN5Materials(QDataStream &stream, const KN5Header &header);

    QString m_meshName;
    QVector<MeshVertex> m_vertices;
    QVector<quint32> m_indices;
    QVector<QString> m_materials;
    QVector<QString> m_textures;

    QVector3D m_position;
    QVector3D m_rotation;
    QVector3D m_scale;
};