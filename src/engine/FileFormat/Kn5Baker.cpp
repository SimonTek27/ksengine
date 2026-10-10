#include "Kn5Baker.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <vector>

namespace ks::engine::fileformat {

// Brief P1 — default roughness for a KN5 material that authors neither a
// ksRoughness property nor a roughness map. Carbon fibre reads rougher
// than paint; every other surface (paint, plastic, unknown shaders) starts
// at the paint value. Matched case-insensitively against the material name
// AND the shader, because real AC carbon materials carry "carbon" in one of
// the two. Metalness has no counterpart: without an authored value paint,
// carbon and bare metal alike default to 0 (metalness is 1 only when a
// property or map says so). Exposed for kn5_test.
float heuristicMaterialRoughness(const std::string& materialName,
                                 const std::string& shaderName) {
    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string name = lower(materialName);
    const std::string shader = lower(shaderName);
    if (name.find("carbon") != std::string::npos ||
        shader.find("carbon") != std::string::npos) {
        return 0.5f;
    }
    return 0.35f;
}

namespace {

namespace fs = std::filesystem;

// Mirrors ks::sim::NativeVertex (src/simulator/NativeRenderer.h) exactly:
// 3+3+2+4 floats, 48 bytes. NativeRenderer.h lives outside ksengine, so the
// layout is duplicated here — keep field order in sync with it.
struct BakedVertex {
    float px = 0, py = 0, pz = 0;
    float nx = 0, ny = 0, nz = 0;
    float u = 0, v = 0;
    float r = 1, g = 1, b = 1, a = 1;
};

static_assert(sizeof(BakedVertex) == 12 * sizeof(float),
              "BakedVertex must match NativeVertex's 12-float layout");

std::string sanitizeFileName(const std::string& name) {
    std::string out = name;
    for (char& c : out) {
        switch (c) {
        case '\\': case '/': case ':': case '*': case '?':
        case '"': case '<': case '>': case '|':
            c = '_';
            break;
        default:
            break;
        }
    }
    if (out.empty()) out = "unnamed";
    return out;
}

// p' = p * M (row-vector convention): translation in m[12..14]; w = 1 for
// points, 0 for directions (normals ride the rotation/scale only).
void transformByMatrix(const std::array<float, 16>& m, float x, float y,
                       float z, float w, float out[3]) {
    out[0] = x * m[0] + y * m[4] + z * m[8] + w * m[12];
    out[1] = x * m[1] + y * m[5] + z * m[9] + w * m[13];
    out[2] = x * m[2] + y * m[6] + z * m[10] + w * m[14];
}

bool writeNMSH(const std::string& path, const std::vector<BakedVertex>& verts,
               const std::vector<std::uint32_t>& indices, float lod_in,
               float lod_out) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    // "NMS2" = "NMSH" + the KN5 authored distance window right after the
    // counts (Roadmap 2.4). The runtime loader still accepts plain NMSH
    // (editor bakes, terrain caches) and defaults those to "no window".
    file.write("NMS2", 4);
    const auto v_count = static_cast<std::uint32_t>(verts.size());
    const auto i_count = static_cast<std::uint32_t>(indices.size());
    file.write(reinterpret_cast<const char*>(&v_count), sizeof(v_count));
    file.write(reinterpret_cast<const char*>(&i_count), sizeof(i_count));
    file.write(reinterpret_cast<const char*>(&lod_in), sizeof(lod_in));
    file.write(reinterpret_cast<const char*>(&lod_out), sizeof(lod_out));
    file.write(reinterpret_cast<const char*>(verts.data()),
               static_cast<std::streamsize>(sizeof(BakedVertex) * verts.size()));
    file.write(reinterpret_cast<const char*>(indices.data()),
               static_cast<std::streamsize>(sizeof(std::uint32_t) * indices.size()));
    return file.good();
}

// Picks a manifest name that hasn't been used yet in this bake: duplicate
// mesh names exist in real content, and two meshes writing the same .nmsh
// would silently drop one.
std::string uniqueName(const std::string& base, std::unordered_set<std::string>& used) {
    if (used.insert(base).second) return base;
    for (int suffix = 2;; ++suffix) {
        const std::string candidate = base + "_" + std::to_string(suffix);
        if (used.insert(candidate).second) return candidate;
    }
}

static bool nameIsNormalMap(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("normal") != std::string::npos
        || lower.find("nmap") != std::string::npos
        || lower.find("bump") != std::string::npos;
}

static bool nameIsAlbedoMap(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("albedo") != std::string::npos
        || lower.find("basecolor") != std::string::npos
        || lower.find("diffuse") != std::string::npos
        || lower.find("color") != std::string::npos;
}

// Brief P1 — the two new texture-slot heuristics, same shape as the
// normal/albedo ones above: the mapping *name* decides the slot ("ksRough",
// "roughness_map", "TX_METAL" ...). Case-insensitive substring on purpose —
// AC content spells these a dozen ways and a wrong guess only means the
// scalar fallback stays in effect.
static bool nameIsRoughnessMap(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("rough") != std::string::npos;
}

static bool nameIsMetalnessMap(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("metal") != std::string::npos;
}

// Everything one KN5 material says about its PBR surface (brief P1).
// Scalars come from ksRoughness/ksMetalness properties (40-byte blobs,
// first float), maps from the texture mappings — a map wins over its
// scalar at emission time, the materials.txt cell can hold only one.
struct MaterialPbr {
    float roughness = 0.0f;
    float metalness = 0.0f;
    bool hasRoughness = false; // authored property present
    bool hasMetalness = false;
    std::string roughTex; // authored roughness map ("" = none)
    std::string metalTex;
    std::string normalTex;
    std::string albedoTex;
};

static MaterialPbr extractPbr(const Kn5Material& material) {
    MaterialPbr pbr;

    // Heuristic: look through property names for known PBR tags. The 40-byte
    // value encodes the float in its first 4 bytes (same convention the
    // pre-P1 code used).
    for (const auto& prop : material.properties) {
        const std::string& name = prop.name;
        if (name.find("Roughness") != std::string::npos || name.find("roughness") != std::string::npos) {
            if (prop.value.size() >= 4) {
                float v = *reinterpret_cast<const float*>(prop.value.data());
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                pbr.roughness = v;
                pbr.hasRoughness = true;
            }
        } else if (name.find("Metalness") != std::string::npos || name.find("metalness") != std::string::npos) {
            if (prop.value.size() >= 4) {
                float v = *reinterpret_cast<const float*>(prop.value.data());
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                pbr.metalness = v;
                pbr.hasMetalness = true;
            }
        }
    }

    // Texture mappings: first match per slot wins (an empty guard keeps the
    // first, exactly like the old per-category loops with break).
    for (const auto& mapping : material.mappings) {
        if (pbr.normalTex.empty() && nameIsNormalMap(mapping.name)) {
            pbr.normalTex = mapping.texture;
        }
        if (pbr.roughTex.empty() && nameIsRoughnessMap(mapping.name)) {
            pbr.roughTex = mapping.texture;
        }
        if (pbr.metalTex.empty() && nameIsMetalnessMap(mapping.name)) {
            pbr.metalTex = mapping.texture;
        }
        if (pbr.albedoTex.empty() && nameIsAlbedoMap(mapping.name)) {
            pbr.albedoTex = mapping.texture;
        }
    }
    return pbr;
}

// One materials.txt row, assembled while visiting meshes (brief P1: the
// rough/metal cells carry a texture name when a map is authored — see
// writeMaterialsTxt; brief P3 appends clearcoat as an optional 6th cell).
struct MaterialRow {
    std::string mesh; // sanitized, unique manifest name
    std::string albedo;
    std::string normal;
    std::string roughTex; // non-empty = emit the name in the rough cell
    std::string metalTex; // non-empty = emit the name in the metal cell
    float roughness = 0.35f;
    float metalness = 0.0f;
    // Brief P3 — clear-coat flag: 1 for glossy dielectric paint-look rows
    // (the material that gets the sharp coat lobe), 0 otherwise. The cell
    // is only appended when non-zero, so non-coated rows stay byte-equal
    // to the legacy 5-cell format.
    float clearcoat = 0.0f;
};

// -------------------------------------------------------------------
// Extract PBR properties from the mesh's material and collect them.
// -------------------------------------------------------------------

void bakeMesh(const Kn5Mesh& mesh, const std::string& output_dir,
              std::ofstream& manifest, std::unordered_set<std::string>& used_names,
              Kn5BakeResult& result,
              const std::vector<Kn5Material>* kn5_materials,
              std::vector<MaterialRow>& rows) {
    if (mesh.positions.empty()) {
        ++result.meshes_skipped;
        return;
    }
    const auto vertex_count = mesh.positions.size() / 3;
    for (const std::uint32_t index : mesh.indices) {
        if (index >= vertex_count) {
            result.success = false;
            result.error = "mesh '" + mesh.name + "': index " +
                           std::to_string(index) + " out of range";
            return;
        }
    }

    std::vector<BakedVertex> verts;
    verts.reserve(vertex_count);
    for (std::size_t i = 0; i < vertex_count; ++i) {
        BakedVertex baked;
        transformByMatrix(mesh.world, mesh.positions[i * 3],
                          mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2],
                          1.0f, &baked.px);

        float nx = 0.0f, ny = 1.0f, nz = 0.0f;
        if (i * 3 + 2 < mesh.normals.size()) {
            nx = mesh.normals[i * 3];
            ny = mesh.normals[i * 3 + 1];
            nz = mesh.normals[i * 3 + 2];
        }
        float tn[3];
        transformByMatrix(mesh.world, nx, ny, nz, 0.0f, tn);
        const float len = std::sqrt(tn[0] * tn[0] + tn[1] * tn[1] + tn[2] * tn[2]);
        if (len > 1e-12f) { // degenerate world normal falls back to +Y
            baked.nx = tn[0] / len;
            baked.ny = tn[1] / len;
            baked.nz = tn[2] / len;
        } else {
            baked.ny = 1.0f;
        }

        if (i * 2 + 1 < mesh.uvs.size()) {
            baked.u = mesh.uvs[i * 2];
            baked.v = mesh.uvs[i * 2 + 1];
        }
        // KN5 has no per-vertex colour; white lets the renderer's material
        // (later) or plain lighting pass through untinted.
        verts.push_back(baked);
    }

    const std::string safe_name = uniqueName(sanitizeFileName(mesh.name), used_names);
    if (!writeNMSH(output_dir + "/" + safe_name + ".nmsh", verts, mesh.indices,
                   mesh.lod_in, mesh.lod_out)) {
        ++result.meshes_skipped;
        return;
    }
    manifest << safe_name << '\n';
    ++result.meshes_written;

    // -------------------------------------------------------------------
    // Extract PBR properties from the mesh's material and collect them.
    // Brief P1: maps win over scalars (the materials.txt cell holds only
    // one), and absent scalars fall back to the paint/carbon heuristic.
    // -------------------------------------------------------------------
    if (mesh.material_id >= 0 && static_cast<std::size_t>(mesh.material_id) <
        (kn5_materials ? kn5_materials->size() : 0)) {
        const auto& mat = (*kn5_materials)[mesh.material_id];
        const MaterialPbr pbr = extractPbr(mat);

        MaterialRow row;
        row.mesh = safe_name;
        row.albedo = pbr.albedoTex;
        row.normal = pbr.normalTex;
        row.roughTex = pbr.roughTex;
        row.metalTex = pbr.metalTex;
        row.roughness = pbr.hasRoughness
                            ? pbr.roughness
                            : heuristicMaterialRoughness(mat.name, mat.shader);
        row.metalness = pbr.hasMetalness ? pbr.metalness : 0.0f;
        // Brief P3 — clear-coat for glossy dielectric paint: non-metal and
        // paint-look roughness (<= 0.4, i.e. the paint heuristic 0.35 or an
        // authored smooth value; carbon 0.5 stays matte). For a row whose
        // rough cell holds a MAP the stored scalar is the identity 1.0, so
        // decide from the value the shader would see without the map —
        // authored scalar or the name-based heuristic — because real paint
        // often ships with a roughness map and must still get its coat.
        const float visualRough = pbr.hasRoughness
                                      ? pbr.roughness
                                      : heuristicMaterialRoughness(mat.name, mat.shader);
        row.clearcoat = (row.metalness < 0.5f && visualRough <= 0.4f) ? 1.0f : 0.0f;
        rows.push_back(std::move(row));
    } else {
        // No material assigned; brief P1 heuristic defaults (paint-like).
        MaterialRow row;
        row.mesh = safe_name;
        row.roughness = heuristicMaterialRoughness("", "");
        row.metalness = 0.0f;
        row.clearcoat = row.roughness <= 0.4f ? 1.0f : 0.0f;
        rows.push_back(std::move(row));
    }
}

void visitNodes(const std::vector<Kn5Node>& nodes, const std::string& output_dir,
                std::ofstream& manifest, std::unordered_set<std::string>& used_names,
                Kn5BakeResult& result,
                const std::vector<Kn5Material>* kn5_materials,
                std::vector<MaterialRow>& rows) {
    for (const Kn5Node& node : nodes) {
        if (!result.success) return; // a mesh bake failed; stop early
        if (node.node_class == Kn5NodeClass::skinned_mesh) {
            ++result.meshes_skipped;
        } else if (node.has_mesh) {
            bakeMesh(node.mesh, output_dir, manifest, used_names, result,
                     kn5_materials, rows);
        }
        visitNodes(node.children, output_dir, manifest, used_names, result,
                   kn5_materials, rows);
    }
}

// Dumps every retained texture payload verbatim under <output_dir>/textures.
// The bytes are copied as-is — no decompression — so an extracted file can be
// handed straight to any DDS viewer or re-imported into the KS editor.
void writeTextures(const Kn5File& kn5, const std::string& output_dir,
                   Kn5BakeResult& result) {
    if (kn5.textures.empty()) return;

    const std::string texture_dir = output_dir + "/textures";
    std::error_code ec;
    fs::create_directories(texture_dir, ec);
    if (ec && !fs::is_directory(texture_dir)) {
        result.success = false;
        result.error = "could not create texture directory: " + texture_dir +
                       " (" + ec.message() + ")";
        return;
    }

    for (const Kn5Texture& texture : kn5.textures) {
        if (texture.data.empty()) {
            ++result.textures_skipped; // payload never retained by the parser
            continue;
        }
        std::ofstream file(texture_dir + "/" + sanitizeFileName(texture.name),
                           std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            ++result.textures_skipped;
            continue;
        }
        file.write(reinterpret_cast<const char*>(texture.data.data()),
                   static_cast<std::streamsize>(texture.data.size()));
        if (!file.good()) {
            ++result.textures_skipped;
            continue;
        }
        ++result.textures_written;
    }
}

// -------------------------------------------------------------------
// Write a materials.txt summarising per-mesh PBR properties extracted
// from the parsed KN5 materials.  Each line has the format (tab-separated,
// five cells — unchanged since Roadmap 2.3):
//   mesh_name \t albedo_tex_name \t roughness \t metalness \t normal_tex_name
// Brief P1: the roughness/metalness cells are dual-typed — an authored map
// goes in as its texture name (first match per slot wins, maps beat the
// scalar properties), otherwise the scalar. MaterialCache reads both forms
// fail-open, so old bakes keep parsing unchanged.
// Values are the baked texture names (as stored in Kn5Material.mappings) or
// heuristic defaults (paint/carbon, see heuristicMaterialRoughness) when the
// mesh has no explicit material definition.
// The file is optional — the runtime can fall back to the per-mesh defaults
// stored in the NMS2 header (lodIn/lodOut only, no PBR).
// -------------------------------------------------------------------
static void writeMaterialsTxt(const std::string& output_dir,
                              const std::vector<MaterialRow>& rows) {
    std::string txt_path = output_dir + "/materials.txt";
    std::ofstream file(txt_path);
    if (!file.is_open()) return;
    for (const MaterialRow& row : rows) {
        file << row.mesh << '\t' << row.albedo << '\t';
        if (!row.roughTex.empty()) file << row.roughTex;
        else file << row.roughness;
        file << '\t';
        if (!row.metalTex.empty()) file << row.metalTex;
        else file << row.metalness;
        file << '\t' << row.normal;
        // Brief P3 — append the clear-coat cell only for coated rows, so
        // every other row stays byte-identical to the legacy 5-cell format
        // (an old parser rejects rows wider than 5 fields; only painted,
        // coated meshes actually need the new reader).
        if (row.clearcoat > 0.0f) file << '\t' << row.clearcoat;
        file << '\n';
    }
    file.close();
}

} // namespace

Kn5BakeResult bakeKn5(const Kn5File& kn5, const std::string& output_dir) {
    Kn5BakeResult result;
    result.success = true;

    std::error_code ec;
    fs::create_directories(output_dir, ec);
    if (ec && !fs::is_directory(output_dir)) {
        result.success = false;
        result.error = "could not create output directory: " + output_dir +
                       " (" + ec.message() + ")";
        return result;
    }

    std::ofstream manifest(output_dir + "/manifest.txt", std::ios::trunc);
    if (!manifest.is_open()) {
        result.success = false;
        result.error = "could not open manifest.txt for writing in " + output_dir;
        return result;
    }

    // Per-mesh PBR rows for materials.txt (brief P1: dual-typed cells)
    std::vector<MaterialRow> rows;

    // Pass KN5 materials through the bake chain so extractPbr can read them
    const std::vector<Kn5Material>* kn5_materials = &kn5.materials;

    std::unordered_set<std::string> used_names;
    visitNodes(kn5.nodes, output_dir, manifest, used_names, result,
               kn5_materials, rows);
    manifest.flush();
    if (!manifest.good() && result.success) {
        result.success = false;
        result.error = "failed writing manifest.txt";
    }
    if (result.success) writeTextures(kn5, output_dir, result);
    if (result.success) writeMaterialsTxt(output_dir, rows);
    return result;
}

Kn5BakeResult bakeKn5ToNativeMeshes(const std::string& kn5_path,
                                    const std::string& output_dir) {
    // Retention is on here (and only here): the baker is the one consumer
    // that needs the payload bytes.
    Kn5ParseResult parsed =
        parseKn5File(kn5_path, Kn5ParseOptions{/*keep_texture_data=*/true});
    if (!parsed.ok()) {
        Kn5BakeResult result;
        result.error = parsed.error;
        return result;
    }
    return bakeKn5(parsed.file, output_dir);
}

} // namespace ks::engine::fileformat
