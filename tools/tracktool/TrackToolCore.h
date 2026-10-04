#pragma once

// Track folder tooling (roadmap 3.3): validation report, scaffolding of a
// new track folder, and a generated oval racing line. Shared between the
// ks_tracktool CLI and the track_tool_test unit test.

#include <cstddef>
#include <string>
#include <vector>

#include "engine/AI/AiFileReader.h"

namespace ks {
namespace tracktool {

struct TrackReport {
    bool ok = false; // no errors (warnings allowed)
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    std::string name;
    std::string kn5Path;     // set when the KN5 header validated
    std::string aiSplinePath; // set when the spline loaded
    std::size_t splinePoints = 0;
    float trackLength = 0.0f;
    bool splineClosed = false;
    std::size_t surfaceCount = 0;
    float baseGrip = 1.0f;
};

// Validates an existing track folder through TrackLoader (KN5 header,
// AI spline, surfaces.ini). Never throws; IO problems become report errors.
TrackReport validateTrack(const std::string& trackDirectory);

// Creates trackDirectory/data + trackDirectory/ai and writes:
//   model.kn5         minimal structurally valid KN5 (v5, Base root)
//   data/surfaces.ini ROAD/KERB/GRASS friction map
//   ai/fast_lane.ai   generated oval racing line (AiFileWriter binary)
// Returns false with errorOut set on IO failure. Idempotent.
bool scaffoldTrack(const std::string& trackDirectory, std::string& errorOut);

// Closed oval line on the XZ plane: `points` stations at radius `radius`,
// constant curvature 1/radius, constant speed. distance/lap left at 0 —
// AiFileReader derives them on load.
ai::AiSpline generateOvalSpline(float radius = 500.0f, unsigned int points = 200,
                                float speed = 55.0f);

// Human readable multi-line summary for CLI output.
std::string formatReport(const TrackReport& report);

} // namespace tracktool
} // namespace ks
