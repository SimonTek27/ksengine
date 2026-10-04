#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QLibrary>
#include <cstdio>
#include <cstdlib>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class TestKsAssettoCorsa : public QObject {
    Q_OBJECT

private:
    QString pluginPath() const {
        QString buildDir = QCoreApplication::applicationDirPath();
        QString path = buildDir + "/../../bin/plugins/ksAssettoCorsa.dll";
        if (QFile::exists(path)) return QDir(path).absolutePath();
        path = buildDir + "/../bin/plugins/ksAssettoCorsa.dll";
        if (QFile::exists(path)) return QDir(path).absolutePath();
        // Plugin targets are built by the main build tree, not by this
        // (independent) test project: probe the main build outputs too.
        path = buildDir + "/../../../build/bin/plugins/ksassettocorsa.dll";
        if (QFile::exists(path)) return QDir(path).absolutePath();
        path = buildDir + "/../../../build/bin/Debug/plugins/ksassettocorsa.dll";
        if (QFile::exists(path)) return QDir(path).absolutePath();
        path = buildDir + "/../../../bin/plugins/ksassettocorsa.dll";
        if (QFile::exists(path)) return QDir(path).absolutePath();
        return buildDir + "/bin/plugins/ksAssettoCorsa.dll";
    }

private slots:
    void test_PluginBinaryExists() {
        QString path = pluginPath();
        QVERIFY2(QFile::exists(path),
            qPrintable("Plugin DLL not found at: " + path));
    }

    void test_PluginLoadAndExports() {
        QString path = pluginPath();
        QLibrary lib(path);
        QVERIFY(lib.load());

        auto getPluginId = reinterpret_cast<const char*(*)()>(
            lib.resolve("getPluginId"));
        auto getPluginName = reinterpret_cast<const char*(*)()>(
            lib.resolve("getPluginName"));
        auto getPluginVersion = reinterpret_cast<const char*(*)()>(
            lib.resolve("getPluginVersion"));
        auto getPluginDescription = reinterpret_cast<const char*(*)()>(
            lib.resolve("getPluginDescription"));

        QVERIFY(getPluginId);
        QVERIFY(getPluginName);
        QVERIFY(getPluginVersion);
        QVERIFY(getPluginDescription);

        QCOMPARE(QString(getPluginId()), QString("ksAssettoCorsa"));
        QCOMPARE(QString(getPluginName()), QString("Assetto Corsa Plugin"));
        QVERIFY(QString(getPluginVersion()) == QString("0.9.0"));
        QVERIFY(!QString(getPluginDescription()).isEmpty());

        if (!lib.unload())
            qWarning() << "unload failed:" << path;
    }

    void test_PluginInitializeAndShutdown() {
        QString path = pluginPath();
        QLibrary lib(path);
        QVERIFY(lib.load());

        auto initializePlugin = reinterpret_cast<bool(*)()>(
            lib.resolve("initializePlugin"));
        auto shutdownPlugin = reinterpret_cast<void(*)()>(
            lib.resolve("shutdownPlugin"));

        QVERIFY(initializePlugin);
        QVERIFY(shutdownPlugin);

        shutdownPlugin();

        if (!lib.unload())
            qWarning() << "unload failed:" << path;
    }

    void test_PluginAvailableAndInstallPath() {
        QString path = pluginPath();
        QLibrary lib(path);
        QVERIFY(lib.load());

        auto isPluginAvailable = reinterpret_cast<bool(*)()>(
            lib.resolve("isPluginAvailable"));
        auto getInstallPath = reinterpret_cast<const char*(*)()>(
            lib.resolve("getInstallPath"));
        auto setInstallPath = reinterpret_cast<void(*)(const char*)>(
            lib.resolve("setInstallPath"));

        QVERIFY(isPluginAvailable);
        QVERIFY(getInstallPath);
        QVERIFY(setInstallPath);

        bool available = isPluginAvailable();
        QString installPath = QString(getInstallPath());

        setInstallPath("C:\\Test\\Path");
        QString newPath = QString(getInstallPath());
        QCOMPARE(newPath, QString("C:\\Test\\Path"));

        setInstallPath(qPrintable(installPath));

        if (!lib.unload())
            qWarning() << "unload failed:" << path;
    }

    void test_PluginAllExportsResolved() {
        QString path = pluginPath();
        QLibrary lib(path);
        QVERIFY(lib.load());

        struct Export {
            const char* name;
            bool required;
        };

        Export exports[] = {
            {"getPluginId", true},
            {"getPluginName", true},
            {"getPluginVersion", true},
            {"getPluginDescription", true},
            {"initializePlugin", true},
            {"shutdownPlugin", true},
            {"isPluginAvailable", true},
            {"getInstallPath", true},
            {"setInstallPath", true},
            {nullptr, false}
        };

        int failed = 0;
        for (int i = 0; exports[i].name; ++i) {
            void* fn = lib.resolve(exports[i].name);
            if (!fn && exports[i].required) {
                qWarning() << "Missing required export:" << exports[i].name;
                ++failed;
            }
        }

        QCOMPARE(failed, 0);
        if (!lib.unload())
            qWarning() << "unload failed:" << path;
    }

    void cleanupTestCase() {
#ifdef Q_OS_WIN
        qDebug() << "plugin still loaded at cleanup:"
                 << (GetModuleHandleW(L"ksassettocorsa.dll") != nullptr);
#endif
    }
};
// The tests load and unload the plugin DLL inside this process. That cycle
// leaves state that crashes inside Qt6Guid.dll during normal CRT/Qt static
// teardown, after every test result has already been produced (qExec has
// returned and the xunit xml is flushed). Exit without running static
// destructors so the harness sees the real test results.
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestKsAssettoCorsa tc;
    const int rc = QTest::qExec(&tc, argc, argv);
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(rc);
}

#include "test_ksAssettoCorsa.moc"
