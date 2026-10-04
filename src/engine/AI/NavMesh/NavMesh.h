#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ks {
namespace ai {

struct NavPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// One source triangle handed to NavMesh::build(). Winding must be
// counter-clockwise seen from the walkable side: the signed face normal
// decides walkability, so a floor triangle listed upside down classifies as
// an obstacle footprint instead of ground.
struct NavTriangle {
    NavPoint a;
    NavPoint b;
    NavPoint c;
};

struct NavBuildParams {
    float cellSize = 0.5f;      // grid pitch in metres (XZ plane)
    float maxSlopeDeg = 45.0f;  // walkable limit: face normal.y >= cos(this)
    float agentRadius = 0.4f;   // keep-away distance from obstacle footprints
    float maxStepHeight = 0.4f; // largest climb between adjacent cells
};

// Grid-based navigation mesh ("nav grid"): walkable surfaces are rasterized
// onto a regular XZ grid, steep faces (walls, too-steep slopes) block their
// footprint inflated by the agent radius, paths are searched with A* over
// the cells and then string-pulled into a short collision-free polyline.
//
// Solid volumes read as obstacles through their downward/steep faces, so an
// agent never paths through a box even when its top face is flat.
//
// Single-threaded: build/save/load mutate the instance, queries are const.
class NavMesh {
public:
    NavMesh() = default;

    // Historical shared instance kept for existing call sites.
    static NavMesh& instance() { static NavMesh s; return s; }

    bool initialize() { return true; }
    void shutdown() { clear(); }

    // Rasterizes geometry into the grid. Returns false (and leaves the mesh
    // empty) for no geometry, invalid params, or an unreasonably large grid.
    bool build(const std::vector<NavTriangle>& geometry, const NavBuildParams& params);
    bool save(const std::string& path) const;
    bool load(const std::string& path);
    void clear();

    // Walkable polyline from `from` to `to`; ends are the exact query points
    // when their cells are walkable, otherwise the nearest walkable spot
    // within 10 m. Empty when an end cannot be placed or no route exists.
    std::vector<NavPoint> findPath(NavPoint from, NavPoint to) const;

    // True when the XZ cell under `p` has walkable support and is clear
    // (y is ignored; the cell's own surface height is authoritative).
    bool isWalkable(NavPoint p) const;

    // `p` itself when walkable, otherwise the walkable cell centre closest
    // to `p` within maxRadius (XZ metres); nullopt when there is none.
    std::optional<NavPoint> nearestWalkable(NavPoint p, float maxRadius) const;

    bool empty() const { return m_width <= 0 || m_height <= 0; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    float cellSize() const { return m_cellSize; }

private:
    int cellIndex(float x, float z) const;
    NavPoint cellPoint(int idx) const;
    bool lineWalkable(NavPoint a, NavPoint b) const;

    float m_cellSize = 0.5f;
    float m_originX = 0.0f;
    float m_originZ = 0.0f;
    float m_maxStepHeight = 0.4f;
    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_walkable;
    std::vector<float> m_heights;
};

} // namespace ai
} // namespace ks
