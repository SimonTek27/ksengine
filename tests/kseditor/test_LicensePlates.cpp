#include <QtTest>
#include <QImage>
#include <QDir>
#include <QColor>
#include <QVector>

#include "LicensePlateEditorModule.h"
#include "QRCodeWriter.h"

using namespace ks;

class TestLicensePlates : public QObject {
    Q_OBJECT

private slots:
    void test_countryFormats();
    void test_generatePlateSimple();
    void test_validatePlateText();
    void test_batchGeneration();
    void test_atlasGeneration();
    void test_saveTexture();
    void test_qrCodeWriter();
    void test_qrCodeDifferentInputs();
    void test_qrCodeEmptyInput();
    void test_renderWithCornerRadius();
    void test_renderWithGradient();
    void test_renderWithTextAlignment();
    void test_holographicEffect();
    void test_ddsExport();
    void test_saveAsDDS();
    void test_countryFormatRoundTrip();
    void test_addCountryFormat();
    void test_plateStyleLoad();
};

void TestLicensePlates::test_countryFormats() {
    LicensePlatesManager manager;
    QStringList countries = manager.availableCountries();
    QVERIFY(countries.contains("IT"));
    QVERIFY(countries.contains("DE"));
    QVERIFY(countries.contains("UK"));
    QVERIFY(countries.contains("BE"));
    QVERIFY(countries.contains("AT"));
    QVERIFY(countries.contains("NO"));
    QVERIFY(countries.contains("DK"));
    QVERIFY(countries.contains("FI"));
    QVERIFY(countries.contains("PL"));
    QVERIFY(countries.contains("CZ"));
    QVERIFY(countries.contains("PT"));
    QCOMPARE(countries.size() >= 18, true);
}

void TestLicensePlates::test_generatePlateSimple() {
    LicensePlatesManager manager;

    PlateGenerationParams params;
    params.text = "AB 123 CD";
    params.width = 512;
    params.height = 128;
    params.backgroundColor = Qt::white;
    params.textColor = Qt::black;
    params.borderColor = Qt::black;
    params.borderWidth = 2.0f;
    params.fontFamily = "Arial";
    params.fontSize = 48;

    LicensePlateResult result = manager.generatePlateSimple(params);
    QVERIFY(result.success);
    QVERIFY(!result.texture.isNull());
    QCOMPARE(result.texture.width(), 512);
    QCOMPARE(result.texture.height(), 128);
}

void TestLicensePlates::test_validatePlateText() {
    LicensePlatesManager manager;

    // Italian format: AB-123-CD
    QVERIFY(manager.validatePlateText("AB123CD", "IT"));
    QVERIFY(manager.validatePlateText("AB 123 CD", "IT"));
    QVERIFY(!manager.validatePlateText("123", "IT"));
    QVERIFY(!manager.validatePlateText("ABCDEFG", "IT"));

    // German format
    QVERIFY(manager.validatePlateText("ABC1234", "DE"));
    QVERIFY(manager.validatePlateText("AB12", "DE"));

    // UK format
    QVERIFY(manager.validatePlateText("AB12CDE", "UK"));
}

void TestLicensePlates::test_batchGeneration() {
    LicensePlatesManager manager;

    PlateGenerationParams params;
    params.width = 256;
    params.height = 64;
    params.textColor = Qt::black;
    params.backgroundColor = Qt::white;

    std::vector<QString> texts = {"AAA 001", "BBB 002", "CCC 003"};
    auto results = manager.generatePlatesBatch(texts, params);

    QCOMPARE(results.size(), 3);
    for (const auto& r : results) {
        QVERIFY(r.success);
        QVERIFY(!r.texture.isNull());
    }
}

void TestLicensePlates::test_atlasGeneration() {
    LicensePlatesManager manager;

    PlateGenerationParams params;
    params.width = 256;
    params.height = 64;
    params.textColor = Qt::black;
    params.backgroundColor = Qt::white;

    std::vector<QString> texts = {"AAA", "BBB", "CCC", "DDD"};
    auto plates = manager.generatePlatesBatch(texts, params);

    QImage atlas = manager.createAtlas(plates, 1024);
    QVERIFY(!atlas.isNull());
    QVERIFY(atlas.width() > 0);
    QVERIFY(atlas.height() > 0);
}

void TestLicensePlates::test_saveTexture() {
    LicensePlatesManager manager;

    PlateGenerationParams params;
    params.text = "TEST";
    params.width = 128;
    params.height = 48;
    params.backgroundColor = Qt::white;
    params.textColor = Qt::black;

    LicensePlateResult result = manager.generatePlateSimple(params);
    QVERIFY(result.success);

    QString path = QDir::tempPath() + "/ks_plate_test.png";
    QVERIFY(manager.savePlateTexture(result, path));
    QVERIFY(QFile::exists(path));
    QFile::remove(path);
}

void TestLicensePlates::test_qrCodeWriter() {
    QImage qr = QRCodeWriter::encode("TEST123", 0, QRCodeWriter::M, 4);
    QVERIFY(!qr.isNull());
    QCOMPARE(qr.width(), qr.height());
    QVERIFY(qr.width() > 0);

    // Should have finder patterns (dark pixels in corners)
    QRgb topLeft = qr.pixel(2, 2);
    int gray = qGray(topLeft);
    QVERIFY(gray < 128);
}

void TestLicensePlates::test_qrCodeDifferentInputs() {
    QImage qr1 = QRCodeWriter::encode("ABC123", 0, QRCodeWriter::M, 4);
    QImage qr2 = QRCodeWriter::encode("XYZ789", 0, QRCodeWriter::M, 4);
    QVERIFY(!qr1.isNull());
    QVERIFY(!qr2.isNull());

    // Different inputs should produce different QR codes
    bool different = false;
    for (int y = 0; y < qr1.height() && !different; ++y) {
        for (int x = 0; x < qr1.width() && !different; ++x) {
            if (qr1.pixel(x, y) != qr2.pixel(x, y)) different = true;
        }
    }
    QVERIFY(different);
}

void TestLicensePlates::test_qrCodeEmptyInput() {
    QImage qr = QRCodeWriter::encode("", 0, QRCodeWriter::M, 4);
    QVERIFY(qr.isNull());
}

void TestLicensePlates::test_renderWithCornerRadius() {
    LicensePlatesManager manager;

    PlateGenerationParams params;
    params.text = "RADIUS";
    params.width = 512;
    params.height = 128;
    params.backgroundColor = Qt::white;
    params.textColor = Qt::black;
    params.cornerRadius = 16.0f;

    LicensePlateResult result = manager.generatePlateSimple(params);
    QVERIFY(result.success);
    QVERIFY(!result.texture.isNull());
}

void TestLicensePlates::test_renderWithGradient() {
    LicensePlatesManager manager;

    PlateGenerationParams params;
    params.text = "GRADIENT";
    params.width = 512;
    params.height = 128;
    params.backgroundColor = Qt::white;
    params.textColor = Qt::black;
    params.backgroundType = 1;
    params.gradientColor = QColor(200, 200, 200);

    LicensePlateResult result = manager.generatePlateSimple(params);
    QVERIFY(result.success);
    QVERIFY(!result.texture.isNull());
}

void TestLicensePlates::test_renderWithTextAlignment() {
    LicensePlatesManager manager;

    PlateGenerationParams params;
    params.text = "ALIGNED";
    params.width = 512;
    params.height = 128;
    params.backgroundColor = Qt::white;
    params.textColor = Qt::black;

    params.textAlignment = 1;
    LicensePlateResult left = manager.generatePlateSimple(params);
    QVERIFY(left.success);

    params.textAlignment = 2;
    LicensePlateResult right = manager.generatePlateSimple(params);
    QVERIFY(right.success);

    // Left and right aligned should differ (check across entire image)
    bool different = false;
    for (int y = 0; y < 128 && !different; ++y) {
        for (int x = 0; x < 512 && !different; ++x) {
            if (left.texture.pixel(x, y) != right.texture.pixel(x, y)) different = true;
        }
    }
    QVERIFY(different);
}

void TestLicensePlates::test_holographicEffect() {
    LicensePlatesManager manager;

    HolographicEffect effect;
    effect.enabled = true;
    effect.angle = 45.0;
    effect.intensity = 0.3;
    effect.primaryColor = QColor(100, 180, 255);
    effect.secondaryColor = QColor(255, 100, 200);
    manager.setHolographicEffect(effect);

    QImage testImage(100, 100, QImage::Format_ARGB32);
    testImage.fill(Qt::white);

    manager.applyHolographicEffect(testImage);

    // Effect should have changed pixels
    bool changed = false;
    for (int y = 0; y < 100 && !changed; ++y) {
        for (int x = 0; x < 100 && !changed; ++x) {
            if (testImage.pixel(x, y) != qRgb(255, 255, 255)) changed = true;
        }
    }
    QVERIFY(changed);
}

void TestLicensePlates::test_ddsExport() {
    LicensePlatesManager manager;

    PlateGenerationParams params;
    params.text = "DDS";
    params.width = 64;
    params.height = 32;
    params.backgroundColor = Qt::white;
    params.textColor = Qt::black;

    LicensePlateResult result = manager.generatePlateSimple(params);
    QVERIFY(result.success);

    QString ddsPath = QDir::tempPath() + "/ks_plate_test.dds";
    QVERIFY(manager.savePlateTexture(result, ddsPath));
    QVERIFY(QFile::exists(ddsPath));
    QVERIFY(QFile(ddsPath).size() > 0);
    QFile::remove(ddsPath);
}

void TestLicensePlates::test_saveAsDDS() {
    LicensePlatesManager manager;

    PlateGenerationParams params;
    params.text = "DDS2";
    params.width = 32;
    params.height = 32;
    params.backgroundColor = Qt::red;
    params.textColor = Qt::black;

    LicensePlateResult result = manager.generatePlateSimple(params);
    QVERIFY(result.success);

    QString path = QDir::tempPath() + "/ks_plate_test2.dds";
    QVERIFY(manager.saveAsDDS(result, path));
    QVERIFY(QFile::exists(path));
    QFile::remove(path);
}

void TestLicensePlates::test_countryFormatRoundTrip() {
    LicensePlatesManager manager;

    // Verify we can look up each country's format and it matches
    QStringList codes = manager.availableCountries();
    for (const auto& code : codes) {
        CountryFormat fmt = manager.getCountryFormat(code);
        QCOMPARE(fmt.code, code);
        QVERIFY(!fmt.name.isEmpty());
        QVERIFY(fmt.maxLength > 0);
        QVERIFY(fmt.backgroundColor.isValid());
        QVERIFY(fmt.textColor.isValid());
    }
}

void TestLicensePlates::test_addCountryFormat() {
    LicensePlatesManager manager;

    CountryFormat gr;
    gr.code = "GR";
    gr.name = "Greece";
    gr.plateExample = "AAA-1234";
    gr.pattern = QRegularExpression("^[A-Z]{3}[ -]?\\d{4}$");
    gr.maxLength = 7;
    gr.backgroundColor = Qt::white;
    gr.textColor = Qt::black;
    gr.borderColor = Qt::black;
    gr.hasCountryBand = true;
    gr.countryBandText = "GR";
    gr.hasEUStars = true;
    gr.fontFamily = "Arial";

    manager.addCountryFormat(gr);
    QVERIFY(manager.availableCountries().contains("GR"));
    CountryFormat loaded = manager.getCountryFormat("GR");
    QCOMPARE(loaded.name, "Greece");
    QVERIFY(manager.validatePlateText("ABC1234", "GR"));
}

void TestLicensePlates::test_plateStyleLoad() {
    // Write a temp style file
    QString stylePath = QDir::tempPath() + "/ks_test_style.ini";
    QFile file(stylePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QTextStream out(&file);
    out << "size = {520, 110}\n";
    out << "textRect = {50, 20, 420, 70}\n";
    out << "fontFamily = \"Arial\"\n";
    out << "fontSize = 48\n";
    out << "textColor = {0, 0, 0}\n";
    out << "backgroundColor = {255, 255, 255}\n";
    out << "borderColor = {0, 0, 0}\n";
    out << "borderWidth = 2\n";
    file.close();

    LicensePlatesManager manager;
    PlateStyle style;
    QVERIFY(manager.loadStyle(stylePath, style));
    QCOMPARE(style.size, QSize(520, 110));
    QCOMPARE(style.textFont.family(), "Arial");
    QCOMPARE(style.textColor, QColor(0, 0, 0));
    QCOMPARE(style.backgroundColor, QColor(255, 255, 255));

    QFile::remove(stylePath);
}

QTEST_MAIN(TestLicensePlates)
#include "test_LicensePlates.moc"
