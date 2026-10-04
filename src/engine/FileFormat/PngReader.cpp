#include "PngReader.h"

#include "Deflate.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace ks::engine::fileformat {

namespace {

const std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

bool fail(std::string* error, const char* msg) {
    if (error != nullptr && error->empty()) *error = msg;
    return false;
}

std::uint32_t u32be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return ~crc;
}

int paethPredictor(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

} // namespace

bool loadPngGray(std::string_view bytes, GrayImage& out, std::string* error) {
    out = GrayImage{};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSignature, 8) != 0)
        return fail(error, "not a PNG (bad signature)");
    if (bytes.size() < 8 + 25) return fail(error, "truncated PNG (no IHDR)");

    const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    const std::size_t size = bytes.size();

    std::uint32_t width = 0, height = 0;
    int bitDepth = 0, colorType = 0, interlace = 0;
    bool haveIhdr = false, haveIend = false;
    std::vector<std::uint8_t> idat;

    std::size_t off = 8;
    while (off + 12 <= size) {
        const std::uint32_t len = u32be(data + off);
        if (len > size || off + 12u + len > size)
            return fail(error, "truncated PNG chunk");
        const std::uint8_t* type = data + off + 4;
        const std::uint8_t* body = type + 4;
        const std::uint32_t storedCrc = u32be(body + len);
        if (crc32(type, 4u + len) != storedCrc)
            return fail(error, "PNG chunk CRC mismatch");

        const bool critical = (type[0] & 0x20u) == 0;
        if (!haveIhdr) {
            if (std::memcmp(type, "IHDR", 4) != 0) return fail(error, "IHDR is not the first chunk");
            if (len != 13) return fail(error, "bad IHDR length");
            width = u32be(body);
            height = u32be(body + 4);
            bitDepth = body[8];
            colorType = body[9];
            interlace = body[12];
            if (body[10] != 0 || body[11] != 0) return fail(error, "unsupported PNG compression/filter method");
            if (width == 0 || height == 0 || width > 16384 || height > 16384)
                return fail(error, "unsupported PNG dimensions");
            if (colorType != 0) return fail(error, "only grayscale PNGs are supported");
            if (bitDepth != 8 && bitDepth != 16) return fail(error, "only 8/16-bit PNGs are supported");
            if (interlace != 0) return fail(error, "interlaced PNGs are not supported");
            haveIhdr = true;
        } else if (std::memcmp(type, "IHDR", 4) == 0) {
            return fail(error, "duplicate IHDR");
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), body, body + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            if (len != 0) return fail(error, "bad IEND length");
            haveIend = true;
            break;
        } else if (critical && std::memcmp(type, "PLTE", 4) != 0) {
            // PLTE is optional even for grayscale; every other critical chunk
            // (i.e. one this reader does not know) means the file is not one
            // we can honour.
            return fail(error, "unsupported critical PNG chunk");
        }

        off += 12u + len;
    }

    if (!haveIhdr) return fail(error, "missing IHDR");
    if (idat.empty()) return fail(error, "missing IDAT");
    if (!haveIend) return fail(error, "missing IEND");

    std::vector<std::uint8_t> raw;
    {
        std::string inflateError;
        const std::string_view stream(reinterpret_cast<const char*>(idat.data()), idat.size());
        if (!inflateZlib(stream, raw, &inflateError)) {
            if (error != nullptr && error->empty()) *error = "PNG IDAT: " + inflateError;
            return false;
        }
    }

    const std::size_t bpp = bitDepth == 16 ? 2u : 1u;
    const std::size_t stride = static_cast<std::size_t>(width) * bpp;
    const std::size_t rowBytes = 1u + stride;
    if (raw.size() != rowBytes * height) return fail(error, "PNG decompressed size mismatch");

    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.pixels.resize(static_cast<std::size_t>(width) * height);

    std::vector<std::uint8_t> prev(stride, 0);
    std::vector<std::uint8_t> cur(stride, 0);
    std::size_t pos = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t filter = raw[pos++];
        std::memcpy(cur.data(), raw.data() + pos, stride);
        pos += stride;
        switch (filter) {
            case 0:
                break;
            case 1:
                for (std::size_t i = bpp; i < stride; ++i)
                    cur[i] = static_cast<std::uint8_t>(cur[i] + cur[i - bpp]);
                break;
            case 2:
                for (std::size_t i = 0; i < stride; ++i)
                    cur[i] = static_cast<std::uint8_t>(cur[i] + prev[i]);
                break;
            case 3:
                for (std::size_t i = 0; i < stride; ++i) {
                    const int left = i >= bpp ? cur[i - bpp] : 0;
                    cur[i] = static_cast<std::uint8_t>(cur[i] + (left + prev[i]) / 2);
                }
                break;
            case 4:
                for (std::size_t i = 0; i < stride; ++i) {
                    const int left = i >= bpp ? cur[i - bpp] : 0;
                    const int upLeft = i >= bpp ? prev[i - bpp] : 0;
                    cur[i] = static_cast<std::uint8_t>(cur[i] + paethPredictor(left, prev[i], upLeft));
                }
                break;
            default:
                return fail(error, "unknown PNG filter type");
        }

        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint16_t v;
            if (bpp == 1) {
                v = static_cast<std::uint16_t>(cur[x]) * 257u;
            } else {
                v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(cur[x * 2]) << 8) |
                                               cur[x * 2 + 1]);
            }
            out.pixels[static_cast<std::size_t>(y) * width + x] = v;
        }
        prev.swap(cur);
    }

    return true;
}

bool loadPngGrayFile(const std::string& path, GrayImage& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return fail(error, "cannot open PNG file");
    const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return loadPngGray(bytes, out, error);
}

} // namespace ks::engine::fileformat
