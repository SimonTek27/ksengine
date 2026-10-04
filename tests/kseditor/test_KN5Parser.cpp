// Qt-side KN5 parser/writer/baker/decryptor test.
//
// Covers the real Assetto Corsa KN5 container format as implemented in
// src/sdk/kseditor/plugins/simulators/kunos/assettocorsa/acFiles
// (KN5Types.h / KN5Parser.cpp / KN5Baker.cpp / KN5Decrypt.cpp):
//   - format constants and magic ("sc6969", versions 4..6)
//   - full-fidelity synthetic round trip (textures with DDS-derived info,
//     40-byte shader property blobs with ValueB/C/D, mapping slots,
//     per-mesh world matrices via Base wrappers, skinned meshes with
//     float bone indices, LOD extraction)
//   - error reporting for bad magic / bad version
//   - KN5 -> NMSH baking (world transform applied, skinned skipped)
//   - KN5Decrypt plain/CSP-envelope handling
//   - guarded cross-validation against a real AC .kn5 corpus AND the
//     Qt-free engine reader (engine/FileFormat/Kn5Reader.h)
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <cstring>
#include <string_view>

#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/acFiles/KN5Parser.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/acFiles/KN5Baker.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/acFiles/KN5Decrypt.h"
#include "engine/FileFormat/Kn5Reader.h"

using KN5Parser::KN5File;
using KN5Parser::KN5ParserImpl;
using KN5Parser::Material;
using KN5Parser::Mesh;
using KN5Parser::Bone;
using KN5Parser::Texture;

namespace {

// Minimal DDS header so parse()'s fillDdsInfo() derives width/height/mips/
// format the same way it does for real texture payloads.
QByteArray makeDds(quint32 w, quint32 h) {
    QByteArray d(128 + 8, '\0');
    char* p = d.data();
    auto wu = [&](int off, quint32 v) { std::memcpy(p + off, &v, 4); };
    std::memcpy(p, "DDS ", 4);
    wu(4, 124);                    // dwSize
    wu(12, h);                     // dwHeight
    wu(16, w);                     // dwWidth
    wu(28, 1);                     // dwMipMapCount
    wu(76, 32);                    // ddspf.dwSize
    wu(84, 0x40);                  // ddspf.dwFlags = DDPF_RGB
    wu(92, 32);                    // ddspf.dwRGBBitCount
    wu(108, 0xFF000000u);          // ddspf.dwABitMask -> format 28 (RGBA8)
    d[128] = 'X';
    d[129] = 'Y';
    return d;
}

QByteArray u16Indices(std::initializer_list<quint16> vals) {
    QByteArray b;
    for (quint16 v : vals) {
        char le[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)};
        b.append(le, 2);
    }
    return b;
}

void setIdentity(float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = ((i % 5) == 0) ? 1.0f : 0.0f;
}

// Synthetic KN5 exercising every fidelity feature the writer must preserve:
// one static mesh under a non-identity world matrix (gets a Base "_xf"
// wrapper), one skinned mesh at the identity, two materials with raw
// property blobs / mapping slots, one DDS texture + one payload-less
// texture (dropped on write).
KN5File makeSynthetic(const QByteArray& dds) {
    KN5File kn5;

    Texture tex;
    tex.name = QStringLiteral("diffuse.dds");
    tex.data = dds;
    kn5.textures.append(tex);
    kn5.textureNames.append(tex.name);
    Texture empty; // no payload: not writable, dropped by write()
    empty.name = QStringLiteral("empty.dds");
    kn5.textures.append(empty);

    Material body;
    body.name = QStringLiteral("body");
    body.shaderName = QStringLiteral("ksPerPixel");
    body.type = Material::Type::Transparent;
    body.blendMode = 1; // AlphaBlend
    body.depthMode = 1; // NoWrite
    body.alphaBlending = true;

    Material::RawProperty diffuse;
    diffuse.name = QStringLiteral("diffuse");
    diffuse.value = QByteArray(40, '\0');
    const float a0 = 0.5f, b0 = 1.5f;
    std::memcpy(diffuse.value.data(), &a0, 4);
    std::memcpy(diffuse.value.data() + 4, &b0, 4);
    diffuse.value[8] = '\xAB';
    diffuse.value[39] = '\xCD';
    body.rawProperties.append(diffuse);

    Material::RawProperty detail;
    detail.name = QStringLiteral("detailMul");
    detail.value = QByteArray(40, '\0');
    const float a1 = 0.75f, b1 = 2.5f, c1 = 3.5f, d1 = 4.5f;
    std::memcpy(detail.value.data(), &a1, 4);
    std::memcpy(detail.value.data() + 4, &b1, 4);
    std::memcpy(detail.value.data() + 8, &c1, 4);
    std::memcpy(detail.value.data() + 12, &d1, 4);
    body.rawProperties.append(detail);

    // QMap view: wins for presence and ValueA on write()
    body.properties.insert(QStringLiteral("diffuse"), QString::number(a0));
    body.properties.insert(QStringLiteral("detailMul"), QString::number(a1));
    // Editor-created property without a raw blob: appended on write
    body.properties.insert(QStringLiteral("newProp"), QString::number(0.25f));

    Material::RawMapping m0;
    m0.name = QStringLiteral("txDiffuse");
    m0.slot = 0;
    m0.texture = QStringLiteral("diffuse.dds");
    Material::RawMapping m1;
    m1.name = QStringLiteral("txNormal");
    m1.slot = 2; // deliberately non-sequential: slots must survive
    m1.texture = QStringLiteral("normal.dds");
    body.rawMappings.append(m0);
    body.rawMappings.append(m1);
    body.textureMapping.insert(m0.name, m0.texture);
    body.textureMapping.insert(m1.name, m1.texture);
    kn5.materials.append(body);

    Material glass;
    glass.name = QStringLiteral("glass");
    glass.shaderName = QStringLiteral("ksGfx");
    glass.type = Material::Type::Normal;
    glass.blendMode = 0;
    glass.depthMode = 0;
    // Raw property whose name is absent from properties: dropped by write()
    Material::RawProperty ghost;
    ghost.name = QStringLiteral("ghost");
    ghost.value = QByteArray(40, '\x5A');
    glass.rawProperties.append(ghost);
    kn5.materials.append(glass);

    Mesh a;
    a.name = QStringLiteral("LOD_A_body");
    a.worldMatrix.m[3][0] = 10.0f; // row-vector translation
    a.worldMatrix.m[3][1] = 20.0f;
    a.worldMatrix.m[3][2] = 30.0f;
    a.positions = {QVector3D(1, 2, 3), QVector3D(4, 5, 6), QVector3D(7, 8, 9)};
    a.normals = {QVector3D(0, 0, 1), QVector3D(0, 0, 1), QVector3D(0, 0, 1)};
    a.uv0 = {QVector2D(0, 0), QVector2D(1, 0), QVector2D(0, 1)};
    a.tangents = {QVector3D(1, 0, 0), QVector3D(1, 0, 0), QVector3D(1, 0, 0)};
    a.indexData = u16Indices({0, 1, 2});
    a.materialIndex = 1;
    a.layer = 2;
    a.lodIn = 5.0f;
    a.lodOut = 250.0f;
    a.isRenderable = false;
    a.nodeActive = false;
    a.castShadows = false;
    a.isVisible = true;
    a.isTransparent = true;
    a.boundingMin = {0.0f, 0.0f, 0.0f};
    a.boundingMax = {2.0f, 4.0f, 6.0f}; // radius 0 -> derived: sqrt(14)
    kn5.meshes.append(a);

    Mesh s;
    s.name = QStringLiteral("LOD_B_skinned");
    s.isSkinnedMesh = true;
    Bone rootBone;
    rootBone.name = QStringLiteral("root_bone");
    setIdentity(rootBone.matrix);
    Bone spine;
    spine.name = QStringLiteral("spine");
    setIdentity(spine.matrix);
    spine.matrix[13] = -1.0f; // translation (0,-1,0)
    s.bones.append(rootBone);
    s.bones.append(spine);
    s.positions = {QVector3D(0, 0, 0), QVector3D(1, 0, 0), QVector3D(0, 1, 0)};
    s.normals = {QVector3D(0, 0, 1), QVector3D(0, 0, 1), QVector3D(0, 0, 1)};
    s.uv0 = {QVector2D(0, 0), QVector2D(1, 0), QVector2D(0, 1)};
    s.tangents = {QVector3D(1, 0, 0), QVector3D(1, 0, 0), QVector3D(1, 0, 0)};
    s.boneWeights = {QVector4D(1, 0, 0, 0), QVector4D(0.5f, 0.5f, 0, 0),
                     QVector4D(1, 0, 0, 0)};
    s.boneIndices = {0u, 1u, 0u};
    s.indexData = u16Indices({0, 1, 2});
    s.materialIndex = 0;
    s.layer = 0;
    s.lodIn = 0.0f;
    s.lodOut = 500.0f;
    kn5.meshes.append(s);

    return kn5;
}

// First usable AC .kn5: fonts/axis.kn5 if present, else the first parsable
// mesh-bearing file found under the content root. Empty when no corpus.
QString findRealKn5WithMeshes() {
    static const QStringList roots = {
        QStringLiteral("F:/SteamLibrary/steamapps/common/assettocorsa/content"),
        QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/assettocorsa/content"),
        QStringLiteral("E:/SteamLibrary/steamapps/common/assettocorsa/content"),
        QStringLiteral("D:/SteamLibrary/steamapps/common/assettocorsa/content"),
    };

    QStringList candidates;
    for (const QString& root : roots) {
        const QString axis = root + QStringLiteral("/fonts/axis.kn5");
        if (QFile::exists(axis)) candidates.append(axis);
    }
    for (const QString& root : roots) {
        if (candidates.size() >= 8) break;
        if (!QDir(root).exists()) continue;
        QDirIterator it(root, {QStringLiteral("*.kn5")}, QDir::Files,
                        QDirIterator::Subdirectories);
        int scanned = 0;
        while (it.hasNext() && scanned < 400 && candidates.size() < 8) {
            const QString p = it.next();
            ++scanned;
            const QFileInfo fi(p);
            if (fi.size() >= 14 && fi.size() < qint64(120) * 1024 * 1024)
                candidates.append(p);
        }
    }

    for (const QString& p : candidates) {
        QString err;
        const KN5File kn5 = KN5ParserImpl::parse(p, &err);
        if (err.isEmpty() && !kn5.meshes.isEmpty()) return p;
    }
    return {};
}

void collectEngineMeshNames(const ks::engine::fileformat::Kn5Node& node,
                            QStringList& out) {
    if (node.has_mesh) out.append(QString::fromLatin1(node.mesh.name.c_str()));
    for (const auto& child : node.children) collectEngineMeshNames(child, out);
}

} // namespace

class TestKN5Parser : public QObject {
    Q_OBJECT

private:
    QTemporaryDir* m_tempDir = nullptr;
    QString m_dir;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testMagicConstants();
    void testParseRejectsBadMagic();
    void testParseRejectsBadVersion();
    void testSyntheticRoundTrip();
    void testBakeToNativeMeshes();
    void testUnprotectPlainAndEnvelope();
    void testRealFileRoundTrip();
};

void TestKN5Parser::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
    m_dir = m_tempDir->path();
}

void TestKN5Parser::cleanupTestCase()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

void TestKN5Parser::testMagicConstants()
{
    QCOMPARE(qsizetype(std::strlen(KN5Parser::KN5_MAGIC_BYTES)), qsizetype(6));
    QCOMPARE(QByteArray(KN5Parser::KN5_MAGIC_BYTES, 6), QByteArray("sc6969"));
    quint32 firstFour = 0;
    std::memcpy(&firstFour, KN5Parser::KN5_MAGIC_BYTES, 4);
    QCOMPARE(KN5Parser::KN5_MAGIC, firstFour);
    QCOMPARE(KN5Parser::KN5_VERSION, 5u);
    QCOMPARE(KN5Parser::KN5_VERSION_MIN, 4u);
    QCOMPARE(KN5Parser::KN5_VERSION_MAX, 6u);
}

void TestKN5Parser::testParseRejectsBadMagic()
{
    const QString path = m_dir + QStringLiteral("/junk.kn5");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(64, 'J'));
    f.close();

    QVERIFY(!KN5ParserImpl::isValid(path));
    QString err;
    const KN5File kn5 = KN5ParserImpl::parse(path, &err);
    QVERIFY(!kn5.isValid());
    QCOMPARE(err, QStringLiteral("offset 0: bad KN5 magic (expected \"sc6969\")"));
    QVERIFY(KN5ParserImpl::lastError().contains(QStringLiteral("sc6969")));
}

void TestKN5Parser::testParseRejectsBadVersion()
{
    const QString path = m_dir + QStringLiteral("/badver.kn5");
    QByteArray data("sc6969", 6);
    const qint32 version = 99;
    data.append(reinterpret_cast<const char*>(&version), 4);
    data.append(QByteArray(4, '\0')); // pad past the 14-byte minimum
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
    f.close();

    QVERIFY(!KN5ParserImpl::isValid(path)); // version 99 > max 6
    QString err;
    const KN5File kn5 = KN5ParserImpl::parse(path, &err);
    QVERIFY(!kn5.isValid());
    QVERIFY2(err.contains(QStringLiteral("unsupported KN5 version 99")),
             qPrintable(err));
}

void TestKN5Parser::testSyntheticRoundTrip()
{
    const QByteArray dds = makeDds(32, 64);
    const QString path = m_dir + QStringLiteral("/synthetic.kn5");

    KN5File kn5 = makeSynthetic(dds);
    QVERIFY2(KN5ParserImpl::write(path, kn5),
             qPrintable(KN5ParserImpl::lastError()));
    QVERIFY(KN5ParserImpl::isValid(path));

    QString err;
    const KN5File k2 = KN5ParserImpl::parse(path, &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QVERIFY(k2.isValid());

    // --- header / textures ---
    QCOMPARE(k2.header.magic, KN5Parser::KN5_MAGIC);
    QCOMPARE(k2.header.version, 5u);
    QCOMPARE(k2.header.textureCount, 1u); // payload-less texture dropped
    QCOMPARE(k2.header.materialCount, 2u);
    QCOMPARE(k2.header.nodeCount, 2u);
    QCOMPARE(k2.textures.size(), 1);
    QCOMPARE(k2.textureNames, QStringList{QStringLiteral("diffuse.dds")});
    QCOMPARE(k2.textures[0].name, QStringLiteral("diffuse.dds"));
    QCOMPARE(k2.textures[0].width, 32u);
    QCOMPARE(k2.textures[0].height, 64u);
    QCOMPARE(k2.textures[0].mipmapCount, 1u);
    QCOMPARE(k2.textures[0].format, 28u);
    QCOMPARE(k2.textures[0].data, dds);

    // --- materials ---
    QCOMPARE(k2.materials.size(), 2);
    const Material& m0 = k2.materials[0];
    QCOMPARE(m0.name, QStringLiteral("body"));
    QCOMPARE(m0.shaderName, QStringLiteral("ksPerPixel"));
    QCOMPARE(m0.blendMode, 1);
    QCOMPARE(m0.depthMode, 1);
    QCOMPARE(int(m0.type), int(Material::Type::Transparent));
    QVERIFY(m0.alphaBlending);
    QVERIFY(!m0.alphaTesting);

    QCOMPARE(m0.rawProperties.size(), 3);
    QCOMPARE(m0.rawProperties[0].name, QStringLiteral("diffuse"));
    QCOMPARE(m0.rawProperties[0].value.size(), 40);
    float va = 0, vb = 0;
    std::memcpy(&va, m0.rawProperties[0].value.constData(), 4);
    std::memcpy(&vb, m0.rawProperties[0].value.constData() + 4, 4);
    QVERIFY(qFuzzyCompare(va, 0.5f));
    QVERIFY(qFuzzyCompare(vb, 1.5f)); // ValueB preserved, not just ValueA
    QCOMPARE(m0.rawProperties[0].value.at(8), '\xAB');
    QCOMPARE(m0.rawProperties[0].value.at(39), '\xCD');

    QCOMPARE(m0.rawProperties[1].name, QStringLiteral("detailMul"));
    float c = 0, d = 0;
    std::memcpy(&c, m0.rawProperties[1].value.constData() + 8, 4);
    std::memcpy(&d, m0.rawProperties[1].value.constData() + 12, 4);
    QVERIFY(qFuzzyCompare(c, 3.5f)); // ValueC
    QVERIFY(qFuzzyCompare(d, 4.5f)); // ValueD

    QCOMPARE(m0.rawProperties[2].name, QStringLiteral("newProp")); // appended
    QCOMPARE(m0.properties.size(), 3);
    QCOMPARE(m0.properties.value(QStringLiteral("diffuse")), QStringLiteral("0.5"));
    QCOMPARE(m0.properties.value(QStringLiteral("detailMul")), QStringLiteral("0.75"));
    QCOMPARE(m0.properties.value(QStringLiteral("newProp")), QStringLiteral("0.25"));

    QCOMPARE(m0.rawMappings.size(), 2);
    QCOMPARE(m0.rawMappings[0].name, QStringLiteral("txDiffuse"));
    QCOMPARE(m0.rawMappings[0].slot, 0);
    QCOMPARE(m0.rawMappings[1].name, QStringLiteral("txNormal"));
    QCOMPARE(m0.rawMappings[1].slot, 2); // non-sequential slot preserved
    QCOMPARE(m0.rawMappings[1].texture, QStringLiteral("normal.dds"));
    QCOMPARE(m0.textureMapping.size(), 2);

    QCOMPARE(m0.shaderParams.size(), 3);
    QVERIFY(qFuzzyCompare(m0.shaderParams[0].value, 0.5f));
    QVERIFY(qFuzzyCompare(m0.shaderParams[0].valueA, 1.5f));

    const Material& m1 = k2.materials[1];
    QCOMPARE(m1.name, QStringLiteral("glass"));
    QVERIFY(m1.rawProperties.isEmpty()); // "ghost" dropped (not in QMap)
    QVERIFY(m1.properties.isEmpty());
    QCOMPARE(m1.blendMode, 0);
    QCOMPARE(int(m1.type), int(Material::Type::Normal));

    // --- meshes ---
    QCOMPARE(k2.meshes.size(), 2);
    const Mesh& a = k2.meshes[0];
    QCOMPARE(a.name, QStringLiteral("LOD_A_body"));
    QCOMPARE(a.vertexData.size(), 3 * 44);
    QCOMPARE(a.getTriangleCount(), 1u);
    QCOMPARE(a.positions.size(), 3);
    QCOMPARE(a.positions[0], QVector3D(1, 2, 3));
    QCOMPARE(a.positions[2], QVector3D(7, 8, 9));
    QCOMPARE(a.normals[0], QVector3D(0, 0, 1));
    QCOMPARE(a.uv0[1], QVector2D(1, 0));
    QCOMPARE(a.tangents[0], QVector3D(1, 0, 0));
    QCOMPARE(a.indexData, u16Indices({0, 1, 2}));
    QCOMPARE(a.materialIndex, 1u);
    QCOMPARE(a.layer, 2u);
    QCOMPARE(a.lodIn, 5.0f);
    QCOMPARE(a.lodOut, 250.0f);
    QVERIFY(!a.isRenderable);
    QVERIFY(!a.nodeActive);
    QVERIFY(!a.castShadows);
    QVERIFY(a.isVisible);
    QVERIFY(a.isTransparent);
    QVERIFY(!a.isSkinnedMesh);
    // world matrix round-trips through the per-mesh "_xf" Base wrapper
    QVERIFY(qFuzzyCompare(a.worldMatrix.m[3][0], 10.0f));
    QVERIFY(qFuzzyCompare(a.worldMatrix.m[3][1], 20.0f));
    QVERIFY(qFuzzyCompare(a.worldMatrix.m[3][2], 30.0f));
    QVERIFY(qFuzzyCompare(a.worldMatrix.m[0][0], 1.0f));
    // bounding sphere: center (1,2,3), radius sqrt(14)
    QVERIFY(qFuzzyCompare(a.boundingRadius, std::sqrt(14.0f)));

    const Mesh& s = k2.meshes[1];
    QCOMPARE(s.name, QStringLiteral("LOD_B_skinned"));
    QVERIFY(s.isSkinnedMesh);
    QCOMPARE(s.vertexData.size(), 3 * 76);
    QCOMPARE(s.getTriangleCount(), 1u);
    QCOMPARE(s.bones.size(), 2);
    QCOMPARE(s.bones[0].name, QStringLiteral("root_bone"));
    QVERIFY(qFuzzyCompare(s.bones[0].matrix[0], 1.0f));
    QCOMPARE(s.bones[1].name, QStringLiteral("spine"));
    QVERIFY(qFuzzyCompare(s.bones[1].matrix[13], -1.0f));
    QCOMPARE(k2.bones.size(), 2); // deduplicated file-level list
    QCOMPARE(s.boneIndices.size(), 3);
    QCOMPARE(s.boneIndices[0], 0u);
    QCOMPARE(s.boneIndices[1], 1u);
    QVERIFY(qFuzzyCompare(s.boneWeights[1].x(), 0.5f));
    QCOMPARE(s.materialIndex, 0u);
    QCOMPARE(s.lodOut, 500.0f);
    QVERIFY(s.isRenderable); // forced by the format (actools behaviour)
    QVERIFY(s.nodeActive);
    QVERIFY(qFuzzyCompare(s.worldMatrix.m[3][1], 0.0f)); // identity root

    // --- LOD groups + root matrix ---
    QCOMPARE(k2.lodGroups.size(), 2);
    QCOMPARE(k2.lodGroups[0].name, QStringLiteral("LOD_A"));
    QCOMPARE(k2.lodGroups[0].meshIndices, QVector<int>{0});
    QCOMPARE(k2.lodGroups[1].name, QStringLiteral("LOD_B"));
    QCOMPARE(k2.lodGroups[1].meshIndices, QVector<int>{1});
    QVERIFY(qFuzzyCompare(k2.worldMatrix.m[0][0], 1.0f));
    QVERIFY(qFuzzyCompare(k2.worldMatrix.m[3][0], 0.0f)); // identity root
}

void TestKN5Parser::testBakeToNativeMeshes()
{
    const QByteArray dds = makeDds(32, 64);
    const QString path = m_dir + QStringLiteral("/bake_src.kn5");
    QVERIFY2(KN5ParserImpl::write(path, makeSynthetic(dds)),
             qPrintable(KN5ParserImpl::lastError()));

    const std::string outDir = (m_dir + QStringLiteral("/nmsh")).toStdString();
    const ks::tools::BakeResult result =
        ks::tools::bakeKN5ToNativeMeshes(path, outDir);
    QVERIFY2(result.success, qPrintable(result.error));
    QCOMPARE(result.meshesWritten, 1); // skinned mesh skipped
    QCOMPARE(result.meshesSkipped, 1);

    QFile manifest(QString::fromStdString(outDir) + QStringLiteral("/manifest.txt"));
    QVERIFY(manifest.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromLatin1(manifest.readAll()), QStringLiteral("LOD_A_body\n"));
    manifest.close();

    QFile nmsh(QString::fromStdString(outDir) + QStringLiteral("/LOD_A_body.nmsh"));
    QVERIFY(nmsh.open(QIODevice::ReadOnly));
    const QByteArray raw = nmsh.readAll();
    nmsh.close();
    // 4 magic + 4 vcount + 4 icount + 3 * 48 (NativeVertex) + 3 * 4 (u32 idx)
    QCOMPARE(qsizetype(raw.size()), qsizetype(12 + 3 * 48 + 3 * 4));
    QCOMPARE(QByteArray(raw.constData(), 4), QByteArray("NMSH"));
    quint32 vcount = 0, icount = 0;
    std::memcpy(&vcount, raw.constData() + 4, 4);
    std::memcpy(&icount, raw.constData() + 8, 4);
    QCOMPARE(vcount, 3u);
    QCOMPARE(icount, 3u);

    // First vertex: input (1,2,3) + world translation (10,20,30) — row-vector
    float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0, u = 0, v = 0;
    const char* vert = raw.constData() + 12;
    std::memcpy(&px, vert + 0, 4);
    std::memcpy(&py, vert + 4, 4);
    std::memcpy(&pz, vert + 8, 4);
    std::memcpy(&nx, vert + 12, 4);
    std::memcpy(&ny, vert + 16, 4);
    std::memcpy(&nz, vert + 20, 4);
    std::memcpy(&u, vert + 24, 4);
    std::memcpy(&v, vert + 28, 4);
    QVERIFY2(qFuzzyCompare(px, 11.0f) && qFuzzyCompare(py, 22.0f) &&
                 qFuzzyCompare(pz, 33.0f),
             qPrintable(QStringLiteral("baked pos = (%1,%2,%3)").arg(px).arg(py).arg(pz)));
    QVERIFY(qFuzzyCompare(nx, 0.0f));
    QVERIFY(qFuzzyCompare(nz, 1.0f));
    QVERIFY(qFuzzyCompare(u, 0.0f));
    QVERIFY(qFuzzyCompare(v, 0.0f));
}

void TestKN5Parser::testUnprotectPlainAndEnvelope()
{
    const QString plain = m_dir + QStringLiteral("/plain.kn5");
    QVERIFY2(KN5ParserImpl::write(plain, makeSynthetic(makeDds(8, 8))),
             qPrintable(KN5ParserImpl::lastError()));

    QFile f(plain);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray before = f.readAll();
    f.close();

    QVERIFY(!KN5Decrypt::isCSPProtected(plain));
    QVERIFY(!KN5Decrypt::isKN5Unprotectable(plain));
    QString err;
    QVERIFY2(KN5Decrypt::unprotect(plain, &err), qPrintable(err));

    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray after = f.readAll();
    f.close();
    QCOMPARE(after, before); // plain file: no-op, byte-identical

    // CSP envelope: the marker as KN5Decrypt defines it (31 bytes = literal
    // + the C-string NUL) followed by a version digit and delimiter, i.e.
    // the layout parseEncryptedHeader() expects.
    const QString envelope = m_dir + QStringLiteral("/envelope.kn5");
    QByteArray e("sc6969", 6);
    const qint32 version = 5;
    e.append(reinterpret_cast<const char*>(&version), 4);
    e.append(QByteArray(54, '\0'));
    e.append(QByteArrayLiteral("__AC_SHADERS_PATCH_KN5ENC_v1__"));
    e.append(QByteArray(1, '\0'));
    e.append(QByteArrayLiteral("1:"));
    QFile ef(envelope);
    QVERIFY(ef.open(QIODevice::WriteOnly));
    ef.write(e);
    ef.close();

    QVERIFY(KN5Decrypt::isCSPProtected(envelope));
    QVERIFY(KN5Decrypt::isKN5Unprotectable(envelope));
    QString uerr;
    QVERIFY2(!KN5Decrypt::unprotect(envelope, &uerr), "envelope must not be \"unprotected\"");
    QVERIFY2(uerr.contains(QStringLiteral("decrypt()")), qPrintable(uerr));
}

void TestKN5Parser::testRealFileRoundTrip()
{
    const QString path = findRealKn5WithMeshes();
    if (path.isEmpty())
        QSKIP("No Assetto Corsa .kn5 corpus found on this machine");

    QString err;
    const KN5File kn5 = KN5ParserImpl::parse(path, &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QVERIFY(!kn5.meshes.isEmpty());

    // --- cross-check against the Qt-free engine reader ---
    QFile src(path);
    QVERIFY(src.open(QIODevice::ReadOnly));
    const QByteArray bytes = src.readAll();
    src.close();
    auto res = ks::engine::fileformat::parseKn5(
        std::string_view(bytes.constData(), static_cast<size_t>(bytes.size())));
    QVERIFY2(res.ok(), res.error.c_str());
    QCOMPARE(static_cast<quint32>(res.file.version), kn5.header.version);
    QCOMPARE(qsizetype(res.file.textures.size()), qsizetype(kn5.textures.size()));
    QCOMPARE(qsizetype(res.file.materials.size()), qsizetype(kn5.materials.size()));
    for (int i = 0; i < kn5.materials.size(); ++i) {
        QCOMPARE(QString::fromLatin1(res.file.materials[i].name.c_str()),
                 kn5.materials[i].name);
        QCOMPARE(QString::fromLatin1(res.file.materials[i].shader.c_str()),
                 kn5.materials[i].shaderName);
    }
    QStringList engineNames;
    for (const auto& root : res.file.nodes)
        collectEngineMeshNames(root, engineNames);
    QStringList qtStaticNames;
    for (const Mesh& m : kn5.meshes)
        if (!m.isSkinnedMesh) qtStaticNames.append(m.name);
    QCOMPARE(engineNames, qtStaticNames);

    // --- write -> reparse: full-fidelity stability ---
    const QString out = m_dir + QStringLiteral("/roundtrip.kn5");
    QVERIFY2(KN5ParserImpl::write(out, kn5), qPrintable(KN5ParserImpl::lastError()));
    QString err2;
    const KN5File k2 = KN5ParserImpl::parse(out, &err2);
    QVERIFY2(err2.isEmpty(), qPrintable(err2));
    QVERIFY(k2.isValid());

    QCOMPARE(k2.header.version, 5u); // writer always emits v5
    QCOMPARE(k2.textures.size(), kn5.textures.size());
    QCOMPARE(k2.textureNames, kn5.textureNames);
    for (int i = 0; i < kn5.textures.size(); ++i) {
        QCOMPARE(k2.textures[i].name, kn5.textures[i].name);
        QVERIFY2(k2.textures[i].data == kn5.textures[i].data,
                 "texture payload mismatch");
        QCOMPARE(k2.textures[i].width, kn5.textures[i].width);
        QCOMPARE(k2.textures[i].height, kn5.textures[i].height);
        QCOMPARE(k2.textures[i].format, kn5.textures[i].format);
        QCOMPARE(k2.textures[i].mipmapCount, kn5.textures[i].mipmapCount);
    }

    QCOMPARE(k2.materials.size(), kn5.materials.size());
    for (int i = 0; i < kn5.materials.size(); ++i) {
        const Material& x = k2.materials[i];
        const Material& o = kn5.materials[i];
        QCOMPARE(x.name, o.name);
        QCOMPARE(x.shaderName, o.shaderName);
        QCOMPARE(x.blendMode, o.blendMode);
        QCOMPARE(x.depthMode, o.depthMode);
        QCOMPARE(int(x.type), int(o.type));
        QCOMPARE(x.rawProperties.size(), o.rawProperties.size());
        for (int j = 0; j < o.rawProperties.size() && j < x.rawProperties.size(); ++j) {
            QCOMPARE(x.rawProperties[j].name, o.rawProperties[j].name);
            QCOMPARE(x.rawProperties[j].value.size(), 40);
            // ValueA round-trips through QString::number (6 significant
            // digits); B/C/D are memcpy'd verbatim.
            float xa = 0, oa = 0;
            std::memcpy(&xa, x.rawProperties[j].value.constData(), 4);
            std::memcpy(&oa, o.rawProperties[j].value.constData(), 4);
            QVERIFY2(qAbs(xa - oa) <= qMax(1e-5f, qAbs(oa) * 1e-5f),
                     qPrintable(QStringLiteral("property '%1' ValueA drifted: %2 -> %3")
                                    .arg(o.rawProperties[j].name).arg(oa).arg(xa)));
            QVERIFY2(std::memcmp(x.rawProperties[j].value.constData() + 4,
                                 o.rawProperties[j].value.constData() + 4, 36) == 0,
                     qPrintable(QStringLiteral("property '%1' B/C/D blob drifted")
                                    .arg(o.rawProperties[j].name)));
        }
        QCOMPARE(x.rawMappings.size(), o.rawMappings.size());
        for (int j = 0; j < o.rawMappings.size() && j < x.rawMappings.size(); ++j) {
            QCOMPARE(x.rawMappings[j].name, o.rawMappings[j].name);
            QCOMPARE(x.rawMappings[j].slot, o.rawMappings[j].slot);
            QCOMPARE(x.rawMappings[j].texture, o.rawMappings[j].texture);
        }
    }

    QCOMPARE(k2.meshes.size(), kn5.meshes.size());
    for (int i = 0; i < kn5.meshes.size(); ++i) {
        const Mesh& x = k2.meshes[i];
        const Mesh& o = kn5.meshes[i];
        QCOMPARE(x.name, o.name);
        QCOMPARE(x.isSkinnedMesh, o.isSkinnedMesh);
        QCOMPARE(x.vertexData, o.vertexData); // canonical: byte-exact
        QCOMPARE(x.indexData, o.indexData);
        QCOMPARE(x.materialIndex, o.materialIndex);
        QCOMPARE(x.layer, o.layer);
        QCOMPARE(x.lodIn, o.lodIn);
        QCOMPARE(x.lodOut, o.lodOut);
        QCOMPARE(x.castShadows, o.castShadows);
        QCOMPARE(x.isVisible, o.isVisible);
        QCOMPARE(x.isTransparent, o.isTransparent);
        QCOMPARE(x.nodeActive, o.nodeActive);
        QCOMPARE(x.isRenderable, o.isRenderable);
        QCOMPARE(x.boundingRadius, o.boundingRadius);
        for (int r = 0; r < 4; ++r)
            for (int cIdx = 0; cIdx < 4; ++cIdx)
                QCOMPARE(x.worldMatrix.m[r][cIdx], o.worldMatrix.m[r][cIdx]);
        QCOMPARE(x.bones.size(), o.bones.size());
        for (int b = 0; b < o.bones.size() && b < x.bones.size(); ++b) {
            QCOMPARE(x.bones[b].name, o.bones[b].name);
            for (int k = 0; k < 16; ++k)
                QCOMPARE(x.bones[b].matrix[k], o.bones[b].matrix[k]);
        }
    }
}

QTEST_MAIN(TestKN5Parser)
#include "test_KN5Parser.moc"
