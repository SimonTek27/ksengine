// Geotrack fase 2 — misura dell'asfalto da un'immagine georeferenziata.
//
// Fixture sintetica e deterministica: erba verde, pista grigia larga 14 m,
// cordolo bianco sul bordo e un apron dei box largo 30 m. Serve a verificare la
// catena decodifica PNG -> campionamento perpendicolare -> mediana, che e'
// esattamente il punto in cui i dati vettoriali sbagliavano (30-39 m invece di
// ~14 perche' l'apron finiva classificato come tracciato).
#include "KsTest.h"

#include "adapters/assetto_corsa/TrackDataJsonLoader.h"
#include "engine/material/PngCodec.h"
#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"
#include "geotrack/OrthoImage.h"
#include "geotrack/PitLane.h"
#include "geotrack/TrackSceneCore.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ks::geo;

static void paintDisc(ks::image::RawImage& img, int cx, int cy, int radius, std::uint8_t r,
                      std::uint8_t g, std::uint8_t b) {
    for (int y = cy - radius; y <= cy + radius; ++y) {
        if (y < 0 || y >= img.height) continue;
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (x < 0 || x >= img.width) continue;
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > radius * radius) continue;
            const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4u;
            img.rgba[i] = r;
            img.rgba[i + 1] = g;
            img.rgba[i + 2] = b;
            img.rgba[i + 3] = 255;
        }
    }
}

static void fillGrass(ks::image::RawImage& img) {
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i] = 70;
        img.rgba[i + 1] = 110;
        img.rgba[i + 2] = 60;
        img.rgba[i + 3] = 255;
    }
}

int main() {
    const std::string dir = "geotrack_ortho_tmp";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string err;

    // --- 1. catena di misura su un rettangolo noto ------------------------
    const Square square{-750.0, 750.0, -750.0, 750.0};
    const int size = 1500; // 1 pixel per metro
    ks::image::RawImage img;
    img.resize(size, size);
    fillGrass(img);

    // Rettilineo isolato: qui la misura perpendicolare deve essere esatta. Gli
    // angoli a 90 gradi sono un caso limite (il cordolo interno allarga il
    // campione per costruzione) e li copre la prova end-to-end piu' sotto.
    std::vector<Station> stations;
    for (int i = 0; i <= 800; ++i) {
        Station s;
        s.p = {-450.0, -400.0 + static_cast<double>(i)};
        s.tangent = {0.0, 1.0};
        s.left = {-1.0, 0.0};
        s.s = static_cast<double>(i);
        stations.push_back(s);
    }
    for (const Station& s : stations) {
        const int px = static_cast<int>((s.p.x - square.minX));
        const int py = static_cast<int>(square.maxZ - s.p.z);
        paintDisc(img, px, py, 7, 110, 110, 112); // 14 m di asfalto
        // Cordolo bianco a 8 m: 1 pixel, non deve troncare la carreggiata.
        paintDisc(img, px - 8, py, 0, 235, 235, 235);
    }

    const std::string png = dir + "/ortho.png";
    KS_CHECK(ks::image::saveImageFile(png, img, &err));
    KS_CHECK(fs::is_regular_file(png));

    OrthoImage ortho;
    KS_CHECK(ortho.load(png, square, &err));
    KS_CHECK(ortho.valid());
    KS_CHECK_NEAR(ortho.metresPerPixel(), 1.0, 1e-9);

    KS_CHECK(!isAsphaltPixel(70, 110, 60));   // erba
    KS_CHECK(!isAsphaltPixel(235, 235, 235)); // vernice bianca
    KS_CHECK(isAsphaltPixel(110, 110, 112));  // asfalto
    KS_CHECK(!ortho.isAsphalt(0.0, 0.0)); // centro del quadrato: erba
    KS_CHECK(ortho.isAsphalt(-450.0, -200.0));

    const WidthMeasurement m = measureTrackWidth(ortho, stations, 4.0, 25.0);
    KS_CHECK_NEAR(m.samples, 801.0, 0.0);
    KS_CHECK_NEAR(m.medianM, 14.0, 1.0);
    KS_CHECK(m.minM > 12.0);
    KS_CHECK(m.maxM < 16.0);

    OrthoImage missing;
    KS_CHECK(!missing.load(dir + "/nope.png", square, &err));
    KS_CHECK(!err.empty());

    // --- 2. end-to-end: la misura sostituisce la larghezza stimata --------
    const std::string centerline = dir + "/loop.csv";
    {
        std::ofstream out(centerline);
        out << "50.3355,6.9474\n50.3355,6.961470\n50.33999,6.961470\n50.33999,6.9474\n";
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
    const Square trackSquare = squareAround(pts, 250.0);
    const std::vector<Station> trackStations = buildStations(pts, 5.0, true);

    const int px = 1500;
    const double mpp = trackSquare.side() / px;
    const int radius = static_cast<int>(std::lround(7.0 / mpp));
    ks::image::RawImage photo;
    photo.resize(px, px);
    fillGrass(photo);
    const auto toPixel = [&](double x, double z, int* outX, int* outY) {
        *outX = static_cast<int>(std::lround((x - trackSquare.minX) / trackSquare.side() * (px - 1)));
        *outY = static_cast<int>(
            std::lround((1.0 - (z - trackSquare.minZ) / trackSquare.side()) * (px - 1)));
    };
    for (const Station& s : trackStations) {
        int x = 0, y = 0;
        toPixel(s.p.x, s.p.z, &x, &y);
        paintDisc(photo, x, y, radius, 110, 110, 112);
    }
    // Apron dei box: blob largo ~30 m accanto ai primi 60 m di pista. E' la
    // sorgente del bug della larghezza: la mediana non deve seguirlo.
    for (double s = 20.0; s <= 80.0; s += 5.0) {
        const Station at = stationAt(trackStations, s);
        int x = 0, y = 0;
        toPixel(at.p.x + at.left.x * 18.0, at.p.z + at.left.z * 18.0, &x, &y);
        paintDisc(photo, x, y, static_cast<int>(std::lround(15.0 / mpp)), 112, 112, 114);
    }
    const std::string photoPath = dir + "/photo.png";
    KS_CHECK(ks::image::saveImageFile(photoPath, photo, &err));

    GeotrackConfig cfg;
    cfg.centerlinePath = centerline;
    cfg.outDir = dir + "/track";
    cfg.trackName = "Measured";
    cfg.widthM = 12.0; // stima iniziale, sbagliata di proposito
    cfg.orthoPath = photoPath;

    GeotrackResult res;
    KS_CHECK(generateTrack(cfg, &res, &err));
    KS_CHECK(err.empty());
    KS_CHECK(res.widthSamples > 300);
    KS_CHECK_NEAR(res.measuredWidthM, 14.0, 1.5);

    ks::ac::TrackDataJsonLoader loader;
    KS_CHECK(loader.loadForTrackDir(cfg.outDir));
    KS_CHECK_NEAR(loader.meta().widthM, res.measuredWidthM, 0.01);

    // --keep-width: la misura resta riportata ma la geometria usa --width.
    GeotrackConfig keep = cfg;
    keep.outDir = dir + "/keep";
    keep.trackName = "Keep";
    keep.keepWidth = true;
    keep.widthM = 12.0;
    KS_CHECK(generateTrack(keep, &res, &err));
    KS_CHECK_NEAR(res.measuredWidthM, 14.0, 1.5);
    ks::ac::TrackDataJsonLoader keepLoader;
    KS_CHECK(keepLoader.loadForTrackDir(keep.outDir));
    KS_CHECK_NEAR(keepLoader.meta().widthM, 12.0, 0.01);

    fs::remove_all(dir);
    return KS_TEST_RESULT("geotrack_ortho_test");
}
