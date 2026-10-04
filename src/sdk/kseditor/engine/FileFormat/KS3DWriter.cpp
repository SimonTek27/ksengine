#include "KS3DWriter.h"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace ks {

template<typename T>
void KS3DWriter::writeRaw(std::vector<uint8_t>& buf, const T& value)
{
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), ptr, ptr + sizeof(T));
}

void KS3DWriter::writeString(std::vector<uint8_t>& buf, const std::string& str)
{
    uint32_t len = static_cast<uint32_t>(str.size());
    writeRaw(buf, len);
    buf.insert(buf.end(), str.begin(), str.end());
}

uint32_t KS3DWriter::addString(const std::string& str)
{
    uint32_t index = static_cast<uint32_t>(m_stringEntries.size());
    uint32_t offset = static_cast<uint32_t>(m_stringData.size());
    uint32_t length = static_cast<uint32_t>(str.size());

    m_stringEntries.push_back({offset, length});
    m_stringData.insert(m_stringData.end(), str.begin(), str.end());

    return index;
}

void KS3DWriter::buildStringTable(const KS3DScene& scene)
{
    m_stringEntries.clear();
    m_stringData.clear();

    for (const auto& mat : scene.materials)
        addString(mat.name);
    for (const auto& mesh : scene.meshes)
        addString(mesh.name);
    for (const auto& tex : scene.textures)
        addString(tex.name);
    for (const auto& node : scene.nodes)
        addString(node.name);
}

std::vector<uint8_t> KS3DWriter::serializeHeader()
{
    std::vector<uint8_t> buf;
    buf.reserve(sizeof(ks3d::FileHeader));
    ks3d::FileHeader header{};
    header.magic = ks3d::MAGIC;
    header.version = ks3d::VERSION;
    header.flags = 0;
    // offsets will be patched later
    writeRaw(buf, header);
    return buf;
}

std::vector<uint8_t> KS3DWriter::serializeStringTable()
{
    std::vector<uint8_t> buf;

    uint32_t totalEntries = static_cast<uint32_t>(m_stringEntries.size());
    writeRaw(buf, totalEntries);

    for (const auto& [offset, length] : m_stringEntries) {
        writeRaw(buf, offset);
        writeRaw(buf, length);
    }

    buf.insert(buf.end(), m_stringData.begin(), m_stringData.end());
    return buf;
}

std::vector<uint8_t> KS3DWriter::serializeMaterials(const KS3DScene& scene)
{
    std::vector<uint8_t> buf;

    uint32_t count = static_cast<uint32_t>(scene.materials.size());
    writeRaw(buf, count);

    for (const auto& mat : scene.materials) {
        ks3d::MaterialData md{};
        std::memcpy(md.baseColor, mat.baseColor, sizeof(float) * 3);
        md.metallic = mat.metallic;
        md.roughness = mat.roughness;
        md.opacity = mat.opacity;
        std::memcpy(md.emissive, mat.emissive, sizeof(float) * 3);
        md.clearcoat = mat.clearcoat;
        md.clearcoatRoughness = mat.clearcoatRoughness;
        md.sheen = mat.sheen;
        std::memcpy(md.sheenColor, mat.sheenColor, sizeof(float) * 3);
        md.transmission = mat.transmission;
        md.ior = mat.ior;
        md.subsurface = mat.subsurface;
        md.twoSided = mat.twoSided ? 1 : 0;
        md.transparent = mat.transparent ? 1 : 0;

        // Resolve string indices (materials come first in string table)
        uint32_t matStringBase = 0;
        md.nameIndex = matStringBase + static_cast<uint32_t>(
            std::distance(scene.materials.data(), &mat));

        auto texIndex = [&](int32_t texIdx) -> uint32_t {
            if (texIdx < 0 || texIdx >= static_cast<int32_t>(scene.textures.size()))
                return 0xFFFFFFFF;
            return static_cast<uint32_t>(scene.materials.size() + scene.meshes.size() + texIdx);
        };

        md.baseColorTexIndex = texIndex(mat.baseColorTex);
        md.normalTexIndex = texIndex(mat.normalTex);
        md.roughnessTexIndex = texIndex(mat.roughnessTex);
        md.metallicTexIndex = texIndex(mat.metallicTex);
        md.emissiveTexIndex = texIndex(mat.emissiveTex);
        md.opacityTexIndex = texIndex(mat.opacityTex);
        md.aoTexIndex = texIndex(mat.aoTex);
        md.heightTexIndex = texIndex(mat.heightTex);

        writeRaw(buf, md);
    }

    return buf;
}

std::vector<uint8_t> KS3DWriter::serializeMeshes(const KS3DScene& scene)
{
    std::vector<uint8_t> buf;

    uint32_t count = static_cast<uint32_t>(scene.meshes.size());
    writeRaw(buf, count);

    for (const auto& mesh : scene.meshes) {
        ks3d::MeshData md{};
        md.nameIndex = static_cast<uint32_t>(scene.materials.size()) +
                       static_cast<uint32_t>(
                           std::distance(scene.meshes.data(), &mesh));
        md.vertexFlags = mesh.vertexFlags;
        md.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
        md.indexCount = static_cast<uint32_t>(mesh.indices.size());
        md.submeshCount = static_cast<uint32_t>(mesh.submeshes.size());

        writeRaw(buf, md);

        // vertex data (raw floats)
        buf.insert(buf.end(),
                   reinterpret_cast<const uint8_t*>(mesh.vertices.data()),
                   reinterpret_cast<const uint8_t*>(mesh.vertices.data() + mesh.vertices.size()));

        // index data
        buf.insert(buf.end(),
                   reinterpret_cast<const uint8_t*>(mesh.indices.data()),
                   reinterpret_cast<const uint8_t*>(mesh.indices.data() + mesh.indices.size()));

        // submesh data
        for (const auto& sub : mesh.submeshes) {
            ks3d::SubmeshData sd{};
            sd.materialIndex = sub.materialIndex;
            sd.indexOffset = sub.indexOffset;
            sd.indexCount = sub.indexCount;
            writeRaw(buf, sd);
        }
    }

    return buf;
}

std::vector<uint8_t> KS3DWriter::serializeTextures(const KS3DScene& scene)
{
    std::vector<uint8_t> buf;

    uint32_t count = static_cast<uint32_t>(scene.textures.size());
    writeRaw(buf, count);

    for (const auto& tex : scene.textures) {
        ks3d::TextureData td{};
        td.nameIndex = static_cast<uint32_t>(scene.materials.size() +
                       scene.meshes.size()) +
                       static_cast<uint32_t>(
                           std::distance(scene.textures.data(), &tex));
        td.format = static_cast<uint32_t>(tex.format);
        td.width = tex.width;
        td.height = tex.height;
        td.dataSize = static_cast<uint32_t>(tex.data.size());

        writeRaw(buf, td);
        buf.insert(buf.end(), tex.data.begin(), tex.data.end());
    }

    return buf;
}

std::vector<uint8_t> KS3DWriter::serializeNodes(const KS3DScene& scene)
{
    std::vector<uint8_t> buf;

    uint32_t count = static_cast<uint32_t>(scene.nodes.size());
    writeRaw(buf, count);

    for (const auto& node : scene.nodes) {
        ks3d::NodeData nd{};
        nd.parentIndex = node.parentIndex;
        nd.nameIndex = static_cast<uint32_t>(scene.materials.size() +
                       scene.meshes.size() + scene.textures.size()) +
                       static_cast<uint32_t>(
                           std::distance(scene.nodes.data(), &node));
        std::memcpy(nd.position, node.position, sizeof(float) * 3);
        std::memcpy(nd.rotationQuat, node.rotationQuat, sizeof(float) * 4);
        std::memcpy(nd.scale, node.scale, sizeof(float) * 3);
        nd.meshIndex = (node.meshIndex >= 0)
            ? static_cast<uint32_t>(node.meshIndex) : 0xFFFFFFFF;
        nd.materialIndex = (node.materialIndex >= 0)
            ? static_cast<uint32_t>(node.materialIndex) : 0xFFFFFFFF;
        nd.visible = node.visible ? 1 : 0;
        nd.childCount = static_cast<uint32_t>(node.children.size());

        writeRaw(buf, nd);

        for (uint32_t childIdx : node.children) {
            writeRaw(buf, childIdx);
        }
    }

    return buf;
}

bool KS3DWriter::writeToFile(const std::string& path, const KS3DScene& scene)
{
    m_lastError.clear();

    buildStringTable(scene);

    auto headerBuf = serializeHeader();
    auto stringBuf = serializeStringTable();
    auto materialBuf = serializeMaterials(scene);
    auto meshBuf = serializeMeshes(scene);
    auto textureBuf = serializeTextures(scene);
    auto nodeBuf = serializeNodes(scene);

    // Compute section offsets (after header)
    uint32_t offset = sizeof(ks3d::FileHeader);
    uint32_t stringTableOffset = offset;
    uint32_t stringTableSize = static_cast<uint32_t>(stringBuf.size());
    offset += stringTableSize;

    uint32_t materialOffset = offset;
    uint32_t materialCount = static_cast<uint32_t>(scene.materials.size());
    offset += static_cast<uint32_t>(materialBuf.size());

    uint32_t meshOffset = offset;
    uint32_t meshCount = static_cast<uint32_t>(scene.meshes.size());
    offset += static_cast<uint32_t>(meshBuf.size());

    uint32_t textureOffset = offset;
    uint32_t textureCount = static_cast<uint32_t>(scene.textures.size());
    offset += static_cast<uint32_t>(textureBuf.size());

    uint32_t nodeOffset = offset;
    uint32_t nodeCount = static_cast<uint32_t>(scene.nodes.size());
    offset += static_cast<uint32_t>(nodeBuf.size());

    uint32_t auxOffset = 0;
    uint32_t auxSize = 0;
    if (!scene.auxJson.empty()) {
        auxOffset = offset;
        auxSize = static_cast<uint32_t>(scene.auxJson.size());
    }

    // Patch header
    auto* header = reinterpret_cast<ks3d::FileHeader*>(headerBuf.data());
    header->stringTableOffset = stringTableOffset;
    header->stringTableSize = stringTableSize;
    header->materialCount = materialCount;
    header->materialOffset = materialOffset;
    header->meshCount = meshCount;
    header->meshOffset = meshOffset;
    header->textureCount = textureCount;
    header->textureOffset = textureOffset;
    header->nodeCount = nodeCount;
    header->nodeOffset = nodeOffset;
    header->reserved[0] = auxOffset;
    header->reserved[1] = auxSize;

    // Write file
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        m_lastError = "Failed to open file for writing: " + path;
        return false;
    }

    auto writeSection = [&](const std::vector<uint8_t>& buf) {
        file.write(reinterpret_cast<const char*>(buf.data()),
                   static_cast<std::streamsize>(buf.size()));
    };

    writeSection(headerBuf);
    writeSection(stringBuf);
    writeSection(materialBuf);
    writeSection(meshBuf);
    writeSection(textureBuf);
    writeSection(nodeBuf);
    if (auxSize > 0)
        file.write(scene.auxJson.data(), static_cast<std::streamsize>(auxSize));

    if (!file.good()) {
        m_lastError = "Failed to write file: " + path;
        return false;
    }

    return true;
}

} // namespace ks
