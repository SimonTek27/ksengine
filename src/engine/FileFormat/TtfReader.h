#pragma once
#include "KsExport.h"
/**
 * TrueType (sfnt) font reader, std-only.
 *
 * Parses the tables needed to place and draw text: head/maxp/hhea/hmtx/cmap/
 * loca/glyf. Glyph outlines come back as contours of TrueType points (on/off
 * curve flags included) so a rasterizer can flatten them.
 *
 * CFF/OTTO fonts are rejected explicitly - no outline is available without a
 * PostScript interpreter.
 */
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ks {
namespace engine {
namespace fileformat {

/** Outline point in font units (y up, unscaled). */
struct TtfPoint {
    float x = 0.f;
    float y = 0.f;
    bool onCurve = false;
};

/** One closed contour; points are in TrueType order (quadratic segments). */
struct TtfGlyphOutline {
    std::vector<std::vector<TtfPoint>> contours;

    bool empty() const {
        for (const auto& c : contours) {
            if (!c.empty()) return false;
        }
        return true;
    }

    void clear() { contours.clear(); }
};

class KSENGINE_API TtfReader {
public:
    /** True if `data` starts with a supported sfnt scaler type. */
    static bool isTrueType(const std::uint8_t* data, std::size_t size);

    /** Copies `data` and parses it. On failure returns false and fills error(). */
    bool load(const std::uint8_t* data, std::size_t size, std::string* error = nullptr);
    bool load(const std::string& bytes, std::string* error = nullptr) {
        return load(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), error);
    }

    /** Diagnostics of the last load(); later calls report through *error. */
    const std::string& error() const { return m_error; }

    int tableCount() const { return static_cast<int>(m_tables.size()); }
    bool hasTable(const char* tag) const { return findTable(tag) != nullptr; }

    std::uint16_t numGlyphs() const { return m_numGlyphs; }
    std::uint16_t unitsPerEm() const { return m_unitsPerEm; }
    std::int16_t ascender() const { return m_ascender; }
    std::int16_t descender() const { return m_descender; }
    std::int16_t lineGap() const { return m_lineGap; }
    std::uint16_t numberOfHMetrics() const { return m_numberOfHMetrics; }
    bool locaIsLong() const { return m_locaIsLong; }

    /** 0 when the code point is not in cmap (not the same as ".notdef"). */
    std::uint32_t glyphIndex(std::uint32_t codepoint) const;

    /** Advance width in font units; 0 for an out-of-range glyph id. */
    std::uint16_t advanceWidth(std::uint16_t gid) const;
    std::int16_t leftSideBearing(std::uint16_t gid) const;

    bool outline(std::uint16_t gid, TtfGlyphOutline& out, std::string* error = nullptr) const;

    const std::string& familyName() const { return m_family; }

private:
    struct Table {
        std::uint32_t tag = 0;
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
    };

    static std::uint32_t makeTag(const char* fourcc);

    const Table* findTable(const char* tag) const;

    bool readU8(std::size_t offset, std::uint8_t& out) const;
    bool readU16(std::size_t offset, std::uint16_t& out) const;
    bool readI16(std::size_t offset, std::int16_t& out) const;
    bool readU32(std::size_t offset, std::uint32_t& out) const;
    bool inRange(std::size_t offset, std::size_t length) const;

    bool readGlyph(std::uint16_t gid, TtfGlyphOutline& out, int depth,
                   std::string* error) const;
    bool readSimpleGlyph(std::size_t glyphOffset, std::size_t glyphLength,
                         TtfGlyphOutline& out, std::string* error) const;
    bool readCompositeGlyph(std::size_t glyphOffset, std::size_t glyphLength,
                            TtfGlyphOutline& out, int depth, std::string* error) const;
    bool glyphRange(std::uint16_t gid, std::size_t& offset, std::size_t& length) const;
    bool cmapLookup(std::uint32_t codepoint, std::uint32_t& gid) const;

    std::vector<std::uint8_t> m_data;
    std::vector<Table> m_tables;
    std::string m_error;
    std::string m_family;

    std::uint32_t m_cmapSubtable = 0;  // absolute offset, 0 = none
    std::uint16_t m_cmapFormat = 0;
    std::uint16_t m_numGlyphs = 0;
    std::uint16_t m_unitsPerEm = 0;
    std::int16_t m_ascender = 0;
    std::int16_t m_descender = 0;
    std::int16_t m_lineGap = 0;
    std::uint16_t m_numberOfHMetrics = 0;
    bool m_locaIsLong = false;
};

}  // namespace fileformat
}  // namespace engine
}  // namespace ks
