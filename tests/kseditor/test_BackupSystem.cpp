#include "engine/Tools/BackupSystem.h"
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

using namespace ks;

class TestBackupSystem : public QObject {
    Q_OBJECT

private slots:
    void test_instance();
    void test_setBackupDirectory();
    void test_createBackup();
    void test_getBackups();
    void test_deleteBackup();
    void test_autoBackup();
    void test_retentionPolicy();
    void test_incrementalBackup();
};

void TestBackupSystem::test_instance()
{
    BackupManager* bm = BackupManager::instance();
    QVERIFY(bm != nullptr);
    QCOMPARE(BackupManager::instance(), bm);
}

void TestBackupSystem::test_setBackupDirectory()
{
    BackupManager* bm = BackupManager::instance();
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    bm->setBackupDirectory(tmpDir.path());
    QCOMPARE(bm->getBackupDirectory(), tmpDir.path());
}

void TestBackupSystem::test_createBackup()
{
    BackupManager* bm = BackupManager::instance();
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    bm->setBackupDirectory(tmpDir.path());

    QString projDir = tmpDir.path() + "/project";
    QDir().mkpath(projDir);

    QString filePath = projDir + "/test.txt";
    QFile projFile(filePath);
    QVERIFY(projFile.open(QIODevice::WriteOnly));
    projFile.write("test data");
    projFile.close();

    QString backupId = bm->createBackup(filePath, "test backup");
    QVERIFY(!backupId.isEmpty());

    auto backups = bm->getBackups(filePath);
    QCOMPARE(backups.size(), 1);
    QCOMPARE(backups[0].description, QString("test backup"));

    bm->deleteBackup(backupId);
}

void TestBackupSystem::test_getBackups()
{
    BackupManager* bm = BackupManager::instance();
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    bm->setBackupDirectory(tmpDir.path());

    auto backups = bm->getBackups("/nonexistent");
    QVERIFY(backups.isEmpty());
}

void TestBackupSystem::test_deleteBackup()
{
    BackupManager* bm = BackupManager::instance();
    QVERIFY(!bm->deleteBackup("nonexistent_id"));
}

void TestBackupSystem::test_autoBackup()
{
    BackupManager* bm = BackupManager::instance();
    bm->setAutoBackup(true, 60);
    QVERIFY(bm->isAutoBackupEnabled());
    bm->setAutoBackup(false, 0);
    QVERIFY(!bm->isAutoBackupEnabled());
}

void TestBackupSystem::test_retentionPolicy()
{
    BackupRetention* br = BackupRetention::instance();
    QVERIFY(br != nullptr);

    BackupRetention::RetentionPolicy policy;
    policy.maxDailyBackups = 2;
    policy.maxWeeklyBackups = 4;
    policy.maxMonthlyBackups = 6;
    policy.maxTotalBackups = 10;

    br->setPolicy(policy);
    auto retrieved = br->getPolicy();
    QCOMPARE(retrieved.maxDailyBackups, 2);
    QCOMPARE(retrieved.maxTotalBackups, 10);

    QVector<QString> toDelete = br->getBackupsToDelete("/nonexistent");
    QVERIFY(toDelete.isEmpty());
}

void TestBackupSystem::test_incrementalBackup()
{
    IncrementalBackup* ib = IncrementalBackup::instance();
    QVERIFY(ib != nullptr);

    ib->addChange("/path/to/file.txt", "modified");
    QCOMPARE(ib->getChanges().size(), 1);
    QCOMPARE(ib->getChanges()[0].filePath, QString("/path/to/file.txt"));
}

QTEST_MAIN(TestBackupSystem)
#include "test_BackupSystem.moc"
