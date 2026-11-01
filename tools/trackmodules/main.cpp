// ks_trackmodules — module catalog for track generation (roadmap 3.8).
//
//   ks_trackmodules <track_dir|model.kn5> [--ai <fast_lane.ai>]
//                   [--out <modules.json>] [--min-radius <m>]
//                   [--max-nodes <n>]
//
// Reads a Kunos-style track: the .kn5 supplies the per-node inventory (name,
// material, triangles, world bounds, role guess), the AI line supplies the
// layout in station coordinates (corners with radius, sweep angle, side and
// apex). The JSON is small enough to review or commit next to the track,
// unlike the model itself.
//
// Exit codes: 0 = ok, 1 = nothing analyzed (no KN5, no AI line), 2 = usage.

#include "TrackModulesCore.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void usage(std::FILE* out)
{
    std::fputs(
        "usage: ks_trackmodules <track_dir|model.kn5> [--ai <fast_lane.ai>]\n"
        "                       [--out <modules.json>] [--min-radius <m>]\n"
        "                       [--max-nodes <n>] [--mesh-centerline]\n"
        "  track_dir         uses the largest .kn5 and ai/fast_lane.ai in the folder\n"
        "  --min-radius      corner threshold in metres (default 400)\n"
        "  --max-nodes       export only the n biggest meshes (default: all)\n"
        "  --mesh-centerline experimental: with no AI line, derive the reference\n"
        "                    line from the asphalt mesh instead of stopping at the\n"
        "                    inventory\n",
        out);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    std::string target = argv[1];
    std::string aiPath;
    std::string outPath;
    float minRadius = 400.0f;
    std::size_t maxNodes = 0;
    bool meshCenterline = false;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--ai" && i + 1 < argc) {
            aiPath = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (arg == "--min-radius" && i + 1 < argc) {
            minRadius = std::strtof(argv[++i], nullptr);
            if (!(minRadius > 0.0f)) minRadius = 400.0f;
        } else if (arg == "--mesh-centerline") {
            meshCenterline = true;
        } else if (arg == "--max-nodes" && i + 1 < argc) {
            maxNodes = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
        } else {
            usage(stderr);
            return 2;
        }
    }

    const ks::trackmodules::Report report =
        ks::trackmodules::analyze(target, aiPath, minRadius, meshCenterline);

    std::fputs(ks::trackmodules::formatSummary(report).c_str(), stderr);

    const std::string json = ks::trackmodules::toJson(report, maxNodes);
    if (outPath.empty()) {
        std::fputs(json.c_str(), stdout);
    } else {
        std::FILE* file = std::fopen(outPath.c_str(), "wb");
        if (!file) {
            std::fprintf(stderr, "ks_trackmodules: cannot write %s\n", outPath.c_str());
            return 1;
        }
        std::fwrite(json.data(), 1, json.size(), file);
        std::fclose(file);
        std::printf("wrote %s\n", outPath.c_str());
    }

    if (!report.kn5_loaded && !report.ai.loaded) return 1;
    return 0;
}
