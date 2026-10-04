#pragma once

#include "sdk/kseditor/engine/mesh/MeshOperations.h"
#include <QString>

namespace ks {

struct SymmetryResult {
    MeshData result;
    int originalVertexCount = 0;
    int resultVertexCount = 0;
    int weldedVertices = 0;
    int originalFaces = 0;
    int resultFaces = 0;
    double executionTimeMs = 0.0;
    bool success = false;
    QString errorMessage;
};

class SymmetryManager {
public:
    enum class Axis { X = 0, Y = 1, Z = 2 };
    enum class ClipMode { None = 0, KeepPositive = 1, KeepNegative = 2 };
    enum class MergeMode { Append = 0, Replace = 1, NewObject = 2 };

    static SymmetryResult mirrorMesh(
        const MeshData& input,
        Axis axis,
        float offset = 0.0f,
        ClipMode clipMode = ClipMode::None,
        MergeMode mergeMode = MergeMode::Append,
        float weldThreshold = 0.001f);

    static MeshData weldAtPlane(
        const MeshData& mesh,
        Axis axis,
        float offset,
        float threshold = 0.001f);

    static MeshData clipMesh(
        const MeshData& input,
        Axis axis,
        float offset,
        bool keepPositive);

    static MeshData createMirrorHalf(
        const MeshData& input,
        Axis axis,
        float offset);

    static QString axisToString(Axis axis) {
        return axis == Axis::X ? "X" : (axis == Axis::Y ? "Y" : "Z");
    }

    static QVector3D axisToVector(Axis axis) {
        return axis == Axis::X ? QVector3D(1,0,0) : (axis == Axis::Y ? QVector3D(0,1,0) : QVector3D(0,0,1));
    }

private:
    static float signedDistanceToPlane(const QVector3D& pos, Axis axis, float offset);
};

}

Q_DECLARE_METATYPE(ks::SymmetryResult)
Q_DECLARE_METATYPE(ks::SymmetryManager::Axis)
Q_DECLARE_METATYPE(ks::SymmetryManager::ClipMode)
Q_DECLARE_METATYPE(ks::SymmetryManager::MergeMode)
