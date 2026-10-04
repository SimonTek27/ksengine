#include <QtTest/QtTest>
#include "engine/Tools/LODSystem.h"
using namespace ks;

class TestLODGenerator : public QObject {
    Q_OBJECT

private slots:
    void testMakeLODLevels();
    void testCountTriangles();
    void testDecimateIdentity();
    void testDecimateHalf();
    void testOptimizeMesh();
    void testUnsubdivide();
    void testSimplifyMeshQuadric();
    void testGenerateAutoLODs();
    void testGenerateCustomLODs();
};

static MeshData makeTestMesh()
{
    MeshData mesh;
    mesh.vertices.resize(8);
    mesh.vertices[0].position = QVector3D(0,0,0);
    mesh.vertices[1].position = QVector3D(1,0,0);
    mesh.vertices[2].position = QVector3D(1,1,0);
    mesh.vertices[3].position = QVector3D(0,1,0);
    mesh.vertices[4].position = QVector3D(0,0,1);
    mesh.vertices[5].position = QVector3D(1,0,1);
    mesh.vertices[6].position = QVector3D(1,1,1);
    mesh.vertices[7].position = QVector3D(0,1,1);
    mesh.faces = {
        Face{0,1,2,3}, Face{4,5,6,7},
        Face{0,1,5,4}, Face{2,3,7,6},
        Face{0,3,7,4}, Face{1,2,6,5}
    };
    mesh.computeNormals();
    mesh.computeBoundingBox();
    return mesh;
}

void TestLODGenerator::testMakeLODLevels()
{
    LODLevel lod0 = LODGenerator::makeLOD0();
    QCOMPARE(lod0.decimateRatio, 1.0f);
    QCOMPARE(lod0.name, QString("LOD0_High"));

    LODLevel lod1 = LODGenerator::makeLOD1();
    QVERIFY(lod1.decimateRatio < 1.0f);
    QVERIFY(lod1.distance > 0);

    LODLevel lod3 = LODGenerator::makeLOD3();
    QCOMPARE(lod3.name, QString("LOD3_Impostor"));
}

void TestLODGenerator::testCountTriangles()
{
    MeshData mesh = makeTestMesh();
    LODResult result = LODGenerator::generateAutoLODs(mesh, 1);
    QCOMPARE(result.originalTriCount, 6);
}

void TestLODGenerator::testDecimateIdentity()
{
    MeshData mesh = makeTestMesh();
    MeshData result = LODGenerator::decimateMesh(mesh, 1.0f);
    QCOMPARE(result.vertices.size(), mesh.vertices.size());
    QCOMPARE(result.faces.size(), mesh.faces.size());
}

void TestLODGenerator::testDecimateHalf()
{
    MeshData mesh = makeTestMesh();
    MeshData result = LODGenerator::decimateMesh(mesh, 0.5f);
    QVERIFY(result.faces.size() <= mesh.faces.size());
    QVERIFY(result.faces.size() >= 3);
    QVERIFY(!result.vertices.isEmpty());
}

void TestLODGenerator::testOptimizeMesh()
{
    MeshData mesh = makeTestMesh();
    int origVerts = mesh.vertices.size();
    LODGenerator::optimizeMeshForLOD(mesh);
    QVERIFY(!mesh.vertices.isEmpty());
    QCOMPARE(mesh.faces.size(), 6);
}

void TestLODGenerator::testUnsubdivide()
{
    MeshData mesh;
    mesh.vertices.resize(4);
    mesh.vertices[0].position = QVector3D(0,0,0);
    mesh.vertices[1].position = QVector3D(1,0,0);
    mesh.vertices[2].position = QVector3D(1,1,0);
    mesh.vertices[3].position = QVector3D(0,1,0);
    mesh.faces = {Face{0,1,2}, Face{0,2,3}};

    MeshData result = LODGenerator::unsubdivideMesh(mesh, 1);
    QVERIFY(!result.faces.isEmpty());
}

void TestLODGenerator::testSimplifyMeshQuadric()
{
    MeshData mesh = makeTestMesh();
    MeshData result = LODGenerator::simplifyMeshQuadric(mesh, 3);
    QVERIFY(result.faces.size() <= 3);
    QVERIFY(result.faces.size() >= 1);
}

void TestLODGenerator::testGenerateAutoLODs()
{
    MeshData mesh = makeTestMesh();
    LODResult result = LODGenerator::generateAutoLODs(mesh, 3);
    QCOMPARE(result.levels.size(), 3);
    QCOMPARE(result.originalTriCount, 6);
    QVERIFY(result.levels[0].faces.size() >= result.levels[1].faces.size());
    QVERIFY(result.levels[1].faces.size() >= result.levels[2].faces.size());
}

void TestLODGenerator::testGenerateCustomLODs()
{
    MeshData mesh = makeTestMesh();
    QVector<LODLevel> levels = {LODGenerator::makeLOD0(), LODGenerator::makeLOD1()};
    LODResult result = LODGenerator::generateCustomLODs(mesh, levels);
    QCOMPARE(result.levels.size(), 2);
}

QTEST_MAIN(TestLODGenerator)
#include "test_LODGenerator.moc"
