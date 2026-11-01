#pragma once
// Geotrack fase 1 — corsia box.
//
// La corsia non e' un tracciato separato: e' una finestra del tracciato
// spostata lateralmente, con un raccordo lineare a entrambi i capi cosi' la
// macchina esce dalla pista senza scalini geometrici. Le stazioni prodotte
// hanno gia' il punto spostato: si passano direttamente a ribbon() con
// offset 0.

#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ks::geo {

/// Interpola fra due stazioni (usata per tagliare esattamente sugli estremi).
inline Station lerpStation(const Station& a, const Station& b, double t) {
    Station s;
    s.p = Enu{a.p.x + (b.p.x - a.p.x) * t, a.p.z + (b.p.z - a.p.z) * t};
    double tx = a.tangent.x + (b.tangent.x - a.tangent.x) * t;
    double tz = a.tangent.z + (b.tangent.z - a.tangent.z) * t;
    const double len = std::hypot(tx, tz);
    if (len > 1e-9) {
        tx /= len;
        tz /= len;
    } else {
        tx = a.tangent.x;
        tz = a.tangent.z;
    }
    s.tangent = Enu{tx, tz};
    s.left = Enu{-tz, tx};
    s.s = a.s + (b.s - a.s) * t;
    s.curvature = a.curvature + (b.curvature - a.curvature) * t;
    return s;
}

/// Stazione all'ascissa `s`; fuori dai limiti restituisce il primo o l'ultimo.
inline Station stationAt(const std::vector<Station>& st, double s) {
    if (st.empty()) return Station{};
    if (s <= st.front().s) return st.front();
    if (s >= st.back().s) return st.back();
    for (std::size_t i = 0; i + 1 < st.size(); ++i) {
        if (s > st[i + 1].s) continue;
        const double span = st[i + 1].s - st[i].s;
        const double t = span > 1e-9 ? (s - st[i].s) / span : 0.0;
        return lerpStation(st[i], st[i + 1], t);
    }
    return st.back();
}

/// Stazioni fra `fromS` e `toS`, con gli estremi interpolati esatti.
inline std::vector<Station> clipStations(const std::vector<Station>& st, double fromS,
                                         double toS) {
    std::vector<Station> out;
    if (st.size() < 2 || toS <= fromS) return out;
    out.push_back(stationAt(st, fromS));
    for (const Station& s : st)
        if (s.s > fromS && s.s < toS) out.push_back(s);
    out.push_back(stationAt(st, toS));
    return out;
}

/// Corsia box fra `sStart` e `sEnd`, spostata di `offset` verso sinistra, con
/// raccordo lineare lungo `rampMetres` a ciascun capo. I punti tornano gia'
/// traslati: ribbon(stazioni, {0, halfWidth, y, uv}) e' sufficiente.
inline std::vector<Station> rampStations(const std::vector<Station>& st, double sStart,
                                         double sEnd, double offset, double rampMetres) {
    const double ramp = std::max(0.0, rampMetres);
    std::vector<Station> out = clipStations(st, sStart - ramp, sEnd + ramp);
    for (Station& s : out) {
        double k = 1.0;
        if (ramp > 1e-6) {
            if (s.s < sStart) k = (s.s - (sStart - ramp)) / ramp;
            else if (s.s > sEnd) k = 1.0 - (s.s - sEnd) / ramp;
        }
        k = std::clamp(k, 0.0, 1.0);
        s.p.x += s.left.x * offset * k;
        s.p.z += s.left.z * offset * k;
    }
    return out;
}

} // namespace ks::geo
