#pragma once
/**
 * Packed R8 atlas produced by TtfRasterizer.
 * Light header: only data, no parsing or rasterization code, so UI headers can
 * include it without pulling in the font pipeline.
 */
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ks {
namespace engine {
namespace fileformat {

struct TtfAtlasGlyph {
    float u0 = 0.f;
    float v0 = 0.f;
    float u1 = 0.f;
    float v1 = 0.f;
    float xoff = 0.f;    // bitmap left edge relative to the pen, px
    float yoff = 0.f;    // bitmap top edge relative to baseline, px (y down, <= 0 typical)
    float advance = 0.f; // pen advance, px
    float width = 0.f;
    float height = 0.f;
};

struct TtfAtlas {
    int firstChar = 32;
    int lastChar = 126;
    int width = 0;
    int height = 0;
    float pixelSize = 0.f;
    float ascent = 0.f;   // px above baseline
    float descent = 0.f;  // px below baseline (positive)
    float lineHeight = 0.f;
    float whiteU0 = 0.f;
    float whiteV0 = 0.f;
    float whiteU1 = 0.f;
    float whiteV1 = 0.f;
    std::vector<std::uint8_t> pixels;  // R8, width * height
    std::vector<TtfAtlasGlyph> glyphs; // indexed by codepoint - firstChar

    int glyphCount() const { return lastChar - firstChar + 1; }

    bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) &&
               glyphs.size() == static_cast<std::size_t>(glyphCount());
    }

    const TtfAtlasGlyph& glyph(char ch) const {
        int code = static_cast<unsigned char>(ch);
        if (code < firstChar || code > lastChar) code = static_cast<unsigned char>('?');
        if (code < firstChar || code > lastChar) code = firstChar;
        return glyphs[static_cast<std::size_t>(code - firstChar)];
    }
};

}  // namespace fileformat
}  // namespace engine
}  // namespace ks
