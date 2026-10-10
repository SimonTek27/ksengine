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
 * Reads Assetto Corsa AI line files (ai/fast_lane.ai).
 *
 * Binary layout (little endian), matching AiSplineEditor::parseAiBinary in
 * the Qt editor:
 *   uint32 magic   = 0x00414900 ("\0AI")
 *   uint32 version
 *   uint32 pointCount (<= 100000)
 *   pointCount * { float x, y, z, curvature, speed }
 *
 * Falls back to a comma separated text file (x,y,z[,curvature[,speed]])
 * when the magic does not match.
 */
class KSENGINE_API AiFileReader {
public:
    static AiSpline readSpline(const std::string& path);
};

} // namespace ai
} // namespace ks
