#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QQmlEngine>
#include <QJSEngine>
#include <QQuickStyle>
#include <QQuickWidget>
#include <QQuickItem>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QWindow>
#include <QPointer>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcessEnvironment>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <QQmlContext>
#include <exception>

#ifdef _WIN32
#include <windows.h>
#endif

#include "MainWindow.h"
#include "../resources/ui/CustomTitleBar.h"
#include "engine/sys/ModuleManager.h"
#include "sdk/kseditor/engine/sys/PluginManager.h"
#include "../resources/ui/SplashScreen.h"
#include "../resources/ui/WelcomeScreenQmlBridge.h"
#include "sdk/kseditor/engine/sys/HangMonitor.h"
#include "../resources/ui/FontEditorDialog.h"
#include "help/HelpSystem.h"
#include "../resources/ui/ribbontheme.h"

// QML Bridge registrations
#include "sdk/kseditor/modules/modellingEditor/3DModelingQmlBridge.h"
#include "sdk/kseditor/modules/modellingEditor/RayTraceImageProvider.h"
#include "sdk/kseditor/modules/modellingEditor/BakeImageProvider.h"
#include "sdk/kseditor/modules/modellingEditor/BoolOpQmlBridge.h"
#include "sdk/kseditor/modules/modellingEditor/SymmetryQmlBridge.h"
#include "sdk/kseditor/modules/PhysicsEditor/PhysicsQmlBridge.h"
#include "sdk/kseditor/qmlbridges/Audio/AudioQMLBridge.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/audio/AudioEngineQML.h"
#include "sdk/kseditor/engine/Audio/AudioModuleBridge.h"
#include "editor/AIEditor/AIEditorQmlBridge.h"
#include "tools/FormatToolsQmlBridge.h"
#include "modmanager/ModManagerQmlBridge.h"
#include "editor/ppfiltersEditor/PPFiltersQmlBridge.h"
#include "sdk/kseditor/engine/devices/3dprint/ThreeDPrintQmlBridge.h"
#include "editor/FfbEditor/FfbEditorQmlBridge.h"
#include "sdk/kseditor/modules/cockpitInstruments/CockpitInstrumentsQmlBridge.h"
#include "sdk/kseditor/modules/LicensePlatesEditor/LicensePlatesQmlBridge.h"
#include "sdk/kseditor/modules/fontEditor/FontCreatorQmlBridge.h"
#include "sdk/kseditor/modules/sound/editor/AudioEffectsQmlBridge.h"
#include "sdk/kseditor/modules/sound/editor/AudioEditorModule.h"
#include "sdk/kseditor/modules/ShowroomEditor/ShowroomEditorQmlBridge.h"
#include "sdk/kseditor/modules/PhysicsEditor/telemetry/TelemetryQmlBridge.h"
#include "sdk/kseditor/modules/PhysicsEditor/telemetry/TelemetryFeedbackBridge.h"
#include "sdk/kseditor/modules/PhysicsEditor/SetupEditor/SetupEditorQmlBridge.h"
#include "sdk/kseditor/modules/PhysicsEditor/PhysicsProfiler.h"
#include "sdk/kseditor/modules/PhysicsEditor/telemetry/LapTimeValidation.h"
#include "sdk/kseditor/modules/modellingEditor/TrackBuilder/TerrainEditorQmlBridge.h"
#include "sdk/kseditor/qmlbridges/assets/AssetsLibraryQmlBridge.h"
#include "sdk/kseditor/engine/mesh/MeshDataBridge.h"
#include "sdk/kseditor/engine/material/TexturePaintQmlBridge.h"
#include "sdk/kseditor/qmlbridges/mesh/MeshLoaderQML.h"
#include "sdk/kseditor/modules/modellingEditor/SceneMeshGeometry.h"
#include "sdk/kseditor/modules/modellingEditor/ParticlePointsGeometry.h"
#include "sdk/kseditor/modules/modellingEditor/ParticleInstancing.h"
#include "sdk/kseditor/qmlbridges/Scripting/CspConfigQmlBridge.h"
#include "sdk/kseditor/qmlbridges/Scripting/ContentQMLBridge.h"
#include "sdk/kseditor/modules/modellingEditor/CharacterBuilder/CharacterEditorQmlBridge.h"
#include "sdk/kseditor/engine/network/CollabEditorQmlBridge.h"
#include "sdk/kseditor/engine/network/ChatQmlBridge.h"
#include "sdk/kseditor/modules/ksOffice/OfficeQmlBridge.h"
#include "sdk/kseditor/modules/ksOffice/OfficeDocumentBridge.h"
#include "sdk/kseditor/qmlbridges/Audio/AudioWaveformBridge.h"
#include "sdk/kseditor/engine/Audio/KsACSndEventBridge.h"
#include "sdk/kseditor/engine/Audio/AudioStudioTypes.h"
#include "sdk/kseditor/qmlbridges/Scripting/ACEContentQMLBridge.h"
#include "sdk/kseditor/engine/Audio/AIAudioStemSeparator.h"
#include "sdk/kseditor/engine/FileFormat/PublishValidatorBridge.h"
#include "engine/devices/scanners/photogrammetry/PhotogrammetryCapture.h"
#include "engine/devices/scanners/turntable_scanner/TurntableScanner.h"
#include "engine/devices/scanners/structured_light/StructuredLightScanner.h"
#include "engine/devices/scanners/stereo_vision/StereoVisionScanner.h"


static int runMainWindow(QApplication& app, const QString& projectPath)
{
    qDebug() << "Creating MainWindow with projectPath:" << projectPath;
    MainWindow window(projectPath);
    window.show();

    QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
        ks::PluginManager::instance()->saveLoadedList();
    });

    return app.exec();
}

static QMainWindow* createStandaloneWindow(const QString& title, const QString& themeKey,
    int w, int h, const QString& bgColor, bool includeTitleBar = true)
{
    QMainWindow* win = new QMainWindow(nullptr, Qt::Window | Qt::FramelessWindowHint);
    win->setWindowTitle(title);
    win->resize(w, h);
    win->setStyleSheet(QString("background-color: %1;").arg(bgColor));
    auto* central = new QWidget(win);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    if (includeTitleBar) {
        auto* tb = new CustomTitleBar(win);
        QObject::connect(tb, &CustomTitleBar::minimizeRequested, win, &QWidget::showMinimized);
        QObject::connect(tb, &CustomTitleBar::maximizeRequested, win, [win]() {
            win->isMaximized() ? win->showNormal() : win->showMaximized();
        });
        QObject::connect(tb, &CustomTitleBar::closeRequested, win, &QWidget::close);
        tb->setTitle(title);
        layout->addWidget(tb);
        // Apply theme
        ks::editor::RibbonThemeManager::instance().applyWindowFrame(win, themeKey);
        ks::editor::RibbonTheme t = ks::editor::RibbonThemeManager::instance().theme(themeKey);
        tb->applyTheme(t.titleBarBg, t.windowBorder, t.titleBarText,
                       t.buttonHover, t.buttonPressed, t.windowBorder);
    }
    win->setCentralWidget(central);
    return win;
}

// Bridge exposing frameless-window controls to the QML modeler UI (ribbon-as-titlebar).
class ModelerWindowBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool maximized READ isMaximized NOTIFY maximizedChanged)
public:
    explicit ModelerWindowBridge(QMainWindow* win, QObject* parent = nullptr)
        : QObject(parent), m_win(win) {}

    bool isMaximized() const { return m_win ? m_win->isMaximized() : false; }

    Q_INVOKABLE void minimize() { if (m_win) m_win->showMinimized(); }
    Q_INVOKABLE void toggleMaximize() {
        if (m_win) {
            if (m_win->isMaximized()) m_win->showNormal(); else m_win->showMaximized();
            emit maximizedChanged();
        }
    }
    Q_INVOKABLE void closeWindow() { if (m_win) m_win->close(); }
    Q_INVOKABLE void beginMove() {
        if (m_win && m_win->windowHandle()) m_win->windowHandle()->startSystemMove();
    }
    Q_INVOKABLE void requestFocus() { if (m_win) { m_win->raise(); m_win->activateWindow(); } }
    Q_INVOKABLE void showHelp() { ks::HelpSystem::instance()->showHelp(); }

signals:
    void maximizedChanged();

private:
    QPointer<QMainWindow> m_win;
};

static int appMain(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ksEditor");
    app.setApplicationVersion("1.16.4");
    app.setOrganizationName("ksEditor");

    // Detect when the GUI thread stops responding and offer the user a native
    // dialog with: Relaunch / Export logs / Keep waiting / Terminate program.
    // Started once the event loop begins to avoid false positives during
    // synchronous startup work; stopped automatically when the app quits.
    QTimer::singleShot(0, []() {
        HangMonitor::instance().start();
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        HangMonitor::instance().stop();
    });

    // Ensure Qt bin directory is on PATH so QML plugin DLLs can load
    QString qtBin = QStringLiteral("C:/Qt/6.11.1/msvc2022_64/bin");
    QString path = QProcessEnvironment::systemEnvironment().value("PATH");
    if (!path.contains(qtBin, Qt::CaseInsensitive)) {
        qputenv("PATH", (qtBin + ";" + path).toLocal8Bit());
    }

    // Load translations
    QTranslator qtTranslator;
    if (qtTranslator.load("qt_" + QLocale::system().name(),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }

    QTranslator appTranslator;
    QString lang = QLocale::system().name(); // e.g. "de", "it", "ja"
    QSettings s;
    if (s.contains("ui/language")) {
        QString savedLang = s.value("ui/language").toString();
        if (savedLang != "system")
            lang = savedLang;
    }
    QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + "/i18n",
        QCoreApplication::applicationDirPath() + "/../i18n",
        ":/i18n"
    };
    for (const QString& path : searchPaths) {
        if (appTranslator.load(path + "/kseditor_" + lang)) {
            app.installTranslator(&appTranslator);
            break;
        }
    }

    // Register QML bridge types
    qmlRegisterSingletonType<ks::KSModelerQml>("ksEditor.Modeler", 1, 0, "Modeler",
        [](QQmlEngine* engine, QJSEngine*) -> QObject* {
            engine->addImageProvider("raytrace", new ks::RayTraceImageProvider(&ks::KSModelerQml::instance()));
            engine->addImageProvider("bake", new ks::BakeImageProvider(&ks::KSModelerQml::instance()));
            return &ks::KSModelerQml::instance();
        });
    qmlRegisterSingletonType<ks::PhysicsQmlBridge>("ksEditor.Physics", 1, 0, "Physics",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::PhysicsQmlBridge::instance(); });
    qmlRegisterSingletonType<AudioQMLBridge>("ksEditor.Audio", 1, 0, "AudioBridge",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return AudioQMLBridge::instance(); });
    qmlRegisterSingletonType<ks::audio::AudioEngineQML>("ksEditor.AudioEngine", 1, 0, "AudioEngine",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::audio::AudioEngineQML::instance(); });
    qmlRegisterSingletonType<ks::MeshDataBridge>("ksEditor.MeshData", 1, 0, "MeshData",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::MeshDataBridge::instance(); });
    qmlRegisterSingletonType<ks::AIEditorQmlBridge>("ksEditor.AIEditor", 1, 0, "AIEditor",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::AIEditorQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::FormatToolsQmlBridge>("ksEditor.FormatTools", 1, 0, "FormatTools",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::FormatToolsQmlBridge::instance(); });
    // Register AI Stem Separation bridge
    qmlRegisterSingletonType<ks::audio::AIAudioStemSeparator>("ksEditor.AIAudioStemSeparator", 1, 0, "AIAudioStemSeparator",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::audio::AIAudioStemSeparator::instance(); });
    // Plugin registers "ksEditor.Workshop" singleton
    qmlRegisterSingletonType<ks::ModManagerQmlBridge>("ksEditor.ModManager", 1, 0, "ModManager",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::ModManagerQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::TelemetryQmlBridge>("ksEditor.Telemetry", 1, 0, "Telemetry",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::TelemetryQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::TelemetryFeedbackBridge>("ksEditor.TelemetryFeedback", 1, 0, "TelemetryFeedback",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::TelemetryFeedbackBridge::instance(); });
    qmlRegisterSingletonType<ks::TexturePaintQmlBridge>("ksEditor.TexturePainter", 1, 0, "TexturePainter",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::TexturePaintQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::SetupEditorQmlBridge>("ksEditor.SetupEditor", 1, 0, "SetupEditor",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::SetupEditorQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::PPFiltersQmlBridge>("ksEditor.PPFilters", 1, 0, "PPFilters",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::PPFiltersQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::CockpitInstrumentsQmlBridge>("ksEditor.CockpitInstruments", 1, 0, "CockpitInstruments",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::CockpitInstrumentsQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::LicensePlatesQmlBridge>("ksEditor.LicensePlates", 1, 0, "LicensePlates",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::LicensePlatesQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::FontCreatorQmlBridge>("ksEditor.FontCreator", 1, 0, "FontCreator",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::FontCreatorQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::TerrainEditorQmlBridge>("ksEditor.TerrainEditor", 1, 0, "TerrainEditor",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::TerrainEditorQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::FfbEditorQmlBridge>("ksEditor.FfbEditor", 1, 0, "FfbEditor",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::FfbEditorQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::ShowroomEditorQmlBridge>("ksEditor.ShowroomEditor", 1, 0, "ShowroomEditor",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::ShowroomEditorQmlBridge::instance(); });

    // Register photogrammetry capture bridge (scanner classes are not singletons)
#if 0
    qmlRegisterSingletonType<ks::photogrammetry::PhotogrammetryCapture>("ksEditor.Photogrammetry", 1, 0, "PhotogrammetryCapture",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::photogrammetry::PhotogrammetryCapture::instance(); });

    // Register turntable scanner bridge
    qmlRegisterSingletonType<ks::turntable_scanner::TurntableScanner>("ksEditor.TurntableScanner", 1, 0, "TurntableScanner",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::turntable_scanner::TurntableScanner::instance(); });

    // Register structured light scanner bridge
    qmlRegisterSingletonType<ks::structured_light::StructuredLightScanner>("ksEditor.StructuredLight", 1, 0, "StructuredLightScanner",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::structured_light::StructuredLightScanner::instance(); });

    // Register stereo vision scanner bridge
    qmlRegisterSingletonType<ks::stereo_vision::StereoVisionScanner>("ksEditor.StereoVision", 1, 0, "StereoVisionScanner",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::stereo_vision::StereoVisionScanner::instance(); });
#endif
    qmlRegisterSingletonType<ks::AssetsLibraryQmlBridge>("ksEditor.AssetsLibrary", 1, 0, "AssetsLibrary",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::AssetsLibraryQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::audio::AudioEffectsQmlBridge>("ksEditor.AudioEffects", 1, 0, "AudioEffects",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::audio::AudioEffectsQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::PhysicsProfiler>("ksEditor.PhysicsProfiler", 1, 0, "PhysicsProfiler",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::PhysicsProfiler::instance(); });
    qmlRegisterSingletonType<ks::LapTimeValidation>("ksEditor.LapTimeValidation", 1, 0, "LapTimeValidation",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::LapTimeValidation::instance(); });
    qmlRegisterSingletonType<ks::fileformat::PublishValidatorBridge>("ksEditor.PublishValidator", 1, 0, "PublishValidator",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::fileformat::PublishValidatorBridge::instance(); });

    // Register non-singleton QML types
    qmlRegisterType<ks::audio::KSAudioModuleBridge>("ksEditor.AudioModule", 1, 0, "AudioModule");
    qmlRegisterSingletonType<ks::CollabEditorQmlBridge>("ksEditor.Collaboration", 1, 0, "CollabEditor",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::CollabEditorQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::chat::ChatQmlBridge>("ksEditor.KsChat", 1, 0, "KsChat",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::chat::ChatQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::office::OfficeQmlBridge>("ksEditor.KsOffice", 1, 0, "KsOffice",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::office::OfficeQmlBridge::instance(); });
    qmlRegisterSingletonType<ks::office::OfficeDocumentBridge>("ksEditor.KsOffice", 1, 0, "OfficeDocument",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::office::OfficeDocumentBridge::instance(); });
    qmlRegisterSingletonType<ks::CspConfigQmlBridge>("ksEditor.CspConfig", 1, 0, "CspConfig",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::CspConfigQmlBridge::instance(); });
    qmlRegisterType<ContentQMLBridge>("ksEditor.Content", 1, 0, "Content");
    qmlRegisterType<MeshLoaderQML>("ksEditor.MeshLoader", 1, 0, "MeshLoader");
    qmlRegisterType<ks::QmlSceneMeshGeometry>("ksEditor.Modeler", 1, 0, "SceneMeshGeometry");
    qmlRegisterType<ks::QmlParticlePointsGeometry>("ksEditor.Modeler", 1, 0, "ParticlePointsGeometry");
    qmlRegisterType<ks::QmlParticleInstancing>("ksEditor.Modeler", 1, 0, "ParticleInstancing");
    qmlRegisterSingletonType<ks::editor::BoolOpQmlBridge>("ksEditor.BoolOp", 1, 0, "BoolOp",
        [](QQmlEngine*, QJSEngine*) -> QObject* {
            return &ks::editor::BoolOpQmlBridge::instance();
        });
    qmlRegisterSingletonType<ks::SymmetryQmlBridge>("ksEditor.Symmetry", 1, 0, "Symmetry",
        [](QQmlEngine*, QJSEngine*) -> QObject* {
            return &ks::SymmetryQmlBridge::instance();
        });
    qmlRegisterSingletonType<ks::CharacterEditorQmlBridge>("ksEditor.Character", 1, 0, "CharacterEditor",
        [](QQmlEngine*, QJSEngine*) -> QObject* {
            return ks::CharacterEditorQmlBridge::instance();
        });

    // Register 3D Printing module
    qmlRegisterType<ks::printing::ThreeDPrintQmlBridge>("ksEditor.Printing", 1, 0, "PrintManager");

    // Register ACE (Assetto Corsa EVO) bridge
    qmlRegisterSingletonType<ks::ACEContentQMLBridge>("ksEditor.ACEContent", 1, 0, "ACEContent",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return ks::ACEContentQMLBridge::instance(); });

    // Register ACE Protobuf inspector bridge
    qmlRegisterSingletonType<ks::ACEProtobufQmlBridge>("ksEditor.ACEProtobuf", 1, 0, "ACEProtobuf",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return new ks::ACEProtobufQmlBridge(); });

    // Initialize plugin system
    ks::PluginManager::instance()->scan();

    // Load modern dark theme
    QFile qssFile(":/ui/styles/dark.qss");
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
        qssFile.close();
    }

    QStringList args;
    for (int i = 0; i < argc; ++i) args << QString(argv[i]);

    // Helper: run a standalone mode and return true if handled
    auto runMode = [&](const QString& mode) -> bool {
        if (mode == "font") {
            SplashScreen::showSplash(app);
            QMainWindow* w = createStandaloneWindow("KS Font Editor", "font", 1280, 800, "#1e1e1e");
            auto* quickWidget = new QQuickWidget();
            quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
            quickWidget->setAttribute(Qt::WA_DeleteOnClose);
            quickWidget->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
            QQmlComponent* comp = new QQmlComponent(quickWidget->engine());
            comp->setData(QByteArray(),
                QUrl::fromLocalFile(":/qml/modules/font_KSFontCreator.qml"));
            QObject* root = comp->beginCreate(quickWidget->engine()->rootContext());
            if (comp->isError()) {
                QString err = comp->errors().first().toString();
                QFile ef(QCoreApplication::applicationDirPath() + "/qml_error.txt");
                if (ef.open(QIODevice::WriteOnly)) { ef.write(err.toUtf8()); ef.close(); }
                return true;
            }
            comp->completeCreate();
            quickWidget->setContent(QUrl::fromLocalFile(":/qml/modules/font_KSFontCreator.qml"), comp, qobject_cast<QQuickItem*>(root));
            auto* layout = qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());
            if (layout) layout->addWidget(quickWidget, 1);
            w->show();
            app.exec();
            return true;
        }
        if (mode == "paint") {
            MainWindow window{QString()};
            window.setPaintMode(true);
            window.show();
            app.exec();
            return true;
        }
        if (mode == "modeler") {
            qDebug() << "Modeler mode starting...";
            SplashScreen::showSplash(app);
            QMainWindow* w = createStandaloneWindow("KS Modeler", "car", 1652, 1062, "#111111", false);
            auto* qw = new QQuickWidget();
            qw->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
            qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
            auto* winBridge = new ModelerWindowBridge(w, qw);
            qw->rootContext()->setContextProperty("winBridge", winBridge);
            qDebug() << "Modeler QML path:" << ":/qml/pages/page_ksModeler.qml";
            qw->setSource(QUrl("qrc:///qml/pages/page_ksModeler.qml"));
            if (qw->status() == QQuickWidget::Error) {
                QString err = qw->errors().first().toString();
                qDebug() << "Modeler QML error:" << err;
                QMessageBox::critical(nullptr, "Modeler Error",
                    "Failed to load 3D Modeler:\n" + err);
                QFile ef(QCoreApplication::applicationDirPath() + "/qml_error.txt");
                if (ef.open(QIODevice::WriteOnly)) { ef.write(err.toUtf8()); ef.close(); }
                return true;
            }
            auto* layout = qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());
            if (layout) layout->addWidget(qw, 1);
            w->show();
            w->raise();
            w->activateWindow();
            qDebug() << "Modeler window shown, entering event loop";
            app.exec();
            qDebug() << "Modeler event loop exited";
            return true;
        }
        if (mode == "audiostudio") {
            SplashScreen::showSplash(app);
            QMainWindow* w = createStandaloneWindow("KS Audio Studio", "sound", 1652, 1062, "#111111", false);
            auto* qw = new QQuickWidget();
            qw->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
            qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
            auto* winBridge = new ModelerWindowBridge(w, qw);
            qw->rootContext()->setContextProperty("winBridge", winBridge);
            qw->setSource(QUrl("qrc:///qml/pages/page_ksAudioStudio.qml"));
            if (qw->status() == QQuickWidget::Error) {
                QString err = qw->errors().first().toString();
                QFile ef(QCoreApplication::applicationDirPath() + "/qml_error.txt");
                if (ef.open(QIODevice::WriteOnly)) { ef.write(err.toUtf8()); ef.close(); }
            }
            auto* layout = qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());
            if (layout) layout->addWidget(qw, 1);
            w->show();
            app.exec();
            return true;
        }
        if (mode == "audioeditor") {
            SplashScreen::showSplash(app);
            QMainWindow* w = createStandaloneWindow("KS Audio Editor", "sound", 1280, 800, "#121212");
            auto* qw = new QQuickWidget();
            qw->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
            qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
            auto* waveformBridge = new ks::audio::AudioWaveformBridge(qw);
            auto* eventBridge = new ks::audio::KsACSndEventBridge(qw);
            if (!ks::AudioEditorModule::instance())
                new ks::AudioEditorModule(qw);
            qw->rootContext()->setContextProperty("waveformBridge", waveformBridge);
            qw->rootContext()->setContextProperty("eventDefs", eventBridge);
            qw->setSource(QUrl("qrc:///qml/pages/page_ksAudioEditor.qml"));
            if (qw->status() == QQuickWidget::Error) {
                QString err = qw->errors().first().toString();
                QFile ef(QCoreApplication::applicationDirPath() + "/qml_error.txt");
                if (ef.open(QIODevice::WriteOnly)) { ef.write(err.toUtf8()); ef.close(); }
            }
            auto* layout = qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());
            if (layout) layout->addWidget(qw, 1);
            w->show();
            app.exec();
            return true;
        }
        if (mode == "physics") {
            SplashScreen::showSplash(app);
            QMainWindow* w = createStandaloneWindow("KS Physics Studio", "car", 1400, 900, "#111111");
            auto* qw = new QQuickWidget();
            qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
            qw->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
            qw->setSource(QUrl("qrc:///qml/modules/physicsEditor/phys_Editor.qml"));
            if (qw->status() == QQuickWidget::Error) {
                QString err = qw->errors().first().toString();
                QFile ef(QCoreApplication::applicationDirPath() + "/qml_error.txt");
                if (ef.open(QIODevice::WriteOnly)) { ef.write(err.toUtf8()); ef.close(); }
            }
            auto* layout = qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());
            if (layout) layout->addWidget(qw, 1);
            w->show();
            app.exec();
            return true;
        }
        if (mode == "code") {
            SplashScreen::showSplash(app);
            QMainWindow* w = createStandaloneWindow("ksIDEEditor", "code", 1400, 900, "#1e1e1e");
            auto* qw = new QQuickWidget();
            qw->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
            qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
            qw->setSource(QUrl("qrc:///qml/pages/page_ksIDEEditor.qml"));
            if (qw->status() == QQuickWidget::Error) {
                QString err = qw->errors().first().toString();
                QFile ef(QCoreApplication::applicationDirPath() + "/qml_error.txt");
                if (ef.open(QIODevice::WriteOnly)) { ef.write(err.toUtf8()); ef.close(); }
            }
            auto* layout = qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());
            if (layout) layout->addWidget(qw, 1);
            w->show();
            app.exec();
            return true;
        }
        if (mode == "ppfilters") {
            SplashScreen::showSplash(app);
            QMainWindow* w = createStandaloneWindow("KS PP Filters Editor", "ppfilters", 1400, 900, "#111111");
            auto* qw = new QQuickWidget();
            qw->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
            qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
            qw->setSource(QUrl("qrc:///qml/pages/page_ksPPFiltersEditor.qml"));
            if (qw->status() == QQuickWidget::Error) {
                QString err = qw->errors().first().toString();
                QFile ef(QCoreApplication::applicationDirPath() + "/qml_error.txt");
                if (ef.open(QIODevice::WriteOnly)) { ef.write(err.toUtf8()); ef.close(); }
            }
            auto* layout = qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());
            if (layout) layout->addWidget(qw, 1);
            w->show();
            app.exec();
            return true;
        }
        if (mode == "chat") {
            SplashScreen::showSplash(app);
            QMainWindow* w = createStandaloneWindow("KS Chat", "collab", 1280, 720, "#121212");
            auto* qw = new QQuickWidget();
            qw->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
            qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
            qw->setSource(QUrl("qrc:///qml/pages/page_ksChat.qml"));
            if (qw->status() == QQuickWidget::Error) {
                QString err = qw->errors().first().toString();
                QFile ef(QCoreApplication::applicationDirPath() + "/qml_error.txt");
                if (ef.open(QIODevice::WriteOnly)) { ef.write(err.toUtf8()); ef.close(); }
            }
            auto* layout = qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());
            if (layout) layout->addWidget(qw, 1);
            w->show();
            app.exec();
            return true;
        }
        if (mode == "office") {
            SplashScreen::showSplash(app);
            QMainWindow* w = createStandaloneWindow("KS Office", "collab", 1280, 720, "#121212");
            auto* qw = new QQuickWidget();
            qw->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
            qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
            qw->setSource(QUrl("qrc:///qml/pages/page_ksOffice.qml"));
            if (qw->status() == QQuickWidget::Error) {
                QString err = qw->errors().first().toString();
                QFile ef(QCoreApplication::applicationDirPath() + "/qml_error.txt");
                if (ef.open(QIODevice::WriteOnly)) { ef.write(err.toUtf8()); ef.close(); }
            }
            auto* layout = qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());
            if (layout) layout->addWidget(qw, 1);
            w->show();
            app.exec();
            return true;
        }
        if (mode == "simulator") {
            SplashScreen::showSplash(app);
            QMainWindow* w = createStandaloneWindow("ksEditor Simulator", "car", 1280, 720, "#111111");
            auto* qw = new QQuickWidget();
            qw->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
            qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
            qw->setSource(QUrl("qrc:///qml/pages/page_KsSimulator.qml"));
            if (qw->status() == QQuickWidget::Error) {
                QString err = qw->errors().first().toString();
                QFile ef(QCoreApplication::applicationDirPath() + "/qml_error.txt");
                if (ef.open(QIODevice::WriteOnly)) { ef.write(err.toUtf8()); ef.close(); }
            }
            auto* layout = qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());
            if (layout) layout->addWidget(qw, 1);
            w->show();
            app.exec();
            return true;
        }
        return false;
    };

    if (args.size() > 1) {
        QString cmd = args[1].toLower();
        if (cmd.startsWith("--")) cmd = cmd.mid(1);
        else if (cmd.startsWith("-")) cmd = cmd.mid(1);
        if (runMode(cmd))
            return 0;
        // Unknown mode — check for help or project file
        if (cmd == "h" || cmd == "help") {
            QMessageBox::information(nullptr, "ksEditor Help",
                "ksEditor 0.9.0 - Assetto Corsa Modding Suite\n\n"
                "Usage: kseditor.exe [options]\n\n"
                "Options:\n"
                "  -font, --font        Open font editor directly\n"
                "  -paint, --paint      Open PaintEditor in PhotoGIMP paint mode\n"
                "  -modeler, --modeler  Open 3D Modeler Studio directly\n"
                "  -audiostudio         Open Audio Studio directly\n"
                "  -audioeditor         Open Audio Editor directly\n"
                "  -physics, --physics  Open Physics Studio directly\n"
                "  -code, --code        Open IDE Code Editor directly\n"
                "  -ppfilters           Open PP Filters Editor directly\n"
                "  -chat, --chat        Open ksChat collaboration directly\n"
                "  -office, --office    Open ksOffice collaboration directly\n"
                "  -simulator           Open Driving Simulator (load track + drive car)\n"
                "  -nohw, --nohw        Disable hardware acceleration\n"
                "  -h, --help           Show this help message\n"
                "  <file.ksproj>        Open a project file");
            return 0;
        }
        if (args[1].endsWith(".ksproj", Qt::CaseInsensitive)) {
            QFile file(args[1]);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                file.close();
                if (!doc.isNull() && doc.isObject()) {
                    QString projectPath = doc.object()["path"].toString();
                    if (!projectPath.isEmpty() && QDir(projectPath).exists()) {
                        SplashScreen::showSplash(app);
                        MainWindow window(projectPath);
                        window.show();
                        return app.exec();
                    }
                }
            }
        }
    }

    SplashScreen::showSplash(app);

    qDebug() << "Showing WelcomeScreen (QML)...";

    auto* welcomeBridge = new WelcomeScreenQmlBridge();
    bool accepted = false;

    try {
        QMainWindow* welcomeWin = new QMainWindow(nullptr, Qt::Window | Qt::FramelessWindowHint);
        welcomeWin->setWindowTitle("ksEditor 1.16");
        welcomeWin->setWindowIcon(QIcon(":/icons/modeler.svg"));
        welcomeWin->setMinimumSize(600, 600);
        welcomeWin->setStyleSheet("background: #252526;");

        auto* welcomeWidget = new QQuickWidget();
        welcomeWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
        welcomeWidget->setAttribute(Qt::WA_DeleteOnClose);
        welcomeWidget->engine()->addImportPath(QStringLiteral("C:/Qt/6.11.1/msvc2022_64/qml"));
        welcomeWidget->rootContext()->setContextProperty("WelcomeScreenBridge", welcomeBridge);
        welcomeWidget->rootContext()->setContextProperty("welcomeWindow", welcomeWin);
        welcomeWidget->setSource(QUrl("qrc:///qml/pages/page_WelcomeScreen.qml"));

        if (welcomeWidget->status() == QQuickWidget::Error) {
            QString err = welcomeWidget->errors().first().toString();
            qDebug() << "WelcomeScreen QML error:" << err;
            QMessageBox::warning(nullptr, "Startup Error",
                QString("Failed to load welcome screen:\n%1").arg(err));
            delete welcomeWin;
            delete welcomeBridge;
            return 0;
        }

        auto* layout = qobject_cast<QVBoxLayout*>(welcomeWin->centralWidget()->layout());
        if (layout) layout->addWidget(welcomeWidget, 1);

        QEventLoop loop;
        bool completed = false;
        QObject::connect(welcomeBridge, &WelcomeScreenQmlBridge::completed, &loop, [&]() {
            completed = true;
            loop.quit();
        });
        QObject::connect(welcomeWin, &QMainWindow::destroyed, &loop, [&]() {
            loop.quit();
        });
        QObject::connect(welcomeWidget, &QQuickWidget::destroyed, &loop, [&]() {
            loop.quit();
        });

        welcomeWin->show();
        loop.exec();

        if (completed) accepted = true;
        else {
            qDebug() << "WelcomeScreen closed without selection, exiting";
            delete welcomeWin;
            return 0;
        }

        welcomeWin->close();
        delete welcomeWin;
    } catch (const std::exception& e) {
        qDebug() << "Exception during WelcomeScreen:" << e.what();
        QMessageBox::warning(nullptr, "Startup Error",
            QString("An error occurred while showing the welcome screen:\n%1").arg(e.what()));
        delete welcomeBridge;
        return 0;
    } catch (...) {
        qDebug() << "Unknown exception during WelcomeScreen";
        QMessageBox::warning(nullptr, "Startup Error",
            "An unknown error occurred while showing the welcome screen.");
        delete welcomeBridge;
        return 0;
    }

    if (!welcomeBridge->launchMode().isEmpty()) {
        QString mode = welcomeBridge->launchMode();
        delete welcomeBridge;
        qDebug() << "Launching suite mode:" << mode;
        runMode(mode);
        return 0;
    }

    if (!welcomeBridge->recentProjectPath().isEmpty()) {
        QString path = welcomeBridge->recentProjectPath();
        delete welcomeBridge;
        qDebug() << "Opening recent project:" << path;
        SplashScreen::showSplash(app);
        MainWindow window(path);
        window.show();
        return app.exec();
    }

    delete welcomeBridge;
    return 0;
}

static int sehSafeMain(int argc, char *argv[])
{
    return appMain(argc, argv);
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
    __try {
        return sehSafeMain(argc, argv);
    } __except(1) {
        return 1;
    }
#else
    return appMain(argc, argv);
#endif
}

#ifdef _WIN32
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    extern int __argc;
    extern char** __argv;
    return main(__argc, __argv);
}
#endif

#include "main.moc"
