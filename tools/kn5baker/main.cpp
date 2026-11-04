// kn5baker — Qt-free KN5 -> .nmsh offline baker.
//
//   kn5baker <input.kn5> <output_directory>
//       Parses and writes one <name>.nmsh per static mesh plus manifest.txt,
//       ready for NativeRenderer::loadMeshesFromManifest(), and copies every
//       embedded texture payload verbatim to <output_directory>/textures/
//       (usually a set of ready-to-open .dds files).
//
//   kn5baker --validate <input.kn5>
//       Parses only (no output) and prints per-file stats — used to smoke the
//       reader against the whole AC content corpus.
//
//   kn5baker --only <name_substring> [--recentre] [--skip <name_substring>] \
//            <input.kn5> <output_directory>
//       Bakes a subset: ONE prop pulled out of a whole track into the shared
//       asset folder every track then instances, or a track bake with that prop
//       left out so the geometry is not duplicated.
//
//       --only       keep the meshes whose name contains the needle, and only
//                    the textures they map (the rest of the track's texture set
//                    stays behind).
//       --skip       drop the meshes whose name contains the needle; used when
//                    baking a track whose props live in the shared folder.
//       --recentre   move the bake's origin to the middle of its footprint with
//                    the feet on y = 0, so the asset can be placed at any
//                    coordinate instead of only where the source track had it.
//
// Built on the real "sc6969" format reader (Kn5Reader), so it works on
// actual AC content and applies the KN5 node-tree transforms.

#include "FileFormat/DdsReader.h"
#include "FileFormat/Kn5Baker.h"
#include "FileFormat/Kn5Reader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using ks::engine::fileformat::Kn5BakeResult;
using ks::engine::fileformat::Kn5Node;
using ks::engine::fileformat::Kn5NodeClass;
using ks::engine::fileformat::Kn5ParseResult;

int usage() {
    std::fprintf(stderr,
                 "usage: kn5baker <input.kn5> <output_directory>\n"
                 "       kn5baker --validate <input.kn5>\n"
                 "       kn5baker [--only <name_substring>]... [--skip <name_substring>]\n"
                 "                [--recentre] <input.kn5> <output_directory>\n"
                 "       (--only repeats: several parts of one assembly bake together)\n");
    return 2;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Collects every mesh node whose name contains any of `needles` (case-insensitive),
// once each: one walk, so a node that matches two needles is still baked once.
// Several needles are how a part that only makes sense with its companions is
// pulled out together — a helmet with its HANS, say, which must keep the two
// meshes' positions relative to each other.
// Transforms are already accumulated into Kn5Mesh::world by the reader, so the
// flat copy stays where the asset put it.
void collectMatchingAny(const std::vector<Kn5Node>& nodes,
                        const std::vector<std::string>& needles,
                        std::vector<Kn5Node>& out) {
    for (const Kn5Node& node : nodes) {
        if (node.has_mesh) {
            const std::string name = lower(node.name);
            for (const std::string& needle : needles) {
                if (name.find(needle) == std::string::npos) continue;
                out.push_back(node);
                break;
            }
        }
        collectMatchingAny(node.children, needles, out);
    }
}

// Drops every mesh node whose name contains `needle`. Children stay: a prop
// node may carry sub-parts under it, and dropping the branch wholesale would
// take anything the track still needs.
void removeMatching(std::vector<Kn5Node>& nodes, const std::string& needle) {
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [&needle](const Kn5Node& node) {
                                   return node.has_mesh &&
                                          lower(node.name).find(needle) != std::string::npos;
                               }),
                nodes.end());
    for (Kn5Node& node : nodes) removeMatching(node.children, needle);
}

// Vertex bounds of everything baked, in world space: Kn5Baker applies each
// mesh's accumulated matrix when it writes, so the local positions alone are
// not where the asset ends up.
void boundsOf(const std::vector<Kn5Node>& nodes, float lo[3], float hi[3], bool& any) {
    for (const Kn5Node& node : nodes) {
        const auto& positions = node.mesh.positions;
        if (node.has_mesh && !positions.empty()) {
            const auto& w = node.mesh.world;
            for (std::size_t i = 0; i + 2 < positions.size(); i += 3) {
                const float x = positions[i];
                const float y = positions[i + 1];
                const float z = positions[i + 2];
                const float world[3] = {
                    x * w[0] + y * w[4] + z * w[8] + w[12],
                    x * w[1] + y * w[5] + z * w[9] + w[13],
                    x * w[2] + y * w[6] + z * w[10] + w[14],
                };
                for (int axis = 0; axis < 3; ++axis) {
                    if (!any) {
                        lo[axis] = hi[axis] = world[axis];
                    } else {
                        lo[axis] = std::min(lo[axis], world[axis]);
                        hi[axis] = std::max(hi[axis], world[axis]);
                    }
                }
                any = true;
            }
        }
        boundsOf(node.children, lo, hi, any);
    }
}

bool invert3x3(const float m[3][3], float out[3][3]) {
    const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                      m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                      m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (std::fabs(det) < 1e-12f) return false;
    const float inv = 1.f / det;
    out[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * inv;
    out[0][1] = -(m[0][1] * m[2][2] - m[0][2] * m[2][1]) * inv;
    out[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv;
    out[1][0] = -(m[1][0] * m[2][2] - m[1][2] * m[2][0]) * inv;
    out[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv;
    out[1][2] = -(m[0][0] * m[1][2] - m[0][2] * m[1][0]) * inv;
    out[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * inv;
    out[2][1] = -(m[0][0] * m[2][1] - m[0][1] * m[2][0]) * inv;
    out[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv;
    return true;
}

// Shifts the bake by `delta` world metres. The positions are local to each
// mesh and its accumulated matrix is applied when the mesh is written, so the
// world shift is carried into each mesh's own space first — adding it to the
// local numbers would move a rotated prop somewhere else entirely.
void offsetNodes(std::vector<Kn5Node>& nodes, const float delta[3]) {
    for (Kn5Node& node : nodes) {
        if (node.has_mesh && !node.mesh.positions.empty()) {
            const auto& w = node.mesh.world;
            const float basis[3][3] = {{w[0], w[1], w[2]},
                                       {w[4], w[5], w[6]},
                                       {w[8], w[9], w[10]}};
            float inverse[3][3];
            float local[3] = {0.f, 0.f, 0.f};
            if (invert3x3(basis, inverse)) {
                // row-vector convention: local = world * basis^-1
                for (int column = 0; column < 3; ++column)
                    local[column] = delta[0] * inverse[0][column] +
                                    delta[1] * inverse[1][column] +
                                    delta[2] * inverse[2][column];
            }
            auto& positions = node.mesh.positions;
            for (std::size_t i = 0; i + 2 < positions.size(); i += 3) {
                positions[i] -= local[0];
                positions[i + 1] -= local[1];
                positions[i + 2] -= local[2];
            }
        }
        offsetNodes(node.children, delta);
    }
}

// Origin in the middle of the footprint, feet on the ground: what a prop needs
// to be placeable at a coordinate instead of only where its source track had it.
void recentreOnFootprint(std::vector<Kn5Node>& nodes) {
    float lo[3] = {0.f, 0.f, 0.f};
    float hi[3] = {0.f, 0.f, 0.f};
    bool any = false;
    boundsOf(nodes, lo, hi, any);
    if (!any) return;
    const float delta[3] = {(lo[0] + hi[0]) * 0.5f, lo[1], (lo[2] + hi[2]) * 0.5f};
    offsetNodes(nodes, delta);
}

// Keeps only the texture payloads the remaining meshes actually map, so a prop
// pulled out of a track does not carry the track's whole texture set.
void keepUsedTextures(ks::engine::fileformat::Kn5File& file) {
    std::unordered_set<std::string> used;
    auto collect = [&](auto&& self, const std::vector<Kn5Node>& nodes) -> void {
        for (const Kn5Node& node : nodes) {
            if (node.has_mesh) {
                const int id = node.mesh.material_id;
                if (id >= 0 && static_cast<std::size_t>(id) < file.materials.size()) {
                    for (const auto& mapping : file.materials[static_cast<std::size_t>(id)].mappings)
                        used.insert(mapping.texture);
                }
            }
            self(self, node.children);
        }
    };
    collect(collect, file.nodes);

    std::vector<ks::engine::fileformat::Kn5Texture> textures;
    for (auto& texture : file.textures)
        if (used.count(texture.name)) textures.push_back(std::move(texture));
    file.textures = std::move(textures);
}

void countNodes(const std::vector<Kn5Node>& nodes, int& static_meshes,
                int& skinned_meshes, std::size_t& vertices) {
    for (const Kn5Node& node : nodes) {
        if (node.node_class == Kn5NodeClass::skinned_mesh) {
            ++skinned_meshes;
        } else if (node.has_mesh) {
            ++static_meshes;
            vertices += node.mesh.positions.size() / 3;
        }
        countNodes(node.children, static_meshes, skinned_meshes, vertices);
    }
}

int validate(const std::string& input) {
    // Texture payloads are read back for the stats line below, so the whole
    // image (including every .dds) lands in memory once here — acceptable for
    // a tool, not for the runtime, which parses with retention off.
    const Kn5ParseResult parsed = ks::engine::fileformat::parseKn5File(
        input, ks::engine::fileformat::Kn5ParseOptions{/*keep_texture_data=*/true});
    if (!parsed.ok()) {
        std::fprintf(stderr, "kn5baker: %s: %s\n", input.c_str(),
                     parsed.error.c_str());
        return 1;
    }

    int static_meshes = 0;
    int skinned_meshes = 0;
    std::size_t vertices = 0;
    countNodes(parsed.file.nodes, static_meshes, skinned_meshes, vertices);

    std::size_t texture_bytes = 0;
    int dds_textures = 0;
    for (const auto& texture : parsed.file.textures) {
        texture_bytes += texture.data.size();
        if (ks::engine::fileformat::isDds(std::string_view(
                reinterpret_cast<const char*>(texture.data.data()),
                texture.data.size()))) {
            ++dds_textures;
        }
    }

    std::printf("%s: ok v%d, %zu texture(s) (%zu bytes, %d .dds), "
                "%zu material(s), %d static mesh(es), %d skinned, %zu vertices\n",
                input.c_str(), parsed.file.version, parsed.file.textures.size(),
                texture_bytes, dds_textures, parsed.file.materials.size(),
                static_meshes, skinned_meshes, vertices);
    return 0;
}

int report(const std::string& input, const std::string& output_dir,
           const Kn5BakeResult& result) {
    if (!result.ok()) {
        std::fprintf(stderr, "kn5baker: %s: %s\n", input.c_str(),
                     result.error.c_str());
        return 1;
    }
    std::printf("kn5baker: %s -> %s: %d mesh(es) written, %d skipped; "
                "%d texture(s) extracted to textures/, %d skipped\n",
                input.c_str(), output_dir.c_str(), result.meshes_written,
                result.meshes_skipped, result.textures_written,
                result.textures_skipped);
    return 0;
}

int bake(const std::string& input, const std::string& output_dir,
         const std::vector<std::string>& only, const std::string& skip, bool recentre) {
    if (only.empty() && skip.empty() && !recentre)
        return report(input, output_dir,
                      ks::engine::fileformat::bakeKn5ToNativeMeshes(input, output_dir));

    Kn5ParseResult parsed = ks::engine::fileformat::parseKn5File(
        input, ks::engine::fileformat::Kn5ParseOptions{/*keep_texture_data=*/true});
    if (!parsed.ok()) {
        std::fprintf(stderr, "kn5baker: %s: %s\n", input.c_str(),
                     parsed.error.c_str());
        return 1;
    }

    if (!only.empty()) {
        std::vector<std::string> needles;
        std::string wanted;
        for (const std::string& needle : only) {
            needles.push_back(lower(needle));
            if (!wanted.empty()) wanted += ", ";
            wanted += needle;
        }
        std::vector<Kn5Node> kept;
        collectMatchingAny(parsed.file.nodes, needles, kept);
        if (kept.empty()) {
            std::fprintf(stderr, "kn5baker: %s: no mesh name contains '%s'\n",
                         input.c_str(), wanted.c_str());
            return 1;
        }
        parsed.file.nodes = std::move(kept);
        std::printf("kn5baker: %zu mesh(es) matched '%s'\n", parsed.file.nodes.size(),
                    wanted.c_str());
    }
    if (!skip.empty()) {
        int before = 0, skinned = 0;
        std::size_t vertices = 0;
        countNodes(parsed.file.nodes, before, skinned, vertices);
        removeMatching(parsed.file.nodes, lower(skip));
        int after = 0;
        vertices = 0;
        countNodes(parsed.file.nodes, after, skinned, vertices);
        std::printf("kn5baker: dropped %d mesh(es) matching '%s'\n", before - after,
                    skip.c_str());
        if (after == 0) {
            std::fprintf(stderr, "kn5baker: %s: '%s' left nothing to bake\n", input.c_str(),
                         skip.c_str());
            return 1;
        }
    }
    if (recentre) {
        recentreOnFootprint(parsed.file.nodes);
        std::printf("kn5baker: origin moved to the footprint centre, feet on y = 0\n");
    }
    if (!only.empty() || !skip.empty()) keepUsedTextures(parsed.file);

    return report(input, output_dir, ks::engine::fileformat::bakeKn5(parsed.file, output_dir));
}

} // namespace

int main(int argc, char** argv) {
    int next = 1;
    bool validate_only = false;
    bool recentre = false;
    std::vector<std::string> only;
    std::string skip;
    while (next < argc) {
        const std::string_view arg = argv[next];
        if (arg == "--validate") {
            validate_only = true;
            ++next;
        } else if (arg == "--only" && next + 1 < argc) {
            only.emplace_back(argv[next + 1]);
            next += 2;
        } else if (arg == "--skip" && next + 1 < argc) {
            skip = argv[next + 1];
            next += 2;
        } else if (arg == "--recentre") {
            recentre = true;
            ++next;
        } else {
            break;
        }
    }

    if (validate_only) {
        if (argc - next != 1) return usage();
        return validate(argv[next]);
    }
    if (argc - next != 2) return usage();
    return bake(argv[next], argv[next + 1], only, skip, recentre);
}
