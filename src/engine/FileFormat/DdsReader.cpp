#include <cmath>
#include "DdsReader.h"

#include <algorithm>
#include <cstring>

namespace ks::engine::fileformat {

namespace {

// DDS_PIXELFORMAT flags
constexpr std::uint32_t kDdpfAlpha = 0x00000002u;
constexpr std::uint32_t kDdpfFourCc = 0x00000004u;
constexpr std::uint32_t kDdpfRgb = 0x00000040u;
constexpr std::uint32_t kDdpfLuminance = 0x00020000u;

// DDS_HEADER flags
constexpr std::uint32_t kDdsdPitch = 0x00000008u;
constexpr std::uint32_t kDdsdMipmapCount = 0x00020000u;

constexpr std::uint32_t kFourCc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
}

constexpr std::uint32_t kFourCcDxt1 = kFourCc('D', 'X', 'T', '1');
constexpr std::uint32_t kFourCcDxt2 = kFourCc('D', 'X', 'T', '2');
constexpr std::uint32_t kFourCcDxt3 = kFourCc('D', 'X', 'T', '3');
constexpr std::uint32_t kFourCcDxt4 = kFourCc('D', 'X', 'T', '4');
constexpr std::uint32_t kFourCcDxt5 = kFourCc('D', 'X', 'T', '5');
constexpr std::uint32_t kFourCcDx10 = kFourCc('D', 'X', '1', '0');

bool readU32(std::string_view bytes, std::size_t offset, std::uint32_t& out) {
    if (offset + 4 > bytes.size()) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data() + offset);
    out = static_cast<std::uint32_t>(p[0]) |
          (static_cast<std::uint32_t>(p[1]) << 8) |
          (static_cast<std::uint32_t>(p[2]) << 16) |
          (static_cast<std::uint32_t>(p[3]) << 24);
    return true;
}

std::uint8_t* pixelAt(std::vector<std::uint8_t>& rgba, int width, int x, int y) {
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
    return rgba.data() + row + static_cast<std::size_t>(x) * 4;
}

// Expands a 565 colour to 8 bits per channel (bit-replicate rounding).
void expand565(std::uint16_t c, std::uint8_t& r, std::uint8_t& g,
               std::uint8_t& b) {
    const auto r5 = static_cast<std::uint8_t>((c >> 11) & 0x1F);
    const auto g6 = static_cast<std::uint8_t>((c >> 5) & 0x3F);
    const auto b5 = static_cast<std::uint8_t>(c & 0x1F);
    r = static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2));
    g = static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4));
    b = static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2));
}

std::uint16_t readU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8);
}

// 4x4 colour half of a BC1 block. When `force_opaque` is set (BC2/BC3, whose
// colour block never carries 1-bit alpha) the c0<=c1 3-colour+transparent
// mode is disabled, as required by the spec.
void decodeBc1Color(const std::uint8_t* block, std::uint8_t* out,
                    bool force_opaque) {
    const std::uint16_t c0 = readU16(block);
    const std::uint16_t c1 = readU16(block + 2);
    std::uint8_t r[4], g[4], b[4], a[4];

    expand565(c0, r[0], g[0], b[0]);
    expand565(c1, r[1], g[1], b[1]);
    a[0] = 255;
    a[1] = 255;

    if (force_opaque || c0 > c1) {
        r[2] = static_cast<std::uint8_t>((2 * r[0] + r[1]) / 3);
        g[2] = static_cast<std::uint8_t>((2 * g[0] + g[1]) / 3);
        b[2] = static_cast<std::uint8_t>((2 * b[0] + b[1]) / 3);
        a[2] = 255;
        r[3] = static_cast<std::uint8_t>((r[0] + 2 * r[1]) / 3);
        g[3] = static_cast<std::uint8_t>((g[0] + 2 * g[1]) / 3);
        b[3] = static_cast<std::uint8_t>((b[0] + 2 * b[1]) / 3);
        a[3] = 255;
    } else {
        r[2] = static_cast<std::uint8_t>((r[0] + r[1]) / 2);
        g[2] = static_cast<std::uint8_t>((g[0] + g[1]) / 2);
        b[2] = static_cast<std::uint8_t>((b[0] + b[1]) / 2);
        a[2] = 255;
        r[3] = g[3] = b[3] = 0;
        a[3] = 0;
    }

    const std::uint32_t indices =
        static_cast<std::uint32_t>(block[4]) |
        (static_cast<std::uint32_t>(block[5]) << 8) |
        (static_cast<std::uint32_t>(block[6]) << 16) |
        (static_cast<std::uint32_t>(block[7]) << 24);
    for (int i = 0; i < 16; ++i) {
        const unsigned index = (indices >> (i * 2)) & 0x3u;
        out[i * 4 + 0] = r[index];
        out[i * 4 + 1] = g[index];
        out[i * 4 + 2] = b[index];
        out[i * 4 + 3] = a[index];
    }
}

// 4x4 alpha half of a BC3 block (8 bytes: a0, a1, then 7 3-bit indices each).
// `out` is 16 bytes — one alpha per texel, not an RGBA stride.
void decodeBc3Alpha(const std::uint8_t* block, std::uint8_t* out) {
    std::uint8_t palette[8];
    palette[0] = block[0];
    palette[1] = block[1];
    if (block[0] > block[1]) {
        for (int i = 1; i <= 6; ++i) {
            palette[i + 1] = static_cast<std::uint8_t>(
                ((7 - i) * block[0] + i * block[1]) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            palette[i + 1] = static_cast<std::uint8_t>(
                ((5 - i) * block[0] + i * block[1]) / 5);
        }
        palette[6] = 0;
        palette[7] = 255;
    }

    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<std::uint64_t>(block[2 + i]) << (i * 8);
    }
    for (int i = 0; i < 16; ++i) {
        out[i] = palette[(bits >> (i * 3)) & 0x7u];
    }
}

// Extracts one channel through its bit mask, so any mask alignment works.
std::uint8_t channelFromMask(std::uint32_t pixel, std::uint32_t mask) {
    if (mask == 0) return 0;
    std::uint32_t shift = 0;
    while (shift < 32 && ((mask >> shift) & 1u) == 0) ++shift;
    if (shift >= 32) return 0;
    const std::uint32_t range = mask >> shift;
    std::uint32_t bits = 0;
    while (bits < 32 && (range >> bits) != 0) ++bits;
    const std::uint32_t value = (pixel & mask) >> shift;
    if (bits >= 8) return static_cast<std::uint8_t>(value >> (bits - 8));
    if (range == 0) return 0;
    return static_cast<std::uint8_t>((value * 255u) / range);
}

std::uint32_t readPixelAt(const std::uint8_t* base, std::size_t offset,
                          std::uint32_t bpp) {
    switch (bpp) {
    case 8:
        return base[offset];
    case 16:
        return static_cast<std::uint32_t>(base[offset]) |
               (static_cast<std::uint32_t>(base[offset + 1]) << 8);
    case 24:
        return static_cast<std::uint32_t>(base[offset]) |
               (static_cast<std::uint32_t>(base[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(base[offset + 2]) << 16);
    default:
        return static_cast<std::uint32_t>(base[offset]) |
               (static_cast<std::uint32_t>(base[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(base[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(base[offset + 3]) << 24);
    }
}

// ---------------------------------------------------------------------------
// BC7 (DXGI_FORMAT_BC7_UNORM / _SRGB, 98/99): 16 bytes per 4x4 block, eight
// modes that differ in endpoint precision, partitioning and index layout.
//
// The partition, anchor and weight tables are the BPTC specification ones; the
// per-mode field order below follows Richard Geldreich's bc7decomp (public
// domain / MIT), because the fields are not read in one common order across
// modes and hand-rolling that order is how decoders quietly swap colour planes.
constexpr std::uint8_t kBc7Partition2[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
    {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0},
    {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0},
    {0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0},
    {0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0},
    {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1},
    {0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1},
    {0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0},
    {0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0},
    {0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1},
    {0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1},
    {0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0},
    {0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1},
    {0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0},
    {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1},
};

constexpr std::uint8_t kBc7Partition3[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2},
    {0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1},
    {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2},
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2},
    {0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2},
    {0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2},
    {0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2},
    {0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2},
    {0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2},
    {0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0},
    {0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2},
    {0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2},
    {0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2},
    {0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0},
    {0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0},
    {0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2},
    {0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0},
    {0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1},
    {0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2},
    {0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1},
    {0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2},
    {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2},
    {0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0},
    {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0},
    {0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0},
    {0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1},
    {0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1},
    {0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2},
    {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1},
    {0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1},
    {0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2},
    {0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1},
    {0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2},
    {0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2},
    {0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2},
    {0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2},
    {0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1},
    {0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0},
};

// Texel whose index is stored one bit short (the "fix-up" index) per
// partition set: texel 0 always is, plus one per non-zero subset.
constexpr std::uint8_t kBc7AnchorSecond[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
    15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6,
    6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15,
};

constexpr std::uint8_t kBc7AnchorThird1[64] = {
    3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3,
    3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
    8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15,
    3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3,
};

constexpr std::uint8_t kBc7AnchorThird2[64] = {
    15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8,
    15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
    15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8,
    15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8,
};

constexpr std::uint8_t kBc7Weights2[4] = {0, 21, 43, 64};
constexpr std::uint8_t kBc7Weights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr std::uint8_t kBc7Weights4[16] = {0,  4,  9,  13, 17, 21, 26, 30,
                                           34, 38, 43, 47, 51, 55, 60, 64};

// Little-endian bit cursor: BC7 packs each field starting at the low bit of the
// block and continuing upwards, so a field can straddle a byte boundary.
struct Bc7Bits {
    const std::uint8_t* bytes;
    std::uint32_t offset = 0;

    std::uint32_t read(std::uint32_t count) {
        std::uint32_t value = 0;
        for (std::uint32_t done = 0; done < count;) {
            const std::uint32_t inByte = offset & 7u;
            const std::uint32_t take = std::min(count - done, 8u - inByte);
            value |= ((bytes[offset >> 3] >> inByte) & ((1u << take) - 1u)) << done;
            done += take;
            offset += take;
        }
        return value;
    }
};

// BPTC's "unquantize": fold in the parity bit when the mode has one, then
// stretch the value to 8 bits by replicating its most significant bits.
std::uint32_t bc7Expand(std::uint32_t value, std::uint32_t bits, int pbit) {
    if (pbit >= 0) {
        value = (value << 1) | static_cast<std::uint32_t>(pbit);
        ++bits;
    }
    const std::uint32_t shifted = value << (8 - bits);
    return (shifted | (shifted >> bits)) & 0xFFu;
}

std::uint8_t bc7Interpolate(std::uint32_t low, std::uint32_t high,
                            const std::uint8_t* weights, std::uint32_t index) {
    const std::uint32_t w = weights[index];
    return static_cast<std::uint8_t>((low * (64 - w) + high * w + 32) >> 6);
}

// Modes 0/2 (three subsets) and 1/3/7 (two subsets) share one layout: the
// partition, then every endpoint component-major, the parity bits, the indices.
bool bc7DecodePartitioned(std::uint32_t mode, const std::uint8_t* block,
                          std::uint8_t out[16][4]) {
    const bool three = (mode == 0 || mode == 2);
    const std::uint32_t subsets = three ? 3u : 2u;
    const std::uint32_t endpointCount = subsets * 2u;
    const std::uint32_t comps = (mode == 7) ? 4u : 3u;
    const std::uint32_t weightBits = (mode == 0 || mode == 1) ? 3u : 2u;
    const std::uint32_t endpointBits =
        (mode == 0) ? 4u : (mode == 1) ? 6u : (mode == 2) ? 5u : (mode == 3) ? 7u : 5u;
    const std::uint32_t pbitCount =
        (mode == 1) ? 2u : (mode == 0) ? 6u : (mode == 3 || mode == 7) ? 4u : 0u;

    Bc7Bits bits{block};
    if (bits.read(mode + 1) != (1u << mode)) return false;
    const std::uint32_t partition = bits.read(mode == 0 ? 4u : 6u);

    std::uint32_t endpoints[6][4] = {};
    for (std::uint32_t c = 0; c < comps; ++c)
        for (std::uint32_t e = 0; e < endpointCount; ++e)
            endpoints[e][c] = bits.read(endpointBits);

    std::uint32_t pbits[6] = {0, 0, 0, 0, 0, 0};
    for (std::uint32_t p = 0; p < pbitCount; ++p) pbits[p] = bits.read(1);

    std::uint32_t index[16] = {};
    for (std::uint32_t i = 0; i < 16; ++i) {
        bool anchor = i == 0;
        if (three) {
            anchor = anchor || i == kBc7AnchorThird1[partition] ||
                     i == kBc7AnchorThird2[partition];
        } else {
            anchor = anchor || i == kBc7AnchorSecond[partition];
        }
        index[i] = bits.read(anchor ? weightBits - 1u : weightBits);
    }

    for (std::uint32_t e = 0; e < endpointCount; ++e) {
        // Mode 1 shares one parity bit per subset, the rest per endpoint.
        const int pbit = pbitCount == 0
                             ? -1
                             : (mode == 1 ? static_cast<int>(pbits[e >> 1])
                                          : static_cast<int>(pbits[e]));
        for (std::uint32_t c = 0; c < 4; ++c) {
            endpoints[e][c] = c < comps ? bc7Expand(endpoints[e][c], endpointBits, pbit)
                                        : 255u;
        }
    }

    const std::uint8_t* weights = weightBits == 3 ? kBc7Weights3 : kBc7Weights2;
    const std::uint32_t weightCount = 1u << weightBits;
    std::uint8_t palette[3][8][4];
    for (std::uint32_t s = 0; s < subsets; ++s)
        for (std::uint32_t w = 0; w < weightCount; ++w)
            for (std::uint32_t c = 0; c < 4; ++c)
                palette[s][w][c] = bc7Interpolate(endpoints[s * 2][c],
                                                  endpoints[s * 2 + 1][c], weights, w);

    const std::uint8_t* table = three ? &kBc7Partition3[0][0] : &kBc7Partition2[0][0];
    for (std::uint32_t i = 0; i < 16; ++i) {
        const std::uint32_t subset = table[partition * 16u + i];
        for (std::uint32_t c = 0; c < 4; ++c) out[i][c] = palette[subset][index[i]][c];
    }
    return true;
}

// Modes 4/5 (one subset, dual plane): colour and alpha carry separate index
// sets, and the index-selection bit says which set drives which.
bool bc7DecodeDualPlane(std::uint32_t mode, const std::uint8_t* block,
                        std::uint8_t out[16][4]) {
    const std::uint32_t colourBits = (mode == 4) ? 5u : 7u;
    const std::uint32_t alphaEndpointBits = (mode == 4) ? 6u : 8u;
    const std::uint32_t weightBits = 2u;
    const std::uint32_t alphaWeightBits = (mode == 4) ? 3u : 2u;

    Bc7Bits bits{block};
    if (bits.read(mode + 1) != (1u << mode)) return false;
    const std::uint32_t rotation = bits.read(2);
    const std::uint32_t indexMode = (mode == 4) ? bits.read(1) : 0u;

    std::uint32_t endpoints[2][4] = {};
    for (std::uint32_t c = 0; c < 4; ++c)
        for (std::uint32_t e = 0; e < 2; ++e)
            endpoints[e][c] = bits.read(c == 3 ? alphaEndpointBits : colourBits);

    // The shorter set is always stored first; which plane it drives is what the
    // index-selection bit decides.
    std::uint32_t colourIndex[16] = {};
    std::uint32_t alphaIndex[16] = {};
    for (std::uint32_t i = 0; i < 16; ++i) {
        const std::uint32_t v = bits.read(i == 0 ? weightBits - 1u : weightBits);
        (indexMode ? alphaIndex : colourIndex)[i] = v;
    }
    for (std::uint32_t i = 0; i < 16; ++i) {
        const std::uint32_t v =
            bits.read(i == 0 ? alphaWeightBits - 1u : alphaWeightBits);
        (indexMode ? colourIndex : alphaIndex)[i] = v;
    }

    for (std::uint32_t e = 0; e < 2; ++e) {
        for (std::uint32_t c = 0; c < 3; ++c)
            endpoints[e][c] = bc7Expand(endpoints[e][c], colourBits, -1);
        endpoints[e][3] = bc7Expand(endpoints[e][3], alphaEndpointBits, -1);
    }

    std::uint8_t palette[8][4] = {};
    for (std::uint32_t w = 0; w < (1u << weightBits); ++w)
        for (std::uint32_t c = 0; c < 3; ++c)
            palette[w][c] = bc7Interpolate(endpoints[0][c], endpoints[1][c],
                                           kBc7Weights2, w);
    for (std::uint32_t w = 0; w < (1u << alphaWeightBits); ++w)
        palette[w][3] = bc7Interpolate(endpoints[0][3], endpoints[1][3],
                                       alphaWeightBits == 3 ? kBc7Weights3
                                                            : kBc7Weights2,
                                       w);

    for (std::uint32_t i = 0; i < 16; ++i) {
        for (std::uint32_t c = 0; c < 4; ++c) out[i][c] = palette[colourIndex[i]][c];
        out[i][3] = palette[alphaIndex[i]][3];
        // A rotation makes one component scalar and frees the alpha slot for it.
        if (rotation >= 1) std::swap(out[i][3], out[i][rotation - 1]);
    }
    return true;
}

// Mode 6: one subset, 7-bit components, one parity bit per endpoint, 4-bit
// indices. Stored component-major like the partitioned modes.
bool bc7DecodeSingle(const std::uint8_t* block, std::uint8_t out[16][4]) {
    Bc7Bits bits{block};
    if (bits.read(7) != (1u << 6)) return false;

    std::uint32_t endpoints[2][4] = {};
    for (std::uint32_t c = 0; c < 4; ++c)
        for (std::uint32_t e = 0; e < 2; ++e)
            endpoints[e][c] = bits.read(7);

    const int pbit[2] = {static_cast<int>(bits.read(1)),
                         static_cast<int>(bits.read(1))};

    std::uint32_t index[16] = {};
    for (std::uint32_t i = 0; i < 16; ++i)
        index[i] = bits.read(i == 0 ? 3u : 4u);

    std::uint8_t palette[16][4];
    for (std::uint32_t w = 0; w < 16; ++w)
        for (std::uint32_t c = 0; c < 4; ++c)
            palette[w][c] = bc7Interpolate(bc7Expand(endpoints[0][c], 7, pbit[0]),
                                           bc7Expand(endpoints[1][c], 7, pbit[1]),
                                           kBc7Weights4, w);

    for (std::uint32_t i = 0; i < 16; ++i)
        for (std::uint32_t c = 0; c < 4; ++c) out[i][c] = palette[index[i]][c];
    return true;
}

// Mode 8 and the "10011"/"10111"/... patterns are reserved: they report false
// so the caller can write opaque black instead of uninitialised memory.
bool decodeBc7Block(const std::uint8_t* block, std::uint8_t out[16][4]) {
    for (std::uint32_t mode = 0; mode < 8; ++mode) {
        if ((block[0] & (1u << mode)) == 0) continue;
        switch (mode) {
        case 0: case 2: return bc7DecodePartitioned(mode, block, out);
        case 1: case 3: case 7: return bc7DecodePartitioned(mode, block, out);
        case 4: case 5: return bc7DecodeDualPlane(mode, block, out);
        case 6: return bc7DecodeSingle(block, out);
        default: break;
        }
    }
    return false;
}

struct PixelFormat {
    std::uint32_t flags = 0;
    std::uint32_t fourcc = 0;
    std::uint32_t bit_count = 0;
    std::uint32_t red = 0;
    std::uint32_t green = 0;
    std::uint32_t blue = 0;
    std::uint32_t alpha = 0;

    bool hasAlphaMask() const { return alpha != 0; }
};

struct Header {
    std::uint32_t flags = 0;
    std::uint32_t height = 0;
    std::uint32_t width = 0;
    std::uint32_t pitch_or_linear = 0;
    std::uint32_t mip_count = 0;
    PixelFormat format;
};

std::size_t unpackedRowBytes(const Header& header) {
    return (static_cast<std::size_t>(header.width) * header.format.bit_count + 7) / 8;
}

std::size_t dataOffset(const Header& header) {
    const auto base = kDdsMagicSize + kDdsHeaderSize;
    const bool dx10 = (header.format.flags & kDdpfFourCc) != 0 &&
                      header.format.fourcc == kFourCcDx10;
    return dx10 ? base + kDdsDxt10HeaderSize : base;
}

// Byte stride of one surface row: trust the declared pitch when it is at
// least as wide as an unpacked row, otherwise fall back to the packed size.
std::size_t rowBytes(const Header& header) {
    const std::size_t packed = unpackedRowBytes(header);
    if ((header.flags & kDdsdPitch) != 0 && header.pitch_or_linear > packed) {
        return header.pitch_or_linear;
    }
    return packed;
}

bool parseHeader(std::string_view bytes, Header& header, std::string& error) {
    if (bytes.size() < kDdsMagicSize + kDdsHeaderSize) {
        error = "truncated DDS header";
        return false;
    }
    std::uint32_t size = 0;
    if (!readU32(bytes, kDdsMagicSize, size) || size != kDdsHeaderSize) {
        error = "DDS header size is " + std::to_string(size) + ", expected 124";
        return false;
    }
    if (!readU32(bytes, kDdsMagicSize + 4, header.flags) ||
        !readU32(bytes, kDdsMagicSize + 8, header.height) ||
        !readU32(bytes, kDdsMagicSize + 12, header.width) ||
        !readU32(bytes, kDdsMagicSize + 16, header.pitch_or_linear) ||
        !readU32(bytes, kDdsMagicSize + 24, header.mip_count)) {
        error = "truncated DDS header";
        return false;
    }

    // DDS_PIXELFORMAT: dwSize@76, dwFlags, dwFourCC, dwRGBBitCount,
    // dwR/G/B/ABitMask — so every field sits 4 bytes past the one before.
    constexpr std::size_t pf = kDdsPixelFormatOffset;
    if (!readU32(bytes, pf, size) || size != kDdsPixelFormatSize ||
        !readU32(bytes, pf + 4, header.format.flags) ||
        !readU32(bytes, pf + 8, header.format.fourcc) ||
        !readU32(bytes, pf + 12, header.format.bit_count) ||
        !readU32(bytes, pf + 16, header.format.red) ||
        !readU32(bytes, pf + 20, header.format.green) ||
        !readU32(bytes, pf + 24, header.format.blue) ||
        !readU32(bytes, pf + 28, header.format.alpha)) {
        error = "truncated DDS pixel format";
        return false;
    }
    if (size != kDdsPixelFormatSize) {
        error = "DDS pixel format size is " + std::to_string(size) +
                ", expected 32";
        return false;
    }

    if (header.width == 0 || header.height == 0) {
        error = "DDS reports a " + std::to_string(header.width) + "x" +
                std::to_string(header.height) + " surface";
        return false;
    }
    if (header.width > 65536 || header.height > 65536) {
        error = "DDS dimensions " + std::to_string(header.width) + "x" +
                std::to_string(header.height) + " exceed the 64K limit";
        return false;
    }
    if ((header.flags & kDdsdMipmapCount) == 0 || header.mip_count == 0 ||
        header.mip_count > 32) {
        header.mip_count = 1;
    }
    return true;
}

// Maps the pixel format onto a DdsFormat and fills in whatever masks the
// decoder needs. `header` is taken by reference because DX10 raw surfaces
// carry no masks at all: only the DXGI format id, from which the (unique)
// conventional channel order is inferred.
DdsFormat resolveFormat(Header& header, std::string_view bytes,
                        std::string& error) {
    const PixelFormat& pf = header.format;
    if ((pf.flags & kDdpfFourCc) != 0) {
        switch (pf.fourcc) {
        case kFourCcDxt1:
            return DdsFormat::bc1;
        case kFourCcDxt2: // premultiplied variants: identical block layout
        case kFourCcDxt3:
            return DdsFormat::bc2;
        case kFourCcDxt4:
        case kFourCcDxt5:
            return DdsFormat::bc3;
        case 116: // D3DFMT_A32B32G32R32F, written where a FourCC tag belongs
            return DdsFormat::rgba32f;
        case kFourCcDx10:
            break;
        default:
            error = "unsupported DDS FourCC tag '" +
                    std::string(reinterpret_cast<const char*>(&pf.fourcc), 4) +
                    "' (supported: DXT1, DXT3, DXT5, DX10)";
            return DdsFormat::unknown;
        }

        std::uint32_t dxgi = 0;
        if (!readU32(bytes, kDdsMagicSize + kDdsHeaderSize, dxgi)) {
            error = "truncated DX10 header";
            return DdsFormat::unknown;
        }
        switch (dxgi) {
        case 71: case 72: return DdsFormat::bc1; // BC1_UNORM[_SRGB]
        case 74: case 75: return DdsFormat::bc2; // BC2_UNORM[_SRGB]
        case 77: case 78: return DdsFormat::bc3; // BC3_UNORM[_SRGB]
        case 98: case 99: return DdsFormat::bc7; // BC7_UNORM[_SRGB]
        case 87: case 91:                        // B8G8R8A8[_SRGB]
            header.format.bit_count = 32;
            header.format.red = 0x00FF0000u;
            header.format.green = 0x0000FF00u;
            header.format.blue = 0x000000FFu;
            header.format.alpha = 0xFF000000u;
            return DdsFormat::raw;
        case 28: case 29:                        // R8G8B8A8[_SRGB]
            header.format.bit_count = 32;
            header.format.red = 0x000000FFu;
            header.format.green = 0x0000FF00u;
            header.format.blue = 0x00FF0000u;
            header.format.alpha = 0xFF000000u;
            return DdsFormat::raw;
        default:
            error = "unsupported DX10 DXGI_FORMAT " + std::to_string(dxgi) +
                    " (only BC1/BC2/BC3/BC7 and 8-bit RGBA/BGRA decode)";
            return DdsFormat::unknown;
        }
    }

    if ((pf.flags & kDdpfLuminance) != 0) {
        if (pf.bit_count != 8 && pf.bit_count != 16) {
            error = "unsupported luminance depth " + std::to_string(pf.bit_count);
            return DdsFormat::unknown;
        }
        return DdsFormat::luminance;
    }

    if ((pf.flags & (kDdpfRgb | kDdpfAlpha)) != 0) {
        if (pf.bit_count != 8 && pf.bit_count != 16 && pf.bit_count != 24 &&
            pf.bit_count != 32) {
            error = "unsupported RGB depth " + std::to_string(pf.bit_count);
            return DdsFormat::unknown;
        }
        // A8-only surface: no colour masks at all, the byte *is* the alpha.
        if ((pf.flags & kDdpfRgb) == 0 && header.format.alpha == 0) {
            header.format.alpha = 0x000000FFu;
        }
        return DdsFormat::raw;
    }

    error = "DDS pixel format has no RGB/FourCC/LUMINANCE flags";
    return DdsFormat::unknown;
}

std::size_t bytesNeeded(const Header& header, DdsFormat format) {
    if (format == DdsFormat::rgba32f)
        return static_cast<std::size_t>(header.width) *
               static_cast<std::size_t>(header.height) * 16u;
    if (format == DdsFormat::bc1 || format == DdsFormat::bc2) {
        return 8u * ((header.width + 3) / 4) * ((header.height + 3) / 4);
    }
    if (format == DdsFormat::bc3 || format == DdsFormat::bc7) {
        return 16u * ((header.width + 3) / 4) * ((header.height + 3) / 4);
    }
    return rowBytes(header) * header.height;
}

} // namespace

const char* ddsFormatName(DdsFormat format) {
    if (format == DdsFormat::rgba32f) return "A32B32G32R32F (HDR)";
    switch (format) {
    case DdsFormat::bc1: return "BC1/DXT1";
    case DdsFormat::bc2: return "BC2/DXT3";
    case DdsFormat::bc3: return "BC3/DXT5";
    case DdsFormat::bc7: return "BC7";
    case DdsFormat::raw: return "uncompressed (mask-driven)";
    case DdsFormat::luminance: return "uncompressed luminance";
    case DdsFormat::unknown: break;
    }
    return "unknown";
}

bool isDds(std::string_view bytes) {
    return bytes.size() >= kDdsMagicSize &&
           std::memcmp(bytes.data(), kDdsMagic, kDdsMagicSize) == 0;
}

DdsInfo readDdsInfo(std::string_view bytes) {
    DdsInfo info;
    if (!isDds(bytes)) {
        info.error = "not a DDS file (missing \"DDS \" magic)";
        return info;
    }

    Header header;
    if (!parseHeader(bytes, header, info.error)) return info;

    info.width = static_cast<int>(header.width);
    info.height = static_cast<int>(header.height);
    info.mip_count = static_cast<int>(header.mip_count);
    info.fourcc = header.format.fourcc;

    info.format = resolveFormat(header, bytes, info.error);
    if (info.format == DdsFormat::unknown) return info;

    const std::size_t offset = dataOffset(header);
    const std::size_t needed = bytesNeeded(header, info.format);
    const std::size_t available =
        offset > bytes.size() ? 0 : bytes.size() - offset;
    if (available < needed) {
        info.error = "DDS pixel data is " + std::to_string(available) +
                     " bytes, expected at least " + std::to_string(needed);
    }
    return info;
}

DdsImage decodeDds(std::string_view bytes) {
    DdsImage image;
    if (!isDds(bytes)) {
        image.info.error = "not a DDS file (missing \"DDS \" magic)";
        return image;
    }

    Header header;
    if (!parseHeader(bytes, header, image.info.error)) return image;
    image.info.width = static_cast<int>(header.width);
    image.info.height = static_cast<int>(header.height);
    image.info.mip_count = static_cast<int>(header.mip_count);
    image.info.fourcc = header.format.fourcc;

    const DdsFormat format = resolveFormat(header, bytes, image.info.error);
    if (format == DdsFormat::unknown) return image;
    image.info.format = format;

    const std::size_t offset = dataOffset(header);
    const std::size_t needed = bytesNeeded(header, format);
    if (offset > bytes.size() || bytes.size() - offset < needed) {
        image.info.error = "DDS pixel data is " +
                           std::to_string(offset > bytes.size() ? 0
                                                                : bytes.size() - offset) +
                           " bytes, expected at least " + std::to_string(needed);
        return image;
    }

    const int width = image.info.width;
    const int height = image.info.height;
    image.rgba.assign(static_cast<std::size_t>(width) *
                          static_cast<std::size_t>(height) * 4,
                      0);
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data()) + offset;

    if (format == DdsFormat::rgba32f) {
        // Linear radiance out of a panorama: Reinhard keeps the bright sky from
        // clipping to white, and the gamma step puts it back in the space the
        // albedo sampler expects. Alpha is dropped — a sky has none.
        const auto* floats = reinterpret_cast<const float*>(data);
        const std::size_t pixels = static_cast<std::size_t>(width) * height;
        // Gain before the curve: a panorama is an unlit backdrop, so it has to
        // survive being multiplied by the ambient term on its way to the screen.
        // Without it midday reads as dusk.
        auto encode = [](float v) {
            if (!(v > 0.f)) v = 0.f;
            v *= 4.2f;
            v = v / (1.f + v);
            v = std::pow(v, 1.f / 2.2f);
            if (v > 1.f) v = 1.f;
            return static_cast<std::uint8_t>(v * 255.f + 0.5f);
        };
        for (std::size_t i = 0; i < pixels; ++i) {
            image.rgba[i * 4 + 0] = encode(floats[i * 4 + 0]);
            image.rgba[i * 4 + 1] = encode(floats[i * 4 + 1]);
            image.rgba[i * 4 + 2] = encode(floats[i * 4 + 2]);
            image.rgba[i * 4 + 3] = 255;
        }
        return image;
    }

    if (format == DdsFormat::bc7) {
        std::uint8_t pixels[16][4];
        std::size_t cursor = 0;
        for (int by = 0; by < (height + 3) / 4; ++by) {
            for (int bx = 0; bx < (width + 3) / 4; ++bx) {
                // A reserved/corrupt block decodes to opaque black: visible in
                // the capture, never uninitialised memory on screen.
                if (!decodeBc7Block(data + cursor, pixels)) {
                    for (auto& pixel : pixels) {
                        pixel[0] = pixel[1] = pixel[2] = 0;
                        pixel[3] = 255;
                    }
                }
                cursor += 16;
                for (int i = 0; i < 16; ++i) {
                    const int x = bx * 4 + (i & 3);
                    const int y = by * 4 + (i >> 2);
                    if (x >= width || y >= height) continue;
                    std::uint8_t* dst = pixelAt(image.rgba, width, x, y);
                    for (int c = 0; c < 4; ++c) dst[c] = pixels[i][c];
                }
            }
        }
        return image;
    }

    if (format == DdsFormat::bc1 || format == DdsFormat::bc2 ||
        format == DdsFormat::bc3) {
        const std::size_t block_bytes = format == DdsFormat::bc3 ? 16u : 8u;
        std::uint8_t colour[64];
        std::uint8_t alpha[16];
        std::size_t cursor = 0;
        for (int by = 0; by < (height + 3) / 4; ++by) {
            for (int bx = 0; bx < (width + 3) / 4; ++bx) {
                const std::uint8_t* block = data + cursor;
                cursor += block_bytes;
                if (format == DdsFormat::bc1) {
                    decodeBc1Color(block, colour, false);
                    for (int i = 0; i < 16; ++i) alpha[i] = colour[i * 4 + 3];
                } else if (format == DdsFormat::bc2) {
                    decodeBc1Color(block + 8, colour, true);
                    for (int i = 0; i < 16; ++i) {
                        const std::uint8_t packed = block[i / 2];
                        const std::uint8_t nibble =
                            static_cast<std::uint8_t>((i & 1) ? packed >> 4
                                                              : packed & 0x0F);
                        alpha[i] = static_cast<std::uint8_t>(nibble * 17);
                    }
                } else {
                    decodeBc3Alpha(block, alpha);
                    decodeBc1Color(block + 8, colour, true);
                }
                for (int i = 0; i < 16; ++i) {
                    const int x = bx * 4 + (i & 3);
                    const int y = by * 4 + (i >> 2);
                    if (x >= width || y >= height) continue;
                    std::uint8_t* dst = pixelAt(image.rgba, width, x, y);
                    dst[0] = colour[i * 4 + 0];
                    dst[1] = colour[i * 4 + 1];
                    dst[2] = colour[i * 4 + 2];
                    dst[3] = alpha[i];
                }
            }
        }
        return image;
    }

    const std::size_t pitch = rowBytes(header);
    const std::uint32_t bpp = header.format.bit_count;
    const std::size_t stride = bpp / 8;
    const bool luminance = format == DdsFormat::luminance;
    for (int y = 0; y < height; ++y) {
        const auto* row = data + static_cast<std::size_t>(y) * pitch;
        for (int x = 0; x < width; ++x) {
            const auto byte_offset = static_cast<std::size_t>(x) * stride;
            if (byte_offset + stride > pitch) break;
            const std::uint32_t pixel = readPixelAt(row, byte_offset, bpp);
            std::uint8_t* dst = pixelAt(image.rgba, width, x, y);
            if (luminance) {
                const std::uint8_t luma = channelFromMask(pixel, header.format.red);
                dst[0] = luma;
                dst[1] = luma;
                dst[2] = luma;
            } else {
                dst[0] = channelFromMask(pixel, header.format.red);
                dst[1] = channelFromMask(pixel, header.format.green);
                dst[2] = channelFromMask(pixel, header.format.blue);
            }
            dst[3] = header.format.hasAlphaMask()
                         ? channelFromMask(pixel, header.format.alpha)
                         : static_cast<std::uint8_t>(255);
        }
    }
    return image;
}

} // namespace ks::engine::fileformat
