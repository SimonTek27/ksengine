#include "engine/assets/RecentFilesManager.h"
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>

using namespace ks;

class TestRecentFilesManager : public QObject {
    Q_OBJECT

private slots:
    void test_instance();
    void test_addFile();
    void test_removeFile();
    void test_getFiles();
    void test_clear();
    void test_contains();
    void test_maxFiles();
    void test_category();
};

void TestRecentFilesManager::test_instance()
{
    RecentFilesManager* rfm = RecentFilesManager::instance();
    QVERIFY(rfm != nullptr);
    QCOMPARE(RecentFilesManager::instance(), rfm);
}

void TestRecentFilesManager::test_addFile()
{
    RecentFilesManager* rfm = RecentFilesManager::instance();
    rfm->clear();

    rfm->addFile("/path/to/file.txt", "documents");
    QVERIFY(rfm->contains("/path/to/file.txt"));
    auto files = rfm->getFiles("documents");
    QCOMPARE(files.size(), 1);
    QCOMPARE(files[0].name, QString("file.txt"));
}

void TestRecentFilesManager::test_removeFile()
{
    RecentFilesManager* rfm = RecentFilesManager::instance();
    rfm->clear();

    rfm->addFile("/path/to/remove.txt");
    QVERIFY(rfm->contains("/path/to/remove.txt"));
    rfm->removeFile("/path/to/remove.txt");
    QVERIFY(!rfm->contains("/path/to/remove.txt"));
}

void TestRecentFilesManager::test_getFiles()
{
    RecentFilesManager* rfm = RecentFilesManager::instance();
    rfm->clear();

    rfm->addFile("/a.txt");
    rfm->addFile("/b.txt");
    auto all = rfm->getFiles();
    QCOMPARE(all.size(), 2);
}

void TestRecentFilesManager::test_clear()
{
    RecentFilesManager* rfm = RecentFilesManager::instance();
    rfm->clear();

    rfm->addFile("/clear_test.txt");
    QCOMPARE(rfm->getFiles().size(), 1);
    rfm->clear();
    QCOMPARE(rfm->getFiles().size(), 0);
}

void TestRecentFilesManager::test_contains()
{
    RecentFilesManager* rfm = RecentFilesManager::instance();
    rfm->clear();

    QVERIFY(!rfm->contains("/not_added.txt"));
    rfm->addFile("/added.txt");
    QVERIFY(rfm->contains("/added.txt"));
}

void TestRecentFilesManager::test_maxFiles()
{
    RecentFilesManager* rfm = RecentFilesManager::instance();
    rfm->clear();

    int original = rfm->getMaxFiles();
    rfm->setMaxFiles(5);
    QCOMPARE(rfm->getMaxFiles(), 5);
    rfm->setMaxFiles(original);
}

void TestRecentFilesManager::test_category()
{
    RecentFilesManager* rfm = RecentFilesManager::instance();
    rfm->clear();

    rfm->addFile("/cat_a.txt", "category_a");
    rfm->addFile("/cat_b.txt", "category_b");

    auto catA = rfm->getFiles("category_a");
    QCOMPARE(catA.size(), 1);
    QCOMPARE(catA[0].category, QString("category_a"));

    auto catB = rfm->getFiles("category_b");
    QCOMPARE(catB.size(), 1);

    auto catNone = rfm->getFiles("nonexistent");
    QVERIFY(catNone.isEmpty());
}

QTEST_MAIN(TestRecentFilesManager)
#include "test_RecentFilesManager.moc"
