// KN5Types.h — Assetto Corsa KN5 format type definitions
// Shared between KN5Parser, FBXExporter, and the modelling editor.
#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include <QByteArray>
#include <QMatrix4x4>
#include <cmath>

namespace KN5Parser {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
// Real Assetto Corsa KN5 magic: the 6 ASCII bytes "sc6969". KN5_MAGIC is the
// first four of those bytes read as a little-endian uint32 ('s','c','6','9'),
// i.e. what quint32-based checks see at offset 0. Verified against ~2770 real
// AC .kn5 files and actools' reference implementation (Kn5Writer.cs).
static constexpr quint32 KN5_MAGIC        = 0x39366373;
static constexpr char    KN5_MAGIC_BYTES[7] = "sc6969";
static constexpr quint32 KN5_VERSION      = 5; // version emitted by write()
static constexpr quint32 KN5_VERSION_MIN  = 4; // minimum supported (axis.kn5)
static constexpr quint32 KN5_VERSION_MAX  = 6; // latest seen in the wild

// AC node naming convention prefixes (for validation)
static const QStringList AC_LOD_PREFIXES   = {"LOD_A","LOD_B","LOD_C","LOD_D"};
static const QStringList AC_WHEEL_NODES    = {"WHEEL_LF","WHEEL_RF","WHEEL_LR","WHEEL_RR"};
static const QStringList AC_COLLIDER_NODES = {"AC_POBJECT","AC_WHEEL_","AC_OFF"};

// ---------------------------------------------------------------------------
// Vertex attribute types (matches FMOD AC kN5 vertex layout)
// ---------------------------------------------------------------------------
enum class AttributeType : quint32 {
    Position   = 0,  // vec3
    Normal     = 1,  // vec3
    TexCoord0  = 2,  // vec2 — primary UV
    TexCoord1  = 3,  // vec2 — UV2 (lightmap / damage)
    Tangent    = 4,  // vec3
    Bitangent  = 5,  // vec3
    BoneIndex  = 6,  // vec4 — bone indices stored as FLOATS in real kn5 files
                     // (actools Kn5Basic.cs: "Yes! Those are floats!")
    BoneWeight = 7,  // vec4
    Color      = 8,  // vec4 (RGBA)
};

struct VertexLayout {
    QVector<QPair<AttributeType, quint8>> attributes; // (type, offset)
    quint32 vertexSize = 0;

    bool has(AttributeType a) const {
        for (const auto& p : attributes)
            if (p.first == a) return true;
        return false;
    }
    int offsetOf(AttributeType a) const {
        for (const auto& p : attributes)
            if (p.first == a) return p.second;
        return -1;
    }
};

// ---------------------------------------------------------------------------
// Sub-mesh (one material per sub-mesh)
// ---------------------------------------------------------------------------
struct SubMesh {
    quint32 materialIndex = 0;
    quint32 vertexOffset  = 0;
    quint32 vertexCount   = 0;
    quint32 indexOffset   = 0;
    quint32 indexCount    = 0;
    struct Vec3 { float x=0,y=0,z=0; };
    Vec3 boundingMin, boundingMax;
};

// ---------------------------------------------------------------------------
// Bone (for skinned meshes) — declared before Mesh, which stores per-node
// bones (the real format keeps a bone list on every SkinnedMesh node).
// ---------------------------------------------------------------------------
struct Bone {
    QString name;
    int     parentIndex = -1; // not part of the real format; -1 when parsed
    float   matrix[16] = {};  // bind pose matrix (row-major, row-vector)
};

// ---------------------------------------------------------------------------
// World matrix (row-vector convention: p' = p * M, translation at [12..14])
// ---------------------------------------------------------------------------
struct Matrix4x4 {
    float m[4][4] = {};
    Matrix4x4() { m[0][0]=m[1][1]=m[2][2]=m[3][3]=1.0f; }
    QMatrix4x4 toQMatrix() const {
        return QMatrix4x4(m[0][0],m[0][1],m[0][2],m[0][3],
                          m[1][0],m[1][1],m[1][2],m[1][3],
                          m[2][0],m[2][1],m[2][2],m[2][3],
                          m[3][0],m[3][1],m[3][2],m[3][3]);
    }
};

// ---------------------------------------------------------------------------
// Mesh node
// ---------------------------------------------------------------------------
struct Mesh {
    QString   name;
    quint32   nodeIndex    = 0;
    bool      castShadows  = true;
    bool      isVisible    = true;
    bool      isTransparent = false;
    bool      isSkinnedMesh = false;

    enum class MaterialType : quint32 {
        Standard   = 0,
        AlphaMask  = 1,
        Transparent = 2,
        Additive   = 3
    } materialType = MaterialType::Standard;

    struct Vec3 { float x=0,y=0,z=0; };
    Vec3    boundingMin, boundingMax;
    float   boundingRadius = 0.0f;

    VertexLayout         vertexLayout;
    QByteArray           vertexData;
    QByteArray           indexData;
    QVector<SubMesh>     subMeshes;

    // --- Real-format fields (round-trip fidelity; defaults match files we
    // --- construct in-editor). See src/engine/FileFormat/Kn5Reader.h.
    quint32 materialIndex = 0;   // mesh tail "materialId" -> materials[]
    quint32 layer         = 0;
    float   lodIn         = 0.0f;
    float   lodOut        = 1000.0f;
    bool    isRenderable  = true;
    bool    nodeActive    = true;   // node "active" byte (hierarchy, != isVisible)
    Matrix4x4 worldMatrix;          // accumulated world transform (incl. root)
    QVector<Bone> bones;            // skinned: per-node bone list (real format)

    // Decoded vertex helpers
    QVector<QVector3D>   positions;
    QVector<QVector3D>   normals;
    QVector<QVector3D>   tangents;
    QVector<QVector3D>   bitangents;
    QVector<QVector2D>   uv0;          // primary UV channel
    QVector<QVector2D>   uv1;          // UV2 (damage maps, lightmaps)
    QVector<QVector4D>   boneWeights;
    QVector<quint32>     boneIndices;  // packed 4×uint8

    quint32 getVertexCount() const {
        const quint32 stride = vertexLayout.vertexSize > 0 ? vertexLayout.vertexSize : 44;
        return stride ? (quint32)vertexData.size() / stride : 0;
    }
    quint32 getTriangleCount() const {
        return (quint32)indexData.size() / 6; // quint16 indices, 3 per tri
    }

    // Decode raw vertexData into typed arrays
    void decodeVertices();

    // Encode typed arrays back into vertexData (inverse of decodeVertices)
    void encodeVertices();
};

// ---------------------------------------------------------------------------
// Material
// ---------------------------------------------------------------------------
struct ShaderParam {
    QString name;
    float   value = 0.0f;
    float   valueA = 0.0f; // for vec2 params
};

struct Material {
    quint32 id = 0;
    QString name;
    QString shaderName;     // e.g. "ksPerPixelMultiMap_NMDetail"

    enum class Type : quint32 {
        Normal     = 0,
        AlphaMask  = 1,
        Transparent = 2,
        Additive   = 3,
        Invisible  = 4,
    } type = Type::Normal;

    QMap<QString, QString> properties;        // prop name → QString::number(ValueA)
    QMap<QString, QString> textureMapping;    // mapping name → texture name

    // --- Real-format fidelity (40-byte ValueA/B/C/D blobs and mapping slots
    // --- cannot be represented by the QMaps above). Populated by parse();
    // --- write() merges them with the QMaps (QMap wins for presence/value).
    struct RawProperty { QString name; QByteArray value; }; // value = 40 bytes
    QVector<RawProperty> rawProperties;
    struct RawMapping { QString name; qint32 slot = 0; QString texture; };
    QVector<RawMapping> rawMappings;

    // Real blend/depth bytes: -1 = not parsed (write() derives blendMode from
    // type/alphaBlending and uses depthMode 0).
    qint32 blendMode = -1;   // 0 Opaque | 1 AlphaBlend | 2 AlphaToCoverage
    qint32 depthMode = -1;   // 0 Normal | 1 NoWrite | 2 Off

    // Parsed shader parameters (for editor display)
    QVector<ShaderParam>   shaderParams;
    bool     alphaBlending  = false;
    bool     alphaTesting   = false;
    float    alphaRef       = 0.5f;
    bool     depthTest      = true;
    bool     depthWrite     = true;
    bool     useDetailMap   = false;
    bool     useNormalMap   = false;
    float    detailUVMultiplier = 1.0f;
};

// ---------------------------------------------------------------------------
// Texture
// ---------------------------------------------------------------------------
struct Texture {
    QString    name;
    quint32    width  = 0;
    quint32    height = 0;
    quint32    format = 0;   // DDS format
    quint32    mipmapCount = 0;
    QByteArray data;        // raw DDS data
};

// ---------------------------------------------------------------------------
// LOD group
// ---------------------------------------------------------------------------
struct LODGroup {
    QString name;       // "LOD_A", "LOD_B", "LOD_C", "LOD_D"
    float   distance = 0.0f;  // switch-in distance (metres)
    QVector<int> meshIndices;
};

// ---------------------------------------------------------------------------
// File header
// ---------------------------------------------------------------------------
struct FileHeader {
    quint32 magic             = KN5_MAGIC;
    quint32 version           = KN5_VERSION;
    quint32 flags             = 0;
    quint32 textureCount      = 0;
    quint32 materialCount     = 0;
    quint32 nodeCount         = 0;
    quint32 headerSize        = 0;
    quint32 nodeOffset        = 0;
    quint32 textureOffset     = 0;
    quint32 vertexBufferOffset = 0;
    quint32 indexBufferOffset  = 0;
    quint32 vertexBufferSize   = 0;
    quint32 indexBufferSize    = 0;
};

// ---------------------------------------------------------------------------
// KN5 node naming validation helpers
// ---------------------------------------------------------------------------
struct NodeNameWarning {
    QString nodeName;
    QString message;
    enum Level { Info, Warning, Error } level = Warning;
};

// ---------------------------------------------------------------------------
// Top-level KN5 file
// ---------------------------------------------------------------------------
struct KN5File {
    QString             filePath;
    FileHeader          header;
    QVector<Texture>    textures;
    QStringList         textureNames;
    QVector<Material>   materials;
    QVector<Mesh>       meshes;
    QVector<Bone>       bones;
    QVector<LODGroup>   lodGroups;
    Matrix4x4           worldMatrix;

    // Validate AC naming convention, populate lodGroups
    QVector<NodeNameWarning> validateNodeNames() const;
    void extractLODGroups();

    bool isValid() const { return header.magic == KN5_MAGIC && !meshes.isEmpty(); }
};

// ---------------------------------------------------------------------------
// KN5File::validateNodeNames() — checks AC naming convention
// ---------------------------------------------------------------------------
inline QVector<NodeNameWarning> KN5File::validateNodeNames() const {
    QVector<NodeNameWarning> warnings;
    bool hasLodA = false;
    QStringList meshNames;
    for (const auto& mesh : meshes) {
        meshNames << mesh.name;
        if (mesh.name.startsWith("LOD_A")) hasLodA = true;
        // Check for duplicate names
        if (meshNames.count(mesh.name) > 1) {
            warnings.append({mesh.name,
                "Duplicate node name — AC will only load the first occurrence",
                NodeNameWarning::Error});
        }
        // Check for spaces in names (AC doesn't support them)
        if (mesh.name.contains(' ')) {
            warnings.append({mesh.name,
                "Node name contains spaces — replace with underscores",
                NodeNameWarning::Error});
        }
    }
    if (!hasLodA && !meshes.isEmpty()) {
        warnings.append({"", "No LOD_A node found — AC requires at least LOD_A",
                         NodeNameWarning::Warning});
    }
    return warnings;
}

inline void KN5File::extractLODGroups() {
    lodGroups.clear();
    QStringList lodNames = {"LOD_A", "LOD_B", "LOD_C", "LOD_D"};
    float distances[]    = {0.0f, 50.0f, 100.0f, 250.0f};
    for (int li = 0; li < lodNames.size(); ++li) {
        LODGroup g;
        g.name     = lodNames[li];
        g.distance = distances[li];
        for (int mi = 0; mi < meshes.size(); ++mi) {
            if (meshes[mi].name.startsWith(lodNames[li]))
                g.meshIndices << mi;
        }
        if (!g.meshIndices.isEmpty())
            lodGroups << g;
    }
}

// ---------------------------------------------------------------------------
// Mesh::decodeVertices() — unpack raw vertexData into typed arrays
// ---------------------------------------------------------------------------
inline void Mesh::decodeVertices() {
    if (vertexData.isEmpty()) return;
    quint32 stride = vertexLayout.vertexSize > 0 ? vertexLayout.vertexSize
                    : (isSkinnedMesh ? 76u : 44u); // real kn5 vertex sizes
    quint32 count  = (quint32)vertexData.size() / stride;

    positions.resize(count);
    normals.resize(count);
    uv0.resize(count);

    bool hasUV1  = vertexLayout.has(AttributeType::TexCoord1);
    bool hasTan  = vertexLayout.has(AttributeType::Tangent);
    bool hasBiTan = vertexLayout.has(AttributeType::Bitangent);
    bool hasBone = vertexLayout.has(AttributeType::BoneWeight);

    if (hasUV1)  uv1.resize(count);
    if (hasTan)  tangents.resize(count);
    if (hasBiTan) bitangents.resize(count);
    if (hasBone) { boneWeights.resize(count); boneIndices.resize(count); }

    auto off = [&](AttributeType a) { return vertexLayout.offsetOf(a); };

    for (quint32 i = 0; i < count; ++i) {
        const char* v = vertexData.constData() + i * stride;
        auto rf = [&](int o) { float f; memcpy(&f, v+o, 4); return f; };

        int pOff  = off(AttributeType::Position);
        if (pOff >= 0) positions[i] = {rf(pOff), rf(pOff+4), rf(pOff+8)};

        int nOff  = off(AttributeType::Normal);
        if (nOff >= 0) normals[i] = {rf(nOff), rf(nOff+4), rf(nOff+8)};

        int t0Off = off(AttributeType::TexCoord0);
        if (t0Off >= 0) uv0[i] = {rf(t0Off), rf(t0Off+4)};

        if (hasUV1) {
            int t1Off = off(AttributeType::TexCoord1);
            if (t1Off >= 0) uv1[i] = {rf(t1Off), rf(t1Off+4)};
        }
        if (hasTan) {
            int tOff = off(AttributeType::Tangent);
            if (tOff >= 0) tangents[i] = {rf(tOff), rf(tOff+4), rf(tOff+8)};
        }
        if (hasBiTan) {
            int bOff = off(AttributeType::Bitangent);
            if (bOff >= 0) bitangents[i] = {rf(bOff), rf(bOff+4), rf(bOff+8)};
        }
        if (hasBone) {
            int wOff = off(AttributeType::BoneWeight);
            int iOff = off(AttributeType::BoneIndex);
            if (wOff >= 0) boneWeights[i] = {rf(wOff), rf(wOff+4), rf(wOff+8), rf(wOff+12)};
            // Real kn5 stores bone indices as floats ("Yes! Those are floats!")
            if (iOff >= 0) boneIndices[i] = (quint32)lroundf(rf(iOff));
        }
    }
}

inline void Mesh::encodeVertices() {
    // Build vertex layout from populated arrays
    vertexLayout.attributes.clear();
    vertexLayout.vertexSize = 0;
    using AT = AttributeType;

    auto addAttr = [&](AT type, quint8 size) {
        vertexLayout.attributes.append({type, vertexLayout.vertexSize});
        vertexLayout.vertexSize += size;
    };

    auto off = [&](AT a) { return vertexLayout.offsetOf(a); };

    addAttr(AT::Position, 12);
    addAttr(AT::Normal, 12);
    addAttr(AT::TexCoord0, 8);

    if (!uv1.isEmpty())     addAttr(AT::TexCoord1, 8);
    if (!tangents.isEmpty())   addAttr(AT::Tangent, 12);
    if (!bitangents.isEmpty()) addAttr(AT::Bitangent, 12);
    if (!boneWeights.isEmpty()) {
        addAttr(AT::BoneWeight, 16);
        addAttr(AT::BoneIndex, 4);
    }

    quint32 count = (quint32)positions.size();
    vertexData.resize(count * vertexLayout.vertexSize);

    auto wf = [&](int o, float f) { memcpy(vertexData.data() + o, &f, 4); };
    auto wub = [&](int o, quint32 v) {
        quint8 b[4] = { (quint8)(v & 0xFF), (quint8)((v>>8)&0xFF),
                        (quint8)((v>>16)&0xFF), (quint8)((v>>24)&0xFF) };
        memcpy(vertexData.data() + o, b, 4);
    };

    for (quint32 i = 0; i < count; ++i) {
        int base = i * vertexLayout.vertexSize;

        int pOff = off(AT::Position);
        if (pOff >= 0 && i < (quint32)positions.size()) {
            wf(base + pOff, positions[i].x());
            wf(base + pOff + 4, positions[i].y());
            wf(base + pOff + 8, positions[i].z());
        }

        int nOff = off(AT::Normal);
        if (nOff >= 0 && i < (quint32)normals.size()) {
            wf(base + nOff, normals[i].x());
            wf(base + nOff + 4, normals[i].y());
            wf(base + nOff + 8, normals[i].z());
        }

        int t0Off = off(AT::TexCoord0);
        if (t0Off >= 0 && i < (quint32)uv0.size()) {
            wf(base + t0Off, uv0[i].x());
            wf(base + t0Off + 4, uv0[i].y());
        }

        if (!uv1.isEmpty()) {
            int t1Off = off(AT::TexCoord1);
            if (t1Off >= 0 && i < (quint32)uv1.size()) {
                wf(base + t1Off, uv1[i].x());
                wf(base + t1Off + 4, uv1[i].y());
            }
        }
        if (!tangents.isEmpty()) {
            int tOff = off(AT::Tangent);
            if (tOff >= 0 && i < (quint32)tangents.size()) {
                wf(base + tOff, tangents[i].x());
                wf(base + tOff + 4, tangents[i].y());
                wf(base + tOff + 8, tangents[i].z());
            }
        }
        if (!bitangents.isEmpty()) {
            int bOff = off(AT::Bitangent);
            if (bOff >= 0 && i < (quint32)bitangents.size()) {
                wf(base + bOff, bitangents[i].x());
                wf(base + bOff + 4, bitangents[i].y());
                wf(base + bOff + 8, bitangents[i].z());
            }
        }
        if (!boneWeights.isEmpty()) {
            int wOff = off(AT::BoneWeight);
            if (wOff >= 0 && i < (quint32)boneWeights.size()) {
                wf(base + wOff, boneWeights[i].x());
                wf(base + wOff + 4, boneWeights[i].y());
                wf(base + wOff + 8, boneWeights[i].z());
                wf(base + wOff + 12, boneWeights[i].w());
            }
            int iOff = off(AT::BoneIndex);
            // Bone indices are floats in the real kn5 format (see decodeVertices)
            if (iOff >= 0 && i < (quint32)boneIndices.size()) {
                wf(base + iOff, (float)boneIndices[i]);
            }
        }
    }
}


// ---------------------------------------------------------------------------
// MeshHelper
// ---------------------------------------------------------------------------
struct Vector3 {
    float x=0,y=0,z=0;
    Vector3() = default;
    Vector3(float x,float y,float z):x(x),y(y),z(z){}
    Vector3 operator+(const Vector3& o) const { return {x+o.x,y+o.y,z+o.z}; }
    Vector3 operator-(const Vector3& o) const { return {x-o.x,y-o.y,z-o.z}; }
    Vector3 operator*(float f) const { return {x*f,y*f,z*f}; }
    float length() const { return sqrtf(x*x+y*y+z*z); }
};

class MeshHelper {
public:
    static void computeBoundingBox(const KN5File&, Vector3& min, Vector3& max);
    static void computeBoundingSphere(const KN5File&, Vector3& center, float& radius);
    static quint32 getTotalTriangles(const KN5File&);
    static quint32 getTotalVertices(const KN5File&);
};

// ---------------------------------------------------------------------------
// KN5Parser class (Qt-based, full implementation in KN5Types.h (declaration) and KN5Parser.cpp (definition).
// ---------------------------------------------------------------------------
class KN5ParserImpl {
public:
    static KN5File  parse(const QString& filePath, QString* error = nullptr);
    static bool     write(const QString& filePath, const KN5File& kn5);
    static bool     isValid(const QString& filePath);
    static QString  lastError();

private:
    static QString  m_lastError;
};

} // namespace KN5Parser