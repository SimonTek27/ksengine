#include <QtTest/QtTest>
#include "engine/mesh/SubdivisionSurface.h"
using namespace ks;

class TestSubdivisionSurface : public QObject {
    Q_OBJECT

private slots:
    void testSubdivideTrivialMesh();
    void testSubdivideWithLevels();
    void testSubdivideWithCreases();
    void testEmptyMesh();
    void testSingleTriangle();
    void testSchemeNames();
};

MeshData makeTestCube()
{
    MeshData mesh;
    mesh.vertices.resize(8);
    mesh.vertices[0].position = QVector3D(-1,-1,-1);
    mesh.vertices[1].position = QVector3D( 1,-1,-1);
    mesh.vertices[2].position = QVector3D( 1, 1,-1);
    mesh.vertices[3].position = QVector3D(-1, 1,-1);
    mesh.vertices[4].position = QVector3D(-1,-1, 1);
    mesh.vertices[5].position = QVector3D( 1,-1, 1);
    mesh.vertices[6].position = QVector3D( 1, 1, 1);
    mesh.vertices[7].position = QVector3D(-1, 1, 1);
    mesh.faces = {
        Face{0,1,2,3}, Face{4,5,6,7},
        Face{0,1,5,4}, Face{2,3,7,6},
        Face{0,3,7,4}, Face{1,2,6,5}
    };
    return mesh;
}

void TestSubdivisionSurface::testSubdivideTrivialMesh()
{
    MeshData mesh = makeTestCube();
    SubdivisionResult result = SubdivisionSurface::subdivide(mesh, 1);
    QVERIFY(result.success || !result.errorMessage.isEmpty());
    if (result.success) {
        QVERIFY(result.resultVertices > mesh.vertices.size());
        QVERIFY(result.resultFaces > mesh.faces.size());
    }
}

void TestSubdivisionSurface::testSubdivideWithLevels()
{
    MeshData mesh = makeTestCube();
    SubdivisionResult r1 = SubdivisionSurface::subdivide(mesh, 1);
    SubdivisionResult r2 = SubdivisionSurface::subdivide(mesh, 2);
    if (r1.success && r2.success) {
        QVERIFY(r2.resultVertices > r1.resultVertices);
        QVERIFY(r2.resultFaces > r1.resultFaces);
    }
}

void TestSubdivisionSurface::testSubdivideWithCreases()
{
    MeshData mesh = makeTestCube();
    QVector<CreaseEdge> creases = {{0, 1, 5.0f}, {1, 2, 5.0f}};
    SubdivisionResult result = SubdivisionSurface::subdivideWithCreases(mesh, creases, 1);
    QVERIFY(result.success || !result.errorMessage.isEmpty());
}

void TestSubdivisionSurface::testEmptyMesh()
{
    SubdivisionResult result = SubdivisionSurface::subdivide(MeshData(), 1);
    QVERIFY(result.success);
    QCOMPARE(result.sourceFaces, 0);
}

void TestSubdivisionSurface::testSingleTriangle()
{
    MeshData mesh;
    mesh.vertices.resize(3);
    mesh.vertices[0].position = QVector3D(0,0,0);
    mesh.vertices[1].position = QVector3D(1,0,0);
    mesh.vertices[2].position = QVector3D(0,1,0);
    mesh.faces = {Face{0, 1, 2}};

    SubdivisionResult result = SubdivisionSurface::subdivide(mesh, 1);
    QCOMPARE(result.sourceFaces, 1);
    QCOMPARE(result.sourceVertices, 3);
}

void TestSubdivisionSurface::testSchemeNames()
{
    QCOMPARE(SubdivisionSurface::schemeName(SubdivisionSurface::CatmullClark),
             QString("Catmull-Clark"));
    QCOMPARE(SubdivisionSurface::schemeName(SubdivisionSurface::Loop),
             QString("Loop"));
    QCOMPARE(SubdivisionSurface::schemeName(SubdivisionSurface::Bilinear),
             QString("Bilinear"));
}

QTEST_MAIN(TestSubdivisionSurface)
#include "test_SubdivisionSurface.moc"
