#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace ks {

// ============================================================================
// .ks3d Binary File Format
//
// Layout:
//   [FileHeader]              32 bytes
//   [StringTable]             variable
//   [MaterialSection]         variable
//   [MeshSection]             variable
//   [TextureDataSection]      variable
//   [NodeSection]             variable
//
// All multi-byte values are little-endian.
// ============================================================================

namespace ks3d {

constexpr uint32_t MAGIC = 0x33534B4B;  // "KS3\0" little-endian
constexpr uint32_t VERSION = 1;

enum class VertexFlags : uint32_t {
    None       = 0,
    Position   = 1 << 0,
    Normal     = 1 << 1,
    UV0        = 1 << 2,
    UV1        = 1 << 3,
    Tangent    = 1 << 4,
    Bitangent  = 1 << 5,
    BoneWeight = 1 << 6,
};

enum class TextureFormat : uint32_t {
    None = 0,
    PNG  = 1,
    JPG  = 2,
    TGA  = 3,
    BMP  = 4,
    DDS  = 5,
    KTX  = 6,
};

// Packed vertex layout (always stored interleaved)
struct Vertex {
    float px, py, pz;        // position
    float nx, ny, nz;        // normal
    float u0, v0;            // texture coordinate layer 0
    float tx, ty, tz;        // tangent
    float btx, bty, btz;     // bitangent
    float boneWeights[4];    // skeletal animation weights
    uint32_t boneIndices;    // packed bone indices (4 x uint8)
};

// File header (32 bytes)
struct FileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t stringTableOffset;
    uint32_t stringTableSize;
    uint32_t materialCount;
    uint32_t materialOffset;
    uint32_t meshCount;
    uint32_t meshOffset;
    uint32_t textureCount;
    uint32_t textureOffset;
    uint32_t nodeCount;
    uint32_t nodeOffset;
    uint32_t reserved[2];
};

// String table entry (stored as length-prefixed UTF-8)
struct StringEntry {
    uint32_t length;
    // followed by `length` bytes of UTF-8 data (no null terminator)
};

// Material definition
struct MaterialData {
    float baseColor[3];           // RGB
    float metallic;
    float roughness;
    float opacity;
    float emissive[3];            // RGB
    float clearcoat;
    float clearcoatRoughness;
    float sheen;
    float sheenColor[3];
    float transmission;
    float ior;
    float subsurface;
    uint32_t twoSided;            // 0 = one-sided, 1 = two-sided
    uint32_t transparent;         // 0 = opaque, 1 = transparent
    // string indices (into string table)
    uint32_t nameIndex;
    uint32_t baseColorTexIndex;   // texture index, 0xFFFFFFFF = none
    uint32_t normalTexIndex;
    uint32_t roughnessTexIndex;
    uint32_t metallicTexIndex;
    uint32_t emissiveTexIndex;
    uint32_t opacityTexIndex;
    uint32_t aoTexIndex;
    uint32_t heightTexIndex;
};

// Submesh definition
struct SubmeshData {
    uint32_t materialIndex;
    uint32_t indexOffset;
    uint32_t indexCount;
};

// Mesh definition
struct MeshData {
    uint32_t nameIndex;           // string table index
    uint32_t vertexFlags;         // VertexFlags bitmask
    uint32_t vertexCount;
    uint32_t indexCount;          // always triangle list
    uint32_t submeshCount;
    // followed by vertex data, index data, submesh data
};

// Texture definition
struct TextureData {
    uint32_t nameIndex;           // string table index
    uint32_t format;              // TextureFormat
    uint32_t width;
    uint32_t height;
    uint32_t dataSize;            // byte size of embedded image
    // followed by dataSize bytes
};

// Scene node definition
struct NodeData {
    int32_t parentIndex;          // -1 = root
    uint32_t nameIndex;           // string table index
    float position[3];
    float rotationQuat[4];        // x, y, z, w
    float scale[3];
    uint32_t meshIndex;           // 0xFFFFFFFF = no mesh
    uint32_t materialIndex;       // 0xFFFFFFFF = no material
    uint32_t visible;             // 0 = hidden, 1 = visible
    uint32_t childCount;
    // followed by childCount child indices (uint32_t each)
};

} // namespace ks3d

// ============================================================================
// In-memory representation (used by reader/writer)
// ============================================================================

struct KS3DMaterial {
    std::string name;
    float baseColor[3] = {0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float opacity = 1.0f;
    float emissive[3] = {0.0f, 0.0f, 0.0f};
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.03f;
    float sheen = 0.0f;
    float sheenColor[3] = {1.0f, 1.0f, 1.0f};
    float transmission = 0.0f;
    float ior = 1.5f;
    float subsurface = 0.0f;
    bool twoSided = false;
    bool transparent = false;
    int32_t baseColorTex = -1;    // index into textures, -1 = none
    int32_t normalTex = -1;
    int32_t roughnessTex = -1;
    int32_t metallicTex = -1;
    int32_t emissiveTex = -1;
    int32_t opacityTex = -1;
    int32_t aoTex = -1;
    int32_t heightTex = -1;
};

struct KS3DSubmesh {
    uint32_t materialIndex = 0;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
};

struct KS3DMesh {
    std::string name;
    uint32_t vertexFlags = 0;
    std::vector<float> vertices;    // interleaved floats per ks3d::Vertex layout
    std::vector<uint32_t> indices;
    std::vector<KS3DSubmesh> submeshes;
};

struct KS3DTexture {
    std::string name;
    ks3d::TextureFormat format = ks3d::TextureFormat::None;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> data;
};

struct KS3DNode {
    std::string name;
    int32_t parentIndex = -1;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotationQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};
    int32_t meshIndex = -1;
    int32_t materialIndex = -1;
    bool visible = true;
    std::vector<uint32_t> children;
};

struct KS3DScene {
    std::vector<KS3DMaterial> materials;
    std::vector<KS3DMesh> meshes;
    std::vector<KS3DTexture> textures;
    std::vector<KS3DNode> nodes;
    // Optional JSON metadata blob (curves, f-curves, modifier/boolean stacks,
    // ICE systems, ...). Offset/size recorded in FileHeader::reserved[0/1].
    std::string auxJson;
};

} // namespace ks
