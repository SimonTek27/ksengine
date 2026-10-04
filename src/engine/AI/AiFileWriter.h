#pragma once

#include <string>

namespace ks {
namespace ai {

struct AiSpline;

/**
 * Writes Assetto Corsa AI line files (ai/fast_lane.ai).
 *
 * Inverse of AiFileReader's binary path; layout matches
 * AiSplineEditor::writeAiBinary in the Qt editor byte for byte, so files
 * are interchangeable between engine and editor:
 *   uint32 magic   = 0x00414900 ("\0AI")
 *   uint32 version = 1
 *   uint32 pointCount
 *   pointCount * { float x, y, z, curvature, speed }
 *
 * distance/lap are runtime-derived (AiFileReader::finishSpline), never
 * stored on disk.
 */
class AiFileWriter {
public:
    static bool writeSpline(const std::string& path, const AiSpline& spline);
};

} // namespace ai
} // namespace ks
