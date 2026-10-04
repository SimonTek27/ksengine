#include "engine/mesh/AdvancedSculpt.h"
#include <QtTest/QtTest>

using namespace ks::graphics;

class TestAdvancedSculpt : public QObject {
    Q_OBJECT

private slots:
    void testDefaultState();
    void testSetSculptMode();
    void testDyntopoSettings();
    void testSymmetryMode();
    void testAutoMaskEnabled();
    void testDetailSettings();

    // SculptProject
    void testProjectLayers();
    void testProjectActiveLayer();
    void testProjectMaskStroke();

    // DyntopoRefiner
    void testRefinerDetail();

    // SculptBrushPreset
    void testPresetAddRemove();
    void testDefaultPresets();
};

void TestAdvancedSculpt::testDefaultState()
{
    AdvancedSculptMode asmMode;
    QCOMPARE(asmMode.getSculptMode(), AdvancedSculptMode::SculptModeType::Draw);
    QCOMPARE(asmMode.isDyntopoEnabled(), false);
    QCOMPARE(asmMode.isAutoMaskEnabled(), false);
}

void TestAdvancedSculpt::testSetSculptMode()
{
    AdvancedSculptMode asmMode;
    asmMode.setSculptMode(AdvancedSculptMode::SculptModeType::Smooth);
    QCOMPARE(asmMode.getSculptMode(), AdvancedSculptMode::SculptModeType::Smooth);

    asmMode.setSculptMode(AdvancedSculptMode::SculptModeType::GrabElastic);
    QCOMPARE(asmMode.getSculptMode(), AdvancedSculptMode::SculptModeType::GrabElastic);
}

void TestAdvancedSculpt::testDyntopoSettings()
{
    AdvancedSculptMode asmMode;
    asmMode.setDyntopo(true);
    QVERIFY(asmMode.isDyntopoEnabled());
    asmMode.setDyntopo(false);
    QVERIFY(!asmMode.isDyntopoEnabled());
}

void TestAdvancedSculpt::testSymmetryMode()
{
    AdvancedSculptMode asmMode;
    asmMode.symmetry = AdvancedSculptMode::SymmetryMode::X;
    QCOMPARE(static_cast<int>(asmMode.symmetry), static_cast<int>(AdvancedSculptMode::SymmetryMode::X));
    asmMode.symmetry = AdvancedSculptMode::SymmetryMode::XYZ;
    QCOMPARE(static_cast<int>(asmMode.symmetry), static_cast<int>(AdvancedSculptMode::SymmetryMode::XYZ));
}

void TestAdvancedSculpt::testAutoMaskEnabled()
{
    AdvancedSculptMode asmMode;
    asmMode.setAutoMask(true);
    QVERIFY(asmMode.isAutoMaskEnabled());
}

void TestAdvancedSculpt::testDetailSettings()
{
    AdvancedSculptMode asmMode;
    asmMode.setDetail(5.0f);
    QCOMPARE(asmMode.getDetail(), 5.0f);
    asmMode.setDetail(0.5f);
    QCOMPARE(asmMode.getDetail(), 0.5f);
}

void TestAdvancedSculpt::testProjectLayers()
{
    SculptProject project;
    QCOMPARE(project.getActiveLayer(), 0);
    project.addLayer("Layer 1");
    project.addLayer("Layer 2");
    project.setActiveLayer(1);
    QCOMPARE(project.getActiveLayer(), 1);
}

void TestAdvancedSculpt::testProjectActiveLayer()
{
    SculptProject project;
    project.addLayer("Base");
    project.setActiveLayer(0);
    QCOMPARE(project.getActiveLayer(), 0);
}

void TestAdvancedSculpt::testProjectMaskStroke()
{
    SculptProject project;
    project.addLayer("Base");
    project.setActiveLayer(0);
    QVector<int> verts = {0, 1, 2, 3};
    project.addMaskStroke(0, verts, 0.8f);
    QVector<int> masked = project.getMaskedVertices(0);
    QCOMPARE(masked.size(), 4);
}

void TestAdvancedSculpt::testRefinerDetail()
{
    DyntopoRefiner refiner;
    refiner.setDetail(8.0f);
    refiner.setOriginalDetail(4.0f);
}

void TestAdvancedSculpt::testPresetAddRemove()
{
    SculptBrushPreset presets;
    const int initialCount = presets.getPresets().size();
    QVERIFY(initialCount >= 5); // default presets are built in the constructor

    SculptBrushPreset::PresetData p;
    p.name = "TestPreset";
    p.mode = AdvancedSculptMode::SculptModeType::ClayStrips;
    p.size = 0.5f;
    p.strength = 0.75f;

    presets.addPreset(p);
    QCOMPARE(presets.getPresets().size(), initialCount + 1);

    int idx = -1;
    QVector<SculptBrushPreset::PresetData> all = presets.getPresets();
    for (int i = 0; i < all.size(); ++i) {
        if (all[i].name == "TestPreset") { idx = i; break; }
    }
    QVERIFY(idx >= 0);

    SculptBrushPreset::PresetData* retrieved = presets.getPreset(idx);
    QVERIFY(retrieved != nullptr);
    QCOMPARE(retrieved->name, QString("TestPreset"));
    QCOMPARE(retrieved->mode, AdvancedSculptMode::SculptModeType::ClayStrips);

    presets.removePreset(idx);
    QCOMPARE(presets.getPresets().size(), initialCount);
}

void TestAdvancedSculpt::testDefaultPresets()
{
    SculptBrushPreset presets;
    QVERIFY(presets.getPresets().size() >= 5);
}

QTEST_MAIN(TestAdvancedSculpt)
#include "test_AdvancedSculpt.moc"
