#include "USDAExporter.h"
#include <QFile>
#include <QTextStream>
#include <QSet>
#include <QLocale>

namespace ks {
namespace fileformat {

namespace {

// USD identifier tokens must only contain [A-Za-z0-9_] at minimum; replace
// everything else with '_' so prim paths stay valid in text USD.
QString sanitizeToken(QString s) {
    if (s.isEmpty()) s = "prim";
    QString out;
    out.reserve(s.size());
    for (const QChar& ch : s) {
        if (ch.isLetterOrNumber() || ch == L'_')
            out.append(ch);
        else
            out.append(L'_');
    }
    if (out.isEmpty() || out[0].isDigit())
        out.prepend(L'_');
    return out;
}

void writeVecArray(QTextStream& out, const QString& indent, const QString& decl,
                   const QVector<QVector3D>& data, int perLine = 3) {
    out << indent << decl << " = [";
    for (int i = 0; i < data.size(); ++i) {
        const QVector3D& v = data[i];
        if (i % perLine == 0) out << "\n" << indent << "    ";
        out << "(" << QString::number(double(v.x()), 'g', 9) << ", "
            << QString::number(double(v.y()), 'g', 9) << ", "
            << QString::number(double(v.z()), 'g', 9) << ")";
        if (i < data.size() - 1) out << ",";
    }
    out << "\n" << indent << "]\n";
}

void writeFloat2Array(QTextStream& out, const QString& indent, const QString& decl,
                      const QVector<QVector2D>& data, int perLine = 4) {
    out << indent << decl << " = [";
    for (int i = 0; i < data.size(); ++i) {
        const QVector2D& v = data[i];
        if (i % perLine == 0) out << "\n" << indent << "    ";
        out << "(" << QString::number(double(v.x()), 'g', 9) << ", "
            << QString::number(double(v.y()), 'g', 9) << ")";
        if (i < data.size() - 1) out << ",";
    }
    out << "\n" << indent << "]\n";
}

void writeIntArray(QTextStream& out, const QString& indent, const QString& decl,
                   const QVector<uint32_t>& data, int perLine = 24) {
    out << indent << decl << " = [";
    for (int i = 0; i < data.size(); ++i) {
        if (i % perLine == 0) out << "\n" << indent << "    ";
        out << QString::number(data[i]);
        if (i < data.size() - 1) out << ", ";
    }
    out << "\n" << indent << "]\n";
}

void writePrim(QTextStream& out, const USDAExMesh& mesh, int index) {
    const QString primName = sanitizeToken(mesh.name);

    out << "    def Xform \"" << primName << "\" {\n";
    if (index >= 0) {
        const float* m = mesh.transform.constData();
        out << "        matrix4d xformOp:transform = (";
        for (int i = 0; i < 16; ++i) {
            if (i % 4 == 0) out << "\n            ";
            else out << ", ";
            out << QString::number(m[i], 'g', 9);
        }
        out << ")\n";
        out << "        uniform token[] xformOpOrder = [\"xformOp:transform\"]\n";
    }

    out << "        def Mesh \"mesh\" {\n";
    if (!mesh.materialName.isEmpty())
        out << "            rel material:binding = </root/Materials/"
            << sanitizeToken(mesh.materialName) << ">\n";

    QVector<QVector3D> points, normals;
    QVector<QVector2D> uvs;
    points.reserve(mesh.vertices.size());
    normals.reserve(mesh.vertices.size());
    uvs.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        points.append(QVector3D(v.x, v.y, v.z));
        normals.append(QVector3D(v.nx, v.ny, v.nz));
        uvs.append(QVector2D(v.u, v.v));
    }

    writeIntArray(out, "            ", "int[] faceVertexCounts",
                  QVector<uint32_t>(mesh.indices.size() / 3, 3u));
    writeIntArray(out, "            ", "int[] faceVertexIndices", mesh.indices);
    writeVecArray(out, "            ", "point3f[] points", points);
    writeVecArray(out, "            ", "normal3f[] normals", normals);
    writeFloat2Array(out, "            ", "float2[] primvars:st", uvs);

    out << "        }\n";
    out << "    }\n\n";
}

void writeMaterial(QTextStream& out, const USDAExMaterial& source, int index) {
    const QString matName = sanitizeToken(source.name);
    out << "    def Material \"" << matName << "\" {\n";
    out << "        token outputs:surface.connect = </root/Materials/"
        << matName << "/PreviewSurface.outputs:surface>\n";

    // Optional UsdUVTexture maps wired into the preview surface inputs.
    const QString texNames[] = {
        source.baseColorTexture.isEmpty() ? QString() : QStringLiteral("baseColorTexture"),
        source.normalTexture.isEmpty() ? QString() : QStringLiteral("normalTexture"),
        source.roughnessTexture.isEmpty() ? QString() : QStringLiteral("roughnessMap"),
        source.metallicTexture.isEmpty() ? QString() : QStringLiteral("metallicMap")
    };
    const bool hasDiffuseMap = !source.baseColorTexture.isEmpty();
    const bool hasNormalMap = !source.normalTexture.isEmpty();

    QString baseColorVal = QString("(%1, %2, %3)")
        .arg(source.baseColor.redF(), 0, 'g', 4)
        .arg(source.baseColor.greenF(), 0, 'g', 4)
        .arg(source.baseColor.blueF(), 0, 'g', 4);

    out << "        def Shader \"PreviewSurface\" {\n";
    out << "            uniform token info:id = \"UsdPreviewSurface\"\n";
if (hasDiffuseMap)
        out << "            color3f inputs:diffuseColor.connect = </root/Materials/"
        << matName << "/baseColorTexture.outputs:rgb>\n";
    else
        out << "            color3f inputs:diffuseColor = " << baseColorVal << "\n";
    out << "            float inputs:metallic = " << source.metallic << "\n";
    out << "            float inputs:roughness = " << source.roughness << "\n";
    out << "            color3f inputs:emissiveColor = ("
        << source.emissive.redF() << ", " << source.emissive.greenF() << ", "
        << source.emissive.blueF() << ")\n";
    out << "            float inputs:opacity = " << source.opacity << "\n";
    out << "        }\n";

    // Write one StbImage/UsdUVTexture node per unique texture.
    const QString texPrim[] = { "baseColorTexture", "normalTexture", "roughnessMap", "metallicMap" };
    const QString texPaths[] = { source.baseColorTexture, source.normalTexture,
                                 source.roughnessTexture, source.metallicTexture };
    for (int i = 0; i < 4; ++i) {
        if (texNames[i].isEmpty()) continue;
        out << "        def Shader \"" << texPrim[i] << "\" {\n";
        out << "            uniform token info:id = \"UsdUVTexture\"\n";
        out << "            asset inputs:file = @"
            << QString(texPaths[i]).replace('\\', '/') << "@\n";
        if (i == 1)
            out << "            float4 inputs:bias = (0.5, 0.5, 0.5, 0)\n"
                << "            float4 inputs:scale = (1, 1, 1, 1)\n";
        out << "            token inputs:wrapS = \"repeat\"\n";
        out << "            token inputs:wrapT = \"repeat\"\n";
        out << "            float2 inputs:st.connect = </root/Materials/"
            << matName << "/stReader.outputs:result>\n";
        out << "        }\n";
    }
    out << "        def Shader \"stReader\" {\n";
    out << "            uniform token info:id = \"UsdPrimvarReader_float2\"\n";
    out << "            string inputs:varname = \"st\"\n";
    out << "        }\n";
    out << "    }\n\n";
}

} // anonymous namespace

bool exportUSDA(const QString& path,
                const QVector<USDAExMesh>& meshes,
                const QVector<USDAExMaterial>& materials,
                QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Cannot open file for writing: " + path;
        return false;
    }

    for (const auto& mesh : meshes) {
        if (mesh.indices.size() % 3 != 0) {
            if (error) *error = "Mesh '" + mesh.name + "' indices are not a triangle list";
            return false;
        }
    }

    QTextStream out(&file);
    out.setLocale(QLocale::c());

    out << "#usda 1.0\n";
    out << "(\n";
    out << "    defaultPrim = \"root\"\n";
    out << "    metersPerUnit = 1\n";
    out << "    upAxis = \"Y\"\n";
    out << ")\n\n";
    out << "def Xform \"root\" {\n";
    out << "    def Scope \"Materials\" {\n\n";

    for (const auto& mat : materials)
        writeMaterial(out, mat, 0);

    out << "    }\n\n";

    int i = 0;
    for (const auto& mesh : meshes)
        writePrim(out, mesh, i++);

    out << "}\n";
    file.close();
    return true;
}

} // namespace fileformat
} // namespace ks