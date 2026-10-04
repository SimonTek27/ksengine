#include "GLBParser.h"
#include <fstream>
#include <cstring>
#include <sstream>

namespace ks {

static std::string extractJSONValue(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        pos++;

    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }

    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']' && json[end] != '\n' && json[end] != '\r')
        end++;

    return json.substr(pos, end - pos);
}

static std::vector<std::string> extractJSONArray(const std::string& json, const std::string& key) {
    std::vector<std::string> results;
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return results;

    pos = json.find('[', pos);
    if (pos == std::string::npos) return results;

    size_t end = json.find(']', pos);
    if (end == std::string::npos) return results;

    std::string content = json.substr(pos + 1, end - pos - 1);
    std::istringstream iss(content);
    std::string token;
    while (std::getline(iss, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t\n\r"));
        token.erase(token.find_last_not_of(" \t\n\r") + 1);
        if (!token.empty()) results.push_back(token);
    }
    return results;
}

static size_t findArrayStart(const std::string& json, const std::string& key, size_t startFrom = 0) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search, startFrom);
    if (pos == std::string::npos) return std::string::npos;
    pos = json.find('[', pos);
    return pos;
}

bool GLBParser::loadFromFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;

    if (size < 12) return false;

    uint32_t magic = *reinterpret_cast<uint32_t*>(buffer.data());
    if (magic != 0x46546C67) return false;

    uint32_t version = *reinterpret_cast<uint32_t*>(buffer.data() + 4);
    if (version != 2) return false;

    uint32_t totalLength = *reinterpret_cast<uint32_t*>(buffer.data() + 8);
    if (totalLength > static_cast<uint32_t>(size)) return false;

    m_scene = GLBScene();
    m_scene.sourcePath = path;
    m_jsonChunk.clear();

    size_t offset = 12;
    while (offset < totalLength - 8) {
        uint32_t chunkLength = *reinterpret_cast<uint32_t*>(buffer.data() + offset);
        uint32_t chunkType = *reinterpret_cast<uint32_t*>(buffer.data() + offset + 4);
        offset += 8;

        if (offset + chunkLength > totalLength) break;

        if (chunkType == 0x4E4F534A) {
            m_jsonChunk.assign(reinterpret_cast<char*>(buffer.data() + offset), chunkLength);
            parseJSONChunk(std::vector<uint8_t>(buffer.begin() + offset, buffer.begin() + offset + chunkLength));
        } else if (chunkType == 0x004E4942) {
            parseBINChunk(std::vector<uint8_t>(buffer.begin() + offset, buffer.begin() + offset + chunkLength));
        }

        offset += chunkLength;
    }

    return true;
}

bool GLBParser::parseJSONChunk(const std::vector<uint8_t>& data)
{
    std::string json(reinterpret_cast<const char*>(data.data()), data.size());

    auto meshesArr = extractJSONArray(json, "meshes");
    auto accessorsArr = extractJSONArray(json, "accessors");
    auto bufferViewsArr = extractJSONArray(json, "bufferViews");
    auto buffersArr = extractJSONArray(json, "buffers");
    auto materialsArr = extractJSONArray(json, "materials");

    for (const auto& buf : buffersArr) {
        GLBBuffer b;
        m_scene.buffers.push_back(b);
    }

    for (size_t i = 0; i < bufferViewsArr.size(); i++) {
        GLBBufferView bv;
        std::string viewJson = bufferViewsArr[i];
        std::string bv_str = extractJSONValue(viewJson, "buffer");
        if (!bv_str.empty()) bv.buffer = static_cast<uint32_t>(std::stoul(bv_str));
        bv_str = extractJSONValue(viewJson, "byteOffset");
        if (!bv_str.empty()) bv.byteOffset = static_cast<uint32_t>(std::stoul(bv_str));
        bv_str = extractJSONValue(viewJson, "byteLength");
        if (!bv_str.empty()) bv.byteLength = static_cast<uint32_t>(std::stoul(bv_str));
        bv_str = extractJSONValue(viewJson, "target");
        if (!bv_str.empty()) bv.target = static_cast<uint32_t>(std::stoul(bv_str));
        m_scene.bufferViews.push_back(bv);
    }

    for (size_t i = 0; i < accessorsArr.size(); i++) {
        GLBAccessor acc;
        std::string accJson = accessorsArr[i];
        std::string val = extractJSONValue(accJson, "bufferView");
        if (!val.empty()) acc.bufferView = static_cast<uint32_t>(std::stoul(val));
        val = extractJSONValue(accJson, "byteOffset");
        if (!val.empty()) acc.byteOffset = static_cast<uint32_t>(std::stoul(val));
        val = extractJSONValue(accJson, "componentType");
        if (!val.empty()) acc.componentType = static_cast<uint32_t>(std::stoul(val));
        val = extractJSONValue(accJson, "count");
        if (!val.empty()) acc.count = static_cast<uint32_t>(std::stoul(val));
        acc.type = extractJSONValue(accJson, "type");
        m_scene.accessors.push_back(acc);
    }

    for (size_t i = 0; i < materialsArr.size(); i++) {
        GLBMaterial mat;
        std::string matJson = materialsArr[i];
        mat.name = "material_" + std::to_string(i);

        std::string pbrStr = extractJSONValue(matJson, "pbrMetallicRoughness");
        if (!pbrStr.empty()) {
            std::string bcStr = extractJSONValue(pbrStr, "baseColorFactor");
            if (!bcStr.empty()) {
                auto colors = extractJSONArray("[" + bcStr + "]", "");
                if (colors.size() >= 3) {
                    mat.baseColorFactor.x = static_cast<float>(std::stod(colors[0]));
                    mat.baseColorFactor.y = static_cast<float>(std::stod(colors[1]));
                    mat.baseColorFactor.z = static_cast<float>(std::stod(colors[2]));
                }
            }
            std::string mf = extractJSONValue(pbrStr, "metallicFactor");
            if (!mf.empty()) mat.metallicFactor = static_cast<float>(std::stod(mf));
            std::string rf = extractJSONValue(pbrStr, "roughnessFactor");
            if (!rf.empty()) mat.roughnessFactor = static_cast<float>(std::stod(rf));
        }
        m_scene.materials.push_back(mat);
    }

    for (size_t i = 0; i < meshesArr.size(); i++) {
        GLBMesh mesh;
        mesh.name = "mesh_" + std::to_string(i);

        size_t primitivesStart = findArrayStart(json, "primitives", 0);
        if (primitivesStart != std::string::npos) {
            size_t primEnd = json.find(']', primitivesStart);
            if (primEnd != std::string::npos) {
                std::string primContent = json.substr(primitivesStart, primEnd - primitivesStart + 1);
                auto prims = extractJSONArray(primContent, "");
                for (const auto& primStr : prims) {
                    GLBPrimitive prim;
                    std::string idx = extractJSONValue(primStr, "indices");
                    if (!idx.empty()) prim.indices = static_cast<uint32_t>(std::stoul(idx));
                    idx = extractJSONValue(primStr, "material");
                    if (!idx.empty()) prim.material = static_cast<uint32_t>(std::stoul(idx));

                    size_t attrStart = primStr.find("attributes");
                    if (attrStart != std::string::npos) {
                        attrStart = primStr.find('{', attrStart);
                        size_t attrEnd = primStr.find('}', attrStart);
                        if (attrStart != std::string::npos && attrEnd != std::string::npos) {
                            std::string attrContent = primStr.substr(attrStart + 1, attrEnd - attrStart - 1);
                            std::istringstream attrStream(attrContent);
                            std::string attrToken;
                            while (std::getline(attrStream, attrToken, ',')) {
                                attrToken.erase(0, attrToken.find_first_not_of(" \t\n\r"));
                                size_t colonPos = attrToken.find(':');
                                if (colonPos != std::string::npos) {
                                    std::string attrName = attrToken.substr(1, attrToken.find('"', 1) - 1);
                                    std::string attrVal = attrToken.substr(colonPos + 1);
                                    attrVal.erase(0, attrVal.find_first_not_of(" \t"));
                                    if (!attrVal.empty()) {
                                        prim.attributes[attrName] = static_cast<uint32_t>(std::stoul(attrVal));
                                    }
                                }
                            }
                        }
                    }
                    mesh.primitives.push_back(prim);
                }
            }
        }
        m_scene.meshes.push_back(mesh);
    }

    return true;
}

bool GLBParser::parseBINChunk(const std::vector<uint8_t>& data)
{
    GLBBuffer buf;
    buf.data = data;
    m_scene.buffers.push_back(buf);
    return true;
}

std::vector<Vec3> GLBParser::getVertices(uint32_t accessorIndex) const
{
    std::vector<Vec3> result;
    if (accessorIndex >= m_scene.accessors.size()) return result;

    const auto& acc = m_scene.accessors[accessorIndex];
    if (acc.bufferView >= m_scene.bufferViews.size()) return result;
    if (m_scene.buffers.empty()) return result;

    const auto& bv = m_scene.bufferViews[acc.bufferView];
    const auto& buf = m_scene.buffers[0];

    size_t offset = bv.byteOffset + acc.byteOffset;
    size_t stride = (bv.byteLength / acc.count);
    if (stride == 0) stride = 12;

    for (uint32_t i = 0; i < acc.count; i++) {
        size_t pos = offset + i * stride;
        if (pos + 12 > buf.data.size()) break;

        Vec3 v;
        std::memcpy(&v.x, buf.data.data() + pos, sizeof(float));
        std::memcpy(&v.y, buf.data.data() + pos + 4, sizeof(float));
        std::memcpy(&v.z, buf.data.data() + pos + 8, sizeof(float));
        result.push_back(v);
    }

    return result;
}

std::vector<Vec3> GLBParser::getNormals(uint32_t accessorIndex) const
{
    return getVertices(accessorIndex);
}

std::vector<Vec2> GLBParser::getTexCoords(uint32_t accessorIndex) const
{
    std::vector<Vec2> result;
    if (accessorIndex >= m_scene.accessors.size()) return result;

    const auto& acc = m_scene.accessors[accessorIndex];
    if (acc.bufferView >= m_scene.bufferViews.size()) return result;
    if (m_scene.buffers.empty()) return result;

    const auto& bv = m_scene.bufferViews[acc.bufferView];
    const auto& buf = m_scene.buffers[0];

    size_t offset = bv.byteOffset + acc.byteOffset;
    size_t stride = (bv.byteLength / acc.count);
    if (stride == 0) stride = 8;

    for (uint32_t i = 0; i < acc.count; i++) {
        size_t pos = offset + i * stride;
        if (pos + 8 > buf.data.size()) break;

        Vec2 v;
        std::memcpy(&v.x, buf.data.data() + pos, sizeof(float));
        std::memcpy(&v.y, buf.data.data() + pos + 4, sizeof(float));
        result.push_back(v);
    }

    return result;
}

std::vector<uint32_t> GLBParser::getIndices(uint32_t accessorIndex) const
{
    std::vector<uint32_t> result;
    if (accessorIndex >= m_scene.accessors.size()) return result;

    const auto& acc = m_scene.accessors[accessorIndex];
    if (acc.bufferView >= m_scene.bufferViews.size()) return result;
    if (m_scene.buffers.empty()) return result;

    const auto& bv = m_scene.bufferViews[acc.bufferView];
    const auto& buf = m_scene.buffers[0];

    size_t offset = bv.byteOffset + acc.byteOffset;

    for (uint32_t i = 0; i < acc.count; i++) {
        size_t pos = offset;
        if (acc.componentType == 5123) {
            pos += i * 2;
            if (pos + 2 > buf.data.size()) break;
            uint16_t idx;
            std::memcpy(&idx, buf.data.data() + pos, sizeof(uint16_t));
            result.push_back(idx);
        } else if (acc.componentType == 5125) {
            pos += i * 4;
            if (pos + 4 > buf.data.size()) break;
            uint32_t idx;
            std::memcpy(&idx, buf.data.data() + pos, sizeof(uint32_t));
            result.push_back(idx);
        }
    }

    return result;
}

std::string GLB::Parser::m_lastError;

} // namespace ks
