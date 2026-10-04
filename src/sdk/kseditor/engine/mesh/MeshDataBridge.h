#pragma once

#include <QObject>
#include <QVariant>
#include <QVector3D>
#include "MeshOperations.h"

namespace ks {

class MeshDataBridge : public QObject {
    Q_OBJECT

public:
    static MeshDataBridge* instance();

    Q_INVOKABLE void setCurrentMesh(const QVariant& meshData);
    Q_INVOKABLE QVariant getCurrentMesh() const { return m_currentMesh; }

    Q_INVOKABLE void updateFromViewport(const QString& meshName);
    Q_INVOKABLE void pushToViewport(const QString& meshName);

    Q_INVOKABLE QVariant loopCut(int cuts, const QVariant& center, const QVariant& normal);
    Q_INVOKABLE QVariant knifeCut(const QVariant& start, const QVariant& end, bool snapToVertex = true);
    Q_INVOKABLE QVariant bisectCut(const QVariant& planePoint, const QVariant& planeNormal, bool cutCenter = true);

    Q_INVOKABLE QVariant subdivide(int levels = 1);
    Q_INVOKABLE QVariant triangulate();
    Q_INVOKABLE QVariant quadrangulate();

    Q_INVOKABLE QVariant extrude(float distance, bool individual = false);
    Q_INVOKABLE QVariant bevel(float distance, int segments = 1);
    Q_INVOKABLE QVariant inset(float distance);

    Q_INVOKABLE QVariant decimate(float targetRatio);
    Q_INVOKABLE QVariant triRemesh();
    Q_INVOKABLE QVariant quadRemesh(int targetCount = 1000);

    Q_INVOKABLE QVariant fillHoles(int maxSize = 100);
    Q_INVOKABLE QVariant planarFaces(float threshold = 0.001f);

    Q_INVOKABLE void translate(const QVariant& delta);
    Q_INVOKABLE void rotate(const QVariant& euler);
    Q_INVOKABLE void scale(const QVariant& factors);

    Q_INVOKABLE void duplicate();
    Q_INVOKABLE void deleteSelected();
    Q_INVOKABLE void mirror(const QVariant& axis);

    Q_INVOKABLE QVariant getSelectionBounds();
    Q_INVOKABLE QVariant getVertexPositions();
    Q_INVOKABLE QVariant getFacePositions();
    Q_INVOKABLE QVariant getNormals();

    Q_INVOKABLE int getVertexCount();
    Q_INVOKABLE int getFaceCount();
    Q_INVOKABLE int getEdgeCount();

    signals:
        void meshModified(const QString& meshName);
        void selectionChanged();
        void boundsChanged(const QVariant& bounds);

private:
    MeshDataBridge(QObject* parent = nullptr);
    ~MeshDataBridge();
    Q_DISABLE_COPY(MeshDataBridge)

    static MeshDataBridge* s_instance;

    QVariant m_currentMesh;
    MeshData m_meshData;
    QString m_currentMeshName;

    QVector<int> m_selectedVertices;
    QVector<int> m_selectedFaces;
    QVector<int> m_selectedEdges;

    void updateFromMeshData();
    void updateMeshDataFromVariant();
    void emitMeshModified();
};

}