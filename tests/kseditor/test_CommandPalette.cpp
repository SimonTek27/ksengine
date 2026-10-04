#include "sdk/kseditor/modules/modellingEditor/CommandPalette.h"
#include <QtTest/QtTest>

using namespace ks;

class TestCommandPalette : public QObject {
    Q_OBJECT

private slots:
    void test_instance();
    void test_registerCommand();
    void test_unregisterCommand();
    void test_searchCommands();
    void test_executeCommand();
    void test_categories();
    void test_fuzzySearch();
};

void TestCommandPalette::test_instance()
{
    CommandPalette* cp = CommandPalette::instance();
    QVERIFY(cp != nullptr);
    QCOMPARE(CommandPalette::instance(), cp);
}

void TestCommandPalette::test_registerCommand()
{
    CommandPalette* cp = CommandPalette::instance();
    cp->registerCommand("test.cmd", "Test Command", "Testing", "Ctrl+T");
    QVERIFY(cp->hasCommand("test.cmd"));
    auto cmd = cp->getCommand("test.cmd");
    QCOMPARE(cmd.name, QString("Test Command"));
    QCOMPARE(cmd.category, QString("Testing"));
}

void TestCommandPalette::test_unregisterCommand()
{
    CommandPalette* cp = CommandPalette::instance();
    cp->registerCommand("temp.cmd", "Temporary", "Temp");
    QVERIFY(cp->hasCommand("temp.cmd"));
    cp->unregisterCommand("temp.cmd");
    QVERIFY(!cp->hasCommand("temp.cmd"));
}

void TestCommandPalette::test_searchCommands()
{
    CommandPalette* cp = CommandPalette::instance();
    cp->registerCommand("search.one", "Alpha Command", "Search");
    cp->registerCommand("search.two", "Beta Command", "Search");
    auto results = cp->searchCommands("Alpha");
    QVERIFY(results.size() >= 1);
    bool found = false;
    for (const auto& c : results) {
        if (c.id == "search.one") { found = true; break; }
    }
    QVERIFY(found);
}

void TestCommandPalette::test_executeCommand()
{
    CommandPalette* cp = CommandPalette::instance();
    bool executed = false;
    cp->registerCommand("exec.test", "Exec Test", "Exec");
    cp->setHandler("exec.test", [&]() { executed = true; });
    cp->executeCommand("exec.test");
    QVERIFY(executed);
}

void TestCommandPalette::test_categories()
{
    CommandPalette* cp = CommandPalette::instance();
    cp->registerCommand("cat.a", "Cat A", "Group1");
    cp->registerCommand("cat.b", "Cat B", "Group2");
    auto cats = cp->getCategories();
    QVERIFY(cats.contains("Group1"));
    QVERIFY(cats.contains("Group2"));
    auto group1 = cp->getByCategory("Group1");
    QCOMPARE(group1.size(), 1);
}

void TestCommandPalette::test_fuzzySearch()
{
    CommandPalette* cp = CommandPalette::instance();
    cp->setFuzzySearch(true);
    QVERIFY(cp->isFuzzySearchEnabled());
    cp->setFuzzySearch(false);
    QVERIFY(!cp->isFuzzySearchEnabled());
}

QTEST_MAIN(TestCommandPalette)
#include "test_CommandPalette.moc"
