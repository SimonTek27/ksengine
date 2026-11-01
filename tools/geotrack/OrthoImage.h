#pragma once
// Geotrack fase 2 — misura dell'asfalto da un'immagine georeferenziata.
//
// L'immagine (ortofoto o mosaico di tile) copre il quadrato di terreno: la
// stessa georeferenziazione che finisce in scene.json, quindi nessun file di
// georeferenziazione separato e nessun provider cablato. Per ogni stazione si
// cammina perpendicolarmente alla centerline finche' i pixel restano asfalto:
// la somma delle due mezze larghezze e' la larghezza reale del tracciato, che
// sostituisce il valore stimato a mano.
//
// Perche' conta: sui dati vettoriali l'apron dei box finisce nella stessa
// classificazione del tracciato e la larghezza esce a 30-39 m invece di ~14.

#include "engine/material/PngCodec.h"
#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ks::geo {

/// Asfalto in un'immagine satellitare: grigio (canali vicini), ne' nero ne'
/// bianco da segnaletica. Erba, ghiaia e cordoli hanno saturazione alta o
/// luminanza fuori intervallo, quindi restano fuori.
inline bool isAsphaltPixel(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const int maxChannel = std::max({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
    const int minChannel = std::min({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
    const int brightness = (static_cast<int>(r) + 2 * static_cast<int>(g) + static_cast<int>(b)) / 4;
    return (maxChannel - minChannel) <= 22 && brightness >= 45 && brightness <= 190;
}

struct OrthoImage {
    ks::image::RawImage image;
    Square square;

    bool valid() const { return !image.isNull() && square.side() > 0.0; }
    double metresPerPixel() const {
        return image.width > 0 ? square.side() / static_cast<double>(image.width) : 0.0;
    }

    /// Carica l'immagine e la georeferenzia sul quadrato (riga 0 = nord).
    bool load(const std::string& path, const Square& sq, std::string* error) {
        std::string loadError;
        if (!ks::image::loadImageFile(path, &image, &loadError)) {
            if (error) *error = "ortho: " + (loadError.empty() ? path : loadError);
            return false;
        }
        if (image.isNull()) {
            if (error) *error = "ortho: empty image " + path;
            return false;
        }
        square = sq;
        return true;
    }

    /// Colore del pixel che contiene (x, z); false fuori dall'immagine.
    bool pixelAt(double x, double z, std::uint8_t rgb[3]) const {
        if (!valid()) return false;
        const double u = (x - square.minX) / square.side();
        const double v = (z - square.minZ) / square.side();
        if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) return false;
        const int px = static_cast<int>(u * (image.width - 1));
        const int py = static_cast<int>((1.0 - v) * (image.height - 1));
        const std::size_t index = (static_cast<std::size_t>(py) * image.width + px) * 4u;
        if (index + 2 >= image.rgba.size()) return false;
        rgb[0] = image.rgba[index];
        rgb[1] = image.rgba[index + 1];
        rgb[2] = image.rgba[index + 2];
        return true;
    }

    bool isAsphalt(double x, double z) const {
        std::uint8_t rgb[3] = {0, 0, 0};
        if (!pixelAt(x, z, rgb)) return false;
        return isAsphaltPixel(rgb[0], rgb[1], rgb[2]);
    }
};

struct WidthMeasurement {
    double medianM = 0.0;
    double minM = 0.0;
    double maxM = 0.0;
    int samples = 0;
};

/// Larghezza misurata stazione per stazione. Sul lato di ogni stazione si
/// cammina a passi di un pixel; due campioni consecutivi non-asfalto chiudono
/// il bordo (cosi' una striscia di vernice non tronca la carreggiata). Le
/// stazioni senza due bordi entro `maxHalfWidthM` vengono scartate.
/// Generico sull'origine dei campioni: vale per un'ortofoto singola
/// (OrthoImage) e per un mosaico di tile (TileMosaic), che espongono entrambi
/// metresPerPixel() e isAsphalt(x, z).
template <class Sampler>
inline WidthMeasurement measureTrackWidthImpl(const Sampler& sampler, const std::vector<Station>& st,
                                              double minWidthM, double maxHalfWidthM) {
    WidthMeasurement result;
    if (st.size() < 2) return result;

    const double step = std::max(0.25, sampler.metresPerPixel() * 0.5);
    std::vector<double> widths;
    widths.reserve(st.size());
    for (const Station& s : st) {
        double halves[2] = {0.0, 0.0};
        bool found[2] = {false, false};
        for (int side = 0; side < 2; ++side) {
            const double sign = side == 0 ? 1.0 : -1.0;
            int missRun = 0;
            for (double t = step; t <= maxHalfWidthM; t += step) {
                const double x = s.p.x + s.left.x * sign * t;
                const double z = s.p.z + s.left.z * sign * t;
                if (sampler.isAsphalt(x, z)) {
                    halves[side] = t;
                    missRun = 0;
                } else if (++missRun >= 2) {
                    break;
                }
            }
            found[side] = halves[side] > 0.0;
        }
        if (!found[0] || !found[1]) continue;
        const double width = halves[0] + halves[1];
        if (width < minWidthM) continue;
        widths.push_back(width);
    }
    if (widths.empty()) return result;

    std::sort(widths.begin(), widths.end());
    result.samples = static_cast<int>(widths.size());
    result.minM = widths.front();
    result.maxM = widths.back();
    const std::size_t mid = widths.size() / 2;
    result.medianM = widths.size() % 2 ? widths[mid] : 0.5 * (widths[mid - 1] + widths[mid]);
    return result;
}

inline WidthMeasurement measureTrackWidth(const OrthoImage& ortho, const std::vector<Station>& st,
                                          double minWidthM, double maxHalfWidthM) {
    if (!ortho.valid()) return WidthMeasurement{};
    return measureTrackWidthImpl(ortho, st, minWidthM, maxHalfWidthM);
}

} // namespace ks::geo
