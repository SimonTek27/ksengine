#include "ToolsEditorModule.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QTableWidgetItem>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QMenu>
#include <QAction>
#include <QProgressBar>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QUuid>
#include <QDirIterator>
#include "LODGenerator.h"
#include "CollisionMeshGenerator.h"
#include "BackupSystem.h"
#include "MacroSystem.h"
#include "sdk/kseditor/engine/mesh/MeshOperations.h"
#include "sdk/kseditor/engine/mesh/AdvancedMeshOps.h"

namespace ks {
namespace tools {

ToolsEditorModule::ToolsEditorModule(QWidget* parent)
    : ModuleGuiBase(parent)
    , m_tabWidget(nullptr)
    , m_toolsListTab(nullptr)
    , m_toolTree(nullptr)
    , m_toolDescriptionLabel(nullptr)
    , m_launchToolBtn(nullptr)
    , m_batchTab(nullptr)
    , m_batchQueueTable(nullptr)
    , m_batchProcessBtn(nullptr)
    , m_addBatchItemBtn(nullptr)
    , m_removeBatchItemBtn(nullptr)
    , m_batchProgress(nullptr)
    , m_batchStatusLabel(nullptr)
    , m_lodTab(nullptr)
    , m_lodErrorSpin(nullptr)
    , m_lodTargetCountSpin(nullptr)
    , m_generateLODBtn(nullptr)
    , m_lodProgress(nullptr)
    , m_collisionTab(nullptr)
    , m_collisionMethodCombo(nullptr)
    , m_collisionMaxHullsSpin(nullptr)
    , m_collisionPrecisionSpin(nullptr)
    , m_generateCollisionBtn(nullptr)
    , m_collisionProgress(nullptr)
    , m_backupTab(nullptr)
    , m_backupBtn(nullptr)
    , m_restoreBtn(nullptr)
    , m_backupTree(nullptr)
    , m_macrosTab(nullptr)
    , m_recordBtn(nullptr)
    , m_playBtn(nullptr)
    , m_openPythonBtn(nullptr)
    , m_macroList(nullptr)
{
    setObjectName("ToolsEditorModule");
}

bool ToolsEditorModule::initialize() {
    if (m_uiBuilt) return true;
    ModuleGuiBase::initialize();
    return true;
}

void ToolsEditorModule::shutdown() {
    m_uiBuilt = false;
}

void ToolsEditorModule::onActivation() {}
void ToolsEditorModule::onDeactivation() {}

void ToolsEditorModule::buildUI() {
    m_tabWidget = new QTabWidget();

    setupToolsListTab();
    setupBatchTab();
    setupLODTab();
    setupCollisionTab();
    setupBackupTab();
    setupMacrosTab();

    m_mainLayout->addWidget(m_tabWidget);
    m_mainLayout->addWidget(m_logOutput);
}

void ToolsEditorModule::setupToolsListTab() {
    m_toolsListTab = new QWidget();
    auto* layout = new QVBoxLayout(m_toolsListTab);

    auto* splitter = createSplitter(Qt::Horizontal);
    m_toolTree = createTreeWidget({"Tool", "Category", "Status"});
    splitter->addWidget(m_toolTree);

    auto* infoPanel = new QWidget();
    auto* infoLayout = new QVBoxLayout(infoPanel);
    m_toolDescriptionLabel = createLabel("Select a tool to view description");
    m_launchToolBtn = createButton("Launch Tool");
    infoLayout->addWidget(m_toolDescriptionLabel);
    infoLayout->addWidget(m_launchToolBtn);
    infoLayout->addStretch();
    splitter->addWidget(infoPanel);

    layout->addWidget(splitter);

    connect(m_toolTree, &QTreeWidget::itemClicked, this, &ToolsEditorModule::onToolSelected);
    connect(m_launchToolBtn, &QPushButton::clicked, this, [this]() {
        auto* item = m_toolTree->currentItem();
        if (item) log(QString("Launching tool: %1").arg(item->text(0)));
    });

    populateToolsList();
    m_tabWidget->addTab(m_toolsListTab, "Tool Library");
}

void ToolsEditorModule::setupBatchTab() {
    m_batchTab = new QWidget();
    auto* layout = new QVBoxLayout(m_batchTab);

    auto* btnLayout = new QHBoxLayout();
    m_batchProcessBtn = createButton("Process All");
    m_addBatchItemBtn = createButton("Add Items");
    m_removeBatchItemBtn = createButton("Remove");
    btnLayout->addWidget(m_batchProcessBtn);
    btnLayout->addWidget(m_addBatchItemBtn);
    btnLayout->addWidget(m_removeBatchItemBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    m_batchQueueTable = new QTableWidget(0, 4);
    m_batchQueueTable->setHorizontalHeaderLabels({"Item", "Tool", "Status", "Progress"});
    m_batchQueueTable->horizontalHeader()->setStretchLastSection(true);
    m_batchQueueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_batchQueueTable->setAlternatingRowColors(true);
    layout->addWidget(m_batchQueueTable);

    m_batchProgress = new QProgressBar();
    m_batchProgress->setVisible(false);
    layout->addWidget(m_batchProgress);

    m_batchStatusLabel = createLabel("");
    layout->addWidget(m_batchStatusLabel);

    connect(m_batchProcessBtn, &QPushButton::clicked, this, &ToolsEditorModule::onBatchProcess);
    connect(m_addBatchItemBtn, &QPushButton::clicked, this, [this]() {
        QStringList files = selectFiles("Add Items to Batch", "All Supported (*.kn5 *.fbx *.gltf *.glb *.obj *.dae);;All Files (*)");
        for (const auto& f : files) {
            int row = m_batchQueueTable->rowCount();
            m_batchQueueTable->insertRow(row);
            m_batchQueueTable->setItem(row, 0, new QTableWidgetItem(QFileInfo(f).fileName()));
            m_batchQueueTable->setItem(row, 1, new QTableWidgetItem("Auto"));
            m_batchQueueTable->setItem(row, 2, new QTableWidgetItem("Pending"));
            m_batchQueueTable->setItem(row, 3, new QTableWidgetItem("0%"));
        }
    });
    connect(m_removeBatchItemBtn, &QPushButton::clicked, this, [this]() {
        int row = m_batchQueueTable->currentRow();
        if (row >= 0) m_batchQueueTable->removeRow(row);
    });

    m_tabWidget->addTab(m_batchTab, "Batch Processing");
}

void ToolsEditorModule::setupLODTab() {
    m_lodTab = new QWidget();
    auto* layout = new QVBoxLayout(m_lodTab);

    auto* params = createGroupBox("LOD Generation");
    auto* form = new QFormLayout(params);
    m_lodErrorSpin = createDoubleSpinBox(0.001, 1.0, 0.05, 3, "");
    m_lodTargetCountSpin = createSpinBox(1, 10, 3, " LODs");
    form->addRow("Max Error:", m_lodErrorSpin);
    form->addRow("Target LOD Count:", m_lodTargetCountSpin);

    auto* btnLayout = new QHBoxLayout();
    m_generateLODBtn = createButton("Generate LODs");
    btnLayout->addWidget(m_generateLODBtn);
    btnLayout->addStretch();
    form->addRow("", btnLayout);

    layout->addWidget(params);

    m_lodProgress = new QProgressBar();
    m_lodProgress->setVisible(false);
    layout->addWidget(m_lodProgress);

    layout->addStretch();

    connect(m_generateLODBtn, &QPushButton::clicked, this, &ToolsEditorModule::onGenerateLOD);

    m_tabWidget->addTab(m_lodTab, "LOD Generator");
}

void ToolsEditorModule::setupCollisionTab() {
    m_collisionTab = new QWidget();
    auto* layout = new QVBoxLayout(m_collisionTab);

    auto* params = createGroupBox("Collision Mesh Generation");
    auto* form = new QFormLayout(params);
    m_collisionMethodCombo = createComboBox({"V-HACD (Convex Decomposition)", "Primitive Fitting (Box/Sphere/Capsule)", "Convex Hull"});
    m_collisionMaxHullsSpin = createSpinBox(1, 64, 16, " hulls");
    m_collisionPrecisionSpin = createDoubleSpinBox(0.001, 1.0, 0.01, 3, "");
    form->addRow("Method:", m_collisionMethodCombo);
    form->addRow("Max Hulls:", m_collisionMaxHullsSpin);
    form->addRow("Precision:", m_collisionPrecisionSpin);

    auto* btnLayout = new QHBoxLayout();
    m_generateCollisionBtn = createButton("Generate Collision");
    btnLayout->addWidget(m_generateCollisionBtn);
    btnLayout->addStretch();
    form->addRow("", btnLayout);

    layout->addWidget(params);

    m_collisionProgress = new QProgressBar();
    m_collisionProgress->setVisible(false);
    layout->addWidget(m_collisionProgress);

    layout->addStretch();

    connect(m_generateCollisionBtn, &QPushButton::clicked, this, &ToolsEditorModule::onGenerateCollision);

    m_tabWidget->addTab(m_collisionTab, "Collision Mesh");
}

void ToolsEditorModule::setupBackupTab() {
    m_backupTab = new QWidget();
    auto* layout = new QVBoxLayout(m_backupTab);

    auto* btnLayout = new QHBoxLayout();
    m_backupBtn = createButton("Backup Now");
    m_restoreBtn = createButton("Restore from Backup");
    btnLayout->addWidget(m_backupBtn);
    btnLayout->addWidget(m_restoreBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    m_backupTree = createTreeWidget({"Backup", "Date", "Size", "Type"});
    m_backupTree->addTopLevelItem(new QTreeWidgetItem({"Auto-backup 001", "2024-01-15 10:00", "1.2 GB", "Auto"}));
    m_backupTree->addTopLevelItem(new QTreeWidgetItem({"Before LOD Gen", "2024-01-14 15:30", "980 MB", "Manual"}));
    m_backupTree->addTopLevelItem(new QTreeWidgetItem({"End of Day", "2024-01-13 18:00", "1.1 GB", "Scheduled"}));
    layout->addWidget(m_backupTree);

    connect(m_backupBtn, &QPushButton::clicked, this, &ToolsEditorModule::onBackupNow);
    connect(m_restoreBtn, &QPushButton::clicked, this, &ToolsEditorModule::onRestoreBackup);

    m_tabWidget->addTab(m_backupTab, "Backup");
}

void ToolsEditorModule::setupMacrosTab() {
    m_macrosTab = new QWidget();
    auto* layout = new QVBoxLayout(m_macrosTab);

    auto* btnLayout = new QHBoxLayout();
    m_recordBtn = createButton("Record Macro");
    m_playBtn = createButton("Play Macro");
    m_openPythonBtn = createButton("Python Console");
    btnLayout->addWidget(m_recordBtn);
    btnLayout->addWidget(m_playBtn);
    btnLayout->addWidget(m_openPythonBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    m_macroList = new QListWidget();
    m_macroList->addItem("Batch Export All");
    m_macroList->addItem("Generate LODs + Collision");
    m_macroList->addItem("Validate Project");
    m_macroList->addItem("Publish to Workshop");
    layout->addWidget(m_macroList);

    connect(m_recordBtn, &QPushButton::clicked, this, &ToolsEditorModule::onMacroRecord);
    connect(m_playBtn, &QPushButton::clicked, this, &ToolsEditorModule::onMacroPlay);
    connect(m_openPythonBtn, &QPushButton::clicked, this, &ToolsEditorModule::onOpenPythonConsole);

    m_tabWidget->addTab(m_macrosTab, "Macros & Scripting");
}

void ToolsEditorModule::populateToolsList() {
    m_toolTree->clear();

    auto* generation = new QTreeWidgetItem(m_toolTree, {"Mesh Generation", "", ""});
    generation->addChild(new QTreeWidgetItem({"LOD Generator", "Mesh", "Ready"}));
    generation->addChild(new QTreeWidgetItem({"Collision Mesh Generator", "Mesh", "Ready"}));
    generation->addChild(new QTreeWidgetItem({"UV Unwrapper", "Mesh", "Ready"}));

    auto* validation = new QTreeWidgetItem(m_toolTree, {"Validation", "", ""});
    validation->addChild(new QTreeWidgetItem({"Asset Validator", "QA", "Ready"}));
    validation->addChild(new QTreeWidgetItem({"Format Validator", "QA", "Ready"}));
    validation->addChild(new QTreeWidgetItem({"Dependency Scanner", "QA", "Ready"}));

    auto* conversion = new QTreeWidgetItem(m_toolTree, {"Conversion", "", ""});
    conversion->addChild(new QTreeWidgetItem({"Batch Converter", "Format", "Ready"}));
    conversion->addChild(new QTreeWidgetItem({"Export Presets", "Format", "Ready"}));

    auto* utilities = new QTreeWidgetItem(m_toolTree, {"Utilities", "", ""});
    utilities->addChild(new QTreeWidgetItem({"Backup Manager", "System", "Ready"}));
    utilities->addChild(new QTreeWidgetItem({"Macro Recorder", "Automation", "Ready"}));
    utilities->addChild(new QTreeWidgetItem({"Performance Optimizer", "System", "Ready"}));
    utilities->addChild(new QTreeWidgetItem({"Theme Editor", "UI", "Ready"}));

    m_toolTree->expandAll();
}

void ToolsEditorModule::onToolSelected(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (item && item->childCount() == 0) {
        m_toolDescriptionLabel->setText(QString("Tool: %1\nCategory: %2\nStatus: %3").arg(item->text(0), item->text(1), item->text(2)));
    }
}

void ToolsEditorModule::onBatchProcess() {
    if (m_batchQueueTable->rowCount() == 0) {
        logError("No items in batch queue");
        return;
    }
    log(QString("Starting batch processing of %1 items").arg(m_batchQueueTable->rowCount()));
    m_batchProgress->setVisible(true);
    m_batchProgress->setRange(0, m_batchQueueTable->rowCount());
    m_batchStatusLabel->setText("Processing...");
}

void ToolsEditorModule::onGenerateLOD() {
    int targetLevels = m_lodTargetCountSpin->value();
    log(QString("Generating %1 LOD levels...").arg(targetLevels));
    m_lodProgress->setVisible(true);
    m_lodProgress->setRange(0, 100);
    m_lodProgress->setValue(0);
    QApplication::processEvents();

    LODGenerator::Options opts;
    opts.lodCount = targetLevels;
    opts.reductionRatio = 0.5f;
    opts.useQuadricError = true;
    opts.preserveBoundaries = true;

    // Generate from a default box mesh as example
    auto box = MeshOperations::createBox(1.0f, 1.0f, 1.0f);
    QVector<QVector3D> verts;
    for (const auto& v : box.vertices) verts.append(v.position);
    QVector<int> indices;
    for (const auto& f : box.faces) { for (int idx : f.indices) indices.append(idx); }

    auto result = LODGenerator::generate(verts, indices, {}, {}, opts);
    if (result.success) {
        m_lodProgress->setValue(100);
        logSuccess(QString("Generated %1 LOD levels").arg(result.levels.size()));
    } else {
        logError("LOD generation failed: " + result.errorMessage);
    }
    m_lodProgress->setVisible(false);
}

void ToolsEditorModule::onGenerateCollision() {
    QString method = m_collisionMethodCombo->currentText();
    log(QString("Generating collision mesh using %1...").arg(method));
    m_collisionProgress->setVisible(true);
    m_collisionProgress->setRange(0, 100);
    m_collisionProgress->setValue(10);

    auto box = MeshOperations::createBox(1.0f, 1.0f, 1.0f);
    QVector<QVector3D> verts;
    for (const auto& v : box.vertices) verts.append(v.position);
    QVector<int> idxs;
    for (const auto& f : box.faces) { for (int i : f.indices) idxs.append(i); }
    m_collisionProgress->setValue(30);

    CollisionMeshGenerator generator;
    CollisionMeshOptions opts;
    opts.type = CollisionMeshOptions::Type::ConvexHull;
    opts.maxHulls = m_collisionMaxHullsSpin->value();

    auto result = generator.generate(verts, idxs, opts);
    if (result.success) {
        m_collisionProgress->setValue(100);
        logSuccess(QString("Collision mesh generated: %1 hulls, %2 triangles")
            .arg(result.hulls.size()).arg(result.totalTriangles));
    } else {
        logError("Collision generation failed: " + result.error);
    }
    m_collisionProgress->setVisible(false);
}

void ToolsEditorModule::onValidateAssets() {
    log("Starting asset validation...");
    QString projectDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    int errors = 0, warnings = 0;
    QDirIterator it(projectDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        QString suffix = fi.suffix().toLower();
        if (suffix == "kn5" || suffix == "fbx" || suffix == "glb" || suffix == "obj") {
            if (fi.size() == 0) { errors++; logError("Empty file: " + fi.filePath()); }
            else if (fi.size() < 100) { warnings++; logWarning("Suspiciously small file: " + fi.filePath()); }
        }
    }
    logSuccess(QString("Asset validation complete: %1 errors, %2 warnings").arg(errors).arg(warnings));
}

void ToolsEditorModule::onBackupNow() {
    QString projectDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    log(QString("Creating backup of: %1").arg(projectDir));
    QString backupId = BackupManager::instance()->createBackup(projectDir, "Manual backup from Tools panel");
    if (!backupId.isEmpty()) {
        auto entry = BackupManager::instance()->getBackup(backupId);
        m_backupTree->addTopLevelItem(new QTreeWidgetItem({
            QFileInfo(entry.backupPath).fileName(),
            entry.created.toString("yyyy-MM-dd HH:mm"),
            entry.size > 0 ? QString::number(entry.size) + " B" : "0 B"
        }));
        logSuccess(QString("Backup created: %1").arg(backupId));
        BackupManager::instance()->setCurrentProject(projectDir);
    } else {
        logError("Failed to create backup");
    }
}

void ToolsEditorModule::onRestoreBackup() {
    auto* item = m_backupTree->currentItem();
    if (!item) { logWarning("Select a backup to restore"); return; }
    if (confirmAction("Restore Backup", QString("Restore '%1'? Current data will be overwritten.").arg(item->text(0)))) {
        QString backupName = item->text(0);
        auto backups = BackupManager::instance()->getBackups(QString());
        for (const auto& b : backups) {
            if (QFileInfo(b.backupPath).fileName() == backupName) {
                QString target = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/restored";
                if (BackupManager::instance()->restoreBackup(b.id, target)) {
                    logSuccess(QString("Backup restored to: %1").arg(target));
                } else {
                    logError("Failed to restore backup");
                }
                return;
            }
        }
        logError("Backup not found: " + backupName);
    }
}

void ToolsEditorModule::onMacroRecord() {
    log("Starting macro recording...");
    if (m_recordBtn) {
        if (m_recordBtn->text() == QStringLiteral("Recording...")) {
            Macro* macro = MacroManager::instance()->createMacro("Recorded " + QDateTime::currentDateTime().toString("HHmmss"));
            Macro::Action action;
            action.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
            action.type = "tool";
            action.params["description"] = "Recorded action sequence";
            macro->addAction(action);
            m_macroList->addItem(macro->getName() + " (" + macro->getId().left(8) + ")");
            m_recordBtn->setText("Record");
            logSuccess("Macro recording saved");
        } else {
            m_recordBtn->setText("Recording...");
        }
    }
}

void ToolsEditorModule::onMacroPlay() {
    auto* item = m_macroList->currentItem();
    if (item) {
        QString itemText = item->text();
        QString id = itemText.right(10).left(8);
        for (Macro* m : MacroManager::instance()->getMacros()) {
            if (m->getId().left(8) == id || m->getName() == itemText.section(" (", 0, 0)) {
                m->execute();
                log(QString("Playing macro: %1").arg(m->getName()));
                return;
            }
        }
        logWarning(QString("Macro not found: %1").arg(itemText));
    } else {
        logWarning("Select a macro to play");
    }
}

void ToolsEditorModule::onOpenPythonConsole() {
    log("Opening Python console...");
    QStringList pythonPaths = {"python", "python3", "C:/Python312/python.exe", "C:/Python311/python.exe"};
    bool launched = false;
    for (const QString& py : pythonPaths) {
        QProcess proc;
        proc.setProgram(py);
        proc.setArguments({"-c", "print('ksEditor Python Console v0.9'); import code; code.interact(local=dict(kseditor='Tools Module'))"});
        proc.setProcessChannelMode(QProcess::ForwardedChannels);
        qint64 pid = 0;
        if (proc.startDetached(&pid)) {
            logSuccess(QString("Python console started (PID: %1)").arg(pid));
            launched = true;
            break;
        }
    }
    if (!launched) {
        logWarning("Python not found. Install Python 3.x from python.org");
        QProcess::startDetached("cmd", {"/c", "start", "https://www.python.org/downloads/"});
    }
}

} // namespace tools
} // namespace ks

