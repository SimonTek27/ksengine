#pragma once
#include "KsExport.h"

// Qt-free image codec used by the material module.
//
// QImage::save()/QImage::load() were the only reason TextureAtlas and
// TexturePaint needed Qt for file IO. This provides the same job without Qt
// and without pulling in a third-party dependency:
//   - zlib streams: full inflate (dynamic/fixed/stored) and deflate
//     (dynamic Huffman with LZ77, falling back to fixed Huffman)
//   - PNG read/write (color types 0/2/3/4/6, 8 bit, no interlacing)
//   - BMP read/write (24/32 bit uncompressed)
//   - TGA read/write (type 2 uncompressed, type 10 RLE)
// The decoder picks the format from the file signature, so callers can keep
// passing any path the way they did with QImage::load().

#include <cstdint>
#include <string>
#include <vector>

namespace ks::image {

// Packed 0xAARRGGBB pixel, matches ks::Rgb / QColor::rgba().
using Rgba32 = std::uint32_t;

struct RawImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // width*height*4, RGBA order, straight alpha

    bool isNull() const { return width <= 0 || height <= 0 || rgba.empty(); }
    void resize(int w, int h)
    {
        width = w;
        height = h;
        rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0);
    }
};

// Raw zlib/deflate primitives (also useful on their own).
bool zlibCompress(const std::uint8_t* src, std::size_t len,
                  std::vector<std::uint8_t>* out, bool storeOnly = false,
                  std::string* err = nullptr);
bool zlibDecompress(const std::uint8_t* src, std::size_t len,
                    std::vector<std::uint8_t>* out, std::size_t maxOut = 0,
                    std::string* err = nullptr);

std::uint32_t crc32(const void* data, std::size_t len, std::uint32_t seed = 0);
std::uint32_t adler32(const void* data, std::size_t len, std::uint32_t seed = 1);

// Format detection by signature, independent of the file extension.
bool decodeImage(const std::uint8_t* data, std::size_t len, RawImage* out,
                 std::string* err = nullptr);
bool decodePng(const std::uint8_t* data, std::size_t len, RawImage* out,
               std::string* err = nullptr);
bool decodeBmp(const std::uint8_t* data, std::size_t len, RawImage* out,
               std::string* err = nullptr);
bool decodeTga(const std::uint8_t* data, std::size_t len, RawImage* out,
               std::string* err = nullptr);

// Encoders. encodeImage() dispatches on the extension in `path`
// (".bmp" -> BMP, ".tga" -> TGA, everything else -> PNG).
bool encodePng(const RawImage& image, std::vector<std::uint8_t>* out,
               std::string* err = nullptr);
bool encodeBmp(const RawImage& image, std::vector<std::uint8_t>* out,
               std::string* err = nullptr);
bool encodeTga(const RawImage& image, std::vector<std::uint8_t>* out,
               std::string* err = nullptr);
bool encodeImage(const RawImage& image, const std::string& path,
                 std::vector<std::uint8_t>* out, std::string* err = nullptr);

// Whole-file helpers used by Image::load()/Image::save().
bool loadImageFile(const std::string& path, RawImage* out, std::string* err = nullptr);
KSENGINE_API bool saveImageFile(const std::string& path, const RawImage& image, std::string* err = nullptr);

} // namespace ks::image
