#pragma once
// Geotrack fase 1 — mesh dei layer: quadrato di terreno, strisce, cubetti.
//
// Le mesh escono come ks::engine::terrain::TerrainMeshData (stesso layout di
// vertex del renderer) e vengono scritte in NMSH da writeTerrainNMSH() nel
// chiamante. Qui niente I/O: tutto puro e testabile.
//
// Convenzione di avvolgimento: con +X est e +Z nord, i triangoli sono emessi in
// modo che la normale calcolata da (b-a)x(c-a) punti a +Y. Verificato dai test
// (un triangolo capovolto verrebbe scartato dal back-face culling).

#include "engine/terrain/TerrainMesh.h"
#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace ks::geo {

struct StripSpec {
    double offset = 0.0;         ///< metri verso sinistra rispetto alla centerline
    double halfWidth = 6.0;      ///< mezza larghezza della striscia
    double y = 0.0;              ///< quota del layer
    float uvRepeatMetres = 10.0f; ///< il v si ripete ogni N metri
};

inline ks::engine::terrain::TerrainVertex makeVertex(double x, double y, double z, float u,
                                                    float v) {
    ks::engine::terrain::TerrainVertex vert;
    vert.px = static_cast<float>(x);
    vert.py = static_cast<float>(y);
    vert.pz = static_cast<float>(z);
    vert.nx = 0.0f;
    vert.ny = 1.0f;
    vert.nz = 0.0f;
    vert.u = u;
    vert.v = v;
    return vert;
}

/// Quadrato di terreno: 4 vertici, 2 triangoli, UV geografiche (NW = (0,1)).
inline ks::engine::terrain::TerrainMeshData groundQuad(const Square& sq, double y,
                                                       float uvRepeatMetres) {
    ks::engine::terrain::TerrainMeshData mesh;
    const float uScale = sq.side() > 0.0 ? static_cast<float>(1.0 / sq.side()) : 1.0f;
    (void)uvRepeatMetres; // il fondo copre il quadrato una volta sola
    float u = 0.f, v = 0.f;
    const double xs[4] = {sq.minX, sq.maxX, sq.maxX, sq.minX};
    const double zs[4] = {sq.minZ, sq.minZ, sq.maxZ, sq.maxZ};
    for (int i = 0; i < 4; ++i) {
        uvOn(sq, xs[i], zs[i], &u, &v);
        mesh.vertices.push_back(makeVertex(xs[i], y, zs[i], u, v));
    }
    (void)uScale;
    const uint32_t idx[6] = {0, 3, 1, 3, 2, 1};
    mesh.indices.assign(idx, idx + 6);
    return mesh;
}

/// Griglia regolare sul quadrato: serve al fondo "lontano", il piano che
/// resta disegnato oltre la finestra dei chunk (senza, i chunk scartati
/// lascerebbero un vuoto all'orizzonte).
inline ks::engine::terrain::TerrainMeshData groundGrid(const Square& sq, double y, int cells) {
    ks::engine::terrain::TerrainMeshData mesh;
    cells = std::max(1, cells);
    const int side = cells + 1;
    for (int iz = 0; iz <= cells; ++iz) {
        for (int ix = 0; ix <= cells; ++ix) {
            const double fx = static_cast<double>(ix) / cells;
            const double fz = static_cast<double>(iz) / cells;
            const double x = sq.minX + fx * sq.side();
            const double z = sq.minZ + fz * sq.side();
            float u = 0.f, v = 0.f;
            uvOn(sq, x, z, &u, &v);
            mesh.vertices.push_back(makeVertex(x, y, z, u, v));
        }
    }
    for (int iz = 0; iz < cells; ++iz) {
        for (int ix = 0; ix < cells; ++ix) {
            const uint32_t a = static_cast<uint32_t>(iz * side + ix);
            const uint32_t b = a + 1;
            const uint32_t c = a + static_cast<uint32_t>(side);
            const uint32_t d = c + 1;
            const uint32_t tris[6] = {a, c, b, c, d, b};
            mesh.indices.insert(mesh.indices.end(), tris, tris + 6);
        }
    }
    return mesh;
}

/// Striscia lungo la centerline: due vertici per stazione, due triangoli per
/// segmento. Con `offset` la striscia si sposta sul lato sinistro.
inline ks::engine::terrain::TerrainMeshData ribbon(const std::vector<Station>& st,
                                                   const StripSpec& spec) {
    ks::engine::terrain::TerrainMeshData mesh;
    if (st.size() < 2) return mesh;
    const float invRepeat = spec.uvRepeatMetres > 0.f ? 1.f / spec.uvRepeatMetres : 0.f;
    for (const Station& s : st) {
        const double lx = s.p.x + s.left.x * (spec.offset + spec.halfWidth);
        const double lz = s.p.z + s.left.z * (spec.offset + spec.halfWidth);
        const double rx = s.p.x + s.left.x * (spec.offset - spec.halfWidth);
        const double rz = s.p.z + s.left.z * (spec.offset - spec.halfWidth);
        const float v = static_cast<float>(s.s) * invRepeat;
        mesh.vertices.push_back(makeVertex(lx, spec.y, lz, 0.f, v));
        mesh.vertices.push_back(makeVertex(rx, spec.y, rz, 1.f, v));
    }
    for (std::size_t i = 0; i + 1 < st.size(); ++i) {
        const uint32_t li = static_cast<uint32_t>(2 * i);
        const uint32_t ri = li + 1;
        const uint32_t ln = li + 2;
        const uint32_t rn = li + 3;
        const uint32_t tris[6] = {li, ln, ri, ln, rn, ri};
        mesh.indices.insert(mesh.indices.end(), tris, tris + 6);
    }
    return mesh;
}

/// Cubetti della griglia di partenza: `count` rettangoli ruotati secondo la
/// tangente, sfalsati di lato a scacchiera, a partire da `sStart`.
inline ks::engine::terrain::TerrainMeshData gridSlots(const std::vector<Station>& st,
                                                      double sStart, int count, double spacing,
                                                      double lateral, double boxWidth,
                                                      double boxLength, double y) {
    ks::engine::terrain::TerrainMeshData mesh;
    if (st.size() < 2 || count <= 0 || boxWidth <= 0.0 || boxLength <= 0.0) return mesh;

    std::size_t cursor = 0;
    const double stagger = 0.6; // mezzo passo fra le due colonne
    for (int k = 0; k < count; ++k) {
        const double s = sStart + k * spacing;
        while (cursor + 1 < st.size() && st[cursor + 1].s <= s) ++cursor;
        const Station& here = st[cursor];
        const double off = lateral + ((k % 2) ? stagger : -stagger);
        const double cx = here.p.x + here.left.x * off + here.tangent.x * (s - here.s);
        const double cz = here.p.z + here.left.z * off + here.tangent.z * (s - here.s);

        const double hw = 0.5 * boxWidth;
        const double hl = 0.5 * boxLength;
        // a, b, d, e nell'ordine che da' normale +Y (vedi convenzione in cima).
        const double ax = cx - here.left.x * hw - here.tangent.x * hl;
        const double az = cz - here.left.z * hw - here.tangent.z * hl;
        const double bx = cx - here.left.x * hw + here.tangent.x * hl;
        const double bz = cz - here.left.z * hw + here.tangent.z * hl;
        const double dx = cx + here.left.x * hw - here.tangent.x * hl;
        const double dz = cz + here.left.z * hw - here.tangent.z * hl;
        const double ex = cx + here.left.x * hw + here.tangent.x * hl;
        const double ez = cz + here.left.z * hw + here.tangent.z * hl;

        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(makeVertex(ax, y, az, 0.f, 0.f));
        mesh.vertices.push_back(makeVertex(bx, y, bz, 0.f, 1.f));
        mesh.vertices.push_back(makeVertex(dx, y, dz, 1.f, 0.f));
        mesh.vertices.push_back(makeVertex(ex, y, ez, 1.f, 1.f));
        const uint32_t tris[6] = {base + 0, base + 2, base + 1, base + 2, base + 3, base + 1};
        mesh.indices.insert(mesh.indices.end(), tris, tris + 6);
    }
    return mesh;
}

} // namespace ks::geo
