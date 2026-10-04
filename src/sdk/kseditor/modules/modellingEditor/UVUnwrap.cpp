#include "UVUnwrap.h"
#include "sdk/kseditor/engine/mesh/UVUnwrap.h"
#include <chrono>
#include <cmath>
#include <algorithm>

namespace ks::geometry {

using UV = UVCoord;

static QVector<QVector3D> meshDataToQVector3D(const GeoMeshData& mesh) {
    QVector<QVector3D> result;
    result.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        result.append(QVector3D(
            static_cast<float>(v.x),
            static_cast<float>(v.y),
            static_cast<float>(v.z)
        ));
    }
    return result;
}

static QVector<QVector<int>> meshDataToQVectorInt(const GeoMeshData& mesh) {
    QVector<QVector<int>> result;
    result.reserve(mesh.faces.size());
    for (const auto& f : mesh.faces) {
        QVector<int> tri;
        tri.reserve(3);
        tri.append(static_cast<int>(f.v0));
        tri.append(static_cast<int>(f.v1));
        tri.append(static_cast<int>(f.v2));
        result.append(tri);
    }
    return result;
}

static QSet<QPair<int, int>> seamEdgesToQSet(
    const std::vector<std::pair<uint32_t, uint32_t>>& seamEdges)
{
    QSet<QPair<int, int>> result;
    for (const auto& e : seamEdges) {
        result.insert(std::make_pair(
            static_cast<int>(e.first),
            static_cast<int>(e.second)
        ));
    }
    return result;
}

static UV convertQVector2DToUV(const QVector2D& v) {
    return UV(v.x(), v.y());
}

static std::vector<UV> qVectorToStdVector(const QVector<QVector2D>& qv) {
    std::vector<UV> result;
    result.reserve(qv.size());
    for (const auto& v : qv) {
        result.push_back(convertQVector2DToUV(v));
    }
    return result;
}

static ks::geometry::UVIsland convertIsland(const ::ks::UVIsland& src, int id) {
    ks::geometry::UVIsland dst;
    dst.id = id;
    dst.faceIndices.reserve(src.faceIndices.size());
    for (int fi : src.faceIndices) {
        dst.faceIndices.push_back(static_cast<uint32_t>(fi));
    }
    dst.area = src.area;
    dst.isOnBoundary = false;
    dst.stretchRatio = 1.0f;
    return dst;
}

std::vector<std::pair<uint32_t, uint32_t>> UVUnwrapper::detectSeams(
    const GeoMeshData& mesh,
    SeamDetectionMethod method,
    float angleThresholdDegrees)
{
    auto vertices = meshDataToQVector3D(mesh);
    auto faces = meshDataToQVectorInt(mesh);
    float angleRad = angleThresholdDegrees * M_PI / 180.0f;

    QVector<QPair<int, int>> seams;

    switch (method) {
    case BoundaryOnly:
        break;

    case AngleBased:
        seams = ::ks::UVIslandDetector::findSeamsFromAngle(vertices, faces, angleRad);
        break;

    case ConvexityBased:
        seams = ::ks::UVIslandDetector::findSeamsFromAngle(vertices, faces, angleRad * 0.5f);
        break;

    case Auto:
    default:
        seams = ::ks::UVIslandDetector::findSeamsFromAngle(vertices, faces, qDegreesToRadians(60.0f));
        break;
    }

    std::vector<std::pair<uint32_t, uint32_t>> result;
    result.reserve(seams.size());
    for (const auto& s : seams) {
        result.push_back({
            static_cast<uint32_t>(s.first),
            static_cast<uint32_t>(s.second)
        });
    }
    return result;
}

std::vector<UVIsland> UVUnwrapper::detectIslands(
    const GeoMeshData& mesh,
    const std::vector<std::pair<uint32_t, uint32_t>>& seamEdges)
{
    auto vertices = meshDataToQVector3D(mesh);
    auto faces = meshDataToQVectorInt(mesh);
    auto seams = seamEdgesToQSet(seamEdges);

    auto coreIslands = ::ks::UVIslandDetector::findIslands(vertices, faces, seams);

    std::vector<UVIsland> result;
    result.reserve(coreIslands.size());
    for (int i = 0; i < coreIslands.size(); ++i) {
        result.push_back(convertIsland(coreIslands[i], i));
    }
    return result;
}

UnwrapResult UVUnwrapper::unwrapMesh(const GeoMeshData& mesh, UnwrapMethod method) {
    auto seams = detectSeams(mesh, Auto, 60.0f);
    return unwrapWithSeams(mesh, seams, method);
}

UnwrapResult UVUnwrapper::unwrapWithSeams(
    const GeoMeshData& mesh,
    const std::vector<std::pair<uint32_t, uint32_t>>& seamEdges,
    UnwrapMethod method)
{
    UnwrapResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    if (!mesh.isValid()) {
        result.status = UnwrapResult::InvalidMesh;
        result.errorMessage = "Mesh is empty or invalid";
        return result;
    }

    auto vertices = meshDataToQVector3D(mesh);
    auto faces = meshDataToQVectorInt(mesh);
    auto seamSet = seamEdgesToQSet(seamEdges);

    QVector<QVector2D> uvs;

    bool success = false;

    switch (method) {
    case LSCM:
    case Angle:
        success = ::ks::LSCMUnwrapper::unwrap(vertices, faces, seamSet, uvs);
        break;

    case Harmonic:
    case ABFPlus: {
        ::ks::UVUnwrapConfig config;
        success = ::ks::ConformalUnwrapper::unwrap(vertices, faces, uvs, config);
        break;
    }

    case FAST: {
        uvs = ::ks::UVMapper::smartProject(vertices, faces);
        success = !uvs.isEmpty();
        break;
    }

    default:
        success = ::ks::LSCMUnwrapper::unwrap(vertices, faces, seamSet, uvs);
        break;
    }

    if (!success || uvs.isEmpty()) {
        uvs = ::ks::UVMapper::planarProject(vertices, faces, QVector3D(0, 0, 1));
        success = !uvs.isEmpty();
    }

    if (!success || uvs.isEmpty()) {
        result.status = UnwrapResult::UnwrapFailed;
        result.errorMessage = "All unwrapping methods failed";
        return result;
    }

    result.uvCoordinates = qVectorToStdVector(uvs);

    auto coreIslands = ::ks::UVIslandDetector::findIslands(vertices, faces, seamSet);
    result.islands.reserve(coreIslands.size());
    for (int i = 0; i < coreIslands.size(); ++i) {
        result.islands.push_back(convertIsland(coreIslands[i], i));
    }

    float packingEff = packAtlas(result.uvCoordinates, result.islands, 1024, 1024, 2);
    result.packingEfficiency = packingEff;

    auto stretchValues = calculateStretch(mesh, result.uvCoordinates);
    result.maxStretchRatio = 1.0f;
    for (float s : stretchValues) {
        if (s > result.maxStretchRatio) result.maxStretchRatio = s;
    }

    result.status = UnwrapResult::Success;

    auto endTime = std::chrono::high_resolution_clock::now();
    result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    return result;
}

float UVUnwrapper::packAtlas(
    std::vector<UV>& uvCoords,
    std::vector<UVIsland>& islands,
    uint32_t textureWidth,
    uint32_t textureHeight,
    uint32_t padding)
{
    if (uvCoords.empty() || islands.empty()) return 0.0f;

    float texelWidth = (textureWidth > 0) ? 1.0f / textureWidth : 0.001f;
    float texelHeight = (textureHeight > 0) ? 1.0f / textureHeight : 0.001f;

    float uMin = 1e9f, uMax = -1e9f, vMin = 1e9f, vMax = -1e9f;
    for (const auto& uv : uvCoords) {
        if (uv.u < uMin) uMin = uv.u;
        if (uv.u > uMax) uMax = uv.u;
        if (uv.v < vMin) vMin = uv.v;
        if (uv.v > vMax) vMax = uv.v;
    }
    float rangeU = (uMax > uMin) ? uMax - uMin : 1.0f;
    float rangeV = (vMax > vMin) ? vMax - vMin : 1.0f;
    float scale = 1.0f / qMax(rangeU, rangeV);

    for (auto& uv : uvCoords) {
        uv.u = (uv.u - uMin) * scale * 0.9f + 0.05f;
        uv.v = (uv.v - vMin) * scale * 0.9f + 0.05f;
    }

    for (size_t i = 0; i < islands.size(); ++i) {
        islands[i].id = static_cast<uint32_t>(i);
    }

    float usedArea = 0.0f;
    float totalArea = 1.0f;
    for (const auto& uv : uvCoords) {
        if (uv.u >= 0.0f && uv.u <= 1.0f && uv.v >= 0.0f && uv.v <= 1.0f) {
            usedArea += texelWidth * texelHeight;
        }
    }

    return qMin(1.0f, usedArea / totalArea);
}

std::vector<float> UVUnwrapper::calculateStretch(
    const GeoMeshData& mesh,
    const std::vector<UV>& uvCoords)
{
    std::vector<float> stretchRatios;
    if (mesh.faces.empty() || uvCoords.size() < mesh.vertices.size()) {
        return stretchRatios;
    }

    stretchRatios.reserve(mesh.faces.size());

    for (const auto& face : mesh.faces) {
        const auto& v0 = mesh.vertices[face.v0];
        const auto& v1 = mesh.vertices[face.v1];
        const auto& v2 = mesh.vertices[face.v2];

        double e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
        double e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;

        double area3D = 0.5 * std::sqrt(
            std::pow(e1y * e2z - e1z * e2y, 2) +
            std::pow(e1z * e2x - e1x * e2z, 2) +
            std::pow(e1x * e2y - e1y * e2x, 2)
        );

        float du1 = uvCoords[face.v1].u - uvCoords[face.v0].u;
        float dv1 = uvCoords[face.v1].v - uvCoords[face.v0].v;
        float du2 = uvCoords[face.v2].u - uvCoords[face.v0].u;
        float dv2 = uvCoords[face.v2].v - uvCoords[face.v0].v;

        float areaUV = 0.5f * std::abs(du1 * dv2 - du2 * dv1);

        if (area3D > 1e-10 && areaUV > 1e-10) {
            stretchRatios.push_back(static_cast<float>(areaUV / area3D));
        } else {
            stretchRatios.push_back(1.0f);
        }
    }

    return stretchRatios;
}

float UVUnwrapper::optimizeStretch(
    const GeoMeshData& mesh,
    std::vector<UV>& uvCoords,
    std::vector<UVIsland>& islands,
    uint32_t maxIterations)
{
    if (uvCoords.empty() || mesh.faces.empty()) return 1.0f;

    float initialStretch = 0.0f;
    auto initialRatios = calculateStretch(mesh, uvCoords);
    for (float s : initialRatios) initialStretch += s;
    initialStretch = initialRatios.empty() ? 1.0f : initialStretch / initialRatios.size();

    auto vertices = meshDataToQVector3D(mesh);
    auto faces = meshDataToQVectorInt(mesh);

    QVector<QVector2D> qUvs;
    qUvs.reserve(uvCoords.size());
    for (const auto& uv : uvCoords) {
        qUvs.append(QVector2D(uv.u, uv.v));
    }

    ::ks::MinStretchUnwrapper::minimizeStretch(vertices, faces, qUvs,
        static_cast<int>(maxIterations));

    for (int i = 0; i < qUvs.size() && i < (int)uvCoords.size(); ++i) {
        uvCoords[i].u = qUvs[i].x();
        uvCoords[i].v = qUvs[i].y();
    }

    float finalStretch = 0.0f;
    auto finalRatios = calculateStretch(mesh, uvCoords);
    for (float s : finalRatios) finalStretch += s;
    finalStretch = finalRatios.empty() ? 1.0f : finalStretch / finalRatios.size();

    return (initialStretch > 0.0f) ? finalStretch / initialStretch : 1.0f;
}

UnwrapResult UVUnwrapper::unwrapImpl(
    const GeoMeshData& mesh,
    const std::vector<std::pair<uint32_t, uint32_t>>& seamEdges,
    UnwrapMethod method)
{
    return unwrapWithSeams(mesh, seamEdges, method);
}

std::vector<uint32_t> UVUnwrapper::extractBoundaryLoop(
    const GeoMeshData& mesh,
    const std::vector<std::pair<uint32_t, uint32_t>>& seamEdges)
{
    std::set<uint32_t> boundaryVerts;
    std::map<uint32_t, int> edgeCount;

    for (const auto& face : mesh.faces) {
        uint32_t tri[3] = { face.v0, face.v1, face.v2 };
        for (int i = 0; i < 3; ++i) {
            uint32_t a = tri[i];
            uint32_t b = tri[(i + 1) % 3];
            auto key = std::make_pair(
                std::min(a, b),
                std::max(a, b)
            );

            bool isSeam = false;
            for (const auto& s : seamEdges) {
                if ((s.first == key.first && s.second == key.second) ||
                    (s.first == key.second && s.second == key.first)) {
                    isSeam = true;
                    break;
                }
            }

            if (!isSeam) {
                edgeCount[key.first]++;
                edgeCount[key.second]++;
            }
        }
    }

    for (const auto& ec : edgeCount) {
        if (ec.second < 2) {
            boundaryVerts.insert(ec.first);
        }
    }

    std::vector<uint32_t> result(boundaryVerts.begin(), boundaryVerts.end());
    return result;
}

std::vector<std::pair<uint32_t, uint32_t>> UVUnwrapper::extractSeamEdges(
    const GeoMeshData& mesh,
    const std::vector<std::pair<uint32_t, uint32_t>>& providedSeams)
{
    if (!providedSeams.empty()) return providedSeams;
    return detectSeams(mesh, Auto, 60.0f);
}

const char* UVUnwrapper::methodName(UnwrapMethod method) {
    switch (method) {
        case LSCM:   return "LSCM";
        case Harmonic: return "Harmonic";
        case Angle:  return "Angle";
        case ABFPlus: return "ABF++";
        case FAST:   return "Fast";
        default:     return "Unknown";
    }
}

} // namespace ks::geometry
