#pragma once
// ============================================================================
// BlueprintEditorModule.h
// Visual scripting editor for creating gameplay logic blueprints.
// Integrates with the NodeGraphEditor framework and BlueprintExecutor.
// ============================================================================

#include "editor/EditorModule.h"
#include "sdk/kseditor/engine/Scripting/Blueprint/BlueprintTypes.h"
#include "sdk/kseditor/engine/Scripting/Blueprint/BlueprintExecutor.h"
#include "sdk/kseditor/engine/Scripting/Blueprint/BlueprintNodeLibrary.h"
#include <QObject>
#include <QWidget>
#include <QDockWidget>
#include <QSplitter>
#include <QTreeWidget>
#include <QListWidget>
#include <QLabel>
#include <QTimer>
#include <QUuid>

namespace ks { namespace ui {
class NodeGraphWidget;
class NodeGraphView;
}} // namespace ks::ui

namespace ks {
namespace blueprint {

// ============================================================================
// BlueprintEditorModule
// ============================================================================
class BlueprintEditorModule : public EditorModule
{
    Q_OBJECT
public:
    explicit BlueprintEditorModule(QWidget* parent = nullptr);
    ~BlueprintEditorModule() override;

    // EditorModule interface
    bool initialize() override;
    void shutdown() override;
    QString moduleName() const override { return "Blueprint Editor"; }
    QString moduleId() const override { return "blueprintEditor"; }
    int getModulePriority() const override { return 39; }

    QDockWidget* getOrCreateDockWidget(QMainWindow* mainWindow) override;

    // File operations
    void exportFile(const QString& path) override;
    void importFile(const QString& path) override;
    void saveProject(const QString& path) override;

    // Serialization
    QJsonObject serializeProject() const override;
    void deserializeProject(const QJsonObject& json) override;

    // ---- Blueprint operations -----------------------------------------------
    void newBlueprint(const QString& name = "New Blueprint");
    void openBlueprint(const QString& path);
    void saveBlueprint(const QString& path);

    // ---- Execution ----------------------------------------------------------
    void play();
    void stop();
    void simulate();

    // ---- Node operations ----------------------------------------------------
    void addNode(const QString& typeName);
    void addVariable(const QString& name, PinType type, BlueprintVarScope scope);
    void removeVariable(const QString& name);

    // ---- Accessors ----------------------------------------------------------
    BlueprintExecutor* executor() { return m_executor; }
    BlueprintGraphData& graphData() { return m_graphData; }
    ui::NodeGraphWidget* graphWidget() { return m_graphWidget; }

signals:
    void blueprintChanged();
    void executionStarted();
    void executionStopped();
    void variableAdded(const QString& name);
    void variableRemoved(const QString& name);

private slots:
    void onNodeSelected(const QUuid& nodeId);
    void onNodeDeselected(const QUuid& nodeId);
    void onGraphChanged();
    void onBreakpointHit(const QUuid& nodeId);

private:
    void buildUI();
    void buildNodePalette();
    void buildVariablePanel();
    void buildDetailsPanel();
    void buildToolbar();
    void refreshNodePalette();
    void refreshVariableList();

    // ---- UI Components ------------------------------------------------------
    ui::NodeGraphWidget* m_graphWidget = nullptr;
    QTreeWidget* m_nodePalette = nullptr;
    QListWidget* m_variableList = nullptr;
    QWidget* m_detailsPanel = nullptr;
    QLabel* m_statusLabel = nullptr;

    // ---- Data ---------------------------------------------------------------
    BlueprintGraphData m_graphData;
    BlueprintExecutor* m_executor = nullptr;
    BlueprintNodeLibrary& m_library;

    // ---- Selection state ----------------------------------------------------
    QUuid m_selectedNodeId;
};

}} // namespace ks::blueprint
