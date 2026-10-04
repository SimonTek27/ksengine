#pragma once
#include "sdk/kseditor/engine/mesh/ModifierSystem.h"
#include "sdk/kseditor/engine/mesh/SkeletonSystem.h"

namespace ks {

class WireframeModifierEx : public GenerateModifier {
public:
    WireframeModifierEx();

    float thickness = 0.01f;
    bool useReplaceOriginal = true;
    bool useBoundary = true;
    bool useSmoothNormals = true;
    float creaseWeight = 0.0f;

    enum class BoundMesh { Complement, Intersection };
    BoundMesh boundMesh = BoundMesh::Complement;

    MeshData apply(const MeshData& input) override;
};

class SkinModifierEx : public GenerateModifier {
public:
    SkinModifierEx();

    QVector<Bone> m_skeleton;

    enum class NormalsMode { Preserve, Recalculate };
    NormalsMode normalsMode = NormalsMode::Preserve;

    bool useShell = true;
    float shellCount = 1;

    MeshData apply(const MeshData& input) override;

private:
    void buildSkeleton(const MeshData& mesh);
    QVector<QVector<int>> findAdjacency(const MeshData& mesh);
};

class DisplaceModifierEx : public DeformModifier {
public:
    DisplaceModifierEx();

    enum class TextureType { Image, Cloud, Cloth, Marble, Musgrave, Voronoi, Wood };
    TextureType textureType = TextureType::Cloud;

    float strength = 1.0f;
    float midlevel = 0.5f;

    float uScale = 1.0f;
    float vScale = 1.0f;
    float wScale = 1.0f;

    enum class Coordinates { Local, Global, Normal, Object, UV };
    Coordinates coordinates = Coordinates::Local;

    QString textureName;
    int textureInternalId = -1;

    float textureOffset[3] = {0, 0, 0};
    float textureScale[3] = {1, 1, 1};

    MeshData apply(const MeshData& input) override;

private:
    float cloud(float x, float y, float z);
    float marble(float x, float y, float z);
    float voronoi(float x, float y, float z);
    QVector3D noise3D(float x, float y, float z);
};

class SimpleDeformModifier : public DeformModifier {
public:
    SimpleDeformModifier();

    enum class DeformMethod { Stretch, Twist, Bend, Linear };
    DeformMethod deformMethod = DeformMethod::Twist;

    enum class Axis { X, Y, Z, AUTO };
    Axis deformAxis = Axis::X;

    float angle = 0.0f;
    float maxAngle = 1.5708f;

    float factor = 1.0f;
    bool useAngle = false;
    bool useFactor = true;

    float locked = 0.0f;

    MeshData apply(const MeshData& input) override;

private:
    MeshData applyTwist(const MeshData& input, const QVector3D& axis, float angle);
    MeshData applyStretch(const MeshData& input, const QVector3D& axis, float factor);
    MeshData applyBend(const MeshData& input, const QVector3D& axis, float angle);
    MeshData applyLinear(const MeshData& input, const QVector3D& axis, float factor);
};

class CorrectiveSmoothModifier : public DeformModifier {
public:
    CorrectiveSmoothModifier();

    enum class SmoothType { LengthDistance, AngleDistance };
    SmoothType smoothType = SmoothType::LengthDistance;

    float iterations = 10;
    float lambdaFactor = 0.5f;
    float lambdaBias = 0.5f;

    bool useSmoothBoneEnds = true;
    bool usePinBoundary = true;

    float pinWeight = 1.0f;

    int smoothStep = 1;

    float repeat = 1.0f;

    MeshData apply(const MeshData& input) override;

private:
    QMap<int, QVector3D> getPins(const MeshData& mesh, const QStringList& vertexGroup);
    MeshData smoothMesh(const MeshData& mesh, const QMap<int, QVector3D>& pins, int iterations);
};

class CurveModifier : public DeformModifier {
public:
    CurveModifier();

    QString curveObject;
    QString vertexGroup;

    enum class DeformAxis { X, Y, Z, NEG_X, NEG_Y, NEG_Z };
    DeformAxis deformAxis = DeformAxis::X;

    bool useEndPoints = true;
    bool useHelpPoints = false;

    float radius = 1.0f;
    bool useScaleX = true;

    MeshData apply(const MeshData& input) override;

private:
    MeshData deformToCurve(const MeshData& mesh, const MeshData& curve);
    QVector3D getPointOnCurve(const MeshData& curve, float t);
};

class LatticeModifierEx : public DeformModifier {
public:
    LatticeModifierEx();

    QString latticeObject;

    enum class DeformAxis { X, Y, Z, NEG_X, NEG_Y, NEG_Z };
    DeformAxis deformAxis = DeformAxis::Y;

    float strength = 1.0f;

    MeshData apply(const MeshData& input) override;
};

class WaveModifierEx : public DeformModifier {
public:
    WaveModifierEx();

    enum class TimeOffset { None, Linear, Quadratic, Sinusoidal };
    TimeOffset timeOffset = TimeOffset::None;
    bool useNormal = true;
    bool useCenter = true;

    float timeOffsetValue = 0.0f;
    float height = 0.5f;
    float width = 2.0f;
    float Narrowness = 1.0f;
    float phase = 0.0f;

    float speed = 1.0f;
    float lifetime = 0.0f;

    bool use_normal = false;

    int offset[3] = {0, 0, 0};
    int position[3] = {0, 0, 0};

    MeshData apply(const MeshData& input) override {
        MeshData output = input;
        float time = lifetime;
        for (int i = 0; i < output.vertices.size(); ++i) {
            auto& v = output.vertices[i];
            float w = waveFunction(v.position.x(), v.position.y(), v.position.z(), time);
            QVector3D dir = useNormal ? v.normal : QVector3D(0, 0, 1);
            if (dir.lengthSquared() > 0.0001f) dir.normalize();
            v.position += dir * w;
        }
        output.computeNormals();
        return output;
    }

private:
    float waveFunction(float x, float y, float z, float time);
};

class CastModifierEx : public DeformModifier {
public:
    CastModifierEx();

    enum class CastType { Sphere, Cylinder, Cubic };
    CastType castType = CastType::Sphere;

    float strength = 1.0f;
    float radius = 1.0f;
    float size = 1.0f;

    bool useX = true;
    bool useY = true;
    bool useZ = true;

    enum class DeformAxis { X, Y, Z, AUTO };
    DeformAxis deformAxis = DeformAxis::AUTO;

    MeshData apply(const MeshData& input) override;
};

class SimpleSmoothModifier : public DeformModifier {
public:
    SimpleSmoothModifier();

    int iterations = 10;
    float lambdaFactor = 0.5f;
    float lambdaBias = 0.0f;

    bool useNormalSmooth = true;
    bool useVolumePreserve = true;

    MeshData apply(const MeshData& input) override;
};

class SmoothModifierEx : public DeformModifier {
public:
    SmoothModifierEx();

    float strength = 0.5f;
    int iterations = 10;
    bool useAxisX = true;
    bool useAxisY = true;
    bool useAxisZ = true;

    MeshData apply(const MeshData& input) override;

private:
    QVector3D smoothVertex(const MeshData& mesh, int index, float strength, bool useX, bool useY, bool useZ);
};

class LaplacianSmoothModifier : public DeformModifier {
public:
    LaplacianSmoothModifier();

    float lambdaFactor = 1.0f;
    float areaWeight = 0.0f;
    bool useNormalized = false;
    bool useXSingularity = false;

    MeshData apply(const MeshData& input) override;
};

class SurfaceSmoothModifier : public DeformModifier {
public:
    SurfaceSmoothModifier();

    float smoothness = 0.5f;
    int iterations = 1;
    float reuse = 0.0f;

    enum class SmoothType { Surface, Tangent };
    SmoothType smoothType = SmoothType::Surface;

    MeshData apply(const MeshData& input) override;
};

class VolumeSmoothModifier : public DeformModifier {
public:
    VolumeSmoothModifier();

    float smoothness = 0.5f;
    int iterations = 10;

    MeshData apply(const MeshData& input) override;
};

class UVProjectModifier : public Modifier {
public:
    UVProjectModifier();

    QString uvLayer = "UVMap";
    int projectLoopIndex = 0;
    int projectIndex = 0;

    enum class ProjectAxis { X, Y, Z, NEG_X, NEG_Y, NEG_Z };
    ProjectAxis axisX = ProjectAxis::X;
    ProjectAxis axisY = ProjectAxis::Y;
    ProjectAxis axisZ = ProjectAxis::NEG_Z;

    bool useAutoTexnames = true;

    MeshData apply(const MeshData& input) override;
};

class WeldModifier : public GenerateModifier {
public:
    WeldModifier();

    float threshold = 0.001f;
    int maxShadingDistance = 1;
    bool useInvert = false;

    MeshData apply(const MeshData& input) override;
};

class WeightVGroupModifier : public DeformModifier {
public:
    WeightVGroupModifier();

    QString vertexGroup = "Group";
    float addThreshold = 0.0f;
    float removeThreshold = 1.0f;

    enum class MirrorType { Additive, Subtractive, All, Automatic };
    MirrorType mirrorType = MirrorType::Automatic;

    bool useAdd = false;
    bool useRemove = false;

    enum class OpType { All, Blur, Clean, Levels, Mask, Quantize, Remove, Smooth };
    OpType opType = OpType::All;

    float level = 0.5f;
    float threshold = 0.5f;
    float strength = 1.0f;

    MeshData apply(const MeshData& input) override;
};

class CageDeformModifier : public DeformModifier {
public:
    CageDeformModifier();

    QString cageObjectName;
    MeshData cageMesh;
    MeshData targetRestPose;

    float strength = 1.0f;
    bool usePreserveVolume = true;
    int qualityIterations = 3;

    enum class CoordType { MeanValue, Green, Harmonic };
    CoordType coordType = CoordType::MeanValue;

    MeshData apply(const MeshData& input) override;

    void setCageMesh(const MeshData& cage);
    void buildCoordinates(const MeshData& target);

private:
    struct CageWeight {
        QVector<float> weights;
        QVector<QVector3D> cageVertices;
    };
    QVector<CageWeight> m_cageWeights;
    bool m_coordinatesValid = false;

    QVector<float> computeMeanValueCoordinates(
        const QVector3D& point,
        const QVector<QVector3D>& cageVerts,
        const QVector<QVector<int>>& cageFaces);
    float computeAngleWeight(
        const QVector3D& p,
        const QVector3D& vi,
        const QVector3D& vj,
        const QVector3D& vk);
};

class LatticeExModifier : public DeformModifier {
public:
    LatticeExModifier();

    int uDivs = 2, vDivs = 2, wDivs = 2;
    float strength = 1.0f;
    QString vertexGroup;

    enum class Interpolation { Trilinear, BSpline, CatmullRom };
    Interpolation interpolation = Interpolation::Trilinear;

    // Control point positions in local lattice space
    QVector<QVector3D> controlPoints;
    QVector<QVector3D> restControlPoints;

    void setDivisions(int u, int v, int w);
    void resetControlPoints();
    bool moveControlPoint(int index, const QVector3D& delta);

    MeshData apply(const MeshData& input) override;
    MeshData applyWithDeformedLattice(const MeshData& input, const QVector<QVector3D>& deformedCPs);

    int controlPointCount() const { return (uDivs + 1) * (vDivs + 1) * (wDivs + 1); }

private:
    QVector3D interpolateTrilinear(const QVector3D& localPos, const QVector<QVector3D>& cps) const;
    QVector3D interpolateBSpline(const QVector3D& localPos, const QVector<QVector3D>& cps) const;
    QVector3D interpolateCatmullRom(const QVector3D& localPos, const QVector<QVector3D>& cps) const;
    int cpIndex(int i, int j, int k) const;
};

class TaperModifier : public DeformModifier {
public:
    TaperModifier();

    enum class Axis { X, Y, Z, AUTO };
    Axis taperAxis = Axis::AUTO;

    float factor = 1.0f;
    bool useCurve = false;

    MeshData apply(const MeshData& input) override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class RippleModifier : public DeformModifier {
public:
    RippleModifier();

    enum class Axis { X, Y, Z };
    Axis rippleAxis = Axis::Y;

    float amplitude = 0.1f;
    float wavelength = 2.0f;
    float phase = 0.0f;
    float decay = 0.0f;

    MeshData apply(const MeshData& input) override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class NoiseModifier : public DeformModifier {
public:
    NoiseModifier();

    float scale = 1.0f;
    float strength = 1.0f;
    int seed = 0;
    float depth = 2.0f;

    MeshData apply(const MeshData& input) override;
    void readParameters(const QMap<QString, QVariant>& params) override;

private:
    float noiseValue(float x, float y, float z) const;
};

class PushModifier : public DeformModifier {
public:
    PushModifier();

    float distance = 0.1f;

    MeshData apply(const MeshData& input) override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class RelaxModifier : public DeformModifier {
public:
    RelaxModifier();

    int iterations = 5;
    float factor = 1.0f;
    bool preserveVolume = false;
    bool pinBoundary = true;

    MeshData apply(const MeshData& input) override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class OffsetModifier : public DeformModifier {
public:
    OffsetModifier();

    float thickness = 0.1f;
    bool useFlipNormals = false;
    bool useEvenOffset = true;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class SmoothingGroupsModifier : public DeformModifier {
public:
    SmoothingGroupsModifier();

    float angleThreshold = 30.0f;  // degrees; edges below this share a group

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class UVResolveOverlapsModifier : public DeformModifier {
public:
    UVResolveOverlapsModifier();

    float padding = 0.01f;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class BevelModifierEx : public DeformModifier {
public:
    BevelModifierEx();

    float width = 0.01f;
    int segments = 1;
    float angleLimit = qDegreesToRadians(30.0f);
    bool useClampOverlap = true;
    float clampOverlap = 0.01f;

    enum class ProfileShape { None, Convex, Concave, Custom };
    ProfileShape profileShape = ProfileShape::None;

    float profileValue = 0.5f; // Custom profile value (0..1)

    bool bevelEdgeOnly = true;
    bool bevelVertices = false;

    MeshData apply(const MeshData& input) override;
    QMap<QString, QVariant> writeParameters() const override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class MeltModifier : public DeformModifier {
public:
    MeltModifier();

    enum class MeltAxisType { X, Y, Z };
    MeltAxisType axis = MeltAxisType::Y;

    float amount = 1.0f;
    float viscosity = 0.0f;

    MeshData apply(const MeshData& input) override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

class LatheModifier : public GenerateModifier {
public:
    LatheModifier();

    int segments = 24;
    float angle = 360.0f;
    enum class Axis { X, Y, Z };
    Axis latheAxis = Axis::Y;

    MeshData apply(const MeshData& input) override;
    void readParameters(const QMap<QString, QVariant>& params) override;
};

}