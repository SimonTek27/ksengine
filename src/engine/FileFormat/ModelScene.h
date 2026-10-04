#pragma once

// Shared triangle-sink scene produced by every model importer in this
// directory (Wavefront OBJ, FBX). Both formats weld their own index spaces
// into parallel position/normal/texcoord arrays and hand back a triangle
// list, so downstream code only has to know one shape.
//
// Layout contract: a mesh is a soup of welded corners. `vertices`, `normals`
// and `texCoords` are parallel and `indices` addresses all three. Flags say
// whether the source actually carried normals / texcoords; when it did not,
// the placeholder slots are zero-filled so the arrays still line up.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Math/MathCore.h"

namespace ks::engine::fileformat {

struct ModelMaterial {
    std::string name;
    Vec3 ka{0.0f, 0.0f, 0.0f};
    Vec3 kd{0.8f, 0.8f, 0.8f};
    Vec3 ks{0.0f, 0.0f, 0.0f};
    float ns = 0.0f;
    float d = 1.0f;
    int illum = 2;
    std::string mapKd;
    std::string mapKs;
    std::string mapBump;
};

struct ModelMesh {
    std::string name;
    std::string materialName;

    // Welded, so all three arrays are parallel and `indices` indexes them
    // directly.
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;
    std::vector<std::uint32_t> indices; // triangle list, 3 per triangle

    bool hasNormals = false;
    bool hasTexCoords = false;

    std::size_t triangleCount() const { return indices.size() / 3; }
};

struct ModelScene {
    std::vector<ModelMesh> meshes;
    std::map<std::string, ModelMaterial> materials;
    std::string sourcePath;

    // Global (pre-weld) element counts — useful for sanity checks and for
    // detecting a file that parsed but produced nothing.
    std::size_t positionCount = 0;
    std::size_t texCoordCount = 0;
    std::size_t normalCount = 0;
    std::size_t faceCount = 0;
    std::size_t triangleCount = 0;

    const ModelMesh* findMesh(std::string_view name) const {
        for (const ModelMesh& mesh : meshes) {
            if (mesh.name == name) return &mesh;
        }
        return nullptr;
    }

    const ModelMaterial* findMaterial(std::string_view name) const {
        const auto it = materials.find(std::string(name));
        return it == materials.end() ? nullptr : &it->second;
    }
};

} // namespace ks::engine::fileformat
