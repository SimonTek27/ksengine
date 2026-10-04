#include "ShaderGraphWidget.h"
#include "resources/ui/NodeGraphScene.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace ks {
namespace graphics {

static QString portTypeToString(ShaderPortType t)
{
    switch (t) {
    case ShaderPortType::Float: return "float";
    case ShaderPortType::Float2: return "vec2";
    case ShaderPortType::Float3: return "vec3";
    case ShaderPortType::Float4: return "vec4";
    case ShaderPortType::Color: return "color";
    case ShaderPortType::Int: return "int";
    case ShaderPortType::Bool: return "bool";
    case ShaderPortType::Texture2D:
    case ShaderPortType::Texture3D:
    case ShaderPortType::TextureCube: return "texture";
    case ShaderPortType::Sampler: return "sampler";
    case ShaderPortType::Matrix2:
    case ShaderPortType::Matrix3:
    case ShaderPortType::Matrix4: return "mat4";
    case ShaderPortType::Generic: return "generic";
    }
    return "float";
}

static ShaderPortType stringToPortType(const QString& s)
{
    if (s == "float") return ShaderPortType::Float;
    if (s == "vec2") return ShaderPortType::Float2;
    if (s == "vec3") return ShaderPortType::Float3;
    if (s == "vec4") return ShaderPortType::Float4;
    if (s == "color") return ShaderPortType::Color;
    if (s == "int") return ShaderPortType::Int;
    if (s == "bool") return ShaderPortType::Bool;
    if (s == "texture") return ShaderPortType::Texture2D;
    if (s == "mat4") return ShaderPortType::Matrix4;
    return ShaderPortType::Float;
}

// ─── Conversion helpers ─────────────────────────────────────────────────

static void storePortsInProperties(const QVector<ShaderPort>& ports, bool isInput, QMap<QString, QVariant>& props)
{
    QString prefix = isInput ? "in" : "out";
    props[prefix + "_count"] = static_cast<int>(ports.size());
    for (int i = 0; i < ports.size(); ++i) {
        const ShaderPort& sp = ports[i];
        QString idx = QString::number(i);
        props[prefix + "_id_" + idx] = sp.id.toString();
        props[prefix + "_name_" + idx] = sp.name;
        props[prefix + "_type_" + idx] = portTypeToString(sp.type);
        props[prefix + "_desc_" + idx] = sp.description;
        props[prefix + "_default_" + idx] = sp.defaultValue;
        props[prefix + "_sort_" + idx] = sp.sortOrder;
    }
}

static QVector<ShaderPort> loadPortsFromProperties(const QMap<QString, QVariant>& props, bool isInput)
{
    QString prefix = isInput ? "in" : "out";
    int count = props.value(prefix + "_count", 0).toInt();
    QVector<ShaderPort> ports;
    ports.reserve(count);
    for (int i = 0; i < count; ++i) {
        QString idx = QString::number(i);
        ShaderPort sp;
        sp.id = QUuid::fromString(props.value(prefix + "_id_" + idx).toString());
        sp.name = props.value(prefix + "_name_" + idx).toString();
        sp.type = stringToPortType(props.value(prefix + "_type_" + idx).toString());
        sp.isInput = isInput;
        sp.description = props.value(prefix + "_desc_" + idx).toString();
        sp.defaultValue = props.value(prefix + "_default_" + idx);
        sp.sortOrder = props.value(prefix + "_sort_" + idx, 0).toInt();
        ports.append(sp);
    }
    return ports;
}

static void storeConnectionInProperties(const ShaderConnection& sc, QMap<QString, QVariant>& props, int index)
{
    QString idx = QString::number(index);
    props["conn_id_" + idx] = sc.id.toString();
    props["conn_fromNode_" + idx] = sc.fromNodeId.toString();
    props["conn_fromPort_" + idx] = sc.fromPortId.toString();
    props["conn_toNode_" + idx] = sc.toNodeId.toString();
    props["conn_toPort_" + idx] = sc.toPortId.toString();
    props["conn_color_" + idx] = sc.color.name();
}

static ShaderConnection loadConnectionFromProperties(const QMap<QString, QVariant>& props, int index)
{
    QString idx = QString::number(index);
    ShaderConnection sc;
    sc.id = QUuid::fromString(props.value("conn_id_" + idx).toString());
    sc.fromNodeId = QUuid::fromString(props.value("conn_fromNode_" + idx).toString());
    sc.fromPortId = QUuid::fromString(props.value("conn_fromPort_" + idx).toString());
    sc.toNodeId = QUuid::fromString(props.value("conn_toNode_" + idx).toString());
    sc.toPortId = QUuid::fromString(props.value("conn_toPort_" + idx).toString());
    sc.color = QColor(props.value("conn_color_" + idx).toString());
    return sc;
}

// ─── ShaderGraph → Widget ────────────────────────────────────────────────

void ShaderGraphWidget::syncGraphToWidget()
{
    auto* graph = m_mgr->getGraph(m_graphId);
    if (!graph) {
        m_graphId = m_mgr->createGraph("Untitled");
        graph = m_mgr->getGraph(m_graphId);
        if (!graph) return;
    }

    m_nodeGraph->clearAll();

    for (const ShaderNode& sn : graph->nodes) {
        ui::GraphNode gn;
        gn.id = sn.id;
        gn.typeName = sn.title;
        gn.title = sn.title;
        gn.position = sn.position;
        gn.size = QSizeF(sn.size);
        gn.selected = sn.selected;
        gn.error = sn.error;
        gn.errorMessage = sn.errorMessage;
        gn.headerColor = sn.headerColor;
        gn.comment = sn.comment;
        gn.minimized = sn.minimized;

        // Store shader node type in properties
        gn.properties["shaderNodeType"] = static_cast<int>(sn.type);
        gn.properties["shaderTypeName"] = sn.title;
        for (auto it = sn.properties.begin(); it != sn.properties.end(); ++it)
            gn.properties["prop_" + it.key()] = it.value();

        // Store port data in properties
        storePortsInProperties(sn.inputs, true, gn.properties);
        storePortsInProperties(sn.outputs, false, gn.properties);

        // Build port ID lists
        for (const ShaderPort& sp : sn.inputs)
            gn.inputPortIds.append(sp.id);
        for (const ShaderPort& sp : sn.outputs)
            gn.outputPortIds.append(sp.id);

        m_nodeGraph->addNode(gn);
    }

    // Store connections as metadata on nodes
    for (int i = 0; i < graph->connections.size(); ++i) {
        const ShaderConnection& sc = graph->connections[i];
        m_nodeGraph->addConnection(sc.fromNodeId, sc.fromPortId,
                                   sc.toNodeId, sc.toPortId);
    }
}

// ─── Widget → ShaderGraph ────────────────────────────────────────────────

void ShaderGraphWidget::syncWidgetToGraph()
{
    auto* graph = m_mgr->getGraph(m_graphId);
    if (!graph) return;

    graph->nodes.clear();
    graph->connections.clear();

    // Rebuild nodes from scene items
    for (QGraphicsItem* item : m_nodeGraph->scene()->items()) {
        auto* nodeItem = qgraphicsitem_cast<ui::GraphNodeItem*>(item);
        if (!nodeItem) continue;

        const ui::GraphNode& gn = nodeItem->nodeData();
        ShaderNode sn;
        sn.id = gn.id;
        sn.title = gn.title;
        sn.position = gn.position;
        sn.size = gn.size.toSize();
        sn.selected = gn.selected;
        sn.error = gn.error;
        sn.errorMessage = gn.errorMessage;
        sn.headerColor = gn.headerColor;
        sn.comment = gn.comment;
        sn.minimized = gn.minimized;

        auto typeVal = gn.properties.value("shaderNodeType");
        sn.type = typeVal.isValid()
            ? static_cast<ShaderNodeType>(typeVal.toInt())
            : ShaderNodeType::ConstantFloat;

        // Restore original node properties
        for (auto it = gn.properties.begin(); it != gn.properties.end(); ++it) {
            if (it.key().startsWith("prop_"))
                sn.properties[it.key().mid(5)] = it.value();
        }

        // Restore port data from properties
        sn.inputs = loadPortsFromProperties(gn.properties, true);
        sn.outputs = loadPortsFromProperties(gn.properties, false);

        graph->nodes.append(sn);
    }

    // Rebuild connections
    QList<QGraphicsItem*> allItems = m_nodeGraph->scene()->items();
    for (QGraphicsItem* item : allItems) {
        auto* connItem = qgraphicsitem_cast<ui::GraphConnectionItem*>(item);
        if (!connItem) continue;

        const auto& cd = connItem->connectionData();
        ShaderConnection sc;
        sc.id = cd.id;
        sc.fromNodeId = cd.fromNodeId;
        sc.fromPortId = cd.fromPortId;
        sc.toNodeId = cd.toNodeId;
        sc.toPortId = cd.toPortId;
        graph->connections.append(sc);
    }

    graph->modified = QDateTime::currentDateTime();
}

// ─── Constructor / Setup ─────────────────────────────────────────────────

ShaderGraphWidget::ShaderGraphWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupConnections();
}

void ShaderGraphWidget::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_nodeGraph = new ui::NodeGraphWidget(this);
    layout->addWidget(m_nodeGraph, 1);
}

void ShaderGraphWidget::setupConnections()
{
    connect(m_nodeGraph, &ui::NodeGraphWidget::graphChanged,
            this, &ShaderGraphWidget::onGraphChanged);
    connect(m_nodeGraph, &ui::NodeGraphWidget::nodeSelected,
            this, &ShaderGraphWidget::onNodeSelected);
    connect(m_nodeGraph, &ui::NodeGraphWidget::statusMessage,
            this, &ShaderGraphWidget::statusMessage);
}

void ShaderGraphWidget::setNodePalette(QTreeWidget* palette)
{
    m_nodePalette = palette;
}

void ShaderGraphWidget::setGraph(const QUuid& graphId)
{
    m_graphId = graphId;
    syncGraphToWidget();
}

// ─── Slots ───────────────────────────────────────────────────────────────

void ShaderGraphWidget::onNodeSelected(const QUuid& nodeId)
{
    updatePropertyEditor();
}

void ShaderGraphWidget::onGraphChanged()
{
    syncWidgetToGraph();
    emit graphChanged();
}

void ShaderGraphWidget::updatePropertyEditor()
{
    // Handled by NodeGraphWidget internally
}

void ShaderGraphWidget::onCompile()
{
    if (m_graphId.isNull()) return;

    auto result = m_mgr->validateGraph(m_graphId);
    if (!result.valid) {
        emit statusMessage("Validation failed — fix errors before compiling");
        return;
    }

    m_mgr->generateHLSL(m_graphId);
    emit statusMessage("Shader compiled successfully");
}

void ShaderGraphWidget::onValidate()
{
    if (m_graphId.isNull()) return;
    auto result = m_mgr->validateGraph(m_graphId);

    if (result.valid && result.warnings.isEmpty()) {
        emit statusMessage("Shader graph validation passed");
    } else if (result.valid) {
        emit statusMessage(QString("Validation passed with %1 warnings")
            .arg(result.warnings.size()));
    } else {
        emit statusMessage(QString("Validation failed: %1 errors, %2 warnings")
            .arg(result.errors.size()).arg(result.warnings.size()));
    }
}

} // namespace graphics
} // namespace ks
