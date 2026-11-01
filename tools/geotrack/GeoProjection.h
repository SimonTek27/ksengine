#pragma once
// Geotrack fase 1 — proiezione geografica locale.
//
// Equirettangolare attorno a un'origine: a pochi chilometri dall'origine
// l'errore di scala e' ben sotto lo 0,1% e non serve una libreria di
// geodesia (ne' PROJ, ne' una griglia UTM) per posare un tracciato su un
// quadrato di terreno. Il sistema e' destrorso visto dall'alto: +X est,
// +Z nord, Y usato solo dai layer (le altezze).
//
// Header-only perche' come le altre utility Qt-free della pipeline (LodWindow,
// SceneAssets) viene inclusa sia dal tool sia dai test senza toccare le
// liste di sorgenti di CMake.

#include <cmath>
#include <vector>

namespace ks::geo {

// Raggio equatoriale WGS84: m/deg di latitudine = R * pi / 180 = 111319,49.
constexpr double kEarthRadius = 6378137.0;
constexpr double kPi = 3.14159265358979323846;

struct LatLon {
    double lat = 0.0;
    double lon = 0.0;
};

/// Posizione in metri nel piano locale: +X est, +Z nord.
struct Enu {
    double x = 0.0;
    double z = 0.0;
};

inline double metresPerDegLat() { return kEarthRadius * kPi / 180.0; }

/// Riporta un delta di longitudine in [-180, 180] — l'antimeridiano non deve
/// diventare un salto di 40000 km.
inline double wrapDegrees(double deg) {
    while (deg > 180.0) deg -= 360.0;
    while (deg < -180.0) deg += 360.0;
    return deg;
}

class Projection {
public:
    static Projection fromOrigin(LatLon origin) {
        Projection p;
        p.m_origin = origin;
        p.m_cosLat = std::cos(origin.lat * kPi / 180.0);
        // Un polo esatto annullerebbe la scala in longitudine: si degrada a 1.
        if (std::fabs(p.m_cosLat) < 1e-6) p.m_cosLat = 1e-6;
        return p;
    }

    LatLon origin() const { return m_origin; }

    Enu toEnu(LatLon p) const {
        const double mLon = metresPerDegLat() * m_cosLat;
        return Enu{wrapDegrees(p.lon - m_origin.lon) * mLon,
                   (p.lat - m_origin.lat) * metresPerDegLat()};
    }

    LatLon toLatLon(Enu p) const {
        LatLon out;
        out.lat = m_origin.lat + p.z / metresPerDegLat();
        out.lon = wrapDegrees(m_origin.lon + p.x / (metresPerDegLat() * m_cosLat));
        return out;
    }

private:
    LatLon m_origin{};
    double m_cosLat = 1.0;
};

/// Quadrato di terreno in metri: il layer pista sta al centro.
struct Square {
    double minX = 0.0;
    double maxX = 0.0;
    double minZ = 0.0;
    double maxZ = 0.0;

    double side() const { return maxX - minX; }
    double centreX() const { return 0.5 * (minX + maxX); }
    double centreZ() const { return 0.5 * (minZ + maxZ); }
};

/// Quadrato che contiene tutti i punti con `padMeters` di margine per lato.
/// Il lato e' quello dell'estensione maggiore, cosi' il terreno resta quadrato
/// anche su un tracciato molto allungato.
inline Square squareAround(const std::vector<Enu>& pts, double padMeters) {
    Square sq;
    if (pts.empty()) return sq;
    double minX = pts.front().x, maxX = pts.front().x;
    double minZ = pts.front().z, maxZ = pts.front().z;
    for (const Enu& p : pts) {
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.z < minZ) minZ = p.z;
        if (p.z > maxZ) maxZ = p.z;
    }
    const double cx = 0.5 * (minX + maxX);
    const double cz = 0.5 * (minZ + maxZ);
    const double half = 0.5 * std::fmax(maxX - minX, maxZ - minZ) + padMeters;
    sq.minX = cx - half;
    sq.maxX = cx + half;
    sq.minZ = cz - half;
    sq.maxZ = cz + half;
    return sq;
}

/// UV del quadrato: l'angolo nord-ovest e' (0,1), il sud-est (1,0). Cosi' una
/// immagine geografica si allinea senza rotazioni ne' ribaltamenti.
inline void uvOn(const Square& sq, double x, double z, float* u, float* v) {
    const double side = sq.side() > 0.0 ? sq.side() : 1.0;
    if (u) *u = static_cast<float>((x - sq.minX) / side);
    if (v) *v = static_cast<float>((z - sq.minZ) / side);
}

} // namespace ks::geo
