#include <QtTest/QtTest>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>

#include "sdk/kseditor/modules/modmanager/ModCollection.h"

using namespace ks;

// ============================================================================
// Test CollectionManager — standalone, no EditorModule dependency
// ============================================================================

class TestModManagerFeatures : public QObject {
    Q_OBJECT

private:
    QTemporaryDir* m_tempDir = nullptr;
    CollectionManager* m_mgr = nullptr;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testDefaultCollection();
    void testCreateCollection();
    void testCreateDuplicateNames();
    void testDeleteCollection();
    void testDeleteDefaultFails();
    void testRenameCollection();
    void testRenameEmptyFails();
    void testDuplicateCollection();
    void testDuplicateWithoutName();
    void testAddModToCollection();
    void testAddDuplicateMod();
    void testRemoveModFromCollection();
    void testRemoveNonExistentMod();
    void testSetCollectionMods();
    void testFindCollectionsForMod();
    void testCollectionsForModsQuery();
    void testSaveLoadCollections();
    void testLoadFromInvalidFile();
    void testMultipleCollectionsIsolation();
};

void TestModManagerFeatures::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
    m_mgr = new CollectionManager(this);
    m_mgr->loadCollections(m_tempDir->path() + "/collections.json");
}

void TestModManagerFeatures::cleanupTestCase()
{
    delete m_tempDir;
}

void TestModManagerFeatures::testDefaultCollection()
{
    ModCollection def = m_mgr->getCollection("default");
    QVERIFY(def.isDefault);
    QCOMPARE(def.name, QString("All Mods"));
    QVERIFY(def.modNames.isEmpty());
}

void TestModManagerFeatures::testCreateCollection()
{
    QVERIFY(m_mgr->createCollection("Favorites", "My top mods", "#ff6600"));
    QVERIFY(m_mgr->createCollection("Racing", "Racing/competition mods", "#00cc00"));
    QVERIFY(m_mgr->createCollection("Visual Only", "Graphics/visual mods"));

    QStringList ids = m_mgr->listCollections();
    QVERIFY(ids.size() >= 4); // default + 3 new

    int found = 0;
    for (const QString& id : ids) {
        ModCollection c = m_mgr->getCollection(id);
        if (c.name == "Favorites") found++;
        if (c.name == "Racing") found++;
        if (c.name == "Visual Only") found++;
    }
    QCOMPARE(found, 3);
}

void TestModManagerFeatures::testCreateDuplicateNames()
{
    // Creating with same name is allowed (internal IDs differ)
    QVERIFY(m_mgr->createCollection("Favorites", "Duplicate name test"));
    int count = 0;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "Favorites") count++;
    }
    QVERIFY(count >= 2);

    // Empty name should fail
    QVERIFY(!m_mgr->createCollection("  "));
}

void TestModManagerFeatures::testDeleteCollection()
{
    // Find a non-default collection
    QString targetId;
    for (const QString& id : m_mgr->listCollections()) {
        ModCollection c = m_mgr->getCollection(id);
        if (c.name == "Visual Only") { targetId = id; break; }
    }
    QVERIFY(!targetId.isEmpty());

    QVERIFY(m_mgr->deleteCollection(targetId));

    ModCollection gone = m_mgr->getCollection(targetId);
    QVERIFY(gone.id.isEmpty()); // Verify fully removed
}

void TestModManagerFeatures::testDeleteDefaultFails()
{
    QVERIFY(!m_mgr->deleteCollection("default"));
    ModCollection def = m_mgr->getCollection("default");
    QVERIFY(def.isDefault);
}

void TestModManagerFeatures::testRenameCollection()
{
    QString targetId;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "Racing") { targetId = id; break; }
    }
    QVERIFY(!targetId.isEmpty());

    QVERIFY(m_mgr->renameCollection(targetId, "Competizione"));

    ModCollection renamed = m_mgr->getCollection(targetId);
    QCOMPARE(renamed.name, QString("Competizione"));
}

void TestModManagerFeatures::testRenameEmptyFails()
{
    QString targetId;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "Favorites") { targetId = id; break; }
    }
    QVERIFY(!targetId.isEmpty());
    QVERIFY(!m_mgr->renameCollection(targetId, "   "));
}

void TestModManagerFeatures::testDuplicateCollection()
{
    QString targetId;
    for (const QString& id : m_mgr->listCollections()) {
        ModCollection c = m_mgr->getCollection(id);
        if (c.name == "Favorites") { targetId = id; break; }
    }
    QVERIFY(!targetId.isEmpty());

    // Add mods before duplicating
    m_mgr->addModToCollection(targetId, "CarPack1");
    m_mgr->addModToCollection(targetId, "TrackPack1");

    QVERIFY(m_mgr->duplicateCollection(targetId, "Favorites Backup"));

    bool foundBackup = false;
    for (const QString& id : m_mgr->listCollections()) {
        ModCollection c = m_mgr->getCollection(id);
        if (c.name == "Favorites Backup") {
            foundBackup = true;
            QCOMPARE(c.modNames.size(), 2);
            QVERIFY(c.modNames.contains("CarPack1"));
            QVERIFY(!c.isDefault);
        }
    }
    QVERIFY(foundBackup);
}

void TestModManagerFeatures::testDuplicateWithoutName()
{
    QString targetId;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "Favorites") { targetId = id; break; }
    }
    QVERIFY(!targetId.isEmpty());

    // Duplicate with empty name should auto-generate
    QVERIFY(m_mgr->duplicateCollection(targetId, ""));
    bool foundAuto = false;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name.contains("Copy")) foundAuto = true;
    }
    QVERIFY(foundAuto);
}

void TestModManagerFeatures::testAddModToCollection()
{
    QString targetId;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "Competizione") { targetId = id; break; }
    }
    // Use first non-default, non-Favorites collection if not found
    if (targetId.isEmpty()) {
        for (const QString& id : m_mgr->listCollections()) {
            ModCollection c = m_mgr->getCollection(id);
            if (!c.isDefault && c.name != "Favorites") { targetId = id; break; }
        }
    }
    QVERIFY(!targetId.isEmpty());

    QVERIFY(m_mgr->addModToCollection(targetId, "RacingMod1"));
    QVERIFY(m_mgr->addModToCollection(targetId, "RacingMod2"));

    QStringList mods = m_mgr->getModsForCollection(targetId);
    QCOMPARE(mods.size(), 2);
    QVERIFY(mods.contains("RacingMod1"));
    QVERIFY(mods.contains("RacingMod2"));

    // Invalid collection ID
    QVERIFY(!m_mgr->addModToCollection("bogus", "ModX"));
}

void TestModManagerFeatures::testAddDuplicateMod()
{
    QString targetId;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "Favorites") { targetId = id; break; }
    }
    QVERIFY(!targetId.isEmpty());

    // Add mod
    QVERIFY(m_mgr->addModToCollection(targetId, "UniqueMod"));

    // Add same mod again — should succeed (idempotent, no duplicate entry)
    QVERIFY(m_mgr->addModToCollection(targetId, "UniqueMod"));

    QStringList mods = m_mgr->getModsForCollection(targetId);
    int count = 0;
    for (const QString& m : mods) { if (m == "UniqueMod") count++; }
    QCOMPARE(count, 1); // No duplicates
}

void TestModManagerFeatures::testRemoveModFromCollection()
{
    QString targetId;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "Favorites") { targetId = id; break; }
    }
    QVERIFY(!targetId.isEmpty());

    // Remove existing mod
    QVERIFY(m_mgr->removeModFromCollection(targetId, "CarPack1"));

    QStringList mods = m_mgr->getModsForCollection(targetId);
    QVERIFY(!mods.contains("CarPack1"));
    QVERIFY(mods.contains("TrackPack1"));

    // Remove non-existent returns false
    QVERIFY(!m_mgr->removeModFromCollection(targetId, "NonExistent"));

    // Invalid collection
    QVERIFY(!m_mgr->removeModFromCollection("invalid", "ModB"));
}

void TestModManagerFeatures::testRemoveNonExistentMod()
{
    QString targetId;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "Favorites") { targetId = id; break; }
    }
    QVERIFY(!targetId.isEmpty());

    QVERIFY(!m_mgr->removeModFromCollection(targetId, "GhostMod_DoesNotExist"));
}

void TestModManagerFeatures::testSetCollectionMods()
{
    // Create a fresh collection for this test
    QVERIFY(m_mgr->createCollection("TestSet", "For setCollectionMods test", "#ff00ff"));

    QString targetId;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "TestSet") { targetId = id; break; }
    }
    QVERIFY(!targetId.isEmpty());

    QStringList newMods = {"Alpha", "Beta", "Gamma"};
    QVERIFY(m_mgr->setCollectionMods(targetId, newMods));

    QStringList mods = m_mgr->getModsForCollection(targetId);
    QCOMPARE(mods.size(), 3);
    QVERIFY(mods.contains("Alpha"));
    QVERIFY(mods.contains("Gamma"));

    // Replace with different set
    QStringList replacement = {"Delta", "Epsilon"};
    QVERIFY(m_mgr->setCollectionMods(targetId, replacement));
    mods = m_mgr->getModsForCollection(targetId);
    QCOMPARE(mods.size(), 2);
    QVERIFY(mods.contains("Delta"));

    // Verify other collections unchanged
    QString favId;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "Favorites") { favId = id; break; }
    }
    QStringList favMods = m_mgr->getModsForCollection(favId);
    QVERIFY(favMods.contains("TrackPack1"));

    // Invalid collection
    QVERIFY(!m_mgr->setCollectionMods("bogus", {"X"}));
}

void TestModManagerFeatures::testFindCollectionsForMod()
{
    // Make sure "UniqueMod" is in Favorites
    QString favId;
    for (const QString& id : m_mgr->listCollections()) {
        if (m_mgr->getCollection(id).name == "Favorites") { favId = id; break; }
    }
    m_mgr->addModToCollection(favId, "UniqueMod");

    QStringList cols = m_mgr->findCollectionsForMod("UniqueMod");
    QVERIFY(!cols.isEmpty());
    bool found = false;
    for (const QString& id : cols) {
        if (m_mgr->getCollection(id).name == "Favorites") found = true;
    }
    QVERIFY(found);

    // Non-existent mod
    QVERIFY(m_mgr->findCollectionsForMod("NoWhereToBeFound").isEmpty());
}

void TestModManagerFeatures::testCollectionsForModsQuery()
{
    QVector<ModCollection> result = m_mgr->collectionsForMods({"UniqueMod", "Alpha"});
    QVERIFY(result.size() >= 1);

    // Empty query
    QVERIFY(m_mgr->collectionsForMods({}).isEmpty());

    // No matches
    QVERIFY(m_mgr->collectionsForMods({"ZZZZ_GHOST_999"}).isEmpty());
}

void TestModManagerFeatures::testSaveLoadCollections()
{
    QString savePath = m_tempDir->path() + "/collections_save_test.json";

    QVERIFY(m_mgr->saveCollections(savePath));
    QVERIFY(QFile::exists(savePath));

    // Verify JSON structure
    QFile file(savePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QVERIFY(doc.isObject());
    QVERIFY(doc.object().contains("collections"));
    QVERIFY(doc.object()["collections"].toArray().size() >= 3);

    // Load into a fresh manager
    CollectionManager fresh(this);
    QVERIFY(fresh.loadCollections(savePath));
    QVERIFY(fresh.listCollections().size() >= 3);

    // Verify specific collection data survives round-trip.
    // The colored "Favorites" collection is unique by color; check its round-trip.
    bool foundColored = false;
    for (const QString& id : fresh.listCollections()) {
        ModCollection c = fresh.getCollection(id);
        if (c.name == "Favorites" && c.color == "#ff6600") {
            QCOMPARE(c.color, QString("#ff6600"));
            foundColored = true;
            break;
        }
    }
    QVERIFY(foundColored);

    // A "Favorites" collection should contain "UniqueMod" after the round-trip.
    bool foundUnique = false;
    for (const QString& id : fresh.listCollections()) {
        ModCollection c = fresh.getCollection(id);
        if (c.name == "Favorites" && c.modNames.contains("UniqueMod")) {
            foundUnique = true;
            break;
        }
    }
    QVERIFY(foundUnique);

    // Test saving with empty path uses default location
    QVERIFY(m_mgr->saveCollections());
}

void TestModManagerFeatures::testLoadFromInvalidFile()
{
    // Non-existent file — should silently handle and create default
    CollectionManager fresh(this);
    QVERIFY(fresh.loadCollections("/nonexistent/path/collections.json"));

    // Default should still exist
    ModCollection def = fresh.getCollection("default");
    QVERIFY(def.isDefault);
}

void TestModManagerFeatures::testMultipleCollectionsIsolation()
{
    QString colA, colB;
    QVERIFY(m_mgr->createCollection("IsolationA"));
    QVERIFY(m_mgr->createCollection("IsolationB"));

    for (const QString& id : m_mgr->listCollections()) {
        QString n = m_mgr->getCollection(id).name;
        if (n == "IsolationA") colA = id;
        if (n == "IsolationB") colB = id;
    }
    QVERIFY(!colA.isEmpty() && !colB.isEmpty());

    m_mgr->addModToCollection(colA, "SharedMod");
    m_mgr->addModToCollection(colB, "SharedMod");

    QVERIFY(m_mgr->getModsForCollection(colA).contains("SharedMod"));
    QVERIFY(m_mgr->getModsForCollection(colB).contains("SharedMod"));

    // Removing from one doesn't affect the other
    m_mgr->removeModFromCollection(colA, "SharedMod");
    QVERIFY(!m_mgr->getModsForCollection(colA).contains("SharedMod"));
    QVERIFY(m_mgr->getModsForCollection(colB).contains("SharedMod"));
}

QTEST_MAIN(TestModManagerFeatures)
#include "test_ModManagerFeatures.moc"
