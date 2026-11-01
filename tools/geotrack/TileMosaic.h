#pragma once
// Geotrack fase 2 — mosaico di tile slippy-map.
//
// La cache prodotta da fetch_cache.py contiene <zoom>_<x>_<y>.png e un
// cache.json con zoom ed elenco. Qui le tile sono riportate sullo stesso piano
// ENU del tracciato (Mercator -> lat/lon -> proiezione locale), quindi valgono
// per due usi:
//   - campionamento dell'asfalto per la misura della larghezza;
//   - fondo del terreno a chunk: un ritaglio per chunk, una mesh per chunk,
//     una riga di materials.txt per chunk. Cosi' il fondo arriva a schermo con
//     il renderer attuale (un albedo per mesh) senza aspettare un piano LOD nel
//     motore, e il numero di chunk e' il primo livello di LOD.
//
// Nessuna rete: le tile devono essere gia' in cache.

#include "engine/FileFormat/Json.h"
#include "engine/material/PngCodec.h"
#include "geotrack/GeoProjection.h"
#include "geotrack/OrthoImage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ks::geo {

/// Angolo nord-ovest di una tile slippy-map in lat/lon.
inline LatLon tileNorthWest(int zoom, int x, int y) {
    const double n = std::pow(2.0, static_cast<double>(zoom));
    LatLon p;
    p.lon = static_cast<double>(x) / n * 360.0 - 180.0;
    const double latRad = std::atan(std::sinh(kPi * (1.0 - 2.0 * static_cast<double>(y) / n)));
    p.lat = latRad * 180.0 / kPi;
    return p;
}

struct Tile {
    ks::image::RawImage image;
    double minX = 0.0, maxX = 0.0, minZ = 0.0, maxZ = 0.0;

    double metresPerPixel() const {
        return image.width > 0 ? (maxX - minX) / static_cast<double>(image.width) : 0.0;
    }
};

struct TileMosaic {
    std::vector<Tile> tiles;
    int zoom = 0;

    bool valid() const { return !tiles.empty(); }

    double metresPerPixel() const {
        double total = 0.0;
        for (const Tile& t : tiles) total += t.metresPerPixel();
        return tiles.empty() ? 0.0 : total / static_cast<double>(tiles.size());
    }

    /// Legge `<cacheDir>/cache.json`, carica le tile elencate e le georeferenzia
    /// col piano ENU del tracciato.
    bool loadCache(const std::string& cacheJsonPath, const Projection& projection,
                   std::string* error) {
        std::ifstream in(cacheJsonPath);
        if (!in) {
            if (error) *error = "tiles: cannot open " + cacheJsonPath;
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        bool ok = false;
        std::string parseError;
        const ks::Json doc = ks::Json::parse(text, &ok, &parseError);
        if (!ok) {
            if (error) *error = "tiles: bad cache.json: " + parseError;
            return false;
        }
        zoom = doc.value("zoom").toInt(0);
        if (zoom <= 0) {
            if (error) *error = "tiles: cache.json has no zoom";
            return false;
        }
        const std::string cacheDir = std::filesystem::path(cacheJsonPath).parent_path().string();
        const ks::Json list = doc.value("tiles");
        for (int i = 0; i < list.size(); ++i) {
            const std::string name = list.at(i).toString();
            int z = 0, tx = 0, ty = 0;
            if (std::sscanf(name.c_str(), "%d_%d_%d", &z, &tx, &ty) != 3) continue;
            if (z != zoom) continue;

            Tile tile;
            const std::string path = cacheDir + "/tiles/" + name;
            std::string loadError;
            if (!ks::image::loadImageFile(path, &tile.image, &loadError)) {
                if (error) *error = "tiles: " + path + " unreadable";
                return false;
            }
            const LatLon nw = tileNorthWest(zoom, tx, ty);
            const LatLon se = tileNorthWest(zoom, tx + 1, ty + 1);
            const Enu a = projection.toEnu(nw);
            const Enu b = projection.toEnu(se);
            tile.minX = std::min(a.x, b.x);
            tile.maxX = std::max(a.x, b.x);
            tile.minZ = std::min(a.z, b.z);
            tile.maxZ = std::max(a.z, b.z);
            tiles.push_back(tile);
        }
        if (tiles.empty()) {
            if (error) *error = "tiles: no usable tile in " + cacheJsonPath;
            return false;
        }
        return true;
    }

    bool pixelAt(double x, double z, std::uint8_t rgb[3]) const {
        for (const Tile& t : tiles) {
            if (x < t.minX || x > t.maxX || z < t.minZ || z > t.maxZ) continue;
            const double u = (x - t.minX) / std::max(1e-9, t.maxX - t.minX);
            const double v = (z - t.minZ) / std::max(1e-9, t.maxZ - t.minZ);
            const int px = static_cast<int>(u * (t.image.width - 1));
            const int py = static_cast<int>((1.0 - v) * (t.image.height - 1));
            const std::size_t index =
                (static_cast<std::size_t>(py) * static_cast<std::size_t>(t.image.width) +
                 static_cast<std::size_t>(px)) * 4u;
            if (index + 2 >= t.image.rgba.size()) return false;
            rgb[0] = t.image.rgba[index];
            rgb[1] = t.image.rgba[index + 1];
            rgb[2] = t.image.rgba[index + 2];
            return true;
        }
        return false;
    }

    bool isAsphalt(double x, double z) const {
        std::uint8_t rgb[3] = {0, 0, 0};
        if (!pixelAt(x, z, rgb)) return false;
        return isAsphaltPixel(rgb[0], rgb[1], rgb[2]);
    }

    /// Ritaglio `pixels` x `pixels` del quadrato `chunk`, campionato al centro
    /// pixel. Fuori dal mosaico resta trasparente.
    ks::image::RawImage crop(const Square& chunk, int pixels) const {
        ks::image::RawImage out;
        out.resize(std::max(1, pixels), std::max(1, pixels));
        for (int y = 0; y < out.height; ++y) {
            for (int x = 0; x < out.width; ++x) {
                const double wx =
                    chunk.minX + (x + 0.5) / out.width * (chunk.maxX - chunk.minX);
                const double wz =
                    chunk.maxZ - (y + 0.5) / out.height * (chunk.maxZ - chunk.minZ);
                std::uint8_t rgb[3] = {0, 0, 0};
                const std::size_t i =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) +
                     static_cast<std::size_t>(x)) * 4u;
                if (pixelAt(wx, wz, rgb)) {
                    out.rgba[i] = rgb[0];
                    out.rgba[i + 1] = rgb[1];
                    out.rgba[i + 2] = rgb[2];
                    out.rgba[i + 3] = 255;
                } else {
                    out.rgba[i + 3] = 0;
                }
            }
        }
        return out;
    }
};

} // namespace ks::geo
