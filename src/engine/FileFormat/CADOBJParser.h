#pragma once
#include "KsExport.h"

// Qt-free Wavefront OBJ / MTL importer.
//
// The Qt copy in src/sdk/kseditor/engine/FileFormat/CADOBJParser.cpp is the
// reference for the supported subset, but it only stored the raw corner index
// triples and never filled OBJMesh::vertices/normals/texCoords at all, handled
// faces of at most four corners (real OBJ content is full of n-gons), and went
// through std::stoi, which throws on the first malformed token. This reader
// keeps the same command set and produces a directly usable mesh instead:
//
//   - `v` / `vn` / `vt` accumulate in file order (OBJ indices are global and
//     cumulative, negative indices count back from the end of the list *at
//     that point in the file*),
//   - `f` accepts any corner count and is fan-triangulated; the `v`, `vt` and
//     `vn` triple is the weld key, so a vertex shared by several faces exists
//     once in the output,
//   - `o` / `g` start a new mesh, `usemtl` tags the current one, `mtllib`
//     pulls in the material library relative to the OBJ's own directory,
//   - `s`, `l`, `p` and comments are skipped (smooth groups do not survive a
//     triangle soup, and OBJ lines/points have no triangle representation).
//
// Index bounds are checked and every failure is reported through error()
// instead of throwing, so a corrupt model degrades to a false return rather
// than a crash in a loader thread.
//
// Verified against real content: content/cars/vsf1_ferrari_f300/template/
// f300.obj (Blender export, 23279 vertices / 19608 quad faces / 11 objects,
// plus its f300.mtl).

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "FileFormat/ModelScene.h"
#include "Math/MathCore.h"

namespace ks::engine::fileformat {

// The shared scene types are format-neutral; these aliases keep the OBJ names
// readable at OBJ call sites.
using ObjMaterial = ModelMaterial;
using ObjMesh = ModelMesh;
using ObjScene = ModelScene;

class KSENGINE_API CADOBJParser {
public:
    CADOBJParser() = default;

    // Returns true only when at least one triangle was produced and no
    // face-level problem was hit. error() is empty on a clean parse.
    bool loadFromFile(const std::string& path);
    bool loadFromString(const std::string& content, const std::string& basePath = {});

    // Also called internally for `mtllib`; returns false when the file cannot
    // be opened. A missing library is not fatal for the OBJ itself.
    bool loadMTL(const std::string& path);

    const ObjScene& scene() const { return m_scene; }
    const std::string& error() const { return m_error; }

private:
    bool parse(const std::string& content, const std::string& basePath);

    ObjScene m_scene;
    std::string m_error;

    std::vector<Vec3> m_positions;
    std::vector<Vec3> m_normals;
    std::vector<Vec2> m_texCoords;

    // Weld table: OBJ index triple -> mesh-local index. Reset per mesh.
    std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::uint32_t> m_weld;
    ObjMesh* m_current = nullptr;
    std::size_t m_faceProblems = 0;
};

} // namespace ks::engine::fileformat
