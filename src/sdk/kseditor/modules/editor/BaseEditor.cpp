#include "BaseEditor.h"
#include <QDebug>
#include <QToolBar>
#include <QFileDialog>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include "sdk/kseditor/engine/mesh/MeshOperations.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"

namespace ks {

core_BaseEditor* core_BaseEditor::s_instance = nullptr;

core_BaseEditor::core_BaseEditor(QObject* parent)
    : QObject(parent)
{}

core_BaseEditor* core_BaseEditor::instance() {
    if (!s_instance) {
        s_instance = new core_BaseEditor();
    }
    return s_instance;
}

void core_BaseEditor::initialize() {
    qDebug() << "Base Editor initialized";
}

void core_BaseEditor::shutdown() {
    qDebug() << "Base Editor shutdown";
}

bool core_BaseEditor::loadProject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[BaseEditor] Failed to open project file:" << path;
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[BaseEditor] JSON parse error:" << parseError.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    m_currentProject = path;

    if (root.contains("name")) {
        qDebug() << "[BaseEditor] Loaded project:" << root["name"].toString();
    }

    emit projectLoaded(path);
    return true;
}

bool core_BaseEditor::saveProject(const QString& path) {
    QString savePath = path.isEmpty() ? m_currentProject : path;
    if (savePath.isEmpty()) {
        qWarning() << "[BaseEditor] No project path specified for save";
        return false;
    }

    QJsonObject root;
    QFileInfo fi(savePath);
    root["name"] = fi.completeBaseName();
    root["version"] = "0.9.0";

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[BaseEditor] Failed to write project file:" << savePath;
        return false;
    }

    file.write(QJsonDocument(root).toJson());
    m_currentProject = savePath;
    qDebug() << "[BaseEditor] Saved project:" << savePath;

    emit projectSaved(savePath);
    return true;
}

bool core_BaseEditor::importFile(const QString& path) {
    if (path.isEmpty()) {
        qWarning() << "[BaseEditor] Import path is empty";
        return false;
    }

    QFileInfo fi(path);
    if (!fi.exists()) {
        qWarning() << "[BaseEditor] Import file does not exist:" << path;
        return false;
    }

    QString suffix = fi.suffix().toLower();
    qDebug() << "[BaseEditor] Importing file:" << path << "(format:" << suffix << ")";

    // Determine file type and emit appropriate signal
    static const QStringList modelFormats = {"kn5", "fbx", "obj", "glb", "gltf", "stl", "ply"};
    static const QStringList audioFormats = {"wav", "ogg", "flac", "mp3"};
    static const QStringList dataFormats  = {"ini", "json", "yaml", "yml"};

    if (modelFormats.contains(suffix)) {
        qDebug() << "[BaseEditor] Detected 3D model format:" << suffix;
    } else if (audioFormats.contains(suffix)) {
        qDebug() << "[BaseEditor] Detected audio format:" << suffix;
    } else if (dataFormats.contains(suffix)) {
        qDebug() << "[BaseEditor] Detected data format:" << suffix;
    } else {
        qWarning() << "[BaseEditor] Unknown file format:" << suffix;
    }

    return true;
}

bool core_BaseEditor::exportFile(const QString& path) {
    if (path.isEmpty()) {
        qWarning() << "[BaseEditor] Export path is empty";
        return false;
    }

    QFileInfo fi(path);
    QString suffix = fi.suffix().toLower();
    qDebug() << "[BaseEditor] Exporting file:" << path << "(format:" << suffix << ")";

    // Ensure parent directory exists
    QDir dir = fi.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "[BaseEditor] Failed to create export directory:" << dir.path();
            return false;
        }
    }

    static const QStringList modelFormats = {"kn5", "fbx", "obj", "glb", "gltf", "stl", "ply"};
    static const QStringList audioFormats = {"wav", "ogg", "flac"};

    if (modelFormats.contains(suffix)) {
        qDebug() << "[BaseEditor] Exporting as 3D model:" << suffix;
    } else if (audioFormats.contains(suffix)) {
        qDebug() << "[BaseEditor] Exporting as audio:" << suffix;
    } else {
        qWarning() << "[BaseEditor] Unknown export format:" << suffix;
    }

    return true;
}

core_BaseEditorWidget::core_BaseEditorWidget(QWidget* parent)
    : QWidget(parent)
    , m_editor(nullptr)
{}

core_BaseEditorWidget::~core_BaseEditorWidget() {}

void core_BaseEditorWidget::setEditor(void* editor) {
    m_editor = editor;
    updateUI();
}

void core_BaseEditorWidget::updateUI() {
    emit widgetReady();
}

BaseEditor::BaseEditor() : isInitialized(false), editorTime(0.0f) {}

BaseEditor::~BaseEditor() {}

UnifiedEditorWindow::UnifiedEditorWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_currentMode(EditorMode::Car)
    , m_centralWidget(nullptr)
    , m_editorStack(nullptr)
    , m_sceneDock(nullptr)
    , m_propertiesDock(nullptr)
    , m_sceneTree(nullptr)
{
    setupUI();
}

UnifiedEditorWindow::~UnifiedEditorWindow() {}

void UnifiedEditorWindow::setupUI() {
    setWindowTitle("Unified Editor");
    resize(1200, 800);
    
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    QHBoxLayout* mainLayout = new QHBoxLayout(m_centralWidget);
    
    m_sceneTree = new QTreeWidget(this);
    m_sceneTree->setHeaderLabel("Scene");
    mainLayout->addWidget(m_sceneTree, 1);
    
    m_editorStack = new QStackedWidget(this);
    mainLayout->addWidget(m_editorStack, 3);
}

void UnifiedEditorWindow::setupRibbon() {
    m_ribbon = new QToolBar("Ribbon", this);
    m_ribbon->setMovable(false);
    m_ribbon->setIconSize(QSize(24, 24));
    m_ribbon->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    m_ribbon->addAction(QIcon(":/icons/document-new.svg"), "New", this, &UnifiedEditorWindow::onNewFile);
    m_ribbon->addAction(QIcon(":/icons/document-open.svg"), "Open", this, &UnifiedEditorWindow::onOpenFile);
    m_ribbon->addAction(QIcon(":/icons/document-save.svg"), "Save", this, &UnifiedEditorWindow::onSaveFile);
    m_ribbon->addSeparator();
    m_ribbon->addAction(QIcon(":/icons/edit-undo.svg"), "Undo");
    m_ribbon->addAction(QIcon(":/icons/edit-redo.svg"), "Redo");
    m_ribbon->addSeparator();

    auto* modeCombo = new QComboBox(this);
    modeCombo->addItems({"Car", "Track", "Character"});
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        setMode(static_cast<EditorMode>(idx));
    });
    m_ribbon->addWidget(modeCombo);

    addToolBar(m_ribbon);
}

void UnifiedEditorWindow::setupEditorStack() {
    if (!m_editorStack) {
        m_editorStack = new QStackedWidget(this);
    }
    // Create default pages for each mode
    for (int i = 0; i < 3; ++i) {
        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);
        layout->setAlignment(Qt::AlignCenter);
        QStringList modeNames = {"Car", "Track", "Character"};
        layout->addWidget(new QLabel(QString("<h2>%1 Editor</h2>").arg(modeNames[i]), page));
        layout->addWidget(new QLabel("Editor content will appear here", page));
        m_editorStack->addWidget(page);
    }
}

void UnifiedEditorWindow::setMode(EditorMode mode) {
    m_currentMode = mode;
    emit modeChanged(mode);
}

QWidget* UnifiedEditorWindow::currentEditorWidget() const {
    return m_editorStack ? m_editorStack->currentWidget() : nullptr;
}

void UnifiedEditorWindow::updateWindowTitle() {
    QString title = "Unified Editor";
    if (!m_currentFileName.isEmpty()) {
        title += " - " + m_currentFileName;
    }
    if (m_sceneModified) {
        title += " *";
    }
    setWindowTitle(title);
}

void UnifiedEditorWindow::closeEvent(QCloseEvent* event) {
    event->accept();
}

void UnifiedEditorWindow::keyPressEvent(QKeyEvent* event) {
    QMainWindow::keyPressEvent(event);
}

void UnifiedEditorWindow::onNewFile() {
    QString path = QFileDialog::getSaveFileName(this, "New File", m_currentFileName,
        "All Supported (*.kn5 *.fbx *.obj *.glb *.wav *.ogg *.ini);;All Files (*)");
    if (!path.isEmpty()) {
        m_currentFileName = path;
        m_sceneModified = false;
        updateWindowTitle();
        emit fileOpened(path);
    }
}

void UnifiedEditorWindow::onOpenFile() {
    QString path = QFileDialog::getOpenFileName(this, "Open File", QString(),
        "All Supported (*.kn5 *.fbx *.obj *.glb *.wav *.ogg *.ini);;All Files (*)");
    if (!path.isEmpty()) {
        m_currentFileName = path;
        m_sceneModified = false;
        updateWindowTitle();
        emit fileOpened(path);
    }
}

void UnifiedEditorWindow::onSaveFile() {
    if (m_currentFileName.isEmpty()) {
        onNewFile();
        return;
    }
    // Signal to save; actual save handled by the active editor widget
    emit fileSaved(m_currentFileName);
    m_sceneModified = false;
    updateWindowTitle();
}

void UnifiedEditorWindow::onModeCar() { setMode(EditorMode::Car); }
void UnifiedEditorWindow::onModeTrack() { setMode(EditorMode::Track); }
void UnifiedEditorWindow::onModeCharacter() { setMode(EditorMode::Character); }

void UnifiedEditorWindow::onToggleGrid() {
    m_gridVisible = !m_gridVisible;
    emit gridToggled(m_gridVisible);
}

void UnifiedEditorWindow::onResetView() {
    emit viewReset();
}

void UnifiedEditorWindow::onFocusSelection() {
    emit focusOnSelection();
}

Editor3DModule::Editor3DModule(QWidget* parent)
    : m_editorWindow(nullptr)
    , m_dockWidget(nullptr)
{}

Editor3DModule::~Editor3DModule() {}

QDockWidget* Editor3DModule::getOrCreateDockWidget(QMainWindow* mainWindow) {
    if (!m_dockWidget) {
        m_editorWindow = new UnifiedEditorWindow(mainWindow);
        m_dockWidget = new QDockWidget("3D Editor", mainWindow);
        m_dockWidget->setWidget(m_editorWindow);
    }
    return m_dockWidget;
}

void Editor3DModule::onActivation()
{
    if (m_editorWindow) {
        m_editorWindow->show();
        m_editorWindow->raise();
        m_editorWindow->activateWindow();
    }
    qDebug() << "[Editor3DModule] Activated";
}

void Editor3DModule::onDeactivation()
{
    if (m_editorWindow) {
        m_editorWindow->hide();
    }
    qDebug() << "[Editor3DModule] Deactivated";
}

EditorSceneGraphWidget::EditorSceneGraphWidget(QWidget* parent)
    : QTreeView(parent) {
    setHeaderHidden(false);
    setAlternatingRowColors(true);
    setAnimated(true);
    setSortingEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);

    m_model = new QStandardItemModel(this);
    setModel(m_model);

    connect(selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &EditorSceneGraphWidget::onSelectionChanged);
}

EditorSceneGraphWidget::~EditorSceneGraphWidget() {}

void EditorSceneGraphWidget::setScene(SceneGraph* scene) {
    m_scene = scene;
    refresh();
}

void EditorSceneGraphWidget::refresh() {
    if (!m_scene) {
        clear();
        return;
    }
    m_model->clear();
    QList<SceneObject*> roots = m_scene->root() ? m_scene->root()->children() : QList<SceneObject*>();
    for (SceneObject* obj : roots) {
        addObjectToModel(m_model->invisibleRootItem(), obj);
    }
}

void EditorSceneGraphWidget::clear() {
    m_model->clear();
}

void EditorSceneGraphWidget::selectObject(SceneObject* obj) {
    if (!obj) return;
    QModelIndexList matches = m_model->match(m_model->index(0, 0), Qt::DisplayRole, obj->name(), 1, Qt::MatchRecursive);
    if (!matches.isEmpty()) {
        selectionModel()->select(matches.first(), QItemSelectionModel::Select);
    }
}

SceneObject* EditorSceneGraphWidget::selectedObject() const {
    QModelIndexList selected = selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return nullptr;
    auto* item = m_model->itemFromIndex(selected.first());
    return item ? reinterpret_cast<SceneObject*>(item->data(Qt::UserRole).value<quintptr>()) : nullptr;
}

QList<SceneObject*> EditorSceneGraphWidget::selectedObjects() const {
    QList<SceneObject*> result;
    for (const QModelIndex& idx : selectionModel()->selectedIndexes()) {
        auto* item = m_model->itemFromIndex(idx);
        if (item) {
            auto* obj = reinterpret_cast<SceneObject*>(item->data(Qt::UserRole).value<quintptr>());
            if (obj) result.append(obj);
        }
    }
    return result;
}

void EditorSceneGraphWidget::toggleVisibility(SceneObject* obj) {
    if (!obj) return;
    obj->setVisible(!obj->isVisible());
    refresh();
}

void EditorSceneGraphWidget::isolateSelection() {
    QList<SceneObject*> selected = selectedObjects();
    if (selected.isEmpty()) return;
    for (SceneObject* obj : m_scene->allObjects()) {
        obj->setVisible(selected.contains(obj));
    }
    refresh();
}

void EditorSceneGraphWidget::setFilter(const QString& filter) {
    for (int row = 0; row < m_model->rowCount(); ++row) {
        auto* item = m_model->item(row);
        if (!item) continue;
        bool matches = filter.isEmpty() || item->text().contains(filter, Qt::CaseInsensitive);
        setRowHidden(row, QModelIndex(), !matches);
    }
}

void EditorSceneGraphWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.addAction("Rename", this, &EditorSceneGraphWidget::onRename);
    menu.addAction("Delete", this, &EditorSceneGraphWidget::onDelete);
    menu.addSeparator();
    menu.addAction("Toggle Visibility", this, [this]() {
        if (auto* obj = selectedObject()) toggleVisibility(obj);
    });
    menu.addAction("Isolate Selection", this, &EditorSceneGraphWidget::isolateSelection);
    menu.exec(event->globalPos());
}

void EditorSceneGraphWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (auto* obj = selectedObject()) {
        emit objectDoubleClicked(obj);
    }
    QTreeView::mouseDoubleClickEvent(event);
}

void EditorSceneGraphWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
}

void EditorSceneGraphWidget::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void EditorSceneGraphWidget::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            qDebug() << "Dropped file:" << url.toLocalFile();
        }
    }
    event->acceptProposedAction();
}

void EditorSceneGraphWidget::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    for (const auto& index : deselected.indexes()) {
        auto* item = m_model->itemFromIndex(index);
        if (item) {
            auto* obj = reinterpret_cast<SceneObject*>(item->data(Qt::UserRole).value<quintptr>());
            if (obj) obj->setSelected(false);
        }
    }
    for (const auto& index : selected.indexes()) {
        auto* item = m_model->itemFromIndex(index);
        if (item) {
            auto* obj = reinterpret_cast<SceneObject*>(item->data(Qt::UserRole).value<quintptr>());
            if (obj) obj->setSelected(true);
        }
    }
    if (auto* obj = selectedObject()) {
        emit objectSelected(obj);
    }
}

void EditorSceneGraphWidget::onItemChanged(QStandardItem* item) {
    if (!item) return;
    auto* obj = reinterpret_cast<SceneObject*>(item->data(Qt::UserRole).value<quintptr>());
    if (obj && item->checkState() != Qt::PartiallyChecked) {
        obj->setVisible(item->checkState() == Qt::Checked);
        emit objectVisibilityChanged(obj, obj->isVisible());
    }
}

void EditorSceneGraphWidget::onRename() {
    if (auto* obj = selectedObject()) {
        bool ok;
        QString newName = QInputDialog::getText(this, "Rename Object",
            "Enter new name:", QLineEdit::Normal, obj->name(), &ok);
        if (ok && !newName.isEmpty()) {
            obj->setName(newName);
            refresh();
            emit objectRenamed(obj, newName);
        }
    }
}

void EditorSceneGraphWidget::onDelete() {
    QList<SceneObject*> toDelete = selectedObjects();
    if (!toDelete.isEmpty()) {
        for (SceneObject* obj : toDelete) {
            delete obj;
        }
        emit objectsDeleted(toDelete);
        refresh();
    }
}

void EditorSceneGraphWidget::updateItem(SceneObject* obj, QStandardItem* item) {
    if (!obj || !item) return;
    item->setText(obj->name());
    item->setCheckable(true);
    item->setCheckState(obj->isVisible() ? Qt::Checked : Qt::Unchecked);
    item->setData(QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(obj)), Qt::UserRole);
}

void EditorSceneGraphWidget::addObjectToModel(QStandardItem* parent, SceneObject* obj) {
    auto* item = new QStandardItem();
    updateItem(obj, item);
    parent->appendRow(item);
    for (SceneObject* child : obj->children()) {
        addObjectToModel(item, child);
    }
}

void EditorSceneGraphWidget::onDuplicate() {
    QList<SceneObject*> sel = selectedObjects();
    for (SceneObject* obj : sel) {
        if (m_scene && obj->parent()) {
            SceneObject* dup = m_scene->createObject(obj->name() + "_copy", obj->type(), obj->parent());
            dup->setPosition(obj->position());
            dup->setRotationEuler(obj->rotationEuler());
            dup->setScale(obj->scale());
        }
    }
    refresh();
}

void EditorSceneGraphWidget::onHide() {
    for (SceneObject* obj : selectedObjects()) {
        obj->setVisible(false);
    }
    refresh();
    emit visibilityChanged();
}

void EditorSceneGraphWidget::onShow() {
    QVector<SceneObject*> all = m_scene ? m_scene->allObjects() : QVector<SceneObject*>();
    for (SceneObject* obj : all) {
        obj->setVisible(true);
    }
    refresh();
    emit visibilityChanged();
}

void EditorSceneGraphWidget::onIsolate() {
    QList<SceneObject*> sel = selectedObjects();
    QVector<SceneObject*> all = m_scene ? m_scene->allObjects() : QVector<SceneObject*>();
    for (SceneObject* obj : all) {
        obj->setVisible(sel.contains(obj));
    }
    refresh();
    emit visibilityChanged();
}

void EditorSceneGraphWidget::onFocus() {
    if (auto* obj = selectedObject()) {
        emit focusRequested(obj);
    }
}

ToolSettingsWidget::ToolSettingsWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

ToolSettingsWidget::~ToolSettingsWidget() {}

void ToolSettingsWidget::setupUI() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(8);

    m_toolBarLayout = new QHBoxLayout();
    m_toolBarLayout->setSpacing(2);
    m_toolButtonGroup = new QButtonGroup(this);
    m_toolButtonGroup->setExclusive(true);

    QStringList toolNames = {
        "Select", "Move", "Rotate", "Scale",
        "Extrude", "Bevel", "Inset", "Loop Cut",
        "Knife", "Paint", "Sculpt", "UV"
    };

    for (int i = 0; i < toolNames.size(); i++) {
        QPushButton* btn = new QPushButton(toolNames[i], this);
        btn->setCheckable(true);
        btn->setFixedSize(60, 28);
        m_toolButtonGroup->addButton(btn, i);
        m_toolBarLayout->addWidget(btn);
    }

    connect(m_toolButtonGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &ToolSettingsWidget::onToolSelected);

    m_mainLayout->addLayout(m_toolBarLayout);
    m_settingsStack = new QStackedWidget(this);
    m_mainLayout->addWidget(m_settingsStack);

    createTransformPanel();
    createExtrudePanel();
    createBevelPanel();
    createInsetPanel();
    createLoopCutPanel();
    createKnifePanel();
    createPaintPanel();
    createSculptPanel();
    createUVPanel();

    setToolType(ToolType::Select);
}

void ToolSettingsWidget::createTransformPanel() {
    m_transformPanel = new QWidget(this);
    QFormLayout* layout = new QFormLayout(m_transformPanel);

    m_snapToGridCheck = new QCheckBox("Snap to Grid", this);
    m_snapToGridCheck->setChecked(m_transformSettings.snapToGrid);
    connect(m_snapToGridCheck, &QCheckBox::toggled, this, &ToolSettingsWidget::onTransformSnapToggled);
    layout->addRow("", m_snapToGridCheck);

    QHBoxLayout* gridLayout = new QHBoxLayout();
    gridLayout->addWidget(new QLabel("Grid Size:", this));
    m_gridSizeSpin = new QDoubleSpinBox(this);
    m_gridSizeSpin->setRange(0.01, 10);
    m_gridSizeSpin->setValue(m_transformSettings.gridSize);
    m_gridSizeSpin->setSuffix(" m");
    connect(m_gridSizeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ToolSettingsWidget::onGridSizeChanged);
    gridLayout->addWidget(m_gridSizeSpin);
    layout->addRow("", gridLayout);

    m_settingsStack->addWidget(m_transformPanel);
}

void ToolSettingsWidget::createExtrudePanel() {
    m_extrudePanel = new QWidget(this);
    QFormLayout* layout = new QFormLayout(m_extrudePanel);

    m_extrudeDistanceSpin = new QDoubleSpinBox(this);
    m_extrudeDistanceSpin->setRange(0.001, 100);
    m_extrudeDistanceSpin->setValue(m_extrudeSettings.distance);
    m_extrudeDistanceSpin->setSuffix(" m");
    m_extrudeDistanceSpin->setSingleStep(0.1);
    connect(m_extrudeDistanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ToolSettingsWidget::onExtrudeDistanceChanged);
    layout->addRow("Distance:", m_extrudeDistanceSpin);

    m_extrudeIndividualCheck = new QCheckBox("Extrude Individual", this);
    m_extrudeIndividualCheck->setChecked(m_extrudeSettings.individual);
    connect(m_extrudeIndividualCheck, &QCheckBox::toggled,
            this, &ToolSettingsWidget::onExtrudeIndividualToggled);
    layout->addRow("", m_extrudeIndividualCheck);

    m_settingsStack->addWidget(m_extrudePanel);
}

void ToolSettingsWidget::createBevelPanel() {
    m_bevelPanel = new QWidget(this);
    QFormLayout* layout = new QFormLayout(m_bevelPanel);

    m_bevelAmountSpin = new QDoubleSpinBox(this);
    m_bevelAmountSpin->setRange(0.001, 10);
    m_bevelAmountSpin->setValue(m_bevelSettings.amount);
    m_bevelAmountSpin->setSuffix(" m");
    connect(m_bevelAmountSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ToolSettingsWidget::onBevelAmountChanged);
    layout->addRow("Amount:", m_bevelAmountSpin);

    m_bevelSegmentsSpin = new QSpinBox(this);
    m_bevelSegmentsSpin->setRange(1, 10);
    m_bevelSegmentsSpin->setValue(m_bevelSettings.segments);
    connect(m_bevelSegmentsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ToolSettingsWidget::onBevelSegmentsChanged);
    layout->addRow("Segments:", m_bevelSegmentsSpin);

    m_settingsStack->addWidget(m_bevelPanel);
}

void ToolSettingsWidget::createInsetPanel() {
    m_insetPanel = new QWidget(this);
    QFormLayout* layout = new QFormLayout(m_insetPanel);
    m_insetAmountSpin = new QDoubleSpinBox(this);
    m_insetAmountSpin->setRange(0.001, 10);
    m_insetAmountSpin->setValue(0.1);
    m_insetAmountSpin->setSuffix(" m");
    layout->addRow("Amount:", m_insetAmountSpin);
    m_settingsStack->addWidget(m_insetPanel);
}

void ToolSettingsWidget::createLoopCutPanel() {
    m_loopCutPanel = new QWidget(this);
    QFormLayout* layout = new QFormLayout(m_loopCutPanel);
    m_loopCutCountSpin = new QSpinBox(this);
    m_loopCutCountSpin->setRange(1, 100);
    m_loopCutCountSpin->setValue(1);
    layout->addRow("Number of Cuts:", m_loopCutCountSpin);
    m_settingsStack->addWidget(m_loopCutPanel);
}

void ToolSettingsWidget::createKnifePanel() {
    m_knifePanel = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_knifePanel);
    m_knifeMidpointCheck = new QCheckBox("Snap to Midpoint", this);
    layout->addWidget(m_knifeMidpointCheck);
    m_knifeIgnoreSnapCheck = new QCheckBox("Ignore Snap", this);
    layout->addWidget(m_knifeIgnoreSnapCheck);
    layout->addStretch();
    m_settingsStack->addWidget(m_knifePanel);
}

void ToolSettingsWidget::createPaintPanel() {
    m_paintPanel = new QWidget(this);
    QFormLayout* layout = new QFormLayout(m_paintPanel);

    m_paintRadiusSpin = new QDoubleSpinBox(this);
    m_paintRadiusSpin->setRange(0.01, 10);
    m_paintRadiusSpin->setValue(m_paintSettings.radius);
    m_paintRadiusSpin->setSuffix(" m");
    connect(m_paintRadiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ToolSettingsWidget::onPaintRadiusChanged);
    layout->addRow("Radius:", m_paintRadiusSpin);

    m_paintStrengthSpin = new QDoubleSpinBox(this);
    m_paintStrengthSpin->setRange(0, 1);
    m_paintStrengthSpin->setValue(m_paintSettings.strength);
    m_paintStrengthSpin->setSingleStep(0.05);
    connect(m_paintStrengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ToolSettingsWidget::onPaintStrengthChanged);
    layout->addRow("Strength:", m_paintStrengthSpin);

    m_settingsStack->addWidget(m_paintPanel);
}

void ToolSettingsWidget::createSculptPanel() {
    m_sculptPanel = new QWidget(this);
    QFormLayout* layout = new QFormLayout(m_sculptPanel);
    m_sculptBrushCombo = new QComboBox(this);
    m_sculptBrushCombo->addItems({"Standard", "Smooth", "Flatten", "Clay", "Pinch", "Grab"});
    layout->addRow("Brush:", m_sculptBrushCombo);
    m_settingsStack->addWidget(m_sculptPanel);
}

void ToolSettingsWidget::createUVPanel() {
    m_uvPanel = new QWidget(this);
    QFormLayout* layout = new QFormLayout(m_uvPanel);

    m_uvProjectionCombo = new QComboBox(this);
    m_uvProjectionCombo->addItems({"Planar", "Cylindrical", "Spherical", "Box", "Smart"});
    layout->addRow("Projection:", m_uvProjectionCombo);

    m_uvUnwrapMethodCombo = new QComboBox(this);
    m_uvUnwrapMethodCombo->addItems({"Angle Based", "Conformal", "Least Squares Conformal", "Planar", "Cylindrical", "Spherical"});
    layout->addRow("Unwrap Method:", m_uvUnwrapMethodCombo);

    m_seamAngleSpin = new QDoubleSpinBox(this);
    m_seamAngleSpin->setRange(0, 180);
    m_seamAngleSpin->setValue(66);
    m_seamAngleSpin->setSingleStep(1);
    m_seamAngleSpin->setSuffix(" deg");
    layout->addRow("Seam Angle:", m_seamAngleSpin);

    m_packMarginSpin = new QDoubleSpinBox(this);
    m_packMarginSpin->setRange(0.0, 1.0);
    m_packMarginSpin->setValue(0.02);
    m_packMarginSpin->setSingleStep(0.01);
    m_packMarginSpin->setSuffix(" px");
    layout->addRow("Pack Margin:", m_packMarginSpin);

    m_shareUVCheck = new QCheckBox("Share UV coordinates", this);
    m_shareUVCheck->setChecked(true);
    layout->addRow("", m_shareUVCheck);

    m_unwrapBtn = new QPushButton("Unwrap", this);
    m_packChartsBtn = new QPushButton("Pack Charts", this);
    connect(m_unwrapBtn, &QPushButton::clicked, this, &ToolSettingsWidget::onUnwrapClicked);
    connect(m_packChartsBtn, &QPushButton::clicked, this, &ToolSettingsWidget::onPackChartsClicked);
    layout->addRow("", m_unwrapBtn);
    layout->addRow("", m_packChartsBtn);

    m_uvPreviewLabel = new QLabel("Load a mesh to preview UV layout", this);
    m_uvPreviewLabel->setAlignment(Qt::AlignCenter);
    m_uvPreviewLabel->setMinimumHeight(200);
    m_uvPreviewLabel->setStyleSheet("QLabel { background-color: #1a1a2e; border: 1px solid #3a3a5e; border-radius: 4px; }");
    layout->addWidget(m_uvPreviewLabel);

    m_settingsStack->addWidget(m_uvPanel);
}

void ToolSettingsWidget::onUnwrapClicked() {
    QString methodText;
    switch (m_uvUnwrapMethodCombo->currentIndex()) {
        case 0: methodText = "Angle Based"; break;
        case 1: methodText = "Conformal"; break;
        case 2: methodText = "Least Squares Conformal"; break;
        case 3: methodText = "Planar"; break;
        case 4: methodText = "Cylindrical"; break;
        case 5: methodText = "Spherical"; break;
        default: methodText = "Unknown"; break;
    }
    QString info = QString("UV %1 method selected (seam angle: %2°, pack margin: %3px)")
        .arg(methodText)
        .arg(m_seamAngleSpin->value())
        .arg(m_packMarginSpin->value());
    emit uvUnwrapRequested(info);
}

void ToolSettingsWidget::onPackChartsClicked() {
    QString marginText = QString::number(m_packMarginSpin->value()) + "px";
    emit uvPackChartsRequested(m_packMarginSpin->value(), m_shareUVCheck->isChecked());
}

void ToolSettingsWidget::setToolType(ToolType type) {
    m_currentTool = type;
    if (QAbstractButton* btn = m_toolButtonGroup->button(static_cast<int>(type))) {
        btn->setChecked(true);
    }
    updateVisibility();
    emit toolChanged(type);
}

void ToolSettingsWidget::onToolSelected(int toolIndex) {
    setToolType(static_cast<ToolType>(toolIndex));
}

void ToolSettingsWidget::updateVisibility() {
    switch (m_currentTool) {
        case ToolType::Select:
        case ToolType::Move:
        case ToolType::Rotate:
        case ToolType::Scale:
            m_settingsStack->setCurrentWidget(m_transformPanel);
            break;
        case ToolType::Extrude:
            m_settingsStack->setCurrentWidget(m_extrudePanel);
            break;
        case ToolType::Bevel:
            m_settingsStack->setCurrentWidget(m_bevelPanel);
            break;
        case ToolType::Inset:
            m_settingsStack->setCurrentWidget(m_insetPanel);
            break;
        case ToolType::LoopCut:
            m_settingsStack->setCurrentWidget(m_loopCutPanel);
            break;
        case ToolType::Knife:
            m_settingsStack->setCurrentWidget(m_knifePanel);
            break;
        case ToolType::Paint:
            m_settingsStack->setCurrentWidget(m_paintPanel);
            break;
        case ToolType::Sculpt:
            m_settingsStack->setCurrentWidget(m_sculptPanel);
            break;
        case ToolType::UV:
            m_settingsStack->setCurrentWidget(m_uvPanel);
            break;
    }
}

void ToolSettingsWidget::onTransformSnapToggled(bool enabled) {
    m_transformSettings.snapToGrid = enabled;
    m_gridSizeSpin->setEnabled(enabled);
    emit settingsChanged();
}

void ToolSettingsWidget::onGridSizeChanged(double size) {
    m_transformSettings.gridSize = (float)size;
    emit settingsChanged();
}

void ToolSettingsWidget::onExtrudeDistanceChanged(double dist) {
    m_extrudeSettings.distance = (float)dist;
    emit settingsChanged();
}

void ToolSettingsWidget::onExtrudeIndividualToggled(bool enabled) {
    m_extrudeSettings.individual = enabled;
    emit settingsChanged();
}

void ToolSettingsWidget::onBevelAmountChanged(double amount) {
    m_bevelSettings.amount = (float)amount;
    emit settingsChanged();
}

void ToolSettingsWidget::onBevelSegmentsChanged(int segments) {
    m_bevelSettings.segments = segments;
    emit settingsChanged();
}

void ToolSettingsWidget::onPaintRadiusChanged(double radius) {
    m_paintSettings.radius = (float)radius;
    emit settingsChanged();
}

void ToolSettingsWidget::onPaintStrengthChanged(double strength) {
    m_paintSettings.strength = (float)strength;
    emit settingsChanged();
}

void ToolSettingsWidget::onPaintHardnessChanged(double hardness) {
    m_paintSettings.hardness = (float)hardness;
    emit settingsChanged();
}

void ToolSettingsWidget::onPaintFalloffChanged(int type) {
    m_paintSettings.falloffType = type;
    emit settingsChanged();
}

ToolBarWidget::ToolBarWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

ToolBarWidget::~ToolBarWidget() {}

void ToolBarWidget::setupUI() {
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_buttonGroup = new QButtonGroup(this);
    m_buttonGroup->setExclusive(true);

    QStringList toolNames = {"Select", "Move", "Rotate", "Scale"};

    for (int i = 0; i < toolNames.size(); i++) {
        QPushButton* btn = new QPushButton(toolNames[i], this);
        btn->setCheckable(true);
        btn->setFixedSize(50, 24);
        m_buttonGroup->addButton(btn, i);
        layout->addWidget(btn);
        m_buttons.append(btn);
    }

    layout->addStretch();
    connect(m_buttonGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &ToolBarWidget::onButtonClicked);
    setActiveTool(ToolType::Select);
}

void ToolBarWidget::setActiveTool(ToolType type) {
    m_activeTool = type;
    int index = static_cast<int>(type);
    if (index >= 0 && index < m_buttons.size()) {
        m_buttons[index]->setChecked(true);
    }
}

void ToolBarWidget::onButtonClicked(int id) {
    m_activeTool = static_cast<ToolType>(id);
    emit toolSelected(m_activeTool);
}

}