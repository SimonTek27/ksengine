#pragma once
#include "KsExport.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ks {
namespace ai {

struct AiVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct AiSplinePoint {
    AiVec3 position;
    float curvature = 0.0f;
    float speed = 0.0f;
    float distance = 0.0f;
    // Static splines describe a single lap; AIController derives lap
    // transitions from the point index, so this stays 0 on disk data.
    int lap = 0;
};

struct AiSpline {
    std::vector<AiSplinePoint> points;
    float totalDistance = 0.0f;
    bool closed = false;

    bool isValid() const { return points.size() >= 2; }
    bool empty() const { return points.empty(); }
    std::size_t size() const { return points.size(); }
};

/**
 * Reads Kinetika AI line files (ai/fast_lane.ai).
 *
 * Binary layout (little endian), matching AiSplineEditor::parseAiBinary in
 * the Qt editor:
 *   uint32 magic   = 0x00414900 ("\0AI")
 *   uint32 version
 *   uint32 pointCount (<= 100000)
 *   pointCount * { float x, y, z, curvature, speed }
 *
 * Falls back to a comma separated text file (x,y,z[,curvature[,speed]])
 * when the magic does not match, and to readLanesCsv when the file carries
 * a lanes.csv header (see below).
 */
class KSENGINE_API AiFileReader {
public:
    static AiSpline readSpline(const std::string& path);

    /**
     * Reads one lane out of a merged lanes.csv, the world-space dump of the
     * Kunos .ai splines:
     *
     *     source_file,row_index,x,y,z,dist_m,speed,radius_m,edge_left,edge_right
     *     fast_lane.ai,0,-55.06568,0.5738942,25.21376,0.000,30.28989,3105.588,...
     *
     * A single file holds several lanes, so lineName picks the group to load
     * ("fast_lane.ai", "ideal_line.ai", "pit_lane.ai"). Empty means "the
     * best racing line available": fast_lane, then ideal_line, then the
     * first lane that is not the pit lane.
     *
     * `speed` is m/s as authored. `radius_m` is a turning RADIUS, not a
     * curvature, so it is converted to 1/radius here — AIController multiplies
     * target speed by (1 - curvature * ...). Records that shipped with
     * sentinel garbage instead of a speed leave speed at 0 on disk; those are
     * interpolated from their neighbours so the AI does not brake to a stop
     * over a bad record.
     *
     * Returns an empty spline when the file is not lanes.csv (no source_file
     * column), so callers can fall through to the plain text reader.
     */
    static AiSpline readLanesCsv(const std::string& path,
                                 const std::string& lineName = std::string());
};

} // namespace ai
} // namespace ks
