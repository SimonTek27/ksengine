#include <QtTest/QtTest>

#include <QTemporaryDir>

#include "sdk/kseditor/modules/modellingEditor/LightSystem.h"

using namespace ks;

class TestLightSystem : public QObject
{
    Q_OBJECT

private slots:
    void addRemoveFind();
    void serializeRoundTrip();
    void parseIESValid();
    void parseIESRejectsGarbage();
    void iesMultiplierInterpolates();
};

void TestLightSystem::addRemoveFind()
{
    LightSystem sys;
    QVERIFY(!sys.hasAny());

    LightDef a;
    a.objectId = 1; a.name = "Key"; a.type = 1; a.color = QColor(255, 0, 0); a.intensity = 2.5f;
    LightDef b;
    b.objectId = 2; b.name = "Fill"; b.type = 2;

    QVERIFY(sys.add(a));
    QVERIFY(sys.add(b));
    QVERIFY(sys.hasAny());
    QCOMPARE(sys.lightObjectIds(), QVector<int>({ 1, 2 }));
    QVERIFY(sys.has(1));
    QVERIFY(!sys.add(a)); // duplicate rejected
    QCOMPARE(sys.lights().size(), 2);

    LightDef* found = sys.find(2);
    QVERIFY(found);
    QCOMPARE(found->name, QString("Fill"));
    QCOMPARE(found->type, 2);

    QVERIFY(sys.remove(1));
    QVERIFY(!sys.has(1));
    QVERIFY(!sys.remove(99));
    sys.clearAll();
    QVERIFY(!sys.hasAny());
}

void TestLightSystem::serializeRoundTrip()
{
    LightDef a;
    a.objectId = 7;
    a.name = "Rim";
    a.type = 2;
    a.color = QColor(0, 180, 255);
    a.intensity = 1.75f;
    a.enabled = false;
    a.range = 120.0f;
    a.spotAngleDeg = 60.0f;
    a.spotPenumbraDeg = 20.0f;
    a.iesProfile = "C:/profiles/studio.ies";
    a.iesIntensity = 0.5f;

    LightDef b;
    b.fromVariant(a.toVariant());

    QCOMPARE(b.name, a.name);
    QCOMPARE(b.type, a.type);
    QCOMPARE(b.color, a.color);
    QCOMPARE(b.intensity, a.intensity);
    QCOMPARE(b.enabled, a.enabled);
    QCOMPARE(b.range, a.range);
    QCOMPARE(b.spotAngleDeg, a.spotAngleDeg);
    QCOMPARE(b.spotPenumbraDeg, a.spotPenumbraDeg);
    QCOMPARE(b.iesProfile, a.iesProfile);
    QCOMPARE(b.iesIntensity, a.iesIntensity);
}

void TestLightSystem::parseIESValid()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("test.ies");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("IESNA:LM-63-2002\n"
            "[TEST] ksEditor unit\n"
            "[MORE] nothing\n"
            "TILT=NONE\n"
            "1 1000 1.0\n"
            "5 1\n"
            "0 20 40 60 90\n"
            "0\n"
            "1000 900 700 400 100\n");
    f.close();

    QVector<float> curve;
    QVERIFY(LightSystem::parseIESFile(path, curve));
    QCOMPARE(curve.size(), 91);
    // Normalized peak at 0 deg, falling to 0.1 at 90 deg.
    QVERIFY(std::abs(curve[0] - 1.0f) < 1e-4f);
    QVERIFY(std::abs(curve[45] - 0.7f) < 1e-3f);
    QVERIFY(std::abs(curve[90] - 0.1f) < 1e-3f);
}

void TestLightSystem::parseIESRejectsGarbage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("bad.ies");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("this is not an IES file\nno numbers here\n");
    f.close();

    QVector<float> curve;
    QVERIFY(!LightSystem::parseIESFile(path, curve));
    QVERIFY(curve.isEmpty());

    QVERIFY(!LightSystem::parseIESFile(dir.filePath("missing.ies"), curve));
}

void TestLightSystem::iesMultiplierInterpolates()
{
    LightDef def;
    def.iesCurve.resize(91);
    for (int i = 0; i < 91; ++i)
        def.iesCurve[i] = 1.0f - i / 90.0f; // linear 1..0
    def.iesIntensity = 2.0f;

    QVERIFY(std::abs(def.iesMultiplier(0.0f) - 2.0f) < 1e-3f);
    QVERIFY(std::abs(def.iesMultiplier(45.0f) - 1.0f) < 0.05f);
    QVERIFY(std::abs(def.iesMultiplier(90.0f) - 0.0f) < 1e-3f);
    QVERIFY(std::abs(def.iesMultiplier(180.0f) - 0.0f) < 1e-3f); // clamped to 90

    LightDef plain;
    QVERIFY(std::abs(plain.iesMultiplier(30.0f) - 1.0f) < 1e-6f); // no curve -> 1
}

QTEST_APPLESS_MAIN(TestLightSystem)
#include "test_LightSystem.moc"
