#pragma once
// Geotrack fase 1 — sidecar materials.txt.
//
// Formato letto da ks::sim::MaterialCache (src/simulator/MaterialCache.h):
// una riga per mesh, celle separate da TAB,
//     mesh \t albedo \t roughness \t metalness [\t normal] [\t clearcoat]
// Le immagini si risolvono in <baked>/textures/. Una cella albedo "-" non e'
// numerica e non contiene un '.', quindi il runtime la ignora e lascia il
// materiale ai default: nessuna texture, nessun errore.

#include <string>
#include <vector>

namespace ks::geo {

struct LayerMaterial {
    std::string mesh;
    std::string albedo;
    std::string roughness = "0.9";
    std::string metalness = "0.0";
};

inline std::string materialsTxt(const std::vector<LayerMaterial>& rows) {
    std::string out;
    for (const LayerMaterial& row : rows) {
        out += row.mesh;
        out += '\t';
        out += row.albedo.empty() ? "-" : row.albedo;
        out += '\t';
        out += row.roughness;
        out += '\t';
        out += row.metalness;
        out += '\n';
    }
    return out;
}

} // namespace ks::geo
