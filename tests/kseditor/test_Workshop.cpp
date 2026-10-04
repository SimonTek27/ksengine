#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include "WorkshopItem.h"
#include "WorkshopManager.h"

using namespace ks;

class TestWorkshop : public QObject {
    Q_OBJECT

private:
    QTemporaryDir* m_tempDir = nullptr;
    int m_testCounter = 0;

    // Each test gets a unique subdirectory so database state never leaks
    QString uniqueTestDir() {
        return m_tempDir->path() + "/test_" + QString::number(++m_testCounter);
    }

private slots:
    void initTestCase() {
        m_tempDir = new QTemporaryDir();
        QVERIFY(m_tempDir->isValid());
    }

    void init() {
        WorkshopManager::destroyInstance();
        WorkshopManager* mgr = WorkshopManager::instance();
        mgr->reset();
        QVERIFY(mgr->getPublishedItems().isEmpty());
        mgr->setDataDir(uniqueTestDir());
        QVERIFY(mgr->getPublishedItems().isEmpty());
    }

    void cleanup() {
        WorkshopManager::destroyInstance();
    }

    void cleanupTestCase() {
        WorkshopManager::destroyInstance();
        delete m_tempDir;
    }

    void test_WorkshopItem_Serialization() {
        WorkshopItem item;
        item.id = WorkshopItem::generateId();
        item.name = "Test Car";
        item.version = "1.2.3";
        item.author = "TestAuthor";
        item.description = "A test car mod";
        item.category = "cars";
        item.tags = {"racing", "gt3", "v8"};
        item.license = "MIT";
        item.website = "https://example.com";
        item.fileSize = 1024;
        item.rating = 4.5f;
        item.ratingCount = 10;
        item.downloadCount = 100;
        item.isInstalled = true;
        item.dependencies = {"tracks (>= 1.0)", "shared_data"};
        item.conflicts = {"other_car_pack"};

        QJsonObject json = item.toJson();
        QVERIFY(!json.isEmpty());
        QCOMPARE(json["name"].toString(), QString("Test Car"));
        QCOMPARE(json["version"].toString(), QString("1.2.3"));
        QCOMPARE(json["author"].toString(), QString("TestAuthor"));
        QCOMPARE(json["category"].toString(), QString("cars"));
        QCOMPARE(json["license"].toString(), QString("MIT"));
        QCOMPARE(static_cast<float>(json["rating"].toDouble()), 4.5f);
        QCOMPARE(json["rating_count"].toInt(), 10);
        QCOMPARE(json["download_count"].toInt(), 100);
        QCOMPARE(json["installed"].toBool(), true);
        QCOMPARE(json["dependencies"].toArray().size(), 2);
        QCOMPARE(json["conflicts"].toArray().size(), 1);
        QCOMPARE(json["tags"].toArray().size(), 3);

        WorkshopItem restored = WorkshopItem::fromJson(json);
        QCOMPARE(restored.id, item.id);
        QCOMPARE(restored.name, item.name);
        QCOMPARE(restored.version, item.version);
        QCOMPARE(restored.author, item.author);
        QCOMPARE(restored.description, item.description);
        QCOMPARE(restored.category, item.category);
        QCOMPARE(restored.license, item.license);
        QCOMPARE(restored.website, item.website);
        QCOMPARE(restored.fileSize, item.fileSize);
        QCOMPARE(restored.rating, item.rating);
        QCOMPARE(restored.ratingCount, item.ratingCount);
        QCOMPARE(restored.downloadCount, item.downloadCount);
        QCOMPARE(restored.isInstalled, item.isInstalled);
        QCOMPARE(restored.tags.size(), 3);
        QCOMPARE(restored.dependencies.size(), 2);
        QCOMPARE(restored.conflicts.size(), 1);
    }

    void test_WorkshopItem_GenerateId() {
        QString id1 = WorkshopItem::generateId();
        QString id2 = WorkshopItem::generateId();
        QVERIFY(!id1.isEmpty());
        QVERIFY(!id2.isEmpty());
        QVERIFY(id1 != id2);
    }

    void test_WorkshopItem_StandardCategories() {
        QStringList cats = WorkshopItem::standardCategories();
        QVERIFY(cats.contains("cars"));
        QVERIFY(cats.contains("tracks"));
        QVERIFY(cats.contains("skins"));
        QVERIFY(cats.contains("apps"));
        QVERIFY(cats.contains("weather"));
    }

    void test_WorkshopManager_PublishAndRetrieve() {
        WorkshopManager* mgr = WorkshopManager::instance();

        WorkshopItem item;
        item.name = "Published Mod";
        item.version = "1.0";
        item.author = "Author";
        item.description = "Description";
        item.category = "tracks";
        item.tags = {"racing", "f1"};

        bool ok = mgr->publishItem(item, "");
        QVERIFY(ok);

        auto allItems = mgr->getPublishedItems();
        QCOMPARE(allItems.size(), 1);
        QCOMPARE(allItems[0].name, QString("Published Mod"));
        QCOMPARE(allItems[0].category, QString("tracks"));
        QCOMPARE(allItems[0].tags.size(), 2);
        QVERIFY(!allItems[0].id.isEmpty());
        QVERIFY(allItems[0].createdAt.isValid());
    }

    void test_WorkshopManager_BrowseCategories() {
        WorkshopManager* mgr = WorkshopManager::instance();

        WorkshopItem car;
        car.name = "Ferrari F2004";
        car.category = "cars";
        car.author = "Ferrari";
        mgr->publishItem(car, "");

        WorkshopItem track;
        track.name = "Monza";
        track.category = "tracks";
        track.author = "AC Team";
        mgr->publishItem(track, "");

        WorkshopItem skin;
        skin.name = "Red Bull Skin";
        skin.category = "skins";
        skin.author = "Designer";
        mgr->publishItem(skin, "");

        WorkshopManager::BrowseQuery query;
        query.category = "cars";
        auto carResults = mgr->browse(query);
        QCOMPARE(carResults.size(), 1);
        QCOMPARE(carResults[0].name, QString("Ferrari F2004"));

        query.category = "tracks";
        auto trackResults = mgr->browse(query);
        QCOMPARE(trackResults.size(), 1);
        QCOMPARE(trackResults[0].name, QString("Monza"));

        query.category = "apps";
        auto appResults = mgr->browse(query);
        QCOMPARE(appResults.size(), 0);

        auto allResults = mgr->browse();
        QCOMPARE(allResults.size(), 3);
    }

    void test_WorkshopManager_Search() {
        WorkshopManager* mgr = WorkshopManager::instance();

        WorkshopItem car;
        car.name = "Porsche 911 GT3";
        car.description = "A fast German sports car";
        car.category = "cars";
        car.author = "Porsche Fan";
        mgr->publishItem(car, "");

        WorkshopItem track;
        track.name = "Nürburgring";
        track.description = "The Green Hell";
        track.category = "tracks";
        track.author = "AC Team";
        mgr->publishItem(track, "");

        WorkshopItem app;
        app.name = "Heli Corsa";
        app.description = "Track map helper app";
        app.category = "apps";
        app.author = "Helper Dev";
        mgr->publishItem(app, "");

        auto results = mgr->search("Porsche");
        QCOMPARE(results.size(), 1);

        results = mgr->search("porsche");
        QCOMPARE(results.size(), 1);

        results = mgr->search("Green");
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].name, QString("Nürburgring"));

        results = mgr->search("AC Team");
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].name, QString("Nürburgring"));

        results = mgr->search("");
        QCOMPARE(results.size(), 3);

        results = mgr->search("Nonexistent");
        QCOMPARE(results.size(), 0);
    }

    void test_WorkshopManager_Rating() {
        WorkshopManager* mgr = WorkshopManager::instance();

        WorkshopItem item;
        item.name = "Rated Mod";
        item.category = "cars";
        mgr->publishItem(item, "");

        auto items = mgr->getPublishedItems();
        QString id = items[0].id;

        QCOMPARE(items[0].rating, 0.0f);
        QCOMPARE(items[0].ratingCount, 0);

        bool ok = mgr->rateItem(id, 5);
        QVERIFY(ok);

        ok = mgr->rateItem(id, 3);
        QVERIFY(ok);

        items = mgr->getPublishedItems();
        QCOMPARE(items[0].ratingCount, 2);
        QCOMPARE(items[0].rating, 4.0f);

        ok = mgr->rateItem(id, 0);
        QVERIFY(!ok);

        ok = mgr->rateItem(id, 6);
        QVERIFY(!ok);

        ok = mgr->rateItem("nonexistent", 4);
        QVERIFY(!ok);
    }

    void test_WorkshopManager_Remove() {
        WorkshopManager* mgr = WorkshopManager::instance();

        WorkshopItem item;
        item.name = "Remove Me";
        item.category = "cars";
        mgr->publishItem(item, "");

        QCOMPARE(mgr->getPublishedItems().size(), 1);

        auto items = mgr->getPublishedItems();
        QString id = items[0].id;

        bool ok = mgr->removeItem(id);
        QVERIFY(ok);
        QCOMPARE(mgr->getPublishedItems().size(), 0);

        ok = mgr->removeItem(id);
        QVERIFY(!ok);
    }

    void test_WorkshopManager_InstallState() {
        WorkshopManager* mgr = WorkshopManager::instance();

        WorkshopItem item;
        item.name = "Installed Mod";
        item.category = "cars";
        mgr->publishItem(item, "");

        auto items = mgr->getPublishedItems();
        QString id = items[0].id;
        QVERIFY(!items[0].isInstalled);
        QCOMPARE(items[0].downloadCount, 0);

        mgr->setInstalled(id, true);
        items = mgr->getPublishedItems();
        QVERIFY(items[0].isInstalled);
        QCOMPARE(items[0].downloadCount, 1);

        auto installed = mgr->getInstalledItems();
        QCOMPARE(installed.size(), 1);
        QCOMPARE(installed[0].name, QString("Installed Mod"));

        mgr->setInstalled(id, false);
        items = mgr->getPublishedItems();
        QVERIFY(!items[0].isInstalled);

        installed = mgr->getInstalledItems();
        QCOMPARE(installed.size(), 0);
    }

    void test_WorkshopManager_DatabasePersistence() {
        WorkshopManager* mgr = WorkshopManager::instance();
        QString persistDir = uniqueTestDir();

        mgr->setDataDir(persistDir);

        WorkshopItem item1;
        item1.name = "Persist Car";
        item1.category = "cars";
        item1.author = "Author1";
        mgr->publishItem(item1, "");

        WorkshopItem item2;
        item2.name = "Persist Track";
        item2.category = "tracks";
        item2.author = "Author2";
        mgr->publishItem(item2, "");

        QCOMPARE(mgr->getPublishedItems().size(), 2);

        // Simulate closing and reopening: destroy and recreate
        WorkshopManager::destroyInstance();
        WorkshopManager* mgr2 = WorkshopManager::instance();
        mgr2->setDataDir(persistDir);

        auto items = mgr2->getPublishedItems();
        QCOMPARE(items.size(), 2);

        bool hasCar = false, hasTrack = false;
        for (const auto& item : items) {
            if (item.name == "Persist Car") hasCar = true;
            if (item.name == "Persist Track") hasTrack = true;
        }
        QVERIFY(hasCar);
        QVERIFY(hasTrack);
    }

    void test_WorkshopManager_GetItem() {
        WorkshopManager* mgr = WorkshopManager::instance();

        WorkshopItem item;
        item.name = "Specific Item";
        item.category = "cars";
        mgr->publishItem(item, "");

        auto items = mgr->getPublishedItems();
        QString id = items[0].id;

        auto retrieved = mgr->getItem(id);
        QCOMPARE(retrieved.name, QString("Specific Item"));

        auto missing = mgr->getItem("nonexistent");
        QVERIFY(missing.id.isEmpty());
    }

    void test_WorkshopManager_BrowseSorting() {
        WorkshopManager* mgr = WorkshopManager::instance();

        WorkshopItem a;
        a.name = "Alpha";
        a.category = "cars";
        mgr->publishItem(a, "");

        WorkshopItem b;
        b.name = "Beta";
        b.category = "cars";
        mgr->publishItem(b, "");

        WorkshopItem c;
        c.name = "Gamma";
        c.category = "cars";
        mgr->publishItem(c, "");

        auto items = mgr->getPublishedItems();
        QString alphaId = items[0].id;
        QString betaId = items[1].id;
        QString gammaId = items[2].id;

        mgr->rateItem(alphaId, 5);
        mgr->rateItem(betaId, 3);
        mgr->rateItem(gammaId, 4);

        mgr->setInstalled(alphaId, true);

        WorkshopManager::BrowseQuery query;
        query.category = "cars";
        query.sortBy = "name";
        query.ascending = true;

        auto sorted = mgr->browse(query);
        QCOMPARE(sorted.size(), 3);
        QCOMPARE(sorted[0].name, QString("Alpha"));
        QCOMPARE(sorted[1].name, QString("Beta"));
        QCOMPARE(sorted[2].name, QString("Gamma"));

        query.ascending = false;
        sorted = mgr->browse(query);
        QCOMPARE(sorted[0].name, QString("Gamma"));
        QCOMPARE(sorted[2].name, QString("Alpha"));
    }

    void test_WorkshopManager_GetItemsByAuthor() {
        WorkshopManager* mgr = WorkshopManager::instance();

        WorkshopItem a;
        a.name = "Mod A";
        a.author = "Dev1";
        mgr->publishItem(a, "");

        WorkshopItem b;
        b.name = "Mod B";
        b.author = "Dev2";
        mgr->publishItem(b, "");

        WorkshopItem c;
        c.name = "Mod C";
        c.author = "Dev1";
        mgr->publishItem(c, "");

        auto dev1Items = mgr->getItemsByAuthor("Dev1");
        QCOMPARE(dev1Items.size(), 2);

        auto dev2Items = mgr->getItemsByAuthor("Dev2");
        QCOMPARE(dev2Items.size(), 1);

        auto devXItems = mgr->getItemsByAuthor("Nonexistent");
        QCOMPARE(devXItems.size(), 0);
    }
};

QTEST_MAIN(TestWorkshop)
#include "test_Workshop.moc"
