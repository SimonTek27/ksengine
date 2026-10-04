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
// Built on the real "sc6969" format reader (Kn5Reader), so it works on
// actual AC content and applies the KN5 node-tree transforms.

#include "FileFormat/DdsReader.h"
#include "FileFormat/Kn5Baker.h"
#include "FileFormat/Kn5Reader.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace {

using ks::engine::fileformat::Kn5BakeResult;
using ks::engine::fileformat::Kn5Node;
using ks::engine::fileformat::Kn5NodeClass;
using ks::engine::fileformat::Kn5ParseResult;

int usage() {
    std::fprintf(stderr,
                 "usage: kn5baker <input.kn5> <output_directory>\n"
                 "       kn5baker --validate <input.kn5>\n");
    return 2;
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

int bake(const std::string& input, const std::string& output_dir) {
    const Kn5BakeResult result =
        ks::engine::fileformat::bakeKn5ToNativeMeshes(input, output_dir);
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

} // namespace

int main(int argc, char** argv) {
    int next = 1;
    bool validate_only = false;
    if (next < argc && std::string_view(argv[next]) == "--validate") {
        validate_only = true;
        ++next;
    }

    if (validate_only) {
        if (argc - next != 1) return usage();
        return validate(argv[next]);
    }
    if (argc - next != 2) return usage();
    return bake(argv[next], argv[next + 1]);
}
