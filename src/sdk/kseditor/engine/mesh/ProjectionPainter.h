#pragma once

#include <QObject>
#include <QString>
#include <QVector3D>
#include <QVector2D>
#include <QImage>
#include <QMap>

struct ProjectionStencil {
    QImage image;
    QVector3D position;    // Center position in world space
    QVector3D rotation;    // Euler angles (X, Y, Z)
    QVector3D scale;       // Scale factors (X, Y, Z)
    float opacity = 1.0f;
    bool useAlpha = true;
    bool loop = false;     // Tile/stretch vs repeat
    
    ProjectionStencil() : position(0,0,0), rotation(0,0,0), scale(1,1,1) {}
    ProjectionStencil(const QImage& img, const QVector3D& pos = QVector3D(0,0,0),
                      const QVector3D& rot = QVector3D(0,0,0), const QVector3D& sc = QVector3D(1,1,1))
        : image(img), position(pos), rotation(rot), scale(sc) {}
};

class ProjectionPainter : public QObject
{
    Q_OBJECT
public:
    explicit ProjectionPainter(QObject* parent = nullptr);
    ~ProjectionPainter();
    
    // Stencil management
    void setStencil(const QImage& image);
    QImage stencil() const { return m_stencil; }
    void setStencilPosition(const QVector3D& pos);
    void setStencilRotation(const QVector3D& rot);
    void setStencilScale(const QVector3D& sc);
    void setStencilOpacity(qreal opac);
    void setStencilUseAlpha(bool useAlpha);
    void setStencilLoop(bool loop);
    
    // Mesh data for projection (must be set before projecting)
    void setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& indices,
                     const QVector<QVector2D>& uvs, const QVector<QVector3D>& normals);
    
    // Projection operations
    int projectStencilToMesh(int objectId, const QVector3D& viewportCenter, 
                             float radius, float strength, int mode,
                             const QVector2D& uvOffset = QVector2D(0,0));
    int cloneStencilToPoint(int objectId, const QVector2D& sourceUV, 
                            const QVector2D& destUV, float strength,
                            float blendMode = 1.0f);
    
signals:
    void stencilChanged();
    void projectCompleted(int affectedCount);
    void cloneCompleted(int affectedCount);
    
private:
    QImage m_stencil;
    QVector3D m_stencilPos;
    QVector3D m_stencilRot;
    QVector3D m_stencilScale;
    qreal m_stencilOpacity;
    bool m_useAlpha;
    bool m_loop;

    // Mesh data for raycasting
    QVector<QVector3D> m_meshVertices;
    QVector<int> m_meshIndices;
    QVector<QVector2D> m_meshUVs;
    QVector<QVector3D> m_meshNormals;
    
    bool projectPointToMesh(int objectId, const QVector3D& worldPos, 
                           QVector2D& uv, QVector3D& normal);
    bool rayTriangleIntersect(const QVector3D& rayOrigin, const QVector3D& rayDir,
                              const QVector3D& v0, const QVector3D& v1, const QVector3D& v2,
                              float& t, float& u, float& v) const;
    QColor sampleStencilAt(const QVector2D& uv) const;
};