#include "KS3DReader.h"
#include <fstream>
#include <cstring>

namespace ks {

bool KS3DReader::readFromFile(const std::string& path)
{
    m_lastError.clear();
    m_scene = KS3DScene();
    m_stringEntries.clear();
    m_stringData.clear();

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        m_lastError = "Failed to open file: " + path;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size < static_cast<std::streamsize>(sizeof(ks3d::FileHeader))) {
        m_lastError = "File too small for header";
        return false;
    }

    m_fileData.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(m_fileData.data()), size)) {
        m_lastError = "Failed to read file data";
        return false;
    }

    ks3d::FileHeader header{};
    if (!parseHeader(m_fileData, header)) return false;
    if (!parseStringTable(m_fileData, header.stringTableOffset, header.stringTableSize))
        return false;
    if (!parseMaterials(m_fileData, header.materialOffset, header.materialCount))
        return false;
    if (!parseMeshes(m_fileData, header.meshOffset, header.meshCount))
        return false;
    if (!parseTextures(m_fileData, header.textureOffset, header.textureCount))
        return false;
    if (!parseNodes(m_fileData, header.nodeOffset, header.nodeCount))
        return false;

    // Optional JSON metadata trailer (offset/size in reserved[0/1]).
    m_scene.auxJson.clear();
    if (header.reserved[1] > 0 && header.reserved[0] > 0) {
        size_t auxStart = header.reserved[0];
        size_t auxSize = header.reserved[1];
        if (auxStart + auxSize <= m_fileData.size()) {
            m_scene.auxJson.assign(
                reinterpret_cast<const char*>(m_fileData.data() + auxStart),
                auxSize);
        } else {
            m_lastError = "Corrupt auxiliary metadata section";
            return false;
        }
    }

    return true;
}

bool KS3DReader::parseHeader(const std::vector<uint8_t>& data, ks3d::FileHeader& header)
{
    if (data.size() < sizeof(ks3d::FileHeader)) {
        m_lastError = "File too small for header";
        return false;
    }

    std::memcpy(&header, data.data(), sizeof(ks3d::FileHeader));

    if (header.magic != ks3d::MAGIC) {
        m_lastError = "Invalid magic number (not a .ks3d file)";
        return false;
    }

    if (header.version > ks3d::VERSION) {
        m_lastError = "Unsupported version: " + std::to_string(header.version);
        return false;
    }

    return true;
}

bool KS3DReader::parseStringTable(const std::vector<uint8_t>& data, uint32_t offset, uint32_t size)
{
    if (offset + 4 > data.size()) {
        m_lastError = "String table offset out of bounds";
        return false;
    }

    uint32_t entryCount = 0;
    std::memcpy(&entryCount, data.data() + offset, sizeof(uint32_t));

    size_t pos = offset + 4;
    m_stringEntries.resize(entryCount);

    for (uint32_t i = 0; i < entryCount; i++) {
        if (pos + 8 > data.size()) {
            m_lastError = "String table truncated";
            return false;
        }
        uint32_t strOffset = 0, strLength = 0;
        std::memcpy(&strOffset, data.data() + pos, sizeof(uint32_t));
        std::memcpy(&strLength, data.data() + pos + 4, sizeof(uint32_t));
        m_stringEntries[i] = {strOffset, strLength};
        pos += 8;
    }

    // The string data follows the entries in the string table section
    m_stringData.assign(data.begin() + offset + 4 + entryCount * 8,
                        data.begin() + offset + size);

    return true;
}

std::string KS3DReader::getString(uint32_t index) const
{
    if (index >= m_stringEntries.size()) return {};
    const auto& [strOffset, strLength] = m_stringEntries[index];
    if (strOffset + strLength > m_stringData.size()) return {};
    return std::string(m_stringData.begin() + strOffset,
                       m_stringData.begin() + strOffset + strLength);
}

bool KS3DReader::parseMaterials(const std::vector<uint8_t>& data, uint32_t offset, uint32_t count)
{
    if (offset + 4 > data.size()) {
        m_lastError = "Material section offset out of bounds";
        return false;
    }

    size_t pos = offset;

    uint32_t storedCount = 0;
    std::memcpy(&storedCount, data.data() + pos, sizeof(uint32_t));
    pos += 4;

    if (storedCount != count) {
        m_lastError = "Material count mismatch";
        return false;
    }

    m_scene.materials.resize(count);

    for (uint32_t i = 0; i < count; i++) {
        if (pos + sizeof(ks3d::MaterialData) > data.size()) {
            m_lastError = "Material data truncated";
            return false;
        }

        ks3d::MaterialData md{};
        std::memcpy(&md, data.data() + pos, sizeof(ks3d::MaterialData));
        pos += sizeof(ks3d::MaterialData);

        auto& mat = m_scene.materials[i];
        mat.name = getString(md.nameIndex);
        std::memcpy(mat.baseColor, md.baseColor, sizeof(float) * 3);
        mat.metallic = md.metallic;
        mat.roughness = md.roughness;
        mat.opacity = md.opacity;
        std::memcpy(mat.emissive, md.emissive, sizeof(float) * 3);
        mat.clearcoat = md.clearcoat;
        mat.clearcoatRoughness = md.clearcoatRoughness;
        mat.sheen = md.sheen;
        std::memcpy(mat.sheenColor, md.sheenColor, sizeof(float) * 3);
        mat.transmission = md.transmission;
        mat.ior = md.ior;
        mat.subsurface = md.subsurface;
        mat.twoSided = (md.twoSided != 0);
        mat.transparent = (md.transparent != 0);

        auto resolveTex = [](uint32_t idx) -> int32_t {
            return (idx == 0xFFFFFFFF) ? -1 : static_cast<int32_t>(idx);
        };
        mat.baseColorTex = resolveTex(md.baseColorTexIndex);
        mat.normalTex = resolveTex(md.normalTexIndex);
        mat.roughnessTex = resolveTex(md.roughnessTexIndex);
        mat.metallicTex = resolveTex(md.metallicTexIndex);
        mat.emissiveTex = resolveTex(md.emissiveTexIndex);
        mat.opacityTex = resolveTex(md.opacityTexIndex);
        mat.aoTex = resolveTex(md.aoTexIndex);
        mat.heightTex = resolveTex(md.heightTexIndex);
    }

    return true;
}

bool KS3DReader::parseMeshes(const std::vector<uint8_t>& data, uint32_t offset, uint32_t count)
{
    if (offset + 4 > data.size()) {
        m_lastError = "Mesh section offset out of bounds";
        return false;
    }

    size_t pos = offset;

    uint32_t storedCount = 0;
    std::memcpy(&storedCount, data.data() + pos, sizeof(uint32_t));
    pos += 4;

    if (storedCount != count) {
        m_lastError = "Mesh count mismatch";
        return false;
    }

    m_scene.meshes.resize(count);

    for (uint32_t i = 0; i < count; i++) {
        if (pos + sizeof(ks3d::MeshData) > data.size()) {
            m_lastError = "Mesh header truncated";
            return false;
        }

        ks3d::MeshData md{};
        std::memcpy(&md, data.data() + pos, sizeof(ks3d::MeshData));
        pos += sizeof(ks3d::MeshData);

        auto& mesh = m_scene.meshes[i];
        mesh.name = getString(md.nameIndex);
        mesh.vertexFlags = md.vertexFlags;

        // Read vertex data
        size_t vertexDataSize = md.vertexCount * sizeof(float);
        if (pos + vertexDataSize > data.size()) {
            m_lastError = "Mesh vertex data truncated";
            return false;
        }
        mesh.vertices.resize(md.vertexCount);
        std::memcpy(mesh.vertices.data(), data.data() + pos, vertexDataSize);
        pos += vertexDataSize;

        // Read index data
        size_t indexDataSize = md.indexCount * sizeof(uint32_t);
        if (pos + indexDataSize > data.size()) {
            m_lastError = "Mesh index data truncated";
            return false;
        }
        mesh.indices.resize(md.indexCount);
        std::memcpy(mesh.indices.data(), data.data() + pos, indexDataSize);
        pos += indexDataSize;

        // Read submesh data
        mesh.submeshes.resize(md.submeshCount);
        for (uint32_t s = 0; s < md.submeshCount; s++) {
            if (pos + sizeof(ks3d::SubmeshData) > data.size()) {
                m_lastError = "Submesh data truncated";
                return false;
            }
            ks3d::SubmeshData sd{};
            std::memcpy(&sd, data.data() + pos, sizeof(ks3d::SubmeshData));
            pos += sizeof(ks3d::SubmeshData);

            mesh.submeshes[s].materialIndex = sd.materialIndex;
            mesh.submeshes[s].indexOffset = sd.indexOffset;
            mesh.submeshes[s].indexCount = sd.indexCount;
        }
    }

    return true;
}

bool KS3DReader::parseTextures(const std::vector<uint8_t>& data, uint32_t offset, uint32_t count)
{
    if (offset + 4 > data.size()) {
        m_lastError = "Texture section offset out of bounds";
        return false;
    }

    size_t pos = offset;

    uint32_t storedCount = 0;
    std::memcpy(&storedCount, data.data() + pos, sizeof(uint32_t));
    pos += 4;

    if (storedCount != count) {
        m_lastError = "Texture count mismatch";
        return false;
    }

    m_scene.textures.resize(count);

    for (uint32_t i = 0; i < count; i++) {
        if (pos + sizeof(ks3d::TextureData) > data.size()) {
            m_lastError = "Texture header truncated";
            return false;
        }

        ks3d::TextureData td{};
        std::memcpy(&td, data.data() + pos, sizeof(ks3d::TextureData));
        pos += sizeof(ks3d::TextureData);

        auto& tex = m_scene.textures[i];
        tex.name = getString(td.nameIndex);
        tex.format = static_cast<ks3d::TextureFormat>(td.format);
        tex.width = td.width;
        tex.height = td.height;

        if (td.dataSize > 0) {
            if (pos + td.dataSize > data.size()) {
                m_lastError = "Texture data truncated";
                return false;
            }
            tex.data.assign(data.begin() + pos, data.begin() + pos + td.dataSize);
            pos += td.dataSize;
        }
    }

    return true;
}

bool KS3DReader::parseNodes(const std::vector<uint8_t>& data, uint32_t offset, uint32_t count)
{
    if (offset + 4 > data.size()) {
        m_lastError = "Node section offset out of bounds";
        return false;
    }

    size_t pos = offset;

    uint32_t storedCount = 0;
    std::memcpy(&storedCount, data.data() + pos, sizeof(uint32_t));
    pos += 4;

    if (storedCount != count) {
        m_lastError = "Node count mismatch";
        return false;
    }

    m_scene.nodes.resize(count);

    for (uint32_t i = 0; i < count; i++) {
        if (pos + sizeof(ks3d::NodeData) > data.size()) {
            m_lastError = "Node data truncated";
            return false;
        }

        ks3d::NodeData nd{};
        std::memcpy(&nd, data.data() + pos, sizeof(ks3d::NodeData));
        pos += sizeof(ks3d::NodeData);

        auto& node = m_scene.nodes[i];
        node.name = getString(nd.nameIndex);
        node.parentIndex = nd.parentIndex;
        std::memcpy(node.position, nd.position, sizeof(float) * 3);
        std::memcpy(node.rotationQuat, nd.rotationQuat, sizeof(float) * 4);
        std::memcpy(node.scale, nd.scale, sizeof(float) * 3);
        node.meshIndex = (nd.meshIndex == 0xFFFFFFFF)
            ? -1 : static_cast<int32_t>(nd.meshIndex);
        node.materialIndex = (nd.materialIndex == 0xFFFFFFFF)
            ? -1 : static_cast<int32_t>(nd.materialIndex);
        node.visible = (nd.visible != 0);

        // Read child indices
        node.children.resize(nd.childCount);
        for (uint32_t c = 0; c < nd.childCount; c++) {
            if (pos + sizeof(uint32_t) > data.size()) {
                m_lastError = "Node child data truncated";
                return false;
            }
            std::memcpy(&node.children[c], data.data() + pos, sizeof(uint32_t));
            pos += sizeof(uint32_t);
        }
    }

    return true;
}

std::vector<float> KS3DReader::getVertexPositions(const KS3DMesh& mesh)
{
    // Vertex layout: px, py, pz, nx, ny, nz, u0, v0, tx, ty, tz, btx, bty, btz, bw[4], boneIdx
    // = 3 + 3 + 2 + 3 + 3 + 4 + 1 = 19 floats per vertex
    constexpr int FLOATS_PER_VERTEX = 19;
    constexpr int POS_OFFSET = 0;

    std::vector<float> positions;
    size_t vertCount = mesh.vertices.size() / FLOATS_PER_VERTEX;
    positions.reserve(vertCount * 3);

    for (size_t i = 0; i < vertCount; i++) {
        size_t base = i * FLOATS_PER_VERTEX + POS_OFFSET;
        if (base + 3 <= mesh.vertices.size()) {
            positions.push_back(mesh.vertices[base]);
            positions.push_back(mesh.vertices[base + 1]);
            positions.push_back(mesh.vertices[base + 2]);
        }
    }

    return positions;
}

std::vector<float> KS3DReader::getVertexNormals(const KS3DMesh& mesh)
{
    constexpr int FLOATS_PER_VERTEX = 19;
    constexpr int NORM_OFFSET = 3;

    std::vector<float> normals;
    size_t vertCount = mesh.vertices.size() / FLOATS_PER_VERTEX;
    normals.reserve(vertCount * 3);

    for (size_t i = 0; i < vertCount; i++) {
        size_t base = i * FLOATS_PER_VERTEX + NORM_OFFSET;
        if (base + 3 <= mesh.vertices.size()) {
            normals.push_back(mesh.vertices[base]);
            normals.push_back(mesh.vertices[base + 1]);
            normals.push_back(mesh.vertices[base + 2]);
        }
    }

    return normals;
}

std::vector<float> KS3DReader::getVertexUVs(const KS3DMesh& mesh)
{
    constexpr int FLOATS_PER_VERTEX = 19;
    constexpr int UV_OFFSET = 6;

    std::vector<float> uvs;
    size_t vertCount = mesh.vertices.size() / FLOATS_PER_VERTEX;
    uvs.reserve(vertCount * 2);

    for (size_t i = 0; i < vertCount; i++) {
        size_t base = i * FLOATS_PER_VERTEX + UV_OFFSET;
        if (base + 2 <= mesh.vertices.size()) {
            uvs.push_back(mesh.vertices[base]);
            uvs.push_back(mesh.vertices[base + 1]);
        }
    }

    return uvs;
}

} // namespace ks
