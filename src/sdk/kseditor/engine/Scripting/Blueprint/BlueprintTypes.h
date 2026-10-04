#pragma once
// ============================================================================
// BlueprintTypes.h
// Core data types for the Blueprint visual scripting system.
// Defines pin types, execution pins, variable declarations, and graph extensions.
// ============================================================================

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QVariant>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <QColor>
#include <functional>

#include "resources/ui/NodeGraphEditor.h"

namespace ks {
namespace blueprint {

// ============================================================================
// Pin Types
// ============================================================================
enum class PinType {
    Exec,           // Execution flow (white wire)
    Bool,           // Boolean (red)
    Int,            // Integer (cyan)
    Float,          // Float (green)
    String,         // String (magenta)
    Vector,         // Vector3 (yellow)
    Vector2,        // Vector2
    Color,          // Color (orange)
    Object,         // Object reference (blue)
    Array,          // Array (teal)
    Function,       // Function reference (purple)
    Enum,           // Enum value
    Struct,         // Custom struct
    Wildcard        // Any type (gray)
};

// ============================================================================
// Pin Color Map
// ============================================================================
inline QColor pinTypeColor(PinType type) {
    switch (type) {
        case PinType::Exec:      return QColor(255, 255, 255);
        case PinType::Bool:      return QColor(230, 50, 50);
        case PinType::Int:       return QColor(0, 200, 200);
        case PinType::Float:     return QColor(100, 200, 80);
        case PinType::String:    return QColor(200, 100, 200);
        case PinType::Vector:    return QColor(255, 220, 50);
        case PinType::Vector2:   return QColor(200, 180, 50);
        case PinType::Color:     return QColor(255, 140, 40);
        case PinType::Object:    return QColor(60, 120, 255);
        case PinType::Array:     return QColor(50, 180, 180);
        case PinType::Function:  return QColor(180, 80, 220);
        case PinType::Enum:      return QColor(150, 150, 150);
        case PinType::Struct:    return QColor(120, 160, 200);
        case PinType::Wildcard:  return QColor(128, 128, 128);
    }
    return QColor(128, 128, 128);
}

// ============================================================================
// Blueprint Pin
// ============================================================================
struct BlueprintPin {
    QUuid id;
    QString name;
    PinType type = PinType::Exec;
    bool isInput = true;
    bool isMultiConnection = false;
    QVariant defaultValue;
    QString description;
    QString tooltip;

    // Conversion to/from ui::NodePort
    ui::NodePort toNodePort() const;
    static BlueprintPin fromNodePort(const ui::NodePort& port);
};

// ============================================================================
// Blueprint Node Type (registered in library)
// ============================================================================
using BlueprintExecFunc = std::function<void(
    /*nodeId*/ const QUuid&,
    /*inputs*/ const QMap<QUuid, QVariant>&,
    /*outputs*/ QMap<QUuid, QVariant>&,
    /*context*/ void*
)>;

struct BlueprintNodeType {
    QString typeName;
    QString displayName;
    QString category;       // "Events", "Flow Control", "Math", "Variables", etc.
    QString description;
    QString icon;
    QVector<BlueprintPin> inputs;
    QVector<BlueprintPin> outputs;
    QMap<QString, QVariant> defaultProperties;

    bool isEventNode = false;       // Event nodes start execution
    bool isPureNode = false;        // Pure nodes have no side effects, cached
    bool isMacroNode = false;       // Macro nodes expand inline
    bool isConstNode = false;       // Constant nodes always return same value

    BlueprintExecFunc execute;      // Execution function (null for pure nodes)

    // Conversion to/from ui::NodeInfo
    ui::NodeInfo toNodeInfo() const;
    static BlueprintNodeType fromNodeInfo(const ui::NodeInfo& info);
};

// ============================================================================
// Blueprint Variable Declaration
// ============================================================================
enum class BlueprintVarScope {
    Local,          // Function-local variable
    Member,         // Object member variable
    Global          // Global variable
};

struct BlueprintVariable {
    QString name;
    PinType type = PinType::Float;
    BlueprintVarScope scope = BlueprintVarScope::Local;
    QVariant defaultValue;
    QString description;
    bool exposed = false;           // Visible in editor details panel
    bool readOnly = false;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
};

// ============================================================================
// Blueprint Function Declaration
// ============================================================================
struct BlueprintFunction {
    QString name;
    QString category;
    QString description;
    QVector<BlueprintPin> inputs;   // Input parameters
    QVector<BlueprintPin> outputs;  // Return values
    bool isEvent = false;
    bool isPure = false;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
};

// ============================================================================
// Blueprint Graph (extended from ui::GraphData)
// ============================================================================
struct BlueprintGraphData {
    QString name;
    QUuid id;
    QVector<ui::GraphNode> nodes;
    QVector<ui::GraphConnection> connections;
    QVector<BlueprintVariable> variables;
    QVector<BlueprintFunction> customFunctions;
    QMap<QString, QVariant> metadata;

    // Function graphs (each function has its own node graph)
    QMap<QString, QVector<ui::GraphNode>> functionGraphs;
    QMap<QString, QVector<ui::GraphConnection>> functionConnections;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
};

// ============================================================================
// Execution frame (for call stack)
// ============================================================================
struct BlueprintExecFrame {
    QUuid nodeId;                   // Current executing node
    int outputPinIndex = 0;         // Which output exec pin to follow
    QMap<QUuid, QVariant> locals;   // Local variables for this frame
};

}} // namespace ks::blueprint
