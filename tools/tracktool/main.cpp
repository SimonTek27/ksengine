// ks_tracktool — track folder tooling (roadmap 3.3): scaffold a new track
// folder, validate an existing one, inspect an AI line file. Qt-free, built
// on TrackLoader + AiFileWriter.
//
// Exit codes: 0 = ok, 1 = validation/IO failure, 2 = usage error.

#include "TrackToolCore.h"

#include <cstdio>
#include <string>

namespace {

void usage(std::FILE* out)
{
    std::fputs(
        "usage: ks_tracktool <command> [args]\n"
        "  scaffold <dir>      create model.kn5 + data/surfaces.ini +\n"
        "                      ai/fast_lane.ai (oval line), then validate\n"
        "  validate <dir>      validate an existing track folder\n"
        "  spline-info <file>  print summary of an AI line file\n",
        out);
}

int splineInfo(const char* path)
{
    const ks::ai::AiSpline spline = ks::ai::AiFileReader::readSpline(path);
    if (!spline.isValid()) {
        std::fprintf(stderr, "ks_tracktool: cannot read AI spline: %s\n", path);
        return 1;
    }
    float minSpeed = spline.points.front().speed;
    float maxSpeed = spline.points.front().speed;
    for (const ks::ai::AiSplinePoint& point : spline.points) {
        if (point.speed < minSpeed) minSpeed = point.speed;
        if (point.speed > maxSpeed) maxSpeed = point.speed;
    }
    std::printf("ai spline: %s\n", path);
    std::printf("  points:      %zu\n", spline.points.size());
    std::printf("  length:      %.2f m\n", static_cast<double>(spline.totalDistance));
    std::printf("  closed:      %s\n", spline.closed ? "yes" : "no");
    std::printf("  speed range: %.2f .. %.2f m/s\n", static_cast<double>(minSpeed),
                static_cast<double>(maxSpeed));
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    const std::string command = argv[1];

    if (command == "scaffold" && argc == 3) {
        std::string error;
        if (!ks::tracktool::scaffoldTrack(argv[2], error)) {
            std::fprintf(stderr, "ks_tracktool: %s\n", error.c_str());
            return 1;
        }
        std::printf("scaffolded track: %s\n", argv[2]);
        const ks::tracktool::TrackReport report =
            ks::tracktool::validateTrack(argv[2]);
        std::fputs(ks::tracktool::formatReport(report).c_str(), stdout);
        return report.ok ? 0 : 1;
    }

    if (command == "validate" && argc == 3) {
        const ks::tracktool::TrackReport report =
            ks::tracktool::validateTrack(argv[2]);
        std::fputs(ks::tracktool::formatReport(report).c_str(), stdout);
        return report.ok ? 0 : 1;
    }

    if (command == "spline-info" && argc == 3)
        return splineInfo(argv[2]);

    usage(stderr);
    return 2;
}
