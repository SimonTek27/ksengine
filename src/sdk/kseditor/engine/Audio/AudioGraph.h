#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QHash>
#include <QVariant>
#include <QUuid>
#include <functional>
#include <memory>
#include <map>

namespace ks {
namespace audio {

// Forward declarations
class AudioGraph;
class AudioNode;

enum class AudioPortType {
    Audio,
    Control,
    Event
};

enum class PortDirection {
    Input,
    Output
};

struct AudioPort {
    QUuid id;
    QString name;
    AudioPortType type = AudioPortType::Audio;
    PortDirection direction = PortDirection::Input;
    int index = 0;
    
    // Connection info
    QUuid connectedNodeId;
    QUuid connectedPortId;
    
    // Value for control ports
    QVariant value;
    
    // Audio buffer for audio ports (runtime)
    const float* inputBuffer = nullptr;
    float* outputBuffer = nullptr;
    int bufferSize = 0;
};

struct AudioNodeInfo {
    QString typeName;
    QString displayName;
    QString category;
    QString description;
    QVector<AudioPort> inputPorts;
    QVector<AudioPort> outputPorts;
    QMap<QString, QVariant> defaultParameters;
    bool isGenerator = false;
    bool isEffect = false;
    bool isAnalyzer = false;
    bool isOutput = false;
};

class AudioNode : public QObject {
    Q_OBJECT

public:
    explicit AudioNode(const QString& typeName, const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr);
    virtual ~AudioNode() = default;

    // Node identity
    QUuid id() const { return m_id; }
    QString typeName() const { return m_typeName; }
    QString displayName() const { return m_displayName; }
    void setDisplayName(const QString& name) { m_displayName = name; emit displayNameChanged(name); }

    // Port management
    virtual QVector<AudioPort> getInputPorts() const = 0;
    virtual QVector<AudioPort> getOutputPorts() const = 0;
    virtual void addInputPort(const AudioPort& port);
    virtual void addOutputPort(const AudioPort& port);
    virtual void removeInputPort(const QUuid& portId);
    virtual void removeOutputPort(const QUuid& portId);
    
    AudioPort* getInputPort(const QUuid& portId);
    AudioPort* getOutputPort(const QUuid& portId);
    const AudioPort* getInputPort(const QUuid& portId) const;
    const AudioPort* getOutputPort(const QUuid& portId) const;

    // Parameter management
    virtual QMap<QString, QVariant> getParameters() const { return m_parameters; }
    virtual void setParameter(const QString& name, const QVariant& value);
    virtual QVariant getParameter(const QString& name) const;
    virtual void setParameters(const QMap<QString, QVariant>& params);

    // Processing
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void process(const QMap<QUuid, const float*>& inputs, 
                         QMap<QUuid, float*>& outputs,
                         int numFrames) = 0;
    virtual void reset() = 0;

    // Graph connection
    void setGraph(AudioGraph* graph) { m_graph = graph; }
    AudioGraph* graph() const { return m_graph; }

    // State
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    // Serialization
    virtual QJsonObject toJson() const;
    virtual bool fromJson(const QJsonObject& obj);

signals:
    void parameterChanged(const QString& name, const QVariant& value);
    void displayNameChanged(const QString& name);
    void portsChanged();

protected:
    QUuid m_id;
    QString m_typeName;
    QString m_displayName;
    AudioGraph* m_graph = nullptr;
    QMap<QString, QVariant> m_parameters;
    QVector<AudioPort> m_inputPorts;
    QVector<AudioPort> m_outputPorts;
    bool m_enabled = true;
    bool m_bypass = false;
};

using AudioNodeFactory = std::function<std::unique_ptr<AudioNode>(const QUuid&)>;
using NodeFactoryMap = QMap<QString, AudioNodeFactory>;

class AudioGraph : public QObject {
    Q_OBJECT

public:
    explicit AudioGraph(QObject* parent = nullptr);
    ~AudioGraph() override;

    // Node management
    QUuid addNode(const QString& typeName, const QUuid& id = QUuid::createUuid());
    void removeNode(const QUuid& nodeId);
    AudioNode* getNode(const QUuid& nodeId);
    const AudioNode* getNode(const QUuid& nodeId) const;
    QVector<AudioNode*> getAllNodes() const;
    QVector<QUuid> getNodeIds() const;

    // Connection management
    bool connect(const QUuid& fromNodeId, const QUuid& fromPortId,
                 const QUuid& toNodeId, const QUuid& toPortId);
    bool disconnect(const QUuid& fromNodeId, const QUuid& fromPortId,
                    const QUuid& toNodeId, const QUuid& toPortId);
    void disconnectNode(const QUuid& nodeId);

    // Processing
    void prepare(double sampleRate, int maxBlockSize);
    void process(int numFrames);
    void reset();

    // Topological sort for processing order
    QVector<QUuid> getProcessingOrder() const;
    bool validate() const;

    // Factory registration
    static void registerNodeType(const QString& typeName, 
                                  const AudioNodeInfo& info,
                                  AudioNodeFactory factory);
    static QMap<QString, AudioNodeInfo> getAvailableNodeTypes();
    static QVector<QString> getNodeTypesByCategory(const QString& category);

    // Serialization
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj);

    // Graph properties
    void setName(const QString& name) { m_name = name; }
    QString name() const { return m_name; }
    void setSampleRate(double rate) { m_sampleRate = rate; }
    double sampleRate() const { return m_sampleRate; }

signals:
    void nodeAdded(const QUuid& nodeId);
    void nodeRemoved(const QUuid& nodeId);
    void connectionAdded(const QUuid& fromNode, const QUuid& fromPort,
                         const QUuid& toNode, const QUuid& toPort);
    void connectionRemoved(const QUuid& fromNode, const QUuid& fromPort,
                           const QUuid& toNode, const QUuid& toPort);
    void graphChanged();
    void processingError(const QString& error);

private:
    struct Connection {
        QUuid fromNode;
        QUuid fromPort;
        QUuid toNode;
        QUuid toPort;
    };

    QString m_name;
    double m_sampleRate = 44100.0;
    int m_maxBlockSize = 512;
    std::map<QUuid, std::unique_ptr<AudioNode>> m_nodes;
    QVector<Connection> m_connections;
    QVector<QUuid> m_processingOrder;
    bool m_needsReorder = true;
    QMap<QUuid, QVector<float>> m_audioBuffers;

    void rebuildProcessingOrder();
    void allocateBuffers();
    void clearBuffers();

    static QMap<QString, AudioNodeInfo> s_nodeInfos;
    static NodeFactoryMap s_nodeFactories;
};

} // namespace audio
} // namespace ks