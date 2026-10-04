#include "AudioGraph.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QQueue>
#include <QSet>
#include <QDebug>
#include <algorithm>

namespace ks {
namespace audio {

// Static member definitions
QMap<QString, AudioNodeInfo> AudioGraph::s_nodeInfos;
NodeFactoryMap AudioGraph::s_nodeFactories;

// ─── AudioPort ──────────────────────────────────────────────────────────────

// ─── AudioNode ──────────────────────────────────────────────────────────────

AudioNode::AudioNode(const QString& typeName, const QUuid& id, QObject* parent)
    : QObject(parent)
    , m_id(id.isNull() ? QUuid::createUuid() : id)
    , m_typeName(typeName)
    , m_displayName(typeName)
{
}

void AudioNode::addInputPort(const AudioPort& port) {
    AudioPort p = port;
    if (p.id.isNull()) p.id = QUuid::createUuid();
    p.direction = PortDirection::Input;
    p.index = m_inputPorts.size();
    m_inputPorts.append(p);
    emit portsChanged();
}

void AudioNode::addOutputPort(const AudioPort& port) {
    AudioPort p = port;
    if (p.id.isNull()) p.id = QUuid::createUuid();
    p.direction = PortDirection::Output;
    p.index = m_outputPorts.size();
    m_outputPorts.append(p);
    emit portsChanged();
}

void AudioNode::removeInputPort(const QUuid& portId) {
    for (int i = 0; i < m_inputPorts.size(); ++i) {
        if (m_inputPorts[i].id == portId) {
            m_inputPorts.removeAt(i);
            // Reindex
            for (int j = i; j < m_inputPorts.size(); ++j)
                m_inputPorts[j].index = j;
            emit portsChanged();
            return;
        }
    }
}

void AudioNode::removeOutputPort(const QUuid& portId) {
    for (int i = 0; i < m_outputPorts.size(); ++i) {
        if (m_outputPorts[i].id == portId) {
            m_outputPorts.removeAt(i);
            for (int j = i; j < m_outputPorts.size(); ++j)
                m_outputPorts[j].index = j;
            emit portsChanged();
            return;
        }
    }
}

AudioPort* AudioNode::getInputPort(const QUuid& portId) {
    for (auto& p : m_inputPorts) if (p.id == portId) return &p;
    return nullptr;
}

AudioPort* AudioNode::getOutputPort(const QUuid& portId) {
    for (auto& p : m_outputPorts) if (p.id == portId) return &p;
    return nullptr;
}

const AudioPort* AudioNode::getInputPort(const QUuid& portId) const {
    for (const auto& p : m_inputPorts) if (p.id == portId) return &p;
    return nullptr;
}

const AudioPort* AudioNode::getOutputPort(const QUuid& portId) const {
    for (const auto& p : m_outputPorts) if (p.id == portId) return &p;
    return nullptr;
}

void AudioNode::setParameter(const QString& name, const QVariant& value) {
    if (m_parameters[name] != value) {
        m_parameters[name] = value;
        emit parameterChanged(name, value);
    }
}

QVariant AudioNode::getParameter(const QString& name) const {
    return m_parameters.value(name);
}

void AudioNode::setParameters(const QMap<QString, QVariant>& params) {
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        setParameter(it.key(), it.value());
    }
}

QJsonObject AudioNode::toJson() const {
    QJsonObject obj;
    obj["id"] = m_id.toString(QUuid::WithoutBraces);
    obj["typeName"] = m_typeName;
    obj["displayName"] = m_displayName;
    obj["enabled"] = m_enabled;
    obj["bypass"] = m_bypass;
    
    QJsonObject params;
    for (auto it = m_parameters.constBegin(); it != m_parameters.constEnd(); ++it) {
        params[it.key()] = QJsonValue::fromVariant(it.value());
    }
    obj["parameters"] = params;
    
    // Ports
    QJsonArray inPorts, outPorts;
    for (const auto& p : m_inputPorts) {
        QJsonObject pObj;
        pObj["id"] = p.id.toString(QUuid::WithoutBraces);
        pObj["name"] = p.name;
        pObj["type"] = static_cast<int>(p.type);
        pObj["connectedNode"] = p.connectedNodeId.toString(QUuid::WithoutBraces);
        pObj["connectedPort"] = p.connectedPortId.toString(QUuid::WithoutBraces);
        inPorts.append(pObj);
    }
    for (const auto& p : m_outputPorts) {
        QJsonObject pObj;
        pObj["id"] = p.id.toString(QUuid::WithoutBraces);
        pObj["name"] = p.name;
        pObj["type"] = static_cast<int>(p.type);
        outPorts.append(pObj);
    }
    obj["inputPorts"] = inPorts;
    obj["outputPorts"] = outPorts;
    
    return obj;
}

bool AudioNode::fromJson(const QJsonObject& obj) {
    m_id = QUuid(obj["id"].toString());
    m_typeName = obj["typeName"].toString();
    m_displayName = obj["displayName"].toString();
    m_enabled = obj["enabled"].toBool(true);
    m_bypass = obj["bypass"].toBool(false);
    
    QJsonObject params = obj["parameters"].toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        m_parameters[it.key()] = it.value().toVariant();
    }
    
    // Ports would be restored by graph during connection phase
    return true;
}

// ─── AudioGraph ─────────────────────────────────────────────────────────────

AudioGraph::AudioGraph(QObject* parent) : QObject(parent) {
}

AudioGraph::~AudioGraph() {
    m_nodes.clear();
}

void AudioGraph::registerNodeType(const QString& typeName, 
                                   const AudioNodeInfo& info,
                                   AudioNodeFactory factory) {
    s_nodeInfos[typeName] = info;
    s_nodeFactories[typeName] = std::move(factory);
}

QMap<QString, AudioNodeInfo> AudioGraph::getAvailableNodeTypes() {
    return s_nodeInfos;
}

QVector<QString> AudioGraph::getNodeTypesByCategory(const QString& category) {
    QVector<QString> result;
    for (auto it = s_nodeInfos.constBegin(); it != s_nodeInfos.constEnd(); ++it) {
        if (it.value().category == category) result.append(it.key());
    }
    return result;
}

QUuid AudioGraph::addNode(const QString& typeName, const QUuid& id) {
    auto it = s_nodeFactories.find(typeName);
    if (it == s_nodeFactories.end()) {
        emit processingError("Unknown node type: " + typeName);
        return QUuid();
    }
    
    std::unique_ptr<AudioNode> node = (*it)(id.isNull() ? QUuid::createUuid() : id);
    if (!node) return QUuid();
    
    QUuid nodeId = node->id();
    node->setGraph(this);
    m_nodes[nodeId] = std::move(node);
    m_needsReorder = true;
    
    emit nodeAdded(nodeId);
    emit graphChanged();
    return nodeId;
}

void AudioGraph::removeNode(const QUuid& nodeId) {
    disconnectNode(nodeId);
    if (m_nodes.count(nodeId) > 0) {
        m_nodes.erase(nodeId); // unique_ptr will auto-delete
        m_needsReorder = true;
        emit nodeRemoved(nodeId);
        emit graphChanged();
    }
}

AudioNode* AudioGraph::getNode(const QUuid& nodeId) {
    auto it = m_nodes.find(nodeId);
    return it != m_nodes.end() ? it->second.get() : nullptr;
}

const AudioNode* AudioGraph::getNode(const QUuid& nodeId) const {
    auto it = m_nodes.find(nodeId);
    return it != m_nodes.end() ? it->second.get() : nullptr;
}

QVector<AudioNode*> AudioGraph::getAllNodes() const {
    QVector<AudioNode*> result;
    result.reserve(m_nodes.size());
    for (const auto& [id, node] : m_nodes) result.append(node.get());
    return result;
}

QVector<QUuid> AudioGraph::getNodeIds() const {
    QVector<QUuid> result;
    result.reserve(m_nodes.size());
    for (const auto& [id, node] : m_nodes) result.append(id);
    return result;
}

bool AudioGraph::connect(const QUuid& fromNodeId, const QUuid& fromPortId,
                         const QUuid& toNodeId, const QUuid& toPortId) {
    auto* fromNode = getNode(fromNodeId);
    auto* toNode = getNode(toNodeId);
    if (!fromNode || !toNode) return false;
    
    auto* fromPort = fromNode->getOutputPort(fromPortId);
    auto* toPort = toNode->getInputPort(toPortId);
    if (!fromPort || !toPort) return false;
    
    if (fromPort->type != toPort->type) return false;
    
    // Disconnect existing connection on target port
    if (!toPort->connectedNodeId.isNull()) {
        disconnect(toPort->connectedNodeId, toPort->connectedPortId, toNodeId, toPortId);
    }
    
    Connection conn{fromNodeId, fromPortId, toNodeId, toPortId};
    m_connections.append(conn);
    
    // Update port connection info
    fromPort->connectedNodeId = toNodeId;
    fromPort->connectedPortId = toPortId;
    toPort->connectedNodeId = fromNodeId;
    toPort->connectedPortId = fromPortId;
    
    m_needsReorder = true;
    emit connectionAdded(fromNodeId, fromPortId, toNodeId, toPortId);
    emit graphChanged();
    return true;
}

bool AudioGraph::disconnect(const QUuid& fromNodeId, const QUuid& fromPortId,
                            const QUuid& toNodeId, const QUuid& toPortId) {
    for (int i = 0; i < m_connections.size(); ++i) {
        auto& c = m_connections[i];
        if (c.fromNode == fromNodeId && c.fromPort == fromPortId &&
            c.toNode == toNodeId && c.toPort == toPortId) {
            
            // Clear port connection info
            if (auto* fromNode = getNode(fromNodeId)) {
                if (auto* p = fromNode->getOutputPort(fromPortId)) {
                    p->connectedNodeId = QUuid();
                    p->connectedPortId = QUuid();
                }
            }
            if (auto* toNode = getNode(toNodeId)) {
                if (auto* p = toNode->getInputPort(toPortId)) {
                    p->connectedNodeId = QUuid();
                    p->connectedPortId = QUuid();
                }
            }
            
            m_connections.removeAt(i);
            m_needsReorder = true;
            emit connectionRemoved(fromNodeId, fromPortId, toNodeId, toPortId);
            emit graphChanged();
            return true;
        }
    }
    return false;
}

void AudioGraph::disconnectNode(const QUuid& nodeId) {
    // Remove all connections involving this node
    for (int i = m_connections.size() - 1; i >= 0; --i) {
        auto& c = m_connections[i];
        if (c.fromNode == nodeId || c.toNode == nodeId) {
            if (auto* fromNode = getNode(c.fromNode)) {
                if (auto* p = fromNode->getOutputPort(c.fromPort)) {
                    p->connectedNodeId = QUuid();
                    p->connectedPortId = QUuid();
                }
            }
            if (auto* toNode = getNode(c.toNode)) {
                if (auto* p = toNode->getInputPort(c.toPort)) {
                    p->connectedNodeId = QUuid();
                    p->connectedPortId = QUuid();
                }
            }
            emit connectionRemoved(c.fromNode, c.fromPort, c.toNode, c.toPort);
            m_connections.removeAt(i);
        }
    }
    m_needsReorder = true;
    emit graphChanged();
}

void AudioGraph::prepare(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    
    for (auto& [id, node] : m_nodes) {
        node->prepare(sampleRate, maxBlockSize);
    }
    
    allocateBuffers();
}

void AudioGraph::process(int numFrames) {
    if (m_needsReorder) rebuildProcessingOrder();
    
    // Clear output buffers
    for (auto& buf : m_audioBuffers) buf.fill(0.0f);
    
    // Process in topological order
    for (const auto& nodeId : m_processingOrder) {
        auto* node = getNode(nodeId);
        if (!node || !node->isEnabled()) continue;
        
        // Gather input buffers
        QMap<QUuid, const float*> inputs;
        for (const auto& port : node->getInputPorts()) {
            if (!port.connectedNodeId.isNull()) {
                auto* srcNode = getNode(port.connectedNodeId);
                if (srcNode) {
                    auto* srcPort = srcNode->getOutputPort(port.connectedPortId);
                    if (srcPort && srcPort->outputBuffer) {
                        inputs[port.id] = srcPort->outputBuffer;
                    }
                }
            } else if (port.inputBuffer) {
                // Direct input buffer
                inputs[port.id] = port.inputBuffer;
            }
        }
        
        // Prepare output buffers
        QMap<QUuid, float*> outputs;
        auto* mutableNode = const_cast<AudioNode*>(node);
        for (auto& port : mutableNode->getOutputPorts()) {
            auto it = m_audioBuffers.find(port.id);
            if (it != m_audioBuffers.end()) {
                outputs[port.id] = it->data();
                port.outputBuffer = it->data(); // For downstream nodes
            }
        }
        
        node->process(inputs, outputs, numFrames);
    }
}

void AudioGraph::reset() {
    for (auto& [id, node] : m_nodes) {
        node->reset();
    }
    clearBuffers();
}

QVector<QUuid> AudioGraph::getProcessingOrder() const {
    return m_processingOrder;
}

bool AudioGraph::validate() const {
    // Check for cycles
    QMap<QUuid, int> inDegree;
    QMap<QUuid, QVector<QUuid>> adj;
    
    for (const auto& [id, node] : m_nodes) {
        inDegree[id] = 0;
    }
    
    for (const auto& c : m_connections) {
        adj[c.fromNode].append(c.toNode);
        inDegree[c.toNode]++;
    }
    
    QQueue<QUuid> queue;
    for (auto it = inDegree.constBegin(); it != inDegree.constEnd(); ++it) {
        if (it.value() == 0) queue.enqueue(it.key());
    }
    
    int processed = 0;
    while (!queue.isEmpty()) {
        auto u = queue.dequeue();
        processed++;
        for (const auto& v : adj[u]) {
            if (--inDegree[v] == 0) queue.enqueue(v);
        }
    }
    
    return processed == m_nodes.size();
}

void AudioGraph::rebuildProcessingOrder() {
    if (!validate()) {
        emit processingError("Graph contains cycles!");
        m_processingOrder.clear();
        m_needsReorder = false;
        return;
    }
    
    // Kahn's algorithm
    QMap<QUuid, int> inDegree;
    QMap<QUuid, QVector<QUuid>> adj;
    
    for (const auto& [id, node] : m_nodes) {
        inDegree[id] = 0;
    }
    
    for (const auto& c : m_connections) {
        adj[c.fromNode].append(c.toNode);
        inDegree[c.toNode]++;
    }
    
    QQueue<QUuid> queue;
    for (auto it = inDegree.constBegin(); it != inDegree.constEnd(); ++it) {
        if (it.value() == 0) queue.enqueue(it.key());
    }
    
    m_processingOrder.clear();
    while (!queue.isEmpty()) {
        auto u = queue.dequeue();
        m_processingOrder.append(u);
        for (const auto& v : adj[u]) {
            if (--inDegree[v] == 0) queue.enqueue(v);
        }
    }
    
    m_needsReorder = false;
    allocateBuffers();
}

void AudioGraph::allocateBuffers() {
    clearBuffers();
    for (const auto& nodeId : m_processingOrder) {
        auto* node = getNode(nodeId);
        if (!node) continue;
        for (const auto& port : node->getOutputPorts()) {
            if (port.type == AudioPortType::Audio) {
                m_audioBuffers[port.id].resize(m_maxBlockSize);
            }
        }
    }
}

void AudioGraph::clearBuffers() {
    m_audioBuffers.clear();
}

QJsonObject AudioGraph::toJson() const {
    QJsonObject obj;
    obj["name"] = m_name;
    obj["sampleRate"] = m_sampleRate;
    
    QJsonArray nodesArr;
    for (const auto& [id, node] : m_nodes) {
        nodesArr.append(node->toJson());
    }
    obj["nodes"] = nodesArr;
    
    QJsonArray connsArr;
    for (const auto& c : m_connections) {
        QJsonObject cObj;
        cObj["fromNode"] = c.fromNode.toString(QUuid::WithoutBraces);
        cObj["fromPort"] = c.fromPort.toString(QUuid::WithoutBraces);
        cObj["toNode"] = c.toNode.toString(QUuid::WithoutBraces);
        cObj["toPort"] = c.toPort.toString(QUuid::WithoutBraces);
        connsArr.append(cObj);
    }
    obj["connections"] = connsArr;
    
    return obj;
}

bool AudioGraph::fromJson(const QJsonObject& obj) {
    m_name = obj["name"].toString();
    m_sampleRate = obj["sampleRate"].toDouble(44100.0);
    
    // Clear existing
    m_nodes.clear();
    m_connections.clear();
    
    // Create nodes first
    QJsonArray nodesArr = obj["nodes"].toArray();
    for (const auto& v : nodesArr) {
        QJsonObject nObj = v.toObject();
        QString typeName = nObj["typeName"].toString();
        QUuid id = QUuid(nObj["id"].toString());
        
        auto it = s_nodeFactories.find(typeName);
        if (it == s_nodeFactories.end()) {
            qWarning() << "Unknown node type in graph:" << typeName;
            continue;
        }
        
        std::unique_ptr<AudioNode> node = (*it)(id);
        if (node) {
            node->fromJson(nObj);
            node->setGraph(this);
            m_nodes[id] = std::move(node);
        }
    }
    
    // Restore connections
    QJsonArray connsArr = obj["connections"].toArray();
    for (const auto& v : connsArr) {
        QJsonObject cObj = v.toObject();
        Connection c;
        c.fromNode = QUuid(cObj["fromNode"].toString());
        c.fromPort = QUuid(cObj["fromPort"].toString());
        c.toNode = QUuid(cObj["toNode"].toString());
        c.toPort = QUuid(cObj["toPort"].toString());
        
        // Update port connection info
        if (auto* fromNode = getNode(c.fromNode)) {
            if (auto* p = fromNode->getOutputPort(c.fromPort)) {
                p->connectedNodeId = c.toNode;
                p->connectedPortId = c.toPort;
            }
        }
        if (auto* toNode = getNode(c.toNode)) {
            if (auto* p = toNode->getInputPort(c.toPort)) {
                p->connectedNodeId = c.fromNode;
                p->connectedPortId = c.fromPort;
            }
        }
        
        m_connections.append(c);
    }
    
    m_needsReorder = true;
    emit graphChanged();
    return true;
}

} // namespace audio
} // namespace ks