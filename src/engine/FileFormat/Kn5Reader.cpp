#include "Kn5Reader.h"

#include <cstring>
#include <fstream>

namespace ks::engine::fileformat {

namespace {

// Count ceilings mirrored from the Python reference validated against the
// full AC corpus; anything above these is a corrupt/hostile file, not real
// content (real maxima: textures ~10^2, materials ~10^2, bones ~10^1).
constexpr std::int32_t kMaxRecords = 100000;   // textures/materials/children
constexpr std::int32_t kMaxProperties = 10000;
constexpr std::int32_t kMaxMappings = 10000;
constexpr std::int32_t kMaxBones = 1000;
constexpr int kMaxDepth = 64;

constexpr std::size_t kMeshVertexBytes = 44;   // pos3+normal3+uv2+tangent3
constexpr std::size_t kSkinnedVertexBytes = 76;
constexpr std::size_t kMeshTailBytes = 33;     // materialId..isRenderable
constexpr std::size_t kSkinnedTailBytes = 16;  // materialId..lodOut
constexpr std::size_t kMatrixBytes = 16 * sizeof(float);

// Bounds-checked little-endian cursor over the file image. Every read either
// consumes exactly what it promises or latches the first error; `ok()` then
// stays false and callers unwind with early returns.
class ByteReader {
public:
    explicit ByteReader(std::string_view bytes)
        : data_(reinterpret_cast<const std::uint8_t*>(bytes.data())),
          size_(bytes.size()) {}

    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    std::size_t position() const { return pos_; }
    std::size_t remaining() const { return size_ - pos_; }

    bool fail(std::string message) {
        if (error_.empty()) {
            error_ = "offset " + std::to_string(pos_) + ": " + std::move(message);
        }
        return false;
    }

    bool read(void* dst, std::size_t n) {
        if (n > remaining()) return fail("unexpected end of file");
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    bool skip(std::size_t n) {
        if (n > remaining()) return fail("payload extends past end of file");
        pos_ += n;
        return true;
    }

    // Copies the next `n` bytes out instead of stepping over them.
    bool readBytes(std::vector<std::uint8_t>& out, std::size_t n) {
        if (n > remaining()) return fail("payload extends past end of file");
        const auto* begin = data_ + pos_;
        out.assign(begin, begin + n);
        pos_ += n;
        return true;
    }

    bool readU32(std::uint32_t& v) { return read(&v, sizeof(v)); }
    bool readI32(std::int32_t& v) { return read(&v, sizeof(v)); }
    bool readU8(std::uint8_t& v) { return read(&v, sizeof(v)); }

    bool readString(std::string& out) {
        std::int32_t len = 0;
        if (!readI32(len)) return false;
        if (len < 0) return fail("negative string length");
        if (static_cast<std::size_t>(len) > remaining()) {
            return fail("string extends past end of file");
        }
        out.assign(reinterpret_cast<const char*>(data_ + pos_),
                   static_cast<std::size_t>(len));
        pos_ += static_cast<std::size_t>(len);
        return true;
    }

    // Reads a count field and rejects implausible values up front so a
    // corrupt count can't drive a huge allocation or loop.
    bool readCount(std::int32_t& count, std::int32_t limit, const char* what) {
        if (!readI32(count)) return false;
        if (count < 0 || count > limit) {
            return fail(std::string("implausible ") + what + " count " +
                        std::to_string(count));
        }
        return true;
    }

    // Reserves a byte range of exactly `n * stride` and reports whether that
    // many bytes exist — used before allocating vertex/index arrays.
    bool bytesAvailable(std::uint64_t n, std::uint64_t stride, const char* what) {
        if (stride != 0 && n > remaining() / stride) {
            return fail(std::string(what) + " extends past end of file");
        }
        return true;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    std::string error_;
};

// Row-major matrix product for the row-vector convention: result = a * b,
// i.e. applying `a` first, then `b` (p * (a * b) == (p * a) * b).
void multiplyMatrices(const std::array<float, 16>& a,
                      const std::array<float, 16>& b,
                      std::array<float, 16>& out) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[static_cast<std::size_t>(r * 4 + k)] *
                       b[static_cast<std::size_t>(k * 4 + c)];
            }
            out[static_cast<std::size_t>(r * 4 + c)] = sum;
        }
    }
}

bool parseTextures(ByteReader& r, std::vector<Kn5Texture>& out, bool keep_data) {
    std::int32_t count = 0;
    if (!r.readCount(count, kMaxRecords, "texture")) return false;
    for (std::int32_t i = 0; i < count; ++i) {
        std::int32_t active = 0;
        if (!r.readI32(active)) return false;
        if (active == 0) continue; // flag-only record, nothing else stored

        Kn5Texture texture;
        std::uint32_t data_len = 0;
        if (!r.readString(texture.name)) return false;
        if (!r.readU32(data_len)) return false;
        if (keep_data) {
            if (!r.readBytes(texture.data, data_len)) return false;
        } else if (!r.skip(data_len)) {
            return false;
        }
        texture.data_size = data_len;
        out.push_back(std::move(texture));
    }
    return true;
}

bool parseMaterials(ByteReader& r, std::int32_t version,
                    std::vector<Kn5Material>& out) {
    std::int32_t count = 0;
    if (!r.readCount(count, kMaxRecords, "material")) return false;
    for (std::int32_t i = 0; i < count; ++i) {
        Kn5Material material;
        if (!r.readString(material.name)) return false;
        if (!r.readString(material.shader)) return false;
        if (!r.readU8(material.blend)) return false;
        if (!r.readU8(material.alpha_tested)) return false;
        if (version >= 5 && !r.readI32(material.depth_mode)) return false;

        std::int32_t properties = 0;
        if (!r.readCount(properties, kMaxProperties, "material property")) {
            return false;
        }
        for (std::int32_t p = 0; p < properties; ++p) {
            Kn5Property property;
            if (!r.readString(property.name)) return false;
            if (!r.read(property.value.data(), property.value.size())) {
                return false;
            }
            material.properties.push_back(std::move(property));
        }

        std::int32_t mappings = 0;
        if (!r.readCount(mappings, kMaxMappings, "texture mapping")) return false;
        for (std::int32_t m = 0; m < mappings; ++m) {
            Kn5TextureMapping mapping;
            if (!r.readString(mapping.name)) return false;
            if (!r.readI32(mapping.slot)) return false;
            if (!r.readString(mapping.texture)) return false;
            material.mappings.push_back(std::move(mapping));
        }
        out.push_back(std::move(material));
    }
    return true;
}

bool parseMeshPayload(ByteReader& r, Kn5Mesh& mesh) {
    std::uint8_t flags[3];
    if (!r.read(flags, sizeof(flags))) return false; // castShadows/visible/transparent

    std::uint32_t vertex_count = 0;
    if (!r.readU32(vertex_count)) return false;
    if (!r.bytesAvailable(vertex_count, kMeshVertexBytes, "vertex data")) {
        return false;
    }
    mesh.positions.reserve(static_cast<std::size_t>(vertex_count) * 3);
    mesh.normals.reserve(static_cast<std::size_t>(vertex_count) * 3);
    mesh.uvs.reserve(static_cast<std::size_t>(vertex_count) * 2);
    for (std::uint32_t i = 0; i < vertex_count; ++i) {
        float vertex[11]; // pos3, normal3, uv2, tangent3 — tangent dropped
        if (!r.read(vertex, sizeof(vertex))) return false;
        mesh.positions.insert(mesh.positions.end(), vertex, vertex + 3);
        mesh.normals.insert(mesh.normals.end(), vertex + 3, vertex + 6);
        mesh.uvs.push_back(vertex[6]);
        mesh.uvs.push_back(vertex[7]);
    }

    std::uint32_t index_count = 0;
    if (!r.readU32(index_count)) return false;
    if (!r.bytesAvailable(index_count, sizeof(std::uint16_t), "index data")) {
        return false;
    }
    mesh.indices.resize(index_count);
    for (std::uint32_t i = 0; i < index_count; ++i) {
        std::uint16_t index = 0;
        if (!r.read(&index, sizeof(index))) return false;
        if (index >= vertex_count) {
            return r.fail("index " + std::to_string(index) +
                          " out of range (vertex count " +
                          std::to_string(vertex_count) + ")");
        }
        mesh.indices[i] = index;
    }

    if (!r.readI32(mesh.material_id)) return false;
    if (!r.readU32(mesh.layer)) return false;
    return r.skip(kMeshTailBytes - 8); // lodIn, lodOut, bsCenter, bsRadius, renderable
}

bool parseSkinnedPayload(ByteReader& r) {
    std::uint8_t flags[3];
    if (!r.read(flags, sizeof(flags))) return false;

    std::int32_t bones = 0;
    if (!r.readCount(bones, kMaxBones, "bone")) return false;
    for (std::int32_t i = 0; i < bones; ++i) {
        std::string name;
        if (!r.readString(name)) return false;
        if (!r.skip(kMatrixBytes)) return false;
    }

    std::uint32_t vertex_count = 0;
    if (!r.readU32(vertex_count)) return false;
    if (!r.bytesAvailable(vertex_count, kSkinnedVertexBytes, "skinned vertex data")) {
        return false;
    }
    if (!r.skip(static_cast<std::size_t>(vertex_count) * kSkinnedVertexBytes)) {
        return false;
    }

    std::uint32_t index_count = 0;
    if (!r.readU32(index_count)) return false;
    if (!r.bytesAvailable(index_count, sizeof(std::uint16_t), "skinned index data")) {
        return false;
    }
    return r.skip(static_cast<std::size_t>(index_count) * sizeof(std::uint16_t) +
                  kSkinnedTailBytes);
}

bool parseNode(ByteReader& r, const std::array<float, 16>& parent_world,
               int depth, Kn5Node& out) {
    if (depth > kMaxDepth) return r.fail("node nesting too deep");

    std::int32_t class_value = 0;
    if (!r.readI32(class_value)) return false;
    if (class_value < 1 || class_value > 3) {
        return r.fail("unknown node class " + std::to_string(class_value));
    }
    out.node_class = static_cast<Kn5NodeClass>(class_value);

    if (!r.readString(out.name)) return false;
    std::int32_t children = 0;
    if (!r.readCount(children, kMaxRecords, "node child")) return false;
    if (!r.readU8(out.active)) return false;

    std::array<float, 16> world = parent_world;
    switch (out.node_class) {
    case Kn5NodeClass::base:
        if (!r.read(out.transform.data(), out.transform.size() * sizeof(float))) {
            return false;
        }
        multiplyMatrices(out.transform, parent_world, world);
        break;
    case Kn5NodeClass::mesh:
        if (!parseMeshPayload(r, out.mesh)) return false;
        out.has_mesh = true;
        out.mesh.name = out.name;    // baker keys files off the mesh name
        out.mesh.world = world;      // meshes carry no transform of their own
        break;
    case Kn5NodeClass::skinned_mesh:
        if (!parseSkinnedPayload(r)) return false;
        break;
    }

    for (std::int32_t i = 0; i < children; ++i) {
        Kn5Node child;
        if (!parseNode(r, world, depth + 1, child)) return false;
        out.children.push_back(std::move(child));
    }
    return true;
}

} // namespace

Kn5ParseResult parseKn5(std::string_view bytes, const Kn5ParseOptions& options) {
    Kn5ParseResult result;
    ByteReader reader(bytes);

    char magic[kKn5MagicSize];
    if (!reader.read(magic, sizeof(magic))) {
        result.error = reader.error();
        return result;
    }
    if (std::memcmp(magic, kKn5Magic, kKn5MagicSize) != 0) {
        result.error = "offset 0: bad KN5 magic (expected \"sc6969\")";
        return result;
    }

    if (!reader.readI32(result.file.version)) {
        result.error = reader.error();
        return result;
    }
    if (result.file.version < kKn5MinVersion ||
        result.file.version > kKn5MaxVersion) {
        reader.fail("unsupported KN5 version " +
                    std::to_string(result.file.version));
        result.error = reader.error();
        return result;
    }
    if (result.file.version > 5 && !reader.skip(sizeof(std::int32_t))) {
        result.error = reader.error();
        return result;
    }

    if (!parseTextures(reader, result.file.textures,
                       options.keep_texture_data) ||
        !parseMaterials(reader, result.file.version, result.file.materials)) {
        result.error = reader.error();
        return result;
    }

    // Exactly one root node; trailing bytes after the tree are tolerated
    // (observed in 25 mod files that append data past the hierarchy).
    Kn5Node root;
    if (!parseNode(reader, kn5IdentityMatrix(), 0, root)) {
        result.error = reader.error();
        return result;
    }
    result.file.nodes.push_back(std::move(root));
    return result;
}

Kn5ParseResult parseKn5File(const std::string& path,
                            const Kn5ParseOptions& options) {
    // Sized read: istreambuf_iterator walks the stream byte-by-byte, which
    // costs tens of seconds on the ~900 MB track files in AC content.
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        Kn5ParseResult result;
        result.error = "cannot open file: " + path;
        return result;
    }
    const std::streamoff size = input.tellg();
    if (size < 0) {
        Kn5ParseResult result;
        result.error = "cannot determine file size: " + path;
        return result;
    }
    input.seekg(0, std::ios::beg);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.read(bytes.data(), size);
    if (!input && size > 0) {
        Kn5ParseResult result;
        result.error = "read error: " + path;
        return result;
    }
    return parseKn5(bytes, options);
}

} // namespace ks::engine::fileformat
