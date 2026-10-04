#include "FbxReader.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

#include "Deflate.h"

namespace ks::engine::fileformat {
namespace {

constexpr std::uint8_t kMagic[23] = {
    'K', 'a', 'y', 'd', 'a', 'r', 'a', ' ', 'F', 'B', 'X', ' ',
    'B', 'i', 'n', 'a', 'r', 'y', ' ', ' ', 0x00, 0x1A, 0x00};

bool allZero(const std::uint8_t* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (p[i] != 0) return false;
    }
    return true;
}

// Property 1 of an object is `<class>\0\x01<className>`; the part before the
// NUL is what the file calls the object.
std::string objectName(const FbxNode& node) {
    if (node.properties.size() < 2 || node.properties[1].type != 'S') return node.name;
    const std::string& value = node.properties[1].text;
    const std::size_t nul = value.find('\0');
    return nul == std::string::npos ? value : value.substr(0, nul);
}

// A layer element (normals, uvs, ...) resolved down to "which entry belongs
// to this corner", honouring the mapping/reference pair FBX uses to describe
// how the arrays are indexed.
struct LayerRef {
    bool present = false;
    std::string mapping;
    std::string reference;
    const std::vector<double>* values = nullptr;
    const std::vector<std::int64_t>* indices = nullptr;
    std::size_t components = 0;
};

LayerRef readLayer(const FbxNode& geometry, const char* layerName,
                   const char* valueName, const char* indexName,
                   std::size_t components) {
    LayerRef layer;
    layer.components = components;
    const FbxNode* node = geometry.child(layerName);
    if (node == nullptr) return layer;
    layer.mapping = node->childString("MappingInformationType");
    layer.reference = node->childString("ReferenceInformationType");

    const FbxNode* values = node->child(valueName);
    if (values != nullptr && !values->properties.empty()) {
        layer.values = &values->properties[0].doubles;
    }
    const FbxNode* indices = node->child(indexName);
    if (indices != nullptr && !indices->properties.empty()) {
        layer.indices = &indices->properties[0].ints;
    }
    layer.present = layer.values != nullptr && !layer.values->empty();
    return layer;
}

// Returns the entry of `layer` that belongs to this corner, or -1 when the
// layer is absent or the corner points outside it.
int resolveLayer(const LayerRef& layer, std::size_t corner, int vertex, int polygon) {
    if (!layer.present) return -1;
    if (layer.mapping == "AllSame") return layer.values->empty() ? -1 : 0;

    std::size_t base = corner;
    if (layer.mapping == "ByVertice" || layer.mapping == "ByVertex") {
        base = static_cast<std::size_t>(vertex);
    } else if (layer.mapping == "ByPolygon") {
        base = static_cast<std::size_t>(polygon);
    }

    if (layer.reference == "IndexToDirect") {
        if (layer.indices == nullptr || base >= layer.indices->size()) return -1;
        const std::int64_t indexed = (*layer.indices)[base];
        if (indexed < 0) return -1;
        base = static_cast<std::size_t>(indexed);
    }

    const std::size_t elementCount = layer.values->size() / layer.components;
    if (base >= elementCount) return -1;
    return static_cast<int>(base);
}

} // namespace

struct FbxReader::Cursor {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;
    bool ok = true;

    bool need(std::size_t n) {
        if (!ok || pos + n > size) {
            ok = false;
            return false;
        }
        return true;
    }

    std::uint8_t u8() {
        if (!need(1)) return 0;
        return data[pos++];
    }
    std::uint16_t u16() {
        if (!need(2)) return 0;
        std::uint16_t value = 0;
        std::memcpy(&value, data + pos, sizeof(value));
        pos += 2;
        return value;
    }
    std::uint32_t u32() {
        if (!need(4)) return 0;
        std::uint32_t value = 0;
        std::memcpy(&value, data + pos, sizeof(value));
        pos += 4;
        return value;
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::int64_t i64() {
        if (!need(8)) return 0;
        std::int64_t value = 0;
        std::memcpy(&value, data + pos, sizeof(value));
        pos += 8;
        return value;
    }
    float f32() {
        if (!need(4)) return 0.0f;
        float value = 0.0f;
        std::memcpy(&value, data + pos, sizeof(value));
        pos += 4;
        return value;
    }
    double f64() {
        if (!need(8)) return 0.0;
        double value = 0.0;
        std::memcpy(&value, data + pos, sizeof(value));
        pos += 8;
        return value;
    }
};

bool FbxReader::fail(const char* message) {
    if (m_error.empty()) m_error = message;
    return false;
}

bool FbxReader::loadFromFile(const std::string& path) {
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream) {
        m_scene = ModelScene();
        m_roots.clear();
        m_error = "cannot open " + path;
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    return load(content, path);
}

bool FbxReader::load(std::string_view data, const std::string& sourcePath) {
    m_scene = ModelScene();
    m_roots.clear();
    m_error.clear();
    m_version = 0;
    m_objectCount = 0;

    if (data.size() < 27) {
        fail("file is too small to be an FBX");
        return false;
    }
    if (std::memcmp(data.data(), kMagic, sizeof(kMagic)) != 0) {
        fail("not a binary FBX file");
        return false;
    }

    Cursor reader;
    reader.data = reinterpret_cast<const std::uint8_t*>(data.data());
    reader.size = data.size();
    reader.pos = 23;
    m_version = reader.u32();
    if (!reader.ok) {
        fail("truncated FBX header");
        return false;
    }

    // The node walk stops at the first null record; the ~168 bytes that
    // follow it are footer, not nodes.
    while (reader.pos < reader.size) {
        FbxNode node;
        const NodeStatus status = parseNode(reader, node, 0);
        if (status == NodeStatus::Null) break;
        if (status == NodeStatus::Error) return false;
        m_roots.push_back(std::move(node));
    }

    buildScene();
    m_scene.sourcePath = sourcePath;
    return m_error.empty();
}

FbxReader::NodeStatus FbxReader::parseNode(Cursor& r, FbxNode& out, int depth) {
    if (depth > 64) {
        fail("FBX node nesting is too deep");
        return NodeStatus::Error;
    }
    if (!r.need(12)) {
        fail("truncated FBX node header");
        return NodeStatus::Error;
    }

    const std::size_t start = r.pos;
    const std::uint32_t endOffset = r.u32();
    const std::uint32_t numProperties = r.u32();
    const std::uint32_t propertyListLength = r.u32();
    if (endOffset == 0 && numProperties == 0 && propertyListLength == 0) {
        // Null record: the 12 header bytes plus the u8 name length.
        if (!r.need(1)) {
            fail("truncated FBX null record");
            return NodeStatus::Error;
        }
        r.pos += 1;
        return NodeStatus::Null;
    }
    if (endOffset > r.size || endOffset <= start) {
        fail("invalid FBX node end offset");
        return NodeStatus::Error;
    }

    const std::uint8_t nameLength = r.u8();
    if (!r.need(nameLength)) {
        fail("truncated FBX node name");
        return NodeStatus::Error;
    }
    out.name.assign(reinterpret_cast<const char*>(r.data + r.pos), nameLength);
    r.pos += nameLength;

    const std::size_t propertyStart = r.pos;
    for (std::uint32_t i = 0; i < numProperties; ++i) {
        FbxProperty property;
        if (!parseProperty(r, property)) return NodeStatus::Error;
        out.properties.push_back(std::move(property));
    }
    if (r.pos - propertyStart != propertyListLength) {
        fail("FBX property list length mismatch");
        return NodeStatus::Error;
    }

    while (r.pos < endOffset) {
        if (r.pos + 13 <= endOffset && allZero(r.data + r.pos, 13)) {
            r.pos += 13;
            break;
        }
        FbxNode childNode;
        const NodeStatus status = parseNode(r, childNode, depth + 1);
        if (status == NodeStatus::Error) return NodeStatus::Error;
        if (status == NodeStatus::Null) break;
        if (r.pos > endOffset) {
            fail("FBX child node overruns its parent");
            return NodeStatus::Error;
        }
        out.children.push_back(std::move(childNode));
    }
    if (r.pos > endOffset) {
        fail("FBX node overruns its declared end offset");
        return NodeStatus::Error;
    }
    r.pos = endOffset;
    return NodeStatus::Ok;
}

bool FbxReader::parseProperty(Cursor& r, FbxProperty& out) {
    if (!r.need(1)) {
        fail("truncated FBX property");
        return false;
    }
    const char type = static_cast<char>(r.u8());
    out.type = type;
    switch (type) {
    case 'Y':
        out.integerValue = r.i16();
        out.number = static_cast<double>(out.integerValue);
        break;
    case 'C':
        out.integerValue = r.u8();
        out.number = static_cast<double>(out.integerValue);
        break;
    case 'I':
        out.integerValue = r.i32();
        out.number = static_cast<double>(out.integerValue);
        break;
    case 'L':
        out.integerValue = r.i64();
        out.number = static_cast<double>(out.integerValue);
        break;
    case 'F':
        out.number = r.f32();
        break;
    case 'D':
        out.number = r.f64();
        break;
    case 'S': {
        const std::uint32_t length = r.u32();
        if (!r.need(length)) {
            fail("truncated FBX string property");
            return false;
        }
        out.text.assign(reinterpret_cast<const char*>(r.data + r.pos), length);
        r.pos += length;
        break;
    }
    case 'R': {
        const std::uint32_t length = r.u32();
        if (!r.need(length)) {
            fail("truncated FBX raw property");
            return false;
        }
        out.bytes.assign(r.data + r.pos, r.data + r.pos + length);
        r.pos += length;
        break;
    }
    case 'f':
    case 'd':
    case 'l':
    case 'i':
    case 'b':
        return readArray(r, type, out);
    default:
        return fail("unknown FBX property type");
    }
    return r.ok;
}

bool FbxReader::readArray(Cursor& r, char type, FbxProperty& out) {
    const std::uint32_t count = r.u32();
    const std::uint32_t encoding = r.u32();
    const std::uint32_t compressedLength = r.u32();
    if (!r.ok) {
        fail("truncated FBX array header");
        return false;
    }
    if (!r.need(compressedLength)) {
        fail("truncated FBX array payload");
        return false;
    }
    const std::uint8_t* payload = r.data + r.pos;
    r.pos += compressedLength;
    out.arrayCount = count;

    if (encoding == 0) {
        out.bytes.assign(payload, payload + compressedLength);
    } else if (encoding == 1) {
        std::string inflateError;
        if (!inflateZlib(std::string_view(reinterpret_cast<const char*>(payload), compressedLength),
                         out.bytes, &inflateError)) {
            const std::string message = "cannot inflate FBX array: " + inflateError;
            fail(message.c_str());
            return false;
        }
    } else {
        fail("unknown FBX array encoding");
        return false;
    }

    const std::size_t elementSize =
        (type == 'f' || type == 'i') ? 4u : (type == 'd' || type == 'l') ? 8u : 1u;
    if (out.bytes.size() != static_cast<std::size_t>(count) * elementSize) {
        fail("FBX array payload length does not match its element count");
        return false;
    }

    if (type == 'd' || type == 'f') {
        out.doubles.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (type == 'd') {
                double value = 0.0;
                std::memcpy(&value, out.bytes.data() + i * 8u, sizeof(value));
                out.doubles[i] = value;
            } else {
                float value = 0.0f;
                std::memcpy(&value, out.bytes.data() + i * 4u, sizeof(value));
                out.doubles[i] = value;
            }
        }
    } else {
        out.ints.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (type == 'i') {
                std::int32_t value = 0;
                std::memcpy(&value, out.bytes.data() + i * 4u, sizeof(value));
                out.ints[i] = value;
            } else if (type == 'l') {
                std::int64_t value = 0;
                std::memcpy(&value, out.bytes.data() + i * 8u, sizeof(value));
                out.ints[i] = value;
            } else {
                out.ints[i] = out.bytes[i];
            }
        }
    }
    // The decoded values now live in doubles/ints; the payload bytes are only
    // worth keeping for a raw 'R'-style array, not for a numeric one.
    out.bytes.clear();
    out.bytes.shrink_to_fit();
    return true;
}

bool FbxReader::extractGeometry(const FbxNode& geometry, ModelMesh& mesh) {
    const FbxNode* verticesNode = geometry.child("Vertices");
    const FbxNode* cornerNode = geometry.child("PolygonVertexIndex");
    if (verticesNode == nullptr || verticesNode->properties.empty() ||
        verticesNode->properties[0].doubles.empty()) {
        fail("FBX geometry without a Vertices array");
        return false;
    }
    if (cornerNode == nullptr || cornerNode->properties.empty()) {
        fail("FBX geometry without PolygonVertexIndex");
        return false;
    }
    if (cornerNode->properties[0].ints.empty()) {
        fail("FBX geometry with an empty PolygonVertexIndex");
        return false;
    }

    const std::vector<double>& positions = verticesNode->properties[0].doubles;
    if (positions.size() % 3 != 0) {
        fail("FBX vertex array is not a multiple of three");
        return false;
    }
    const std::int64_t vertexCount = static_cast<std::int64_t>(positions.size() / 3);

    mesh.name = objectName(geometry);
    const LayerRef normals = readLayer(geometry, "LayerElementNormal", "Normals", "NormalsIndex", 3);
    const LayerRef uvs = readLayer(geometry, "LayerElementUV", "UV", "UVIndex", 2);

    std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::uint32_t> weld;
    std::vector<std::uint32_t> loop;
    std::size_t cornerIndex = 0;
    std::size_t polygonIndex = 0;
    std::size_t faceCount = 0;
    std::size_t triangleCount = 0;

    auto pushCorner = [&](std::int64_t vertex) -> bool {
        if (vertex < 0 || vertex >= vertexCount) {
            fail("FBX vertex index out of range");
            return false;
        }
        const int normalSlot = resolveLayer(normals, cornerIndex, static_cast<int>(vertex),
                                            static_cast<int>(polygonIndex));
        const int uvSlot =
            resolveLayer(uvs, cornerIndex, static_cast<int>(vertex), static_cast<int>(polygonIndex));

        const std::tuple<std::int64_t, std::int64_t, std::int64_t> key{
            vertex, normalSlot >= 0 ? normalSlot : -1, uvSlot >= 0 ? uvSlot : -1};
        auto it = weld.find(key);
        if (it == weld.end()) {
            const auto local = static_cast<std::uint32_t>(mesh.vertices.size());

            Vec3 position;
            position.x = static_cast<float>(positions[static_cast<std::size_t>(vertex) * 3u]);
            position.y = static_cast<float>(positions[static_cast<std::size_t>(vertex) * 3u + 1u]);
            position.z = static_cast<float>(positions[static_cast<std::size_t>(vertex) * 3u + 2u]);
            mesh.vertices.push_back(position);

            Vec3 normal;
            if (normalSlot >= 0) {
                const std::size_t base = static_cast<std::size_t>(normalSlot) * 3u;
                normal.x = static_cast<float>(normals.values->at(base));
                normal.y = static_cast<float>(normals.values->at(base + 1u));
                normal.z = static_cast<float>(normals.values->at(base + 2u));
                mesh.hasNormals = true;
            }
            mesh.normals.push_back(normal);

            Vec2 texCoord;
            if (uvSlot >= 0) {
                const std::size_t base = static_cast<std::size_t>(uvSlot) * 2u;
                texCoord.x = static_cast<float>(uvs.values->at(base));
                texCoord.y = static_cast<float>(uvs.values->at(base + 1u));
                mesh.hasTexCoords = true;
            }
            mesh.texCoords.push_back(texCoord);

            it = weld.emplace(key, local).first;
        }
        loop.push_back(it->second);
        ++cornerIndex;
        return true;
    };

    auto closePolygon = [&]() {
        for (std::size_t i = 1; i + 1 < loop.size(); ++i) {
            mesh.indices.push_back(loop[0]);
            mesh.indices.push_back(loop[i]);
            mesh.indices.push_back(loop[i + 1]);
        }
        ++faceCount;
        if (loop.size() >= 3) triangleCount += loop.size() - 2;
        loop.clear();
        ++polygonIndex;
    };

    for (const std::int64_t raw : cornerNode->properties[0].ints) {
        if (raw > 0x7FFFFFFFll) {
            fail("FBX polygon index out of range");
            return false;
        }
        const bool last = raw < 0;
        const std::int64_t vertex = last ? ~raw : raw;
        if (!pushCorner(vertex)) return false;
        if (last) closePolygon();
    }
    if (!loop.empty()) closePolygon();

    if (faceCount == 0) {
        fail("FBX geometry produced no polygons");
        return false;
    }

    m_scene.positionCount += static_cast<std::size_t>(vertexCount);
    m_scene.faceCount += faceCount;
    m_scene.triangleCount += triangleCount;
    if (normals.present) m_scene.normalCount += normals.values->size() / 3u;
    if (uvs.present) m_scene.texCoordCount += uvs.values->size() / 2u;
    return true;
}

void FbxReader::buildScene() {
    const FbxNode* objects = nullptr;
    const FbxNode* connections = nullptr;
    for (const FbxNode& node : m_roots) {
        if (node.name == "Objects") {
            objects = &node;
        } else if (node.name == "Connections") {
            connections = &node;
        }
    }
    if (objects == nullptr) {
        fail("FBX has no Objects section");
        return;
    }
    m_objectCount = objects->children.size();

    struct Extracted {
        ModelMesh mesh;
        std::int64_t id = 0;
        std::int64_t modelId = 0;
        bool hasModel = false;
    };

    std::vector<Extracted> extracted;
    std::map<std::int64_t, std::size_t> geometryIndex;
    std::map<std::int64_t, std::string> modelNames;
    std::map<std::int64_t, std::string> materialNames;
    std::map<std::int64_t, std::vector<std::int64_t>> modelMaterials;
    std::map<std::int64_t, std::vector<std::int64_t>> geometryMaterials;

    for (const FbxNode& object : objects->children) {
        if (object.properties.empty()) continue;
        const std::int64_t id = object.properties[0].integerValue;
        if (object.name == "Geometry") {
            Extracted item;
            item.id = id;
            if (!extractGeometry(object, item.mesh)) return;
            geometryIndex.emplace(id, extracted.size());
            extracted.push_back(std::move(item));
        } else if (object.name == "Model") {
            modelNames.emplace(id, objectName(object));
        } else if (object.name == "Material") {
            ModelMaterial material;
            material.name = objectName(object);
            if (const FbxNode* properties = object.child("Properties70"); properties != nullptr) {
                for (const FbxNode& entry : properties->children) {
                    if (entry.name != "P" || entry.properties.size() < 5) continue;
                    const std::string key = entry.stringAt(0);
                    const std::size_t count = entry.properties.size();
                    auto value = [&](std::size_t i) { return static_cast<float>(entry.properties[i].number); };
                    if (key == "DiffuseColor" && count >= 7) {
                        material.kd = Vec3{value(4), value(5), value(6)};
                    } else if (key == "AmbientColor" && count >= 7) {
                        material.ka = Vec3{value(4), value(5), value(6)};
                    } else if (key == "SpecularColor" && count >= 7) {
                        material.ks = Vec3{value(4), value(5), value(6)};
                    } else if (key == "Shininess" && count >= 5) {
                        material.ns = value(4);
                    } else if (key == "TransparencyFactor" && count >= 5) {
                        material.d = 1.0f - value(4);
                    }
                }
            }
            const std::string key(material.name);
            m_scene.materials.emplace(key, std::move(material));
            materialNames.emplace(id, key);
        }
    }

    if (connections != nullptr) {
        for (const FbxNode& connection : connections->children) {
            if (connection.properties.size() < 3) continue;
            if (connection.properties[0].text != "OO") continue;
            const std::int64_t childId = connection.properties[1].integerValue;
            const std::int64_t parentId = connection.properties[2].integerValue;

            const auto material = materialNames.find(childId);
            if (material != materialNames.end()) {
                if (modelNames.find(parentId) != modelNames.end()) {
                    modelMaterials[parentId].push_back(childId);
                }
                if (geometryIndex.find(parentId) != geometryIndex.end()) {
                    geometryMaterials[parentId].push_back(childId);
                }
                continue;
            }
            // `OO` from a Geometry to its Model.
            const auto geometry = geometryIndex.find(childId);
            if (geometry != geometryIndex.end() && modelNames.find(parentId) != modelNames.end()) {
                extracted[geometry->second].modelId = parentId;
                extracted[geometry->second].hasModel = true;
            }
        }
    }

    for (Extracted& item : extracted) {
        // Prefer a material wired straight to the geometry, fall back to the
        // ones wired to the model it belongs to. Multi-material meshes pick
        // the first of the set; the per-polygon material layer is not read.
        const std::vector<std::int64_t>* list = nullptr;
        const auto fromGeometry = geometryMaterials.find(item.id);
        if (fromGeometry != geometryMaterials.end()) {
            list = &fromGeometry->second;
        } else if (item.hasModel) {
            const auto fromModel = modelMaterials.find(item.modelId);
            if (fromModel != modelMaterials.end()) list = &fromModel->second;
        }
        if (list != nullptr && !list->empty()) {
            const auto name = materialNames.find(list->front());
            if (name != materialNames.end()) item.mesh.materialName = name->second;
        }
        m_scene.meshes.push_back(std::move(item.mesh));
    }
}

} // namespace ks::engine::fileformat
