#pragma once
// Geotrack fase 1 — scenario: posti commissario e piazzola safety car.
//
// Le posizioni finiscono in data/data.json nella forma che TracksidePosts sa
// gia' leggere (x/y/z piatti, `corner` 1-based, `mesh` per l'asset); questo
// header produce solo geometria, nessun I/O.

#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"
#include "geotrack/PitLane.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace ks::geo {

/// Deve restare uguale a ks::sim::kMarshalMeshName (src/simulator/
/// TracksidePosts.h): il test confronta i due valori, quindi una divergenza
/// rompe la build dei test invece di passare inosservata.
inline constexpr const char* kMarshalMeshName = "marshall_A_HI_010";

struct MarshalPost {
    int corner = 0;          ///< 1-based, in ordine di percorrenza
    std::string side = "left";
    double s = 0.0;
    double x = 0.0;
    double z = 0.0;
    std::string mesh = kMarshalMeshName;
};

/// Quattro posti per curva come li descrive l'asset: due a sinistra e due a
/// destra, uno in ingresso e uno in uscita, fuori dalla carreggiata.
inline std::vector<MarshalPost> marshalPosts(const std::vector<Station>& st, double halfWidth,
                                             double minRadiusMetres, double mergeMetres) {
    std::vector<MarshalPost> out;
    if (st.size() < 2) return out;

    const double threshold = 1.0 / std::max(1.0, minRadiusMetres);
    std::vector<double> apexes;
    double lastS = -1e30;
    for (const Station& s : st) {
        if (s.curvature <= threshold) continue;
        if (apexes.empty() || s.s - lastS > mergeMetres) apexes.push_back(s.s);
        lastS = s.s;
    }

    const double lateral = halfWidth + 8.0; // fuori dai kerb, dentro la via di fuga
    for (std::size_t c = 0; c < apexes.size(); ++c) {
        for (const double side : {1.0, -1.0}) {
            for (const double ds : {-8.0, 8.0}) {
                const Station here = stationAt(st, apexes[c] + ds);
                MarshalPost post;
                post.corner = static_cast<int>(c) + 1;
                post.side = side > 0.0 ? "left" : "right";
                post.s = here.s;
                post.x = here.p.x + here.left.x * lateral * side;
                post.z = here.p.z + here.left.z * lateral * side;
                out.push_back(post);
            }
        }
    }
    return out;
}

struct SafetyCarBay {
    double s = 0.0;
    double lateral = 0.0;
    double x = 0.0;
    double z = 0.0;
    double radius = 12.0;
};

/// Piazzola fuori pista sul tratto piu' rettilineo vicino a `preferS`
/// (di norma l'uscita dei box); senza tratti rettilinei ripiega sulla prima
/// stazione.
inline SafetyCarBay safetyCarBay(const std::vector<Station>& st, double halfWidth,
                                double preferS = -1.0) {
    SafetyCarBay bay;
    if (st.empty()) return bay;
    const double target = preferS >= 0.0 ? preferS : 0.5 * st.back().s;
    const Station* best = nullptr;
    double bestScore = 1e30;
    for (const Station& s : st) {
        if (s.curvature > 1.0 / 300.0) continue;
        const double score = std::fabs(s.s - target);
        if (score < bestScore) {
            bestScore = score;
            best = &s;
        }
    }
    if (!best) best = &st.front();
    bay.s = best->s;
    bay.lateral = halfWidth + 12.0;
    bay.x = best->p.x + best->left.x * bay.lateral;
    bay.z = best->p.z + best->left.z * bay.lateral;
    return bay;
}

} // namespace ks::geo
