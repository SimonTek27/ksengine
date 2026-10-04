#include "FontTtfLoader.h"

#include <cstdint>
#include <fstream>
#include <sstream>

#include "FontAtlas.h"
#include "engine/FileFormat/TtfRasterizer.h"
#include "engine/FileFormat/TtfReader.h"

namespace ks {
namespace sim {
namespace ui {
namespace ff = ks::engine::fileformat;

bool loadTtfFont(FontAtlas& atlas, const std::string& path, float pixelSize,
                 std::string* error) {
    auto fail = [error](const std::string& message) -> bool {
        if (error != nullptr) *error = message;
        return false;
    };

    if (pixelSize <= 0.f) return fail("pixelSize must be positive");

    std::ifstream in(path, std::ios::binary);
    if (!in) return fail("cannot open font: " + path);
    std::ostringstream contents;
    contents << in.rdbuf();
    const std::string bytes = contents.str();
    if (bytes.empty()) return fail("empty font file: " + path);

    ff::TtfReader reader;
    std::string detail;
    if (!reader.load(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &detail)) {
        return fail(path + ": " + detail);
    }

    ff::TtfRasterOptions options;
    options.pixelSize = pixelSize;
    ff::TtfAtlas ttfAtlas;
    if (!ff::buildTtfAtlas(reader, options, ttfAtlas, &detail)) {
        return fail(path + ": " + detail);
    }
    if (!atlas.loadTtf(ttfAtlas)) return fail(path + ": atlas rejected");
    return true;
}

}  // namespace ui
}  // namespace sim
}  // namespace ks
