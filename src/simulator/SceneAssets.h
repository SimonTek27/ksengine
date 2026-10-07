#pragma once

// Roadmap 1.2 / GAP P2.1 — stable scene-asset helpers for track/car load.
//
// - findBakedManifestDir(): locates the offline kn5baker output for a
//   content folder (<dir>/baked/manifest.txt preferred, <dir>/manifest.txt
//   accepted because kn5baker's output directory is user-chosen). Empty
//   string = "nothing baked yet"; callers keep the current scene instead of
//   failing, which is what makes switching stable.
// - makeSolidBox(): the "placeholder solido" for a car with no baked
//   visual — a lit, correctly wound box in car dimensions so the vehicle
//   is always visible after load.
//
// Deliberately Vulkan-free (no NativeMesh/NativeRenderer include) so this
// header compiles in the Qt-free test targets without the SDK. The vertex
// layout matches NativeVertex exactly (12 floats: p, n, uv, rgba);
// SimulationLoop static_asserts the sizes before memcpying.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ks::sim {

struct SolidBoxVertex {
    float px = 0, py = 0, pz = 0;
    float nx = 0, ny = 0, nz = 0;
    float u = 0, v = 0;
    float r = 1, g = 1, b = 1, a = 1;
};
static_assert(sizeof(SolidBoxVertex) == sizeof(float) * 12,
              "SolidBoxVertex must stay layout-compatible with NativeVertex");

struct SolidBox {
    std::vector<SolidBoxVertex> vertices;
    std::vector<uint32_t> indices;
};

// Axis-aligned box centred on X, spanning [yMin, yMax] and [zMin, zMax]
// (metres, vehicle-local: car forward is the Z axis). Six faces wound CCW
// as seen from outside — cross(v1-v0, v2-v0) points along the face normal —
// which is what the forward pipeline's VK_CULL_MODE_BACK_BIT +
// VK_FRONT_FACE_COUNTER_CLOCKWISE requires for the faces to be visible.
inline SolidBox makeSolidBox(float xHalf, float yMin, float yMax,
                             float zMin, float zMax,
                             float r = 0.76f, float g = 0.79f, float b = 0.85f) {
    struct Face {
        float n[3];
        float c[4][3];
    };
    const Face faces[] = {
        {{1, 0, 0}, {{xHalf, yMin, zMin}, {xHalf, yMax, zMin},
                    {xHalf, yMax, zMax}, {xHalf, yMin, zMax}}},
        {{-1, 0, 0}, {{-xHalf, yMin, zMax}, {-xHalf, yMax, zMax},
                     {-xHalf, yMax, zMin}, {-xHalf, yMin, zMin}}},
        {{0, 1, 0}, {{-xHalf, yMax, zMin}, {-xHalf, yMax, zMax},
                    {xHalf, yMax, zMax}, {xHalf, yMax, zMin}}},
        {{0, -1, 0}, {{-xHalf, yMin, zMax}, {-xHalf, yMin, zMin},
                     {xHalf, yMin, zMin}, {xHalf, yMin, zMax}}},
        {{0, 0, 1}, {{-xHalf, yMin, zMax}, {xHalf, yMin, zMax},
                    {xHalf, yMax, zMax}, {-xHalf, yMax, zMax}}},
        {{0, 0, -1}, {{xHalf, yMin, zMin}, {-xHalf, yMin, zMin},
                     {-xHalf, yMax, zMin}, {xHalf, yMax, zMin}}},
    };
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    SolidBox box;
    box.vertices.reserve(24);
    box.indices.reserve(36);
    for (const Face& f : faces) {
        const uint32_t base = static_cast<uint32_t>(box.vertices.size());
        for (int i = 0; i < 4; ++i) {
            SolidBoxVertex sv;
            sv.px = f.c[i][0];
            sv.py = f.c[i][1];
            sv.pz = f.c[i][2];
            sv.nx = f.n[0];
            sv.ny = f.n[1];
            sv.nz = f.n[2];
            sv.u = uv[i][0];
            sv.v = uv[i][1];
            sv.r = r;
            sv.g = g;
            sv.b = b;
            sv.a = 1.0f;
            box.vertices.push_back(sv);
        }
        for (const uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            box.indices.push_back(base + i);
    }
    return box;
}

// Returns the directory holding manifest.txt for a content folder, or ""
// when nothing is baked. Prefers <dir>/baked over a manifest directly in
// <dir> (kn5baker's output dir is user-chosen; both layouts ship in the
// wild).
inline std::string findBakedManifestDir(const std::string& contentDir) {
    namespace fs = std::filesystem;
    if (contentDir.empty()) return std::string();
    std::error_code ec;
    const fs::path baked = fs::path(contentDir) / "baked";
    if (fs::is_regular_file(baked / "manifest.txt", ec)) return baked.string();
    if (fs::is_regular_file(fs::path(contentDir) / "manifest.txt", ec))
        return contentDir;
    return std::string();
}

} // namespace ks::sim
