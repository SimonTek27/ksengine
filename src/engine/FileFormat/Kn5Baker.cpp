#include "Kn5Baker.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace ks::engine::fileformat {

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

void bakeMesh(const Kn5Mesh& mesh, const std::string& output_dir,
              std::ofstream& manifest, std::unordered_set<std::string>& used_names,
              Kn5BakeResult& result) {
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
}

void visitNodes(const std::vector<Kn5Node>& nodes, const std::string& output_dir,
                std::ofstream& manifest, std::unordered_set<std::string>& used_names,
                Kn5BakeResult& result) {
    for (const Kn5Node& node : nodes) {
        if (!result.success) return; // a mesh bake failed; stop early
        if (node.node_class == Kn5NodeClass::skinned_mesh) {
            ++result.meshes_skipped;
        } else if (node.has_mesh) {
            bakeMesh(node.mesh, output_dir, manifest, used_names, result);
        }
        visitNodes(node.children, output_dir, manifest, used_names, result);
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

    std::unordered_set<std::string> used_names;
    visitNodes(kn5.nodes, output_dir, manifest, used_names, result);
    manifest.flush();
    if (!manifest.good() && result.success) {
        result.success = false;
        result.error = "failed writing manifest.txt";
    }
    if (result.success) writeTextures(kn5, output_dir, result);
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
