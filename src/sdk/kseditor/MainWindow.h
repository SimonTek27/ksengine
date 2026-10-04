#pragma once

#include <QMainWindow>
#include <QDockWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QActionGroup>
#include <QLabel>
#include <QProgressBar>
#include <QUndoStack>
#include <QPointer>
#include <QJSEngine>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QTimer>
#include <QTranslator>
#include <QLocale>

#include "../resources/ui/FileTreeWidget.h"
#include "../resources/ui/ProjectSearchWidget.h"
#include "../resources/ui/TerminalWidget.h"
#include "vcs/GitStatusWidget.h"
#include "modules/moduleManager/ModuleManager.h"
#include "sys/SettingsManager.h"
#include "../resources/ui/RibbonUI.h"
#include "tools/TemplateManager.h"
#include "tools/FileDiffEngine.h"
#include "tools/CrashRecovery.h"
#include "help/HelpSystem.h"
#include "help/HelpBrowser.h"

namespace Constants {
    inline const QString PROJECT_EXTENSION = ".ksep";
    inline const int MAX_RECENT_PROJECTS = 10;
    inline const QString APP_NAME = "ksEditor";
    inline const QSize DEFAULT_WINDOW_SIZE = {1400, 900};
    inline const QSize MIN_WINDOW_SIZE = {1024, 768};
    inline const int PROGRESS_BAR_WIDTH = 200;
    inline const int MIN_SIDEBAR_WIDTH = 250;
    inline const int MIN_PROPERTIES_WIDTH = 280;
    inline const int MIN_OUTPUT_HEIGHT = 150;
}

namespace ks { class CrashRecovery; }
class ProjectBuilder;
class MainWindowPrivate;

#include "../resources/ui/CustomTitleBar.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString& projectPath = QString(), QWidget* parent = nullptr);
    ~MainWindow() override;

public slots:
    void showRecoveryDialog(const QVector<ks::CrashRecovery::Session>& sessions);

    // Module access
    ModuleManager* moduleManager() const { return m_moduleManager; }
    SettingsManager* settings() const { return m_settings; }

    // Status bar
    void setStatusMessage(const QString& message, int timeoutMs = 0);
    void setProgress(int value, int maximum = 100);
    void clearProgress();
    void setStatusNavSpeed(const QString& text);
    void setStatusSnap(const QString& text);
    void setStatusPlacement(const QString& text);

    // Assetto Corsa path
    QString simPath() const { return m_simPath; }
    bool setSimPath(const QString& path);

    // CSP version
    QString cspVersion() const { return m_cspVersion; }
    void setCSPVersion(const QString& version);

    // Paint mode (PhotoGIMP-inspired)
    void setPaintMode(bool enabled);
    bool isPaintMode() const { return m_paintMode; }

    // Project management
    QString currentProjectPath() const { return m_currentProjectPath; }
    bool hasOpenProject() const { return !m_currentProjectPath.isEmpty(); }
    bool saveProject();
    bool saveProjectAs();

signals:
    void simPathChanged(const QString& path);
    void cspVersionChanged(const QString& version);
    void projectOpened(const QString& path);
    void projectClosed();
    void moduleChanged(int moduleIndex);

public slots:
    // File operations
    void newProject();
    void openProject();
    void openRecentProject(const QString& path);
    void buildProject();
    bool closeProject();

    // Module switching
    bool switchToModule(int index);
    bool switchToModule(const QString& moduleName);

    // Edit operations
    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void deleteSelected();

    // View operations
    void toggleFullscreen();
    void toggleSidebar();
    void toggleStatusBar();
    void toggleProperties();
    void resetLayout();

    // Terminal
    void toggleTerminal();

    // Git
    void toggleGit();

    // Script console
    void toggleScriptConsole();
    void executeScript();

    // Settings
    void showSettings();
    void showAbout();
    void showDocumentation();
    void runInSimulator();
    void runInAssettoCorsa() { runInSimulator(); }

    // Paint mode
    void connectPaintTabButtons();

    // UI customization
    void saveWindowLayout();
    void loadWindowLayout();
    void resetWindowLayout();
    
    // Template system
    void createProjectFromTemplate(const QString& templateId);
    void showTemplateBrowser();
    
    // File comparison
    void showFileDiff(const QString& filePath);
    void compareProjectFiles();
    
    // Templates (alias for save/load layout functionality)
    void saveProjectLayout();
    void loadProjectLayout();
    void resetProjectLayout();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void loadLanguage(const QString& langCode);
    void onLanguageChanged(const QString& langCode);

private:
    void updateWindowTitle();
    void onModuleChanged(int index);
    void onBuildProgress(int percent);
    void onBuildComplete(bool success, const QString& message);
    void performAutoSave();
    void saveSessionBackup();
    void onFileTreeActivated(const QString& filePath);
    void onSearchResultActivated(const QString& filePath, int lineNumber);

private:
    // UI Setup
    void setupUI();
    void setupMenuBar();
    void setupCustomTitleBar();
    void setupFileMenu();
    void setupEditMenu();
    void setupViewMenu();
    void setupModulesMenu();
    void setupSettingsMenu();
    void setupHelpMenu();
    void setupToolsMenu();
    void setupToolBar();
    void setupStatusBar();
    void setupDockWidgets();
    void setupConnections();
    void setupRibbon();
    void setupCarTab();
    void setupTrackTab();
    void setupCharacterTab();
    void setupShowroomTab();
    void setupSoundTab();
    void setupFontTab();
    void setupPaintTab();
    void applyWindowFrameTheme(const QString& themeKey);
    QDockWidget* createScriptConsoleDock();
    QPixmap loadSvgIcon(const QString& path, const QSize& size);

    // Actions management
    void createActions();
    void updateActions();
    void updateRecentProjectsMenu();

    // Project handling
    bool createProjectFile(const QString& path, const QString& name);
    bool loadProjectFile(const QString& path);
    bool saveProjectFile(const QString& path);
    void addToRecentProjects(const QString& path);
    bool promptForUnsavedChanges();

    // Path detection
    void detectSimulator();
    void detectCSPVersion();
    QString detectSimulatorFromRegistry() const;
    QStringList getDefaultSimulatorPaths() const;

    // Member variables
    ModuleManager* m_moduleManager = nullptr;
    SettingsManager* m_settings = nullptr;
    QPointer<QDialog> m_startupDialog;

    QUndoStack* m_undoStack = nullptr;

    QString m_simPath;
    QString m_cspVersion;
    QString m_currentProjectPath;

    // Translator for i18n
    QTranslator m_appTranslator;
    QTranslator m_qtTranslator;

    // Paint mode flag
    bool m_paintMode = false;

    // Ribbon UI
    ks::editor::RibbonBar* m_ribbonBar = nullptr;

    // Menu bar
    QMenuBar* m_menuBar = nullptr;

    // Builder (async)
    ProjectBuilder* m_projectBuilder = nullptr;

    // UI Components - owned by Qt parent system
    QToolBar* m_mainToolBar = nullptr;
    QToolBar* m_moduleToolBar = nullptr;
    QStatusBar* m_statusBar = nullptr;

    QLabel* m_simPathLabel = nullptr;
    QLabel* m_cspVersionLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_navSpeedLabel = nullptr;
    QLabel* m_snapStatusLabel = nullptr;
    QLabel* m_placementStatusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;

    // Dock widgets
    QDockWidget* m_sidebarDock = nullptr;
    FileTreeWidget* m_fileTree = nullptr;
    QDockWidget* m_searchDock = nullptr;
    ProjectSearchWidget* m_projectSearch = nullptr;
    QDockWidget* m_propertiesDock = nullptr;
    QDockWidget* m_outputDock = nullptr;
    QDockWidget* m_scriptConsoleDock = nullptr;
    QDockWidget* m_terminalDock = nullptr;
    TerminalWidget* m_terminal = nullptr;
    QDockWidget* m_gitDock = nullptr;
    GitStatusWidget* m_gitStatus = nullptr;

    // Script console
    QJSEngine* m_scriptEngine = nullptr;
    QPlainTextEdit* m_scriptOutput = nullptr;
    QLineEdit* m_scriptInput = nullptr;

    // Actions
    QActionGroup* m_moduleGroup = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_cutAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_buildAction = nullptr;
    QList<QAction*> m_recentProjectsActions;

    QTimer* m_autoSaveTimer = nullptr;
    QTimer* m_sessionRecoveryTimer = nullptr;
    ks::CrashRecovery* m_crashRecovery = nullptr;
    ks::TemplateManager* m_templateManager = nullptr;
    ks::FileComparisonEngine* m_diffEngine = nullptr;
    ks::HelpBrowser* m_helpBrowser = nullptr;
    CustomTitleBar* m_customTitleBar = nullptr;

    // Paint tab ribbon buttons (connected in setPaintMode)
    ks::editor::RibbonButton* m_paintBrushBtn = nullptr;
    ks::editor::RibbonButton* m_paintPencilBtn = nullptr;
    ks::editor::RibbonButton* m_paintEraserBtn = nullptr;
    ks::editor::RibbonButton* m_paintAirbrushBtn = nullptr;
    ks::editor::RibbonButton* m_paintFillBtn = nullptr;
    ks::editor::RibbonButton* m_paintGradientBtn = nullptr;
    ks::editor::RibbonButton* m_paintCloneBtn = nullptr;
    ks::editor::RibbonButton* m_paintFgColorBtn = nullptr;
    ks::editor::RibbonButton* m_paintBgColorBtn = nullptr;
    ks::editor::RibbonButton* m_paintSwapColorsBtn = nullptr;
    ks::editor::RibbonButton* m_paintDefaultColorsBtn = nullptr;
    ks::editor::RibbonButton* m_paintRectSelectBtn = nullptr;
    ks::editor::RibbonButton* m_paintEllipseSelectBtn = nullptr;
    ks::editor::RibbonButton* m_paintLassoBtn = nullptr;
    ks::editor::RibbonButton* m_paintMagicWandBtn = nullptr;
    ks::editor::RibbonButton* m_paintDeselectBtn = nullptr;
    ks::editor::RibbonButton* m_paintInvertBtn = nullptr;
    ks::editor::RibbonButton* m_paintZoomInBtn = nullptr;
    ks::editor::RibbonButton* m_paintZoomOutBtn = nullptr;
    ks::editor::RibbonButton* m_paintFitBtn = nullptr;
    ks::editor::RibbonButton* m_paintZoomToolBtn = nullptr;
    ks::editor::RibbonButton* m_paintFullscreenBtn = nullptr;
    ks::editor::RibbonButton* m_paintRulersBtn = nullptr;
    ks::editor::RibbonButton* m_paintNewLayerBtn = nullptr;
    ks::editor::RibbonButton* m_paintDupLayerBtn = nullptr;
    ks::editor::RibbonButton* m_paintMergeLayerBtn = nullptr;
    ks::editor::RibbonButton* m_paintFlattenBtn = nullptr;
};
