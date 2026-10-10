#pragma once
#include "KsExport.h"

// Minimal PNG reader for heightmaps: 8- or 16-bit grayscale, non-interlaced.
// That is exactly what TrackTerrainEditor::saveTerrain writes
// (QImage::Format_Grayscale8), so a heightmap edited in kseditor can be
// loaded by the Qt-free simulator. RGB/palette/interlaced files are rejected
// rather than guessed at.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ks::engine::fileformat {

struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint16_t> pixels; // row-major, 0..65535 (8-bit source scaled by 257)
};

KSENGINE_API bool loadPngGray(std::string_view bytes, GrayImage& out, std::string* error = nullptr);
KSENGINE_API bool loadPngGrayFile(const std::string& path, GrayImage& out, std::string* error = nullptr);

} // namespace ks::engine::fileformat
