#pragma once
// ============================================================================
// BlueprintNodeLibrary.h
// Registry of all available Blueprint node types.
// Provides event nodes, flow control, math, string, vector, and utility nodes.
// ============================================================================

#include "BlueprintTypes.h"
#include <QObject>
#include <QVector>
#include <QMap>
#include <QString>

namespace ks {
namespace blueprint {

// ============================================================================
// BlueprintNodeLibrary - Singleton registry of node types
// ============================================================================
class BlueprintNodeLibrary
{
public:
    static BlueprintNodeLibrary& instance();

    // Register a node type
    void registerNodeType(const BlueprintNodeType& nodeType);

    // Get node type by name
    bool hasNodeType(const QString& typeName) const;
    const BlueprintNodeType& getNodeType(const QString& typeName) const;
    QVector<BlueprintNodeType> getAllNodeTypes() const;
    QVector<BlueprintNodeType> getNodeTypesByCategory(const QString& category) const;
    QStringList getCategories() const;

    // Create a new node instance from a registered type
    ui::GraphNode createNode(const QString& typeName, const QPointF& position = QPointF());

    // Initialize all built-in nodes
    void registerBuiltInNodes();

private:
    BlueprintNodeLibrary() = default;
    QMap<QString, BlueprintNodeType> m_nodeTypes;
};

}} // namespace ks::blueprint
