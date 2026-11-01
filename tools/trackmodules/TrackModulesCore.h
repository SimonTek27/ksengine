#pragma once

// Track module catalog (roadmap 3.8 groundwork): reads a KN5 track model plus
// its AI racing line and reports what can be turned into a reusable module
// library — per-node inventory with a role (road / kerb / grass / ...), the
// corners of the layout measured on the ROAD SURFACE (not on the AI line),
// and the kerb cross-section of every corner (width + height above the road).
//
// Qt-free and dependency-free on purpose: same spirit as tools/tracktool
// (TrackToolCore). Built on Kn5Reader (real "sc6969" parser) and AiFileReader.

#include <cstddef>
#include <string>
#include <vector>

namespace ks {
namespace trackmodules {

struct NodeInfo {
    std::string name;
    std::string material; // KN5 material name ("" when material_id is invalid)
    std::string role;     // see classifyRole()
    std::string role_source; // "name" | "material" | "geometry" | "none"
    int node_class = 0;   // 1 Base | 2 Mesh | 3 SkinnedMesh
    bool has_mesh = false;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    double world[3] = {0.0, 0.0, 0.0};    // accumulated translation
    double bbox_min[3] = {0.0, 0.0, 0.0}; // world-space mesh bounds
    double bbox_max[3] = {0.0, 0.0, 0.0};
    float lod_in = 0.0f;
    float lod_out = 0.0f;
};

struct CornerInfo {
    int index = 0;    // 1-based, in lap order
    std::string side; // "left" | "right"
    double station_in = 0.0; // metres from the start line
    double station_out = 0.0;
    double length_m = 0.0;
    double radius_m = 0.0; // radius at the apex (1 / |curvature|)
    double angle_deg = 0.0;
    double apex_station = 0.0;
    double apex_position[3] = {0.0, 0.0, 0.0};
    double width_m = 0.0; // road width measured on the mesh
};

// One point of a kerb cross-section: distance from the road edge and height
// above the asphalt. A sweep along a centerline only needs this polyline.
struct ProfilePoint {
    double x = 0.0; // metres outward from the road edge (0 = asphalt contact)
    double y = 0.0; // metres above the road surface
};

// Kerb of one corner, as a cross-section: the measured polyline plus the two
// scalars a generator usually wants first.
struct KerbProfile {
    int corner = 0;
    std::string side; // side of the corner the kerb sits on
    double station_in = 0.0;
    double station_out = 0.0;
    double width_m = 0.0;
    double height_m = 0.0;
    int nodes = 0; // kerb nodes that contributed
    std::vector<ProfilePoint> section; // simplified (x, y) polyline
};

// Anchor cube found in the model ("AC_PIT_3", "AC_START_7",
// "AC_HOTLAP_START_0"): world-space centre of its mesh. The pit pads build the
// garage row (one shared pad per garage, see simulator/PitGarage.h); the start
// cubes are the race grid.
struct AnchorInfo {
    std::string name;
    int index = 0;
    double center[3] = {0.0, 0.0, 0.0};
};

// Trackside marshal post: the asset places two per corner side (entry and
// exit), i.e. four per corner. The simulator drives them at runtime
// (simulator/TracksidePosts.h), which is why only the pose is stored here.
struct PostInfo {
    int corner = 0;       // 1-based corner number, in lap order
    std::string side;     // "left" | "right"
    double station = 0.0; // metres from the start line
    double center[3] = {0.0, 0.0, 0.0};
};

struct AiInfo {
    bool loaded = false;
    std::string path;
    std::size_t points = 0;
    double length_m = 0.0;
    bool closed = false;
    double bbox_min[3] = {0.0, 0.0, 0.0};
    double bbox_max[3] = {0.0, 0.0, 0.0};
};

struct Report {
    std::string kn5_path;
    std::string error; // empty when the KN5 parsed (AI problems are not fatal)
    bool kn5_loaded = false;

    int kn5_version = 0;
    std::size_t node_count = 0;
    std::size_t mesh_count = 0;
    std::size_t material_count = 0;
    std::size_t texture_count = 0;

    std::vector<NodeInfo> nodes;
    std::vector<std::string> materials; // "name (shader)", unique, sorted
    std::vector<std::string> roles;     // unique role names present, sorted

    AiInfo ai;
    // "ai" when no road mesh was found, "road_mesh" when the measurements come
    // from the asphalt itself.
    std::string centerline = "ai";
    // Where the reference line came from: "ai" (a fast_lane.ai on disk),
    // "skeleton" (derived from the asphalt when no AI line exists) or "none".
    std::string centerline_source = "none";
    // Why the reference line has that source, in one line: the skeleton stages
    // a generated layout goes through, or the reason it could not be built.
    std::string centerline_note;

    // Sorted by index. Empty when the track carries no such cubes.
    std::vector<AnchorInfo> pads;         // AC_PIT_n
    std::vector<AnchorInfo> grid_slots;   // AC_START_n
    std::vector<AnchorInfo> hotlap_starts; // AC_HOTLAP_START_n

    // Four per corner (two right, two left) on the outside of the asphalt.
    std::vector<PostInfo> marshal_posts;
    // Safety car bay at the pit exit, derived from the pit row.
    AnchorInfo safety_car;
    bool has_safety_car = false;
    double road_width_m = 0.0; // median width of the road surface
    std::vector<CornerInfo> corners;
    std::vector<KerbProfile> kerbs;
};

// Classifies a name into a content role: road, kerb, grass, gravel, wall,
// fence, vegetation, grandstand, pit, building, startline, other.
std::string classifyRole(const std::string& name);

// Analyzes a track directory (picks its largest .kn5) or a single .kn5.
// ai_path defaults to <track>/ai/fast_lane.ai when the target is a directory.
// min_corner_radius: straight-line threshold in metres (400 = anything tighter
// than a 400 m radius counts as a corner).
// mesh_centerline: opt-in, experimental. With no AI line the reference line is
// derived from the asphalt itself (rasterise, thin, stitch the sectors). Off by
// default because the stitched result is not yet reliable on real assets.
Report analyze(const std::string& target, const std::string& ai_path = std::string(),
               float min_corner_radius = 400.0f, bool mesh_centerline = false);

// Deterministic JSON (indented), ready to be committed next to a track or
// pasted into a review. max_nodes > 0 keeps only the biggest meshes (by
// triangle count): a real track has hundreds of nodes and the catalog is
// meant to be reviewable.
std::string toJson(const Report& report, std::size_t max_nodes = 0);

// Short human-readable summary (counts + corner list + kerb profiles).
std::string formatSummary(const Report& report);

} // namespace trackmodules
} // namespace ks
