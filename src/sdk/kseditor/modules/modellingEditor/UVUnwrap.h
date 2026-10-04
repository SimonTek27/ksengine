#pragma once

#include "sdk/kseditor/engine/mesh/GeometryTypes.h"
#include <vector>
#include <utility>

namespace ks::geometry {

// UV unwrapping engine - canonical implementation
// Uses GeoMeshData from GeometryTypes.h

class UVUnwrapper {
public:
    enum UnwrapMethod { LSCM, Harmonic, Angle, ABFPlus, FAST };
    enum SeamDetectionMethod { Auto, Manual, BoundaryOnly, AngleBased, ConvexityBased };

    static UnwrapResult unwrapMesh(const GeoMeshData& mesh, UnwrapMethod method = LSCM);
    static UnwrapResult unwrapWithSeams(const GeoMeshData& mesh,
        const std::vector<std::pair<uint32_t, uint32_t>>& seamEdges, UnwrapMethod method = LSCM);

    static std::vector<std::pair<uint32_t, uint32_t>> detectSeams(
        const GeoMeshData& mesh, SeamDetectionMethod method = Auto, float angleThresholdDegrees = 60.0f);

    static std::vector<UVIsland> detectIslands(const GeoMeshData& mesh,
        const std::vector<std::pair<uint32_t, uint32_t>>& seamEdges);

    static float packAtlas(std::vector<UVCoord>& uvCoords, std::vector<UVIsland>& islands,
        uint32_t textureWidth = 1024, uint32_t textureHeight = 1024, uint32_t padding = 2);

    static std::vector<float> calculateStretch(const GeoMeshData& mesh, const std::vector<UVCoord>& uvCoords);
    static float optimizeStretch(const GeoMeshData& mesh, std::vector<UVCoord>& uvCoords,
        std::vector<UVIsland>& islands, uint32_t maxIterations = 10);

    static const char* methodName(UnwrapMethod method);

private:
    static UnwrapResult unwrapImpl(const GeoMeshData& mesh,
        const std::vector<std::pair<uint32_t, uint32_t>>& seamEdges,
        UnwrapMethod method);
    static std::vector<uint32_t> extractBoundaryLoop(const GeoMeshData& mesh,
        const std::vector<std::pair<uint32_t, uint32_t>>& seamEdges);
    static std::vector<std::pair<uint32_t, uint32_t>> extractSeamEdges(const GeoMeshData& mesh,
        const std::vector<std::pair<uint32_t, uint32_t>>& providedSeams);
};

} // namespace ks::geometry
