#include "TrackModulesCore.h"

#include "AI/AiFileReader.h"
#include "FileFormat/Kn5Reader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace ks {
namespace trackmodules {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::string lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// First match wins, so the specific roles are listed before the generic ones.
// Kunos tracks name their nodes per sector ("1ROAD", "12KERB", "GRASS_2", ...),
// and their materials usually carry the same word, which gives two independent
// signals before any geometry analysis is needed.
struct Keyword {
    const char* token;
    const char* role;
};

const Keyword kKeywords[] = {
    {"kerb", "kerb"},          {"curb", "kerb"},          {"cordol", "kerb"},
    {"gravel", "gravel"},      {"ghiai", "gravel"},       {"sabbia", "gravel"},
    {"sand", "gravel"},
    {"grass", "grass"},        {"erba", "grass"},         {"prato", "grass"},
    {"armco", "wall"},         {"wall", "wall"},          {"barrier", "wall"},
    {"guard", "wall"},         {"muro", "wall"},          {"bloc", "wall"},
    {"fence", "fence"},        {"rete", "fence"},         {"grate", "fence"},
    {"tree", "vegetation"},    {"alber", "vegetation"},   {"veget", "vegetation"},
    {"bush", "vegetation"},    {"hedge", "vegetation"},
    {"tribun", "grandstand"},  {"grandstand", "grandstand"}, {"crowd", "grandstand"},
    {"pit", "pit"},
    {"garage", "building"},    {"build", "building"},     {"house", "building"},
    {"tower", "building"},     {"roof", "building"},
    {"start", "startline"},    {"finish", "startline"},
    // Real Kunos tracks: Vallelunga names its asphalt "asph_gr1..15" and its
    // material "ashpalt_new" (typo for asphalt), so neither "asphalt" nor the
    // node name matches. Both truncations are listed before the generic words.
    {"asph", "road"},          {"ashp", "road"},          {"road", "road"},
    {"asphalt", "road"},       {"tarmac", "road"},
    {"track", "road"},         {"surface", "road"},
};

void addUnique(std::vector<std::string>& list, const std::string& value)
{
    if (value.empty()) return;
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
}

using Vec3 = std::array<double, 3>;

// Row-vector convention (p' = p * M), translation in [12..14].
void transformPoint(const float m[16], const float* p, double out[3])
{
    out[0] = double(p[0]) * m[0] + double(p[1]) * m[4] + double(p[2]) * m[8] + double(m[12]);
    out[1] = double(p[0]) * m[1] + double(p[1]) * m[5] + double(p[2]) * m[9] + double(m[13]);
    out[2] = double(p[0]) * m[2] + double(p[1]) * m[6] + double(p[2]) * m[10] + double(m[14]);
}

std::size_t strideFor(std::size_t vertices, std::size_t budget)
{
    if (vertices <= budget || budget == 0) return 1;
    return std::max<std::size_t>(1, vertices / budget);
}

// Station frame: the AI line as a polyline, with signed lateral offsets and a
// vertical delta. Everything geometric (road centre, widths, kerbs) is measured
// through it, so no analysis has to care about the track's absolute placement.
struct Frames {
    std::vector<Vec3> pts;
    std::vector<double> station;
    std::vector<double> ds;
    double total = 0.0;
    bool closed = false;
    std::size_t n = 0;
    std::size_t segs = 0;

    explicit Frames(const ai::AiSpline& spline)
    {
        n = spline.points.size();
        closed = spline.closed;
        if (n < 2) return;
        segs = closed ? n : n - 1;
        pts.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            pts[i] = Vec3{double(spline.points[i].position.x),
                          double(spline.points[i].position.y),
                          double(spline.points[i].position.z)};
        }
        station.assign(n, 0.0);
        ds.assign(n, 0.0);
        for (std::size_t i = 0; i < segs; ++i) {
            const Vec3& a = pts[i];
            const Vec3& b = pts[(i + 1) % n];
            const double dx = b[0] - a[0];
            const double dz = b[2] - a[2];
            ds[i] = std::sqrt(dx * dx + dz * dz);
            station[i] = total;
            total += ds[i];
        }
    }

    // Nearest point on the polyline: station, signed lateral distance and the
    // height difference to the line itself.
    void project(const Vec3& p, double& s, double& lateral, double& dy) const
    {
        double best = std::numeric_limits<double>::max();
        s = lateral = dy = 0.0;
        for (std::size_t i = 0; i < segs; ++i) {
            const Vec3& a = pts[i];
            const Vec3& b = pts[(i + 1) % n];
            const double ex = b[0] - a[0];
            const double ez = b[2] - a[2];
            const double len2 = ex * ex + ez * ez;
            if (len2 <= 1e-9) continue;
            double t = ((p[0] - a[0]) * ex + (p[2] - a[2]) * ez) / len2;
            t = std::clamp(t, 0.0, 1.0);
            const double qx = a[0] + t * ex;
            const double qz = a[2] + t * ez;
            const double qy = a[1] + t * (b[1] - a[1]);
            const double d2 = (p[0] - qx) * (p[0] - qx) + (p[2] - qz) * (p[2] - qz);
            if (d2 >= best) continue;
            best = d2;
            const double len = std::sqrt(len2);
            // cross_y of (segment direction, point offset): consistent sign
            // for every segment, which is all the callers need.
            lateral = (ex * (p[2] - a[2]) - ez * (p[0] - a[0])) / len;
            dy = p[1] - qy;
            s = station[i] + t * ds[i];
        }
    }

    // Point at a station, offset laterally (positive = the project() sign).
    Vec3 pointAt(double s, double lateral) const
    {
        if (segs == 0) return Vec3{0.0, 0.0, 0.0};
        double target = s;
        if (closed && total > 0.0) {
            while (target >= total) target -= total;
            while (target < 0.0) target += total;
        }
        std::size_t i = 0;
        while (i + 1 < segs && station[i + 1] <= target) ++i;
        const Vec3& a = pts[i];
        const Vec3& b = pts[(i + 1) % n];
        const double ex = b[0] - a[0];
        const double ez = b[2] - a[2];
        const double len = std::sqrt(ex * ex + ez * ez);
        const double t = ds[i] > 1e-9 ? std::clamp((target - station[i]) / ds[i], 0.0, 1.0) : 0.0;
        Vec3 out{a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2])};
        if (len > 1e-9) {
            out[0] += (-ez / len) * lateral;
            out[2] += (ex / len) * lateral;
        }
        return out;
    }
};

// Road surface sampled in 2 m station bins: lateral extent and vertical offset.
struct Bin {
    double minLat = std::numeric_limits<double>::max();
    double maxLat = std::numeric_limits<double>::lowest();
    double sumDy = 0.0;
    int count = 0;
};

using Bins = std::map<long long, Bin>;

constexpr double kBinSize = 2.0;

bool binAt(const Bins& bins, double s, double& center, double& half, double& yOff)
{
    // Union of a 10 m window: on a curve the two edges of the road travel
    // different arc lengths, so their vertices fall in different 2 m bins and a
    // single bin would report one edge as if it were the whole road.
    const long long key = static_cast<long long>(std::floor(s / kBinSize));
    double minLat = std::numeric_limits<double>::max();
    double maxLat = std::numeric_limits<double>::lowest();
    double sumDy = 0.0;
    long long count = 0;
    for (long long probe = key - 2; probe <= key + 2; ++probe) {
        const auto it = bins.find(probe);
        if (it == bins.end() || it->second.count <= 0) continue;
        minLat = std::min(minLat, it->second.minLat);
        maxLat = std::max(maxLat, it->second.maxLat);
        sumDy += it->second.sumDy;
        count += it->second.count;
    }
    if (count <= 0 || maxLat < minLat) return false;
    center = 0.5 * (minLat + maxLat);
    half = 0.5 * (maxLat - minLat);
    yOff = sumDy / double(count);
    return true;
}

void walkNodes(const std::vector<engine::fileformat::Kn5Node>& nodes,
               const std::vector<engine::fileformat::Kn5Material>& materials, Report& report)
{
    for (const engine::fileformat::Kn5Node& node : nodes) {
        ++report.node_count;

        NodeInfo info;
        info.name = node.name;
        info.node_class = static_cast<int>(node.node_class);
        info.has_mesh = node.has_mesh;
        info.lod_in = node.mesh.lod_in;
        info.lod_out = node.mesh.lod_out;
        info.role = classifyRole(node.name);
        info.role_source = info.role == "other" ? "none" : "name";
        if (node.has_mesh) {
            ++report.mesh_count;
            const engine::fileformat::Kn5Mesh& mesh = node.mesh;
            info.vertices = mesh.positions.size() / 3;
            info.triangles = mesh.indices.size() / 3;
            if (mesh.material_id >= 0 &&
                static_cast<std::size_t>(mesh.material_id) < materials.size()) {
                const engine::fileformat::Kn5Material& material =
                    materials[static_cast<std::size_t>(mesh.material_id)];
                info.material = material.name;
                addUnique(report.materials, material.name + " (" + material.shader + ")");
                // Second signal before geometry: opaque node names ("obj_5")
                // often still carry a readable material ("kerb_stone").
                if (info.role == "other") {
                    const std::string byMaterial = classifyRole(material.name);
                    if (byMaterial != "other") {
                        info.role = byMaterial;
                        info.role_source = "material";
                    }
                }
            }
            info.world[0] = mesh.world[12];
            info.world[1] = mesh.world[13];
            info.world[2] = mesh.world[14];

            bool first = true;
            for (std::size_t v = 0; v + 2 < mesh.positions.size(); v += 3) {
                double p[3];
                transformPoint(mesh.world.data(), &mesh.positions[v], p);
                for (int axis = 0; axis < 3; ++axis) {
                    if (first || p[axis] < info.bbox_min[axis]) info.bbox_min[axis] = p[axis];
                    if (first || p[axis] > info.bbox_max[axis]) info.bbox_max[axis] = p[axis];
                }
                first = false;
            }
        }
        report.nodes.push_back(std::move(info));

        walkNodes(node.children, materials, report);
    }
}

// Keeps the sampled world positions of one node without storing the whole mesh.
void sampleNode(const NodeInfo& node, const engine::fileformat::Kn5Mesh& mesh,
                const float world[16], std::size_t budget, std::vector<Vec3>& out)
{
    (void)node;
    const std::size_t vertices = mesh.positions.size() / 3;
    const std::size_t stride = strideFor(vertices, budget);
    for (std::size_t v = 0; v + 2 < mesh.positions.size(); v += 3 * stride) {
        double p[3];
        transformPoint(world, &mesh.positions[v], p);
        out.push_back(Vec3{p[0], p[1], p[2]});
    }
}

// Walks the KN5 again to feed the samplers (the parsed tree is not kept in the
// report, and the buffers are large).
struct MeshSample {
    std::size_t node_index = 0; // index into Report::nodes
    std::string role;           // role at collection time
    std::string name;
    std::vector<Vec3> points;
};

void collectSamples(const std::vector<engine::fileformat::Kn5Node>& nodes,
                    const Report& report, std::size_t& cursor, std::size_t budget,
                    std::vector<MeshSample>& out)
{
    for (const engine::fileformat::Kn5Node& node : nodes) {
        if (cursor < report.nodes.size()) ++cursor;
        if (node.has_mesh) {
            const NodeInfo& info = report.nodes[cursor - 1];
            MeshSample sample;
            sample.node_index = cursor - 1;
            sample.role = info.role;
            sample.name = info.name;
            sampleNode(info, node.mesh, node.mesh.world.data(), budget, sample.points);
            if (!sample.points.empty()) out.push_back(std::move(sample));
        }
        collectSamples(node.children, report, cursor, budget, out);
    }
}

// ---------------------------------------------------------------------------
// Centerline from the asphalt alone
// ---------------------------------------------------------------------------
//
// A layout generated by the engine, or an asset whose data folder was dropped,
// has no ai/fast_lane.ai — but the asphalt is in the .kn5, and it is the surface
// every measurement here is taken on anyway. Rasterise the road samples on a
// 1 m grid, thin the mask to a one-cell skeleton (Zhang-Suen), prune the spurs
// (pit apron, paddock loops) and walk the remaining cycle. The result is an
// AiSpline in the shape the rest of the analysis already consumes, so corners,
// widths and kerb sections need no second code path.

struct SkeletonGrid {
    double minX = 0.0;
    double minZ = 0.0;
    double cell = 1.0;
    int w = 0;
    int h = 0;
    std::vector<unsigned char> mask;
    std::vector<float> sumY;
    std::vector<int> countY;

    std::size_t index(int x, int z) const
    {
        return std::size_t(z) * std::size_t(w) + std::size_t(x);
    }
    bool inside(int x, int z) const { return x >= 0 && z >= 0 && x < w && z < h; }
    unsigned char at(int x, int z) const { return inside(x, z) ? mask[index(x, z)] : 0; }
    int neighbours(int x, int z) const
    {
        int count = 0;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                if (at(x + dx, z + dz)) ++count;
            }
        return count;
    }
};

// Fills a triangle on the mask instead of walking its vertices. A Kunos road is
// tiled with quads 5-10 m across, so a vertex cloud leaves a mask of isolated
// dots (Vallelunga: 6236 cells in 4443 components) and no skeleton at all.
void fillTriangle(const Vec3& a, const Vec3& b, const Vec3& c, SkeletonGrid& grid)
{
    const double area = (b[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (b[2] - a[2]);
    if (std::fabs(area) < 1e-9) return;
    const int x0 = std::max(0, int((std::min({a[0], b[0], c[0]}) - grid.minX) / grid.cell));
    const int x1 = std::min(grid.w - 1,
                            int((std::max({a[0], b[0], c[0]}) - grid.minX) / grid.cell));
    const int z0 = std::max(0, int((std::min({a[2], b[2], c[2]}) - grid.minZ) / grid.cell));
    const int z1 = std::min(grid.h - 1,
                            int((std::max({a[2], b[2], c[2]}) - grid.minZ) / grid.cell));
    for (int z = z0; z <= z1; ++z) {
        const double pz = grid.minZ + (double(z) + 0.5) * grid.cell;
        for (int x = x0; x <= x1; ++x) {
            const double px = grid.minX + (double(x) + 0.5) * grid.cell;
            const double wa = ((b[0] - px) * (c[2] - pz) - (c[0] - px) * (b[2] - pz)) / area;
            const double wb = ((c[0] - px) * (a[2] - pz) - (a[0] - px) * (c[2] - pz)) / area;
            const double wc = 1.0 - wa - wb;
            if (wa < -1e-6 || wb < -1e-6 || wc < -1e-6) continue;
            const std::size_t i = grid.index(x, z);
            grid.mask[i] = 1;
            grid.sumY[i] += float(wa * a[1] + wb * b[1] + wc * c[1]);
            grid.countY[i] += 1;
        }
    }
}

// Marks every cell a segment passes through. Filling only the triangle interiors
// leaves the long thin triangles of a road ribbon as a dotted line (one cell here
// and there), because a cell is filled only when its centre lands inside the
// triangle; a dotted mask thins into hundreds of fragments. The edges always
// keep a connected chain.
void drawSegment(const Vec3& a, const Vec3& b, SkeletonGrid& grid)
{
    const double dx = b[0] - a[0];
    const double dz = b[2] - a[2];
    const double length = std::hypot(dx, dz);
    const int steps = std::max(1, int(length / (0.5 * grid.cell)));
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / double(steps);
        const int x = int((a[0] + dx * t - grid.minX) / grid.cell);
        const int z = int((a[2] + dz * t - grid.minZ) / grid.cell);
        if (!grid.inside(x, z)) continue;
        const std::size_t index = grid.index(x, z);
        grid.mask[index] = 1;
        grid.sumY[index] += float(a[1] + (b[1] - a[1]) * t);
        grid.countY[index] += 1;
    }
}

// Same depth-first order as walkNodes(), so report.nodes[cursor - 1] is the
// node being visited.
void fillNodeTriangles(const std::vector<engine::fileformat::Kn5Node>& nodes,
                       const Report& report, std::size_t& cursor, SkeletonGrid& grid)
{
    for (const engine::fileformat::Kn5Node& node : nodes) {
        if (cursor < report.nodes.size()) ++cursor;
        if (node.has_mesh && report.nodes[cursor - 1].role == "road") {
            const auto& positions = node.mesh.positions;
            const auto& indices = node.mesh.indices;
            auto vertex = [&node, &positions](std::size_t index, Vec3& out) {
                if (index * 3 + 2 >= positions.size()) return false;
                double p[3];
                transformPoint(node.mesh.world.data(), &positions[index * 3], p);
                out = Vec3{p[0], p[1], p[2]};
                return true;
            };
            if (!indices.empty()) {
                for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
                    Vec3 a{}, b{}, c{};
                    if (!vertex(indices[t], a) || !vertex(indices[t + 1], b) ||
                        !vertex(indices[t + 2], c))
                        continue;
                    fillTriangle(a, b, c, grid);
                    drawSegment(a, b, grid);
                    drawSegment(b, c, grid);
                    drawSegment(c, a, grid);
                }
            } else {
                for (std::size_t v = 0; v + 2 < positions.size(); v += 9) {
                    Vec3 a{}, b{}, c{};
                    if (!vertex(v / 3, a) || !vertex(v / 3 + 1, b) || !vertex(v / 3 + 2, c))
                        continue;
                    fillTriangle(a, b, c, grid);
                    drawSegment(a, b, grid);
                    drawSegment(b, c, grid);
                    drawSegment(c, a, grid);
                }
            }
        }
        fillNodeTriangles(node.children, report, cursor, grid);
    }
}

bool rasterizeRoad(const engine::fileformat::Kn5File& kn5, const Report& report,
                   SkeletonGrid& grid)
{
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minZ = std::numeric_limits<double>::max();
    double maxZ = std::numeric_limits<double>::lowest();
    std::size_t surface = 0;
    for (const NodeInfo& node : report.nodes) {
        if (!node.has_mesh || node.role != "road") continue;
        minX = std::min(minX, node.bbox_min[0]);
        maxX = std::max(maxX, node.bbox_max[0]);
        minZ = std::min(minZ, node.bbox_min[2]);
        maxZ = std::max(maxZ, node.bbox_max[2]);
        surface += node.triangles;
    }
    if (surface == 0 || maxX <= minX || maxZ <= minZ) return false;

    // A cell finer than the road width lets the skeleton branch at every seam;
    // a cell too coarse loses the hairpins. 1 m, then 2 m and 4 m for a huge
    // layout, keeps the grid within a few million cells.
    for (const double cell : {1.0, 2.0, 4.0}) {
        const int w = int((maxX - minX) / cell) + 5;
        const int h = int((maxZ - minZ) / cell) + 5;
        if (double(w) * double(h) > 8.0e6) continue;
        grid.cell = cell;
        grid.minX = minX - 2.0 * cell;
        grid.minZ = minZ - 2.0 * cell;
        grid.w = w;
        grid.h = h;
        break;
    }
    if (grid.w <= 0) return false;

    grid.mask.assign(std::size_t(grid.w) * std::size_t(grid.h), 0);
    grid.sumY.assign(grid.mask.size(), 0.0f);
    grid.countY.assign(grid.mask.size(), 0);
    std::size_t cursor = 0;
    fillNodeTriangles(kn5.nodes, report, cursor, grid);
    return true;
}

// Zhang-Suen thinning: peels one layer of the mask per sub-iteration until the
// band collapses to a one-cell skeleton. Two sub-iterations per pass keep the
// result centred instead of letting it drift to one edge of the road.
void thinMask(SkeletonGrid& grid)
{
    std::vector<std::size_t> remove;
    for (int pass = 0; pass < 200; ++pass) {
        bool changed = false;
        for (int step = 0; step < 2; ++step) {
            remove.clear();
            for (int z = 1; z < grid.h - 1; ++z) {
                for (int x = 1; x < grid.w - 1; ++x) {
                    if (!grid.mask[grid.index(x, z)]) continue;
                    const int p2 = grid.at(x, z - 1) ? 1 : 0;
                    const int p3 = grid.at(x + 1, z - 1) ? 1 : 0;
                    const int p4 = grid.at(x + 1, z) ? 1 : 0;
                    const int p5 = grid.at(x + 1, z + 1) ? 1 : 0;
                    const int p6 = grid.at(x, z + 1) ? 1 : 0;
                    const int p7 = grid.at(x - 1, z + 1) ? 1 : 0;
                    const int p8 = grid.at(x - 1, z) ? 1 : 0;
                    const int p9 = grid.at(x - 1, z - 1) ? 1 : 0;
                    const int occupied = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                    if (occupied < 2 || occupied > 6) continue;
                    const int ring[9] = {p2, p3, p4, p5, p6, p7, p8, p9, p2};
                    int transitions = 0;
                    for (int i = 0; i < 8; ++i)
                        if (ring[i] == 0 && ring[i + 1] == 1) ++transitions;
                    if (transitions != 1) continue;
                    // Both conditions of each sub-iteration, as in the paper:
                    // dropping one of them is more aggressive and starts cutting
                    // the band into hundreds of fragments.
                    const bool blocked =
                        step == 0 ? ((p2 * p4 * p6) != 0 || (p4 * p6 * p8) != 0)
                                  : ((p2 * p4 * p8) != 0 || (p2 * p6 * p8) != 0);
                    if (blocked) continue;
                    remove.push_back(grid.index(x, z));
                }
            }
            for (const std::size_t i : remove) grid.mask[i] = 0;
            if (!remove.empty()) changed = true;
        }
        if (!changed) break;
    }
}

// Square dilation then erosion. Kunos splits the asphalt into per-sector meshes
// ("asph_gr1".."asph_gr15") and a few metres go missing where the pit lane joins
// or a bridge starts; a 2 m closing bridges those seams, and a radius much above
// the road half-width would start fusing parallel straights.
void morphDilate(SkeletonGrid& grid, int radius)
{
    std::vector<unsigned char> grown = grid.mask;
    for (int z = 0; z < grid.h; ++z)
        for (int x = 0; x < grid.w; ++x) {
            if (!grid.mask[grid.index(x, z)]) continue;
            for (int dz = -radius; dz <= radius; ++dz)
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (grid.inside(nx, nz)) grown[grid.index(nx, nz)] = 1;
                }
        }
    grid.mask.swap(grown);
}

void morphErode(SkeletonGrid& grid, int radius)
{
    std::vector<unsigned char> shrunk = grid.mask;
    for (int z = 0; z < grid.h; ++z)
        for (int x = 0; x < grid.w; ++x) {
            if (!grid.mask[grid.index(x, z)]) continue;
            bool keep = true;
            for (int dz = -radius; dz <= radius && keep; ++dz)
                for (int dx = -radius; dx <= radius && keep; ++dx)
                    if (!grid.at(x + dx, z + dz)) keep = false;
            if (!keep) shrunk[grid.index(x, z)] = 0;
        }
    grid.mask.swap(shrunk);
}

// Spurs (a pit apron, a paddock loop) end in a one-cell tip. Removing tips until
// none is left keeps the lap cycle and drops the branches.
void pruneSpurs(SkeletonGrid& grid)
{
    std::vector<std::size_t> remove;
    // Short spurs only: an open end that survived the closing is a tear in the
    // asphalt, and eating it for hundreds of passes would delete real lap.
    for (int pass = 0; pass < 12; ++pass) {
        remove.clear();
        for (int z = 1; z < grid.h - 1; ++z)
            for (int x = 1; x < grid.w - 1; ++x) {
                const std::size_t i = grid.index(x, z);
                if (!grid.mask[i]) continue;
                if (grid.neighbours(x, z) <= 1) remove.push_back(i);
            }
        if (remove.empty()) break;
        for (const std::size_t i : remove) grid.mask[i] = 0;
    }
}

// A closed skeleton has no end; every cell still has two neighbours. Anything
// left with one neighbour is a tear in the mask, and the lap cannot close there.
std::size_t countOpenEnds(const SkeletonGrid& grid)
{
    std::size_t ends = 0;
    for (int z = 1; z < grid.h - 1; ++z)
        for (int x = 1; x < grid.w - 1; ++x)
            if (grid.mask[grid.index(x, z)] && grid.neighbours(x, z) <= 1) ++ends;
    return ends;
}

// The pit apron is often a separate island: only the biggest component is the
// lap, and a BFS seeded on a smaller one would return a few metres of paddock.
void keepLargestComponent(SkeletonGrid& grid)
{
    std::vector<int> label(grid.mask.size(), -1);
    std::vector<int> queue;
    std::size_t best = 0;
    int bestLabel = -1;
    int next = 0;
    for (std::size_t seed = 0; seed < grid.mask.size(); ++seed) {
        if (!grid.mask[seed] || label[seed] >= 0) continue;
        queue.clear();
        queue.push_back(int(seed));
        label[seed] = next;
        std::size_t size = 0;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const int current = queue[head];
            ++size;
            const int cx = current % grid.w;
            const int cz = current / grid.w;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    const int nx = cx + dx;
                    const int nz = cz + dz;
                    if (!grid.inside(nx, nz)) continue;
                    const std::size_t ni = grid.index(nx, nz);
                    if (!grid.mask[ni] || label[ni] >= 0) continue;
                    label[ni] = next;
                    queue.push_back(int(ni));
                }
        }
        if (size > best) {
            best = size;
            bestLabel = next;
        }
        ++next;
    }
    if (bestLabel < 0) return;
    for (std::size_t i = 0; i < grid.mask.size(); ++i)
        if (grid.mask[i] && label[i] != bestLabel) grid.mask[i] = 0;
}

// Breadth-first sweep that returns the cell farthest from `start`; `parent`
// keeps the tree, so the path can be rebuilt afterwards.
int bfsFarthest(const SkeletonGrid& grid, int start, std::vector<int>& parent)
{
    parent.assign(grid.mask.size(), -2);
    std::vector<int> depth(grid.mask.size(), -1);
    std::vector<int> queue;
    queue.reserve(grid.mask.size() / 8 + 16);
    parent[std::size_t(start)] = -1;
    depth[std::size_t(start)] = 0;
    queue.push_back(start);
    int farthest = start;
    int bestDepth = 0;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int current = queue[head];
        const int cx = current % grid.w;
        const int cz = current / grid.w;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                const int nx = cx + dx;
                const int nz = cz + dz;
                if (!grid.inside(nx, nz)) continue;
                const std::size_t ni = grid.index(nx, nz);
                if (!grid.mask[ni] || parent[ni] != -2) continue;
                parent[ni] = current;
                depth[ni] = depth[std::size_t(current)] + 1;
                if (depth[ni] > bestDepth) {
                    bestDepth = depth[ni];
                    farthest = int(ni);
                }
                queue.push_back(int(ni));
            }
    }
    return farthest;
}

std::vector<int> tracePath(const std::vector<int>& parent, int from, int to)
{
    std::vector<int> path;
    int cursor = to;
    for (int guard = 0; guard < 4000000 && cursor >= 0; ++guard) {
        path.push_back(cursor);
        if (cursor == from) break;
        cursor = parent[std::size_t(cursor)];
    }
    std::reverse(path.begin(), path.end());
    if (path.empty() || path.front() != from) path.clear();
    return path;
}

double distanceXZToSegment(const Vec3& p, const Vec3& a, const Vec3& b)
{
    const double dx = b[0] - a[0];
    const double dz = b[2] - a[2];
    const double len2 = dx * dx + dz * dz;
    if (len2 < 1e-12) return std::hypot(p[0] - a[0], p[2] - a[2]);
    double t = ((p[0] - a[0]) * dx + (p[2] - a[2]) * dz) / len2;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p[0] - (a[0] + t * dx), p[2] - (a[2] + t * dz));
}

void simplifyLoop(std::vector<Vec3>& points, double tolerance)
{
    const std::size_t n = points.size();
    if (n < 3) return;
    std::vector<bool> keep(n, false);
    keep.front() = true;
    keep.back() = true;
    std::vector<std::pair<std::size_t, std::size_t>> stack{{0, n - 1}};
    while (!stack.empty()) {
        const std::pair<std::size_t, std::size_t> span = stack.back();
        stack.pop_back();
        double worst = tolerance;
        std::size_t index = 0;
        bool found = false;
        for (std::size_t i = span.first + 1; i < span.second; ++i) {
            const double d = distanceXZToSegment(points[i], points[span.first], points[span.second]);
            if (d > worst) {
                worst = d;
                index = i;
                found = true;
            }
        }
        if (!found) continue;
        keep[index] = true;
        stack.emplace_back(span.first, index);
        stack.emplace_back(index, span.second);
    }
    std::vector<Vec3> kept;
    kept.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        if (keep[i]) kept.push_back(points[i]);
    points.swap(kept);
}

// The skeleton of a real track is rarely one clean cycle: the asphalt is split
// into per-sector meshes and a few metres go missing at some seams, so thinning
// leaves a handful of open polylines. Order each of them, then chain them end to
// end, preferring the neighbour that continues the current direction instead of
// cutting across the infield.
struct SkeletonPiece {
    std::vector<Vec3> pts;
    bool used = false;
};

bool walkLap(SkeletonGrid grid, std::vector<Vec3>& knots, std::string& why);


bool orderComponent(const SkeletonGrid& grid, const std::vector<int>& cells,
                    std::vector<Vec3>& out)
{
    if (cells.size() < 2) return false;
    std::map<int, int> local;
    for (std::size_t i = 0; i < cells.size(); ++i) local[cells[i]] = int(i);
    std::vector<std::vector<int>> adj(cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const int x = cells[i] % grid.w;
        const int z = cells[i] / grid.w;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                const int nx = x + dx;
                const int nz = z + dz;
                if (!grid.inside(nx, nz)) continue;
                const auto it = local.find(nz * grid.w + nx);
                if (it != local.end()) adj[i].push_back(it->second);
            }
    }

    std::size_t start = 0;
    for (std::size_t i = 0; i < adj.size(); ++i)
        if (adj[i].size() == 1) {
            start = i;
            break;
        }

    auto bfs = [&adj](std::size_t from, std::vector<int>& parent) {
        parent.assign(adj.size(), -2);
        std::vector<int> depth(adj.size(), -1);
        std::vector<int> queue{int(from)};
        parent[from] = -1;
        depth[from] = 0;
        int farthest = int(from);
        int best = 0;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const int current = queue[head];
            for (const int next : adj[std::size_t(current)]) {
                if (parent[std::size_t(next)] != -2) continue;
                parent[std::size_t(next)] = current;
                depth[std::size_t(next)] = depth[std::size_t(current)] + 1;
                if (depth[std::size_t(next)] > best) {
                    best = depth[std::size_t(next)];
                    farthest = next;
                }
                queue.push_back(next);
            }
        }
        return farthest;
    };

    std::vector<int> parentA;
    const int a = bfs(start, parentA);
    std::vector<int> parentB;
    const int b = bfs(std::size_t(a), parentB);
    std::vector<int> path;
    for (int cursor = b; cursor >= 0; cursor = parentB[std::size_t(cursor)])
        path.push_back(cursor);
    std::reverse(path.begin(), path.end());
    if (path.size() < 2) return false;

    out.clear();
    out.reserve(path.size());
    for (const int localCell : path) {
        const int cell = cells[std::size_t(localCell)];
        const int x = cell % grid.w;
        const int z = cell / grid.w;
        const std::size_t index = std::size_t(cell);
        const double y = grid.countY[index] > 0
                             ? double(grid.sumY[index]) / double(grid.countY[index])
                             : (out.empty() ? 0.0 : out.back()[1]);
        out.push_back(Vec3{grid.minX + (double(x) + 0.5) * grid.cell, y,
                           grid.minZ + (double(z) + 0.5) * grid.cell});
    }
    return true;
}

bool stitchLap(SkeletonGrid grid, std::vector<Vec3>& knots, std::string& why)
{
    thinMask(grid);
    pruneSpurs(grid);

    std::vector<int> label(grid.mask.size(), 0);
    std::vector<std::vector<int>> groups;
    for (std::size_t i = 0; i < grid.mask.size(); ++i) {
        if (!grid.mask[i] || label[i]) continue;
        groups.emplace_back();
        const int id = int(groups.size());
        std::vector<int> stack{int(i)};
        label[i] = id;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            groups.back().push_back(c);
            const int x = c % grid.w;
            const int z = c / grid.w;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (!grid.inside(nx, nz)) continue;
                    const std::size_t ni = grid.index(nx, nz);
                    if (!grid.mask[ni] || label[ni]) continue;
                    label[ni] = id;
                    stack.push_back(int(ni));
                }
        }
    }

    std::vector<SkeletonPiece> pieces;
    for (const std::vector<int>& group : groups) {
        if (group.size() < 8) continue;
        SkeletonPiece piece;
        if (orderComponent(grid, group, piece.pts)) pieces.push_back(std::move(piece));
    }
    if (pieces.empty()) {
        why = "no polylines in the skeleton";
        return false;
    }

    std::size_t first = 0;
    for (std::size_t i = 1; i < pieces.size(); ++i)
        if (pieces[i].pts.size() > pieces[first].pts.size()) first = i;
    knots = pieces[first].pts;
    pieces[first].used = true;

    for (int guard = 0; guard < 256; ++guard) {
        const Vec3 end = knots.back();
        double dirX = 0.0;
        double dirZ = 0.0;
        if (knots.size() >= 3) {
            const Vec3& back = knots[knots.size() - 3];
            dirX = end[0] - back[0];
            dirZ = end[2] - back[2];
        }
        const double outLength = std::hypot(dirX, dirZ);
        double bestScore = 1.0e18;
        std::size_t bestPiece = 0;
        bool bestReversed = false;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (pieces[i].used) continue;
            const std::vector<Vec3>& pts = pieces[i].pts;
            for (int reverse = 0; reverse < 2; ++reverse) {
                const Vec3& head = reverse ? pts.back() : pts.front();
                const double gap = std::hypot(head[0] - end[0], head[2] - end[2]);
                if (gap > 60.0) continue;
                const Vec3& next = reverse ? pts[pts.size() - 2] : pts[1];
                const double inX = head[0] - next[0];
                const double inZ = head[2] - next[2];
                const double inLength = std::hypot(inX, inZ);
                double alignment = 0.0;
                if (inLength > 1e-6 && outLength > 1e-6)
                    alignment = (dirX * inX + dirZ * inZ) / (inLength * outLength);
                if (alignment < -0.2) continue; // that would double back
                const double score = gap - 25.0 * alignment;
                if (score < bestScore) {
                    bestScore = score;
                    bestPiece = i;
                    bestReversed = reverse == 1;
                }
            }
        }
        if (bestScore > 1.0e17) break;
        const std::vector<Vec3>& pts = pieces[bestPiece].pts;
        if (bestReversed) {
            for (std::size_t i = pts.size() - 1; i-- > 0;) knots.push_back(pts[i]);
        } else {
            for (std::size_t i = 1; i < pts.size(); ++i) knots.push_back(pts[i]);
        }
        pieces[bestPiece].used = true;
    }

    // Close the lap when the walk came back near its own start.
    if (knots.size() >= 8 &&
        std::hypot(knots.back()[0] - knots.front()[0], knots.back()[2] - knots.front()[2]) < 80.0)
        knots.pop_back();

    simplifyLoop(knots, 2.0);
    if (knots.size() < 8) {
        why = "no lap after stitching";
        return false;
    }
    return true;
}

// A clean single cycle is the best case; the stitched walk is what real assets
// need. Both work on a copy, so the caller can retry with a wider closing.
bool tryLap(SkeletonGrid grid, std::vector<Vec3>& knots, std::string& why)
{
    if (walkLap(grid, knots, why)) return true;
    std::string stitched;
    if (stitchLap(std::move(grid), knots, stitched)) return true;
    why += "; " + stitched;
    return false;
}

// Walks a prepared mask: thinning, spur pruning, then the two halves of the
// cycle. Split out so the caller can retry with a wider closing without
// rebuilding the grid from the samples.
bool walkLap(SkeletonGrid grid, std::vector<Vec3>& knots, std::string& why)
{
    thinMask(grid);
    pruneSpurs(grid);
    keepLargestComponent(grid);

    int start = -1;
    for (std::size_t i = 0; i < grid.mask.size() && start < 0; ++i)
        if (grid.mask[i]) start = int(i);
    if (start < 0) {
        why = "empty skeleton";
        return false;
    }

    std::vector<int> parentA;
    const int a = bfsFarthest(grid, start, parentA);
    std::vector<int> parentB;
    const int b = bfsFarthest(grid, a, parentB);
    if (b == a) {
        why = "single cell";
        return false;
    }
    const std::vector<int> arc1 = tracePath(parentB, a, b);
    if (arc1.size() < 8) {
        why = "first half only " +
              std::to_string(int(double(arc1.size()) * grid.cell)) + " m";
        return false;
    }

    // A cycle is b's far side coming back: clear arc1, then walk from b and look
    // for a on the way. An open layout never reaches it.
    SkeletonGrid second = grid;
    for (std::size_t i = 1; i + 1 < arc1.size(); ++i) second.mask[std::size_t(arc1[i])] = 0;
    std::vector<int> parentC;
    const int back = bfsFarthest(second, b, parentC);
    std::vector<int> arc2 = tracePath(parentC, b, back);
    const auto meeting = std::find(arc2.begin(), arc2.end(), a);
    if (meeting == arc2.end()) {
        why = std::to_string(countOpenEnds(grid)) + " open ends, first half " +
              std::to_string(int(double(arc1.size()) * grid.cell)) + " m";
        return false;
    }
    arc2.erase(meeting + 1, arc2.end());
    if (arc2.size() < 8) {
        why = "second half too short";
        return false;
    }

    std::vector<Vec3> loop;
    loop.reserve(arc1.size() + arc2.size());
    auto push = [&grid, &loop](int cell) {
        const int x = cell % grid.w;
        const int z = cell / grid.w;
        const std::size_t i = std::size_t(cell);
        double y = grid.countY[i] > 0 ? double(grid.sumY[i]) / double(grid.countY[i]) : 0.0;
        if (grid.countY[i] <= 0 && !loop.empty()) y = loop.back()[1];
        loop.push_back(Vec3{grid.minX + (double(x) + 0.5) * grid.cell, y,
                            grid.minZ + (double(z) + 0.5) * grid.cell});
    };
    for (const int cell : arc1) push(cell);
    for (std::size_t i = 1; i + 1 < arc2.size(); ++i) push(arc2[i]);

    simplifyLoop(loop, 1.5);
    if (loop.size() < 8) {
        why = "cycle collapsed while simplifying";
        return false;
    }

    // Heights come from the asphalt under each knot: thinning moves the centre
    // by up to a cell, so a knot without a sample borrows its neighbour's.
    knots.clear();
    knots.reserve(loop.size());
    for (const Vec3& p : loop) {
        Vec3 q = p;
        const int x = int((q[0] - grid.minX) / grid.cell);
        const int z = int((q[2] - grid.minZ) / grid.cell);
        if (grid.inside(x, z) && grid.countY[grid.index(x, z)] > 0) {
            const std::size_t i = grid.index(x, z);
            q[1] = double(grid.sumY[i]) / double(grid.countY[i]);
        } else if (!knots.empty()) {
            q[1] = knots.back()[1];
        }
        knots.push_back(q);
    }
    return true;
}

bool buildMeshCenterline(const engine::fileformat::Kn5File& kn5, const Report& report,
                         ai::AiSpline& out, std::string& note)
{
    SkeletonGrid base;
    if (!rasterizeRoad(kn5, report, base)) {
        note = "no road samples to rasterise";
        return false;
    }

    // How far apart two asphalt meshes sit at a seam differs per track, and a
    // closing wide enough to bridge them can fuse two parallel straights. Widen
    // until the lap closes instead of hard-coding a radius: each attempt is a
    // couple of tenths of a second.
    std::vector<Vec3> knots;
    std::string attempts;
    double closing = 0.0;
    for (const double metres : {2.0, 4.0, 6.0, 8.0, 12.0}) {
        SkeletonGrid grid = base;
        const int radius = int(std::max(1.0, metres / grid.cell));
        morphDilate(grid, radius);
        morphErode(grid, radius);
        std::string why;
        std::vector<Vec3> candidate;
        if (tryLap(std::move(grid), candidate, why)) {
            knots.swap(candidate);
            closing = metres;
            break;
        }
        if (!attempts.empty()) attempts += "; ";
        attempts += std::to_string(int(metres)) + " m: " + why;
    }
    if (knots.size() < 8) {
        note = "no lap in the asphalt (" + attempts + ")";
        return false;
    }

    out.points.clear();
    out.closed = true;
    out.totalDistance = 0.0f;
    double total = 0.0;
    for (std::size_t i = 0; i < knots.size(); ++i) {
        const Vec3& p = knots[i];
        const Vec3& q = knots[(i + 1) % knots.size()];
        ai::AiSplinePoint point;
        point.position.x = float(p[0]);
        point.position.y = float(p[1]);
        point.position.z = float(p[2]);
        point.distance = float(total);
        out.points.push_back(point);
        total += std::sqrt((q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) +
                           (q[2] - p[2]) * (q[2] - p[2]));
    }
    out.totalDistance = float(total);
    note = "skeleton " + std::to_string(out.points.size()) + " knots, " +
           std::to_string(int(total)) + " m (closing " + std::to_string(int(closing)) + " m)";
    return out.points.size() >= 2;
}

// Samples only the nodes of one role, at full density. The global budget above
// keeps about 96 vertices per node — fine for a kerb strip, far too sparse for
// an asphalt ribbon 12 m wide and 4 km long, where it leaves holes in the mask.
void collectNodeSamples(const std::vector<engine::fileformat::Kn5Node>& nodes,
                        const Report& report, std::size_t& cursor, const std::string& role,
                        std::size_t budget, std::vector<MeshSample>& out)
{
    for (const engine::fileformat::Kn5Node& node : nodes) {
        if (cursor < report.nodes.size()) ++cursor;
        if (node.has_mesh) {
            const NodeInfo& info = report.nodes[cursor - 1];
            if (info.role == role) {
                MeshSample sample;
                sample.node_index = cursor - 1;
                sample.role = info.role;
                sample.name = info.name;
                sampleNode(info, node.mesh, node.mesh.world.data(), budget, sample.points);
                if (!sample.points.empty()) out.push_back(std::move(sample));
            }
        }
        collectNodeSamples(node.children, report, cursor, role, budget, out);
    }
}


// Kunos ships its AI line in its own binary format, which the engine reader
// (magic 0x00414900) does not parse:
//   int32 version (=7), int32 num_points, int32 0, int32 0        // 16 bytes
//   num_points * { float x, float z, float y, float dist, int32 } // 20 bytes
//   num_points * 18 floats                                       // detail block
//   an opaque tail (speed/border data written by CSP-era tools)
// Only the first array matters here: it carries the line and its stations.
bool readKunosAi(const std::string& path, ai::AiSpline& out)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::int32_t header[4] = {0, 0, 0, 0};
    if (std::fread(header, sizeof(header), 1, file) != 1) {
        std::fclose(file);
        return false;
    }
    const std::int32_t count = header[1];
    if (header[0] != 7 || count <= 2 || count > 10000000) {
        std::fclose(file);
        return false;
    }
    out.points.clear();
    out.points.reserve(std::size_t(count));
    for (std::int32_t i = 0; i < count; ++i) {
        float record[5];
        if (std::fread(record, sizeof(record), 1, file) != 1) {
            out.points.clear();
            std::fclose(file);
            return false;
        }
        ai::AiSplinePoint point;
        point.position.x = record[0];
        point.position.y = record[1]; // altitude: the axis with the small span
        point.position.z = record[2];
        point.distance = record[3];
        out.points.push_back(point);
    }
    std::fclose(file);
    out.closed = true;
    out.totalDistance = out.points.empty() ? 0.0f : out.points.back().distance;
    return out.points.size() >= 2;
}

double wrapAngle(double a)
{
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}

// ---------------------------------------------------------------------------
// Corner detection
// ---------------------------------------------------------------------------

// Per-segment geometry of a line, indexed in lap order.
struct Profile {
    std::vector<Vec3> pos;
    std::vector<double> ds;
    std::vector<double> head;
    std::vector<double> station;
    std::vector<double> k;
    std::vector<double> ks;
    double total = 0.0;
    double seamStation = 0.0;
    bool closed = false;
    std::size_t count = 0;
};

Profile buildProfile(const ai::AiSpline& spline)
{
    Profile p;
    const std::size_t n = spline.points.size();
    p.closed = spline.closed;
    p.count = p.closed ? n : n - 1;
    p.pos.resize(p.count);
    for (std::size_t i = 0; i < p.count; ++i) {
        const ai::AiSplinePoint& point = spline.points[i];
        p.pos[i] = Vec3{double(point.position.x), double(point.position.y),
                        double(point.position.z)};
    }
    auto measure = [&p]() {
        p.ds.assign(p.count, 0.0);
        p.head.assign(p.count, 0.0);
        p.station.assign(p.count, 0.0);
        p.total = 0.0;
        for (std::size_t i = 0; i < p.count; ++i) {
            const Vec3& a = p.pos[i];
            const Vec3& b = p.pos[(i + 1) % p.count];
            const double dx = b[0] - a[0];
            const double dz = b[2] - a[2];
            p.ds[i] = std::sqrt(dx * dx + dz * dz);
            p.head[i] = std::atan2(dz, dx);
            p.station[i] = p.total;
            p.total += p.ds[i];
        }
        p.k.assign(p.count, 0.0);
        for (std::size_t i = 0; i < p.count; ++i) {
            if (!p.closed && i + 1 >= p.count) break;
            const std::size_t next = (i + 1) % p.count;
            const double span = 0.5 * (p.ds[i] + p.ds[next]);
            // A very short chord makes its heading noise-dominated (0.5 m on a
            // 150 m radius reads tens of degrees off), so it inherits the
            // previous curvature instead of inventing a hairpin.
            if (span <= 1e-4 || p.ds[i] < 1.0 || p.ds[next] < 1.0) {
                p.k[i] = i > 0 ? p.k[i - 1] : 0.0;
                continue;
            }
            p.k[i] = wrapAngle(p.head[next] - p.head[i]) / span;
        }
        p.ks.assign(p.count, 0.0);
        for (std::size_t i = 0; i < p.count; ++i) {
            if (!p.closed && (i == 0 || i + 1 >= p.count)) {
                p.ks[i] = p.k[i];
                continue;
            }
            const std::size_t prev = p.closed ? (i + p.count - 1) % p.count : i - 1;
            const std::size_t next = (i + 1) % p.count;
            p.ks[i] = 0.25 * (p.k[prev] + 2.0 * p.k[i] + p.k[next]);
        }
    };
    measure();

    // A closed lap has no natural start: rotate it onto the straightest segment
    // so a corner is never cut in half at the seam.
    if (p.closed && p.count > 2) {
        std::size_t seam = 0;
        for (std::size_t i = 1; i < p.count; ++i) {
            if (std::fabs(p.k[i]) < std::fabs(p.k[seam])) seam = i;
        }
        p.seamStation = p.station[seam];
        if (seam != 0) std::rotate(p.pos.begin(), p.pos.begin() + std::ptrdiff_t(seam),
                                   p.pos.end());
        measure();
    }
    return p;
}

void analyzeCorners(const ai::AiSpline& spline, float min_radius, Report& report)
{
    Profile profile = buildProfile(spline);
    if (profile.count < 4 || profile.total < 10.0) return;

    const std::size_t segs = profile.count;
    const double threshold = 1.0 / double(min_radius > 0.0f ? min_radius : 400.0f);

    // Curvature is measured per segment, and Kunos ships its AI line at about
    // 1.5 m: at that spacing a centimetre of position noise already reads as a
    // 30 m radius, so the raw series reports hundreds of phantom corners
    // (Vallelunga: 161 corners for ~10 real ones). Average over a METRIC window
    // — the window has to follow the spacing, not the point count.
    {
        const double window = 8.0; // metres on each side
        std::vector<double> smoothed(segs, 0.0);
        for (std::size_t i = 0; i < segs; ++i) {
            double sum = profile.ks[i] * profile.ds[i];
            double weight = profile.ds[i];
            for (int direction = -1; direction <= 1; direction += 2) {
                double travelled = 0.0;
                for (std::size_t step = 1; step < segs && travelled < window; ++step) {
                    const long long raw = direction < 0
                                              ? (long long)i - (long long)step
                                              : (long long)i + (long long)step;
                    long long index = raw;
                    if (profile.closed) {
                        index = ((raw % (long long)segs) + (long long)segs) % (long long)segs;
                    } else if (raw < 0 || raw >= (long long)segs) {
                        break;
                    }
                    const std::size_t j = std::size_t(index);
                    travelled += profile.ds[j];
                    sum += profile.ks[j] * profile.ds[j];
                    weight += profile.ds[j];
                }
            }
            smoothed[i] = weight > 0.0 ? sum / weight : 0.0;
        }
        profile.ks = smoothed;
    }

    // 1. Raw runs: consecutive segments above threshold, short gaps bridged.
    struct Run {
        std::size_t first = 0;
        std::size_t last = 0;
    };
    std::vector<Run> runs;
    Run current;
    bool open = false;
    double gap = 0.0;
    for (std::size_t i = 0; i < segs; ++i) {
        const bool tight = std::fabs(profile.ks[i]) >= threshold;
        if (tight) {
            if (!open) {
                current = Run{i, i};
                open = true;
            } else {
                current.last = i;
            }
            gap = 0.0;
        } else if (open) {
            gap += profile.ds[i];
            if (gap > 12.0) {
                runs.push_back(current);
                open = false;
            }
        }
    }
    if (open) runs.push_back(current);

    int index = 0;
    for (const Run& run : runs) {
        std::vector<double> magnitudes;
        magnitudes.reserve(run.last - run.first + 1);
        for (std::size_t i = run.first; i <= run.last; ++i)
            magnitudes.push_back(std::fabs(profile.ks[i]));
        std::sort(magnitudes.begin(), magnitudes.end());
        // Median, not max: one noisy segment must not raise the peak and shred
        // a constant-radius corner into fragments.
        const double peak = magnitudes[magnitudes.size() / 2];
        if (peak <= 0.0) continue;

        // 2. Cut the run into single corners. Two independent criteria:
        //    - the curvature reverses sign: that is an S / chicane, and the two
        //      halves are different modules even with no straight between them;
        //    - the curvature dips to a third of the peak: that is the straight
        //      linking two corners of the same direction.
        std::vector<std::size_t> cuts;
        for (std::size_t i = run.first + 1; i <= run.last; ++i) {
            const double here = profile.ks[i];
            const double prev = profile.ks[i - 1];
            if ((here > 0.0) != (prev > 0.0) && std::fabs(here) > 1e-6 &&
                std::fabs(prev) > 1e-6) {
                cuts.push_back(i);
                continue;
            }
            if (i + 1 > run.last) continue;
            const double magnitude = std::fabs(here);
            if (magnitude > std::fabs(prev) ||
                magnitude > std::fabs(profile.ks[i + 1]))
                continue;
            if (magnitude >= 0.35 * peak) continue;
            if (!cuts.empty() && i - cuts.back() < 10) continue;
            cuts.push_back(i);
        }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

        std::vector<Run> pieces;
        std::size_t start = run.first;
        for (const std::size_t cut : cuts) {
            if (cut > start) pieces.push_back(Run{start, cut - 1});
            start = cut;
        }
        pieces.push_back(Run{start, run.last});

        for (const Run& piece : pieces) {
            double length = 0.0;
            double swept = 0.0;
            double best_k = -1.0;
            std::size_t apex = piece.first;
            for (std::size_t u = piece.first; u <= piece.last; ++u) {
                length += profile.ds[u];
                if (profile.closed || u + 1 < segs)
                    swept += wrapAngle(profile.head[(u + 1) % segs] - profile.head[u]);
                if (std::fabs(profile.ks[u]) > best_k) {
                    best_k = std::fabs(profile.ks[u]);
                    apex = u;
                }
            }
            const double angle_rad = std::fabs(swept);
            if (length < 8.0 || angle_rad < 0.087) continue; // < 8 m or < 5 deg

            CornerInfo corner;
            corner.index = ++index;
            // Y-up, XZ plane: a positive heading sweep is clockwise = a
            // right-hander (the same convention the AI line uses).
            corner.side = swept > 0.0 ? "right" : "left";
            // Radius at the apex: the span carries the widened entry/exit and
            // the length/angle ratio would read a few percent wide.
            corner.radius_m = best_k > 1e-6 ? 1.0 / best_k : 0.0;
            corner.station_in = profile.station[piece.first] + profile.seamStation;
            corner.station_out =
                profile.station[piece.last] + profile.ds[piece.last] + profile.seamStation;
            if (profile.closed && profile.total > 0.0) {
                while (corner.station_in >= profile.total) corner.station_in -= profile.total;
                while (corner.station_out >= profile.total) corner.station_out -= profile.total;
                while (corner.station_in < 0.0) corner.station_in += profile.total;
                while (corner.station_out < 0.0) corner.station_out += profile.total;
            }
            if (corner.station_out < corner.station_in) corner.station_out += profile.total;
            if (profile.total > 0.0 && length > 0.95 * profile.total)
                corner.station_out = corner.station_in + profile.total;
            corner.length_m = length;
            corner.angle_deg = angle_rad * 180.0 / kPi;
            corner.apex_station =
                profile.station[apex] + 0.5 * profile.ds[apex] + profile.seamStation;
            if (profile.closed && profile.total > 0.0) {
                while (corner.apex_station >= profile.total)
                    corner.apex_station -= profile.total;
            }

            const Vec3& a = profile.pos[apex];
            const Vec3& b = profile.pos[(apex + 1) % segs];
            corner.apex_position[0] = 0.5 * (a[0] + b[0]);
            corner.apex_position[1] = 0.5 * (a[1] + b[1]);
            corner.apex_position[2] = 0.5 * (a[2] + b[2]);
            report.corners.push_back(corner);
        }
    }
}

// ---------------------------------------------------------------------------
// Geometry passes
// ---------------------------------------------------------------------------

double median(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double pointLineDistance(const ProfilePoint& p, const ProfilePoint& a, const ProfilePoint& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9) return std::hypot(p.x - a.x, p.y - a.y);
    return std::fabs(dy * (p.x - a.x) - dx * (p.y - a.y)) / len;
}

// Douglas-Peucker: keeps the corners of the profile, drops collinear samples.
void simplifySection(std::vector<ProfilePoint>& points, double tolerance)
{
    if (points.size() < 3) return;
    std::vector<bool> keep(points.size(), false);
    keep.front() = true;
    keep.back() = true;
    std::vector<std::pair<std::size_t, std::size_t>> stack{{0, points.size() - 1}};
    while (!stack.empty()) {
        const std::pair<std::size_t, std::size_t> span = stack.back();
        stack.pop_back();
        double worst = tolerance;
        std::size_t index = 0;
        bool found = false;
        for (std::size_t i = span.first + 1; i < span.second; ++i) {
            const double d = pointLineDistance(points[i], points[span.first], points[span.second]);
            if (d > worst) {
                worst = d;
                index = i;
                found = true;
            }
        }
        if (!found) continue;
        keep[index] = true;
        stack.emplace_back(span.first, index);
        stack.emplace_back(index, span.second);
    }
    std::vector<ProfilePoint> kept;
    kept.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i)
        if (keep[i]) kept.push_back(points[i]);
    points.swap(kept);
}

// Builds the cross-section: lateral offset from the road edge on x, height
// above the asphalt on y. 5 cm bins with a median height reject the odd vertex
// that projects onto the wrong side.
void buildSection(const std::vector<double>& outward, const std::vector<double>& heights,
                  double roadHalf, std::vector<ProfilePoint>& section)
{
    section.clear();
    std::map<long long, std::vector<double>> bins;
    for (std::size_t i = 0; i < outward.size() && i < heights.size(); ++i) {
        const double x = outward[i] - roadHalf;
        if (x < -0.05) continue;
        const long long key = static_cast<long long>(std::llround(x / 0.05));
        bins[key].push_back(heights[i]);
    }
    if (bins.empty()) return;

    for (const auto& entry : bins) {
        ProfilePoint point;
        point.x = std::max(0.0, double(entry.first) * 0.05);
        point.y = std::max(0.0, median(entry.second));
        if (!section.empty() && std::fabs(point.x - section.back().x) < 1e-6) {
            section.back().y = std::max(section.back().y, point.y);
            continue;
        }
        section.push_back(point);
    }
    // A kerb that starts a few centimetres off the edge gets the asphalt
    // contact point, so a sweep closes down onto the road surface.
    if (!section.empty() && section.front().x > 0.03)
        section.insert(section.begin(), ProfilePoint{0.0, 0.0});
    simplifySection(section, 0.01);
}

bool stationInCorner(double s, const CornerInfo& corner, double margin, double total)
{
    (void)total; // stations are already normalised into [0, total)
    const double lo = corner.station_in - margin;
    const double hi = corner.station_out + margin;
    if (hi >= lo) return s >= lo && s <= hi;
    // Wrapped corner (crosses the start line).
    return s >= lo || s <= hi;
}

} // namespace

std::string classifyRole(const std::string& name)
{
    const std::string n = lower(name);
    for (const Keyword& keyword : kKeywords) {
        if (n.find(keyword.token) != std::string::npos) return keyword.role;
    }
    return "other";
}

Report analyze(const std::string& target, const std::string& aiPath, float minCornerRadius,
               bool meshCenterline)
{
    Report report;
    std::string kn5Path;
    std::error_code ec;

    engine::fileformat::Kn5File kn5;
    if (fs::is_directory(target, ec)) {
        // The main model is the biggest .kn5 in the folder.
        std::uintmax_t best = 0;
        for (const auto& entry : fs::directory_iterator(target, ec)) {
            if (entry.path().extension() != ".kn5") continue;
            std::error_code sizeEc;
            const std::uintmax_t size = fs::file_size(entry.path(), sizeEc);
            if (sizeEc) continue;
            if (size > best) {
                best = size;
                kn5Path = entry.path().string();
            }
        }
        report.ai.path = aiPath.empty() ? (fs::path(target) / "ai" / "fast_lane.ai").string()
                                        : aiPath;
    } else {
        kn5Path = target;
        report.ai.path = aiPath;
    }

    report.kn5_path = kn5Path;
    if (!kn5Path.empty() && fs::is_regular_file(kn5Path, ec)) {
        engine::fileformat::Kn5ParseResult parsed = engine::fileformat::parseKn5File(kn5Path);
        if (parsed.ok()) {
            report.kn5_loaded = true;
            report.kn5_version = parsed.file.version;
            report.material_count = parsed.file.materials.size();
            report.texture_count = parsed.file.textures.size();
            walkNodes(parsed.file.nodes, parsed.file.materials, report);
            kn5 = std::move(parsed.file);
        } else {
            report.error = parsed.error;
        }
    } else {
        report.error = "no .kn5 found in " + target;
    }

    std::sort(report.materials.begin(), report.materials.end());

    // Anchors: the pit pads and the start slots are cubes in the model, not ini
    // data. Their centres are what a garage row and a starting grid are built
    // from, and one pad is now shared by both cars of a garage.
    {
        auto suffixIndex = [](const std::string& name, const std::string& prefix) {
            if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
                return -1;
            int value = 0;
            for (std::size_t i = prefix.size(); i < name.size(); ++i) {
                if (name[i] < '0' || name[i] > '9') return -1;
                value = value * 10 + (name[i] - '0');
            }
            return value;
        };
        auto addAnchor = [](std::vector<AnchorInfo>& list, const NodeInfo& node, int index) {
            AnchorInfo anchor;
            anchor.name = node.name;
            anchor.index = index;
            for (int axis = 0; axis < 3; ++axis)
                anchor.center[axis] = 0.5 * (node.bbox_min[axis] + node.bbox_max[axis]);
            list.push_back(anchor);
        };
        for (const NodeInfo& node : report.nodes) {
            // Two nodes share a name in Kunos tracks (an empty parent plus the
            // cube); only the one carrying the mesh has usable bounds.
            if (!node.has_mesh || node.triangles == 0) continue;
            int index = suffixIndex(node.name, "AC_HOTLAP_START_");
            if (index >= 0) {
                addAnchor(report.hotlap_starts, node, index);
                continue;
            }
            index = suffixIndex(node.name, "AC_PIT_");
            if (index >= 0) {
                addAnchor(report.pads, node, index);
                continue;
            }
            index = suffixIndex(node.name, "AC_START_");
            if (index >= 0) addAnchor(report.grid_slots, node, index);
        }
        auto byIndex = [](const AnchorInfo& a, const AnchorInfo& b) { return a.index < b.index; };
        std::sort(report.pads.begin(), report.pads.end(), byIndex);
        std::sort(report.grid_slots.begin(), report.grid_slots.end(), byIndex);
        std::sort(report.hotlap_starts.begin(), report.hotlap_starts.end(), byIndex);
    }

    auto finishRoles = [&report]() {
        report.roles.clear();
        for (const NodeInfo& node : report.nodes) addUnique(report.roles, node.role);
        std::sort(report.roles.begin(), report.roles.end());
    };

    // The road samples feed both paths: an AI line refines them, and with no AI
    // line they ARE the centerline. Sample the meshes once, then choose.
    std::vector<MeshSample> samples;
    if (report.kn5_loaded) {
        std::size_t cursor = 0;
        // Total budget, not per node: a real track carries dozens of kerb
        // meshes, and a per-node budget would make the projection quadratic.
        const std::size_t meshes = std::max<std::size_t>(1, report.mesh_count);
        const std::size_t perNode = std::clamp<std::size_t>(60000 / meshes, 96, 20000);
        collectSamples(kn5.nodes, report, cursor, perNode, samples);
    }

    // Vertices of the asphalt, densely: the width measurement wants the two
    // edges of the ribbon, which the global budget above trims away.
    std::vector<MeshSample> roadSamples;
    if (report.kn5_loaded) {
        std::size_t roadCursor = 0;
        collectNodeSamples(kn5.nodes, report, roadCursor, "road", 2000000, roadSamples);
    }

    ai::AiSpline spline;
    if (!report.ai.path.empty() && fs::is_regular_file(report.ai.path, ec)) {
        spline = ai::AiFileReader::readSpline(report.ai.path);
        // The engine reader handles the ksengine format; a stock Kunos track
        // ships its line in the Kunos binary one, so try that before giving up.
        if (!spline.isValid()) readKunosAi(report.ai.path, spline);
    }

    if (spline.isValid()) {
        report.centerline_source = "ai";
        report.ai.loaded = true;
        report.ai.points = spline.size();
        report.ai.closed = spline.closed;
        report.ai.length_m = double(spline.totalDistance);
        bool first = true;
        for (const ai::AiSplinePoint& p : spline.points) {
            const double xyz[3] = {double(p.position.x), double(p.position.y),
                                   double(p.position.z)};
            for (int axis = 0; axis < 3; ++axis) {
                if (first || xyz[axis] < report.ai.bbox_min[axis])
                    report.ai.bbox_min[axis] = xyz[axis];
                if (first || xyz[axis] > report.ai.bbox_max[axis])
                    report.ai.bbox_max[axis] = xyz[axis];
            }
            first = false;
        }
    } else if (meshCenterline && buildMeshCenterline(kn5, report, spline, report.centerline_note)) {
        // Opt-in: there is no fast_lane.ai on disk and the caller asked the
        // asphalt to supply the reference line. Experimental — on Vallelunga the
        // stitched skeleton still wanders where the sectors leave large gaps, so
        // the measurements it produces there are not trustworthy yet.
        report.centerline_source = "skeleton";
    } else {
        // Without a reference line the inventory is all that can be reported:
        // widths, corners and kerb sections are all measured along it.
        finishRoles();
        return report;
    }

    const Frames frames(spline);
    if (report.ai.length_m < 1.0) report.ai.length_m = frames.total;

    // 1. Road surface in station bins.
    Bins bins;
    std::vector<double> halves;
    for (const MeshSample& sample : roadSamples) {
        if (report.nodes[sample.node_index].role != "road") continue;
        for (const Vec3& point : sample.points) {
            double s = 0.0, lateral = 0.0, dy = 0.0;
            frames.project(point, s, lateral, dy);
            Bin& bin = bins[static_cast<long long>(std::floor(s / kBinSize))];
            bin.minLat = std::min(bin.minLat, lateral);
            bin.maxLat = std::max(bin.maxLat, lateral);
            bin.sumDy += dy;
            ++bin.count;
        }
    }
    for (const auto& entry : bins) {
        double center = 0.0, half = 0.0, yOff = 0.0;
        if (!binAt(bins, double(entry.first) * kBinSize + 0.5 * kBinSize, center, half, yOff))
            continue;
        halves.push_back(half);
    }
    report.road_width_m = halves.empty() ? 0.0 : 2.0 * median(halves);
    report.centerline = bins.empty() ? report.centerline_source : "road_mesh";

    // 2. Nodes with an opaque name and an opaque material: decide from where
    //    the geometry sits relative to the asphalt.
    if (!bins.empty()) {
        const double half = 0.5 * report.road_width_m;
        for (std::size_t nodeIndex = 0; nodeIndex < report.nodes.size(); ++nodeIndex) {
            NodeInfo& node = report.nodes[nodeIndex];
            if (!node.has_mesh || node.role != "other") continue;
            const MeshSample* sample = nullptr;
            for (const MeshSample& candidate : samples) {
                if (candidate.node_index == nodeIndex) {
                    sample = &candidate;
                    break;
                }
            }
            if (sample == nullptr) continue;
            std::vector<double> outwardValues;
            std::vector<double> heightValues;
            for (const Vec3& point : sample->points) {
                double s = 0.0, lateral = 0.0, dy = 0.0;
                frames.project(point, s, lateral, dy);
                double center = 0.0, binHalf = 0.0, yOff = 0.0;
                if (!binAt(bins, s, center, binHalf, yOff)) continue;
                outwardValues.push_back(std::fabs(lateral - center));
                heightValues.push_back(dy - yOff);
            }
            if (outwardValues.empty()) continue;
            // Median over the sampled surface: a kerb strip belongs to the
            // asphalt edge for its whole length, a long bbox centre does not.
            const double outward = median(outwardValues);
            const double height = median(heightValues);
            // Raised surface next to the edge first: a kerb starts at the
            // asphalt, so testing "inside the road" first swallows it.
            if (outward >= half - 0.6 && outward <= half + 2.5 && height > 0.02 &&
                height < 0.30) {
                node.role = "kerb";
                node.role_source = "geometry";
            } else if (outward <= half + 0.4 && std::fabs(height) <= 0.02) {
                node.role = "road";
                node.role_source = "geometry";
            } else if (outward <= half + 40.0 && height <= 0.0 && height > -0.6) {
                node.role = "grass";
                node.role_source = "geometry";
            }
        }
    }

    // 3. Centerline from the asphalt: knots of the AI line pushed onto the
    //    measured road centre, at the measured height.
    ai::AiSpline centerline = spline;
    if (!bins.empty()) {
        for (std::size_t i = 0; i < centerline.points.size(); ++i) {
            const double s = frames.station[i];
            double center = 0.0, half = 0.0, yOff = 0.0;
            if (!binAt(bins, s, center, half, yOff)) continue;
            const Vec3 point = frames.pointAt(s, center);
            centerline.points[i].position.x = static_cast<float>(point[0]);
            centerline.points[i].position.y = static_cast<float>(point[1] + yOff);
            centerline.points[i].position.z = static_cast<float>(point[2]);
        }
    }

    // Corners are measured on the reference line the width can vouch for. The
    // refined centerline is only as good as the asphalt sampling: when the
    // measured width is implausible (the pit apron fusing with the racing
    // surface reads 39 m on Vallelunga) the refinement wanders and turns every
    // metre into a "corner" — 129 of them instead of 15. The raw line is then
    // the safer read, and the note says so.
    const bool refinedUsable = report.road_width_m >= 6.0 && report.road_width_m <= 20.0;
    if (!refinedUsable)
        report.centerline_note += " (corners on the raw line: measured width " +
                                  std::to_string(int(report.road_width_m)) + " m)";
    analyzeCorners(refinedUsable ? centerline : spline, minCornerRadius, report);

    // 4. Per-corner road width, straight off the bins.
    for (CornerInfo& corner : report.corners) {
        std::vector<double> widths;
        for (const auto& entry : bins) {
            const double s = double(entry.first) * kBinSize + 0.5 * kBinSize;
            if (entry.second.count <= 0) continue;
            if (!stationInCorner(s, corner, 4.0, report.ai.length_m)) continue;
            double center = 0.0, half = 0.0, yOff = 0.0;
            if (!binAt(bins, s, center, half, yOff)) continue;
            widths.push_back(2.0 * half);
        }
        if (!widths.empty()) corner.width_m = median(widths);
    }

    // 5. Trackside posts: two per side of every corner (entry and exit), on the
    //    outside of the asphalt plus a margin, plus the safety car bay past the
    //    last pit pad. Same pad idea as the garages: a footprint someone parks
    //    in, here for the safety car instead of a team car.
    {
        const double margin = 4.0;
        for (const CornerInfo& corner : report.corners) {
            // A post stands off the racing surface, not off the whole asphalt
            // layer: the measured width can be inflated by run-off or the pit
            // apron, so the lateral offset is capped.
            const double measuredHalf = corner.width_m > 0.0
                                            ? 0.5 * corner.width_m
                                            : (report.road_width_m > 0.0
                                                   ? 0.5 * report.road_width_m
                                                   : 6.0);
            const double halfWidth = std::min(measuredHalf, 10.0);
            for (int sideIndex = 0; sideIndex < 2; ++sideIndex) {
                const double sign = sideIndex == 0 ? -1.0 : 1.0;
                for (int end = 0; end < 2; ++end) {
                    const double station = end ? corner.station_out : corner.station_in;
                    // Every corner gets its four posts. A station that falls in
                    // a tear of the asphalt has no lane offset to read, so the
                    // post is placed off the reference line instead of being
                    // dropped — a corner missing a marshal is worse than one
                    // standing a metre wide.
                    double center = 0.0, half = 0.0, yOff = 0.0;
                    if (!binAt(bins, station, center, half, yOff)) {
                        center = 0.0;
                        yOff = 0.0;
                    }
                    const Vec3 point = frames.pointAt(station, center + sign * (halfWidth + margin));
                    PostInfo post;
                    post.corner = corner.index;
                    post.side = sign > 0.0 ? "right" : "left";
                    post.station = station;
                    post.center[0] = point[0];
                    post.center[1] = point[1] + yOff;
                    post.center[2] = point[2];
                    report.marshal_posts.push_back(post);
                }
            }
        }
        if (report.pads.size() >= 2) {
            const AnchorInfo& first = report.pads.front();
            const AnchorInfo& last = report.pads.back();
            double dirX = last.center[0] - first.center[0];
            double dirZ = last.center[2] - first.center[2];
            const double length = std::hypot(dirX, dirZ);
            if (length > 1.0) {
                report.safety_car.center[0] = last.center[0] + dirX / length * 25.0;
                report.safety_car.center[1] = last.center[1];
                report.safety_car.center[2] = last.center[2] + dirZ / length * 25.0;
                report.safety_car.name = "safety-car bay (derived from the pit row)";
                report.safety_car.index = 0;
                report.has_safety_car = true;
            }
        }
    }

    // 6. Kerb cross-section per corner: lateral run and height above the road.
    if (!bins.empty()) {
        std::vector<std::pair<int, std::size_t>> kerbNodes; // report index, node id
        for (std::size_t i = 0; i < report.nodes.size(); ++i) {
            if (report.nodes[i].role == "kerb" && report.nodes[i].has_mesh)
                kerbNodes.emplace_back(static_cast<int>(i), i);
        }
        for (const CornerInfo& corner : report.corners) {
            std::vector<double> outwardBySign; // signed lateral relative to centre
            std::vector<std::pair<double, int>> heights; // height, node index
            std::size_t usedNodes = 0;
            for (const MeshSample& sample : samples) {
                if (report.nodes[sample.node_index].role != "kerb") continue;
                bool contributed = false;
                for (const Vec3& point : sample.points) {
                    double s = 0.0, lateral = 0.0, dy = 0.0;
                    frames.project(point, s, lateral, dy);
                    if (!stationInCorner(s, corner, 2.0, report.ai.length_m)) continue;
                    double center = 0.0, half = 0.0, yOff = 0.0;
                    if (!binAt(bins, s, center, half, yOff)) continue;
                    const double outward = lateral - center;
                    if (std::fabs(outward) < 0.5 * half) continue; // on the road
                    outwardBySign.push_back(outward);
                    heights.emplace_back(dy - yOff, 0);
                    contributed = true;
                }
                if (contributed) ++usedNodes;
            }
            if (outwardBySign.empty()) continue;

            // Kerbs sit on one side: pick the dominant sign and measure there.
            double sum = 0.0;
            for (const double value : outwardBySign) sum += value;
            const double sign = sum >= 0.0 ? 1.0 : -1.0;
            std::vector<double> magnitudes;
            std::vector<double> heightsOnSide;
            for (std::size_t i = 0; i < outwardBySign.size(); ++i) {
                if (sign * outwardBySign[i] <= 0.0) continue;
                magnitudes.push_back(sign * outwardBySign[i]);
                heightsOnSide.push_back(heights[i].first);
            }
            if (magnitudes.size() < 2) continue;

            KerbProfile profile;
            profile.corner = corner.index;
            profile.side = sign > 0.0 ? "right" : "left";
            profile.station_in = corner.station_in;
            profile.station_out = corner.station_out;
            profile.width_m = *std::max_element(magnitudes.begin(), magnitudes.end()) -
                              *std::min_element(magnitudes.begin(), magnitudes.end());
            // Cross-section before the percentile sort, which would unpair the
            // two arrays: x from the road edge, y above the asphalt.
            buildSection(magnitudes, heightsOnSide, 0.5 * report.road_width_m, profile.section);
            std::sort(heightsOnSide.begin(), heightsOnSide.end());
            const double top = heightsOnSide[static_cast<std::size_t>(
                double(heightsOnSide.size() - 1) * 0.95)];
            profile.height_m = std::max(0.0, top);
            if (profile.width_m > 0.05 && profile.height_m > 0.001 &&
                profile.width_m < 6.0 && profile.height_m < 0.4)
                report.kerbs.push_back(profile);
            (void)usedNodes;
        }
    }

    report.roles.clear();
    for (const NodeInfo& node : report.nodes) addUnique(report.roles, node.role);
    std::sort(report.roles.begin(), report.roles.end());
    return report;
}

std::string toJson(const Report& report, std::size_t max_nodes)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    auto escape = [](const std::string& value) {
        std::string escaped;
        for (char c : value) {
            if (c == '"' || c == '\\') escaped.push_back('\\');
            escaped.push_back(c);
        }
        return escaped;
    };

    out << "{\n";
    out << "  \"version\": 2,\n";
    out << "  \"kn5\": \"" << escape(report.kn5_path) << "\",\n";
    out << "  \"kn5_loaded\": " << (report.kn5_loaded ? "true" : "false") << ",\n";
    out << "  \"error\": \"" << escape(report.error) << "\",\n";
    out << "  \"kn5_format_version\": " << report.kn5_version << ",\n";
    out << "  \"counts\": {\"nodes\": " << report.node_count << ", \"meshes\": "
        << report.mesh_count << ", \"materials\": " << report.material_count
        << ", \"textures\": " << report.texture_count << "},\n";
    out << "  \"centerline\": \"" << escape(report.centerline) << "\",\n";
    out << "  \"centerline_source\": \"" << escape(report.centerline_source) << "\",\n";
    out << "  \"centerline_note\": \"" << escape(report.centerline_note) << "\",\n";
    out << "  \"road_width_m\": " << report.road_width_m << ",\n";

    out << "  \"materials\": [";
    for (std::size_t i = 0; i < report.materials.size(); ++i) {
        out << (i ? ", " : "") << "\"" << escape(report.materials[i]) << "\"";
    }
    out << "],\n";

    out << "  \"ai\": {\"path\": \"" << escape(report.ai.path) << "\", \"loaded\": "
        << (report.ai.loaded ? "true" : "false") << ", \"points\": " << report.ai.points
        << ", \"length_m\": " << report.ai.length_m << ", \"closed\": "
        << (report.ai.closed ? "true" : "false") << "},\n";

    auto anchors = [&out, &escape](const char* key, const std::vector<AnchorInfo>& list) {
        out << "  \"" << key << "\": [";
        for (std::size_t i = 0; i < list.size(); ++i) {
            const AnchorInfo& a = list[i];
            out << (i ? ", " : "") << "{\"name\": \"" << escape(a.name)
                << "\", \"index\": " << a.index << ", \"center\": [" << a.center[0] << ", "
                << a.center[1] << ", " << a.center[2] << "]}";
        }
        out << "],\n";
    };
    anchors("pads", report.pads);
    anchors("grid_slots", report.grid_slots);
    anchors("hotlap_starts", report.hotlap_starts);

    out << "  \"safety_car\": ";
    if (report.has_safety_car)
        out << "{\"name\": \"" << escape(report.safety_car.name) << "\", \"center\": ["
            << report.safety_car.center[0] << ", " << report.safety_car.center[1] << ", "
            << report.safety_car.center[2] << "], \"radius_m\": 8.00},";
    else
        out << "null,";
    out << "\n";

    out << "  \"marshal_posts\": [";
    for (std::size_t i = 0; i < report.marshal_posts.size(); ++i) {
        const PostInfo& p = report.marshal_posts[i];
        out << (i ? ", " : "") << "{\"corner\": " << p.corner << ", \"side\": \"" << p.side
            << "\", \"station\": " << p.station << ", \"center\": [" << p.center[0]
            << ", " << p.center[1] << ", " << p.center[2] << "]}";
    }
    out << "],\n";

    out << "  \"corners\": [\n";
    for (std::size_t i = 0; i < report.corners.size(); ++i) {
        const CornerInfo& c = report.corners[i];
        out << "    {\"index\": " << c.index << ", \"side\": \"" << c.side
            << "\", \"station_in\": " << c.station_in << ", \"station_out\": " << c.station_out
            << ", \"length_m\": " << c.length_m << ", \"radius_m\": " << c.radius_m
            << ", \"angle_deg\": " << c.angle_deg << ", \"width_m\": " << c.width_m
            << ", \"apex_station\": " << c.apex_station << ", \"apex\": ["
            << c.apex_position[0] << ", " << c.apex_position[1] << ", " << c.apex_position[2]
            << "]}" << (i + 1 < report.corners.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    out << "  \"kerbs\": [\n";
    for (std::size_t i = 0; i < report.kerbs.size(); ++i) {
        const KerbProfile& k = report.kerbs[i];
        out << "    {\"corner\": " << k.corner << ", \"side\": \"" << k.side
            << "\", \"station_in\": " << k.station_in << ", \"station_out\": " << k.station_out
            << ", \"width_m\": " << k.width_m << ", \"height_m\": " << k.height_m
            << ", \"section\": [";
        for (std::size_t j = 0; j < k.section.size(); ++j) {
            out << (j ? ", " : "") << "[" << k.section[j].x << ", " << k.section[j].y
                << "]";
        }
        out << "]}" << (i + 1 < report.kerbs.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    std::vector<const NodeInfo*> listed;
    listed.reserve(report.nodes.size());
    for (const NodeInfo& node : report.nodes) listed.push_back(&node);
    if (max_nodes > 0 && listed.size() > max_nodes) {
        std::partial_sort(listed.begin(), listed.begin() + std::ptrdiff_t(max_nodes),
                          listed.end(), [](const NodeInfo* a, const NodeInfo* b) {
                              return a->triangles > b->triangles;
                          });
        listed.resize(max_nodes);
    }
    out << "  \"nodes_shown\": " << listed.size() << ",\n";
    out << "  \"nodes\": [\n";
    for (std::size_t i = 0; i < listed.size(); ++i) {
        const NodeInfo& n = *listed[i];
        out << "    {\"name\": \"" << escape(n.name) << "\", \"role\": \"" << n.role
            << "\", \"role_source\": \"" << n.role_source << "\", \"class\": " << n.node_class
            << ", \"mesh\": " << (n.has_mesh ? "true" : "false") << ", \"material\": \""
            << escape(n.material) << "\", \"vertices\": " << n.vertices
            << ", \"triangles\": " << n.triangles << ", \"world\": [" << n.world[0] << ", "
            << n.world[1] << ", " << n.world[2] << "], \"bbox\": [" << n.bbox_min[0] << ", "
            << n.bbox_min[1] << ", " << n.bbox_min[2] << ", " << n.bbox_max[0] << ", "
            << n.bbox_max[1] << ", " << n.bbox_max[2] << "], \"lod\": [" << n.lod_in << ", "
            << n.lod_out << "]}" << (i + 1 < listed.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

std::string formatSummary(const Report& report)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    out << "kn5:  " << (report.kn5_loaded ? "OK " : "MISSING/INVALID ") << report.kn5_path
        << "\n";
    if (!report.error.empty()) out << "  error: " << report.error << "\n";
    if (report.kn5_loaded) {
        out << "  v" << report.kn5_version << ", nodes " << report.node_count << ", meshes "
            << report.mesh_count << ", materials " << report.material_count << ", textures "
            << report.texture_count << "\n";
        for (const std::string& role : report.roles) {
            std::size_t count = 0;
            std::size_t triangles = 0;
            std::size_t other = 0;
            for (const NodeInfo& node : report.nodes) {
                if (node.role != role) continue;
                ++count;
                triangles += node.triangles;
                if (node.role_source == "geometry") ++other;
            }
            out << "  role " << std::left << std::setw(12) << role << std::right << count
                << " nodes, " << triangles << " tris";
            if (other) out << " (" << other << " from geometry)";
            out << "\n";
        }
        if (!report.pads.empty())
            out << "  anchors " << report.pads.size() << " pit pads, "
                << report.grid_slots.size() << " grid slots\n";
        if (report.road_width_m > 0.0)
            out << "  road width " << report.road_width_m << " m (centerline: "
                << report.centerline << ")\n";
    }
    out << "ai:   " << (report.ai.loaded ? "OK " : "MISSING ") << report.ai.path
        << " (reference line: " << report.centerline_source << ")\n";
    if (!report.centerline_note.empty()) out << "  note: " << report.centerline_note << "\n";
    if (report.ai.loaded)
        out << "  points " << report.ai.points << ", length " << report.ai.length_m << " m, "
            << (report.ai.closed ? "closed" : "open") << "\n";
    if (report.ai.loaded || !report.corners.empty()) {
        out << "  corners: " << report.corners.size() << "\n";
        for (const CornerInfo& c : report.corners) {
            out << "    #" << c.index << " " << std::left << std::setw(6) << c.side
                << std::right << " r=" << std::setw(7) << c.radius_m << " m  ang="
                << std::setw(6) << c.angle_deg << "  w=" << std::setw(5) << c.width_m
                << "  s=" << c.station_in << ".." << c.station_out << "\n";
        }
        if (!report.kerbs.empty()) {
            out << "  kerbs:\n";
            for (const KerbProfile& k : report.kerbs) {
                out << "    corner " << k.corner << " " << std::left << std::setw(6) << k.side
                    << std::right << " w=" << k.width_m << " m  h=" << k.height_m << " m\n";
            }
        }
    }
    return out.str();
}

} // namespace trackmodules
} // namespace ks
