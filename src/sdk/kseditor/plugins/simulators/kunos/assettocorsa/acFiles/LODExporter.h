#pragma once

#include <QString>
#include <QVector>
#include <QVector3D>
#include <QMatrix4x4>
#include "sdk/kseditor/engine/mesh/MeshOperations.h"
#include "tools/LODSystem.h"

namespace ks {

struct LODSettings {
    int lodCount;
    QVector<float> distances;
    QVector<float> decimateRatios;
    bool separateFiles;
    QString outputPattern;
};

class LODExporter {
public:
    static bool exportLODs(const QString& basePath, const MeshData& highPoly, const LODSettings& settings);

    static MeshData generateLOD(const MeshData& source, float decimateRatio, int targetTris = -1);

    static MeshData reduceVertices(MeshData source, int targetCount);

    static MeshData simplifyMesh(const MeshData& source, float targetRatio);

    static float calculateScreenSize(const MeshData& mesh, float distance, float fov);

    static int estimateLODTriangles(int highPolyTris, int lodIndex, int totalLODs);

    static bool validateLODChain(const QVector<LODLevel>& lods);

    static QString getLODFileName(const QString& baseName, int lodIndex);

    static QVector3D calculateBoundingSphere(const MeshData& mesh);
};

}
