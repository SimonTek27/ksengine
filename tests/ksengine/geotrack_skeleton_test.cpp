// Geotrack fase 2 — scheletro dell'asfalto e snap della centerline.
//
// Il caso che conta: la centerline vettoriale sta 5 m fuori dal centro
// dell'asfalto. Dopo lo snap deve tornarci, senza cambiare numero di stazioni
// ne' lunghezza del giro.
#include "KsTest.h"

#include "engine/material/PngCodec.h"
#include "geotrack/AsphaltSkeleton.h"
#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"
#include "geotrack/TrackSceneCore.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ks::geo;

/// Asfalto analitico: fascia larga 14 m centrata su z = offset.
struct BandSampler {
    double offsetZ = 0.0;
    double halfWidth = 7.0;
    double minX = -320.0;
    double maxX = 320.0;

    double metresPerPixel() const { return 1.0; }
    bool isAsphalt(double x, double z) const {
        return x >= minX && x <= maxX && std::fabs(z - offsetZ) <= halfWidth;
    }
};

static std::vector<Station> straightLine(double z, double step) {
    std::vector<Station> st;
    for (int i = 0; i <= 300; ++i) {
        Station s;
        s.p = {-300.0 + i * step, z};
        s.tangent = {1.0, 0.0};
        s.left = {0.0, 1.0};
        s.s = i * step;
        st.push_back(s);
    }
    return st;
}

static double meanAbsZ(const std::vector<Station>& st, double truth) {
    double total = 0.0;
    for (const Station& s : st) total += std::fabs(s.p.z - truth);
    return total / static_cast<double>(st.size());
}

int main() {
    const std::string dir = "geotrack_skeleton_tmp";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string err;

    // --- 1. raster + thinning ---------------------------------------------
    const Square square{-320.0, 320.0, -320.0, 320.0};
    const BandSampler band;
    const MaskGrid mask = rasterizeAsphalt(band, square, 1.0);
    KS_CHECK(mask.width == 640 && mask.height == 640);
    KS_CHECK(!mask.cell.empty());
    int asphaltCells = 0;
    for (const std::uint8_t c : mask.cell) asphaltCells += c ? 1 : 0;
    KS_CHECK(asphaltCells > 8000); // 640 m * 14 m ~ 8960 celle

    MaskGrid skeleton = mask;
    const int iterations = thinZhangSuen(skeleton);
    KS_CHECK(iterations > 0);
    int skeletonCells = 0;
    for (const std::uint8_t c : skeleton.cell) skeletonCells += c ? 1 : 0;
    KS_CHECK(skeletonCells > 300);
    KS_CHECK(skeletonCells < asphaltCells / 3); // e' uno scheletro, non la fascia

    // Lo scheletro sta sul centro della fascia (z ~ 0), non sul bordo.
    int onCentre = 0, offCentre = 0;
    for (int y = 0; y < skeleton.height; ++y) {
        for (int x = 0; x < skeleton.width; ++x) {
            if (!skeleton.at(x, y)) continue;
            if (std::fabs(skeleton.centreZ(y)) <= 1.5) ++onCentre;
            else ++offCentre;
        }
    }
    KS_CHECK(onCentre > offCentre * 10);

    // --- 2. snap di una centerline spostata di 5 m -------------------------
    std::vector<Station> wrong = straightLine(5.0, 2.0);
    KS_CHECK_NEAR(meanAbsZ(wrong, 0.0), 5.0, 1e-9);

    std::vector<Enu> positions;
    for (const Station& s : wrong) positions.push_back(s.p);
    const std::vector<Enu> snapped = snapToSkeleton(positions, skeleton, 8.0);
    const std::vector<Station> fixed = restationFromPoints(wrong, snapped, false);
    KS_CHECK(fixed.size() == wrong.size()); // topologia intatta
    KS_CHECK(meanAbsZ(fixed, 0.0) < 1.5);   // e ora sta sul centro

    double lengthBefore = 0.0, lengthAfter = 0.0;
    for (std::size_t i = 1; i < wrong.size(); ++i)
        lengthBefore += std::hypot(wrong[i].p.x - wrong[i - 1].p.x, wrong[i].p.z - wrong[i - 1].p.z);
    for (std::size_t i = 1; i < fixed.size(); ++i)
        lengthAfter += std::hypot(fixed[i].p.x - fixed[i - 1].p.x, fixed[i].p.z - fixed[i - 1].p.z);
    KS_CHECK_NEAR(lengthAfter, lengthBefore, 12.0); // nessun allungamento

    // Tangente e sinistra restano coerenti dopo la correzione.
    for (const Station& s : fixed) {
        KS_CHECK_NEAR(std::hypot(s.tangent.x, s.tangent.z), 1.0, 1e-9);
        KS_CHECK_NEAR(s.tangent.x * s.left.x + s.tangent.z * s.left.z, 0.0, 1e-9);
    }

    // --- 3. senza asfalto niente si muove ---------------------------------
    BandSampler empty;
    empty.halfWidth = 0.0;
    empty.minX = 1e9;
    empty.maxX = 1e9 + 1.0;
    MaskGrid emptyMask = rasterizeAsphalt(empty, square, 1.0);
    thinZhangSuen(emptyMask);
    std::vector<Station> untouched = straightLine(5.0, 2.0);
    std::vector<Enu> untouchedPositions;
    for (const Station& s : untouched) untouchedPositions.push_back(s.p);
    const std::vector<Enu> same = snapToSkeleton(untouchedPositions, emptyMask, 8.0);
    for (std::size_t i = 0; i < same.size(); ++i) {
        KS_CHECK_NEAR(same[i].x, untouchedPositions[i].x, 1e-9);
        KS_CHECK_NEAR(same[i].z, untouchedPositions[i].z, 1e-9);
    }

    // --- 4. end-to-end: --correct sposta la centerline sull'asfalto -------
    const std::string centerline = dir + "/loop.csv";
    {
        std::ofstream out(centerline);
        out << "50.3355,6.9474\n50.3355,6.961470\n50.33999,6.961470\n50.33999,6.9474\n";
    }
    const std::vector<LatLon> lls = readCenterline(centerline, &err);
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
    const Square trackSquare = squareAround(pts, 150.0);
    const std::vector<Station> trackStations = buildStations(pts, 4.0, true);

    // Ortofoto con l'asfalto spostato di 5 m verso sinistra rispetto al
    // vettoriale: e' l'errore tipico di un nodo OSM.
    const int pixels = 1200;
    const double mpp = trackSquare.side() / pixels;
    ks::image::RawImage photo;
    photo.resize(pixels, pixels);
    for (std::size_t i = 0; i < photo.rgba.size(); i += 4) {
        photo.rgba[i] = 70;
        photo.rgba[i + 1] = 110;
        photo.rgba[i + 2] = 60;
        photo.rgba[i + 3] = 255;
    }
    const int radius = std::max(2, static_cast<int>(std::lround(7.0 / mpp)));
    for (const Station& s : trackStations) {
        const double cxw = s.p.x + s.left.x * 5.0;
        const double czw = s.p.z + s.left.z * 5.0;
        const int cx = static_cast<int>(std::lround((cxw - trackSquare.minX) / trackSquare.side() * (pixels - 1)));
        const int cy = static_cast<int>(std::lround((1.0 - (czw - trackSquare.minZ) / trackSquare.side()) * (pixels - 1)));
        for (int y = cy - radius; y <= cy + radius; ++y) {
            if (y < 0 || y >= pixels) continue;
            for (int x = cx - radius; x <= cx + radius; ++x) {
                if (x < 0 || x >= pixels) continue;
                const int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy > radius * radius) continue;
                const std::size_t i = (static_cast<std::size_t>(y) * pixels + x) * 4u;
                photo.rgba[i] = 110;
                photo.rgba[i + 1] = 110;
                photo.rgba[i + 2] = 112;
                photo.rgba[i + 3] = 255;
            }
        }
    }
    const std::string photoPath = dir + "/photo.png";
    KS_CHECK(ks::image::saveImageFile(photoPath, photo, &err));

    GeotrackConfig cfg;
    cfg.centerlinePath = centerline;
    cfg.outDir = dir + "/plain";
    cfg.trackName = "Plain";
    cfg.orthoPath = photoPath;
    cfg.keepWidth = true;
    // L'ortofoto e' georeferenziata sul quadrato con pad 150: il generatore
    // deve usare lo stesso, altrimenti immagine e geometria non coincidono.
    cfg.padM = 150.0;
    GeotrackResult plainRes;
    const bool plainOk = generateTrack(cfg, &plainRes, &err);
    if (!plainOk) std::printf("plain error: %s\n", err.c_str());
    KS_CHECK(plainOk);
    KS_CHECK(plainRes.centerlineShiftM == 0.0);

    GeotrackConfig corrected = cfg;
    corrected.outDir = dir + "/fixed";
    corrected.trackName = "Fixed";
    corrected.correctCenterline = true;
    corrected.correctSearchM = 8.0;
    GeotrackResult fixedRes;
    const bool fixedOk = generateTrack(corrected, &fixedRes, &err);
    if (!fixedOk) std::printf("corrected error: %s\n", err.c_str());
    KS_CHECK(fixedOk);
    KS_CHECK(fixedRes.centerlineShiftM > 2.0); // si e' spostata verso l'asfalto
    KS_CHECK(fixedRes.centerlineShiftM < 8.0); // senza saltare altrove
    KS_CHECK(fs::is_regular_file(corrected.outDir + "/baked/track.nmsh"));
    {
        std::ifstream in(corrected.outDir + "/scene.json");
        const std::string scene((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        KS_CHECK(scene.find("centerline_shift_m") != std::string::npos);
    }

    fs::remove_all(dir);
    return KS_TEST_RESULT("geotrack_skeleton_test");
}
