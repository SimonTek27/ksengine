// Geotrack fase 1 — proiezione ENU e quadrato del terreno (Task 1).
#include "KsTest.h"

#include "geotrack/GeoProjection.h"

#include <cmath>

using namespace ks::geo;

int main() {
    const LatLon origin{50.3355, 6.9474}; // Nordschleife
    const Projection pr = Projection::fromOrigin(origin);

    // 0.001 deg a questa latitudine: 111.319 m in lat, 111.319*cos(lat) in lon.
    KS_CHECK_NEAR(pr.toEnu({origin.lat + 0.001, origin.lon}).z, 111.319, 0.1);
    KS_CHECK_NEAR(pr.toEnu({origin.lat, origin.lon + 0.001}).x, 71.05, 0.2);

    // Andata e ritorno coerenti entro il millimetro.
    const Enu back = pr.toEnu(pr.toLatLon({1234.5, -987.25}));
    KS_CHECK_NEAR(back.x, 1234.5, 1e-3);
    KS_CHECK_NEAR(back.z, -987.25, 1e-3);

    // Quadrato: lato = estensione maggiore + 2*pad, centrato sul bbox.
    const std::vector<Enu> pts{{0, 0}, {2000, 0}, {2000, 500}, {0, 500}};
    const Square sq = squareAround(pts, 250.0);
    KS_CHECK_NEAR(sq.side(), 2500.0, 1e-9);
    KS_CHECK_NEAR(sq.centreX(), 1000.0, 1e-9);
    KS_CHECK_NEAR(sq.centreZ(), 250.0, 1e-9);

    // UV: angolo NW = (0,1), SE = (1,0).
    float u = -1, v = -1;
    uvOn(sq, sq.minX, sq.maxZ, &u, &v);
    KS_CHECK_NEAR(u, 0.0f, 1e-6);
    KS_CHECK_NEAR(v, 1.0f, 1e-6);
    uvOn(sq, sq.maxX, sq.minZ, &u, &v);
    KS_CHECK_NEAR(u, 1.0f, 1e-6);
    KS_CHECK_NEAR(v, 0.0f, 1e-6);

    // Antimeridiano: due punti a lon 179.999 e -179.999 distano ~200 m, non 40000 km.
    const Projection dateline = Projection::fromOrigin({0.0, 180.0});
    const Enu a = dateline.toEnu({0.0, 179.999});
    const Enu b = dateline.toEnu({0.0, -179.999});
    KS_CHECK_NEAR(std::fabs(a.x - b.x), 222.6, 1.0);

    return KS_TEST_RESULT("geotrack_geo_test");
}
