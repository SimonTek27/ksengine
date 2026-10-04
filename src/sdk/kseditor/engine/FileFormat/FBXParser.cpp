#include "FBXParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <QtCore>
#include <QDebug>

#if HAS_ZLIB
#include <zlib.h>
#endif

namespace ks {

bool FBXParser::loadFromFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) return false;

    m_scene = FBXScene();
    m_scene.sourcePath = path;
    m_meshGeometry.clear();
    m_materialProps.clear();

    bool isBinary = false;
    if (size >= 23) {
        std::string header(buffer.data(), 23);
        isBinary = (header == "Kaydara FBX Binary  ");
    }

    if (isBinary) {
        return parseBinary(buffer);
    } else {
        std::string content(buffer.data(), size);
        return parseASCII(content);
    }
}

bool FBXParser::parseASCII(const std::string& content)
{
    std::istringstream stream(content);
    std::string line;
    std::string currentSection;
    std::string currentObject;
    std::string accumulatedSection;
    int braceDepth = 0;
    bool inGeometry = false;
    bool inMaterials = false;
    bool inConnections = false;
    bool inSkeleton = false;
    bool inAnimation = false;

    std::string currentMeshId;
    std::string currentMatId;

    while (std::getline(stream, line)) {
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        line.erase(0, line.find_first_not_of(" \t"));

        if (line.empty()) continue;

        if (line.back() == '{') {
            braceDepth++;
            std::string keyword = line.substr(0, line.size() - 1);
            keyword.erase(keyword.find_last_not_of(" \t") + 1);

            if (keyword == "Geometry") {
                inGeometry = true;
                accumulatedSection.clear();
            } else if (keyword == "Material") {
                inMaterials = true;
                accumulatedSection.clear();
            } else if (keyword == "Connections") {
                inConnections = true;
                accumulatedSection.clear();
            } else if (keyword.find("Model") == 0 && keyword.find("LimbNode") != std::string::npos) {
                inSkeleton = true;
                accumulatedSection.clear();
            } else if (keyword == "Deformer" && accumulatedSection.find("Cluster") != std::string::npos) {
                inSkeleton = true;
                accumulatedSection.clear();
            } else if (keyword.find("AnimationStack") != std::string::npos ||
                       keyword.find("AnimationLayer") != std::string::npos ||
                       keyword.find("AnimationCurve") != std::string::npos) {
                inAnimation = true;
                accumulatedSection.clear();
            }

            accumulatedSection += line + "\n";
            continue;
        }

        if (line == "}") {
            braceDepth--;

            if (inGeometry && braceDepth <= 2) {
                accumulatedSection += line + "\n";
                parseGeometry(accumulatedSection);
                inGeometry = false;
            } else if (inMaterials && braceDepth <= 2) {
                accumulatedSection += line + "\n";
                parseMaterials(accumulatedSection);
                inMaterials = false;
            } else if (inConnections && braceDepth <= 1) {
                accumulatedSection += line + "\n";
                parseConnections(accumulatedSection);
                inConnections = false;
            } else if (inSkeleton && braceDepth <= 2) {
                accumulatedSection += line + "\n";
                parseSkeleton(accumulatedSection);
                inSkeleton = false;
            } else if (inAnimation && braceDepth <= 2) {
                accumulatedSection += line + "\n";
                parseAnimationStack(accumulatedSection);
                inAnimation = false;
            } else {
                accumulatedSection += line + "\n";
            }
            continue;
        }

        if (inGeometry || inMaterials || inConnections || inSkeleton || inAnimation) {
            accumulatedSection += line + "\n";
        }
    }

    return true;
}

bool FBXParser::parseBinary(const std::vector<char>& data)
{
    if (data.size() < 140) return false;

    size_t offset = 23;
    uint32_t version = *reinterpret_cast<const uint32_t*>(data.data() + 28);

    // Helper: skip one property in the property list, returning bytes consumed
    auto skipProperty = [&](size_t pos) -> size_t {
        if (pos >= data.size()) return 0;
        uint8_t typeByte = static_cast<uint8_t>(data[pos]);
        switch (typeByte) {
            case 'Y': return 1 + 2; // int16
            case 'C': return 1 + 1; // bool
            case 'I': return 1 + 4; // int32
            case 'F': return 1 + 4; // float
            case 'D': return 1 + 8; // double
            case 'L': return 1 + 8; // int64
            case 'R': case 'S': {
                if (pos + 5 > data.size()) return 0;
                uint32_t len = *reinterpret_cast<const uint32_t*>(data.data() + pos + 1);
                return 1 + 4 + len;
            }
            case 'f': case 'd': case 'l': case 'i': case 'b': {
                // Array types: count(uint32) + encoding(uint32) + byteLen(uint32) + data
                if (pos + 13 > data.size()) return 0;
                uint32_t byteLen = *reinterpret_cast<const uint32_t*>(data.data() + pos + 9);
                return 1 + 4 + 4 + 4 + byteLen;
            }
            default: return 0; // Unknown type, can't skip
        }
    };

    while (offset < data.size() - 25) {
        uint64_t endOffset = *reinterpret_cast<const uint64_t*>(data.data() + offset);
        uint64_t numProperties = *reinterpret_cast<const uint64_t*>(data.data() + offset + 8);
        uint64_t propertyListLen = *reinterpret_cast<const uint64_t*>(data.data() + offset + 16);
        uint8_t nameLen = *reinterpret_cast<const uint8_t*>(data.data() + offset + 24);

        std::string name(data.data() + offset + 25, nameLen);
        size_t propDataStart = offset + 25 + nameLen;

        // For array data nodes, find the array property within the property list
        // by skipping non-array properties to reach the actual array data
        if (name == "Vertices" || name == "PolygonVertexIndex" || name == "Normals" || name == "UV" || name == "UVSetData") {
            size_t propPos = propDataStart;
            size_t propEnd = propDataStart + static_cast<size_t>(propertyListLen);

            // Walk through properties to find the array
            for (uint64_t p = 0; p < numProperties && propPos < propEnd; ++p) {
                if (propPos >= data.size()) break;
                uint8_t typeByte = static_cast<uint8_t>(data[propPos]);

                if ((name == "Vertices" && (typeByte == 'f' || typeByte == 'd')) ||
                    (name == "PolygonVertexIndex" && (typeByte == 'i' || typeByte == 'I')) ||
                    (name == "Normals" && (typeByte == 'f' || typeByte == 'd')) ||
                    (name == "UV" && (typeByte == 'f' || typeByte == 'd')) ||
                    (name == "UVSetData" && (typeByte == 'f' || typeByte == 'd'))) {

                    // Array property: type(1) + count(4) + encoding(4) + byteLen(4) + data
                    if (propPos + 13 > data.size()) break;
                    uint32_t arrCount = *reinterpret_cast<const uint32_t*>(data.data() + propPos + 1);
                    uint32_t encoding = *reinterpret_cast<const uint32_t*>(data.data() + propPos + 5);
                    uint32_t byteLen = *reinterpret_cast<const uint32_t*>(data.data() + propPos + 9);
                    const char* arrData = data.data() + propPos + 13;

                    if (encoding == 0 && propPos + 13 + byteLen <= data.size()) {
                        // Raw encoding
                        if (name == "Vertices") {
                            if (!m_scene.meshes.empty()) {
                                auto& mesh = m_scene.meshes.back();
                                uint32_t vertCount = arrCount / 3;
                                for (uint32_t i = 0; i < vertCount; i++) {
                                    Vec3 v;
                                    std::memcpy(&v.x, arrData + i * 12, sizeof(float));
                                    std::memcpy(&v.y, arrData + i * 12 + 4, sizeof(float));
                                    std::memcpy(&v.z, arrData + i * 12 + 8, sizeof(float));
                                    mesh.vertices.push_back(v);
                                }
                            }
                        } else if (name == "PolygonVertexIndex") {
                            if (!m_scene.meshes.empty()) {
                                auto& mesh = m_scene.meshes.back();
                                for (uint32_t i = 0; i < arrCount; i++) {
                                    int32_t idx;
                                    std::memcpy(&idx, arrData + i * 4, sizeof(int32_t));
                                    if (idx < 0) {
                                        mesh.indices.push_back(static_cast<uint32_t>(~idx));
                                    } else {
                                        mesh.indices.push_back(static_cast<uint32_t>(idx));
                                    }
                                }
                            }
                        } else if (name == "Normals") {
                            if (!m_scene.meshes.empty()) {
                                auto& mesh = m_scene.meshes.back();
                                uint32_t normCount = arrCount / 3;
                                for (uint32_t i = 0; i < normCount; i++) {
                                    Vec3 n;
                                    std::memcpy(&n.x, arrData + i * 12, sizeof(float));
                                    std::memcpy(&n.y, arrData + i * 12 + 4, sizeof(float));
                                    std::memcpy(&n.z, arrData + i * 12 + 8, sizeof(float));
                                    mesh.normals.push_back(n);
                                }
                            }
                        } else if (name == "UV" || name == "UVSetData") {
                            if (!m_scene.meshes.empty()) {
                                auto& mesh = m_scene.meshes.back();
                                uint32_t uvCount = arrCount / 2;
                                for (uint32_t i = 0; i < uvCount; i++) {
                                    Vec2 uv;
                                    std::memcpy(&uv.x, arrData + i * 8, sizeof(float));
                                    std::memcpy(&uv.y, arrData + i * 8 + 4, sizeof(float));
                                    mesh.texCoords.push_back(uv);
                                }
                            }
                        }
#if HAS_ZLIB
                    } else if (encoding == 1 && propPos + 13 + byteLen <= data.size()) {
                        std::vector<char> decompressed(arrCount * 4);
                        uLongf decompressedSize = static_cast<uLongf>(decompressed.size());
                        int zret = uncompress(reinterpret_cast<Bytef*>(decompressed.data()), &decompressedSize,
                                              reinterpret_cast<const Bytef*>(arrData), static_cast<uLong>(byteLen));
                        if (zret != Z_OK) {
                            qWarning() << "FBX: ZLIB decompression failed for" << name.c_str() << "with error" << zret;
                            size_t skip = skipProperty(propPos);
                            if (skip == 0) break;
                            propPos += skip;
                            continue;
                        }
                        const char* raw = decompressed.data();
                        if (name == "Vertices") {
                            if (!m_scene.meshes.empty()) {
                                auto& mesh = m_scene.meshes.back();
                                uint32_t vertCount = arrCount / 3;
                                for (uint32_t i = 0; i < vertCount; i++) {
                                    Vec3 v;
                                    std::memcpy(&v.x, raw + i * 12, sizeof(float));
                                    std::memcpy(&v.y, raw + i * 12 + 4, sizeof(float));
                                    std::memcpy(&v.z, raw + i * 12 + 8, sizeof(float));
                                    mesh.vertices.push_back(v);
                                }
                            }
                        } else if (name == "PolygonVertexIndex") {
                            if (!m_scene.meshes.empty()) {
                                auto& mesh = m_scene.meshes.back();
                                for (uint32_t i = 0; i < arrCount; i++) {
                                    int32_t idx;
                                    std::memcpy(&idx, raw + i * 4, sizeof(int32_t));
                                    if (idx < 0) mesh.indices.push_back(static_cast<uint32_t>(~idx));
                                    else mesh.indices.push_back(static_cast<uint32_t>(idx));
                                }
                            }
                        } else if (name == "Normals") {
                            if (!m_scene.meshes.empty()) {
                                auto& mesh = m_scene.meshes.back();
                                uint32_t normCount = arrCount / 3;
                                for (uint32_t i = 0; i < normCount; i++) {
                                    Vec3 n;
                                    std::memcpy(&n.x, raw + i * 12, sizeof(float));
                                    std::memcpy(&n.y, raw + i * 12 + 4, sizeof(float));
                                    std::memcpy(&n.z, raw + i * 12 + 8, sizeof(float));
                                    mesh.normals.push_back(n);
                                }
                            }
                        } else if (name == "UV" || name == "UVSetData") {
                            if (!m_scene.meshes.empty()) {
                                auto& mesh = m_scene.meshes.back();
                                uint32_t uvCount = arrCount / 2;
                                for (uint32_t i = 0; i < uvCount; i++) {
                                    Vec2 uv;
                                    std::memcpy(&uv.x, raw + i * 8, sizeof(float));
                                    std::memcpy(&uv.y, raw + i * 8 + 4, sizeof(float));
                                    mesh.texCoords.push_back(uv);
                                }
                            }
                        }
#else
                    } else if (encoding == 1 && propPos + 13 + byteLen <= data.size()) {
                        qWarning() << "FBX: ZLIB compressed data encountered but zlib is not available";
                        size_t skip = skipProperty(propPos);
                        if (skip == 0) break;
                        propPos += skip;
                        continue;
#endif
                    }
                    break; // Found the array, done with this node
                }

                size_t skip = skipProperty(propPos);
                if (skip == 0) break;
                propPos += skip;
            }
        }

        // Also look for Name property inside Geometry nodes for mesh naming
        if (name == "Vertices" && !m_scene.meshes.empty()) {
            // Geometry name is typically in the parent node, handled elsewhere
        }

        offset = static_cast<size_t>(endOffset);
    }

    return true;
}

void FBXParser::parseGeometry(const std::string& section)
{
    FBXMesh mesh;

    std::istringstream stream(section);
    std::string line;

    while (std::getline(stream, line)) {
        line.erase(0, line.find_first_not_of(" \t"));

        if (line.find("Name: ") == 0) {
            size_t start = line.find('"');
            size_t end = line.find('"', start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                mesh.name = line.substr(start + 1, end - start - 1);
            }
        } else if (line.find("Vertices: ") == 0) {
            size_t arrStart = line.find('{');
            size_t arrEnd = line.find('}');
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arr = line.substr(arrStart + 1, arrEnd - arrStart - 1);
                std::istringstream arrStream(arr);
                std::string val;
                std::vector<float> verts;
                while (std::getline(arrStream, val, ',')) {
                    val.erase(0, val.find_first_not_of(" \t"));
                    if (!val.empty()) verts.push_back(static_cast<float>(std::stod(val)));
                }
                for (size_t i = 0; i + 2 < verts.size(); i += 3) {
                    mesh.vertices.push_back(Vec3(verts[i], verts[i + 1], verts[i + 2]));
                }
            }
        } else if (line.find("PolygonVertexIndex: ") == 0) {
            size_t arrStart = line.find('{');
            size_t arrEnd = line.find('}');
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arr = line.substr(arrStart + 1, arrEnd - arrStart - 1);
                std::istringstream arrStream(arr);
                std::string val;
                while (std::getline(arrStream, val, ',')) {
                    val.erase(0, val.find_first_not_of(" \t"));
                    if (!val.empty()) {
                        int32_t idx = std::stoi(val);
                        if (idx < 0) idx = ~idx;
                        mesh.indices.push_back(static_cast<uint32_t>(idx));
                    }
                }
            }
        } else if (line.find("Normals: ") == 0) {
            size_t arrStart = line.find('{');
            size_t arrEnd = line.find('}');
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arr = line.substr(arrStart + 1, arrEnd - arrStart - 1);
                std::istringstream arrStream(arr);
                std::string val;
                std::vector<float> norms;
                while (std::getline(arrStream, val, ',')) {
                    val.erase(0, val.find_first_not_of(" \t"));
                    if (!val.empty()) norms.push_back(static_cast<float>(std::stod(val)));
                }
                for (size_t i = 0; i + 2 < norms.size(); i += 3) {
                    mesh.normals.push_back(Vec3(norms[i], norms[i + 1], norms[i + 2]));
                }
            }
        } else if (line.find("UV: ") == 0 || line.find("UVSetData: ") == 0) {
            size_t arrStart = line.find('{');
            size_t arrEnd = line.find('}');
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arr = line.substr(arrStart + 1, arrEnd - arrStart - 1);
                std::istringstream arrStream(arr);
                std::string val;
                std::vector<float> uvs;
                while (std::getline(arrStream, val, ',')) {
                    val.erase(0, val.find_first_not_of(" \t"));
                    if (!val.empty()) uvs.push_back(static_cast<float>(std::stod(val)));
                }
                for (size_t i = 0; i + 1 < uvs.size(); i += 2) {
                    mesh.texCoords.push_back(Vec2(uvs[i], uvs[i + 1]));
                }
            }
        }
    }

    if (!mesh.vertices.empty()) {
        m_scene.meshes.push_back(mesh);
    }
}

void FBXParser::parseMaterials(const std::string& section)
{
    FBXMaterial mat;

    std::istringstream stream(section);
    std::string line;

    while (std::getline(stream, line)) {
        line.erase(0, line.find_first_not_of(" \t"));

        if (line.find("Name: ") == 0) {
            size_t start = line.find('"');
            size_t end = line.find('"', start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                mat.name = line.substr(start + 1, end - start - 1);
            }
        } else if (line.find("Diffuse: ") == 0) {
            size_t arrStart = line.find('{');
            size_t arrEnd = line.find('}');
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arr = line.substr(arrStart + 1, arrEnd - arrStart - 1);
                std::istringstream arrStream(arr);
                std::string val;
                std::vector<float> vals;
                while (std::getline(arrStream, val, ',')) {
                    val.erase(0, val.find_first_not_of(" \t"));
                    if (!val.empty()) vals.push_back(static_cast<float>(std::stod(val)));
                }
                if (vals.size() >= 3) {
                    mat.diffuseColor = Vec3(vals[0], vals[1], vals[2]);
                }
            }
        } else if (line.find("Specular: ") == 0) {
            size_t arrStart = line.find('{');
            size_t arrEnd = line.find('}');
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arr = line.substr(arrStart + 1, arrEnd - arrStart - 1);
                std::istringstream arrStream(arr);
                std::string val;
                std::vector<float> vals;
                while (std::getline(arrStream, val, ',')) {
                    val.erase(0, val.find_first_not_of(" \t"));
                    if (!val.empty()) vals.push_back(static_cast<float>(std::stod(val)));
                }
                if (vals.size() >= 3) {
                    mat.specularColor = Vec3(vals[0], vals[1], vals[2]);
                }
            }
        } else if (line.find("Ambient: ") == 0) {
            size_t arrStart = line.find('{');
            size_t arrEnd = line.find('}');
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arr = line.substr(arrStart + 1, arrEnd - arrStart - 1);
                std::istringstream arrStream(arr);
                std::string val;
                std::vector<float> vals;
                while (std::getline(arrStream, val, ',')) {
                    val.erase(0, val.find_first_not_of(" \t"));
                    if (!val.empty()) vals.push_back(static_cast<float>(std::stod(val)));
                }
                if (vals.size() >= 3) {
                    mat.ambientColor = Vec3(vals[0], vals[1], vals[2]);
                }
            }
        } else if (line.find("TransparencyFactor: ") == 0) {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string val = line.substr(colonPos + 1);
                val.erase(0, val.find_first_not_of(" \t"));
                if (!val.empty()) mat.opacity = 1.0f - static_cast<float>(std::stod(val));
            }
        } else if (line.find("ShadingModel: ") == 0) {
            size_t start = line.find('"');
            size_t end = line.find('"', start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                std::string model = line.substr(start + 1, end - start - 1);
                if (model == "phong") mat.shininess = 0.5f;
            }
        }
    }

    if (!mat.name.empty()) {
        m_scene.materials[mat.name] = mat;
    }
}

void FBXParser::parseConnections(const std::string& section)
{
    std::istringstream stream(section);
    std::string line;

    while (std::getline(stream, line)) {
        line.erase(0, line.find_first_not_of(" \t"));

        if (line.find("C: ") == 0) {
            size_t firstQuote = line.find('"');
            size_t secondQuote = line.find('"', firstQuote + 1);
            size_t thirdQuote = line.find('"', secondQuote + 1);
            size_t fourthQuote = line.find('"', thirdQuote + 1);

            if (firstQuote != std::string::npos && fourthQuote != std::string::npos) {
                std::string relType = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                std::string childId = line.substr(secondQuote + 1, thirdQuote - secondQuote - 1);
                std::string parentId = line.substr(thirdQuote + 1, fourthQuote - thirdQuote - 1);

                if (relType == "OO") {
                    for (auto& mesh : m_scene.meshes) {
                        if (mesh.name == childId) {
                            mesh.materialName = parentId;
                        }
                    }
                }
            }
        }
    }
}

void FBXParser::parseSkeleton(const std::string& section)
{
    std::istringstream stream(section);
    std::string line;

    FBXBone bone;

    while (std::getline(stream, line)) {
        line.erase(0, line.find_first_not_of(" \t"));

        if (line.find("Name: ") == 0) {
            size_t firstQuote = line.find('"');
            size_t secondQuote = line.find('"', firstQuote + 1);
            if (firstQuote != std::string::npos && secondQuote != std::string::npos) {
                bone.name = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
            }
        } else if (line.find("Lcl Translation") != std::string::npos) {
            size_t firstQuote = line.find('(');
            size_t lastQuote = line.find(')');
            if (firstQuote != std::string::npos && lastQuote != std::string::npos) {
                std::string vals = line.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                std::istringstream vss(vals);
                std::string token;
                std::vector<float> components;
                while (std::getline(vss, token, ',')) {
                    components.push_back(std::stof(token));
                }
                if (components.size() >= 3) {
                    bone.position = {components[0], components[1], components[2]};
                }
            }
        } else if (line.find("Lcl Rotation") != std::string::npos) {
            size_t firstQuote = line.find('(');
            size_t lastQuote = line.find(')');
            if (firstQuote != std::string::npos && lastQuote != std::string::npos) {
                std::string vals = line.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                std::istringstream vss(vals);
                std::string token;
                std::vector<float> components;
                while (std::getline(vss, token, ',')) {
                    components.push_back(std::stof(token));
                }
                if (components.size() >= 3) {
                    // Convert Euler angles to quaternion (simplified)
                    float cx = cosf(components[0] * 0.5f * 3.14159265f / 180.0f);
                    float sx = sinf(components[0] * 0.5f * 3.14159265f / 180.0f);
                    float cy = cosf(components[1] * 0.5f * 3.14159265f / 180.0f);
                    float sy = sinf(components[1] * 0.5f * 3.14159265f / 180.0f);
                    float cz = cosf(components[2] * 0.5f * 3.14159265f / 180.0f);
                    float sz = sinf(components[2] * 0.5f * 3.14159265f / 180.0f);
                    bone.rotation.x = sx * cy * cz - cx * sy * sz;
                    bone.rotation.y = cx * sy * cz + sx * cy * sz;
                    bone.rotation.z = cx * cy * sz - sx * sy * cz;
                    bone.rotation.w = cx * cy * cz + sx * sy * sz;
                }
            }
        }
    }

    if (!bone.name.empty()) {
        bone.parentIndex = (int)m_scene.skeleton.bones.size() > 0 ? (int)m_scene.skeleton.bones.size() - 1 : -1;
        m_scene.skeleton.bones.push_back(bone);
        qDebug() << "Parsed bone:" << QString::fromStdString(bone.name);
    }
}

void FBXParser::parseAnimationStack(const std::string& section)
{
    std::istringstream stream(section);
    std::string line;

    FBXAnimation anim;
    FBXAnimationChannel currentChannel;
    bool inCurve = false;

    while (std::getline(stream, line)) {
        line.erase(0, line.find_first_not_of(" \t"));

        if (line.find("Name: ") == 0) {
            size_t firstQuote = line.find('"');
            size_t secondQuote = line.find('"', firstQuote + 1);
            if (firstQuote != std::string::npos && secondQuote != std::string::npos) {
                std::string name = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                if (anim.name.empty()) {
                    anim.name = name;
                } else {
                    currentChannel.boneName = name;
                }
            }
        } else if (line.find("AnimationCurve") != std::string::npos) {
            inCurve = true;
        } else if (line.find("KeyCount") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                // Read key count - we'll parse actual keyframe data below
            }
        } else if (line.find("KeyTime") != std::string::npos) {
            size_t firstQuote = line.find('(');
            size_t lastQuote = line.find(')');
            if (firstQuote != std::string::npos && lastQuote != std::string::npos) {
                std::string vals = line.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                std::istringstream vss(vals);
                std::string token;
                std::vector<int64_t> times;
                while (std::getline(vss, token, ',')) {
                    try {
                        times.push_back(std::stoll(token));
                    } catch (...) {
                        qWarning() << "FBXParser: Failed to parse animation time token:" << QString::fromStdString(token);
                    }
                }

                // Also try to read KeyValueFloat/Vector
                std::string keyValLine;
                while (std::getline(stream, keyValLine)) {
                    keyValLine.erase(0, keyValLine.find_first_not_of(" \t"));
                    if (keyValLine.find("KeyValueFloat") != std::string::npos ||
                        keyValLine.find("KeyValueVector") != std::string::npos) {
                        size_t lp = keyValLine.find('(');
                        size_t rp = keyValLine.find(')');
                        if (lp != std::string::npos && rp != std::string::npos) {
                            std::string kvals = keyValLine.substr(lp + 1, rp - lp - 1);
                            std::istringstream kvss(kvals);
                            std::string ktoken;
                            std::vector<float> values;
                            while (std::getline(kvss, ktoken, ',')) {
                                try {
                                    values.push_back(std::stof(ktoken));
                                } catch (...) {
                                    qWarning() << "FBXParser: Failed to parse animation value token:" << QString::fromStdString(ktoken);
                                }
                            }

                            for (size_t i = 0; i < times.size() && i < values.size(); ++i) {
                                FBXKeyframe kf;
                                kf.time = (float)times[i] / 46186158000.0f; // FBX ticks to seconds
                                kf.translation = {values[i], 0, 0};
                                currentChannel.keyframes.push_back(kf);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    if (!currentChannel.boneName.empty() && !currentChannel.keyframes.empty()) {
        anim.channels.push_back(currentChannel);
    }

    if (!anim.name.empty()) {
        m_scene.animations.push_back(anim);
        qDebug() << "Parsed animation:" << QString::fromStdString(anim.name)
                 << "with" << anim.channels.size() << "channels";
    }
}

// ─── FBX::Parser implementation ──────────────────────────────────────────────

static QString s_lastFBXError;

bool FBX::Parser::read(const QString& filePath, FBX::File& outFile) {
    FBXParser parser;
    if (!parser.loadFromFile(filePath.toStdString())) {
        s_lastFBXError = QStringLiteral("Failed to load FBX file: %1").arg(filePath);
        return false;
    }

    const auto& scene = parser.scene();
    outFile.version = QStringLiteral("FBX 7400");
    outFile.scene.name = QString::fromStdString(scene.sourcePath);

    for (const auto& mesh : scene.meshes) {
        FBX::MeshData md;
        md.name = QString::fromStdString(mesh.name);
        size_t triCount = mesh.indices.size() / 3;
        for (size_t i = 0; i < triCount; ++i) {
            FBX::Polygon poly;
            for (int j = 0; j < 3; ++j) {
                uint32_t idx = mesh.indices[i * 3 + j];
                poly.indices.append(static_cast<int>(idx));

                FBX::Vertex vert;
                if (idx < mesh.vertices.size())
                    vert.position = {mesh.vertices[idx].x, mesh.vertices[idx].y, mesh.vertices[idx].z};
                if (idx < mesh.normals.size())
                    vert.normal = {mesh.normals[idx].x, mesh.normals[idx].y, mesh.normals[idx].z};
                if (idx < mesh.texCoords.size())
                    vert.uv = {mesh.texCoords[idx].x, mesh.texCoords[idx].y};
                // Only add unique vertices per polygon (FBX::Vertex is per-polygon)
                md.vertices.append(vert);
                // Remap indices for per-polygon vertex list
                poly.indices.back() = static_cast<int>(md.vertices.size()) - 1;
            }
            md.polygons.append(poly);
        }
        outFile.scene.meshes.append(md);
    }
    return true;
}

QString FBX::Parser::lastError() {
    return s_lastFBXError;
}

} // namespace ks
