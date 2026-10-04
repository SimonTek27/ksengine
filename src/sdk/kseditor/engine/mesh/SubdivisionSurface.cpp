#include "SubdivisionSurface.h"
#include <QElapsedTimer>
#include <QDebug>
#include <cmath>

#if HAS_OPENSUBDIV
#include <opensubdiv/far/topologyRefiner.h>
#include <opensubdiv/far/topologyRefinerFactory.h>
#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/primvarRefiner.h>
#include <opensubdiv/sdc/scheme.h>
#endif

namespace ks {

#if HAS_OPENSUBDIV
using namespace OpenSubdiv;

struct SubdVertex {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    void Clear() { x = y = z = 0.0f; }
    void AddWithWeight(SubdVertex const& src, float weight) {
        x += weight * src.x;
        y += weight * src.y;
        z += weight * src.z;
    }
};

static Far::TopologyRefiner* createTopologyRefiner(
    const QVector<QVector3D>& vertices,
    const QVector<int>& faceSizes,
    const QVector<int>& faceIndices,
    const QVector<CreaseEdge>& creases,
    Sdc::SchemeType schemeType)
{
    Sdc::Options options;

    std::vector<int> creasePairs;
    std::vector<float> creaseSharpness;
    creasePairs.reserve(creases.size() * 2);
    creaseSharpness.reserve(creases.size());
    for (const auto& c : creases) {
        creasePairs.push_back(c.vertexA);
        creasePairs.push_back(c.vertexB);
        creaseSharpness.push_back(c.sharpness);
    }

    Far::TopologyDescriptor desc;
    desc.numVertices = vertices.size();
    desc.numFaces = faceSizes.size();
    desc.numVertsPerFace = faceSizes.constData();
    desc.vertIndicesPerFace = faceIndices.constData();
    desc.numCreases = creases.size();
    desc.creaseVertexIndexPairs = creasePairs.empty() ? nullptr : creasePairs.data();
    desc.creaseWeights = creaseSharpness.empty() ? nullptr : creaseSharpness.data();
    desc.numCorners = 0;
    desc.cornerVertexIndices = nullptr;
    desc.cornerWeights = nullptr;
    desc.numHoles = 0;
    desc.holeIndices = nullptr;
    desc.isLeftHanded = false;
    desc.numFVarChannels = 0;
    desc.fvarChannels = nullptr;

    return Far::TopologyRefinerFactory<Far::TopologyDescriptor>::Create(
        desc,
        Far::TopologyRefinerFactory<Far::TopologyDescriptor>::Options(schemeType, options));
}
#endif

SubdivisionResult SubdivisionSurface::subdivide(
    const MeshData& mesh, int levels, Scheme scheme)
{
    return subdivideWithCreases(mesh, {}, levels, scheme);
}

SubdivisionResult SubdivisionSurface::subdivideWithCreases(
    const MeshData& mesh,
    const QVector<CreaseEdge>& creases,
    int levels,
    Scheme scheme)
{
    SubdivisionResult result;
    result.sourceFaces = mesh.faces.size();
    result.sourceVertices = mesh.vertices.size();

    QElapsedTimer timer;
    timer.start();

    if (levels < 1 || mesh.faces.isEmpty() || mesh.vertices.isEmpty()) {
        result.mesh = mesh;
        result.success = true;
        return result;
    }

    QVector<QVector3D> positions;
    positions.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices)
        positions.append(v.position);

    QVector<int> faceSizes;
    QVector<int> faceIndices;
    faceSizes.reserve(mesh.faces.size());
    faceIndices.reserve(mesh.faces.size() * 3);

    for (const auto& face : mesh.faces) {
        faceSizes.append(face.indices.size());
        for (int idx : face.indices)
            faceIndices.append(idx);
    }

    auto internalResult = subdivideInternal(positions, faceSizes, faceIndices, creases, levels, scheme);

    result.resultVertices = internalResult.resultVertices;
    result.resultFaces = internalResult.resultFaces;
    result.success = internalResult.success;
    result.errorMessage = internalResult.errorMessage;
    result.executionTimeMs = internalResult.executionTimeMs;
    result.mesh = internalResult.mesh;

    return result;
}

SubdivisionResult SubdivisionSurface::subdivideInternal(
    const QVector<QVector3D>& vertices,
    const QVector<int>& faceSizes,
    const QVector<int>& faceIndices,
    const QVector<CreaseEdge>& creases,
    int levels,
    Scheme scheme)
{
    SubdivisionResult result;
    MeshData resultMesh;

#if HAS_OPENSUBDIV
    try {
        Sdc::SchemeType schemeType = Sdc::SCHEME_CATMARK;
        if (scheme == Loop)
            schemeType = Sdc::SCHEME_LOOP;
        else if (scheme == Bilinear)
            schemeType = Sdc::SCHEME_BILINEAR;

        Far::TopologyRefiner* refiner = createTopologyRefiner(
            vertices, faceSizes, faceIndices, creases, schemeType);

        if (!refiner) {
            result.errorMessage = "Failed to create OpenSubdiv topology refiner";
            return result;
        }

        refiner->RefineUniform(Far::TopologyRefiner::UniformOptions(levels));

        int nVertBase = refiner->GetLevel(0).GetNumVertices();
        int nVertTotal = refiner->GetNumVerticesTotal();
        int nFacesFinal = refiner->GetLevel(levels).GetNumFaces();

        std::vector<SubdVertex> srcBuffer(nVertTotal);
        for (int i = 0; i < nVertBase && i < vertices.size(); ++i) {
            srcBuffer[i].x = vertices[i].x();
            srcBuffer[i].y = vertices[i].y();
            srcBuffer[i].z = vertices[i].z();
        }

        Far::PrimvarRefinerReal<float> primvarRefiner(*refiner);

        SubdVertex* src = srcBuffer.data();
        for (int level = 1; level <= levels; ++level) {
            SubdVertex* dst = src + refiner->GetLevel(level - 1).GetNumVertices();
            primvarRefiner.Interpolate(level, src, dst);
            src = dst;
        }

        resultMesh.vertices.resize(nVertTotal);
        for (int i = 0; i < nVertTotal; ++i) {
            resultMesh.vertices[i].position.setX(srcBuffer[i].x);
            resultMesh.vertices[i].position.setY(srcBuffer[i].y);
            resultMesh.vertices[i].position.setZ(srcBuffer[i].z);
        }

        const auto& refinerLevel = refiner->GetLevel(levels);
        for (int fi = 0; fi < nFacesFinal; ++fi) {
            Far::ConstIndexArray fv = refinerLevel.GetFaceVertices(fi);
            if (fv.size() >= 3) {
                resultMesh.faces.append(Face());
                auto& f = resultMesh.faces.last();
                f.indices.reserve(fv.size());
                for (int i = 0; i < fv.size(); ++i)
                    f.indices.append(fv[i]);
            }
        }

        resultMesh.computeNormals();
        resultMesh.computeBoundingBox();
        result.mesh = resultMesh;
        result.resultVertices = nVertTotal;
        result.resultFaces = nFacesFinal;
        result.success = true;

        delete refiner;

    } catch (const std::exception& e) {
        result.errorMessage = QString("OpenSubdiv exception: %1").arg(e.what());
    } catch (...) {
        result.errorMessage = "Unknown OpenSubdiv exception";
    }
#else
    // Fallback: simple triangle subdivision (split each triangle into 4)
    Q_UNUSED(creases);
    Q_UNUSED(scheme);

    if (levels < 1) {
        result.vertices.resize(vertices.size() * 3);
        for (int i = 0; i < vertices.size(); ++i) {
            result.vertices[i * 3] = vertices[i].x();
            result.vertices[i * 3 + 1] = vertices[i].y();
            result.vertices[i * 3 + 2] = vertices[i].z();
        }
        result.faces = faceIndices;
        result.success = true;
        return result;
    }

    QVector<float> outVerts;
    outVerts.resize(vertices.size() * 3);
    for (int i = 0; i < vertices.size(); ++i) {
        outVerts[i * 3] = vertices[i].x();
        outVerts[i * 3 + 1] = vertices[i].y();
        outVerts[i * 3 + 2] = vertices[i].z();
    }
    QVector<int> outFaces = faceIndices;
    QVector<int> outSizes = faceSizes;

    for (int level = 0; level < levels; ++level) {
        QVector<float> nextVerts = outVerts;
        QVector<int> nextFaces;
        QVector<int> nextSizes;

        int faceCount = outSizes.size();
        int vertOffset = 0;
        for (int f = 0; f < faceCount; ++f) {
            int nv = outSizes[f];
            int baseIdx = 0;
            for (int i = 0; i < f; ++i)
                baseIdx += outSizes[i];

            // Only subdivide triangles (3 vertices) and quads (4 vertices)
            if (nv < 3) continue;

            // Collect face vertex indices
            QVector<int> fv(nv);
            int sum = 0;
            for (int i = 0; i < nv; ++i)
                fv[i] = outFaces[vertOffset + i];

            // For quads, treat as two triangles
            if (nv == 4) {
                int i0 = fv[0], i1 = fv[1], i2 = fv[2], i3 = fv[3];

                // Edge midpoints
                int m01 = -1, m12 = -1, m23 = -1, m30 = -1;

                auto findOrAddMid = [&](int a, int b) -> int {
                    float mx = (outVerts[a * 3] + outVerts[b * 3]) * 0.5f;
                    float my = (outVerts[a * 3 + 1] + outVerts[b * 3 + 1]) * 0.5f;
                    float mz = (outVerts[a * 3 + 2] + outVerts[b * 3 + 2]) * 0.5f;
                    int idx = nextVerts.size() / 3;
                    nextVerts.append({mx, my, mz});
                    return idx;
                };

                int center = -1;
                {
                    float cx = (outVerts[i0*3] + outVerts[i1*3] + outVerts[i2*3] + outVerts[i3*3]) * 0.25f;
                    float cy = (outVerts[i0*3+1] + outVerts[i1*3+1] + outVerts[i2*3+1] + outVerts[i3*3+1]) * 0.25f;
                    float cz = (outVerts[i0*3+2] + outVerts[i1*3+2] + outVerts[i2*3+2] + outVerts[i3*3+2]) * 0.25f;
                    center = nextVerts.size() / 3;
                    nextVerts.append({cx, cy, cz});
                }

                m01 = findOrAddMid(i0, i1);
                m12 = findOrAddMid(i1, i2);
                m23 = findOrAddMid(i2, i3);
                m30 = findOrAddMid(i3, i0);

                // 4 quads from center + edge midpoints
                auto addQuad = [&](int a, int b, int c, int d) {
                    nextFaces.append({a, b, c, d});
                    nextSizes.append(4);
                };
                addQuad(i0, m01, center, m30);
                addQuad(i1, m12, center, m01);
                addQuad(i2, m23, center, m12);
                addQuad(i3, m30, center, m23);
            } else {
                // Triangle: split into 4 (insert edge midpoints)
                int i0 = fv[0], i1 = fv[1], i2 = fv[2];

                auto addMid = [&](int a, int b) -> int {
                    float mx = (outVerts[a * 3] + outVerts[b * 3]) * 0.5f;
                    float my = (outVerts[a * 3 + 1] + outVerts[b * 3 + 1]) * 0.5f;
                    float mz = (outVerts[a * 3 + 2] + outVerts[b * 3 + 2]) * 0.5f;
                    int idx = nextVerts.size() / 3;
                    nextVerts.append({mx, my, mz});
                    return idx;
                };

                int m01 = addMid(i0, i1);
                int m12 = addMid(i1, i2);
                int m20 = addMid(i2, i0);

                nextFaces.append({i0, m01, m20});
                nextFaces.append({i1, m12, m01});
                nextFaces.append({i2, m20, m12});
                nextFaces.append({m01, m12, m20});
                nextSizes.append(3);
                nextSizes.append(3);
                nextSizes.append(3);
                nextSizes.append(3);
            }

            vertOffset += nv;
        }

        outVerts = nextVerts;
        outFaces = nextFaces;
        outSizes = nextSizes;
    }

    result.vertices = outVerts;
    result.faces = outFaces;
    result.resultVertices = outVerts.size() / 3;
    result.resultFaces = outSizes.size();
    result.success = true;
    result.errorMessage = "Used fallback subdivision (OpenSubdiv not available)";

    {
        resultMesh.vertices.resize(outVerts.size() / 3);
        for (int i = 0; i < resultMesh.vertices.size(); ++i) {
            resultMesh.vertices[i].position = QVector3D(outVerts[i * 3],
                                                       outVerts[i * 3 + 1],
                                                       outVerts[i * 3 + 2]);
        }
        int fi = 0;
        for (int s = 0; s < outSizes.size(); ++s) {
            Face face;
            for (int k = 0; k < outSizes[s]; ++k)
                face.indices.append(outFaces[fi++]);
            resultMesh.faces.append(face);
        }
        resultMesh.computeNormals();
        resultMesh.computeBoundingBox();
        result.mesh = resultMesh;
    }

    qDebug() << "SubdivisionSurface: OpenSubdiv not available, used fallback subdivision";
#endif

    return result;
}

}
