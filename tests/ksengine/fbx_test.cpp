// FBX importer test. The synthetic file is assembled byte by byte so every
// structural rule the reader depends on (u8 name length, absolute endOffset,
// null record, 12-byte array header with a zlib payload, Connections wiring)
// is exercised on data with a known answer; the real AC files then confirm the
// same rules hold for shipped content, including the raw `encoding == 0`
// arrays and the multi-material Model wiring.

#include "engine/FileFormat/FbxReader.h"
#include "KsTest.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

namespace ff = ks::engine::fileformat;

void checkClose(const char* what, double actual, double expected,
                double eps = 1e-5) {
    ks::test::report(std::fabs(actual - expected) <= eps, what, __FILE__, __LINE__);
}

using Bytes = std::vector<std::uint8_t>;

void append(Bytes& out, const std::uint8_t* data, std::size_t n) {
    out.insert(out.end(), data, data + n);
}

void append(Bytes& out, const Bytes& other) { out.insert(out.end(), other.begin(), other.end()); }

void append(Bytes& out, const std::string& s) {
    append(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

void putU32(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void putU64(Bytes& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu));
}

void putDouble(Bytes& out, double value) {
    std::uint8_t raw[sizeof(double)];
    std::memcpy(raw, &value, sizeof(value));
    append(out, raw, sizeof(raw));
}

std::uint32_t adler32(const Bytes& data) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t c : data) {
        a += c;
        if (a >= 65521u) a -= 65521u;
        b += a;
        if (b >= 65521u) b -= 65521u;
    }
    return (b << 16) | a;
}

// A zlib container whose payload is one or more stored DEFLATE blocks — no
// compressor in sight, but enough to drive the `encoding == 1` path.
Bytes zlibStored(const Bytes& payload) {
    Bytes out;
    out.push_back(0x78);
    out.push_back(0x01);
    std::size_t off = 0;
    if (payload.empty()) {
        out.push_back(1);
        for (int i = 0; i < 4; ++i) out.push_back(0);
    }
    while (off < payload.size()) {
        const std::size_t chunk =
            payload.size() - off > 65535 ? 65535 : payload.size() - off;
        const bool last = off + chunk >= payload.size();
        const auto len = static_cast<std::uint16_t>(chunk);
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(len & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(len >> 8));
        const auto nlen = static_cast<std::uint16_t>(~len);
        out.push_back(static_cast<std::uint8_t>(nlen & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(nlen >> 8));
        append(out, payload.data() + off, chunk);
        off += chunk;
    }
    const std::uint32_t ad = adler32(payload);
    out.push_back(static_cast<std::uint8_t>(ad >> 24));
    out.push_back(static_cast<std::uint8_t>(ad >> 16));
    out.push_back(static_cast<std::uint8_t>(ad >> 8));
    out.push_back(static_cast<std::uint8_t>(ad));
    return out;
}

struct Props {
    Bytes bytes;
    std::uint32_t count = 0;

    void add(const Bytes& property) {
        append(bytes, property);
        ++count;
    }
};

Bytes propLong(std::int64_t value) {
    Bytes out;
    out.push_back('L');
    putU64(out, static_cast<std::uint64_t>(value));
    return out;
}

Bytes propString(const std::string& value) {
    Bytes out;
    out.push_back('S');
    putU32(out, static_cast<std::uint32_t>(value.size()));
    append(out, value);
    return out;
}

Bytes propDoubleArray(const std::vector<double>& values, bool compress) {
    Bytes payload;
    for (const double v : values) putDouble(payload, v);
    const Bytes body = compress ? zlibStored(payload) : payload;
    Bytes out;
    out.push_back('d');
    putU32(out, static_cast<std::uint32_t>(values.size()));
    putU32(out, compress ? 1u : 0u);
    putU32(out, static_cast<std::uint32_t>(body.size()));
    append(out, body);
    return out;
}

Bytes propIntArray(const std::vector<std::int32_t>& values) {
    Bytes out;
    out.push_back('i');
    putU32(out, static_cast<std::uint32_t>(values.size()));
    putU32(out, 0u);
    putU32(out, static_cast<std::uint32_t>(values.size() * 4u));
    for (const std::int32_t v : values) putU32(out, static_cast<std::uint32_t>(v));
    return out;
}

struct Build {
    std::string name;
    Props props;
    std::vector<Build> children;
};

// Serializes bottom-up: a child's start offset is the parent's header plus
// properties plus everything already emitted for its older siblings, and the
// parent's endOffset is the running absolute position after the last child.
Bytes serialize(const Build& node, std::size_t start) {
    const std::size_t childrenStart =
        start + 12u + 1u + node.name.size() + node.props.bytes.size();

    Bytes children;
    std::size_t pos = childrenStart;
    for (const Build& child : node.children) {
        Bytes bytes = serialize(child, pos);
        pos += bytes.size();
        append(children, bytes);
    }

    Bytes out;
    putU32(out, static_cast<std::uint32_t>(pos));
    putU32(out, node.props.count);
    putU32(out, static_cast<std::uint32_t>(node.props.bytes.size()));
    out.push_back(static_cast<std::uint8_t>(node.name.size()));
    append(out, node.name);
    append(out, node.props.bytes);
    append(out, children);
    return out;
}

Build makeNode(const std::string& name, Props props, std::vector<Build> children = {}) {
    Build node;
    node.name = name;
    node.props = std::move(props);
    node.children = std::move(children);
    return node;
}

Build makeLeaf(const std::string& name, Props props) { return makeNode(name, std::move(props), {}); }

Build makeLayer(const std::string& name, const std::string& mapping,
                const std::string& reference, std::vector<Build> arrays) {
    Props props;
    std::vector<Build> children;
    children.push_back(makeLeaf("MappingInformationType", [&] {
        Props p;
        p.add(propString(mapping));
        return p;
    }()));
    children.push_back(makeLeaf("ReferenceInformationType", [&] {
        Props p;
        p.add(propString(reference));
        return p;
    }()));
    for (Build& array : arrays) children.push_back(std::move(array));
    return makeNode(name, std::move(props), std::move(children));
}

Build makeArrayLeaf(const std::string& name, Props props) {
    return makeLeaf(name, std::move(props));
}

Bytes propDouble(double value) {
    Bytes out;
    out.push_back('D');
    putDouble(out, value);
    return out;
}

// FBX object properties carry `<name>\0\x01<className>`; the reader splits on
// the NUL, so the synthetic file has to include it.
std::string objectClassName(const std::string& name, const std::string& className) {
    std::string value = name;
    value.push_back('\0');
    value.push_back('\x01');
    value += className;
    return value;
}

// One quad: 4 positions, two triangles, six corners.
// Normals are ByPolygonVertex/Direct (one per corner, zlib-compressed), uvs
// are ByPolygonVertex/IndexToDirect (4 entries + a 6-entry index array).
std::string buildSyntheticFbx() {
    const std::int64_t kGeometryId = 1;
    const std::int64_t kModelId = 2;
    const std::int64_t kMaterialId = 3;

    Build geometry = makeNode(
        "Geometry",
        [&] {
            Props p;
            p.add(propLong(kGeometryId));
            p.add(propString(objectClassName("quad", "Geometry")));
            p.add(propString("Mesh"));
            return p;
        }(),
        {makeArrayLeaf("Vertices", [&] {
             Props p;
             p.add(propDoubleArray({0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 0.0},
                                   false));
             return p;
         }()),
         makeArrayLeaf("PolygonVertexIndex", [&] {
             Props p;
             p.add(propIntArray({0, 1, -3, 0, 2, -4}));
             return p;
         }()),
         makeLayer("LayerElementNormal", "ByPolygonVertex", "Direct",
                   {makeArrayLeaf("Normals", [&] {
                       Props p;
                       p.add(propDoubleArray({0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0,
                                              1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0},
                                             true));
                       return p;
                   }())}),
         makeLayer("LayerElementUV", "ByPolygonVertex", "IndexToDirect",
                   {makeArrayLeaf("UV", [&] {
                       Props p;
                       p.add(propDoubleArray({0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0}, false));
                       return p;
                   }()),
                    makeArrayLeaf("UVIndex", [&] {
                        Props p;
                        p.add(propIntArray({0, 1, 2, 0, 2, 3}));
                        return p;
                    }())})});

    Build model = makeLeaf("Model", [&] {
        Props p;
        p.add(propLong(kModelId));
        p.add(propString(objectClassName("quad", "Model")));
        p.add(propString("Mesh"));
        return p;
    }());

    Build material = makeNode(
        "Material",
        [&] {
            Props p;
            p.add(propLong(kMaterialId));
            p.add(propString(objectClassName("TESTMAT", "Material")));
            p.add(propString(""));
            return p;
        }(),
        [&] {
            Build properties = makeLeaf("Properties70", Props{});
            Props diffuse;
            diffuse.add(propString("DiffuseColor"));
            diffuse.add(propString("Color"));
            diffuse.add(propString(""));
            diffuse.add(propString("A"));
            diffuse.add(propDouble(0.64));
            diffuse.add(propDouble(0.074426));
            diffuse.add(propDouble(0.09119));
            properties.children.push_back(makeLeaf("P", std::move(diffuse)));
            return std::vector<Build>{std::move(properties)};
        }());

    Build objects = makeNode("Objects", Props{}, {geometry, model, material});

    auto connection = [](std::int64_t child, std::int64_t parent) {
        Props p;
        p.add(propString("OO"));
        p.add(propLong(child));
        p.add(propLong(parent));
        return makeLeaf("C", std::move(p));
    };
    Build connections =
        makeNode("Connections", Props{},
                 {connection(kGeometryId, kModelId), connection(kMaterialId, kModelId)});

    const Bytes objectsBytes = serialize(objects, 27);
    const Bytes connectionsBytes = serialize(connections, 27 + objectsBytes.size());

    Bytes file;
    const char magic[] = "Kaydara FBX Binary  ";  // 20 characters plus the NUL
    append(file, reinterpret_cast<const std::uint8_t*>(magic), 21);
    file.push_back(0x1A);
    file.push_back(0x00);
    putU32(file, 7400);
    append(file, objectsBytes);
    append(file, connectionsBytes);
    for (int i = 0; i < 13; ++i) file.push_back(0);  // top-level null record

    return std::string(reinterpret_cast<const char*>(file.data()), file.size());
}

void testSynthetic() {
    const std::string data = buildSyntheticFbx();

    ff::FbxReader reader;
    const bool ok = reader.load(data, "synthetic.fbx");
    KS_CHECK(ok);
    if (!ok) std::printf("  synthetic: %s\n", reader.error().c_str());
    KS_CHECK(reader.error().empty());
    KS_CHECK(reader.version() == 7400);
    KS_CHECK(reader.objectCount() == 3);
    KS_CHECK(reader.roots().size() == 2);

    const ff::ModelScene& scene = reader.scene();
    KS_CHECK(scene.sourcePath == "synthetic.fbx");
    KS_CHECK(scene.meshes.size() == 1);
    KS_CHECK(scene.materials.size() == 1);
    if (scene.meshes.size() != 1) return;

    const ff::ModelMesh& mesh = scene.meshes.front();
    KS_CHECK(mesh.name == "quad");
    KS_CHECK(mesh.materialName == "TESTMAT");
    KS_CHECK(mesh.hasNormals);
    KS_CHECK(mesh.hasTexCoords);

    // Six corners, all distinct (vertex, normal slot, uv slot) triples.
    KS_CHECK(mesh.vertices.size() == 6);
    KS_CHECK(mesh.normals.size() == 6);
    KS_CHECK(mesh.texCoords.size() == 6);
    KS_CHECK(mesh.indices.size() == 6);
    KS_CHECK(scene.faceCount == 2);
    KS_CHECK(scene.triangleCount == 2);
    KS_CHECK(scene.positionCount == 4);

    const std::uint32_t expectedIndices[6] = {0, 1, 2, 3, 4, 5};
    for (std::size_t i = 0; i < 6 && i < mesh.indices.size(); ++i) {
        KS_CHECK(mesh.indices[i] == expectedIndices[i]);
    }

    // Corner 3 is the first corner of the second triangle and carries normal
    // index 3, i.e. (1,0,0), while the first three carry (0,0,1).
    if (mesh.normals.size() == 6 && mesh.vertices.size() == 6) {
        checkClose("normal0 z", mesh.normals[0].z, 1.0);
        checkClose("normal3 x", mesh.normals[3].x, 1.0);
        checkClose("normal3 z", mesh.normals[3].z, 0.0);
        checkClose("vertex3 x", mesh.vertices[3].x, 0.0);   // vertex 0 again
        checkClose("vertex4 x", mesh.vertices[4].x, 1.0);   // vertex 2 again
        checkClose("vertex5 x", mesh.vertices[5].x, 0.0);   // vertex 3
        checkClose("uv1 u", mesh.texCoords[1].x, 1.0);
        checkClose("uv3 u", mesh.texCoords[3].x, 0.0);
        checkClose("uv5 v", mesh.texCoords[5].y, 1.0);
    }

    const ff::ModelMaterial* material = scene.findMaterial("TESTMAT");
    KS_CHECK(material != nullptr);
    if (material != nullptr) {
        checkClose("kd r", material->kd.x, 0.64, 1e-5);
        checkClose("kd g", material->kd.y, 0.074426, 1e-5);
        checkClose("kd b", material->kd.z, 0.09119, 1e-5);
    }
    KS_CHECK(scene.findMesh("quad") == &mesh);
    KS_CHECK(scene.findMesh("missing") == nullptr);
}

void testFailures() {
    ff::FbxReader reader;

    KS_CHECK(!reader.load(""));
    KS_CHECK(reader.error() == "file is too small to be an FBX");

    KS_CHECK(!reader.load(std::string(64, 'x')));
    KS_CHECK(reader.error() == "not a binary FBX file");

    // An ASCII FBX starts with a comment, not the binary magic.
    KS_CHECK(!reader.load("; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n"));
    KS_CHECK(reader.error() == "not a binary FBX file");

    // A valid header over a node whose endOffset points past the buffer.
    std::string truncated = buildSyntheticFbx();
    truncated.resize(40);
    KS_CHECK(!reader.load(truncated));
    KS_CHECK(!reader.error().empty());
    KS_CHECK(reader.scene().meshes.empty());

    KS_CHECK(!reader.loadFromFile("no/such/directory/model.fbx"));
    KS_CHECK(reader.error().find("cannot open") != std::string::npos);
}

constexpr const char* kCarDir = "F:/SteamLibrary/steamapps/common/assettocorsa/content/cars/";

// 433708 B, FBX 7400: 2 geometries (all triangles), 4 materials all wired to
// the Models, 4 UV layers per geometry (only the first is read).
void testCoverspring() {
    const std::string path = std::string(kCarDir) + "asr_1992_dallara_192/animations/coverspring.fbx";
    if (!fs::exists(path)) return;

    ff::FbxReader reader;
    const bool ok = reader.loadFromFile(path);
    KS_CHECK(ok);
    if (!ok) std::printf("  coverspring: %s\n", reader.error().c_str());
    KS_CHECK(reader.error().empty());
    KS_CHECK(reader.version() == 7400);
    KS_CHECK(reader.objectCount() == 46);
    KS_CHECK(reader.roots().size() == 11);

    const ff::ModelScene& scene = reader.scene();
    KS_CHECK(scene.meshes.size() == 2);
    KS_CHECK(scene.materials.size() == 4);
    KS_CHECK(scene.positionCount == 3858);
    KS_CHECK(scene.faceCount == 5272);
    KS_CHECK(scene.triangleCount == 5272);
    KS_CHECK(scene.normalCount == 15816);
    KS_CHECK(scene.texCoordCount == 3858);

    const ff::ModelMesh* first = scene.findMesh("dal_cover_spring_b");
    KS_CHECK(first != nullptr);
    if (first != nullptr) {
        KS_CHECK(first->hasNormals);
        KS_CHECK(first->hasTexCoords);
        KS_CHECK(first->materialName == "WCCARBODY");
        KS_CHECK(first->triangleCount() == 2538);
        KS_CHECK(first->vertices.size() == first->normals.size());
        KS_CHECK(first->vertices.size() == first->texCoords.size());
        for (std::uint32_t index : first->indices) KS_CHECK(index < first->vertices.size());
    }
    KS_CHECK(scene.findMesh("dal_cover_spring") != nullptr);

    const ff::ModelMaterial* material = scene.findMaterial("D192_MET");
    KS_CHECK(material != nullptr);
    if (material != nullptr) checkClose("metal kd", material->kd.x, 0.8, 1e-5);
}

// 7282000 B, FBX 7200: 138 geometries. One of them stores its normal index
// and uv arrays uncompressed (`encoding == 0`), so both array paths are live.
void testModel() {
    const std::string path = std::string(kCarDir) + "pontiac_transam_87/unpacked/model.fbx";
    if (!fs::exists(path)) return;

    ff::FbxReader reader;
    const bool ok = reader.loadFromFile(path);
    KS_CHECK(ok);
    if (!ok) std::printf("  model.fbx: %s\n", reader.error().c_str());
    KS_CHECK(reader.error().empty());
    KS_CHECK(reader.version() == 7200);
    KS_CHECK(reader.objectCount() == 558);

    const ff::ModelScene& scene = reader.scene();
    KS_CHECK(scene.meshes.size() == 138);
    KS_CHECK(scene.materials.size() == 46);
    KS_CHECK(scene.positionCount == 148065);
    KS_CHECK(scene.faceCount == 147867);
    KS_CHECK(scene.triangleCount == 147867);

    std::size_t withMaterial = 0;
    std::size_t triangles = 0;
    for (const ff::ModelMesh& mesh : scene.meshes) {
        triangles += mesh.triangleCount();
        if (!mesh.materialName.empty()) ++withMaterial;
        KS_CHECK(mesh.hasNormals);
        KS_CHECK(mesh.hasTexCoords);
        KS_CHECK(mesh.vertices.size() == mesh.normals.size());
        KS_CHECK(mesh.vertices.size() == mesh.texCoords.size());
        KS_CHECK(mesh.indices.size() % 3 == 0);
        for (std::uint32_t index : mesh.indices) KS_CHECK(index < mesh.vertices.size());
    }
    KS_CHECK(triangles == 147867);
    KS_CHECK(withMaterial > 0);
}

// An animation FBX still has geometry and must parse like any other.
void testSteer() {
    const std::string path =
        std::string(kCarDir) + "rss_formula_americas_2020_oval/animations/steer.FBX";
    if (!fs::exists(path)) return;

    ff::FbxReader reader;
    const bool ok = reader.loadFromFile(path);
    KS_CHECK(ok);
    if (!ok) std::printf("  steer: %s\n", reader.error().c_str());
    KS_CHECK(reader.error().empty());

    const ff::ModelScene& scene = reader.scene();
    KS_CHECK(scene.meshes.size() == 3);
    KS_CHECK(scene.positionCount == 1481);
    KS_CHECK(scene.faceCount == 2610);
    KS_CHECK(scene.triangleCount == 2704);
}

}  // namespace

int main() {
    testSynthetic();
    testFailures();
    testCoverspring();
    testModel();
    testSteer();
    return KS_TEST_RESULT("fbx_test");
}
