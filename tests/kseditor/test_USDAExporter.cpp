#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>
#include "engine/FileFormat/USDAExporter.h"

using namespace ks::fileformat;

class TestUSDAExporter : public QObject {
    Q_OBJECT
private slots:
    void writesValidPreamble();
    void writesTriangleMeshData();
    void writesMaterialAndBinding();
    void sanitizesInvalidTokens();
    void rejectsNonTriangles();
};

void TestUSDAExporter::writesValidPreamble()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pre.usda");

    USDAExMesh mesh;
    mesh.name = "Tri";
    mesh.transform.setToIdentity();
    mesh.vertices.reserve(3);
    USDAExVertex v;
    v.x = 0; v.y = 0; v.z = 0; v.nz = 1; mesh.vertices.append(v);
    v.x = 1; v.y = 0; v.z = 0; v.nz = 1; mesh.vertices.append(v);
    v.x = 0; v.y = 1; v.z = 0; v.nz = 1; mesh.vertices.append(v);
    mesh.indices = { 0, 1, 2 };
    mesh.materialName = "Default";

    USDAExMaterial mat;
    mat.name = "Default";

    QString err;
    QVERIFY2(exportUSDA(path, { mesh }, { mat }, &err), qPrintable(err));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString txt = QString::fromUtf8(f.readAll());
    f.close();

    QVERIFY(txt.startsWith("#usda 1.0"));
    QVERIFY(txt.contains("defaultPrim = \"root\""));
    QVERIFY(txt.contains("upAxis = \"Y\""));
    QVERIFY(txt.contains("def Xform \"root\" {"));
}

void TestUSDAExporter::writesTriangleMeshData()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("mesh.usda");

    USDAExMesh mesh;
    mesh.name = "QuadMesh";
    mesh.transform.setToIdentity();
    for (float i = 0; i < 4; ++i) {
        USDAExVertex v;
        v.x = i; v.y = i * 2; v.z = i * 3;
        v.nx = 0; v.ny = 0; v.nz = 1;
        v.u = i / 4.0f; v.v = i / 4.0f;
        mesh.vertices.append(v);
    }
    mesh.indices = { 0, 1, 2, 0, 2, 3 };
    mesh.materialName = "M";

    USDAExMaterial mat;
    mat.name = "M";

    QString err;
    QVERIFY2(exportUSDA(path, { mesh }, { mat }, &err), qPrintable(err));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString txt = QString::fromUtf8(f.readAll());
    f.close();

    QVERIFY(txt.contains("def Xform \"QuadMesh\" {"));
    QVERIFY(txt.contains("def Mesh \"mesh\" {"));
    QVERIFY(txt.contains("int[] faceVertexCounts = ["));
    QVERIFY(txt.contains("int[] faceVertexIndices = [\n"));
    QVERIFY(txt.contains("3, 3"));
    QVERIFY(txt.contains("point3f[] points = ["));
    QVERIFY(txt.contains("normal3f[] normals = ["));
    QVERIFY(txt.contains("float2[] primvars:st = ["));
    // All 4 points present.
    QVERIFY(txt.contains("(3, 6, 9)"));
}

void TestUSDAExporter::writesMaterialAndBinding()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("mat.usda");

    USDAExMesh mesh;
    mesh.name = "Box";
    mesh.materialName = "Red Metal";
    mesh.vertices.append(USDAExVertex{});
    mesh.indices = { 0, 0, 0 };

    USDAExMaterial mat;
    mat.name = "Red Metal";
    mat.baseColor = QColor(180, 20, 20);
    mat.metallic = 0.8f;
    mat.roughness = 0.35f;
    mat.baseColorTexture = "C:/tx/albedo.png";

    QString err;
    QVERIFY2(exportUSDA(path, { mesh }, { mat }, &err), qPrintable(err));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString txt = QString::fromUtf8(f.readAll());
    f.close();

    // Material prim + binding rel + preview surface values.
    QVERIFY(txt.contains("def Material \"Red_Metal\" {"));
    QVERIFY(txt.contains("rel material:binding = </root/Materials/Red_Metal>"));
    QVERIFY(txt.contains("uniform token info:id = \"UsdPreviewSurface\""));
    QVERIFY(txt.contains("color3f inputs:diffuseColor.connect"));
    QVERIFY(txt.contains("asset inputs:file = @C:/tx/albedo.png@"));
}

void TestUSDAExporter::sanitizesInvalidTokens()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("san.usda");

    USDAExMesh mesh;
    mesh.name = "3D item.1";
    mesh.materialName = "mat(1)";
    mesh.vertices.append(USDAExVertex{});
    mesh.indices = { 0, 0, 0 };

    USDAExMaterial mat;
    mat.name = "mat(1)";

    QString err;
    QVERIFY2(exportUSDA(path, { mesh }, { mat }, &err), qPrintable(err));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString txt = QString::fromUtf8(f.readAll());
    f.close();

    QVERIFY(txt.contains("def Xform \"_3D_item_1\" {"));
    QVERIFY(txt.contains("def Material \"mat_1_\" {"));
    QVERIFY(txt.contains("rel material:binding = </root/Materials/mat_1_>"));
}

void TestUSDAExporter::rejectsNonTriangles()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("bad.usda");

    USDAExMesh mesh;
    mesh.name = "Bad";
    mesh.vertices.append(USDAExVertex{});
    mesh.vertices.append(USDAExVertex{});
    mesh.indices = { 0, 1 };   // not a triangle list

    USDAExMaterial mat;
    mat.name = "M";

    QString err;
    QVERIFY(!exportUSDA(path, { mesh }, { mat }, &err));
    QVERIFY(err.contains("triangle list"));
}

QTEST_MAIN(TestUSDAExporter)
#include "test_USDAExporter.moc"
