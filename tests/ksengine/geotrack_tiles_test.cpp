// Geotrack fase 2 — mosaico di tile: misura e fondo a chunk.
//
// Le tile sono sintetiche (slippy-map, PNG) ma la georeferenziazione e' quella
// vera: lat/lon di ciascuna tile -> piano ENU -> pixel. La pista e' disegnata
// sulla centerline del tracciato generato, quindi misura e generazione vedono
// la stessa immagine.
#include "KsTest.h"

#include "adapters/assetto_corsa/TrackDataJsonLoader.h"
#include "engine/material/PngCodec.h"
#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"
#include "geotrack/TileMosaic.h"
#include "geotrack/TrackSceneCore.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ks::geo;

static int tileIndexX(double lon, int zoom) {
    return static_cast<int>((lon + 180.0) / 360.0 * (1 << zoom));
}

static int tileIndexY(double lat, int zoom) {
    const double rad = std::max(-85.0, std::min(85.0, lat)) * kPi / 180.0;
    return static_cast<int>((1.0 - std::asinh(std::tan(rad)) / kPi) / 2.0 * (1 << zoom));
}

/// Ellisse in pixel: le tile Mercator non hanno la stessa scala sui due assi
/// (mppX ~ mppY / cos(lat)), quindi un cerchio in pixel darebbe una fascia
/// allungata in orizzontale e la misura non avrebbe senso.
static void paintEllipse(ks::image::RawImage& img, int cx, int cy, double rx, double ry,
                         std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const int bx = static_cast<int>(std::ceil(rx));
    const int by = static_cast<int>(std::ceil(ry));
    for (int y = cy - by; y <= cy + by; ++y) {
        if (y < 0 || y >= img.height) continue;
        for (int x = cx - bx; x <= cx + bx; ++x) {
            if (x < 0 || x >= img.width) continue;
            const double dx = (x - cx) / std::max(1e-9, rx);
            const double dy = (y - cy) / std::max(1e-9, ry);
            if (dx * dx + dy * dy > 1.0) continue;
            const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4u;
            img.rgba[i] = r;
            img.rgba[i + 1] = g;
            img.rgba[i + 2] = b;
            img.rgba[i + 3] = 255;
        }
    }
}

int main() {
    const std::string dir = "geotrack_tiles_tmp";
    fs::remove_all(dir);
    fs::create_directories(dir + "/tiles");
    std::string err;

    // --- tracciato di prova -----------------------------------------------
    const std::string centerline = dir + "/loop.csv";
    {
        std::ofstream out(centerline);
        out << "50.3355,6.9474\n50.3355,6.9560\n50.3390,6.9560\n50.3390,6.9474\n";
    }
    const std::vector<LatLon> lls = readCenterline(centerline, &err);
    KS_CHECK(lls.size() == 4);
    LatLon centre{0.0, 0.0};
    for (const LatLon& p : lls) {
        centre.lat += p.lat;
        centre.lon += p.lon;
    }
    centre.lat /= static_cast<double>(lls.size());
    centre.lon /= static_cast<double>(lls.size());
    const Projection projection = Projection::fromOrigin(centre);
    std::vector<Enu> pts;
    for (const LatLon& p : lls) pts.push_back(projection.toEnu(p));
    const std::vector<Station> stations = buildStations(pts, 4.0, true);
    KS_CHECK(stations.size() > 100);
    const Square square = squareAround(pts, 120.0);

    // --- tile sintetiche attorno al quadrato ------------------------------
    const LatLon nw = projection.toLatLon(Enu{square.minX, square.maxZ});
    const LatLon se = projection.toLatLon(Enu{square.maxX, square.minZ});
    int zoom = 16;
    int x0 = tileIndexX(nw.lon, zoom), x1 = tileIndexX(se.lon, zoom);
    int y0 = tileIndexY(nw.lat, zoom), y1 = tileIndexY(se.lat, zoom);
    while ((x1 - x0 + 1) * (y1 - y0 + 1) > 16 && zoom > 12) {
        --zoom;
        x0 = tileIndexX(nw.lon, zoom);
        x1 = tileIndexX(se.lon, zoom);
        y0 = tileIndexY(nw.lat, zoom);
        y1 = tileIndexY(se.lat, zoom);
    }
    const int nx = x1 - x0 + 1, ny = y1 - y0 + 1;
    KS_CHECK(nx >= 1 && ny >= 1 && nx * ny <= 16);

    const int pixels = 512;
    std::vector<Tile> tiles(static_cast<std::size_t>(nx * ny));
    std::string listJson;
    for (int iy = 0; iy < ny; ++iy) {
        for (int ix = 0; ix < nx; ++ix) {
            Tile& tile = tiles[static_cast<std::size_t>(iy * nx + ix)];
            tile.image.resize(pixels, pixels);
            for (std::size_t i = 0; i < tile.image.rgba.size(); i += 4) {
                tile.image.rgba[i] = 70;
                tile.image.rgba[i + 1] = 110;
                tile.image.rgba[i + 2] = 60;
                tile.image.rgba[i + 3] = 255;
            }
            const LatLon a = tileNorthWest(zoom, x0 + ix, y0 + iy);
            const LatLon b = tileNorthWest(zoom, x0 + ix + 1, y0 + iy + 1);
            const Enu ea = projection.toEnu(a);
            const Enu eb = projection.toEnu(b);
            tile.minX = std::min(ea.x, eb.x);
            tile.maxX = std::max(ea.x, eb.x);
            tile.minZ = std::min(ea.z, eb.z);
            tile.maxZ = std::max(ea.z, eb.z);
        }
    }

    // Asfalto largo 14 m lungo la centerline, sul mosaico.
    for (const Station& s : stations) {
        for (Tile& tile : tiles) {
            const double width = tile.maxX - tile.minX;
            const double height = tile.maxZ - tile.minZ;
            if (s.p.x < tile.minX || s.p.x > tile.maxX || s.p.z < tile.minZ || s.p.z > tile.maxZ)
                continue;
            const double mppX = width / (pixels - 1);
            const double mppY = height / (pixels - 1);
            const int px = static_cast<int>((s.p.x - tile.minX) / width * (pixels - 1));
            const int py = static_cast<int>((1.0 - (s.p.z - tile.minZ) / height) * (pixels - 1));
            paintEllipse(tile.image, px, py, std::max(1.0, 7.0 / mppX), std::max(1.0, 7.0 / mppY),
                         110, 110, 112);
        }
    }

    std::string names;
    for (int iy = 0; iy < ny; ++iy) {
        for (int ix = 0; ix < nx; ++ix) {
            const std::string name =
                std::to_string(zoom) + "_" + std::to_string(x0 + ix) + "_" + std::to_string(y0 + iy) +
                ".png";
            KS_CHECK(ks::image::saveImageFile(dir + "/tiles/" + name,
                                              tiles[static_cast<std::size_t>(iy * nx + ix)].image,
                                              &err));
            if (!names.empty()) names += ",";
            names += "\"" + name + "\"";
        }
    }
    {
        std::ofstream out(dir + "/cache.json");
        out << "{\"zoom\": " << zoom << ", \"tiles\": [" << names << "], "
            << "\"bounds\": {\"south\": " << se.lat << ", \"west\": " << nw.lon
            << ", \"north\": " << nw.lat << ", \"east\": " << se.lon << "}}";
    }

    // --- mosaico ----------------------------------------------------------
    TileMosaic mosaic;
    KS_CHECK(mosaic.loadCache(dir + "/cache.json", projection, &err));
    KS_CHECK(static_cast<int>(mosaic.tiles.size()) == nx * ny);
    KS_CHECK(mosaic.metresPerPixel() > 0.5);
    KS_CHECK(mosaic.isAsphalt(stations.front().p.x, stations.front().p.z));
    KS_CHECK(!mosaic.isAsphalt(square.minX + 1.0, square.minZ + 1.0));

    const WidthMeasurement m = measureTrackWidthImpl(mosaic, stations, 4.0, 25.0);
    KS_CHECK(m.samples > 50);
    KS_CHECK_NEAR(m.medianM, 14.0, 2.0);

    // Ritaglio di un chunk: dimensione richiesta e alpha pieno al centro.
    const ks::image::RawImage crop = mosaic.crop(square, 64);
    KS_CHECK(crop.width == 64 && crop.height == 64);
    KS_CHECK(!crop.rgba.empty());

    // Cache inesistente: errore, non un crash.
    TileMosaic missing;
    KS_CHECK(!missing.loadCache(dir + "/nope.json", projection, &err));
    KS_CHECK(!err.empty());

    // --- generazione con le tile ------------------------------------------
    GeotrackConfig cfg;
    cfg.centerlinePath = centerline;
    cfg.outDir = dir + "/track";
    cfg.trackName = "Tiled";
    cfg.widthM = 12.0; // stima sbagliata di proposito
    cfg.tileCachePath = dir + "/cache.json";
    cfg.chunkSizeM = 300.0;

    GeotrackResult res;
    KS_CHECK(generateTrack(cfg, &res, &err));
    KS_CHECK(err.empty());
    KS_CHECK(res.groundChunks >= 4);
    KS_CHECK_NEAR(res.measuredWidthM, 14.0, 2.0);

    // Formato: i chunk sono NMS2 con la finestra di distanza, gli altri layer
    // restano NMSH (nessuna finestra). Il layout e' quello che legge
    // NativeRenderer::loadMeshFromFile.
    {
        const std::string path = cfg.outDir + "/baked/ground_0_0.nmsh";
        std::ifstream in(path, std::ios::binary);
        char magic[4] = {0, 0, 0, 0};
        std::uint32_t vertexCount = 0, indexCount = 0;
        float lodIn = -1.0f, lodOut = -1.0f;
        in.read(magic, 4);
        in.read(reinterpret_cast<char*>(&vertexCount), 4);
        in.read(reinterpret_cast<char*>(&indexCount), 4);
        in.read(reinterpret_cast<char*>(&lodIn), 4);
        in.read(reinterpret_cast<char*>(&lodOut), 4);
        KS_CHECK(std::string(magic, 4) == "NMS2");
        KS_CHECK(vertexCount == 4);  // un quadrato per chunk
        KS_CHECK(indexCount == 6);
        KS_CHECK_NEAR(lodIn, 0.0f, 1e-6);
        KS_CHECK_NEAR(lodOut, cfg.chunkViewM ? 1400.0f : 0.0f, 1e-3);
        // Header = magic(4) + vCount(4) + iCount(4) + lodIn(4) + lodOut(4) = 20,
        // poi vertici (48 byte ciascuno, la dimensione che il renderer legge) e
        // indici uint32. Niente di piu': il reader si aspetta esattamente questo.
        KS_CHECK_NEAR(static_cast<double>(fs::file_size(path)),
                      20.0 + vertexCount * 48.0 + indexCount * 4.0, 0.5);
    }
    {
        std::ifstream in(cfg.outDir + "/baked/track.nmsh", std::ios::binary);
        char magic[4] = {0, 0, 0, 0};
        in.read(magic, 4);
        KS_CHECK(std::string(magic, 4) == "NMSH");
    }
    // Piano lontano: NMSH (sempre disegnato), griglia 16x16 sul quadrato.
    {
        const std::string path = cfg.outDir + "/baked/ground_far.nmsh";
        KS_CHECK(fs::is_regular_file(path));
        KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/textures/ground_far.png"));
        std::ifstream in(path, std::ios::binary);
        char magic[4] = {0, 0, 0, 0};
        std::uint32_t vertexCount = 0, indexCount = 0;
        in.read(magic, 4);
        in.read(reinterpret_cast<char*>(&vertexCount), 4);
        in.read(reinterpret_cast<char*>(&indexCount), 4);
        KS_CHECK(std::string(magic, 4) == "NMSH");
        KS_CHECK(vertexCount == 17u * 17u);
        KS_CHECK(indexCount == 6u * 16u * 16u);
    }

    const std::string manifest = [&] {
        std::ifstream in(cfg.outDir + "/baked/manifest.txt");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return text;
    }();
    KS_CHECK(manifest.find("ground_0_0") != std::string::npos);
    KS_CHECK(manifest.find("track") != std::string::npos);
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/ground_0_0.nmsh"));
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/textures/ground_0_0.png"));

    // Scoped: see geotrack_generate_test — an open handle makes the closing
    // remove_all() throw on Windows, which aborts instead of failing.
    {
        std::ifstream materials(cfg.outDir + "/baked/materials.txt");
        std::string firstRow;
        std::getline(materials, firstRow);
        KS_CHECK(firstRow.find("ground_0_0\tground_0_0.png") == 0);
    }

    ks::ac::TrackDataJsonLoader loader;
    KS_CHECK(loader.loadForTrackDir(cfg.outDir));
    KS_CHECK_NEAR(loader.meta().widthM, res.measuredWidthM, 0.01);

    std::error_code cleanupEc;
    fs::remove_all(dir, cleanupEc);
    return KS_TEST_RESULT("geotrack_tiles_test");
}
