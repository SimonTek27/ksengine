#include "engine/mesh/MeshOperations.h"
#include <QtTest/QtTest>

using namespace ks;

static Vertex V(const QVector3D& p) { Vertex v; v.position = p; return v; }

class TestMeshOperations : public QObject {
    Q_OBJECT

private slots:
    void testCreateBox();
    void testCreateSphere();
    void testCreateCylinder();
    void testCreateCone();
    void testCreatePlane();
    void testCreateTorus();
    void testCreateGrid();
    void testCreateIcosphere();
    void testTriangulate();
    void testMirror();
    void testArray();
    void testRadialArray();
    void testMergeMeshes();
    void testWeldVertices();
    void testSubdivide();
    void testDisplace();
    void testFlipFaces();
    void testComputeNormals();
    void testBoundingBox();
    void testShell();
    void testBridgeEdges();
    void testBridgeFaces();
    void testAutoSmooth();
    void testBorderEdges();
    void testFaceElements();
    void testPushPullSolid();
    void testOffsetFacesSolid();
    void testExtendedSculptBrushes();
    void testAdvancedBrushesFoldsPores();
    void testBulgeSlashBrushes();
    void testSculptPin();
    void testSculptFalloffPower();
    void testSnapPointToMesh();
    void testFillHoles();
    void testExtractFaces();
    void testFalloffFactor();
    void testTransformAround();
    void testSplitSmoothingGroups();
    void testBevelEdges();
    void testResolveUVOverlaps();
    void testRevolveSketch();
    void testBevelChain();
    void testBevelProfiles();
    void testRetargetSkeleton();

    // Quilt of `cols x rows` quads lying in the XZ plane (normal +Y).
    MeshData buildQuilt(int cols, int rows) const {
        MeshData mesh;
        for (int r = 0; r <= rows; ++r) {
            for (int c = 0; c <= cols; ++c) {
                Vertex v;
                v.position = QVector3D(float(c), 0.0f, float(r));
                mesh.vertices.append(v);
            }
        }
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int tl = r * (cols + 1) + c;
                Face f;
                f.indices = { tl + (cols + 1), tl + (cols + 1) + 1, tl + 1, tl };
                mesh.faces.append(f);
            }
        }
        mesh.computeNormals();
        mesh.computeBoundingBox();
        return mesh;
    }
};

void TestMeshOperations::testCreateBox()
{
    MeshData box = MeshOperations::createBox(2.0f, 1.0f, 3.0f);
    QVERIFY(box.getVertexCount() > 0);
    QVERIFY(box.getTriangleCount() > 0);
    QCOMPARE(box.getTriangleCount(), 12);
}

void TestMeshOperations::testCreateSphere()
{
    MeshData sphere = MeshOperations::createSphere(0.5f, 16, 8);
    QVERIFY(sphere.getVertexCount() > 0);
    QVERIFY(sphere.getTriangleCount() > 0);
}

void TestMeshOperations::testCreateCylinder()
{
    MeshData cyl = MeshOperations::createCylinder(0.5f, 1.0f, 16);
    QVERIFY(cyl.getVertexCount() > 0);
    QVERIFY(cyl.getTriangleCount() > 0);
}

void TestMeshOperations::testCreateCone()
{
    MeshData cone = MeshOperations::createCone(0.5f, 1.0f, 16);
    QVERIFY(cone.getVertexCount() > 0);
    QVERIFY(cone.getTriangleCount() > 0);
}

void TestMeshOperations::testCreatePlane()
{
    MeshData plane = MeshOperations::createPlane(2.0f, 2.0f, 2, 2);
    QVERIFY(plane.getVertexCount() >= 9);
    QVERIFY(plane.getTriangleCount() >= 8);
}

void TestMeshOperations::testCreateTorus()
{
    MeshData torus = MeshOperations::createTorus(0.5f, 0.2f, 16, 8);
    QVERIFY(torus.getVertexCount() > 0);
    QVERIFY(torus.getTriangleCount() > 0);
}

void TestMeshOperations::testCreateGrid()
{
    MeshData grid = MeshOperations::createGrid(2.0f, 2.0f, 10, 10);
    QVERIFY(grid.getVertexCount() >= 121);
    QVERIFY(grid.getTriangleCount() >= 200);
}

void TestMeshOperations::testCreateIcosphere()
{
    MeshData ico = MeshOperations::createIcosphere(0.5f, 2);
    QVERIFY(ico.getVertexCount() > 0);
    QVERIFY(ico.getTriangleCount() > 0);
}

void TestMeshOperations::testTriangulate()
{
    MeshData mesh;
    mesh.vertices.resize(4);
    mesh.vertices[0].position = QVector3D(0,0,0);
    mesh.vertices[1].position = QVector3D(1,0,0);
    mesh.vertices[2].position = QVector3D(1,1,0);
    mesh.vertices[3].position = QVector3D(0,1,0);
    mesh.faces.append(Face{0,1,2,3});

    QCOMPARE(mesh.getTriangleCount(), 1);
    mesh.triangulate();
    QCOMPARE(mesh.getTriangleCount(), 2);
}

void TestMeshOperations::testMirror()
{
    MeshData box = MeshOperations::createBox(1,1,1);
    MeshData mirrored = MeshOperations::mirror(box, QVector3D(1,0,0));
    QVERIFY(mirrored.getVertexCount() > box.getVertexCount());
}

void TestMeshOperations::testArray()
{
    MeshData box = MeshOperations::createBox(0.5f, 0.5f, 0.5f);
    MeshData arr = MeshOperations::array(box, 5, QVector3D(1,0,0));
    QVERIFY(arr.getVertexCount() > box.getVertexCount());
}

void TestMeshOperations::testRadialArray()
{
    MeshData box = MeshOperations::createBox(0.5f, 0.5f, 0.5f);
    MeshData arr = MeshOperations::radialArray(box, 8, QVector3D(0,1,0), 360.0f);
    QVERIFY(arr.getVertexCount() > box.getVertexCount());
}

void TestMeshOperations::testMergeMeshes()
{
    MeshData a = MeshOperations::createBox(1,1,1);
    MeshData b = MeshOperations::createSphere(0.5f);
    int before = a.getVertexCount();
    MeshOperations::mergeMeshes(a, b);
    QVERIFY(a.getVertexCount() > before);
}

void TestMeshOperations::testWeldVertices()
{
    MeshData mesh;
    mesh.vertices.resize(6);
    for (int i = 0; i < 6; ++i)
        mesh.vertices[i].position = QVector3D(i < 3 ? 0.0f : 1.0f, 0, 0);
    mesh.faces.append(Face{0,1,2});
    mesh.faces.append(Face{3,4,5});
    MeshData welded = MeshOperations::weldVertices(mesh, 0.01f);
    QVERIFY(welded.getVertexCount() <= mesh.getVertexCount());
}

void TestMeshOperations::testSubdivide()
{
    MeshData box = MeshOperations::createBox(1,1,1);
    MeshData subdivided = MeshOperations::subdivide(box, 1);
    QVERIFY(subdivided.getVertexCount() > box.getVertexCount());
    QVERIFY(subdivided.getTriangleCount() > box.getTriangleCount());
}

void TestMeshOperations::testDisplace()
{
    MeshData plane = MeshOperations::createPlane(2, 2, 4, 4);
    QImage heightmap(16, 16, QImage::Format_Grayscale8);
    heightmap.fill(128);
    MeshData displaced = MeshOperations::displace(plane, heightmap, 1.0f);
    QCOMPARE(displaced.getVertexCount(), plane.getVertexCount());
}

void TestMeshOperations::testFlipFaces()
{
    MeshData box = MeshOperations::createBox(1,1,1);
    auto normsBefore = box.normals;
    box.flipFaces();
    box.computeNormals();
}

void TestMeshOperations::testComputeNormals()
{
    MeshData box = MeshOperations::createBox(1,1,1);
    box.computeNormals();
    QCOMPARE(box.normals.size(), box.vertices.size());
    for (const auto& n : box.normals) {
        QVERIFY(!qIsNaN(n.length()));
    }
}

void TestMeshOperations::testBoundingBox()
{
    MeshData box = MeshOperations::createBox(2, 4, 6);
    box.computeBoundingBox();
    QCOMPARE(box.boundingBoxMin, QVector3D(-1, -2, -3));
    QCOMPARE(box.boundingBoxMax, QVector3D(1, 2, 3));
}

void TestMeshOperations::testShell()
{
    // A flat open quad (single face) thickens into a closed volume.
    MeshData plane;
    for (const auto& p : { QVector3D(-1, 0, -1), QVector3D(1, 0, -1),
                           QVector3D(1, 0, 1),  QVector3D(-1, 0, 1) })
        plane.vertices.append(V(p));
    Face f; f.indices = { 0, 1, 2, 3 };
    plane.faces = { f };

    MeshData shelled = MeshOperations::shell(plane, 0.5f, 1, false);
    QVERIFY(shelled.vertices.size() == 8);          // 4 original + 4 offset
    QVERIFY(shelled.faces.size() == 6);             // front + back + 4 rim walls
    QVERIFY(shelled.faces[0].indices.size() > 0);
    // The offset copy's first vertex is displaced by `thickness` along its normal.
    float d1 = (shelled.vertices[4].position - shelled.vertices[0].position).length();
    QVERIFY(qAbs(d1 - 0.5f) < 1e-3f);

    // Negative direction displaces the same distance but opposite sign.
    MeshData inner = MeshOperations::shell(plane, 0.5f, -1, false);
    float d2 = (inner.vertices[4].position - inner.vertices[0].position).length();
    QVERIFY(qAbs(d2 - 0.5f) < 1e-3f);
}

void TestMeshOperations::testBridgeEdges()
{
    MeshData a = MeshOperations::createPlane(1, 1, 2, 2);
    QVERIFY(a.vertices.size() > 0);

    // Take the first 4 vertices as loop A and a shifted copy as loop B.
    QVector<int> loopA = { 0, 1, 2, 3 };
    QVector<int> loopB;
    for (int i = 0; i < 4; ++i)
        loopB.append(a.vertices.size() + i);
    for (int i = 0; i < 4; ++i) {
        Vertex v = a.vertices[i];
        v.position += QVector3D(0, 1, 0);
        a.vertices.append(v);
    }

    MeshData bridged = MeshOperations::bridgeEdges(a, loopA, loopB, 2);
    QVERIFY(bridged.vertices.size() >= a.vertices.size());
    // 2 segments => 1 intermediate ring of 4 new verts (endpoints already exist).
    QVERIFY(bridged.vertices.size() == a.vertices.size() + 4 * 1);
    // Quads between ring pairs: (segments) rings of 4 quads.
    QCOMPARE(bridged.faces.size(), a.faces.size() + 2 * 4);

    // Invalid loops return an empty mesh.
    MeshData bad = MeshOperations::bridgeEdges(a, { 0, 1 }, { 2, 3 }, 1);
    QVERIFY(bad.vertices.isEmpty());
}

void TestMeshOperations::testBridgeFaces()
{
    // Two separate quads bridged by their 4 vertices.
    MeshData mesh;
    for (const auto& p : { QVector3D(0, 0, 0), QVector3D(1, 0, 0),
                           QVector3D(1, 0, 1), QVector3D(0, 0, 1),
                           QVector3D(0, 2, 0), QVector3D(1, 2, 0),
                           QVector3D(1, 2, 1), QVector3D(0, 2, 1) })
        mesh.vertices.append(V(p));
    Face fa; fa.indices = { 0, 1, 2, 3 };
    Face fb; fb.indices = { 4, 5, 6, 7 };
    mesh.faces = { fa, fb };

    MeshData bridged = MeshOperations::bridgeFaces(mesh, 0, 1, 1);
    // Original 2 faces + 4 side quads.
    QCOMPARE(bridged.faces.size(), 6);
    // The bridged strip connects the loops (no new vertices at segments == 1).
    QCOMPARE(bridged.vertices.size(), 8);
}

void TestMeshOperations::testAutoSmooth()
{
    // A box: adjacent faces meet at 90° so they fall into distinct groups.
    MeshData box = MeshOperations::createBox(1, 1, 1);
    QVector<int> groups = MeshOperations::autoSmooth(box, 30.0f);
    QCOMPARE(groups.size(), box.faces.size());
    // No face is left unsmoothed.
    for (int g : groups) QVERIFY(g >= 0);

    // A flat grid: coplanar faces should land in the same group.
    MeshData grid = MeshOperations::createGrid(2, 2, 2);
    QVector<int> gridGroups = MeshOperations::autoSmooth(grid, 30.0f);
    QVERIFY(gridGroups.size() == grid.faces.size());
    for (int g : gridGroups) QVERIFY(g >= 0);
}

void TestMeshOperations::testBorderEdges()
{
    MeshData a = MeshOperations::createPlane(1, 1, 1, 1);
    auto borders = MeshOperations::borderEdges(a);
    // A single quad plane's outline has 4 border edges.
    QCOMPARE(borders.size(), 4);
    for (const auto& e : borders) {
        QVERIFY(e.v1 >= 0 && e.v2 >= 0);
        QVERIFY(e.v1 != e.v2);
    }

    // A closed box has no borders.
    MeshData box = MeshOperations::createBox(1, 1, 1);
    QCOMPARE(MeshOperations::borderEdges(box).size(), 0);
}

void TestMeshOperations::testFaceElements()
{
    // Two separate quads => two elements.
    MeshData mesh;
    for (const auto& p : { QVector3D(0, 0, 0), QVector3D(1, 0, 0),
                           QVector3D(1, 1, 0), QVector3D(0, 1, 0),
                           QVector3D(5, 0, 0), QVector3D(6, 0, 0),
                           QVector3D(6, 1, 0), QVector3D(5, 1, 0) })
        mesh.vertices.append(V(p));
    Face fa; fa.indices = { 0, 1, 2, 3 };
    Face fb; fb.indices = { 4, 5, 6, 7 };
    mesh.faces = { fa, fb };

    QVector<int> elems = MeshOperations::faceElements(mesh);
    // Single quad each => each gets its own element.
    QVERIFY(elems.size() == 2);
    QVERIFY(elems[0] != elems[1]);

    // A box is one connected element.
    MeshData box = MeshOperations::createBox(1, 1, 1);
    QVector<int> elems2 = MeshOperations::faceElements(box);
    int first = elems2[0];
    for (int e : elems2) QCOMPARE(e, first);
}

void TestMeshOperations::testPushPullSolid()
{
    // A single open quad pushed by 0.5 becomes a closed 5-face volume (the
    // moved cap + 4 rim walls) with 8 vertices (4 originals + 4 copies).
    MeshData plane;
    for (const auto& p : { QVector3D(-1, 0, -1), QVector3D(1, 0, -1),
                           QVector3D(1, 0, 1),  QVector3D(-1, 0, 1) })
        plane.vertices.append(V(p));
    Face f; f.indices = { 3, 2, 1, 0 };
    plane.faces = { f };

    MeshData pushed = MeshOperations::pushPullFaces(plane, { 0 }, 0.5f);
    QCOMPARE(pushed.faces.size(), 5);
    QCOMPARE(pushed.vertices.size(), 8);
    // The moved cap copy sits at y = 0.5 (region normal is +Y).
    int up = 0;
    for (const auto& v : pushed.vertices)
        if (qAbs(v.position.y() - 0.5f) < 1e-3f) ++up;
    QCOMPARE(up, 4);

    // Pushing the whole top-face region of a box (two triangles) moves the cap
    // and stitches it back with 4 rim walls.
    MeshData box = MeshOperations::createBox(1, 1, 1);
    int beforeFaces = box.faces.size();
    int beforeVerts = box.vertices.size();
    MeshData boxPushed = MeshOperations::pushPullFaces(box, { 8, 9 }, 0.25f);
    QCOMPARE(boxPushed.faces.size(), beforeFaces + 4);
    QCOMPARE(boxPushed.vertices.size(), beforeVerts + 4);
}

void TestMeshOperations::testOffsetFacesSolid()
{
    // Offset on an interior 2x2 patch of a 3x3 quilt: 9 original quads, the
    // 4 patch quads are moved (+ copies of their 9 unique verts) and the 8-edge
    // perimeter gets 8 rim walls.
    MeshData quilt = buildQuilt(3, 3);
    QCOMPARE(quilt.faces.size(), 9);
    QVector<int> patch = { 0, 1, 3, 4 };

    MeshData offset = MeshOperations::offsetFaces(quilt, patch, 0.3f);
    QCOMPARE(offset.faces.size(), 9 + 8);
    QCOMPARE(offset.vertices.size(), quilt.vertices.size() + 9);
    // The moved patch copies sit 0.3 above the ground plane.
    int up = 0;
    for (const auto& v : offset.vertices)
        if (qAbs(v.position.y() - 0.3f) < 1e-3f) ++up;
    QCOMPARE(up, 9);
}

void TestMeshOperations::testExtendedSculptBrushes()
{
    MeshData base = MeshOperations::createPlane(2.0f, 2.0f, 8, 8);
    base.computeNormals();
    const int vertexCount = base.vertices.size();

    auto changedCount = [](const QVector<Vertex>& before, const MeshData& after) {
        int changed = 0;
        const int n = qMin(before.size(), after.vertices.size());
        for (int i = 0; i < n; ++i)
            if ((before[i].position - after.vertices[i].position).lengthSquared() > 1e-9f)
                ++changed;
        return changed;
    };

    // Inflate (5) pushes vertices outward (up for a flat +Y plane).
    {
        MeshData m = base;
        auto before = m.vertices;
        QVERIFY2(MeshOperations::sculptBrush(m, QVector3D(0, 0, 0), 0.6f, 0.5f, 5) > 0, "inflate affected 0");
        QVERIFY(changedCount(before, m) > 0);
        bool up = false;
        for (const auto& v : m.vertices) if (v.position.y() > 0.01f) { up = true; break; }
        QVERIFY(up);
    }
    // Negate (8) is the inverse of draw: it sinks vertices.
    {
        MeshData m = base;
        auto before = m.vertices;
        QVERIFY(MeshOperations::sculptBrush(m, QVector3D(0, 0, 0), 0.6f, 0.5f, 8) > 0);
        QVERIFY(changedCount(before, m) > 0);
        bool down = false;
        for (const auto& v : m.vertices) if (v.position.y() < -0.01f) { down = true; break; }
        QVERIFY(down);
    }
    // Pinch (6) pulls vertices toward the brush center (shrinks x/z spread).
    {
        MeshData m = base;
        auto before = m.vertices;
        QVERIFY(MeshOperations::sculptBrush(m, QVector3D(0.2f, 0.0f, 0.0f), 0.5f, 0.5f, 6) > 0);
        QVERIFY(changedCount(before, m) > 0);
    }
    // Smear (7) drags the surface with the cursor movement (previousCenter).
    {
        MeshData m = base;
        auto before = m.vertices;
        QVERIFY(MeshOperations::sculptBrush(m, QVector3D(0.3f, 0, 0), 0.4f, 0.5f, 7, QVector3D(),
                                            QVector3D(0, 0, 0)) > 0);
        QVERIFY(changedCount(before, m) > 0);
        bool movedX = false;
        for (int i = 0; i < m.vertices.size(); ++i)
            if (m.vertices[i].position.x() > before[i].position.x() + 1e-4f) { movedX = true; break; }
        QVERIFY(movedX);
    }
}

void TestMeshOperations::testSculptPin()
{
    MeshData base = MeshOperations::createPlane(2.0f, 2.0f, 8, 8);
    base.computeNormals();

    // Pin a subset of vertices: the brush must never move them.
    MeshData m = base;
    QSet<int> pinned;
    // 4 center vertices (index of center in an 9x9 grid = row*9+col).
    pinned.insert(4 * 9 + 4);
    pinned.insert(4 * 9 + 5);
    pinned.insert(5 * 9 + 4);
    pinned.insert(5 * 9 + 5);
    QVector3D before0 = m.vertices[4 * 9 + 4].position;
    QVector3D before1 = m.vertices[5 * 9 + 5].position;

    int affected = MeshOperations::sculptBrush(m, QVector3D(0, 0, 0), 0.8f, 0.8f, 0,
                                               QVector3D(), QVector3D(), 2.0f, &pinned);
    QVERIFY2(affected > 0, "pinned stroke affected 0 vertices");
    QCOMPARE(m.vertices[4 * 9 + 4].position, before0);
    QCOMPARE(m.vertices[5 * 9 + 5].position, before1);

    // Without pins the same vertices do move.
    MeshData m2 = base;
    MeshOperations::sculptBrush(m2, QVector3D(0, 0, 0), 0.8f, 0.8f, 0);
    QVERIFY((m2.vertices[4 * 9 + 4].position - before0).lengthSquared() > 1e-9f);
}

void TestMeshOperations::testSculptFalloffPower()
{
    MeshData base = MeshOperations::createPlane(2.0f, 2.0f, 8, 8);
    base.computeNormals();

    // A high falloff exponent concentrates displacement near the brush center:
    // the sum of |delta| must be smaller than with exponent 0.5.
    float sumLow = 0.0f, sumHigh = 0.0f;
    {
        MeshData m = base;
        MeshOperations::sculptBrush(m, QVector3D(0, 0, 0), 0.8f, 1.0f, 0,
                                    QVector3D(), QVector3D(), 0.5f);
        for (auto& v : m.vertices) sumLow += v.position.y();
    }
    {
        MeshData m = base;
        MeshOperations::sculptBrush(m, QVector3D(0, 0, 0), 0.8f, 1.0f, 0,
                                    QVector3D(), QVector3D(), 6.0f);
        for (auto& v : m.vertices) sumHigh += v.position.y();
    }
    QVERIFY2(sumHigh < sumLow, "high falloff exponent should limit overall displacement");
}

void TestMeshOperations::testBulgeSlashBrushes()
{
    MeshData base = MeshOperations::createPlane(2.0f, 2.0f, 8, 8);
    base.computeNormals();

    auto changedCount = [](const QVector<Vertex>& before, const MeshData& after) {
        int changed = 0;
        for (int i = 0; i < before.size() && i < after.vertices.size(); ++i)
            if ((before[i].position - after.vertices[i].position).lengthSquared() > 1e-9f)
                ++changed;
        return changed;
    };

    // Bulge (11): outward push, all affected vertices go up on a +Y plane.
    {
        MeshData m = base;
        auto before = m.vertices;
        QVERIFY2(MeshOperations::sculptBrush(m, QVector3D(0, 0, 0), 0.5f, 0.6f, 11) > 0, "bulge affected 0");
        QVERIFY(changedCount(before, m) > 0);
        bool up = false;
        for (const auto& v : m.vertices) if (v.position.y() > 0.01f) { up = true; break; }
        QVERIFY(up);
    }
    // Slash (12): cuts along the stroke direction (default X), shifting vertices in -X.
    {
        MeshData m = base;
        auto before = m.vertices;
        QVERIFY(MeshOperations::sculptBrush(m, QVector3D(0, 0, 0), 0.5f, 0.6f, 12) > 0);
        QVERIFY(changedCount(before, m) > 0);
        bool cutX = false;
        for (int i = 0; i < m.vertices.size(); ++i)
            if (before[i].position.x() > 0 && m.vertices[i].position.x() < before[i].position.x())
                { cutX = true; break; }
        QVERIFY(cutX);
    }
}

void TestMeshOperations::testAdvancedBrushesFoldsPores()
{
    MeshData base = MeshOperations::createPlane(2.0f, 2.0f, 12, 12);
    base.computeNormals();

    auto changedCount = [](const QVector<Vertex>& before, const MeshData& after) {
        int changed = 0;
        for (int i = 0; i < before.size() && i < after.vertices.size(); ++i)
            if ((before[i].position - after.vertices[i].position).lengthSquared() > 1e-9f)
                ++changed;
        return changed;
    };

    // Folds (9): concentric ridges/valleys - both positive and negative offsets.
    {
        MeshData m = base;
        auto before = m.vertices;
        QVERIFY2(MeshOperations::sculptBrush(m, QVector3D(0, 0, 0), 0.6f, 0.6f, 9) > 0, "folds affected 0");
        QVERIFY(changedCount(before, m) > 0);
        bool up = false, down = false;
        for (int i = 0; i < m.vertices.size(); ++i) {
            float dy = m.vertices[i].position.y() - before[i].position.y();
            if (dy > 1e-4f) up = true;
            if (dy < -1e-4f) down = true;
        }
        QVERIFY(up && down);
    }
    // Pores (10): deterministic micro-depressions (positive and negative).
    {
        MeshData m1 = base;
        MeshData m2 = base;
        QVERIFY(MeshOperations::sculptBrush(m1, QVector3D(0, 0, 0), 0.6f, 0.5f, 10) > 0);
        QVERIFY(changedCount(base.vertices, m1) > 0);
        MeshOperations::sculptBrush(m2, QVector3D(0, 0, 0), 0.6f, 0.5f, 10);
        for (int i = 0; i < m1.vertices.size(); ++i)
            QCOMPARE(m1.vertices[i].position, m2.vertices[i].position);
    }
}

void TestMeshOperations::testSnapPointToMesh()
{
    MeshData box = MeshOperations::createBox(1, 1, 1);   // verts at ±0.5
    box.computeBoundingBox();
    QMatrix4x4 world;
    world.setToIdentity();

    // Vertex snap: a point just outside a corner snaps to the corner.
    QVector3D vp = MeshOperations::snapPointToMesh(box, world,
        QVector3D(0.505f, 0.505f, 0.505f), int(MeshOperations::SnapType::Vertex));
    QVERIFY((vp - QVector3D(0.5f, 0.5f, 0.5f)).length() < 1e-3f);

    // Midpoint snap: edge midpoint (0.5, 0.5, 0.0).
    QVector3D mp = MeshOperations::snapPointToMesh(box, world,
        QVector3D(0.502f, 0.502f, 0.001f), int(MeshOperations::SnapType::Midpoint));
    QVERIFY((mp - QVector3D(0.5f, 0.5f, 0.0f)).length() < 1e-3f);

    // Edge snap: point slightly off an edge projects onto it.
    QVector3D ep = MeshOperations::snapPointToMesh(box, world,
        QVector3D(0.5f, 0.498f, 0.0f), int(MeshOperations::SnapType::Edge));
    QVERIFY(qAbs(ep.y() - 0.5f) < 1e-3f);

    // Face snap: just above the top face projects onto it.
    QVector3D fp = MeshOperations::snapPointToMesh(box, world,
        QVector3D(0.0f, 0.502f, 0.0f), int(MeshOperations::SnapType::Face));
    QVERIFY(qAbs(fp.y() - 0.5f) < 1e-3f);

    // A point far away with only geometry snaps is not pulled in.
    QVector3D far = MeshOperations::snapPointToMesh(box, world,
        QVector3D(3.0f, 3.0f, 3.0f), int(MeshOperations::SnapType::Vertex));
    QVERIFY(far == QVector3D(3.0f, 3.0f, 3.0f));
}

void TestMeshOperations::testFillHoles()
{
    // A single open quad has exactly one 4-edge boundary loop.
    MeshData plane;
    for (const auto& p : { QVector3D(-1, 0, -1), QVector3D(1, 0, -1),
                           QVector3D(1, 0, 1),  QVector3D(-1, 0, 1) })
        plane.vertices.append(V(p));
    Face f; f.indices = { 0, 1, 2, 3 };
    plane.faces = { f };
    plane.computeNormals();
    plane.computeBoundingBox();

    MeshData capped = plane;
    int filled = MeshOperations::fillHoles(capped, 8);
    QCOMPARE(filled, 1);
    QCOMPARE(capped.faces.size(), 1 + 4);
    QCOMPARE(capped.vertices.size(), 5);   // 4 originals + centroid

    // Closing a large hole into the cap dimension is rejected by maxHoleEdges.
    MeshData small = plane;
    QCOMPARE(MeshOperations::fillHoles(small, 2), 0);

    // A closed box has no holes.
    MeshData box = MeshOperations::createBox(1, 1, 1);
    QCOMPARE(MeshOperations::fillHoles(box, 0), 0);
}

void TestMeshOperations::testExtractFaces()
{
    // Top face of the box (unit) is the two triangles {8, 9}; region has 4
    // unique vertices.
    MeshData box = MeshOperations::createBox(1, 1, 1);
    MeshData top = MeshOperations::extractFaces(box, { 8, 9 }, 0.0f, true);
    QCOMPARE(top.faces.size(), 2);
    QCOMPARE(top.vertices.size(), 4);

    // Extraction with a thickness becomes a closed solid (shelled patch).
    MeshData solid = MeshOperations::extractFaces(box, { 8, 9 }, 0.2f, true);
    QCOMPARE(solid.vertices.size(), 8);
    QCOMPARE(solid.faces.size(), 8);   // 2 front + 2 back + 4 rim walls

    // Invalid selection returns an empty mesh.
    MeshData empty = MeshOperations::extractFaces(box, { -1, 999 }, 0.0f, true);
    QVERIFY(empty.vertices.isEmpty());
}

void TestMeshOperations::testFalloffFactor()
{
    // Full weight at the center.
    QCOMPARE(MeshOperations::falloffFactor(0.0f, 2.0f, 0), 1.0f);
    // Zero at/outside the radius.
    QCOMPARE(MeshOperations::falloffFactor(2.0f, 2.0f, 0), 0.0f);
    QCOMPARE(MeshOperations::falloffFactor(5.0f, 2.0f, 0), 0.0f);
    // Inside the radius it must be strictly between 0 and 1.
    float w = MeshOperations::falloffFactor(1.0f, 2.0f, 0);
    QVERIFY(w > 0.0f && w < 1.0f);
    // Smooth(0) is softer than Linear(1) toward the outer radius. (Note:
    // smoothstep and linear are exactly equal at the midpoint, so sample
    // at t = 0.75 where the smooth curve has the lower tail weight.)
    float wSmooth = MeshOperations::falloffFactor(1.5f, 2.0f, 0);
    float wLinear = MeshOperations::falloffFactor(1.5f, 2.0f, 1);
    QVERIFY(wLinear > wSmooth);
    // Constant(5) ignores distance.
    QCOMPARE(MeshOperations::falloffFactor(1.9f, 2.0f, 5), 1.0f);
    // Degenerate radius: no influence (avoids divide-by-zero).
    QCOMPARE(MeshOperations::falloffFactor(0.5f, 0.0f, 3), 0.0f);
}

void TestMeshOperations::testTransformAround()
{
    // Two isolated vertices; pivot at the origin.
    MeshData mesh;
    mesh.vertices = { V(QVector3D(1, 0, 0)), V(QVector3D(-1, 0, 0)) };
    Face f; f.indices = { 0, 1, 0 };
    mesh.faces = { f };   // degenerate, but keeps all verts referenced

    // Translate with full influence moves both verts.
    MeshData t = MeshOperations::transformAround(
        mesh, {}, MeshOperations::TransformCenterMode::Translate,
        QVector3D(0, 0, 0), QVector3D(), QVector3D(0, 2, 0), 0.0f, 0);
    QCOMPARE(t.vertices.at(0).position, QVector3D(1, 2, 0));
    QCOMPARE(t.vertices.at(1).position, QVector3D(-1, 2, 0));

    // Rotate 90° about Y moves (1,0,0) onto (0,0,-1).
    MeshData r = MeshOperations::transformAround(
        mesh, {}, MeshOperations::TransformCenterMode::Rotate,
        QVector3D(0, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 90, 0), 0.0f, 0);
    QVERIFY((r.vertices.at(0).position - QVector3D(0, 0, -1)).length() < 1e-3f);

    // Uniform scale 2x doubles the distance from the pivot.
    MeshData s = MeshOperations::transformAround(
        mesh, {}, MeshOperations::TransformCenterMode::ScaleUniform,
        QVector3D(0, 0, 0), QVector3D(), QVector3D(2, 0, 0), 0.0f, 0);
    QVERIFY((s.vertices.at(0).position - QVector3D(2, 0, 0)).length() < 1e-3f);
    QVERIFY((s.vertices.at(1).position - QVector3D(-2, 0, 0)).length() < 1e-3f);

    // Falloff radius zeroes everything outside the brush (distance 1 > radius 0.5).
    MeshData ff = MeshOperations::transformAround(
        mesh, {}, MeshOperations::TransformCenterMode::Translate,
        QVector3D(0, 0, 0), QVector3D(), QVector3D(0, 1, 0), 0.5f, 0);
    QCOMPARE(ff.vertices.at(0).position, QVector3D(1, 0, 0));       // untouched
    QCOMPARE(ff.vertices.at(1).position, QVector3D(-1, 0, 0));      // untouched

    // Subset selection: only the picked vertex moves.
    MeshData sel = MeshOperations::transformAround(
        mesh, { 0 }, MeshOperations::TransformCenterMode::Translate,
        QVector3D(0, 0, 0), QVector3D(), QVector3D(0, 1, 0), 0.0f, 0);
    QCOMPARE(sel.vertices.at(0).position, QVector3D(1, 1, 0));
    QCOMPARE(sel.vertices.at(1).position, QVector3D(-1, 0, 0));
}

void TestMeshOperations::testSplitSmoothingGroups()
{
    // A box: each face a distinct group (like a hard-edged cube).
    MeshData box = MeshOperations::createBox(1, 1, 1);
    const int vCount = box.vertices.size();
    QVector<int> groups;
    groups.reserve(box.faces.size());
    for (int i = 0; i < box.faces.size(); ++i) groups.append(i);   // all unique

    MeshData split = MeshOperations::splitSmoothingGroups(box, groups);
    // Every face is now isolated: vertex count grows to one set per face.
    QVERIFY(split.vertices.size() > vCount);
    QCOMPARE(split.faces.size(), box.faces.size());

    // Single group: mesh passes through unchanged.
    QVector<int> single(groups.size(), 0);
    MeshData same = MeshOperations::splitSmoothingGroups(box, single);
    QCOMPARE(same.vertices.size(), vCount);
    QCOMPARE(same.faces.size(), box.faces.size());

    // Invalid group vector length is rejected.
    MeshData bad = MeshOperations::splitSmoothingGroups(box, { 0 });
    QCOMPARE(bad.vertices.size(), vCount);
}

void TestMeshOperations::testBevelEdges()
{
    // Unit box: 8 verts, 12 tri faces (2 per cube face), 12 edges.
    MeshData box = MeshOperations::createBox(1, 1, 1);
    QCOMPARE(box.vertices.size(), 8);
    QCOMPARE(box.faces.size(), 12);

    const float bb0 = box.boundingRadius;

    // Bevel every feature edge by 0.1 with one segment.
    MeshData bev = MeshOperations::bevelEdges(box, 0.1f, 1, qDegreesToRadians(40.0f));

    // Offsets per (face, corner) expand the mesh slightly.
    QVERIFY(bev.vertices.size() > box.vertices.size());
    QVERIFY(bev.boundingRadius > bb0);

    // Original 12 faces stay, plus 2 triangles per beveled edge (12 edges).
    QCOMPARE(bev.faces.size(), 12 + 12 * 2);

    // Every triangle references existing vertices, no degenerate faces.
    bool valid = true;
    for (const auto& f : bev.faces) {
        if (f.indices.size() < 3) { valid = false; break; }
        QSet<int> uniq;
        for (int i : f.indices) {
            uniq.insert(i);
            if (i < 0 || i >= bev.vertices.size()) { valid = false; break; }
        }
        if (uniq.size() < 3) { valid = false; break; }
    }
    QVERIFY(valid);

    // A limit above the box's 90-degree edges (nothing is sharper than the
    // limit) leaves the box untouched.
    MeshData flat = MeshOperations::bevelEdges(box, 0.1f, 1, qDegreesToRadians(140.0f));
    QCOMPARE(flat.vertices.size(), 8);
    QCOMPARE(flat.faces.size(), 12);

    // Degenerate distances are rejected.
    QCOMPARE(MeshOperations::bevelEdges(box, 0.0f, 1, qDegreesToRadians(40.0f)).vertices.size(), 8);
}

void TestMeshOperations::testResolveUVOverlaps()
{
    auto quad = [](const QString& name, const QVector3D& zOff,
                   float u0, float v0, float u1, float v1) {
        MeshData m;
        m.name = name;
        for (int i = 0; i < 4; ++i) {
            Vertex v;
            v.position = QVector3D(float(i & 1), float(i >> 1), zOff.z());
            v.uv = QVector2D((i & 1) ? u1 : u0, (i >> 1) ? v1 : v0);
            m.vertices.append(v);
        }
        m.uvs = m.vertices.size() ?
            QVector<QVector2D>(m.vertices.size(), QVector2D()) : QVector<QVector2D>();
        for (int i = 0; i < m.vertices.size(); ++i) m.uvs[i] = m.vertices[i].uv;
        m.faces.append(Face({ 0, 1, 2, 3 }));
        m.computeNormals();
        m.computeBoundingBox();
        return m;
    };

    // Fully overlapping charts: both quads are mapped onto the whole [0,1]^2.
    MeshData overlapping;
    MeshData q1 = quad("q1", QVector3D(0, 0, 0), 0, 0, 1, 1);
    MeshData q2 = quad("q2", QVector3D(0, 0, 1), 0, 0, 1, 1);
    q2.vertices[0].position.setY(0); q2.vertices[1].position.setY(0);
    q2.vertices[2].position.setY(0); q2.vertices[3].position.setY(0);
    MeshOperations::mergeMeshes(overlapping, q1);
    MeshOperations::mergeMeshes(overlapping, q2);
    QCOMPARE(overlapping.faces.size(), 2);

    MeshData resolved = MeshOperations::resolveUVOverlaps(overlapping, 0.05f);
    QCOMPARE(resolved.vertices.size(), overlapping.vertices.size());

    // The layouts differ: something was moved.
    bool moved = false;
    for (int i = 0; i < resolved.vertices.size(); ++i)
        if ((resolved.vertices[i].uv - overlapping.vertices[i].uv).lengthSquared() > 1e-6f)
            { moved = true; break; }
    QVERIFY2(moved, "overlapping islands should have been re-packed");

    // Every UV ends up inside the padded [0,1] box.
    for (const auto& v : resolved.vertices) {
        QVERIFY(v.uv.x() >= -1e-4f && v.uv.x() <= 1.0f + 1e-4f);
        QVERIFY(v.uv.y() >= -1e-4f && v.uv.y() <= 1.0f + 1e-4f);
    }

    // The two charts' bounding boxes no longer overlap.
    auto islandBox = [&resolved](int faceIndex) {
        QVector2D mn(1e10f, 1e10f), mx(-1e10f, -1e10f);
        for (int vi : resolved.faces[faceIndex].indices) {
            const QVector2D& u = resolved.vertices[vi].uv;
            mn.setX(qMin(mn.x(), u.x())); mn.setY(qMin(mn.y(), u.y()));
            mx.setX(qMax(mx.x(), u.x())); mx.setY(qMax(mx.y(), u.y()));
        }
        return qMakePair(mn, mx);
    };
    const auto b1 = islandBox(0);
    const auto b2 = islandBox(1);
    const bool xOverlap = b1.first.x() + 1e-4f <= b2.second.x() && b2.first.x() + 1e-4f <= b1.second.x();
    const bool yOverlap = b1.first.y() + 1e-4f <= b2.second.y() && b2.first.y() + 1e-4f <= b1.second.y();
    QVERIFY2(!(xOverlap && yOverlap), "island boxes must not overlap after resolution");

    // Already-separated charts are left untouched.
    MeshData separated;
    MeshData s1 = quad("s1", QVector3D(0, 0, 0), 0, 0, 0.4f, 0.4f);
    MeshData s2 = quad("s2", QVector3D(0, 0, 1), 0.6f, 0.6f, 1.0f, 1.0f);
    s2.vertices[0].position.setY(0); s2.vertices[1].position.setY(0);
    s2.vertices[2].position.setY(0); s2.vertices[3].position.setY(0);
    MeshOperations::mergeMeshes(separated, s1);
    MeshOperations::mergeMeshes(separated, s2);
    MeshData unchanged = MeshOperations::resolveUVOverlaps(separated, 0.05f);
    bool same = true;
    for (int i = 0; i < unchanged.vertices.size(); ++i)
        if ((unchanged.vertices[i].uv - separated.vertices[i].uv).lengthSquared() > 1e-9f)
            { same = false; break; }
    QVERIFY2(same, "non-overlapping charts should remain unchanged");

    // Empty input is a no-op.
    MeshData emptyMesh;
    QCOMPARE(MeshOperations::resolveUVOverlaps(emptyMesh, 0.05f).vertices.size(), 0);
}

void TestMeshOperations::testRevolveSketch()
{
    // A vertical sketch segment standing 0.5 away from the Y axis.
    MeshData profile;
    Vertex a, b;
    a.position = QVector3D(0.5f, 0.0f, 0.0f);
    b.position = QVector3D(0.5f, 1.0f, 0.0f);
    profile.vertices = { a, b };
    profile.computeBoundingBox();

    // Full lathe: 16 steps + caps on both ends.
    MeshData lathe = MeshOperations::revolveSketch(profile, QVector3D(0, 1, 0), 360.0f, 16, true);
    QCOMPARE(lathe.vertices.size(), 17 * 2 + 2);   // rings x profile points + 2 cap centers
    QCOMPARE(lathe.faces.size(), 16 * 2 + 2 * 16); // surface tris + cap tris

    // Every grid vertex keeps its distance to the axis.
    int capCenters = 0;
    for (int i = 0; i < lathe.vertices.size(); ++i) {
        const QVector3D& p = lathe.vertices[i].position;
        const float r2 = p.x() * p.x() + p.z() * p.z();
        if (qAbs(r2) < 1e-6f) { ++capCenters; continue; }
        QVERIFY2(qAbs(r2 - 0.25f) < 1e-3f, "lathe points must keep the profile radius");
        QVERIFY(p.y() >= -1e-4f && p.y() <= 1.0f + 1e-4f);
    }
    QCOMPARE(capCenters, 2);

    // Normals are unit length.
    QCOMPARE(lathe.normals.size(), lathe.vertices.size());
    for (const auto& n : lathe.normals)
        QVERIFY(qAbs(n.length() - 1.0f) < 1e-3f);

    // Face indices are all in range and non-degenerate.
    for (const auto& f : lathe.faces) {
        QVERIFY(f.indices.size() == 3);
        for (int idx : f.indices)
            QVERIFY(idx >= 0 && idx < lathe.vertices.size());
    }

    // Partial 90-degree sweep without caps is an open surface.
    MeshData part = MeshOperations::revolveSketch(profile, QVector3D(0, 1, 0), 90.0f, 4, false);
    QCOMPARE(part.vertices.size(), 5 * 2);
    QCOMPARE(part.faces.size(), 4 * 2);

    // Degenerate profile input is a no-op.
    MeshData single;
    single.vertices.append(a);
    QCOMPARE(MeshOperations::revolveSketch(single, QVector3D(0, 1, 0), 360.0f, 8, true).vertices.size(), 1);
}

void TestMeshOperations::testBevelChain()
{
    MeshData box = MeshOperations::createBox(1, 1, 1);
    QCOMPARE(box.vertices.size(), 8);
    MeshOperations::ensureEdgeList(box);
    QVERIFY(box.edges.size() >= 6);

    // The edge list also contains triangulation diagonals (longer than the
    // cube side); pick two real cube edges that share a vertex.
    auto isCubeEdge = [&](const Edge& e) {
        const float len = (box.vertices[e.v1].position
                         - box.vertices[e.v2].position).length();
        return qAbs(len - 1.0f) < 1e-4f;
    };
    int e0 = -1;
    for (int k = 0; k < box.edges.size(); ++k) {
        if (isCubeEdge(box.edges[k])) { e0 = k; break; }
    }
    QVERIFY(e0 >= 0);
    int sharedEdge = -1;
    for (int k = 0; k < box.edges.size(); ++k) {
        if (k == e0 || !isCubeEdge(box.edges[k])) continue;
        const Edge& e = box.edges[k];
        const Edge& s = box.edges[e0];
        if (e.v1 == s.v1 || e.v1 == s.v2 || e.v2 == s.v1 || e.v2 == s.v2) {
            sharedEdge = k;
            break;
        }
    }
    QVERIFY2(sharedEdge >= 0, "box edges must contain a shared corner");

    auto valid = [](const MeshData& m) {
        for (const auto& f : m.faces) {
            if (f.indices.size() < 3) return false;
            QSet<int> uniq;
            for (int i : f.indices) {
                uniq.insert(i);
                if (i < 0 || i >= m.vertices.size()) return false;
            }
            if (uniq.size() < 3) return false;
        }
        return true;
    };

    // A two-edge fillet chain with a tapering radius.
    MeshData chain = MeshOperations::bevelChain(
        box, QVector<int>{ e0, sharedEdge }, QVector<float>{ 0.1f, 0.2f },
        1, qDegreesToRadians(40.0f));
    QVERIFY(chain.vertices.size() > 8);
    QCOMPARE(chain.faces.size(), 12 + 2 * 2); // 12 original + one strip per edge
    QVERIFY(valid(chain));

    // Backwards-compatible single-edge selection.
    MeshData single = MeshOperations::bevelChain(
        box, QVector<int>{ e0 }, QVector<float>{ 0.15f }, 1, qDegreesToRadians(40.0f));
    QCOMPARE(single.faces.size(), 12 + 1 * 2);
    QVERIFY(valid(single));

    // Invalid edge indices are skipped -> mesh untouched.
    MeshData bad = MeshOperations::bevelChain(
        box, QVector<int>{ 999, -5 }, QVector<float>{ 0.1f }, 1, qDegreesToRadians(40.0f));
    QCOMPARE(bad.vertices.size(), 8);
    QCOMPARE(bad.faces.size(), 12);

    // Radii vector shorter than edges: last radius is reused.
    MeshData uniform = MeshOperations::bevelChain(
        box, QVector<int>{ e0, sharedEdge }, QVector<float>{ 0.05f }, 1, qDegreesToRadians(40.0f));
    QCOMPARE(uniform.faces.size(), 16);
    QVERIFY(valid(uniform));

    // A limit above the box's 90-degree edges keeps the selected edges hard.
    MeshData hard = MeshOperations::bevelChain(
        box, QVector<int>{ e0, sharedEdge }, QVector<float>{ 0.1f }, 1, qDegreesToRadians(140.0f));
    QCOMPARE(hard.vertices.size(), 8);
    QCOMPARE(hard.faces.size(), 12);
}

void TestMeshOperations::testBevelProfiles()
{
    MeshData box = MeshOperations::createBox(1, 1, 1);
    
    // Test linear profile (default)
    MeshData linear = MeshOperations::bevelEdges(box, 0.1f, 2, qDegreesToRadians(40.0f), 0, 0.5f);
    QVERIFY(linear.vertices.size() > box.vertices.size());
    QVERIFY(linear.faces.size() > box.faces.size());
    
    // Test concave profile
    MeshData concave = MeshOperations::bevelEdges(box, 0.1f, 2, qDegreesToRadians(40.0f), 1, 0.5f);
    QVERIFY(concave.vertices.size() > box.vertices.size());
    
    // Test convex profile
    MeshData convex = MeshOperations::bevelEdges(box, 0.1f, 2, qDegreesToRadians(40.0f), 2, 0.5f);
    QVERIFY(convex.vertices.size() > box.vertices.size());
    
    // Test custom profile
    MeshData custom = MeshOperations::bevelEdges(box, 0.1f, 2, qDegreesToRadians(40.0f), 3, 0.75f);
    QVERIFY(custom.vertices.size() > box.vertices.size());
    
    // All profiles should produce valid meshes
    auto validateMesh = [](const MeshData& m) -> bool {
        for (const auto& f : m.faces) {
            if (f.indices.size() < 3) return false;
            QSet<int> uniq;
            for (int i : f.indices) {
                if (i < 0 || i >= m.vertices.size()) return false;
                uniq.insert(i);
            }
            if (uniq.size() < 3) return false;
        }
        return true;
    };
    
    QVERIFY(validateMesh(linear));
    QVERIFY(validateMesh(concave));
    QVERIFY(validateMesh(convex));
    QVERIFY(validateMesh(custom));
}

void TestMeshOperations::testRetargetSkeleton()
{
    // Test empty inputs
    QVector<int> empty = MeshOperations::retargetSkeleton({}, {});
    QVERIFY(empty.isEmpty());
    
    // Test identity mapping (same positions)
    QVector<QVector3D> src1 = { {0,0,0}, {1,0,0}, {2,0,0} };
    QVector<QVector3D> dst1 = { {0,0,0}, {1,0,0}, {2,0,0} };
    QVector<int> map1 = MeshOperations::retargetSkeleton(src1, dst1);
    QCOMPARE(map1.size(), 3);
    QCOMPARE(map1[0], 0);
    QCOMPARE(map1[1], 1);
    QCOMPARE(map1[2], 2);
    
    // Test offset mapping (dst is shifted)
    QVector<QVector3D> dst2 = { {0,1,0}, {1,1,0}, {2,1,0} };
    QVector<int> map2 = MeshOperations::retargetSkeleton(src1, dst2);
    QCOMPARE(map2.size(), 3);
    QCOMPARE(map2[0], 0);
    QCOMPARE(map2[1], 1);
    QCOMPARE(map2[2], 2);
    
    // Test with different joint counts
    QVector<QVector3D> dst3 = { {0,0,0}, {1,0,0} };
    QVector<int> map3 = MeshOperations::retargetSkeleton(src1, dst3);
    QCOMPARE(map3.size(), 3);
    QCOMPARE(map3[0], 0);
    QCOMPARE(map3[1], 1);
    QCOMPARE(map3[2], -1); // no match for third joint
    
    // Test no matches (too far apart)
    QVector<QVector3D> src4 = { {0,0,0} };
    QVector<QVector3D> dst4 = { {100,100,100} };
    QVector<int> map4 = MeshOperations::retargetSkeleton(src4, dst4);
    QCOMPARE(map4.size(), 1);
    QCOMPARE(map4[0], -1); // too far, no match
}

QTEST_MAIN(TestMeshOperations)
#include "test_MeshOperations.moc"
