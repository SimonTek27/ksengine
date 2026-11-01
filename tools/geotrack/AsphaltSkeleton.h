#pragma once
// Geotrack fase 2 — correzione della centerline sullo skeleton dell'asfalto.
//
// I dati vettoriali danno la topologia ma non il centro della carreggiata: un
// nodo OSM sta dove l'autore l'ha messo, spesso sul bordo o a meta' fra due
// corsie. L'immagine da' il contrario. La catena qui sotto prende l'asfalto
// dall'immagine, lo assottiglia a uno scheletro di uno spessore di un pixel
// (Zhang-Suen) e sposta ogni stazione vettoriale sul punto di scheletro piu'
// vicino, entro un raggio di ricerca. Ordine e numero di stazioni non cambiano:
// si corregge la posizione, non la topologia.
//
// Perche' non si riusa thinMask() di tools/trackmodules: e' file-local dentro
// TrackModulesCore.cpp (file con modifiche locali non committate) e lavora su
// un raster di un'altra pipeline. Qui serve la stessa idea, trenta righe.

#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ks::geo {

/// Raster binario dell'asfalto sul quadrato (1 = asfalto).
struct MaskGrid {
    int width = 0;
    int height = 0;
    double metresPerPixel = 1.0;
    double minX = 0.0;
    double minZ = 0.0;
    std::vector<std::uint8_t> cell;

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
    std::uint8_t at(int x, int y) const {
        return inside(x, y) ? cell[static_cast<std::size_t>(y) * width + x] : 0;
    }
    /// Cella che contiene il punto mondo; false fuori dal raster.
    bool cellAt(double x, double z, int* outX, int* outY) const {
        if (metresPerPixel <= 0.0) return false;
        const int cx = static_cast<int>(std::floor((x - minX) / metresPerPixel));
        const int cy = static_cast<int>(std::floor((z - minZ) / metresPerPixel));
        if (!inside(cx, cy)) return false;
        if (outX) *outX = cx;
        if (outY) *outY = cy;
        return true;
    }
    double centreX(int x) const { return minX + (x + 0.5) * metresPerPixel; }
    double centreZ(int y) const { return minZ + (y + 0.5) * metresPerPixel; }
};

/// Rasterizza l'asfalto campionando il centro di ogni cella.
template <class Sampler>
inline MaskGrid rasterizeAsphalt(const Sampler& sampler, const Square& square, double metresPerPixel) {
    MaskGrid grid;
    grid.metresPerPixel = std::max(0.25, metresPerPixel);
    grid.minX = square.minX;
    grid.minZ = square.minZ;
    grid.width = std::max(2, static_cast<int>(std::ceil(square.side() / grid.metresPerPixel)));
    grid.height = grid.width;
    grid.cell.assign(static_cast<std::size_t>(grid.width) * grid.height, 0);
    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            const double wx = grid.centreX(x);
            const double wz = grid.centreZ(y);
            grid.cell[static_cast<std::size_t>(y) * grid.width + x] = sampler.isAsphalt(wx, wz) ? 1 : 0;
        }
    }
    return grid;
}

/// Assottigliamento di Zhang-Suen: riduce una fascia a uno scheletro di un
/// pixel, iterando finche' nessun pixel si toglie piu'.
inline int thinZhangSuen(MaskGrid& grid, int maxIterations = 64) {
    int iterations = 0;
    std::vector<std::uint8_t> mark(grid.cell.size(), 0);
    for (; iterations < maxIterations; ++iterations) {
        bool changed = false;
        for (int phase = 0; phase < 2; ++phase) {
            std::fill(mark.begin(), mark.end(), 0);
            for (int y = 1; y + 1 < grid.height; ++y) {
                for (int x = 1; x + 1 < grid.width; ++x) {
                    const std::size_t i = static_cast<std::size_t>(y) * grid.width + x;
                    if (!grid.cell[i]) continue;
                    const std::uint8_t p2 = grid.cell[i - grid.width];
                    const std::uint8_t p3 = grid.cell[i - grid.width + 1];
                    const std::uint8_t p4 = grid.cell[i + 1];
                    const std::uint8_t p5 = grid.cell[i + grid.width + 1];
                    const std::uint8_t p6 = grid.cell[i + grid.width];
                    const std::uint8_t p7 = grid.cell[i + grid.width - 1];
                    const std::uint8_t p8 = grid.cell[i - 1];
                    const std::uint8_t p9 = grid.cell[i - grid.width - 1];

                    const int neighbours = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                    if (neighbours < 2 || neighbours > 6) continue;
                    const int transitions = (!p2 && p3) + (!p3 && p4) + (!p4 && p5) + (!p5 && p6) +
                                            (!p6 && p7) + (!p7 && p8) + (!p8 && p9) + (!p9 && p2);
                    if (transitions != 1) continue;
                    if (phase == 0) {
                        if (p2 && p4 && p6) continue;
                        if (p4 && p6 && p8) continue;
                    } else {
                        if (p2 && p4 && p8) continue;
                        if (p2 && p6 && p8) continue;
                    }
                    mark[i] = 1;
                }
            }
            for (std::size_t i = 0; i < grid.cell.size(); ++i) {
                if (!mark[i]) continue;
                grid.cell[i] = 0;
                changed = true;
            }
        }
        if (!changed) break;
    }
    return iterations;
}

/// Sposta ogni punto sul pixel di scheletro piu' vicino entro `searchRadius`
/// metri, con media dei vicini per togliere il gradino del pixel, e poi
/// smussa la sequenza. I punti senza scheletro nel raggio restano dove sono.
inline std::vector<Enu> snapToSkeleton(const std::vector<Enu>& pts, const MaskGrid& skeleton,
                                       double searchRadius) {
    std::vector<Enu> out = pts;
    const double step = skeleton.metresPerPixel;
    const int reach = static_cast<int>(std::ceil(std::max(0.0, searchRadius) / step));
    for (std::size_t i = 0; i < pts.size(); ++i) {
        int cx = 0, cy = 0;
        if (!skeleton.cellAt(pts[i].x, pts[i].z, &cx, &cy)) continue;

        double bestDist2 = 1e30;
        int bestX = -1, bestY = -1;
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dx = -reach; dx <= reach; ++dx) {
                const int x = cx + dx, y = cy + dy;
                if (!skeleton.at(x, y)) continue;
                const double ddx = skeleton.centreX(x) - pts[i].x;
                const double ddz = skeleton.centreZ(y) - pts[i].z;
                const double dist2 = ddx * ddx + ddz * ddz;
                if (dist2 < bestDist2) {
                    bestDist2 = dist2;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        if (bestX < 0) continue;

        // Media delle celle di scheletro adiacenti al migliore: il pixel da'
        // un errore fino a mezzo pixel che qui si dimezza.
        double sumX = 0.0, sumZ = 0.0;
        int count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = bestX + dx, y = bestY + dy;
                if (!skeleton.at(x, y)) continue;
                sumX += skeleton.centreX(x);
                sumZ += skeleton.centreZ(y);
                ++count;
            }
        }
        if (count > 0) out[i] = Enu{sumX / count, sumZ / count};
    }

    // Smussamento a tre punti: lo scheletro e' a gradini, la centerline no.
    std::vector<Enu> smoothed = out;
    for (std::size_t i = 1; i + 1 < out.size(); ++i) {
        smoothed[i] = Enu{(out[i - 1].x + out[i].x + out[i + 1].x) / 3.0,
                          (out[i - 1].z + out[i].z + out[i + 1].z) / 3.0};
    }
    return smoothed;
}

/// Ricostruisce tangenti, sinistra, ascissa e curvatura sulle posizioni
/// corrette **senza** ricampionare: il numero di stazioni resta quello di
/// partenza, altrimenti la correzione cambierebbe anche la topologia (e la
/// lunghezza) del tracciato.
inline std::vector<Station> restationFromPoints(const std::vector<Station>& source,
                                               const std::vector<Enu>& pts, bool closed) {
    std::vector<Station> out = source;
    const std::size_t n = std::min(source.size(), pts.size());
    if (n < 2) return out;

    for (std::size_t i = 0; i < n; ++i) {
        out[i].p = pts[i];
        const Enu prev = pts[closed ? (i + n - 1) % n : (i == 0 ? 0 : i - 1)];
        const Enu next = pts[closed ? (i + 1) % n : (i + 1 < n ? i + 1 : n - 1)];
        double tx = next.x - prev.x;
        double tz = next.z - prev.z;
        const double len = std::hypot(tx, tz);
        if (len > 1e-9) {
            tx /= len;
            tz /= len;
        } else {
            tx = source[i].tangent.x;
            tz = source[i].tangent.z;
        }
        out[i].tangent = Enu{tx, tz};
        out[i].left = Enu{-tz, tx};
    }

    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        out[i].s = s;
        const Enu next = pts[closed ? (i + 1) % n : (i + 1 < n ? i + 1 : n - 1)];
        s += std::hypot(next.x - pts[i].x, next.z - pts[i].z);
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (!closed && (i == 0 || i + 1 >= n)) continue;
        const Enu a = pts[(i + n - 1) % n];
        const Enu b = pts[i];
        const Enu c = pts[(i + 1) % n];
        const double ab = std::hypot(b.x - a.x, b.z - a.z);
        const double bc = std::hypot(c.x - b.x, c.z - b.z);
        const double ca = std::hypot(a.x - c.x, a.z - c.z);
        const double area = 0.5 * std::fabs((b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z));
        if (ab < 1e-9 || bc < 1e-9 || ca < 1e-9 || area < 1e-12) continue;
        out[i].curvature = ab * bc * ca / (4.0 * area);
    }
    return out;
}

} // namespace ks::geo
