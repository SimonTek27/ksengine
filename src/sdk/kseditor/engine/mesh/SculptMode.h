#pragma once
#include <QObject>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QMap>
#include <QPair>

class SculptMode : public QObject
{
    Q_OBJECT

public:
    enum SculptTool {
        ToolDraw,
        ToolFlatten,
        ToolSmooth,
        ToolCrease,
        ToolClip,
        ToolGrab,
        ToolElastic,
        ToolSnake,
        ToolInflate,
        ToolPin,
        ToolMask,
        ToolTrim,
        ToolKnife,
        ToolEdgeSlide,
        ToolRetopo
    };

    enum BrushType {
        BrushStandard,
        BrushElastic,
        BrushClay,
        BrushCrease,
        BrushFlatten,
        BrushInflate,
        BrushSmooth,
        BrushMask,
        BrushUnmask,
        BrushInvertMask
    };

    struct BrushSettings {
        float radius;
        float strength;
        float textureId;
        bool useTexture;
        bool useNormalFalloff;
        float normalFalloff;
        bool useAutoSmooth;
        float autoSmooth;
        bool useLockScreenX;
        int lockScreenX;
        bool useLockScreenY;
        int lockScreenY;
    };

    explicit SculptMode(QObject* parent = nullptr);
    ~SculptMode();

    void setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& faces);
    void setTool(SculptTool tool);
    void setBrushSize(float size);
    void setBrushStrength(float strength);

    void beginStroke(const QVector3D& point);
    void addPoint(const QVector3D& point, const QVector3D& normal, float pressure = 1.0f);
    void endStroke();

    void setRetopoMode(bool enabled);
    bool isRetopoMode() const { return m_retopoMode; }

    void quadDrawSplitEdge(int edgeIndex);

QVector<QVector3D> getVertices() const { 
        QVector<QVector3D> result; 
        result.reserve(m_vertices.size()); 
        for (const auto& v : m_vertices) result.append(v.position); 
        return result; 
    }
    QVector<int> getFaces() const { return m_faceIndices; }

    struct Vertex {
        QVector3D position;
        QVector3D normal;
        float mask = 0.0f;
    };

signals:
    void strokeStarted();
    void strokeUpdated();
    void strokeEnded();

private:
    struct StrokePoint {
        QVector3D position;
        QVector3D normal;
        float pressure;
    };

    SculptTool m_currentTool;
    BrushSettings m_brush;
    QVector<Vertex> m_vertices;
    QVector<int> m_faces;
    QVector<int> m_faceIndices;
    QVector<QVector3D> m_normals;

    bool m_isStroking;
    QVector<StrokePoint> m_currentStroke;

    void sculptPoint(QVector3D& vertex, const QVector3D& normal, float strength);
    void drawVertex(QVector3D& vertex, const QVector3D& direction, float strength);
    void flattenVertex(QVector3D& vertex, const QVector3D& sculptNormal, float strength);
    void smoothVertex(QVector3D& vertex, float strength);
    void creaseVertex(QVector3D& vertex, const QVector3D& normal, float strength);
    void grabVertex(QVector3D& vertex, const QVector3D& delta, float strength);
    void inflateVertex(QVector3D& vertex, float strength);
    void snakeHookVertex(QVector3D& vertex, const QVector3D& direction, float strength);
    int findNearestVertex(const QVector3D& point) const;
    QVector3D computeVertexNormal(int index) const;
    void rebuildNormals();

protected:
    bool m_retopoMode = false;
};

class SelectionTools : public QObject
{
    Q_OBJECT

public:
    enum SelectMode {
        SelectVertex,
        SelectEdge,
        SelectFace,
        SelectElement
    };

    enum SelectAction {
        ActionSet,
        ActionAdd,
        ActionSubtract,
        ActionIntersect
    };

    explicit SelectionTools(QObject* parent = nullptr);
    ~SelectionTools();

    void setSelectMode(SelectMode mode);
    void setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& faces);
    void setFaceMaterials(const QVector<int>& materials);

    const QVector<int> getFaceVertices(int faceIndex) const;
    QVector3D computeFaceNormal(int faceIndex) const;

    void selectAll();
    void selectNone();
    void selectInverse();

    void selectVertex(int index);
    void selectEdge(int v1, int v2);
    void selectFace(int faceIndex);

    void selectVertexLoop(int startVertex);
    void selectEdgeLoop(int startEdge);
    void selectFaceRing(int startFace);

    void selectVertexRing(int startVertex);
    void selectEdgeRing(int startEdge);

    void selectSimilar(SelectMode mode, const QString& property, float tolerance);
    void selectByNormal(const QVector3D& normal, float angleTolerance);
    void selectByMaterial(int materialId);

    void growSelection();
    void shrinkSelection();
    void selectBorder();

    const QVector<int>& getSelectedVertices() const { return m_selectedVertices; }
    const QVector<int>& getSelectedEdges() const { return m_selectedEdges; }
    const QVector<int>& getSelectedFaces() const { return m_selectedFaces; }

signals:
    void selectionChanged();

private:
    SelectMode m_mode;

    QVector<QVector3D> m_vertices;
    QVector<int> m_faces;

    QVector<int> m_selectedVertices;
    QVector<int> m_selectedEdges;
    QVector<int> m_selectedFaces;

    QMap<int, QVector<int>> m_vertexToFaces;
    QMap<int, QVector<int>> m_edgeToFaces;
    QVector<int> m_faceMaterials;

    void buildAdjacency();
    int findEdge(int v1, int v2) const;
    int findEdgeLoop(int startEdge, const QVector3D& direction);
    QVector3D computeEdgeDirection(int edgeIndex) const;
    QVector<int> getEdgeVertices(int edgeIndex) const;
    QVector<int> findAdjacentEdges(int edgeIndex) const;
    int oppositeVertex(int edgeIndex, int faceIndex) const;
};

class KnifeTool : public QObject
{
    Q_OBJECT

public:
    explicit KnifeTool(QObject* parent = nullptr);
    ~KnifeTool();

    enum Mode {
        ModeCut,
        ModeLoop
    };

    void setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& faces);
    void setMode(Mode mode);
    void setCutThrough(bool cutThrough);

    void beginCut(const QVector3D& start);
    void continueCut(const QVector3D& current);
    QVector<int> completeCut(const QVector3D& end);

    void splitEdgeAtPoint(int edgeIndex, const QVector3D& point);
    void addEdgeLoop(const QVector<QVector3D>& points);
    void connectCuts();

    const QVector<QVector3D>& getVertices() const { return m_newVertices; }
    const QVector<int>& getNewFaces() const { return m_newFaces; }

signals:
    void cutCompleted();

private:
    Mode m_mode;
    bool m_cutThrough;

    QVector<QVector3D> m_vertices;
    QVector<int> m_faces;

    QVector<QVector3D> m_cutPoints;
    QVector<QVector3D> m_newVertices;
    QVector<int> m_newFaces;

    float m_poisonThreshold;
    int m_maxSubdivisions;

    struct CutEdge {
        int edgeIndex;
        float t;
        QVector3D point;
    };

    QVector<CutEdge> m_pendingCuts;

    void findEdgeIntersections(const QVector3D& start, const QVector3D& end, QVector<CutEdge>& intersections);
    QVector3D pointOnEdge(int edgeIndex, float t) const;
    void subdivideEdge(int edgeIndex, float t);

    QPair<QVector3D, float> lineTriangleIntersect(const QVector3D& origin, const QVector3D& dir,
                                                  const QVector3D& v0, const QVector3D& v1,
                                                  float epsilon);
};

class LoopCutTool : public QObject
{
    Q_OBJECT

public:
    explicit LoopCutTool(QObject* parent = nullptr);
    ~LoopCutTool();

    void setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& faces);

    void setEdge(const QVector3D& point, const QVector3D& direction);
    void setNumberOfCuts(int count);
    void setCutPosition(float percentage, bool relativeToSelection);

    void addLoop();
    void removeLoop(int loopIndex);
    void slideLoop(int loopIndex, float delta);

    const QVector<QVector<QVector3D>>& getVertices() const { return m_newVertices; }
    const QVector<QVector<int>>& getFaces() const { return m_newFaces; }

signals:
    void loopAdded();
    void loopsUpdated();

private:
    QVector<QVector3D> m_vertices;
    QVector<int> m_faces;

    QVector<QVector<int>> m_edgeLoops;
    int m_numberOfCuts;
    float m_cutPosition;
    bool m_relativeToSelection = false;
    QVector3D m_edgePoint;
    QVector3D m_edgeDirection;

    QVector<QVector<QVector3D>> m_newVertices;
    QVector<QVector<int>> m_newFaces;
};

class BisectTool : public QObject
{
    Q_OBJECT

public:
    explicit BisectTool(QObject* parent = nullptr);
    ~BisectTool();

    void setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& faces);

    void setPlane(const QVector3D& origin, const QVector3D& normal);
    void setFillMode(int mode);
    void setClearInner(bool clear);
    void setClearOuter(bool clear);

    void apply(bool markDelete = true);

    const QVector<QVector3D>& getVertices() const { return m_resultVertices; }
    const QVector<int>& getFaces() const { return m_resultFaces; }

signals:
    void bisectApplied();

private:
    QVector3D m_planeOrigin;
    QVector3D m_planeNormal;
    int m_fillMode;
    bool m_clearInner;
    bool m_clearOuter;

    QVector<QVector3D> m_vertices;
    QVector<int> m_faces;

    QVector<QVector3D> m_resultVertices;
    QVector<int> m_resultFaces;

    QVector<int> classifyVertex(int index);
    QVector<int> findCrossingEdges(float threshold);
    void splitCrossingEdge(int edgeIndex, float t, QVector<QVector3D>& newVerts, QVector<int>& newFaces);
};