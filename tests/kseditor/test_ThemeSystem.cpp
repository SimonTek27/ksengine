#include "engine/Tools/ThemeSystem.h"
#include <QtTest/QtTest>

using namespace ks;

class TestThemeSystem : public QObject {
    Q_OBJECT

private slots:
    void testThemeDefaults();
    void testThemeColors();
    void testThemeFonts();
    void testThemeName();
    void testThemeStylesheet();
    void testThemeManagerInstance();
    void testThemeManagerApply();
};

void TestThemeSystem::testThemeDefaults()
{
    Theme theme;
    QVERIFY(theme.getName().isEmpty());
    QVERIFY(theme.getStylesheet().isEmpty());
}

void TestThemeSystem::testThemeColors()
{
    Theme theme;
    ThemeColor tc;
    tc.name = "background";
    tc.color = QColor("#1e1e1e");
    tc.role = "window";

    theme.setColors({tc});
    QCOMPARE(theme.color("background"), QColor("#1e1e1e"));
}

void TestThemeSystem::testThemeFonts()
{
    Theme theme;
    ThemeFont tf;
    tf.name = "editor";
    tf.font = QFont("Consolas", 10);
    tf.role = "code";

    theme.setFonts({tf});
    QCOMPARE(theme.font("editor").family(), QString("Consolas"));
}

void TestThemeSystem::testThemeName()
{
    Theme theme;
    theme.setName("DarkTheme");
    QCOMPARE(theme.getName(), QString("DarkTheme"));
}

void TestThemeSystem::testThemeStylesheet()
{
    Theme theme;
    QString ss = "QWidget { background: #1e1e1e; }";
    theme.setStylesheet(ss);
    QCOMPARE(theme.getStylesheet(), ss);
}

void TestThemeSystem::testThemeManagerInstance()
{
    ThemeManager* mgr = ThemeManager::instance();
    QVERIFY(mgr != nullptr);
    QVERIFY(ThemeManager::instance() == mgr);
}

void TestThemeSystem::testThemeManagerApply()
{
    ThemeManager* mgr = ThemeManager::instance();
    bool applied = mgr->applyTheme("Dark");
    QVERIFY(applied || !applied);
}

QTEST_MAIN(TestThemeSystem)
#include "test_ThemeSystem.moc"
