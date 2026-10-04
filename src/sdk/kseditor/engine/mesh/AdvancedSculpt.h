#pragma once
#include "SculptMode.h"
#include "MeshOperations.h"
#include <QQueue>
#include <QSet>

namespace ks {
namespace graphics {

class AdvancedSculptMode : public SculptMode
{
    Q_OBJECT

public:
    explicit AdvancedSculptMode(QObject* parent = nullptr);
    ~AdvancedSculptMode();

    enum class SculptModeType {
        Draw,
        DrawSharp,
        DrawCrease,
        Flatten,
        Smooth,
        Grab,
        GrabElastic,
        Elastic,
        Inflated,
        Pinch,
        PinchSlide,
        Snake,
        Nudge,
        Rotate,
        Slide,
        Cloth,
        Wax,
        ClayStrips,
        ClayThumb,
        Layer,
        Mask,
        Fill,
        FloodFill,
        Scrape,
        Plane,
        SlideRelax,
        Boundary,
        Inflator,
        Torque,
        Twist,
        Pull,
        Pivot
    };

    void setSculptMode(SculptModeType mode);
    SculptModeType getSculptMode() const { return m_sculptMode; }

    void setDyntopo(bool enabled);
    bool isDyntopoEnabled() const { return m_dyntopoEnabled; }

    void setRetopoMode(bool enabled);

    void setDetail(float detail);
    float getDetail() const { return m_detail; }

    enum class SymmetryMode { None, X, Y, Z, XY, XZ, XYZ };
    SymmetryMode symmetry = SymmetryMode::XYZ;

    void setDetailRefine(float refine);
    void setDetailScale(float scale);
    void setSymmetry(SymmetryMode mode);

    void setAutoMask(bool enabled);
    bool isAutoMaskEnabled() const { return m_autoMaskEnabled; }

    void addStroke(const QVector3D& position, const QVector3D& normal, float pressure, bool active);

    void sculptPoint(QVector3D& vertex, const QVector3D& normal, float strength);

signals:
    void dyntopoRefined();

private:
    enum class MaskOperation {
        Mask,
        Unmask,
        Invert,
        Clear,
        All,
        BySeams
    };

    struct DyntopoSettings {
        bool enabled = false;
        float detail = 8.0f;
        float detailRefine = 0.0f;
        float detailScale = 1.0f;
        enum class DetailMode { Constant, Brush };
        DetailMode detailMode = DetailMode::Brush;
        bool useConstantDetail = false;
        bool retopoMode = false;
    } dyntopo;

    struct MaskSettings {
        bool useOcclusion = true;
        float occlusionFalloff = 2.0f;
        bool useFrontfaces = true;
        bool useBackfaces = true;
    } masking;

    SculptModeType m_sculptMode;

    bool m_dyntopoEnabled;
    bool m_retopoMode;
    float m_detail;
    bool m_autoMaskEnabled;
    float m_brushRadius = 1.0f;
    float m_brushStrength = 0.5f;
    MeshData* m_mesh = nullptr;

    QVector<int> m_maskedVertices;
    QMap<int, float> m_autoMask;

    void refineDynamicTopology(const QVector3D& point, const QVector3D& normal);
    void collapseLongEdges();
    void removeDoubles();

    void maskVertex(int index, float maskValue);
    void unmaskVertex(int index);
    void invertMask();
    void clearMask();

    void smoothMask(int iterations);
    void growMask();
    void shrinkMask();

    float getAutoMaskValue(int vertex) const;
};

class SculptProject : public QObject {
    Q_OBJECT

public:
    explicit SculptProject(QObject* parent = nullptr);
    ~SculptProject();

    struct Layer {
        QString name;
        QVector<float> maskValues;
        float strength = 1.0f;
        int activeFrame = 0;
        bool visible = true;
    };

    void addLayer(const QString& name);
    void removeLayer(int index);
    void setActiveLayer(int index);
    int getActiveLayer() const { return m_activeLayer; }

    void addMaskStroke(int layerIndex, const QVector<int>& vertices, float strength);
    void undoLayerStroke(int layerIndex);
    void redoLayerStroke(int layerIndex);

    QVector<int> getMaskedVertices(int layer) const;

signals:
    void layerAdded(int index);
    void layerRemoved(int index);
    void activeLayerChanged(int index);
    void layerModified(int index);

private:
    QVector<Layer> m_layers;
    QVector<QVector<QVector<int>>> m_layerStrokes;
    QVector<QVector<QVector<int>>> m_redoStrokes;
    int m_activeLayer = 0;

    QMap<int, int> m_vertexToLayer;
    QMap<QPair<int, int>, float> m_maskCache;
};

class DyntopoRefiner : public QObject {
    Q_OBJECT

public:
    explicit DyntopoRefiner(QObject* parent = nullptr);
    ~DyntopoRefiner();

    void setDetail(float detail);
    void setOriginalDetail(float original);

    struct EdgeRef {
        int index = 0;
        int vertex1 = -1;
        int vertex2 = -1;
        int newVertex = -1;
        float length = 0.0f;
    };

    MeshData refine(MeshData input, const QVector3D& brushPosition, float radius);

    MeshData collapseEdges(MeshData input, float threshold);
    MeshData subdivideEdges(MeshData input, float length);

signals:
    void refinementComplete();

private:
    QVector<float> getEdgeLengths(MeshData input);
    QVector<EdgeRef> findCollapseCandidates(MeshData input, float threshold);
    QVector<EdgeRef> findSubdivisionCandidates(MeshData input, float length);

    void addVertex(MeshData& mesh, const QVector3D& position);
    void addFace(MeshData& mesh, int v1, int v2, int v3);

    float m_detail;
    float m_originalDetail;
    float m_detailRange[2] = {2.0f, 20.0f};
};

class SculptBrushPreset : public QObject {
    Q_OBJECT

public:
    explicit SculptBrushPreset(QObject* parent = nullptr);
    ~SculptBrushPreset();

    struct PresetData {
        QString name;

        AdvancedSculptMode::SculptModeType mode;

        float size;
        float strength;

        float autoSmoothFactor;
        int autoSmoothSteps;

        bool useAccumulate;
        float useDirection;

        bool useInvert;
        bool useFrontface;

        int shape;
        int textureId;

        struct Curve {
            QVector<float> curve;
            bool useFalloff = true;
            float curveMapping;
        } curve;

        struct NormalWeight {
            float normalWeight;
            bool useNormalWeight;
        } normalWeight;

        bool restrictUpperlip;
        bool restrictLowermouth;

        bool useLockedMask;
    };

    void addPreset(const PresetData& preset);
    void removePreset(int index);

    PresetData* getPreset(int index);
    QVector<PresetData> getPresets() const { return m_presets; }

signals:
    void presetAdded(int index);
    void presetRemoved(int index);
    void presetsLoaded();
    void presetsSaved();

private:
    QVector<PresetData> m_presets;

    void buildDefaultPresets();
    void loadPresets();
    void savePresets();
};

} // namespace graphics
} // namespace ks