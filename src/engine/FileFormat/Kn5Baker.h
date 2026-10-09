#pragma once

// Offline KN5 -> NativeMesh (.nmsh) baker, Qt-free counterpart of the editor
// tool in src/sdk/kseditor/.../KN5Baker. Same output contract the native
// runtime already consumes:
//
//   <output_dir>/manifest.txt      one sanitized mesh name per line
//   <output_dir>/<name>.nmsh       "NMS2" + u32 vCount + u32 iCount +
//                                  f32 lodIn + f32 lodOut (the KN5 authored
//                                  distance window, Roadmap 2.4) +
//                                  vCount * 12 floats (px,py,pz,nx,ny,nz,u,v,
//                                  r,g,b,a) + iCount * u32 indices. Plain
//                                  "NMSH" files (no LOD pair) are the legacy
//                                  layout the runtime still accepts.
//   <output_dir>/textures/<name>   each embedded texture payload, byte for
//                                  byte (normally a complete .dds)
//   <output_dir>/materials.txt     "mesh \t albedo \t roughness \t
//                                  metalness \t normal" per mesh. Brief P1:
//                                  the roughness/metalness cells hold a
//                                  texture name instead of a number when
//                                  the KN5 material authors a map — both
//                                  forms parse fail-open in MaterialCache.
//
// matching NativeRenderer::loadMeshFromFile()/loadMeshesFromManifest().
//
// Differences vs the Qt version: transforms come from the real KN5 node tree
// (accumulated per mesh by Kn5Reader), so meshes that live under nested Base
// nodes land in the right place — the Qt parser only exposed one file-level
// matrix. Skinned meshes and empty meshes are skipped (no skinning support
// in the native renderer yet).
//
// Texture extraction needs Kn5ParseOptions::keep_texture_data, otherwise the
// payloads were never retained and `textures_skipped` reports every one.
// bakeKn5ToNativeMeshes() turns it on for you.

#include "Kn5Reader.h"

#include <string>

namespace ks::engine::fileformat {

struct Kn5BakeResult {
    bool success = false;
    int meshes_written = 0;
    int meshes_skipped = 0; // skinned or empty
    int textures_written = 0;
    int textures_skipped = 0; // payload absent (not retained) or unwritable
    std::string error;        // empty on success

    bool ok() const { return success; }
};

// Bakes an already-parsed KN5 image into output_dir.
Kn5BakeResult bakeKn5(const Kn5File& kn5, const std::string& output_dir);

// Convenience: parse from disk (keeping texture payloads), then bake.
Kn5BakeResult bakeKn5ToNativeMeshes(const std::string& kn5_path,
                                    const std::string& output_dir);

// Brief P1 — default roughness for a KN5 material with no authored
// ksRoughness property and no roughness map: carbon fibre (name or shader
// contains "carbon", case-insensitive) reads 0.5, paint and everything
// else 0.35. Metalness has no heuristic — it stays 0 unless authored.
// Declared for kn5_test; bakeKn5() applies it internally.
float heuristicMaterialRoughness(const std::string& materialName,
                                 const std::string& shaderName);

} // namespace ks::engine::fileformat
