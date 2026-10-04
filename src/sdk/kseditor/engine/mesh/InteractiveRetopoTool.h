#ifndef INTERACTIVERETOPOOOL_H
#define INTERACTIVERETOPOOOL_H

#include "MeshOperations.h"
#include <QObject>
#include <QVector>
#include <QVector3D>
#include <QMatrix4x4>
#include <QSet>

namespace ks {

class InteractiveRetopoTool : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isActive READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(float snapRadius READ snapRadius WRITE setSnapRadius NOTIFY snapRadiusChanged)
    Q_PROPERTY(int selectedVertex READ selectedVertex WRITE setSelectedVertex NOTIFY selectedVertexChanged)

public:
    explicit InteractiveRetopoTool(QObject* parent = nullptr);
    ~InteractiveRetopoTool();

    bool isActive() const { return m_isActive; }
    void setActive(bool active);

    float snapRadius() const { return m_snapRadius; }
    void setSnapRadius(float radius);

    int selectedVertex() const { return m_selectedVertex; }
    void setSelectedVertex(int index);

    void setHighPolyMesh(const MeshData& mesh);
    void setLowPolyMesh(MeshData* mesh);

    bool handleMousePress(const QVector3D& rayOrigin, const QVector3D& rayDir, const QMatrix4x4& viewProj, const QSize& viewportSize);
    bool handleMouseMove(const QVector3D& rayOrigin, const QVector3D& rayDir, const QMatrix4x4& viewProj, const QSize& viewportSize);
    bool handleMouseRelease(const QVector3D& rayOrigin, const QVector3D& rayDir, const QMatrix4x4& viewProj, const QSize& viewportSize);

    QVector<QVector3D> getSnapPreviewPoints() const { return m_snapPreviewPoints; }
    QVector<int> getSnapPreviewIndices() const { return m_snapPreviewIndices; }

    Q_INVOKABLE bool addVertexAtCursor(const QVector3D& rayOrigin, const QVector3D& rayDir, const QMatrix4x4& viewProj, const QSize& viewportSize);
    Q_INVOKABLE bool createQuadFromSelection();
    Q_INVOKABLE bool createTriangleFromSelection();
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE bool mergeVertices(float threshold = 0.01f);
    Q_INVOKABLE bool relaxMesh(int iterations = 10, float strength = 0.5f);

signals:
    void activeChanged();
    void snapRadiusChanged();
    void selectedVertexChanged();
    void meshChanged();
    void statusMessage(const QString& message);

private:
    QVector3D snapToHighPoly(const QVector3D& point) const;
    QVector3D projectOnHighPoly(const QVector3D& rayOrigin, const QVector3D& rayDir) const;
    bool rayTriangleIntersect(const QVector3D& rayOrigin, const QVector3D& rayDir,
                             const QVector3D& v0, const QVector3D& v1, const QVector3D& v2,
                             float& t, QVector3D& baryCoords) const;
    void updateSnapPreview();

    bool m_isActive = false;
    float m_snapRadius = 0.1f;
    int m_selectedVertex = -1;

    MeshData m_highPolyMesh;
    MeshData* m_lowPolyMesh = nullptr;

    QVector<QVector3D> m_snapPreviewPoints;
    QVector<int> m_snapPreviewIndices;

    QVector<int> m_selectedVertices;
    QVector3D m_lastMousePos;
    bool m_isDragging = false;

    struct Triangle {
        QVector3D v0, v1, v2;
        QVector3D normal;
        int faceIndex;
    };
    QVector<Triangle> m_triangleCache;
    bool m_cacheDirty = true;
};

} // namespace ks

#endif // INTERACTIVERETOPOOOL_H
