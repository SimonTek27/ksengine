#pragma once

// Qt-free reader for the Assetto Corsa KN5 mesh container.
//
// The format below was verified byte-for-byte against ~2770 real AC .kn5
// files (content/cars, content/tracks, content/objects3D) and against
// actools' C# implementation (actools/AcTools/Kn5File/Kn5Basic.cs), which is
// the reference reader the AC modding ecosystem is built on. Note that the
// older assumptions baked into KN5Types.h / KN5Parser (magic 0x346E6B73 /
// "KN5\0") do NOT match real files — the magic is the 6 ASCII bytes
// "sc6969".
//
// Layout:
//   char   magic[6]     = "sc6969"
//   int32  version      = 4 | 5 | 6
//   int32  extra        (only when version > 5)
//   int32  textureCount, then per texture record:
//            int32 active; when active == 0 the record ends here
//            (some mod exporters write flag-only records; vanilla files are
//            always active=1), otherwise:
//            int32 nameLen + name bytes + uint32 dataLen + data bytes
//            (data is a full .dds image; skipped unless Kn5ParseOptions
//            asks for it to be kept)
//   int32  materialCount, then per material:
//            string name, string shader, u8 blend, u8 alphaTested,
//            int32 depthMode          <- version >= 5 only; absent in v4
//            int32 propertyCount { string name + 40 raw bytes }
//            int32 mappingCount  { string name + int32 slot + string texture }
//   node hierarchy: a single root node, then children recursively
//     (preorder: header + own payload first, then children):
//            int32 nodeClass (1 Base | 2 Mesh | 3 SkinnedMesh),
//            string name, int32 childrenCount, u8 active, payload:
//              Base        : 16-float (64-byte) transform matrix
//              Mesh        : 3 flag bytes, u32 vertexCount,
//                            44-byte vertices (pos3, normal3, uv2, tangent3
//                            as floats), u32 indexCount, u16 indices,
//                            33-byte tail (materialId u32, layer u32,
//                            lodIn f32, lodOut f32, bsCenter vec3,
//                            bsRadius f32, isRenderable u8)
//              SkinnedMesh : 3 flag bytes, u32 boneCount { string + 64-byte
//                            matrix }, u32 vertexCount, 76-byte vertices
//                            (44 + weights vec4 + bone indices vec4-as-float),
//                            u32 indexCount, u16 indices,
//                            16-byte tail (materialId, layer, lodIn, lodOut)
//
// Matrices use the DirectX row-vector convention: p' = p * M, so the
// translation lives in elements 12..14 and a child composes as
// world = local * parent (matching actools' `node.Transform * parentMatrix`).
//
// Tolerated on purpose (observed in the wild): trailing bytes after the node
// tree (25 mod files append data), non-ASCII name bytes, and duplicate mesh
// names. Rejected: the two known third-party encrypted files (magic check).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ks::engine::fileformat {

inline constexpr std::size_t kKn5MagicSize = 6;
inline constexpr char kKn5Magic[kKn5MagicSize + 1] = "sc6969";
inline constexpr std::int32_t kKn5MinVersion = 4;
inline constexpr std::int32_t kKn5MaxVersion = 6;

// Row-major 4x4, row-vector convention (translation in [12..14]).
constexpr std::array<float, 16> kn5IdentityMatrix() {
    return {1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
}

enum class Kn5NodeClass : std::int32_t {
    base = 1,
    mesh = 2,
    skinned_mesh = 3,
};

struct Kn5Texture {
    std::string name;
    std::uint32_t data_size = 0; // payload size, always reported
    // Raw payload bytes — normally a complete .dds image (magic included).
    // Only filled when Kn5ParseOptions::keep_texture_data is set; extracting
    // them doubles peak memory for the file, which the runtime (which loads
    // textures through the GPU instead) has no reason to pay for.
    std::vector<std::uint8_t> data;
};

// One shader property: a name plus an opaque 40-byte ValueA/B/C/D blob.
// Kept raw — nothing in the native runtime consumes these yet, but keeping
// them makes the reader usable for tooling that does.
struct Kn5Property {
    std::string name;
    std::array<std::uint8_t, 40> value{};
};

struct Kn5TextureMapping {
    std::string name;
    std::int32_t slot = 0;
    std::string texture;
};

struct Kn5Material {
    std::string name;
    std::string shader;
    std::uint8_t blend = 0;
    std::uint8_t alpha_tested = 0;
    std::int32_t depth_mode = 0; // 0 for version-4 files (field absent)
    std::vector<Kn5Property> properties;
    std::vector<Kn5TextureMapping> mappings;
};

struct Kn5Mesh {
    std::string name;
    std::int32_t material_id = -1;
    std::uint32_t layer = 0;
    // Absolute transform of the mesh, accumulated from its Base ancestors at
    // parse time (row-vector convention). Applied by Kn5Baker.
    std::array<float, 16> world = kn5IdentityMatrix();
    std::vector<float> positions; // xyz triples, mesh-local
    std::vector<float> normals;   // xyz triples, mesh-local
    std::vector<float> uvs;       // uv pairs, mesh-local
    std::vector<std::uint32_t> indices; // widened from the file's u16
};

struct Kn5Node {
    Kn5NodeClass node_class = Kn5NodeClass::base;
    std::string name;
    std::uint8_t active = 1;
    std::array<float, 16> transform = kn5IdentityMatrix(); // local, Base only
    bool has_mesh = false;
    Kn5Mesh mesh; // valid when node_class == mesh
    std::vector<Kn5Node> children;
};

struct Kn5File {
    std::int32_t version = 0;
    std::vector<Kn5Texture> textures;   // active records only
    std::vector<Kn5Material> materials;
    std::vector<Kn5Node> nodes;         // exactly one root, per the format
};

struct Kn5ParseResult {
    Kn5File file;
    std::string error; // empty on success, "offset N: ..." on failure

    bool ok() const { return error.empty(); }
};

// Knobs for parseKn5()/parseKn5File(). Defaults reproduce the historical
// behaviour (texture payloads skipped), so existing callers are unaffected.
struct Kn5ParseOptions {
    // Retain each texture's payload bytes in Kn5Texture::data. Needed by
    // tooling that extracts textures (Kn5Baker); off by default because the
    // payloads are a large share of a .kn5 and the runtime never reads them.
    bool keep_texture_data = false;
};

// Parses an in-memory KN5 image (binary-safe).
Kn5ParseResult parseKn5(std::string_view bytes,
                        const Kn5ParseOptions& options = {});

// Reads the whole file into memory, then parses it.
Kn5ParseResult parseKn5File(const std::string& path,
                            const Kn5ParseOptions& options = {});

} // namespace ks::engine::fileformat
