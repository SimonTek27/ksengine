// KN5 reader/baker test: round-trips a synthetic KN5 built to the real
// format (verified against actools + ~2770 AC content files) through
// parse + bake, checks node-tree transform accumulation, NMSH bytes,
// manifest contents, corruption rejection, and (when present on this
// machine) smokes a real AC file.

#include "engine/FileFormat/DdsReader.h"
#include "engine/FileFormat/Kn5Baker.h"
#include "engine/FileFormat/Kn5Reader.h"
#include "KsTest.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using ks::engine::fileformat::Kn5File;
using ks::engine::fileformat::Kn5Node;
using ks::engine::fileformat::Kn5NodeClass;
using ks::engine::fileformat::Kn5ParseResult;
using ks::engine::fileformat::kKn5Magic;

// ---------------------------------------------------------------------------
// Synthetic KN5 writer (mirrors the layout in Kn5Reader.h).
// ---------------------------------------------------------------------------

void putU8(std::vector<char>& out, std::uint8_t v) {
    out.push_back(static_cast<char>(v));
}

void putU16(std::vector<char>& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void putU32(std::vector<char>& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

void putI32(std::vector<char>& out, std::int32_t v) {
    putU32(out, static_cast<std::uint32_t>(v));
}

void putF32(std::vector<char>& out, float v) {
    static_assert(sizeof(float) == 4, "KN5 assumes 4-byte float");
    char bytes[4];
    std::memcpy(bytes, &v, sizeof(bytes));
    out.insert(out.end(), bytes, bytes + sizeof(bytes));
}

void putStr(std::vector<char>& out, const std::string& s) {
    putI32(out, static_cast<std::int32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

// Row-major translation matrix, row-vector convention (translation [12..14]).
void putTranslation(std::vector<char>& out, float tx, float ty, float tz) {
    const float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        tx, ty, tz, 1,
    };
    const char* bytes = reinterpret_cast<const char*>(m);
    out.insert(out.end(), bytes, bytes + sizeof(m));
}

struct TestTriangle {
    float pos[3][3];
    float normal[3][3];
    float uv[3][2];
    std::uint16_t index[3];
};

const TestTriangle kTriangle = {
    {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}},
    {0, 1, 2},
};

void putMeshNode(std::vector<char>& out, const std::string& name,
                 std::int32_t material_id, const TestTriangle* triangle) {
    putI32(out, 2); // nodeClass = Mesh
    putStr(out, name);
    putI32(out, 0); // children
    putU8(out, 1);  // active
    putU8(out, 1);  // castShadows
    putU8(out, 1);  // isVisible
    putU8(out, 0);  // isTransparent

    const std::uint32_t vertex_count = triangle ? 3 : 0;
    putU32(out, vertex_count);
    if (triangle != nullptr) {
        for (int i = 0; i < 3; ++i) {
            putF32(out, triangle->pos[i][0]);
            putF32(out, triangle->pos[i][1]);
            putF32(out, triangle->pos[i][2]);
            putF32(out, triangle->normal[i][0]);
            putF32(out, triangle->normal[i][1]);
            putF32(out, triangle->normal[i][2]);
            putF32(out, triangle->uv[i][0]);
            putF32(out, triangle->uv[i][1]);
            putF32(out, 0.0f); // tangent x
            putF32(out, 0.0f); // tangent y
            putF32(out, 0.0f); // tangent z
        }
    }

    const std::uint32_t index_count = triangle ? 3 : 0;
    putU32(out, index_count);
    if (triangle != nullptr) {
        for (int i = 0; i < 3; ++i) putU16(out, triangle->index[i]);
    }

    putI32(out, material_id);
    putU32(out, 0);        // layer
    putF32(out, 0.0f);     // lodIn
    putF32(out, 1000.0f);  // lodOut
    putF32(out, 0.0f);     // bsCenter.x
    putF32(out, 0.0f);     // bsCenter.y
    putF32(out, 0.0f);     // bsCenter.z
    putF32(out, 1.0f);     // bsRadius
    putU8(out, 1);         // isRenderable
}

void putSkinnedNode(std::vector<char>& out, const std::string& name) {
    putI32(out, 3); // nodeClass = SkinnedMesh
    putStr(out, name);
    putI32(out, 0);
    putU8(out, 1);
    putU8(out, 1);
    putU8(out, 1);
    putU8(out, 0);

    putU32(out, 1); // one bone
    putStr(out, "bone");
    putTranslation(out, 0.0f, 0.0f, 0.0f); // 64-byte bone matrix

    putU32(out, 1); // one 76-byte vertex = 19 floats
    for (int i = 0; i < 19; ++i) putF32(out, static_cast<float>(i));

    putU32(out, 3);
    putU16(out, 0);
    putU16(out, 0);
    putU16(out, 0);

    putI32(out, 0);        // materialId
    putU32(out, 0);        // layer
    putF32(out, 0.0f);     // lodIn
    putF32(out, 1000.0f);  // lodOut
}

// A minimal but valid 4x4 A8R8G8B8 DDS, the shape AC embeds in .kn5 texture
// records. 128 header bytes + 16 opaque-red pixels.
std::vector<std::uint8_t> buildMinimalDds() {
    std::vector<std::uint8_t> out;
    const auto u32 = [&out](std::uint32_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    };
    out.insert(out.end(), {'D', 'D', 'S', ' '});
    u32(124);                                    // header size
    u32(0x1 | 0x2 | 0x4 | 0x8 | 0x1000);         // caps|height|width|pitch|pf
    u32(4);                                      // height
    u32(4);                                      // width
    u32(16);                                     // pitch (4 px * 4 bytes)
    u32(0);                                      // depth
    u32(1);                                      // mip count
    for (int i = 0; i < 11; ++i) u32(0);         // reserved
    u32(32);                                     // pixel format size
    u32(0x40 | 0x1);                             // DDPF_RGB | DDPF_ALPHAPIXELS
    u32(0);                                      // fourcc (unused)
    u32(32);                                     // bits per pixel
    u32(0x00FF0000u);                            // R mask
    u32(0x0000FF00u);                            // G mask
    u32(0x000000FFu);                            // B mask
    u32(0xFF000000u);                            // A mask
    u32(0);
    u32(0);
    u32(0);
    u32(0);
    u32(0); // caps, caps2, caps3, caps4, reserved
    for (int i = 0; i < 16; ++i) {
        out.insert(out.end(), {0x00, 0x00, 0xFF, 0xFF}); // B, G, R, A = red
    }
    return out;
}

// root(Base, T+10x) -> grp(Base, T+5y) -> triangle + wheels(skinned)
//                    -> bad:name(mesh) + empty(mesh) + triangle(duplicate)
std::vector<char> buildKn5(int version) {
    std::vector<char> out;
    out.insert(out.end(), kKn5Magic, kKn5Magic + 6);
    putI32(out, version);
    if (version > 5) putI32(out, 0); // extra header int (v6+)

    // Textures: one flag-only inactive record, one active record carrying a
    // real DDS image (so extraction can be verified byte for byte).
    putI32(out, 2);
    putI32(out, 0); // active == 0: record ends here
    putI32(out, 1);
    putStr(out, "diffuse.dds");
    const std::vector<std::uint8_t> dds = buildMinimalDds();
    putU32(out, static_cast<std::uint32_t>(dds.size()));
    out.insert(out.end(), dds.begin(), dds.end());

    // One material with two properties and two texture mappings (brief P1:
    // ksMetalness exercises the scalar-property cell, ksRoughnessMap the
    // dual-typed texture-name cell). depthMode (42) is only present for
    // version >= 5 — writing a distinctive value proves the reader consumes
    // exactly that field and stays aligned after it.
    putI32(out, 1);
    putStr(out, "body");
    putStr(out, "ksPerPixel");
    putU8(out, 0); // blend
    putU8(out, 0); // alphaTested
    if (version >= 5) putI32(out, 42);
    putI32(out, 2);
    putStr(out, "ksAmbient");
    for (int i = 0; i < 10; ++i) putF32(out, static_cast<float>(i)); // 40 bytes
    putStr(out, "ksMetalness");
    for (int i = 0; i < 10; ++i) {
        putF32(out, i == 0 ? 0.25f : 0.0f); // value lives in the first float
    }
    putI32(out, 2);
    putStr(out, "ksDiffuse");
    putI32(out, 0);
    putStr(out, "diffuse.dds");
    putStr(out, "ksRoughnessMap");
    putI32(out, 1);
    putStr(out, "rough.dds");

    // Root Base node with two children chains plus two direct meshes.
    putI32(out, 1); // Base
    putStr(out, "root");
    putI32(out, 4); // grp, bad:name, empty, triangle (duplicate name)
    putU8(out, 1);
    putTranslation(out, 10.0f, 0.0f, 0.0f);

    putI32(out, 1); // Base "grp"
    putStr(out, "grp");
    putI32(out, 2);
    putU8(out, 1);
    putTranslation(out, 0.0f, 5.0f, 0.0f);

    putMeshNode(out, "triangle", 0, &kTriangle);
    putSkinnedNode(out, "wheels");

    putMeshNode(out, "bad:name", 0, &kTriangle);
    putMeshNode(out, "empty", 0, nullptr);
    putMeshNode(out, "triangle", 1, &kTriangle);
    return out;
}

const Kn5Node* findNode(const std::vector<Kn5Node>& nodes, const std::string& name) {
    for (const Kn5Node& node : nodes) {
        if (node.name == name) return &node;
        if (const Kn5Node* child = findNode(node.children, name)) return child;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// NMSH read-back
// ---------------------------------------------------------------------------

struct NmshData {
    bool valid = false;
    bool v2 = false; // NMS2 = NMSH + authored LOD window (Roadmap 2.4)
    std::uint32_t vcount = 0;
    std::uint32_t icount = 0;
    float lod_in = 0.0f;
    float lod_out = 0.0f;
    std::vector<float> verts; // vcount * 12 floats
    std::vector<std::uint32_t> indices;
};

NmshData readNmsh(const fs::path& path) {
    NmshData data;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return data;

    char magic[4];
    file.read(magic, sizeof(magic));
    data.v2 = std::memcmp(magic, "NMS2", 4) == 0;
    if (!data.v2 && std::memcmp(magic, "NMSH", 4) != 0) return data;
    file.read(reinterpret_cast<char*>(&data.vcount), 4);
    file.read(reinterpret_cast<char*>(&data.icount), 4);
    if (data.vcount > 1000000 || data.icount > 1000000) return data;
    if (data.v2) {
        file.read(reinterpret_cast<char*>(&data.lod_in), 4);
        file.read(reinterpret_cast<char*>(&data.lod_out), 4);
    }

    data.verts.resize(static_cast<std::size_t>(data.vcount) * 12);
    data.indices.resize(data.icount);
    file.read(reinterpret_cast<char*>(data.verts.data()),
              static_cast<std::streamsize>(data.verts.size() * sizeof(float)));
    file.read(reinterpret_cast<char*>(data.indices.data()),
              static_cast<std::streamsize>(data.indices.size() * sizeof(std::uint32_t)));
    data.valid = static_cast<bool>(file);
    return data;
}

std::vector<std::string> readLines(const fs::path& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

std::vector<std::uint8_t> readAllBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                     std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    const std::vector<std::uint8_t> expected_dds = buildMinimalDds();

    // --- in-memory parse of the synthetic v6 image -------------------------
    const std::vector<char> image = buildKn5(6);
    const std::string bytes(image.begin(), image.end());
    const Kn5ParseResult parsed = ks::engine::fileformat::parseKn5(bytes);
    KS_CHECK(parsed.ok());
    if (parsed.ok()) {
        const Kn5File& kn5 = parsed.file;
        KS_CHECK(kn5.version == 6);

        KS_CHECK(kn5.textures.size() == 1); // inactive record dropped
        if (kn5.textures.size() == 1) {
            KS_CHECK(kn5.textures[0].name == "diffuse.dds");
            KS_CHECK(kn5.textures[0].data_size == expected_dds.size());
            // Retention is opt-in: the size is reported, the bytes are not.
            KS_CHECK(kn5.textures[0].data.empty());
        }

        KS_CHECK(kn5.materials.size() == 1);
        if (kn5.materials.size() == 1) {
            const auto& material = kn5.materials[0];
            KS_CHECK(material.name == "body");
            KS_CHECK(material.shader == "ksPerPixel");
            KS_CHECK(material.depth_mode == 42); // v6 carries depthMode
            KS_CHECK(material.properties.size() == 2);
            if (material.properties.size() == 2) {
                KS_CHECK(material.properties[0].name == "ksAmbient");
                float last_value = 0.0f;
                std::memcpy(&last_value, material.properties[0].value.data() + 36,
                            sizeof(last_value));
                KS_CHECK_NEAR(last_value, 9.0f, 1e-6); // 10th float of the blob
                KS_CHECK(material.properties[1].name == "ksMetalness");
            }
            KS_CHECK(material.mappings.size() == 2);
            if (material.mappings.size() == 2) {
                KS_CHECK(material.mappings[0].name == "ksDiffuse");
                KS_CHECK(material.mappings[0].slot == 0);
                KS_CHECK(material.mappings[0].texture == "diffuse.dds");
                KS_CHECK(material.mappings[1].name == "ksRoughnessMap");
                KS_CHECK(material.mappings[1].texture == "rough.dds");
            }
        }

        KS_CHECK(kn5.nodes.size() == 1); // single-root format
        KS_CHECK(!kn5.nodes.empty() && kn5.nodes[0].name == "root");
        if (!kn5.nodes.empty()) {
            KS_CHECK(kn5.nodes[0].children.size() == 4);

            const Kn5Node* triangle = findNode(kn5.nodes, "triangle");
            KS_CHECK(triangle != nullptr && triangle->has_mesh);
            if (triangle != nullptr && triangle->has_mesh) {
                KS_CHECK(triangle->mesh.name == "triangle");
                // world = T(0,5,0) * T(10,0,0) = T(10,5,0) (row-vector)
                KS_CHECK_NEAR(triangle->mesh.world[12], 10.0f, 1e-6);
                KS_CHECK_NEAR(triangle->mesh.world[13], 5.0f, 1e-6);
                KS_CHECK_NEAR(triangle->mesh.world[14], 0.0f, 1e-6);
                // Roadmap 2.4: the payload tail's authored distance window
                // is kept instead of skipped (builder writes 0 / 1000).
                KS_CHECK_NEAR(triangle->mesh.lod_in, 0.0f, 1e-6);
                KS_CHECK_NEAR(triangle->mesh.lod_out, 1000.0f, 1e-6);
                KS_CHECK(triangle->mesh.positions.size() == 9);
                KS_CHECK(triangle->mesh.uvs.size() == 6);
                KS_CHECK(triangle->mesh.indices.size() == 3);
                if (triangle->mesh.indices.size() == 3) {
                    KS_CHECK(triangle->mesh.indices[0] == 0);
                    KS_CHECK(triangle->mesh.indices[1] == 1);
                    KS_CHECK(triangle->mesh.indices[2] == 2);
                }
            }

            const Kn5Node* direct = findNode(kn5.nodes, "bad:name");
            KS_CHECK(direct != nullptr && direct->has_mesh);
            if (direct != nullptr && direct->has_mesh) {
                KS_CHECK_NEAR(direct->mesh.world[12], 10.0f, 1e-6);
                KS_CHECK_NEAR(direct->mesh.world[13], 0.0f, 1e-6);
            }

            const Kn5Node* skinned = findNode(kn5.nodes, "wheels");
            KS_CHECK(skinned != nullptr);
            if (skinned != nullptr) {
                KS_CHECK(skinned->node_class == Kn5NodeClass::skinned_mesh);
                KS_CHECK(!skinned->has_mesh);
            }

            const Kn5Node* empty = findNode(kn5.nodes, "empty");
            KS_CHECK(empty != nullptr && empty->has_mesh);
            if (empty != nullptr && empty->has_mesh) {
                KS_CHECK(empty->mesh.positions.empty());
            }
        }
    }

    // --- v4: no depthMode field, no extra header int ----------------------
    const std::vector<char> image_v4 = buildKn5(4);
    const Kn5ParseResult parsed_v4 = ks::engine::fileformat::parseKn5(
        std::string(image_v4.begin(), image_v4.end()));
    KS_CHECK(parsed_v4.ok());
    if (parsed_v4.ok()) {
        KS_CHECK(parsed_v4.file.version == 4);
        KS_CHECK(parsed_v4.file.materials.size() == 1);
        if (parsed_v4.file.materials.size() == 1) {
            KS_CHECK(parsed_v4.file.materials[0].depth_mode == 0);
            KS_CHECK(parsed_v4.file.materials[0].properties.size() == 2);
        }
        KS_CHECK(findNode(parsed_v4.file.nodes, "triangle") != nullptr);
    }

    // --- corruption rejection ---------------------------------------------
    {
        std::string bad_magic = bytes;
        bad_magic[0] = 'X';
        KS_CHECK(!ks::engine::fileformat::parseKn5(bad_magic).ok());

        KS_CHECK(!ks::engine::fileformat::parseKn5(
                     bytes.substr(0, bytes.size() / 2)).ok());
        KS_CHECK(!ks::engine::fileformat::parseKn5("").ok());

        std::vector<char> bad_version;
        bad_version.insert(bad_version.end(), kKn5Magic, kKn5Magic + 6);
        putI32(bad_version, 3);
        KS_CHECK(!ks::engine::fileformat::parseKn5(
                     std::string(bad_version.begin(), bad_version.end())).ok());

        std::vector<char> huge_count;
        huge_count.insert(huge_count.end(), kKn5Magic, kKn5Magic + 6);
        putI32(huge_count, 6);
        putI32(huge_count, 0); // extra
        putI32(huge_count, 0x7FFFFFFF);
        KS_CHECK(!ks::engine::fileformat::parseKn5(
                     std::string(huge_count.begin(), huge_count.end())).ok());
    }

    // --- texture payload retention (Kn5ParseOptions) -----------------------
    {
        using ks::engine::fileformat::Kn5ParseOptions;
        const Kn5ParseResult kept =
            ks::engine::fileformat::parseKn5(bytes, Kn5ParseOptions{true});
        KS_CHECK(kept.ok());
        if (kept.ok() && kept.file.textures.size() == 1) {
            const auto& texture = kept.file.textures[0];
            KS_CHECK(texture.data_size == expected_dds.size());
            KS_CHECK(texture.data.size() == expected_dds.size());
            KS_CHECK(texture.data == expected_dds); // verbatim, not re-encoded
            const std::string payload(
                reinterpret_cast<const char*>(texture.data.data()),
                texture.data.size());
            KS_CHECK(ks::engine::fileformat::isDds(payload));
            const auto decoded = ks::engine::fileformat::decodeDds(payload);
            KS_CHECK(decoded.ok());
            if (decoded.ok()) {
                KS_CHECK(decoded.info.width == 4);
                KS_CHECK(decoded.info.height == 4);
                KS_CHECK(decoded.rgba.size() == 64);
                if (decoded.rgba.size() == 64) {
                    KS_CHECK(decoded.rgba[0] == 255); // R
                    KS_CHECK(decoded.rgba[1] == 0);   // G
                    KS_CHECK(decoded.rgba[2] == 0);   // B
                    KS_CHECK(decoded.rgba[3] == 255); // A
                }
            }
        } else {
            KS_CHECK(false); // expected exactly one retained texture
        }

        // The default still skips the bytes — retention must cost nothing
        // for callers that never ask for it.
        const Kn5ParseResult skipped = ks::engine::fileformat::parseKn5(bytes);
        KS_CHECK(skipped.ok());
        if (skipped.ok() && skipped.file.textures.size() == 1) {
            KS_CHECK(skipped.file.textures[0].data.empty());
            KS_CHECK(skipped.file.textures[0].data_size == expected_dds.size());
        }
    }

    // --- brief P1 heuristic defaults (paint vs carbon) ----------------------
    using ks::engine::fileformat::heuristicMaterialRoughness;
    KS_CHECK_NEAR(heuristicMaterialRoughness("body", "ksPerPixel"), 0.35f, 1e-6);
    KS_CHECK_NEAR(heuristicMaterialRoughness("carbon_fiber", "ksPerPixel"), 0.5f, 1e-6);
    KS_CHECK_NEAR(heuristicMaterialRoughness("body", "ksCarbonPaint"), 0.5f, 1e-6);
    KS_CHECK_NEAR(heuristicMaterialRoughness("CARBON", "ksMultilayer"), 0.5f, 1e-6);
    KS_CHECK_NEAR(heuristicMaterialRoughness("", ""), 0.35f, 1e-6);

    // --- bake: parse from disk, check NMSH bytes + manifest ---------------
    const fs::path temp_dir = fs::temp_directory_path() / "ks_qtfree_kn5_test";
    const fs::path kn5_path = temp_dir / "synthetic.kn5";
    const fs::path out_dir = temp_dir / "baked";
    fs::remove_all(temp_dir);
    fs::create_directories(temp_dir);
    {
        std::ofstream out(kn5_path, std::ios::binary);
        out.write(image.data(), static_cast<std::streamsize>(image.size()));
    }

    const auto bake = ks::engine::fileformat::bakeKn5ToNativeMeshes(
        kn5_path.string(), out_dir.string());
    KS_CHECK(bake.ok());
    KS_CHECK(bake.meshes_written == 3); // triangle, bad_name, triangle_2
    KS_CHECK(bake.meshes_skipped == 2); // wheels (skinned), empty
    if (!bake.ok()) std::fprintf(stderr, "bake error: %s\n", bake.error.c_str());

    const std::vector<std::string> manifest = readLines(out_dir / "manifest.txt");
    KS_CHECK(manifest.size() == 3);
    if (manifest.size() == 3) {
        KS_CHECK(manifest[0] == "triangle");
        KS_CHECK(manifest[1] == "bad_name"); // ":" sanitized to "_"
        KS_CHECK(manifest[2] == "triangle_2");
    }

    // --- materials.txt (brief P1 dual-typed cells, P3 clear-coat) -----------
    // triangle/bad_name share material "body": the rough cell carries the
    // authored map name (maps beat the scalar), the metal cell the
    // ksMetalness property float, albedo the ksDiffuse mapping, normal is
    // empty. triangle_2 references material_id 1 (out of range) → the
    // paint heuristic defaults with no textures. Brief P3: both rows are
    // glossy dielectric paint (non-metal, roughness 0.35 ≤ 0.4) so the
    // baker appends the clear-coat cell "1" — the only rows that gain a
    // 6th field are coated ones, non-coated rows stay legacy 5-cell.
    const std::vector<std::string> material_rows =
        readLines(out_dir / "materials.txt");
    KS_CHECK(material_rows.size() == 3);
    if (material_rows.size() == 3) {
        KS_CHECK(material_rows[0] == "triangle\tdiffuse.dds\trough.dds\t0.25\t\t1");
        KS_CHECK(material_rows[1] == "bad_name\tdiffuse.dds\trough.dds\t0.25\t\t1");
        KS_CHECK(material_rows[2] == "triangle_2\t\t0.35\t0\t\t1");
    }

    const NmshData triangle_nmsh = readNmsh(out_dir / "triangle.nmsh");
    KS_CHECK(triangle_nmsh.valid);
    if (triangle_nmsh.valid) {
        KS_CHECK(triangle_nmsh.vcount == 3);
        KS_CHECK(triangle_nmsh.icount == 3);
        // Roadmap 2.4: the bake writes NMS2 and carries the KN5 window
        // (synthetic tail: lodIn=0, lodOut=1000) through to disk.
        KS_CHECK(triangle_nmsh.v2);
        KS_CHECK_NEAR(triangle_nmsh.lod_in, 0.0f, 1e-6);
        KS_CHECK_NEAR(triangle_nmsh.lod_out, 1000.0f, 1e-6);
        KS_CHECK(triangle_nmsh.verts.size() == 36);
        KS_CHECK(triangle_nmsh.indices.size() == 3);
        if (triangle_nmsh.verts.size() == 36) {
            // world = T(10,5,0) applied to (0,0,0) / (1,0,0) / (0,1,0)
            KS_CHECK_NEAR(triangle_nmsh.verts[0], 10.0f, 1e-5);
            KS_CHECK_NEAR(triangle_nmsh.verts[1], 5.0f, 1e-5);
            KS_CHECK_NEAR(triangle_nmsh.verts[2], 0.0f, 1e-5);
            KS_CHECK_NEAR(triangle_nmsh.verts[12], 11.0f, 1e-5);
            KS_CHECK_NEAR(triangle_nmsh.verts[13], 5.0f, 1e-5);
            KS_CHECK_NEAR(triangle_nmsh.verts[24], 10.0f, 1e-5);
            KS_CHECK_NEAR(triangle_nmsh.verts[25], 6.0f, 1e-5);
            // normal (0,0,1) through translation-only world stays (0,0,1)
            KS_CHECK_NEAR(triangle_nmsh.verts[3], 0.0f, 1e-5);
            KS_CHECK_NEAR(triangle_nmsh.verts[4], 0.0f, 1e-5);
            KS_CHECK_NEAR(triangle_nmsh.verts[5], 1.0f, 1e-5);
            // uv passthrough (vertex 1)
            KS_CHECK_NEAR(triangle_nmsh.verts[18], 1.0f, 1e-5);
            KS_CHECK_NEAR(triangle_nmsh.verts[19], 0.0f, 1e-5);
            // default opaque white colour
            KS_CHECK_NEAR(triangle_nmsh.verts[8], 1.0f, 1e-5);
            KS_CHECK_NEAR(triangle_nmsh.verts[11], 1.0f, 1e-5);
        }
        if (triangle_nmsh.indices.size() == 3) {
            KS_CHECK(triangle_nmsh.indices[0] == 0);
            KS_CHECK(triangle_nmsh.indices[1] == 1);
            KS_CHECK(triangle_nmsh.indices[2] == 2);
        }
    }

    KS_CHECK(fs::exists(out_dir / "bad_name.nmsh"));
    KS_CHECK(fs::exists(out_dir / "triangle_2.nmsh"));
    KS_CHECK(!fs::exists(out_dir / "wheels.nmsh"));
    KS_CHECK(!fs::exists(out_dir / "empty.nmsh"));

    // --- embedded texture extraction --------------------------------------
    KS_CHECK(bake.textures_written == 1);
    KS_CHECK(bake.textures_skipped == 0);
    const std::vector<std::uint8_t> extracted =
        readAllBytes(out_dir / "textures" / "diffuse.dds");
    KS_CHECK(extracted == expected_dds); // byte-identical to the payload

    // Baking a parse that never retained the payloads must report them as
    // skipped instead of dropping empty files on disk.
    {
        const Kn5ParseResult bare = ks::engine::fileformat::parseKn5(bytes);
        KS_CHECK(bare.ok());
        const fs::path bare_dir = temp_dir / "baked_no_textures";
        const auto bare_bake =
            ks::engine::fileformat::bakeKn5(bare.file, bare_dir.string());
        KS_CHECK(bare_bake.ok());
        KS_CHECK(bare_bake.meshes_written == 3);
        KS_CHECK(bare_bake.textures_written == 0);
        KS_CHECK(bare_bake.textures_skipped == 1);
        KS_CHECK(!fs::exists(bare_dir / "textures" / "diffuse.dds"));
    }

    // --- real-file smoke test (skipped when AC isn't installed) ------------
    const std::string real_kn5 =
        "F:/SteamLibrary/steamapps/common/assettocorsa/content/cars/abarth500/collider.kn5";
    if (fs::exists(real_kn5)) {
        const Kn5ParseResult real_parsed =
            ks::engine::fileformat::parseKn5File(real_kn5);
        KS_CHECK(real_parsed.ok());
        if (real_parsed.ok()) {
            KS_CHECK(real_parsed.file.version >= 4 &&
                     real_parsed.file.version <= 6);
            KS_CHECK(!real_parsed.file.nodes.empty());
            const auto real_bake = ks::engine::fileformat::bakeKn5ToNativeMeshes(
                real_kn5, (temp_dir / "real_baked").string());
            KS_CHECK(real_bake.ok());
        }
    }

    // A real textured car: every embedded payload must survive a retention
    // parse and decode as a DDS. This is the only place the block decoders
    // meet production content.
    const std::string real_textured =
        "F:/SteamLibrary/steamapps/common/assettocorsa/content/cars/abarth500/abarth500.kn5";
    if (fs::exists(real_textured)) {
        const Kn5ParseResult real_kept = ks::engine::fileformat::parseKn5File(
            real_textured, ks::engine::fileformat::Kn5ParseOptions{true});
        KS_CHECK(real_kept.ok());
        if (real_kept.ok()) {
            KS_CHECK(!real_kept.file.textures.empty());
            int decoded = 0;
            for (const auto& texture : real_kept.file.textures) {
                const std::string payload(
                    reinterpret_cast<const char*>(texture.data.data()),
                    texture.data.size());
                if (!ks::engine::fileformat::isDds(payload)) continue;
                const auto info = ks::engine::fileformat::readDdsInfo(payload);
                if (!info.ok()) {
                    std::fprintf(stderr, "%s: %s\n", texture.name.c_str(),
                                 info.error.c_str());
                }
                KS_CHECK(info.ok());
                KS_CHECK(ks::engine::fileformat::decodeDds(payload).ok());
                ++decoded;
            }
            KS_CHECK(decoded > 0);
        }
    }

    fs::remove_all(temp_dir);
    return KS_TEST_RESULT("kn5_test");
}
