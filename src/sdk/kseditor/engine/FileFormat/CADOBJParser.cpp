#include "CADOBJParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace ks {

bool CADOBJParser::loadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;

    m_scene = CADOBJScene();
    m_scene.sourcePath = path;
    m_positions.clear();
    m_normals.clear();
    m_texCoords.clear();
    m_currentMesh = nullptr;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            Vec3 v;
            iss >> v.x >> v.y >> v.z;
            m_positions.push_back(v);
        } else if (prefix == "vn") {
            Vec3 n;
            iss >> n.x >> n.y >> n.z;
            m_normals.push_back(n);
        } else if (prefix == "vt") {
            Vec2 t;
            iss >> t.x >> t.y;
            m_texCoords.push_back(t);
        } else if (prefix == "f") {
            if (!m_currentMesh) {
                m_scene.meshes.emplace_back();
                m_currentMesh = &m_scene.meshes.back();
                m_currentMesh->name = "default";
            }

            std::string v1, v2, v3, v4;
            iss >> v1 >> v2 >> v3 >> v4;

            auto parseVertex = [this](const std::string& token) -> Vec3 {
                Vec3 idx(-1, -1, -1);
                std::istringstream ts(token);
                std::string part;
                int i = 0;
                while (std::getline(ts, part, '/')) {
                    if (!part.empty()) {
                        int val = std::stoi(part);
                        if (val < 0) {
                            if (i == 0) val = static_cast<int>(m_positions.size()) + val;
                            else if (i == 1) val = static_cast<int>(m_texCoords.size()) + val;
                            else if (i == 2) val = static_cast<int>(m_normals.size()) + val;
                        } else {
                            val -= 1;
                        }
                        if (i == 0) idx.x = static_cast<float>(val);
                        else if (i == 1) idx.y = static_cast<float>(val);
                        else if (i == 2) idx.z = static_cast<float>(val);
                    }
                    i++;
                }
                return idx;
            };

            Vec3 vi1 = parseVertex(v1);
            Vec3 vi2 = parseVertex(v2);
            Vec3 vi3 = parseVertex(v3);

            m_currentMesh->indices.push_back(vi1);
            m_currentMesh->indices.push_back(vi2);
            m_currentMesh->indices.push_back(vi3);

            if (!v4.empty()) {
                Vec3 vi4 = parseVertex(v4);
                m_currentMesh->indices.push_back(vi1);
                m_currentMesh->indices.push_back(vi3);
                m_currentMesh->indices.push_back(vi4);
            }
        } else if (prefix == "o" || prefix == "g") {
            std::string name;
            std::getline(iss, name);
            name.erase(0, name.find_first_not_of(" \t"));
            if (!name.empty()) {
                m_scene.meshes.emplace_back();
                m_currentMesh = &m_scene.meshes.back();
                m_currentMesh->name = name;
            }
        } else if (prefix == "usemtl") {
            std::string matName;
            iss >> matName;
            if (m_currentMesh) {
                m_currentMesh->materialName = matName;
            }
        } else if (prefix == "mtllib") {
            std::string mtlFile;
            iss >> mtlFile;

            size_t lastSlash = path.find_last_of("/\\");
            std::string basePath = (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "";
            loadMTL(basePath + mtlFile);
        }
    }

    return true;
}

bool CADOBJParser::loadFromString(const std::string& content)
{
    m_scene = CADOBJScene();
    m_positions.clear();
    m_normals.clear();
    m_texCoords.clear();
    m_currentMesh = nullptr;

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            Vec3 v;
            iss >> v.x >> v.y >> v.z;
            m_positions.push_back(v);
        } else if (prefix == "vn") {
            Vec3 n;
            iss >> n.x >> n.y >> n.z;
            m_normals.push_back(n);
        } else if (prefix == "vt") {
            Vec2 t;
            iss >> t.x >> t.y;
            m_texCoords.push_back(t);
        } else if (prefix == "f") {
            if (!m_currentMesh) {
                m_scene.meshes.emplace_back();
                m_currentMesh = &m_scene.meshes.back();
                m_currentMesh->name = "default";
            }

            std::string v1, v2, v3, v4;
            iss >> v1 >> v2 >> v3 >> v4;

            auto parseVertex = [this](const std::string& token) -> Vec3 {
                Vec3 idx(-1, -1, -1);
                std::istringstream ts(token);
                std::string part;
                int i = 0;
                while (std::getline(ts, part, '/')) {
                    if (!part.empty()) {
                        int val = std::stoi(part);
                        if (val < 0) {
                            if (i == 0) val = static_cast<int>(m_positions.size()) + val;
                            else if (i == 1) val = static_cast<int>(m_texCoords.size()) + val;
                            else if (i == 2) val = static_cast<int>(m_normals.size()) + val;
                        } else {
                            val -= 1;
                        }
                        if (i == 0) idx.x = static_cast<float>(val);
                        else if (i == 1) idx.y = static_cast<float>(val);
                        else if (i == 2) idx.z = static_cast<float>(val);
                    }
                    i++;
                }
                return idx;
            };

            Vec3 vi1 = parseVertex(v1);
            Vec3 vi2 = parseVertex(v2);
            Vec3 vi3 = parseVertex(v3);

            m_currentMesh->indices.push_back(vi1);
            m_currentMesh->indices.push_back(vi2);
            m_currentMesh->indices.push_back(vi3);

            if (!v4.empty()) {
                Vec3 vi4 = parseVertex(v4);
                m_currentMesh->indices.push_back(vi1);
                m_currentMesh->indices.push_back(vi3);
                m_currentMesh->indices.push_back(vi4);
            }
        } else if (prefix == "o" || prefix == "g") {
            std::string name;
            std::getline(iss, name);
            name.erase(0, name.find_first_not_of(" \t"));
            if (!name.empty()) {
                m_scene.meshes.emplace_back();
                m_currentMesh = &m_scene.meshes.back();
                m_currentMesh->name = name;
            }
        } else if (prefix == "usemtl") {
            std::string matName;
            iss >> matName;
            if (m_currentMesh) m_currentMesh->materialName = matName;
        }
    }

    return true;
}

bool CADOBJParser::loadMTL(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;

    OBJMaterial* currentMat = nullptr;
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "newmtl") {
            std::string name;
            iss >> name;
            OBJMaterial mat;
            mat.name = name;
            m_scene.materials[name] = mat;
            currentMat = &m_scene.materials[name];
        } else if (currentMat) {
            if (prefix == "Ka") iss >> currentMat->Ka.x >> currentMat->Ka.y >> currentMat->Ka.z;
            else if (prefix == "Kd") iss >> currentMat->Kd.x >> currentMat->Kd.y >> currentMat->Kd.z;
            else if (prefix == "Ks") iss >> currentMat->Ks.x >> currentMat->Ks.y >> currentMat->Ks.z;
            else if (prefix == "Ns") iss >> currentMat->Ns;
            else if (prefix == "d" || prefix == "Tr") iss >> currentMat->d;
            else if (prefix == "illum") iss >> currentMat->illum;
            else if (prefix == "map_Kd") {
                std::string texPath;
                std::getline(iss, texPath);
                texPath.erase(0, texPath.find_first_not_of(" \t"));
                currentMat->mapKd = texPath;
            } else if (prefix == "map_Ks") {
                std::string texPath;
                std::getline(iss, texPath);
                texPath.erase(0, texPath.find_first_not_of(" \t"));
                currentMat->mapKs = texPath;
            } else if (prefix == "map_Bump" || prefix == "bump") {
                std::string texPath;
                std::getline(iss, texPath);
                texPath.erase(0, texPath.find_first_not_of(" \t"));
                currentMat->mapBump = texPath;
            }
        }
    }

    return true;
}

} // namespace ks
