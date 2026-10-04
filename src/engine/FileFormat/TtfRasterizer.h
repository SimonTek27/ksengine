#pragma once
/**
 * Quadratic-outline rasterizer for TtfReader.
 *
 * Coverage is computed per pixel row with a non-zero winding fill: the row is
 * sampled on `supersample` sub-scanlines, crossings are sorted and spans are
 * clipped per pixel, so horizontal coverage is exact and vertical coverage has
 * `supersample` levels of anti-aliasing.
 */
#include <cstdint>
#include <string>
#include <vector>

#include "TtfAtlas.h"
#include "TtfReader.h"

namespace ks {
namespace engine {
namespace fileformat {

struct TtfGlyphBitmap {
    int width = 0;
    int height = 0;
    int left = 0;  // bitmap left edge relative to pen origin, px
    int top = 0;   // bitmap top edge relative to baseline, px (y down, <= 0 typical)
    std::vector<std::uint8_t> pixels;

    bool empty() const { return pixels.empty(); }
    std::size_t ink() const;
};

struct TtfRasterOptions {
    float pixelSize = 16.f;      // em size in pixels
    int supersample = 8;         // sub-scanlines per pixel row (1 = aliased)
    float flattenTolerance = 0.3f; // max quadratic deviation, px
    int padding = 1;             // empty pixels around a glyph box
    int maxAtlasWidth = 1024;    // shelf-packing row limit
};

bool rasterizeGlyph(const TtfReader& font, std::uint16_t gid, const TtfRasterOptions& options,
                    TtfGlyphBitmap& out, std::string* error = nullptr);

/** Rasterizes ASCII `firstChar`..`lastChar` (defaults 32..126) into a packed atlas. */
bool buildTtfAtlas(const TtfReader& font, const TtfRasterOptions& options, TtfAtlas& out,
                   std::string* error = nullptr);

}  // namespace fileformat
}  // namespace engine
}  // namespace ks
