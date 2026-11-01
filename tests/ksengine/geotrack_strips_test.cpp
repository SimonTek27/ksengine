// Geotrack fase 1 — mesh dei layer: quadrato, strisce, cubetti (Task 3).
#include "KsTest.h"

#include "geotrack/Strips.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace ks::geo;
using ks::engine::terrain::TerrainMeshData;

static void checkMesh(const TerrainMeshData& m) {
    KS_CHECK(!m.vertices.empty());
    KS_CHECK(m.indices.size() % 3 == 0);
    for (std::uint32_t i : m.indices) KS_CHECK(i < m.vertices.size());
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const auto& a = m.vertices[m.indices[t]];
        const auto& b = m.vertices[m.indices[t + 1]];
        const auto& c = m.vertices[m.indices[t + 2]];
        const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
        const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
        const float nx = uy * vz - uz * vy;
        const float ny = uz * vx - ux * vz;
        const float nz = ux * vy - uy * vx;
        KS_CHECK(nx * nx + ny * ny + nz * nz > 1e-12f); // non degenere
        KS_CHECK(ny > 0.0f);                           // normale verso l'alto
    }
}

int main() {
    const Square sq{0, 2500, 0, 2500};
    const TerrainMeshData g = groundQuad(sq, 0.0, 500.0f);
    checkMesh(g);
    KS_CHECK_NEAR(g.vertices.size(), 4.0, 0.0);
    KS_CHECK_NEAR(g.vertices[0].py, 0.0f, 1e-6);

    // Rettilineo di 200 m verso nord, una stazione ogni 5 m.
    std::vector<Station> st;
    for (int i = 0; i <= 40; ++i) {
        Station s;
        s.p = {0.0, i * 5.0};
        s.tangent = {0.0, 1.0};
        s.left = {-1.0, 0.0};
        s.s = i * 5.0;
        st.push_back(s);
    }

    const TerrainMeshData r = ribbon(st, {0.0, 6.0, 0.02, 10.0f});
    checkMesh(r);
    KS_CHECK_NEAR(r.vertices.size(), 2.0 * st.size(), 0.0);
    KS_CHECK_NEAR(std::fabs(r.vertices[0].px - r.vertices[1].px), 12.0, 1e-4);
    KS_CHECK_NEAR(r.vertices[0].py, 0.02f, 1e-6);

    // Offset laterale verso sinistra (-X per una pista che va a nord).
    const TerrainMeshData off = ribbon(st, {8.0, 3.0, 0.02, 10.0f});
    KS_CHECK_NEAR(off.vertices[0].px, -11.0, 1e-4);
    KS_CHECK_NEAR(off.vertices[1].px, -5.0, 1e-4);

    // Curvatura forte: normali ancora verso l'alto.
    std::vector<Station> loop;
    for (int i = 0; i < 72; ++i) {
        const double a = i * 5.0 / 60.0;
        Station s;
        s.p = {60.0 * std::cos(a), 60.0 * std::sin(a)};
        s.tangent = {-std::sin(a), std::cos(a)};
        s.left = {-s.tangent.z, s.tangent.x};
        s.s = i * 5.0;
        loop.push_back(s);
    }
    checkMesh(ribbon(loop, {0.0, 6.0, 0.02, 10.0f}));

    // Griglia: 30 slot, mesh non vuota e sopra l'asfalto.
    const TerrainMeshData slots = gridSlots(st, 20.0, 30, 8.0, 0.0, 1.2, 4.0, 0.06);
    checkMesh(slots);
    KS_CHECK(slots.vertices.size() >= 4 * 30);
    KS_CHECK_NEAR(slots.vertices[0].py, 0.06f, 1e-6);

    // Input degeneri: nessun crash, mesh vuota.
    KS_CHECK(ribbon({}, {0.0, 6.0, 0.0, 10.0f}).vertices.empty());
    KS_CHECK(gridSlots(st, 0.0, 0, 8.0, 0.0, 1.2, 4.0, 0.0).vertices.empty());

    return KS_TEST_RESULT("geotrack_strips_test");
}
