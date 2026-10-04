#include "SymmetryManager.h"
#include <QElapsedTimer>

namespace ks {

float SymmetryManager::signedDistanceToPlane(const QVector3D& pos, Axis axis, float offset) {
    switch (axis) {
        case Axis::X: return pos.x() - offset;
        case Axis::Y: return pos.y() - offset;
        case Axis::Z: return pos.z() - offset;
    }
    return 0.0f;
}

MeshData SymmetryManager::clipMesh(
    const MeshData& input, Axis axis, float offset, bool keepPositive)
{
    MeshData result;

    QVector<int> vertRemap(input.vertices.size(), -1);
    QVector<bool> vertKeep(input.vertices.size(), false);

    for (int i = 0; i < input.vertices.size(); ++i) {
        float dist = signedDistanceToPlane(input.vertices[i].position, axis, offset);
        bool keep = keepPositive ? (dist >= 0) : (dist <= 0);
        if (keep) {
            vertRemap[i] = result.vertices.size();
            result.vertices.append(input.vertices[i]);
            vertKeep[i] = true;
        }
    }

    for (const auto& face : input.faces) {
        bool allKept = true;
        for (int idx : face.indices) {
            if (idx < 0 || idx >= vertKeep.size() || !vertKeep[idx]) {
                allKept = false;
                break;
            }
        }
        if (allKept && face.indices.size() >= 3) {
            Face newFace;
            for (int idx : face.indices) {
                newFace.indices.append(vertRemap[idx]);
            }
            newFace.materialId = face.materialId;
            result.faces.append(newFace);
        }
    }

    result.name = input.name;
    result.materialName = input.materialName;
    result.diffuseColor = input.diffuseColor;
    result.metallic = input.metallic;
    result.roughness = input.roughness;
    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData SymmetryManager::createMirrorHalf(const MeshData& input, Axis axis, float offset) {
    QVector3D axisVec = axisToVector(axis);
    MeshData mirrored = MeshOperations::mirror(input, axisVec, offset);
    return mirrored;
}

MeshData SymmetryManager::weldAtPlane(
    const MeshData& mesh, Axis axis, float offset, float threshold)
{
    if (threshold <= 0.0f) return mesh;
    return MeshOperations::weldVertices(mesh, threshold);
}

SymmetryResult SymmetryManager::mirrorMesh(
    const MeshData& input,
    Axis axis,
    float offset,
    ClipMode clipMode,
    MergeMode mergeMode,
    float weldThreshold)
{
    QElapsedTimer timer;
    timer.start();

    SymmetryResult result;
    result.originalVertexCount = input.vertices.size();
    result.originalFaces = input.faces.size();

    if (input.vertices.isEmpty()) {
        result.success = false;
        result.errorMessage = "Input mesh is empty";
        return result;
    }

    MeshData workMesh = input;

    if (clipMode != ClipMode::None) {
        workMesh = clipMesh(workMesh, axis, offset,
            clipMode == ClipMode::KeepPositive);
    }

    MeshData mirroredHalf = createMirrorHalf(workMesh, axis, offset);

    MeshData outputMesh;
    switch (mergeMode) {
        case MergeMode::Append: {
            outputMesh = workMesh;
            int baseVertCount = outputMesh.vertices.size();
            for (const auto& v : mirroredHalf.vertices)
                outputMesh.vertices.append(v);
            for (const auto& f : mirroredHalf.faces) {
                Face nf;
                for (int idx : f.indices)
                    nf.indices.append(idx + baseVertCount);
                nf.materialId = f.materialId;
                outputMesh.faces.append(nf);
            }
            break;
        }
        case MergeMode::Replace: {
            outputMesh = mirroredHalf;
            break;
        }
        case MergeMode::NewObject: {
            outputMesh = mirroredHalf;
            outputMesh.name = input.name + "_sym";
            break;
        }
    }

    if (weldThreshold > 0.0f) {
        int preWeldVerts = outputMesh.vertices.size();
        outputMesh = weldAtPlane(outputMesh, axis, offset, weldThreshold);
        result.weldedVertices = preWeldVerts - outputMesh.vertices.size();
    }

    outputMesh.computeNormals();
    outputMesh.computeBoundingBox();

    result.result = outputMesh;
    result.resultVertexCount = outputMesh.vertices.size();
    result.resultFaces = outputMesh.faces.size();
    result.executionTimeMs = timer.elapsed();
    result.success = true;

    return result;
}

}
