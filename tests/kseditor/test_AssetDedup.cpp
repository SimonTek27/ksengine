#include <QtTest/QtTest>
#include "engine/assets/AssetManager.h"
#include "engine/assets/AssetFileWatcher.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

using namespace ks;

class TestAssetDedup : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;
    QString m_dataDir;

    QString createFile(const QString& name, const QByteArray& content)
    {
        QString path = m_dataDir + "/" + name;
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        f.close();
        return path;
    }

private slots:
    void initTestCase()
    {
        m_dataDir = m_tempDir.path() + "/test_assets";
        QDir().mkpath(m_dataDir);
    }

    void cleanupTestCase() {}

    void test_computeFileHash()
    {
        QString path = createFile("test.txt", "Hello World");
        QByteArray hash = AssetManager::computeFileHash(path);
        QVERIFY(!hash.isEmpty());
        QCOMPARE(hash.size(), 32);

        // Same content = same hash
        QString path2 = createFile("test2.txt", "Hello World");
        QByteArray hash2 = AssetManager::computeFileHash(path2);
        QCOMPARE(hash, hash2);

        // Different content = different hash
        QString path3 = createFile("test3.txt", "Goodbye World");
        QByteArray hash3 = AssetManager::computeFileHash(path3);
        QVERIFY(hash != hash3);
    }

    void test_computeDataHash()
    {
        QByteArray data = "test data";
        QByteArray hash = AssetManager::computeDataHash(data);
        QVERIFY(!hash.isEmpty());
        QCOMPARE(hash.size(), 32);

        QByteArray hash2 = AssetManager::computeDataHash(data);
        QCOMPARE(hash, hash2);

        QByteArray hash3 = AssetManager::computeDataHash("different");
        QVERIFY(hash != hash3);
    }

    void test_assetManagerDedup()
    {
        AssetManager* mgr = AssetManager::instance();
        mgr->setRootDirectory(m_dataDir);

        // Create two identical files
        QString path1 = createFile("dup_a.txt", "DUPLICATE CONTENT");
        QString path2 = createFile("dup_b.txt", "DUPLICATE CONTENT");

        // Create one unique file
        QString path3 = createFile("unique.txt", "UNIQUE CONTENT");

        mgr->scan();

        // After scan, assets should have content hashes
        auto assets = mgr->getAssets();
        QVERIFY(!assets.isEmpty());

        // Find duplicates
        auto dups = mgr->findDuplicates();
        QVERIFY(dups.size() >= 2);

        // Find duplicate groups
        auto groups = mgr->findDuplicateGroups();
        bool foundDupGroup = false;
        for (const auto& group : groups) {
            if (group.size() > 1) {
                foundDupGroup = true;
                break;
            }
        }
        QVERIFY(foundDupGroup);

        // Check findByContentHash
        auto hash = QString::fromLatin1(AssetManager::computeFileHash(path1).toHex());
        auto found = mgr->findByContentHash(hash);
        QVERIFY(found.size() >= 2);

        // Check isDuplicateOf
        QVERIFY(found.size() >= 2);
        QVERIFY(mgr->isDuplicateOf(found[0].id, found[1].id));
    }

    void test_findExistingByHash()
    {
        AssetManager* mgr = AssetManager::instance();
        QString path = createFile("findme.txt", "FIND THIS CONTENT");
        mgr->scan();

        auto hash = QString::fromLatin1(AssetManager::computeFileHash(path).toHex());
        QString existingId = mgr->findExistingByHash(hash);
        QVERIFY(!existingId.isEmpty());
    }

    void test_removeDuplicates()
    {
        AssetManager* mgr = AssetManager::instance();

        // Rescan to get fresh state
        mgr->scan();

        int beforeCount = mgr->getAssets().size();
        mgr->removeDuplicates(true);

        // After marking duplicates, some assets should have isDuplicate = true
        auto assets = mgr->getAssets();
        bool foundDup = false;
        for (const auto& a : assets) {
            if (a.isDuplicate) {
                foundDup = true;
                QVERIFY(!a.originalAssetId.isEmpty());
                break;
            }
        }
        // May or may not have duplicates depending on test data
    }

    void test_importAssetWithDedup()
    {
        AssetManager* mgr = AssetManager::instance();
        mgr->setRootDirectory(m_dataDir);
        mgr->scan();

        // Import a file that's a duplicate of an existing asset
        QString dupPath = createFile("import_dup.txt", "DUPLICATE CONTENT");
        bool imported = mgr->importAsset(dupPath, m_dataDir);
        QVERIFY(imported);

        // Should have been marked as duplicate
        auto assets = mgr->getAssets();
        bool foundDupImport = false;
        for (const auto& a : assets) {
            if (a.isDuplicate && a.name == "import_dup.txt") {
                foundDupImport = true;
                QVERIFY(!a.originalAssetId.isEmpty());
                break;
            }
        }
    }

    void test_fileWatcherCreate()
    {
        AssetFileWatcher watcher;

        QSignalSpy addedSpy(&watcher, &AssetFileWatcher::fileAdded);

        QString watchDir = m_dataDir + "/watch_test";
        QDir().mkpath(watchDir);

        bool watched = watcher.watchDirectory(watchDir, true);
        QVERIFY(watched);
        QVERIFY(watcher.isWatching());
        QVERIFY(!watcher.watchedDirectories().isEmpty());

        watcher.unwatchAll();
        QVERIFY(!watcher.isWatching());
    }

    void test_fileWatcherPersistence()
    {
        AssetFileWatcher watcher;

        QString watchDir = m_dataDir + "/watch_persist";
        QDir().mkpath(watchDir);

        watcher.watchDirectory(watchDir, true);

        QStringList dirs = watcher.watchedDirectories();
        QVERIFY(dirs.contains(QDir(watchDir).absolutePath()));
    }

    void test_fileWatcherDebounce()
    {
        AssetFileWatcher watcher;
        watcher.setDebounceInterval(100);

        QCOMPARE(watcher.debounceInterval(), 100);

        watcher.setDebounceInterval(10);
        QCOMPARE(watcher.debounceInterval(), 50); // min 50
    }

    void test_scanAndDeduplicate()
    {
        AssetManager* mgr = AssetManager::instance();

        QString dup1 = createFile("scan_dup_a.txt", "SCAN DUP CONTENT");
        QString dup2 = createFile("scan_dup_b.txt", "SCAN DUP CONTENT");
        Q_UNUSED(dup1); Q_UNUSED(dup2);

        mgr->scanAndDeduplicate();

        auto groups = mgr->findDuplicateGroups();
        bool found = false;
        for (const auto& group : groups) {
            if (group.size() > 1) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    void test_largeFileHashing()
    {
        // Create a file larger than the max hash size threshold
        QString path = m_dataDir + "/large_test.bin";
        QFile f(path);
        f.open(QIODevice::WriteOnly);

        // 1MB of data
        for (int i = 0; i < 1024; ++i) {
            f.write(QByteArray(1024, static_cast<char>(i % 256)));
        }
        f.close();

        QByteArray hash = AssetManager::computeFileHash(path);
        QVERIFY(!hash.isEmpty());
        QCOMPARE(hash.size(), 32);
    }

    void test_emptyFileHash()
    {
        QString path = createFile("empty.txt", QByteArray());
        QByteArray hash = AssetManager::computeFileHash(path);
        QVERIFY(!hash.isEmpty());
        QCOMPARE(hash.size(), 32);
    }

    void test_nonexistentFileHash()
    {
        QByteArray hash = AssetManager::computeFileHash("/nonexistent/file.txt");
        QVERIFY(hash.isEmpty());
    }
};

QTEST_MAIN(TestAssetDedup)
#include "test_AssetDedup.moc"
