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
                    " (only BC1/BC2/BC3 and 8-bit RGBA/BGRA decode)";
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
    if (format == DdsFormat::bc1 || format == DdsFormat::bc2) {
        return 8u * ((header.width + 3) / 4) * ((header.height + 3) / 4);
    }
    if (format == DdsFormat::bc3) {
        return 16u * ((header.width + 3) / 4) * ((header.height + 3) / 4);
    }
    return rowBytes(header) * header.height;
}

} // namespace

const char* ddsFormatName(DdsFormat format) {
    switch (format) {
    case DdsFormat::bc1: return "BC1/DXT1";
    case DdsFormat::bc2: return "BC2/DXT3";
    case DdsFormat::bc3: return "BC3/DXT5";
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
