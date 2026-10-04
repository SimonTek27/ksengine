#include "KN5Baker.h"
#include "KN5Parser.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

namespace ks::tools {

namespace {

// Mirrors ks::sim::NativeVertex's layout exactly (position, normal, uv,
// color = 3+3+2+4 floats) — see src/simulator/NativeRenderer.h. Duplicated
// here rather than included, since NativeRenderer.h lives in the Qt-free
// src/simulator/ tree and this file is compiled as part of the Qt editor;
// the layout is a plain POD struct either way, so keeping the two in sync
// is just "don't reorder these fields in either file".
struct BakedVertex {
    float px = 0, py = 0, pz = 0;
    float nx = 0, ny = 0, nz = 0;
    float u = 0, v = 0;
    float r = 1, g = 1, b = 1, a = 1;
};

QString sanitizeFileName(const QString& name) {
    QString out = name;
    static const QString invalid = QStringLiteral("\\/:*?\"<>|");
    for (const QChar& c : invalid) out.replace(c, QLatin1Char('_'));
    if (out.isEmpty()) out = QStringLiteral("unnamed");
    return out;
}

// Applies a KN5 matrix to a position (w=1) or direction (w=0). Row-vector
// convention, p' = p * M, translation in m[3][0..2] — same as the engine's
// transformByMatrix in src/engine/FileFormat/Kn5Baker.cpp.
QVector3D transformPoint(const KN5Parser::Matrix4x4& m, const QVector3D& p, float w) {
    float x = p.x()*m.m[0][0] + p.y()*m.m[1][0] + p.z()*m.m[2][0] + w*m.m[3][0];
    float y = p.x()*m.m[0][1] + p.y()*m.m[1][1] + p.z()*m.m[2][1] + w*m.m[3][1];
    float z = p.x()*m.m[0][2] + p.y()*m.m[1][2] + p.z()*m.m[2][2] + w*m.m[3][2];
    return QVector3D(x, y, z);
}

bool writeNMSH(const std::string& path, const std::vector<BakedVertex>& verts, const std::vector<uint32_t>& indices) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write("NMSH", 4);
    uint32_t vCount = static_cast<uint32_t>(verts.size());
    uint32_t iCount = static_cast<uint32_t>(indices.size());
    f.write(reinterpret_cast<const char*>(&vCount), 4);
    f.write(reinterpret_cast<const char*>(&iCount), 4);
    f.write(reinterpret_cast<const char*>(verts.data()), static_cast<std::streamsize>(sizeof(BakedVertex) * verts.size()));
    f.write(reinterpret_cast<const char*>(indices.data()), static_cast<std::streamsize>(sizeof(uint32_t) * indices.size()));
    return f.good();
}

} // namespace

BakeResult bakeKN5ToNativeMeshes(const QString& kn5Path, const std::string& outputDir) {
    BakeResult result;

    QString err;
    KN5Parser::KN5File kn5 = KN5Parser::KN5ParserImpl::parse(kn5Path, &err);
    if (!kn5.isValid()) {
        result.error = err.isEmpty() ? QStringLiteral("KN5 parse failed (invalid or unreadable file)") : err;
        return result;
    }

    QDir dir;
    if (!dir.mkpath(QString::fromStdString(outputDir))) {
        result.error = QStringLiteral("could not create output directory: %1").arg(QString::fromStdString(outputDir));
        return result;
    }

    QFile manifestFile(QString::fromStdString(outputDir) + QStringLiteral("/manifest.txt"));
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.error = QStringLiteral("could not open manifest.txt for writing");
        return result;
    }
    QTextStream manifest(&manifestFile);

    for (const auto& meshConst : kn5.meshes) {
        if (meshConst.isSkinnedMesh) { result.meshesSkipped++; continue; }
        if (meshConst.vertexData.isEmpty()) { result.meshesSkipped++; continue; }

        KN5Parser::Mesh mesh = meshConst;
        mesh.decodeVertices();
        if (mesh.positions.isEmpty()) { result.meshesSkipped++; continue; }

        std::vector<BakedVertex> verts;
        verts.reserve(static_cast<size_t>(mesh.positions.size()));
        for (int i = 0; i < mesh.positions.size(); ++i) {
            BakedVertex bv;
            // mesh.worldMatrix is the accumulated transform from parse()
            // (root Base included), so nested node transforms bake correctly.
            QVector3D pos = transformPoint(mesh.worldMatrix, mesh.positions[i], 1.0f);
            bv.px = pos.x(); bv.py = pos.y(); bv.pz = pos.z();

            QVector3D n = (i < mesh.normals.size()) ? mesh.normals[i] : QVector3D(0, 1, 0);
            QVector3D nt = transformPoint(mesh.worldMatrix, n, 0.0f).normalized();
            bv.nx = nt.x(); bv.ny = nt.y(); bv.nz = nt.z();

            QVector2D uv = (i < mesh.uv0.size()) ? mesh.uv0[i] : QVector2D(0, 0);
            bv.u = uv.x(); bv.v = uv.y();
            // KN5 meshes carry their real color via the material/texture,
            // not a per-vertex color channel here — default to opaque white
            // so native_forward.frag's (color * light) falls back to
            // untinted lighting until material/texture support exists.
            verts.push_back(bv);
        }

        std::vector<uint32_t> indices;
        if (mesh.positions.size() <= 65535 && mesh.indexData.size() >= 2) {
            const uint16_t* src = reinterpret_cast<const uint16_t*>(mesh.indexData.constData());
            int count = mesh.indexData.size() / static_cast<int>(sizeof(uint16_t));
            indices.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) indices.push_back(src[i]);
        } else if (!mesh.indexData.isEmpty()) {
            const uint32_t* src = reinterpret_cast<const uint32_t*>(mesh.indexData.constData());
            int count = mesh.indexData.size() / static_cast<int>(sizeof(uint32_t));
            indices.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) indices.push_back(src[i]);
        }

        QString safeName = sanitizeFileName(mesh.name);
        std::string outPath = outputDir + "/" + safeName.toStdString() + ".nmsh";
        if (!writeNMSH(outPath, verts, indices)) {
            result.meshesSkipped++;
            continue;
        }

        manifest << safeName << "\n";
        result.meshesWritten++;
    }

    manifest.flush();
    manifestFile.close();
    result.success = true;
    return result;
}

} // namespace ks::tools
