#pragma once
/**
 * Loads a real TrueType font into a FontAtlas (Qt-free).
 * Kept out of FontAtlas.h so the TTF parser/rasterizer stay out of every
 * translation unit that only renders text.
 */
#include <string>

namespace ks {
namespace sim {
namespace ui {

class FontAtlas;

/**
 * Reads `path`, rasterizes ASCII 32..126 at `pixelSize` px and rebuilds
 * `atlas` in place. Returns false and fills *error on any failure (the atlas
 * is left untouched in that case).
 */
bool loadTtfFont(FontAtlas& atlas, const std::string& path, float pixelSize,
                 std::string* error = nullptr);

}  // namespace ui
}  // namespace sim
}  // namespace ks
