#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QStandardPaths>
#include "sdk/kseditor/modules/modmanager/ContentRepair.h"
#include "sdk/kseditor/modules/modmanager/ContentRepairTool.h"

using namespace ks;

class TestContentRepair : public QObject {
    Q_OBJECT

private:
    QTemporaryDir* m_tempDir = nullptr;
    QString m_dataDir;

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ContentValidator tests
    void testValidateFileExists();
    void testValidateFileExistsMissing();
    void testValidateFileSize();
    void testValidateIniFile();
    void testValidateIniFileMissingSection();
    void testValidateIniFileMissingKey();
    void testValidateMeshFileKn5();
    void testValidateMeshFileFbx();
    void testValidateMeshFileObj();
    void testValidateSoundFileWav();
    void testValidateSoundFileBadHeader();

    // ContentRepairEngine tests
    void testCalculateFileHash();
    void testVerifyFileIntegrity();
    void testBackupRestore();

    // Physics data validation (from ContentRepairTool)
    void testValidatePhysicsValuesMassAndFuel();
    void testValidatePhysicsValuesEngine();
    void testValidateSuspensionGeometry();
    void testValidateAeroBalance();
    void testValidateTyreData();
    void testValidateEngineCurve();

    // Car/Track structure tests
    void testValidateCarStructure();

private:
    QString createIniFile(const QString& name, const QString& content);
    QString createBinaryFile(const QString& name, const QByteArray& data);
    QByteArray createWavHeader(quint32 sampleRate);
};

void TestContentRepair::initTestCase()
{
    QCoreApplication::setApplicationName("kseditor_test");
    QCoreApplication::setOrganizationName("ksEditor");

    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
    m_dataDir = m_tempDir->path();
}

void TestContentRepair::cleanupTestCase()
{
    delete m_tempDir;
}

QString TestContentRepair::createIniFile(const QString& name, const QString& content)
{
    QString path = m_dataDir + "/" + name;
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(content.toUtf8());
    f.close();
    return path;
}

QString TestContentRepair::createBinaryFile(const QString& name, const QByteArray& data)
{
    QString path = m_dataDir + "/" + name;
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    f.close();
    return path;
}

QByteArray TestContentRepair::createWavHeader(quint32 sampleRate)
{
    QByteArray wav;
    // RIFF header
    wav.append("RIFF", 4);
    quint32 fileSize = 36 + 16; // 36 + fmt chunk size
    wav.append(reinterpret_cast<const char*>(&fileSize), 4);
    wav.append("WAVE", 4);
    // fmt chunk
    wav.append("fmt ", 4);
    quint32 fmtSize = 16;
    wav.append(reinterpret_cast<const char*>(&fmtSize), 4);
    quint16 audioFmt = 1; // PCM
    wav.append(reinterpret_cast<const char*>(&audioFmt), 2);
    quint16 channels = 2;
    wav.append(reinterpret_cast<const char*>(&channels), 2);
    wav.append(reinterpret_cast<const char*>(&sampleRate), 4);
    quint32 byteRate = sampleRate * 2 * 2;
    wav.append(reinterpret_cast<const char*>(&byteRate), 4);
    quint16 blockAlign = 4;
    wav.append(reinterpret_cast<const char*>(&blockAlign), 2);
    quint16 bitsPerSample = 16;
    wav.append(reinterpret_cast<const char*>(&bitsPerSample), 2);
    // data chunk
    wav.append("data", 4);
    quint32 dataSize = 1024;
    wav.append(reinterpret_cast<const char*>(&dataSize), 4);
    wav.append(QByteArray(1024, '\0'));
    return wav;
}

// ============================================================================
// ContentValidator tests
// ============================================================================

void TestContentRepair::testValidateFileExists()
{
    QString path = createIniFile("exists.ini", "[TEST]\nKEY=1\n");
    auto issues = ContentValidator::validateFileExists(path, "Test file", ContentIssue::Error);
    QVERIFY(issues.isEmpty());
}

void TestContentRepair::testValidateFileExistsMissing()
{
    auto issues = ContentValidator::validateFileExists(
        m_dataDir + "/nonexistent.ini", "Missing file", ContentIssue::Error);
    QCOMPARE(issues.size(), 1);
    QCOMPARE(issues[0].severity, ContentIssue::Error);
}

void TestContentRepair::testValidateFileSize()
{
    QString path = createIniFile("small.ini", "A");
    auto issues = ContentValidator::validateFileSize(path, 1);
    QVERIFY(issues.isEmpty());

    issues = ContentValidator::validateFileSize(path, 100);
    QCOMPARE(issues.size(), 1);
    QCOMPARE(issues[0].id, "file_too_small");
}

void TestContentRepair::testValidateIniFile()
{
    QString path = createIniFile("test.ini",
        "[SECTION_A]\nKEY1=val1\nKEY2=val2\n");
    auto issues = ContentValidator::validateIniFile(path,
        {"SECTION_A"},
        {{"SECTION_A", {"KEY1", "KEY2"}}});
    QVERIFY(issues.isEmpty());
}

void TestContentRepair::testValidateIniFileMissingSection()
{
    QString path = createIniFile("test.ini",
        "[OTHER]\nKEY1=val1\n");
    auto issues = ContentValidator::validateIniFile(path,
        {"REQUIRED_SECTION"},
        {});
    QCOMPARE(issues.size(), 1);
    QCOMPARE(issues[0].id, "missing_section");
}

void TestContentRepair::testValidateIniFileMissingKey()
{
    QString path = createIniFile("test.ini",
        "[SECTION_A]\nKEY1=val1\n");
    auto issues = ContentValidator::validateIniFile(path,
        {"SECTION_A"},
        {{"SECTION_A", {"KEY1", "KEY2"}}});
    QCOMPARE(issues.size(), 1);
    QCOMPARE(issues[0].id, "missing_key");
}

void TestContentRepair::testValidateMeshFileKn5()
{
    // Valid KN5: real magic "sc6969" + version 5, padded past the 1024-byte
    // "empty mesh" threshold
    QByteArray kn5Data("sc6969", 6);
    kn5Data.append(QByteArray::fromHex("05000000")); // i32 version = 5
    kn5Data.append(QByteArray(1024 - kn5Data.size(), '\0'));
    QString path = createBinaryFile("test.kn5", kn5Data);
    auto issues = ContentValidator::validateMeshFile(path);
    QVERIFY(issues.isEmpty());

    // Bad magic
    path = createBinaryFile("bad.kn5", QByteArray(1024, 'X'));
    issues = ContentValidator::validateMeshFile(path);
    QCOMPARE(issues.size(), 1);
    QCOMPARE(issues[0].id, "kn5_bad_magic");
}

void TestContentRepair::testValidateMeshFileFbx()
{
    // Valid FBX: "Kaydara FBX Binary  " + version bytes + padding
    QByteArray fbxData("Kaydara FBX Binary  \x00\x1a\x00\x00", 23);
    fbxData.append(QByteArray(100, ' '));
    QString path = createBinaryFile("test.fbx", fbxData);
    auto issues = ContentValidator::validateMeshFile(path);
    QVERIFY(issues.isEmpty());

    // Bad FBX
    path = createBinaryFile("bad.fbx", QByteArray(100, 'X'));
    issues = ContentValidator::validateMeshFile(path);
    QCOMPARE(issues.size(), 1);
}

void TestContentRepair::testValidateMeshFileObj()
{
    // Valid OBJ: has vertices AND faces
    QString path = createIniFile("test.obj",
        "# Wavefront OBJ\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    auto issues = ContentValidator::validateMeshFile(path);
    QVERIFY(issues.isEmpty());
}

void TestContentRepair::testValidateSoundFileWav()
{
    QByteArray wav = createWavHeader(44100);
    QString path = createBinaryFile("test.wav", wav);
    auto issues = ContentValidator::validateSoundFile(path);
    QVERIFY(issues.isEmpty());
}

void TestContentRepair::testValidateSoundFileBadHeader()
{
    QString path = createBinaryFile("bad.wav", QByteArray("OGG\x00\x00\x00\x00WAVE", 12));
    auto issues = ContentValidator::validateSoundFile(path);
    bool found = false;
    for (const auto& issue : issues)
        if (issue.id == "wav_bad_header") found = true;
    QVERIFY(found);
}

// ============================================================================
// ContentRepairEngine tests
// ============================================================================

void TestContentRepair::testCalculateFileHash()
{
    QString path = createIniFile("hash_test.ini", "Content to hash");
    QString hash = ContentRepairEngine::calculateFileHash(path);
    QVERIFY(!hash.isEmpty());
    QCOMPARE(hash.length(), 64);
}

void TestContentRepair::testVerifyFileIntegrity()
{
    QString path = createIniFile("integrity_test.ini", "Verify this content");
    QString hash = ContentRepairEngine::calculateFileHash(path);
    QVERIFY(ContentRepairEngine::verifyFileIntegrity(path, hash));

    QVERIFY(!ContentRepairEngine::verifyFileIntegrity(path, "0000000000000000000000000000000000000000000000000000000000000000"));
}

void TestContentRepair::testBackupRestore()
{
    QString path = createIniFile("backup_test.ini", "Original content");

    bool backedUp = ContentRepairEngine::backupBeforeFix(path);
    if (!backedUp) {
        // backupDir may not be writable in test env; skip with manual test
        QSKIP("backupBeforeFix failed (AppLocalDataLocation may not be writable in test env)");
    }

    // Modify the file
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write("Modified content");
    f.close();

    QVERIFY(ContentRepairEngine::restoreFromBackup(path));

    f.open(QIODevice::ReadOnly | QIODevice::Text);
    QCOMPARE(f.readAll(), QByteArray("Original content"));
    f.close();
}

// ============================================================================
// Physics data validation (from ContentRepairTool)
// ============================================================================

void TestContentRepair::testValidatePhysicsValuesMassAndFuel()
{
    QString dataDir = m_dataDir + "/car/data";
    QDir().mkpath(dataDir);

    createIniFile("car/data/car.ini",
        "[BASIC]\nMASSES=1500|100|50\nMAXFUEL=50\n");

    auto issues = ContentRepairTool::validatePhysicsValues(m_dataDir + "/car");
    bool hasMass = false, hasFuel = false;
    for (const auto& issue : issues) {
        if (issue.id == "MASS_OUT_OF_RANGE") hasMass = true;
        if (issue.id == "FUEL_OUT_OF_RANGE") hasFuel = true;
    }
    QVERIFY(!hasMass);
    QVERIFY(!hasFuel);

    createIniFile("car/data/car.ini",
        "[BASIC]\nMASSES=50000|100|50\nMAXFUEL=500\n");

    issues = ContentRepairTool::validatePhysicsValues(m_dataDir + "/car");
    bool foundMass = false, foundFuel = false;
    for (const auto& issue : issues) {
        if (issue.id == "MASS_OUT_OF_RANGE") foundMass = true;
        if (issue.id == "FUEL_OUT_OF_RANGE") foundFuel = true;
    }
    QVERIFY(foundMass);
    QVERIFY(foundFuel);
}

void TestContentRepair::testValidatePhysicsValuesEngine()
{
    QString dataDir = m_dataDir + "/engine_car/data";
    QDir().mkpath(dataDir);

    createIniFile("engine_car/data/engine.ini",
        "[ENGINE]\nMAXRPM=8000\nIDLE_RPM=850\nFUEL_CONSUMPTION=0.05\n");

    auto issues = ContentRepairTool::validatePhysicsValues(m_dataDir + "/engine_car");
    bool hasMaxRpm = false, hasIdleRpm = false, hasFuelCons = false;
    for (const auto& issue : issues) {
        if (issue.id == "MAXRPM_OUT_OF_RANGE") hasMaxRpm = true;
        if (issue.id == "IDLE_RPM_OUT_OF_RANGE") hasIdleRpm = true;
        if (issue.id == "FUEL_CONSUMPTION_ODD") hasFuelCons = true;
    }
    QVERIFY(!hasMaxRpm);
    QVERIFY(!hasIdleRpm);
    QVERIFY(!hasFuelCons);

    createIniFile("engine_car/data/engine.ini",
        "[ENGINE]\nMAXRPM=99999\nIDLE_RPM=9999\nFUEL_CONSUMPTION=50\n");

    issues = ContentRepairTool::validatePhysicsValues(m_dataDir + "/engine_car");
    for (const auto& issue : issues) {
        if (issue.id == "MAXRPM_OUT_OF_RANGE") hasMaxRpm = true;
        if (issue.id == "IDLE_RPM_OUT_OF_RANGE") hasIdleRpm = true;
        if (issue.id == "FUEL_CONSUMPTION_ODD") hasFuelCons = true;
    }
    QVERIFY(hasMaxRpm);
    QVERIFY(hasIdleRpm);
    QVERIFY(hasFuelCons);
}

void TestContentRepair::testValidateSuspensionGeometry()
{
    QString dataDir = m_dataDir + "/susp_car/data";
    QDir().mkpath(dataDir);

    createIniFile("susp_car/data/suspensions.ini",
        "[FRONT]\nSPRING_RATE=80000\nDAMPING=5000\nRIDE_HEIGHT=0.12\nCAMBER=-2.5\nTOE=0.1\n"
        "[REAR]\nSPRING_RATE=60000\nDAMPING=4000\nRIDE_HEIGHT=0.10\nCAMBER=-1.5\nTOE=0.2\n");

    auto issues = ContentRepairTool::validateSuspensionGeometry(m_dataDir + "/susp_car");
    QVERIFY(issues.isEmpty());

    createIniFile("susp_car/data/suspensions.ini",
        "[FRONT]\nSPRING_RATE=50\nDAMPING=5\nRIDE_HEIGHT=2.0\nCAMBER=-20\nTOE=15\n");

    issues = ContentRepairTool::validateSuspensionGeometry(m_dataDir + "/susp_car");
    QCOMPARE(issues.size(), 5);
}

void TestContentRepair::testValidateAeroBalance()
{
    QString dataDir = m_dataDir + "/aero_car/data";
    QDir().mkpath(dataDir);

    createIniFile("aero_car/data/aero.ini",
        "[FRONT]\nCD=0.35\nFRONT=500\nREAR=800\n");

    auto issues = ContentRepairTool::validateAeroBalance(m_dataDir + "/aero_car");
    QVERIFY(issues.isEmpty());

    // Missing aero.ini - data dir exists but no aero.ini
    QDir().mkpath(m_dataDir + "/no_aero_car/data");
    issues = ContentRepairTool::validateAeroBalance(m_dataDir + "/no_aero_car");
    QCOMPARE(issues.size(), 1);
    QCOMPARE(issues[0].id, "MISSING_AERO_INI");
}

void TestContentRepair::testValidateTyreData()
{
    QString dataDir = m_dataDir + "/tyre_car/data";
    QDir().mkpath(dataDir);

    createIniFile("tyre_car/data/tyres.ini",
        "[FRONT]\nPRESSURE_LEFT=26\nPRESSURE_RIGHT=26\nCARCASS=2.5\nHEAT_CAPACITY=500\n");

    auto issues = ContentRepairTool::validateTyreData(m_dataDir + "/tyre_car");
    QVERIFY(issues.isEmpty());

    createIniFile("tyre_car/data/tyres.ini",
        "[FRONT]\nPRESSURE_LEFT=5\nCARCASS=50\nHEAT_CAPACITY=0.5\n");

    issues = ContentRepairTool::validateTyreData(m_dataDir + "/tyre_car");
    bool foundPressure = false, foundCarcass = false, foundHeat = false;
    for (const auto& issue : issues) {
        if (issue.id == "TYRE_PRESSURE_OUT_OF_RANGE") foundPressure = true;
        if (issue.id == "CARCASS_STIFFNESS_ODD") foundCarcass = true;
        if (issue.id == "HEAT_CAPACITY_ODD") foundHeat = true;
    }
    QVERIFY(foundPressure);
    QVERIFY(foundCarcass);
    QVERIFY(foundHeat);
}

void TestContentRepair::testValidateEngineCurve()
{
    QString dataDir = m_dataDir + "/curve_car/data";
    QDir().mkpath(dataDir);

    createIniFile("curve_car/data/engine.ini",
        "[ENGINE]\n"
        "RPM_0=1000\nRPM_1=3000\nRPM_2=6000\nRPM_3=8000\n"
        "POWER_0=100\nPOWER_1=250\nPOWER_2=350\nPOWER_3=300\n"
        "TORQUE_0=200\nTORQUE_1=300\nTORQUE_2=320\nTORQUE_3=250\n");

    auto issues = ContentRepairTool::validateEngineData(m_dataDir + "/curve_car");
    bool mismatch = false;
    for (const auto& issue : issues)
        if (issue.id.startsWith("ENGINE_")) mismatch = true;
    QVERIFY(!mismatch);

    createIniFile("curve_car/data/engine.ini", "[ENGINE]\nMAXRPM=8000\n");

    issues = ContentRepairTool::validateEngineData(m_dataDir + "/curve_car");
    bool foundNoRpm = false;
    for (const auto& issue : issues)
        if (issue.id == "ENGINE_NO_RPM_DATA") foundNoRpm = true;
    QVERIFY(foundNoRpm);

    createIniFile("curve_car/data/engine.ini",
        "[ENGINE]\nRPM_0=1000\nRPM_1=3000\nPOWER_0=100\nTORQUE_0=200\n");

    issues = ContentRepairTool::validateEngineData(m_dataDir + "/curve_car");
    bool foundMismatch = false;
    for (const auto& issue : issues)
        if (issue.id == "ENGINE_POWER_MISMATCH") foundMismatch = true;
    QVERIFY(foundMismatch);
}

// ============================================================================
// Structure validation tests
// ============================================================================

void TestContentRepair::testValidateCarStructure()
{
    QString carPath = m_dataDir + "/test_car";
    QDir().mkpath(carPath + "/data");

    // AC car structure: car.ini etc in root, engine/drivetrain/brakes/aero in data/
    createIniFile("test_car/car.ini",
        "[BASIC]\nMODEL=test\nNAME=Test Car\nWEIGHT=1200\nMAXFUEL=50\nDIMENSIONS=2.0x1.0x1.0\n");
    createIniFile("test_car/suspensions.ini",
        "[FRONT]\nSPRING_RATE=80000\nDAMPING=5000\nRIDE_HEIGHT=0.12\nTIRE_DIAMETER=0.65\nCAMBER=-2.5\nTOE=0.1\nWHEELBASE=2.6\nTRACK=1.5\n"
        "[REAR]\nSPRING_RATE=60000\nDAMPING=4000\nRIDE_HEIGHT=0.10\nTIRE_DIAMETER=0.65\nCAMBER=-1.5\nTOE=0.2\nWHEELBASE=2.6\nTRACK=1.5\n");
    createIniFile("test_car/data/engine.ini",
        "[ENGINE]\nMAXRPM=8000\nMINRPM=850\nTORQUE_CURVE=1.0\nFUEL_CONSUMPTION=0.05\n");
    createIniFile("test_car/data/drivetrain.ini",
        "[DRIVETRAIN]\nDRIVE_TYPE=RWD\nFINAL_RATIO=3.5\nGEAR_RATIOS=2.5|1.8|1.3|1.0|0.8\n");
    createIniFile("test_car/data/brakes.ini",
        "[FRONT_BRAKES]\nTORQUE=2000\nBIAS=0.6\n[REAR_BRAKES]\nTORQUE=1500\nBIAS=0.4\n");
    createIniFile("test_car/data/aero.ini",
        "[AERO]\nCD=0.35\nFRONT_AREA=1.8\nFRONT_LIFT=0.05\nREAR_LIFT=0.10\n");
    QByteArray carKn5("sc6969", 6);
    carKn5.append(QByteArray::fromHex("05000000")); // version 5
    carKn5.append(QByteArray(1024 - carKn5.size(), '\0'));
    createBinaryFile("test_car/test.kn5", carKn5);
    createBinaryFile("test_car/data/test.kn5", carKn5);

    auto issues = ContentValidator::validateCarStructure(carPath);
    for (const auto& issue : issues)
        qDebug() << "Issue:" << issue.id << issue.title;
    QVERIFY(issues.isEmpty());
}

QTEST_MAIN(TestContentRepair)
#include "test_ContentRepair.moc"
