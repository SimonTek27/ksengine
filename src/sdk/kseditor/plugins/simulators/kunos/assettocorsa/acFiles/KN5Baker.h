#pragma once

// Offline KN5 -> NativeMesh (.nmsh) baker.
//
// This is the Qt-based half of the "bake, don't parse live" pipeline
// documented in src/simulator/NativeRenderer.h: SimulatorApp (Qt-free) never
// links KN5Parser or touches QString; instead, this editor-side tool reads a
// .kn5 through the existing, unmodified KN5Parser and writes out a simple
// binary format (NativeRenderer::loadMeshFromFile()'s "NMSH" format) that the
// native runtime can load with nothing but <fstream>.
//
// Deliberately does not modify KN5Parser.h/.cpp/KN5Types.h — those are
// shared with the live editor content-loading path, and changing parsing
// behavior there without a way to compile-test risks breaking real AC
// content loading in the editor. This file only reads what KN5Parser already
// exposes.
//
// Transform handling: KN5Parser::parse() accumulates the KN5 node tree into a
// per-mesh Mesh::worldMatrix (row-vector, including the root Base node), and
// the baker applies that matrix to every vertex — nested node transforms bake
// correctly.
//
// Skinned meshes are skipped (NativeRenderer has no bone/skinning support
// yet); only static geometry is baked.

#include <QString>
#include <string>

namespace ks::tools {

struct BakeResult {
    bool success = false;
    int meshesWritten = 0;
    int meshesSkipped = 0; // skinned, or empty vertex data
    QString error;
};

// Writes one <outputDir>/<sanitized mesh name>.nmsh per static mesh in the
// .kn5, plus <outputDir>/manifest.txt (one mesh name per line, matching the
// names NativeRenderer::loadMeshFromFile() should be called with).
BakeResult bakeKN5ToNativeMeshes(const QString& kn5Path, const std::string& outputDir);

} // namespace ks::tools
