#pragma once

#include <string>
#include <vector>
#include <map>
#include "Math/MathCore.h"

namespace ks {

struct OBJMaterial {
    std::string name;
    Vec3 Ka = {0, 0, 0};
    Vec3 Kd = {0.8f, 0.8f, 0.8f};
    Vec3 Ks = {0, 0, 0};
    float Ns = 0;
    float d = 1.0f;
    int illum = 2;
    std::string mapKd;
    std::string mapKs;
    std::string mapBump;
};

struct OBJMesh {
    std::string name;
    std::string materialName;
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;
    std::vector<Vec3> indices;
};

struct CADOBJScene {
    std::vector<OBJMesh> meshes;
    std::map<std::string, OBJMaterial> materials;
    std::string sourcePath;
};

class CADOBJParser {
public:
    CADOBJParser() = default;

    bool loadFromFile(const std::string& path);
    bool loadFromString(const std::string& content);
    bool loadMTL(const std::string& path);

    const CADOBJScene& scene() const { return m_scene; }

private:
    CADOBJScene m_scene;
    std::vector<Vec3> m_positions;
    std::vector<Vec3> m_normals;
    std::vector<Vec2> m_texCoords;
    OBJMesh* m_currentMesh = nullptr;
};

} // namespace ks
