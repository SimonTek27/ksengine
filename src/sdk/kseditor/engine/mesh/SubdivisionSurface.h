#pragma once

#include "MeshOperations.h"
#include <QVector>

namespace ks {

struct SubdivisionResult {
    MeshData mesh;
    int sourceFaces = 0;
    int sourceVertices = 0;
    int resultFaces = 0;
    int resultVertices = 0;
    double executionTimeMs = 0.0;
    bool success = false;
    QString errorMessage;
    QVector<float> vertices;
    QVector<int> faces;
};

struct CreaseEdge {
    int vertexA, vertexB;
    float sharpness = 0.0f;
};

class SubdivisionSurface {
public:
    enum Scheme { CatmullClark, Loop, Bilinear };

    static SubdivisionResult subdivide(
        const MeshData& mesh,
        int levels = 1,
        Scheme scheme = CatmullClark);

    static SubdivisionResult subdivideWithCreases(
        const MeshData& mesh,
        const QVector<CreaseEdge>& creases,
        int levels = 1,
        Scheme scheme = CatmullClark);

    static QString schemeName(Scheme scheme) {
        switch (scheme) {
            case CatmullClark: return "Catmull-Clark";
            case Loop: return "Loop";
            case Bilinear: return "Bilinear";
        }
        return "Unknown";
    }

private:
    static SubdivisionResult subdivideInternal(
        const QVector<QVector3D>& vertices,
        const QVector<int>& faceSizes,
        const QVector<int>& faceIndices,
        const QVector<CreaseEdge>& creases,
        int levels,
        Scheme scheme);
};

}
