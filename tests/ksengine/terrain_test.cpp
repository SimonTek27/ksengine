// Roadmap ksengine-vs-cryengine P2 - trackside terrain, CPU half.
//
// TerrainMesh (grid -> triangles with normals/UVs, NMSH on disk) and the
// fBm heightmap that feeds it are the parts that need no GPU; the pixel
// side of the same feature is checked by test_renderer.
#include "KsTest.h"
#include "engine/terrain/TerrainMesh.h"
#include "engine/terrain/TerrainHeightmap.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

using namespace ks;
using namespace ks::engine::terrain;

namespace {

static_assert(sizeof(TerrainVertex) == 48, "TerrainVertex must match the 48-byte NativeVertex layout");

float heightAt(const TerrainMeshData& m, int x, int z, int gridW) {
    return m.vertices[static_cast<std::size_t>(z) * static_cast<std::size_t>(gridW) + static_cast<std::size_t>(x)].py;
}

void testMeshGeneration() {
    std::vector<float> flat(4 * 3, 1.5f);
    TerrainMeshData m = generateTerrainMesh(flat, 4, 3, 30.0f, 20.0f, 10.0f);
    KS_CHECK(m.vertices.size() == 12);
    KS_CHECK(m.indices.size() == 6u * 3u * 2u);

    for (const TerrainVertex& v : m.vertices) {
        KS_CHECK_NEAR(v.py, 1.5f, 1e-6);
        KS_CHECK_NEAR(v.nx, 0.0f, 1e-5);
        KS_CHECK_NEAR(v.ny, 1.0f, 1e-5);
        KS_CHECK_NEAR(v.nz, 0.0f, 1e-5);
        KS_CHECK(v.px >= 0.0f && v.px <= 30.0f + 1e-4f);
        KS_CHECK(v.pz >= 0.0f && v.pz <= 20.0f + 1e-4f);
        KS_CHECK_NEAR(v.u, v.px / 10.0f, 1e-5);
        KS_CHECK_NEAR(v.v, v.pz / 10.0f, 1e-5);
    }
    KS_CHECK_NEAR(m.vertices[0].px, 0.0f, 1e-6);
    KS_CHECK_NEAR(m.vertices[1].px, 10.0f, 1e-6);
    KS_CHECK_NEAR(m.vertices[3].px, 30.0f, 1e-6);

    const TerrainVertex& a = m.vertices[m.indices[0]];
    const TerrainVertex& b = m.vertices[m.indices[1]];
    const TerrainVertex& c = m.vertices[m.indices[2]];
    const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
    const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
    const float ny = uz * vx - ux * vz;
    KS_CHECK(ny > 0.0f); // counter-clockwise seen from +Y

    for (std::uint32_t i : m.indices) KS_CHECK(i < m.vertices.size());
}

void testSlopeNormals() {
    std::vector<float> ramp(4 * 4);
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 4; ++x)
            ramp[static_cast<std::size_t>(z) * 4 + static_cast<std::size_t>(x)] = static_cast<float>(x);
    TerrainMeshData m = generateTerrainMesh(ramp, 4, 4, 30.0f, 30.0f, 10.0f);
    KS_CHECK(m.vertices.size() == 16);
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            const TerrainVertex& v = m.vertices[static_cast<std::size_t>(z) * 4 + static_cast<std::size_t>(x)];
            KS_CHECK(v.nx < 0.0f);   // rising in +x, so the normal leans to -x
            KS_CHECK(v.ny > 0.0f);
            KS_CHECK_NEAR(v.nz, 0.0f, 1e-5);
        }
    }
    KS_CHECK_NEAR(heightAt(m, 3, 2, 4), 3.0f, 1e-6);
}

void testMalformedInput() {
    KS_CHECK(generateTerrainMesh({}, 4, 4, 10.0f, 10.0f).vertices.empty());
    KS_CHECK(generateTerrainMesh(std::vector<float>(16, 0.0f), 1, 16, 10.0f, 10.0f).vertices.empty());
    KS_CHECK(generateTerrainMesh(std::vector<float>(15, 0.0f), 4, 4, 10.0f, 10.0f).vertices.empty());
}

void testLod() {
    std::vector<float> h(5 * 5);
    for (int i = 0; i < 25; ++i) h[static_cast<std::size_t>(i)] = static_cast<float>(i);
    TerrainMeshData lod = generateTerrainMeshLOD(h, 5, 5, 40.0f, 40.0f, 2);
    KS_CHECK(lod.vertices.size() == 9u); // (5-1)/2 + 1 = 3 per axis
    KS_CHECK(lod.indices.size() == 6u * 2u * 2u);
    KS_CHECK_NEAR(lod.vertices[0].px, 0.0f, 1e-6);
    KS_CHECK_NEAR(lod.vertices[2].px, 40.0f, 1e-6);
    KS_CHECK_NEAR(heightAt(lod, 1, 1, 3), h[2 * 5 + 2], 1e-6);
    TerrainMeshData same = generateTerrainMeshLOD(h, 5, 5, 40.0f, 40.0f, 1);
    KS_CHECK(same.vertices.size() == 25u);
}

void testNmsRoundtrip() {
    std::vector<float> h(4 * 4, 0.0f);
    h[5] = 2.5f;
    TerrainMeshData m = generateTerrainMesh(h, 4, 4, 30.0f, 30.0f, 10.0f);
    const char* path = "terrain_test_roundtrip.nmsh";
    KS_CHECK(writeTerrainNMSH(path, m));

    std::ifstream f(path, std::ios::binary);
    KS_CHECK(f.is_open());
    char magic[4] = {};
    std::uint32_t vCount = 0, iCount = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&vCount), 4);
    f.read(reinterpret_cast<char*>(&iCount), 4);
    KS_CHECK(std::memcmp(magic, "NMSH", 4) == 0);
    KS_CHECK(vCount == m.vertices.size());
    KS_CHECK(iCount == m.indices.size());

    std::vector<char> verts(sizeof(TerrainVertex) * m.vertices.size());
    std::vector<char> inds(sizeof(std::uint32_t) * m.indices.size());
    f.read(verts.data(), static_cast<std::streamsize>(verts.size()));
    f.read(inds.data(), static_cast<std::streamsize>(inds.size()));
    KS_CHECK(f.gcount() == static_cast<std::streamsize>(inds.size()));
    KS_CHECK(f.peek() == EOF);
    KS_CHECK(std::memcmp(verts.data(), m.vertices.data(), verts.size()) == 0);
    KS_CHECK(std::memcmp(inds.data(), m.indices.data(), inds.size()) == 0);
    f.close();
    std::remove(path);

    KS_CHECK(!writeTerrainNMSH("terrain_test_missing_dir/nope/x.nmsh", m));
}

void testHeightmap() {
    FbmHeightmapParams p;
    p.seed = 42;
    p.amplitude = 8.0f;
    p.baseHeight = -1.0f;
    p.frequency = 2.5f;
    p.octaves = 5;

    std::vector<float> a = generateFbmHeightmap(33, 17, p);
    std::vector<float> b = generateFbmHeightmap(33, 17, p);
    KS_CHECK(a.size() == 33u * 17u);
    KS_CHECK(a == b); // bit-identical for the same seed

    p.seed = 43;
    std::vector<float> other = generateFbmHeightmap(33, 17, p);
    KS_CHECK(a != other);
    p.seed = 42;

    bool variation = false;
    for (float h : a) {
        KS_CHECK(h >= p.baseHeight - p.amplitude - 1e-4f);
        KS_CHECK(h <= p.baseHeight + p.amplitude + 1e-4f);
        if (h != a.front()) variation = true;
    }
    KS_CHECK(variation);

    p.amplitude = 0.0f;
    for (float h : generateFbmHeightmap(8, 8, p)) KS_CHECK_NEAR(h, p.baseHeight, 1e-6);
    p.amplitude = 8.0f;

    p.octaves = 0;
    for (float h : generateFbmHeightmap(8, 8, p)) KS_CHECK_NEAR(h, p.baseHeight, 1e-6);
    p.octaves = 5;

    KS_CHECK(generateFbmHeightmap(0, 4, p).empty());
    KS_CHECK(generateFbmHeightmap(4, -1, p).empty());

    TerrainMeshData m = generateTerrainMesh(generateFbmHeightmap(65, 65, p), 65, 65, 800.0f, 800.0f, 25.0f);
    KS_CHECK(m.vertices.size() == 65u * 65u);
    KS_CHECK(m.indices.size() == 6u * 64u * 64u);
}

} // namespace

int main() {
    testMeshGeneration();
    testSlopeNormals();
    testMalformedInput();
    testLod();
    testNmsRoundtrip();
    testHeightmap();
    return KS_TEST_RESULT("terrain_test");
}
