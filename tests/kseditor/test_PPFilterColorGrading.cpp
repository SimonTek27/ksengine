#include "PPFilterColorGrading.h"
#include <QtTest/QtTest>
#include <QTemporaryDir>

using namespace ks;

class TestPPFilterColorGrading : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {}
    void cleanupTestCase() {}

    void testDefaultParams()
    {
        auto p = PPFilterColorGrading::defaultParams();
        QCOMPARE(p.gamma, 1.0f);
        QCOMPARE(p.contrast, 1.0f);
        QCOMPARE(p.brightness, 1.0f);
        QCOMPARE(p.saturation, 1.0f);
    }

    void testCinematicParams()
    {
        auto p = PPFilterColorGrading::cinematicParams();
        QVERIFY(p.contrast > 1.0f);
        QVERIFY(p.saturation < 1.0f);
        QVERIFY(p.temperature < 0.0f);
    }

    void testVividParams()
    {
        auto p = PPFilterColorGrading::vividParams();
        QVERIFY(p.saturation > 1.0f);
        QVERIFY(p.vibrance > 0.0f);
    }

    void testLUTGeneration()
    {
        PPFilterColorGrading grading;
        QCOMPARE(grading.lutSize(), 33);

        auto lut = grading.generateLUT3D(33);
        int expected = 33 * 33 * 33 * 3;
        QCOMPARE(lut.size(), expected);

        // All values should be in 0-1 range
        for (float v : lut) {
            QVERIFY(v >= 0.0f);
            QVERIFY(v <= 1.0f);
        }
    }

    void testLUTIdentity()
    {
        PPFilterColorGrading grading;
        grading.setParams(PPFilterColorGrading::defaultParams());

        auto lut = grading.generateLUT3D(33);
        int lutSize = 33;

        // Center sample should roughly preserve identity
        int mid = (lutSize / 2);
        int idx = ((mid * lutSize + mid) * lutSize + mid) * 3;
        QVERIFY(qAbs(lut[idx] - 0.5f) < 0.05f);
        QVERIFY(qAbs(lut[idx + 1] - 0.5f) < 0.05f);
        QVERIFY(qAbs(lut[idx + 2] - 0.5f) < 0.05f);
    }

    void testAdjustChannel()
    {
        PPFilterColorGrading grading;

        float neutral = grading.adjustChannel(0.5f, 0.0f, 1.0f, 1.0f);
        QCOMPARE(neutral, 0.5f);

        float lifted = grading.adjustChannel(0.5f, 0.1f, 1.0f, 1.0f);
        QVERIFY(lifted > 0.5f);

        float reduced = grading.adjustChannel(0.5f, 0.0f, 0.5f, 1.0f);
        QVERIFY(reduced < 0.5f);

        float gained = grading.adjustChannel(0.5f, 0.0f, 1.0f, 1.5f);
        QVERIFY(gained > 0.5f);

        // Clamping
        float over = grading.adjustChannel(1.0f, 0.5f, 1.0f, 1.0f);
        QCOMPARE(over, 1.0f);
    }

    void testToneCurve()
    {
        PPFilterColorGrading grading;

        QVector<QPair<float, float>> curve;
        curve.append({0.0f, 0.0f});
        curve.append({0.5f, 0.3f});
        curve.append({1.0f, 1.0f});

        float v = grading.applyToneCurve(0.5f, curve);
        QCOMPARE(v, 0.3f);

        float below = grading.applyToneCurve(0.0f, curve);
        QCOMPARE(below, 0.0f);

        float above = grading.applyToneCurve(1.0f, curve);
        QCOMPARE(above, 1.0f);

        float mid = grading.applyToneCurve(0.25f, curve);
        QVERIFY(mid > 0.0f && mid < 0.3f);
    }

    void testExportImportCubeLUT()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        PPFilterColorGrading grading;
        grading.setParams(PPFilterColorGrading::cinematicParams());

        QString path = tmpDir.filePath("test_lut.cube");
        bool exported = grading.exportCubeLUT(path);
        QVERIFY(exported);

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QString content = file.readAll();
        file.close();

        QVERIFY(content.contains("LUT_3D_SIZE"));
        QVERIFY(content.contains("TITLE"));

        // Import into a new instance
        PPFilterColorGrading grading2;
        bool imported = grading2.importCubeLUT(path);
        QVERIFY(imported);
        QCOMPARE(grading2.lutSize(), 33);
    }

    void testToJsonFromJson()
    {
        PPFilterColorGrading grading;
        grading.setParams(PPFilterColorGrading::vintageParams());

        QJsonObject json = grading.toJson();
        QVERIFY(!json.isEmpty());
        QVERIFY(qAbs(json["saturation"].toDouble() - 0.7) < 0.001);
        QVERIFY(qAbs(json["contrast"].toDouble() - 0.95) < 0.001);
        QVERIFY(json.contains("temperature"));

        PPFilterColorGrading grading2;
        bool restored = grading2.fromJson(json);
        QVERIFY(restored);

        auto p2 = grading2.params();
        QCOMPARE(p2.saturation, 0.7f);
        QCOMPARE(p2.contrast, 0.95f);
    }

    void testApplyToImage()
    {
        PPFilterColorGrading grading;
        QVector<float> image;
        for (int i = 0; i < 9; i++) {
            image.append(i / 8.0f);
        }

        auto result = grading.applyToImage(image, 3, 3);
        QCOMPARE(result.size(), 9); // 3 pixels x 3 channels
        for (float v : result) {
            QVERIFY(v >= 0.0f);
            QVERIFY(v <= 1.0f);
        }
    }

    void testVintageVsDefault()
    {
        PPFilterColorGrading defaultGrading;
        defaultGrading.setParams(PPFilterColorGrading::defaultParams());
        auto defaultLut = defaultGrading.generateLUT3D(17);

        PPFilterColorGrading vintageGrading;
        vintageGrading.setParams(PPFilterColorGrading::vintageParams());
        auto vintageLut = vintageGrading.generateLUT3D(17);

        // Vintage should differ from default
        bool differs = false;
        for (int i = 0; i < defaultLut.size(); ++i) {
            if (qAbs(defaultLut[i] - vintageLut[i]) > 0.01f) {
                differs = true;
                break;
            }
        }
        QVERIFY(differs);
    }

    void testLUTDifferentSizes()
    {
        PPFilterColorGrading grading;

        for (int size : {8, 16, 33, 64}) {
            auto lut = grading.generateLUT3D(size);
            int expected = size * size * size * 3;
            QCOMPARE(lut.size(), expected);
        }
    }

    void testMutateThenRecover()
    {
        PPFilterColorGrading grading;
        auto original = PPFilterColorGrading::defaultParams();
        grading.setParams(original);

        auto before = grading.params();
        auto p = PPFilterColorGrading::cinematicParams();
        grading.setParams(p);
        grading.setParams(original);

        auto after = grading.params();
        QCOMPARE(after.gamma, before.gamma);
        QCOMPARE(after.saturation, before.saturation);
    }
};

QTEST_MAIN(TestPPFilterColorGrading)
#include "test_PPFilterColorGrading.moc"
