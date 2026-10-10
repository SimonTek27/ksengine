#pragma once
#include "KsExport.h"

// Qt-free binary FBX importer.
//
// Format facts this reader relies on, all verified against real AC content
// (asr_1992_dallara_192/animations/coverspring.fbx @7400,
// pontiac_transam_87/unpacked/model.fbx @7200, rss_formula_americas_2020_
// oval/animations/steer.FBX):
//
//   - 27-byte header: the 23-byte magic `Kaydara FBX Binary  \0\x1A\0`
//     followed by a u32 version (7100..7400 in this content set).
//   - A node is u32 endOffset + u32 numProperties + u32 propertyListLen,
//     then a u8 name length and that many name bytes. endOffset is an
//     absolute file offset covering the node, its children and their
//     terminating null record. A null record is the same 12 header bytes
//     plus the u8 name length, i.e. 13 zero bytes.
//   - Property type codes: Y i16, C u8, I i32, F f32, D f64, L i64, S string
//     (u32 length), R raw blob (u32 length), and f/d/l/i/b arrays. An array
//     header is **u32 elementCount, u32 encoding, u32 compressedLength**
//     — 12 bytes, encoding 0 = payload verbatim, 1 = one zlib stream — and
//     the decoded payload must be exactly elementCount * sizeof(element).
//   - Files stop ~168 bytes short of EOF: the footer after the last null
//     record is not part of the node walk.
//
// Geometry lives under `Objects` as `Geometry` objects with `Vertices`
// (doubles, xyz triples), `PolygonVertexIndex` (i32, every polygon except
// possibly the last ends at the first index whose sign bit is set, the
// vertex being ~index), and per-layer `LayerElementNormal` /
// `LayerElementUV` nodes carrying a MappingInformationType,
// ReferenceInformationType and either the values themselves (Direct) or an
// index array into them (IndexToDirect). Meshes and materials are wired
// together through OO records under `Connections`.
//
// Extraction mirrors the OBJ importer: corners are welded on the
// (vertex, normal, uv) triple and polygons are fan-triangulated, so both
// formats land in the same ModelScene.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "FileFormat/ModelScene.h"

namespace ks::engine::fileformat {

struct FbxProperty {
    char type = 0;
    std::int64_t integerValue = 0;   // Y C I L (never set for F/D)
    double number = 0.0;             // Y C I F D L
    std::string text;                // S
    std::vector<std::uint8_t> bytes; // R, and the payload of a raw array
    std::vector<double> doubles;     // decoded 'd' and 'f' arrays
    std::vector<std::int64_t> ints;  // decoded 'i', 'l' and 'b' arrays
    std::size_t arrayCount = 0;      // element count, 0 when not an array
};

struct FbxNode {
    std::string name;
    std::vector<FbxProperty> properties;
    std::vector<FbxNode> children;

    const FbxNode* child(std::string_view childName) const {
        for (const FbxNode& n : children) {
            if (n.name == childName) return &n;
        }
        return nullptr;
    }

    // Value of the i-th property when it is a string.
    std::string stringAt(std::size_t i) const {
        if (i >= properties.size() || properties[i].type != 'S') return {};
        return properties[i].text;
    }

    // Value of a single-string child, e.g. MappingInformationType.
    std::string childString(std::string_view childName) const {
        const FbxNode* n = child(childName);
        return n == nullptr ? std::string() : n->stringAt(0);
    }
};

class KSENGINE_API FbxReader {
public:
    bool loadFromFile(const std::string& path);
    bool load(std::string_view data, const std::string& sourcePath = {});

    const ModelScene& scene() const { return m_scene; }
    // The raw node tree, kept for diagnostics and for callers that need more
    // than geometry (animation stacks, cameras, ...).
    const std::vector<FbxNode>& roots() const { return m_roots; }

    const std::string& error() const { return m_error; }
    std::uint32_t version() const { return m_version; }
    std::size_t objectCount() const { return m_objectCount; }

private:
    enum class NodeStatus { Ok, Null, Error };

    // Bounds-checked cursor over the file buffer, defined in the .cpp.
    struct Cursor;

    bool fail(const char* message);
    NodeStatus parseNode(Cursor& r, FbxNode& out, int depth);
    bool parseProperty(Cursor& r, FbxProperty& out);
    bool readArray(Cursor& r, char type, FbxProperty& out);

    void buildScene();
    bool extractGeometry(const FbxNode& geometry, ModelMesh& mesh);

    ModelScene m_scene;
    std::vector<FbxNode> m_roots;
    std::string m_error;
    std::uint32_t m_version = 0;
    std::size_t m_objectCount = 0;
};

} // namespace ks::engine::fileformat
