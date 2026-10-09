/**
 * Roadmap 2.3 — materials.txt association made testable:
 *  - MaterialCache::load() parses the exact row format Kn5Baker::
 *    writeMaterialsTxt() emits (mesh \t albedo \t roughness \t metalness
 *    \t normal), including CRLF files from a Windows bake;
 *  - brief P1: the roughness/metalness cells are dual-typed — a finite
 *    number stays the scalar, a dotted non-numeric cell is a map texture
 *    name (and the scalar becomes the identity 1.0 the shaders multiply
 *    by), dotless garbage keeps the per-field default so old bakes behave
 *    exactly as before;
 *  - degradation: missing file = load() false + nullptr lookups (the scene
 *    still renders with defaults), empty file = valid table with 0 entries,
 *    malformed rows are skipped and counted, bad/out-of-range PBR values
 *    fall back per field and clamp to [0,1];
 *  - lookup is exact per mesh name, duplicates keep the last row;
 *  - resolveTexturePath()/sanitizeTextureFileName() mirror the baker's
 *    sanitizeFileName() so a raw KN5 reference ("textures/BODY.dds") lands
 *    on the sanitized file the baker wrote into <cache>/textures/.
 */
#include "KsTest.h"
#include "simulator/MaterialCache.h"

#include <cstdio>
#include <fstream>
#include <string>

using ks::sim::MaterialCache;
using ks::sim::MeshMaterial;

namespace {

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
}

} // namespace

int main() {
    const std::string tmp = "material_cache_test_tmp.txt";

    // --- Full row as the baker writes it -----------------------------------
    writeFile(tmp,
              "# mesh\talbedo\troughness\tmetalness\tnormal\n"
              "car_body\tBODY.dds\t0.35\t0\tBODY_n.dds\n");
    MaterialCache cache;
    KS_CHECK(cache.load(tmp));
    KS_CHECK(cache.size() == 1);
    KS_CHECK(cache.skippedRows() == 0);
    const MeshMaterial* body = cache.find("car_body");
    KS_CHECK(body != nullptr);
    if (body) {
        KS_CHECK(body->albedo == "BODY.dds");
        KS_CHECK(body->normal == "BODY_n.dds");
        KS_CHECK_NEAR(body->roughness, 0.35f, 1e-6f);
        KS_CHECK_NEAR(body->metalness, 0.0f, 1e-6f);
        KS_CHECK(body->authored);
    }
    KS_CHECK(cache.find("car_body_2") == nullptr); // exact names only
    KS_CHECK(cache.find("missing") == nullptr);

    // --- CRLF bake, empty texture fields, defaults for bare rows ------------
    writeFile(tmp,
              "trim_mesh\t\t0.5\t0.25\t\r\n"   // no textures at all
              "bare_mesh\r\n"                   // name only: pure defaults
              "\r\n"                            // blank line
              "# another comment\r\n");         // comment
    KS_CHECK(cache.load(tmp));                  // replaces the previous table
    KS_CHECK(cache.size() == 2);
    KS_CHECK(cache.skippedRows() == 0);
    const MeshMaterial* trim = cache.find("trim_mesh");
    KS_CHECK(trim != nullptr);
    if (trim) {
        KS_CHECK(trim->albedo.empty());
        KS_CHECK(trim->normal.empty());
        KS_CHECK_NEAR(trim->roughness, 0.5f, 1e-6f);
        KS_CHECK_NEAR(trim->metalness, 0.25f, 1e-6f);
    }
    const MeshMaterial* bare = cache.find("bare_mesh");
    KS_CHECK(bare != nullptr);
    if (bare) {
        KS_CHECK_NEAR(bare->roughness, 0.35f, 1e-6f); // heuristic default
        KS_CHECK_NEAR(bare->metalness, 0.0f, 1e-6f);
        KS_CHECK(bare->albedo.empty());
    }

    // --- Malformed rows are skipped, not fatal ------------------------------
    writeFile(tmp,
              "good\tG.dds\t0.4\t0\t\n"
              "\toverlong\tname\trow\twith\textra\tfields\n" // >5 fields
              "\tnameless_row\t0.1\t0\t\n"                   // empty name
              "badnums\tB.dds\tNOTAFLOAT\tnan\t\n"           // NaN kept out
              "wide\tW.dds\t7.5\t-2\t\n");                   // out of range
    KS_CHECK(cache.load(tmp));
    KS_CHECK(cache.size() == 3); // good, badnums, wide survive
    KS_CHECK(cache.skippedRows() == 2);
    KS_CHECK(cache.find("good") != nullptr);
    KS_CHECK(cache.find("nameless_row") == nullptr);
    KS_CHECK(cache.find("overlong") == nullptr);
    const MeshMaterial* bad = cache.find("badnums");
    KS_CHECK(bad != nullptr);
    if (bad) {
        KS_CHECK_NEAR(bad->roughness, 0.35f, 1e-6f); // NaN → default, not NaN
        KS_CHECK_NEAR(bad->metalness, 0.0f, 1e-6f);
        // Dotless garbage is NOT a map name (only dotted cells are): the
        // legacy per-field fallback must keep holding for old bakes.
        KS_CHECK(bad->roughnessMap.empty());
        KS_CHECK(bad->metalnessMap.empty());
    }
    const MeshMaterial* wide = cache.find("wide");
    KS_CHECK(wide != nullptr);
    if (wide) {
        KS_CHECK_NEAR(wide->roughness, 1.0f, 1e-6f);
        KS_CHECK_NEAR(wide->metalness, 0.0f, 1e-6f);
    }

    // --- Duplicate rows: last one wins --------------------------------------
    writeFile(tmp, "dup\tFIRST.dds\t0.1\t0\t\n"
                   "dup\tSECOND.dds\t0.9\t0\t\n");
    KS_CHECK(cache.load(tmp));
    KS_CHECK(cache.size() == 1);
    const MeshMaterial* dup = cache.find("dup");
    KS_CHECK(dup != nullptr);
    if (dup) KS_CHECK(dup->albedo == "SECOND.dds");

    // --- Brief P1: dual-typed roughness/metalness cells --------------------
    writeFile(tmp,
              // Legacy scalar row: no maps anywhere (regression).
              "painted\tP.dds\t0.35\t0\tP_n.dds\n"
              // Both cells hold texture names: scalars become identity 1.0.
              "bumpy\tB.dds\tB_rough.dds\tB_metal.dds\t\n"
              // Mixed: scalar roughness, textured metalness.
              "half\tH.dds\t0.60\tH_metal.dds\t\n"
              // Numeric-looking map name: "0.5.dds" is not a full-number
              // parse, so the '.' decides it is a texture.
              "tiny\tT.dds\t0.5.dds\t0\t\n");
    KS_CHECK(cache.load(tmp));
    KS_CHECK(cache.size() == 4);
    KS_CHECK(cache.skippedRows() == 0);
    const MeshMaterial* painted = cache.find("painted");
    KS_CHECK(painted != nullptr);
    if (painted) {
        KS_CHECK(painted->roughnessMap.empty());
        KS_CHECK(painted->metalnessMap.empty());
        KS_CHECK_NEAR(painted->roughness, 0.35f, 1e-6f);
        KS_CHECK_NEAR(painted->metalness, 0.0f, 1e-6f);
    }
    const MeshMaterial* bumpy = cache.find("bumpy");
    KS_CHECK(bumpy != nullptr);
    if (bumpy) {
        KS_CHECK(bumpy->roughnessMap == "B_rough.dds");
        KS_CHECK(bumpy->metalnessMap == "B_metal.dds");
        KS_CHECK_NEAR(bumpy->roughness, 1.0f, 1e-6f); // identity multiplier
        KS_CHECK_NEAR(bumpy->metalness, 1.0f, 1e-6f);
        KS_CHECK(bumpy->albedo == "B.dds");
        KS_CHECK(bumpy->authored);
    }
    const MeshMaterial* half = cache.find("half");
    KS_CHECK(half != nullptr);
    if (half) {
        KS_CHECK(half->roughnessMap.empty());
        KS_CHECK_NEAR(half->roughness, 0.60f, 1e-6f);
        KS_CHECK(half->metalnessMap == "H_metal.dds");
        KS_CHECK_NEAR(half->metalness, 1.0f, 1e-6f);
    }
    const MeshMaterial* tiny = cache.find("tiny");
    KS_CHECK(tiny != nullptr);
    if (tiny) {
        KS_CHECK(tiny->roughnessMap == "0.5.dds");
        KS_CHECK_NEAR(tiny->roughness, 1.0f, 1e-6f);
    }

    // --- Missing / empty file ------------------------------------------------
    MaterialCache missing;
    KS_CHECK(!missing.load("material_cache_test_does_not_exist.txt"));
    KS_CHECK(missing.size() == 0);
    KS_CHECK(!missing.lastError().empty());
    KS_CHECK(missing.find("anything") == nullptr);

    writeFile(tmp, "");
    KS_CHECK(cache.load(tmp)); // readable but empty = success, 0 entries
    KS_CHECK(cache.size() == 0);
    std::remove(tmp.c_str());

    // --- Texture path resolution mirrors the baker --------------------------
    // Same substitution as Kn5Baker::sanitizeFileName(): \ / : * ? " < > |
    // become '_', because the baker wrote the DDS under this name.
    KS_CHECK(MaterialCache::sanitizeTextureFileName("BODY.dds") == "BODY.dds");
    KS_CHECK(MaterialCache::sanitizeTextureFileName("textures/BODY.dds") ==
             "textures_BODY.dds");
    KS_CHECK(MaterialCache::sanitizeTextureFileName("a\\b:c*d?e") == "a_b_c_d_e");
    KS_CHECK(MaterialCache::sanitizeTextureFileName("") == "unnamed");
    KS_CHECK(MaterialCache::resolveTexturePath("cache/textures", "BODY.dds") ==
             "cache/textures/BODY.dds");
    KS_CHECK(MaterialCache::resolveTexturePath("cache/textures", "textures/BODY.dds") ==
             "cache/textures/textures_BODY.dds");
    KS_CHECK(MaterialCache::resolveTexturePath("cache/textures", "").empty());

    return KS_TEST_RESULT("material_cache_test");
}
