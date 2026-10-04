#include "KsTest.h"
#include "engine/AI/NavMesh/NavMesh.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ks;
using namespace ks::ai;

namespace {

// Two triangles with +Y normal (CCW seen from above) covering [x0,x1]x[z0,z1].
void addFloor(std::vector<NavTriangle>& geo, float x0, float x1, float z0, float z1, float y)
{
    const NavPoint a{x0, y, z0};
    const NavPoint b{x0, y, z1};
    const NavPoint c{x1, y, z1};
    const NavPoint d{x1, y, z0};
    geo.push_back({a, b, c});
    geo.push_back({a, c, d});
}

// Vertical quad at x = wx spanning [z0,z1], y in [y0,y1]. Projects to a
// line on XZ: cells within agentRadius of it become obstacles.
void addWall(std::vector<NavTriangle>& geo, float wx, float z0, float z1, float y0, float y1)
{
    const NavPoint a{wx, y0, z0};
    const NavPoint b{wx, y1, z0};
    const NavPoint c{wx, y1, z1};
    const NavPoint d{wx, y0, z1};
    geo.push_back({a, b, c});
    geo.push_back({a, c, d});
}

// Ramp in [x0,x1]x[z0,z1] rising from y0 (at x0) to y1 (at x1).
void addRamp(std::vector<NavTriangle>& geo, float x0, float x1, float z0, float z1,
             float y0, float y1)
{
    const NavPoint a{x0, y0, z0};
    const NavPoint b{x0, y0, z1};
    const NavPoint c{x1, y1, z1};
    const NavPoint d{x1, y1, z0};
    geo.push_back({a, b, c});
    geo.push_back({a, c, d});
}

float pathLength(const std::vector<NavPoint>& path)
{
    float len = 0.0f;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const float dx = path[i].x - path[i - 1].x;
        const float dz = path[i].z - path[i - 1].z;
        len += std::sqrt(dx * dx + dz * dz);
    }
    return len;
}

bool samePath(const std::vector<NavPoint>& a, const std::vector<NavPoint>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i].x - b[i].x) > 1e-6f) return false;
        if (std::fabs(a[i].y - b[i].y) > 1e-6f) return false;
        if (std::fabs(a[i].z - b[i].z) > 1e-6f) return false;
    }
    return true;
}

} // namespace

int main() {
    const NavBuildParams params; // cell 0.5, slope 45, radius 0.4, step 0.4

    // ---- Empty / invalid builds. ----
    {
        NavMesh nm;
        KS_CHECK(!nm.build({}, params));
        KS_CHECK(nm.empty());
        KS_CHECK(nm.findPath({0, 0, 0}, {1, 0, 1}).empty());
        KS_CHECK(!nm.isWalkable({0, 0, 0}));
        KS_CHECK(!nm.nearestWalkable({0, 0, 0}, 5.0f).has_value());
        KS_CHECK(!nm.load("definitely_missing_navmesh.nav"));

        NavBuildParams bad = params;
        bad.cellSize = 0.0f;
        KS_CHECK(!nm.build({{{0, 0, 0}, {0, 0, 1}, {1, 0, 1}}}, bad));
        bad = params;
        bad.maxSlopeDeg = 90.0f;
        KS_CHECK(!nm.build({{{0, 0, 0}, {0, 0, 1}, {1, 0, 1}}}, bad));
        KS_CHECK(!nm.save("should_not_exist.nav"));
    }

    // ---- Flat floor: walkability + straight path. ----
    {
        std::vector<NavTriangle> geo;
        addFloor(geo, 0.0f, 20.0f, 0.0f, 20.0f, 0.0f);
        NavMesh nm;
        KS_CHECK(nm.build(geo, params));
        KS_CHECK(!nm.empty());
        KS_CHECK(nm.isWalkable({10, 0, 10}));
        KS_CHECK(nm.isWalkable({1, 0, 1}));
        KS_CHECK(!nm.isWalkable({-5, 0, 10})); // outside the grid
        KS_CHECK(!nm.isWalkable({50, 0, 10}));

        const auto path = nm.findPath({1, 0, 1}, {19, 0, 19});
        KS_CHECK(!path.empty());
        if (!path.empty()) {
            // Exact endpoints when both cells are walkable.
            KS_CHECK_NEAR(path.front().x, 1.0f, 1e-6);
            KS_CHECK_NEAR(path.front().z, 1.0f, 1e-6);
            KS_CHECK_NEAR(path.back().x, 19.0f, 1e-6);
            KS_CHECK_NEAR(path.back().z, 19.0f, 1e-6);
            // String pulling collapses the open floor to one segment.
            KS_CHECK(path.size() <= 3);
            for (const NavPoint& p : path) KS_CHECK(nm.isWalkable(p));
            // Deterministic: second run identical.
            KS_CHECK(samePath(path, nm.findPath({1, 0, 1}, {19, 0, 19})));
        }

        // Same point: single-element path, no crash.
        const auto same = nm.findPath({5, 0, 5}, {5, 0, 5});
        KS_CHECK(same.size() == 1);

        // Goal beyond snap radius (>10 m outside the grid edge): no path.
        KS_CHECK(nm.findPath({5, 0, 5}, {60, 0, 5}).empty());
    }

    // ---- Wall with a 2 m gap: detour. ----
    {
        std::vector<NavTriangle> geo;
        addFloor(geo, 0.0f, 20.0f, 0.0f, 20.0f, 0.0f);
        addWall(geo, 10.0f, 0.0f, 7.0f, 0.0f, 3.0f);
        addWall(geo, 10.0f, 9.0f, 20.0f, 0.0f, 3.0f);
        NavMesh nm;
        KS_CHECK(nm.build(geo, params));

        KS_CHECK(!nm.isWalkable({10, 0, 3}));   // inside the wall band
        KS_CHECK(!nm.isWalkable({10, 0, 15}));  // ditto beyond the gap
        KS_CHECK(nm.isWalkable({10, 0, 8}));    // gap is open
        KS_CHECK(nm.isWalkable({9.5f, 0, 3}));  // cell centre 9.35: 0.65 m out
        KS_CHECK(!nm.isWalkable({9.7f, 0, 3})); // cell centre 9.85: <=0.4 m, inflated

        const auto path = nm.findPath({5, 0, 3}, {15, 0, 3});
        KS_CHECK(!path.empty());
        if (!path.empty()) {
            for (const NavPoint& p : path) {
                KS_CHECK(nm.isWalkable(p));
                // Never inside the inflated wall footprint.
                if (p.x > 9.4f && p.x < 10.6f) {
                    KS_CHECK(p.z > 6.5f && p.z < 9.5f);
                }
            }
            // A detour must be longer than the 10 m straight line.
            KS_CHECK(pathLength(path) > 12.0f);
            KS_CHECK_NEAR(path.front().x, 5.0f, 1e-6);
            KS_CHECK_NEAR(path.back().x, 15.0f, 1e-6);
        }
    }

    // ---- Sealed wall: no path. ----
    {
        std::vector<NavTriangle> geo;
        addFloor(geo, 0.0f, 20.0f, 0.0f, 20.0f, 0.0f);
        addWall(geo, 10.0f, 0.0f, 20.0f, 0.0f, 3.0f);
        NavMesh nm;
        KS_CHECK(nm.build(geo, params));
        KS_CHECK(!nm.isWalkable({10, 0, 10}));
        KS_CHECK(nm.findPath({5, 0, 10}, {15, 0, 10}).empty());
    }

    // ---- Gap narrower than 2*agentRadius: sealed by inflation. ----
    {
        std::vector<NavTriangle> geo;
        addFloor(geo, 0.0f, 20.0f, 0.0f, 20.0f, 0.0f);
        addWall(geo, 10.0f, 0.0f, 9.9f, 0.0f, 3.0f);
        addWall(geo, 10.0f, 10.1f, 20.0f, 0.0f, 3.0f);
        NavMesh nm;
        KS_CHECK(nm.build(geo, params));
        KS_CHECK(!nm.isWalkable({10, 0, 10})); // 0.2 m gap, both sides +0.4
        KS_CHECK(nm.findPath({5, 0, 10}, {15, 0, 10}).empty());
    }

    // ---- Slope classification: 30 deg ramp walkable, 63 deg is an obstacle. ----
    {
        std::vector<NavTriangle> geo;
        addFloor(geo, 0.0f, 10.0f, 0.0f, 20.0f, 0.0f);
        addRamp(geo, 10.0f, 14.0f, 0.0f, 20.0f, 0.0f, 2.31f); // ~30 deg
        addFloor(geo, 14.0f, 20.0f, 0.0f, 20.0f, 2.31f);
        NavMesh nm;
        KS_CHECK(nm.build(geo, params));
        KS_CHECK(nm.isWalkable({5, 0, 10}));
        KS_CHECK(nm.isWalkable({12, 0, 10})); // on the ramp
        const auto path = nm.findPath({5, 0, 10}, {18, 0, 10});
        KS_CHECK(!path.empty());
        for (const NavPoint& p : path) KS_CHECK(nm.isWalkable(p));
        if (!path.empty()) KS_CHECK_NEAR(path.back().x, 18.0f, 1e-6);
    }
    {
        std::vector<NavTriangle> geo;
        addFloor(geo, 0.0f, 10.0f, 0.0f, 20.0f, 0.0f);
        addRamp(geo, 10.0f, 14.0f, 0.0f, 20.0f, 0.0f, 8.0f); // ~63 deg
        addFloor(geo, 14.0f, 20.0f, 0.0f, 20.0f, 0.0f);
        NavMesh nm;
        KS_CHECK(nm.build(geo, params));
        KS_CHECK(!nm.isWalkable({12, 0, 10})); // too steep: obstacle band
        KS_CHECK(nm.findPath({5, 0, 10}, {18, 0, 10}).empty());
        KS_CHECK(nm.isWalkable({5, 0, 10}));  // left side still open
        KS_CHECK(nm.isWalkable({18, 0, 10})); // right side isolated island
    }

    // ---- Step height: 0.3 m hop allowed, 0.6 m ledge blocks. ----
    {
        std::vector<NavTriangle> geo;
        addFloor(geo, 0.0f, 10.0f, 0.0f, 20.0f, 0.0f);
        addFloor(geo, 10.0f, 20.0f, 0.0f, 20.0f, 0.3f);
        NavMesh nm;
        KS_CHECK(nm.build(geo, params));
        const auto path = nm.findPath({5, 0, 10}, {15, 0, 10});
        KS_CHECK(!path.empty());
        for (const NavPoint& p : path) KS_CHECK(nm.isWalkable(p));
    }
    {
        std::vector<NavTriangle> geo;
        addFloor(geo, 0.0f, 10.0f, 0.0f, 20.0f, 0.0f);
        addFloor(geo, 10.0f, 20.0f, 0.0f, 20.0f, 0.6f);
        NavMesh nm;
        KS_CHECK(nm.build(geo, params));
        KS_CHECK(nm.isWalkable({5, 0, 10}));
        KS_CHECK(nm.isWalkable({15, 0, 10}));
        KS_CHECK(nm.findPath({5, 0, 10}, {15, 0, 10}).empty());
    }

    // ---- nearestWalkable: snap and give-up. ----
    {
        std::vector<NavTriangle> geo;
        addFloor(geo, 0.0f, 20.0f, 0.0f, 20.0f, 0.0f);
        addWall(geo, 10.0f, 0.0f, 7.0f, 0.0f, 3.0f);
        NavMesh nm;
        KS_CHECK(nm.build(geo, params));

        const auto inside = nm.nearestWalkable({5, 0, 5}, 10.0f);
        KS_CHECK(inside.has_value());
        if (inside) {
            KS_CHECK_NEAR(inside->x, 5.0f, 1e-6); // already walkable: returned as-is
            KS_CHECK(nm.isWalkable(*inside));
        }

        const auto on_wall = nm.nearestWalkable({10, 0, 3}, 5.0f);
        KS_CHECK(on_wall.has_value());
        if (on_wall) {
            KS_CHECK(nm.isWalkable(*on_wall));
            const float dx = on_wall->x - 10.0f;
            const float dz = on_wall->z - 3.0f;
            KS_CHECK(std::sqrt(dx * dx + dz * dz) > 0.4f); // off the wall band
        }

        KS_CHECK(!nm.nearestWalkable({100, 0, 100}, 5.0f).has_value());
        KS_CHECK(!nm.nearestWalkable({5, 0, 5}, -1.0f).has_value());
    }

    // ---- save / load roundtrip. ----
    {
        std::vector<NavTriangle> geo;
        addFloor(geo, 0.0f, 20.0f, 0.0f, 20.0f, 0.0f);
        addWall(geo, 10.0f, 0.0f, 7.0f, 0.0f, 3.0f);
        addWall(geo, 10.0f, 9.0f, 20.0f, 0.0f, 3.0f);
        NavMesh nm;
        KS_CHECK(nm.build(geo, params));
        const auto path = nm.findPath({5, 0, 3}, {15, 0, 3});
        KS_CHECK(!path.empty());

        const std::string file = "navmesh_roundtrip.nav";
        KS_CHECK(nm.save(file));

        NavMesh loaded;
        KS_CHECK(loaded.load(file));
        KS_CHECK(!loaded.empty());
        KS_CHECK(loaded.width() == nm.width());
        KS_CHECK(loaded.height() == nm.height());
        KS_CHECK_NEAR(loaded.cellSize(), nm.cellSize(), 1e-6f);
        KS_CHECK(loaded.isWalkable({10, 0, 8}));
        KS_CHECK(!loaded.isWalkable({10, 0, 3}));
        KS_CHECK(samePath(path, loaded.findPath({5, 0, 3}, {15, 0, 3})));

        // Truncated / corrupt payloads are rejected, mesh stays empty.
        const char junk[] = "KSNVjunkjunkjunk";
        {
            std::FILE* f = std::fopen("navmesh_corrupt.nav", "wb");
            if (f) {
                std::fwrite(junk, 1, sizeof(junk), f);
                std::fclose(f);
            }
        }
        NavMesh corrupt;
        KS_CHECK(!corrupt.load("navmesh_corrupt.nav"));
        KS_CHECK(corrupt.empty());
        std::remove(file.c_str());
        std::remove("navmesh_corrupt.nav");
    }

    return KS_TEST_RESULT("navmesh_test");
}
