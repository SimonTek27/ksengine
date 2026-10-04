// DDS reader test: synthesises real DDS images (header + payload) covering
// uncompressed mask-driven pixels, BC1/BC2/BC3 blocks, the DX10 extension,
// luminance surfaces, and the rejection paths, then decodes them and checks
// exact pixels.

#include "engine/FileFormat/DdsReader.h"
#include "KsTest.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using ks::engine::fileformat::decodeDds;
using ks::engine::fileformat::DdsFormat;
using ks::engine::fileformat::DdsImage;
using ks::engine::fileformat::DdsInfo;
using ks::engine::fileformat::isDds;
using ks::engine::fileformat::readDdsInfo;

// DDS_HEADER flags
constexpr std::uint32_t kCaps = 0x00000001u;
constexpr std::uint32_t kHeight = 0x00000002u;
constexpr std::uint32_t kWidth = 0x00000004u;
constexpr std::uint32_t kPitch = 0x00000008u;
constexpr std::uint32_t kPixelFormat = 0x00001000u;
constexpr std::uint32_t kMipmapCount = 0x00020000u;
constexpr std::uint32_t kLinearSize = 0x00080000u;

// DDS_PIXELFORMAT flags
constexpr std::uint32_t kAlphaPixels = 0x00000001u;
constexpr std::uint32_t kAlpha = 0x00000002u;
constexpr std::uint32_t kFourCcFlag = 0x00000004u;
constexpr std::uint32_t kRgb = 0x00000040u;
constexpr std::uint32_t kLuminance = 0x00020000u;

std::uint32_t fourCc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

struct DdsSpec {
    std::uint32_t header_flags = kCaps | kHeight | kWidth | kPixelFormat;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pitch_or_linear = 0;
    std::uint32_t mip_count = 0;
    std::uint32_t pf_flags = 0;
    std::uint32_t fourcc = 0;
    std::uint32_t bit_count = 0;
    std::uint32_t red = 0;
    std::uint32_t green = 0;
    std::uint32_t blue = 0;
    std::uint32_t alpha = 0;
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> dx10; // raw 20-byte DX10 header, when present
    int drop_payload_bytes = 0;     // truncate the buffer for error tests
};

std::vector<std::uint8_t> buildDds(const DdsSpec& spec) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'D', 'D', 'S', ' '});
    putU32(out, 124);                              // dwSize
    putU32(out, spec.header_flags);                // dwFlags
    putU32(out, spec.height);                      // dwHeight
    putU32(out, spec.width);                       // dwWidth
    putU32(out, spec.pitch_or_linear);             // dwPitchOrLinearSize
    putU32(out, 0);                                // dwDepth
    putU32(out, spec.mip_count);                   // dwMipMapCount
    for (int i = 0; i < 11; ++i) putU32(out, 0);   // dwReserved1
    putU32(out, 32);                               // pixel format dwSize
    putU32(out, spec.pf_flags);
    putU32(out, spec.fourcc);
    putU32(out, spec.bit_count);
    putU32(out, spec.red);
    putU32(out, spec.green);
    putU32(out, spec.blue);
    putU32(out, spec.alpha);
    putU32(out, 0); // caps
    putU32(out, 0);
    putU32(out, 0);
    putU32(out, 0);
    putU32(out, 0);
    out.insert(out.end(), spec.dx10.begin(), spec.dx10.end());
    out.insert(out.end(), spec.pixels.begin(), spec.pixels.end());
    if (spec.drop_payload_bytes > 0) {
        const auto drop = static_cast<std::size_t>(spec.drop_payload_bytes);
        out.resize(out.size() > drop ? out.size() - drop : 0);
    }
    return out;
}

std::string asString(const std::vector<std::uint8_t>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

const std::uint8_t* pixel(const DdsImage& image, int x, int y) {
    if (image.rgba.empty()) return nullptr;
    const auto offset =
        (static_cast<std::size_t>(y) * image.info.width + x) * 4;
    if (offset + 4 > image.rgba.size()) return nullptr;
    return image.rgba.data() + offset;
}

// ---------------------------------------------------------------------------
// Uncompressed 32-bit A8R8G8B8 (the layout a KS .dds export produces)
// ---------------------------------------------------------------------------

void testRaw32() { 
    DdsSpec spec;
    spec.width = 4;
    spec.height = 2;
    spec.header_flags |= kPitch;
    spec.pitch_or_linear = 4 * 4; // 4 pixels * 4 bytes
    spec.pf_flags = kRgb | kAlphaPixels;
    spec.bit_count = 32;
    spec.red = 0x00FF0000u;
    spec.green = 0x0000FF00u;
    spec.blue = 0x000000FFu;
    spec.alpha = 0xFF000000u;

    // dword = 0xAARRGGBB stored little-endian => bytes B, G, R, A
    const auto pixel_dword = [&](std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                 std::uint8_t a) {
        const std::uint32_t v = (static_cast<std::uint32_t>(a) << 24) |
                                (static_cast<std::uint32_t>(r) << 16) |
                                (static_cast<std::uint32_t>(g) << 8) | b;
        for (int i = 0; i < 4; ++i) {
            spec.pixels.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };
    // row 0
    pixel_dword(10, 20, 30, 40);
    pixel_dword(255, 0, 0, 255);
    pixel_dword(0, 255, 0, 128);
    pixel_dword(0, 0, 255, 0);
    // row 1
    pixel_dword(1, 2, 3, 4);
    pixel_dword(9, 8, 7, 6);
    pixel_dword(100, 110, 120, 130);
    pixel_dword(200, 210, 220, 230);

    const auto bytes = buildDds(spec);
    KS_CHECK(isDds(asString(bytes)));

    const DdsInfo info = readDdsInfo(asString(bytes));
    KS_CHECK(info.ok());
    KS_CHECK(info.width == 4);
    KS_CHECK(info.height == 2);
    KS_CHECK(info.mip_count == 1);
    KS_CHECK(info.format == DdsFormat::raw);
    KS_CHECK(!info.compressed());

    const DdsImage image = decodeDds(asString(bytes));
    KS_CHECK(image.ok());
    KS_CHECK(image.rgba.size() == 4 * 2 * 4);
    if (image.rgba.size() == 32) {
        const std::uint8_t* p0 = pixel(image, 0, 0);
        KS_CHECK(p0 != nullptr);
        if (p0) {
            KS_CHECK(p0[0] == 10);
            KS_CHECK(p0[1] == 20);
            KS_CHECK(p0[2] == 30);
            KS_CHECK(p0[3] == 40);
        }
        const std::uint8_t* p1 = pixel(image, 1, 0);
        if (p1) {
            KS_CHECK(p1[0] == 255);
            KS_CHECK(p1[3] == 255);
        }
        const std::uint8_t* p2 = pixel(image, 2, 0);
        if (p2) {
            KS_CHECK(p2[1] == 255);
            KS_CHECK(p2[3] == 128);
        }
        const std::uint8_t* p3 = pixel(image, 3, 0);
        if (p3) KS_CHECK(p3[3] == 0);
        const std::uint8_t* p7 = pixel(image, 3, 1);
        if (p7) {
            KS_CHECK(p7[0] == 200);
            KS_CHECK(p7[3] == 230);
        }
    }
}

// ---------------------------------------------------------------------------
// BC1 / DXT1
// ---------------------------------------------------------------------------

void testBc1Opaque() { 
    DdsSpec spec;
    spec.width = 4;
    spec.height = 4;
    spec.header_flags |= kLinearSize;
    spec.pitch_or_linear = 8;
    spec.pf_flags = kFourCcFlag;
    spec.fourcc = fourCc('D', 'X', 'T', '1');

    // c0 red (0xF800) > c1 green (0x07E0) => 4-colour mode.
    // texel indices: texel 0 -> 0 (red), texel 1 -> 1 (green).
    spec.pixels = {
        0x00, 0xF8,                   // c0 = 0xF800 (red)
        0xE0, 0x07,                   // c1 = 0x07E0 (green)
        0x04, 0x00, 0x00, 0x00,       // indices: texel1 = 1, others 0
    };

    const auto bytes = buildDds(spec);
    const DdsInfo info = readDdsInfo(asString(bytes));
    KS_CHECK(info.ok());
    KS_CHECK(info.format == DdsFormat::bc1);
    KS_CHECK(info.compressed());

    const DdsImage image = decodeDds(asString(bytes));
    KS_CHECK(image.ok());
    if (image.rgba.size() == 64) {
        const std::uint8_t* p0 = pixel(image, 0, 0);
        if (p0) {
            KS_CHECK(p0[0] == 255);
            KS_CHECK(p0[1] == 0);
            KS_CHECK(p0[2] == 0);
            KS_CHECK(p0[3] == 255);
        }
        const std::uint8_t* p1 = pixel(image, 1, 0);
        if (p1) {
            KS_CHECK(p1[0] == 0);
            KS_CHECK(p1[1] == 255);
            KS_CHECK(p1[3] == 255);
        }
        // texel 15 (bottom-right of the block) still uses index 0 -> red
        const std::uint8_t* p15 = pixel(image, 3, 3);
        if (p15) KS_CHECK(p15[0] == 255 && p15[1] == 0);
    }
}

void testBc1Transparent() { 
    DdsSpec spec;
    spec.width = 4;
    spec.height = 4;
    spec.header_flags |= kLinearSize;
    spec.pitch_or_linear = 8;
    spec.pf_flags = kFourCcFlag;
    spec.fourcc = fourCc('D', 'X', 'T', '1');

    // c0 (0x07E0) <= c1 (0xF800) => 3-colour + transparent mode.
    // index 3 is the fully transparent entry.
    spec.pixels = {
        0xE0, 0x07, // c0 = green
        0x00, 0xF8, // c1 = red
        0x03, 0x00, 0x00, 0x00, // texel 0 -> index 3 (transparent)
    };

    const auto bytes = buildDds(spec);
    const DdsImage image = decodeDds(asString(bytes));
    KS_CHECK(image.ok());
    if (image.rgba.size() == 64) {
        const std::uint8_t* p0 = pixel(image, 0, 0);
        if (p0) {
            KS_CHECK(p0[0] == 0);
            KS_CHECK(p0[1] == 0);
            KS_CHECK(p0[2] == 0);
            KS_CHECK(p0[3] == 0);
        }
        // texel 1 keeps index 0 => green, fully opaque
        const std::uint8_t* p1 = pixel(image, 1, 0);
        if (p1) {
            KS_CHECK(p1[1] == 255);
            KS_CHECK(p1[3] == 255);
        }
    }
}

// ---------------------------------------------------------------------------
// BC3 / DXT5
// ---------------------------------------------------------------------------

void testBc3() { 
    DdsSpec spec;
    spec.width = 4;
    spec.height = 4;
    spec.header_flags |= kLinearSize;
    spec.pitch_or_linear = 16;
    spec.pf_flags = kFourCcFlag;
    spec.fourcc = fourCc('D', 'X', 'T', '5');

    spec.pixels = {
        0x80,                   // alpha0 = 128
        0x00,                   // alpha1 = 0
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, // texel1 -> index 1, rest 0
        0x00, 0xF8,             // c0 = red
        0xE0, 0x07,             // c1 = green
        0x00, 0x00, 0x00, 0x00, // colour indices all 0 -> red
    };

    const auto bytes = buildDds(spec);
    const DdsInfo info = readDdsInfo(asString(bytes));
    KS_CHECK(info.ok());
    KS_CHECK(info.format == DdsFormat::bc3);
    KS_CHECK(info.compressed());

    const DdsImage image = decodeDds(asString(bytes));
    KS_CHECK(image.ok());
    if (image.rgba.size() == 64) {
        const std::uint8_t* p0 = pixel(image, 0, 0);
        if (p0) {
            KS_CHECK(p0[0] == 255);
            KS_CHECK(p0[3] == 128);
        }
        const std::uint8_t* p1 = pixel(image, 1, 0);
        if (p1) {
            KS_CHECK(p1[3] == 0); // alpha index 1 -> alpha1
            KS_CHECK(p1[0] == 255); // colour half unaffected
        }
    }
}

// ---------------------------------------------------------------------------
// BC2 / DXT3
// ---------------------------------------------------------------------------

void testBc2() { 
    DdsSpec spec;
    spec.width = 4;
    spec.height = 4;
    spec.header_flags |= kLinearSize;
    spec.pitch_or_linear = 16;
    spec.pf_flags = kFourCcFlag;
    spec.fourcc = fourCc('D', 'X', 'T', '3');

    spec.pixels = {
        0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // texel0 alpha=0xF, texel1=0x0
        0x00, 0xF8,             // c0 = red
        0xE0, 0x07,             // c1 = green
        0x00, 0x00, 0x00, 0x00, // colour indices all 0 -> red
    };

    const auto bytes = buildDds(spec);
    const DdsImage image = decodeDds(asString(bytes));
    KS_CHECK(image.ok());
    if (image.rgba.size() == 64) {
        const std::uint8_t* p0 = pixel(image, 0, 0);
        if (p0) {
            KS_CHECK(p0[0] == 255);
            KS_CHECK(p0[3] == 255); // 0xF * 17
        }
        const std::uint8_t* p1 = pixel(image, 1, 0);
        if (p1) KS_CHECK(p1[3] == 0);
    }
}

// ---------------------------------------------------------------------------
// DX10 extended header
// ---------------------------------------------------------------------------

void testDx10() { 
    DdsSpec spec;
    spec.width = 4;
    spec.height = 4;
    spec.header_flags |= kLinearSize;
    spec.pitch_or_linear = 8;
    spec.pf_flags = kFourCcFlag;
    spec.fourcc = fourCc('D', 'X', '1', '0');
    spec.dx10 = {71, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0,
                 1, 0, 0, 0, 0, 0, 0, 0}; // DXGI_FORMAT_BC1_UNORM
    spec.pixels = {0x00, 0xF8, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00};

    const auto bytes = buildDds(spec);
    const DdsInfo info = readDdsInfo(asString(bytes));
    KS_CHECK(info.ok());
    KS_CHECK(info.dx10());
    KS_CHECK(info.format == DdsFormat::bc1);

    const DdsImage image = decodeDds(asString(bytes));
    KS_CHECK(image.ok());
    if (image.rgba.size() == 64) {
        const std::uint8_t* p0 = pixel(image, 0, 0);
        if (p0) KS_CHECK(p0[0] == 255 && p0[1] == 0);
    }

    // An unsupported DXGI id must be reported, never silently decoded.
    DdsSpec bad = spec;
    bad.dx10 = {98, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0,
                1, 0, 0, 0, 0, 0, 0, 0}; // DXGI_FORMAT_BC7_UNORM
    const auto bad_bytes = buildDds(bad);
    const DdsInfo bad_info = readDdsInfo(asString(bad_bytes));
    KS_CHECK(!bad_info.ok());
    KS_CHECK(bad_info.error.find("98") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Luminance, mip chains, rejection paths
// ---------------------------------------------------------------------------

void testLuminance() { 
    DdsSpec spec;
    spec.width = 4;
    spec.height = 2;
    spec.header_flags |= kPitch;
    spec.pitch_or_linear = 4;
    spec.pf_flags = kLuminance;
    spec.bit_count = 8;
    spec.red = 0xFFu;
    spec.pixels = {10, 20, 30, 40, 50, 60, 70, 80};

    const auto bytes = buildDds(spec);
    const DdsInfo info = readDdsInfo(asString(bytes));
    KS_CHECK(info.ok());
    KS_CHECK(info.format == DdsFormat::luminance);

    const DdsImage image = decodeDds(asString(bytes));
    KS_CHECK(image.ok());
    if (image.rgba.size() == 32) {
        const std::uint8_t* p0 = pixel(image, 0, 0);
        if (p0) {
            KS_CHECK(p0[0] == 10);
            KS_CHECK(p0[1] == 10);
            KS_CHECK(p0[2] == 10);
            KS_CHECK(p0[3] == 255); // no alpha mask => opaque
        }
        const std::uint8_t* p7 = pixel(image, 3, 1);
        if (p7) KS_CHECK(p7[0] == 80 && p7[3] == 255);
    }
}

void testMipChainReadsMip0() { 
    // A mip count without any of the lower-level bytes: the reader must only
    // require mip 0 (the top level) and still decode it.
    DdsSpec spec;
    spec.width = 4;
    spec.height = 4;
    spec.header_flags |= kLinearSize | kMipmapCount;
    spec.pitch_or_linear = 8;
    spec.mip_count = 5;
    spec.pf_flags = kFourCcFlag;
    spec.fourcc = fourCc('D', 'X', 'T', '1');
    spec.pixels = {0x00, 0xF8, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00};

    const auto bytes = buildDds(spec);
    const DdsInfo info = readDdsInfo(asString(bytes));
    KS_CHECK(info.ok());
    KS_CHECK(info.mip_count == 5);
    KS_CHECK(decodeDds(asString(bytes)).ok());
}

void testRejection() { 
    // Wrong magic.
    const std::string not_dds = "this is not a dds file at all, honestly";
    KS_CHECK(!isDds(not_dds));
    KS_CHECK(!readDdsInfo(not_dds).ok());

    // Header claims a payload that isn't there.
    DdsSpec spec;
    spec.width = 4;
    spec.height = 4;
    spec.header_flags |= kLinearSize;
    spec.pitch_or_linear = 8;
    spec.pf_flags = kFourCcFlag;
    spec.fourcc = fourCc('D', 'X', 'T', '1');
    spec.pixels = {0, 0, 0, 0, 0, 0, 0, 0};
    spec.drop_payload_bytes = 8;
    const DdsImage truncated = decodeDds(asString(buildDds(spec)));
    KS_CHECK(!truncated.ok());
    KS_CHECK(truncated.info.error.find("expected at least") != std::string::npos);
    KS_CHECK(truncated.rgba.empty());

    // Zero-sized surface.
    DdsSpec zero = spec;
    zero.drop_payload_bytes = 0;
    zero.width = 0;
    KS_CHECK(!readDdsInfo(asString(buildDds(zero))).ok());

    // Unrecognised FourCC.
    DdsSpec odd = spec;
    odd.drop_payload_bytes = 0;
    odd.fourcc = fourCc('X', 'X', 'X', 'X');
    const DdsInfo odd_info = readDdsInfo(asString(buildDds(odd)));
    KS_CHECK(!odd_info.ok());
    KS_CHECK(odd_info.error.find("XXXX") != std::string::npos);

    // Short buffer: not even a header.
    KS_CHECK(!readDdsInfo("DDS ").ok());

    // Header size that isn't 124.
    std::vector<std::uint8_t> bad_size = {'D', 'D', 'S', ' ', 0, 0, 0, 0};
    for (int i = 0; i < 130; ++i) bad_size.push_back(0);
    KS_CHECK(!readDdsInfo(asString(bad_size)).ok());
}

} // namespace

int main() {
    testRaw32();
    testBc1Opaque();
    testBc1Transparent();
    testBc3();
    testBc2();
    testDx10();
    testLuminance();
    testMipChainReadsMip0();
    testRejection();
    return KS_TEST_RESULT("dds_test");
}
