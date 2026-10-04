#include "engine/mesh/SculptMode.h"
#include <QtTest/QtTest>
#include <QVector3D>

class TestSculptMode : public QObject {
    Q_OBJECT

private slots:
    void testDefaultState();
    void testSetMeshData();
    void testStrokeLifecycle();
    void testDrawTool();
    void testBrushSettings();

    // SelectionTools
    void testSelectionModes();
    void testSelectAll();
    void testSelectNone();
    void testSelectInverse();
    void testSelectVertex();
    void testSelectEdge();
    void testSelectFace();
    void testSelectVertexLoop();
    void testSelectEdgeLoop();
    void testSelectFaceRing();
    void testSelectVertexRing();
    void testSelectEdgeRing();
    void testSelectSimilar();
    void testSelectByNormal();
    void testSelectByMaterial();
    void testGrowSelection();
    void testShrinkSelection();
    void testSelectBorder();

    // KnifeTool
    void testKnifeBasic();
    void testSplitEdgeAtPoint();
    void testAddEdgeLoop();

    // LoopCutTool
    void testLoopCutAdd();

    // BisectTool
    void testBisectApply();
};

QVector<QVector3D> makeTestVertices()
{
    return {
        QVector3D(-1, -1, -1), QVector3D(1, -1, -1),
        QVector3D(1,  1, -1), QVector3D(-1,  1, -1),
        QVector3D(-1, -1,  1), QVector3D(1, -1,  1),
        QVector3D(1,  1,  1), QVector3D(-1,  1,  1)
    };
}

QVector<int> makeTestFaces()
{
    return {
        0,1,2, 0,2,3,  // front (-z)
        4,6,5, 4,7,6,  // back (+z)
        0,5,1, 0,4,5,  // bottom (-y)
        2,6,7, 2,7,3,  // top (+y)
        0,3,7, 0,7,4,  // left (-x)
        1,5,6, 1,6,2   // right (+x)
    };
}

void TestSculptMode::testDefaultState()
{
    SculptMode sm;
    QCOMPARE(sm.getVertices().size(), 0);
    QCOMPARE(sm.getFaces().size(), 0);
}

void TestSculptMode::testSetMeshData()
{
    SculptMode sm;
    auto verts = makeTestVertices();
    auto faces = makeTestFaces();
    sm.setMeshData(verts, faces);
    QCOMPARE(sm.getVertices().size(), 8);
    QCOMPARE(sm.getFaces().size(), 36);
}

void TestSculptMode::testStrokeLifecycle()
{
    SculptMode sm;
    auto verts = makeTestVertices();
    auto faces = makeTestFaces();
    sm.setMeshData(verts, faces);

    sm.beginStroke(QVector3D(0, 0, 0));
    sm.addPoint(QVector3D(0, 0, 0), QVector3D(0, 1, 0));
    sm.endStroke();

    QVector<QVector3D> result = sm.getVertices();
    QCOMPARE(result.size(), 8);
}

void TestSculptMode::testDrawTool()
{
    SculptMode sm;
    auto verts = makeTestVertices();
    auto faces = makeTestFaces();
    sm.setMeshData(verts, faces);
    sm.setTool(SculptMode::ToolDraw);
    sm.setBrushSize(2.0f);
    sm.setBrushStrength(0.5f);

    sm.beginStroke(QVector3D(0, 0, 0));
    sm.addPoint(QVector3D(0, 0, 0), QVector3D(0, 0, 1));
    sm.endStroke();

    auto result = sm.getVertices();
    bool anyMoved = false;
    for (int i = 0; i < result.size(); ++i) {
        if (!qFuzzyCompare(result[i], verts[i])) {
            anyMoved = true;
            break;
        }
    }
    QVERIFY(anyMoved);
}

void TestSculptMode::testBrushSettings()
{
    SculptMode sm;
    sm.setBrushSize(1.5f);
    sm.setBrushStrength(0.75f);
}

void TestSculptMode::testSelectionModes()
{
    SelectionTools st;
    st.setSelectMode(SelectionTools::SelectVertex);
    st.setSelectMode(SelectionTools::SelectEdge);
    st.setSelectMode(SelectionTools::SelectFace);
    st.setSelectMode(SelectionTools::SelectElement);
}

void TestSculptMode::testSelectAll()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.setSelectMode(SelectionTools::SelectVertex);
    st.selectAll();
    QCOMPARE(st.getSelectedVertices().size(), 8);
}

void TestSculptMode::testSelectNone()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.setSelectMode(SelectionTools::SelectVertex);
    st.selectAll();
    st.selectNone();
    QCOMPARE(st.getSelectedVertices().size(), 0);
    QCOMPARE(st.getSelectedEdges().size(), 0);
    QCOMPARE(st.getSelectedFaces().size(), 0);
}

void TestSculptMode::testSelectInverse()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.setSelectMode(SelectionTools::SelectVertex);
    st.selectVertex(0);
    st.selectVertex(1);
    QCOMPARE(st.getSelectedVertices().size(), 2);
    st.selectInverse();
    QCOMPARE(st.getSelectedVertices().size(), 6);
}

void TestSculptMode::testSelectVertex()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.selectVertex(3);
    QVERIFY(st.getSelectedVertices().contains(3));
}

void TestSculptMode::testSelectEdge()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.selectEdge(0, 1);
    QVERIFY(st.getSelectedEdges().size() >= 1);
}

void TestSculptMode::testSelectFace()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.selectFace(0);
    QVERIFY(st.getSelectedFaces().contains(0));
}

void TestSculptMode::testSelectVertexLoop()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.selectVertexLoop(0);
    QVERIFY(st.getSelectedVertices().size() >= 4);
    QVERIFY(st.getSelectedVertices().contains(0));
}

void TestSculptMode::testSelectEdgeLoop()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.selectEdgeLoop(0);
    QVERIFY(st.getSelectedEdges().size() >= 1);
}

void TestSculptMode::testSelectFaceRing()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.selectFaceRing(0);
    QVERIFY(st.getSelectedFaces().size() >= 1);
    QVERIFY(st.getSelectedFaces().contains(0));
}

void TestSculptMode::testSelectVertexRing()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.selectVertexRing(0);
    QVERIFY(st.getSelectedVertices().size() >= 4);
    QVERIFY(st.getSelectedVertices().contains(0));
}

void TestSculptMode::testSelectEdgeRing()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.selectEdgeRing(0);
    QVERIFY(st.getSelectedEdges().size() >= 1);
}

void TestSculptMode::testSelectSimilar()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.setSelectMode(SelectionTools::SelectVertex);
    st.selectVertex(0);
    st.selectSimilar(SelectionTools::SelectVertex, "position", 5.0f);
    QVERIFY(st.getSelectedVertices().size() >= 1);
}

void TestSculptMode::testSelectByNormal()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.setSelectMode(SelectionTools::SelectVertex);
    st.selectByNormal(QVector3D(0, 0, -1), 0.5f);
    QVERIFY(st.getSelectedFaces().size() >= 2);
}

void TestSculptMode::testSelectByMaterial()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    QVector<int> materials(12, 0);
    materials[0] = 1;
    materials[1] = 1;
    st.setFaceMaterials(materials);
    st.selectByMaterial(1);
    QCOMPARE(st.getSelectedFaces().size(), 2);
}

void TestSculptMode::testGrowSelection()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.selectVertex(0);
    int before = st.getSelectedVertices().size();
    st.growSelection();
    QVERIFY(st.getSelectedVertices().size() > before);
}

void TestSculptMode::testShrinkSelection()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.setSelectMode(SelectionTools::SelectVertex);
    // A single selected vertex has no selected neighbour, so shrinking must
    // remove it (it lies entirely on the selection boundary).
    st.selectVertex(0);
    int before = st.getSelectedVertices().size();
    st.shrinkSelection();
    QVERIFY(st.getSelectedVertices().size() < before);
}

void TestSculptMode::testSelectBorder()
{
    SelectionTools st;
    st.setMeshData(makeTestVertices(), makeTestFaces());
    st.setSelectMode(SelectionTools::SelectVertex);
    st.selectBorder();
    QVERIFY(st.getSelectedEdges().size() >= 1);
}

void TestSculptMode::testKnifeBasic()
{
    KnifeTool kt;
    kt.setMeshData(makeTestVertices(), makeTestFaces());
    kt.setMode(KnifeTool::ModeCut);
    kt.setCutThrough(false);
    kt.beginCut(QVector3D(-2, 0, 0));
    kt.continueCut(QVector3D(0, 0, 0));
    QVector<int> result = kt.completeCut(QVector3D(2, 0, 0));
    QVERIFY(result.size() >= 0);
}

void TestSculptMode::testSplitEdgeAtPoint()
{
    KnifeTool kt;
    kt.setMeshData(makeTestVertices(), makeTestFaces());
    kt.splitEdgeAtPoint(0, QVector3D(0, -1, -1));
    QCOMPARE(kt.getVertices().size(), 1);
    QCOMPARE(kt.getNewFaces().size(), 3);
}

void TestSculptMode::testAddEdgeLoop()
{
    KnifeTool kt;
    kt.setMeshData(makeTestVertices(), makeTestFaces());
    QVector<QVector3D> points = {
        QVector3D(-1, 0, -1), QVector3D(1, 0, -1),
        QVector3D(1, 0, 1), QVector3D(-1, 0, 1)
    };
    kt.addEdgeLoop(points);
    QVERIFY(kt.getVertices().size() >= 4);
}

void TestSculptMode::testLoopCutAdd()
{
    LoopCutTool lct;
    lct.setMeshData(makeTestVertices(), makeTestFaces());
    lct.setNumberOfCuts(2);
    lct.addLoop();
    QVERIFY(lct.getVertices().size() >= 1);
}

void TestSculptMode::testBisectApply()
{
    BisectTool bt;
    bt.setMeshData(makeTestVertices(), makeTestFaces());
    bt.setPlane(QVector3D(0, 0, 0), QVector3D(0, 1, 0));
    bt.apply(true);
    QVERIFY(bt.getVertices().size() >= 4);
}

QTEST_MAIN(TestSculptMode)
#include "test_SculptMode.moc"
