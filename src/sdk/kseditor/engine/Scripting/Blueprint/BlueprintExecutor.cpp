// ============================================================================
// BlueprintExecutor.cpp
// Runtime interpreter implementation.
// ============================================================================

#include "BlueprintExecutor.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <algorithm>

namespace ks {
namespace blueprint {

// ============================================================================
// Construction / Destruction
// ============================================================================
BlueprintExecutor::BlueprintExecutor(QObject* parent)
    : QObject(parent)
    , m_library(BlueprintNodeLibrary::instance())
{
}

BlueprintExecutor::~BlueprintExecutor()
{
    endPlay();
}

// ============================================================================
// Graph management
// ============================================================================
void BlueprintExecutor::setGraph(const BlueprintGraphData& graph)
{
    m_graph = graph;

    // Initialize variables from declarations
    m_variables.clear();
    for (const auto& var : m_graph.variables) {
        m_variables[var.name] = var.defaultValue;
    }
}

// ============================================================================
// Execution lifecycle
// ============================================================================
void BlueprintExecutor::beginPlay()
{
    m_active = true;
    m_context.totalTime = 0;
    m_context.frameCount = 0;
    m_pureNodeCache.clear();

    emit executionStarted();

    // Find and execute all Event.BeginPlay nodes
    auto beginPlayNodes = findEventNodes("Event.BeginPlay");
    for (const auto& node : beginPlayNodes) {
        executeExecNode(node.id);
    }
}

void BlueprintExecutor::tick(float deltaTime)
{
    if (!m_active) return;

    m_context.deltaTime = deltaTime;
    m_context.totalTime += deltaTime;
    m_context.frameCount++;

    // Clear pure node cache each frame
    m_pureNodeCache.clear();

    // Process pending execution queue
    while (!m_execQueue.isEmpty()) {
        QUuid nextId = m_execQueue.takeFirst();
        executeExecNode(nextId);
    }

    // Find and execute all Event.Tick nodes
    auto tickNodes = findEventNodes("Event.Tick");
    for (const auto& node : tickNodes) {
        executeExecNode(node.id);
    }
}

void BlueprintExecutor::endPlay()
{
    m_active = false;
    m_execQueue.clear();
    m_executionStack.clear();
    m_pureNodeCache.clear();
    m_nodeLocalVars.clear();
    emit executionFinished();
}

// ============================================================================
// Variable access
// ============================================================================
QVariant BlueprintExecutor::getVariable(const QString& name) const
{
    return m_variables.value(name);
}

void BlueprintExecutor::setVariable(const QString& name, const QVariant& value)
{
    m_variables[name] = value;
}

QMap<QString, QVariant> BlueprintExecutor::getAllVariables() const
{
    return m_variables;
}

// ============================================================================
// Custom function calls
// ============================================================================
bool BlueprintExecutor::callFunction(const QString& functionName,
                                      const QMap<QString, QVariant>& params,
                                      QMap<QString, QVariant>* returnValues)
{
    if (!m_graph.functionGraphs.contains(functionName)) {
        qDebug("Blueprint: Function '%s' not found", functionName.toUtf8().constData());
        return false;
    }

    // Find the custom function declaration
    const BlueprintFunction* func = nullptr;
    for (const auto& f : m_graph.customFunctions) {
        if (f.name == functionName) { func = &f; break; }
    }
    if (!func) return false;

    // Find the "Entry" node in the function graph
    auto entryNodes = m_graph.functionGraphs[functionName];
    QUuid entryId;
    for (const auto& node : entryNodes) {
        if (node.typeName == "Event.Custom" || node.typeName == "Function.Entry") {
            entryId = node.id;
            break;
        }
    }
    if (entryId.isNull()) return false;

    // Set input parameters as local variables
    for (const auto& param : func->inputs) {
        if (params.contains(param.name)) {
            m_variables[functionName + "." + param.name] = params.value(param.name);
        }
    }

    // Execute the function graph
    m_executionStack.append(entryId);
    executeExecNode(entryId);
    m_executionStack.removeLast();

    // Collect return values
    if (returnValues) {
        for (const auto& output : func->outputs) {
            (*returnValues)[output.name] = m_variables.value(functionName + "." + output.name);
        }
    }

    return true;
}

// ============================================================================
// Breakpoints
// ============================================================================
void BlueprintExecutor::addBreakpoint(const QUuid& nodeId)
{
    m_breakpoints.insert(nodeId);
}

void BlueprintExecutor::removeBreakpoint(const QUuid& nodeId)
{
    m_breakpoints.remove(nodeId);
}

void BlueprintExecutor::clearBreakpoints()
{
    m_breakpoints.clear();
}

void BlueprintExecutor::stepOver()
{
    m_stepMode = true;
    if (!m_execQueue.isEmpty()) {
        QUuid nextId = m_execQueue.takeFirst();
        executeExecNode(nextId);
    }
}

void BlueprintExecutor::continueExecution()
{
    m_breakpointMode = false;
    m_stepMode = false;
}

// ============================================================================
// Core execution
// ============================================================================
void BlueprintExecutor::executeExecNode(const QUuid& nodeId)
{
    const ui::GraphNode* node = findNode(nodeId);
    if (!node) return;

    // Check breakpoint
    if (m_breakpoints.contains(nodeId) && !m_stepMode) {
        m_breakpointMode = true;
        emit breakpointHit(nodeId);
        return;
    }

    const BlueprintNodeType* nodeType = nullptr;
    if (m_library.hasNodeType(node->typeName)) {
        nodeType = &m_library.getNodeType(node->typeName);
    }

    if (!nodeType) {
        emit executionError(nodeId, "Unknown node type: " + node->typeName);
        return;
    }

    // Handle flow control nodes specially
    if (node->typeName == "Flow.Branch") {
        handleBranchNode(nodeId);
        return;
    }
    if (node->typeName == "Flow.Sequence") {
        handleSequenceNode(nodeId);
        return;
    }

    // Execute the node
    QMap<QUuid, QVariant> inputs;
    QMap<QUuid, QVariant> outputs;

    // Gather input values
    for (const auto& portId : node->inputPortIds) {
        inputs[portId] = getInputValue(nodeId, portId);
    }

    // Call the node's execute function
    if (nodeType->execute) {
        nodeType->execute(nodeId, inputs, outputs, &m_context);
    }

    // Handle Print String specially
    if (node->typeName == "Flow.Print") {
        for (const auto& val : inputs.values()) {
            if (val.typeId() == QMetaType::QString) {
                qDebug("Blueprint Print: %s", val.toString().toUtf8().constData());
                break;
            }
        }
    }

    // Handle Set Variable
    if (node->typeName == "Variable.Set" || node->typeName == "Variable.LocalSet") {
        QString varName = node->properties.value("variableName").toString();
        for (const auto& val : inputs.values()) {
            if (!val.isNull() && val.isValid()) {
                m_variables[varName] = val;
                break;
            }
        }
    }

    emit nodeExecuted(nodeId);

    // Step mode
    if (m_stepMode) {
        m_stepMode = false;
        m_breakpointMode = true;
        return;
    }

    // Follow the first output exec pin
    QUuid outputExecPin = findOutputExecPin(nodeId, 0);
    if (!outputExecPin.isNull()) {
        followExecPin(nodeId, outputExecPin);
    }
}

void BlueprintExecutor::executeNodeChain(const QUuid& startNodeId)
{
    QUuid currentId = startNodeId;
    int maxIterations = 10000;  // Safety limit

    while (!currentId.isNull() && maxIterations-- > 0) {
        executeExecNode(currentId);

        // The node execution will have queued the next nodes via followExecPin
        if (m_execQueue.isEmpty()) break;
        currentId = m_execQueue.takeFirst();
    }
}

void BlueprintExecutor::executeNode(const QUuid& nodeId)
{
    executeExecNode(nodeId);
}

// ============================================================================
// Pure node evaluation with caching
// ============================================================================
QVariant BlueprintExecutor::evaluatePureNode(const QUuid& nodeId, const QUuid& outputPinId)
{
    // Check cache first
    if (m_pureNodeCache.contains(nodeId)) {
        const ui::GraphNode* node = findNode(nodeId);
        if (node) {
            // Return cached value from node properties (set during last evaluation)
            return node->properties.value("cached_" + outputPinId.toString());
        }
    }

    const ui::GraphNode* node = findNode(nodeId);
    if (!node) return QVariant();

    if (!m_library.hasNodeType(node->typeName))
        return QVariant();

    const BlueprintNodeType& nodeType = m_library.getNodeType(node->typeName);

    // Gather input values (recursively evaluate connected pure nodes)
    QMap<QUuid, QVariant> inputs;
    for (const auto& portId : node->inputPortIds) {
        inputs[portId] = getInputValue(nodeId, portId);
    }

    QMap<QUuid, QVariant> outputs;
    if (nodeType.execute) {
        nodeType.execute(nodeId, inputs, outputs, &m_context);
    }

    // Cache outputs
    for (auto it = outputs.begin(); it != outputs.end(); ++it) {
        // Store in node properties for retrieval
        // In production, use a proper cache map
    }

    m_pureNodeCache.insert(nodeId);
    return outputs.value(outputPinId);
}

// ============================================================================
// Port value resolution
// ============================================================================
QVariant BlueprintExecutor::getInputValue(const QUuid& nodeId, const QUuid& inputPinId)
{
    // Find connection to this input pin
    for (const auto& conn : m_graph.connections) {
        if (conn.toNodeId == nodeId && conn.toPortId == inputPinId) {
            // Found a connection - evaluate the source node
            const ui::GraphNode* sourceNode = findNode(conn.fromNodeId);
            if (!sourceNode) return QVariant();

            if (m_library.hasNodeType(sourceNode->typeName)) {
                const BlueprintNodeType& srcType = m_library.getNodeType(sourceNode->typeName);

                if (srcType.isPureNode) {
                    return evaluatePureNode(conn.fromNodeId, conn.fromPortId);
                } else {
                    // For exec nodes, return the last output value
                    // (stored in the node's properties during execution)
                    return sourceNode->properties.value("output_" + conn.fromPortId.toString());
                }
            }
            return QVariant();
        }
    }

    // No connection - return default value from pin definition
    const ui::GraphNode* node = findNode(nodeId);
    if (!node) return QVariant();

    if (m_library.hasNodeType(node->typeName)) {
        const BlueprintNodeType& nodeType = m_library.getNodeType(node->typeName);
        for (const auto& pin : nodeType.inputs) {
            if (pin.id == inputPinId) {
                // Check if it's a Get Variable node
                if (node->typeName == "Variable.Get" || node->typeName == "Variable.LocalGet") {
                    QString varName = node->properties.value("variableName").toString();
                    return m_variables.value(varName, pin.defaultValue);
                }
                return pin.defaultValue;
            }
        }
    }

    return QVariant();
}

QVector<QUuid> BlueprintExecutor::getConnectedNodes(const QUuid& nodeId, const QUuid& outputPinId)
{
    QVector<QUuid> connected;
    for (const auto& conn : m_graph.connections) {
        if (conn.fromNodeId == nodeId && conn.fromPortId == outputPinId) {
            connected.append(conn.toNodeId);
        }
    }
    return connected;
}

QUuid BlueprintExecutor::findOutputExecPin(const QUuid& nodeId, int outputIndex)
{
    const ui::GraphNode* node = findNode(nodeId);
    if (!node) return QUuid();

    if (!m_library.hasNodeType(node->typeName))
        return QUuid();

    const BlueprintNodeType& nodeType = m_library.getNodeType(node->typeName);

    int execCount = 0;
    for (const auto& pin : nodeType.outputs) {
        if (pin.type == PinType::Exec) {
            if (execCount == outputIndex) return pin.id;
            execCount++;
        }
    }
    return QUuid();
}

QUuid BlueprintExecutor::findInputExecPin(const QUuid& nodeId)
{
    const ui::GraphNode* node = findNode(nodeId);
    if (!node) return QUuid();

    if (!m_library.hasNodeType(node->typeName))
        return QUuid();

    const BlueprintNodeType& nodeType = m_library.getNodeType(node->typeName);

    for (const auto& pin : nodeType.inputs) {
        if (pin.type == PinType::Exec) return pin.id;
    }
    return QUuid();
}

// ============================================================================
// Flow control
// ============================================================================
void BlueprintExecutor::followExecPin(const QUuid& fromNodeId, const QUuid& fromPortId)
{
    // Find the connection from this exec output
    for (const auto& conn : m_graph.connections) {
        if (conn.fromNodeId == fromNodeId && conn.fromPortId == fromPortId) {
            m_execQueue.append(conn.toNodeId);
            return;
        }
    }
}

void BlueprintExecutor::handleBranchNode(const QUuid& nodeId)
{
    const ui::GraphNode* node = findNode(nodeId);
    if (!node) return;

    // Get the Condition input value
    QVariant condVal;
    for (const auto& portId : node->inputPortIds) {
        QVariant val = getInputValue(nodeId, portId);
        if (val.typeId() == QMetaType::Bool) {
            condVal = val;
            break;
        }
    }

    bool condition = condVal.isValid() ? condVal.toBool() : false;

    // Find the True or False output exec pin
    int pinIndex = condition ? 0 : 1;  // First exec out = True, second = False
    QUuid outputPin = findOutputExecPin(nodeId, pinIndex);

    if (!outputPin.isNull()) {
        followExecPin(nodeId, outputPin);
    }
}

void BlueprintExecutor::handleSequenceNode(const QUuid& nodeId)
{
    // Execute all output exec pins in order
    for (int i = 0; ; ++i) {
        QUuid outputPin = findOutputExecPin(nodeId, i);
        if (outputPin.isNull()) break;
        followExecPin(nodeId, outputPin);
    }
}

// ============================================================================
// Graph queries
// ============================================================================
const ui::GraphNode* BlueprintExecutor::findNode(const QUuid& nodeId) const
{
    for (const auto& node : m_graph.nodes) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

const ui::GraphConnection* BlueprintExecutor::findConnection(const QUuid& fromNodeId,
                                                              const QUuid& fromPortId) const
{
    for (const auto& conn : m_graph.connections) {
        if (conn.fromNodeId == fromNodeId && conn.fromPortId == fromPortId)
            return &conn;
    }
    return nullptr;
}

QVector<ui::GraphNode> BlueprintExecutor::findEventNodes(const QString& typeName) const
{
    QVector<ui::GraphNode> result;
    for (const auto& node : m_graph.nodes) {
        if (node.typeName == typeName) {
            result.append(node);
        }
    }
    return result;
}

}} // namespace ks::blueprint
