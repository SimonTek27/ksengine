#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QPair>
#include <QColor>
#include <QQmlListProperty>
#include <QVector3D>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QTimer>
#include "KitSystem.h"
#include <QtMath>

#include "editor/BaseEditor.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include "sdk/kseditor/engine/FileFormat/MeshData.h"
#include "sdk/kseditor/engine/FileFormat/FBXParser.h"
#include "sdk/kseditor/engine/FileFormat/GLBParser.h"
#include "sdk/kseditor/engine/FileFormat/CADOBJParser.h"
#include "engine/Math/MathCore.h"
#include "3DModeling_utils.h"
#include "sdk/kseditor/engine/mesh/ShapeKeyData.h"
#include "sdk/kseditor/engine/animation/ShapeKeyAnimDriver.h"
using ks::ShapeKeyAnimDriver;
#include "tools/LODSystem.h"
#include "sdk/kseditor/engine/mesh/PhysicsCollisionSystem.h"
#include "sdk/kseditor/engine/mesh/FaceGroupSystem.h"
#include "SceneObjectListModel.h"
#include "CommandHistory.h"
#include "ShortcutManager.h"
#include "ModifierStack.h"
#include "CurveSystem.h"
#include "FCurveSystem.h"
#include "BooleanStack.h"
#include "ConstraintSystem.h"
#include "ControllerSystem.h"
#include "WireParameterSystem.h"
#include "SkinWrapSystem.h"
#include "ICEParticleSystem.h"
#include "RigidBodySystem.h"
#include "ClothSystem.h"
#include "HairSystem.h"
#include "RayTraceRenderer.h"
#include "LightSystem.h"
#include "BvhImporter.h"
#include "TextureBaker.h"
#include "sdk/kseditor/engine/mesh/MultiresLevel.h"
#include "sdk/kseditor/engine/mesh/SculptLayer.h"
#include "sdk/kseditor/engine/mesh/MorphTargetEditor.h"
#include "sdk/kseditor/engine/mesh/ProjectionPainter.h"
#include <QVector4D>
#include <QImage>
#include <QJsonObject>
#include "sdk/kseditor/modules/modellingEditor/KitSystem.h"

namespace ks {

namespace physics { struct FluidSimulator; }

struct PresetData {
    QString name;
    QString category;
    QJsonObject data;
};

class SceneObjectQml : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName)
    Q_PROPERTY(int id READ id)
    Q_PROPERTY(QVector3D position READ position WRITE setPosition)
    Q_PROPERTY(QVector3D rotation READ rotation WRITE setRotation)
    Q_PROPERTY(QVector3D scale READ scale WRITE setScale)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected)
    Q_PROPERTY(QString type READ type)

public:
    explicit SceneObjectQml(QObject* parent = nullptr) : QObject(parent), m_object(nullptr) {}
    explicit SceneObjectQml(SceneObject* obj, QObject* parent = nullptr) : QObject(parent), m_object(obj) {}

    QString name() const { return m_object ? m_object->name() : QString(); }
    void setName(const QString& n) { if (m_object) m_object->setName(n); emit changed(); }

    int id() const { return m_object ? m_object->id() : -1; }

    QVector3D position() const { return m_object ? m_object->position() : QVector3D(); }
    void setPosition(const QVector3D& p) { if (m_object) { m_object->setPosition(p); emit changed(); } }

    QVector3D rotation() const {
        if (!m_object) return QVector3D();
        return m_object->rotationEuler();
    }
    void setRotation(const QVector3D& r) {
        if (m_object) {
            m_object->setRotationEuler(r);
            emit changed();
        }
    }

    QVector3D scale() const { if (!m_object) return QVector3D(1,1,1); return m_object->scale(); }
    void setScale(const QVector3D& s) { if (m_object) { m_object->setScale(s); emit changed(); } }

    bool isSelected() const { return m_object ? m_object->isSelected() : false; }
    void setSelected(bool s) { if (m_object) m_object->setSelected(s); emit changed(); }

    QString type() const {
        if (!m_object) return "None";
        switch (m_object->type()) {
            case SceneObject::Type::Node: return "Node";
            case SceneObject::Type::Mesh: return "Mesh";
            case SceneObject::Type::Light: return "Light";
            case SceneObject::Type::Camera: return "Camera";
            case SceneObject::Type::Bone: return "Bone";
            default: return "Unknown";
        }
    }

    SceneObject* object() const { return m_object; }

signals:
    void changed();

private:
    SceneObject* m_object;
};

class MeshDataQml : public QObject {
    Q_OBJECT
    Q_PROPERTY(int vertexCount READ vertexCount)
    Q_PROPERTY(int faceCount READ faceCount)
    Q_PROPERTY(int triangleCount READ triangleCount)
    Q_PROPERTY(int materialCount READ materialCount)
    Q_PROPERTY(bool hasNormals READ hasNormals)
    Q_PROPERTY(bool hasUVs READ hasUVs)

public:
    explicit MeshDataQml(QObject* parent = nullptr) : QObject(parent), m_mesh(nullptr) {}
    explicit MeshDataQml(Mesh* mesh, QObject* parent = nullptr) : QObject(parent), m_mesh(mesh) {}

    int vertexCount() const { return m_mesh ? (int)m_mesh->vertices.size() : 0; }
    int faceCount() const { return m_mesh ? (int)(m_mesh->indices.size() / 3) : 0; }
    int triangleCount() const { return faceCount(); }
    int materialCount() const { return m_mesh ? (int)m_mesh->materialIds.size() : 0; }
    bool hasNormals() const { if (!m_mesh) return false; for (const auto &v : m_mesh->vertices) if (v.nx!=0 || v.ny!=0 || v.nz!=0) return true; return false; }
    bool hasUVs() const { if (!m_mesh) return false; for (const auto &v : m_mesh->vertices) if (v.u!=0 || v.v!=0) return true; return false; }

private:
    Mesh* m_mesh;
};

class KSModelerQml : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString currentEditorType READ currentEditorType WRITE setCurrentEditorType NOTIFY editorTypeChanged)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY sceneChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(SceneObjectQml* selectedObject READ selectedObject NOTIFY selectionChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY fileChanged)

    Q_PROPERTY(qreal camTheta READ camTheta WRITE setCamTheta NOTIFY cameraChanged)
    Q_PROPERTY(qreal camPhi READ camPhi WRITE setCamPhi NOTIFY cameraChanged)
    Q_PROPERTY(qreal camDistance READ camDistance WRITE setCamDistance NOTIFY cameraChanged)
    Q_PROPERTY(qreal camTargetX READ camTargetX WRITE setCamTargetX NOTIFY cameraChanged)
    Q_PROPERTY(qreal camTargetY READ camTargetY WRITE setCamTargetY NOTIFY cameraChanged)
    Q_PROPERTY(qreal camTargetZ READ camTargetZ WRITE setCamTargetZ NOTIFY cameraChanged)
    Q_PROPERTY(bool gridVisible READ gridVisible WRITE setGridVisible NOTIFY gridVisibleChanged)
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(QString currentViewName READ currentViewName NOTIFY cameraChanged)
    Q_PROPERTY(QVector3D gizmoPosition READ gizmoPosition NOTIFY gizmoTransformChanged)
    Q_PROPERTY(QVector3D gizmoRotation READ gizmoRotation NOTIFY gizmoTransformChanged)
    Q_PROPERTY(QVector3D gizmoScale READ gizmoScale NOTIFY gizmoTransformChanged)
    Q_PROPERTY(int gizmoMode READ gizmoMode NOTIFY gizmoModeChanged)
    Q_PROPERTY(bool curveCvEdit READ curveCvEdit WRITE setCurveCvEdit NOTIFY curveCvEditChanged)
    Q_PROPERTY(int curveSelectedCV READ curveSelectedCV WRITE setCurveSelectedCV NOTIFY curveSelectedCVChanged)
    Q_PROPERTY(int dragTargetObject READ dragTargetObject WRITE setDragTargetObject NOTIFY dragTargetChanged)
    Q_PROPERTY(bool nurbsCvVisible READ nurbsCvVisible WRITE setNurbsCvVisible NOTIFY nurbsCvVisibleChanged)
    Q_PROPERTY(int nurbsSelectedRow READ nurbsSelectedRow WRITE setNurbsSelectedRow NOTIFY nurbsSelectedCVChanged)
    Q_PROPERTY(int nurbsSelectedCol READ nurbsSelectedCol WRITE setNurbsSelectedCol NOTIFY nurbsSelectedCVChanged)
    Q_PROPERTY(int boneVersion READ boneVersion NOTIFY skeletonChanged)
    Q_PROPERTY(SceneObjectListModel* sceneModel READ sceneModel CONSTANT)
    Q_PROPERTY(bool hasClipboard READ hasClipboard NOTIFY clipboardChanged)

    // Environment / IBL (HDRI light probe for the viewport)
    Q_PROPERTY(QString environmentHDR READ environmentHDR WRITE setEnvironmentHDR NOTIFY environmentHDRChanged)

    // Viewport culling (distance-based, combined with Quick3D frustum culling)
    Q_PROPERTY(bool cullingEnabled READ cullingEnabled WRITE setCullingEnabled NOTIFY cullingChanged)
    Q_PROPERTY(qreal cullDistance READ cullDistance WRITE setCullDistance NOTIFY cullingChanged)

    // Camera matching (lens simulation for the perspective viewport)
    Q_PROPERTY(qreal cameraFocalLength READ cameraFocalLength WRITE setCameraFocalLength NOTIFY cameraChanged)
    Q_PROPERTY(qreal cameraSensorWidth READ cameraSensorWidth WRITE setCameraSensorWidth NOTIFY cameraChanged)
    Q_PROPERTY(qreal cameraFov READ cameraFov NOTIFY cameraChanged)

    // Rendering: tonemapping + exposure (Quick3D SceneEnvironment).
    Q_PROPERTY(int tonemappingMode READ tonemappingMode WRITE setTonemappingMode NOTIFY renderingChanged)
    Q_PROPERTY(qreal tonemapExposure READ tonemapExposure WRITE setTonemapExposure NOTIFY renderingChanged)

    // Subdivision cage modeling: toggle between the control cage and the
    // smoothed (subdivided) result; cage geometry is preserved and restored.
    Q_PROPERTY(bool subdivCageEnabled READ subdivCageEnabled WRITE setSubdivCageEnabled NOTIFY subdivCageChanged)
    Q_PROPERTY(int subdivCageLevel READ subdivCageLevel WRITE setSubdivCageLevel NOTIFY subdivCageChanged)

    // Raytraced viewport mode: CPU-rendered preview fed to an Image overlay.
    Q_PROPERTY(bool rayTraceEnabled READ rayTraceEnabled WRITE setRayTraceEnabled NOTIFY rayTraceEnabledChanged)
    Q_PROPERTY(int rayTraceFrame READ rayTraceFrame NOTIFY rayTraceFrameChanged)
    // Render element (AOV) shown by the preview overlay: 0=Color, 1=Depth,
    // 2=AmbientOcclusion, 3=Diffuse, 4=Normal.
    Q_PROPERTY(int rayTracePass READ rayTracePass WRITE setRayTracePass NOTIFY rayTracePassChanged)
    // Bake-to-texture: revision of the last baked map, used to refresh the
    // "image://bake/result" QML image.
    Q_PROPERTY(int bakeRevision READ bakeRevision NOTIFY bakeResultChanged)

    // Animation state for QML timeline
    Q_PROPERTY(qreal animationTime READ animationTime NOTIFY animationTimeChanged)
    Q_PROPERTY(qreal animationDuration READ animationDuration NOTIFY animationTimeChanged)
    Q_PROPERTY(bool isAnimating READ isAnimating NOTIFY playbackStateChanged)
    Q_PROPERTY(QString animationName READ animationName NOTIFY animationNameChanged)
    Q_PROPERTY(int animationFps READ animationFps CONSTANT)

public:
    explicit KSModelerQml(QObject* parent = nullptr);
    ~KSModelerQml();

    static KSModelerQml& instance() {
        static KSModelerQml inst;
        return inst;
    }

    QString currentEditorType() const { return m_currentEditorType; }
    void setCurrentEditorType(const QString& type) {
        if (m_currentEditorType != type) {
            m_currentEditorType = type;
            emit editorTypeChanged(type);
        }
    }

    QString currentFile() const { return m_currentFile; }

    int objectCount() const { return m_scene ? m_scene->allObjects().size() : 0; }
    bool hasSelection() const { return m_selectedObject != nullptr; }
    SceneObjectQml* selectedObject() const { return m_selectedObject; }

    Q_INVOKABLE void newProject();
    Q_INVOKABLE void newScene();
    Q_INVOKABLE bool saveScene(const QString& path);
    Q_INVOKABLE bool loadScene(const QString& path);
    Q_INVOKABLE bool importFile(const QString& path);
    Q_INVOKABLE bool exportFile(const QString& path);
    Q_INVOKABLE bool importKN5(const QString& path);
    Q_INVOKABLE bool importFBX(const QString& path);
    Q_INVOKABLE bool importGLB(const QString& path);
    Q_INVOKABLE bool importOBJ(const QString& path);
    Q_INVOKABLE bool importLXO(const QString& path);
    Q_INVOKABLE bool importSTL(const QString& path);
    Q_INVOKABLE bool importXSI(const QString& path);
    Q_INVOKABLE bool importGrasshopper(const QString& path);
    Q_INVOKABLE QString exportAOV(const QString& path, const QString& aov = "beauty");
    Q_INVOKABLE bool createKit(const QString& name);
    Q_INVOKABLE QStringList kitList() const;
    Q_INVOKABLE bool expressionSet(const QString& target, const QString& expr);
    Q_INVOKABLE QString expressionGet(const QString& target) const;
    Q_INVOKABLE bool fluidSimulate(int frames = 24, float viscosity = 1.0f);
    Q_INVOKABLE bool retopoQuadDraw();
    Q_INVOKABLE bool startInteractiveRetopo();
    Q_INVOKABLE bool stopInteractiveRetopo();
    Q_INVOKABLE bool addRetopoVertex();
    Q_INVOKABLE bool createRetopoQuad();
    Q_INVOKABLE bool createRetopoTriangle();
    Q_INVOKABLE bool deleteRetopoVertex();
    Q_INVOKABLE bool mergeRetopoVertices(float threshold = 0.01f);
    Q_INVOKABLE bool relaxRetopoMesh(int iterations = 10, float strength = 0.5f);
    Q_INVOKABLE void setRetopoSnapRadius(float radius);
    Q_INVOKABLE bool uvPeelSeams();
    Q_INVOKABLE bool uvPackIslands(float padding = 2.0f);
    Q_INVOKABLE QString renderAOVImage(const QString& aov, int w = 1920, int h = 1080);
    Q_INVOKABLE bool importBlend(const QString& path);
    Q_INVOKABLE bool importSTEP(const QString& path);
    Q_INVOKABLE bool exportKN5(const QString& path);
    Q_INVOKABLE bool exportFBX(const QString& path);
    Q_INVOKABLE bool exportGLB(const QString& path);
    Q_INVOKABLE bool exportOBJ(const QString& path);
    Q_INVOKABLE bool exportSTL(const QString& path);
    Q_INVOKABLE bool exportUSD(const QString& path);

    Q_INVOKABLE void selectObject(int id);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deselectAll();
    Q_INVOKABLE void toggleSelectObject(int id);
    Q_INVOKABLE QStringList selectedObjectNames() const;

    // Object hierarchy (families / rig hierarchy tools). Reparenting preserves
    // the child's world transform by recomputing its local TRS from the parent.
    Q_INVOKABLE bool groupSelected(const QString& name);
    Q_INVOKABLE void ungroupSelected();
    Q_INVOKABLE bool reparentObject(int childId, int parentId);
    Q_INVOKABLE int parentObjectId(int objectId) const;

    QVector<SceneObject*> selectedTopLevelObjects() const;

    // Scene factories (XSI-style parameterized templates). Built-ins are the
    // named primitives; user factories capture a selection (mesh + material +
    // scale) and are persisted in the .ks3d aux JSON.
    Q_INVOKABLE QStringList factoryNames() const;
    Q_INVOKABLE QString factoryType(const QString& name) const;
    Q_INVOKABLE bool factoryCreate(const QString& name, float p0 = 0.0f, float p1 = 0.0f, float p2 = 0.0f);
    Q_INVOKABLE bool factorySaveFromSelection(const QString& name);
    Q_INVOKABLE bool factoryDelete(const QString& name);
    Q_INVOKABLE QVariantMap factoryParams(const QString& name) const;
    Q_INVOKABLE int factoryUserCount() const;

    // Named selection sets (persisted in .ks3d aux JSON)
    Q_INVOKABLE QStringList selectionSetNames() const;
    Q_INVOKABLE int selectionSetMemberCount(const QString& name) const;
    Q_INVOKABLE bool createSelectionSet(const QString& name);
    Q_INVOKABLE bool addSelectionToSet(const QString& name);
    Q_INVOKABLE bool recallSelectionSet(const QString& name);
    Q_INVOKABLE bool clearSelectionSet(const QString& name);
    Q_INVOKABLE bool deleteSelectionSet(const QString& name);
    Q_INVOKABLE bool renameSelectionSet(const QString& oldName, const QString& newName);

    Q_INVOKABLE void deleteSelected();
    Q_INVOKABLE void duplicateSelected();
    Q_INVOKABLE void cutSelected();
    Q_INVOKABLE void copySelected();
    Q_INVOKABLE void pasteClipboard();
    Q_INVOKABLE void printScene();
    Q_INVOKABLE void checkMesh();
    Q_INVOKABLE void exportSTL();
    Q_INVOKABLE void scaleForPrint();
    Q_INVOKABLE void hollowMesh();
    Q_INVOKABLE void generateSupports();
    Q_INVOKABLE void sliceModel();
    bool hasClipboard() const { return m_clipboardActive; }

    Q_INVOKABLE void setGizmoMode(int mode);
    Q_INVOKABLE void translateSelected(float x, float y, float z);
    Q_INVOKABLE void rotateSelected(float x, float y, float z);
    Q_INVOKABLE void scaleSelected(float x, float y, float z);
    Q_INVOKABLE void setSelectedPosition(float x, float y, float z);
    Q_INVOKABLE void setSelectedRotation(float x, float y, float z);
    Q_INVOKABLE void setSelectedScale(float x, float y, float z);

    // Curve CV editing in the viewport (gizmo operates on the selected CV).
    Q_INVOKABLE void translateSelectedCV(float x, float y, float z);
    Q_INVOKABLE int curvePickCV(int objectId, float wx, float wy, float wz) const;
    Q_INVOKABLE QVariantList curveCvPositions(int objectId) const;
    bool curveCvEdit() const;
    void setCurveCvEdit(bool on);
    int curveSelectedCV() const;
    void setCurveSelectedCV(int index);

    // Snapping (grid/rotation)
    Q_INVOKABLE void setSnapEnabled(bool enabled);
    Q_INVOKABLE bool snapEnabled() const { return m_snapEnabled; }
    Q_INVOKABLE void setSnapIncrement(float inc);
    Q_INVOKABLE float snapIncrement() const { return m_snapIncrement; }

    // Proportional editing
    Q_INVOKABLE void setProportionalEditing(bool enabled);
    Q_INVOKABLE bool isProportionalEditing() const;
    Q_INVOKABLE void setProportionalRadius(float radius);
    Q_INVOKABLE float proportionalRadius() const;
    Q_INVOKABLE void setProportionalFalloffType(int type);
    Q_INVOKABLE int proportionalFalloffType() const;
    Q_INVOKABLE bool pickProportionalCenter(float pickX, float pickY, float pickZ);
    Q_INVOKABLE void clearProportionalCenter();
    Q_INVOKABLE bool hasProportionalCenter() const;
    Q_INVOKABLE QVector3D proportionalCenter() const;

    // Kit/Preset system
    Q_INVOKABLE modelling::PresetData presetData(const QString& name) const;
    Q_INVOKABLE QStringList presetList() const;
    Q_INVOKABLE bool savePreset(const QString& name, const QString& category);
    Q_INVOKABLE bool deletePreset(const QString& name);
    Q_INVOKABLE void applyPresetToObject(const QString& presetName, int objectId);

    void setScene(SceneGraph* scene);
    SceneGraph* sceneGraph() const { return m_scene; }
    void setEditor(core_BaseEditor* editor);

    QJsonObject modifierStackSnapshot(int objectId) const;
    bool modifierStackRestore(int objectId, const QJsonObject& state);
    QJsonObject modifierStackToJson(const ModifierStack* stack) const;
    ModifierStack* modifierStackFromJson(const QJsonObject& o);

    Q_INVOKABLE void extrudeFaces(const QList<int>& faceIndices, float distance);
    Q_INVOKABLE void bevelEdges(const QList<int>& edgeIndices, float amount, int segments);
    Q_INVOKABLE void subdivideFaces(const QList<int>& faceIndices, int cuts);
    Q_INVOKABLE void insetFaces(const QList<int>& faceIndices, float amount);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool canUndo() const;
    Q_INVOKABLE bool canRedo() const;

private:
    ks::modelling::KitSystem m_kitSystem;
    Q_INVOKABLE void translateProportional(float x, float y, float z);
    Q_INVOKABLE void rotateProportional(float x, float y, float z);
    Q_INVOKABLE void scaleProportional(float x, float y, float z);

    // Action-center transforms (Modo / 3ds Max): transform the selected mesh
    // vertices around an explicit pivot instead of the object origin.
    // `mode`: 0 = translate (tx,ty,tz delta), 1 = rotate (tx,ty,tz Euler deg),
    // 2 = uniform scale (factor = tx), 3 = per-axis scale (tx,ty,tz factors).
    // `falloffRadius` > 0 applies a spatial falloff from the pivot using the
    // current proportional-editing profile. Returns true on success.
    Q_INVOKABLE bool transformVerticesAround(int mode,
                                             float pivotX, float pivotY, float pivotZ,
                                             float tx, float ty, float tz,
                                             float falloffRadius = 0.0f);

    Q_INVOKABLE void triangulateMesh();
    Q_INVOKABLE void flipNormals();
    Q_INVOKABLE void recalculateNormals();
    Q_INVOKABLE void weldVertices(float threshold);
    Q_INVOKABLE void removeDoubles(float threshold);
    Q_INVOKABLE void mirrorMesh(int axis);
    Q_INVOKABLE void booleanOperation(int operation, int targetObjectId = -1);
    Q_INVOKABLE QVariantList getMeshObjects() const;
    // Sketch-to-solid (Plasticity P1): revolves the selected object's geometry
    // (treated as a 2D sketch profile) around `axis` (0=X,1=Y,2=Z) through
    // `angle` degrees with `steps` rings, optionally capping the open ends.
    Q_INVOKABLE bool revolveSketch(int steps = 24, float angle = 360.0f,
                                   bool closeCaps = true, int axis = 1);
    Q_INVOKABLE void knifeCut(float startX, float startY, float startZ, float endX, float endY, float endZ);
    Q_INVOKABLE bool knifeCutSelected(int objectId);
    Q_INVOKABLE bool knifeCutWorld(int objectId, float sx, float sy, float sz, float ex, float ey, float ez);

    // Fillet / chamfer (rounded/beveled edges)
    Q_INVOKABLE bool filletEdges(const QList<int>& edgeIndices, float radius);
    // Fillet chain (Plasticity P2): bevels the selected edges each at its own
    // radius so a chain tapers. `radii` is a JS array with one radius per
    // selected edge + a uniform `vrn`-style fallback is not needed - supply
    // per-edge radii or one shared value.
    Q_INVOKABLE bool filletChain(const QList<int>& edgeIndices, const QVariantList& radii,
                                 int segments = 1, float angleLimitDeg = 40.0f);
    Q_INVOKABLE bool chamferEdges(const QList<int>& edgeIndices, float distance);

    // Push/pull and offset selected faces
    Q_INVOKABLE bool pushPullFaces(const QList<int>& faceIndices, float distance);
    Q_INVOKABLE bool offsetSelectedFaces(const QList<int>& faceIndices, float distance);

    // Hole fill / mesh extraction (phase-3 gaps). fillMeshHoles caps every open
    // boundary loop in place and returns the number of holes filled.
    // extractSelectedFaces builds a standalone (optionally solid) object from the
    // sub-object face selection and returns the new object id (-1 on failure).
    Q_INVOKABLE int fillMeshHoles(int maxHoleEdges = 16);
    Q_INVOKABLE int extractSelectedFaces(float thickness = 0.0f, bool closeCaps = true);

    // Section analysis - cut mesh with a plane
    Q_INVOKABLE bool cutByPlane(float px, float py, float pz, float nx, float ny, float nz);
    // Non-destructive section preview: creates (or updates) a section-profile
    // object for the selected mesh without replacing it. Returns the profile id.
    Q_INVOKABLE int createSectionPreview(float px, float py, float pz, float nx, float ny, float nz);
    Q_INVOKABLE bool updateSectionPreview(int profileId, float px, float py, float pz, float nx, float ny, float nz);
    Q_INVOKABLE bool deleteSectionPreview(int profileId);
    // Applies the section cut permanently to the target object (destructive).
    Q_INVOKABLE bool applySectionPreview(int profileId, int targetObjectId);

    // Construction plane + snapping
    Q_INVOKABLE void setCPlane(float ox, float oy, float oz, float nx, float ny, float nz, float ux, float uy, float uz);
    Q_INVOKABLE QVariantMap getCPlane() const;
    Q_INVOKABLE QVariantMap snapToCPlane(float px, float py, float pz) const;
    // Mesh-aware snapping: snaps a world-space point against the selected
    // object's geometry (vertex/midpoint/edge/face/tangent) honoring the
    // current vertex-size tolerance, returns {point, hit} in world space.
    Q_INVOKABLE QVariantMap snapToMesh(float px, float py, float pz, int snapTypes) const;
    Q_INVOKABLE void snapTypes(int types);
    Q_INVOKABLE int snapTypes() const;

    // Arrays
    Q_INVOKABLE bool linearArray(int count, float ox, float oy, float oz);
    Q_INVOKABLE bool radialArray(int count, float axisX, float axisY, float axisZ, float angle);
    // 2D grid pattern: countX columns, countY rows, spacing (sx, sy, sz).
    Q_INVOKABLE bool gridArray(int countX, int countY, float sx, float sy, float sz);

    // Live instances (Plasticity-style). An instance shares the master's mesh;
    // edits to the master propagate to every instance automatically.
    Q_INVOKABLE int createInstance(int masterId);
    Q_INVOKABLE bool realizeInstance(int instanceId);
    Q_INVOKABLE QVariantList getInstances(int masterId);
    Q_INVOKABLE int masterOfInstance(int objectId);
    Q_INVOKABLE bool isInstance(int objectId);
    Q_INVOKABLE int instanceCount(int masterId);
    // Removes a single instance (deletes the scene object, keeps the master).
    Q_INVOKABLE bool deleteInstance(int instanceId);
    // Pushes the master's current mesh to all of its instances (manual refresh).
    Q_INVOKABLE bool updateInstances(int masterId);

    // Live dimensions
    Q_INVOKABLE void addDistanceDimension(int v1, int v2, const QString& label, int objectId = -1);
    Q_INVOKABLE void addAngleDimension(int v1, int v2, int v3, const QString& label, int objectId = -1);
    Q_INVOKABLE void addRadiusDimension(int vertex, const QList<int>& edgeIndices, const QString& label, int objectId = -1);
    Q_INVOKABLE void addDiameterDimension(int vertex, const QList<int>& edgeIndices, const QString& label, int objectId = -1);
    Q_INVOKABLE float computeDistanceValue(int objectId, int v1, int v2);
    Q_INVOKABLE float computeAngleValue(int objectId, int v1, int v2, int v3);
    Q_INVOKABLE QVariantList dimensions() const;
    // Clears all stored dimensions.
    Q_INVOKABLE void clearDimensions();
    // Removes a single dimension by index (0=distance,1=angle,2=radius).
    Q_INVOKABLE bool removeDimension(int type, int index);
    // Toggles visibility of a stored dimension (0=distance,1=angle,2=radius).
    Q_INVOKABLE void setDimensionVisible(int type, int index, bool visible);

    // Radial menu / context menu
    Q_INVOKABLE void showRadialMenu(int mode, float px, float py);
    Q_INVOKABLE void hideRadialMenu();
    Q_INVOKABLE QVariantMap radialMenuState() const;
    Q_INVOKABLE void showContextMenu(float wx, float wy, float wz);

    // Sub-object selection
    Q_INVOKABLE void setSubobjectMode(int mode); // 0=Vertex,1=Edge,2=Face,3=Object,4=Border,5=Element
    Q_INVOKABLE int subobjectMode() const;
    Q_INVOKABLE QVariantList selectedSubVertices() const;
    Q_INVOKABLE QVariantList selectedSubEdges() const;
    Q_INVOKABLE QVariantList selectedSubFaces() const;
    Q_INVOKABLE QVariantList selectedBorderEdges() const;
    Q_INVOKABLE void addSelectedVertex(int vertexIndex);
    Q_INVOKABLE void addSelectedEdge(int edgeIndex);
    Q_INVOKABLE void addSelectedFace(int faceIndex);
    Q_INVOKABLE void clearSubSelection();
    Q_INVOKABLE bool hideFace(int faceIndex);
    Q_INVOKABLE bool unhideFace(int faceIndex);
    Q_INVOKABLE void unhideAllFaces();
    Q_INVOKABLE QVariantList getFaceNeighbors(int faceIndex);
    Q_INVOKABLE bool exportSTEP(const QString& path, bool useBREP = false);
    // Technical SVG export of the selected object (orthographic projection,
    // visible edges solid, hidden edges dashed). viewAxis: 0=X,1=Y,2=Z.
    Q_INVOKABLE bool exportHiddenLineSVG(const QString& path, int viewAxis = 2, float lineWidth = 0.3f);

    // Vertex/Edge slide (sub-object picking)
    Q_INVOKABLE int findClosestVertex(int objectId, float wx, float wy, float wz);
    Q_INVOKABLE QVariantMap findClosestEdge(int objectId, float wx, float wy, float wz);
    Q_INVOKABLE bool vertexSlide(int objectId, int vertexIndex, float wx, float wy, float wz);
    Q_INVOKABLE bool edgeSlide(int objectId, int v0, int v1, float factor);
    // Edge loop/ring selection tools: return the edges (world-space
    // [[ax,ay,az,bx,by,bz],...]) forming the loop/ring of the given edge.
    Q_INVOKABLE QVariantList edgeLoop(int objectId, int v0, int v1);
    Q_INVOKABLE QVariantList edgeRing(int objectId, int v0, int v1);
    Q_INVOKABLE bool loopCut(int objectId, int axis, float factor, float slide = 0.0f);
    Q_INVOKABLE int sculptBrush(int objectId, float wx, float wy, float wz, float radius,
                                float strength, int mode, float dx, float dy, float dz,
                                float px = 0.0f, float py = 0.0f, float pz = 0.0f,
                                float falloffPower = 2.0f);
    // Pin/lock vertices so sculpt brushes never move them. Pins persist per
    // object in the aux .ks3d metadata and survive topology edits by index.
    Q_INVOKABLE void setSculptPins(int objectId, const QList<int>& vertexIndices, bool pinned = true);
    Q_INVOKABLE void clearSculptPins(int objectId);
    Q_INVOKABLE int sculptPinnedCount(int objectId) const;
    Q_INVOKABLE QVariantList sculptPinnedVertices(int objectId) const;
    Q_INVOKABLE QStringList sculptBrushNames() const;

    // ---- Multiresolution sculpting (Mudbox-style level management) ----
    Q_INVOKABLE QVariantList multiresLevelList(int objectId) const;
    Q_INVOKABLE int multiresLevelCount(int objectId) const;
    Q_INVOKABLE int multiresCurrentLevel(int objectId) const;
    Q_INVOKABLE bool multiresSetCurrentLevel(int objectId, int level);
    Q_INVOKABLE bool multiresAddLevel(int objectId);
    Q_INVOKABLE bool multiresRemoveLevel(int objectId);
    Q_INVOKABLE bool multiresSubdivide(int objectId);
    Q_INVOKABLE bool multiresBake(int objectId);
    Q_INVOKABLE int multiresVertexCount(int objectId, int level = -1) const;

    // ---- Sculpt layers (Mudbox-style per-layer sculpting) ----
    Q_INVOKABLE QVariantList sculptLayerList() const;
    Q_INVOKABLE int sculptLayerCount() const;
    Q_INVOKABLE int sculptLayerCurrent() const;
    Q_INVOKABLE void sculptLayerSetCurrent(int index);
    Q_INVOKABLE int sculptLayerAdd(const QString& name);
    Q_INVOKABLE bool sculptLayerRemove(int index);
    Q_INVOKABLE bool sculptLayerRename(int index, const QString& name);
    Q_INVOKABLE bool sculptLayerSetVisible(int index, bool visible);
    Q_INVOKABLE bool sculptLayerSetLocked(int index, bool locked);
    Q_INVOKABLE bool sculptLayerSetBlendMode(int index, float mode);
    Q_INVOKABLE bool sculptLayerSetOpacity(int index, float opacity);
    Q_INVOKABLE bool sculptLayerBakeCurrent();
    Q_INVOKABLE QVariantList sculptLayerWeights(int index) const;

    // ---- Projection painting (stencil from image, 3D projection) ----
    Q_INVOKABLE bool projectionLoadStencil(const QString& imagePath);
    Q_INVOKABLE bool projectionLoadStencilData(const QByteArray& imageBytes);
    Q_INVOKABLE bool projectionSetStencilPosition(float x, float y, float z);
    Q_INVOKABLE bool projectionSetStencilRotation(float x, float y, float z);
    Q_INVOKABLE bool projectionSetStencilScale(float x, float y, float z);
    Q_INVOKABLE bool projectionSetStencilOpacity(float opacity);
    Q_INVOKABLE bool projectionSetStencilUseAlpha(bool useAlpha);
    Q_INVOKABLE bool projectionSetStencilLoop(bool loop);
    Q_INVOKABLE QVariantMap projectionStencilInfo() const;
    Q_INVOKABLE int projectionPaint(int objectId, float wx, float wy, float wz,
                                    float radius, float strength, int mode,
                                    float dx = 0, float dy = 0, float dz = 0);
    Q_INVOKABLE int projectionClone(int objectId, float srcU, float srcV,
                                    float dstU, float dstV, float strength,
                                    float blendMode = 1.0f);

    // ---- Tiling textures (seamless tiling preview + generation) ----
    Q_INVOKABLE QByteArray tilingGenerateSeamless(const QByteArray& imageData, int tileSize, int blendRadius);
    Q_INVOKABLE QByteArray tilingPreview(const QByteArray& imageData, int repeats);
    Q_INVOKABLE bool tilingApplyToObject(int objectId, const QByteArray& tiledImage);

    // ---- Matcap rendering ----
    Q_INVOKABLE bool matcapSetEnabled(bool enabled);
    Q_INVOKABLE bool matcapIsEnabled() const;
    Q_INVOKABLE bool matcapLoad(const QString& imagePath);
    Q_INVOKABLE bool matcapLoadData(const QByteArray& imageBytes);
    Q_INVOKABLE QVariantList matcapPresets() const;
    Q_INVOKABLE bool matcapApplyPreset(const QString& name);

    // ---- Wireframe overlay (wireframe-on-shaded) ----
    Q_INVOKABLE bool wireframeOverlaySetEnabled(bool enabled);
    Q_INVOKABLE bool wireframeOverlayIsEnabled() const;
    Q_INVOKABLE bool wireframeOverlaySetColor(float r, float g, float b, float a);
    Q_INVOKABLE bool wireframeOverlaySetThickness(float pixels);
    Q_INVOKABLE QVariantMap wireframeOverlayInfo() const;

    // ---- Silhouette display ----
    Q_INVOKABLE bool silhouetteSetEnabled(bool enabled);
    Q_INVOKABLE bool silhouetteIsEnabled() const;
    Q_INVOKABLE bool silhouetteSetColor(float r, float g, float b, float a);
    Q_INVOKABLE bool silhouetteSetThreshold(float angleDeg);
    Q_INVOKABLE QVariantMap silhouetteInfo() const;

    // ---- Turntable auto-rotate ----
    Q_INVOKABLE bool turntableSetEnabled(bool enabled);
    Q_INVOKABLE bool turntableIsEnabled() const;
    Q_INVOKABLE bool turntableSetSpeed(float degreesPerSecond);
    Q_INVOKABLE bool turntableSetAxis(float x, float y, float z);
    Q_INVOKABLE QVariantMap turntableInfo() const;

    Q_INVOKABLE void dissolveEdges(const QList<int>& edgeIndices);
    Q_INVOKABLE void dissolveVertices(const QList<int>& vertexIndices);
    Q_INVOKABLE void splitMeshes();

    // UV seams (real Mark Seam): mark the edge under the cursor as a seam on
    // the selected object; unwrapUVs merges stored seams with auto-detection.
    Q_INVOKABLE bool markSeamFromClosestEdge(float wx, float wy, float wz);
    Q_INVOKABLE bool clearSeams();
    Q_INVOKABLE int seamEdgeCount();

    // ---- Fase-1 gaps: Shell, Bridge, Smoothing groups, Border/Element ----
    // Shell: thicken the selected object's surface by `thickness` along its
    // averaged vertex normals and close open boundaries with rim walls.
    Q_INVOKABLE bool applyShell(float thickness, int direction = 1, bool flipNormals = false);
    // Bridge: connect two selected edge loops (sub-object edge selection) or two
    // selected faces with a strip of quads, `segments` intermediate rings.
    Q_INVOKABLE bool bridgeSelectedLoops(int segments = 1);
    Q_INVOKABLE bool bridgeSelectedFaces(int segments = 1);
    // Auto-smooth: partition the selected object's faces into smoothing groups
    // based on the dihedral angle threshold. Returns the number of groups.
    Q_INVOKABLE int smoothGroupsAuto(float angleDeg = 30.0f);
    Q_INVOKABLE QVariantList smoothGroupFaceIds(int groupId) const;
    Q_INVOKABLE int smoothGroupCount() const;
    Q_INVOKABLE bool smoothGroupSetFace(int objectId, int faceIndex, int groupId);
    Q_INVOKABLE int smoothGroupForFace(int objectId, int faceIndex) const;
    Q_INVOKABLE int smoothGroupAssignSelected(int groupId);
    Q_INVOKABLE void smoothGroupClear(int objectId);
    // Splits vertices at smoothing-group boundaries so group seams become real
    // geometric edges on the selected object. Returns the new vertex count.
    Q_INVOKABLE int splitSmoothingGroupsMesh();

    // ---- Face Groups (Mudbox-style named/colored/masked face regions) ----
    Q_INVOKABLE int faceGroupCreate(const QString& name, int color = 0);
    Q_INVOKABLE bool faceGroupRemove(int index);
    Q_INVOKABLE bool faceGroupRename(int index, const QString& name);
    Q_INVOKABLE bool faceGroupSetColor(int index, int color);
    Q_INVOKABLE bool faceGroupSetVisible(int index, bool visible);
    Q_INVOKABLE QVariantList faceGroupList() const;
    // Assigns the currently selected sub-faces of the selected object to the
    // group (pass -1 to remove them from any group).
    Q_INVOKABLE int faceGroupAssignSelected(int groupIndex);
    Q_INVOKABLE void faceGroupAssignFaces(int objectId, const QVariantList& faceIndices, int groupIndex);
    Q_INVOKABLE int faceGroupForFace(int objectId, int faceIndex) const;
    Q_INVOKABLE QVariantList faceGroupFaces(int objectId, int groupIndex) const;
    Q_INVOKABLE void faceGroupClearObject(int objectId);

    // Border/Element mode helpers: select the border edge under the cursor and
    // determine the connected element of a face under the cursor.
    Q_INVOKABLE QVariantList findClosestBorder(int objectId, float wx, float wy, float wz);
    Q_INVOKABLE int elementAtWorld(int objectId, float wx, float wy, float wz);
    Q_INVOKABLE bool selectBorderUnderCursor(float wx, float wy, float wz);
    Q_INVOKABLE bool selectElementUnderCursor(float wx, float wy, float wz);

    // ---- Layers (Max-style scene layers) ----
    Q_INVOKABLE QStringList layerNames() const;
    Q_INVOKABLE int layerCount() const;
    Q_INVOKABLE bool addLayer(const QString& name);
    Q_INVOKABLE bool removeLayer(int index);
    Q_INVOKABLE bool setLayerVisible(int index, bool visible);
    Q_INVOKABLE bool isLayerVisible(int index) const;
    Q_INVOKABLE bool setLayerColor(int index, const QColor& color);
    Q_INVOKABLE int assignSelectionToLayer(int index);
    Q_INVOKABLE int objectLayerId(int objectId) const;
    Q_INVOKABLE bool setObjectLayer(int objectId, int layerIndex);
    Q_INVOKABLE int currentLayerIndex() const;
    Q_INVOKABLE void setCurrentLayerIndex(int index);

    // Shape key / morph target operations
    Q_INVOKABLE int addShapeKey(const QString& name);
    Q_INVOKABLE bool removeShapeKey(int index);
    Q_INVOKABLE bool renameShapeKey(int index, const QString& newName);
    Q_INVOKABLE int captureShapeKey(const QString& name);
    Q_INVOKABLE void setShapeKeyWeight(int index, float weight);
    Q_INVOKABLE float getShapeKeyWeight(int index);
    Q_INVOKABLE QStringList getShapeKeyNames();
    Q_INVOKABLE int getShapeKeyCount();
    Q_INVOKABLE void resetShapeKeys();
    Q_INVOKABLE void muteShapeKey(int index, bool mute);
    Q_INVOKABLE void applyShapeKeys();
    Q_INVOKABLE void clearAllShapeKeys();

    // LOD generation
    Q_INVOKABLE bool generateLODs(int levelCount = 3);
    Q_INVOKABLE bool exportLODs(const QString& basePath);
    Q_INVOKABLE void setLODDistance(int level, float distance);
    Q_INVOKABLE float getLODDistance(int level) const;

    // Non-destructive modifier stack
    Q_INVOKABLE bool modifierStackAdd(const QString& type);
    Q_INVOKABLE bool modifierStackRemove(int index);
    Q_INVOKABLE bool modifierStackMove(int fromIndex, int toIndex);
    Q_INVOKABLE bool modifierStackSetEnabled(int index, bool enabled);
    Q_INVOKABLE bool modifierStackSetParam(int index, const QString& name, const QVariant& value);
    Q_INVOKABLE void modifierStackFreeze();
    Q_INVOKABLE void modifierStackClear();
    Q_INVOKABLE int modifierStackCount() const;
    Q_INVOKABLE QStringList modifierStackTypes() const;
    Q_INVOKABLE QStringList modifierStackParamNames(int index) const;
    Q_INVOKABLE QVariantList modifierStackList() const;

    // Subdivision crease / pinned-vertex support (TurboSmooth-style).
    // Uses the current edge/vertex sub-object selection on the selected object.
    Q_INVOKABLE bool subdivisionCreaseSelectedEdges(int stackIndex);
    Q_INVOKABLE bool subdivisionPinSelectedVertices(int stackIndex);
    Q_INVOKABLE void subdivisionClearPinnedVertices(int stackIndex);

    // Curve / NURBS modeling
    Q_INVOKABLE int addCurve(const QString& type, const QVariantList& points);
    Q_INVOKABLE QVariantMap getCurve(int objectId) const;
    Q_INVOKABLE bool setCurve(int objectId, const QVariantMap& curve);
    Q_INVOKABLE QVariantList curvePoints(int objectId, int segments = 32) const;
    Q_INVOKABLE bool curveUpdateCV(int objectId, int index, const QVariantList& xyz);
    Q_INVOKABLE void curveSetContinuity(int objectId, int continuity); // 0=C0 1=C1 2=C2
    Q_INVOKABLE int curveContinuityOf(int objectId) const;
    Q_INVOKABLE bool curveAddCV(int objectId, const QVariantList& xyz);
    Q_INVOKABLE bool curveRemoveCV(int objectId, int index);
    Q_INVOKABLE int curveToMesh(int objectId, float width = 0.02f, int segments = 32);
    Q_INVOKABLE int curveLoft(const QVariantList& objectIds, int segments = 32);
    Q_INVOKABLE int curveSweep(int profileId, int pathId, int segments = 32);
    Q_INVOKABLE int curveRevolve(int profileId, float angleDeg = 360.0f, int steps = 32);
    Q_INVOKABLE int curveRail(const QVariantList& railIds, const QVariantList& profileId, int segments = 32);
    Q_INVOKABLE bool curveDelete(int objectId);
    // All curve object ids currently in the scene (from the curve registry).
    Q_INVOKABLE QVariantList curveIds() const;

    // NURBS surface modeling (real NURBS via MeshOperations, not just curves)
    // Creates a NURBS surface from a 2D grid of control points given as a flat
    // list of "u rows", each a list of [x,y,z] triplets.
    Q_INVOKABLE int nurbsSurfaceCreate(const QVariantList& rows, int uDegree = 3, int vDegree = 3);
    // Recreates the object's mesh by tessellating its stored NURBS surface.
    Q_INVOKABLE bool nurbsSurfaceTessellate(int objectId, int uSeg = 32, int vSeg = 32);
    // Evaluates u,v in [0,1] and returns [x,y,z]. Auto-tessellates into a temp
    // mesh when no surface is stored yet but the object is a NURBS surface.
    Q_INVOKABLE QVariantList nurbsSurfaceEvaluate(int objectId, float u, float v) const;
    // Builds a NURBS surface from the control points of the given curve ids
    // (loft along V): row i = curve i's control points.
    Q_INVOKABLE int nurbsSurfaceLoftCurves(const QVariantList& curveIds, int uDegree = 3, int vDegree = 3);
    // Stores mesh-evaluated NURBS grid as an object and returns its id.
    Q_INVOKABLE QVariantMap nurbsSurfaceInfo(int objectId) const;
    // Removes a NURBS surface and its scene object.
    Q_INVOKABLE bool nurbsSurfaceDelete(int objectId);
    // Extends the stored NURBS surface along the given edge and re-tessellates.
    // direction: 0=U-, 1=U+, 2=V-, 3=V+.
    Q_INVOKABLE bool nurbsSurfaceExtend(int objectId, int direction, float distance);
    // Slides a control point (row,col) by factor in [-1,1] and re-tessellates.
    Q_INVOKABLE bool nurbsSurfaceSlideCV(int objectId, int row, int col, float factor);
    // Returns the world-space control-point grid as [[[x,y,z],...],...] (rows).
    Q_INVOKABLE QVariantList nurbsSurfaceCvPositions(int objectId) const;
    // Moves a control point (row,col) to an absolute local position and re-tessellates.
    Q_INVOKABLE bool nurbsSurfaceMoveCV(int objectId, int row, int col, float x, float y, float z);
    // Builds a curvature-comb visualization object for the surface.
    Q_INVOKABLE int nurbsSurfaceCurvatureComb(int objectId, int direction, int combCount = 24, float scale = 0.1f);

    // F-Curve / Dope Sheet
    Q_INVOKABLE QVariantMap fcurveGet(int objectId) const;
    Q_INVOKABLE QStringList fcurveChannelNames(int objectId) const;
    Q_INVOKABLE QVariantList fcurveKeys(int objectId, const QString& channel) const;
    Q_INVOKABLE bool fcurveSetKey(int objectId, const QString& channel, float frame, float value, const QString& interpolation = "Cubic");
    Q_INVOKABLE bool fcurveRemoveKey(int objectId, const QString& channel, float frame);
    Q_INVOKABLE bool fcurveMoveKey(int objectId, const QString& channel, int index, float newFrame);
    Q_INVOKABLE bool fcurveSetValue(int objectId, const QString& channel, int index, float value);
    Q_INVOKABLE bool fcurveSetInterpolation(int objectId, const QString& channel, int index, const QString& interp);
    Q_INVOKABLE bool fcurveSetTangentHandle(int objectId, const QString& channel, int index, bool isOut, float handleFrame, float handleValue);
    Q_INVOKABLE bool fcurveSetTangentMode(int objectId, const QString& channel, int index, const QString& mode);
    Q_INVOKABLE bool fcurveCanUndo(int objectId) const;
    Q_INVOKABLE bool fcurveCanRedo(int objectId) const;
    Q_INVOKABLE bool fcurveUndo(int objectId);
    Q_INVOKABLE bool fcurveRedo(int objectId);
    Q_INVOKABLE bool fcurvePushUndo(int objectId);
    Q_INVOKABLE float fcurveEvaluate(int objectId, const QString& channel, float frame) const;
    Q_INVOKABLE bool fcurveApplyToObject(int objectId, float frame);
    Q_INVOKABLE bool fcurveRemoveObject(int objectId);
    Q_INVOKABLE QVariantList fcurveFrameKeys(int objectId, float frame, float tolerance = 0.05f) const;
    Q_INVOKABLE void fcurvePlayPause();
    Q_INVOKABLE bool fcurveIsPlaying() const;

    // Non-destructive Boolean stack
    Q_INVOKABLE bool booleanAdd(int objectId, int operation, int operandId);
    Q_INVOKABLE bool booleanRemove(int objectId, int index);
    Q_INVOKABLE bool booleanMove(int objectId, int fromIndex, int toIndex);
    Q_INVOKABLE bool booleanSetEnabled(int objectId, int index, bool enabled);
    Q_INVOKABLE bool booleanSetOperation(int objectId, int index, int operation);
    Q_INVOKABLE bool booleanClear(int objectId);
    Q_INVOKABLE bool booleanApply(int objectId);
    Q_INVOKABLE bool booleanEvaluate(int objectId);
    // CAGE re-edit: switches the selection to the operand of stack row `index`
    // so it can be edited directly. Editing the operand geometry re-evaluates
    // the parent boolean stack live. Returns the operand object id, or -1.
    Q_INVOKABLE int booleanSelectOperand(int objectId, int index);
    Q_INVOKABLE bool booleanHasStack(int objectId) const;
    Q_INVOKABLE QVariantList booleanStack(int objectId) const;

    // Rigging constraints (Point / Orientation / Aim / Parent / Path / Attachment / Link / Spring)
    Q_INVOKABLE bool constraintAdd(int objectId, int type, int targetId,
                                   float ox = 0.0f, float oy = 0.0f, float oz = 0.0f,
                                   float rx = 0.0f, float ry = 0.0f, float rz = 0.0f);
    Q_INVOKABLE bool constraintRemove(int objectId, int index);
    Q_INVOKABLE bool constraintSetEnabled(int objectId, int index, bool enabled);
    Q_INVOKABLE bool constraintSetOffset(int objectId, int index, float ox, float oy, float oz);
    // Path constraint: snaps the target's spline (from its CurveData) into local samples.
    Q_INVOKABLE bool constraintAddPath(int objectId, int targetId, int segments,
                                       float t = 0.0f, bool follow = false);
    // Attachment constraint: binds to the target mesh vertex `vertexIndex`.
    Q_INVOKABLE bool constraintAddAttachment(int objectId, int targetId, int vertexIndex,
                                             float ox = 0.0f, float oy = 0.0f, float oz = 0.0f);
    Q_INVOKABLE bool constraintSetParam(int objectId, int index, float param);
    Q_INVOKABLE bool constraintSetFollow(int objectId, int index, bool follow);
    Q_INVOKABLE bool constraintSetSpringParams(int objectId, int index, float stiffness, float damping);
    Q_INVOKABLE bool constraintEvaluate(int objectId);
    Q_INVOKABLE QVariantList constraintList(int objectId) const;

    // Procedural controllers (Noise / Spring / LookAt / Attachment)
    Q_INVOKABLE bool controllerAdd(int objectId, int type, int targetId, const QString& channel,
                                   float base = 0.0f, float amplitude = 1.0f, float frequency = 1.0f,
                                   float phase = 0.0f, float stiffness = 50.0f, float damping = 2.0f);
    Q_INVOKABLE bool controllerRemove(int objectId, int index);
    Q_INVOKABLE bool controllerSetEnabled(int objectId, int index, bool enabled);
    Q_INVOKABLE bool controllerSetParams(int objectId, int index, float amplitude = 1.0f,
                                         float frequency = 1.0f, float phase = 0.0f,
                                         float stiffness = 50.0f, float damping = 2.0f);
    Q_INVOKABLE bool controllerSetAttachment(int objectId, int index, int vertexIndex,
                                             float ox = 0.0f, float oy = 0.0f, float oz = 0.0f);
    Q_INVOKABLE bool controllerEvaluate(int objectId);
    Q_INVOKABLE bool controllerEvaluateAll();
    Q_INVOKABLE QVariantList controllerList(int objectId) const;

    // Wire Parameters (driver property -> driven property)
    Q_INVOKABLE bool wireAdd(int driverId, const QString& driverProp,
                             int drivenId, const QString& drivenProp,
                             float scale = 1.0f, float offset = 0.0f);
    Q_INVOKABLE bool wireRemove(int drivenId, int index);
    Q_INVOKABLE bool wireSetEnabled(int drivenId, int index, bool enabled);
    Q_INVOKABLE bool wireSetParams(int drivenId, int index, float scale, float offset);
    Q_INVOKABLE bool wireSetProperty(int drivenId, int index, const QString& drivenProp);
    Q_INVOKABLE bool wireSetExpression(int drivenId, int index, const QString& expression);
    Q_INVOKABLE bool wireEvaluateAll();
    Q_INVOKABLE QVariantList wireList(int objectId) const;

    // Skin Wrap (deform skin mesh by a cage mesh)
    Q_INVOKABLE bool skinWrapAdd(int objectId, int cageId);
    Q_INVOKABLE bool skinWrapRemove(int objectId, int index);
    Q_INVOKABLE bool skinWrapSetEnabled(int objectId, int index, bool enabled);
    Q_INVOKABLE bool skinWrapRebind(int objectId, int index);
    Q_INVOKABLE bool skinWrapEvaluate(int objectId);
    Q_INVOKABLE bool skinWrapEvaluateAll();
    Q_INVOKABLE QVariantList skinWrapList(int objectId) const;

    // ICE particle system (node-based)
    Q_INVOKABLE bool iceCreate(int objectId);
    Q_INVOKABLE bool iceRemove(int objectId);
    Q_INVOKABLE bool iceAddNode(int objectId, const QString& type, float x, float y);
    Q_INVOKABLE bool iceRemoveNode(int objectId, const QString& nodeId);
    Q_INVOKABLE bool iceSetNodeProperty(int objectId, const QString& nodeId, const QString& prop, const QVariant& value);
    Q_INVOKABLE bool iceConnect(int objectId, const QString& fromNode, const QString& fromPort,
                                const QString& toNode, const QString& toPort);
    Q_INVOKABLE bool iceRemoveConnection(int objectId, const QString& fromNode, const QString& toNode);
    Q_INVOKABLE bool iceSetNodePosition(int objectId, const QString& nodeId, float x, float y);
    Q_INVOKABLE QVariantMap iceGetGraph(int objectId) const;
    Q_INVOKABLE QVariantList iceGetPositions(int objectId) const;
    Q_INVOKABLE QVariantList iceGetColors(int objectId) const;
    Q_INVOKABLE QVariantList iceGetSizes(int objectId) const;
    Q_INVOKABLE int iceGetAliveCount(int objectId) const;
    Q_INVOKABLE bool icePlayPause(int objectId, bool play);
    Q_INVOKABLE void iceStopAll();
    Q_INVOKABLE bool iceSetCollisionObject(int objectId, int collisionObjectId);
    Q_INVOKABLE bool iceSetEmitterObject(int objectId, int emitterObjectId);
    Q_INVOKABLE bool iceBake(int objectId, int frames);
    Q_INVOKABLE bool iceScrubToFrame(int objectId, int frame);
    Q_INVOKABLE int iceCacheLength(int objectId) const;
    Q_INVOKABLE void iceClearCache(int objectId);
    // Render mode preference for ICE particles (true = instanced spheres, false = GL points).
    Q_PROPERTY(bool iceSpheresEnabled READ iceSpheresEnabled WRITE setIceSpheresEnabled NOTIFY iceSpheresEnabledChanged)
    bool iceSpheresEnabled() const { return m_iceSpheresEnabled; }
    void setIceSpheresEnabled(bool on);

    // Deformation tools
    Q_INVOKABLE void applyCageDeform(const QString& cageObjectName, float strength, bool preserveVolume);
    Q_INVOKABLE void cageDeformBuildCage(int subdivisions);
    Q_INVOKABLE void applyLattice(int uDivs, int vDivs, int wDivs, float strength);
    Q_INVOKABLE void applySimpleDeform(int method, int axis, float angle, float factor);
    Q_INVOKABLE QVector3D getLatticeControlPoint(int index);
    Q_INVOKABLE void setLatticeControlPoint(int index, float x, float y, float z);
    Q_INVOKABLE int getLatticeControlPointCount();
    Q_INVOKABLE void resetLattice();

    // Collision mesh generation
    Q_INVOKABLE bool generateCollisionMesh(int shapeType);
    Q_INVOKABLE bool generateCollisionConvexDecomp(int maxHulls);
    Q_INVOKABLE void setCollisionSimplifyRatio(float ratio);
    Q_INVOKABLE float getCollisionSimplifyRatio() const;

    // Shape key animation
    Q_INVOKABLE void animateShapeKey(const QString& keyName, int startFrame, int endFrame, float startWeight, float endWeight);
    Q_INVOKABLE void clearShapeKeyAnimation(const QString& keyName);
    Q_INVOKABLE void evaluateShapeKeyAnimation(int frame);

    Q_INVOKABLE void addPrimitiveCube(float size = 1.0f);
    Q_INVOKABLE void addPrimitiveSphere(float radius = 1.0f, int segments = 32, int rings = 16);
    Q_INVOKABLE void addPrimitiveCylinder(float radius = 1.0f, float height = 2.0f, int segments = 32);
    Q_INVOKABLE void addPrimitiveCone(float radius = 1.0f, float height = 2.0f, int segments = 32);
    Q_INVOKABLE void addPrimitiveTorus(float majorRadius = 1.0f, float minorRadius = 0.25f, int majorSegments = 32, int minorSegments = 16);
    Q_INVOKABLE void addPrimitivePlane(float width = 2.0f, float height = 2.0f, int widthSegments = 1, int heightSegments = 1);

    Q_INVOKABLE void projectUVPlanar(int axis);
    Q_INVOKABLE void projectUVCylindrical(int axis);
    Q_INVOKABLE void projectUVSpherical();
    Q_INVOKABLE void projectUVBox(float size);
    Q_INVOKABLE void unwrapUVs(const QString& method);
    Q_INVOKABLE void packUVs(float margin, int resolution);
    // Detects UV-island overlaps in texture space and re-packs the islands into a
    // non-overlapping, normalized layout (3ds Max "overlap resolution"). Returns
    // true when the UVs had to be fixed.
    Q_INVOKABLE bool resolveUVOverlaps();
    Q_INVOKABLE QVariantList analyzeUVDensity(int objectId);
    Q_INVOKABLE QString uvDensityHeatmap(int objectId, int w = 512, int h = 512);
    Q_INVOKABLE QString uvOverlapHeatmap(int objectId, int w = 512, int h = 512);
    Q_INVOKABLE bool createXRef(const QString& path, float x = 0, float y = 0, float z = 0);
    Q_INVOKABLE bool updateXRefs();
    Q_INVOKABLE QVariantList xrefList() const;
    Q_INVOKABLE float sceneTolerance() const;
    Q_INVOKABLE void setSceneTolerance(float t);
    Q_INVOKABLE float sceneUnitScale() const;
    Q_INVOKABLE void setSceneUnitScale(float s);
    Q_INVOKABLE bool retargetSkeleton(int srcBone, int dstBone);
    Q_INVOKABLE bool applyClusterDeform(const QList<int>& indices, float dx, float dy, float dz, float w = 1.0f);
    Q_INVOKABLE bool applyBlendShape(int targetObjectId, float weight);
    Q_INVOKABLE QVariantList fcurveFilteredKeys(int objectId, const QString& channel, float from, float to) const;
    Q_INVOKABLE bool bevelEdgesAdvanced(const QList<int>& edgeIndices, float distance, int segments, int profileType, float tension);
    Q_INVOKABLE void translateUVs(float u, float v);
    Q_INVOKABLE void rotateUVs(float angle);
    Q_INVOKABLE void scaleUVs(float u, float v);
    Q_INVOKABLE void generateAutoUVs(int resolution);

    Q_INVOKABLE void createSkeleton(const QString& type);
    Q_INVOKABLE void addBone(const QString& name, int parentId, float x, float y, float z);
    Q_INVOKABLE void bindToSkeleton(float maxDistance);
    Q_INVOKABLE void smoothSkinning(int iterations);
    Q_INVOKABLE void normalizeWeights();
    Q_INVOKABLE void pruneWeights(float threshold, int maxInfluences);
    Q_INVOKABLE void paintWeights(int boneId, float x, float y, float z, float radius, float strength);
    Q_INVOKABLE void mirrorWeights(int axis);
    Q_INVOKABLE void solveTwoBoneIK(float targetX, float targetY, float targetZ);
    Q_INVOKABLE void solveCCDIK(float targetX, float targetY, float targetZ, int iterations);

    // Bone access and IK/FK control
    Q_INVOKABLE int boneCount() const;
    Q_INVOKABLE QString getBoneName(int boneIdx) const;
    Q_INVOKABLE int getBoneParentId(int boneIdx) const;
    Q_INVOKABLE QVector3D getBonePosition(int boneIdx) const;
    Q_INVOKABLE QVector3D getBoneWorldPosition(int boneIdx) const;
    Q_INVOKABLE QVector3D getBoneRotation(int boneIdx) const;
    Q_INVOKABLE QVector3D getBoneLength(int boneIdx) const;
    Q_INVOKABLE int selectedBoneIndex() const;
    Q_INVOKABLE void selectBone(int boneIdx);
    Q_INVOKABLE void setBoneIKWeight(int boneIdx, float weight);
    Q_INVOKABLE float getBoneIKWeight(int boneIdx) const;
    Q_INVOKABLE void updateBoneHierarchy();
    Q_INVOKABLE void setBonePosition(int boneIdx, float x, float y, float z);
    Q_INVOKABLE void setBoneRotation(int boneIdx, float x, float y, float z);
    Q_INVOKABLE void applyFKPose(int boneIdx, float x, float y, float z, float rx, float ry, float rz);

    Q_INVOKABLE void createMaterial(const QString& name);
    Q_INVOKABLE void setMaterialAlbedo(float r, float g, float b, float a);
    Q_INVOKABLE void setMaterialMetallic(float value);
    Q_INVOKABLE void setMaterialRoughness(float value);

    Q_INVOKABLE void setSelectedBaseColor(float r, float g, float b);
    Q_INVOKABLE void setSelectedMetallic(float value);
    Q_INVOKABLE void setSelectedRoughness(float value);
    Q_INVOKABLE void setSelectedOpacity(float value);

    Q_INVOKABLE int addTransformGroup(const QString& name = "TransformGroup");
    Q_INVOKABLE int addCamera(const QString& name = "Camera");
    Q_INVOKABLE int addLight(const QString& name = "Light");
    Q_INVOKABLE void setMaterialNormalStrength(float value);
    Q_INVOKABLE void setMaterialEmissive(float r, float g, float b);
    Q_INVOKABLE void setMaterialOpacity(float value);
    Q_INVOKABLE void createShader(const QString& vertexShader, const QString& fragmentShader);

    // Light Lister: photometric light definitions attached to Type::Light scene
    // objects. Types: 0 Directional, 1 Point, 2 Spot, 3 Area.
    Q_INVOKABLE QVariantList lightList() const;
    Q_INVOKABLE int lightCreate(int type, const QString& name = "Light");
    Q_INVOKABLE bool lightRemove(int objectId);
    Q_INVOKABLE void lightSetType(int objectId, int type);
    Q_INVOKABLE void lightSetColor(int objectId, float r, float g, float b);
    Q_INVOKABLE void lightSetIntensity(int objectId, float value);
    Q_INVOKABLE void lightSetEnabled(int objectId, bool enabled);
    Q_INVOKABLE void lightSetRange(int objectId, float value);
    Q_INVOKABLE void lightSetSpotAngle(int objectId, float angleDeg);
    Q_INVOKABLE void lightSetIesProfile(int objectId, const QString& path);
    Q_INVOKABLE void lightSetIesIntensity(int objectId, float value);
    Q_INVOKABLE void compileShader();

    // Material drag-and-drop to the viewport: pick the object under a screen
    // point (orbit camera raycast vs world AABBs) and apply a built-in preset
    // to a specific object (not just the current selection).
    Q_INVOKABLE int pickObjectAtScreen(float screenX, float screenY, float viewportW, float viewportH);
    Q_INVOKABLE void applyPresetToObject(int objectId, const QString& preset);
    Q_INVOKABLE void applyMaterialParamsToObject(int objectId,
                                                 float r, float g, float b,
                                                 float metallic, float roughness,
                                                 float opacity);
    Q_INVOKABLE void setObjectVisibility(int objectId, bool visible);

    // Subdivision cage accessors
    bool subdivCageEnabled() const { return m_subdivCageEnabled; }
    void setSubdivCageEnabled(bool enabled);
    int subdivCageLevel() const { return m_subdivCageLevel; }
    void setSubdivCageLevel(int level);

    // Quad remesh (2.3): converts a mesh to a quad-dominant mesh. Non
    // destructive: the original mesh is snapshotted and re-applied on demand.
    Q_INVOKABLE bool quadRemesh(int objectId, int level);
    Q_INVOKABLE void quadRemeshClear(int objectId);
    Q_INVOKABLE void quadRemeshClearAll();

public:
    // Raytraced viewport accessors
    bool rayTraceEnabled() const { return m_rayTraceEnabled; }
    void setRayTraceEnabled(bool enabled);
    int rayTraceFrame() const { return m_rayTraceFrameRevision; }
    QImage rayTraceFrameImage() const { return m_rayTraceFrame; }
    int rayTracePass() const { return m_rayTracePass; }
    void setRayTracePass(int pass);
    Q_INVOKABLE void rayTraceSetCamera(float ex, float ey, float ez, float tx, float ty, float tz, float fov);
    Q_INVOKABLE void rayTraceSetSize(int w, int h);
    // Mental-ray-style final render: path-traces the current scene to a PNG.
    // `pass` selects the AOV (0=Color/path-traced, otherwise the matching
    // single-bounce pass of the current render element).
    Q_INVOKABLE bool rayTraceRenderToFile(const QString& path, int width, int height, int samples, int pass = 0);

    // Bake-to-texture accessors. bakeType uses TextureBaker::BakeType:
    // 0=Diffuse, 1=Normal, 2=Roughness, 3=Metallic, 4=AO, 5=Height, 6=Emission.
    int bakeRevision() const { return m_bakeRevision; }
    QImage bakeResultImage() const { return m_bakeResultImage; }
    Q_INVOKABLE bool bakeObject(int objectId, int bakeType, const QString& path, int width, int height);
    Q_INVOKABLE bool bakePreview(int objectId, int bakeType, int width, int height);
    Q_INVOKABLE void bakeClearResult();
    Q_INVOKABLE bool packBakeChannels(int objectId, int packChannels, int width, int height);
    Q_INVOKABLE bool saveBakedTexture(const QString& path);
    Q_INVOKABLE QString bakeTypeName(int bakeType) const;

    // Animation state accessors
    qreal animationTime() const { return m_animationTime; }
    qreal animationDuration() const {
        return (m_currentAnimation >= 0 && m_currentAnimation < m_animations.size())
            ? m_animations[m_currentAnimation].duration : 0.0;
    }
    QString animationName() const {
        return (m_currentAnimation >= 0 && m_currentAnimation < m_animations.size())
            ? m_animations[m_currentAnimation].name : QString();
    }
    int animationFps() const { return m_animFps; }

    Q_INVOKABLE void addAnimation(const QString& name, float duration);
    Q_INVOKABLE QStringList animationNames() const;
    Q_INVOKABLE void setCurrentAnimationByName(const QString& name);
    Q_INVOKABLE void deleteAnimation(const QString& name);
    Q_INVOKABLE void addKeyframe(const QString& animName, float time, int boneId, float x, float y, float z, float rotX, float rotY, float rotZ);
    Q_INVOKABLE void addKeyframeForSelectedObject(const QString& animName);
    Q_INVOKABLE void generateWalkCycle(const QString& animName, double duration, double amplitude);
    // Imports a Biovision BVH motion capture file as an animation.
    Q_INVOKABLE bool importBVH(const QString& path, const QString& animName);
    Q_INVOKABLE void playAnimation(const QString& name);
    Q_INVOKABLE void stopAnimation();
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void setAnimationTime(float time);
    Q_INVOKABLE float getAnimationTime() const;
    Q_INVOKABLE bool isAnimating() const;
    Q_INVOKABLE void setAnimationLoop(bool loop);
    Q_INVOKABLE QVariantList currentAnimationKeyframes() const;

    // Animation layers (non-destructive blend layers, XSI-style).
    Q_INVOKABLE int animationAddLayer(const QString& animName, const QString& layerName);
    Q_INVOKABLE bool animationRemoveLayer(const QString& animName, int layerIndex);
    Q_INVOKABLE bool animationRenameLayer(const QString& animName, int layerIndex, const QString& newName);
    Q_INVOKABLE bool animationSetLayerEnabled(const QString& animName, int layerIndex, bool enabled);
    Q_INVOKABLE bool animationSetLayerWeight(const QString& animName, int layerIndex, float weight);
    Q_INVOKABLE int animationLayerCount(const QString& animName) const;
    Q_INVOKABLE QVariantList animationLayerList(const QString& animName) const;
    Q_INVOKABLE bool animationAddLayerKeyframe(const QString& animName, int layerIndex, float time, int boneId, float x, float y, float z, float rotX, float rotY, float rotZ);
    Q_INVOKABLE QVariantList animationLayerKeyframes(const QString& animName, int layerIndex) const;
    Q_INVOKABLE void addKeyframeForSelectedObjectToLayer(const QString& animName, int layerIndex);

    // NLA (non-linear animation): clip/source track on a master timeline.
    Q_INVOKABLE int nlaAddClip(const QString& sourceAnim);
    Q_INVOKABLE bool nlaRemoveClip(int index);
    Q_INVOKABLE bool nlaSetClipRange(int index, double start, double duration);
    Q_INVOKABLE bool nlaSetClipTimescale(int index, double timescale);
    Q_INVOKABLE bool nlaSetClipLoop(int index, bool loop);
    Q_INVOKABLE bool nlaSetClipEnabled(int index, bool enabled);
    Q_INVOKABLE bool nlaSetClipWeight(int index, float weight);
    Q_INVOKABLE QVariantList nlaClipList() const;
    Q_INVOKABLE void nlaPlay();
    Q_INVOKABLE void nlaPause();
    Q_INVOKABLE void nlaStop();
    Q_INVOKABLE void nlaSetTime(double time);
    Q_INVOKABLE double nlaTime() const { return m_nlaTime; }
    Q_INVOKABLE bool nlaPlaying() const { return m_nlaPlaying; }
    Q_INVOKABLE double nlaDuration() const;

    // Dynamics (rigid body simulation, XSI-style).
    Q_INVOKABLE bool dynAddBody(int objectId, int shapeType, float mass, bool kinematic);
    Q_INVOKABLE bool dynRemoveBody(int objectId);
    Q_INVOKABLE int dynBodyCount() const;
    Q_INVOKABLE QVariantList dynBodies() const;
    Q_INVOKABLE void dynSetGravity(double x, double y, double z);
    Q_INVOKABLE QVector3D dynGravity() const;
    Q_INVOKABLE void dynSetBodyKinematic(int objectId, bool kinematic);
    Q_INVOKABLE bool dynSetBodyMass(int objectId, float mass);
    Q_INVOKABLE void dynPlay();
    Q_INVOKABLE void dynPause();
    Q_INVOKABLE void dynStepOnce(double dt);
    Q_INVOKABLE void dynReset();
    Q_INVOKABLE bool dynRunning() const { return m_dynRunning; }

    // Cloth simulation (soft body, XSI-style cloth).
    Q_INVOKABLE bool clothAdd(int objectId, int pinMode);
    Q_INVOKABLE bool clothRemove(int objectId);
    Q_INVOKABLE int clothCount() const { return m_cloth.count(); }
    Q_INVOKABLE QVariantList clothList() const;
    Q_INVOKABLE int clothPinModeOf(int objectId) const;
    Q_INVOKABLE int clothSpringCount(int objectId) const { return m_cloth.springCount(objectId); }
    Q_INVOKABLE void clothSetGravity(double x, double y, double z);
    Q_INVOKABLE void clothSetStiffness(int objectId, float v);
    Q_INVOKABLE void clothSetDamping(int objectId, float v);
    Q_INVOKABLE void clothSetWind(int objectId, float v);
    Q_INVOKABLE void clothPlay();
    Q_INVOKABLE void clothPause();
    Q_INVOKABLE void clothReset();
    Q_INVOKABLE void clothRemoveAll();
    Q_INVOKABLE bool clothRunning() const { return m_clothRunning; }
    Q_INVOKABLE QVector3D clothGravity() const { return m_cloth.gravity(); }
    // Cloth collisions (2.5 advanced): solid mesh objects + self-collision.
    Q_INVOKABLE void clothSetCollisionObjects(const QVariantList& objectIds);
    Q_INVOKABLE QVariantList clothCollisionObjects() const;
    Q_INVOKABLE void clothSetCollision(int objectId, bool enabled);
    Q_INVOKABLE bool clothCollision(int objectId) const { return m_cloth.collisionEnabled(objectId); }
    Q_INVOKABLE void clothSetSelfCollision(int objectId, bool enabled);
    Q_INVOKABLE bool clothSelfCollision(int objectId) const { return m_cloth.selfCollision(objectId); }
    // Fabric presets (XSI-style): apply a material's physical parameters.
    Q_INVOKABLE bool clothPreset(int objectId, const QString& preset);
    Q_INVOKABLE QStringList clothPresetNames() const;
    // Procedural fabric textures (2.5): weave diffuse + normal map on a cloth mesh.
    Q_INVOKABLE bool clothApplyFabric(int objectId, const QString& fabric, double scale = 1.0);
    Q_INVOKABLE void clothRemoveFabric(int objectId);
    Q_INVOKABLE QString fabricFor(int objectId) const;
    Q_INVOKABLE QStringList fabricNames() const;
    // Hair / fur (2.5): strands grown from a mesh surface with dynamics.
    Q_INVOKABLE bool hairAdd(int objectId, int strandCount, int segments, double length);
    Q_INVOKABLE bool hairRemove(int objectId);
    Q_INVOKABLE int hairCount() const { return m_hair.count(); }
    Q_INVOKABLE QVariantList hairList() const;
    Q_INVOKABLE void hairSetLength(int objectId, double v);
    Q_INVOKABLE void hairSetStiffness(int objectId, double v);
    Q_INVOKABLE void hairSetWind(int objectId, double v);
    Q_INVOKABLE void hairPlay();
    Q_INVOKABLE void hairPause();
    Q_INVOKABLE bool hairRunning() const { return m_hairRunning; }
    Q_INVOKABLE void hairRemoveAll();

    // Keyboard shortcuts
    Q_INVOKABLE bool loadShortcuts(const QString& filePath);
    Q_INVOKABLE bool saveShortcuts(const QString& filePath);
    Q_INVOKABLE QString getShortcutKey(const QString& id) const;
    Q_INVOKABLE QString getShortcutDescription(const QString& id) const;
    Q_INVOKABLE void remapShortcut(const QString& id, const QString& newKey);
    Q_INVOKABLE void resetShortcutsToDefaults();
    Q_INVOKABLE QStringList getAllShortcutIds() const;
    Q_INVOKABLE QVariantMap getShortcutsData() const;

    Q_INVOKABLE void showMaterialEditor();
    Q_INVOKABLE void showPropertiesPanel();
    Q_INVOKABLE void showSceneGraph();

    // Material node editor (Slate-style). Node graph payloads are JSON strings
    // so QML can mirror the graph without per-node QObject wrappers.
    Q_INVOKABLE QString matNodeEditorGraph() const;
    Q_INVOKABLE QString matNodeEditorAvailableTypes() const;
    Q_INVOKABLE QString matNodeEditorCreateNode(const QString& type, double x, double y);
    Q_INVOKABLE void matNodeEditorDeleteNode(const QString& nodeId);
    Q_INVOKABLE void matNodeEditorConnect(const QString& fromNode, const QString& fromSocket,
                                          const QString& toNode, const QString& toSocket);
    Q_INVOKABLE void matNodeEditorDisconnect(const QString& fromNode, const QString& fromSocket,
                                             const QString& toNode, const QString& toSocket);
    Q_INVOKABLE void matNodeEditorMoveNode(const QString& nodeId, double x, double y);
    Q_INVOKABLE void matNodeEditorSetSocketValue(const QString& nodeId, const QString& socketId,
                                                 const QVariant& value);
    Q_INVOKABLE void matNodeEditorSetTexture(const QString& nodeId, const QString& path);
    Q_INVOKABLE void matNodeEditorClear();
    Q_INVOKABLE QString matNodeEditorGenerateShader();

    // Camera controls
    qreal camTheta() const { return m_camTheta; }
    void setCamTheta(qreal v);
    qreal camPhi() const { return m_camPhi; }
    void setCamPhi(qreal v);
    qreal camDistance() const { return m_camDistance; }
    void setCamDistance(qreal v);
    qreal camTargetX() const { return m_camTargetX; }
    void setCamTargetX(qreal v);
    qreal camTargetY() const { return m_camTargetY; }
    void setCamTargetY(qreal v);
    qreal camTargetZ() const { return m_camTargetZ; }
    void setCamTargetZ(qreal v);

    // Gizmo state for QML viewport
    QVector3D gizmoPosition() const;
    QVector3D gizmoRotation() const;
    QVector3D gizmoScale() const;
    int gizmoMode() const { return m_gizmoMode; }

    Q_INVOKABLE void setCameraView(const QString& view);
    Q_INVOKABLE void focusOnSelected();

    bool gridVisible() const { return m_gridVisible; }
    void setGridVisible(bool v);

    int viewMode() const { return m_viewMode; }
    void setViewMode(int mode);

    QString currentViewName() const { return m_currentViewName; }
    int boneVersion() const { return m_boneVersion; }
    int dragTargetObject() const { return m_dragTargetObject; }
    void setDragTargetObject(int id) {
        if (m_dragTargetObject != id) {
            m_dragTargetObject = id;
            emit dragTargetChanged();
        }
    }
    bool nurbsCvVisible() const { return m_nurbsCvVisible; }
    void setNurbsCvVisible(bool on) {
        if (m_nurbsCvVisible != on) {
            m_nurbsCvVisible = on;
            emit nurbsCvVisibleChanged();
        }
    }
    int nurbsSelectedRow() const { return m_nurbsSelectedRow; }
    void setNurbsSelectedRow(int r) {
        if (m_nurbsSelectedRow != r) {
            m_nurbsSelectedRow = r;
            emit nurbsSelectedCVChanged();
        }
    }
    int nurbsSelectedCol() const { return m_nurbsSelectedCol; }
    void setNurbsSelectedCol(int c) {
        if (m_nurbsSelectedCol != c) {
            m_nurbsSelectedCol = c;
            emit nurbsSelectedCVChanged();
        }
    }

    QString environmentHDR() const { return m_environmentHDR; }
    void setEnvironmentHDR(const QString& path);

    bool cullingEnabled() const { return m_cullingEnabled; }
    void setCullingEnabled(bool on);
    qreal cullDistance() const { return m_cullDistance; }
    void setCullDistance(qreal v);

    qreal cameraFocalLength() const { return m_cameraFocalLength; }
    void setCameraFocalLength(qreal v);
    qreal cameraSensorWidth() const { return m_cameraSensorWidth; }
    void setCameraSensorWidth(qreal v);
    qreal cameraFov() const;
    Q_INVOKABLE void matchCameraToSelection();

    int tonemappingMode() const { return m_tonemappingMode; }
    void setTonemappingMode(int mode);
    qreal tonemapExposure() const { return m_tonemapExposure; }
    void setTonemapExposure(qreal v);

    SceneObjectListModel* sceneModel() const { return m_sceneModel; }

    // Track methods
    Q_INVOKABLE void newTrack(const QString& name);
    Q_INVOKABLE bool loadTrack(const QString& path);
    Q_INVOKABLE bool saveTrack(const QString& path);
    Q_INVOKABLE bool exportTrackKN5(const QString& path);
    Q_INVOKABLE bool importGPX(const QString& path);
    Q_INVOKABLE bool importKML(const QString& path);
    Q_INVOKABLE bool importFromMap(const QString& service, double lat, double lon, int zoom);
    Q_INVOKABLE bool exportGPX(const QString& path);
    Q_INVOKABLE bool exportKML(const QString& path);
    Q_INVOKABLE bool exportAILine(const QString& path);

    Q_INVOKABLE void addTrackPoint(float x, float y, float z);
    Q_INVOKABLE void insertTrackPoint(int index, float x, float y, float z);
    Q_INVOKABLE void removeTrackPoint(int index);
    Q_INVOKABLE void smoothTrackPoints(int iterations);
    Q_INVOKABLE void closeTrackLoop();
    Q_INVOKABLE void setTrackWidth(float width);
    Q_INVOKABLE void setTrackCamber(float camber);
    Q_INVOKABLE float trackWidth() const;
    Q_INVOKABLE float trackCamber() const;

    Q_INVOKABLE void addTrackSection(int type);
    Q_INVOKABLE void removeTrackSection(int index);
    Q_INVOKABLE void setSectionSurface(int index, const QString& surface);
    Q_INVOKABLE void addSectionKerb(int index, float height, float width);

    Q_INVOKABLE void generateTrackMesh();
    Q_INVOKABLE void generateTrackEdges();
    Q_INVOKABLE void generateTerrain(float size, float maxHeight);
    Q_INVOKABLE void generateAILine();

    Q_INVOKABLE int trackPointCount() const;
    Q_INVOKABLE int trackSectionCount() const;
    Q_INVOKABLE float trackLength() const;
    Q_INVOKABLE int cornerCount() const;

    Q_INVOKABLE void addTrackCamera(const QString& name, float x, float y, float z, float targetX, float targetY, float targetZ);
    Q_INVOKABLE void removeTrackCamera(int index);
    Q_INVOKABLE void setCameraPosition(int index, float x, float y, float z);
    Q_INVOKABLE void setCameraTarget(int index, float x, float y, float z);

    Q_INVOKABLE QStringList getSupportedImportFormats() const;
    Q_INVOKABLE QStringList getSupportedExportFormats() const;

signals:
    void editorTypeChanged(const QString& type);
    void sceneChanged();
    void selectionChanged();
    void statusMessage(const QString& message);
    void errorMessage(const QString& error);
    void fileChanged(const QString& path);
    void error(const QString& message);
    void cameraChanged();
    void gridVisibleChanged();
    void viewModeChanged();
    void gizmoModeChanged();
    void gizmoTransformChanged();
    void dragTargetChanged();
    void nurbsCvVisibleChanged();
    void nurbsSelectedCVChanged();
    void rayTraceEnabledChanged();
    void rayTraceFrameChanged();
    void rayTracePassChanged();
    void lightsChanged();
    void bakeResultChanged();
    void matNodeGraphChanged();
    void clothChanged();
    void hairChanged();
    void fabricChanged();
    void animationTimeChanged();
    void playbackStateChanged();
    void animationNameChanged();
    void clipboardChanged();
    void printRequested();
    void meshCheckResult(const QString& result);
    void stlExportRequested();
    void printScaleRequested();
    void hollowRequested();
    void supportsRequested();
    void sliceRequested();
    void shapeKeysChanged();
    void lodsGenerated(int count);
    void collisionGenerated(int hullCount);
    void proportionalEditingChanged();
    void proportionalCenterChanged();
    void boneSelectionChanged();
    void skeletonChanged();
    void modifierStackChanged();
    void curveChanged();
    void seamChanged();
    void curveCvEditChanged();
    void curveSelectedCVChanged();
    void environmentHDRChanged();
    void selectionSetsChanged();
    void faceGroupsChanged();
    void cullingChanged();
    void factoryChanged();
    void renderingChanged();
    void animationLayersChanged();
    void nlaChanged();
    void nlaTimeChanged();
    void dynChanged();
    void subdivCageChanged();
    void fcurveChanged(int objectId);
    void booleanStackChanged(int objectId);
    void constraintChanged(int objectId);
    void controllerChanged(int objectId);
    void wireChanged(int objectId);
    void skinWrapChanged(int objectId);
    void iceChanged(int objectId);
    void iceParticlesUpdated(int objectId, int count);
    void iceSpheresEnabledChanged();
    void sculptLayersChanged();
    void projectionStencilChanged();

private:
    QString m_currentEditorType = "car";
    SceneGraph* m_scene = nullptr;
    core_BaseEditor* m_editor = nullptr;
    SceneObjectQml* m_selectedObject = nullptr;
    SceneObjectListModel* m_sceneModel = nullptr;
    QString m_currentFile;
    int m_gizmoMode = 0;
    bool m_snapEnabled = false;
    float m_snapIncrement = 0.1f;
    int m_dragTargetObject = -1;
    bool m_nurbsCvVisible = false;
    int m_nurbsSelectedRow = -1;
    int m_nurbsSelectedCol = -1;

    qreal m_camTheta = 45.0;
    qreal m_camPhi = 30.0;
    qreal m_camDistance = 5.0;
    qreal m_camTargetX = 0.0;
    qreal m_camTargetY = 0.0;
    qreal m_camTargetZ = 0.0;
    bool m_gridVisible = true;
    int m_viewMode = 0;
    QString m_currentViewName = "Perspective";
    QString m_environmentHDR;
    bool m_cullingEnabled = false;
    qreal m_cullDistance = 200.0;
    qreal m_cameraFocalLength = 35.0;
    qreal m_cameraSensorWidth = 36.0;
    int m_tonemappingMode = 0;       // SceneEnvironment.TonemappingNone
    qreal m_tonemapExposure = 1.0;

    // Named selection sets (set name -> object names).
    QMap<QString, QSet<QString>> m_selectionSets;

    // User-defined scene factories (factory name -> template).
    struct FactoryTemplate {
        QString type = "Mesh";            // Mesh / Camera / Light / Node
        QString objectName = "Factory";
        QColor color = QColor(200, 200, 200);
        float metallic = 0.0f;
        float roughness = 0.5f;
        float opacity = 1.0f;
        QVector3D scale = QVector3D(1, 1, 1);
        QJsonObject meshData;             // serialized MeshData for Mesh factories
    };
    QMap<QString, FactoryTemplate> m_userFactories;

    // Track data
    QVector<QVector3D> m_trackPoints;
    QVector<int> m_trackSections;
    float m_trackWidth = 12.0f;
    float m_trackCamber = 0.0f;
    double m_originLat = 45.0;
    double m_originLon = 7.0;
    struct TrackSurface {
        QString type = "asphalt";
        float grip = 1.0f;
        float roughness = 0.3f;
    };
    struct TrackKerb {
        float height = 0.1f;
        float width = 0.3f;
        QColor color = Qt::red;
    };
    QMap<int, TrackSurface> m_surfaces;
    QMap<int, TrackKerb> m_kerbs;

    // Undo/Redo
    QVector<QVariantMap> m_undoStack;
    QVector<QVariantMap> m_redoStack;
    QVariantMap m_currentState;
    CommandHistory* m_commandHistory = nullptr;
    ShortcutManager* m_shortcutManager = nullptr;

    // Clipboard
    bool m_clipboardActive = false;
    SceneObject::Type m_clipboardType = SceneObject::Type::Node;
    QString m_clipboardName;
    QVector3D m_clipboardPosition;
    QVector3D m_clipboardRotation;
    QVector3D m_clipboardScale;

    // Skeleton data
    struct Bone {
        QString name;
        int parentId;
        QVector3D position;
        QVector3D rotation;
        QVector<int> children;
        float ikWeight = 1.0f; // 0=FK, 1=full IK
        QVector3D length;      // bone offset from parent (computed)
    };
    QVector<Bone> m_bones;
    int m_selectedBone = -1;

    // Animation data
    struct Keyframe {
        float time;
        int boneId;
        QVector3D position;
        QVector3D rotation;
    };
    // Non-destructive blend layer: absolute keyframes blended over the base
    // pose with a per-layer weight (XSI-style animation layers).
    struct AnimationLayer {
        QString name;
        bool enabled = true;
        float weight = 1.0f;
        QVector<Keyframe> keyframes;
    };
    struct Animation {
        QString name;
        float duration;
        QVector<Keyframe> keyframes;
        QVector<AnimationLayer> layers;
    };
    // NLA (non-linear animation): a clip references an existing Animation
    // (source) and places it on a master timeline with its own range.
    struct NLAClip {
        QString name;
        QString sourceAnim;
        double start = 0.0;
        double duration = 1.0;
        double timescale = 1.0;
        bool loop = false;
        bool enabled = true;
        float weight = 1.0f;
    };
    // Evaluated pose (position + ZYX euler rotation) for one bone.
    struct BonePose {
        QVector3D position;
        QVector3D rotation;
    };
    static int animationIndexByName(QVector<Animation>& anims, const QString& name);
    static int animationIndexByName(const QVector<Animation>& anims, const QString& name);
    void computeAnimationPose(const Animation& anim, float time, QVector<BonePose>& out);
    void applyNLAPose();
    QVector<Animation> m_animations;
    QVector<NLAClip> m_nlaClips;
    double m_nlaTime = 0.0;
    bool m_nlaPlaying = false;
    QTimer* m_nlaTimer = nullptr;
    RigidBodySystem m_dynamics;
    QTimer* m_dynTimer = nullptr;
    bool m_dynRunning = false;
    ClothSystem m_cloth;
    QTimer* m_clothTimer = nullptr;
    bool m_clothRunning = false;
    QVector<int> m_clothColliderIds;
    void refreshClothColliders();
    QMap<int, QString> m_fabrics;           // objectId -> fabric name (procedural texture)
    QMap<int, QString> m_fabricDiffuseCache; // objectId -> generated diffuse path
    QMap<int, QString> m_fabricNormalCache;  // objectId -> generated normal path
    QString generateFabricTextures(int objectId, const QString& fabric, double scale);
    void clearFabricFor(int objectId);
    HairSystem m_hair;
    QTimer* m_hairTimer = nullptr;
    bool m_hairRunning = false;
    QMap<int, int> m_hairObjects; // surface objectId -> hair SceneObject id
    ks::physics::FluidSimulator* m_fluidSimulator = nullptr;
    void hairTick();
    void rebuildHairMesh(int surfaceObjectId);
    void dynTick();
    bool m_subdivCageEnabled = false;
    int m_subdivCageLevel = 0;
    QMap<int, QJsonObject> m_cageOrigins;
    QMap<int, QString> m_remeshOrigins; // objectId -> original mesh JSON
    void applySubdivCage();
    bool m_rayTraceEnabled = false;
    QTimer* m_rayTraceTimer = nullptr;
    QImage m_rayTraceFrame;
    int m_rayTraceFrameRevision = 0;
    int m_rayTracePass = 0;
    QImage m_bakeResultImage;
    int m_bakeRevision = 0;
    RTCamera m_rtCam;
    int m_rtWidth = 320;
    int m_rtHeight = 180;
    RayTraceRenderer m_rtRenderer;
    void rayTraceTick();
    QVector<RTTriangle> buildRTTriangles() const;
    QVector<RTLight> buildRTLights() const;
    LightSystem m_lightSystem;
    int m_currentAnimation = -1;
    float m_animationTime = 0.0f;
    bool m_isAnimating = false;
    QTimer* m_animTimer = nullptr;
    int m_animFps = 30;
    bool m_animLoop = true;
    int m_animEasing = 0;

    // F-Curve playback timer (independent of bone animations).
    QTimer* m_fcurveTimer = nullptr;
    bool m_fcurvePlaying = false;
    bool m_iceSpheresEnabled = true;
    QVector3D interpolatePosition(const QVector<Keyframe>& kfs, float time, int boneId);
    QVector3D interpolateRotation(const QVector<Keyframe>& kfs, float time, int boneId);
    float applyEasing(float t, int type) const;
    void applyPoseToBones(const Animation& anim, float time);
    int findKeyframeIndex(const QVector<Keyframe>& kfs, float time, int boneId) const;

private slots:
    void advanceAnimation();

private:
    // Shape key data
    MeshData m_shapeKeyMesh;
    bool m_shapeKeysDirty = false;

    // Shape key animation driver
    ShapeKeyAnimDriver* m_shapeKeyAnimDriver = nullptr;

    // Proportional editing state
    struct ProportionalEditState {
        bool enabled = false;
        float radius = 2.0f;
        int falloffType = 1; // 0=Smooth, 1=Linear, 2=Sharp, 3=Root, 4=Sphere, 5=Constant
        QVector3D center;
        bool hasCenter = false;
    };
    ProportionalEditState m_propEdit;

    // LOD data
    LODResult m_lodResult;
    CollisionResult m_collisionResult;
    CollisionConfig m_collisionConfig;
    float m_collisionSimplifyRatio = 0.3f;
    int m_boneVersion = 0;

    // Lattice state
    struct LatticeState {
        bool active = false;
        int uDivs = 2, vDivs = 2, wDivs = 2;
        float strength = 1.0f;
        QVector<QVector3D> controlPoints;
        QVector<QVector3D> restControlPoints;
        int selectedObject=-1;
    };
    LatticeState m_lattice;

    // Vertex weights
    struct VertexWeight {
        int boneId;
        float weight;
    };
    QVector<QVector<VertexWeight>> m_vertexWeights;

    // Material state
    struct MaterialState {
        QString name;
        QColor albedo = QColor(200, 200, 200, 255);
        float metallic = 0.0f;
        float roughness = 0.5f;
        float normalStrength = 1.0f;
        QColor emissive = QColor(0, 0, 0);
        float opacity = 1.0f;
        QString vertexShader;
        QString fragmentShader;
    };
    QVector<MaterialState> m_materials;
    int m_currentMaterial = -1;

    // Material node editor (Slate-style) state.
    class MaterialNodeEditorImpl;
    MaterialNodeEditorImpl* m_matNodeEditor = nullptr;
    void ensureMatNodeEditor();
    void emitMatNodeGraphChanged();

    void syncShapeKeyMesh();
    QString detectFormat(const QString& path) const;
    Mesh* getSelectedMesh();

    ModifierStack* modifierStackForObject(int objectId);
    void evaluateAndWriteStack(SceneObject* obj);
    QMap<int, ModifierStack*> m_modifierStacks;

    // Curve data per scene object id (scene objects of Type::Spline).
    QMap<int, CurveData> m_curves;
    QMap<int, int> m_curveContinuities;
    CurveData curveForObject(int objectId) const;
    void writeCurveMesh(SceneObject* obj, const CurveData& curve, float width, int segments);
    SceneObject* selectedSceneObject() const;
    bool m_curveCvEdit = false;
    int m_curveSelectedCv = -1;

    // Real NURBS surfaces per scene object id (created via nurbsSurface*).
    QMap<int, NURBSSurface> m_nurbsSurfaces;

    // F-Curve data per scene object id (transform channels).
    QMap<int, FCurveData> m_fcurves;
    FCurveData& fcurveForObject(int objectId);

    // UV seam edges per scene object id (normalized {minV, maxV} pairs).
    QHash<int, QSet<QPair<int, int>>> m_seamEdges;

    // Layer system (Max-style). Layer colors index into a fixed palette.
    struct LayerDef {
        QString name;
        bool visible = true;
        int color = 0;
    };
    QVector<LayerDef> m_layers;
    int m_currentLayer = 0;
    // objectId -> layer index. Objects not present default to layer 0.
    QHash<int, int> m_objectLayers;
    // objectId -> last smoothing-group assignment (per-face group id, -1 unassigned).
    QHash<int, QVector<int>> m_smoothGroups;
    // objectId -> pinned vertex indices that sculpt brushes must not move.
    QHash<int, QSet<int>> m_sculptPins;
    // Mudbox-style named/colored face groups + per-object per-face assignment.
    ks::FaceGroupSystem m_faceGroups;
    static const QStringList s_layerColorPalette;
    void layerVisibleDo(int layerIndex);

    // Non-destructive boolean stacks per scene object id.
    QMap<int, BooleanStack*> m_booleanStacks;
    QMap<int, QVector<QMetaObject::Connection>> m_booleanSubscriptions;
    // Re-entrancy guards: boolean/modifier evaluation writes the result back
    // through SceneObject::setMesh, which emits meshChanged and would otherwise
    // re-trigger the subscription slot into infinite recursion.
    QSet<int> m_booleanBusy;
    QSet<int> m_modifierBusy;
    QMap<int, QVector<QMetaObject::Connection>> m_modifierSubscriptions;
    void modifierSubscribe(int objectId);
    void modifierUnsubscribe(int objectId);
    void onSceneObjectMeshChanged(int objectId);
    MeshData booleanEvaluateStack(SceneObject* obj, const BooleanStack* stack) const;
    void booleanSubscribe(int objectId);
    void booleanUnsubscribe(int objectId);

    // Builds mesh.edges if empty (used by bridgeSelectedLoops).
    void ensureMeshEdges(MeshData& data);

    // Rigging constraints.
    ConstraintSystem m_constraintSystem;
    QTimer* m_constraintTimer = nullptr;
    bool m_constraintApplying = false;
    void constraintStartTimer();
    void constraintStopTimer();
    bool constraintEvaluateAll();

    // Procedural controllers.
    ControllerSystem m_controllerSystem;
    QTimer* m_controllerTimer = nullptr;
    bool m_controllerApplying = false;
    float m_controllerClock = 0.0f;
    void controllerStartTimer();
    void controllerStopTimer();

    // Wire Parameters.
    WireParameterSystem m_wireSystem;
    QTimer* m_wireTimer = nullptr;
    bool m_wireApplying = false;
    void wireStartTimer();
    void wireStopTimer();

    // Skin Wrap.
    SkinWrapSystem m_skinWrapSystem;
    QTimer* m_skinWrapTimer = nullptr;
    bool m_skinWrapApplying = false;
    void skinWrapStartTimer();
    void skinWrapStopTimer();

    // ICE particle systems per scene object id.
    struct ICESystemEntry {
        ICEParticleGraph graph;
        ICEParticleEvaluator* evaluator = nullptr;
        QTimer* timer = nullptr;
        bool playing = false;
        int collisionObjectId = -1; // scene object feeding triangle soup
        int emitterObjectId = -1;   // scene object feeding emitter mesh
        QVector<ICEParticleState> cache; // baked frame snapshots
        int cacheLength = 0;
    };
    QMap<int, ICESystemEntry*> m_iceSystems;
    void iceStartTimer(int objectId);
    void iceStopTimer(int objectId);
    QVector<QVector3D> iceEvaluatedPositions(int objectId);
    void updateCollisionTriangles(int objectId);
    void updateEmitterTriangles(int objectId);

    // Mudbox-style sculpt layers (per-layer vertex weights).
    SculptLayersManager* m_sculptLayers = nullptr;
    SculptLayersManager* sculptLayers();
    void initSculptLayers();

    // Projection painting (stencil from image).
    ProjectionPainter* m_projectionPainter = nullptr;
    ProjectionPainter* projectionPainter();
    void initProjectionPainter();

    // Matcap rendering state.
    bool m_matcapEnabled = false;
    QImage m_matcapImage;

    // Wireframe overlay state.
    bool m_wireframeOverlayEnabled = false;
    QVector4D m_wireframeOverlayColor = QVector4D(0.0f, 0.0f, 0.0f, 0.5f);
    float m_wireframeOverlayThickness = 1.0f;

    // Silhouette display state.
    bool m_silhouetteEnabled = false;
    QVector4D m_silhouetteColor = QVector4D(0.0f, 0.0f, 0.0f, 0.8f);
    float m_silhouetteThreshold = 60.0f;

    // Turntable auto-rotate state.
    bool m_turntableEnabled = false;
    float m_turntableSpeed = 45.0f; // degrees per second
    QVector3D m_turntableAxis = QVector3D(0, 1, 0);
    QTimer* m_turntableTimer = nullptr;
    float m_turntableAngle = 0.0f;
    void turntableTick();

    // .ks3d auxiliary metadata persistence (curves, f-curves, stacks, ICE).
    QJsonObject serializeAuxMetadata() const;
    void restoreAuxMetadata(const QJsonObject& root);
    static QJsonObject meshDataToJson(const MeshData& md);
    static MeshData meshDataFromJson(const QJsonObject& o);
    static QJsonObject curveDataToJson(const CurveData& c);
    static CurveData curveDataFromJson(const QJsonObject& o);
};

} // namespace ks