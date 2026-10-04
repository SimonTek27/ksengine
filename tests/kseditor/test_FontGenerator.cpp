#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFontDatabase>
#include "modules/fontEditor/FontGenerator.h"

class TestFontGenerator : public QObject {
    Q_OBJECT

private:
    FontAtlasGenerator* m_gen = nullptr;
    QTemporaryDir* m_tempDir = nullptr;
    QString m_fontFamily;

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void testSystemFonts();
    void testGenerateAtlas();
    void testGenerateAtlasSmallBuffer();
    void testGenerateAtlasEmptyFamily();
    void testApplyHintingDisabled();
    void testApplyHintingNormalLevel();
    void testApplyHintingFullLevel();
    void testApplyHintingGridFitting();
    void testExtractKerningPairs();
    void testGetKerning();
    void testKerningDisabled();
    void testAvailableUnicodeRanges();
    void testGenerateCharsetForRange();
    void testSetUnicodeRange();
    void testSaveLoadPresetRoundTrip();
    void testLoadPresetMissingFile();
    void testSaveResultToPng();
    void testSaveInvalidResult();
    void testHintingConfigAccessors();
    void testValidateGlyphCoverage();
    void testValidateGlyphCoverageEmpty();

    // Metrics optimizer
    void testAnalyzeMetricsEmpty();
    void testAnalyzeMetricsBasic();
    void testSuggestOptimalAtlasSize();

    // Anti-aliasing modes
    void testAntiAliasModes();

    // SDF atlas
    void testSDFAtlas();
    void testMSDFAtlas();
    void testSDFSpreadValues();
};

void TestFontGenerator::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());

    QStringList families = QFontDatabase::families();
    if (families.size() > 0) {
        if (families.contains("Segoe UI")) m_fontFamily = "Segoe UI";
        else if (families.contains("Arial")) m_fontFamily = "Arial";
        else m_fontFamily = families.first();
    }
}

void TestFontGenerator::init()
{
    m_gen = new FontAtlasGenerator();
}

void TestFontGenerator::cleanup()
{
    delete m_gen;
    m_gen = nullptr;
}

void TestFontGenerator::testSystemFonts()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    QStringList fonts = FontAtlasGenerator::systemFonts();
    QVERIFY(fonts.size() > 0);
    QVERIFY(fonts.contains(m_fontFamily));
}

void TestFontGenerator::testGenerateAtlas()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 32;
    cfg.atlasWidth = 256;
    cfg.atlasHeight = 128;
    cfg.charset = "ABCabc123";

    AtlasResult result = m_gen->generate(cfg);
    QVERIFY(result.success);
    QVERIFY(result.errorMsg.isEmpty());
    QCOMPARE(result.config.glyphs.size(), 9);
    QCOMPARE(result.image.width(), 256);
    QCOMPARE(result.image.height(), 128);
    QCOMPARE(result.config.uvMap.size(), 9);
}

void TestFontGenerator::testGenerateAtlasSmallBuffer()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 64;
    cfg.atlasWidth = 32;
    cfg.atlasHeight = 32;
    cfg.charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    AtlasResult result = m_gen->generate(cfg);
    QVERIFY(result.success);
    QVERIFY(result.config.glyphs.size() < 26);
}

void TestFontGenerator::testGenerateAtlasEmptyFamily()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    AtlasConfig cfg;
    cfg.fontSize = 24;
    cfg.atlasWidth = 128;
    cfg.atlasHeight = 128;
    cfg.globalHeight = 30;
    cfg.globalVPad = 4;
    cfg.charset = "Test";

    AtlasResult result = m_gen->generate(cfg);
    QVERIFY(result.success);
    QCOMPARE(result.config.glyphs.size(), 4);
}

void TestFontGenerator::testApplyHintingDisabled()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    HintingConfig hc;
    hc.enableAutoHinting = false;
    m_gen->setHintingConfig(hc);

    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 32;
    cfg.charset = "AB";

    AtlasResult result = m_gen->generate(cfg);
    QVERIFY(result.success);

    QVector<GlyphMetrics> hinted = m_gen->applyHinting(result.config);
    QCOMPARE(hinted.size(), 2);
    QCOMPARE(hinted[0].codepoint, result.config.glyphs[0].codepoint);
}

void TestFontGenerator::testApplyHintingNormalLevel()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    HintingConfig hc;
    hc.enableAutoHinting = true;
    hc.hintingLevel = 1;
    m_gen->setHintingConfig(hc);

    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 32;
    cfg.charset = "ABC";
    cfg.hinting.hintingLevel = 1;

    AtlasResult result = m_gen->generate(cfg);
    QVERIFY(result.success);
    QVERIFY(result.config.glyphs.size() == 3);

    QVector<GlyphMetrics> hinted = m_gen->applyHinting(result.config);
    QCOMPARE(hinted.size(), 3);

    GlyphMetrics original = result.config.glyphs[0];
    GlyphMetrics hinted_gm = hinted[0];
    QCOMPARE(hinted_gm.codepoint, original.codepoint);
    QCOMPARE(hinted_gm.cellWidth % 4, 0);
}

void TestFontGenerator::testApplyHintingFullLevel()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    HintingConfig hc;
    hc.enableAutoHinting = true;
    hc.hintingLevel = 2;
    m_gen->setHintingConfig(hc);

    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 32;
    cfg.charset = "ABC";
    cfg.hinting.hintingLevel = 2;

    AtlasResult result = m_gen->generate(cfg);
    QVERIFY(result.success);

    QVector<GlyphMetrics> hinted = m_gen->applyHinting(result.config);
    QCOMPARE(hinted.size(), 3);

    for (const auto& g : hinted) {
        QCOMPARE(g.cellWidth % 2, 0);
    }
}

void TestFontGenerator::testApplyHintingGridFitting()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    HintingConfig hc;
    hc.enableAutoHinting = true;
    hc.enableGridFitting = true;
    hc.hintingLevel = 1;
    m_gen->setHintingConfig(hc);

    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 32;
    cfg.charset = "ABC";

    AtlasResult result = m_gen->generate(cfg);
    QVERIFY(result.success);

    QVector<GlyphMetrics> hinted = m_gen->applyHinting(result.config);
    QCOMPARE(hinted.size(), 3);

    for (const auto& g : hinted) {
        QVERIFY(g.lsbDelta != 0 || g.rsbDelta != 0);
    }
}

void TestFontGenerator::testExtractKerningPairs()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    QString charset = "AVTo";

    m_gen->setKerningEnabled(true);
    QVector<KerningPair> pairs = m_gen->extractKerningPairs(m_fontFamily, 48, charset);

    QVERIFY(pairs.size() >= 0);
    for (const auto& p : pairs) {
        QVERIFY(charset.contains(QChar(p.left)));
        QVERIFY(charset.contains(QChar(p.right)));
    }
}

void TestFontGenerator::testGetKerning()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    QString charset = "AVTo";

    QVector<KerningPair> pairs = m_gen->extractKerningPairs(m_fontFamily, 48, charset);
    if (!pairs.isEmpty()) {
        int val = m_gen->getKerning(pairs[0].left, pairs[0].right);
        QCOMPARE(val, pairs[0].kerning);
    }

    // Unknown pair returns 0
    int val = m_gen->getKerning(static_cast<uint32_t>('X'), static_cast<uint32_t>('Z'));
    QCOMPARE(val, 0);
}

void TestFontGenerator::testKerningDisabled()
{
    m_gen->setKerningEnabled(false);

    QVector<KerningPair> pairs = m_gen->extractKerningPairs(m_fontFamily, 48, "AV");
    QVERIFY(pairs.isEmpty());

    int val = m_gen->getKerning(static_cast<uint32_t>('A'), static_cast<uint32_t>('V'));
    QCOMPARE(val, 0);
}

void TestFontGenerator::testAvailableUnicodeRanges()
{
    QStringList ranges = FontAtlasGenerator::availableUnicodeRanges();
    QVERIFY(ranges.size() >= 10);
    QVERIFY(ranges.contains("Basic Latin (ASCII)"));
    QVERIFY(ranges.contains("CJK Unified Ideographs"));
    QVERIFY(ranges.contains("Cyrillic"));
}

void TestFontGenerator::testGenerateCharsetForRange()
{
    QString ascii = FontAtlasGenerator::generateCharsetForRange("Basic Latin (ASCII)");
    QVERIFY(ascii.size() > 0);
    QVERIFY(ascii.contains(QChar(0x41)));
    QVERIFY(ascii.contains(QChar(0x20)));
    QVERIFY(ascii.contains(QChar(0x7E)));

    QString cyrillic = FontAtlasGenerator::generateCharsetForRange("Cyrillic");
    QVERIFY(cyrillic.size() > 0);
    QVERIFY(cyrillic.contains(QChar(0x400)));

    QString cjk = FontAtlasGenerator::generateCharsetForRange("CJK Unified Ideographs");
    QVERIFY(cjk.size() > 0);
    QVERIFY(cjk.size() <= 5000);

    QString unknown = FontAtlasGenerator::generateCharsetForRange("Unknown Range");
    QVERIFY(unknown.size() > 0);
    QVERIFY(unknown.contains('A'));
}

void TestFontGenerator::testSetUnicodeRange()
{
    bool ok = m_gen->setUnicodeRange("Basic Latin (ASCII)");
    QVERIFY(ok);
}

void TestFontGenerator::testSaveLoadPresetRoundTrip()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 36;
    cfg.globalHeight = 72;
    cfg.charset = "ABC";

    GlyphMetrics gm;
    gm.codepoint = static_cast<uint32_t>('A');
    gm.cellWidth = 40;
    gm.hPad = 2;
    gm.vPad = 10;
    cfg.glyphs.append(gm);
    gm.codepoint = static_cast<uint32_t>('B');
    gm.cellWidth = 38;
    cfg.glyphs.append(gm);
    gm.codepoint = static_cast<uint32_t>('C');
    gm.cellWidth = 36;
    cfg.glyphs.append(gm);

    QString acfPath = m_tempDir->path() + "/test_preset.acf";
    bool saved = m_gen->savePreset(cfg, acfPath);
    QVERIFY(saved);
    QVERIFY(QFile::exists(acfPath));

    bool loadOk = false;
    AtlasConfig loaded = m_gen->loadPreset(acfPath, &loadOk);
    QVERIFY(loadOk);
    QCOMPARE(loaded.fontFamily, m_fontFamily);
    QCOMPARE(loaded.fontSize, 36);
    QCOMPARE(loaded.globalHeight, 72);
    QCOMPARE(loaded.glyphs.size(), 3);
}

void TestFontGenerator::testLoadPresetMissingFile()
{
    bool ok = true;
    AtlasConfig cfg = m_gen->loadPreset("/nonexistent/path/test.acf", &ok);
    QVERIFY(!ok);
    QCOMPARE(cfg.fontFamily, QString());
}

void TestFontGenerator::testSaveResultToPng()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");
    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 24;
    cfg.atlasWidth = 128;
    cfg.atlasHeight = 64;
    cfg.charset = "Test";

    AtlasResult result = m_gen->generate(cfg);
    QVERIFY(result.success);

    QString pngPath = m_tempDir->path() + "/test_atlas.png";
    QString error;
    bool saved = m_gen->save(result, pngPath, &error);
    QVERIFY(saved);
    QVERIFY(QFile::exists(pngPath));

    QString acfPath = pngPath;
    acfPath.replace(".png", ".acf");
    QVERIFY(QFile::exists(acfPath));
}

void TestFontGenerator::testSaveInvalidResult()
{
    AtlasResult invalidResult;
    invalidResult.success = false;

    QString error;
    bool saved = m_gen->save(invalidResult, "/tmp/nowhere.png", &error);
    QVERIFY(!saved);
    QVERIFY(!error.isEmpty());
}

void TestFontGenerator::testHintingConfigAccessors()
{
    HintingConfig defaultCfg = m_gen->hintingConfig();
    QVERIFY(defaultCfg.enableAutoHinting);

    HintingConfig newCfg;
    newCfg.enableAutoHinting = false;
    newCfg.hintingLevel = 2;
    newCfg.enableSubpixel = true;
    newCfg.optimizeForLCD = true;
    m_gen->setHintingConfig(newCfg);

    HintingConfig retrieved = m_gen->hintingConfig();
    QCOMPARE(retrieved.enableAutoHinting, false);
    QCOMPARE(retrieved.hintingLevel, 2);
    QCOMPARE(retrieved.enableSubpixel, true);
    QCOMPARE(retrieved.optimizeForLCD, true);
}

void TestFontGenerator::testValidateGlyphCoverage()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");

    auto report = m_gen->validateGlyphCoverage(m_fontFamily, 32, "ABCabc123");
    QCOMPARE(report.totalRequested, 9);
    QVERIFY(report.coveragePercent > 0.0);
    QVERIFY(report.available > 0);
    QCOMPARE(report.available + report.missing, report.totalRequested);
}

void TestFontGenerator::testValidateGlyphCoverageEmpty()
{
    auto report = m_gen->validateGlyphCoverage("", 32, "");
    QCOMPARE(report.totalRequested, 0);
    QCOMPARE(report.coveragePercent, 0.0);
}

// ============================================================================
// Metrics Optimizer Tests
// ============================================================================

void TestFontGenerator::testAnalyzeMetricsEmpty()
{
    AtlasConfig emptyCfg;
    auto result = m_gen->analyzeMetrics(emptyCfg);
    QCOMPARE(result.averageAdvance, 0.0);
    QCOMPARE(result.glyphOverflowCount, 0);
    QVERIFY(result.overflowGlyphs.isEmpty());
}

void TestFontGenerator::testAnalyzeMetricsBasic()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");

    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 48;
    cfg.charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    auto result = m_gen->analyzeMetrics(cfg);
    QVERIFY(result.averageAdvance > 0.0);
    QVERIFY(result.maxAdvance >= result.averageAdvance);
    QVERIFY(result.minAdvance <= result.averageAdvance);
    QVERIFY(result.suggestedGlobalHeight > 0);
    QVERIFY(result.suggestedGlobalVPad > 0);
    QVERIFY(result.suggestedHPad > 0);
    QVERIFY(result.optimalAtlasWidth >= 128);
    QVERIFY(result.optimalAtlasHeight >= 128);
}

void TestFontGenerator::testSuggestOptimalAtlasSize()
{
    QVector<GlyphMetrics> glyphs;
    GlyphMetrics gm;
    gm.codepoint = 'A';
    gm.cellWidth = 50;

    // Empty case
    auto sizes = FontAtlasGenerator::suggestOptimalAtlasSize(glyphs, 85);
    QCOMPARE(sizes.size(), 2);
    QCOMPARE(sizes[0], "512");

    // One glyph
    glyphs.append(gm);
    sizes = FontAtlasGenerator::suggestOptimalAtlasSize(glyphs, 85);
    int w = sizes[0].toInt();
    int h = sizes[1].toInt();
    QVERIFY(w >= 50);
    QVERIFY(h >= 85);

    // Many glyphs requiring large atlas
    for (int i = 0; i < 100; ++i) {
        glyphs.append(gm);
    }
    sizes = FontAtlasGenerator::suggestOptimalAtlasSize(glyphs, 85);
    QVERIFY(sizes[0].toInt() >= 256);
}

// ============================================================================
// SDF Atlas Tests
// ============================================================================

void TestFontGenerator::testSDFAtlas()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");

    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 32;
    cfg.atlasWidth = 128;
    cfg.atlasHeight = 128;
    cfg.charset = "ABC";

    FontAtlasGenerator::SDFConfig sdfCfg;
    sdfCfg.spread = 6;
    sdfCfg.onedgeValue = 128;
    sdfCfg.padding = 4;
    sdfCfg.scale = 2.0;

    AtlasResult result = m_gen->generateSDF(cfg, sdfCfg);
    QVERIFY(result.success);
    QCOMPARE(result.image.format(), QImage::Format_Grayscale8);
    QVERIFY(result.image.width() <= 128);
    QVERIFY(result.image.height() <= 128);
    QVERIFY(!result.config.glyphs.isEmpty());
}

void TestFontGenerator::testMSDFAtlas()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");

    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 32;
    cfg.atlasWidth = 256;
    cfg.atlasHeight = 256;
    cfg.charset = "ABC";

    FontAtlasGenerator::SDFConfig sdfCfg;
    sdfCfg.spread = 6;
    sdfCfg.onedgeValue = 128;
    sdfCfg.padding = 4;
    sdfCfg.scale = 2.5;

    AtlasResult result = m_gen->generateMSDF(cfg, sdfCfg);
    QVERIFY(result.success);
    QCOMPARE(result.image.format(), QImage::Format_RGB888);
    QVERIFY(!result.config.glyphs.isEmpty());
}

void TestFontGenerator::testSDFSpreadValues()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");

    AtlasConfig cfg;
    cfg.fontFamily = m_fontFamily;
    cfg.fontSize = 24;
    cfg.atlasWidth = 256;
    cfg.atlasHeight = 256;
    cfg.charset = "AB";

    // Test with different spread values
    for (int spread : {2, 8, 16}) {
        FontAtlasGenerator::SDFConfig sdfCfg;
        sdfCfg.spread = spread;
        AtlasResult result = m_gen->generateSDF(cfg, sdfCfg);
        QVERIFY(result.success);
        QVERIFY(!result.config.glyphs.isEmpty());
    }
}

// ============================================================================
// Anti-aliasing Mode Tests
// ============================================================================

void TestFontGenerator::testAntiAliasModes()
{
    if (m_fontFamily.isEmpty())
        QSKIP("No system fonts available in this environment");

    // All modes should produce valid atlases
    auto testMode = [&](AntiAliasMode mode) {
        HintingConfig hc;
        hc.antiAlias = mode;
        m_gen->setHintingConfig(hc);

        AtlasConfig cfg;
        cfg.fontFamily = m_fontFamily;
        cfg.fontSize = 32;
        cfg.atlasWidth = 128;
        cfg.atlasHeight = 256;
        cfg.globalHeight = 40;
        cfg.charset = "ABCabc123";
        cfg.hinting.antiAlias = mode;

        AtlasResult result = m_gen->generate(cfg);
        QVERIFY2(result.success, QString("AA mode %1 failed").arg(static_cast<int>(mode)).toUtf8().constData());
        QCOMPARE(result.config.glyphs.size(), 9);
    };

    testMode(AntiAliasMode::None);
    testMode(AntiAliasMode::Standard);
    testMode(AntiAliasMode::SubpixelRGB);
    testMode(AntiAliasMode::LCD);
}

QTEST_MAIN(TestFontGenerator)
#include "test_FontGenerator.moc"
