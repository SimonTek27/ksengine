#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "engine/Config/CspConfigParser.h"

class TestCspConfig : public QObject {
    Q_OBJECT

private:
    QTemporaryDir* m_tempDir = nullptr;
    QString m_dataDir;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testParseConfigFile();
    void testParseConfigFileMissing();
    void testSaveConfigFile();
    void testSaveLoadRoundTrip();
    void testParseBoolTrue();
    void testParseBoolFalse();
    void testParseFloat();
    void testParseInt();
    void testParseString();
    void testParseWeatherFx();
    void testParseLightingFx();
    void testParseParticlesFx();
    void testParsePhysicsExtensions();
    void testParseCarExtensions();
    void testParseTrackExtensions();
    void testSaveWeatherFx();
    void testSaveLightingFx();
    void testValidateExtension();
    void testValidateExtensionMissingName();
    void testIsCspInstalled();
    void testGetAvailableExtensions();

private:
    QString createConfigFile(const QString& name, const QString& content);
};

void TestCspConfig::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
    m_dataDir = m_tempDir->path();
}

void TestCspConfig::cleanupTestCase()
{
    delete m_tempDir;
}

QString TestCspConfig::createConfigFile(const QString& name, const QString& content)
{
    QString path = m_dataDir + "/" + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    f.write(content.toUtf8());
    f.close();
    return path;
}

void TestCspConfig::testParseConfigFile()
{
    QString path = createConfigFile("test_ext.ini",
        "# Test extension\n"
        "enabled: true\n"
        "intensity: 1.5\n"
        "count: 42\n"
        "name: MyExtension\n");

    CspConfigParser::CspExtension ext;
    bool ok = CspConfigParser::parseConfigFile(path, ext);
    QVERIFY(ok);
    QCOMPARE(ext.name, "test_ext");
    QCOMPARE(ext.boolSettings["enabled"], true);
    QCOMPARE(ext.floatSettings["intensity"], 1.5f);
    QCOMPARE(ext.intSettings["count"], 42);
    QCOMPARE(ext.stringSettings["name"], "MyExtension");
}

void TestCspConfig::testParseConfigFileMissing()
{
    CspConfigParser::CspExtension ext;
    bool ok = CspConfigParser::parseConfigFile("/nonexistent/file.ini", ext);
    QVERIFY(!ok);
}

void TestCspConfig::testSaveConfigFile()
{
    CspConfigParser::CspExtension ext;
    ext.name = "test_ext";
    ext.description = "Test extension";
    ext.boolSettings["enabled"] = true;
    ext.floatSettings["intensity"] = 2.0f;
    ext.intSettings["count"] = 10;
    ext.stringSettings["name"] = "Test";

    QString path = m_dataDir + "/saved_ext.ini";
    bool ok = CspConfigParser::saveConfigFile(ext, path);
    QVERIFY(ok);
    QVERIFY(QFile::exists(path));
}

void TestCspConfig::testSaveLoadRoundTrip()
{
    CspConfigParser::CspExtension original;
    original.name = "roundtrip";
    original.description = "Round-trip test";
    original.boolSettings["enabled"] = true;
    original.floatSettings["intensity"] = 3.14f;
    original.intSettings["count"] = 7;
    original.stringSettings["version"] = "v2.0";

    QString path = m_dataDir + "/roundtrip.ini";
    QVERIFY(CspConfigParser::saveConfigFile(original, path));

    CspConfigParser::CspExtension loaded;
    QVERIFY(CspConfigParser::parseConfigFile(path, loaded));
    QCOMPARE(loaded.name, "roundtrip");
    QCOMPARE(loaded.boolSettings["enabled"], true);
    QCOMPARE(loaded.floatSettings["intensity"], 3.14f);
    QCOMPARE(loaded.intSettings["count"], 7);
    QCOMPARE(loaded.stringSettings["version"], "v2.0");
}

void TestCspConfig::testParseBoolTrue()
{
    CspConfigParser::CspExtension ext;
    QString path = createConfigFile("bool_true.ini",
        "key_true: true\nkey_one: 1\nkey_yes: yes\n");
    QVERIFY(CspConfigParser::parseConfigFile(path, ext));
    QCOMPARE(ext.boolSettings["key_true"], true);
}

void TestCspConfig::testParseBoolFalse()
{
    CspConfigParser::CspExtension ext;
    QString path = createConfigFile("bool_false.ini",
        "key_false: false\nkey_zero: 0\nkey_no: no\n");
    QVERIFY(CspConfigParser::parseConfigFile(path, ext));
    QCOMPARE(ext.boolSettings["key_false"], false);
}

void TestCspConfig::testParseFloat()
{
    CspConfigParser::CspExtension ext;
    QString path = createConfigFile("float.ini",
        "value: 3.14\nsmall: 0.001\nexp: 1.5e3\n");
    QVERIFY(CspConfigParser::parseConfigFile(path, ext));
    QCOMPARE(ext.floatSettings["value"], 3.14f);
    QCOMPARE(ext.floatSettings["small"], 0.001f);
}

void TestCspConfig::testParseInt()
{
    CspConfigParser::CspExtension ext;
    QString path = createConfigFile("int.ini",
        "count: 42\nmax: 1000\nmin: -5\n");
    QVERIFY(CspConfigParser::parseConfigFile(path, ext));
    QCOMPARE(ext.intSettings["count"], 42);
    QCOMPARE(ext.intSettings["max"], 1000);
    QCOMPARE(ext.intSettings["min"], -5);
}

void TestCspConfig::testParseString()
{
    CspConfigParser::CspExtension ext;
    QString path = createConfigFile("string.ini",
        "name: Hello World\ntitle: CSP Config\npath: /some/path\n");
    QVERIFY(CspConfigParser::parseConfigFile(path, ext));
    QCOMPARE(ext.stringSettings["name"], "Hello World");
    QCOMPARE(ext.stringSettings["path"], "/some/path");
}

void TestCspConfig::testParseWeatherFx()
{
    QString path = createConfigFile("weather.ini",
        "ENABLED: 1\nWEATHER_SCRIPT: weather_script.lua\nTIME_MULTIPLIER: 2.0\nUSE_REAL_WEATHER: 0\n");
    auto config = CspConfigParser::parseWeatherFx(path);
    QVERIFY(config.enabled);
    QCOMPARE(config.scriptName, "weather_script.lua");
    QCOMPARE(config.timeMultiplier, 2.0f);
    QVERIFY(!config.useRealWeather);
}

void TestCspConfig::testParseLightingFx()
{
    QString path = createConfigFile("lighting.ini",
        "ENABLED: 1\nDYNAMIC_LIGHTS: 0\nENABLE_OCCLUSION: 1\nAMBIENT_MULTIPLIER: 0.8\nSUN_MULTIPLIER: 1.2\n");
    auto config = CspConfigParser::parseLightingFx(path);
    QVERIFY(config.enabled);
    QVERIFY(!config.dynamicLights);
    QVERIFY(config.enableOcclusion);
    QCOMPARE(config.ambientMultiplier, 0.8f);
    QCOMPARE(config.sunMultiplier, 1.2f);
}

void TestCspConfig::testParseParticlesFx()
{
    QString path = createConfigFile("particles.ini",
        "ENABLED: 1\nENABLE_SMOKE: 0\nENABLE_SPARKS: 1\nENABLE_GRASS: 1\nSMOKE_INTENSITY: 0.5\nSPARK_INTENSITY: 2.0\n");
    auto config = CspConfigParser::parseParticlesFx(path);
    QVERIFY(config.enabled);
    QVERIFY(!config.enableSmoke);
    QVERIFY(config.enableSparks);
    QCOMPARE(config.smokeIntensity, 0.5f);
    QCOMPARE(config.sparkIntensity, 2.0f);
}

void TestCspConfig::testParsePhysicsExtensions()
{
    QString path = createConfigFile("physics.ini",
        "ENABLED: 1\nENABLE_AERO: 0\nENABLE_SUSPENSION: 1\nENABLE_TIRES: 1\nAERO_MULTIPLIER: 0.9\n");
    auto config = CspConfigParser::parsePhysicsExtensions(path);
    QVERIFY(config.enabled);
    QVERIFY(!config.enableAero);
    QVERIFY(config.enableSuspension);
    QCOMPARE(config.aeroMultiplier, 0.9f);
}

void TestCspConfig::testParseCarExtensions()
{
    QString path = createConfigFile("car_ext.ini",
        "ENABLED: 1\nENABLE_REVERSE_LIGHTS: 0\nENABLE_TURN_SIGNALS: 1\nENABLE_ODOMETER: 1\nENABLE_WORKING_WIPERS: 0\n");
    auto config = CspConfigParser::parseCarExtensions(path);
    QVERIFY(config.enabled);
    QVERIFY(!config.enableReverseLights);
    QVERIFY(config.enableTurnSignals);
    QVERIFY(!config.enableWorkingWipers);
}

void TestCspConfig::testParseTrackExtensions()
{
    QString path = createConfigFile("track_ext.ini",
        "ENABLED: 1\nENABLE_GRASS_FX: 1\nENABLE_PARTICLES: 0\nGRASS_DISTANCE: 200.0\n");
    auto config = CspConfigParser::parseTrackExtensions(path);
    QVERIFY(config.enabled);
    QVERIFY(config.enableGrassFx);
    QVERIFY(!config.enableParticles);
    QCOMPARE(config.grassDistance, 200.0f);
}

void TestCspConfig::testSaveWeatherFx()
{
    CspConfigParser::CspWeatherFx config;
    config.enabled = true;
    config.scriptName = "test.lua";
    config.timeMultiplier = 1.5f;
    config.useRealWeather = true;

    QString path = m_dataDir + "/saved_weather.ini";
    QVERIFY(CspConfigParser::saveWeatherFx(config, path));
    QVERIFY(QFile::exists(path));

    auto loaded = CspConfigParser::parseWeatherFx(path);
    QVERIFY(loaded.enabled);
    QCOMPARE(loaded.scriptName, "test.lua");
    QCOMPARE(loaded.timeMultiplier, 1.5f);
}

void TestCspConfig::testSaveLightingFx()
{
    CspConfigParser::CspLightingFx config;
    config.enabled = true;
    config.dynamicLights = false;
    config.enableOcclusion = true;
    config.ambientMultiplier = 0.5f;

    QString path = m_dataDir + "/saved_lighting.ini";
    QVERIFY(CspConfigParser::saveLightingFx(config, path));
    QVERIFY(QFile::exists(path));

    auto loaded = CspConfigParser::parseLightingFx(path);
    QVERIFY(loaded.enabled);
    QVERIFY(!loaded.dynamicLights);
    QCOMPARE(loaded.ambientMultiplier, 0.5f);
}

void TestCspConfig::testValidateExtension()
{
    CspConfigParser::CspExtension ext;
    ext.name = "TestExt";
    ext.enabled = true;
    ext.description = "A test extension";

    QString error;
    bool valid = CspConfigParser::validateExtension(ext, &error);
    QVERIFY(valid);
    QVERIFY(error.isEmpty());
}

void TestCspConfig::testValidateExtensionMissingName()
{
    CspConfigParser::CspExtension ext;
    ext.enabled = true;

    QString error;
    bool valid = CspConfigParser::validateExtension(ext, &error);
    QVERIFY(!valid);
    QVERIFY(!error.isEmpty());
}

void TestCspConfig::testIsCspInstalled()
{
    // With a non-AC path, CSP should not be detected
    bool installed = CspConfigParser::isCspInstalled(m_dataDir + "/nonexistent_ac");
    QVERIFY(!installed);
}

void TestCspConfig::testGetAvailableExtensions()
{
    // Empty directory should yield no extensions
    QStringList exts = CspConfigParser::getAvailableExtensions(m_dataDir);
    QVERIFY(exts.isEmpty());

    // Create a CSP extension directory with subdirectories
    QDir().mkpath(m_dataDir + "/extensions/weather_fx");
    QDir().mkpath(m_dataDir + "/extensions/lighting_fx");
    createConfigFile("extensions/weather_fx/config.ini",
        "enabled: true\ndescription: Weather FX\n");
    exts = CspConfigParser::getAvailableExtensions(m_dataDir);
    QCOMPARE(exts.size(), 2);
    QVERIFY(exts.contains("weather_fx"));
    QVERIFY(exts.contains("lighting_fx"));
}

QTEST_MAIN(TestCspConfig)
#include "test_CspConfig.moc"
