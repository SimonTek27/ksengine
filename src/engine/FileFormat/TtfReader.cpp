#include "TtfReader.h"

#include <algorithm>
#include <utility>

namespace ks {
namespace engine {
namespace fileformat {
namespace {

bool failWith(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::int16_t bei16(const std::uint8_t* p) { return static_cast<std::int16_t>(be16(p)); }

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::string tagName(std::uint32_t tag) {
    std::string s;
    s.reserve(4);
    for (int shift = 24; shift >= 0; shift -= 8) {
        const auto ch = static_cast<char>((tag >> shift) & 0xFFu);
        s.push_back(ch >= 0x20 && ch < 0x7F ? ch : '?');
    }
    return s;
}

constexpr int kMaxCompositeDepth = 8;

/** cmap subtable preference: Windows UCS-4, Windows UCS-2, Unicode, Mac Roman. */
struct CmapKey {
    std::uint16_t platform;
    std::uint16_t encoding;
};
constexpr CmapKey kCmapPreference[] = {
    {3, 10}, {3, 1}, {0, 4}, {0, 3}, {0, 2}, {0, 1}, {0, 0}, {1, 0},
};

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

}  // namespace

bool TtfReader::isTrueType(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 12) return false;
    const std::uint32_t scaler = be32(data);
    return scaler == 0x00010000u || scaler == 0x74727565u;
}

std::uint32_t TtfReader::makeTag(const char* fourcc) {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(fourcc[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(fourcc[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(fourcc[2])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(fourcc[3]));
}

const TtfReader::Table* TtfReader::findTable(const char* tag) const {
    const std::uint32_t want = makeTag(tag);
    for (const auto& t : m_tables) {
        if (t.tag == want) return &t;
    }
    return nullptr;
}

bool TtfReader::inRange(std::size_t offset, std::size_t length) const {
    return offset <= m_data.size() && length <= m_data.size() - offset;
}

bool TtfReader::readU8(std::size_t offset, std::uint8_t& out) const {
    if (!inRange(offset, 1)) return false;
    out = m_data[offset];
    return true;
}

bool TtfReader::readU16(std::size_t offset, std::uint16_t& out) const {
    if (!inRange(offset, 2)) return false;
    out = be16(m_data.data() + offset);
    return true;
}

bool TtfReader::readI16(std::size_t offset, std::int16_t& out) const {
    if (!inRange(offset, 2)) return false;
    out = bei16(m_data.data() + offset);
    return true;
}

bool TtfReader::readU32(std::size_t offset, std::uint32_t& out) const {
    if (!inRange(offset, 4)) return false;
    out = be32(m_data.data() + offset);
    return true;
}

bool TtfReader::load(const std::uint8_t* data, std::size_t size, std::string* error) {
    m_data.clear();
    m_tables.clear();
    m_family.clear();
    m_cmapSubtable = 0;
    m_cmapFormat = 0;
    m_numGlyphs = 0;
    m_unitsPerEm = 0;
    m_ascender = 0;
    m_descender = 0;
    m_lineGap = 0;
    m_numberOfHMetrics = 0;
    m_locaIsLong = false;
    m_error.clear();

    auto fail = [&](const std::string& message) -> bool {
        m_error = message;
        if (error != nullptr) *error = message;
        return false;
    };

    if (data == nullptr || size < 12) return fail("font too small");

    const std::uint32_t scaler = be32(data);
    if (scaler == 0x4F54544Fu) return fail("CFF/OTTO font is not supported");
    if (scaler == 0x74746366u) return fail("TrueType collection (ttcf) is not supported");
    if (scaler != 0x00010000u && scaler != 0x74727565u) {
        return fail("not a TrueType font (bad scaler type)");
    }

    m_data.assign(data, data + size);

    const std::uint16_t numTables = be16(data + 4);
    if (numTables == 0) return fail("font declares 0 tables");
    if (numTables > 512) return fail("table count out of range");
    const std::size_t recordsEnd = 12 + static_cast<std::size_t>(numTables) * 16;
    if (recordsEnd > size) return fail("table directory truncated");

    m_tables.reserve(numTables);
    for (std::uint16_t i = 0; i < numTables; ++i) {
        const std::uint8_t* rec = data + 12 + static_cast<std::size_t>(i) * 16;
        Table t;
        t.tag = be32(rec);
        t.offset = be32(rec + 8);
        t.length = be32(rec + 12);
        if (t.offset >= size || static_cast<std::size_t>(t.length) > size - t.offset) {
            return fail("table '" + tagName(t.tag) + "' out of file bounds");
        }
        m_tables.push_back(t);
    }

    const Table* head = findTable("head");
    if (head == nullptr || head->length < 54) return fail("missing or short 'head' table");
    if (!readU16(head->offset + 18, m_unitsPerEm) || m_unitsPerEm == 0) {
        return fail("'head' has no unitsPerEm");
    }
    std::int16_t locFormat = 0;
    if (!readI16(head->offset + 50, locFormat)) return fail("'head' indexToLocFormat out of range");
    m_locaIsLong = locFormat == 1;

    const Table* maxp = findTable("maxp");
    if (maxp == nullptr || maxp->length < 6) return fail("missing or short 'maxp' table");
    if (!readU16(maxp->offset + 4, m_numGlyphs) || m_numGlyphs == 0) {
        return fail("'maxp' declares 0 glyphs");
    }

    const Table* hhea = findTable("hhea");
    if (hhea != nullptr && hhea->length >= 36) {
        readI16(hhea->offset + 4, m_ascender);
        readI16(hhea->offset + 6, m_descender);
        readI16(hhea->offset + 8, m_lineGap);
        readU16(hhea->offset + 34, m_numberOfHMetrics);
    }
    if (m_numberOfHMetrics == 0 || m_numberOfHMetrics > m_numGlyphs) {
        m_numberOfHMetrics = m_numGlyphs;
    }
    const Table* hmtx = findTable("hmtx");
    if (hmtx != nullptr) {
        const std::size_t need = static_cast<std::size_t>(m_numberOfHMetrics) * 4;
        const std::size_t lsbNeed =
            static_cast<std::size_t>(m_numGlyphs - m_numberOfHMetrics) * 2;
        if (hmtx->length < need + lsbNeed && hmtx->length < need) {
            return fail("'hmtx' shorter than numberOfHMetrics");
        }
    }

    const Table* cmap = findTable("cmap");
    if (cmap == nullptr || cmap->length < 4) return fail("missing 'cmap' table");
    {
        const std::uint8_t* base = m_data.data() + cmap->offset;
        const std::uint16_t nsub = be16(base + 2);
        for (const auto& key : kCmapPreference) {
            for (std::uint16_t i = 0; i < nsub; ++i) {
                const std::size_t rec = cmap->offset + 4 + static_cast<std::size_t>(i) * 8;
                std::uint16_t plat = 0;
                std::uint16_t enc = 0;
                std::uint32_t rel = 0;
                if (!readU16(rec, plat) || !readU16(rec + 2, enc) || !readU32(rec + 4, rel)) {
                    continue;
                }
                if (plat != key.platform || enc != key.encoding) continue;
                const std::size_t abs = cmap->offset + rel;
                std::uint16_t fmt = 0;
                if (!readU16(abs, fmt)) continue;
                if (fmt != 4 && fmt != 12 && fmt != 6 && fmt != 0) continue;
                m_cmapSubtable = static_cast<std::uint32_t>(abs);
                m_cmapFormat = fmt;
                break;
            }
            if (m_cmapSubtable != 0) break;
        }
    }
    if (m_cmapSubtable == 0) return fail("font has no usable cmap subtable");

    const Table* loca = findTable("loca");
    if (loca != nullptr) {
        const std::size_t entry = m_locaIsLong ? 4u : 2u;
        const std::size_t need = (static_cast<std::size_t>(m_numGlyphs) + 1) * entry;
        if (loca->length < need) return fail("'loca' shorter than numGlyphs + 1");
    }

    const Table* name = findTable("name");
    if (name != nullptr && name->length >= 6 && inRange(name->offset, name->length)) {
        const std::uint8_t* s = m_data.data() + name->offset;
        const std::uint16_t count = be16(s + 2);
        const std::uint16_t stringsAt = be16(s + 4);
        const std::size_t recordsEndAt = name->offset + 6 + static_cast<std::size_t>(count) * 12;
        for (std::uint16_t i = 0; i < count && m_family.empty(); ++i) {
            const std::size_t rec = name->offset + 6 + static_cast<std::size_t>(i) * 12;
            if (rec + 12 > recordsEndAt) break;
            const std::uint16_t platform = be16(s + 6 + static_cast<std::size_t>(i) * 12);
            const std::uint16_t nameId = be16(s + 6 + static_cast<std::size_t>(i) * 12 + 6);
            const std::uint16_t len = be16(s + 6 + static_cast<std::size_t>(i) * 12 + 8);
            const std::uint16_t off = be16(s + 6 + static_cast<std::size_t>(i) * 12 + 10);
            if (nameId != 1) continue;
            if (platform != 3 && platform != 1) continue;
            const std::size_t at = name->offset + stringsAt + off;
            if (!inRange(at, len)) continue;
            if (platform == 1) {
                m_family.assign(reinterpret_cast<const char*>(m_data.data() + at), len);
            } else {
                for (std::uint16_t k = 0; k + 1 < len; k += 2) {
                    std::uint32_t cp = be16(m_data.data() + at + k);
                    if (cp >= 0xD800u && cp < 0xDC00u && k + 3 < len) {
                        const std::uint32_t lo = be16(m_data.data() + at + k + 2);
                        if (lo >= 0xDC00u && lo < 0xE000u) {
                            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                            k += 2;
                        }
                    }
                    appendUtf8(m_family, cp);
                }
            }
        }
    }

    return true;
}

bool TtfReader::cmapLookup(std::uint32_t codepoint, std::uint32_t& gid) const {
    gid = 0;
    if (m_cmapSubtable == 0) return false;
    const std::uint8_t* s = m_data.data() + m_cmapSubtable;

    if (m_cmapFormat == 4) {
        if (codepoint > 0xFFFFu) return false;
        std::uint16_t segCountX2 = 0;
        if (!readU16(m_cmapSubtable + 6, segCountX2) || segCountX2 < 2) return false;
        const std::uint16_t segCount = static_cast<std::uint16_t>(segCountX2 / 2);
        const std::size_t ends = m_cmapSubtable + 14;
        const std::size_t starts = ends + segCountX2 + 2;
        const std::size_t deltas = starts + segCountX2;
        const std::size_t rangeOffsets = deltas + segCountX2;
        if (!inRange(rangeOffsets, segCountX2)) return false;
        const auto cp16 = static_cast<std::uint16_t>(codepoint);
        for (std::uint16_t i = 0; i < segCount; ++i) {
            std::uint16_t end = 0;
            if (!readU16(ends + static_cast<std::size_t>(i) * 2, end)) return false;
            if (cp16 > end) continue;
            std::uint16_t start = 0;
            std::int16_t delta = 0;
            std::uint16_t rangeOffset = 0;
            if (!readU16(starts + static_cast<std::size_t>(i) * 2, start)) return false;
            if (cp16 < start) return false;
            if (!readI16(deltas + static_cast<std::size_t>(i) * 2, delta)) return false;
            if (!readU16(rangeOffsets + static_cast<std::size_t>(i) * 2, rangeOffset)) return false;
            if (rangeOffset == 0) {
                const std::uint16_t g =
                    static_cast<std::uint16_t>((cp16 + delta) & 0xFFFFu);
                gid = g;
                return g != 0;
            }
            const std::size_t at = rangeOffsets + static_cast<std::size_t>(i) * 2 +
                                   rangeOffset + static_cast<std::size_t>(cp16 - start) * 2;
            std::uint16_t g = 0;
            if (!readU16(at, g)) return false;
            if (g != 0) g = static_cast<std::uint16_t>((g + delta) & 0xFFFFu);
            gid = g;
            return g != 0;
        }
        return false;
    }

    if (m_cmapFormat == 12) {
        std::uint32_t groups = 0;
        if (!readU32(m_cmapSubtable + 12, groups)) return false;
        std::size_t lo = 0;
        std::size_t hi = groups;
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            const std::size_t rec = m_cmapSubtable + 16 + mid * 12;
            std::uint32_t a = 0;
            std::uint32_t b = 0;
            std::uint32_t g = 0;
            if (!readU32(rec, a) || !readU32(rec + 4, b) || !readU32(rec + 8, g)) return false;
            if (codepoint < a) {
                hi = mid;
            } else if (codepoint > b) {
                lo = mid + 1;
            } else {
                gid = g + (codepoint - a);
                return gid != 0;
            }
        }
        return false;
    }

    if (m_cmapFormat == 6) {
        std::uint16_t first = 0;
        std::uint16_t count = 0;
        if (!readU16(m_cmapSubtable + 6, first) || !readU16(m_cmapSubtable + 8, count)) {
            return false;
        }
        if (codepoint < first || codepoint >= first + count) return false;
        std::uint16_t g = 0;
        if (!readU16(m_cmapSubtable + 10 + (codepoint - first) * 2, g)) return false;
        gid = g;
        return g != 0;
    }

    if (m_cmapFormat == 0) {
        if (codepoint > 0xFFu) return false;
        std::uint8_t g = 0;
        if (!readU8(m_cmapSubtable + 6 + codepoint, g)) return false;
        gid = g;
        return g != 0;
    }

    return false;
}

std::uint32_t TtfReader::glyphIndex(std::uint32_t codepoint) const {
    std::uint32_t gid = 0;
    cmapLookup(codepoint, gid);
    return gid;
}

std::uint16_t TtfReader::advanceWidth(std::uint16_t gid) const {
    const Table* hmtx = findTable("hmtx");
    if (hmtx == nullptr || m_numberOfHMetrics == 0) return 0;
    if (gid >= m_numGlyphs) return 0;
    const std::uint16_t slot =
        gid < m_numberOfHMetrics ? gid : static_cast<std::uint16_t>(m_numberOfHMetrics - 1);
    std::uint16_t advance = 0;
    if (!readU16(hmtx->offset + static_cast<std::size_t>(slot) * 4, advance)) return 0;
    return advance;
}

std::int16_t TtfReader::leftSideBearing(std::uint16_t gid) const {
    const Table* hmtx = findTable("hmtx");
    if (hmtx == nullptr || m_numberOfHMetrics == 0) return 0;
    if (gid >= m_numGlyphs) return 0;
    std::size_t at = 0;
    if (gid < m_numberOfHMetrics) {
        at = hmtx->offset + static_cast<std::size_t>(gid) * 4 + 2;
    } else {
        at = hmtx->offset + static_cast<std::size_t>(m_numberOfHMetrics) * 4 +
             static_cast<std::size_t>(gid - m_numberOfHMetrics) * 2;
    }
    std::int16_t lsb = 0;
    if (!readI16(at, lsb)) return 0;
    return lsb;
}

bool TtfReader::glyphRange(std::uint16_t gid, std::size_t& offset, std::size_t& length) const {
    const Table* loca = findTable("loca");
    const Table* glyf = findTable("glyf");
    if (loca == nullptr || glyf == nullptr) return false;
    const std::size_t entry = m_locaIsLong ? 4u : 2u;
    const std::size_t at = loca->offset + static_cast<std::size_t>(gid) * entry;
    if (!inRange(at, entry * 2)) return false;
    const std::uint8_t* p = m_data.data() + at;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    if (m_locaIsLong) {
        a = be32(p);
        b = be32(p + 4);
    } else {
        a = static_cast<std::uint32_t>(be16(p)) * 2u;
        b = static_cast<std::uint32_t>(be16(p + 2)) * 2u;
    }
    if (b < a) return false;
    if (static_cast<std::size_t>(a) > glyf->length) return false;
    if (static_cast<std::size_t>(b) > glyf->length) return false;
    offset = glyf->offset + a;
    length = static_cast<std::size_t>(b - a);
    return true;
}

bool TtfReader::readGlyph(std::uint16_t gid, TtfGlyphOutline& out, int depth,
                          std::string* error) const {
    out.clear();
    if (gid >= m_numGlyphs) return failWith(error, "glyph id out of range");
    std::size_t offset = 0;
    std::size_t length = 0;
    if (!glyphRange(gid, offset, length)) {
        return failWith(error, "glyph loca entry out of range");
    }
    if (length == 0) return true;  // blank glyph (.notdef, space, ...)
    if (length < 10) return failWith(error, "glyph record too small");
    const std::int16_t nContours = bei16(m_data.data() + offset);
    if (nContours >= 0) return readSimpleGlyph(offset, length, out, error);
    return readCompositeGlyph(offset, length, out, depth, error);
}

bool TtfReader::readSimpleGlyph(std::size_t glyphOffset, std::size_t glyphLength,
                                TtfGlyphOutline& out, std::string* error) const {
    const std::size_t end = glyphOffset + glyphLength;
    const std::uint8_t* p = m_data.data();
    const std::uint16_t nContours = be16(p + glyphOffset);
    std::size_t cursor = glyphOffset + 10;
    if (nContours == 0) return true;

    if (cursor + static_cast<std::size_t>(nContours) * 2 + 2 > end) {
        return failWith(error, "glyph endPts truncated");
    }
    std::vector<std::uint16_t> endPts(nContours);
    for (std::uint16_t i = 0; i < nContours; ++i) {
        endPts[i] = be16(p + cursor + static_cast<std::size_t>(i) * 2);
    }
    cursor += static_cast<std::size_t>(nContours) * 2;
    const std::uint16_t instructionLength = be16(p + cursor);
    cursor += 2 + instructionLength;
    if (cursor > end) return failWith(error, "glyph instructions truncated");

    const std::size_t numPoints = static_cast<std::size_t>(endPts.back()) + 1;
    std::vector<std::uint8_t> flags;
    flags.reserve(numPoints);
    while (flags.size() < numPoints) {
        if (cursor >= end) return failWith(error, "glyph flags truncated");
        const std::uint8_t flag = p[cursor++];
        flags.push_back(flag);
        if ((flag & 0x08u) != 0) {
            if (cursor >= end) return failWith(error, "glyph flag repeat truncated");
            const std::uint8_t repeats = p[cursor++];
            for (std::uint8_t r = 0; r < repeats && flags.size() < numPoints; ++r) {
                flags.push_back(flag);
            }
        }
    }

    std::vector<std::int32_t> xs(numPoints, 0);
    std::int32_t x = 0;
    for (std::size_t i = 0; i < numPoints; ++i) {
        const std::uint8_t flag = flags[i];
        if ((flag & 0x02u) != 0) {
            if (cursor >= end) return failWith(error, "glyph x deltas truncated");
            const std::int32_t dx = p[cursor++];
            x += (flag & 0x10u) != 0 ? dx : -dx;
        } else if ((flag & 0x10u) == 0) {
            if (cursor + 2 > end) return failWith(error, "glyph x deltas truncated");
            x += bei16(p + cursor);
            cursor += 2;
        }
        xs[i] = x;
    }

    std::vector<std::int32_t> ys(numPoints, 0);
    std::int32_t y = 0;
    for (std::size_t i = 0; i < numPoints; ++i) {
        const std::uint8_t flag = flags[i];
        if ((flag & 0x04u) != 0) {
            if (cursor >= end) return failWith(error, "glyph y deltas truncated");
            const std::int32_t dy = p[cursor++];
            y += (flag & 0x20u) != 0 ? dy : -dy;
        } else if ((flag & 0x20u) == 0) {
            if (cursor + 2 > end) return failWith(error, "glyph y deltas truncated");
            y += bei16(p + cursor);
            cursor += 2;
        }
        ys[i] = y;
    }

    std::size_t start = 0;
    for (std::uint16_t c = 0; c < nContours; ++c) {
        const std::size_t stop = endPts[c];
        if (stop < start || stop >= numPoints) {
            return failWith(error, "glyph contour range invalid");
        }
        std::vector<TtfPoint> pts;
        pts.reserve(stop - start + 1);
        for (std::size_t i = start; i <= stop; ++i) {
            TtfPoint pt;
            pt.x = static_cast<float>(xs[i]);
            pt.y = static_cast<float>(ys[i]);
            pt.onCurve = (flags[i] & 0x01u) != 0;
            pts.push_back(pt);
        }
        if (!pts.empty()) out.contours.push_back(std::move(pts));
        start = stop + 1;
    }
    return true;
}

bool TtfReader::readCompositeGlyph(std::size_t glyphOffset, std::size_t glyphLength,
                                   TtfGlyphOutline& out, int depth, std::string* error) const {
    if (depth >= kMaxCompositeDepth) return failWith(error, "composite glyph nesting too deep");
    const std::size_t end = glyphOffset + glyphLength;
    const std::uint8_t* p = m_data.data();
    std::size_t cursor = glyphOffset + 10;
    if (cursor > end) return failWith(error, "composite glyph truncated");

    while (true) {
        if (cursor + 4 > end) return failWith(error, "composite component truncated");
        const std::uint16_t flags = be16(p + cursor);
        const std::uint16_t componentGid = be16(p + cursor + 2);
        cursor += 4;

        const bool words = (flags & 0x0001u) != 0;
        const bool xyValues = (flags & 0x0002u) != 0;
        std::int32_t arg1 = 0;
        std::int32_t arg2 = 0;
        if (words) {
            if (cursor + 4 > end) return failWith(error, "composite args truncated");
            if (xyValues) {
                arg1 = bei16(p + cursor);
                arg2 = bei16(p + cursor + 2);
            } else {
                arg1 = static_cast<std::uint16_t>(be16(p + cursor));
                arg2 = static_cast<std::uint16_t>(be16(p + cursor + 2));
            }
            cursor += 4;
        } else {
            if (cursor + 2 > end) return failWith(error, "composite args truncated");
            if (xyValues) {
                arg1 = static_cast<std::int8_t>(p[cursor]);
                arg2 = static_cast<std::int8_t>(p[cursor + 1]);
            } else {
                arg1 = p[cursor];
                arg2 = p[cursor + 1];
            }
            cursor += 2;
        }

        float a = 1.f;
        float b = 0.f;
        float c = 0.f;
        float d = 1.f;
        auto readF2 = [&](std::size_t at, float& value) -> bool {
            if (at + 2 > end) return false;
            value = static_cast<float>(bei16(p + at)) / 16384.f;
            return true;
        };
        if ((flags & 0x0008u) != 0) {
            float scale = 1.f;
            if (!readF2(cursor, scale)) return failWith(error, "composite scale truncated");
            a = scale;
            d = scale;
            cursor += 2;
        } else if ((flags & 0x0040u) != 0) {
            float sx = 1.f;
            float sy = 1.f;
            if (!readF2(cursor, sx) || !readF2(cursor + 2, sy)) {
                return failWith(error, "composite xy scale truncated");
            }
            a = sx;
            d = sy;
            cursor += 4;
        } else if ((flags & 0x0080u) != 0) {
            float fa = 1.f;
            float fb = 0.f;
            float fc = 0.f;
            float fd = 1.f;
            if (!readF2(cursor, fa) || !readF2(cursor + 2, fb) || !readF2(cursor + 4, fc) ||
                !readF2(cursor + 6, fd)) {
                return failWith(error, "composite 2x2 matrix truncated");
            }
            a = fa;
            b = fb;
            c = fc;
            d = fd;
            cursor += 8;
        }

        const float e = xyValues ? static_cast<float>(arg1) : 0.f;
        const float f = xyValues ? static_cast<float>(arg2) : 0.f;

        TtfGlyphOutline component;
        if (!readGlyph(componentGid, component, depth + 1, error)) return false;
        for (auto& contour : component.contours) {
            for (auto& pt : contour) {
                const float px = pt.x;
                const float py = pt.y;
                pt.x = a * px + c * py + e;
                pt.y = b * px + d * py + f;
            }
            out.contours.push_back(std::move(contour));
        }

        if ((flags & 0x0020u) == 0) break;  // no MORE_COMPONENTS
    }
    return true;
}

bool TtfReader::outline(std::uint16_t gid, TtfGlyphOutline& out, std::string* error) const {
    return readGlyph(gid, out, 0, error);
}

}  // namespace fileformat
}  // namespace engine
}  // namespace ks
