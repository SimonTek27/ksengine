#include "sys/SettingsManager.h"
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>

class TestSettingsSystem : public QObject {
    Q_OBJECT

private slots:
    void test_defaultValues();
    void test_setAndGet();
    void test_typedAccessors();
    void test_contains();
    void test_remove();
    void test_groupManagement();
    void test_temporaryOverride();
    void test_reset();
};

void TestSettingsSystem::test_defaultValues()
{
    SettingsManager mgr;
    QVariant val = mgr.value("nonexistent.key", QVariant(42));
    QCOMPARE(val, QVariant(42));
}

void TestSettingsSystem::test_setAndGet()
{
    SettingsManager mgr;
    mgr.setValue("test.string", QVariant("hello"));
    QCOMPARE(mgr.value("test.string").toString(), QString("hello"));
    mgr.setValue("test.int", QVariant(123));
    QCOMPARE(mgr.value("test.int").toInt(), 123);
}

void TestSettingsSystem::test_typedAccessors()
{
    SettingsManager mgr;
    mgr.setValue("app.name", "ksEditor");
    mgr.setValue("app.version", 210);
    mgr.setValue("app.debug", true);
    mgr.setValue("app.scale", 1.5);

    QCOMPARE(mgr.string("app.name"), QString("ksEditor"));
    QCOMPARE(mgr.integer("app.version"), 210);
    QCOMPARE(mgr.boolean("app.debug"), true);
    QCOMPARE(mgr.real("app.scale"), 1.5);
}

void TestSettingsSystem::test_contains()
{
    SettingsManager mgr;
    mgr.setValue("test.exists", "yes");
    QVERIFY(mgr.contains("test.exists"));
    QVERIFY(!mgr.contains("test.nope"));
}

void TestSettingsSystem::test_remove()
{
    SettingsManager mgr;
    mgr.setValue("test.remove_me", "value");
    QVERIFY(mgr.contains("test.remove_me"));
    mgr.remove("test.remove_me");
    QVERIFY(!mgr.contains("test.remove_me"));
}

void TestSettingsSystem::test_groupManagement()
{
    SettingsManager mgr;
    mgr.beginGroup("editor");
    mgr.setValue("fontSize", 12);
    mgr.setValue("tabWidth", 4);
    mgr.endGroup();

    QCOMPARE(mgr.value("editor/fontSize").toInt(), 12);
    QCOMPARE(mgr.value("editor/tabWidth").toInt(), 4);

    mgr.beginGroup("editor");
    auto keys = mgr.childKeys();
    QVERIFY(keys.contains("fontSize"));
    QVERIFY(keys.contains("tabWidth"));
    mgr.endGroup();
}

void TestSettingsSystem::test_temporaryOverride()
{
    SettingsManager mgr;
    mgr.setValue("theme", "dark");
    QCOMPARE(mgr.string("theme"), QString("dark"));

    mgr.setTemporaryOverride("theme", "light");
    QCOMPARE(mgr.string("theme"), QString("light"));

    mgr.clearOverride("theme");
    QCOMPARE(mgr.string("theme"), QString("dark"));
}

void TestSettingsSystem::test_reset()
{
    SettingsManager mgr;
    mgr.setValue("reset.me", "value");
    QCOMPARE(mgr.string("reset.me"), QString("value"));
    mgr.reset("reset.me");
    QCOMPARE(mgr.string("reset.me", "default"), QString("default"));
}

QTEST_MAIN(TestSettingsSystem)
#include "test_SettingsSystem.moc"
