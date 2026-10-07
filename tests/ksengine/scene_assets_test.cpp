/**
 * Roadmap 1.2 / GAP P2.1 — stable track/car load helpers (SceneAssets.h):
 * baked-manifest discovery (with its fallbacks) and the solid placeholder
 * box, whose CCW-from-outside winding is what the renderer's backface
 * culling requires to actually show the car.
 */
#include "KsTest.h"
#include "simulator/SceneAssets.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using ks::sim::findBakedManifestDir;
using ks::sim::makeSolidBox;
using ks::sim::SolidBox;

static void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << "car_placeholder.nmsh\n";
}

int main() {
    // No directory / missing directory -> "" (caller keeps current scene).
    KS_CHECK(findBakedManifestDir("").empty());
    KS_CHECK(findBakedManifestDir("scene_assets_test_missing").empty());

    const std::string root = "scene_assets_test_root";
    fs::remove_all(root);

    // Existing folder with nothing baked -> "".
    fs::create_directories(root);
    KS_CHECK(findBakedManifestDir(root).empty());

    // Preferred layout: <dir>/baked/manifest.txt.
    touch(fs::path(root) / "baked" / "manifest.txt");
    KS_CHECK(findBakedManifestDir(root) == (fs::path(root) / "baked").string());

    // Accepted layout: manifest directly in <dir> (kn5baker's output dir is
    // user-chosen) — and when both exist, <dir>/baked wins.
    fs::remove_all(fs::path(root) / "baked");
    touch(fs::path(root) / "manifest.txt");
    KS_CHECK(findBakedManifestDir(root) == root);
    touch(fs::path(root) / "baked" / "manifest.txt");
    KS_CHECK(findBakedManifestDir(root) == (fs::path(root) / "baked").string());
    fs::remove_all(root);

    // Solid placeholder box geometry.
    const float xHalf = 0.90f, yMin = -0.40f, yMax = 1.00f;
    const float zMin = -2.20f, zMax = 2.20f;
    const SolidBox box = makeSolidBox(xHalf, yMin, yMax, zMin, zMax, 0.5f, 0.6f, 0.7f);

    KS_CHECK(box.vertices.size() == 24); // 6 faces x 4 verts
    KS_CHECK(box.indices.size() == 36);  // 6 faces x 2 tris x 3
    if (box.vertices.empty() || box.indices.empty()) return KS_TEST_RESULT("scene_assets_test");

    int facesPerAxis[3] = {0, 0, 0};
    for (size_t i = 0; i + 2 < box.indices.size(); i += 3) {
        KS_CHECK(box.indices[i] < box.vertices.size());
        KS_CHECK(box.indices[i + 1] < box.vertices.size());
        KS_CHECK(box.indices[i + 2] < box.vertices.size());
        const auto& v0 = box.vertices[box.indices[i]];
        const auto& v1 = box.vertices[box.indices[i + 1]];
        const auto& v2 = box.vertices[box.indices[i + 2]];
        const float e1[3] = {v1.px - v0.px, v1.py - v0.py, v1.pz - v0.pz};
        const float e2[3] = {v2.px - v0.px, v2.py - v0.py, v2.pz - v0.pz};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                            e1[0] * e2[1] - e1[1] * e2[0]};
        const float area2 = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        KS_CHECK(area2 > 1e-6f); // no degenerate triangles
        // CCW as seen from outside: the RH-rule normal must point along the
        // face normal, or backface culling hides the placeholder.
        KS_CHECK(n[0] * v0.nx + n[1] * v0.ny + n[2] * v0.nz > 0.0f);
    }

    for (const auto& v : box.vertices) {
        const float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
        KS_CHECK(std::fabs(len - 1.0f) < 1e-5f); // unit normals
        KS_CHECK(std::fabs(v.px) <= xHalf + 1e-5f);
        KS_CHECK(v.py >= yMin - 1e-5f && v.py <= yMax + 1e-5f);
        KS_CHECK(v.pz >= zMin - 1e-5f && v.pz <= zMax + 1e-5f);
        KS_CHECK(v.r == 0.5f && v.g == 0.6f && v.b == 0.7f); // colour propagates
        KS_CHECK(v.a == 1.0f);
        if (std::fabs(v.nx) > 0.5f) ++facesPerAxis[0];
        if (std::fabs(v.ny) > 0.5f) ++facesPerAxis[1];
        if (std::fabs(v.nz) > 0.5f) ++facesPerAxis[2];
    }
    KS_CHECK(facesPerAxis[0] == 8); // 2 faces per axis x 4 verts
    KS_CHECK(facesPerAxis[1] == 8);
    KS_CHECK(facesPerAxis[2] == 8);

    return KS_TEST_RESULT("scene_assets_test");
}
