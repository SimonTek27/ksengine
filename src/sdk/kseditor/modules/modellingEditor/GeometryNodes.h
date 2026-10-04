#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QVariant>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>
#include <QVector3D>

#include "sdk/kseditor/engine/mesh/MeshOperations.h"

namespace ks {
namespace geometry_nodes {

enum class NodeType {
    Unknown,

    Input,
        InputPosition,
        InputNormal,
        InputUV,
        Color,
        Index,
        PointCount,
        SplinePoint,
        SplineLerp,

    Geometry,
        GeometryMesh,
        GeometryCurve,
        Points,
        Volume,

    Object,
        ObjectInfo,
        CollectionInfo,
        InstanceOnPoints,
        InstanceIndex,

    Point,
        MeshToPoints,
        CurveToPoints,
        DistributePoints,
        PointsToVolume,
        DuplicateElements,

    CurveType,
        Curveprimitive,
        MeshToCurve,
        FillCurve,

    MeshType,
        MeshPrimitive,
        MeshToVolume,
        VolumeToMesh,

    Transform,
        TransformOp,
        AlignEulerToVector,
        SetPosition,

    Read,
        ReadPosition,
        ReadNormal,
        ReadUV,
        MeshVertex,
        VertexCount,
        FaceArea,
        FaceSet,

    Write,
        WriteSetPosition,
        SetColor,
        WriteSetShadeSmooth,

    MathGroup,
        MathOp,
        VectorMath,

    Vector,
        VectorSetPosition,
        VectorNormal,
        VectorSetShadeSmooth,

    Utilities,
        Random,

    Group,
};

struct Socket {
    enum class Type {
        Geometry,
        Mesh,
        Curve,
        Points,
        Volume,
        String,
        Float,
        Int,
        Boolean,
        Vector,
        Color,
        Rotation,
        Matrix,
        Transform
    };

    QString name;
    Type type;
    QVariant defaultValue;
    QVariant value;
    bool isLinked = false;
};

struct Node {
    QUuid id;
    QString name;
    NodeType type;
    QVector<Socket> inputs;
    QVector<Socket> outputs;
    QMap<QString, QVariant> properties;

    int positionX = 0;
    int positionY = 0;
    bool hidden = false;

    Node() = default;
    explicit Node(NodeType t);

    QVariant getProperty(const QString& name, const QVariant& defaultValue = QVariant()) const;
    void setProperty(const QString& name, const QVariant& value);
};

struct Connection {
    QUuid fromNode;
    int fromSocket;
    QUuid toNode;
    int toSocket;
};

class NodeSocket {
public:
    NodeSocket() = default;
    ~NodeSocket() = default;

    Node* getNode() const { return m_node; }
    void setNode(Node* node) { m_node = node; }

    QString getName() const { return m_socket.name; }
    void setName(const QString& name) { m_socket.name = name; }

    Socket::Type getType() const { return m_socket.type; }
    void setType(Socket::Type type) { m_socket.type = type; }

    bool isLinked() const { return m_socket.isLinked; }
    void setLinked(bool linked) { m_socket.isLinked = linked; }

    bool hasConnection() const { return m_connectedSocket != nullptr; }
    NodeSocket* getConnectedSocket() const { return m_connectedSocket; }
    void setConnectedSocket(NodeSocket* socket) { m_connectedSocket = socket; }

    void transferData();

private:
    Node* m_node = nullptr;
    Socket m_socket;
    NodeSocket* m_connectedSocket = nullptr;
};

class NodeTree : public QObject {
    Q_OBJECT

public:
    explicit NodeTree(const QString& name, QObject* parent = nullptr);
    ~NodeTree();

    QString getName() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    void clear();
    void clearCaches();

    Node* addNode(NodeType type, int x, int y);
    void removeNode(const QUuid& id);
    Node* getNode(const QUuid& id);
    QVector<Node*> getNodes() const { return m_nodes; }

    void connectSockets(Node* fromNode, int fromSocket, Node* toNode, int toSocket);
    void disconnectSockets(Node* fromNode, int fromSocket, Node* toNode, int toSocket);

    MeshData execute(MeshData input);

    QString toJson() const;
    void fromJson(const QString& json);

signals:
    void treeChanged();
    void executionComplete();
    void nodeGraphModified();

private:
    QString m_name;
    QVector<Node*> m_nodes;
    QMap<QUuid, Node*> m_nodeMap;

    QVector<NodeSocket*> m_activeSockets;
    QVector<Connection> m_connections;

    void sortNodesByDependency();
    MeshData processNode(Node* node, MeshData input);
    QVariant getSocketValue(Node* node, int socketIndex);
};

class NodeEditor : public QObject {
    Q_OBJECT

public:
    explicit NodeEditor(QObject* parent = nullptr);
    ~NodeEditor();

    Q_INVOKABLE void newTree(const QString& name);
    Q_INVOKABLE void loadTree(const QString& path);
    Q_INVOKABLE void saveTree(const QString& path);
    Q_INVOKABLE bool saveCompound(const QString& name, const QString& path);
    Q_INVOKABLE bool loadCompound(const QString& path);
    Q_INVOKABLE QStringList compoundLibrary() const;

    Q_INVOKABLE void addNode(const QString& type, int x, int y);
    Q_INVOKABLE void removeNode(const QString& nodeId);
    Q_INVOKABLE void connectNodes(const QString& fromNode, int fromSocket, const QString& toNode, int toSocket);

    Q_INVOKABLE void setSocketValue(const QString& nodeId, int socketIndex, const QVariant& value);
    Q_INVOKABLE QVariant getSocketValue(const QString& nodeId, int socketIndex);

    QVector<NodeTree*> getLibrary() const { return m_library; }
    NodeTree* getActiveTree() const { return m_activeTree; }

    void addToLibrary(NodeTree* tree);
    void removeFromLibrary(int index);

signals:
    void treeAdded(NodeTree* tree);
    void treeRemoved(NodeTree* tree);
    void treeModified();

private:
    QVector<NodeTree*> m_library;
    NodeTree* m_activeTree = nullptr;
};

class GeometryNodeExecutor : public QObject {
    Q_OBJECT

public:
    explicit GeometryNodeExecutor(QObject* parent = nullptr);
    ~GeometryNodeExecutor();

    MeshData execute(NodeTree* tree, const MeshData& input);

    MeshData executeTransform(Node* node, const MeshData& input);
    MeshData executeMeshPrimitive(Node* node);
    MeshData executePosition(Node* node, const MeshData& input);
    MeshData executeSetPosition(Node* node, const MeshData& input);
    MeshData executeRandom(Node* node);
    MeshData executeMath(Node* node);
    MeshData executeCurveprimitive(Node* node);
    MeshData executeInstanceOnPoints(Node* node, const MeshData& input);
    MeshData executeMeshToPoints(Node* node, const MeshData& input);
    MeshData executeCurveToPoints(Node* node, const MeshData& input);
    MeshData executeDistributePoints(Node* node);
    MeshData executeDuplicateElements(Node* node, const MeshData& input);
    MeshData executeFillCurve(Node* node);
    MeshData executeVolumeToMesh(Node* node);
    MeshData executeSetShadeSmooth(Node* node, const MeshData& input);

signals:
    void executionStarted();
    void executionProgress(float progress);
    void executionComplete();

private:
    MeshData m_currentInput;
    MeshData m_currentOutput;
    float m_progress = 0.0f;

    bool isFloatSocket(Socket::Type type);
    bool isVectorSocket(Socket::Type type);
    QVector3D randomVector3D(int seed);
    float randomFloat(int seed);

    float getFloatInput(Node* node, int index, float defaultValue = 0.0f);
    int getIntInput(Node* node, int index, int defaultValue = 0);
    bool getBoolInput(Node* node, int index, bool defaultValue = false);
    QVector3D getVectorInput(Node* node, int index, const QVector3D& defaultValue = QVector3D());
};

}
}