#include "sdk/kseditor/modules/modellingEditor/AdditionalModifiers.h"
#include "engine/mesh/ModifierSystem.h"
#include <QtTest/QtTest>

using namespace ks;

static Vertex V(const QVector3D& p) { Vertex v; v.position = p; return v; }

class TestModifiers : public QObject {
    Q_OBJECT

private slots:
    void testTaper();
    void testRipple();
    void testNoise();
    void testPush();
    void testRelax();
    void testMelt();
    void testLathe();
    void testSubdivisionCreases();
    void testSubdivisionPinnedVertices();
};

void TestModifiers::testTaper()
{
    MeshData box = MeshOperations::createBox(1.0f, 2.0f, 1.0f);    // axis-aligned: Y height 2
    TaperModifier mod;
    mod.factor = 0.5f;
    MeshData out = mod.apply(box);

    QCOMPARE(out.vertices.size(), box.vertices.size());
    QCOMPARE(out.faces.size(), box.faces.size());

    out.computeBoundingBox();
    QVector3D size = out.boundingBoxMax - out.boundingBoxMin;
    // Tapering along Y (default AUTO -> Y): top pairs spread wider than bottom.
    float maxX = 0.0f;
    for (const Vertex& v : out.vertices)
        maxX = qMax(maxX, qAbs(v.position.x()));
    // Original half-width is 0.5; top is scaled by (1 + factor*1) at t=1 -> 0.75
    QVERIFY(qAbs(maxX - 0.75f) < 1e-3f);
    QVERIFY(size.y() > 1.9f); // height unchanged
}

void TestModifiers::testRipple()
{
    MeshData grid = MeshOperations::createPlane(10.0f, 10.0f, 10, 10); // XY plane facing +Z
    RippleModifier mod;
    mod.amplitude = 0.2f;
    mod.wavelength = 4.0f;
    MeshData out = mod.apply(grid);

    QCOMPARE(out.vertices.size(), grid.vertices.size());
    bool moved = false;
    for (int i = 0; i < out.vertices.size(); ++i) {
        // Ripple along Y: vertices off-center sink/rise along Y by up to ~amplitude
        float dy = qAbs(out.vertices[i].position.y() - grid.vertices[i].position.y());
        if (dy > 0.05f)
            moved = true;
    }
    QVERIFY(moved);
}

void TestModifiers::testNoise()
{
    MeshData box = MeshOperations::createBox(1.0f, 1.0f, 1.0f);
    NoiseModifier mod;
    mod.strength = 0.1f;
    mod.scale = 2.0f;
    mod.seed = 7;
    MeshData out = mod.apply(box);

    QCOMPARE(out.vertices.size(), box.vertices.size());
    bool moved = false;
    for (int i = 0; i < out.vertices.size(); ++i) {
        float d = (out.vertices[i].position - box.vertices[i].position).length();
        if (d > 1e-5f) { moved = true; break; }
    }
    QVERIFY(moved);
}

void TestModifiers::testPush()
{
    MeshData box = MeshOperations::createBox(1.0f, 1.0f, 1.0f);
    MeshData baseline = box; baseline.computeNormals();

    PushModifier mod;
    mod.distance = 0.1f;
    MeshData out = mod.apply(box);
    QCOMPARE(out.vertices.size(), box.vertices.size());
    QCOMPARE(out.faces.size(), box.faces.size());

    // All vertices should move outward by ~0.1 in some direction
    bool moved = false;
    for (int i = 0; i < out.vertices.size(); ++i) {
        if ((out.vertices[i].position - box.vertices[i].position).length() > 0.05f)
            moved = true;
    }
    QVERIFY(moved);
}

void TestModifiers::testRelax()
{
    MeshData box = MeshOperations::createBox(1.0f, 1.0f, 1.0f);
    RelaxModifier mod;
    mod.iterations = 3;
    mod.factor = 0.5f;
    mod.pinBoundary = true;
    MeshData out = mod.apply(box);

    QCOMPARE(out.vertices.size(), box.vertices.size());
    QCOMPARE(out.faces.size(), box.faces.size());

    out.computeBoundingBox();
    // With box, boundary pinned -> bbox unchanged
    QVERIFY((out.boundingBoxMax - out.boundingBoxMin).length() > 1.5f);
}

void TestModifiers::testMelt()
{
    MeshData box = MeshOperations::createBox(1.0f, 2.0f, 1.0f); // Y height 2, from -1..1
    MeltModifier mod;
    mod.amount = 0.5f;
    mod.axis = MeltModifier::MeltAxisType::Y;
    MeshData out = mod.apply(box);

    QCOMPARE(out.vertices.size(), box.vertices.size());
    out.computeBoundingBox();
    // Top of box (y=1) melts down: new max y < 1
    QVERIFY(out.boundingBoxMax.y() < 0.999f);
    QVERIFY(out.boundingBoxMax.y() > -0.999f); // not fully flat
}

void TestModifiers::testLathe()
{
    // Profile: two vertices forming a vertical segment offset from the Y axis.
    MeshData profile;
    profile.vertices.append(V(QVector3D(1.0f, -1.0f, 0.0f)));
    profile.vertices.append(V(QVector3D(1.0f, 1.0f, 0.0f)));

    LatheModifier mod;
    mod.segments = 16;
    mod.angle = 360.0f;
    MeshData out = mod.apply(profile);

    QVERIFY(out.vertices.size() >= 2);
    QVERIFY(out.faces.size() > 0);
    out.computeBoundingBox();
    // Revolved around Y: full circle radius 1
    QVERIFY(qAbs(out.boundingBoxMax.x() - 1.0f) < 1e-3f);
    QVERIFY(qAbs(out.boundingBoxMax.y() - 1.0f) < 1e-3f);
    QVERIFY(qAbs(out.boundingBoxMin.y() + 1.0f) < 1e-3f);
}

void TestModifiers::testSubdivisionCreases()
{
    MeshData box = MeshOperations::createBox(1.0f, 1.0f, 1.0f);

    SubdivisionModifier smooth;
    smooth.levels = 1;
    MeshData smoothOut = smooth.apply(box);

    SubdivisionModifier creased;
    creased.levels = 1;
    creased.creases.append(CreaseEdge{0, 1, 1.0f});   // one hardened edge
    creased.creases.append(CreaseEdge{1, 2, 0.5f});
    MeshData creasedOut = creased.apply(box);

    QVERIFY(smoothOut.vertices.size() > box.vertices.size());
    QVERIFY(creasedOut.vertices.size() > box.vertices.size());

    // write/read parameters round-trips the crease list
    QMap<QString, QVariant> params = creased.writeParameters();
    QVERIFY(params.contains("creases"));
    QCOMPARE(params["creases"].toList().size(), 2);

    SubdivisionModifier restored;
    restored.readParameters(params);
    QCOMPARE(restored.creases.size(), 2);
    QCOMPARE(restored.creases[0].vertexA, 0);
    QCOMPARE(restored.creases[0].vertexB, 1);
    QVERIFY(qAbs(restored.creases[0].sharpness - 1.0f) < 1e-6f);
}

void TestModifiers::testSubdivisionPinnedVertices()
{
    MeshData box = MeshOperations::createBox(1.0f, 1.0f, 1.0f);
    // Pin a symmetric pair of base vertices.
    QVector3D p0 = box.vertices[0].position;

    SubdivisionModifier mod;
    mod.levels = 1;
    mod.pinnedVertices.append(0);
    MeshData out = mod.apply(box);

    QVERIFY(out.vertices.size() > box.vertices.size());

    QMap<QString, QVariant> params = mod.writeParameters();
    QVERIFY(params.contains("pinnedVertices"));
    QCOMPARE(params["pinnedVertices"].toList().size(), 1);

    SubdivisionModifier restored;
    restored.readParameters(params);
    QCOMPARE(restored.pinnedVertices.size(), 1);
    QCOMPARE(restored.pinnedVertices[0], 0);
}

QTEST_MAIN(TestModifiers)
#include "test_Modifiers.moc"
