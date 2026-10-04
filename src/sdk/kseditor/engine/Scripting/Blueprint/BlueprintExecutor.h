#pragma once
// ============================================================================
// BlueprintExecutor.h
// Runtime interpreter for Blueprint graphs. Walks execution paths starting
// from event nodes, evaluates pure nodes on-demand with caching, and
// manages the call stack for recursive function calls.
// ============================================================================

#include "BlueprintTypes.h"
#include "BlueprintNodeLibrary.h"
#include <QObject>
#include <QVector>
#include <QMap>
#include <QUuid>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>

namespace ks {
namespace blueprint {

// ============================================================================
// Execution context passed to nodes
// ============================================================================
struct BlueprintContext {
    void* worldObject = nullptr;     // Pointer to the game world / scene
    void* ownerObject = nullptr;     // Pointer to the owning entity
    float deltaTime = 0.016f;
    float totalTime = 0.0f;
    int frameCount = 0;
    QMap<QString, QVariant> globals;  // Global variables
};

// ============================================================================
// BlueprintExecutor - Runtime graph interpreter
// ============================================================================
class BlueprintExecutor : public QObject
{
    Q_OBJECT
public:
    explicit BlueprintExecutor(QObject* parent = nullptr);
    ~BlueprintExecutor() override;

    // ---- Graph management --------------------------------------------------
    void setGraph(const BlueprintGraphData& graph);
    const BlueprintGraphData& graph() const { return m_graph; }

    // ---- Execution lifecycle -----------------------------------------------
    void beginPlay();                    // Trigger Event.BeginPlay nodes
    void tick(float deltaTime);          // Trigger Event.Tick nodes
    void endPlay();                      // Cleanup
    bool isActive() const { return m_active; }

    // ---- Variable access ---------------------------------------------------
    QVariant getVariable(const QString& name) const;
    void setVariable(const QString& name, const QVariant& value);
    QMap<QString, QVariant> getAllVariables() const;

    // ---- Custom function calls ---------------------------------------------
    bool callFunction(const QString& functionName,
                      const QMap<QString, QVariant>& params = {},
                      QMap<QString, QVariant>* returnValues = nullptr);

    // ---- Context -----------------------------------------------------------
    BlueprintContext& context() { return m_context; }
    void setContext(const BlueprintContext& ctx) { m_context = ctx; }

    // ---- Breakpoints (for debugging) ---------------------------------------
    void addBreakpoint(const QUuid& nodeId);
    void removeBreakpoint(const QUuid& nodeId);
    void clearBreakpoints();
    void stepOver();
    void continueExecution();

signals:
    void nodeExecuted(const QUuid& nodeId);
    void executionError(const QUuid& nodeId, const QString& error);
    void breakpointHit(const QUuid& nodeId);
    void executionStarted();
    void executionFinished();

private:
    // ---- Core execution ----------------------------------------------------
    void executeNode(const QUuid& nodeId);
    void executeNodeChain(const QUuid& startNodeId);
    void executeExecNode(const QUuid& nodeId);

    // ---- Pure node evaluation with caching ---------------------------------
    QVariant evaluatePureNode(const QUuid& nodeId, const QUuid& outputPinId);

    // ---- Port value resolution ---------------------------------------------
    QVariant getInputValue(const QUuid& nodeId, const QUuid& inputPinId);
    QVector<QUuid> getConnectedNodes(const QUuid& nodeId, const QUuid& outputPinId);
    QUuid findOutputExecPin(const QUuid& nodeId, int outputIndex = 0);
    QUuid findInputExecPin(const QUuid& nodeId);

    // ---- Flow control helpers ----------------------------------------------
    void followExecPin(const QUuid& fromNodeId, const QUuid& fromPortId);
    void handleBranchNode(const QUuid& nodeId);
    void handleSequenceNode(const QUuid& nodeId);

    // ---- Graph queries -----------------------------------------------------
    const ui::GraphNode* findNode(const QUuid& nodeId) const;
    const ui::GraphConnection* findConnection(const QUuid& fromNodeId, const QUuid& fromPortId) const;
    QVector<ui::GraphNode> findEventNodes(const QString& typeName) const;

    // ---- State -------------------------------------------------------------
    BlueprintGraphData m_graph;
    BlueprintNodeLibrary& m_library;
    BlueprintContext m_context;

    bool m_active = false;
    bool m_breakpointMode = false;
    bool m_stepMode = false;

    // Execution state
    QVector<QUuid> m_executionStack;       // Call stack
    QVector<QUuid> m_execQueue;            // Pending execution nodes
    QSet<QUuid> m_breakpoints;
    QSet<QUuid> m_pureNodeCache;           // Cached pure node results this frame

    // Variable storage
    QMap<QString, QVariant> m_variables;
    QMap<QUuid, QMap<QString, QVariant>> m_nodeLocalVars;  // Per-node locals

    QElapsedTimer m_timer;
};

}} // namespace ks::blueprint
