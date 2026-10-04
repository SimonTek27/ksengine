#include "engine/Tools/CacheManager.h"
#include <QtTest/QtTest>
#include <QTemporaryDir>

using namespace ks;

class TestCacheManager : public QObject {
    Q_OBJECT

private slots:
    void test_instance();
    void test_setGet();
    void test_has();
    void test_remove();
    void test_clear();
    void test_evictExpired();
    void test_cacheKey();
    void test_cachePath();
    void test_memoryCache();
    void test_diskCache();
};

void TestCacheManager::test_instance()
{
    CacheManager* cm = CacheManager::instance();
    QVERIFY(cm != nullptr);
    QCOMPARE(CacheManager::instance(), cm);
}

void TestCacheManager::test_setGet()
{
    CacheManager* cm = CacheManager::instance();
    cm->clear();

    cm->set("key1", QVariant(42));
    QCOMPARE(cm->get("key1"), QVariant(42));

    cm->set("key2", QVariant("hello"));
    QCOMPARE(cm->get("key2"), QVariant("hello"));
}

void TestCacheManager::test_has()
{
    CacheManager* cm = CacheManager::instance();
    cm->clear();

    QVERIFY(!cm->has("nonexistent"));
    cm->set("exists", QVariant(true));
    QVERIFY(cm->has("exists"));
}

void TestCacheManager::test_remove()
{
    CacheManager* cm = CacheManager::instance();
    cm->clear();

    cm->set("remove_me", QVariant("data"));
    QVERIFY(cm->has("remove_me"));
    cm->remove("remove_me");
    QVERIFY(!cm->has("remove_me"));
}

void TestCacheManager::test_clear()
{
    CacheManager* cm = CacheManager::instance();
    cm->set("a", QVariant(1));
    cm->set("b", QVariant(2));
    cm->clear();
    QVERIFY(!cm->has("a"));
    QVERIFY(!cm->has("b"));
    QCOMPARE(cm->entryCount(), 0);
}

void TestCacheManager::test_evictExpired()
{
    CacheManager* cm = CacheManager::instance();
    cm->clear();

    cm->set("permanent", QVariant("forever"), 0);
    cm->set("expired", QVariant("gone"), -1);
    cm->evictExpired();
    QVERIFY(cm->has("permanent"));
}

void TestCacheManager::test_cacheKey()
{
    CacheManager* cm = CacheManager::instance();
    QString key = cm->cacheKey("test_source");
    QVERIFY(!key.isEmpty());
    QCOMPARE(key, cm->cacheKey("test_source"));
}

void TestCacheManager::test_cachePath()
{
    CacheManager* cm = CacheManager::instance();
    QString key = cm->cacheKey("path_test");
    QString path = cm->cachePath(key, "bin");
    QVERIFY(path.endsWith(".bin"));
    QVERIFY(path.contains(key));
}

void TestCacheManager::test_memoryCache()
{
    MemoryCache mc("test_cache");
    QCOMPARE(mc.getId(), QString("test_cache"));

    mc.put("item1", QVariant(100));
    QVERIFY(mc.contains("item1"));
    QCOMPARE(mc.get("item1"), QVariant(100));

    mc.remove("item1");
    QVERIFY(!mc.contains("item1"));

    mc.put("a", QVariant(1));
    mc.put("b", QVariant(2));
    mc.clear();
    QVERIFY(!mc.contains("a"));
}

void TestCacheManager::test_diskCache()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    DiskCache dc("disk_test");
    dc.setDirectory(tmpDir.path());
    dc.setCompression(false);

    dc.put("file1", QByteArray("disk data"));
    QByteArray retrieved;
    QVERIFY(dc.get("file1", retrieved));
    QCOMPARE(retrieved, QByteArray("disk data"));

    dc.remove("file1");
    QVERIFY(!dc.get("file1", retrieved));

    dc.put("x", QByteArray("1"));
    dc.put("y", QByteArray("2"));
    dc.clear();
    QVERIFY(!dc.get("x", retrieved));
}

QTEST_MAIN(TestCacheManager)
#include "test_CacheManager.moc"
