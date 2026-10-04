#pragma once

#include <QVector3D>
#include <QVector4D>
#include <QVector2D>
#include <QMatrix4x4>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QImage>
#include <QSet>
#include <QPair>
#include <QtMath>
#include "GeometryTypes.h"

namespace ks {

using geometry::NURBSCurve;
using geometry::NURBSSurface;

struct Vertex {
    QVector3D position;
    QVector3D normal;
    QVector2D uv;
    QVector4D color;
    QVector3D tangent;
    float weight = 1.0f;
    float mask = 0.0f;
    int boneIndex = -1;

    Vertex() : color(1, 1, 1, 1) {}
};

struct Edge {
    int v1, v2;
    Edge(int a = -1, int b = -1) : v1(a), v2(b) {}
};

struct Face {
    QVector<int> indices;
    QVector<int> uvIndices;    // separate UV indices (if different from position indices)
    QVector<int> uv2Indices;   // UV2 channel indices
    QVector3D normal;
    int materialId = 0;

    Face() = default;
    Face(std::initializer_list<int> il) : indices(il) {}
    Face(const QVector<int>& v) : indices(v) {}

    int vertexCount() const { return indices.size(); }
    int& operator[](int i) { return indices[i]; }
    int operator[](int i) const { return indices[i]; }
    bool operator==(const Face& other) const { return indices == other.indices; }
    bool operator!=(const Face& other) const { return !(*this == other); }
};

struct MeshData {
    QString   name;
    QString   materialName;
    QVector4D diffuseColor  = {0.8f,0.8f,0.8f,1.f};
    float     metallic      = 0.f;
    float     roughness     = 0.5f;

    QVector<Vertex>  vertices;
    QVector<int> indices;       // Face vertex indices (for quick access)
    QVector<Face>       faces;
    QVector<Edge>       edges;
    QVector<QVector3D>  normals;
    QVector<QVector2D>  uvs;
    QVector<QVector2D>  uv2s;       // UV2 channel (damage maps, lightmaps)
    QVector<QVector3D>  tangents;
    QVector<QVector3D>  bitangents;
    QVector3D boundingBoxMin;
    QVector3D boundingBoxMax;
    float boundingRadius = 0.0f;
    QStringList materials;
    QMap<QString, QVector<float>> vertexGroups;

    // Shape key / morph target data
    QStringList shapeKeyNames;
    QVector<QVector<QVector3D>> shapeKeyDeltas;
    QVector<float> shapeKeyWeights;
    QVector<bool> shapeKeyMute;
    QVector<float> shapeKeyMin;
    QVector<float> shapeKeyMax;

    void clear();
    void computeBoundingBox();
    void computeNormals();
    void computeTangents();
    void flipFaces();
    void triangulate();
    
    int getTriangleCount() const;
    int getVertexCount() const;
    
    geometry::GeoMeshData toGeoMesh() const;
    static MeshData fromGeoMesh(const geometry::GeoMeshData& geo);
};

struct MeshUVIsland {
    QVector<int> faceIndices;
    QVector2D minUV, maxUV;
};

class MeshOperations {
public:
    static MeshData createBox(float width = 1.0f, float height = 1.0f, float depth = 1.0f);
    static MeshData createSphere(float radius = 0.5f, int segments = 32, int rings = 16);
    static MeshData createCylinder(float radius = 0.5f, float height = 1.0f, int segments = 32);
    static MeshData createCone(float radius = 0.5f, float height = 1.0f, int segments = 32);
    static MeshData createPlane(float width = 1.0f, float height = 1.0f, int uSubdiv = 1, int vSubdiv = 1);
    static MeshData createTorus(float majorRadius = 0.5f, float minorRadius = 0.2f, int majorSeg = 32, int minorSeg = 16);
    static MeshData createGrid(float width = 1.0f, float height = 1.0f, int uSubdiv = 10, int vSubdiv = 10);
    static MeshData createIcosphere(float radius = 0.5f, int subdivisions = 2);

    static MeshData booleanUnion(const MeshData& a, const MeshData& b);
    static MeshData booleanDifference(const MeshData& a, const MeshData& b);
    static MeshData booleanIntersection(const MeshData& a, const MeshData& b);
    static MeshData booleanXor(const MeshData& a, const MeshData& b);

    static MeshData extrude(const MeshData& mesh, const QVector3D& direction, float distance, bool individualFaces = false);
    static MeshData extrudeFaces(const MeshData& mesh, const QVector<QVector3D>& directions);
    static MeshData bevelEdges(const MeshData& mesh, float distance, int segments = 1, float angleLimit = qDegreesToRadians(30.0f), int profileType = 0, float tension = 0.5f);
    // Fillet chain (Plasticity P2): bevels the given edges (indices into the
    // mesh's edge list after `ensureEdgeList`) each at its own `radius`, so a
    // chain of edges can get a tapering fillet. Where several beveled edges
    // share a corner vertex, the corner offset is blended from the incident
    // radii. Ineligible edges (non-manifold, co-planar, or softer than
    // `angleLimit`) are skipped. `segments` rows are inserted across the fillet.
    static MeshData bevelChain(const MeshData& mesh, const QVector<int>& edgeIndices,
                               const QVector<float>& radii, int segments = 1,
                               float angleLimit = qDegreesToRadians(30.0f),
                               int profileType = 0, float tension = 0.5f);
    static MeshData bevelVertices(const MeshData& mesh, float distance);

    static MeshData insetFaces(const MeshData& mesh, float distance, float depth = 0.0f);
    static MeshData weldVertices(const MeshData& mesh, float threshold = 0.001f);

    // Shell / solidity: extrudes the mesh along averaged vertex normals by
    // `thickness` and closes every open boundary with a rim wall. Produces a
    // watertight result from an open sheet/plane. `direction` is +1 (outside)
    // or -1 (inside); `flipNormals` optionally reverses the winding of the
    // outer (extruded) surface so normals point correctly after thickening.
    static MeshData shell(const MeshData& mesh, float thickness, int direction = 1, bool flipNormals = false);

    // Transformation modes for `transformAround` (Modo/3ds Max "action center").
    // Translate: amount = per-axis delta. Rotate: amount = per-axis Euler angles
    // in degrees, applied about the world axes through `pivot`. ScaleUniform:
    // amount.x is a single uniform factor. ScaleAxis: amount = per-axis factors
    // (1 = unchanged).
    enum class TransformCenterMode {
        Translate = 0,
        Rotate = 1,
        ScaleUniform = 2,
        ScaleAxis = 3
    };

    // Reusable spatial falloff profile shared by proportional editing, sculpting
    // and transform ops. Type: 0 = smooth (smoothstep), 1 = linear, 2 = sharp,
    // 3 = root, 4 = sphere, 5 = constant. Returns the weight for a point at
    // `distance` from the falloff center; 1.0 at the center, 0.0 at/beyond
    // `radius`. `radius` > 0 is required (else returns 0).
    static float falloffFactor(float distance, float radius, int type);

    // Transforms a subset of vertices around an explicit pivot point (the
    // "action center" pattern). `selection` holds vertex indices; empty means
    // "all vertices". `pivot` is the transform origin in local space.
    // `falloffRadius` > 0 applies a spatial falloff from `pivot` (same profile
    // as proportional editing via `falloffFactor`). Vertex attributes are kept.
    static MeshData transformAround(const MeshData& mesh, const QVector<int>& selection,
                                    TransformCenterMode mode, const QVector3D& pivot,
                                    const QVector3D& axis, const QVector3D& amount,
                                    float falloffRadius = 0.0f, int falloffType = 0);

    // Polygonal bridge: connects two edge loops with a strip of quads.
    // `loopA`/`loopB` are ordered vertex-index loops (same vertex count).
    // `segments` inserts that many additional rings interpolated between the
    // two loops. Returns the new mesh; empty if loops are invalid.
    static MeshData bridgeEdges(const MeshData& mesh,
                                const QVector<int>& loopA,
                                const QVector<int>& loopB,
                                int segments = 1);

    // Polygonal bridge between two whole faces: connects corresponding pairs
    // of vertices with quads (the faces keep their original geometry). Useful
    // to join two holes or duplicate shapes.
    static MeshData bridgeFaces(const MeshData& mesh, int faceA, int faceB, int segments = 1);
    static MeshData dissolveEdges(const MeshData& mesh, const QVector<int>& edgeIndices);
    static MeshData dissolveFaces(const MeshData& mesh, const QVector<int>& faceIndices);
    static MeshData dissolveVertices(const MeshData& mesh, const QVector<int>& vertexIndices);

    static MeshData loft(const QVector<MeshData>& profiles, bool close = false);
    static MeshData sweep(const MeshData& profile, const QVector<QMatrix4x4>& transforms, bool close = false);
    static MeshData spin(const MeshData& profile, const QVector3D& axis, float angle, int steps);

    // Revolves a 2D sketch profile (an ordered polyline of vertices) around
    // `axis` (through the origin, default +Y) to build a surface of revolution
    // - the Plasticity-style "lathe / revolve from sketch" workflow. Each
    // profile segment becomes `steps` quads around the axis; `angleDeg` is the
    // sweep (360 = full lathe). The profile vertex order is taken from the
    // edge chain of `profileMesh` (falling back to index order). When
    // `closeCaps`, the two open end rings of the sweep are closed with fans
    // (degenerate rings on the axis are skipped). Returns the revolved mesh.
    static MeshData revolveSketch(const MeshData& profileMesh,
                                  const QVector3D& axis = QVector3D(0, 1, 0),
                                  float angleDeg = 360.0f, int steps = 24,
                                  bool closeCaps = true);

    static MeshData subdivide(const MeshData& mesh, int levels = 1);
    static MeshData unsubdivide(const MeshData& mesh, float detail = 0.0f);
    static MeshData triangulate(const MeshData& mesh);
    static MeshData quadrangulate(const MeshData& mesh);
    static MeshData retopoQuadDraw(const MeshData& highPoly, const MeshData& lowPoly, float snapDist = 0.1f);
    static MeshData uvPeel(const MeshData& mesh, const QVector<int>& seamEdges);
    static MeshData uvPack(const MeshData& mesh, float padding = 2.0f);
    static QVector<float> analyzeUVDensity(const MeshData& mesh);
    static QImage uvOverlapHeatmap(const MeshData& mesh, int width = 512, int height = 512);
    static QImage uvDensityHeatmap(const MeshData& mesh, int width = 512, int height = 512);
    static QImage renderAOV(const MeshData& mesh, const QString& aov, int width = 1920, int height = 1080, int samples = 16);

    // Assigns smoothing groups (0..31, Max-style) to faces based on the
    // dihedral angle between adjacent faces. Faces whose adjacent angle is
    // below `angleDeg` (so the surface is locally soft) share a group; a new
    // group starts when the angle exceeds the threshold. Returns a vector of
    // group ids with one entry per face. 0 = no group (hard edges).
    static QVector<int> autoSmooth(const MeshData& mesh, float angleDeg = 30.0f);

    // Splits vertices at hard edges so that every smoothing-group boundary
    // becomes a geometric seam (3ds Max "Split" smoothing groups). `faceGroups`
    // holds one entry per face (from `autoSmooth`, or hand-assigned 0..31
    // groups). Faces sharing an edge but belonging to different groups get
    // duplicated corner vertices that keep the original per-vertex attributes
    // (position is shared with the source vertex; UVs, colors, weights and
    // tangents are carried over). Returns the split mesh. When all faces share
    // a single group (or `faceGroups` is empty), the mesh is returned unchanged.
    static MeshData splitSmoothingGroups(const MeshData& mesh, const QVector<int>& faceGroups);

    static MeshData mirror(const MeshData& mesh, const QVector3D& axis, float offset = 0.0f);
    static MeshData array(const MeshData& mesh, int count, const QVector3D& offset);
    static MeshData radialArray(const MeshData& mesh, int count, const QVector3D& axis, float angle);

    static MeshData knifeCut(const MeshData& mesh, const QVector3D& cutStart, const QVector3D& cutEnd);
    // Applies one sculpt brush stroke on the mesh (in-place).
    // `center`/`radius` are in local coordinates; `mode` selects the brush:
    // 0=draw, 1=smooth, 2=grab (moves by `drag`), 3=flatten, 4=crease,
    // 5=inflate (push along averaged vertex normal), 6=pinch (toward brush
    // center), 7=smear (copies the position offset of the previous stroke point
    // so vertices follow the Cursor), 8=negate (opposite of draw, sinks down),
    // 9=folds (concentric ridges/valleys along the normal), 10=pores (micro
    // deterministic depressions), 11=bulge (soft outward push), 12=slash
    // (directional cut along `drag`, else screen-normal projected).
    // `falloffPower` shapes the falloff curve: the default 2.0 uses the classic
    // smoothstep; other values use pow(t, falloffPower). `pinned`, when given,
    // lists vertex indices that must never move (pin/lock during sculpt).
    // Returns the number of vertices affected.
    static int sculptBrush(MeshData& mesh, const QVector3D& center, float radius,
                           float strength, int mode, const QVector3D& drag = QVector3D(),
                           const QVector3D& previousCenter = QVector3D(),
                           float falloffPower = 2.0f,
                           const QSet<int>* pinned = nullptr);

    // Finds the closest vertex to a world-space point (mesh in local space, transformed by world).
    // Returns vertex index or -1 if mesh empty.
    static int findClosestVertex(const MeshData& mesh, const QMatrix4x4& worldTransform,
                                 const QVector3D& worldPoint);

    // Finds the closest edge to a world-space point. Returns edge as pair<vertexA, vertexB> or (-1,-1).
    static QPair<int, int> findClosestEdge(const MeshData& mesh, const QMatrix4x4& worldTransform,
                                           const QVector3D& worldPoint);

    // Border detection: returns the edges that are on a mesh boundary (edges
    // shared by exactly one face). Mode 0 = all borders, 1 = closest to
    // worldPoint (returns a pair with the two vertex indices or (-1,-1)).
    static QVector<Edge> borderEdges(const MeshData& mesh);
    static QPair<int, int> findClosestBorderEdge(const MeshData& mesh, const QMatrix4x4& worldTransform,
                                                 const QVector3D& worldPoint);

    // Connected-face ("element") structures. Returns the element ids of the
    // face that contains the closest vertex to worldPoint, and a helper that
    // tags every face with its connected-component id (-1 when isolated by
    // non-manifold edges).
    static QVector<int> faceElements(const MeshData& mesh);
    static int elementAtWorld(const MeshData& mesh, const QMatrix4x4& worldTransform,
                              const QVector3D& worldPoint);

    // Push/pull faces - extrude selected faces along their normals. The region
    // is extruded as a solid: the selected faces move by `distance` and every
    // boundary edge between the selected region and the rest of the mesh gets a
    // side wall (quads), so the result stays watertight. `individualFace` moves
    // each selected face separately (like Blender's individual extrude).
    static MeshData pushPullFaces(const MeshData& mesh, const QVector<int>& faceIndices, float distance);

    // Offset faces - offset selected faces along the averaged vertex normals by
    // `distance` and stitch the boundary with side walls (watertight).
    static MeshData offsetFaces(const MeshData& mesh, const QVector<int>& faceIndices, float distance);

    // Advanced snapping: snaps `worldPoint` against the current mesh geometry
    // (in `world` space). Priority: Vertex > Midpoint > Edge > Face > Tangent.
    // Snap modes are the `SnapType` enum bit flags. Returns the snapped point
    // (or the input point unchanged when nothing matches).
    static QVector3D snapPointToMesh(const MeshData& mesh, const QMatrix4x4& world,
                                     const QVector3D& worldPoint, int snapTypes);

    // Hole fill / patch: fills every open boundary loop of the mesh with a
    // triangulated cap (ear-clipping midpoint fan). `maxHoleEdges` skips holes
    // larger than the threshold (0 = no limit). Returns the number of holes
    // filled and mutates `mesh` in place.
    static int fillHoles(MeshData& mesh, int maxHoleEdges = 16);

    // Mesh extraction: builds a standalone mesh from the selected faces
    // (vertex-welded copy, recomputed normals/UVs). If `thickness` > 0 the
    // extracted surface is duplicated along vertex normals and the boundary is
    // capped (a "solid" extract, Mudbox-style). Returns the extracted mesh or
    // an empty mesh when `faceIndices` is empty/invalid.
    static MeshData extractFaces(const MeshData& mesh, const QVector<int>& faceIndices,
                                 float thickness = 0.0f, bool closeCaps = true);

    // Construction plane operations
    struct CPlane {
        static QVector3D origin;
        static QVector3D normal;
        static QVector3D up;
        static QVector3D getOrigin();
        static QVector3D getNormal();
        static QVector3D getUp();
    };
    static void setCPlane(const QVector3D& origin, const QVector3D& normal, const QVector3D& up);
    static QVector3D getCPlaneOrigin();
    static QVector3D getCPlaneNormal();
    static QVector3D getCPlaneUp();
    static void snapToCPlane(const QVector3D& point, QVector3D& result);

    // Fillet/Chamfer operations
    static bool filletEdges(const MeshData& mesh, const QVector<int>& edgeIndices, float radius, MeshData& result);
    static bool chamferEdges(const MeshData& mesh, const QVector<int>& edgeIndices, float distance, MeshData& result);

    // Curvature analysis
    static QVector3D computeCurvatureAtVertex(const MeshData& mesh, int vertexIndex);
    static QVector<int> findHighCurvatureVertices(const MeshData& mesh, float angleThreshold = qDegreesToRadians(30.0f));

    // Live dimensions
struct DimensionLine {
    int vertex1;
    int vertex2;
    QString label;
    bool active;
};

struct DimensionData {
    int vertex1;
    int vertex2;
    int vertex3 = -1; // third vertex (angle dimensions)
    QString label;
    bool active;
    int objectId = -1; // owning scene object (for live, geometry-linked dimensions)
};

struct RadiusDimension {
    int vertex;
    QVector<int> edgeIndices;
    QString label;
    bool active;
    int objectId = -1; // owning scene object (live dimension)
    bool diameter = false; // report 2x radius
};

struct CurvatureComb {
    int vertexIndex;
    QVector3D direction;
    float length;
    int segments = 10;
    QString label;
    bool active;
};

struct RadialMenuItem {
    QString text;
    int mode; // 0=select, 1=move, 2=rotate, 3=scale, 4=fillet, 5=chamfer
    QVector3D param1;
    QVector3D param2;
};

struct RadialMenu {
    QVector<RadialMenuItem> items;
    int selectedIndex = -1;
    bool active = false;
    QVector2D pos; // screen position
    
    void addItem(const QString& text, int mode, const QVector3D& p1 = QVector3D(), const QVector3D& p2 = QVector3D()) {
        items.append({text, mode, p1, p2});
    }
    
    void clear() {
        items.clear();
        selectedIndex = -1;
        active = false;
    }
};


    static QVector<DimensionLine> dimensions();
    static void addDistanceDimension(int v1, int v2, const QString& label, int objectId = -1);
    static float computeDistanceValue(const MeshData& mesh, int v1, int v2);
    static float computeAngleValue(const MeshData& mesh, int v1, int v2, int v3);
    static float computeRadiusValue(const MeshData& mesh, int vertex, const QVector<int>& edgeIndices);
    static void addAngleDimension(int v1, int v2, int v3, const QString& label, int objectId = -1);
    static void addRadiusDimension(int vertex, const QVector<int>& edgeIndices, const QString& label, int objectId = -1);
    static void addDiameterDimension(int vertex, const QVector<int>& edgeIndices, const QString& label, int objectId = -1);
    static float evaluateRadiusDimension(const MeshData& mesh, const RadiusDimension& d);
    // Returns the live value of a stored dimension (resolved against the
    // current geometry referenced by the objectId stored in the dimension).
    static float evaluateDistanceDimension(const MeshData& mesh, const DimensionData& d);
    static float evaluateAngleDimension(const MeshData& mesh, const DimensionData& d);
    // Accessors for the dimension registries (used by the bridge to resolve
    // live values and to clear/remove entries).
    static const QVector<DimensionData>& distanceDimensions() { return m_distanceDimensions; }
    static const QVector<DimensionData>& angleDimensions() { return m_angleDimensions; }
    static const QVector<RadiusDimension>& radiusDimensions() { return m_radiusDimensions; }
    static void clearDistanceDimensions() { m_distanceDimensions.clear(); }
    static void clearAngleDimensions() { m_angleDimensions.clear(); }
    static void clearRadiusDimensions() { m_radiusDimensions.clear(); }
    static void removeDistanceDimensionAt(int i) { if (i >= 0 && i < m_distanceDimensions.size()) m_distanceDimensions.removeAt(i); }
    static void removeAngleDimensionAt(int i) { if (i >= 0 && i < m_angleDimensions.size()) m_angleDimensions.removeAt(i); }
    static void removeRadiusDimensionAt(int i) { if (i >= 0 && i < m_radiusDimensions.size()) m_radiusDimensions.removeAt(i); }
    // Toggles visibility (active flag) of a stored dimension. type: 0=distance,1=angle,2=radius.
    static void setDimensionVisible(int type, int index, bool visible);

    // Section analysis
    static MeshData cutByPlane(const MeshData& mesh, const QVector3D& planePoint, const QVector3D& planeNormal);

    // Bridge curve - create surface between two curves or between curve and surface
    static MeshData bridgeCurve(const MeshData& mesh, const QVector<int>& curve1Ids,
                                const QVector<int>& curve2Ids, int segments = 8);

    // Offset curve - offset a curve/profile by a given distance
    static MeshData offsetCurve(const MeshData& mesh, const QVector<int>& curveIndices,
                                float distance, bool offsetBothSides = false);

    // STEP export
    static bool exportSTEP(const MeshData& mesh, const QString& path, bool useBREP = false);
    // Technical SVG export: orthographic projection with visible edges as
    // solid lines and hidden edges dashed. viewAxis selects the projection
    // (0=X,1=Y,2=Z). Returns false on failure.
    static bool exportHiddenLineSVG(const MeshData& mesh, const QString& path,
                                    int viewAxis = 2, float lineWidth = 0.3f);

    // File loading
    static bool loadFile(const std::string& path, MeshData& outMesh, QString& errorMsg);

    // Advanced snapping
    enum class SnapType { None = 0, Vertex = 1, Edge = 2, Face = 4, Midpoint = 8, Grid = 16, Tangent = 32 };
    static QVector3D snapPoint(const QVector3D& worldPoint, int snapTypes);
    static SnapType snapTypes();
    static void setSnapTypes(SnapType types);
    static SnapType m_snapTypes;

    // NURBS curve operations
    static NURBSCurve createCurve(const QVector<QVector3D>& controlPoints, int degree = 3, bool periodic = false);
    static NURBSCurve revolveCurve(const NURBSCurve& profile, float angleDeg, int steps);
    static NURBSCurve loftCurves(const QVector<NURBSCurve>& profiles);

    // NURBS surface operations
    static NURBSSurface createSurface(const QVector<QVector<QVector3D>>& controlPoints,
                                       int uDegree = 3, int vDegree = 3,
                                       bool periodicU = false, bool periodicV = false);
    static NURBSSurface loft(const QVector<NURBSSurface>& surfaces, bool close = false);
    static NURBSSurface sweep(const NURBSSurface& profile, const QVector<QMatrix4x4>& transforms, bool close = false);
    static NURBSSurface revolve(const NURBSSurface& surface, float angleDeg, int steps);
    static NURBSSurface pipe(const QVector<NURBSCurve>& profiles, float radius);
    // NURBS fillet/chamfer blending - creates a blended surface between
    // two adjacent NURBS surfaces at a given radius.
    static NURBSSurface offsetSurface(const NURBSSurface& surface, float distance);
    static NURBSSurface filletSurface(const NURBSSurface& surfaceA,
                                       const NURBSSurface& surfaceB,
                                       const QVector3D& edgePointA,
                                       const QVector3D& edgePointB,
                                       float radius,
                                       int segments = 8);

    // NURBS evaluation
    static QVector3D evaluatePointOnCurve(const NURBSCurve& curve, float u);
    static QVector3D evaluatePointOnSurface(const NURBSSurface& surface, float u, float v);

    // NURBS surface extension: extends the surface in the given U/V direction
    // by `distance` (in local units), keeping curvature. 0=U-,1=U+,2=V-,3=V+.
    static NURBSSurface extendSurface(const NURBSSurface& surface, int direction, float distance);
    // Slides a CV tangentially along its row/column by `factor` in [-1,1].
    static bool slideCV(NURBSSurface& surface, int row, int col, float factor);
    // Trims a NURBS surface with a boundary curve defined by 3D points.
    // The trim curve should be a closed polygon lying on the surface.
    // `keepInside` determines whether to keep the region inside (true) or outside (false) the curve.
    static NURBSSurface trimSurface(const NURBSSurface& surface,
                                     const QVector<QVector3D>& trimCurvePoints,
                                     bool keepInside = true);
// Splits a NURBS surface into two along a cutting curve defined by 3D points.
    // The cut curve should be a closed polygon lying on the surface.
    // Returns two surfaces: the "left" and "right" parts of the split.
    // outSurfaceIndex indicates which side of the cut the result belongs to.
    static NURBSSurface splitSurfaceByCurve(const NURBSSurface& surface,
                                              const QVector<QVector3D>& cutCurvePoints,
                                              int& outSurfaceIndex);
    // NURBS boolean operations - tessellate surfaces and use CGAL mesh booleans.
    static NURBSSurface booleanUnion(const NURBSSurface& surfaceA,
                                      const NURBSSurface& surfaceB);
    static NURBSSurface booleanDifference(const NURBSSurface& surfaceA,
                                         const NURBSSurface& surfaceB);
    static NURBSSurface booleanIntersection(const NURBSSurface& surfaceA,
                                            const NURBSSurface& surfaceB);
    static NURBSSurface booleanXor(const NURBSSurface& surfaceA,
                                    const NURBSSurface& surfaceB);

    static float sceneTolerance() { return s_tolerance; }
    static void setSceneTolerance(float t) { s_tolerance = qMax(1e-6f, t); }
    static float sceneUnitScale() { return s_unitScale; }
    static void setSceneUnitScale(float s) { s_unitScale = qMax(1e-6f, s); }
    static QVector<int> retargetSkeleton(const QVector<QVector3D>& srcJoints, const QVector<QVector3D>& dstJoints);
    static MeshData applyClusterDeform(const MeshData& mesh, const QVector<int>& indices, const QVector3D& delta, float weight = 1.0f);
    static MeshData applyBlendShape(const MeshData& base, const MeshData& target, float weight);
    static bool smoothPreviewEnabled() { return s_smoothPreview; }
    static void setSmoothPreview(bool v) { s_smoothPreview = v; }
    static int smoothPreviewLevel() { return s_smoothPreviewLevel; }
    static void setSmoothPreviewLevel(int l) { s_smoothPreviewLevel = qBound(0,l,4); }

    // NURBS fillet/chamfer blending - creates a blended surface between
    // two adjacent NURBS surfaces at a given radius.
    // The blend replaces the sharp edge with a smooth tangent continuation.
    // Samples curvature (estimated from the surface normals) along an isoparam
    // curve; returns a polyline ribbon suitable for visualization.
    static MeshData curvatureComb(const NURBSSurface& surface, int direction, int combCount, float scale);

    // NURBS tessellation
    static MeshData tessellateCurve(const NURBSCurve& curve, int segments = 32);
    static MeshData tessellateSurface(const NURBSSurface& surface, int uSegments = 32, int vSegments = 32);
    // Finds the edge loop containing the given edge (series of edges whose
    // consecutive pairs are opposite edges of quads). Returns the loop as a
    // list of edges including the seed edge. Empty if not a quad loop.
    static QVector<Edge> findEdgeLoop(const MeshData& mesh, int v1, int v2);
    // Finds the edge ring of the given edge (series of edges that share a
    // vertex, marching around the quads adjacent to the seed edge).
    static QVector<Edge> findEdgeRing(const MeshData& mesh, int v1, int v2);

    // Slides a vertex along its connected edges toward target world position (projected to edge directions).
    // Returns true if vertex moved.
    static bool vertexSlide(MeshData& mesh, int vertexIndex, const QVector3D& targetWorld,
                            const QMatrix4x4& worldTransform);

    // Slides an edge (both vertices) along the edge direction by factor [-1,1].
    static bool edgeSlide(MeshData& mesh, int edgeV0, int edgeV1, float factor);
    // Inserts a loop cut through the mesh on a plane perpendicular to the given
    // local axis (0=X,1=Y,2=Z) at `factor` (0..1) of the bounding box extent.
    // `slide` (-1..1) additionally slides the inserted loop along its
    // supporting edges (loop-cut-and-slide).
    static MeshData loopCut(const MeshData& mesh, int axis, float factor, float slide = 0.0f);
    static MeshData shrinkwrap(const MeshData& mesh, const MeshData& target, const QVector3D& direction);
    static MeshData displace(const MeshData& mesh, const QImage& heightmap, float strength);

    // Advanced array tools
    struct ArrayOptions {
        int count = 4;
        int countY = 1;       // number of rows in gridArray
        QVector3D constantOffset;
        QVector3D relativeOffset;
        QVector3D pivotPoint;
        bool useCount = true;
        bool useConstantOffset = true;
        bool useRelativeOffset = false;
        float mergeThreshold = 0.0001f;
    };

    static MeshData linearArray(const MeshData& mesh, int count, const QVector3D& offset, const ArrayOptions& opts);
    static MeshData radialArray(const MeshData& mesh, int count, const QVector3D& axis, float angle, const ArrayOptions& opts);
    static MeshData gridArray(const MeshData& mesh, const ArrayOptions& opts);

    static void mergeMeshes(MeshData& target, const MeshData& source);
    static void splitMeshes(const MeshData& mesh, QVector<MeshData>& result);

    enum class SelectionMode {
    Vertex = 0,
    Edge = 1,
    Face = 2,
    Object = 3,
    All = 4,
    Border = 5,
    Element = 6
};

class SelectionManager {
public:
    static void setMode(SelectionMode mode);
    static SelectionMode mode() { return m_mode; }
    static void addSelectedVertex(int vertexIndex);
    static void removeSelectedVertex(int vertexIndex);
    static void addSelectedEdge(int edgeIndex);
    static void removeSelectedEdge(int edgeIndex);
    static void addSelectedFace(int faceIndex);
    static void removeSelectedFace(int faceIndex);
    static void selectAll(int vertexCount, int edgeCount, int faceCount);
    static void deselectAll();
    static bool hasSelection() { return !m_selectedVertices.isEmpty() || !m_selectedEdges.isEmpty() || !m_selectedFaces.isEmpty(); }
    static QVector<int> selectedVertices() { return m_selectedVertices; }
    static QVector<int> selectedEdges() { return m_selectedEdges; }
    static QVector<int> selectedFaces() { return m_selectedFaces; }
    static void clear();

    static void hideFace(int faceIndex);
    static void unhideFace(int faceIndex);
    static void hideSelectedFaces();
    static void unhideAllFaces();
    static void showRadialMenu(int mode, const QVector2D& pos);
    static void hideRadialMenu();
    static RadialMenu& radialMenu();

    // Border and element selection (Max-style sub-object modes).
    // `selectedBorderEdges()` holds edges whose faces form a mesh boundary
    // (edges shared by only one face). `selectedElement` is the id of the
    // current connected element (a set of faces linked by shared edges).
    static void addSelectedBorderEdge(int edgeIndex);
    static void removeSelectedBorderEdge(int edgeIndex);
    static bool isBorderEdgeSet() { return !m_selectedBorderEdges.isEmpty(); }
    static QVector<int> selectedBorderEdges() { return m_selectedBorderEdges; }
    static void setSelectedElement(int elementId) { m_selectedElement = elementId; }
    static int selectedElement() { return m_selectedElement; }
    static void clearElement() { m_selectedElement = -1; }

    // Context menu helpers
    static void showContextMenu(const QVector3D& worldPos);
    static QSet<int> getSelectedFaceNeighbors(const MeshData& mesh, int faceIndex);

private:
    static SelectionMode m_mode;
    static QVector<int> m_selectedVertices;
    static QVector<int> m_selectedEdges;
    static QVector<int> m_selectedFaces;
    static QVector<int> m_hiddenFaces;
    static QVector<int> m_tempSelection;
    static QVector<int> m_selectedBorderEdges;
    static int m_selectedElement;
};

static QVector<MeshUVIsland> findUVIslands(const MeshData& mesh);

    // UV overlap resolution (3ds Max "overlap resolution" + packing optimizer):
    // splits the mesh into UV islands (charts stitched only across edges that
    // share vertex indices with matching UVs), detects islands whose bounding
    // boxes overlap in texture space, separates them into a fresh non-overlapping
    // layout and re-normalizes the result to fill [0,1] x [0,1] with `padding`
    // between charts. Per-face UVs are kept; the mesh's per-vertex UVs
    // (mesh.uvs and vertices[].uv) are rewritten. Returns the adjusted mesh or
    // the input unchanged when no overlaps are detected.
    static MeshData resolveUVOverlaps(const MeshData& mesh, float padding = 0.01f);

    static geometry::GeoMeshData toGeoMesh(const MeshData& mesh);
    static MeshData fromGeoMesh(const geometry::GeoMeshData& geo);

    // Rebuilds mesh.edges from the faces (used by consumers such as the QML
    // bridge to resolve edge indices during bridge/fillet operations).
    static void ensureEdgeList(MeshData& mesh);

private:
    static QVector<int> findEdge(const MeshData& mesh, int v1, int v2);
    static bool isEdge(const MeshData& mesh, int v1, int v2);
    static QVector3D computeFaceNormal(const MeshData& mesh, int faceIndex);

    // Live dimensions
    static QVector<DimensionLine> m_dimensions;
    static QVector<DimensionData> m_distanceDimensions;
    static QVector<DimensionData> m_angleDimensions;
    static QVector<RadiusDimension> m_radiusDimensions;
    static float s_tolerance;
    static float s_unitScale;
    static bool s_smoothPreview;
    static int s_smoothPreviewLevel;
};

class ExtrudeOptions {
public:
    QVector3D direction;
    float distance = 1.0f;
    bool individualFaces = false;
    bool createCaps = true;
    bool createFrontCaps = true;
    bool createBackCaps = true;
    float offset = 0.0f;
};

class BevelOptions {
public:
    float distance = 0.01f;
    int segments = 1;
    float angleLimit = qDegreesToRadians(30.0f);
    bool bevelVertices = false;
    bool bevelEdges = true;
    bool useClampOverlap = true;
    float clampOverlap = 0.05f;
int profileType = 0;
    float profileTension = 0.5f;
    int miterType = 0;
};
//
// Create an empty mesh data structure.
//
inline MeshData createEmpty() {
    MeshData md;
    md.vertices.clear();
    md.indices.clear();
    md.normals.clear();
    md.uvs.clear();
    md.faces.clear();
    return md;
}

class ArrayOptions {
public:
    int count = 4;
    QVector3D constantOffset;
    QVector3D relativeOffset;
    QVector3D pivotPoint;
    bool useCount = true;
    bool useConstantOffset = true;
    bool useRelativeOffset = false;
    float mergeThreshold = 0.0001f;
};

// Fillet/chamfer parameters for NURBS surface blending.
struct FilletParams {
    float radius = 0.1f;
    int segments = 8;  // number of divisions around the fillet
};

}