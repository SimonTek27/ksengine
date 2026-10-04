#include "engine/Tools/AutoSave.h"
#include "tools/CrashRecovery.h"
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

using namespace ks;

class TestAutoSave : public QObject {
    Q_OBJECT

private slots:
    void test_documentDefaults();
    void test_documentLoadSave();
    void test_documentModified();
    void test_autoSaveDefaults();
    void test_autoSaveEnableDisable();
    void test_autoSaveInterval();
    void test_autoSaveMaxBackups();
    void test_documentManager();
    void test_crashRecovery();
};

void TestAutoSave::test_documentDefaults()
{
    Document doc;
    QVERIFY(!doc.getId().isEmpty());
    QVERIFY(doc.getPath().isEmpty());
    QVERIFY(!doc.isModified());
    QVERIFY(doc.canSave());
}

void TestAutoSave::test_documentLoadSave()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    QString filePath = tmpDir.path() + "/test_doc.txt";
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("content");
    file.close();

    Document doc;
    QVERIFY(doc.load(filePath));
    QCOMPARE(doc.getName(), QString("test_doc"));
    QVERIFY(!doc.isModified());
    QVERIFY(doc.save());
    QVERIFY(!doc.isModified());

    QVERIFY(doc.save(tmpDir.path() + "/saved_as.txt"));
}

void TestAutoSave::test_documentModified()
{
    Document doc;
    QVERIFY(!doc.isModified());
    doc.setModified(true);
    QVERIFY(doc.isModified());
    doc.setModified(false);
    QVERIFY(!doc.isModified());
}

void TestAutoSave::test_autoSaveDefaults()
{
    AutoSave as;
    QVERIFY(as.isEnabled());
    QCOMPARE(as.getInterval(), 300);
    QCOMPARE(as.getMaxBackups(), 5);
}

void TestAutoSave::test_autoSaveEnableDisable()
{
    AutoSave as;
    as.setEnabled(false);
    QVERIFY(!as.isEnabled());
    as.setEnabled(true);
    QVERIFY(as.isEnabled());
}

void TestAutoSave::test_autoSaveInterval()
{
    AutoSave as;
    as.setInterval(60);
    QCOMPARE(as.getInterval(), 60);
    as.setInterval(600);
    QCOMPARE(as.getInterval(), 600);
}

void TestAutoSave::test_autoSaveMaxBackups()
{
    AutoSave as;
    as.setMaxBackups(10);
    QCOMPARE(as.getMaxBackups(), 10);
    as.setMaxBackups(3);
    QCOMPARE(as.getMaxBackups(), 3);
}

void TestAutoSave::test_documentManager()
{
    DocumentManager dm;
    QCOMPARE(dm.getDocuments().size(), 0);
    QVERIFY(!dm.hasUnsavedChanges());
    QVERIFY(dm.saveAll());

    auto* doc = new Document(&dm);
    doc->setId("doc1");
    doc->setPath("/path/to/doc1.txt");
    dm.addDocument(doc);
    QCOMPARE(dm.getDocuments().size(), 1);

    dm.setActiveDocument(doc);
    QCOMPARE(dm.getActiveDocument(), doc);

    dm.removeDocument("doc1");
    QCOMPARE(dm.getDocuments().size(), 0);
}

void TestAutoSave::test_crashRecovery()
{
    CrashRecovery cr;
    QVERIFY(!cr.hasSession());

    cr.startSession();
    QVERIFY(cr.hasSession());

    cr.addOpenDocument("/path/doc.txt");
    cr.setActiveDocument("/path/doc.txt");
    cr.saveSession();

    auto session = cr.getSession();
    QCOMPARE(session.openDocuments.size(), 1);
    QCOMPARE(session.lastActiveDocument, QString("/path/doc.txt"));

    cr.clearSession();
    QVERIFY(!cr.hasSession());

    cr.endSession();
}

QTEST_MAIN(TestAutoSave)
#include "test_AutoSave.moc"
