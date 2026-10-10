#pragma once
#include "KsExport.h"

// Engine-level terrain mesh generation — deliberately Qt-free (plain
// std::vector in, plain std::vector out) so it can be called from both:
//   - kseditor (Qt-based): TrackTerrainEditor/TerrainEngine hold the
//     heightmap as QVector<float>; converting to std::vector<float> for one
//     call here is cheap, and the result converts back to
//     ks::VulkanRenderer::Vertex for createMesh() in the editor viewport.
//   - SimulatorApp (Qt-free): writeTerrainNMSH() below writes directly to
//     the same "NMSH" binary format ks::sim::NativeRenderer::loadMeshFromFile()
//     already reads (see src/simulator/NativeRenderer.h and
//     src/sdk/.../acFiles/KN5Baker.h, which write/read the identical format).
//
// Why this exists: TerrainEngine (heightmap brush editing: raise, lower,
// smooth, flatten, noise, erosion, hydraulic erosion, texture-paint layers)
// and TrackTerrainEditor (the QML-facing terrain tool) both already hold
// real heightmap data and genuine brush algorithms — but neither ever turns
// that grid into a triangle mesh with normals and UVs. TrackTerrainEditor::
// exportToOBJ() comes closest, but writes triangles with no normals/UVs to a
// text file, and reuses its single `scale` parameter for both the horizontal
// grid spacing and the height multiplier — the same call that makes a wider
// terrain also makes it proportionally taller, which is very likely not
// what a caller wants (that bug is *not* touched here since fixing it means
// changing the QML-facing export contract; the generator below has no such
// coupling: horizontal size and height are independent parameters).

#include <vector>
#include <string>
#include <cstdint>

namespace ks::engine::terrain {

// Same field layout as ks::sim::NativeVertex (src/simulator/NativeRenderer.h)
// and matches ks::VulkanRenderer::Vertex's field order (position, normal,
// uv, color), so converting to either is a straight field-by-field copy.
struct TerrainVertex {
    float px = 0, py = 0, pz = 0;
    float nx = 0, ny = 1, nz = 0;
    float u = 0, v = 0;
    float r = 1, g = 1, b = 1, a = 1;
};

struct TerrainMeshData {
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
};

// Generates a regular-grid triangle mesh from a heightmap.
//   heights: row-major, size must equal gridW * gridH, x fastest.
//   worldW/worldH: total plane extents in meters (independent of height
//     units — heights are used as-is, in meters, not rescaled by worldW/H).
//   uvScale: texture tiles repeat once every `uvScale` meters.
// Normals are computed per-vertex from the heightmap via central
// differences (falling back to a forward/backward difference at the grid
// edges), not carried over from any editor-side normal cache, so this
// function is self-contained and correct given heights alone.
KSENGINE_API TerrainMeshData generateTerrainMesh(const std::vector<float>& heights, int gridW, int gridH,
                                    float worldW, float worldH, float uvScale = 10.0f);

// Same as generateTerrainMesh, but samples every `stride` grid cells for a
// cheaper, lower-detail mesh (stride=1 is identical to generateTerrainMesh;
// stride=2 is quarter the triangle count, etc.). Useful for a simplified
// SimulatorApp draw distance without needing a full LOD/tessellation system.
KSENGINE_API TerrainMeshData generateTerrainMeshLOD(const std::vector<float>& heights, int gridW, int gridH,
                                       float worldW, float worldH, int stride, float uvScale = 10.0f);

// Writes mesh data directly to the "NMSH" binary format (magic "NMSH",
// uint32 vertexCount, uint32 indexCount, raw vertex array, raw uint32 index
// array) that ks::sim::NativeRenderer::loadMeshFromFile() reads. Returns
// false if the file could not be opened for writing.
KSENGINE_API bool writeTerrainNMSH(const std::string& path, const TerrainMeshData& mesh);

} // namespace ks::engine::terrain
