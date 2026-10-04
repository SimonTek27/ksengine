#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include "Math/MathCore.h"

namespace ks {

struct GLBBuffer {
    std::vector<uint8_t> data;
};

struct GLBBufferView {
    uint32_t buffer = 0;
    uint32_t byteOffset = 0;
    uint32_t byteLength = 0;
    uint32_t target = 0;
};

struct GLBAccessor {
    uint32_t bufferView = 0;
    uint32_t byteOffset = 0;
    uint32_t componentType = 0;
    uint32_t count = 0;
    std::string type;
};

struct GLBPrimitive {
    uint32_t indices = 0xFFFFFFFF;
    uint32_t material = 0xFFFFFFFF;
    std::map<std::string, uint32_t> attributes;
};

struct GLBMesh {
    std::string name;
    std::vector<GLBPrimitive> primitives;
};

struct GLBMaterial {
    std::string name;
    Vec3 baseColorFactor = {1, 1, 1};
    float alphaCutoff = 0.5f;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
};

struct GLBScene {
    std::vector<GLBMesh> meshes;
    std::vector<GLBMaterial> materials;
    std::vector<GLBBuffer> buffers;
    std::vector<GLBBufferView> bufferViews;
    std::vector<GLBAccessor> accessors;
    std::string sourcePath;
};

class GLBParser {
public:
    GLBParser() = default;

    bool loadFromFile(const std::string& path);
    const GLBScene& scene() const { return m_scene; }

    std::vector<Vec3> getVertices(uint32_t accessorIndex) const;
    std::vector<Vec3> getNormals(uint32_t accessorIndex) const;
    std::vector<Vec2> getTexCoords(uint32_t accessorIndex) const;
    std::vector<uint32_t> getIndices(uint32_t accessorIndex) const;

private:
    bool parseJSONChunk(const std::vector<uint8_t>& data);
    bool parseBINChunk(const std::vector<uint8_t>& data);

    GLBScene m_scene;
    std::string m_jsonChunk;
};

namespace GLB {

struct File {
    std::string name;
    std::vector<ks::GLBMesh> meshes;
    std::vector<ks::GLBMaterial> materials;
};

class Parser {
public:
    static bool read(const std::string& filePath, File& outFile) {
        ks::GLBParser parser;
        if (!parser.loadFromFile(filePath)) return false;
        const auto& scene = parser.scene();
        outFile.name = filePath;
        outFile.meshes = scene.meshes;
        outFile.materials = scene.materials;
        return true;
    }
    static std::string lastError() { return m_lastError; }

private:
    static std::string m_lastError;
};

} // namespace GLB

} // namespace ks
