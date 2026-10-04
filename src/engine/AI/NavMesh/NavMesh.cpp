#include "AI/NavMesh/NavMesh.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <queue>

namespace ks {
namespace ai {
namespace {

constexpr std::uint32_t kNavMagic = 0x564E534Bu; // "KSNV"
constexpr std::uint32_t kNavVersion = 1;
constexpr std::size_t kMaxCells = 16u * 1024u * 1024u;
constexpr int kMaxGridDim = 65536;
constexpr float kSnapRadius = 10.0f;
constexpr float kPi = 3.14159265f;

// Barycentric coords of (px,pz) in triangle a,b,c projected on XZ.
// False only for a degenerate (zero-area) projection.
bool barycentricXZ(const NavPoint& a, const NavPoint& b, const NavPoint& c,
                   float px, float pz, float& w0, float& w1, float& w2)
{
    const float denom = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
    if (std::fabs(denom) < 1e-12f) return false;
    w0 = ((b.z - c.z) * (px - c.x) + (c.x - b.x) * (pz - c.z)) / denom;
    w1 = ((c.z - a.z) * (px - c.x) + (a.x - c.x) * (pz - c.z)) / denom;
    w2 = 1.0f - w0 - w1;
    return true;
}

// Cell centres sit on a lattice, but the +/- epsilon keeps boundary samples
// inside their triangle instead of flickering between neighbours.
bool pointInTriangleXZ(const NavPoint& a, const NavPoint& b, const NavPoint& c,
                       float px, float pz)
{
    float w0 = 0.0f, w1 = 0.0f, w2 = 0.0f;
    if (!barycentricXZ(a, b, c, px, pz, w0, w1, w2)) return false;
    constexpr float kEps = 1e-4f;
    return w0 >= -kEps && w1 >= -kEps && w2 >= -kEps;
}

float pointSegDist2XZ(float px, float pz, float ax, float az, float bx, float bz)
{
    const float ex = bx - ax;
    const float ez = bz - az;
    const float len2 = ex * ex + ez * ez;
    float t = 0.0f;
    if (len2 > 0.0f) {
        t = ((px - ax) * ex + (pz - az) * ez) / len2;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    const float dx = px - (ax + t * ex);
    const float dz = pz - (az + t * ez);
    return dx * dx + dz * dz;
}

// XZ distance between a point and a triangle projection: 0 when inside,
// otherwise distance to the closest projected edge.
float pointTriDist2XZ(float px, float pz, const NavPoint& a, const NavPoint& b,
                      const NavPoint& c)
{
    if (pointInTriangleXZ(a, b, c, px, pz)) return 0.0f;
    float d2 = pointSegDist2XZ(px, pz, a.x, a.z, b.x, b.z);
    d2 = std::min(d2, pointSegDist2XZ(px, pz, b.x, b.z, c.x, c.z));
    d2 = std::min(d2, pointSegDist2XZ(px, pz, c.x, c.z, a.x, a.z));
    return d2;
}

struct OpenNode {
    float f;
    int idx;
};

// Min-heap on f, ties broken by index so results are deterministic.
struct OpenCmp {
    bool operator()(const OpenNode& l, const OpenNode& r) const
    {
        if (l.f != r.f) return l.f > r.f;
        return l.idx > r.idx;
    }
};

bool readExact(std::ifstream& in, void* dst, std::size_t bytes)
{
    in.read(static_cast<char*>(dst), static_cast<std::streamsize>(bytes));
    return static_cast<std::size_t>(in.gcount()) == bytes;
}

} // namespace

int NavMesh::cellIndex(float x, float z) const
{
    if (empty()) return -1;
    const int cx = static_cast<int>(std::floor((x - m_originX) / m_cellSize));
    const int cz = static_cast<int>(std::floor((z - m_originZ) / m_cellSize));
    if (cx < 0 || cz < 0 || cx >= m_width || cz >= m_height) return -1;
    return cz * m_width + cx;
}

NavPoint NavMesh::cellPoint(int idx) const
{
    const int cx = idx % m_width;
    const int cz = idx / m_width;
    NavPoint p;
    p.x = m_originX + (static_cast<float>(cx) + 0.5f) * m_cellSize;
    p.z = m_originZ + (static_cast<float>(cz) + 0.5f) * m_cellSize;
    p.y = m_heights[static_cast<std::size_t>(idx)];
    return p;
}

bool NavMesh::lineWalkable(NavPoint a, NavPoint b) const
{
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    const float step = m_cellSize * 0.5f;
    const int samples = std::max(2, static_cast<int>(std::ceil(dist / step)));
    for (int i = 0; i <= samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        NavPoint s;
        s.x = a.x + dx * t;
        s.y = 0.0f;
        s.z = a.z + dz * t;
        if (!isWalkable(s)) return false;
    }
    return true;
}

void NavMesh::clear()
{
    m_cellSize = 0.5f;
    m_originX = 0.0f;
    m_originZ = 0.0f;
    m_maxStepHeight = 0.4f;
    m_width = 0;
    m_height = 0;
    m_walkable.clear();
    m_heights.clear();
}

bool NavMesh::build(const std::vector<NavTriangle>& geometry,
                    const NavBuildParams& params)
{
    clear();
    if (geometry.empty()) return false;
    if (!(params.cellSize >= 0.01f && params.cellSize <= 100.0f)) return false;
    if (!(params.maxSlopeDeg > 0.0f && params.maxSlopeDeg <= 80.0f)) return false;
    if (!(params.agentRadius >= 0.0f && params.agentRadius <= 50.0f)) return false;
    if (!(params.maxStepHeight >= 0.0f && params.maxStepHeight <= 20.0f)) return false;

    float min_x = geometry[0].a.x;
    float max_x = min_x;
    float min_z = geometry[0].a.z;
    float max_z = min_z;
    for (const NavTriangle& t : geometry) {
        const NavPoint pts[3] = {t.a, t.b, t.c};
        for (const NavPoint& p : pts) {
            min_x = std::min(min_x, p.x);
            max_x = std::max(max_x, p.x);
            min_z = std::min(min_z, p.z);
            max_z = std::max(max_z, p.z);
        }
    }

    const float pad = params.agentRadius + 2.0f * params.cellSize;
    m_originX = min_x - pad;
    m_originZ = min_z - pad;
    m_cellSize = params.cellSize;
    m_maxStepHeight = params.maxStepHeight;
    m_width = static_cast<int>(std::ceil((max_x + pad - m_originX) / m_cellSize)) + 1;
    m_height = static_cast<int>(std::ceil((max_z + pad - m_originZ) / m_cellSize)) + 1;
    if (m_width < 1) m_width = 1;
    if (m_height < 1) m_height = 1;
    if (m_width > kMaxGridDim || m_height > kMaxGridDim
        || static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) > kMaxCells) {
        clear();
        return false;
    }

    const std::size_t cells = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    m_heights.assign(cells, std::numeric_limits<float>::infinity());
    std::vector<std::uint8_t> blocked(cells, 0);

    const float cos_slope = std::cos(params.maxSlopeDeg * kPi / 180.0f);
    const float radius2 = params.agentRadius * params.agentRadius;

    for (const NavTriangle& t : geometry) {
        const float ux = t.b.x - t.a.x;
        const float uy = t.b.y - t.a.y;
        const float uz = t.b.z - t.a.z;
        const float vx = t.c.x - t.a.x;
        const float vy = t.c.y - t.a.y;
        const float vz = t.c.z - t.a.z;
        const float nx = uy * vz - uz * vy;
        const float ny = uz * vx - ux * vz;
        const float nz = ux * vy - uy * vx;
        const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nlen < 1e-9f) continue; // degenerate triangle

        const bool walkable = (ny / nlen) >= cos_slope;
        const float tri_min_x = std::min({t.a.x, t.b.x, t.c.x});
        const float tri_max_x = std::max({t.a.x, t.b.x, t.c.x});
        const float tri_min_z = std::min({t.a.z, t.b.z, t.c.z});
        const float tri_max_z = std::max({t.a.z, t.b.z, t.c.z});

        const float reach = walkable ? 0.0f : params.agentRadius;
        const int i0 = std::max(0, static_cast<int>(std::floor((tri_min_x - reach - m_originX) / m_cellSize)));
        const int i1 = std::min(m_width - 1, static_cast<int>(std::floor((tri_max_x + reach - m_originX) / m_cellSize)));
        const int j0 = std::max(0, static_cast<int>(std::floor((tri_min_z - reach - m_originZ) / m_cellSize)));
        const int j1 = std::min(m_height - 1, static_cast<int>(std::floor((tri_max_z + reach - m_originZ) / m_cellSize)));

        for (int j = j0; j <= j1; ++j) {
            const float pz = m_originZ + (static_cast<float>(j) + 0.5f) * m_cellSize;
            for (int i = i0; i <= i1; ++i) {
                const float px = m_originX + (static_cast<float>(i) + 0.5f) * m_cellSize;
                const std::size_t idx = static_cast<std::size_t>(j) * m_width + static_cast<std::size_t>(i);
                if (walkable) {
                    float w0 = 0.0f, w1 = 0.0f, w2 = 0.0f;
                    if (!barycentricXZ(t.a, t.b, t.c, px, pz, w0, w1, w2)) continue;
                    constexpr float kEps = 1e-4f;
                    if (w0 < -kEps || w1 < -kEps || w2 < -kEps) continue;
                    const float y = w0 * t.a.y + w1 * t.b.y + w2 * t.c.y;
                    m_heights[idx] = std::min(m_heights[idx], y);
                } else if (pointTriDist2XZ(px, pz, t.a, t.b, t.c) <= radius2) {
                    blocked[idx] = 1;
                }
            }
        }
    }

    m_walkable.resize(cells);
    for (std::size_t i = 0; i < cells; ++i) {
        const bool supported = std::isfinite(m_heights[i]);
        m_walkable[i] = static_cast<std::uint8_t>(supported && !blocked[i]);
        if (!supported) m_heights[i] = 0.0f;
    }
    return true;
}

bool NavMesh::isWalkable(NavPoint p) const
{
    const int idx = cellIndex(p.x, p.z);
    if (idx < 0) return false;
    return m_walkable[static_cast<std::size_t>(idx)] != 0;
}

std::optional<NavPoint> NavMesh::nearestWalkable(NavPoint p, float maxRadius) const
{
    if (empty() || !(maxRadius >= 0.0f)) return std::nullopt;
    if (isWalkable(p)) return p;

    const int cx = static_cast<int>(std::floor((p.x - m_originX) / m_cellSize));
    const int cz = static_cast<int>(std::floor((p.z - m_originZ) / m_cellSize));
    const int rings = static_cast<int>(std::ceil(maxRadius / m_cellSize)) + 1;

    const NavPoint* best = nullptr;
    float best_d2 = std::numeric_limits<float>::infinity();
    NavPoint best_pt;
    for (int r = 0; r <= rings; ++r) {
        if (static_cast<float>(r) * m_cellSize - m_cellSize > maxRadius && best) break;
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue; // ring only
                const int i = cx + dx;
                const int j = cz + dz;
                if (i < 0 || j < 0 || i >= m_width || j >= m_height) continue;
                const std::size_t idx = static_cast<std::size_t>(j) * m_width + static_cast<std::size_t>(i);
                if (m_walkable[idx] == 0) continue;
                const NavPoint cand = cellPoint(static_cast<int>(idx));
                const float ddx = cand.x - p.x;
                const float ddz = cand.z - p.z;
                const float d2 = ddx * ddx + ddz * ddz;
                if (d2 <= maxRadius * maxRadius && d2 < best_d2) {
                    best_d2 = d2;
                    best_pt = cand;
                    best = &best_pt;
                }
            }
        }
    }
    if (!best) return std::nullopt;
    return best_pt;
}

std::vector<NavPoint> NavMesh::findPath(NavPoint from, NavPoint to) const
{
    std::vector<NavPoint> path;
    if (empty()) return path;

    const std::optional<NavPoint> start = nearestWalkable(from, kSnapRadius);
    const std::optional<NavPoint> goal = nearestWalkable(to, kSnapRadius);
    if (!start || !goal) return path;

    const int start_idx = cellIndex(start->x, start->z);
    const int goal_idx = cellIndex(goal->x, goal->z);
    if (start_idx < 0 || goal_idx < 0) return path;

    const int cells = m_width * m_height;
    const float kInf = std::numeric_limits<float>::infinity();
    std::vector<float> g(static_cast<std::size_t>(cells), kInf);
    std::vector<int> parent(static_cast<std::size_t>(cells), -1);
    std::vector<std::uint8_t> closed(static_cast<std::size_t>(cells), 0);

    const float sqrt2 = std::sqrt(2.0f);
    const float cs = m_cellSize;
    auto heuristic = [&](int idx) {
        const int dx = std::abs(idx % m_width - goal_idx % m_width);
        const int dz = std::abs(idx / m_width - goal_idx / m_width);
        const int lo = std::min(dx, dz);
        const int hi = std::max(dx, dz);
        return cs * (static_cast<float>(hi - lo) + sqrt2 * static_cast<float>(lo));
    };
    auto passable = [&](int idx, int from_idx) {
        const std::size_t u = static_cast<std::size_t>(idx);
        if (m_walkable[u] == 0) return false;
        const float dh = std::fabs(m_heights[u] - m_heights[static_cast<std::size_t>(from_idx)]);
        return dh <= m_maxStepHeight + 1e-4f;
    };

    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenCmp> open;
    g[static_cast<std::size_t>(start_idx)] = 0.0f;
    open.push({heuristic(start_idx), start_idx});

    static const int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kDz[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    while (!open.empty()) {
        const OpenNode cur = open.top();
        open.pop();
        const std::size_t cidx = static_cast<std::size_t>(cur.idx);
        if (closed[cidx] != 0) continue;
        closed[cidx] = 1;
        if (cur.idx == goal_idx) break;

        const int cx = cur.idx % m_width;
        const int cz = cur.idx / m_width;
        for (int n = 0; n < 8; ++n) {
            const int nx = cx + kDx[n];
            const int nz = cz + kDz[n];
            if (nx < 0 || nz < 0 || nx >= m_width || nz >= m_height) continue;
            const int nidx = nz * m_width + nx;
            if (!passable(nidx, cur.idx)) continue;
            if (kDx[n] != 0 && kDz[n] != 0) {
                // No corner cutting: both orthogonal cells must be open.
                const int a = cz * m_width + nx;
                const int b = nz * m_width + cx;
                if (m_walkable[static_cast<std::size_t>(a)] == 0
                    || m_walkable[static_cast<std::size_t>(b)] == 0) {
                    continue;
                }
            }
            const float step = (kDx[n] != 0 && kDz[n] != 0) ? cs * sqrt2 : cs;
            const float tentative = g[cidx] + step;
            const std::size_t nu = static_cast<std::size_t>(nidx);
            if (tentative < g[nu]) {
                g[nu] = tentative;
                parent[nu] = cur.idx;
                open.push({tentative + heuristic(nidx), nidx});
            }
        }
    }

    if (closed[static_cast<std::size_t>(goal_idx)] == 0) return path;

    std::vector<int> cells_path;
    for (int idx = goal_idx; idx >= 0; idx = parent[static_cast<std::size_t>(idx)]) {
        cells_path.push_back(idx);
        if (idx == start_idx) break;
    }
    std::reverse(cells_path.begin(), cells_path.end());

    path.push_back(*start);
    for (std::size_t k = 1; k + 1 < cells_path.size(); ++k) {
        path.push_back(cellPoint(cells_path[k]));
    }
    path.push_back(*goal);

    // String pulling: greedily replace runs of cell centres with direct
    // segments as long as every sample stays on walkable ground.
    std::vector<NavPoint> out;
    out.push_back(path.front());
    for (std::size_t i = 0; i + 1 < path.size();) {
        std::size_t j = path.size() - 1;
        for (; j > i + 1; --j) {
            if (lineWalkable(path[i], path[j])) break;
        }
        const NavPoint& p = path[j];
        const NavPoint& q = out.back();
        const float ddx = p.x - q.x;
        const float ddz = p.z - q.z;
        if (ddx * ddx + ddz * ddz > 1e-12f) out.push_back(p); // drop duplicates
        i = j;
    }
    return out;
}

bool NavMesh::save(const std::string& path) const
{
    if (empty()) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const std::uint32_t magic = kNavMagic;
    const std::uint32_t version = kNavVersion;
    const std::uint32_t w = static_cast<std::uint32_t>(m_width);
    const std::uint32_t h = static_cast<std::uint32_t>(m_height);
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&m_cellSize), 4);
    out.write(reinterpret_cast<const char*>(&m_originX), 4);
    out.write(reinterpret_cast<const char*>(&m_originZ), 4);
    out.write(reinterpret_cast<const char*>(&m_maxStepHeight), 4);
    out.write(reinterpret_cast<const char*>(&w), 4);
    out.write(reinterpret_cast<const char*>(&h), 4);
    const std::size_t cells = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    out.write(reinterpret_cast<const char*>(m_walkable.data()),
              static_cast<std::streamsize>(cells));
    out.write(reinterpret_cast<const char*>(m_heights.data()),
              static_cast<std::streamsize>(cells * 4));
    return out.good();
}

bool NavMesh::load(const std::string& path)
{
    clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    float cell_size = 0.0f;
    float origin_x = 0.0f;
    float origin_z = 0.0f;
    float max_step = 0.0f;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    if (!readExact(in, &magic, 4) || !readExact(in, &version, 4)
        || !readExact(in, &cell_size, 4) || !readExact(in, &origin_x, 4)
        || !readExact(in, &origin_z, 4) || !readExact(in, &max_step, 4)
        || !readExact(in, &w, 4) || !readExact(in, &h, 4)) {
        return false;
    }
    if (magic != kNavMagic || version != kNavVersion) return false;
    if (!std::isfinite(cell_size) || !std::isfinite(origin_x) || !std::isfinite(origin_z)
        || !std::isfinite(max_step)) {
        return false;
    }
    if (!(cell_size >= 0.01f && cell_size <= 100.0f) || !(max_step >= 0.0f && max_step <= 20.0f)) {
        return false;
    }
    if (w == 0 || h == 0 || w > kMaxGridDim || h > kMaxGridDim
        || static_cast<std::size_t>(w) * static_cast<std::size_t>(h) > kMaxCells) {
        return false;
    }

    const std::size_t cells = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    std::vector<std::uint8_t> walkable(cells);
    std::vector<float> heights(cells);
    if (!readExact(in, walkable.data(), cells)) return false;
    if (!readExact(in, heights.data(), cells * 4)) return false;
    for (std::size_t i = 0; i < cells; ++i) {
        if (!std::isfinite(heights[i])) return false;
    }

    m_cellSize = cell_size;
    m_originX = origin_x;
    m_originZ = origin_z;
    m_maxStepHeight = max_step;
    m_width = static_cast<int>(w);
    m_height = static_cast<int>(h);
    m_walkable = std::move(walkable);
    m_heights = std::move(heights);
    return true;
}

} // namespace ai
} // namespace ks
