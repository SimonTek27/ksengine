#include "ModifierSystem.h"
#include "MeshOperations.h"
#include "SubdivisionSurface.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace ks {

Modifier::Modifier(const QString& n, ModifierType t) : name(n), type(t) {}

int Modifier::executionSpace() const {
    switch (type) {
        case ModifierType::Generate: return 1;
        case ModifierType::Deform: return 2;
        case ModifierType::Physics: return 3;
        case ModifierType::Remesh: return 4;
        default: return 0;
    }
}

int Modifier::executionMask() const {
    int mask = 0;
    switch (type) {
        case ModifierType::Generate:
            mask = (int)ModifierMode::Editmode | (int)ModifierMode::Render;
            break;
        case ModifierType::Deform:
            mask = (int)ModifierMode::Realtime | (int)ModifierMode::Render | (int)ModifierMode::Editmode;
            break;
        case ModifierType::Physics:
            mask = (int)ModifierMode::Realtime;
            break;
        case ModifierType::Remesh:
            mask = (int)ModifierMode::Render | (int)ModifierMode::Editmode;
            break;
        default:
            mask = (int)ModifierMode::Realtime | (int)ModifierMode::Render | (int)ModifierMode::Editmode;
            break;
    }
    return mask;
}

GenerateModifier::GenerateModifier(const QString& name) : Modifier(name, ModifierType::Generate) {}
DeformModifier::DeformModifier(const QString& name) : Modifier(name, ModifierType::Deform) {}

MirrorModifier::MirrorModifier() : GenerateModifier("Mirror") {}

MeshData MirrorModifier::apply(const MeshData& input) {
    MeshData result = input;

    QVector3D scale = {1, 1, 1};
    if (mirrorAxes.testFlag(Axis::X)) scale.setX(-1);
    if (mirrorAxes.testFlag(Axis::Y)) scale.setY(-1);
    if (mirrorAxes.testFlag(Axis::Z)) scale.setZ(-1);

    QMatrix4x4 mirrorMatrix;
    mirrorMatrix.scale(scale);

    int baseVertCount = result.vertices.size();
    int baseFaceCount = result.faces.size();

    for (int i = 0; i < baseVertCount; ++i) {
        Vertex v = result.vertices[i];
        v.position = mirrorMatrix.map(v.position);
        v.normal = mirrorMatrix.mapVector(v.normal).normalized();
        result.vertices.append(v);
    }

    for (int i = 0; i < baseFaceCount; ++i) {
        Face f = result.faces[i];
        Face mirrored;
        for (int j = f.indices.size() - 1; j >= 0; --j) {
            mirrored.indices.append(f.indices[j] + baseVertCount);
        }
        result.faces.append(mirrored);
    }

    if (mergeVertices) {
        QVector<int> mergeMap(result.vertices.size(), -1);
        QVector<bool> merged(result.vertices.size(), false);

        for (int i = 0; i < baseVertCount; ++i) {
            if (merged[i]) continue;
            for (int j = baseVertCount; j < result.vertices.size(); ++j) {
                if (merged[j]) continue;
                float dist = (result.vertices[i].position - result.vertices[j].position).length();
                if (dist < tolerance) {
                    mergeMap[j] = i;
                    merged[j] = true;
                    result.vertices[i].normal = (result.vertices[i].normal + result.vertices[j].normal).normalized();
                }
            }
        }

        for (auto& face : result.faces) {
            for (int& idx : face.indices) {
                if (mergeMap[idx] >= 0) idx = mergeMap[idx];
            }
        }

        QVector<int> oldToNew(result.vertices.size());
        QVector<Vertex> newVertices;
        for (int i = 0; i < result.vertices.size(); ++i) {
            if (!merged[i]) {
                oldToNew[i] = newVertices.size();
                newVertices.append(result.vertices[i]);
            } else {
                oldToNew[i] = mergeMap[i];
            }
        }
        result.vertices = newVertices;

        for (auto& face : result.faces) {
            for (int& idx : face.indices) {
                idx = oldToNew[idx];
            }
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

QMap<QString, QVariant> MirrorModifier::writeParameters() const {
    QMap<QString, QVariant> params;
    params["mirrorAxes"] = (int)mirrorAxes;
    params["mergeThreshold"] = tolerance;
    return params;
}

void MirrorModifier::readParameters(const QMap<QString, QVariant>& params) {
    if (params.contains("mirrorAxes")) mirrorAxes = (QFlags<Axis>)params["mirrorAxes"].toInt();
    if (params.contains("mergeThreshold")) tolerance = params["mergeThreshold"].toFloat();
}

ArrayModifier::ArrayModifier() : GenerateModifier("Array") {}

MeshData ArrayModifier::apply(const MeshData& input) {
    MeshData result = input;

    for (int i = 1; i < count; ++i) {
        int baseIdx = result.vertices.size();

        for (const auto& v : input.vertices) {
            Vertex nv = v;
            QVector3D offset;
            if (useConstantOffset) offset += constantOffset;
            if (useRelativeOffset) offset += relativeOffset * float(i);
            nv.position += offset;
            result.vertices.append(nv);
        }

        for (const auto& f : input.faces) {
            Face nf;
            for (int idx : f.indices) {
                nf.indices.append(baseIdx + idx);
            }
            nf.materialId = f.materialId;
            result.faces.append(nf);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

BevelModifier::BevelModifier() : DeformModifier("Bevel") {}

MeshData BevelModifier::apply(const MeshData& input) {
    return MeshOperations::bevelEdges(input, width, segments, angleLimit);
}

SolidifyModifier::SolidifyModifier() : GenerateModifier("Solidify") {}

MeshData SolidifyModifier::apply(const MeshData& input) {
    MeshData result = input;

    for (const auto& v : input.vertices) {
        Vertex nv = v;
        nv.position += v.normal * thickness * offset;
        result.vertices.append(nv);
    }

    for (const auto& f : input.faces) {
        Face backFace;
        for (int i = f.indices.size() - 1; i >= 0; --i) {
            backFace.indices.append(result.vertices.size() - f.indices.size() + i);
        }
        backFace.materialId = f.materialId;
        result.faces.append(backFace);
    }

    int origVerts = input.vertices.size();
    int origFaces = input.faces.size();

    for (int fi = 0; fi < origFaces; ++fi) {
        const Face& f = input.faces[fi];
        for (int i = 0; i < f.indices.size(); ++i) {
            int v1 = f.indices[i];
            int v2 = f.indices[(i + 1) % f.indices.size()];
            result.faces.append({v1 + origVerts, v2 + origVerts, v2, v1});
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

SubdivisionModifier::SubdivisionModifier() : GenerateModifier("Subdivision") {}

MeshData SubdivisionModifier::apply(const MeshData& input) {
    if (creases.isEmpty() && pinnedVertices.isEmpty())
        return MeshOperations::subdivide(input, levels);

    // Merge explicit creases with pins (pinned vertices get fully sharp
    // edges so they stay in place through subdivision, like TurboSmooth).
    QVector<CreaseEdge> allCreases = creases;
    if (!pinnedVertices.isEmpty()) {
        QSet<QPair<int, int>> seen;
        for (const auto& c : creases)
            seen.insert(qMakePair(qMin(c.vertexA, c.vertexB), qMax(c.vertexA, c.vertexB)));
        MeshData tmp = input;
        MeshOperations::ensureEdgeList(tmp);
        QSet<int> pinSet;
        for (int p : pinnedVertices)
            pinSet.insert(p);
        for (const Edge& e : tmp.edges) {
            if (pinSet.contains(e.v1) || pinSet.contains(e.v2)) {
                QPair<int, int> key = qMakePair(qMin(e.v1, e.v2), qMax(e.v1, e.v2));
                if (seen.contains(key))
                    continue;
                seen.insert(key);
                allCreases.append(CreaseEdge{e.v1, e.v2, 1.0f});
            }
        }
    }

    SubdivisionResult res = SubdivisionSurface::subdivideWithCreases(
        input, allCreases, levels);
    if (res.success && !res.mesh.vertices.isEmpty())
        return res.mesh;
    return MeshOperations::subdivide(input, levels);
}

DecimateModifier::DecimateModifier() : GenerateModifier("Decimate") {}

MeshData DecimateModifier::apply(const MeshData& input) {
    MeshData result = input;

    if (ratio >= 1.0f || result.faces.size() < 2) return result;

    int targetFaces = qMax(1, int(result.faces.size() * ratio));
    int startFaces = result.faces.size();

    // Build edge-collapse priority queue (shortest edges first)
    struct EdgeCollapse {
        int v1, v2;
        float cost;
        bool operator<(const EdgeCollapse& o) const { return cost < o.cost; }
    };

    auto buildEdgeSet = [&]() -> QVector<EdgeCollapse> {
        QMap<QPair<int,int>, float> edgeMap;
        for (const auto& face : result.faces) {
            for (int i = 0; i < face.indices.size(); ++i) {
                int a = face.indices[i];
                int b = face.indices[(i + 1) % face.indices.size()];
                if (a < 0 || b < 0 || a >= result.vertices.size() || b >= result.vertices.size())
                    continue;
                QPair<int,int> key = qMin(a,b) < qMax(a,b) ? std::make_pair(qMin(a,b), qMax(a,b)) : std::make_pair(qMax(a,b), qMin(a,b));
                float len = (result.vertices[a].position - result.vertices[b].position).length();
                if (!edgeMap.contains(key) || len < edgeMap[key])
                    edgeMap[key] = len;
            }
        }
        QVector<EdgeCollapse> edges;
        for (auto it = edgeMap.begin(); it != edgeMap.end(); ++it)
            edges.append({it.key().first, it.key().second, it.value()});
        std::sort(edges.begin(), edges.end());
        return edges;
    };

    QSet<int> deletedVerts;
    QSet<int> deletedFaces;
    int remainingFaces = result.faces.size();

    while (remainingFaces > targetFaces) {
        auto edges = buildEdgeSet();
        if (edges.isEmpty()) break;

        bool collapsed = false;
        for (const auto& edge : edges) {
            if (remainingFaces <= targetFaces) break;
            if (deletedVerts.contains(edge.v1) || deletedVerts.contains(edge.v2)) continue;

            // Collapse v2 into v1: replace all references to v2 with v1
            for (auto& face : result.faces) {
                for (int& idx : face.indices) {
                    if (idx == edge.v2) idx = edge.v1;
                }
            }

            deletedVerts.insert(edge.v2);

            // Remove degenerate faces (those with duplicate indices or fewer than 3 unique)
            for (int fi = 0; fi < result.faces.size(); ++fi) {
                if (deletedFaces.contains(fi)) continue;
                QSet<int> unique(result.faces[fi].indices.begin(), result.faces[fi].indices.end());
                if (unique.size() < 3) {
                    deletedFaces.insert(fi);
                    remainingFaces--;
                }
            }

            collapsed = true;
            break;
        }

        if (!collapsed) break;
    }

    // Build result mesh excluding deleted vertices and faces
    QVector<int> vertMap(result.vertices.size(), -1);
    MeshData output;
    for (int i = 0; i < result.vertices.size(); ++i) {
        if (!deletedVerts.contains(i)) {
            vertMap[i] = output.vertices.size();
            output.vertices.append(result.vertices[i]);
        }
    }
    for (int fi = 0; fi < result.faces.size(); ++fi) {
        if (deletedFaces.contains(fi)) continue;
        Face newFace;
        for (int idx : result.faces[fi].indices) {
            int mapped = (idx >= 0 && idx < vertMap.size()) ? vertMap[idx] : -1;
            if (mapped >= 0) newFace.indices.append(mapped);
        }
        if (newFace.indices.size() >= 3) {
            QSet<int> uniq(newFace.indices.begin(), newFace.indices.end());
            if (uniq.size() >= 3) output.faces.append(newFace);
        }
    }

    output.computeNormals();
    output.computeBoundingBox();
    return output;
}

DisplaceModifier::DisplaceModifier() : DeformModifier("Displace") {}

MeshData DisplaceModifier::apply(const MeshData& input) {
    MeshData result = input;

    for (auto& v : result.vertices) {
        float disp = 0.0f;
        if (textureCoordinates == TextureCoordinates::UV) {
            disp = v.uv.x() + v.uv.y();
        } else {
            disp = v.position.length() * 0.1f;
        }
        disp = (disp - midlevel) * strength;
        v.position += v.normal * disp;
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

SmoothModifier::SmoothModifier() : DeformModifier("Smooth") {}

MeshData SmoothModifier::apply(const MeshData& input) {
    MeshData result = input;

    for (int iter = 0; iter < iterations; ++iter) {
        QVector<QVector3D> newPositions;

        for (int i = 0; i < result.vertices.size(); ++i) {
            const QVector3D& pos = result.vertices[i].position;

            QVector3D sumPos(0, 0, 0);
            int count = 0;

            for (const auto& face : result.faces) {
                for (int j = 0; j < face.indices.size(); ++j) {
                    if (face.indices[j] == i) {
                        int prev = face.indices[(j - 1 + face.indices.size()) % face.indices.size()];
                        int next = face.indices[(j + 1) % face.indices.size()];
                        sumPos += result.vertices[prev].position;
                        sumPos += result.vertices[next].position;
                        count += 2;
                    }
                }
            }

            if (count > 0) {
                QVector3D avg = sumPos / count;
                newPositions.append(pos + (avg - pos) * factor);
            } else {
                newPositions.append(pos);
            }
        }

        for (int i = 0; i < result.vertices.size() && i < newPositions.size(); ++i) {
            result.vertices[i].position = newPositions[i];
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

CastModifier::CastModifier() : DeformModifier("Cast") {}

MeshData CastModifier::apply(const MeshData& input) {
    MeshData result = input;

    for (auto& v : result.vertices) {
        QVector3D pos = v.position;
        QVector3D projected = pos;

        float t = qBound(0.0f, factor, 1.0f);

        if (castType == CastType::Sphere) {
            float len = pos.length();
            if (len > radius) {
                projected = pos.normalized() * (radius + (len - radius) * (1.0f - t));
            }
        } else {
            if (qAbs(pos.x()) > radius) {
                projected.setX(radius + (pos.x() - radius) * (1.0f - t));
            }
        }

        v.position = projected;
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

QMap<QString, QVariant> CastModifier::writeParameters() const {
    QMap<QString, QVariant> p;
    p["radius"] = radius;
    p["factor"] = factor;
    p["fromRadius"] = fromRadius;
    p["toRadius"] = toRadius;
    p["castType"] = (int)castType;
    p["useX"] = useX;
    p["useY"] = useY;
    p["useZ"] = useZ;
    return p;
}

void CastModifier::readParameters(const QMap<QString, QVariant>& params) {
    if (params.contains("radius")) radius = params["radius"].toFloat();
    if (params.contains("factor")) factor = params["factor"].toFloat();
    if (params.contains("fromRadius")) fromRadius = params["fromRadius"].toFloat();
    if (params.contains("toRadius")) toRadius = params["toRadius"].toFloat();
    if (params.contains("castType")) castType = (CastType)params["castType"].toInt();
    if (params.contains("useX")) useX = params["useX"].toBool();
    if (params.contains("useY")) useY = params["useY"].toBool();
    if (params.contains("useZ")) useZ = params["useZ"].toBool();
}

LatticeModifier::LatticeModifier() : DeformModifier("Lattice") {}

MeshData LatticeModifier::apply(const MeshData& input) {
    MeshData result = input;

    if (result.vertices.isEmpty()) return result;

    result.computeBoundingBox();
    QVector3D size = result.boundingBoxMax - result.boundingBoxMin;
    if (size.x() < 1e-6f || size.y() < 1e-6f || size.z() < 1e-6f) return result;

    for (auto& v : result.vertices) {
        QVector3D local = (v.position - result.boundingBoxMin) / size;
        local.setX(local.x() * (1.0f + strength * (local.y() - 0.5f)));
        local.setY(local.y() * (1.0f + strength * (local.z() - 0.5f)));
        local.setZ(local.z() * (1.0f + strength * (local.x() - 0.5f)));
        v.position = result.boundingBoxMin + local * size;
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

WaveModifier::WaveModifier() : DeformModifier("Wave"), m_elapsed(0.0f) {}

MeshData WaveModifier::apply(const MeshData& input) {
    MeshData result = input;
    m_elapsed += 0.016f;

    for (auto& v : result.vertices) {
        float height = v.position.y();
        float offset = amplitude * sinf(height * frequency + m_elapsed * speed);
        v.position.setX(v.position.x() + offset);
        v.position.setZ(v.position.z() + offset * 0.5f);
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

ShrinkwrapModifier::ShrinkwrapModifier() : DeformModifier("Shrinkwrap") {}

MeshData ShrinkwrapModifier::apply(const MeshData& input) {
    MeshData result = input;
    if (result.vertices.isEmpty()) return result;

    if (!targetMeshData.vertices.isEmpty()) {
        QVector3D wrapDir = direction;
        if (wrapDir.isNull()) wrapDir = {0, 0, 1};
        MeshData wrapped = MeshOperations::shrinkwrap(result, targetMeshData, wrapDir);
        for (int i = 0; i < result.vertices.size(); ++i)
            result.vertices[i].position = wrapped.vertices[i].position;
    }

    if (offset != 0.0f) {
        result.computeBoundingBox();
        QVector3D center = (result.boundingBoxMin + result.boundingBoxMax) * 0.5f;
        for (auto& v : result.vertices) {
            QVector3D dir = v.position - center;
            float len = dir.length();
            if (len > 1e-6f)
                v.position += dir.normalized() * offset;
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

SkinModifier::SkinModifier() : GenerateModifier("Skin") {}

MeshData SkinModifier::apply(const MeshData& input) {
    MeshData result = input;

    if (result.vertices.size() < 2) return result;

    float skinThickness = 0.01f;

    QVector<Vertex> newVertices;
    QVector<Face> newFaces;

    for (const auto& v : result.vertices) {
        Vertex nv1 = v;
        nv1.position += v.normal * skinThickness;
        newVertices.append(nv1);

        Vertex nv2 = v;
        nv2.position -= v.normal * skinThickness;
        newVertices.append(nv2);
    }

    for (const auto& face : result.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            int curr = face.indices[i] * 2;
            int next = face.indices[(i + 1) % face.indices.size()] * 2;

            newFaces.append({curr, next, next + 1, curr + 1});
        }
    }

    result.vertices = newVertices;
    result.faces = newFaces;
    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

TriangulateModifier::TriangulateModifier() : GenerateModifier("Triangulate") {}

MeshData TriangulateModifier::apply(const MeshData& input) {
    return MeshOperations::triangulate(input);
}

QMap<QString, QVariant> TriangulateModifier::writeParameters() const {
    QMap<QString, QVariant> p;
    p["minVertices"] = minVertices;
    p["useBeauty"] = useBeauty;
    p["useNgonEnabled"] = useNgonEnabled;
    p["quadMethod"] = (int)quadMethod;
    p["triangleMethod"] = (int)triangleMethod;
    return p;
}

void TriangulateModifier::readParameters(const QMap<QString, QVariant>& params) {
    if (params.contains("minVertices")) minVertices = params["minVertices"].toInt();
    if (params.contains("useBeauty")) useBeauty = params["useBeauty"].toBool();
    if (params.contains("useNgonEnabled")) useNgonEnabled = params["useNgonEnabled"].toBool();
    if (params.contains("quadMethod")) quadMethod = (QuadMethod)params["quadMethod"].toInt();
    if (params.contains("triangleMethod")) triangleMethod = (TriangleMethod)params["triangleMethod"].toInt();
}

WireframeModifier::WireframeModifier() : GenerateModifier("Wireframe") {}

MeshData WireframeModifier::apply(const MeshData& input) {
    MeshData result;

    for (const auto& v : input.vertices) {
        Vertex nv;
        nv.position = v.position + v.normal * thickness;
        result.vertices.append(nv);
    }

    for (const auto& f : input.faces) {
        for (int i = 0; i < f.indices.size(); ++i) {
            int curr = f.indices[i];
            int next = f.indices[(i + 1) % f.indices.size()];

            result.faces.append({curr, next});
        }
    }

    result.computeBoundingBox();
    return result;
}

RemeshModifier::RemeshModifier() : GenerateModifier("Remesh") {}

MeshData RemeshModifier::apply(const MeshData& input) {
    if (input.vertices.size() < 4 || input.faces.size() < 2) return input;

    float targetEdgeLength = 1.0f / (float)(1 << octreeDepth);
    if (targetEdgeLength < 1e-6f) return input;

    // Step 1: Triangulate all faces
    MeshData result = MeshOperations::triangulate(input);
    if (result.faces.size() < 2) return result;

    // Step 2: Uniformly subdivide long edges
    const int maxIterations = qBound(1, iterations, 8);
    float threshold = targetEdgeLength * 1.5f;

    for (int iter = 0; iter < maxIterations; ++iter) {
        // Find all edges longer than threshold
        struct EdgeSplit {
            int v1, v2;
            int f1, f2;
            QVector3D midpoint;
        };
        QVector<EdgeSplit> splits;
        QMap<QPair<int,int>, int> edgeCount;

        for (int fi = 0; fi < result.faces.size(); ++fi) {
            const auto& face = result.faces[fi];
            for (int i = 0; i < face.indices.size(); ++i) {
                int a = face.indices[i];
                int b = face.indices[(i + 1) % face.indices.size()];
                if (a >= result.vertices.size() || b >= result.vertices.size()) continue;
                float len = (result.vertices[a].position - result.vertices[b].position).length();
                if (len > threshold) {
                    QPair<int,int> key = std::make_pair(qMin(a,b), qMax(a,b));
                    auto& count = edgeCount[key];
                    if (count == 0) {
                        EdgeSplit es;
                        es.v1 = a; es.v2 = b;
                        es.f1 = fi; es.f2 = -1;
                        es.midpoint = (result.vertices[a].position + result.vertices[b].position) * 0.5f;
                        splits.append(es);
                    }
                    count++;
                }
            }
        }

        if (splits.isEmpty()) break;

        for (const auto& split : splits) {
            int midIdx = result.vertices.size();
            Vertex mid;
            mid.position = split.midpoint;
            result.vertices.append(mid);

            // Split all faces containing this edge
            for (int fi = 0; fi < result.faces.size(); ++fi) {
                auto& face = result.faces[fi];
                int ea = -1, eb = -1;
                for (int i = 0; i < face.indices.size(); ++i) {
                    int ai = face.indices[i];
                    int bi = face.indices[(i + 1) % face.indices.size()];
                    if ((ai == split.v1 && bi == split.v2) || (ai == split.v2 && bi == split.v1)) {
                        ea = ai; eb = bi;
                        break;
                    }
                }
                if (ea < 0) continue;

                // Insert mid vertex into this face
                QVector<int> newIndices;
                for (int i = 0; i < face.indices.size(); ++i) {
                    newIndices.append(face.indices[i]);
                    int next = face.indices[(i + 1) % face.indices.size()];
                    if ((face.indices[i] == split.v1 && next == split.v2) ||
                        (face.indices[i] == split.v2 && next == split.v1)) {
                        newIndices.append(midIdx);
                    }
                }
                face.indices = newIndices;
            }
        }
    }

    // Step 3: Optional iterative smoothing
    for (int s = 0; s < qMin(iterations, 4); ++s) {
        QVector<QVector3D> newPos(result.vertices.size());
        QVector<int> neighborCount(result.vertices.size(), 0);
        for (const auto& face : result.faces) {
            for (int i = 0; i < face.indices.size(); ++i) {
                int a = face.indices[i];
                int b = face.indices[(i + 1) % face.indices.size()];
                if (a >= 0 && a < result.vertices.size() && b >= 0 && b < result.vertices.size()) {
                    newPos[a] += result.vertices[b].position;
                    neighborCount[a]++;
                }
            }
        }
        for (int i = 0; i < result.vertices.size(); ++i) {
            if (neighborCount[i] > 0)
                result.vertices[i].position = newPos[i] / (float)neighborCount[i];
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

QMap<QString, QVariant> ArrayModifier::writeParameters() const {
    QMap<QString, QVariant> p;
    p["count"] = count;
    p["length"] = length;
    p["constantOffset"] = constantOffset;
    p["relativeOffset"] = relativeOffset;
    p["useConstantOffset"] = useConstantOffset;
    p["useRelativeOffset"] = useRelativeOffset;
    return p;
}

void ArrayModifier::readParameters(const QMap<QString, QVariant>& params) {
    if (params.contains("count")) count = params["count"].toInt();
    if (params.contains("length")) length = params["length"].toFloat();
    if (params.contains("constantOffset")) constantOffset = params["constantOffset"].value<QVector3D>();
    if (params.contains("relativeOffset")) relativeOffset = params["relativeOffset"].value<QVector3D>();
    if (params.contains("useConstantOffset")) useConstantOffset = params["useConstantOffset"].toBool();
    if (params.contains("useRelativeOffset")) useRelativeOffset = params["useRelativeOffset"].toBool();
}

QMap<QString, QVariant> BevelModifier::writeParameters() const {
    QMap<QString, QVariant> p;
    p["width"] = width;
    p["segments"] = segments;
    p["angleLimit"] = angleLimit;
    p["profileShape"] = profileShape;
    return p;
}

void BevelModifier::readParameters(const QMap<QString, QVariant>& params) {
    if (params.contains("width")) width = params["width"].toFloat();
    if (params.contains("segments")) segments = params["segments"].toInt();
    if (params.contains("angleLimit")) angleLimit = params["angleLimit"].toFloat();
    if (params.contains("profileShape")) profileShape = params["profileShape"].toFloat();
}

QMap<QString, QVariant> SolidifyModifier::writeParameters() const {
    QMap<QString, QVariant> p;
    p["thickness"] = thickness;
    p["offset"] = offset;
    p["useFlipNormals"] = useFlipNormals;
    p["mode"] = (int)mode;
    return p;
}

void SolidifyModifier::readParameters(const QMap<QString, QVariant>& params) {
    if (params.contains("thickness")) thickness = params["thickness"].toFloat();
    if (params.contains("offset")) offset = params["offset"].toFloat();
    if (params.contains("useFlipNormals")) useFlipNormals = params["useFlipNormals"].toBool();
    if (params.contains("mode")) mode = (SolidifyMode)params["mode"].toInt();
}

QMap<QString, QVariant> SubdivisionModifier::writeParameters() const {
    QMap<QString, QVariant> p;
    p["levels"] = levels;
    p["renderLevels"] = renderLevels;
    p["subdivisionType"] = (int)subdivisionType;
    QVariantList creaseList;
    for (const auto& c : creases)
        creaseList.append(QVariant(QVariantList{c.vertexA, c.vertexB, c.sharpness}));
    p["creases"] = creaseList;
    QVariantList pinList;
    for (int v : pinnedVertices)
        pinList.append(v);
    p["pinnedVertices"] = pinList;
    return p;
}

void SubdivisionModifier::readParameters(const QMap<QString, QVariant>& params) {
    if (params.contains("levels")) levels = params["levels"].toInt();
    if (params.contains("renderLevels")) renderLevels = params["renderLevels"].toInt();
    if (params.contains("subdivisionType")) subdivisionType = (SubdivisionType)params["subdivisionType"].toInt();
    if (params.contains("creases")) {
        creases.clear();
        const QVariantList list = params["creases"].toList();
        for (const QVariant& item : list) {
            const QVariantList c = item.toList();
            if (c.size() >= 3)
                creases.append(CreaseEdge{c[0].toInt(), c[1].toInt(), c[2].toFloat()});
        }
    }
    if (params.contains("pinnedVertices")) {
        pinnedVertices.clear();
        const QVariantList list = params["pinnedVertices"].toList();
        for (const QVariant& v : list)
            pinnedVertices.append(v.toInt());
    }
}

QMap<QString, QVariant> DecimateModifier::writeParameters() const {
    QMap<QString, QVariant> p;
    p["ratio"] = ratio;
    p["vertexCount"] = vertexCount;
    p["decimateType"] = (int)decimateType;
    p["angleLimit"] = angleLimit;
    return p;
}

void DecimateModifier::readParameters(const QMap<QString, QVariant>& params) {
    if (params.contains("ratio")) ratio = params["ratio"].toFloat();
    if (params.contains("vertexCount")) vertexCount = params["vertexCount"].toInt();
    if (params.contains("decimateType")) decimateType = (DecimateType)params["decimateType"].toInt();
    if (params.contains("angleLimit")) angleLimit = params["angleLimit"].toFloat();
}

QMap<QString, QVariant> DisplaceModifier::writeParameters() const {
    QMap<QString, QVariant> p;
    p["strength"] = strength;
    p["midlevel"] = midlevel;
    p["textureCoordinates"] = (int)textureCoordinates;
    p["textureName"] = textureName;
    return p;
}

void DisplaceModifier::readParameters(const QMap<QString, QVariant>& params) {
    if (params.contains("strength")) strength = params["strength"].toFloat();
    if (params.contains("midlevel")) midlevel = params["midlevel"].toFloat();
    if (params.contains("textureCoordinates")) textureCoordinates = (TextureCoordinates)params["textureCoordinates"].toInt();
    if (params.contains("textureName")) textureName = params["textureName"].toString();
}

QMap<QString, QVariant> SmoothModifier::writeParameters() const {
    QMap<QString, QVariant> p;
    p["factor"] = factor;
    p["iterations"] = iterations;
    p["smoothMode"] = (int)smoothMode;
    return p;
}

void SmoothModifier::readParameters(const QMap<QString, QVariant>& params) {
    if (params.contains("factor")) factor = params["factor"].toFloat();
    if (params.contains("iterations")) iterations = params["iterations"].toInt();
    if (params.contains("smoothMode")) smoothMode = (SmoothMode)params["smoothMode"].toInt();
}

} // namespace ks