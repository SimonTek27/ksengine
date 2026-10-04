#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include <QMatrix4x4>
#include <QFlags>
#include <QEnableSharedFromThis>
#include "MeshOperations.h"
#include "SubdivisionSurface.h"

namespace ks {

enum class ModifierType {
    None,
    Generate,
    Deform,
    Physics,
    Remesh,
    WeightVGPaint,
    WeightPaint,
    VertexPaint
};

enum class ModifierMode {
    None = 0,
    Realtime = 1,
    Render = 2,
    Editmode = 4
};
Q_DECLARE_FLAGS(ModifierModes, ModifierMode)
Q_DECLARE_OPERATORS_FOR_FLAGS(ModifierModes)

class Modifier;
typedef QSharedPointer<Modifier> ModifierPtr;

class Modifier : public QEnableSharedFromThis<Modifier> {
public:
    Modifier(const QString& name, ModifierType type);
    virtual ~Modifier() = default;

    QString name;
    ModifierType type = ModifierType::None;
    ModifierModes modes = ModifierMode::Realtime | ModifierMode::Render;

    bool isEnabled = true;
    bool showInEditmode = true;
    bool showRender = true;
    int index = 0;

    virtual ModifierType getModifierType() const { return type; }
    virtual MeshData apply(const MeshData& input) = 0;
    virtual MeshData applyEdit(const MeshData& input) { return apply(input); }

    virtual void readParameters(const QMap<QString, QVariant>& params) {}
    virtual QMap<QString, QVariant> writeParameters() const { return QMap<QString, QVariant>(); }

    virtual QString getDescription() const { return QString(); }

    bool canApply(const MeshData& mesh) const {
        return isEnabled && mesh.vertices.size() > 0;
    }

    int executionSpace() const;
    int executionMask() const;
};

class GenerateModifier : public Modifier {
public:
    GenerateModifier(const QString& name);
    ModifierType getModifierType() const override { return ModifierType::Generate; }
};

class DeformModifier : public Modifier {
public:
    DeformModifier(const QString& name);
    ModifierType getModifierType() const override { return ModifierType::Deform; }
};

class MirrorModifier : public GenerateModifier {
public:
    MirrorModifier();

    enum class Axis { X, Y, Z };
    QFlags<Axis> mirrorAxes;
    bool mergeThreshold = true;
    float tolerance = 0.001f;
    bool mergeVertices = true;
    bool flipUVs = false;
    bool flipNormals = false;

    MeshData apply(const MeshData& input) override;

    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class ArrayModifier : public GenerateModifier {
public:
    ArrayModifier();

    int count = 4;
    float length = 1.0f;
    QVector3D constantOffset;
    QVector3D relativeOffset = {1.0f, 0, 0};
    QVector3D offsetObject;
    bool useConstantOffset = true;
    bool useRelativeOffset = true;
    bool useObjectOffset = false;

    int fitType = 0;
    float fitLength = 1.0f;
    QString fitPath;

    bool mergeVertices = false;
    float mergeThreshold = 0.001f;
    bool capFirst = false;
    bool capLast = false;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class BevelModifier : public DeformModifier {
public:
    BevelModifier();

    float width = 0.01f;
    int segments = 1;
    float angleLimit = qDegreesToRadians(30.0f);
    bool useClampOverlap = true;
    float clampOverlap = 0.01f;

    enum class LimitMethod { Angle, Weight, VertexGroup };
    LimitMethod limitMethod = LimitMethod::Angle;

    bool bevelVertexOnly = false;
    float vertexWeight = 1.0f;
    float profileShape = 0.5f;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class SolidifyModifier : public GenerateModifier {
public:
    SolidifyModifier();

    float thickness = 0.01f;
    float offset = 1.0f;
    bool useFlipNormals = false;
    bool useEvenOffset = true;

    enum class SolidifyMode { Outside, Inside, Both };
    SolidifyMode mode = SolidifyMode::Outside;

    bool useOutwardFacetCulling = false;
    bool useInwardSolidify = false;

    float creaseInner = 0.0f;
    float creaseOuter = 0.0f;
    float creaseRim = 0.0f;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class SubdivisionModifier : public GenerateModifier {
public:
    SubdivisionModifier();

    int levels = 1;
    int renderLevels = 3;
    bool useOptimalDisplay = true;

    enum class SubdivisionType { CatmullClark, Simple };
    SubdivisionType subdivisionType = SubdivisionType::CatmullClark;

    enum class BoundarySmoothing { All, KeepCorners };
    BoundarySmoothing boundarySmooth = BoundarySmoothing::All;

    // Crease edges (TurboSmooth-style) fed to the OpenSubdiv refiner.
    // An edge weight of 1.0 fully sharpens the edge; 0.0 = smooth.
    QVector<CreaseEdge> creases;
    // Pinned vertices: indices held fixed through subdivision.
    QVector<int> pinnedVertices;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class DecimateModifier : public GenerateModifier {
public:
    DecimateModifier();

    float ratio = 0.5f;
    int vertexCount = 0;

    enum class DecimateType { Collapse, Unsubdivide, Planar };
    DecimateType decimateType = DecimateType::Collapse;

    float angleLimit = qDegreesToRadians(5.0f);
    bool useDelimit = true;
    bool delimitNormal = true;
    bool delimitMaterial = true;

    int triangulateMaxAngle = qDegreesToRadians(180.0f);
    bool triangulate = false;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class DisplaceModifier : public DeformModifier {
public:
    DisplaceModifier();

    float strength = 1.0f;
    float midlevel = 0.5f;
    QString textureName;
    QString texturePath;

    enum class TextureCoordinates { Local, UV, Object, Normal };
    TextureCoordinates textureCoordinates = TextureCoordinates::UV;

    QString objectName;
    int uvLayer = 0;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class SmoothModifier : public DeformModifier {
public:
    SmoothModifier();

    float factor = 0.5f;
    int iterations = 10;

    enum class SmoothAxis { X, Y, Z, All };
    QFlags<SmoothAxis> smoothAxes;

    enum class SmoothMode { Linear, Laplacian };
    SmoothMode smoothMode = SmoothMode::Laplacian;

    float lambdaFactor = 0.5f;
    float lambdaBorder = 0.5f;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class CastModifier : public DeformModifier {
public:
    CastModifier();

    float radius = 1.0f;
    float factor = 0.0f;
    float fromRadius = 0.0f;
    float toRadius = 1.0f;

    enum class CastType { Sphere, Cylinder };
    CastType castType = CastType::Sphere;

    bool useX = true;
    bool useY = true;
    bool useZ = true;

    int size = 0;
    float radiusFactor = 0.0f;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class LatticeModifier : public DeformModifier {
public:
    LatticeModifier();

    QString objectName;
    int u = 2, v = 2, w = 2;

    enum class LatticeMode { Outside, Inside, Both };
    LatticeMode mode = LatticeMode::Outside;

    float strength = 1.0f;

    MeshData apply(const MeshData& input) override;
};

class WaveModifier : public DeformModifier {
public:
    WaveModifier();

    float amplitude = 0.3f;
    float amplitudeSquare = 0.3f;
    float frequency = 1.0f;
    float speed = 1.0f;
    float phaseMultiplier = 1.0f;
    float timeOffset = 0.0f;
    float m_elapsed = 0.0f;

    bool useX = true;
    bool useY = true;
    bool useZ = false;

    enum class WaveType { X, Y, Z };
    WaveType waveType = WaveType::X;

    float falloffRadius = 0.0f;
    float startRadius = 0.0f;

    MeshData apply(const MeshData& input) override;
};

class ShrinkwrapModifier : public DeformModifier {
public:
    ShrinkwrapModifier();

    QString targetName;
    MeshData targetMeshData;
    enum class ShrinkType { NearestSurfacePoint, Projection, NearestVertex };
    ShrinkType shrinkType = ShrinkType::NearestSurfacePoint;

    float offset = 0.0f;
    QVector3D direction = {0, 0, 1};

    bool usePositiveDirection = true;
    bool useNegativeDirection = false;
    bool useSubsurf = true;

    enum class WrapMethod { Below, Above, Center };
    WrapMethod wrapMethod = WrapMethod::Below;

    MeshData apply(const MeshData& input) override;
};

class SkinModifier : public GenerateModifier {
public:
    SkinModifier();

    int branchSmoothing = 1;
    bool useSmoothShade = false;

    MeshData apply(const MeshData& input) override;
};

class TriangulateModifier : public GenerateModifier {
public:
    TriangulateModifier();

    int minVertices = 4;
    bool useBeauty = true;
    bool useNgonEnabled = true;

    enum class QuadMethod { Static, Beauty, Fixed, FixedAlternate, ShortestDiagonal };
    enum class TriangleMethod { Static, Beauty, FixedAlternate, Fixed };

    QuadMethod quadMethod = QuadMethod::Beauty;
    TriangleMethod triangleMethod = TriangleMethod::Beauty;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class WireframeModifier : public GenerateModifier {
public:
    WireframeModifier();

    float thickness = 0.01f;
    bool useBoundary = false;
    bool useDissolve = false;
    float dissolvePercentage = 0.0f;

    bool useSupport = false;
    bool useSmooth = false;
    bool useSolid = false;

    float creaseWeight = 0.0f;
    float offset = 0.0f;

    enum class OffsetType { Width, Depth };
    OffsetType offsetType = OffsetType::Width;

    MeshData apply(const MeshData& input) override;
};

class ShapeKeyModifier : public DeformModifier {
public:
    ShapeKeyModifier();

    struct ShapeKeyTarget {
        QString name;
        float weight = 0.0f;
        float min = 0.0f;
        float max = 1.0f;
        bool mute = false;
    };

    QVector<ShapeKeyTarget> targets;

    void addTarget(const QString& name);
    void removeTarget(int index);
    void setTargetWeight(int index, float weight);

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class RemeshModifier : public GenerateModifier {
public:
    RemeshModifier();

    float scale = 0.99f;
    int octreeDepth = 4;
    int iterations = 4;

    enum class RemeshType { Mode, Sharp, Smooth };
    RemeshType remeshType = RemeshType::Mode;

    bool useCorrectAspect = true;
    bool useEdgeConstraints = true;
    bool useSymmetryX = false;
    bool useSymmetryY = false;

    MeshData apply(const MeshData& input) override;
};

class SkinData {
public:
    QMap<int, int> vertexReferences;
    QMap<int, float> vertexRadii;
};

}