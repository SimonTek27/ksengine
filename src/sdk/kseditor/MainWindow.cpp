#include "MainWindow.h"
#include "engine/sys/ModuleManager.h"
#include "sys/SettingsManager.h"
#include "sdk/kseditor/engine/sys/PluginManager.h"
#include "tools/TemplateManager.h"
#include "tools/FileDiffEngine.h"
#include "tools/CrashRecovery.h"
#include "help/HelpSystem.h"
#include "help/HelpBrowser.h"
#include "sdk/kseditor/engine/assets/ProjectBuilder.h"
#include "vcs/GitStatusWidget.h"
#include "../resources/ui/CustomTitleBar.h"
#include "../resources/ui/FileTreeWidget.h"
#include "../resources/ui/ProjectSearchWidget.h"
#include "../resources/ui/TerminalWidget.h"
#include "../resources/ui/RibbonUI.h"

#include <QApplication>
#include <QScreen>
#include <QFileDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QKeySequence>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QSettings>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

// ============================================================================
// Constructor
// ============================================================================
MainWindow::MainWindow(const QString& projectPath, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(Constants::APP_NAME);
    setMinimumSize(Constants::MIN_WINDOW_SIZE);
    resize(Constants::DEFAULT_WINDOW_SIZE);
    setAcceptDrops(true);

    m_moduleManager = new ModuleManager(this);
    m_settings = new SettingsManager(this);
    m_customTitleBar = new CustomTitleBar(this);

    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(50);

    m_projectBuilder = new ProjectBuilder(this);

    m_templateManager = ks::TemplateManager::instance();
    m_diffEngine = ks::FileComparisonEngine::instance();
    m_helpBrowser = new ks::HelpBrowser(this);
    m_crashRecovery = new ks::CrashRecovery(this);
    connect(m_crashRecovery, &ks::CrashRecovery::recoveryNeeded,
            this, &MainWindow::showRecoveryDialog);

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setInterval(5 * 60 * 1000);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::performAutoSave);

    m_sessionRecoveryTimer = new QTimer(this);
    m_sessionRecoveryTimer->setInterval(10 * 60 * 1000);
    connect(m_sessionRecoveryTimer, &QTimer::timeout, this, &MainWindow::saveSessionBackup);

    setupUI();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupDockWidgets();
    setupConnections();

    m_autoSaveTimer->start();
    m_sessionRecoveryTimer->start();
    m_crashRecovery->startSession();

    if (!projectPath.isEmpty() && QDir(projectPath).exists()) {
        loadProjectFile(projectPath);
    }

    emit projectOpened(m_currentProjectPath);
}

// ============================================================================
// Destructor
// ============================================================================
MainWindow::~MainWindow()
{
    m_autoSaveTimer->stop();
    m_sessionRecoveryTimer->stop();
    m_crashRecovery->endSession();
    saveSessionBackup();
}

// ============================================================================
// setupUI
// ============================================================================
void MainWindow::setupUI()
{
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setCentralWidget(central);
}

// ============================================================================
// setupMenuBar
// ============================================================================
void MainWindow::setupMenuBar()
{
    m_menuBar = menuBar();
    setupFileMenu();
    setupEditMenu();
    setupViewMenu();
    setupModulesMenu();
    setupSettingsMenu();
    setupToolsMenu();
    setupHelpMenu();
}

void MainWindow::setupCustomTitleBar()
{
    if (m_customTitleBar) {
        connect(m_customTitleBar, &CustomTitleBar::minimizeRequested, this, &QWidget::showMinimized);
        connect(m_customTitleBar, &CustomTitleBar::maximizeRequested, this, [this]() {
            isMaximized() ? showNormal() : showMaximized();
        });
        connect(m_customTitleBar, &CustomTitleBar::closeRequested, this, &QWidget::close);
    }
}

void MainWindow::setupFileMenu()
{
    auto* fileMenu = m_menuBar->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New Project"), this, &MainWindow::newProject, QKeySequence::New);
    fileMenu->addAction(tr("&Open Project..."), this, &MainWindow::openProject, QKeySequence::Open);
    fileMenu->addSeparator();
    m_saveAction = fileMenu->addAction(tr("&Save"), this, &MainWindow::saveProject, QKeySequence::Save);
    m_saveAsAction = fileMenu->addAction(tr("Save &As..."), this, &MainWindow::saveProjectAs, QKeySequence("Ctrl+Shift+S"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Close Project"), this, &MainWindow::closeProject, QKeySequence::Close);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), qApp, &QApplication::quit, QKeySequence::Quit);

    auto* recentMenu = fileMenu->addMenu(tr("Recent &Projects"));
    for (int i = 0; i < Constants::MAX_RECENT_PROJECTS; ++i) {
        auto* action = recentMenu->addAction(tr("Project &%1").arg(i + 1));
        action->setVisible(false);
        connect(action, &QAction::triggered, this, [this, i]() {
            QSettings settings;
            QStringList recent = settings.value("recentProjects").toStringList();
            if (i < recent.size()) openRecentProject(recent[i]);
        });
        m_recentProjectsActions.append(action);
    }
    updateRecentProjectsMenu();
}

void MainWindow::setupEditMenu()
{
    auto* editMenu = m_menuBar->addMenu(tr("&Edit"));
    m_undoAction = editMenu->addAction(tr("&Undo"), this, &MainWindow::undo, QKeySequence::Undo);
    m_redoAction = editMenu->addAction(tr("&Redo"), this, &MainWindow::redo, QKeySequence::Redo);
    editMenu->addSeparator();
    m_cutAction = editMenu->addAction(tr("Cu&t"), this, &MainWindow::cut, QKeySequence::Cut);
    m_copyAction = editMenu->addAction(tr("&Copy"), this, &MainWindow::copy, QKeySequence::Copy);
    m_pasteAction = editMenu->addAction(tr("&Paste"), this, &MainWindow::paste, QKeySequence::Paste);
    m_deleteAction = editMenu->addAction(tr("&Delete"), this, &MainWindow::deleteSelected, QKeySequence::Delete);
}

void MainWindow::setupViewMenu()
{
    auto* viewMenu = m_menuBar->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Fullscreen"), this, &MainWindow::toggleFullscreen, QKeySequence::FullScreen);
    viewMenu->addAction(tr("&Sidebar"), this, &MainWindow::toggleSidebar, QKeySequence("Ctrl+Shift+S"));
    viewMenu->addAction(tr("Status &Bar"), this, &MainWindow::toggleStatusBar);
    viewMenu->addAction(tr("&Properties"), this, &MainWindow::toggleProperties);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Reset Layout"), this, &MainWindow::resetLayout);
}

void MainWindow::setupModulesMenu()
{
    auto* modulesMenu = m_menuBar->addMenu(tr("&Modules"));
    if (m_moduleManager) {
        for (int i = 0; i < m_moduleManager->moduleCount(); ++i) {
            QString name = m_moduleManager->moduleName(i);
            auto* action = modulesMenu->addAction(name, this, [this, i]() {
                switchToModule(i);
            });
            action->setCheckable(true);
        }
    }
}

void MainWindow::setupSettingsMenu()
{
    auto* settingsMenu = m_menuBar->addMenu(tr("&Settings"));
    settingsMenu->addAction(tr("&Preferences..."), this, &MainWindow::showSettings);
}

void MainWindow::setupToolsMenu()
{
    auto* toolsMenu = m_menuBar->addMenu(tr("&Tools"));
    toolsMenu->addAction(tr("&Template Browser..."), this, &MainWindow::showTemplateBrowser);
    toolsMenu->addAction(tr("&File Comparison..."), this, &MainWindow::compareProjectFiles);
    toolsMenu->addSeparator();
    toolsMenu->addAction(tr("Run in &Simulator"), this, &MainWindow::runInSimulator);
}

void MainWindow::setupHelpMenu()
{
    auto* helpMenu = m_menuBar->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&Documentation"), this, &MainWindow::showDocumentation);
    helpMenu->addAction(tr("&About"), this, &MainWindow::showAbout);
}

// ============================================================================
// setupToolBar
// ============================================================================
void MainWindow::setupToolBar()
{
    m_mainToolBar = addToolBar(tr("Main"));
    m_mainToolBar->setObjectName("MainToolBar");
    m_mainToolBar->addAction(m_undoAction);
    m_mainToolBar->addAction(m_redoAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_saveAction);

    m_moduleToolBar = addToolBar(tr("Modules"));
    m_moduleToolBar->setObjectName("ModuleToolBar");
}

// ============================================================================
// setupStatusBar
// ============================================================================
void MainWindow::setupStatusBar()
{
    m_statusBar = statusBar();
    m_statusLabel = new QLabel(tr("Ready"));
    m_statusBar->addWidget(m_statusLabel, 1);

    m_progressBar = new QProgressBar;
    m_progressBar->setMaximumWidth(Constants::PROGRESS_BAR_WIDTH);
    m_progressBar->setVisible(false);
    m_statusBar->addPermanentWidget(m_progressBar);

    m_navSpeedLabel = new QLabel;
    m_navSpeedLabel->setVisible(false);
    m_statusBar->addPermanentWidget(m_navSpeedLabel);

    m_snapStatusLabel = new QLabel;
    m_snapStatusLabel->setVisible(false);
    m_statusBar->addPermanentWidget(m_snapStatusLabel);

    m_placementStatusLabel = new QLabel;
    m_placementStatusLabel->setVisible(false);
    m_statusBar->addPermanentWidget(m_placementStatusLabel);

    m_simPathLabel = new QLabel(tr("Sim: (not set)"));
    m_statusBar->addPermanentWidget(m_simPathLabel);

    m_cspVersionLabel = new QLabel(tr("CSP: (unknown)"));
    m_statusBar->addPermanentWidget(m_cspVersionLabel);
}

// ============================================================================
// setupDockWidgets
// ============================================================================
void MainWindow::setupDockWidgets()
{
    m_sidebarDock = new QDockWidget(tr("File Tree"), this);
    m_sidebarDock->setObjectName("SidebarDock");
    m_sidebarDock->setMinimumWidth(Constants::MIN_SIDEBAR_WIDTH);
    m_fileTree = new FileTreeWidget(m_sidebarDock);
    m_sidebarDock->setWidget(m_fileTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDock);

    m_searchDock = new QDockWidget(tr("Search"), this);
    m_searchDock->setObjectName("SearchDock");
    m_projectSearch = new ProjectSearchWidget(m_searchDock);
    m_searchDock->setWidget(m_projectSearch);
    addDockWidget(Qt::BottomDockWidgetArea, m_searchDock);
    m_searchDock->hide();

    m_propertiesDock = new QDockWidget(tr("Properties"), this);
    m_propertiesDock->setObjectName("PropertiesDock");
    m_propertiesDock->setMinimumWidth(Constants::MIN_PROPERTIES_WIDTH);
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

    m_outputDock = new QDockWidget(tr("Output"), this);
    m_outputDock->setObjectName("OutputDock");
    m_outputDock->setMinimumHeight(Constants::MIN_OUTPUT_HEIGHT);
    addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);

    m_terminalDock = new QDockWidget(tr("Terminal"), this);
    m_terminalDock->setObjectName("TerminalDock");
    m_terminal = new TerminalWidget(m_terminalDock);
    m_terminalDock->setWidget(m_terminal);
    addDockWidget(Qt::BottomDockWidgetArea, m_terminalDock);
    m_terminalDock->hide();

    m_gitDock = new QDockWidget(tr("Git"), this);
    m_gitDock->setObjectName("GitDock");
    m_gitStatus = new GitStatusWidget(m_gitDock);
    m_gitDock->setWidget(m_gitStatus);
    addDockWidget(Qt::LeftDockWidgetArea, m_gitDock);
    m_gitDock->hide();

    m_scriptConsoleDock = createScriptConsoleDock();
    addDockWidget(Qt::BottomDockWidgetArea, m_scriptConsoleDock);
    m_scriptConsoleDock->hide();
}

// ============================================================================
// setupConnections
// ============================================================================
void MainWindow::setupConnections()
{
    if (m_moduleManager) {
        connect(m_moduleManager, &ModuleManager::moduleChanged, this, &MainWindow::onModuleChanged);
    }
    if (m_projectBuilder) {
        connect(m_projectBuilder, &ProjectBuilder::buildComplete, this, &MainWindow::onBuildComplete);
    }
    if (m_fileTree) {
        connect(m_fileTree, &FileTreeWidget::fileActivated, this, &MainWindow::onFileTreeActivated);
    }
    if (m_projectSearch) {
        connect(m_projectSearch, &ProjectSearchWidget::resultActivated, this, &MainWindow::onSearchResultActivated);
    }

    auto* undoShortcut = new QShortcut(QKeySequence("Ctrl+Z"), this);
    connect(undoShortcut, &QShortcut::activated, this, &MainWindow::undo);
    auto* redoShortcut = new QShortcut(QKeySequence("Ctrl+Y"), this);
    connect(redoShortcut, &QShortcut::activated, this, &MainWindow::redo);
}

void MainWindow::setupRibbon() {}
void MainWindow::setupCarTab() {}
void MainWindow::setupTrackTab() {}
void MainWindow::setupCharacterTab() {}
void MainWindow::setupShowroomTab() {}
void MainWindow::setupSoundTab() {}
void MainWindow::setupFontTab() {}
void MainWindow::setupPaintTab() {}
void MainWindow::applyWindowFrameTheme(const QString&) {}

// ============================================================================
// Script console
// ============================================================================
QDockWidget* MainWindow::createScriptConsoleDock()
{
    auto* dock = new QDockWidget(tr("Script Console"), this);
    dock->setObjectName("ScriptConsoleDock");

    auto* widget = new QWidget(dock);
    auto* layout = new QVBoxLayout(widget);

    m_scriptOutput = new QPlainTextEdit(widget);
    m_scriptOutput->setReadOnly(true);
    m_scriptOutput->setMaximumHeight(150);
    layout->addWidget(m_scriptOutput);

    m_scriptInput = new QLineEdit(widget);
    connect(m_scriptInput, &QLineEdit::returnPressed, this, &MainWindow::executeScript);
    layout->addWidget(m_scriptInput);

    dock->setWidget(widget);
    return dock;
}

QPixmap MainWindow::loadSvgIcon(const QString& path, const QSize& size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QIcon icon(path);
    if (!icon.isNull()) {
        pixmap = icon.pixmap(size);
    }
    return pixmap;
}

// ============================================================================
// Module switching
// ============================================================================
bool MainWindow::switchToModule(int index)
{
    if (!m_moduleManager) return false;
    if (index < 0 || index >= m_moduleManager->moduleCount()) return false;
    m_moduleManager->setCurrentModule(index);
    return true;
}

bool MainWindow::switchToModule(const QString& moduleName)
{
    if (!m_moduleManager) return false;
    for (int i = 0; i < m_moduleManager->moduleCount(); ++i) {
        if (m_moduleManager->moduleName(i) == moduleName) {
            return switchToModule(i);
        }
    }
    return false;
}

void MainWindow::onModuleChanged(int index)
{
    Q_UNUSED(index);
    updateWindowTitle();
    emit moduleChanged(index);
}

// ============================================================================
// Project management
// ============================================================================
void MainWindow::newProject()
{
    if (!promptForUnsavedChanges()) return;
    QString path = QFileDialog::getSaveFileName(this, tr("New Project"), QString(),
        tr("ksEditor Project (*.ksep);;All Files (*)"));
    if (path.isEmpty()) return;

    QString name = QFileInfo(path).completeBaseName();
    if (createProjectFile(path, name)) {
        loadProjectFile(path);
    }
}

void MainWindow::openProject()
{
    if (!promptForUnsavedChanges()) return;
    QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(),
        tr("ksEditor Project (*.ksep);;All Files (*)"));
    if (!path.isEmpty()) {
        loadProjectFile(path);
    }
}

void MainWindow::openRecentProject(const QString& path)
{
    if (!promptForUnsavedChanges()) return;
    if (QDir(path).exists()) {
        loadProjectFile(path);
    } else {
        QMessageBox::warning(this, tr("Project Not Found"),
            tr("The project directory no longer exists:\n%1").arg(path));
    }
}

bool MainWindow::saveProject()
{
    if (m_currentProjectPath.isEmpty()) {
        return saveProjectAs();
    }
    return saveProjectFile(m_currentProjectPath);
}

bool MainWindow::saveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project"), m_currentProjectPath,
        tr("ksEditor Project (*.ksep);;All Files (*)"));
    if (path.isEmpty()) return false;
    if (saveProjectFile(path)) {
        m_currentProjectPath = path;
        addToRecentProjects(path);
        updateWindowTitle();
        return true;
    }
    return false;
}

bool MainWindow::closeProject()
{
    if (!promptForUnsavedChanges()) return false;
    m_currentProjectPath.clear();
    updateWindowTitle();
    emit projectClosed();
    return true;
}

bool MainWindow::createProjectFile(const QString& path, const QString& name)
{
    QJsonObject root;
    root["name"] = name;
    root["path"] = QFileInfo(path).absolutePath();
    root["version"] = "1.0";
    root["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson());
    file.close();
    addToRecentProjects(path);
    updateWindowTitle();
    return true;
}

bool MainWindow::loadProjectFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (doc.isNull() || !doc.isObject()) return false;

    m_currentProjectPath = path;
    QString projectDir = doc.object()["path"].toString();
    if (projectDir.isEmpty()) projectDir = QFileInfo(path).absolutePath();

    if (m_fileTree) m_fileTree->setRootPath(projectDir);
    if (m_gitStatus) m_gitStatus->setRepoPath(projectDir);

    addToRecentProjects(path);
    updateWindowTitle();
    detectSimulator();
    detectCSPVersion();
    emit projectOpened(path);
    return true;
}

bool MainWindow::saveProjectFile(const QString& path)
{
    QJsonObject root;
    root["path"] = QFileInfo(path).absolutePath();
    root["version"] = "1.0";
    root["saved"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson());
    file.close();
    return true;
}

void MainWindow::addToRecentProjects(const QString& path)
{
    QSettings settings;
    QStringList recent = settings.value("recentProjects").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > Constants::MAX_RECENT_PROJECTS) recent.removeLast();
    settings.setValue("recentProjects", recent);
    updateRecentProjectsMenu();
}

bool MainWindow::promptForUnsavedChanges()
{
    return true;
}

// ============================================================================
// Actions
// ============================================================================
void MainWindow::createActions() {}
void MainWindow::updateActions() {}

void MainWindow::updateRecentProjectsMenu()
{
    QSettings settings;
    QStringList recent = settings.value("recentProjects").toStringList();
    for (int i = 0; i < m_recentProjectsActions.size(); ++i) {
        if (i < recent.size()) {
            m_recentProjectsActions[i]->setText(recent[i]);
            m_recentProjectsActions[i]->setVisible(true);
        } else {
            m_recentProjectsActions[i]->setVisible(false);
        }
    }
}

// ============================================================================
// Edit operations
// ============================================================================
void MainWindow::undo() { if (m_undoStack) m_undoStack->undo(); }
void MainWindow::redo() { if (m_undoStack) m_undoStack->redo(); }
void MainWindow::cut() {}
void MainWindow::copy() {}
void MainWindow::paste() {}
void MainWindow::deleteSelected() {}

// ============================================================================
// View operations
// ============================================================================
void MainWindow::toggleFullscreen()
{
    isFullScreen() ? showMaximized() : showFullScreen();
}

void MainWindow::toggleSidebar()
{
    if (m_sidebarDock) m_sidebarDock->setVisible(!m_sidebarDock->isVisible());
}

void MainWindow::toggleStatusBar()
{
    if (m_statusBar) m_statusBar->setVisible(!m_statusBar->isVisible());
}

void MainWindow::toggleProperties()
{
    if (m_propertiesDock) m_propertiesDock->setVisible(!m_propertiesDock->isVisible());
}

void MainWindow::resetLayout()
{
    if (m_sidebarDock) m_sidebarDock->show();
    if (m_propertiesDock) m_propertiesDock->show();
    if (m_outputDock) m_outputDock->show();
    if (m_searchDock) m_searchDock->hide();
    if (m_terminalDock) m_terminalDock->hide();
    if (m_gitDock) m_gitDock->hide();
    if (m_scriptConsoleDock) m_scriptConsoleDock->hide();
}

void MainWindow::toggleTerminal()
{
    if (m_terminalDock) m_terminalDock->setVisible(!m_terminalDock->isVisible());
}

void MainWindow::toggleGit()
{
    if (m_gitDock) m_gitDock->setVisible(!m_gitDock->isVisible());
}

void MainWindow::toggleScriptConsole()
{
    if (m_scriptConsoleDock) m_scriptConsoleDock->setVisible(!m_scriptConsoleDock->isVisible());
}

void MainWindow::executeScript()
{
    if (!m_scriptInput || !m_scriptOutput) return;
    QString code = m_scriptInput->text().trimmed();
    if (code.isEmpty()) return;

    m_scriptOutput->appendPlainText(">>> " + code);
    m_scriptInput->clear();

    if (!m_scriptEngine) {
        m_scriptEngine = new QJSEngine(this);
    }
    QJSValue result = m_scriptEngine->evaluate(code);
    if (result.isError()) {
        m_scriptOutput->appendPlainText("Error: " + result.toString());
    } else {
        m_scriptOutput->appendPlainText(result.toString());
    }
}

// ============================================================================
// Status bar
// ============================================================================
void MainWindow::setStatusMessage(const QString& message, int timeoutMs)
{
    if (!m_statusLabel) return;
    m_statusLabel->setText(message);
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this]() {
            m_statusLabel->setText(tr("Ready"));
        });
    }
}

void MainWindow::setProgress(int value, int maximum)
{
    if (!m_progressBar) return;
    m_progressBar->setMaximum(maximum);
    m_progressBar->setValue(value);
    m_progressBar->setVisible(value < maximum);
}

void MainWindow::clearProgress()
{
    if (m_progressBar) m_progressBar->setVisible(false);
}

void MainWindow::setStatusNavSpeed(const QString& text)
{
    if (m_navSpeedLabel) {
        m_navSpeedLabel->setText(text);
        m_navSpeedLabel->setVisible(!text.isEmpty());
    }
}

void MainWindow::setStatusSnap(const QString& text)
{
    if (m_snapStatusLabel) {
        m_snapStatusLabel->setText(text);
        m_snapStatusLabel->setVisible(!text.isEmpty());
    }
}

void MainWindow::setStatusPlacement(const QString& text)
{
    if (m_placementStatusLabel) {
        m_placementStatusLabel->setText(text);
        m_placementStatusLabel->setVisible(!text.isEmpty());
    }
}

// ============================================================================
// Simulator path
// ============================================================================
bool MainWindow::setSimPath(const QString& path)
{
    if (!QDir(path).exists()) return false;
    m_simPath = path;
    if (m_simPathLabel) m_simPathLabel->setText(tr("Sim: %1").arg(path));
    emit simPathChanged(path);
    return true;
}

void MainWindow::setCSPVersion(const QString& version)
{
    m_cspVersion = version;
    if (m_cspVersionLabel) m_cspVersionLabel->setText(tr("CSP: %1").arg(version));
    emit cspVersionChanged(version);
}

void MainWindow::detectSimulator()
{
    QString detected = detectSimulatorFromRegistry();
    if (!detected.isEmpty()) {
        setSimPath(detected);
        return;
    }
    QStringList defaults = getDefaultSimulatorPaths();
    for (const QString& p : defaults) {
        if (QDir(p).exists()) {
            setSimPath(p);
            return;
        }
    }
}

QString MainWindow::detectSimulatorFromRegistry() const
{
#ifdef _WIN32
    QSettings reg("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 244210", QSettings::NativeFormat);
    QString path = reg.value("InstallLocation").toString();
    if (!path.isEmpty() && QDir(path).exists()) return path;
#endif
    return {};
}

QStringList MainWindow::getDefaultSimulatorPaths() const
{
    return {
        "F:/SteamLibrary/steamapps/common/assettocorsa",
        "C:/Program Files (x86)/Steam/steamapps/common/assettocorsa",
        QDir::homePath() + "/Documents/Assetto Corsa"
    };
}

void MainWindow::detectCSPVersion()
{
    if (m_simPath.isEmpty()) return;
    QString cspDir = m_simPath + "/extension";
    if (QDir(cspDir).exists()) {
        setCSPVersion("detected");
    }
}

// ============================================================================
// Build
// ============================================================================
void MainWindow::buildProject()
{
    if (!m_projectBuilder || m_currentProjectPath.isEmpty()) return;
    setStatusMessage(tr("Building project..."));
    m_projectBuilder->build(QFileInfo(m_currentProjectPath).absolutePath(), m_simPath);
}

void MainWindow::onBuildProgress(int percent)
{
    setProgress(percent);
}

void MainWindow::onBuildComplete(bool success, const QString& message)
{
    clearProgress();
    setStatusMessage(success ? tr("Build succeeded") : tr("Build failed: %1").arg(message), 5000);
}

// ============================================================================
// Settings / Help
// ============================================================================
void MainWindow::showSettings() {}
void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About ksEditor"),
        tr("<h3>ksEditor %1</h3><p>Assetto Corsa content editor and driving simulator.</p>")
        .arg("1.16"));
}
void MainWindow::showDocumentation()
{
    if (m_helpBrowser) m_helpBrowser->show();
}

// ============================================================================
// Run
// ============================================================================
void MainWindow::runInSimulator()
{
    if (m_simPath.isEmpty()) {
        QMessageBox::warning(this, tr("Simulator Not Found"),
            tr("Assetto Corsa path not configured. Set it in Settings."));
        return;
    }
    QString acExe = m_simPath + "/acs.exe";
    if (QFileInfo::exists(acExe)) {
        QProcess::startDetached(acExe);
        setStatusMessage(tr("Launching acs.exe..."));
    } else {
        setStatusMessage(tr("acs.exe not found at: %1").arg(acExe));
    }
}

// ============================================================================
// Paint mode
// ============================================================================
void MainWindow::setPaintMode(bool enabled)
{
    m_paintMode = enabled;
    if (m_ribbonBar) {
        m_ribbonBar->setVisible(!enabled);
    }
    connectPaintTabButtons();
}

void MainWindow::connectPaintTabButtons() {}

// ============================================================================
// File tree / search callbacks
// ============================================================================
void MainWindow::onFileTreeActivated(const QString& filePath)
{
    Q_UNUSED(filePath);
}

void MainWindow::onSearchResultActivated(const QString& filePath, int lineNumber)
{
    Q_UNUSED(filePath);
    Q_UNUSED(lineNumber);
}

// ============================================================================
// Auto-save / session backup
// ============================================================================
void MainWindow::performAutoSave()
{
    if (m_currentProjectPath.isEmpty()) return;
    saveSessionBackup();
}

void MainWindow::saveSessionBackup()
{
    if (m_crashRecovery) {
        m_crashRecovery->saveSession();
    }
}

void MainWindow::showRecoveryDialog(const QVector<ks::CrashRecovery::Session>& sessions)
{
    if (sessions.isEmpty()) return;
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Recover Session"));
    msgBox.setText(tr("An unclean shutdown was detected. Recover previous session?"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    if (msgBox.exec() == QMessageBox::Yes) {
        for (const auto& session : sessions) {
            m_crashRecovery->recoverSession(session);
        }
    }
}

// ============================================================================
// Templates / Diff
// ============================================================================
void MainWindow::createProjectFromTemplate(const QString& templateId)
{
    Q_UNUSED(templateId);
}
void MainWindow::showTemplateBrowser() {}
void MainWindow::showFileDiff(const QString& filePath) { Q_UNUSED(filePath); }
void MainWindow::compareProjectFiles() {}
void MainWindow::saveWindowLayout() {}
void MainWindow::loadWindowLayout() {}
void MainWindow::resetWindowLayout() {}
void MainWindow::saveProjectLayout() {}
void MainWindow::loadProjectLayout() {}
void MainWindow::resetProjectLayout() {}

// ============================================================================
// Window title
// ============================================================================
void MainWindow::updateWindowTitle()
{
    QString title = Constants::APP_NAME;
    if (!m_currentProjectPath.isEmpty()) {
        title += " - " + QFileInfo(m_currentProjectPath).baseName();
    }
    if (m_paintMode) {
        title += " [Paint Mode]";
    }
    setWindowTitle(title);
}

// ============================================================================
// Events
// ============================================================================
void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!promptForUnsavedChanges()) {
        event->ignore();
        return;
    }
    m_autoSaveTimer->stop();
    m_sessionRecoveryTimer->stop();
    m_crashRecovery->endSession();
    saveSessionBackup();
    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::event(QEvent* event)
{
    return QMainWindow::event(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    QString path = urls.first().toLocalFile();
    if (path.endsWith(".ksep", Qt::CaseInsensitive)) {
        openRecentProject(path);
    }
}

// ============================================================================
// Language
// ============================================================================
void MainWindow::loadLanguage(const QString& langCode)
{
    Q_UNUSED(langCode);
}

void MainWindow::onLanguageChanged(const QString& langCode)
{
    Q_UNUSED(langCode);
}
