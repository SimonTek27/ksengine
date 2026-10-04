#include "ModuleGuiBase.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QMainWindow>
#include <QStandardPaths>
#include <QStyle>
#include <QDateTime>
#include <QApplication>
#include <QClipboard>
#include <QScrollBar>

ModuleGuiBase::ModuleGuiBase(QWidget* parent)
    : EditorModule(parent)
    , m_centralWidget(nullptr)
    , m_mainLayout(nullptr)
    , m_mainToolbar(nullptr)
    , m_logOutput(nullptr)
    , m_dockWidget(nullptr)
    , m_uiBuilt(false)
    , m_currentProjectPath("")
{
    setObjectName(moduleId() + "Module");
}

bool ModuleGuiBase::initialize() {
    if (m_uiBuilt) return true;
    
    m_centralWidget = new QWidget(this);
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(4);
    
    m_mainToolbar = createToolBar(moduleName());
    m_mainLayout->addWidget(m_mainToolbar);
    
    buildUI();
    
    m_logOutput = createLogOutput();
    m_mainLayout->addWidget(m_logOutput);
    
    QVBoxLayout* selfLayout = new QVBoxLayout(this);
    selfLayout->setContentsMargins(0, 0, 0, 0);
    selfLayout->addWidget(m_centralWidget);
    setLayout(selfLayout);
    
    m_uiBuilt = true;
    LOG_INFO(moduleId().toLatin1().constData(), QString("%1 module initialized").arg(moduleName()));
    return true;
}

void ModuleGuiBase::shutdown() {
    if (!m_uiBuilt) return;
    m_uiBuilt = false;
    LOG_INFO(moduleId().toLatin1().constData(), QString("%1 module shutdown").arg(moduleName()));
}

QDockWidget* ModuleGuiBase::getOrCreateDockWidget(QMainWindow* mainWindow) {
    if (!m_dockWidget) {
        setupDockWidget(moduleName());
        mainWindow->addDockWidget(Qt::RightDockWidgetArea, m_dockWidget);
    }
    return m_dockWidget;
}

void ModuleGuiBase::newProject(const QString& name, const QString& path) {
    m_currentProjectPath = path;
    log(QString("New project: %1 at %2").arg(name, path));
}

void ModuleGuiBase::openProject(const QString& projectPath) {
    m_currentProjectPath = projectPath;
    log(QString("Opened project: %1").arg(projectPath));
}

void ModuleGuiBase::saveProject(const QString& path) {
    QString savePath = path.isEmpty() ? m_currentProjectPath : path;
    if (savePath.isEmpty()) {
        logError("No project path set for saving");
        return;
    }
    QJsonObject data = serializeProject();
    QFile file(savePath + "/" + moduleId() + ".json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(data).toJson());
        log(QString("Project saved to %1").arg(savePath));
    }
}

void ModuleGuiBase::saveProjectAs(const QString& path) {
    saveProject(path);
}

QJsonObject ModuleGuiBase::serializeProject() const {
    QJsonObject data;
    data["moduleId"] = moduleId();
    data["projectPath"] = m_currentProjectPath;
    return data;
}

void ModuleGuiBase::deserializeProject(const QJsonObject& data) {
    m_currentProjectPath = data["projectPath"].toString();
}

void ModuleGuiBase::onActivation() {
    log(QString("%1 activated").arg(moduleName()));
}

void ModuleGuiBase::onDeactivation() {
    log(QString("%1 deactivated").arg(moduleName()));
}

void ModuleGuiBase::setupDockWidget(const QString& title, Qt::DockWidgetAreas areas) {
    m_dockWidget = new QDockWidget(title, nullptr);
    m_dockWidget->setObjectName(moduleId() + "Dock");
    m_dockWidget->setAllowedAreas(areas);
    m_dockWidget->setWidget(this);
    m_dockWidget->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
}

QToolBar* ModuleGuiBase::createToolBar(const QString& name) {
    QToolBar* toolbar = new QToolBar(name, this);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setStyleSheet(
        "QToolBar { background: #2d2d2d; border: none; border-bottom: 1px solid #3a3a3a; padding: 2px; spacing: 2px; }"
        "QToolButton { background: #3a3a3a; color: #ffffff; border: 1px solid #4a4a4a; padding: 4px 8px; border-radius: 3px; }"
        "QToolButton:hover { background: #4a4a4a; }"
        "QToolButton:pressed { background: #5a5a5a; }"
        "QToolButton:checked { background: #3a5a8a; border-color: #4a6a9a; }"
    );
    return toolbar;
}

QGroupBox* ModuleGuiBase::createGroupBox(const QString& title) {
    QGroupBox* group = new QGroupBox(title, this);
    group->setStyleSheet(
        "QGroupBox { background: #252525; border: 1px solid #3a3a3a; border-radius: 3px; margin-top: 12px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #cccccc; font-size: 11px; font-weight: bold; }"
    );
    return group;
}

QAction* ModuleGuiBase::addToolAction(QToolBar* toolbar, const QString& text, const QString& tooltip, const QKeySequence& shortcut) {
    QAction* action = toolbar->addAction(text);
    if (!tooltip.isEmpty()) action->setToolTip(tooltip);
    if (!shortcut.isEmpty()) action->setShortcut(shortcut);
    return action;
}

QSplitter* ModuleGuiBase::createSplitter(Qt::Orientation orientation) {
    QSplitter* splitter = new QSplitter(orientation, this);
    splitter->setStyleSheet("QSplitter::handle { background-color: #3a3a3a; width: 4px; height: 4px; }");
    splitter->setHandleWidth(4);
    return splitter;
}

QTreeWidget* ModuleGuiBase::createTreeWidget(const QStringList& headers) {
    QTreeWidget* tree = new QTreeWidget(this);
    tree->setHeaderLabels(headers);
    tree->setAlternatingRowColors(true);
    tree->setStyleSheet(
        "QTreeWidget { background: #1e1e1e; color: #dddddd; border: 1px solid #3a3a3a; font-size: 11px; }"
        "QTreeWidget::item { padding: 4px; }"
        "QTreeWidget::item:selected { background: #3a5a8a; }"
        "QHeaderView::section { background: #2d2d2d; color: #dddddd; padding: 4px; border: 1px solid #3a3a3a; }"
    );
    return tree;
}

QListWidget* ModuleGuiBase::createListWidget() {
    QListWidget* list = new QListWidget(this);
    list->setAlternatingRowColors(true);
    list->setStyleSheet(
        "QListWidget { background: #1e1e1e; color: #dddddd; border: 1px solid #3a3a3a; font-size: 11px; }"
        "QListWidget::item { padding: 4px; }"
        "QListWidget::item:selected { background: #3a5a8a; }"
    );
    return list;
}

QComboBox* ModuleGuiBase::createComboBox(const QStringList& items) {
    QComboBox* combo = new QComboBox(this);
    combo->addItems(items);
    combo->setStyleSheet(
        "QComboBox { background: #2d2d2d; color: #ffffff; border: 1px solid #4a4a4a; padding: 4px 8px; font-size: 11px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #2d2d2d; color: #ffffff; selection-background-color: #3a5a8a; }"
    );
    return combo;
}

QSpinBox* ModuleGuiBase::createSpinBox(int min, int max, int value, const QString& suffix) {
    QSpinBox* spin = new QSpinBox(this);
    spin->setRange(min, max);
    spin->setValue(value);
    if (!suffix.isEmpty()) spin->setSuffix(suffix);
    spin->setStyleSheet(
        "QSpinBox { background: #2d2d2d; color: #ffffff; border: 1px solid #4a4a4a; padding: 4px; font-size: 11px; }"
        "QSpinBox::up-button, QSpinBox::down-button { background: #3a3a3a; border: none; width: 16px; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: #4a4a4a; }"
    );
    return spin;
}

QDoubleSpinBox* ModuleGuiBase::createDoubleSpinBox(double min, double max, double value, int decimals, const QString& suffix) {
    QDoubleSpinBox* spin = new QDoubleSpinBox(this);
    spin->setRange(min, max);
    spin->setValue(value);
    spin->setDecimals(decimals);
    if (!suffix.isEmpty()) spin->setSuffix(suffix);
    spin->setStyleSheet(
        "QDoubleSpinBox { background: #2d2d2d; color: #ffffff; border: 1px solid #4a4a4a; padding: 4px; font-size: 11px; }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { background: #3a3a3a; border: none; width: 16px; }"
        "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background: #4a4a4a; }"
    );
    return spin;
}

QCheckBox* ModuleGuiBase::createCheckBox(const QString& text, bool checked) {
    QCheckBox* check = new QCheckBox(text, this);
    check->setChecked(checked);
    check->setStyleSheet(
        "QCheckBox { color: #dddddd; font-size: 11px; spacing: 6px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
        "QCheckBox::indicator:unchecked { background: #2d2d2d; border: 1px solid #4a4a4a; }"
        "QCheckBox::indicator:checked { background: #3a5a8a; border: 1px solid #4a6a9a; }"
    );
    return check;
}

QPushButton* ModuleGuiBase::createButton(const QString& text, const QString& style) {
    QPushButton* btn = new QPushButton(text, this);
    if (style.isEmpty()) {
        btn->setStyleSheet(
            "QPushButton { background: #3a5a8a; color: #ffffff; border: 1px solid #4a6a9a; padding: 6px 12px; font-size: 11px; border-radius: 3px; }"
            "QPushButton:hover { background: #4a6a9a; }"
            "QPushButton:pressed { background: #5a7a9a; }"
        );
    } else {
        btn->setStyleSheet(style);
    }
    return btn;
}

QTextEdit* ModuleGuiBase::createLogOutput(int maxHeight) {
    QTextEdit* log = new QTextEdit(this);
    log->setReadOnly(true);
    log->setMaximumHeight(maxHeight);
    log->setStyleSheet(
        "QTextEdit { background: #1a1a1a; color: #c8c8c8; font-family: Consolas, 'Courier New', monospace; font-size: 9pt; border: 1px solid #3a3a3a; }"
    );
    return log;
}

QLabel* ModuleGuiBase::createLabel(const QString& text, const QString& style) {
    QLabel* label = new QLabel(text, this);
    if (style.isEmpty()) {
        label->setStyleSheet("color: #aaaaaa; font-size: 11px;");
    } else {
        label->setStyleSheet(style);
    }
    return label;
}

void ModuleGuiBase::log(const QString& msg, const QString& level) {
    if (!m_logOutput) return;
    
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString color = "#c8c8c8";
    QString prefix = "[INFO]";
    
    if (level == "error") { color = "#ff6b6b"; prefix = "[ERROR]"; }
    else if (level == "warning") { color = "#ffcc66"; prefix = "[WARN]"; }
    else if (level == "success") { color = "#6bff6b"; prefix = "[OK]"; }
    
    QString formatted = QString("<span style='color:#888888'>[%1]</span> <span style='color:%2'>%3</span> %4")
        .arg(timestamp, color, prefix, msg.toHtmlEscaped());
    
    m_logOutput->append(formatted);
    
    QScrollBar* bar = m_logOutput->verticalScrollBar();
    bar->setValue(bar->maximum());
    
    if (level == "error") {
        LOG_ERROR(moduleId().toLatin1().constData(), msg);
    } else if (level == "warning") {
        LOG_WARNING(moduleId().toLatin1().constData(), msg);
    } else {
        LOG_INFO(moduleId().toLatin1().constData(), msg);
    }
}

QString ModuleGuiBase::selectFile(const QString& caption, const QString& filter, const QString& dir) {
    QString startDir = dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : dir;
    return QFileDialog::getOpenFileName(this, caption, startDir, filter);
}

QString ModuleGuiBase::selectDirectory(const QString& caption, const QString& dir) {
    QString startDir = dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : dir;
    return QFileDialog::getExistingDirectory(this, caption, startDir);
}

QStringList ModuleGuiBase::selectFiles(const QString& caption, const QString& filter, const QString& dir) {
    QString startDir = dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : dir;
    return QFileDialog::getOpenFileNames(this, caption, startDir, filter);
}

bool ModuleGuiBase::confirmAction(const QString& title, const QString& text) {
    return QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}

void ModuleGuiBase::saveSettings(const QString& key, const QVariant& value) {
    QSettings settings;
    settings.setValue("sdk/kseditor/modules/" + moduleId() + "/" + key, value);
}

QVariant ModuleGuiBase::loadSetting(const QString& key, const QVariant& defaultValue) {
    QSettings settings;
    return settings.value("sdk/kseditor/modules/" + moduleId() + "/" + key, defaultValue);
}

