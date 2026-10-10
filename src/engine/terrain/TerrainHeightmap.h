#pragma once
#include "KsExport.h"

// Qt-free, deterministic heightmap synthesis for TerrainMesh: the simulator
// has no terrain asset to load (the only real heightmaps live inside the Qt
// TrackTerrainEditor), so KS_TERRAIN generates one from fractal noise.

#include <vector>

namespace ks::engine::terrain {

struct FbmHeightmapParams {
    int seed = 0;
    float baseHeight = 0.0f;
    float amplitude = 12.0f;
    float frequency = 3.0f;
    int octaves = 5;
    float lacunarity = 2.0f;
    float gain = 0.5f;
};

KSENGINE_API std::vector<float> generateFbmHeightmap(int gridW, int gridH, const FbmHeightmapParams& params);

} // namespace ks::engine::terrain
