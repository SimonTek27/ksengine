#include "engine/mesh/BooleanOps.h"
#include <QtTest>
#include <QVector3D>
#include <QString>

using namespace ks::geometry;

static GeoMeshData createCube(const QVector3D& position, float size) {
    GeoMeshData cube;
    float x = position.x(), y = position.y(), z = position.z();
    cube.vertices = {
        GeoVertex(x, y, z),
        GeoVertex(x + size, y, z),
        GeoVertex(x + size, y + size, z),
        GeoVertex(x, y + size, z),
        GeoVertex(x, y, z + size),
        GeoVertex(x + size, y, z + size),
        GeoVertex(x + size, y + size, z + size),
        GeoVertex(x, y + size, z + size),
    };
    cube.faces = {
        GeoFace(0, 3, 2), GeoFace(0, 2, 1),
        GeoFace(4, 5, 6), GeoFace(4, 6, 7),
        GeoFace(0, 1, 5), GeoFace(0, 5, 4),
        GeoFace(1, 2, 6), GeoFace(1, 6, 5),
        GeoFace(2, 3, 7), GeoFace(2, 7, 6),
        GeoFace(3, 0, 4), GeoFace(3, 4, 7),
    };
    return cube;
}

class TestBooleanOps : public QObject {
    Q_OBJECT

private slots:
    void testCanPerform() {
    if (!BooleanOperations::canPerform()) QSKIP("Boolean backend disabled (no CGAL)");
        QVERIFY(BooleanOperations::canPerform());
    }

    void testMeshValidation() {
        GeoMeshData emptyMesh;
        QString err = QString::fromStdString(BooleanOperations::validateMesh(emptyMesh));
        QVERIFY(!err.isEmpty());

        GeoMeshData triangle;
        triangle.vertices.push_back(GeoVertex(0, 0, 0));
        triangle.vertices.push_back(GeoVertex(1, 0, 0));
        triangle.vertices.push_back(GeoVertex(0, 1, 0));
        triangle.faces.push_back(GeoFace(0, 1, 2));
        err = QString::fromStdString(BooleanOperations::validateMesh(triangle));
        QVERIFY(err.isEmpty());
    }

    void testUnionSimpleCubes() {
        GeoMeshData cube1 = createCube(QVector3D(0, 0, 0), 1.0);
        GeoMeshData cube2 = createCube(QVector3D(0.5, 0.5, 0), 1.0);
        auto result = BooleanOperations::performOperation(cube1, cube2, BooleanOperations::Union);
        QVERIFY(result.isSuccess());
        QVERIFY(result.result.vertices.size() > 0);
    }

    void testDifferenceWithHole() {
    if (!BooleanOperations::canPerform()) QSKIP("Boolean backend disabled (no CGAL)");
        GeoMeshData base = createCube(QVector3D(0, 0, 0), 2.0);
        GeoMeshData hole = createCube(QVector3D(0.5, 0.5, 0), 0.5);
        auto result = BooleanOperations::performOperation(base, hole, BooleanOperations::Difference);
        QVERIFY(result.isSuccess());
        QVERIFY(result.result.vertices.size() > 0);
    }

    void testIntersectionSimple() {
    if (!BooleanOperations::canPerform()) QSKIP("Boolean backend disabled (no CGAL)");
        GeoMeshData cube1 = createCube(QVector3D(0, 0, 0), 1.0);
        GeoMeshData cube2 = createCube(QVector3D(0.5, 0.5, 0), 1.0);
        auto result = BooleanOperations::performOperation(cube1, cube2, BooleanOperations::Intersection);
        QVERIFY(result.isSuccess());
        QVERIFY(result.result.vertices.size() > 0);
    }

    void testInvalidInput() {
    if (!BooleanOperations::canPerform()) QSKIP("Boolean backend disabled (no CGAL)");
        GeoMeshData emptyMesh;
        GeoMeshData cube = createCube(QVector3D(0, 0, 0), 1.0);
        auto result = BooleanOperations::performOperation(emptyMesh, cube, BooleanOperations::Union);
        QVERIFY(!result.isSuccess());
        QVERIFY(!result.errorMessage.empty());
    }

    void testRepairMesh() {
        GeoMeshData meshWithIssues;
        meshWithIssues.vertices.push_back(GeoVertex(0, 0, 0));
        meshWithIssues.vertices.push_back(GeoVertex(1, 0, 0));
        meshWithIssues.vertices.push_back(GeoVertex(1, 0, 0));
        meshWithIssues.vertices.push_back(GeoVertex(0, 1, 0));
        meshWithIssues.faces.push_back(GeoFace(0, 1, 2));
        meshWithIssues.faces.push_back(GeoFace(0, 1, 3));
        auto repaired = BooleanOperations::repairMesh(meshWithIssues);
        QVERIFY(repaired.isValid());
    }

    void testOperationName() {
        QCOMPARE(BooleanOperations::operationName(BooleanOperations::Union), "Union");
        QCOMPARE(BooleanOperations::operationName(BooleanOperations::Difference), "Difference");
        QCOMPARE(BooleanOperations::operationName(BooleanOperations::Intersection), "Intersection");
        QCOMPARE(BooleanOperations::operationName(BooleanOperations::SymmetricDiff), "Symmetric Diff");
    }

    void testComputeNormals() {
        GeoMeshData cube = createCube(QVector3D(0, 0, 0), 1.0);
        auto cubeWithNormals = BooleanOperations::computeNormals(cube);
        QVERIFY(cubeWithNormals.normals.size() > 0);
    }

    void testBatchOperations() {
    if (!BooleanOperations::canPerform()) QSKIP("Boolean backend disabled (no CGAL)");
        GeoMeshData cube1 = createCube(QVector3D(0, 0, 0), 1.0);
        GeoMeshData cube2 = createCube(QVector3D(0.5, 0.5, 0), 1.0);
        GeoMeshData cube3 = createCube(QVector3D(1.0, 0.0, 0.5), 1.0);
        std::vector<GeoMeshData> meshes = {cube1, cube2, cube3};
        std::vector<BooleanOperations::Operation> ops = {
            BooleanOperations::Union, BooleanOperations::Difference
        };
        auto result = BooleanOperations::performBatchOperations(meshes, ops);
        QVERIFY(result.isSuccess());
    }
};

QTEST_MAIN(TestBooleanOps)
#include "test_BooleanOps.moc"
