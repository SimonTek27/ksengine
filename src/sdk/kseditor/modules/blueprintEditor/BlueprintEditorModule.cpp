// ============================================================================
// BlueprintEditorModule.cpp
// Blueprint visual scripting editor implementation.
// ============================================================================

#include "BlueprintEditorModule.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDockWidget>
#include <QMainWindow>
#include <QHeaderView>
#include <QGroupBox>
#include <QScrollArea>
#include <QDebug>

// NodeGraphWidget is in resources/ui/
#include "resources/ui/NodeGraphEditor.h"

namespace ks {
namespace blueprint {

// ============================================================================
// Construction / Destruction
// ============================================================================
BlueprintEditorModule::BlueprintEditorModule(QWidget* parent)
    : EditorModule(parent)
    , m_library(BlueprintNodeLibrary::instance())
{
}

BlueprintEditorModule::~BlueprintEditorModule()
{
    shutdown();
}

// ============================================================================
// Initialize / Shutdown
// ============================================================================
bool BlueprintEditorModule::initialize()
{
    m_executor = new BlueprintExecutor(this);

    connect(m_executor, &BlueprintExecutor::nodeExecuted,
            this, [this](const QUuid& id) { Q_UNUSED(id); });
    connect(m_executor, &BlueprintExecutor::executionError,
            this, [this](const QUuid& id, const QString& err) {
        if (m_statusLabel)
            m_statusLabel->setText(QString("Error at %1: %2").arg(id.toString().left(8), err));
    });
    connect(m_executor, &BlueprintExecutor::breakpointHit,
            this, &BlueprintEditorModule::onBreakpointHit);

    buildUI();
    buildNodePalette();
    refreshNodePalette();

    return true;
}

void BlueprintEditorModule::shutdown()
{
    if (m_executor) {
        m_executor->endPlay();
    }
}

// ============================================================================
// buildUI
// ============================================================================
void BlueprintEditorModule::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // ---- Toolbar -----------------------------------------------------------
    buildToolbar();

    // ---- Splitter: Palette | Graph | Details -------------------------------
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left: Node Palette
    auto* leftPanel = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(2, 2, 2, 2);

    auto* paletteLabel = new QLabel("Node Palette");
    paletteLabel->setStyleSheet("font-weight: bold; padding: 4px;");
    leftLayout->addWidget(paletteLabel);

    auto* searchBox = new QLineEdit();
    searchBox->setPlaceholderText("Search nodes...");
    leftLayout->addWidget(searchBox);

    m_nodePalette = new QTreeWidget();
    m_nodePalette->setHeaderLabels({"Node", "Type"});
    m_nodePalette->setColumnCount(2);
    m_nodePalette->header()->setVisible(false);
    m_nodePalette->setRootIsDecorated(true);
    m_nodePalette->setDragEnabled(true);
    leftLayout->addWidget(m_nodePalette);

    connect(searchBox, &QLineEdit::textChanged, [this](const QString& text) {
        // Filter the palette
        for (int i = 0; i < m_nodePalette->topLevelItemCount(); ++i) {
            QTreeWidgetItem* cat = m_nodePalette->topLevelItem(i);
            bool catVisible = false;
            for (int j = 0; j < cat->childCount(); ++j) {
                QTreeWidgetItem* item = cat->child(j);
                bool match = text.isEmpty() ||
                             item->text(0).contains(text, Qt::CaseInsensitive) ||
                             item->text(1).contains(text, Qt::CaseInsensitive);
                item->setHidden(!match);
                if (match) catVisible = true;
            }
            cat->setHidden(!catVisible);
        }
    });

    splitter->addWidget(leftPanel);

    // Center: Node Graph
    auto* centerPanel = new QWidget();
    auto* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);

    m_graphWidget = new ui::NodeGraphWidget();
    centerLayout->addWidget(m_graphWidget);

    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setStyleSheet("padding: 2px; color: #aaa;");
    centerLayout->addWidget(m_statusLabel);

    splitter->addWidget(centerPanel);

    // Right: Variable Panel + Details
    auto* rightPanel = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(2, 2, 2, 2);

    auto* varLabel = new QLabel("Variables");
    varLabel->setStyleSheet("font-weight: bold; padding: 4px;");
    rightLayout->addWidget(varLabel);

    auto* addVarLayout = new QHBoxLayout();
    auto* varNameEdit = new QLineEdit();
    varNameEdit->setPlaceholderText("Variable name");
    addVarLayout->addWidget(varNameEdit);

    auto* varTypeCombo = new QComboBox();
    varTypeCombo->addItems({"Float", "Int", "Bool", "String", "Vector", "Color"});
    addVarLayout->addWidget(varTypeCombo);

    auto* addVarBtn = new QPushButton("+");
    addVarBtn->setMaximumWidth(30);
    addVarLayout->addWidget(addVarBtn);
    rightLayout->addLayout(addVarLayout);

    m_variableList = new QListWidget();
    rightLayout->addWidget(m_variableList);

    auto* delVarBtn = new QPushButton("Remove Variable");
    rightLayout->addWidget(delVarBtn);

    // Variable scope selector
    auto* scopeLayout = new QHBoxLayout();
    scopeLayout->addWidget(new QLabel("Scope:"));
    auto* scopeCombo = new QComboBox();
    scopeCombo->addItems({"Local", "Member", "Global"});
    scopeLayout->addWidget(scopeCombo);
    rightLayout->addLayout(scopeLayout);

    splitter->addWidget(rightPanel);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 1);

    mainLayout->addWidget(splitter);

    // ---- Connect signals ---------------------------------------------------
    connect(addVarBtn, &QPushButton::clicked, [this, varNameEdit, varTypeCombo, scopeCombo]() {
        QString name = varNameEdit->text().trimmed();
        if (name.isEmpty()) return;

        PinType type;
        QString typeName = varTypeCombo->currentText();
        if (typeName == "Float") type = PinType::Float;
        else if (typeName == "Int") type = PinType::Int;
        else if (typeName == "Bool") type = PinType::Bool;
        else if (typeName == "String") type = PinType::String;
        else if (typeName == "Vector") type = PinType::Vector;
        else if (typeName == "Color") type = PinType::Color;
        else type = PinType::Float;

        BlueprintVarScope scope;
        int scopeIdx = scopeCombo->currentIndex();
        if (scopeIdx == 0) scope = BlueprintVarScope::Local;
        else if (scopeIdx == 1) scope = BlueprintVarScope::Member;
        else scope = BlueprintVarScope::Global;

        addVariable(name, type, scope);
        varNameEdit->clear();
    });

    connect(delVarBtn, &QPushButton::clicked, [this]() {
        auto* item = m_variableList->currentItem();
        if (item) {
            removeVariable(item->text().split(" ")[0]);
        }
    });

    connect(m_graphWidget, &ui::NodeGraphWidget::graphChanged,
            this, &BlueprintEditorModule::onGraphChanged);
}

// ============================================================================
// buildToolbar
// ============================================================================
void BlueprintEditorModule::buildToolbar()
{
    // The toolbar will be created by getOrCreateDockWidget
}

// ============================================================================
// buildNodePalette
// ============================================================================
void BlueprintEditorModule::buildNodePalette()
{
    m_nodePalette->clear();

    QStringList categories = m_library.getCategories();
    categories.sort();

    for (const auto& cat : categories) {
        auto* catItem = new QTreeWidgetItem(m_nodePalette);
        catItem->setText(0, cat);
        catItem->setFlags(catItem->flags() & ~Qt::ItemIsDragEnabled);

        QVector<BlueprintNodeType> nodes = m_library.getNodeTypesByCategory(cat);
        for (const auto& nodeType : nodes) {
            auto* nodeItem = new QTreeWidgetItem(catItem);
            nodeItem->setText(0, nodeType.displayName);
            nodeItem->setText(1, nodeType.typeName);
            nodeItem->setData(0, Qt::UserRole, nodeType.typeName);

            // Color code by type
            QColor color;
            if (nodeType.isEventNode) color = QColor(180, 40, 40);
            else if (nodeType.isPureNode) color = QColor(40, 140, 80);
            else color = QColor(40, 80, 160);

            nodeItem->setForeground(0, color);
        }
    }
}

// ============================================================================
// refreshNodePalette
// ============================================================================
void BlueprintEditorModule::refreshNodePalette()
{
    buildNodePalette();
}

// ============================================================================
// Variable management
// ============================================================================
void BlueprintEditorModule::addVariable(const QString& name, PinType type, BlueprintVarScope scope)
{
    BlueprintVariable var;
    var.name = name;
    var.type = type;
    var.scope = scope;

    // Set default value based on type
    switch (type) {
        case PinType::Float: var.defaultValue = 0.f; break;
        case PinType::Int: var.defaultValue = 0; break;
        case PinType::Bool: var.defaultValue = false; break;
        case PinType::String: var.defaultValue = QString(); break;
        case PinType::Vector: var.defaultValue = QVariant::fromValue(QVector3D()); break;
        case PinType::Color: var.defaultValue = QVariant::fromValue(QColor(Qt::white)); break;
        default: var.defaultValue = QVariant(); break;
    }

    m_graphData.variables.append(var);
    refreshVariableList();
    emit variableAdded(name);
    emit blueprintChanged();
}

void BlueprintEditorModule::removeVariable(const QString& name)
{
    for (int i = m_graphData.variables.size() - 1; i >= 0; --i) {
        if (m_graphData.variables[i].name == name) {
            m_graphData.variables.removeAt(i);
            break;
        }
    }
    refreshVariableList();
    emit variableRemoved(name);
    emit blueprintChanged();
}

void BlueprintEditorModule::refreshVariableList()
{
    m_variableList->clear();
    for (const auto& var : m_graphData.variables) {
        QString typeName;
        switch (var.type) {
            case PinType::Float: typeName = "Float"; break;
            case PinType::Int: typeName = "Int"; break;
            case PinType::Bool: typeName = "Bool"; break;
            case PinType::String: typeName = "String"; break;
            case PinType::Vector: typeName = "Vector"; break;
            case PinType::Color: typeName = "Color"; break;
            default: typeName = "Wildcard"; break;
        }

        QString scopeName;
        switch (var.scope) {
            case BlueprintVarScope::Local: scopeName = "Local"; break;
            case BlueprintVarScope::Member: scopeName = "Member"; break;
            case BlueprintVarScope::Global: scopeName = "Global"; break;
        }

        auto* item = new QListWidgetItem(
            QString("%1 : %2 [%3]").arg(var.name, typeName, scopeName));
        m_variableList->addItem(item);
    }
}

// ============================================================================
// Node operations
// ============================================================================
void BlueprintEditorModule::addNode(const QString& typeName)
{
    QPointF pos(100 + m_graphData.nodes.size() * 50,
                100 + m_graphData.nodes.size() * 30);
    ui::GraphNode node = m_library.createNode(typeName, pos);
    if (node.id.isNull()) return;

    m_graphData.nodes.append(node);
    emit blueprintChanged();
}

// ============================================================================
// Blueprint operations
// ============================================================================
void BlueprintEditorModule::newBlueprint(const QString& name)
{
    m_graphData = BlueprintGraphData();
    m_graphData.name = name;
    m_graphData.id = QUuid::createUuid();

    if (m_graphWidget) {
        m_graphWidget->clearAll();
    }

    emit blueprintChanged();
}

void BlueprintEditorModule::saveBlueprint(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;

    QJsonDocument doc(m_graphData.toJson());
    file.write(doc.toJson());
}

void BlueprintEditorModule::openBlueprint(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject()) {
        m_graphData.fromJson(doc.object());

        // Update graph widget
        if (m_graphWidget) {
            m_graphWidget->clearAll();
            m_graphWidget->fromJson(doc.object());
        }

        // Update executor
        if (m_executor) {
            m_executor->setGraph(m_graphData);
        }

        refreshVariableList();
        emit blueprintChanged();
    }
}

// ============================================================================
// Execution
// ============================================================================
void BlueprintEditorModule::play()
{
    if (!m_executor) return;

    m_executor->setGraph(m_graphData);
    m_executor->beginPlay();

    if (m_statusLabel)
        m_statusLabel->setText("Executing...");
    emit executionStarted();
}

void BlueprintEditorModule::stop()
{
    if (m_executor) {
        m_executor->endPlay();
    }

    if (m_statusLabel)
        m_statusLabel->setText("Stopped");
    emit executionStopped();
}

void BlueprintEditorModule::simulate()
{
    if (!m_executor) return;

    m_executor->setGraph(m_graphData);
    m_executor->tick(0.016f);  // Simulate one frame at 60fps
}

// ============================================================================
// File operations
// ============================================================================
void BlueprintEditorModule::exportFile(const QString& path)
{
    saveBlueprint(path);
}

void BlueprintEditorModule::importFile(const QString& path)
{
    openBlueprint(path);
}

void BlueprintEditorModule::saveProject(const QString& path)
{
    saveBlueprint(path);
}

// ============================================================================
// Serialization
// ============================================================================
QJsonObject BlueprintEditorModule::serializeProject() const
{
    return m_graphData.toJson();
}

void BlueprintEditorModule::deserializeProject(const QJsonObject& json)
{
    m_graphData.fromJson(json);

    if (m_executor) {
        m_executor->setGraph(m_graphData);
    }

    refreshVariableList();
    emit blueprintChanged();
}

// ============================================================================
// Dock Widget
// ============================================================================
QDockWidget* BlueprintEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    auto* dock = new QDockWidget("Blueprint Editor", mainWindow);
    dock->setWidget(this);
    dock->setObjectName("BlueprintEditorDock");
    return dock;
}

// ============================================================================
// Slots
// ============================================================================
void BlueprintEditorModule::onNodeSelected(const QUuid& nodeId)
{
    m_selectedNodeId = nodeId;
    // Could update details panel here
}

void BlueprintEditorModule::onNodeDeselected(const QUuid& nodeId)
{
    Q_UNUSED(nodeId);
    m_selectedNodeId = QUuid();
}

void BlueprintEditorModule::onGraphChanged()
{
    // Sync graph data from widget
    if (m_graphWidget) {
        // The graph widget manages its own GraphData internally
        // We need to extract it and merge with our extended data
    }

    if (m_statusLabel)
        m_statusLabel->setText(QString("Nodes: %1 | Connections: %2 | Variables: %3")
            .arg(m_graphData.nodes.size())
            .arg(m_graphData.connections.size())
            .arg(m_graphData.variables.size()));

    emit blueprintChanged();
}

void BlueprintEditorModule::onBreakpointHit(const QUuid& nodeId)
{
    if (m_statusLabel)
        m_statusLabel->setText(QString("Breakpoint hit at %1").arg(nodeId.toString().left(8)));
}

}} // namespace ks::blueprint
