#pragma once

// Qt-free DirectDraw Surface (.dds) reader — the format every Assetto Corsa
// KN5 embeds as its texture payload. std-only, no dependencies, binary-safe.
//
// Layout:
//   char   magic[4]      = "DDS "
//   DDS_HEADER           124 bytes
//       u32 dwSize (=124), u32 dwFlags, u32 dwHeight, u32 dwWidth,
//       u32 dwPitchOrLinearSize, u32 dwDepth, u32 dwMipMapCount,
//       u32 dwReserved1[11],
//       DDS_PIXELFORMAT   32 bytes
//           u32 dwSize (=32), u32 dwFlags, u32 dwFourCC, u32 dwRGBBitCount,
//           u32 dwRBitMask, dwGBitMask, dwBBitMask, dwABitMask
//       u32 dwCaps, dwCaps2, dwCaps3, dwCaps4, u32 dwReserved2
//   [DDS_HEADER_DXT10]   20 bytes, only when dwFourCC == "DX10"
//   pixels
//
// Decoded to mip 0 as top-down RGBA8. Supported:
//   - FourCC BC1/"DXT1", BC2/"DXT3", BC3/"DXT5"
//   - uncompressed 8/16/24/32-bit with R/G/B/A bit masks (covers A8R8G8B8,
//     A8B8G8R8, X8R8G8B8, RGB8, L8/luminance, A8L8)
//   - DX10 header for BC1/BC2/BC3 + R8G8B8A8/B8G8R8A8 (best effort; the
//     format enum is small and anything else reports the id in `error`)
//
// Deliberately rejected (clear message, never silent corruption): BC4/BC5/
// BC6H/BC7 and any format whose payload size doesn't fit the buffer. Real AC
// content uses DXT1/DXT5 almost exclusively (BC7 appears only in newer mods,
// which `ContentRepair` already flags as unusual for AC).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ks::engine::fileformat {

inline constexpr char kDdsMagic[4] = {'D', 'D', 'S', ' '};
inline constexpr std::size_t kDdsMagicSize = 4;
inline constexpr std::size_t kDdsHeaderSize = 124;      // bytes after magic
inline constexpr std::size_t kDdsPixelFormatOffset = 76; // from file start
inline constexpr std::size_t kDdsPixelFormatSize = 32;
inline constexpr std::size_t kDdsDxt10HeaderSize = 20;

enum class DdsFormat {
    unknown,
    bc1,
    bc2,
    bc3,
    raw,        // mask-driven RGB(A), 8/16/24/32 bits per pixel
    luminance,  // 8-bit grayscale (DDPF_LUMINANCE), alpha if a mask says so
};

// Human-readable tag for a DdsFormat ("BC1/DXT1", "uncompressed RGBA8", ...).
const char* ddsFormatName(DdsFormat format);

struct DdsInfo {
    int width = 0;
    int height = 0;
    int mip_count = 1;
    int array_size = 1;
    DdsFormat format = DdsFormat::unknown;
    std::uint32_t fourcc = 0;   // raw dwFourCC, 0 when the tag is a mask combo
    std::string error;          // empty on success

    bool ok() const { return error.empty(); }
    bool compressed() const {
        return format == DdsFormat::bc1 || format == DdsFormat::bc2 ||
               format == DdsFormat::bc3;
    }
    bool dx10() const { return fourcc == dx10FourCc(); }

    static constexpr std::uint32_t dx10FourCc() {
        return 0x30315844u; // "DX10" little-endian
    }
};

struct DdsImage {
    DdsInfo info;
    std::vector<std::uint8_t> rgba; // width * height * 4, top-down, RGBA8

    bool ok() const { return info.ok(); }
};

// True when the buffer starts with the 4-byte DDS magic.
bool isDds(std::string_view bytes);

// Header-only inspection: no pixel data is touched, so it is cheap enough to
// run on every extracted KN5 texture payload.
DdsInfo readDdsInfo(std::string_view bytes);

// Decodes mip 0 to top-down RGBA8. `info.error` is set on failure and
// `rgba` stays empty.
DdsImage decodeDds(std::string_view bytes);

} // namespace ks::engine::fileformat
