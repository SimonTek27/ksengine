// Roadmap ksengine-vs-cryengine P2 - heightmap PNG import.
//
// TrackTerrainEditor::saveTerrain writes an 8-bit grayscale PNG, so this is
// the file format that lets a terrain edited in kseditor reach the Qt-free
// simulator. The test assembles PNGs byte by byte (stored DEFLATE blocks +
// per-chunk CRCs) and checks both the happy paths - every filter type, 8 and
// 16 bit - and the rejection paths.
#include "KsTest.h"
#include "engine/FileFormat/PngReader.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace ks;
using namespace ks::engine::fileformat;

namespace {

using Bytes = std::vector<std::uint8_t>;

void append32be(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

std::uint32_t adler32(const Bytes& data) {
    std::uint32_t a = 1, b = 0;
    for (const std::uint8_t c : data) {
        a += c;
        if (a >= 65521u) a -= 65521u;
        b += a;
        if (b >= 65521u) b -= 65521u;
    }
    return (b << 16) | a;
}

std::uint32_t chunkCrc(const Bytes& typeAndBody) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::uint8_t byte : typeAndBody) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return ~crc;
}

Bytes zlibStored(const Bytes& payload) {
    Bytes out;
    out.push_back(0x78);
    out.push_back(0x01);
    std::size_t off = 0;
    if (payload.empty()) {
        out.push_back(1);          // BFINAL + stored
        out.push_back(0x00);       // LEN = 0
        out.push_back(0x00);
        out.push_back(0xFF);       // NLEN = ~LEN
        out.push_back(0xFF);
        append32be(out, adler32(payload));
        return out;
    }
    while (off < payload.size()) {
        const std::size_t chunk = payload.size() - off > 65535 ? 65535 : payload.size() - off;
        const bool last = off + chunk >= payload.size();
        const auto len = static_cast<std::uint16_t>(chunk);
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(len & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(len >> 8));
        const auto nlen = static_cast<std::uint16_t>(~len);
        out.push_back(static_cast<std::uint8_t>(nlen & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(nlen >> 8));
        out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(off),
                   payload.begin() + static_cast<std::ptrdiff_t>(off + chunk));
        off += chunk;
    }
    append32be(out, adler32(payload));
    return out;
}

Bytes makeChunk(const char* type, const Bytes& body) {
    Bytes out;
    append32be(out, static_cast<std::uint32_t>(body.size()));
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(type[i]));
    out.insert(out.end(), body.begin(), body.end());
    Bytes typeAndBody(out.begin() + 4, out.end());
    append32be(out, chunkCrc(typeAndBody));
    return out;
}

Bytes makeIhdr(std::uint32_t w, std::uint32_t h, int bitDepth, int colorType, int interlace) {
    Bytes body;
    append32be(body, w);
    append32be(body, h);
    body.push_back(static_cast<std::uint8_t>(bitDepth));
    body.push_back(static_cast<std::uint8_t>(colorType));
    body.push_back(0); // compression: deflate
    body.push_back(0); // filter method: adaptive
    body.push_back(static_cast<std::uint8_t>(interlace));
    return makeChunk("IHDR", body);
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// Turns original row bytes into the filtered bytes PNG stores (the exact
// inverse of what PngReader::loadPngGray reconstructs).
Bytes filterRow(int filter, const Bytes& cur, const Bytes& prev, int bpp) {
    Bytes out(cur.size(), 0);
    for (std::size_t i = 0; i < cur.size(); ++i) {
        const int left = i >= static_cast<std::size_t>(bpp) ? cur[i - bpp] : 0;
        const int up = prev.empty() ? 0 : prev[i];
        const int upLeft = (prev.empty() || i < static_cast<std::size_t>(bpp)) ? 0 : prev[i - bpp];
        int pred = 0;
        switch (filter) {
            case 0: pred = 0; break;
            case 1: pred = left; break;
            case 2: pred = up; break;
            case 3: pred = (left + up) / 2; break;
            case 4: pred = paeth(left, up, upLeft); break;
            default: break;
        }
        out[i] = static_cast<std::uint8_t>(cur[i] - pred);
    }
    return out;
}

// Assembles a complete PNG from row-major original pixel bytes.
Bytes makePng(std::uint32_t w, std::uint32_t h, int bitDepth, int colorType, int interlace,
              const Bytes& original, const std::vector<int>& rowFilters) {
    const int bpp = bitDepth / 8;
    const std::size_t stride = static_cast<std::size_t>(w) * bpp;
    Bytes scanlines;
    Bytes prev(stride, 0);
    for (std::uint32_t y = 0; y < h; ++y) {
        const int filter = rowFilters.empty() ? 0 : rowFilters[y];
        Bytes cur(original.begin() + static_cast<std::ptrdiff_t>(y * stride),
                  original.begin() + static_cast<std::ptrdiff_t>((y + 1) * stride));
        scanlines.push_back(static_cast<std::uint8_t>(filter));
        const Bytes filtered = filterRow(filter, cur, prev, bpp);
        scanlines.insert(scanlines.end(), filtered.begin(), filtered.end());
        prev = cur;
    }

    Bytes png;
    const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), sig, sig + 8);
    const Bytes ihdr = makeIhdr(w, h, bitDepth, colorType, interlace);
    png.insert(png.end(), ihdr.begin(), ihdr.end());
    const Bytes idat = makeChunk("IDAT", zlibStored(scanlines));
    png.insert(png.end(), idat.begin(), idat.end());
    const Bytes iend = makeChunk("IEND", {});
    png.insert(png.end(), iend.begin(), iend.end());
    return png;
}

Bytes gray8(std::uint32_t w, std::uint32_t h, const std::vector<int>& values) {
    Bytes out;
    out.reserve(values.size());
    for (const int v : values) out.push_back(static_cast<std::uint8_t>(v));
    (void)w; (void)h;
    return out;
}

void expectFails(const Bytes& png, const char* what) {
    GrayImage img;
    std::string err;
    const std::string_view view(reinterpret_cast<const char*>(png.data()), png.size());
    const bool ok = loadPngGray(view, img, &err);
    KS_CHECK(!ok);
    if (ok) std::printf("  (expected failure: %s)\n", what);
}

std::string_view asView(const Bytes& png) {
    return std::string_view(reinterpret_cast<const char*>(png.data()), png.size());
}

void testGray8() {
    // 4x3, one row per filter type is exercised separately; here plain rows.
    const std::vector<int> values = {0, 1, 2, 127, 128, 200, 254, 255, 10, 20, 30, 40};
    const Bytes png = makePng(4, 3, 8, 0, 0, gray8(4, 3, values), {});
    GrayImage img;
    std::string err;
    KS_CHECK(loadPngGray(asView(png), img, &err));
    if (img.pixels.size() != values.size()) {
        KS_CHECK(img.pixels.size() == values.size());
        return;
    }
    KS_CHECK(img.width == 4);
    KS_CHECK(img.height == 3);
    for (std::size_t i = 0; i < values.size(); ++i)
        KS_CHECK(img.pixels[i] == static_cast<std::uint16_t>(values[i]) * 257u);
}

void testAllFilters() {
    const std::uint32_t w = 5, h = 5;
    Bytes original;
    for (int i = 0; i < int(w * h); ++i) original.push_back(static_cast<std::uint8_t>((i * 37 + 11) & 0xFF));
    const std::vector<int> filters = {0, 1, 2, 3, 4};
    const Bytes png = makePng(w, h, 8, 0, 0, original, filters);
    GrayImage img;
    std::string err;
    KS_CHECK(loadPngGray(asView(png), img, &err));
    KS_CHECK(img.pixels.size() == original.size());
    for (std::size_t i = 0; i < original.size() && i < img.pixels.size(); ++i)
        KS_CHECK(img.pixels[i] == static_cast<std::uint16_t>(original[i]) * 257u);
}

void testGray16() {
    const std::uint32_t w = 3, h = 4;
    const std::vector<std::uint16_t> values = {0, 1, 255, 256, 1000, 4095, 32768, 65535,
                                               12345, 54321, 7, 60000};
    Bytes original;
    for (const std::uint16_t v : values) {
        original.push_back(static_cast<std::uint8_t>(v >> 8));
        original.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    }
    const Bytes png = makePng(w, h, 16, 0, 0, original, {0, 4, 3, 1});
    GrayImage img;
    std::string err;
    KS_CHECK(loadPngGray(asView(png), img, &err));
    KS_CHECK(img.width == 3);
    KS_CHECK(img.height == 4);
    KS_CHECK(img.pixels.size() == values.size());
    for (std::size_t i = 0; i < values.size() && i < img.pixels.size(); ++i)
        KS_CHECK(img.pixels[i] == values[i]);
}

void testOnePixel() {
    const Bytes png = makePng(1, 1, 8, 0, 0, {42}, {});
    GrayImage img;
    std::string err;
    KS_CHECK(loadPngGray(asView(png), img, &err));
    KS_CHECK(img.pixels.size() == 1u);
    if (img.pixels.size() == 1u) KS_CHECK(img.pixels[0] == 42u * 257u);
}

void testChunks() {
    // Ancillary chunks (lowercase first letter) and an optional PLTE must be
    // skipped; an unknown *critical* chunk must be refused.
    const std::vector<int> values = {1, 2, 3, 4};
    Bytes plain = makePng(2, 2, 8, 0, 0, gray8(2, 2, values), {});

    Bytes sig(plain.begin(), plain.begin() + 8);
    const Bytes ihdr = makeIhdr(2, 2, 8, 0, 0);
    const Bytes text = makeChunk("tEXt", {'k', 0, 'v'});
    const Bytes plte = makeChunk("PLTE", {1, 2, 3, 4, 5, 6});
    Bytes scanlines;
    for (int y = 0; y < 2; ++y) {
        scanlines.push_back(0);
        for (int x = 0; x < 2; ++x)
            scanlines.push_back(static_cast<std::uint8_t>(values[y * 2 + x]));
    }
    const Bytes idat = makeChunk("IDAT", zlibStored(scanlines));
    const Bytes iend = makeChunk("IEND", {});

    Bytes withExtras = sig;
    for (const Bytes* c : {&ihdr, &text, &plte, &idat, &iend})
        withExtras.insert(withExtras.end(), c->begin(), c->end());
    GrayImage img;
    std::string err;
    KS_CHECK(loadPngGray(asView(withExtras), img, &err));
    KS_CHECK(img.pixels.size() == 4u);

    Bytes withCritical = sig;
    const Bytes unknown = makeChunk("CRIT", {1});
    for (const Bytes* c : {&ihdr, &unknown, &idat, &iend})
        withCritical.insert(withCritical.end(), c->begin(), c->end());
    expectFails(withCritical, "unknown critical chunk");
}

void testRejections() {
    GrayImage img;
    std::string err;

    KS_CHECK(!loadPngGray("not a png at all", img, &err));
    KS_CHECK(!err.empty());
    err.clear();

    KS_CHECK(!loadPngGray("", img, &err));
    err.clear();

    // Good file, then tamper with the IHDR body so its CRC no longer matches.
    const std::vector<int> values = {5, 6, 7, 8};
    Bytes png = makePng(2, 2, 8, 0, 0, gray8(2, 2, values), {});
    png[8 + 16] ^= 0xFF; // a byte inside the IHDR data
    expectFails(png, "corrupt IHDR CRC");

    // Colour type 2 (truecolour) - refused rather than mis-scaled.
    expectFails(makePng(2, 2, 8, 2, 0, Bytes(12, 0), {}), "colour type 2");

    // Bit depth 4 - refused.
    expectFails(makePng(2, 2, 4, 0, 0, Bytes(4, 0), {}), "bit depth 4");

    // Adam7 interlacing - refused.
    expectFails(makePng(2, 2, 8, 0, 1, gray8(2, 2, values), {}), "interlaced");

    // IDAT that decompresses to the wrong number of bytes.
    {
        Bytes png2;
        const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        png2.insert(png2.end(), sig, sig + 8);
        const Bytes ihdr = makeIhdr(2, 2, 8, 0, 0);
        png2.insert(png2.end(), ihdr.begin(), ihdr.end());
        const Bytes idat = makeChunk("IDAT", zlibStored({9, 9, 9}));
        png2.insert(png2.end(), idat.begin(), idat.end());
        const Bytes iend = makeChunk("IEND", {});
        png2.insert(png2.end(), iend.begin(), iend.end());
        expectFails(png2, "short IDAT payload");
    }

    // Truncated after IHDR (no IDAT/IEND).
    {
        Bytes png3;
        const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        png3.insert(png3.end(), sig, sig + 8);
        const Bytes ihdr = makeIhdr(2, 2, 8, 0, 0);
        png3.insert(png3.end(), ihdr.begin(), ihdr.end());
        expectFails(png3, "missing IDAT/IEND");
    }

    // Corrupt deflate stream inside a well-formed chunk.
    {
        Bytes png4;
        const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        png4.insert(png4.end(), sig, sig + 8);
        const Bytes ihdr = makeIhdr(2, 2, 8, 0, 0);
        png4.insert(png4.end(), ihdr.begin(), ihdr.end());
        Bytes garbage = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        const Bytes idat = makeChunk("IDAT", garbage);
        png4.insert(png4.end(), idat.begin(), idat.end());
        const Bytes iend = makeChunk("IEND", {});
        png4.insert(png4.end(), iend.begin(), iend.end());
        expectFails(png4, "garbage IDAT");
    }
}

void testFileRoundtrip() {
    const std::vector<int> values = {0, 64, 128, 192, 255, 17, 200, 99, 42};
    const Bytes png = makePng(3, 3, 8, 0, 0, gray8(3, 3, values), {0, 1, 4});
    const char* path = "png_test_roundtrip.png";
    {
        std::FILE* f = std::fopen(path, "wb");
        KS_CHECK(f != nullptr);
        if (f) {
            std::fwrite(png.data(), 1, png.size(), f);
            std::fclose(f);
        }
    }
    GrayImage img;
    std::string err;
    KS_CHECK(loadPngGrayFile(path, img, &err));
    KS_CHECK(img.width == 3);
    KS_CHECK(img.height == 3);
    KS_CHECK(img.pixels.size() == values.size());
    for (std::size_t i = 0; i < values.size() && i < img.pixels.size(); ++i)
        KS_CHECK(img.pixels[i] == static_cast<std::uint16_t>(values[i]) * 257u);
    std::remove(path);

    err.clear();
    KS_CHECK(!loadPngGrayFile("png_test_does_not_exist.png", img, &err));
    KS_CHECK(!err.empty());
}

} // namespace

int main() {
    testGray8();
    testAllFilters();
    testGray16();
    testOnePixel();
    testChunks();
    testRejections();
    testFileRoundtrip();
    return KS_TEST_RESULT("png_test");
}
