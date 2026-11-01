// Geotrack fase 1 — lettori centerline e stazioni (Task 2).
#include "KsTest.h"

#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace ks::geo;

static void writeFile(const std::string& p, const std::string& text) {
    std::ofstream(p) << text;
}

int main() {
    const std::string dir = "geotrack_tmp";
    std::filesystem::create_directories(dir);

    // Rettangolo ~1000 x 500 m, chiuso: perimetro ~3000 m.
    // Delta lon 0.01407 deg a lat 50.3355 = 999.6 m, delta lat 0.00449 = 499.8 m.
    const char* corners =
        "50.3355,6.9474\n"
        "50.3355,6.961470\n"
        "50.33999,6.961470\n"
        "50.33999,6.9474\n";
    writeFile(dir + "/rect.csv", std::string("# lat,lon\n") + corners);

    std::string err;
    const std::vector<LatLon> csv = readCenterline(dir + "/rect.csv", &err);
    KS_CHECK(err.empty());
    KS_CHECK_NEAR(csv.size(), 4.0, 0.0);

    const Projection pr = Projection::fromOrigin(csv.front());
    std::vector<Enu> pts;
    for (const LatLon& p : csv) pts.push_back(pr.toEnu(p));
    const double perimeter = polylineLength(pts, true);
    KS_CHECK_NEAR(perimeter, 3000.0, 15.0);

    const std::vector<Station> st = buildStations(pts, 5.0, true);
    KS_CHECK(st.size() > 550 && st.size() < 620); // ~3000/5 stazioni
    KS_CHECK_NEAR(st.back().s, perimeter, 15.0);

    // A meta' del primo rettilineo (verso est) la sinistra punta a nord.
    KS_CHECK_NEAR(st[20].tangent.x, 1.0, 1e-6);
    KS_CHECK_NEAR(st[20].tangent.z, 0.0, 1e-6);
    KS_CHECK_NEAR(st[20].left.z, 1.0, 1e-6);

    // Tangente e normale unitarie e perpendicolari ovunque.
    for (const Station& s : st) {
        KS_CHECK_NEAR(std::hypot(s.tangent.x, s.tangent.z), 1.0, 1e-9);
        KS_CHECK_NEAR(s.tangent.x * s.left.x + s.tangent.z * s.left.z, 0.0, 1e-9);
    }

    // GeoJSON e GPX producono la stessa centerline del CSV.
    writeFile(dir + "/rect.geojson",
              "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\",\"properties\":{},"
              "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[6.9474,50.3355],"
              "[6.961470,50.3355],[6.961470,50.33999],[6.9474,50.33999]]}}]}");
    const std::vector<LatLon> gj = readCenterline(dir + "/rect.geojson", &err);
    KS_CHECK(err.empty());
    KS_CHECK_NEAR(gj.size(), csv.size(), 0.0);
    KS_CHECK_NEAR(gj[1].lon, csv[1].lon, 1e-9);
    KS_CHECK_NEAR(gj[1].lat, csv[1].lat, 1e-9);

    writeFile(dir + "/rect.gpx",
              "<?xml version=\"1.0\"?><gpx><trk><trkseg>"
              "<trkpt lat=\"50.3355\" lon=\"6.9474\"/><trkpt lat=\"50.3355\" lon=\"6.961470\"/>"
              "<trkpt lat=\"50.33999\" lon=\"6.961470\"/><trkpt lat=\"50.33999\" lon=\"6.9474\"/>"
              "</trkseg></trk></gpx>");
    KS_CHECK_NEAR(readCenterline(dir + "/rect.gpx", &err).size(), 4.0, 0.0);

    // Curva di 90 gradi: la curvatura cresce vicino all'angolo.
    double maxCurvature = 0.0;
    for (const Station& s : st) maxCurvature = std::fmax(maxCurvature, s.curvature);
    KS_CHECK(maxCurvature > 0.05); // raggio < 20 m all'angolo retto campionato
    KS_CHECK(countCorners(st, 80.0, 50.0) >= 3);

    // Degeneri: 2 punti chiusi restano una pista valida, punti coincidenti no.
    KS_CHECK(buildStations({{0, 0}, {100, 0}}, 5.0, true).size() >= 2);
    KS_CHECK(buildStations({{0, 0}, {0, 0}, {0, 0}}, 5.0, false).empty());
    KS_CHECK(buildStations({}, 5.0, true).empty());
    KS_CHECK(readCenterline(dir + "/missing.csv", &err).empty());
    KS_CHECK(!err.empty());
    KS_CHECK(readCenterline(dir + "/rect.kml", &err).empty());

    std::filesystem::remove_all(dir);
    return KS_TEST_RESULT("geotrack_centerline_test");
}
