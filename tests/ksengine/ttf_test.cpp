// TrueType reader + rasterizer test: a byte-assembled synthetic font covering
// cmap (idDelta and idRangeOffset paths), simple and composite glyphs and the
// non-zero winding rasterizer, plus ground-truth checks against real AC fonts.

#include "engine/FileFormat/TtfRasterizer.h"
#include "engine/FileFormat/TtfReader.h"
#include "KsTest.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

namespace ff = ks::engine::fileformat;

using Bytes = std::vector<std::uint8_t>;

void putU16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void putI16(Bytes& b, std::int16_t v) { putU16(b, static_cast<std::uint16_t>(v)); }

void putU32(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void append(Bytes& b, const Bytes& other) { b.insert(b.end(), other.begin(), other.end()); }

struct TableSpec {
    const char* tag;
    Bytes data;
};

/** 5 glyphs: blank .notdef, square, quadratic curve, composite, blank (space). */
Bytes buildSyntheticFont() {
    Bytes square;
    putI16(square, 1);
    putI16(square, 100);
    putI16(square, 0);
    putI16(square, 700);
    putI16(square, 700);
    putU16(square, 3);  // endPtsOfContours
    putU16(square, 0);  // instructionLength
    for (int i = 0; i < 4; ++i) square.push_back(0x01);  // all on-curve, 16-bit deltas
    putI16(square, 100);
    putI16(square, 600);
    putI16(square, 0);
    putI16(square, -600);
    putI16(square, 0);
    putI16(square, 0);
    putI16(square, 700);
    putI16(square, 0);

    Bytes curved;
    putI16(curved, 1);
    putI16(curved, 100);
    putI16(curved, 0);
    putI16(curved, 700);
    putI16(curved, 900);
    putU16(curved, 4);
    putU16(curved, 0);
    const std::uint8_t flags[5] = {0x01, 0x01, 0x00, 0x01, 0x01};
    for (std::uint8_t f : flags) curved.push_back(f);
    putI16(curved, 100);
    putI16(curved, 0);
    putI16(curved, 300);
    putI16(curved, 300);
    putI16(curved, 0);
    putI16(curved, 0);
    putI16(curved, 700);
    putI16(curved, 200);
    putI16(curved, -200);
    putI16(curved, -700);

    Bytes composite;
    putI16(composite, -1);
    putI16(composite, 150);
    putI16(composite, 30);
    putI16(composite, 750);
    putI16(composite, 730);
    putU16(composite, 0x0003);  // ARG_1_AND_2_ARE_WORDS | ARGS_ARE_XY_VALUES
    putU16(composite, 1);       // component = the square
    putI16(composite, 50);
    putI16(composite, 30);

    const std::uint32_t len1 = static_cast<std::uint32_t>(square.size());
    const std::uint32_t len2 = static_cast<std::uint32_t>(curved.size());
    const std::uint32_t len3 = static_cast<std::uint32_t>(composite.size());
    Bytes glyf;
    append(glyf, square);
    append(glyf, curved);
    append(glyf, composite);

    Bytes loca;
    putU32(loca, 0);
    putU32(loca, 0);
    putU32(loca, len1);
    putU32(loca, len1 + len2);
    putU32(loca, len1 + len2 + len3);
    putU32(loca, len1 + len2 + len3);

    Bytes head;
    putU32(head, 0x00010000);
    putU32(head, 0x00010000);
    putU32(head, 0);
    putU32(head, 0x5F0F3CF5);
    putU16(head, 0);
    putU16(head, 1000);
    putU32(head, 0);
    putU32(head, 0);
    putU32(head, 0);
    putU32(head, 0);
    putI16(head, 100);
    putI16(head, 0);
    putI16(head, 750);
    putI16(head, 900);
    putU16(head, 0);
    putU16(head, 8);
    putI16(head, 2);
    putI16(head, 1);  // long loca
    putI16(head, 0);

    Bytes maxp;
    putU32(maxp, 0x00010000);
    putU16(maxp, 5);
    putU16(maxp, 8);
    putU16(maxp, 2);
    putU16(maxp, 8);
    putU16(maxp, 1);
    putU16(maxp, 2);
    putU16(maxp, 0);
    putU16(maxp, 0);
    putU16(maxp, 0);
    putU16(maxp, 0);
    putU16(maxp, 0);
    putU16(maxp, 0);
    putU16(maxp, 1);
    putU16(maxp, 1);

    Bytes hhea;
    putU32(hhea, 0x00010000);
    putI16(hhea, 800);
    putI16(hhea, -200);
    putI16(hhea, 0);
    putU16(hhea, 850);
    putI16(hhea, 100);
    putI16(hhea, 0);
    putI16(hhea, 750);
    putI16(hhea, 1);
    putI16(hhea, 0);
    putI16(hhea, 0);
    putI16(hhea, 0);
    putI16(hhea, 0);
    putI16(hhea, 0);
    putI16(hhea, 0);
    putI16(hhea, 0);
    putU16(hhea, 5);

    Bytes hmtx;
    const std::uint16_t advances[5] = {600, 800, 800, 850, 300};
    const std::int16_t lsbs[5] = {0, 100, 100, 150, 0};
    for (int i = 0; i < 5; ++i) {
        putU16(hmtx, advances[i]);
        putI16(hmtx, lsbs[i]);
    }

    Bytes cmap;
    putU16(cmap, 0);
    putU16(cmap, 1);
    putU16(cmap, 3);  // Windows
    putU16(cmap, 1);  // UCS-2
    putU32(cmap, 12);
    putU16(cmap, 4);  // format 4
    putU16(cmap, 54); // length
    putU16(cmap, 0);  // language
    putU16(cmap, 8);  // segCountX2
    putU16(cmap, 8);  // searchRange
    putU16(cmap, 2);  // entrySelector
    putU16(cmap, 0);  // rangeShift
    const std::uint16_t endCode[4] = {32, 63, 67, 0xFFFF};
    const std::uint16_t startCode[4] = {32, 63, 65, 0xFFFF};
    const std::int16_t idDelta[4] = {-28, -62, 0, 1};
    const std::uint16_t idRangeOffset[4] = {0, 0, 4, 0};
    const std::uint16_t glyphIdArray[3] = {1, 3, 2};
    for (std::uint16_t v : endCode) putU16(cmap, v);
    putU16(cmap, 0);  // reservedPad
    for (std::uint16_t v : startCode) putU16(cmap, v);
    for (std::int16_t v : idDelta) putI16(cmap, v);
    for (std::uint16_t v : idRangeOffset) putU16(cmap, v);
    for (std::uint16_t v : glyphIdArray) putU16(cmap, v);

    const TableSpec tables[] = {
        {"cmap", cmap}, {"glyf", glyf}, {"head", head}, {"hhea", hhea},
        {"hmtx", hmtx}, {"loca", loca}, {"maxp", maxp},
    };
    const std::uint16_t numTables = 7;

    Bytes file;
    putU32(file, 0x00010000);
    putU16(file, numTables);
    putU16(file, 64);  // searchRange = 16 * 2^floor(log2(7))
    putU16(file, 2);   // entrySelector
    putU16(file, 48);  // rangeShift
    const std::size_t directoryEnd = 12 + static_cast<std::size_t>(numTables) * 16;
    std::vector<std::uint32_t> offsets(numTables);
    std::size_t cursor = directoryEnd;
    for (std::uint16_t i = 0; i < numTables; ++i) {
        cursor = (cursor + 3) & ~static_cast<std::size_t>(3);
        offsets[i] = static_cast<std::uint32_t>(cursor);
        cursor += tables[i].data.size();
    }
    for (std::uint16_t i = 0; i < numTables; ++i) {
        for (int c = 0; c < 4; ++c) file.push_back(static_cast<std::uint8_t>(tables[i].tag[c]));
        putU32(file, 0);  // checksum (unchecked by this reader)
        putU32(file, offsets[i]);
        putU32(file, static_cast<std::uint32_t>(tables[i].data.size()));
    }
    file.resize(cursor, 0);
    for (std::uint16_t i = 0; i < numTables; ++i) {
        std::copy(tables[i].data.begin(), tables[i].data.end(), file.begin() + offsets[i]);
    }
    return file;
}

int recordIndex(const Bytes& font, const char* tag) {
    const std::uint16_t numTables = static_cast<std::uint16_t>((font[4] << 8) | font[5]);
    for (std::uint16_t i = 0; i < numTables; ++i) {
        const std::size_t rec = 12 + static_cast<std::size_t>(i) * 16;
        bool same = true;
        for (int c = 0; c < 4; ++c) same = same && font[rec + c] == static_cast<std::uint8_t>(tag[c]);
        if (same) return static_cast<int>(i);
    }
    return -1;
}

void patchU32(Bytes& font, std::size_t at, std::uint32_t value) {
    font[at + 0] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    font[at + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    font[at + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    font[at + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

void testSynthetic() {
    const Bytes font = buildSyntheticFont();
    ff::TtfReader reader;
    std::string err;
    const bool ok = reader.load(font.data(), font.size(), &err);
    ks::test::report(ok, err.empty() ? "load ok" : err.c_str(), __FILE__, __LINE__);
    if (!ok) return;

    KS_CHECK(reader.error().empty());
    KS_CHECK(reader.tableCount() == 7);
    KS_CHECK(reader.hasTable("glyf"));
    KS_CHECK(!reader.hasTable("kern"));
    KS_CHECK(reader.numGlyphs() == 5);
    KS_CHECK(reader.unitsPerEm() == 1000);
    KS_CHECK(reader.ascender() == 800);
    KS_CHECK(reader.descender() == -200);
    KS_CHECK(reader.lineGap() == 0);
    KS_CHECK(reader.numberOfHMetrics() == 5);
    KS_CHECK(reader.locaIsLong());

    KS_CHECK(reader.glyphIndex('A') == 1);
    KS_CHECK(reader.glyphIndex('B') == 3);
    KS_CHECK(reader.glyphIndex('C') == 2);
    KS_CHECK(reader.glyphIndex('?') == 1);
    KS_CHECK(reader.glyphIndex(' ') == 4);
    KS_CHECK(reader.glyphIndex('Z') == 0);
    KS_CHECK(reader.glyphIndex(0x1F600) == 0);  // above BMP: no format 4 match

    KS_CHECK(reader.advanceWidth(1) == 800);
    KS_CHECK(reader.advanceWidth(3) == 850);
    KS_CHECK(reader.advanceWidth(4) == 300);
    KS_CHECK(reader.leftSideBearing(1) == 100);
    KS_CHECK(reader.advanceWidth(99) == 0);

    ff::TtfGlyphOutline outlineData;
    err.clear();
    KS_CHECK(reader.outline(1, outlineData, &err));
    KS_CHECK(err.empty());
    KS_CHECK(outlineData.contours.size() == 1);
    if (outlineData.contours.size() == 1) {
        const auto& pts = outlineData.contours[0];
        KS_CHECK(pts.size() == 4);
        if (pts.size() == 4) {
            KS_CHECK_NEAR(pts[0].x, 100.f, 1e-4);
            KS_CHECK_NEAR(pts[0].y, 0.f, 1e-4);
            KS_CHECK_NEAR(pts[1].x, 700.f, 1e-4);
            KS_CHECK_NEAR(pts[2].x, 700.f, 1e-4);
            KS_CHECK_NEAR(pts[2].y, 700.f, 1e-4);
            KS_CHECK_NEAR(pts[3].x, 100.f, 1e-4);
            for (const auto& p : pts) KS_CHECK(p.onCurve);
        }
    }

    err.clear();
    KS_CHECK(reader.outline(2, outlineData, &err));
    KS_CHECK(outlineData.contours.size() == 1);
    if (outlineData.contours.size() == 1) {
        const auto& pts = outlineData.contours[0];
        KS_CHECK(pts.size() == 5);
        if (pts.size() == 5) {
            KS_CHECK(pts[0].onCurve);
            KS_CHECK(pts[1].onCurve);
            KS_CHECK(!pts[2].onCurve);
            KS_CHECK(pts[3].onCurve);
            KS_CHECK_NEAR(pts[2].x, 400.f, 1e-4);
            KS_CHECK_NEAR(pts[2].y, 900.f, 1e-4);
            KS_CHECK_NEAR(pts[4].y, 0.f, 1e-4);
        }
    }

    err.clear();
    KS_CHECK(reader.outline(3, outlineData, &err));
    KS_CHECK(outlineData.contours.size() == 1);
    if (outlineData.contours.size() == 1) {
        const auto& pts = outlineData.contours[0];
        KS_CHECK(pts.size() == 4);
        if (pts.size() == 4) {
            KS_CHECK_NEAR(pts[0].x, 150.f, 1e-4);
            KS_CHECK_NEAR(pts[0].y, 30.f, 1e-4);
            KS_CHECK_NEAR(pts[2].x, 750.f, 1e-4);
            KS_CHECK_NEAR(pts[2].y, 730.f, 1e-4);
        }
    }

    KS_CHECK(reader.outline(0, outlineData, &err));
    KS_CHECK(outlineData.empty());
    KS_CHECK(reader.outline(4, outlineData, &err));
    KS_CHECK(outlineData.empty());
    err.clear();
    KS_CHECK(!reader.outline(99, outlineData, &err));
    KS_CHECK(!err.empty());
}

void testRasterizeSynthetic() {
    const Bytes font = buildSyntheticFont();
    ff::TtfReader reader;
    std::string err;
    if (!reader.load(font.data(), font.size(), &err)) {
        ks::test::report(false, err.c_str(), __FILE__, __LINE__);
        return;
    }

    ff::TtfRasterOptions options;
    options.pixelSize = 100.f;
    options.padding = 1;

    // Square: font units 100..700 x 0..700 at scale 0.1 -> 10..70 px wide,
    // -70..0 px tall (y flipped), padded to a 62x72 box at (9, -71).
    ff::TtfGlyphBitmap square;
    err.clear();
    KS_CHECK(ff::rasterizeGlyph(reader, 1, options, square, &err));
    KS_CHECK(err.empty());
    KS_CHECK(square.width == 62);
    KS_CHECK(square.height == 72);
    KS_CHECK(square.left == 9);
    KS_CHECK(square.top == -71);
    // Every edge lands on an integer pixel boundary, so coverage is binary:
    // exactly 60x70 lit pixels and no partially covered ones.
    KS_CHECK(square.ink() == 4200);
    KS_CHECK(square.pixels[1 * 62 + 1] == 255);
    KS_CHECK(square.pixels[0] == 0);
    KS_CHECK(square.pixels[square.pixels.size() - 1] == 0);

    // Composite is the same square shifted (+50, +30) in font units.
    ff::TtfGlyphBitmap composite;
    err.clear();
    KS_CHECK(ff::rasterizeGlyph(reader, 3, options, composite, &err));
    KS_CHECK(composite.width == 62);
    KS_CHECK(composite.height == 72);
    KS_CHECK(composite.left == 14);
    KS_CHECK(composite.top == -74);
    KS_CHECK(composite.ink() == 4200);

    // Curved glyph: quadratic must flatten and produce anti-aliased pixels.
    ff::TtfGlyphBitmap curved;
    err.clear();
    KS_CHECK(ff::rasterizeGlyph(reader, 2, options, curved, &err));
    KS_CHECK(curved.width > 0 && curved.height > 0);
    KS_CHECK(curved.ink() > 100);
    int partial = 0;
    int solid = 0;
    for (std::uint8_t v : curved.pixels) {
        if (v == 255) ++solid;
        else if (v > 0) ++partial;
    }
    KS_CHECK(solid > 100);
    KS_CHECK(partial > 0);

    // Blank glyphs rasterize to an empty bitmap without error.
    ff::TtfGlyphBitmap blank;
    err.clear();
    KS_CHECK(ff::rasterizeGlyph(reader, 4, options, blank, &err));
    KS_CHECK(blank.empty());

    ff::TtfRasterOptions bad;
    bad.pixelSize = 0.f;
    err.clear();
    KS_CHECK(!ff::rasterizeGlyph(reader, 1, bad, square, &err));
    KS_CHECK(!err.empty());
}

void testAtlasSynthetic() {
    const Bytes font = buildSyntheticFont();
    ff::TtfReader reader;
    std::string err;
    if (!reader.load(font.data(), font.size(), &err)) {
        ks::test::report(false, err.c_str(), __FILE__, __LINE__);
        return;
    }

    ff::TtfRasterOptions options;
    options.pixelSize = 32.f;
    ff::TtfAtlas atlas;
    err.clear();
    KS_CHECK(ff::buildTtfAtlas(reader, options, atlas, &err));
    KS_CHECK(err.empty());
    KS_CHECK(atlas.valid());
    KS_CHECK(atlas.glyphCount() == 95);
    KS_CHECK(atlas.glyphs.size() == 95);
    KS_CHECK(atlas.width >= 2 && atlas.height >= 2);
    KS_CHECK_NEAR(atlas.pixelSize, 32.f, 1e-6);
    KS_CHECK_NEAR(atlas.ascent, 25.6f, 1e-4);   // 800 * 32/1000
    KS_CHECK_NEAR(atlas.descent, 6.4f, 1e-4);   // 200 * 32/1000
    KS_CHECK_NEAR(atlas.lineHeight, 32.f, 1e-4); // (800 + 200 + 0) * 0.032

    const auto& a = atlas.glyph('A');
    KS_CHECK_NEAR(a.advance, 25.6f, 1e-4);  // 800 units
    KS_CHECK(a.width > 0.f && a.height > 0.f);
    KS_CHECK(a.u1 > a.u0 && a.v1 > a.v0);
    KS_CHECK(a.u1 <= 1.f && a.v1 <= 1.f);

    const auto& space = atlas.glyph(' ');
    KS_CHECK_NEAR(space.advance, 9.6f, 1e-4);  // 300 units
    KS_CHECK(space.width == 0.f && space.height == 0.f);

    // 'B' is the composite glyph and must still get a real box.
    KS_CHECK(atlas.glyph('B').width > 0.f);

    // 'Z' is unmapped, so it falls back to '?' (mapped to the square).
    const auto& fallback = atlas.glyph('Z');
    KS_CHECK_NEAR(fallback.width, atlas.glyph('?').width, 1e-6);
    KS_CHECK(fallback.width > 0.f);

    std::size_t lit = 0;
    for (std::uint8_t v : atlas.pixels) {
        if (v != 0) ++lit;
    }
    KS_CHECK(lit > 100);

    const std::size_t whiteIdx =
        static_cast<std::size_t>(atlas.height - 2) * static_cast<std::size_t>(atlas.width);
    KS_CHECK(atlas.pixels[whiteIdx] == 255);
    KS_CHECK(atlas.pixels[whiteIdx + 1] == 255);
    KS_CHECK_NEAR(atlas.whiteV1, 1.f, 1e-6);
    KS_CHECK(atlas.whiteU1 > atlas.whiteU0);

    ff::TtfRasterOptions bad;
    bad.pixelSize = -1.f;
    err.clear();
    KS_CHECK(!ff::buildTtfAtlas(reader, bad, atlas, &err));
    KS_CHECK(!err.empty());
}

void testFailures() {
    const Bytes good = buildSyntheticFont();

    {
        ff::TtfReader reader;
        std::string err;
        KS_CHECK(!reader.load(good.data(), 4, &err));
        KS_CHECK(!err.empty());
    }
    {
        Bytes otto = good;
        otto[0] = 'O';
        otto[1] = 'T';
        otto[2] = 'T';
        otto[3] = 'O';
        ff::TtfReader reader;
        std::string err;
        KS_CHECK(!reader.load(otto.data(), otto.size(), &err));
        KS_CHECK(err.find("CFF/OTTO") != std::string::npos);
    }
    {
        Bytes bogus = good;
        bogus[0] = 0xDE;
        bogus[1] = 0xAD;
        bogus[2] = 0xBE;
        bogus[3] = 0xEF;
        ff::TtfReader reader;
        std::string err;
        KS_CHECK(!reader.load(bogus.data(), bogus.size(), &err));
        KS_CHECK(!err.empty());
    }
    {
        Bytes noTables = good;
        noTables[4] = 0;
        noTables[5] = 0;
        ff::TtfReader reader;
        std::string err;
        KS_CHECK(!reader.load(noTables.data(), noTables.size(), &err));
        KS_CHECK(err.find("0 tables") != std::string::npos);
    }
    {
        Bytes farOffset = good;
        const int rec = recordIndex(farOffset, "glyf");
        KS_CHECK(rec >= 0);
        if (rec >= 0) patchU32(farOffset, 12 + static_cast<std::size_t>(rec) * 16 + 8, 0x7FFFFFFF);
        ff::TtfReader reader;
        std::string err;
        KS_CHECK(!reader.load(farOffset.data(), farOffset.size(), &err));
        KS_CHECK(!err.empty());
    }
    {
        Bytes noCmap = good;
        const int rec = recordIndex(noCmap, "cmap");
        KS_CHECK(rec >= 0);
        if (rec >= 0) noCmap[12 + static_cast<std::size_t>(rec) * 16] = 'X';
        ff::TtfReader reader;
        std::string err;
        KS_CHECK(!reader.load(noCmap.data(), noCmap.size(), &err));
        KS_CHECK(err.find("cmap") != std::string::npos);
    }
    {
        Bytes shortLoca = good;
        const int rec = recordIndex(shortLoca, "loca");
        KS_CHECK(rec >= 0);
        if (rec >= 0) patchU32(shortLoca, 12 + static_cast<std::size_t>(rec) * 16 + 12, 4);
        ff::TtfReader reader;
        std::string err;
        KS_CHECK(!reader.load(shortLoca.data(), shortLoca.size(), &err));
        KS_CHECK(err.find("loca") != std::string::npos);
    }
    {
        ff::TtfReader reader;
        KS_CHECK(!ff::TtfReader::isTrueType(good.data(), 4));
        KS_CHECK(ff::TtfReader::isTrueType(good.data(), good.size()));
        KS_CHECK(!ff::TtfReader::isTrueType(nullptr, 0));
    }
}

struct RealExpect {
    const char* rel;
    std::uint16_t unitsPerEm;
    std::uint16_t numGlyphs;
    std::int16_t ascender;
    std::int16_t descender;
    std::int16_t lineGap;
    std::uint16_t numberOfHMetrics;
    std::uint32_t gidA;
    std::uint16_t advA;
    std::uint32_t gid0;
    std::uint32_t gidLowerA;
    std::uint32_t gidSpace;
    int contoursA;
};

bool readWhole(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return !out.empty();
}

void testRealFile(const RealExpect& expect, const fs::path& root) {
    const fs::path path = root / expect.rel;
    if (!fs::exists(path)) {
        std::printf("SKIP (missing) %s\n", expect.rel);
        return;
    }
    std::string bytes;
    if (!readWhole(path, bytes)) {
        ks::test::report(false, "read real font", __FILE__, __LINE__);
        return;
    }

    ff::TtfReader reader;
    std::string err;
    const bool ok =
        reader.load(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &err);
    ks::test::report(ok, err.empty() ? expect.rel : err.c_str(), __FILE__, __LINE__);
    if (!ok) return;

    KS_CHECK(reader.unitsPerEm() == expect.unitsPerEm);
    KS_CHECK(reader.numGlyphs() == expect.numGlyphs);
    KS_CHECK(reader.ascender() == expect.ascender);
    KS_CHECK(reader.descender() == expect.descender);
    KS_CHECK(reader.lineGap() == expect.lineGap);
    KS_CHECK(reader.numberOfHMetrics() == expect.numberOfHMetrics);
    KS_CHECK(reader.glyphIndex('A') == expect.gidA);
    KS_CHECK(reader.advanceWidth(static_cast<std::uint16_t>(expect.gidA)) == expect.advA);
    KS_CHECK(reader.glyphIndex('0') == expect.gid0);
    KS_CHECK(reader.glyphIndex('a') == expect.gidLowerA);
    KS_CHECK(reader.glyphIndex(' ') == expect.gidSpace);

    ff::TtfGlyphOutline outlineData;
    err.clear();
    KS_CHECK(reader.outline(static_cast<std::uint16_t>(expect.gidA), outlineData, &err));
    KS_CHECK(static_cast<int>(outlineData.contours.size()) == expect.contoursA);
    KS_CHECK(!outlineData.empty());

    ff::TtfRasterOptions options;
    options.pixelSize = 32.f;
    ff::TtfGlyphBitmap bitmap;
    err.clear();
    KS_CHECK(ff::rasterizeGlyph(reader, static_cast<std::uint16_t>(expect.gidA), options, bitmap,
                                &err));
    KS_CHECK(bitmap.width > 0 && bitmap.height > 0);
    KS_CHECK(bitmap.ink() > 0);
    KS_CHECK(bitmap.top <= 0);

    ff::TtfAtlas atlas;
    err.clear();
    KS_CHECK(ff::buildTtfAtlas(reader, options, atlas, &err));
    KS_CHECK(err.empty());
    KS_CHECK(atlas.valid());
    KS_CHECK(atlas.glyphs.size() == 95);
    std::size_t lit = 0;
    for (std::uint8_t v : atlas.pixels) {
        if (v != 0) ++lit;
    }
    KS_CHECK(lit > 0);
    KS_CHECK(atlas.glyph('A').width > 0.f);
    KS_CHECK(atlas.glyph('0').width > 0.f);
    KS_CHECK(atlas.glyph(' ').width == 0.f);
    KS_CHECK_NEAR(atlas.glyph('A').advance,
                  static_cast<double>(reader.advanceWidth(static_cast<std::uint16_t>(expect.gidA))) *
                      32.0 / static_cast<double>(expect.unitsPerEm),
                  1e-3);
}

void testRealSweep(const fs::path& root) {
    const char* dirs[] = {
        "content/fonts",
        "apps/lua/Paintshop/fonts",
        "extension/lua/new-modes",
        "extension/lua/pp-filters/vhs",
        "DynamicMusicPlayer/Fonts",
        "content/cars/rss_formula_1990_v10/extension/font",
        "content/cars/rss_formula_hybrid_v12-r/extension/display/assets",
        "content/cars/rss_gtm_lanzo_v10_evo2/extension/display",
    };

    int seen = 0;
    int loaded = 0;
    int rejected = 0;
    for (const char* rel : dirs) {
        const fs::path dir = root / rel;
        if (!fs::exists(dir)) continue;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".ttf") continue;
            ++seen;
            std::string bytes;
            if (!readWhole(it->path(), bytes)) continue;
            ff::TtfReader reader;
            std::string err;
            const bool ok = reader.load(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                        bytes.size(), &err);
            if (ok) {
                ++loaded;
                KS_CHECK(reader.numGlyphs() > 0);
                KS_CHECK(reader.unitsPerEm() >= 16 && reader.unitsPerEm() <= 16384);
                const std::uint32_t gidA = reader.glyphIndex('A');
                if (gidA != 0) {
                    ff::TtfRasterOptions options;
                    options.pixelSize = 24.f;
                    ff::TtfGlyphBitmap bitmap;
                    std::string rasterError;
                    KS_CHECK(ff::rasterizeGlyph(reader, static_cast<std::uint16_t>(gidA), options,
                                                bitmap, &rasterError));
                    KS_CHECK(bitmap.ink() > 0);
                }
            } else {
                ++rejected;
                const bool knownCff = err.find("CFF/OTTO") != std::string::npos;
                const bool knownTtcf = err.find("ttcf") != std::string::npos;
                ks::test::report(knownCff || knownTtcf, err.c_str(), __FILE__, __LINE__);
            }
        }
    }
    std::printf("ttf sweep: %d files, %d loaded, %d rejected\n", seen, loaded, rejected);
    KS_CHECK(seen >= 20);
    KS_CHECK(loaded >= 15);
    KS_CHECK(rejected >= 1);
}

}  // namespace

int main() {
    const fs::path root("F:/SteamLibrary/steamapps/common/assettocorsa");
    testSynthetic();
    testRasterizeSynthetic();
    testAtlasSynthetic();
    testFailures();

    if (fs::exists(root)) {
        const RealExpect expects[] = {
            {"apps/lua/Paintshop/fonts/1979.ttf", 1000, 165, 800, -200, 0, 165, 35, 1145, 19, 67, 3, 2},
            {"content/cars/rss_formula_1990_v10/extension/font/DSEG7Classic-Bold.ttf", 1000, 72,
             1000, 0, 90, 19, 18, 816, 7, 45, 3, 6},
            {"content/cars/rss_formula_hybrid_v12-r/extension/display/assets/Roboto-Black.ttf",
             2048, 1294, 1900, -500, 0, 1294, 37, 1395, 20, 69, 4, 2},
        };
        for (const RealExpect& e : expects) testRealFile(e, root);
        testRealSweep(root);
    } else {
        std::printf("SKIP real AC font content (missing %s)\n", root.string().c_str());
    }
    return KS_TEST_RESULT("ttf_test");
}
