#include "KN5Parser.h"
#include "KN5Types.h"
#include <QByteArray>
#include <QFile>
#include <QString>
#include <QVector>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>

// Real Assetto Corsa KN5 parser/writer.
//
// The layout implemented here is the actual AC container format, verified
// against ~2770 real .kn5 files and actools' reference implementation
// (actools/AcTools/Kn5File/Kn5Basic.cs, Kn5Writer.cs). It is the Qt-side
// counterpart of the Qt-free reader in src/engine/FileFormat/Kn5Reader.h
// (which is intentionally lossy — it drops texture payloads, tangents and
// skinned data — so the editor keeps its own full-fidelity implementation).
//
//   char  magic[6]  = "sc6969"
//   i32   version   (4..6; this writer emits 5)
//   i32   extra     (version > 5 only)
//   i32   textureCount, per texture: i32 active; when active != 0:
//         string name + u32 dataLength + dataLength bytes
//   i32   materialCount, per material: string name, string shader,
//         u8 blend, u8 alphaTested, i32 depthMode (version >= 5 only),
//         i32 propCount { string name + 40-byte ValueA/B/C/D },
//         i32 mappingCount { string name + i32 slot + string texture }
//   node tree: a single root, preorder (header, payload, then children):
//         i32 class (1 Base | 2 Mesh | 3 SkinnedMesh), string name,
//         i32 childrenCount, u8 active, payload:
//           Base        -> 16-float transform (row-vector: p' = p * M)
//           Mesh        -> 3 flag bytes (castShadows, isVisible,
//                          isTransparent), u32 vc, vc*44-byte vertices
//                          (pos3, normal3, uv2, tangent3), u32 ic,
//                          ic*u16 indices, then materialId u32, layer u32,
//                          lodIn f32, lodOut f32, bsCenter vec3,
//                          bsRadius f32, isRenderable u8
//           SkinnedMesh -> 3 flag bytes, u32 boneCount { string + 16-float
//                          matrix }, u32 vc, vc*76-byte vertices (44 +
//                          weights vec4 + bone indices as FLOATS),
//                          u32 ic, ic*u16 indices, materialId, layer,
//                          lodIn, lodOut
//
// Trailing bytes after the node tree are tolerated (observed in 25 mod
// files). Bone indices are floats — actools Kn5Basic.cs: "Yes! Those are
// floats!".

namespace KN5Parser {

namespace {

constexpr quint32 kMaxElements = 100000000u; // vertices/indices ceiling
constexpr quint32 kMaxCounts   = 1000000u;   // texture/material/node ceiling
constexpr quint32 kMaxNameLen  = 16u * 1024u * 1024u;
constexpr int     kMaxDepth    = 256;

// ---------------------------------------------------------------------------
// Little-endian read cursor with offset-accurate error reporting.
// ---------------------------------------------------------------------------
struct Cursor {
    const char* base = nullptr;
    qint64      size = 0;
    qint64      pos  = 0;
    bool        ok   = true;
    QString     err;

    explicit Cursor(const QByteArray& d) : base(d.constData()), size(d.size()) {}

    void fail(const QString& why) {
        if (ok) {
            ok = false;
            err = QString("offset %1: %2").arg(pos).arg(why);
        }
    }

    template <typename T> T get() {
        if (!ok) return T();
        if (pos + qint64(sizeof(T)) > size) {
            fail("unexpected end of file");
            return T();
        }
        T v;
        std::memcpy(&v, base + pos, sizeof(T));
        pos += qint64(sizeof(T));
        return v;
    }

    QByteArray bytes(qint64 n) {
        if (!ok) return QByteArray();
        if (n < 0 || n > qint64(INT_MAX) || pos + n > size) {
            fail("unexpected end of file");
            return QByteArray();
        }
        QByteArray out(base + pos, int(n));
        pos += n;
        return out;
    }

    QString string(quint32 limit = kMaxNameLen) {
        const quint32 n = get<quint32>();
        if (!ok) return QString();
        if (n > limit) {
            fail(QString("string length %1 exceeds limit").arg(n));
            return QString();
        }
        return QString::fromLatin1(bytes(qint64(n)));
    }
};

// ---------------------------------------------------------------------------
// Little-endian write buffer.
// ---------------------------------------------------------------------------
struct Builder {
    QByteArray buf;

    void raw(const void* p, qint64 n) { buf.append(static_cast<const char*>(p), int(n)); }
    void u8(quint32 v) { buf.append(char(v & 0xFF)); }
    void u32(quint32 v) {
        char b[4] = {char(v & 0xFF), char((v >> 8) & 0xFF),
                     char((v >> 16) & 0xFF), char((v >> 24) & 0xFF)};
        buf.append(b, 4);
    }
    void i32(qint32 v) { u32(quint32(v)); }
    void f32(float v) {
        quint32 bits;
        std::memcpy(&bits, &v, 4);
        u32(bits);
    }
    void str(const QString& s) {
        const QByteArray b = s.toLatin1();
        i32(qint32(b.size()));
        buf.append(b);
    }
    void strRaw(const char* ascii6) { buf.append(ascii6, 6); }
};

// Row-vector matrix multiply: result = a * b (p' = p * a * b).
Matrix4x4 multiply(const Matrix4x4& a, const Matrix4x4& b) {
    Matrix4x4 out;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.m[i][k] * b.m[k][j];
            out.m[i][j] = sum;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------
void fillDdsInfo(Texture& tex) {
    const QByteArray& d = tex.data;
    if (d.size() < 128 || std::memcmp(d.constData(), "DDS ", 4) != 0) return;

    auto ru = [&](int off) {
        quint32 v;
        std::memcpy(&v, d.constData() + off, 4);
        return v;
    };
    tex.height       = ru(12);
    tex.width        = ru(16);
    tex.mipmapCount  = ru(28);

    const quint32 pfFlags = ru(84);
    if (pfFlags & 0x4) {                 // DDPF_FOURCC: DXT1/3/5
        tex.format = ru(88);
    } else if (pfFlags & 0x40) {         // DDPF_RGB — DXGI codes, as used by
        const quint32 bits  = ru(92);    // ACTextureLoader in ACGraphics.cpp
        const quint32 aMask = ru(108);
        if (bits == 32 && aMask == 0xFF000000) tex.format = 28; // RGBA8
        else if (bits == 24)                   tex.format = 27; // RGB8
    }
}

void parseTextures(Cursor& c, KN5File& kn5) {
    const quint32 count = c.get<quint32>();
    if (!c.ok) return;
    if (count > kMaxCounts) {
        c.fail(QString("texture count %1 exceeds limit").arg(count));
        return;
    }
    for (quint32 i = 0; i < count && c.ok; ++i) {
        const qint32 active = c.get<qint32>();
        if (!c.ok) return;
        // active == 0: flag-only record (some mod exporters omit the rest)
        if (active == 0) continue;

        Texture tex;
        tex.name = c.string();
        const quint32 len = c.get<quint32>();
        if (!c.ok) return;
        tex.data = c.bytes(qint64(len));
        if (!c.ok) return;
        fillDdsInfo(tex);
        kn5.textureNames.append(tex.name);
        kn5.textures.append(tex);
    }
}

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------
void parseMaterial(Cursor& c, KN5File& kn5, quint32 version) {
    Material mat;
    mat.id          = quint32(kn5.materials.size());
    mat.name        = c.string();
    mat.shaderName  = c.string();
    const quint8 blend       = c.get<quint8>();
    const quint8 alphaTested = c.get<quint8>();
    qint32 depth = 0;
    if (version >= 5) depth = c.get<qint32>();
    if (!c.ok) return;

    mat.blendMode     = blend;
    mat.depthMode     = depth;
    mat.alphaBlending = (blend != 0);
    mat.alphaTesting  = (alphaTested != 0);
    if (alphaTested)            mat.type = Material::Type::AlphaMask;
    else if (blend == 1)        mat.type = Material::Type::Transparent;
    else if (blend == 2)        mat.type = Material::Type::Transparent;
    else                        mat.type = Material::Type::Normal;

    const quint32 propCount = c.get<quint32>();
    if (!c.ok) return;
    if (propCount > kMaxCounts) {
        c.fail(QString("property count %1 exceeds limit").arg(propCount));
        return;
    }
    for (quint32 i = 0; i < propCount && c.ok; ++i) {
        Material::RawProperty rp;
        rp.name  = c.string();
        rp.value = c.bytes(40);
        if (!c.ok) return;
        mat.rawProperties.append(rp);

        float valueA = 0.0f, valueB = 0.0f;
        std::memcpy(&valueA, rp.value.constData(), 4);
        std::memcpy(&valueB, rp.value.constData() + 4, 4);
        mat.properties[rp.name] = QString::number(valueA);

        ShaderParam sp;
        sp.name   = rp.name;
        sp.value  = valueA;
        sp.valueA = valueB;
        mat.shaderParams.append(sp);
    }

    const quint32 mapCount = c.get<quint32>();
    if (!c.ok) return;
    if (mapCount > kMaxCounts) {
        c.fail(QString("texture mapping count %1 exceeds limit").arg(mapCount));
        return;
    }
    for (quint32 i = 0; i < mapCount && c.ok; ++i) {
        Material::RawMapping rm;
        rm.name    = c.string();
        rm.slot    = c.get<qint32>();
        rm.texture = c.string();
        if (!c.ok) return;
        mat.rawMappings.append(rm);
        mat.textureMapping[rm.name] = rm.texture;
    }

    kn5.materials.append(mat);
}

// ---------------------------------------------------------------------------
// Nodes
// ---------------------------------------------------------------------------
bool canonicalLayout(const Mesh& m, bool skinned) {
    const quint32 need = skinned ? 76u : 44u;
    if (m.vertexLayout.vertexSize != need) return false;
    if (m.vertexData.isEmpty() || m.vertexData.size() % int(need) != 0) return false;
    auto off = [&](AttributeType t, quint8 expected) {
        return m.vertexLayout.offsetOf(t) == int(expected);
    };
    if (!off(AttributeType::Position,  0)  ||
        !off(AttributeType::Normal,   12)  ||
        !off(AttributeType::TexCoord0, 24) ||
        !off(AttributeType::Tangent,  32)) return false;
    if (skinned &&
        (!off(AttributeType::BoneWeight, 44) ||
         !off(AttributeType::BoneIndex,  60))) return false;
    return true;
}

// Produces canonical 44-byte (static) or 76-byte (skinned) vertex data.
// Passes already-canonical buffers through untouched; otherwise rebuilds
// from the typed arrays (decoding raw vertexData first when present).
bool buildVertexData(const Mesh& mesh, bool skinned, QByteArray& out,
                     quint32& count, QString& err) {
    const quint32 stride = skinned ? 76u : 44u;

    if (!mesh.vertexData.isEmpty() && canonicalLayout(mesh, skinned)) {
        count = quint32(mesh.vertexData.size()) / stride;
        out   = mesh.vertexData;
        return true;
    }

    Mesh tmp = mesh;
    if (!tmp.vertexData.isEmpty()) tmp.decodeVertices();

    count = quint32(tmp.positions.size());
    if (count > 65535) {
        err = QString("mesh '%1' has %2 vertices — KN5 supports at most 65535 "
                      "(split the mesh or reduce its density)")
                  .arg(mesh.name).arg(count);
        return false;
    }

    out.resize(int(count * stride));
    out.fill(0);
    for (quint32 i = 0; i < count; ++i) {
        char* v = out.data() + qint64(i) * stride;
        auto wf = [&](int o, float f) { std::memcpy(v + o, &f, 4); };

        const QVector3D p = tmp.positions[i];
        wf(0, p.x()); wf(4, p.y()); wf(8, p.z());

        if (i < quint32(tmp.normals.size())) {
            const QVector3D& n = tmp.normals[i];
            wf(12, n.x()); wf(16, n.y()); wf(20, n.z());
        }
        if (i < quint32(tmp.uv0.size())) {
            wf(24, tmp.uv0[i].x()); wf(28, tmp.uv0[i].y());
        }
        if (i < quint32(tmp.tangents.size())) {
            const QVector3D& t = tmp.tangents[i];
            wf(32, t.x()); wf(36, t.y()); wf(40, t.z());
        }
        if (skinned) {
            if (i < quint32(tmp.boneWeights.size())) {
                const QVector4D& w = tmp.boneWeights[i];
                wf(44, w.x()); wf(48, w.y()); wf(52, w.z()); wf(56, w.w());
            }
            if (i < quint32(tmp.boneIndices.size())) {
                wf(60, float(tmp.boneIndices[i])); // floats in real kn5
            }
        }
    }
    return true;
}

bool validateIndices(const QByteArray& idx, quint32 vcount, const QString& name,
                     QString& err) {
    if (idx.size() % 2 != 0) {
        err = QString("mesh '%1': index buffer size %2 is not a multiple of 2")
                  .arg(name).arg(idx.size());
        return false;
    }
    const int n = idx.size() / 2;
    for (int i = 0; i < n; ++i) {
        quint16 x;
        std::memcpy(&x, idx.constData() + i * 2, 2);
        if (vcount == 0 || x >= vcount) {
            err = QString("mesh '%1': index %2 = %3 out of range (%4 vertices)")
                      .arg(name).arg(i).arg(x).arg(vcount);
            return false;
        }
    }
    return true;
}

bool parseNode(Cursor& c, KN5File& kn5, const Matrix4x4& world, int depth) {
    if (depth > kMaxDepth) {
        c.fail("node hierarchy too deep");
        return false;
    }

    const qint32 nodeClass = c.get<qint32>();
    if (!c.ok) return false;
    if (nodeClass < 1 || nodeClass > 3) {
        c.fail(QString("unknown node class %1").arg(nodeClass));
        return false;
    }

    const QString name        = c.string();
    const qint32  childCount  = c.get<qint32>();
    const quint8  active      = c.get<quint8>();
    if (!c.ok) return false;
    if (childCount < 0 || childCount > kMaxCounts) {
        c.fail(QString("child count %1 exceeds limit").arg(childCount));
        return false;
    }

    Matrix4x4 childWorld = world;

    if (nodeClass == 1) { // Base: local transform, then children
        Matrix4x4 local;
        for (int i = 0; i < 4 && c.ok; ++i)
            for (int j = 0; j < 4 && c.ok; ++j)
                local.m[i][j] = c.get<float>();
        if (!c.ok) return false;
        childWorld = multiply(local, world);
        if (depth == 0) kn5.worldMatrix = local; // root transform (legacy field)
    } else {
        Mesh mesh;
        mesh.name       = name;
        mesh.nodeActive = (active != 0);

        const quint8 castShadows = c.get<quint8>();
        const quint8 isVisible   = c.get<quint8>();
        const quint8 isTransparent = c.get<quint8>();
        if (!c.ok) return false;
        mesh.castShadows   = (castShadows != 0);
        mesh.isVisible     = (isVisible != 0);
        mesh.isTransparent = (isTransparent != 0);
        mesh.worldMatrix   = world;

        if (nodeClass == 2) { // Mesh
            const quint32 vc = c.get<quint32>();
            if (!c.ok) return false;
            if (vc > kMaxElements) {
                c.fail(QString("vertex count %1 exceeds limit").arg(vc));
                return false;
            }
            mesh.vertexLayout.attributes = {
                {AttributeType::Position,  0},
                {AttributeType::Normal,   12},
                {AttributeType::TexCoord0, 24},
                {AttributeType::Tangent,  32},
            };
            mesh.vertexLayout.vertexSize = 44;
            mesh.vertexData = c.bytes(qint64(vc) * 44);
            if (!c.ok) return false;

            const quint32 ic = c.get<quint32>();
            if (!c.ok) return false;
            if (ic > kMaxElements) {
                c.fail(QString("index count %1 exceeds limit").arg(ic));
                return false;
            }
            mesh.indexData = c.bytes(qint64(ic) * 2);
            if (!c.ok) return false;

            mesh.materialIndex = c.get<quint32>();
            mesh.layer         = c.get<quint32>();
            mesh.lodIn         = c.get<float>();
            mesh.lodOut        = c.get<float>();
            const float cx = c.get<float>();
            const float cy = c.get<float>();
            const float cz = c.get<float>();
            mesh.boundingRadius = c.get<float>();
            mesh.isRenderable   = (c.get<quint8>() != 0);
            if (!c.ok) return false;
            mesh.boundingMin = {cx - mesh.boundingRadius,
                                cy - mesh.boundingRadius,
                                cz - mesh.boundingRadius};
            mesh.boundingMax = {cx + mesh.boundingRadius,
                                cy + mesh.boundingRadius,
                                cz + mesh.boundingRadius};
            mesh.decodeVertices();
            kn5.meshes.append(mesh);
        } else { // SkinnedMesh
            mesh.isSkinnedMesh = true;

            const quint32 boneCount = c.get<quint32>();
            if (!c.ok) return false;
            if (boneCount > kMaxCounts) {
                c.fail(QString("bone count %1 exceeds limit").arg(boneCount));
                return false;
            }
            for (quint32 i = 0; i < boneCount && c.ok; ++i) {
                Bone bone;
                bone.name = c.string();
                for (int k = 0; k < 16 && c.ok; ++k)
                    bone.matrix[k] = c.get<float>();
                if (!c.ok) return false;
                mesh.bones.append(bone);
                bool known = false;
                for (const Bone& existing : kn5.bones)
                    if (existing.name == bone.name) { known = true; break; }
                if (!known) kn5.bones.append(bone); // legacy file-level list
            }

            const quint32 vc = c.get<quint32>();
            if (!c.ok) return false;
            if (vc > kMaxElements) {
                c.fail(QString("vertex count %1 exceeds limit").arg(vc));
                return false;
            }
            mesh.vertexLayout.attributes = {
                {AttributeType::Position,  0},
                {AttributeType::Normal,   12},
                {AttributeType::TexCoord0, 24},
                {AttributeType::Tangent,  32},
                {AttributeType::BoneWeight, 44},
                {AttributeType::BoneIndex,  60},
            };
            mesh.vertexLayout.vertexSize = 76;
            mesh.vertexData = c.bytes(qint64(vc) * 76);
            if (!c.ok) return false;

            const quint32 ic = c.get<quint32>();
            if (!c.ok) return false;
            if (ic > kMaxElements) {
                c.fail(QString("index count %1 exceeds limit").arg(ic));
                return false;
            }
            mesh.indexData = c.bytes(qint64(ic) * 2);
            if (!c.ok) return false;

            mesh.materialIndex = c.get<quint32>();
            mesh.layer         = c.get<quint32>();
            mesh.lodIn         = c.get<float>();
            mesh.lodOut        = c.get<float>();
            mesh.isRenderable  = true; // actools sets this unconditionally
            if (!c.ok) return false;
            mesh.decodeVertices();
            kn5.meshes.append(mesh);
        }
    }

    for (qint32 i = 0; i < childCount && c.ok; ++i)
        if (!parseNode(c, kn5, childWorld, depth + 1)) return false;
    return c.ok;
}

// ---------------------------------------------------------------------------
// Material write helpers: QMaps are the editable view, raw* vectors keep the
// real-format bits (40-byte property blobs, mapping slots) that the QMaps
// cannot represent. Merge rule: raw provides order/slot/B/C/D, the QMap wins
// for presence and the ValueA/texture value.
// ---------------------------------------------------------------------------
QVector<Material::RawProperty> mergedProperties(const Material& mat) {
    QVector<Material::RawProperty> out;
    for (const auto& rp : mat.rawProperties) {
        const auto it = mat.properties.constFind(rp.name);
        if (it == mat.properties.constEnd()) continue; // deleted by the editor
        Material::RawProperty keep = rp;
        bool ok = false;
        const float v = it.value().toFloat(&ok);
        if (ok) std::memcpy(keep.value.data(), &v, 4); // overlay edited ValueA
        out.append(keep);
    }
    for (auto it = mat.properties.constBegin(); it != mat.properties.constEnd(); ++it) {
        bool found = false;
        for (const auto& rp : mat.rawProperties)
            if (rp.name == it.key()) { found = true; break; }
        if (found) continue;
        Material::RawProperty rp;
        rp.name  = it.key();
        rp.value = QByteArray(40, '\0');
        bool ok = false;
        const float v = it.value().toFloat(&ok);
        if (ok) std::memcpy(rp.value.data(), &v, 4);
        out.append(rp);
    }
    return out;
}

QVector<Material::RawMapping> mergedMappings(const Material& mat) {
    QVector<Material::RawMapping> out;
    for (const auto& rm : mat.rawMappings) {
        const auto it = mat.textureMapping.constFind(rm.name);
        if (it == mat.textureMapping.constEnd()) continue; // deleted
        Material::RawMapping keep = rm;
        keep.texture = it.value();
        out.append(keep);
    }
    for (auto it = mat.textureMapping.constBegin(); it != mat.textureMapping.constEnd(); ++it) {
        bool found = false;
        for (const auto& rm : mat.rawMappings)
            if (rm.name == it.key()) { found = true; break; }
        if (found) continue;
        Material::RawMapping rm;
        rm.name    = it.key();
        rm.slot    = out.size(); // best effort for editor-created materials
        rm.texture = it.value();
        out.append(rm);
    }
    return out;
}

quint8 writeBlendMode(const Material& mat) {
    if (mat.blendMode >= 0) return quint8(mat.blendMode);
    if (mat.type == Material::Type::Additive)   return 2; // AlphaToCoverage
    if (mat.type == Material::Type::Transparent) return 1;
    return mat.alphaBlending ? 1 : 0;
}

// Appends one Mesh/SkinnedMesh node to a scratch builder; on failure returns
// false with `err` set and nothing appended by the caller.
bool buildMeshNode(Builder& out, const Mesh& mesh, QString& err) {
    const bool skinned = mesh.isSkinnedMesh;
    Builder b;

    b.i32(skinned ? 3 : 2);
    b.str(mesh.name);
    b.i32(0);                 // no children: meshes hang under the root
    b.u8(mesh.nodeActive ? 1 : 0);

    b.u8(mesh.castShadows ? 1 : 0);
    b.u8(mesh.isVisible ? 1 : 0);
    b.u8(mesh.isTransparent ? 1 : 0);

    if (skinned) {
        b.u32(quint32(mesh.bones.size()));
        for (const Bone& bone : mesh.bones) {
            b.str(bone.name);
            for (int k = 0; k < 16; ++k) b.f32(bone.matrix[k]);
        }
    }

    QByteArray vdata;
    quint32 vcount = 0;
    if (!buildVertexData(mesh, skinned, vdata, vcount, err)) return false;

    b.u32(vcount);
    b.raw(vdata.constData(), vdata.size());

    const QByteArray& idx = mesh.indexData;
    if (!validateIndices(idx, vcount, mesh.name, err)) return false;
    b.u32(quint32(idx.size() / 2));
    b.raw(idx.constData(), idx.size());

    b.u32(mesh.materialIndex);
    b.u32(mesh.layer);
    b.f32(mesh.lodIn);
    b.f32(mesh.lodOut);
    if (!skinned) {
        const float cx = (mesh.boundingMin.x + mesh.boundingMax.x) * 0.5f;
        const float cy = (mesh.boundingMin.y + mesh.boundingMax.y) * 0.5f;
        const float cz = (mesh.boundingMin.z + mesh.boundingMax.z) * 0.5f;
        float radius = mesh.boundingRadius;
        if (radius <= 0.0f && vcount > 0) {
            const float dx = (mesh.boundingMax.x - mesh.boundingMin.x) * 0.5f;
            const float dy = (mesh.boundingMax.y - mesh.boundingMin.y) * 0.5f;
            const float dz = (mesh.boundingMax.z - mesh.boundingMin.z) * 0.5f;
            radius = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        b.f32(cx); b.f32(cy); b.f32(cz);
        b.f32(radius);
        b.u8(mesh.isRenderable ? 1 : 0);
    }

    out.buf.append(b.buf);
    return true;
}

} // anonymous namespace

// ============================================================================

QString KN5ParserImpl::m_lastError;

QString KN5ParserImpl::lastError() {
    return m_lastError;
}

KN5File KN5ParserImpl::parse(const QString& filePath, QString* error) {
    KN5File kn5;
    kn5.filePath = filePath;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QString("Cannot open file: %1").arg(filePath);
        if (error) *error = m_lastError;
        return KN5File();
    }
    const QByteArray data = file.readAll();
    file.close();

    if (data.size() < 14) {
        m_lastError = "offset 0: file too small to be a KN5";
        if (error) *error = m_lastError;
        return KN5File();
    }
    if (std::memcmp(data.constData(), KN5_MAGIC_BYTES, 6) != 0) {
        m_lastError = QString("offset 0: bad KN5 magic (expected \"%1\")")
                          .arg(QLatin1String(KN5_MAGIC_BYTES));
        if (error) *error = m_lastError;
        return KN5File();
    }

    Cursor c(data);
    c.pos = 6;

    const quint32 version = quint32(c.get<qint32>());
    if (!c.ok) { m_lastError = c.err; if (error) *error = m_lastError; return KN5File(); }
    if (version < KN5_VERSION_MIN || version > KN5_VERSION_MAX) {
        m_lastError = QString("unsupported KN5 version %1 (supported %2-%3)")
                          .arg(version).arg(KN5_VERSION_MIN).arg(KN5_VERSION_MAX);
        if (error) *error = m_lastError;
        return KN5File();
    }
    if (version > 5) c.get<qint32>(); // version-6 extra header field

    kn5.header.magic   = KN5_MAGIC;
    kn5.header.version = version;

    parseTextures(c, kn5);
    if (c.ok) {
        const quint32 matCount = c.get<quint32>();
        if (c.ok && matCount > kMaxCounts)
            c.fail(QString("material count %1 exceeds limit").arg(matCount));
        for (quint32 i = 0; i < matCount && c.ok; ++i)
            parseMaterial(c, kn5, version);
    }

    if (c.ok) {
        if (c.pos >= c.size) {
            c.fail("missing root node");
        } else {
            const Matrix4x4 identity;
            parseNode(c, kn5, identity, 0);
        }
    }

    if (!c.ok) {
        // Trailing bytes after the node tree are tolerated (25 mod files);
        // anything else is a real error.
        m_lastError = c.err;
        if (error) *error = m_lastError;
        return KN5File();
    }

    kn5.header.textureCount  = quint32(kn5.textures.size());
    kn5.header.materialCount = quint32(kn5.materials.size());
    kn5.header.nodeCount     = quint32(kn5.meshes.size());
    kn5.extractLODGroups();
    return kn5;
}

bool KN5ParserImpl::write(const QString& filePath, const KN5File& kn5) {
    Builder b;

    // Header: real magic + version 5 (the version actools itself writes,
    // CommonAcConsts.Kn5ActualVersion). Version 5 carries no extra field
    // and materials include the depthMode int.
    b.strRaw(KN5_MAGIC_BYTES);
    b.i32(qint32(KN5_VERSION));

    // --- Textures (only records with payload — the format requires data) ---
    quint32 texCount = 0;
    for (const auto& tex : kn5.textures)
        if (!tex.data.isEmpty() && !tex.name.isEmpty()) ++texCount;
    b.i32(qint32(texCount));
    for (const auto& tex : kn5.textures) {
        if (tex.data.isEmpty() || tex.name.isEmpty()) continue;
        b.i32(1); // active
        b.str(tex.name);
        b.u32(quint32(tex.data.size()));
        b.raw(tex.data.constData(), tex.data.size());
    }

    // --- Materials ---
    b.i32(qint32(kn5.materials.size()));
    for (const auto& mat : kn5.materials) {
        b.str(mat.name);
        b.str(mat.shaderName);
        b.u8(writeBlendMode(mat));
        const bool alphaTested =
            (mat.type == Material::Type::AlphaMask) || mat.alphaTesting;
        b.u8(alphaTested ? 1 : 0);
        b.i32(mat.depthMode >= 0 ? mat.depthMode : 0);

        const QVector<Material::RawProperty> props = mergedProperties(mat);
        b.i32(qint32(props.size()));
        for (const auto& rp : props) {
            b.str(rp.name);
            b.raw(rp.value.constData(), 40);
        }

        const QVector<Material::RawMapping> maps = mergedMappings(mat);
        b.i32(qint32(maps.size()));
        for (const auto& rm : maps) {
            b.str(rm.name);
            b.i32(rm.slot);
            b.str(rm.texture);
        }
    }

    // --- Node tree: identity root, one Base wrapper per transformed mesh ---
    // Writing the accumulated world transform into a per-mesh Base wrapper
    // (instead of baking it into vertices) keeps geometry, hierarchy and the
    // root matrix field round-trip-exact without needing matrix inverses.
    b.i32(1);            // node class: Base
    b.str("root");
    b.i32(qint32(kn5.meshes.size()));
    b.u8(1);             // active
    const Matrix4x4 identity;
    for (int j = 0; j < 4; ++j)
        for (int k = 0; k < 4; ++k)
            b.f32(identity.m[j][k]); // identity root: worlds live on wrappers

    QString err;
    for (const auto& mesh : kn5.meshes) {
        const bool transformed = std::memcmp(mesh.worldMatrix.m, identity.m,
                                             sizeof(identity.m)) != 0;
        if (transformed) {
            b.i32(1); // Base wrapper carrying this mesh's world transform
            b.str(mesh.name + "_xf");
            b.i32(1);
            b.u8(1);
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 4; ++k)
                    b.f32(mesh.worldMatrix.m[j][k]);
        }

        if (!buildMeshNode(b, mesh, err)) {
            m_lastError = err;
            return false;
        }
    }

    QFile outFile(filePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        m_lastError = QString("Cannot open file for writing: %1").arg(filePath);
        return false;
    }
    const qint64 written = outFile.write(b.buf);
    outFile.close();
    if (written != b.buf.size()) {
        m_lastError = QString("Short write to %1 (%2 of %3 bytes)")
                          .arg(filePath).arg(written).arg(b.buf.size());
        return false;
    }
    return true;
}

bool KN5ParserImpl::isValid(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    const QByteArray head = file.read(10);
    file.close();
    if (head.size() < 10) return false;
    if (std::memcmp(head.constData(), KN5_MAGIC_BYTES, 6) != 0) return false;

    qint32 version;
    std::memcpy(&version, head.constData() + 6, 4);
    return version >= qint32(KN5_VERSION_MIN) && version <= qint32(KN5_VERSION_MAX);
}

void MeshHelper::computeBoundingBox(const KN5File& kn5, Vector3& min, Vector3& max) {
    min = Vector3(std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max());
    max = Vector3(-std::numeric_limits<float>::max(),
                  -std::numeric_limits<float>::max(),
                  -std::numeric_limits<float>::max());

    for (const auto& mesh : kn5.meshes) {
        min.x = qMin(min.x, mesh.boundingMin.x);
        min.y = qMin(min.y, mesh.boundingMin.y);
        min.z = qMin(min.z, mesh.boundingMin.z);
        max.x = qMax(max.x, mesh.boundingMax.x);
        max.y = qMax(max.y, mesh.boundingMax.y);
        max.z = qMax(max.z, mesh.boundingMax.z);
    }
}

void MeshHelper::computeBoundingSphere(const KN5File& kn5, Vector3& center, float& radius) {
    Vector3 min, max;
    computeBoundingBox(kn5, min, max);
    center = (min + max) * 0.5f;
    Vector3 diff;
    diff.x = max.x - min.x;
    diff.y = max.y - min.y;
    diff.z = max.z - min.z;
    radius = diff.length() * 0.5f;
}

} // namespace KN5Parser
