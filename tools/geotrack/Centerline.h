#pragma once
// Geotrack fase 1 — lettura centerline, ricampionamento, stazioni.
//
// Tre formati in ingresso, nessuna libreria: CSV `lat,lon` (quello che esce da
// un foglio di calcolo), GeoJSON LineString (Overpass/OSM) e GPX (traccia GPS
// di un giro reale). Tutto quello che serve dopo e' una polilinea in metri
// locali, quindi i tre lettori convergono subito su LatLon.
//
// Header-only come GeoProjection.h: lo usano il tool e i test senza toccare le
// liste di sorgenti.

#include "geotrack/GeoProjection.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace ks::geo {

/// Una stazione sulla centerline: punto, verso di marcia, sinistra e
/// ascissa curvilinea. La sinistra e' (-tz, tx): verso nord -> ovest.
struct Station {
    Enu p{};
    Enu tangent{1.0, 0.0};
    Enu left{0.0, 1.0};
    double s = 0.0;
    double curvature = 0.0;
};

namespace detail {

/// Legge il numero a partire da `i`, senza superare `limit`. Serve nei
/// GeoJSON, dove un array chiuso da ']' deve fermare la scansione: senza il
/// limite il parser leggerebbe il numero della coppia successiva.
inline double parseNumberAt(const std::string& text, std::size_t& i, std::size_t limit) {
    const std::size_t end = std::min(limit, text.size());
    while (i < end && !(std::isdigit(static_cast<unsigned char>(text[i])) ||
                        text[i] == '-' || text[i] == '+' || text[i] == '.'))
        ++i;
    if (i >= end) return std::nan("");
    const char* begin = text.c_str() + i;
    char* parsed = nullptr;
    const double v = std::strtod(begin, &parsed);
    if (parsed == begin) return std::nan("");
    i += static_cast<std::size_t>(parsed - begin);
    return v;
}

inline bool isNumberStart(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.';
}

} // namespace detail

/// Legge una centerline. `error` viene riempito e il vettore resta vuoto su
/// qualunque fallimento (file assente, estensione sconosciuta, zero punti).
inline std::vector<LatLon> readCenterline(const std::string& path, std::string* error) {
    if (error) error->clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::string ext;
    const std::size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::vector<LatLon> out;

    if (ext == "csv" || ext == "txt") {
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            const std::string line = text.substr(pos, eol - pos);
            pos = eol + 1;
            const std::size_t hash = line.find('#');
            if (hash != std::string::npos) continue;
            std::size_t i = 0;
            const double lat = detail::parseNumberAt(line, i, line.size());
            const double lon = detail::parseNumberAt(line, i, line.size());
            if (std::isnan(lat) || std::isnan(lon)) continue;
            out.push_back({lat, lon});
        }
    } else if (ext == "geojson" || ext == "json") {
        // Primo array dopo "coordinates": coppie [lon, lat] (una terza
        // coordinata, la quota, viene saltata).
        const std::size_t key = text.find("\"coordinates\"");
        if (key == std::string::npos) {
            if (error) *error = "no \"coordinates\" in " + path;
            return {};
        }
        const std::size_t open = text.find('[', key);
        if (open == std::string::npos) {
            if (error) *error = "malformed coordinates in " + path;
            return {};
        }
        std::size_t i = open;
        while (i < text.size()) {
            const std::size_t close = text.find(']', i);
            if (close == std::string::npos) break;
            std::size_t j = i;
            double first = std::nan(""), second = std::nan("");
            while (j < close) {
                const double v = detail::parseNumberAt(text, j, close);
                if (std::isnan(v)) break;
                if (std::isnan(first)) first = v;
                else if (std::isnan(second)) second = v;
                while (j < close && !detail::isNumberStart(text[j])) ++j;
            }
            // GeoJSON: [longitudine, latitudine] — l'ordine e' invertito
            // rispetto al CSV e al GPX.
            const double lon = first;
            const double lat = second;
            if (!std::isnan(lon) && !std::isnan(lat)) out.push_back({lat, lon});
            i = close + 1;
            // Una ']' chiude tutto l'array delle coordinate: fine.
            std::size_t k = i;
            while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
            if (k < text.size() && text[k] != ',') break;
        }
        // I punti del LineString sono quelli dentro il primo '[' ... ']' e
        // non i numeri di bounding box eventualmente presenti prima.
        std::vector<LatLon> onlyLine;
        for (std::size_t p = 0; p < out.size(); ++p) {
            const double lat = out[p].lat;
            const double lon = out[p].lon;
            if (std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0) continue;
            onlyLine.push_back({lat, lon});
        }
        out.swap(onlyLine);
    } else if (ext == "gpx") {
        std::size_t pos = 0;
        while ((pos = text.find("<trkpt", pos)) != std::string::npos) {
            const std::size_t end = text.find('>', pos);
            if (end == std::string::npos) break;
            const std::string tag = text.substr(pos, end - pos);
            pos = end + 1;
            const std::size_t latKey = tag.find("lat=\"");
            const std::size_t lonKey = tag.find("lon=\"");
            if (latKey == std::string::npos || lonKey == std::string::npos) continue;
            const double lat = std::strtod(tag.c_str() + latKey + 5, nullptr);
            const double lon = std::strtod(tag.c_str() + lonKey + 5, nullptr);
            if (std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0) continue;
            out.push_back({lat, lon});
        }
    } else {
        if (error) *error = "unsupported centerline format: " + ext;
        return {};
    }

    if (out.size() < 2) {
        if (error) *error = "centerline needs at least 2 points: " + path;
        return {};
    }
    return out;
}

/// Lunghezza della polilinea in metri; `closed` aggiunge il segmento di
/// chiusura fra ultimo e primo punto.
inline double polylineLength(const std::vector<Enu>& pts, bool closed) {
    if (pts.size() < 2) return 0.0;
    double len = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i)
        len += std::hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z);
    if (closed) len += std::hypot(pts.front().x - pts.back().x, pts.front().z - pts.back().z);
    return len;
}

/// Riporta la polilinea a passo costante. Giro chiuso: `round(len/step)` punti
/// senza duplicare il primo alla fine. Giro aperto: passo costante con entrambi
/// gli estremi, l'ultimo intervallo puo' essere piu' corto.
inline std::vector<Enu> resample(const std::vector<Enu>& pts, double step, bool closed) {
    std::vector<Enu> out;
    if (pts.size() < 2 || step <= 0.0) return out;
    std::vector<Enu> path = pts;
    if (closed) path.push_back(pts.front());

    std::vector<double> cum(path.size(), 0.0);
    for (std::size_t i = 1; i < path.size(); ++i)
        cum[i] = cum[i - 1] + std::hypot(path[i].x - path[i - 1].x, path[i].z - path[i - 1].z);
    const double len = cum.back();
    if (!(len > 1e-6)) return out;

    const double count = len / step;
    const int n = closed ? std::max(2, static_cast<int>(std::lround(count)))
                         : std::max(2, static_cast<int>(std::lround(count)) + 1);
    out.resize(static_cast<std::size_t>(n));
    std::size_t seg = 0;
    for (int k = 0; k < n; ++k) {
        const double target = closed ? k * len / n : k * len / (n - 1);
        while (seg + 2 < path.size() && cum[seg + 1] < target) ++seg;
        const double segLen = cum[seg + 1] - cum[seg];
        const double f = segLen > 1e-9 ? (target - cum[seg]) / segLen : 0.0;
        out[static_cast<std::size_t>(k)] = Enu{
            path[seg].x + (path[seg + 1].x - path[seg].x) * f,
            path[seg].z + (path[seg + 1].z - path[seg].z) * f};
    }
    return out;
}

/// Costruisce le stazioni (tangente, sinistra, ascissa, curvatura) da una
/// centerline grezza. Restituisce vuoto per una linea degenere (tutti i punti
/// coincidenti, meno di 2 punti).
inline std::vector<Station> buildStations(const std::vector<Enu>& pts, double step, bool closed) {
    std::vector<Station> out;
    const std::vector<Enu> rs = resample(pts, step, closed);
    if (rs.size() < 2) return out;
    const std::size_t n = rs.size();

    std::vector<Station> st(n);
    for (std::size_t i = 0; i < n; ++i) st[i].p = rs[i];

    for (std::size_t i = 0; i < n; ++i) {
        const Enu prev = closed ? rs[(i + n - 1) % n] : rs[i == 0 ? 0 : i - 1];
        const Enu next = closed ? rs[(i + 1) % n] : rs[i + 1 < n ? i + 1 : n - 1];
        double tx = next.x - prev.x;
        double tz = next.z - prev.z;
        const double tlen = std::hypot(tx, tz);
        if (tlen > 1e-9) {
            tx /= tlen;
            tz /= tlen;
        } else {
            tx = 1.0;
            tz = 0.0;
        }
        st[i].tangent = Enu{tx, tz};
        st[i].left = Enu{-tz, tx};
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (i == 0) {
            st[i].s = closed
                          ? 0.0
                          : 0.0;
        } else {
            st[i].s = st[i - 1].s + std::hypot(rs[i].x - rs[i - 1].x, rs[i].z - rs[i - 1].z);
        }
    }

    // Curvatura = 1/raggio del cerchio per tre punti consecutivi (0 in retta).
    for (std::size_t i = 0; i < n; ++i) {
        if (!closed && (i == 0 || i + 1 >= n)) continue;
        const Enu a = rs[(i + n - 1) % n];
        const Enu b = rs[i];
        const Enu c = rs[(i + 1) % n];
        const double ab = std::hypot(b.x - a.x, b.z - a.z);
        const double bc = std::hypot(c.x - b.x, c.z - b.z);
        const double ca = std::hypot(a.x - c.x, a.z - c.z);
        const double area = 0.5 * std::fabs((b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z));
        if (ab < 1e-9 || bc < 1e-9 || ca < 1e-9 || area < 1e-12) continue;
        st[i].curvature = ab * bc * ca / (4.0 * area);
    }
    return st;
}

/// Numero di curve: sequenze di stazioni con curvatura sopra soglia, fuse
/// quando distano meno di `mergeMetres` l'una dall'altra.
inline int countCorners(const std::vector<Station>& st, double minRadiusMetres,
                        double mergeMetres) {
    if (st.empty()) return 0;
    const double threshold = 1.0 / std::max(1.0, minRadiusMetres);
    int corners = 0;
    double lastS = -1e30;
    for (const Station& s : st) {
        if (s.curvature <= threshold) continue;
        if (corners == 0 || s.s - lastS > mergeMetres) ++corners;
        lastS = s.s;
    }
    return corners;
}

} // namespace ks::geo
