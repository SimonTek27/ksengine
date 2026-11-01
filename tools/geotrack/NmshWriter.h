#pragma once
// Geotrack — scrittura mesh con finestra di distanza (NMS2).
//
// `writeTerrainNMSH` di ks::engine::terrain scrive il formato "NMSH", che non
// ha posto per la finestra LOD: il renderer la legge solo dal formato "NMS2"
// (magic, vCount, iCount, lodIn, lodOut, vertici, indici). Per il fondo a
// chunk serve proprio la finestra — un chunk lontano non deve costare un draw
// call — quindi qui si scrive NMS2 con lo stesso layout che legge
// `NativeRenderer::loadMeshFromFile` e che scrive `Kn5Baker`.
//
// Il vertex on-disk deve avere esattamente la dimensione di
// ks::sim::NativeVertex: TerrainVertex ha lo stesso ordine di campi (posizione,
// normale, uv, colore) e il test verifica la dimensione del file.

#include "engine/terrain/TerrainMesh.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace ks::geo {

inline constexpr std::uint32_t kNativeVertexBytes =
    sizeof(ks::engine::terrain::TerrainVertex);

/// Scrive `mesh` in NMS2 con la finestra [lodIn, lodOut] (metri, distanza dal
/// camera piu' vicino punto dell'AABB). Passare lodOut <= lodIn scrive una
/// finestra non valida, che il renderer ignora (degradazione: mesh sempre
/// disegnata).
inline bool writeMeshNms2(const std::string& path,
                          const ks::engine::terrain::TerrainMeshData& mesh, float lodIn,
                          float lodOut) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file.write("NMS2", 4);
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
    const std::uint32_t indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    file.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
    file.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));
    file.write(reinterpret_cast<const char*>(&lodIn), sizeof(lodIn));
    file.write(reinterpret_cast<const char*>(&lodOut), sizeof(lodOut));
    file.write(reinterpret_cast<const char*>(mesh.vertices.data()),
               static_cast<std::streamsize>(sizeof(ks::engine::terrain::TerrainVertex) *
                                            mesh.vertices.size()));
    file.write(reinterpret_cast<const char*>(mesh.indices.data()),
               static_cast<std::streamsize>(sizeof(std::uint32_t) * mesh.indices.size()));
    return file.good();
}

} // namespace ks::geo
