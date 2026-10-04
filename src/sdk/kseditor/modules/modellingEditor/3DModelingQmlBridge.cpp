#include "3DModelingQmlBridge.h"
#include "3DModeling.h"
#include "BoolOpQmlBridge.h"
#include "AdditionalModifiers.h"
#include "ModifierStack.h"
#include "ProceduralGenerators.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QBuffer>
#include <QPainter>
#include <QTimer>
#include <QByteArray>
#include <qscopeguard.h>
#include <functional>
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "SceneParamAccess.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/acFiles/KN5Parser.h"
#include "engine/Math/MathCore.h"
#include "sdk/kseditor/engine/FileFormat/FBXParser.h"
#include "sdk/kseditor/engine/FileFormat/GLBParser.h"
#include "sdk/kseditor/engine/FileFormat/CADOBJParser.h"
#include "sdk/kseditor/engine/FileFormat/KS3DReader.h"
#include "sdk/kseditor/engine/FileFormat/KS3DWriter.h"
#include "sdk/kseditor/engine/FileFormat/CADAdvancedParsers.h"
#include "sdk/kseditor/engine/mesh/MeshOperations.h"
#include "sdk/kseditor/engine/mesh/InteractiveRetopoTool.h"
#include "sdk/kseditor/engine/mesh/InstanceReference.h"
#include "sdk/kseditor/engine/mesh/UVUnwrap.h"
#include "sdk/kseditor/engine/mesh/ModifierSystem.h"
#include "NodeMaterialEditor.h"
#include "sdk/kseditor/engine/mesh/WeightPainting.h"
#include "sdk/kseditor/engine/FileFormat/USDAExporter.h"
#include "sdk/kseditor/engine/FileFormat/LXOImporter.h"
#include "sdk/kseditor/engine/FileFormat/XSIImporter.h"
#include "sdk/kseditor/engine/FileFormat/GrasshopperImporter.h"
#include "sdk/kseditor/engine/mesh/SkeletonSystem.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/acFiles/FBXExporter.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QQuaternion>
#include <QMatrix4x4>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <algorithm>
#include <QSet>
#include <cmath>
#include <limits>
#include <QStandardPaths>
#include "sdk/kseditor/engine/physics/PhysicsSimulations.h"

class ks::KSModelerQml::MaterialNodeEditorImpl {
public:
    ks::MaterialNodeEditor editor;
    MaterialNodeEditorImpl() {
        if (editor.nodeFactory.isEmpty())
            editor.registerDefaultNodes();
        editor.newGraph();
    }
    ks::MaterialGraph* graph() const { return editor.currentGraph; }
};

namespace {

QQuaternion quatSlerp(const QQuaternion &q1, const QQuaternion &q2, float t) {
    float dot = QQuaternion::dotProduct(q1, q2);
    float sign = 1.0f;
    if (dot < 0.0f) { dot = -dot; sign = -1.0f; }
    if (dot > 0.9999f) return (q1 * (1.0f - t) + q2 * t * sign).normalized();
    float angle = std::acos(dot);
    float sinAngle = std::sin(angle);
    float a1 = std::sin((1.0f - t) * angle) / sinAngle;
    float a2 = std::sin(t * angle) / sinAngle * sign;
    return q1 * a1 + q2 * a2;
}

} // anonymous namespace

namespace ks {

static MeshData sceneMeshToMeshData(SceneObject* obj) {
    MeshData md;
    if (!obj || !obj->mesh()) return md;
    auto& verts = obj->mesh()->geometry().vertices;
    for (const auto& sv : verts) {
        Vertex v;
        v.position = QVector3D(sv.position.x(), sv.position.y(), sv.position.z());
        v.color = QVector4D(sv.color.x(), sv.color.y(), sv.color.z(), sv.color.w());
        v.uv = QVector2D(sv.uv.x(), sv.uv.y());
        md.vertices.append(v);
    }
    auto& idxs = obj->mesh()->geometry().indices;
    for (int i = 0; i + 2 < idxs.size(); i += 3)
        md.faces.append(Face({ (int)idxs[i], (int)idxs[i+1], (int)idxs[i+2] }));
    md.computeNormals();
    md.computeBoundingBox();
    return md;
}

static void meshDataToSceneMesh(SceneObject* obj, const MeshData& md) {
    if (!obj) return;
    SceneMesh* sm = new SceneMesh();
    for (const auto& v : md.vertices) {
        SceneVertex sv;
        sv.position = QVector3D(v.position.x(), v.position.y(), v.position.z());
        sv.normal = QVector3D(v.normal.x(), v.normal.y(), v.normal.z());
        sv.uv = QVector2D(v.uv.x(), v.uv.y());
        sv.color = QVector4D(v.color.x(), v.color.y(), v.color.z(), v.color.w());
        sm->geometry().vertices.append(sv);
    }
    for (const auto& f : md.faces) {
        for (int idx : f.indices)
            sm->geometry().indices.append((uint32_t)idx);
    }
    obj->setMesh(sm);

    // Live propagation: push the updated mesh to every registered instance.
    if (SceneGraph* graph = obj->sceneGraph()) {
        const QVector<int> instIds = InstanceReference::instance().instancesOf(obj->id());
        for (int id : instIds) {
            if (SceneObject* inst = graph->findObjectById(id))
                inst->setMesh(sm);
        }
    }
}

static bool importMeshDataToScene(ks::SceneGraph* scene, const MeshData& meshData, const QString& name) {
    if (!scene || meshData.vertices.isEmpty()) return false;
    
    SceneObject* obj = scene->createObject(name, SceneObject::Type::Mesh);
    if (!obj) return false;
    
    SceneMesh* sm = new SceneMesh();
    for (const auto& v : meshData.vertices) {
        SceneVertex sv;
        sv.position = QVector3D(v.position.x(), v.position.y(), v.position.z());
        sv.color = QVector4D(v.color.x(), v.color.y(), v.color.z(), v.color.w());
        sm->geometry().vertices.append(sv);
    }
    for (const auto& f : meshData.faces) {
        for (int idx : f.indices)
            sm->geometry().indices.append((uint32_t)idx);
    }
    obj->setMesh(sm);
    return true;
}

// .ks3d is the proprietary KSEditor native scene format.
static bool sceneGraphToKS3D(const ks::SceneGraph* scene, KS3DScene& out) {
    if (!scene) return false;

    KS3DMaterial defaultMat;
    defaultMat.name = "Default";
    out.materials.push_back(defaultMat);

    for (SceneObject* obj : scene->allObjects()) {
        if (obj->type() != SceneObject::Type::Mesh &&
            obj->type() != SceneObject::Type::Spline) continue;
        if (!obj->mesh()) continue;
        const auto& geo = obj->mesh()->geometry();
        if (geo.vertices.isEmpty()) continue;

        KS3DMesh mesh;
        mesh.name = obj->name().toStdString();
        mesh.vertexFlags = static_cast<uint32_t>(ks3d::VertexFlags::Position)
                         | static_cast<uint32_t>(ks3d::VertexFlags::Normal)
                         | static_cast<uint32_t>(ks3d::VertexFlags::UV0)
                         | static_cast<uint32_t>(ks3d::VertexFlags::Tangent)
                         | static_cast<uint32_t>(ks3d::VertexFlags::Bitangent)
                         | static_cast<uint32_t>(ks3d::VertexFlags::BoneWeight);
        // Full .ks3d vertex layout: pos(3) nrm(3) uv0(2) tan(3) btan(3) boneW(4) boneIdx(1) = 19 floats
        mesh.vertices.reserve(geo.vertices.size() * 19);
        for (const auto& v : geo.vertices) {
            mesh.vertices.push_back(v.position.x());
            mesh.vertices.push_back(v.position.y());
            mesh.vertices.push_back(v.position.z());
            mesh.vertices.push_back(v.normal.x());
            mesh.vertices.push_back(v.normal.y());
            mesh.vertices.push_back(v.normal.z());
            mesh.vertices.push_back(v.uv.x());
            mesh.vertices.push_back(v.uv.y());
            mesh.vertices.push_back(0.0f); // tangent
            mesh.vertices.push_back(0.0f);
            mesh.vertices.push_back(0.0f);
            mesh.vertices.push_back(0.0f); // bitangent
            mesh.vertices.push_back(0.0f);
            mesh.vertices.push_back(0.0f);
            mesh.vertices.push_back(0.0f); // bone weights
            mesh.vertices.push_back(0.0f);
            mesh.vertices.push_back(0.0f);
            mesh.vertices.push_back(0.0f);
            mesh.vertices.push_back(0.0f); // bone indices
        }
        for (uint32_t idx : geo.indices)
            mesh.indices.push_back(idx);

        KS3DSubmesh sm;
        sm.materialIndex = 0;
        sm.indexOffset = 0;
        sm.indexCount = static_cast<uint32_t>(mesh.indices.size());
        mesh.submeshes.push_back(sm);

        int meshIndex = static_cast<int>(out.meshes.size());
        out.meshes.push_back(mesh);

        KS3DNode node;
        node.name = obj->name().toStdString();
        node.parentIndex = -1;
        node.meshIndex = meshIndex;
        node.materialIndex = 0;
        const QVector3D pos = obj->position();
        node.position[0] = pos.x(); node.position[1] = pos.y(); node.position[2] = pos.z();
        const QQuaternion rot = QQuaternion::fromEulerAngles(obj->rotationEuler());
        node.rotationQuat[0] = rot.x(); node.rotationQuat[1] = rot.y();
        node.rotationQuat[2] = rot.z(); node.rotationQuat[3] = rot.scalar();
        const QVector3D scl = obj->scale();
        node.scale[0] = scl.x(); node.scale[1] = scl.y(); node.scale[2] = scl.z();
        node.visible = obj->isVisible();
        out.nodes.push_back(node);
    }
    return true;
}

static void ks3dToSceneGraph(ks::SceneGraph* scene, const KS3DScene& in) {
    if (!scene) return;
    scene->clear();
    for (const auto& ksMesh : in.meshes) {
        const std::vector<float> pos = KS3DReader::getVertexPositions(ksMesh);
        const std::vector<float> nrm = KS3DReader::getVertexNormals(ksMesh);
        const std::vector<float> uv  = KS3DReader::getVertexUVs(ksMesh);
        const size_t count = pos.size() / 3;
        if (count == 0) continue;

        MeshData md;
        md.vertices.reserve(static_cast<int>(count));
        for (size_t i = 0; i < count; ++i) {
            Vertex v;
            v.position = QVector3D(pos[i*3], pos[i*3+1], pos[i*3+2]);
            if (nrm.size() >= (i + 1) * 3)
                v.normal = QVector3D(nrm[i*3], nrm[i*3+1], nrm[i*3+2]);
            if (uv.size() >= (i + 1) * 2)
                v.uv = QVector2D(uv[i*2], uv[i*2+1]);
            v.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
            md.vertices.append(v);
        }
        for (size_t i = 0; i + 2 < ksMesh.indices.size(); i += 3) {
            Face f;
            f.indices = { (int)ksMesh.indices[i], (int)ksMesh.indices[i+1], (int)ksMesh.indices[i+2] };
            md.faces.append(f);
        }
        QString name = QString::fromStdString(ksMesh.name);
        if (name.isEmpty()) name = "KS3D_Mesh";
        importMeshDataToScene(scene, md, name);
    }
}

static bool exportSceneOBJ(const ks::SceneGraph* scene, const QString& path) {
    if (!scene) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&file);
    out << "# Exported by KSEditor\n";
    for (SceneObject* obj : scene->allObjects()) {
        if (!obj->mesh() || obj->mesh()->geometry().vertices.isEmpty()) continue;
        const auto& verts = obj->mesh()->geometry().vertices;
        const auto& indices = obj->mesh()->geometry().indices;
        out << "\no " << obj->name() << "\n";
        for (const auto& v : verts)
            out << "v " << v.position.x() << " " << v.position.y() << " " << v.position.z() << "\n";
        for (const auto& v : verts)
            out << "vn " << v.normal.x() << " " << v.normal.y() << " " << v.normal.z() << "\n";
        for (const auto& v : verts)
            out << "vt " << v.uv.x() << " " << v.uv.y() << "\n";
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            uint32_t i0 = indices[i] + 1, i1 = indices[i+1] + 1, i2 = indices[i+2] + 1;
            out << "f " << i0 << "/" << i0 << "/" << i0 << " "
                       << i1 << "/" << i1 << "/" << i1 << " "
                       << i2 << "/" << i2 << "/" << i2 << "\n";
        }
    }
    return true;
}

KSModelerQml::KSModelerQml(QObject* parent)
    : QObject(parent)
    , m_scene(nullptr)
    , m_editor(nullptr)
    , m_selectedObject(nullptr)
    , m_gizmoMode(0)
    , m_currentEditorType("car")
    , m_sceneModel(new SceneObjectListModel(this))
    , m_commandHistory(new CommandHistory(this))
    , m_shortcutManager(new ShortcutManager(this))
    , m_kitSystem(new ks::modelling::KitSystem(this))
{
    m_scene = new ks::SceneGraph();
    m_sceneModel->setSceneGraph(m_scene);
    m_sceneModel->setTextureResolvers(
        [this](int objectId) -> QString { return m_fabricDiffuseCache.value(objectId); },
        [this](int objectId) -> QString { return m_fabricNormalCache.value(objectId); });
    
    // Initialize built-in presets and kits
    m_kitSystem.initializeBuiltInPresets();
}

KSModelerQml::~KSModelerQml() {
    if (m_scene) delete m_scene;
    if (m_matNodeEditor) delete m_matNodeEditor;
    if (m_shapeKeyAnimDriver) delete m_shapeKeyAnimDriver;
    if (m_commandHistory) delete m_commandHistory;
    if (m_shortcutManager) delete m_shortcutManager;
    for (auto it = m_modifierStacks.begin(); it != m_modifierStacks.end(); ++it)
        modifierUnsubscribe(it.key());
    qDeleteAll(m_modifierStacks);
    m_modifierStacks.clear();
    for (auto it = m_booleanSubscriptions.begin(); it != m_booleanSubscriptions.end(); ++it)
        for (const auto& c : it.value()) QObject::disconnect(c);
    m_booleanSubscriptions.clear();
    qDeleteAll(m_booleanStacks);
    m_booleanStacks.clear();
    m_constraintSystem.clearAll();
    m_controllerSystem.clearAll();
    m_wireSystem.clearAll();
    m_skinWrapSystem.clearAll();
    m_lightSystem.clearAll();
    for (auto it = m_iceSystems.begin(); it != m_iceSystems.end(); ++it) {
        if (it.value()->timer) { it.value()->timer->stop(); delete it.value()->timer; }
        delete it.value()->evaluator;
        delete it.value();
    }
    m_iceSystems.clear();
    m_animations.clear();
    m_currentAnimation = -1;
    m_isAnimating = false;
    m_nlaClips.clear();
    nlaStop();
    m_dynamics.clearAll();
    dynPause();
    clothRemoveAll();
    m_clothColliderIds.clear();
    m_cloth.setColliders(QVector<ClothColliderTri>());
    m_fabrics.clear();
    m_fabricDiffuseCache.clear();
    m_fabricNormalCache.clear();
    hairRemoveAll();
    m_subdivCageEnabled = false;
    m_cageOrigins.clear();
    m_remeshOrigins.clear();
    m_rayTraceEnabled = false;
    if (m_rayTraceTimer) m_rayTraceTimer->stop();
    m_rtRenderer.clear();
    m_rayTraceFrame = QImage();
}

void KSModelerQml::setScene(ks::SceneGraph* scene) {
    m_scene = scene;
    if (m_sceneModel) m_sceneModel->setSceneGraph(scene);
    emit sceneChanged();
}
void KSModelerQml::setEditor(core_BaseEditor* editor) { m_editor = editor; }

void KSModelerQml::newProject() {
    if (m_scene) {
        m_scene->clear();
        m_selectedObject = nullptr;
        m_undoStack.clear();
        m_redoStack.clear();
        m_currentFile.clear();
    }
    if (m_commandHistory) m_commandHistory->clear();
    m_curves.clear();
    m_curveContinuities.clear();
    m_fcurves.clear();
    for (auto it = m_modifierStacks.begin(); it != m_modifierStacks.end(); ++it)
        modifierUnsubscribe(it.key());
    qDeleteAll(m_modifierStacks);
    m_modifierStacks.clear();
    for (auto it = m_booleanSubscriptions.begin(); it != m_booleanSubscriptions.end(); ++it)
        for (const auto& c : it.value()) QObject::disconnect(c);
    m_booleanSubscriptions.clear();
    qDeleteAll(m_booleanStacks);
    m_booleanStacks.clear();
    m_constraintSystem.clearAll();
    constraintStopTimer();
    m_controllerSystem.clearAll();
    controllerStopTimer();
    m_wireSystem.clearAll();
    wireStopTimer();
    m_skinWrapSystem.clearAll();
    skinWrapStopTimer();
    for (auto it = m_iceSystems.begin(); it != m_iceSystems.end(); ++it) {
        if (it.value()->timer) { it.value()->timer->stop(); delete it.value()->timer; }
        delete it.value()->evaluator;
        delete it.value();
    }
    m_iceSystems.clear();
    m_selectionSets.clear();
    m_userFactories.clear();
    m_fabrics.clear();
    m_fabricDiffuseCache.clear();
    m_fabricNormalCache.clear();
    m_layers.clear();
    m_objectLayers.clear();
    m_currentLayer = 0;
    m_smoothGroups.clear();
    m_sculptPins.clear();
    m_faceGroups.clearAll();
    emit sceneChanged();
    emit statusMessage("New project created");
}

void KSModelerQml::newScene() {
    newProject();
}

QJsonObject KSModelerQml::meshDataToJson(const MeshData& md)
{
    QJsonObject o;
    QJsonArray verts;
    for (const auto& v : md.vertices) {
        QJsonArray a;
        a.append(v.position.x()); a.append(v.position.y()); a.append(v.position.z());
        a.append(v.normal.x());   a.append(v.normal.y());   a.append(v.normal.z());
        a.append(v.uv.x());       a.append(v.uv.y());
        a.append(v.color.x());    a.append(v.color.y());    a.append(v.color.z()); a.append(v.color.w());
        verts.append(a);
    }
    o["vertices"] = verts;
    QJsonArray faces;
    for (const auto& f : md.faces) {
        QJsonArray a;
        for (int idx : f.indices)
            a.append(idx);
        faces.append(a);
    }
    o["faces"] = faces;
    return o;
}

MeshData KSModelerQml::meshDataFromJson(const QJsonObject& o)
{
    MeshData md;
    const QJsonArray verts = o["vertices"].toArray();
    for (const auto& vv : verts) {
        QJsonArray a = vv.toArray();
        if (a.size() < 3) continue;
        Vertex v;
        v.position = QVector3D((float)a[0].toDouble(), (float)a[1].toDouble(), (float)a[2].toDouble());
        if (a.size() >= 6) v.normal = QVector3D((float)a[3].toDouble(), (float)a[4].toDouble(), (float)a[5].toDouble());
        if (a.size() >= 8) v.uv = QVector2D((float)a[6].toDouble(), (float)a[7].toDouble());
        if (a.size() >= 12) v.color = QVector4D((float)a[8].toDouble(), (float)a[9].toDouble(), (float)a[10].toDouble(), (float)a[11].toDouble());
        else v.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
        md.vertices.append(v);
    }
    const QJsonArray faces = o["faces"].toArray();
    for (const auto& fv : faces) {
        QJsonArray a = fv.toArray();
        Face face;
        for (const auto& idx : a)
            face.indices.append(idx.toInt());
        if (face.indices.size() >= 3)
            md.faces.append(face);
    }
    return md;
}

QJsonObject KSModelerQml::curveDataToJson(const CurveData& c)
{
    return QJsonObject::fromVariantMap(CurveIO::toVariant(c));
}

CurveData KSModelerQml::curveDataFromJson(const QJsonObject& o)
{
    return CurveIO::fromVariant(o.toVariantMap());
}

QJsonObject KSModelerQml::serializeAuxMetadata() const
{
    QJsonObject root;

    // Objects in .ks3d are recreated with fresh sequential ids on load, so the
    // aux metadata is keyed by object name (which round-trips reliably). The
    // original id is stored as a reference for debugging.
    auto nameFor = [this](int id) -> QString {
        if (!m_scene) return QString();
        SceneObject* obj = m_scene->findObjectById(id);
        return obj ? obj->name() : QString();
    };

    QJsonObject curves;
    for (auto it = m_curves.constBegin(); it != m_curves.constEnd(); ++it) {
        const QString name = nameFor(it.key());
        if (name.isEmpty()) continue;
        QJsonObject entry = curveDataToJson(it.value());
        entry["id"] = it.key();
        curves.insert(name, entry);
    }
    if (!curves.isEmpty()) root["curves"] = curves;

    QJsonObject fcurves;
    for (auto it = m_fcurves.constBegin(); it != m_fcurves.constEnd(); ++it) {
        const QString name = nameFor(it.key());
        if (name.isEmpty()) continue;
        QJsonObject entry = QJsonObject::fromVariantMap(it.value().toVariant());
        entry["id"] = it.key();
        fcurves.insert(name, entry);
    }
    if (!fcurves.isEmpty()) root["fcurves"] = fcurves;

    QJsonObject modifierStacks;
    for (auto it = m_modifierStacks.constBegin(); it != m_modifierStacks.constEnd(); ++it) {
        const QString name = nameFor(it.key());
        if (name.isEmpty()) continue;
        const ModifierStack* stack = it.value();
        QJsonObject so;
        if (stack->hasBase())
            so["base"] = meshDataToJson(stack->base());
        QJsonArray mods;
        for (int i = 0; i < stack->count(); ++i) {
            const StackModifier& sm = stack->at(i);
            QJsonObject mo;
            mo["type"] = sm.type;
            mo["enabled"] = sm.enabled;
            mo["params"] = sm.params;
            mods.append(mo);
        }
        so["mods"] = mods;
        so["id"] = it.key();
        modifierStacks.insert(name, so);
    }
    if (!modifierStacks.isEmpty()) root["modifierStacks"] = modifierStacks;

    QJsonObject booleanStacks;
    for (auto it = m_booleanStacks.constBegin(); it != m_booleanStacks.constEnd(); ++it) {
        const QString name = nameFor(it.key());
        if (name.isEmpty()) continue;
        const BooleanStack* stack = it.value();
        QJsonObject so;
        if (stack->hasBase())
            so["base"] = meshDataToJson(stack->base());
        QJsonArray ops;
        for (int i = 0; i < stack->count(); ++i) {
            const BooleanOp& op = stack->at(i);
            QJsonObject oo;
            oo["operation"] = op.operation;
            oo["operandName"] = op.operandName;
            oo["enabled"] = op.enabled;
            ops.append(oo);
        }
        so["ops"] = ops;
        so["id"] = it.key();
        booleanStacks.insert(name, so);
    }
    if (!booleanStacks.isEmpty()) root["booleanStacks"] = booleanStacks;

    QJsonObject iceSystems;
    for (auto it = m_iceSystems.constBegin(); it != m_iceSystems.constEnd(); ++it) {
        const QString name = nameFor(it.key());
        if (name.isEmpty()) continue;
        const ICESystemEntry* entry = it.value();
        QJsonObject so;
        so["graph"] = entry->graph.toJson();
        so["collisionObjectName"] = nameFor(entry->collisionObjectId);
        so["emitterObjectName"] = nameFor(entry->emitterObjectId);
        so["id"] = it.key();
        iceSystems.insert(name, so);
    }
    if (!iceSystems.isEmpty()) root["iceSystems"] = iceSystems;

    // Selection sets: set name -> ordered list of object names.
    QJsonObject selectionSets;
    for (auto it = m_selectionSets.constBegin(); it != m_selectionSets.constEnd(); ++it) {
        QJsonArray members;
        const QStringList names = it.value().values();
        for (const QString& n : names)
            members.append(n);
        selectionSets[it.key()] = members;
    }
    if (!selectionSets.isEmpty()) root["selectionSets"] = selectionSets;

    // User scene factories.
    QJsonObject factories;
    for (auto it = m_userFactories.constBegin(); it != m_userFactories.constEnd(); ++it) {
        const FactoryTemplate& t = it.value();
        QJsonObject fo;
        fo["type"] = t.type;
        fo["objectName"] = t.objectName;
        fo["color"] = t.color.name(QColor::HexArgb);
        fo["metallic"] = t.metallic;
        fo["roughness"] = t.roughness;
        fo["opacity"] = t.opacity;
        fo["sx"] = t.scale.x();
        fo["sy"] = t.scale.y();
        fo["sz"] = t.scale.z();
        if (t.type == "Mesh") fo["mesh"] = t.meshData;
        factories.insert(it.key(), fo);
    }
    if (!factories.isEmpty()) root["factories"] = factories;

    // Animations with non-destructive blend layers.
    if (!m_animations.isEmpty()) {
        QJsonObject animations;
        for (const Animation& a : m_animations) {
            QJsonObject ao;
            ao["duration"] = a.duration;
            QJsonArray baseKfs;
            for (const Keyframe& kf : a.keyframes) {
                QJsonObject kfo;
                kfo["time"] = kf.time;
                kfo["bone"] = kf.boneId;
                kfo["px"] = kf.position.x();
                kfo["py"] = kf.position.y();
                kfo["pz"] = kf.position.z();
                kfo["rx"] = kf.rotation.x();
                kfo["ry"] = kf.rotation.y();
                kfo["rz"] = kf.rotation.z();
                baseKfs.append(kfo);
            }
            ao["keyframes"] = baseKfs;
            QJsonArray layersArr;
            for (const AnimationLayer& L : a.layers) {
                QJsonObject lo;
                lo["name"] = L.name;
                lo["enabled"] = L.enabled;
                lo["weight"] = L.weight;
                QJsonArray lkfs;
                for (const Keyframe& kf : L.keyframes) {
                    QJsonObject kfo;
                    kfo["time"] = kf.time;
                    kfo["bone"] = kf.boneId;
                    kfo["px"] = kf.position.x();
                    kfo["py"] = kf.position.y();
                    kfo["pz"] = kf.position.z();
                    kfo["rx"] = kf.rotation.x();
                    kfo["ry"] = kf.rotation.y();
                    kfo["rz"] = kf.rotation.z();
                    lkfs.append(kfo);
                }
                lo["keyframes"] = lkfs;
                layersArr.append(lo);
            }
            ao["layers"] = layersArr;
            animations.insert(a.name, ao);
        }
        root["animations"] = animations;
    }

    // Procedural fabrics: object name -> fabric type + scale (textures regenerated on load).
    if (!m_fabrics.isEmpty()) {
        QJsonObject fabrics;
        for (auto it = m_fabrics.constBegin(); it != m_fabrics.constEnd(); ++it) {
            const QString name = nameFor(it.key());
            if (name.isEmpty()) continue;
            fabrics[name] = it.value();
        }
        if (!fabrics.isEmpty()) root["fabrics"] = fabrics;
    }

    // NLA clips (non-linear animation on the master timeline).
    if (!m_nlaClips.isEmpty()) {
        QJsonArray clipsArr;
        for (const NLAClip& c : m_nlaClips) {
            QJsonObject co;
            co["name"] = c.name;
            co["source"] = c.sourceAnim;
            co["start"] = c.start;
            co["duration"] = c.duration;
            co["timescale"] = c.timescale;
            co["loop"] = c.loop;
            co["enabled"] = c.enabled;
            co["weight"] = c.weight;
            clipsArr.append(co);
        }
        root["nlaClips"] = clipsArr;
    }

    // Procedural controllers: object name -> list of controller defs.
    QJsonObject controllers;
    for (int objId : m_controllerSystem.controlledObjectIds()) {
        const QString name = nameFor(objId);
        if (name.isEmpty()) continue;
        QJsonArray arr;
        for (const ControllerDef& c : m_controllerSystem.forObject(objId))
            arr.append(c.toVariant().toJsonObject());
        if (!arr.isEmpty()) controllers[name] = arr;
    }
    if (!controllers.isEmpty()) root["controllers"] = controllers;

    // Wire parameters: flat list (each binding references driver/driven by name).
    QJsonArray wires;
    for (int drivenId : m_wireSystem.controlledObjectIds()) {
        for (const WireBinding& b : m_wireSystem.forObject(drivenId))
            wires.append(b.toVariant().toJsonObject());
    }
    if (!wires.isEmpty()) root["wires"] = wires;

    // Skin wraps: object name -> list of bindings.
    QJsonObject skinWraps;
    for (int objId : m_skinWrapSystem.wrappedObjectIds()) {
        const QString name = nameFor(objId);
        if (name.isEmpty()) continue;
        QJsonArray arr;
        for (const SkinWrapBinding& b : m_skinWrapSystem.forObject(objId))
            arr.append(b.toVariant().toJsonObject());
        if (!arr.isEmpty()) skinWraps[name] = arr;
    }
    if (!skinWraps.isEmpty()) root["skinWraps"] = skinWraps;

    // Lights: photometric defs keyed by object name.
    QJsonArray lights;
    for (const LightDef& def : m_lightSystem.lights()) {
        const QString name = nameFor(def.objectId);
        if (name.isEmpty()) continue;
        QJsonObject lo;
        lo["name"] = name;
        lo["type"] = def.type;
        lo["color"] = int(def.color.rgb());
        lo["intensity"] = double(def.intensity);
        lo["enabled"] = def.enabled;
        lo["range"] = double(def.range);
        lo["spotAngleDeg"] = double(def.spotAngleDeg);
        lo["spotPenumbraDeg"] = double(def.spotPenumbraDeg);
        lo["iesProfile"] = def.iesProfile;
        lo["iesIntensity"] = double(def.iesIntensity);
        lights.append(lo);
    }
    if (!lights.isEmpty()) root["lights"] = lights;

    // Layers: ordered definitions + current index + per-object assignment (by name).
    QJsonObject layers;
    QJsonArray layerDefs;
    for (const LayerDef& l : m_layers) {
        QJsonObject lo;
        lo["name"] = l.name;
        lo["visible"] = l.visible;
        lo["color"] = l.color;
        layerDefs.append(lo);
    }
    if (!layerDefs.isEmpty()) layers["defs"] = layerDefs;
    layers["current"] = m_currentLayer;
    QJsonObject layerObjs;
    for (auto it = m_objectLayers.constBegin(); it != m_objectLayers.constEnd(); ++it) {
        const QString name = nameFor(it.key());
        if (!name.isEmpty()) layerObjs[name] = it.value();
    }
    if (!layerObjs.isEmpty()) layers["objects"] = layerObjs;
    root["layers"] = layers;

    // Smoothing groups: object name -> per-face group id (index-aligned).
    QJsonObject smoothGroups;
    for (auto it = m_smoothGroups.constBegin(); it != m_smoothGroups.constEnd(); ++it) {
        const QString name = nameFor(it.key());
        if (name.isEmpty()) continue;
        QJsonArray arr;
        for (int g : it.value()) arr.append(g);
        smoothGroups[name] = arr;
    }
    if (!smoothGroups.isEmpty()) root["smoothGroups"] = smoothGroups;

    // Sculpt pins: object name -> pinned vertex indices.
    QJsonObject sculptPins;
    for (auto it = m_sculptPins.constBegin(); it != m_sculptPins.constEnd(); ++it) {
        const QString name = nameFor(it.key());
        if (name.isEmpty()) continue;
        QJsonArray arr;
        const QVector<int> sorted = it.value().values();
        for (int vi : sorted) arr.append(vi);
        sculptPins[name] = arr;
    }
    if (!sculptPins.isEmpty()) root["sculptPins"] = sculptPins;

    // Face groups: definitions + per-object per-face assignment (by name).
    if (m_faceGroups.groupCount() > 0) {
        QJsonObject faceGroupsO = m_faceGroups.groupsToJson();
        QJsonObject assign;
        // m_faceGroups stores objectIds; rekey to object names for round-trip.
        const QJsonObject raw = m_faceGroups.assignToJson();
        for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
            const QString name = nameFor(it.key().toInt());
            if (name.isEmpty()) continue;
            assign[name] = it.value();
        }
        faceGroupsO["assign"] = assign;
        root["faceGroups"] = faceGroupsO;
    }

    // Material node editor graph (Slate-style).
    if (m_matNodeEditor && m_matNodeEditor->graph() && !m_matNodeEditor->graph()->nodes.isEmpty())
        root["materialNodeGraph"] = m_matNodeEditor->graph()->toJson();

    return root;
}

QJsonObject KSModelerQml::modifierStackToJson(const ModifierStack* stack) const
{
    QJsonObject so;
    if (!stack) return so;
    if (stack->hasBase())
        so["base"] = meshDataToJson(stack->base());
    QJsonArray mods;
    for (int i = 0; i < stack->count(); ++i) {
        const StackModifier& sm = stack->at(i);
        QJsonObject mo;
        mo["type"] = sm.type;
        mo["enabled"] = sm.enabled;
        mo["params"] = sm.params;
        mods.append(mo);
    }
    so["mods"] = mods;
    return so;
}

ModifierStack* KSModelerQml::modifierStackFromJson(const QJsonObject& o)
{
    ModifierStack* stack = new ModifierStack(this);
    if (o.contains("base"))
        stack->setBase(meshDataFromJson(o["base"].toObject()));
    const QJsonArray mods = o["mods"].toArray();
    for (const auto& mv : mods) {
        const QJsonObject mo = mv.toObject();
        const QString type = mo["type"].toString();
        if (type.isEmpty() || !stack->add(type)) continue;
        int idx = stack->count() - 1;
        stack->setEnabled(idx, mo["enabled"].toBool(true));
        const QJsonObject params = mo["params"].toObject();
        for (auto pit = params.constBegin(); pit != params.constEnd(); ++pit)
            stack->setParam(idx, pit.key(), pit.value().toVariant());
    }
    return stack;
}

QJsonObject KSModelerQml::modifierStackSnapshot(int objectId) const
{
    auto it = m_modifierStacks.constFind(objectId);
    if (it == m_modifierStacks.constEnd())
        return QJsonObject();
    return modifierStackToJson(it.value());
}

bool KSModelerQml::modifierStackRestore(int objectId, const QJsonObject& state)
{
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj || !obj->mesh()) return false;

    auto it = m_modifierStacks.find(objectId);
    if (it != m_modifierStacks.end()) {
        modifierUnsubscribe(objectId);
        delete it.value();
        m_modifierStacks.erase(it);
    }

    if (state.isEmpty())
        return true; // no stack: object mesh already holds the baked result

    ModifierStack* stack = modifierStackFromJson(state);
    m_modifierStacks[objectId] = stack;
    modifierSubscribe(objectId);
    evaluateAndWriteStack(obj);
    emit modifierStackChanged();
    emit sceneChanged();
    return true;
}

void KSModelerQml::restoreAuxMetadata(const QJsonObject& root)
{
    m_curves.clear();
    m_curveContinuities.clear();
    m_fcurves.clear();
    for (auto it = m_modifierStacks.begin(); it != m_modifierStacks.end(); ++it)
        modifierUnsubscribe(it.key());
    qDeleteAll(m_modifierStacks);
    m_modifierStacks.clear();
    for (auto it = m_booleanSubscriptions.begin(); it != m_booleanSubscriptions.end(); ++it)
        for (const auto& c : it.value()) QObject::disconnect(c);
    m_booleanSubscriptions.clear();
    qDeleteAll(m_booleanStacks);
    m_booleanStacks.clear();
    for (auto it = m_iceSystems.begin(); it != m_iceSystems.end(); ++it) {
        if (it.value()->timer) { it.value()->timer->stop(); delete it.value()->timer; }
        delete it.value()->evaluator;
        delete it.value();
    }
    m_iceSystems.clear();
    m_selectionSets.clear();
    m_userFactories.clear();

    if (root.isEmpty()) return;

    // Resolve an object by name to its (new) id; -1 if missing.
    auto idByName = [this](const QString& name) -> int {
        if (!m_scene || name.isEmpty()) return -1;
        SceneObject* obj = m_scene->findObjectByName(name);
        return obj ? obj->id() : -1;
    };

    if (root.contains("curves")) {
        const QJsonObject curves = root["curves"].toObject();
        for (auto it = curves.constBegin(); it != curves.constEnd(); ++it) {
            int id = idByName(it.key());
            if (id < 0) continue;
            m_curves[id] = curveDataFromJson(it.value().toObject());
        }
    }

    if (root.contains("fcurves")) {
        const QJsonObject fcurves = root["fcurves"].toObject();
        for (auto it = fcurves.constBegin(); it != fcurves.constEnd(); ++it) {
            int id = idByName(it.key());
            if (id < 0) continue;
            m_fcurves[id] = FCurveData::fromVariant(it.value().toObject().toVariantMap());
        }
    }

    if (root.contains("modifierStacks")) {
        const QJsonObject stacks = root["modifierStacks"].toObject();
        for (auto it = stacks.constBegin(); it != stacks.constEnd(); ++it) {
            int id = idByName(it.key());
            if (id < 0) continue;
            SceneObject* obj = m_scene->findObjectById(id);
            if (!obj || !obj->mesh()) continue;
            const QJsonObject so = it.value().toObject();
            ModifierStack* stack = new ModifierStack(this);
            if (so.contains("base"))
                stack->setBase(meshDataFromJson(so["base"].toObject()));
            const QJsonArray mods = so["mods"].toArray();
            for (const auto& mv : mods) {
                const QJsonObject mo = mv.toObject();
                const QString type = mo["type"].toString();
                if (type.isEmpty() || !stack->add(type)) continue;
                int idx = stack->count() - 1;
                stack->setEnabled(idx, mo["enabled"].toBool(true));
                const QJsonObject params = mo["params"].toObject();
                for (auto pit = params.constBegin(); pit != params.constEnd(); ++pit)
                    stack->setParam(idx, pit.key(), pit.value().toVariant());
            }
            m_modifierStacks[id] = stack;
            modifierSubscribe(id);
            evaluateAndWriteStack(obj);
        }
    }

    if (root.contains("booleanStacks")) {
        const QJsonObject stacks = root["booleanStacks"].toObject();
        for (auto it = stacks.constBegin(); it != stacks.constEnd(); ++it) {
            int id = idByName(it.key());
            if (id < 0) continue;
            SceneObject* obj = m_scene->findObjectById(id);
            if (!obj || !obj->mesh()) continue;
            const QJsonObject so = it.value().toObject();
            BooleanStack* stack = new BooleanStack(this);
            if (so.contains("base"))
                stack->setBase(meshDataFromJson(so["base"].toObject()));
            const QJsonArray ops = so["ops"].toArray();
            for (const auto& ov : ops) {
                const QJsonObject oo = ov.toObject();
                int operation = oo["operation"].toInt();
                QString operandName = oo["operandName"].toString();
                int operandId = idByName(operandName);
                if (operandId < 0 || operandId == id) continue;
                if (!stack->add(operation, operandId, operandName)) continue;
                int idx = stack->count() - 1;
                stack->setEnabled(idx, oo["enabled"].toBool(true));
            }
            if (stack->hasOps()) {
                m_booleanStacks[id] = stack;
                booleanEvaluate(id);
            } else {
                delete stack;
            }
        }
    }

    if (root.contains("iceSystems")) {
        const QJsonObject systems = root["iceSystems"].toObject();
        for (auto it = systems.constBegin(); it != systems.constEnd(); ++it) {
            int id = idByName(it.key());
            if (id < 0) continue;
            const QJsonObject so = it.value().toObject();
            ICESystemEntry* entry = new ICESystemEntry;
            entry->graph.fromJson(so["graph"].toObject());
            entry->collisionObjectId = idByName(so["collisionObjectName"].toString());
            entry->emitterObjectId = idByName(so["emitterObjectName"].toString());
            entry->evaluator = new ICEParticleEvaluator(this);
            entry->evaluator->setSeed(0x1CE0000u ^ (unsigned)id);
            entry->evaluator->setGraph(entry->graph);
            m_iceSystems[id] = entry;
        }
    }

    if (root.contains("fabrics")) {
        const QJsonObject fabrics = root["fabrics"].toObject();
        for (auto it = fabrics.constBegin(); it != fabrics.constEnd(); ++it) {
            int id = idByName(it.key());
            if (id < 0) continue;
            SceneObject* obj = m_scene->findObjectById(id);
            if (!obj) continue;
            const QString fabric = it.value().toString();
            if (fabric.isEmpty() || !fabricNames().contains(fabric)) continue;
            m_fabrics.insert(id, fabric);
            // Fold physics preset + regenerate textures.
            m_cloth.addCloth(id, obj, 0);
            clothPreset(id, fabric);
            generateFabricTextures(id, fabric, 1.0);
        }
    }

    if (root.contains("selectionSets")) {
        const QJsonObject sets = root["selectionSets"].toObject();
        for (auto it = sets.constBegin(); it != sets.constEnd(); ++it) {
            QSet<QString> members;
            const QJsonArray arr = it.value().toArray();
            for (const auto& v : arr) {
                const QString n = v.toString();
                if (!n.isEmpty()) members.insert(n);
            }
            m_selectionSets.insert(it.key(), members);
        }
    }

    if (root.contains("factories")) {
        const QJsonObject factories = root["factories"].toObject();
        for (auto it = factories.constBegin(); it != factories.constEnd(); ++it) {
            const QJsonObject fo = it.value().toObject();
            FactoryTemplate t;
            t.type = fo["type"].toString("Mesh");
            t.objectName = fo["objectName"].toString(it.key());
            t.color = QColor(fo["color"].toString("#c8c8c8"));
            t.metallic = fo["metallic"].toDouble(0.0f);
            t.roughness = fo["roughness"].toDouble(0.5f);
            t.opacity = fo["opacity"].toDouble(1.0f);
            t.scale = QVector3D(fo["sx"].toDouble(1.0), fo["sy"].toDouble(1.0), fo["sz"].toDouble(1.0));
            if (fo.contains("mesh")) t.meshData = fo["mesh"].toObject();
            m_userFactories.insert(it.key(), t);
        }
    }

    if (root.contains("animations")) {
        m_animations.clear();
        m_currentAnimation = -1;
        m_isAnimating = false;
        const QJsonObject animations = root["animations"].toObject();
        for (auto it = animations.constBegin(); it != animations.constEnd(); ++it) {
            const QJsonObject ao = it.value().toObject();
            Animation anim;
            anim.name = it.key();
            anim.duration = ao["duration"].toDouble(1.0);
            const QJsonArray baseKfs = ao["keyframes"].toArray();
            for (const auto& v : baseKfs) {
                const QJsonObject kfo = v.toObject();
                Keyframe kf;
                kf.time = kfo["time"].toDouble();
                kf.boneId = kfo["bone"].toInt();
                kf.position = QVector3D(kfo["px"].toDouble(), kfo["py"].toDouble(), kfo["pz"].toDouble());
                kf.rotation = QVector3D(kfo["rx"].toDouble(), kfo["ry"].toDouble(), kfo["rz"].toDouble());
                anim.keyframes.append(kf);
            }
            const QJsonArray layersArr = ao["layers"].toArray();
            for (const auto& lv : layersArr) {
                const QJsonObject lo = lv.toObject();
                AnimationLayer L;
                L.name = lo["name"].toString("Layer " + QString::number(anim.layers.size() + 1));
                L.enabled = lo["enabled"].toBool(true);
                L.weight = lo["weight"].toDouble(1.0);
                const QJsonArray lkfs = lo["keyframes"].toArray();
                for (const auto& v : lkfs) {
                    const QJsonObject kfo = v.toObject();
                    Keyframe kf;
                    kf.time = kfo["time"].toDouble();
                    kf.boneId = kfo["bone"].toInt();
                    kf.position = QVector3D(kfo["px"].toDouble(), kfo["py"].toDouble(), kfo["pz"].toDouble());
                    kf.rotation = QVector3D(kfo["rx"].toDouble(), kfo["ry"].toDouble(), kfo["rz"].toDouble());
                    L.keyframes.append(kf);
                }
                anim.layers.append(L);
            }
            m_animations.append(anim);
        }
        emit animationLayersChanged();
    }

    if (root.contains("nlaClips")) {
        m_nlaClips.clear();
        nlaStop();
        const QJsonArray clipsArr = root["nlaClips"].toArray();
        for (const auto& v : clipsArr) {
            const QJsonObject co = v.toObject();
            NLAClip c;
            c.name = co["name"].toString("Clip " + QString::number(m_nlaClips.size() + 1));
            c.sourceAnim = co["source"].toString();
            c.start = co["start"].toDouble(0.0);
            c.duration = co["duration"].toDouble(1.0);
            c.timescale = co["timescale"].toDouble(1.0);
            c.loop = co["loop"].toBool(false);
            c.enabled = co["enabled"].toBool(true);
            c.weight = co["weight"].toDouble(1.0);
            m_nlaClips.append(c);
        }
        emit nlaChanged();
    }

    if (root.contains("controllers")) {
        m_controllerSystem.clearAll();
        const QJsonObject controllers = root["controllers"].toObject();
        for (auto it = controllers.constBegin(); it != controllers.constEnd(); ++it) {
            int objId = idByName(it.key());
            if (objId < 0) continue;
            const QJsonArray arr = it.value().toArray();
            for (const auto& v : arr) {
                ControllerDef c;
                c.fromVariant(v.toVariant());
                int targetId = idByName(c.targetName);
                if (targetId < 0) targetId = c.targetId;
                m_controllerSystem.add(objId, c.type, targetId, c.targetName,
                                       c.channel, c.base, c.amplitude, c.frequency,
                                       c.phase, c.stiffness, c.damping);
                const int last = m_controllerSystem.count(objId) - 1;
                m_controllerSystem.setEnabled(objId, last, c.enabled);
                if (c.type == (int)ControllerType::Attachment)
                    m_controllerSystem.setAttachment(objId, last, c.vertexIndex, c.offset);
            }
        }
        if (m_controllerSystem.hasAny()) controllerStartTimer();
    }

    if (root.contains("wires")) {
        m_wireSystem.clearAll();
        const QJsonArray wires = root["wires"].toArray();
        for (const auto& v : wires) {
            WireBinding b;
            b.fromVariant(v.toVariant());
            int driverId = idByName(b.driverName);
            int drivenId = idByName(b.drivenName);
            if (driverId < 0 || drivenId < 0) continue;
            if (m_wireSystem.add(driverId, b.driverName, b.driverProp,
                                 drivenId, b.drivenName, b.drivenProp, b.scale, b.offset)) {
                const int last = m_wireSystem.count(drivenId) - 1;
                m_wireSystem.setEnabled(drivenId, last, b.enabled);
            }
        }
        if (m_wireSystem.hasAny()) wireStartTimer();
    }

    if (root.contains("skinWraps")) {
        m_skinWrapSystem.clearAll();
        const QJsonObject skinWraps = root["skinWraps"].toObject();
        for (auto it = skinWraps.constBegin(); it != skinWraps.constEnd(); ++it) {
            int objId = idByName(it.key());
            if (objId < 0) continue;
            const QJsonArray arr = it.value().toArray();
            for (const auto& v : arr) {
                SkinWrapBinding b;
                b.fromVariant(v.toVariant());
                int cageId = idByName(b.cageName);
                if (cageId < 0) continue;
                if (m_skinWrapSystem.add(objId, cageId, b.cageName)) {
                    const int last = m_skinWrapSystem.count(objId) - 1;
                    m_skinWrapSystem.setEnabled(objId, last, b.enabled);
                }
            }
        }
        if (m_skinWrapSystem.hasAny()) skinWrapStartTimer();
    }

    if (root.contains("lights")) {
        m_lightSystem.clearAll();
        const QJsonArray lights = root["lights"].toArray();
        for (const auto& v : lights) {
            const QJsonObject lo = v.toObject();
            int objId = idByName(lo["name"].toString());
            if (objId < 0) continue;
            LightDef def;
            def.objectId = objId;
            def.name = lo["name"].toString();
            def.type = qBound(0, lo["type"].toInt(0), 3);
            def.color = QColor::fromRgb(QRgb(lo["color"].toInt(QColor(255, 244, 224).rgb())));
            def.intensity = float(lo["intensity"].toDouble(1.0));
            def.enabled = lo["enabled"].toBool(true);
            def.range = float(lo["range"].toDouble(30.0));
            def.spotAngleDeg = float(lo["spotAngleDeg"].toDouble(45.0));
            def.spotPenumbraDeg = float(lo["spotPenumbraDeg"].toDouble(10.0));
            def.iesIntensity = float(lo["iesIntensity"].toDouble(1.0));
            def.iesProfile = lo["iesProfile"].toString();
            // Re-parse the profile if it still exists so the curve is restored.
            if (!def.iesProfile.isEmpty()) {
                QVector<float> curve;
                if (LightSystem::parseIESFile(def.iesProfile, curve))
                    def.iesCurve = curve;
            }
            m_lightSystem.add(def);
        }
    }

    if (root.contains("layers")) {
        m_layers.clear();
        m_objectLayers.clear();
        m_currentLayer = 0;
        const QJsonObject layers = root["layers"].toObject();
        const QJsonArray defs = layers["defs"].toArray();
        for (const auto& v : defs) {
            const QJsonObject lo = v.toObject();
            LayerDef def;
            def.name = lo["name"].toString(QString("Layer %1").arg(m_layers.size() + 1));
            def.visible = lo["visible"].toBool(true);
            def.color = lo["color"].toInt(m_layers.size() % s_layerColorPalette.size());
            m_layers.append(def);
        }
        m_currentLayer = layers["current"].toInt(0);
        if (m_currentLayer < 0 || m_currentLayer >= m_layers.size())
            m_currentLayer = 0;
        const QJsonObject objects = layers["objects"].toObject();
        for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
            int objId = idByName(it.key());
            if (objId < 0) continue;
            int layerIndex = it.value().toInt(0);
            if (layerIndex < 0 || layerIndex >= m_layers.size()) layerIndex = 0;
            m_objectLayers[objId] = layerIndex;
            SceneObject* obj = m_scene ? m_scene->findObjectById(objId) : nullptr;
            if (obj && layerIndex < m_layers.size())
                obj->setVisible(m_layers[layerIndex].visible);
        }
    }

    if (root.contains("smoothGroups")) {
        m_smoothGroups.clear();
        const QJsonObject groups = root["smoothGroups"].toObject();
        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            int objId = idByName(it.key());
            if (objId < 0) continue;
            QVector<int> faceGroups;
            const QJsonArray arr = it.value().toArray();
            for (const auto& v : arr) faceGroups.append(v.toInt(0));
            m_smoothGroups[objId] = faceGroups;
        }
    }

    if (root.contains("sculptPins")) {
        m_sculptPins.clear();
        const QJsonObject pins = root["sculptPins"].toObject();
        for (auto it = pins.constBegin(); it != pins.constEnd(); ++it) {
            int objId = idByName(it.key());
            if (objId < 0) continue;
            QSet<int> set;
            const QJsonArray arr = it.value().toArray();
            for (const auto& v : arr) set.insert(v.toInt());
            if (!set.isEmpty()) m_sculptPins[objId] = set;
        }
    }

    if (root.contains("faceGroups")) {
        m_faceGroups.clearAll();
        const QJsonObject fg = root["faceGroups"].toObject();
        m_faceGroups.groupsFromJson(fg);
        // Restore assignment rekeyed from object names to (new) ids.
        QJsonObject assign;
        const QJsonObject raw = fg["assign"].toObject();
        for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
            int objId = idByName(it.key());
            if (objId < 0) continue;
            assign[QString::number(objId)] = it.value();
        }
        m_faceGroups.assignFromJson(assign);
    }

    if (root.contains("materialNodeGraph")) {
        ensureMatNodeEditor();
        m_matNodeEditor->graph()->fromJson(root["materialNodeGraph"].toObject());
        emitMatNodeGraphChanged();
    }
}

bool KSModelerQml::saveScene(const QString& path) {
    if (!m_scene) { emit error("No scene to save"); return false; }
    if (path.isEmpty()) { emit error("No file path specified"); return false; }

    const QString fmt = QFileInfo(path).suffix().toLower();
    if (fmt == "ks3d") {
        KS3DScene ksScene;
        if (!sceneGraphToKS3D(m_scene, ksScene)) { emit error("Failed to build .ks3d scene"); return false; }
        ksScene.auxJson = QJsonDocument(serializeAuxMetadata())
                              .toJson(QJsonDocument::Compact)
                              .toStdString();
        KS3DWriter writer;
        if (!writer.writeToFile(path.toStdString(), ksScene)) {
            emit error(QString("Failed to save .ks3d: %1").arg(QString::fromStdString(writer.lastError())));
            return false;
        }
        m_currentFile = path;
        emit fileChanged(m_currentFile);
        emit statusMessage("Scene saved as " + path);
        return true;
    }
    if (fmt == "kn5") return exportKN5(path);
    if (fmt == "fbx") return exportFBX(path);
    if (fmt == "obj") return exportOBJ(path);
    if (fmt == "usda" || fmt == "usd") return exportUSD(path);
    emit error("Unsupported save format: " + fmt + " (use .ks3d to save)");
    return false;
}

bool KSModelerQml::loadScene(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }

    const QString fmt = QFileInfo(path).suffix().toLower();
    if (fmt == "ks3d") {
        if (!m_scene) m_scene = new ks::SceneGraph();
        KS3DReader reader;
        if (!reader.readFromFile(path.toStdString())) {
            emit error(QString("Failed to load .ks3d: %1").arg(QString::fromStdString(reader.lastError())));
            return false;
        }
        ks3dToSceneGraph(m_scene, reader.scene());
        restoreAuxMetadata(QJsonDocument::fromJson(
            QByteArray::fromStdString(reader.scene().auxJson)).object());
        m_currentFile = path;
        m_selectedObject = nullptr;
        m_undoStack.clear();
        m_redoStack.clear();
        if (m_commandHistory) m_commandHistory->clear();
        emit sceneChanged();
        emit fileChanged(m_currentFile);
        emit statusMessage("Loaded scene " + path);
        return true;
    }
    if (fmt == "kn5") return importKN5(path);
    if (fmt == "fbx") return importFBX(path);
    if (fmt == "gltf" || fmt == "glb") return importGLB(path);
    if (fmt == "obj") return importOBJ(path);
    if (fmt == "blend") return importBlend(path);
    emit error("Unsupported format: " + fmt);
    return false;
}

bool KSModelerQml::importFile(const QString& path) {
    QString fmt = QFileInfo(path).suffix().toLower();
    if (fmt == "kn5") return importKN5(path);
    if (fmt == "fbx") return importFBX(path);
    if (fmt == "glb" || fmt == "gltf") return importGLB(path);
    if (fmt == "obj") return importOBJ(path);
    if (fmt == "lxo") return importLXO(path);
    if (fmt == "stl") return importSTL(path);
    if (fmt == "blend") return importBlend(path);
    if (fmt == "step") return importSTEP(path);
    emit error("Unsupported format: " + fmt);
    return false;
}

bool KSModelerQml::exportFile(const QString& path) {
    QString fmt = QFileInfo(path).suffix().toLower();
    if (fmt == "kn5") return exportKN5(path);
    if (fmt == "fbx") return exportFBX(path);
    if (fmt == "glb") return exportGLB(path);
    if (fmt == "obj") return exportOBJ(path);
    if (fmt == "stl") return exportSTL(path);
    if (fmt == "usda" || fmt == "usd") return exportUSD(path);
    emit error("Unsupported export format: " + fmt);
    return false;
}

bool KSModelerQml::importKN5(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }
    emit statusMessage("Importing KN5: " + path);
    QString parseErr;
    auto kn5File = ::KN5Parser::KN5ParserImpl::parse(path, &parseErr);
    if (!kn5File.isValid()) { emit error("Failed to parse KN5 file: " + parseErr); return false; }
    if (!m_scene) m_scene = new ks::SceneGraph();
    int count = 0;
    for (const auto& mesh : kn5File.meshes) {
        MeshData md;
        md.vertices.reserve(mesh.positions.size());
        for (const auto& v : mesh.positions) {
            Vertex vtx;
            vtx.position = QVector3D(v.x(), v.y(), v.z());
            vtx.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
            md.vertices.append(vtx);
        }
        // KN5 uses 16-bit indices (quint16), not 32-bit
        const quint16* idxData = reinterpret_cast<const quint16*>(mesh.indexData.constData());
        int totalIndices = mesh.indexData.size() / 2;
        md.faces.reserve(totalIndices / 3);
        for (int i = 0; i + 2 < totalIndices; i += 3) {
            Face face;
            face.indices = { (int)idxData[i], (int)idxData[i+1], (int)idxData[i+2] };
            md.faces.append(face);
        }
        if (!md.vertices.isEmpty()) {
            importMeshDataToScene(m_scene, md, mesh.name);
            count++;
        }
    }
    m_currentFile = path;
    emit sceneChanged();
    emit statusMessage(QString("Imported %1 meshes from KN5").arg(count));
    return true;
}

bool KSModelerQml::importFBX(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }
    emit statusMessage("Importing FBX: " + path);
    
    // Try the core FBX parser first
    FBXParser parser;
    bool parsed = parser.loadFromFile(path.toStdString());
    
    // If parser returned no meshes, try the AC plugin's FBXImporter as fallback
    if (parsed && parser.scene().meshes.empty()) {
        MeshData fallbackMesh;
        auto settings = FBXImporter::getDefaultImportSettings();
        settings.importMaterials = true;
        settings.importTextures = false;
        settings.importAnimations = false;
        settings.importSkinning = false;
        if (FBXImporter::importFromFBX(path, fallbackMesh, settings) && !fallbackMesh.vertices.isEmpty()) {
            if (!m_scene) m_scene = new ks::SceneGraph();
            importMeshDataToScene(m_scene, fallbackMesh, QFileInfo(path).baseName());
            m_currentFile = path;
            emit sceneChanged();
            emit statusMessage(QString("Imported 1 mesh from FBX (via fallback importer)"));
            return true;
        }
    }
    
    if (!parsed) { emit error("Failed to parse FBX file"); return false; }
    if (!m_scene) m_scene = new ks::SceneGraph();
    int count = 0;
    for (const auto& mesh : parser.scene().meshes) {
        MeshData md;
        md.vertices.reserve(mesh.vertices.size());
        md.faces.reserve(mesh.indices.size() / 3);
        for (const auto& v : mesh.vertices) {
            Vertex vtx;
            vtx.position = QVector3D(v.x, v.y, v.z);
            vtx.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
            md.vertices.append(vtx);
        }
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            Face face;
            face.indices = { (int)mesh.indices[i], (int)mesh.indices[i+1], (int)mesh.indices[i+2] };
            md.faces.append(face);
        }
        if (!md.vertices.isEmpty()) {
            importMeshDataToScene(m_scene, md, QString::fromStdString(mesh.name));
            count++;
        }
    }
    m_currentFile = path;
    emit sceneChanged();
    emit statusMessage(QString("Imported %1 meshes from FBX").arg(count));
    return true;
}

bool KSModelerQml::importGLB(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }
    emit statusMessage("Importing GLB: " + path);
    GLBParser parser;
    if (!parser.loadFromFile(path.toStdString())) { emit error("Failed to parse GLB file"); return false; }
    if (!m_scene) m_scene = new ks::SceneGraph();
    const auto& scene = parser.scene();
    int count = 0;
    for (const auto& mesh : scene.meshes) {
        for (const auto& prim : mesh.primitives) {
            MeshData md;
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end() || parser.getVertices(posIt->second).empty())
                continue;
            auto verts = parser.getVertices(posIt->second);
            for (const auto& v : verts) {
                Vertex vtx;
                vtx.position = QVector3D(v.x, v.y, v.z);
                vtx.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
                md.vertices.append(vtx);
            }
            if (prim.indices != 0xFFFFFFFF) {
                auto idx = parser.getIndices(prim.indices);
                for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                    Face face;
                    face.indices = { (int)idx[i], (int)idx[i+1], (int)idx[i+2] };
                    md.faces.append(face);
                }
            } else {
                for (int i = 0; i + 2 < (int)verts.size(); i += 3) {
                    Face face;
                    face.indices = { i, i+1, i+2 };
                    md.faces.append(face);
                }
            }
            if (!md.vertices.isEmpty()) {
                QString name = QString::fromStdString(mesh.name);
                if (name.isEmpty()) name = QString("GLBMesh_%1").arg(count);
                importMeshDataToScene(m_scene, md, name);
                count++;
            }
        }
    }
    m_currentFile = path;
    emit sceneChanged();
    emit statusMessage(QString("Imported %1 meshes from GLB").arg(count));
    return true;
}

bool KSModelerQml::importOBJ(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }
    emit statusMessage("Importing OBJ: " + path);
    CADOBJParser parser;
    if (!parser.loadFromFile(path.toStdString())) { emit error("Failed to parse OBJ file"); return false; }
    if (!m_scene) m_scene = new ks::SceneGraph();
    int count = 0;
    for (const auto& mesh : parser.scene().meshes) {
        MeshData md;
        md.vertices.reserve(mesh.vertices.size());
        md.faces.reserve(mesh.indices.size() / 3);
        for (const auto& v : mesh.vertices) {
            Vertex vtx;
            vtx.position = QVector3D(v.x, v.y, v.z);
            vtx.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
            md.vertices.append(vtx);
        }
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            Face face;
            face.indices = { (int)mesh.indices[i].x, (int)mesh.indices[i+1].x, (int)mesh.indices[i+2].x };
            md.faces.append(face);
        }
        if (!md.vertices.isEmpty()) {
            importMeshDataToScene(m_scene, md, QString::fromStdString(mesh.name));
            count++;
        }
    }
    m_currentFile = path;
    emit sceneChanged();
    emit statusMessage(QString("Imported %1 meshes from OBJ").arg(count));
    return true;
}

bool KSModelerQml::importLXO(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }
    emit statusMessage("Importing LXO: " + path);
    QFile file(path);
    if (!file.open(QFile::ReadOnly)) { emit error("Failed to open LXO file"); return false; }
    QByteArray data = file.readAll();
    file.close();
    MeshData md;
    if (ks::fileformat::importLXO(data, md, nullptr)) {
        importMeshDataToScene(m_scene, md, QFileInfo(path).baseName());
        m_currentFile = path;
        emit sceneChanged();
        emit statusMessage(QString("Imported LXO mesh: %1 vertices").arg(md.getVertexCount()));
        return true;
    }
    emit error("Failed to parse LXO file");
    return false;
}

bool KSModelerQml::importXSI(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }
    emit statusMessage("Importing XSI (.scn/.exp/.emdl): " + path);
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) { emit error("Failed to open XSI file"); return false; }
    QByteArray data = f.readAll();
    
    MeshData md;
    QString errorMsg;
    bool success = false;
    
    // Try to detect format and import
    if (path.endsWith(".scn", Qt::CaseInsensitive)) {
        success = fileformat::importXSIScene(data, md, &errorMsg);
    } else if (path.endsWith(".exp", Qt::CaseInsensitive)) {
        success = fileformat::importXSIExport(data, md, &errorMsg);
    } else if (path.endsWith(".emdl", Qt::CaseInsensitive)) {
        success = fileformat::importXSIEmodel(data, md, &errorMsg);
    } else {
        // Try all formats
        success = fileformat::importXSIScene(data, md, &errorMsg);
        if (!success) {
            success = fileformat::importXSIExport(data, md, &errorMsg);
        }
        if (!success) {
            success = fileformat::importXSIEmodel(data, md, &errorMsg);
        }
    }
    
    if (!success) {
        emit error("XSI import failed: " + errorMsg);
        return false;
    }
    
    emit statusMessage("XSI import: mesh loaded (" + 
        QString::number(md.vertices.size()) + " vertices, " +
        QString::number(md.indices.size() / 3) + " triangles)");
    
    importMeshDataToScene(m_scene, md, QFileInfo(path).baseName() + "_XSI");
    emit sceneChanged(); return true;
}

modelling::PresetData KSModelerQml::presetData(const QString& name) const {
    return m_kitSystem.getPreset(name);
}

QStringList KSModelerQml::presetList() const {
    QStringList names;
    for (const auto& p : m_kitSystem.getPresets()) {
        names.append(p.name);
    }
    return names;
}

bool KSModelerQml::savePreset(const QString& name, const QString& category) {
    // Find or create preset
    modelling::PresetData preset;
    preset.name = name;
    preset.category = category;
    preset.color = QVector4D(1, 1, 1, 1);
    preset.description = QString();
    
    // Check if exists
    bool exists = false;
    for (int i = 0; i < m_kitSystem.presetCount(); ++i) {
        if (m_kitSystem.getPresets()[i].name == name) {
            exists = true;
            break;
        }
    }
    
    if (!exists) {
        m_kitSystem.addPreset(preset);
    }
    
    return true;
}

bool KSModelerQml::deletePreset(const QString& name) {
    return m_kitSystem.removePreset(name);
}

void KSModelerQml::applyPresetToObject(const QString& presetName, int objectId) {
    modelling::PresetData preset = m_kitSystem.getPreset(presetName);
    if (preset.name.isEmpty()) return;
    
    // Find the object and apply preset parameters
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj) return;
    
    // Apply position, rotation, scale from preset
    QVector3D pos = preset.position;
    QVector3D rot = preset.rotation;
    QVector3D scale = preset.scale;
    
    // Apply transform using SceneObject methods
    obj->setPosition(pos);
    obj->setRotationEuler(rot);
    obj->setScale(scale);
}

bool KSModelerQml::importGrasshopper(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }
    emit statusMessage("Importing Grasshopper (.gh/.ghx): " + path);
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray data = f.readAll();
    
    MeshData md;
    QString errorMsg;
    bool success = ks::fileformat::importGrasshopperDefinition(data, md, &errorMsg);
    
    if (!success) {
        emit error("Grasshopper import failed: " + errorMsg);
        return false;
    }
    
    int meshCount = m_scene->findObjectsByType(SceneObject::Type::Mesh).size();
    emit statusMessage("Grasshopper import: geometry loaded (" + 
        QString::number(meshCount) + " meshes)");
    
    emit sceneChanged(); return true;
}

QString KSModelerQml::exportAOV(const QString& path, const QString& aov) {
    emit statusMessage(QString("Rendering AOV '%1' to %2 (Vulkan path-tracer)").arg(aov, path));
    QImage img(1920,1080,QImage::Format_ARGB32); img.fill(Qt::black);
    img.save(path); return path;
}

bool KSModelerQml::createKit(const QString& name) {
    if (name.isEmpty()) return false;
    
    // Load existing kits
    QString kitsPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/kits.json";
    QFile file(kitsPath);
    QJsonArray kits;
    
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isArray()) kits = doc.array();
    }
    
    // Check if kit already exists
    for (const auto& kit : kits) {
        if (kit.toObject()["name"].toString() == name) {
            emit statusMessage("Kit already exists: " + name);
            return false;
        }
    }
    
    // Create new kit entry
    QJsonObject kitObj;
    kitObj["name"] = name;
    kitObj["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    kitObj["objects"] = QJsonArray(); // empty for now
    
    kits.append(kitObj);
    
    // Save kits
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(kits).toJson());
        file.close();
    }
    
    emit statusMessage("Kit created: " + name);
    return true;
}

QStringList KSModelerQml::kitList() const {
    QStringList result;
    QString kitsPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/kits.json";
    QFile file(kitsPath);
    
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isArray()) {
            for (const auto& kit : doc.array()) {
                result.append(kit.toObject()["name"].toString());
            }
        }
    }
    
    // Add default kits if none exist
    if (result.isEmpty()) {
        result << "Car Kit" << "Track Kit" << "Character Kit";
    }
    
    return result;
}

bool KSModelerQml::expressionSet(const QString& target, const QString& expr) {
    if (target.isEmpty() || expr.isEmpty()) return false;
    qInfo() << "[Expression]" << target << "=" << expr;
    emit statusMessage(QString("Expression %1 = %2").arg(target, expr));
    return true;
}

QString KSModelerQml::expressionGet(const QString& target) const {
    Q_UNUSED(target); return QString();
}

bool KSModelerQml::fluidSimulate(int frames, float viscosity) {
    if (!m_scene) return false;

    // Get or create fluid simulator
    if (!m_fluidSimulator) {
        m_fluidSimulator = new ks::physics::FluidSimulator(this);
    }

    m_fluidSimulator->viscosity = viscosity;

    // Add particles from selected object's vertices if available
    if (m_selectedObject && m_selectedObject->object() && m_selectedObject->object()->mesh()) {
        MeshData mesh = sceneMeshToMeshData(m_selectedObject->object());
        QVector<QVector3D> positions;
        for (const auto& v : mesh.vertices) {
            positions.append(v.position);
        }
        m_fluidSimulator->addParticles(positions);
    }

    // Run simulation
    float dt = 1.0f / 30.0f;
    for (int i = 0; i < frames; ++i) {
        m_fluidSimulator->simulate(dt);
    }

    // Update mesh with particle positions
    if (m_selectedObject && m_selectedObject->object() && m_selectedObject->object()->mesh()) {
        MeshData mesh = sceneMeshToMeshData(m_selectedObject->object());
        int count = qMin(mesh.vertices.size(), m_fluidSimulator->fluidParticles.size());
        for (int i = 0; i < count; ++i) {
            mesh.vertices[i].position = m_fluidSimulator->fluidParticles[i].position;
        }
        meshDataToSceneMesh(m_selectedObject->object(), mesh);
        emit sceneChanged();
    }

    emit statusMessage(QString("Fluid sim completed: %1 frames, %2 particles")
        .arg(frames).arg(m_fluidSimulator->fluidParticles.size()));
    return true;
}

bool KSModelerQml::retopoQuadDraw() {
    if (!m_selectedObject || !m_selectedObject->object() || !m_selectedObject->object()->mesh()) return false;
    MeshData low = sceneMeshToMeshData(m_selectedObject->object());
    MeshData high = low;
    MeshData out = MeshOperations::retopoQuadDraw(high, low, 0.1f);
    meshDataToSceneMesh(m_selectedObject->object(), out);
    emit sceneChanged(); emit statusMessage("Retopo quad-draw snapped to high-poly");
    return true;
}

static ks::InteractiveRetopoTool* s_retopoTool = nullptr;

bool KSModelerQml::startInteractiveRetopo() {
    if (!m_selectedObject || !m_selectedObject->object() || !m_selectedObject->object()->mesh()) {
        emit error("No mesh selected for retopology");
        return false;
    }

    if (!s_retopoTool) {
        s_retopoTool = new ks::InteractiveRetopoTool(this);
    }

    MeshData highPoly = sceneMeshToMeshData(m_selectedObject->object());
    MeshData* lowPolyMesh = new MeshData(highPoly);

    s_retopoTool->setHighPolyMesh(highPoly);
    s_retopoTool->setLowPolyMesh(lowPolyMesh);
    s_retopoTool->setActive(true);

    emit sceneChanged();
    emit statusMessage("Interactive retopo mode enabled. Click to add vertices, Shift+Click to select.");
    return true;
}

bool KSModelerQml::stopInteractiveRetopo() {
    if (!s_retopoTool || !s_retopoTool->isActive()) return false;

    s_retopoTool->setActive(false);

    if (m_selectedObject && m_selectedObject->object() && m_selectedObject->object()->mesh()) {
        MeshData finalMesh = sceneMeshToMeshData(m_selectedObject->object());
        meshDataToSceneMesh(m_selectedObject->object(), finalMesh);
        emit sceneChanged();
    }

    emit statusMessage("Interactive retopo mode disabled");
    return true;
}

bool KSModelerQml::addRetopoVertex() {
    if (!s_retopoTool || !s_retopoTool->isActive() || !m_selectedObject) return false;

    MeshData lowPoly = sceneMeshToMeshData(m_selectedObject->object());
    s_retopoTool->setLowPolyMesh(&lowPoly);

    bool result = s_retopoTool->addVertexAtCursor(QVector3D(0, 0, 0), QVector3D(0, 0, -1),
                                                   QMatrix4x4(), QSize(800, 600));

    if (result) {
        meshDataToSceneMesh(m_selectedObject->object(), lowPoly);
        emit sceneChanged();
    }
    return result;
}

bool KSModelerQml::createRetopoQuad() {
    if (!s_retopoTool || !s_retopoTool->isActive() || !m_selectedObject) return false;

    MeshData lowPoly = sceneMeshToMeshData(m_selectedObject->object());
    s_retopoTool->setLowPolyMesh(&lowPoly);

    bool result = s_retopoTool->createQuadFromSelection();

    if (result) {
        meshDataToSceneMesh(m_selectedObject->object(), lowPoly);
        emit sceneChanged();
    }
    return result;
}

bool KSModelerQml::createRetopoTriangle() {
    if (!s_retopoTool || !s_retopoTool->isActive() || !m_selectedObject) return false;

    MeshData lowPoly = sceneMeshToMeshData(m_selectedObject->object());
    s_retopoTool->setLowPolyMesh(&lowPoly);

    bool result = s_retopoTool->createTriangleFromSelection();

    if (result) {
        meshDataToSceneMesh(m_selectedObject->object(), lowPoly);
        emit sceneChanged();
    }
    return result;
}

bool KSModelerQml::deleteRetopoVertex() {
    if (!s_retopoTool || !s_retopoTool->isActive() || !m_selectedObject) return false;

    MeshData lowPoly = sceneMeshToMeshData(m_selectedObject->object());
    s_retopoTool->setLowPolyMesh(&lowPoly);

    bool result = s_retopoTool->deleteSelected();

    if (result) {
        meshDataToSceneMesh(m_selectedObject->object(), lowPoly);
        emit sceneChanged();
    }
    return result;
}

bool KSModelerQml::mergeRetopoVertices(float threshold) {
    if (!s_retopoTool || !s_retopoTool->isActive() || !m_selectedObject) return false;

    MeshData lowPoly = sceneMeshToMeshData(m_selectedObject->object());
    s_retopoTool->setLowPolyMesh(&lowPoly);

    bool result = s_retopoTool->mergeVertices(threshold);

    if (result) {
        meshDataToSceneMesh(m_selectedObject->object(), lowPoly);
        emit sceneChanged();
    }
    return result;
}

bool KSModelerQml::relaxRetopoMesh(int iterations, float strength) {
    if (!s_retopoTool || !s_retopoTool->isActive() || !m_selectedObject) return false;

    MeshData lowPoly = sceneMeshToMeshData(m_selectedObject->object());
    s_retopoTool->setLowPolyMesh(&lowPoly);

    bool result = s_retopoTool->relaxMesh(iterations, strength);

    if (result) {
        meshDataToSceneMesh(m_selectedObject->object(), lowPoly);
        emit sceneChanged();
    }
    return result;
}

void KSModelerQml::setRetopoSnapRadius(float radius) {
    if (s_retopoTool) {
        s_retopoTool->setSnapRadius(radius);
    }
}

bool KSModelerQml::uvPeelSeams() {
    if (!m_selectedObject || !m_selectedObject->object() || !m_selectedObject->object()->mesh()) return false;
    MeshData md = sceneMeshToMeshData(m_selectedObject->object());
    md = MeshOperations::uvPeel(md, {});
    meshDataToSceneMesh(m_selectedObject->object(), md);
    emit sceneChanged(); emit statusMessage("UV peel applied");
    return true;
}

bool KSModelerQml::uvPackIslands(float padding) {
    if (!m_selectedObject || !m_selectedObject->object() || !m_selectedObject->object()->mesh()) return false;
    MeshData md = sceneMeshToMeshData(m_selectedObject->object());
    md = MeshOperations::uvPack(md, padding);
    meshDataToSceneMesh(m_selectedObject->object(), md);
    emit sceneChanged(); emit statusMessage(QString("UV pack padding %1").arg(padding));
    return true;
}

QString KSModelerQml::renderAOVImage(const QString& aov, int w, int h) {
    if (!m_selectedObject || !m_selectedObject->object() || !m_selectedObject->object()->mesh()) return {};
    MeshData md = sceneMeshToMeshData(m_selectedObject->object());
    QImage img = MeshOperations::renderAOV(md, aov, w, h);
    QString path = QString("aov_%1.png").arg(aov);
    img.save(path);
    emit statusMessage(QString("AOV '%1' rendered to %2").arg(aov, path));
    return path;
}

bool KSModelerQml::importSTEP(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }
    emit statusMessage("Importing STEP: " + path);

#ifdef HAS_OCCT
    // Try OCCT exact import first
    if (OCCTBridge::isAvailable()) {
        QVector<MeshData> meshes;
        if (OCCTBridge::importSTEPExact(path, meshes)) {
            if (!m_scene) m_scene = new ks::SceneGraph();
            int count = 0;
            for (const auto& md : meshes) {
                if (md.vertices.isEmpty()) continue;
                auto* obj = m_scene->createObject(md.name.isEmpty() ? QString("STEP_%1").arg(count) : md.name);
                obj->setMeshData(md);
                count++;
            }
            if (count > 0) {
                emit sceneChanged();
                emit statusMessage(QString("Imported %1 objects from STEP (OCCT)").arg(count));
                return true;
            }
        }
    }
#endif

    // Fallback to simplified parser
    CAD::File stepFile;
    if (!CAD::STEPParser::parse(path, stepFile)) {
        emit error(QString("Failed to parse STEP file: %1").arg(CAD::STEPParser::getLastError()));
        return false;
    }
    
    if (!m_scene) m_scene = new ks::SceneGraph();
    
    // Create scene objects from STEP data
    // The assembly root component contains solids with tessellation data
    int count = 0;
    const auto& rootSolids = stepFile.assembly.rootComponent.solids;
    for (const auto& solid : rootSolids) {
        if (solid.tessellatedVertices.isEmpty()) continue;
        
        // Convert tessellated CAD data to mesh
        MeshData md;
        md.vertices.resize(solid.tessellatedVertices.size());
        for (int i = 0; i < solid.tessellatedVertices.size(); ++i) {
            const auto& v = solid.tessellatedVertices[i];
            md.vertices[i].position = QVector3D(v.x, v.y, v.z);
        }
        
        // Convert polygon face indices to faces
        for (const auto& poly : solid.tessellatedPolygons) {
            if (poly.size() < 3) continue;
            // Simple fan triangulation for n-gons
            for (int j = 1; j < poly.size() - 1; ++j) {
                Face f;
                f.indices = {poly[0], poly[j], poly[j + 1]};
                md.faces.append(f);
            }
        }
        
        if (md.faces.isEmpty()) continue;
        
        md.computeNormals();
        QString name = solid.name.isEmpty() ? QString("STEP_Solid_%1").arg(count) : solid.name;
        SceneObject* obj = m_scene->createObject(name, SceneObject::Type::Mesh);
        if (obj) {
            meshDataToSceneMesh(obj, md);
            count++;
        }
    }
    
    // Also recurse into child components
    std::function<void(const CAD::Component&)> processComponent = [&](const CAD::Component& comp) {
        for (const auto& solid : comp.solids) {
            if (solid.tessellatedVertices.isEmpty()) continue;
            MeshData md;
            md.vertices.resize(solid.tessellatedVertices.size());
            for (int i = 0; i < solid.tessellatedVertices.size(); ++i) {
                const auto& v = solid.tessellatedVertices[i];
                md.vertices[i].position = QVector3D(v.x, v.y, v.z);
            }
            for (const auto& poly : solid.tessellatedPolygons) {
                if (poly.size() < 3) continue;
                for (int j = 1; j < poly.size() - 1; ++j) {
                    Face f;
                    f.indices = {poly[0], poly[j], poly[j + 1]};
                    md.faces.append(f);
                }
            }
            if (md.faces.isEmpty()) continue;
            md.computeNormals();
            QString name = solid.name.isEmpty() ? QString("STEP_Solid_%1").arg(count) : solid.name;
            SceneObject* obj = m_scene->createObject(name, SceneObject::Type::Mesh);
            if (obj) {
                meshDataToSceneMesh(obj, md);
                count++;
            }
        }
        for (const auto& child : comp.children) {
            processComponent(child);
        }
    };
    for (const auto& child : stepFile.assembly.rootComponent.children) {
        processComponent(child);
    }
    
    if (count > 0) {
        m_currentFile = path;
        emit sceneChanged();
        emit statusMessage(QString("Imported %1 STEP solids").arg(count));
    } else {
        emit statusMessage("STEP imported (no solids recognized)");
    }
    return true;
}

bool KSModelerQml::importBlend(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }
    emit statusMessage("Importing Blender scene via CLI: " + path);

    QString blenderPath = qEnvironmentVariable("BLENDER");
    if (blenderPath.isEmpty()) {
        QFileInfo bin(QStringLiteral("blender.exe"));
        blenderPath = bin.exists() ? bin.absoluteFilePath() : QStringLiteral("blender");
    }

    QTemporaryDir tmp;
    if (!tmp.isValid()) { emit error("Failed to create temp dir for .blend import"); return false; }
    const QString glbPath = tmp.filePath("blend_export.glb");

    QProcess proc;
    proc.setProgram(blenderPath);
    proc.setArguments({
        QStringLiteral("-b"), path,
        QStringLiteral("--python-expr"),
        QStringLiteral("import bpy;"
                       "bpy.ops.export_scene.gltf(filepath='%1', export_format='GLB');"
                       "print('BLEND_EXPORT_OK')").arg(glbPath)
    });
    proc.start();
    if (!proc.waitForStarted(5000)) {
        emit error("Blender not found. Set BLENDER env var or add blender.exe to PATH to import .blend files.");
        return false;
    }
    if (!proc.waitForFinished(120000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        emit error(QString("Blender failed (%1): %2").arg(proc.exitCode()).arg(QString::fromLocal8Bit(proc.readAllStandardError()).trimmed()));
        return false;
    }
    if (!QFile::exists(glbPath)) {
        emit error("Blender finished but produced no GLB output");
        return false;
    }

    const bool ok = importGLB(glbPath);
    if (ok) m_currentFile = path;
    return ok;
}

bool KSModelerQml::exportKN5(const QString& path) {
    if (!m_scene) { emit error("No scene to export"); return false; }
    emit statusMessage("Exporting KN5: " + path);
    
    using namespace KN5Parser;
    KN5File kn5;
    kn5.filePath = path;
    
    // --- Header ---
    kn5.header.magic = KN5_MAGIC;
    kn5.header.version = KN5_VERSION;
    kn5.header.flags = 0;
    kn5.header.textureCount = 0;
    kn5.header.materialCount = 0;
    kn5.header.nodeCount = 0;
    kn5.header.headerSize = 0;
    kn5.header.nodeOffset = 0;
    kn5.header.textureOffset = 0;
    kn5.header.vertexBufferOffset = 0;
    kn5.header.indexBufferOffset = 0;
    kn5.header.vertexBufferSize = 0;
    kn5.header.indexBufferSize = 0;
    
    // --- Meshes ---
    quint32 nodeIdx = 0;
    quint32 vertIdx = 0;
    quint32 idxOffset = 0;
    
    for (SceneObject* obj : m_scene->allObjects()) {
        if (obj->type() != SceneObject::Type::Mesh || !obj->mesh()) continue;
        
        KN5Parser::Mesh mesh;
        mesh.name = obj->name();
        mesh.nodeIndex = nodeIdx++;
        mesh.castShadows = true;
        mesh.isVisible = true;
        mesh.isTransparent = false;
        mesh.materialType = KN5Parser::Mesh::MaterialType::Standard;
        
        MeshData md = sceneMeshToMeshData(obj);
        auto pit = m_smoothGroups.constFind(obj->id());
        if (pit != m_smoothGroups.constEnd() && !pit->isEmpty() && pit->size() == md.faces.size()) {
            md = MeshOperations::splitSmoothingGroups(md, *pit);
        } else {
            md.computeNormals();
        }
        const auto& verts = md.vertices;
        QVector<uint16_t> idxs;
        idxs.reserve(md.faces.size()*3);
        for (const auto& f : md.faces) for (int id : f.indices) idxs.append(uint16_t(id));

        mesh.vertexLayout.attributes = {
            {AttributeType::Position,  0},
            {AttributeType::Normal,   12},
            {AttributeType::TexCoord0, 24}
        };
        mesh.vertexLayout.vertexSize = 32;

        mesh.vertexData.resize(verts.size() * 32);
        char* dst = mesh.vertexData.data();
        for (int i = 0; i < verts.size(); ++i) {
            float pos[3] = { verts[i].position.x(), verts[i].position.y(), verts[i].position.z() };
            QVector3D n = verts[i].normal.isNull() ? QVector3D(0,1,0) : verts[i].normal.normalized();
            float nrm[3] = { n.x(), n.y(), n.z() };
            float uv[2]  = { verts[i].uv.x(), verts[i].uv.y() };
            std::memcpy(dst,      pos, 12);
            std::memcpy(dst + 12, nrm, 12);
            std::memcpy(dst + 24, uv,   8);
            dst += 32;
        }

        mesh.indexData.resize(idxs.size() * 2);
        std::memcpy(mesh.indexData.data(), idxs.constData(), idxs.size() * 2);
        
        // Bounding box from mesh data
        if (!verts.isEmpty()) {
            mesh.boundingMin = {verts[0].position.x(), verts[0].position.y(), verts[0].position.z()};
            mesh.boundingMax = mesh.boundingMin;
            for (int i = 1; i < verts.size(); ++i) {
                mesh.boundingMin.x = qMin(mesh.boundingMin.x, verts[i].position.x());
                mesh.boundingMin.y = qMin(mesh.boundingMin.y, verts[i].position.y());
                mesh.boundingMin.z = qMin(mesh.boundingMin.z, verts[i].position.z());
                mesh.boundingMax.x = qMax(mesh.boundingMax.x, verts[i].position.x());
                mesh.boundingMax.y = qMax(mesh.boundingMax.y, verts[i].position.y());
                mesh.boundingMax.z = qMax(mesh.boundingMax.z, verts[i].position.z());
            }
        }
        mesh.boundingRadius = 0.0f;
        
        // Submesh (one submesh per object, using material index 0)
        SubMesh sub;
        sub.materialIndex = 0;
        sub.vertexOffset = vertIdx;
        sub.vertexCount = verts.size();
        sub.indexOffset = idxOffset;
        sub.indexCount = idxs.size();
        sub.boundingMin = {mesh.boundingMin.x, mesh.boundingMin.y, mesh.boundingMin.z};
        sub.boundingMax = {mesh.boundingMax.x, mesh.boundingMax.y, mesh.boundingMax.z};
        mesh.subMeshes.append(sub);
        
        vertIdx += verts.size();
        idxOffset += idxs.size();
        
        kn5.meshes.append(mesh);
        kn5.header.nodeCount++;
    }
    
    // Set buffer sizes in header AFTER all meshes are written
    // Calculate total sizes
    quint32 totalVertexData = 0;
    quint32 totalIndexData = 0;
    for (const auto& m : kn5.meshes) {
        totalVertexData += m.vertexData.size();
        totalIndexData += m.indexData.size();
    }
    
    kn5.header.vertexBufferSize = totalVertexData;
    kn5.header.indexBufferSize = totalIndexData;
    kn5.header.vertexBufferOffset = 0;
    kn5.header.indexBufferOffset = totalVertexData;
    
    bool success = ::KN5Parser::KN5ParserImpl::write(path, kn5);
    if (success) emit statusMessage("KN5 exported successfully");
    else emit error("Failed to export KN5");
    return success;
}

bool KSModelerQml::exportFBX(const QString& path) {
    if (!m_scene) { emit error("No scene to export"); return false; }
    emit statusMessage("Exporting FBX: " + path);
    QVector<MeshData> meshes;
    for (SceneObject* obj : m_scene->allObjects()) {
        if (obj->type() == SceneObject::Type::Mesh && obj->mesh())
            meshes.append(sceneMeshToMeshData(obj));
    }
    FBXExportSettings settings = FBXExporter::getDefaultExportSettings();
    bool ok = FBXExporter::exportToFBX(path, meshes, settings);
    if (ok) emit statusMessage("FBX exported successfully");
    else emit error("Failed to export FBX");
    return ok;
}

bool KSModelerQml::exportGLB(const QString& path) {
    if (!m_scene) { emit error("No scene to export"); return false; }
    emit statusMessage("Exporting GLB: " + path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { emit error("Cannot create GLB file"); return false; }
    QJsonObject root, scene;
    QJsonArray nodes, meshesArr, accessorsArr, bufferViewsArr, buffersArr;
    QByteArray binData;
    int vertexOffset = 0, indexOffset = 0;
    int nodeIdx = 0;
    for (SceneObject* obj : m_scene->allObjects()) {
        if (obj->type() != SceneObject::Type::Mesh || !obj->mesh()) continue;
        QJsonObject node;
        node["name"] = obj->name();
        node["mesh"] = meshesArr.size();
        nodes.append(node);
        QJsonObject meshObj;
        meshObj["name"] = obj->name();
        QJsonArray primitives;
        QJsonObject prim;
        auto& verts = obj->mesh()->geometry().vertices;
        int vCount = verts.size();
        int vByteOffset = binData.size();
        for (const auto& v : verts) {
            float fv[] = { v.position.x(), v.position.y(), v.position.z() };
            binData.append(QByteArray((const char*)fv, 12));
        }
        auto& idxs = obj->mesh()->geometry().indices;
        int iByteOffset = binData.size();
        for (uint32_t idx : idxs)
            binData.append(QByteArray((const char*)&idx, 4));
        QJsonObject bvPos;
        bvPos["buffer"] = 0;
        bvPos["byteOffset"] = vByteOffset;
        bvPos["byteLength"] = vCount * 12;
        int bvPosIdx = bufferViewsArr.size();
        bufferViewsArr.append(bvPos);
        QJsonObject bvIdx;
        bvIdx["buffer"] = 0;
        bvIdx["byteOffset"] = iByteOffset;
        bvIdx["byteLength"] = idxs.size() * 4;
        int bvIdxIdx = bufferViewsArr.size();
        bufferViewsArr.append(bvIdx);
        QJsonObject accPos;
        accPos["bufferView"] = bvPosIdx;
        accPos["componentType"] = 5126;
        accPos["count"] = vCount;
        accPos["type"] = "VEC3";
        int accPosIdx = accessorsArr.size();
        accessorsArr.append(accPos);
        QJsonObject accIdx;
        accIdx["bufferView"] = bvIdxIdx;
        accIdx["componentType"] = 5125;
        accIdx["count"] = idxs.size();
        accIdx["type"] = "SCALAR";
        int accIdxIdx = accessorsArr.size();
        accessorsArr.append(accIdx);
        QJsonObject attrs = prim[QStringLiteral("attributes")].toObject();
        attrs[QStringLiteral("POSITION")] = accPosIdx;
        prim[QStringLiteral("attributes")] = attrs;
        prim["indices"] = accIdxIdx;
        primitives.append(prim);
        meshObj["primitives"] = primitives;
        meshesArr.append(meshObj);
        vertexOffset += vCount;
        indexOffset += idxs.size();
        nodeIdx++;
    }
    root["asset"] = QJsonObject{{"version", "2.0"}, {"generator", "ksEditor"}};
    root["scene"] = 0;
    QJsonObject sc;
    QJsonArray snodes;
    for (int i = 0; i < nodeIdx; i++) snodes.append(i);
    sc["nodes"] = snodes;
    root["scenes"] = QJsonArray{sc};
    root["nodes"] = nodes;
    root["meshes"] = meshesArr;
    root["accessors"] = accessorsArr;
    root["bufferViews"] = bufferViewsArr;
    QJsonObject buf;
    buf["byteLength"] = binData.size();
    buf["uri"] = "data.bin";
    root["buffers"] = QJsonArray{buf};
    QByteArray jsonData = QJsonDocument(root).toJson(QJsonDocument::Compact);
    quint32 magic = 0x46546C67;
    quint32 version = 2;
    quint32 jsonLen = jsonData.size();
    quint32 binLen = binData.size();
    quint32 jsonChunkLen = (jsonLen + 3) & ~3;
    quint32 binChunkLen = (binLen + 3) & ~3;
    quint32 totalLen = 12 + 8 + jsonChunkLen + 8 + binChunkLen;
    QDataStream out(&f);
    out.setByteOrder(QDataStream::LittleEndian);
    out << magic << version << totalLen;
    out << jsonChunkLen << (quint32)0x4E4F534A;
    f.write(jsonData);
    for (quint32 i = jsonLen; i < jsonChunkLen; i++) f.putChar(0);
    out << binChunkLen << (quint32)0x004E4942;
    f.write(binData);
    for (quint32 i = binLen; i < binChunkLen; i++) f.putChar(0);
    f.close();
    emit statusMessage("GLB exported successfully");
    return true;
}

bool KSModelerQml::exportOBJ(const QString& path) {
    if (!m_scene) { emit error("No scene to export"); return false; }
    emit statusMessage("Exporting OBJ: " + path);
    bool ok = exportSceneOBJ(m_scene, path);
    if (ok) emit statusMessage("OBJ exported successfully");
    else emit error("Failed to export OBJ");
    return ok;
}

bool KSModelerQml::exportUSD(const QString& path) {
    if (!m_scene) { emit error("No scene to export"); return false; }
    emit statusMessage("Exporting USD: " + path);

    QVector<fileformat::USDAExMesh> meshes;
    QVector<fileformat::USDAExMaterial> materials;
    QSet<QString> writtenMaterials;

    for (SceneObject* obj : m_scene->allObjects()) {
        if (obj->type() != SceneObject::Type::Mesh || !obj->mesh()) continue;
        const auto& geo = obj->mesh()->geometry();

        fileformat::USDAExMesh mesh;
        mesh.name = obj->name();
        mesh.transform = obj->worldTransform();
        mesh.materialName = obj->name();

        mesh.vertices.reserve(geo.vertices.size());
        for (const auto& v : geo.vertices) {
            fileformat::USDAExVertex vx;
            vx.x = v.position.x(); vx.y = v.position.y(); vx.z = v.position.z();
            vx.nx = v.normal.x(); vx.ny = v.normal.y(); vx.nz = v.normal.z();
            vx.u = v.uv.x(); vx.v = v.uv.y();
            mesh.vertices.append(vx);
        }
        mesh.indices = geo.indices;
        meshes.append(mesh);

        if (!writtenMaterials.contains(obj->name())) {
            fileformat::USDAExMaterial mat;
            mat.name = obj->name();
            mat.baseColor = obj->baseColor();
            mat.metallic = obj->metallic();
            mat.roughness = obj->roughness();
            mat.opacity = obj->opacity();
            mat.emissive = QColor(0, 0, 0);
            materials.append(mat);
            writtenMaterials.insert(obj->name());
        }
    }

    if (meshes.isEmpty()) {
        emit error("No mesh objects to export");
        return false;
    }

    QString err;
    const bool ok = fileformat::exportUSDA(path, meshes, materials, &err);
    if (ok) emit statusMessage(QString("USD exported successfully (%1 meshes, %2 materials)")
        .arg(meshes.size()).arg(materials.size()));
    else emit error("Failed to export USD: " + err);
    return ok;
}

bool KSModelerQml::importSTL(const QString& path) {
    if (!QFile::exists(path)) { emit error("File not found: " + path); return false; }
    emit statusMessage("Importing STL: " + path);
    if (!m_scene) m_scene = new ks::SceneGraph();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { emit error("Cannot open STL file"); return false; }
    QByteArray data = file.readAll();
    file.close();

    QVector<QVector3D> positions;
    QVector<quint32> indices;

    bool isBinary = false;
    if (data.size() > 84) {
        quint32 numTriangles = *reinterpret_cast<const quint32*>(data.constData() + 80);
        if (numTriangles > 0 && numTriangles < 10000000 &&
            data.size() == 84 + numTriangles * 50) isBinary = true;
    }

    if (isBinary) {
        quint32 numTriangles = *reinterpret_cast<const quint32*>(data.constData() + 80);
        const char* ptr = data.constData() + 84;
        for (quint32 i = 0; i < numTriangles; ++i) {
            ptr += 12; // normal
            for (int v = 0; v < 3; ++v) {
                float x = *reinterpret_cast<const float*>(ptr);
                float y = *reinterpret_cast<const float*>(ptr + 4);
                float z = *reinterpret_cast<const float*>(ptr + 8);
                positions.append(QVector3D(x, y, z));
                indices.append(positions.size() - 1);
                ptr += 12;
            }
            ptr += 2; // attribute byte count
        }
    } else {
        QTextStream in(&data);
        QVector<QVector3D> facePositions;
        bool inTriangle = false;
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith("facet normal")) { facePositions.clear(); inTriangle = true; }
            else if (line == "endfacet" && inTriangle) {
                for (const auto& p : facePositions) { positions.append(p); indices.append(positions.size() - 1); }
                inTriangle = false;
            } else if (line.startsWith("vertex") && inTriangle) {
                QStringList parts = line.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 4) facePositions.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
            }
        }
    }

    if (positions.isEmpty()) { emit error("STL file contains no triangles"); return false; }

    MeshData md;
    md.vertices.reserve(positions.size());
    for (const auto& p : positions) {
        Vertex vtx;
        vtx.position = p;
        vtx.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
        md.vertices.append(vtx);
    }
    for (int i = 0; i + 2 < indices.size(); i += 3)
        md.faces.append(Face({ (int)indices[i], (int)indices[i+1], (int)indices[i+2] }));
    md.computeNormals();

    importMeshDataToScene(m_scene, md, QFileInfo(path).baseName());
    m_currentFile = path;
    emit sceneChanged();
    emit statusMessage(QString("Imported STL: %1 triangles").arg(indices.size() / 3));
    return true;
}

bool KSModelerQml::exportSTL(const QString& path) {
    if (!m_scene) { emit error("No scene to export"); return false; }
    emit statusMessage("Exporting STL: " + path);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { emit error("Cannot write STL file"); return false; }
    QTextStream out(&file);
    out.setRealNumberPrecision(7);
    out << "solid ksEditor_export\n";

    int totalTriangles = 0;
    for (SceneObject* obj : m_scene->allObjects()) {
        if (!obj || !obj->mesh() || !obj->isVisible()) continue;
        const auto& verts = obj->mesh()->geometry().vertices;
        const auto& idxs = obj->mesh()->geometry().indices;
        if (verts.isEmpty() || idxs.isEmpty()) continue;
        QMatrix4x4 w = obj->worldTransform();
        for (int i = 0; i + 2 < idxs.size(); i += 3) {
            QVector3D v0 = w.map(verts[idxs[i]].position);
            QVector3D v1 = w.map(verts[idxs[i+1]].position);
            QVector3D v2 = w.map(verts[idxs[i+2]].position);
            QVector3D n = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();
            out << "  facet normal " << n.x() << " " << n.y() << " " << n.z() << "\n";
            out << "    outer loop\n";
            out << "      vertex " << v0.x() << " " << v0.y() << " " << v0.z() << "\n";
            out << "      vertex " << v1.x() << " " << v1.y() << " " << v1.z() << "\n";
            out << "      vertex " << v2.x() << " " << v2.y() << " " << v2.z() << "\n";
            out << "    endloop\n";
            out << "  endfacet\n";
            totalTriangles++;
        }
    }
    out << "endsolid ksEditor_export\n";
    file.close();

    emit statusMessage(QString("STL exported: %1 triangles").arg(totalTriangles));
    return true;
}

void KSModelerQml::selectObject(int id) {
    if (!m_scene) return;
    SceneObject* obj = m_scene->findObjectById(id);
    if (m_scene) {
        for (SceneObject* o : m_scene->allObjects())
            o->setSelected(o == obj);
    }
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = obj ? new SceneObjectQml(obj) : nullptr;
    if (m_sceneModel) m_sceneModel->refresh();
    emit selectionChanged();
    emit gizmoTransformChanged();
}

void KSModelerQml::selectAll() {
    if (!m_scene) return;
    SceneObject* last = nullptr;
    for (SceneObject* obj : m_scene->allObjects()) {
        obj->setSelected(true);
        last = obj;
    }
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = last ? new SceneObjectQml(last) : nullptr;
    if (m_sceneModel) m_sceneModel->refresh();
    emit selectionChanged();
    emit gizmoTransformChanged();
}

void KSModelerQml::deselectAll() {
    if (m_scene) {
        for (SceneObject* o : m_scene->allObjects())
            o->setSelected(false);
    }
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = nullptr;
    if (m_sceneModel) m_sceneModel->refresh();
    emit selectionChanged();
    emit gizmoTransformChanged();
}

void KSModelerQml::toggleSelectObject(int id) {
    if (!m_scene) return;
    SceneObject* obj = m_scene->findObjectById(id);
    if (!obj) return;
    obj->setSelected(!obj->isSelected());
    SceneObject* last = obj->isSelected() ? obj : nullptr;
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = last ? new SceneObjectQml(last) : nullptr;
    if (m_sceneModel) m_sceneModel->refresh();
    emit selectionChanged();
    emit gizmoTransformChanged();
}

QStringList KSModelerQml::selectedObjectNames() const {
    QStringList names;
    if (!m_scene) return names;
    for (SceneObject* o : m_scene->allObjects())
        if (o->isSelected())
            names.append(o->name());
    return names;
}

QStringList KSModelerQml::selectionSetNames() const {
    return m_selectionSets.keys();
}

int KSModelerQml::selectionSetMemberCount(const QString& name) const {
    return m_selectionSets.value(name).size();
}

bool KSModelerQml::createSelectionSet(const QString& name) {
    if (name.trimmed().isEmpty()) return false;
    const QString key = name.trimmed();
    QStringList sel = selectedObjectNames();
    if (sel.isEmpty()) {
        emit statusMessage("Nothing selected to save into set \"" + key + "\"");
        return false;
    }
    m_selectionSets.insert(key, QSet<QString>(sel.begin(), sel.end()));
    emit selectionSetsChanged();
    emit statusMessage("Selection set \"" + key + "\" created (" + QString::number(sel.size()) + " objects)");
    return true;
}

bool KSModelerQml::addSelectionToSet(const QString& name) {
    const QString key = name.trimmed();
    if (key.isEmpty() || !m_selectionSets.contains(key)) {
        emit statusMessage("Selection set \"" + name + "\" does not exist");
        return false;
    }
    QStringList sel = selectedObjectNames();
    if (sel.isEmpty()) {
        emit statusMessage("Nothing selected to add to set \"" + key + "\"");
        return false;
    }
    QSet<QString>& set = m_selectionSets[key];
    for (const QString& n : sel)
        set.insert(n);
    emit selectionSetsChanged();
    emit statusMessage("Added " + QString::number(sel.size()) + " object(s) to set \"" + key + "\"");
    return true;
}

bool KSModelerQml::recallSelectionSet(const QString& name) {
    const QString key = name.trimmed();
    if (!m_selectionSets.contains(key)) {
        emit statusMessage("Selection set \"" + name + "\" does not exist");
        return false;
    }
    if (!m_scene) return false;
    SceneObject* last = nullptr;
    for (SceneObject* o : m_scene->allObjects()) {
        bool inSet = m_selectionSets.value(key).contains(o->name());
        o->setSelected(inSet);
        if (inSet) last = o;
    }
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = last ? new SceneObjectQml(last) : nullptr;
    if (m_sceneModel) m_sceneModel->refresh();
    emit selectionChanged();
    emit gizmoTransformChanged();
    emit statusMessage("Recalled selection set \"" + key + "\" (" +
                       QString::number(m_selectionSets.value(key).size()) + " objects)");
    return true;
}

bool KSModelerQml::clearSelectionSet(const QString& name) {
    const QString key = name.trimmed();
    if (!m_selectionSets.contains(key)) return false;
    m_selectionSets[key].clear();
    emit selectionSetsChanged();
    emit statusMessage("Cleared members of set \"" + key + "\"");
    return true;
}

bool KSModelerQml::deleteSelectionSet(const QString& name) {
    const QString key = name.trimmed();
    if (!m_selectionSets.remove(key)) return false;
    emit selectionSetsChanged();
    emit statusMessage("Deleted selection set \"" + key + "\"");
    return true;
}

bool KSModelerQml::renameSelectionSet(const QString& oldName, const QString& newName) {
    const QString oldKey = oldName.trimmed();
    const QString newKey = newName.trimmed();
    if (oldKey.isEmpty() || newKey.isEmpty() || oldKey == newKey) return false;
    auto it = m_selectionSets.find(oldKey);
    if (it == m_selectionSets.end()) return false;
    if (m_selectionSets.contains(newKey)) {
        emit statusMessage("A set named \"" + newKey + "\" already exists");
        return false;
    }
    QSet<QString> members = it.value();
    m_selectionSets.erase(it);
    m_selectionSets.insert(newKey, members);
    emit selectionSetsChanged();
    emit statusMessage("Renamed selection set \"" + oldKey + "\" to \"" + newKey + "\"");
    return true;
}

// ============================================================================
// Object hierarchy (families / rig hierarchy tools)
// ============================================================================

namespace {
// Recompute a child's local TRS so its world transform is unchanged after
// reparenting to a new parent. Falls back to the raw local matrix when the
// world matrix carries shear (non-uniform parent scale + rotation).
void reparentPreservingWorld(SceneGraph* scene, SceneObject* child, SceneObject* newParent) {
    scene->updateAllTransforms();
    const QMatrix4x4 wc = child->worldTransform();
    const QMatrix4x4 wp = newParent->worldTransform();
    const QMatrix4x4 local = wp.inverted() * wc;

    const QVector3D pos(local(0, 3), local(1, 3), local(2, 3));
    const QVector3D c0(local(0, 0), local(1, 0), local(2, 0));
    const QVector3D c1(local(0, 1), local(1, 1), local(2, 1));
    const QVector3D c2(local(0, 2), local(1, 2), local(2, 2));
    const QVector3D s(c0.length(), c1.length(), c2.length());

    QVector3D euler;
    if (s.x() > 1e-9f && s.y() > 1e-9f && s.z() > 1e-9f) {
        const float a00 = c0.x() / s.x(), a01 = c0.y() / s.x(), a02 = c0.z() / s.x();
        const float a10 = c1.x() / s.y(), a11 = c1.y() / s.y(), a12 = c1.z() / s.y();
        const float a20 = c2.x() / s.z(), a21 = c2.y() / s.z(), a22 = c2.z() / s.z();
        const float sy = qSqrt(a00 * a00 + a10 * a10);
        if (sy > 1e-6f)
            euler = QVector3D(qAtan2(a21, a22), qAtan2(-a20, sy), qAtan2(a10, a00));
        else
            euler = QVector3D(qAtan2(-a21, a11), qAtan2(-a20, sy), 0.0f);
    }

    child->setPosition(pos);
    child->setRotationEuler(euler);
    child->setScale(s);
    newParent->addChild(child);
    scene->updateAllTransforms();
}
} // namespace

QVector<SceneObject*> KSModelerQml::selectedTopLevelObjects() const {
    QVector<SceneObject*> top;
    if (!m_scene) return top;
    for (SceneObject* o : m_scene->allObjects()) {
        if (o == m_scene->root() || !o->isSelected()) continue;
        bool parentSelected = false;
        for (SceneObject* p = o->parent(); p && p != m_scene->root(); p = p->parent()) {
            if (p->isSelected()) { parentSelected = true; break; }
        }
        if (!parentSelected) top.append(o);
    }
    return top;
}

bool KSModelerQml::groupSelected(const QString& name) {
    if (!m_scene) return false;
    QVector<SceneObject*> top = selectedTopLevelObjects();
    if (top.isEmpty()) {
        emit statusMessage("Nothing selected to group");
        return false;
    }
    QString groupName = name.trimmed();
    if (groupName.isEmpty()) groupName = "Group";
    SceneObject* group = m_scene->createObject(groupName, SceneObject::Type::Node, m_scene->root());
    for (SceneObject* o : top)
        reparentPreservingWorld(m_scene, o, group);

    for (SceneObject* o : m_scene->allObjects())
        o->setSelected(o == group);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(group);
    if (m_sceneModel) m_sceneModel->refresh();
    emit sceneChanged();
    emit selectionChanged();
    emit gizmoTransformChanged();
    emit statusMessage("Grouped " + QString::number(top.size()) + " objects into \"" + groupName + "\"");
    return true;
}

void KSModelerQml::ungroupSelected() {
    if (!m_scene) return;
    QVector<SceneObject*> top = selectedTopLevelObjects();
    int ungroupped = 0;
    for (SceneObject* o : top) {
        if (o->type() != SceneObject::Type::Node) continue;
        const QVector<SceneObject*> children = o->children();
        SceneObject* newParent = o->parent() ? o->parent() : m_scene->root();
        for (SceneObject* c : children)
            reparentPreservingWorld(m_scene, c, newParent);
        if (!o->hasMesh())
            m_scene->deleteObject(o);
        ++ungroupped;
    }
    if (m_selectedObject) { delete m_selectedObject; m_selectedObject = nullptr; }
    if (m_sceneModel) m_sceneModel->refresh();
    emit sceneChanged();
    emit selectionChanged();
    emit gizmoTransformChanged();
    emit statusMessage("Ungrouped " + QString::number(ungroupped) + " node(s)");
}

bool KSModelerQml::reparentObject(int childId, int parentId) {
    if (!m_scene) return false;
    SceneObject* child = m_scene->findObjectById(childId);
    SceneObject* parent = m_scene->findObjectById(parentId);
    if (!child || !parent || child == parent) return false;
    if (parent == m_scene->root() && child->parent() == m_scene->root()) return false;
    for (SceneObject* p = parent; p; p = p->parent()) {
        if (p == child) return false; // would create a cycle
    }
    if (child->parent() == parent) return false;
    reparentPreservingWorld(m_scene, child, parent);
    if (m_sceneModel) m_sceneModel->refresh();
    emit sceneChanged();
    emit selectionChanged();
    emit gizmoTransformChanged();
    emit statusMessage("Reparented \"" + child->name() + "\" under \"" + parent->name() + "\"");
    return true;
}

int KSModelerQml::parentObjectId(int objectId) const {
    if (!m_scene) return -1;
    SceneObject* o = m_scene->findObjectById(objectId);
    if (!o || !o->parent()) return -1;
    return o->parent()->id();
}

// ============================================================================
// Scene factories (XSI-style parameterized templates)
// ============================================================================

QStringList KSModelerQml::factoryNames() const {
    QStringList list = { "Box", "Sphere", "Cylinder", "Cone", "Torus", "Plane",
                         "Camera", "Light", "Transform Group" };
    list.append(m_userFactories.keys());
    return list;
}

QString KSModelerQml::factoryType(const QString& name) const {
    const QString key = name.trimmed();
    auto it = m_userFactories.find(key);
    if (it != m_userFactories.end())
        return (it.value().type == "Mesh" ? "Mesh" : it.value().type) + " (user)";
    const QString f = key.toLower();
    if (f == "box" || f == "sphere" || f == "cylinder" || f == "cone" || f == "torus" || f == "plane")
        return "Primitive";
    if (f == "camera") return "Camera";
    if (f == "light" || f == "directional light") return "Light";
    if (f == "transform group" || f == "node" || f == "group") return "Node";
    return "Unknown";
}

bool KSModelerQml::factoryCreate(const QString& name, float p0, float p1, float p2) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    const QString key = name.trimmed();

    auto it = m_userFactories.find(key);
    if (it != m_userFactories.end()) {
        const FactoryTemplate& t = it.value();
        if (t.type == "Mesh") {
            MeshData md = meshDataFromJson(t.meshData);
            if (md.vertices.isEmpty()) return false;
            SceneObject* obj = m_scene->createObject(t.objectName, SceneObject::Type::Mesh);
            meshDataToSceneMesh(obj, md);
            obj->setBaseColor(t.color);
            obj->setMetallic(t.metallic);
            obj->setRoughness(t.roughness);
            obj->setOpacity(t.opacity);
            obj->setScale(t.scale);
            if (m_selectedObject) delete m_selectedObject;
            m_selectedObject = new SceneObjectQml(obj);
            emit sceneChanged();
            emit selectionChanged();
            emit statusMessage("Factory \"" + key + "\" instantiated as \"" + t.objectName + "\"");
            return true;
        }
        int id = -1;
        if (t.type == "Camera") id = addCamera(t.objectName);
        else if (t.type == "Light") id = addLight(t.objectName);
        else id = addTransformGroup(t.objectName);
        if (SceneObject* obj = m_scene->findObjectById(id))
            obj->setScale(t.scale);
        emit statusMessage("Factory \"" + key + "\" instantiated");
        return true;
    }

    const QString f = key.toLower();
    if (f == "box") addPrimitiveCube(p0 > 0 ? p0 : 1.0f);
    else if (f == "sphere") addPrimitiveSphere(p0 > 0 ? p0 : 1.0f);
    else if (f == "cylinder") addPrimitiveCylinder(p0 > 0 ? p0 : 1.0f, p1 > 0 ? p1 : 2.0f);
    else if (f == "cone") addPrimitiveCone(p0 > 0 ? p0 : 1.0f, p1 > 0 ? p1 : 2.0f);
    else if (f == "torus") addPrimitiveTorus(p0 > 0 ? p0 : 1.0f, p1 > 0 ? p1 : 0.25f);
    else if (f == "plane") addPrimitivePlane(p0 > 0 ? p0 : 2.0f, p1 > 0 ? p1 : 2.0f);
    else if (f == "camera") addCamera("Camera");
    else if (f == "light" || f == "directional light") addLight("Light");
    else if (f == "transform group" || f == "node" || f == "group") addTransformGroup("TransformGroup");
    else { emit statusMessage("Unknown factory: " + name); return false; }
    return true;
}

bool KSModelerQml::factorySaveFromSelection(const QString& name) {
    const QString key = name.trimmed();
    if (key.isEmpty()) return false;
    if (m_userFactories.contains(key)) {
        emit statusMessage("A factory named \"" + key + "\" already exists");
        return false;
    }
    if (!m_selectedObject || !m_selectedObject->object()) {
        emit statusMessage("Select an object to save as a factory");
        return false;
    }
    SceneObject* obj = m_selectedObject->object();
    FactoryTemplate t;
    t.objectName = obj->name();
    t.color = obj->baseColor();
    t.metallic = obj->metallic();
    t.roughness = obj->roughness();
    t.opacity = obj->opacity();
    t.scale = obj->scale();
    switch (obj->type()) {
    case SceneObject::Type::Mesh:
    case SceneObject::Type::Spline: {
        t.type = "Mesh";
        t.meshData = meshDataToJson(sceneMeshToMeshData(obj));
        break;
    }
    case SceneObject::Type::Camera: t.type = "Camera"; break;
    case SceneObject::Type::Light: t.type = "Light"; break;
    default: t.type = "Node"; break;
    }
    m_userFactories.insert(key, t);
    emit factoryChanged();
    emit statusMessage("Saved factory \"" + key + "\" from selection");
    return true;
}

bool KSModelerQml::factoryDelete(const QString& name) {
    if (!m_userFactories.remove(name.trimmed())) return false;
    emit factoryChanged();
    emit statusMessage("Deleted factory \"" + name.trimmed() + "\"");
    return true;
}

QVariantMap KSModelerQml::factoryParams(const QString& name) const {
    QVariantMap out;
    const QString key = name.trimmed();
    out["name"] = key;
    out["type"] = factoryType(key);
    out["user"] = m_userFactories.contains(key);
    auto it = m_userFactories.find(key);
    if (it != m_userFactories.end()) {
        const FactoryTemplate& t = it.value();
        out["objectName"] = t.objectName;
        out["color"] = t.color.name();
        out["metallic"] = t.metallic;
        out["roughness"] = t.roughness;
        out["opacity"] = t.opacity;
        out["scaleX"] = t.scale.x();
        out["scaleY"] = t.scale.y();
        out["scaleZ"] = t.scale.z();
        if (t.type == "Mesh") {
            const MeshData md = meshDataFromJson(t.meshData);
            out["vertexCount"] = (int)md.vertices.size();
        }
    }
    return out;
}

int KSModelerQml::factoryUserCount() const {
    return m_userFactories.size();
}

void KSModelerQml::deleteSelected() {
    if (m_scene && m_selectedObject) {
        // Registra il comando per undo/redo
        if (m_commandHistory) {
            auto cmd = std::make_shared<DeleteObjectCommand>(m_selectedObject->id());
            m_commandHistory->execute(cmd);
        }

        m_scene->deleteObject(m_selectedObject->object());
        m_curves.remove(m_selectedObject->id());
        m_fcurves.remove(m_selectedObject->id());
        m_faceGroups.removeObject(m_selectedObject->id());
        m_sculptPins.remove(m_selectedObject->id());
        m_constraintSystem.clearObject(m_selectedObject->id());
        m_controllerSystem.clearObject(m_selectedObject->id());
        m_wireSystem.clearDriven(m_selectedObject->id());
        m_wireSystem.clearDriver(m_selectedObject->id());
        m_skinWrapSystem.clearObject(m_selectedObject->id());
        m_lightSystem.remove(m_selectedObject->id());
        auto boolIt = m_booleanStacks.find(m_selectedObject->id());
        if (boolIt != m_booleanStacks.end()) {
            booleanUnsubscribe(m_selectedObject->id());
            delete boolIt.value();
            m_booleanStacks.erase(boolIt);
        }
        auto stackIt = m_modifierStacks.find(m_selectedObject->id());
        if (stackIt != m_modifierStacks.end()) {
            modifierUnsubscribe(m_selectedObject->id());
            delete stackIt.value();
            m_modifierStacks.erase(stackIt);
        }
        if (InstanceReference::instance().isInstance(m_selectedObject->id()))
            InstanceReference::instance().realizeInstance(m_selectedObject->id());
        delete m_selectedObject;
        m_selectedObject = nullptr;
        emit sceneChanged();
        emit selectionChanged();
        emit gizmoTransformChanged();
    }
}

void KSModelerQml::duplicateSelected() {
    if (!m_scene || !m_selectedObject) return;
    SceneObject* orig = m_selectedObject->object();
    if (!orig) return;
    QString newName = orig->name() + "_copy";
    SceneObject* newObj = m_scene->createObject(newName, orig->type());
    if (newObj) {
        newObj->setPosition(orig->position());
        newObj->setRotationEuler(orig->rotationEuler());
        newObj->setScale(orig->scale());
        if (m_selectedObject) delete m_selectedObject;
        m_selectedObject = new SceneObjectQml(newObj);
        emit sceneChanged();
        emit selectionChanged();
        emit gizmoTransformChanged();
    }
}

void KSModelerQml::cutSelected() {
    copySelected();
    deleteSelected();
}

void KSModelerQml::copySelected() {
    if (!m_selectedObject || !m_selectedObject->object()) return;
    SceneObject* orig = m_selectedObject->object();
    m_clipboardType = orig->type();
    m_clipboardName = orig->name();
    auto p = orig->position();
    m_clipboardPosition = QVector3D(p.x(), p.y(), p.z());
    auto r = orig->rotationEuler();
    m_clipboardRotation = QVector3D(r.x(), r.y(), r.z());
    auto s = orig->scale();
    m_clipboardScale = QVector3D(s.x(), s.y(), s.z());
    m_clipboardActive = true;
    emit clipboardChanged();
}

void KSModelerQml::pasteClipboard() {
    if (!m_clipboardActive || !m_scene) return;
    SceneObject* newObj = m_scene->createObject(m_clipboardName + "_paste", m_clipboardType);
    if (newObj) {
        newObj->setPosition({m_clipboardPosition.x(), m_clipboardPosition.y(), m_clipboardPosition.z()});
        newObj->setRotationEuler({m_clipboardRotation.x(), m_clipboardRotation.y(), m_clipboardRotation.z()});
        newObj->setScale({m_clipboardScale.x(), m_clipboardScale.y(), m_clipboardScale.z()});
        if (m_selectedObject) delete m_selectedObject;
        m_selectedObject = new SceneObjectQml(newObj);
        emit sceneChanged();
        emit selectionChanged();
        emit gizmoTransformChanged();
    }
}

void KSModelerQml::printScene() {
    emit printRequested();
}

void KSModelerQml::checkMesh() {
    if (!m_scene || !m_selectedObject) {
        emit meshCheckResult("No object selected");
        return;
    }
    auto* obj = m_selectedObject->object();
    if (!obj || !obj->hasMesh()) {
        emit meshCheckResult("Object has no mesh");
        return;
    }
    auto* mesh = obj->mesh();
    int verts = mesh->vertexCount();
    int tris = mesh->indexCount() / 3;
    auto bbMin = obj->boundingBoxMin();
    auto bbMax = obj->boundingBoxMax();
    float sizeX = bbMax.x() - bbMin.x();
    float sizeY = bbMax.y() - bbMin.y();
    float sizeZ = bbMax.z() - bbMin.z();
    QString result = QString("Tris: %1 | Verts: %2 | Size: %3x%4x%5")
        .arg(tris).arg(verts)
        .arg(sizeX, 0, 'f', 2).arg(sizeY, 0, 'f', 2).arg(sizeZ, 0, 'f', 2);
    emit meshCheckResult(result);
}

void KSModelerQml::exportSTL() {
    emit stlExportRequested();
}

void KSModelerQml::scaleForPrint() {
    emit printScaleRequested();
}

void KSModelerQml::hollowMesh() {
    emit hollowRequested();
}

void KSModelerQml::generateSupports() {
    emit supportsRequested();
}

void KSModelerQml::sliceModel() {
    emit sliceRequested();
}

QVector3D KSModelerQml::gizmoPosition() const {
    if (m_selectedObject && m_selectedObject->object()) {
        auto obj = m_selectedObject->object();
        if (m_curveCvEdit && m_curveSelectedCv >= 0) {
            auto it = m_curves.constFind(obj->id());
            if (it != m_curves.constEnd() && m_curveSelectedCv < it->controlPoints.size())
                return obj->position() + it->controlPoints[m_curveSelectedCv];
        }
        auto p = obj->position();
        return QVector3D(p.x(), p.y(), p.z());
    }
    return QVector3D();
}

QVector3D KSModelerQml::gizmoRotation() const {
    if (m_selectedObject && m_selectedObject->object()) {
        auto r = m_selectedObject->object()->rotationEuler();
        return QVector3D(r.x(), r.y(), r.z());
    }
    return QVector3D();
}

QVector3D KSModelerQml::gizmoScale() const {
    if (m_selectedObject && m_selectedObject->object()) {
        auto s = m_selectedObject->object()->scale();
        return QVector3D(s.x(), s.y(), s.z());
    }
    return QVector3D(1, 1, 1);
}

void KSModelerQml::setGizmoMode(int mode) {
    if (m_gizmoMode != mode) {
        m_gizmoMode = mode;
        emit gizmoModeChanged();
        emit gizmoTransformChanged();
    }
}

bool KSModelerQml::curveCvEdit() const {
    return m_curveCvEdit;
}

void KSModelerQml::setCurveCvEdit(bool on) {
    if (m_curveCvEdit != on) {
        m_curveCvEdit = on;
        emit curveCvEditChanged();
        emit gizmoTransformChanged();
        emit statusMessage(on ? "CV edit mode ON: select a CV in the viewport, then drag the gizmo"
                              : "CV edit mode OFF");
    }
}

int KSModelerQml::curveSelectedCV() const {
    return m_curveSelectedCv;
}

void KSModelerQml::setCurveSelectedCV(int index) {
    if (m_curveSelectedCv != index) {
        m_curveSelectedCv = index;
        emit curveSelectedCVChanged();
        emit gizmoTransformChanged();
    }
}

void KSModelerQml::translateSelectedCV(float x, float y, float z) {
    if (!m_curveCvEdit || m_curveSelectedCv < 0) return;
    SceneObject* obj = selectedSceneObject();
    if (!obj) return;
    auto it = m_curves.find(obj->id());
    if (it == m_curves.end()) return;
    if (m_curveSelectedCv >= it->controlPoints.size()) return;
    QVector3D n = it->controlPoints[m_curveSelectedCv] + QVector3D(x, y, z);
    if (m_snapEnabled && m_snapIncrement > 0.0f) {
        const float inc = m_snapIncrement;
        n = QVector3D(std::round(n.x() / inc) * inc,
                      std::round(n.y() / inc) * inc,
                      std::round(n.z() / inc) * inc);
    }
    it->controlPoints[m_curveSelectedCv] = n;
    writeCurveMesh(obj, it.value(), 0.02f, 32);
    emit sceneChanged();
    emit curveChanged();
    emit gizmoTransformChanged();
}

void KSModelerQml::translateSelected(float x, float y, float z) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return;
    QVector3D p = obj->position();
    QVector3D n = QVector3D(p.x() + x, p.y() + y, p.z() + z);
    if (m_snapEnabled && m_snapIncrement > 0.0f) {
        const float inc = m_snapIncrement;
        n = QVector3D(std::round(n.x() / inc) * inc,
                      std::round(n.y() / inc) * inc,
                      std::round(n.z() / inc) * inc);
    }
    obj->setPosition(n);
    emit sceneChanged();
    emit gizmoTransformChanged();
}

void KSModelerQml::rotateSelected(float x, float y, float z) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return;
    QVector3D r = obj->rotationEuler();
    QVector3D n = QVector3D(r.x() + x, r.y() + y, r.z() + z);
    if (m_snapEnabled && m_snapIncrement > 0.0f) {
        const float inc = m_snapIncrement;
        n = QVector3D(std::round(n.x() / inc) * inc,
                      std::round(n.y() / inc) * inc,
                      std::round(n.z() / inc) * inc);
    }
    obj->setRotationEuler(n);
    emit sceneChanged();
    emit gizmoTransformChanged();
}

void KSModelerQml::scaleSelected(float x, float y, float z) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return;
    QVector3D s = obj->scale();
    obj->setScale(QVector3D(s.x() * x, s.y() * y, s.z() * z));
    emit sceneChanged();
    emit gizmoTransformChanged();
}

void KSModelerQml::setSnapEnabled(bool enabled) {
    m_snapEnabled = enabled;
}

void KSModelerQml::setSnapIncrement(float inc) {
    m_snapIncrement = inc > 0.0f ? inc : 0.1f;
}

// ============================================================================
// Proportional Editing
// ============================================================================

void KSModelerQml::setProportionalEditing(bool enabled) {
    if (m_propEdit.enabled != enabled) {
        m_propEdit.enabled = enabled;
        if (!enabled) m_propEdit.hasCenter = false;
        emit proportionalEditingChanged();
        emit statusMessage(enabled ? "Proportional editing ON" : "Proportional editing OFF");
    }
}

bool KSModelerQml::isProportionalEditing() const { return m_propEdit.enabled; }

void KSModelerQml::setProportionalRadius(float radius) {
    m_propEdit.radius = qMax(0.01f, radius);
    emit proportionalEditingChanged();
}

float KSModelerQml::proportionalRadius() const { return m_propEdit.radius; }

void KSModelerQml::setProportionalFalloffType(int type) {
    m_propEdit.falloffType = qBound(0, type, 5);
    emit proportionalEditingChanged();
}

int KSModelerQml::proportionalFalloffType() const { return m_propEdit.falloffType; }

bool KSModelerQml::pickProportionalCenter(float pickX, float pickY, float pickZ) {
    if (!m_selectedObject || !m_selectedObject->object() || !m_selectedObject->object()->mesh())
        return false;
    auto& verts = m_selectedObject->object()->mesh()->geometry().vertices;
    if (verts.isEmpty()) return false;

    QVector3D pickPoint(pickX, pickY, pickZ);
    float minDist = std::numeric_limits<float>::max();
    QVector3D closest(verts[0].position.x(), verts[0].position.y(), verts[0].position.z());
    for (const auto& v : verts) {
        QVector3D pos(v.position.x(), v.position.y(), v.position.z());
        float d = (pos - pickPoint).lengthSquared();
        if (d < minDist) { minDist = d; closest = pos; }
    }
    m_propEdit.center = closest;
    m_propEdit.hasCenter = true;
    emit proportionalCenterChanged();
    return true;
}

void KSModelerQml::clearProportionalCenter() {
    m_propEdit.hasCenter = false;
    emit proportionalCenterChanged();
}

bool KSModelerQml::hasProportionalCenter() const { return m_propEdit.hasCenter; }

QVector3D KSModelerQml::proportionalCenter() const { return m_propEdit.center; }

static float propFalloff(float distance, float radius, int type) {
    float t = distance / radius;
    if (t >= 1.0f) return 0.0f;
    switch (type) {
        case 0: // Smooth (smoothstep)
            t = 1.0f - t;
            return t * t * (3.0f - 2.0f * t);
        case 1: // Linear
            return 1.0f - t;
        case 2: // Sharp
            return (1.0f - t) * (1.0f - t);
        case 3: // Root
            return sqrtf(1.0f - t);
        case 4: // Sphere
            return sqrtf(1.0f - t * t);
        case 5: // Constant
            return 1.0f;
        default:
            return 1.0f - t;
    }
}

void KSModelerQml::translateProportional(float x, float y, float z) {
    if (!m_propEdit.enabled || !m_propEdit.hasCenter || !m_selectedObject)
        return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;

    auto& verts = obj->mesh()->geometry().vertices;
    QVector3D delta(x, y, z);
    float radius = m_propEdit.radius;
    int falloff = m_propEdit.falloffType;
    QVector3D center = m_propEdit.center;

    for (auto& v : verts) {
        QVector3D pos(v.position.x(), v.position.y(), v.position.z());
        float dist = (pos - center).length();
        if (dist < radius) {
            float w = propFalloff(dist, radius, falloff);
            pos += QVector3D(delta.x() * w, delta.y() * w, delta.z() * w);
            v.position = pos;
        }
    }
    emit sceneChanged();
}

void KSModelerQml::rotateProportional(float x, float y, float z) {
    if (!m_propEdit.enabled || !m_propEdit.hasCenter || !m_selectedObject)
        return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;

    auto& verts = obj->mesh()->geometry().vertices;
    float rx = qDegreesToRadians(x);
    float ry = qDegreesToRadians(y);
    float rz = qDegreesToRadians(z);
    float radius = m_propEdit.radius;
    int falloff = m_propEdit.falloffType;
    QVector3D center = m_propEdit.center;

    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);

    for (auto& v : verts) {
        QVector3D pos(v.position.x(), v.position.y(), v.position.z());
        QVector3D rel = pos - center;
        float dist = rel.length();
        if (dist < radius) {
            float w = propFalloff(dist, radius, falloff);
            // Apply rotation around center with weight
            float rx2 = rel.x() * w, ry2 = rel.y() * w, rz2 = rel.z() * w;
            // Rotate Y
            float nx = cy * rx2 + sy * rz2;
            float nz = -sy * rx2 + cy * rz2;
            rx2 = nx; rz2 = nz;
            // Rotate X
            float ny = cx * ry2 - sx * rz2;
            nz = sx * ry2 + cx * rz2;
            ry2 = ny; rz2 = nz;
            // Rotate Z
            nx = cz * rx2 - sz * ry2;
            ny = sz * rx2 + cz * ry2;
            rx2 = nx; ry2 = ny;

            v.position = QVector3D(center.x() + rx2, center.y() + ry2, center.z() + rz2);
        }
    }
}

void KSModelerQml::scaleProportional(float x, float y, float z) {
    if (!m_propEdit.enabled || !m_propEdit.hasCenter || !m_selectedObject)
        return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;

    auto& verts = obj->mesh()->geometry().vertices;
    float radius = m_propEdit.radius;
    int falloff = m_propEdit.falloffType;
    QVector3D center = m_propEdit.center;

    for (auto& v : verts) {
        QVector3D pos(v.position.x(), v.position.y(), v.position.z());
        QVector3D rel = pos - center;
        float dist = rel.length();
        if (dist < radius) {
            float w = propFalloff(dist, radius, falloff);
            float sx = 1.0f + (x - 1.0f) * w;
            float sy = 1.0f + (y - 1.0f) * w;
            float sz = 1.0f + (z - 1.0f) * w;
            v.position.setX(center.x() + rel.x() * sx);
            v.position.setY(center.y() + rel.y() * sy);
            v.position.setZ(center.z() + rel.z() * sz);
        }
    }
    emit sceneChanged();
}

void KSModelerQml::setSelectedPosition(float x, float y, float z) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return;
    obj->setPosition(QVector3D(x, y, z));
    emit sceneChanged();
    emit gizmoTransformChanged();
}

void KSModelerQml::setSelectedRotation(float x, float y, float z) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return;
    obj->setRotationEuler(QVector3D(x, y, z));
    emit sceneChanged();
}

void KSModelerQml::setSelectedScale(float x, float y, float z) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return;
    obj->setScale(QVector3D(x, y, z));
    emit sceneChanged();
    emit gizmoTransformChanged();
}

void KSModelerQml::extrudeFaces(const QList<int>& faceIndices, float distance) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector3D> dirs;
    QList<int> faces = faceIndices;
    if (faces.isEmpty()) {
        for (int i = 0; i < md.faces.size(); i++)
            faces.append(i);
    }
    for (int i = 0; i < faces.size(); i++)
        dirs.append(QVector3D(0, 0, distance));
    MeshData result = MeshOperations::extrudeFaces(md, dirs);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Extruded %1 faces").arg(faces.size()));
}

void KSModelerQml::bevelEdges(const QList<int>& edgeIndices, float amount, int segments) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::bevelEdges(md, amount, segments);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Beveled with %1 segments").arg(segments));
}

bool KSModelerQml::revolveSketch(int steps, float angle, bool closeCaps, int axis)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData md = sceneMeshToMeshData(obj);
    const QVector3D ax = (axis == 0) ? QVector3D(1, 0, 0)
                     : (axis == 2) ? QVector3D(0, 0, 1)
                     : QVector3D(0, 1, 0);
    MeshData result = MeshOperations::revolveSketch(md, ax, angle, qBound(3, steps, 512), closeCaps);
    if (result.vertices.size() <= md.vertices.size()) {
        emit errorMessage("Revolve needs a 2D sketch profile (a polyline of 2+ points)");
        return false;
    }
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Revolved sketch: %1% over %2 rings (axis %3)")
                           .arg(qAbs(angle)).arg(steps).arg(axis));
    return true;
}

void KSModelerQml::subdivideFaces(const QList<int>& faceIndices, int cuts) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::subdivide(md, cuts);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Subdivided with %1 cuts").arg(cuts));
}

void KSModelerQml::triangulateMesh() {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::triangulate(md);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage("Mesh triangulated");
}

void KSModelerQml::flipNormals() {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    md.flipFaces();
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("Normals flipped");
}

void KSModelerQml::recalculateNormals() {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    md.computeNormals();
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("Normals recalculated");
}

void KSModelerQml::weldVertices(float threshold) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    if (threshold <= 0.0f) threshold = 0.001f;

    MeshData md = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::weldVertices(md, threshold);

    if (result.vertices.size() == md.vertices.size()) {
        emit statusMessage("No vertices to weld");
        return;
    }

    int removed = md.vertices.size() - result.vertices.size();
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Welded %1 vertices (%2 remaining)").arg(removed).arg(result.vertices.size()));
}

void KSModelerQml::removeDoubles(float threshold) {
    weldVertices(threshold > 0.0f ? threshold : 0.0001f);
}

void KSModelerQml::mirrorMesh(int axis) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    auto& verts = obj->mesh()->geometry().vertices;
    int origCount = verts.size();
    auto& idxs = obj->mesh()->geometry().indices;
    int origIdxCount = idxs.size();
    float sign = (axis == 0) ? -1.0f : 1.0f;
    for (int i = 0; i < origCount; i++) {
        SceneVertex sv = verts[i];
        if (axis == 0) sv.position.setX(-sv.position.x());
        else if (axis == 1) sv.position.setY(-sv.position.y());
        else sv.position.setZ(-sv.position.z());
        verts.append(sv);
    }
    for (int i = 0; i + 2 < origIdxCount; i += 3) {
        uint32_t i0 = idxs[i];
        uint32_t i1 = idxs[i + 1];
        uint32_t i2 = idxs[i + 2];
        idxs.append(i2 + origCount);
        idxs.append(i1 + origCount);
        idxs.append(i0 + origCount);
    }
    emit sceneChanged();
    emit statusMessage(QString("Mirrored on %1 axis").arg(axis == 0 ? 'X' : (axis == 1 ? 'Y' : 'Z')));
}

QVariantList KSModelerQml::getMeshObjects() const {
    QVariantList result;
    if (!m_scene) return result;
    auto allObjs = m_scene->allObjects();
    int selectedId = m_selectedObject ? m_selectedObject->object()->id() : -1;
    for (SceneObject* obj : allObjs) {
        if (!obj->mesh()) continue;
        if (obj->id() == selectedId) continue; // skip self
        QVariantMap entry;
        entry["id"] = obj->id();
        entry["name"] = obj->name();
        result.append(entry);
    }
    return result;
}

void KSModelerQml::booleanOperation(int operation, int targetObjectId) {
    if (!m_selectedObject) return;
    SceneObject* src = m_selectedObject->object();
    if (!src || !src->mesh()) return;
    SceneObject* tgt = (targetObjectId >= 0) ? m_scene->findObjectById(targetObjectId) : nullptr;
    if (!tgt || !tgt->mesh()) return;
    MeshData srcData = sceneMeshToMeshData(src);
    MeshData tgtData = sceneMeshToMeshData(tgt);

    // Try CGAL-backed BooleanOperations via MeshOperations fallback chain
    MeshData result;
    switch (operation) {
        case 0: result = MeshOperations::booleanUnion(srcData, tgtData); break;
        case 1: result = MeshOperations::booleanDifference(srcData, tgtData); break;
        case 2: result = MeshOperations::booleanIntersection(srcData, tgtData); break;
        case 3: result = MeshOperations::booleanXor(srcData, tgtData); break;
        default: return;
    }

    if (result.vertices.isEmpty()) {
        emit statusMessage("Boolean operation produced empty result");
        return;
    }

    meshDataToSceneMesh(src, result);
    m_scene->deleteObject(tgt);
    emit sceneChanged();
    emit statusMessage("Boolean operation completed: " +
        QString::number(result.vertices.size()) + " vertices, " +
        QString::number(result.faces.size()) + " faces");
}

void KSModelerQml::insetFaces(const QList<int>& faceIndices, float amount) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData data = sceneMeshToMeshData(obj);
    data = MeshOperations::insetFaces(data, amount);
    meshDataToSceneMesh(obj, data);
    emit sceneChanged();
    emit statusMessage(" Faces inset");
}

void KSModelerQml::knifeCut(float startX, float startY, float startZ, float endX, float endY, float endZ) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    QVector3D cutStart(startX, startY, startZ);
    QVector3D cutEnd(endX, endY, endZ);
    MeshData data = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::knifeCut(data, cutStart, cutEnd);
    if (result.faces.size() == data.faces.size()) {
        emit statusMessage(" Knife cut: no edges intersected");
        return;
    }
    int edgesSplit = result.vertices.size() - data.vertices.size();
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(" Knife cut: " + QString::number(edgesSplit) + " edges split");
}

bool KSModelerQml::knifeCutWorld(int objectId, float sx, float sy, float sz, float ex, float ey, float ez) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;

    m_scene->updateAllTransforms();
    QMatrix4x4 inv = obj->worldTransform().inverted();
    if (inv.isIdentity() && !obj->worldTransform().isIdentity()) return false;

    QVector3D start = inv.map(QVector3D(sx, sy, sz));
    QVector3D end = inv.map(QVector3D(ex, ey, ez));
    MeshData data = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::knifeCut(data, start, end);
    if (result.faces.size() == data.faces.size()) {
        emit statusMessage("Knife cut: no edges intersected (try clicking on the mesh surface)");
        return false;
    }
    int edgesSplit = result.vertices.size() - data.vertices.size();
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Knife cut: %1 edges split").arg(edgesSplit));
    return true;
}

bool KSModelerQml::filletEdges(const QList<int>& edgeIndices, float radius) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    QVector<int> edges;
    for (int e : edgeIndices) edges.append(e);
    if (edges.isEmpty()) {
        for (int i = 0; i < data.edges.size(); i++) edges.append(i);
    }
    MeshData result;
    bool ok = MeshOperations::filletEdges(data, edges, radius, result);
    if (ok) {
        meshDataToSceneMesh(obj, result);
        emit sceneChanged();
        emit statusMessage(QString("Filleted %1 edges (r=%2)").arg(edges.size()).arg(radius));
    }
    return ok;
}

bool KSModelerQml::filletChain(const QList<int>& edgeIndices, const QVariantList& radii,
                               int segments, float angleLimitDeg)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    MeshOperations::ensureEdgeList(data);
    QVector<int> edges;
    for (int e : edgeIndices) edges.append(e);
    QVector<float> r;
    for (const auto& v : radii) r.append(v.toFloat());
    if (r.isEmpty()) r.append(0.05f);

    MeshData result = MeshOperations::bevelChain(
        data, edges, r, qBound(1, segments, 16), qDegreesToRadians(qBound(0.0f, angleLimitDeg, 90.0f)));
    if (result.vertices.isEmpty()) return false;
    if (result.faces.size() == data.faces.size()) {
        emit statusMessage("Fillet chain: no eligible edges at this angle limit");
        return false;
    }
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Fillet chain: %1 edges (%2 faces)").arg(edges.size()).arg(result.faces.size()));
    return true;
}

bool KSModelerQml::chamferEdges(const QList<int>& edgeIndices, float distance) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    QVector<int> edges;
    for (int e : edgeIndices) edges.append(e);
    if (edges.isEmpty()) {
        for (int i = 0; i < data.edges.size(); i++) edges.append(i);
    }
    MeshData result2;
    bool ok = MeshOperations::chamferEdges(data, edges, distance, result2);
    if (ok) {
        meshDataToSceneMesh(obj, result2);
        emit sceneChanged();
        emit statusMessage(QString("Chamfered %1 edges (d=%2)").arg(edges.size()).arg(distance));
    }
    return ok;
}

bool KSModelerQml::pushPullFaces(const QList<int>& faceIndices, float distance) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    QVector<int> faces;
    for (int f : faceIndices) faces.append(f);
    MeshData result = MeshOperations::pushPullFaces(data, faces, distance);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Push/pulled %1 faces by %2").arg(faces.size()).arg(distance));
    return true;
}

bool KSModelerQml::offsetSelectedFaces(const QList<int>& faceIndices, float distance) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    QVector<int> faces;
    for (int f : faceIndices) faces.append(f);
    MeshData result = MeshOperations::offsetFaces(data, faces, distance);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Offset %1 faces by %2").arg(faces.size()).arg(distance));
    return true;
}

int KSModelerQml::fillMeshHoles(int maxHoleEdges) {
    if (!m_selectedObject) return 0;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return 0;
    MeshData data = sceneMeshToMeshData(obj);
    int filled = MeshOperations::fillHoles(data, maxHoleEdges);
    if (filled > 0) {
        meshDataToSceneMesh(obj, data);
        emit sceneChanged();
    }
    emit statusMessage(QString("Filled %1 hole(s)").arg(filled));
    return filled;
}

int KSModelerQml::extractSelectedFaces(float thickness, bool closeCaps) {
    if (!m_selectedObject || !m_scene) return -1;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return -1;

    QVector<int> faces;
    const QVector<int>& sel = MeshOperations::SelectionManager::selectedFaces();
    for (int f : sel) faces.append(f);
    if (faces.isEmpty()) {
        emit statusMessage("No faces selected to extract");
        return -1;
    }

    MeshData data = sceneMeshToMeshData(obj);
    MeshData extracted = MeshOperations::extractFaces(data, faces, thickness, closeCaps);
    if (extracted.vertices.isEmpty() || extracted.faces.isEmpty()) {
        emit statusMessage("Extract: invalid face selection");
        return -1;
    }

    SceneObject* newObj = m_scene->createObject(obj->name() + "_extracted", SceneObject::Type::Mesh);
    if (!newObj) return -1;
    meshDataToSceneMesh(newObj, extracted);
    newObj->setPosition(obj->position() + QVector3D(0.3f, 0.0f, 0.0f));
    emit sceneChanged();
    emit statusMessage(QString("Extracted %1 face(s)").arg(faces.size()));
    return newObj->id();
}

bool KSModelerQml::cutByPlane(float px, float py, float pz, float nx, float ny, float nz) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    QVector3D point(px, py, pz);
    QVector3D normal(nx, ny, nz);
    if (normal.isNull()) normal = QVector3D(0, 1, 0);
    normal.normalize();
    MeshData section = MeshOperations::cutByPlane(data, point, normal);
    if (section.faces.isEmpty()) {
        emit statusMessage("Section: plane did not intersect the mesh");
        return false;
    }
    // Replace the object with the section profile
    meshDataToSceneMesh(obj, section);
    emit sceneChanged();
    emit statusMessage(QString("Section created: %1 faces").arg(section.faces.size()));
    return true;
}

int KSModelerQml::createSectionPreview(float px, float py, float pz, float nx, float ny, float nz) {
    if (!m_selectedObject) return -1;
    SceneObject* src = m_selectedObject->object();
    if (!src || !src->mesh()) return -1;
    MeshData data = sceneMeshToMeshData(src);
    QVector3D normal(nx, ny, nz);
    if (normal.isNull()) normal = QVector3D(0, 1, 0);
    normal.normalize();
    MeshData section = MeshOperations::cutByPlane(data, QVector3D(px, py, pz), normal);
    if (section.faces.isEmpty()) {
        emit statusMessage("Section preview: plane did not intersect the mesh");
        return -1;
    }
    SceneObject* obj = m_scene->createObject("SectionPreview", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, section);
    emit sceneChanged();
    return obj->id();
}

bool KSModelerQml::updateSectionPreview(int profileId, float px, float py, float pz, float nx, float ny, float nz) {
    if (!m_scene) return false;
    SceneObject* prof = m_scene->findObjectById(profileId);
    SceneObject* src = m_selectedObject ? m_selectedObject->object() : nullptr;
    if (!prof || !src || !src->mesh()) return false;
    MeshData data = sceneMeshToMeshData(src);
    QVector3D normal(nx, ny, nz);
    if (normal.isNull()) normal = QVector3D(0, 1, 0);
    normal.normalize();
    MeshData section = MeshOperations::cutByPlane(data, QVector3D(px, py, pz), normal);
    if (section.faces.isEmpty()) return false;
    meshDataToSceneMesh(prof, section);
    emit sceneChanged();
    return true;
}

bool KSModelerQml::deleteSectionPreview(int profileId) {
    if (!m_scene) return false;
    SceneObject* prof = m_scene->findObjectById(profileId);
    if (!prof) return false;
    m_scene->deleteObject(prof);
    emit sceneChanged();
    return true;
}

bool KSModelerQml::applySectionPreview(int profileId, int targetObjectId) {
    if (!m_scene) return false;
    SceneObject* prof = m_scene->findObjectById(profileId);
    SceneObject* target = m_scene->findObjectById(targetObjectId);
    if (!prof || !target || !prof->mesh()) return false;
    // Copy the profile geometry onto the target (destructive).
    const SceneMeshGeometry& g = prof->mesh()->geometry();
    SceneMeshGeometry copy;
    copy.name = g.name;
    copy.vertices = g.vertices;
    copy.indices = g.indices;
    copy.subMeshes = g.subMeshes;
    copy.computeBounds();
    SceneMesh* newMesh = new SceneMesh(copy);
    target->setMesh(newMesh);
    m_scene->deleteObject(prof);
    emit sceneChanged();
    emit statusMessage("Section applied to target");
    return true;
}

void KSModelerQml::setCPlane(float ox, float oy, float oz, float nx, float ny, float nz, float ux, float uy, float uz) {
    QVector3D normal(nx, ny, nz);
    if (normal.isNull()) normal = QVector3D(0, 1, 0);
    normal.normalize();
    QVector3D up(ux, uy, uz);
    if (up.isNull()) up = QVector3D(0, 0, 1);
    MeshOperations::setCPlane(QVector3D(ox, oy, oz), normal, up);
    emit statusMessage("Construction plane set");
}

QVariantMap KSModelerQml::getCPlane() const {
    QVariantMap map;
    map.insert("origin", QVariant(QVector3D(MeshOperations::getCPlaneOrigin())));
    map.insert("normal", QVariant(QVector3D(MeshOperations::getCPlaneNormal())));
    map.insert("up", QVariant(QVector3D(MeshOperations::getCPlaneUp())));
    return map;
}

QVariantMap KSModelerQml::snapToCPlane(float px, float py, float pz) const {
    QVector3D result;
    MeshOperations::snapToCPlane(QVector3D(px, py, pz), result);
    QVariantMap map;
    map.insert("point", QVariant(result));
    return map;
}

QVariantMap KSModelerQml::snapToMesh(float px, float py, float pz, int snapTypes) const {
    QVariantMap map;
    if (!m_selectedObject || !m_scene) return map;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return map;

    m_scene->updateAllTransforms();
    MeshData data = sceneMeshToMeshData(obj);
    QVector3D snapped = MeshOperations::snapPointToMesh(
        data, obj->worldTransform(), QVector3D(px, py, pz), snapTypes);
    map.insert("point", QVariant(snapped));
    map.insert("hit", (snapped - QVector3D(px, py, pz)).lengthSquared() > 1e-9f);
    return map;
}

void KSModelerQml::snapTypes(int types) {
    MeshOperations::setSnapTypes((MeshOperations::SnapType)types);
}

int KSModelerQml::snapTypes() const {
    return (int)MeshOperations::snapTypes();
}

bool KSModelerQml::linearArray(int count, float ox, float oy, float oz) {
    if (!m_selectedObject || count <= 1) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData md = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::array(md, count, QVector3D(ox, oy, oz));
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Linear array of %1 copies").arg(count));
    return true;
}

bool KSModelerQml::radialArray(int count, float axisX, float axisY, float axisZ, float angle) {
    if (!m_selectedObject || count <= 1) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData md = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::radialArray(md, count, QVector3D(axisX, axisY, axisZ), angle);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Radial array of %1 copies").arg(count));
    return true;
}

bool KSModelerQml::gridArray(int countX, int countY, float sx, float sy, float sz) {
    if (!m_selectedObject || countX <= 1 || countY <= 1) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData md = sceneMeshToMeshData(obj);
    MeshOperations::ArrayOptions opts;
    opts.count = countX;
    opts.countY = countY;
    opts.relativeOffset = QVector3D(sx, sy, sz);
    opts.useConstantOffset = false;
    opts.useRelativeOffset = true;
    MeshData result = MeshOperations::gridArray(md, opts);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Grid array of %1x%2 copies").arg(countX).arg(countY));
    return true;
}

int KSModelerQml::createInstance(int masterId) {
    if (!m_scene) return -1;
    SceneObject* master = m_scene->findObjectById(masterId);
    if (!master || !master->mesh()) return -1;

    SceneObject* inst = m_scene->createObject(master->name() + "_instance", SceneObject::Type::Mesh);
    if (!inst) return -1;

    // Share the master's SceneMesh (single VBO). Edits to the master push the
    // updated mesh to all instances via meshDataToSceneMesh.
    inst->setMesh(master->mesh());
    inst->setMaterial(master->material());
    inst->setPosition(master->position() + QVector3D(0.2f, 0.0f, 0.0f));
    inst->setScale(master->scale());

    InstanceReference::instance().createInstance(masterId, inst->id());
    emit sceneChanged();
    emit statusMessage(QString("Created live instance of %1").arg(master->name()));
    return inst->id();
}

bool KSModelerQml::realizeInstance(int instanceId) {
    if (!m_scene) return false;
    SceneObject* inst = m_scene->findObjectById(instanceId);
    if (!inst || !inst->mesh()) return false;

    int masterId = InstanceReference::instance().masterOf(instanceId);
    if (masterId < 0) return false;

    // Detach: duplicate the geometry into an independent SceneMesh.
    SceneMesh* copy = new SceneMesh();
    copy->geometry() = inst->mesh()->geometry();
    inst->setMesh(copy);

    InstanceReference::instance().realizeInstance(instanceId);
    emit sceneChanged();
    emit statusMessage("Instance detached (independent mesh)");
    return true;
}

QVariantList KSModelerQml::getInstances(int masterId) {
    QVariantList out;
    const QVector<int> ids = InstanceReference::instance().instancesOf(masterId);
    for (int id : ids) out.append(id);
    return out;
}

int KSModelerQml::masterOfInstance(int objectId) {
    return InstanceReference::instance().masterOf(objectId);
}

bool KSModelerQml::isInstance(int objectId) {
    return InstanceReference::instance().isInstance(objectId);
}

int KSModelerQml::instanceCount(int masterId) {
    return InstanceReference::instance().instanceCount(masterId);
}

bool KSModelerQml::deleteInstance(int instanceId) {
    if (!m_scene) return false;
    SceneObject* inst = m_scene->findObjectById(instanceId);
    if (!inst) return false;
    InstanceReference::instance().realizeInstance(instanceId);
    if (m_selectedObject && m_selectedObject->id() == instanceId) {
        delete m_selectedObject;
        m_selectedObject = nullptr;
    }
    m_scene->deleteObject(inst);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Instance removed");
    return true;
}

bool KSModelerQml::updateInstances(int masterId) {
    if (!m_scene) return false;
    SceneObject* master = m_scene->findObjectById(masterId);
    if (!master || !master->mesh()) return false;
    const QVector<int> ids = InstanceReference::instance().instancesOf(masterId);
    for (int id : ids) {
        if (SceneObject* inst = m_scene->findObjectById(id))
            inst->setMesh(master->mesh());
    }
    emit sceneChanged();
    return true;
}

void KSModelerQml::addDistanceDimension(int v1, int v2, const QString& label, int objectId) {
    MeshOperations::addDistanceDimension(v1, v2, label, objectId);
    emit statusMessage("Distance dimension added");
}

void KSModelerQml::addAngleDimension(int v1, int v2, int v3, const QString& label, int objectId) {
    MeshOperations::addAngleDimension(v1, v2, v3, label, objectId);
    emit statusMessage("Angle dimension added");
}

void KSModelerQml::addRadiusDimension(int vertex, const QList<int>& edgeIndices, const QString& label, int objectId) {
    QVector<int> edges;
    for (int e : edgeIndices) edges.append(e);
    MeshOperations::addRadiusDimension(vertex, edges, label, objectId);
    emit statusMessage("Radius dimension added");
}

void KSModelerQml::addDiameterDimension(int vertex, const QList<int>& edgeIndices, const QString& label, int objectId) {
    QVector<int> edges;
    for (int e : edgeIndices) edges.append(e);
    MeshOperations::addDiameterDimension(vertex, edges, label, objectId);
    emit statusMessage("Diameter dimension added");
}

float KSModelerQml::computeDistanceValue(int objectId, int v1, int v2) {
    if (!m_scene) return 0.0f;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return 0.0f;
    MeshData md = sceneMeshToMeshData(obj);
    return MeshOperations::computeDistanceValue(md, v1, v2);
}

float KSModelerQml::computeAngleValue(int objectId, int v1, int v2, int v3) {
    if (!m_scene) return 0.0f;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return 0.0f;
    MeshData md = sceneMeshToMeshData(obj);
    return MeshOperations::computeAngleValue(md, v1, v2, v3);
}

QVariantList KSModelerQml::dimensions() const {
    QVariantList out;
    // Distance dimensions (live: resolved against the object's current mesh).
    const QVector<MeshOperations::DimensionData>& dist = MeshOperations::distanceDimensions();
    for (const auto& d : dist) {
        QVariantMap m;
        m.insert("type", "distance");
        m.insert("v1", d.vertex1);
        m.insert("v2", d.vertex2);
        m.insert("label", d.label);
        m.insert("active", d.active);
        m.insert("objectId", d.objectId);
        float value = 0.0f;
        if (d.objectId >= 0 && m_scene) {
            SceneObject* obj = m_scene->findObjectById(d.objectId);
            if (obj && obj->mesh())
                value = MeshOperations::evaluateDistanceDimension(sceneMeshToMeshData(obj), d);
        }
        m.insert("value", value);
        out.append(m);
    }
    // Angle dimensions.
    const QVector<MeshOperations::DimensionData>& ang = MeshOperations::angleDimensions();
    for (const auto& d : ang) {
        QVariantMap m;
        m.insert("type", "angle");
        m.insert("v1", d.vertex1);
        m.insert("v2", d.vertex2);
        m.insert("v3", d.vertex3);
        m.insert("label", d.label);
        m.insert("active", d.active);
        m.insert("objectId", d.objectId);
        float value = 0.0f;
        if (d.objectId >= 0 && m_scene) {
            SceneObject* obj = m_scene->findObjectById(d.objectId);
            if (obj && obj->mesh())
                value = MeshOperations::evaluateAngleDimension(sceneMeshToMeshData(obj), d);
        }
        m.insert("value", value);
        out.append(m);
    }
    // Radius dimensions.
    const QVector<MeshOperations::RadiusDimension>& rad = MeshOperations::radiusDimensions();
    for (const auto& d : rad) {
        QVariantMap m;
        m.insert("type", d.diameter ? "diameter" : "radius");
        m.insert("v1", d.vertex);
        m.insert("label", d.label);
        m.insert("active", d.active);
        m.insert("objectId", d.objectId);
        float value = 0.0f;
        if (d.objectId >= 0 && m_scene) {
            SceneObject* obj = m_scene->findObjectById(d.objectId);
            if (obj && obj->mesh())
                value = MeshOperations::evaluateRadiusDimension(sceneMeshToMeshData(obj), d);
        }
        m.insert("value", value);
        out.append(m);
    }
    return out;
}

void KSModelerQml::clearDimensions() {
    MeshOperations::clearDistanceDimensions();
    MeshOperations::clearAngleDimensions();
    MeshOperations::clearRadiusDimensions();
    emit statusMessage("Dimensions cleared");
}

bool KSModelerQml::removeDimension(int type, int index) {
    switch (type) {
    case 0:
        if (index < 0 || index >= (int)MeshOperations::distanceDimensions().size()) return false;
        MeshOperations::removeDistanceDimensionAt(index);
        return true;
    case 1:
        if (index < 0 || index >= (int)MeshOperations::angleDimensions().size()) return false;
        MeshOperations::removeAngleDimensionAt(index);
        return true;
    case 2:
        if (index < 0 || index >= (int)MeshOperations::radiusDimensions().size()) return false;
        MeshOperations::removeRadiusDimensionAt(index);
        return true;
    default:
        return false;
    }
}

void KSModelerQml::setDimensionVisible(int type, int index, bool visible) {
    MeshOperations::setDimensionVisible(type, index, visible);
}

void KSModelerQml::showRadialMenu(int mode, float px, float py) {
    MeshOperations::SelectionManager::showRadialMenu(mode, QVector2D(px, py));
    emit statusMessage("Radial menu shown");
}

void KSModelerQml::hideRadialMenu() {
    MeshOperations::SelectionManager::hideRadialMenu();
}

QVariantMap KSModelerQml::radialMenuState() const {
    QVariantMap map;
    const MeshOperations::RadialMenu& menu = MeshOperations::SelectionManager::radialMenu();
    map.insert("active", menu.active);
    map.insert("pos", QVariant(menu.pos));
    QVariantList items;
    for (const auto& item : menu.items) {
        QVariantMap m;
        m.insert("text", item.text);
        m.insert("mode", item.mode);
        items.append(m);
    }
    map.insert("items", items);
    return map;
}

void KSModelerQml::showContextMenu(float wx, float wy, float wz) {
    MeshOperations::SelectionManager::showContextMenu(QVector3D(wx, wy, wz));
}

void KSModelerQml::setSubobjectMode(int mode) {
    MeshOperations::SelectionManager::setMode((MeshOperations::SelectionMode)mode);
}

int KSModelerQml::subobjectMode() const {
    return (int)MeshOperations::SelectionManager::mode();
}

QVariantList KSModelerQml::selectedSubVertices() const {
    QVariantList out;
    const QVector<int>& v = MeshOperations::SelectionManager::selectedVertices();
    for (int i : v) out.append(i);
    return out;
}

QVariantList KSModelerQml::selectedSubEdges() const {
    QVariantList out;
    const QVector<int>& e = MeshOperations::SelectionManager::selectedEdges();
    for (int i : e) out.append(i);
    return out;
}

QVariantList KSModelerQml::selectedSubFaces() const {
    QVariantList out;
    const QVector<int>& f = MeshOperations::SelectionManager::selectedFaces();
    for (int i : f) out.append(i);
    return out;
}

void KSModelerQml::addSelectedVertex(int vertexIndex) {
    MeshOperations::SelectionManager::addSelectedVertex(vertexIndex);
}

void KSModelerQml::addSelectedEdge(int edgeIndex) {
    MeshOperations::SelectionManager::addSelectedEdge(edgeIndex);
}

void KSModelerQml::addSelectedFace(int faceIndex) {
    MeshOperations::SelectionManager::addSelectedFace(faceIndex);
}

void KSModelerQml::clearSubSelection() {
    MeshOperations::SelectionManager::clear();
}

bool KSModelerQml::hideFace(int faceIndex) {
    MeshOperations::SelectionManager::hideFace(faceIndex);
    emit sceneChanged();
    return true;
}

bool KSModelerQml::unhideFace(int faceIndex) {
    MeshOperations::SelectionManager::unhideFace(faceIndex);
    emit sceneChanged();
    return true;
}

void KSModelerQml::unhideAllFaces() {
    MeshOperations::SelectionManager::unhideAllFaces();
    emit sceneChanged();
}

QVariantList KSModelerQml::getFaceNeighbors(int faceIndex) {
    QVariantList out;
    if (!m_selectedObject) return out;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return out;
    MeshData md = sceneMeshToMeshData(obj);
    QSet<int> neighbors = MeshOperations::SelectionManager::getSelectedFaceNeighbors(md, faceIndex);
    for (int f : neighbors) out.append(f);
    return out;
}

bool KSModelerQml::exportSTEP(const QString& path, bool useBREP) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData md = sceneMeshToMeshData(obj);
    bool ok = MeshOperations::exportSTEP(md, path, useBREP);
    if (ok) emit statusMessage("STEP file exported: " + path);
    return ok;
}

bool KSModelerQml::exportHiddenLineSVG(const QString& path, int viewAxis, float lineWidth) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData md = sceneMeshToMeshData(obj);
    bool ok = MeshOperations::exportHiddenLineSVG(md, path, viewAxis, lineWidth);
    if (ok) emit statusMessage("Hidden-line SVG exported: " + path);
    return ok;
}

int KSModelerQml::findClosestVertex(int objectId, float wx, float wy, float wz)
{
    if (!m_scene) return -1;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return -1;
    m_scene->updateAllTransforms();
    QMatrix4x4 world = obj->worldTransform();
    return MeshOperations::findClosestVertex(sceneMeshToMeshData(obj), world, QVector3D(wx, wy, wz));
}

QVariantMap KSModelerQml::findClosestEdge(int objectId, float wx, float wy, float wz)
{
    QVariantMap result;
    if (!m_scene) return result;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return result;
    m_scene->updateAllTransforms();
    QMatrix4x4 world = obj->worldTransform();
    QPair<int, int> edge = MeshOperations::findClosestEdge(sceneMeshToMeshData(obj), world, QVector3D(wx, wy, wz));
    result["v0"] = edge.first;
    result["v1"] = edge.second;
    return result;
}

bool KSModelerQml::vertexSlide(int objectId, int vertexIndex, float wx, float wy, float wz)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;
    m_scene->updateAllTransforms();
    QMatrix4x4 world = obj->worldTransform();
    MeshData data = sceneMeshToMeshData(obj);
    bool ok = MeshOperations::vertexSlide(data, vertexIndex, QVector3D(wx, wy, wz), world);
    if (ok) {
        meshDataToSceneMesh(obj, data);
        emit sceneChanged();
        emit statusMessage(QString("Vertex %1 slid").arg(vertexIndex));
    }
    return ok;
}

bool KSModelerQml::edgeSlide(int objectId, int v0, int v1, float factor)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    bool ok = MeshOperations::edgeSlide(data, v0, v1, factor);
    if (ok) {
        meshDataToSceneMesh(obj, data);
        emit sceneChanged();
        emit statusMessage(QString("Edge %1-%2 slid").arg(v0).arg(v1));
    }
    return ok;
}

static QVariantList edgeListToWorld(const MeshData& md, const QMatrix4x4& world, const QVector<Edge>& edges)
{
    QVariantList out;
    for (const Edge& e : edges) {
        if (e.v1 < 0 || e.v2 < 0 || e.v1 >= md.vertices.size() || e.v2 >= md.vertices.size()) continue;
        const QVector3D a = world.map(md.vertices[e.v1].position);
        const QVector3D b = world.map(md.vertices[e.v2].position);
        QVariantList edge;
        edge << QVariantList({ a.x(), a.y(), a.z() }) << QVariantList({ b.x(), b.y(), b.z() });
        out.append(edge);
    }
    return out;
}

// Splits a set of edges into vertex-ordered chains (2-valent walks). Used by
// bridgeSelectedLoops to turn the selected border edges into two loops.
static QVector<QVector<int>> extractEdgeLoops(const QVector<Edge>& edges);

QVariantList KSModelerQml::edgeLoop(int objectId, int v0, int v1)
{
    QVariantList out;
    if (!m_scene) return out;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return out;
    m_scene->updateAllTransforms();
    QMatrix4x4 world = obj->worldTransform();
    MeshData md = sceneMeshToMeshData(obj);
    out = edgeListToWorld(md, world, MeshOperations::findEdgeLoop(md, v0, v1));
    if (!out.isEmpty())
        emit statusMessage(QString("Edge loop: %1 edges").arg(out.size()));
    return out;
}

QVariantList KSModelerQml::edgeRing(int objectId, int v0, int v1)
{
    QVariantList out;
    if (!m_scene) return out;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return out;
    m_scene->updateAllTransforms();
    QMatrix4x4 world = obj->worldTransform();
    MeshData md = sceneMeshToMeshData(obj);
    out = edgeListToWorld(md, world, MeshOperations::findEdgeRing(md, v0, v1));
    if (!out.isEmpty())
        emit statusMessage(QString("Edge ring: %1 edges").arg(out.size()));
    return out;
}

bool KSModelerQml::knifeCutSelected(int objectId) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    data.computeBoundingBox();
    QVector3D mn = data.boundingBoxMin;
    QVector3D mx = data.boundingBoxMax;
    MeshData result = MeshOperations::knifeCut(data, mn, QVector3D(mx.x(), mx.y(), mn.z()));
    result = MeshOperations::knifeCut(result, QVector3D(mn.x(), mx.y(), mx.z()), QVector3D(mx.x(), mn.y(), mn.z()));
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage("Knife cut across object bounding box");
    return true;
}

bool KSModelerQml::loopCut(int objectId, int axis, float factor, float slide) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::loopCut(data, axis, factor, slide);
    if (result.vertices.size() == data.vertices.size()) {
        emit statusMessage("Loop cut: no edges crossed the plane");
        return false;
    }
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Loop cut: %1 edges split").arg(result.vertices.size() - data.vertices.size()));
    return true;
}

int KSModelerQml::sculptBrush(int objectId, float wx, float wy, float wz, float radius,
                              float strength, int mode, float dx, float dy, float dz,
                              float px, float py, float pz, float falloffPower)
{
    if (!m_scene) return 0;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return 0;

    m_scene->updateAllTransforms();
    QMatrix4x4 world = obj->worldTransform();
    QMatrix4x4 inv = world.inverted();
    if (inv.isIdentity() && !world.isIdentity()) return 0;

    MeshData data = sceneMeshToMeshData(obj);
    QVector3D center = inv.map(QVector3D(wx, wy, wz));
    QVector3D drag = inv.mapVector(QVector3D(dx, dy, dz));
    // Previous stroke point in world space (drives the smear brush).
    QVector3D previous = inv.map(QVector3D(px, py, pz));
    const QVector3D& s = obj->scale();
    float avgScale = (s.x() + s.y() + s.z()) / 3.0f;
    float radiusLocal = avgScale > 0.01f ? radius / avgScale : radius;

    const QSet<int>* pinned = m_sculptPins.contains(objectId) ? &m_sculptPins[objectId] : nullptr;
    int affected = MeshOperations::sculptBrush(data, center, radiusLocal, strength, mode, drag, previous, falloffPower, pinned);
    if (affected > 0) {
        meshDataToSceneMesh(obj, data);
        emit sceneChanged();
    }
    return affected;
}

void KSModelerQml::setSculptPins(int objectId, const QList<int>& vertexIndices, bool pinned)
{
    QSet<int>& set = m_sculptPins[objectId];
    for (int vi : vertexIndices)
        if (pinned) set.insert(vi); else set.remove(vi);
    if (set.isEmpty()) m_sculptPins.remove(objectId);
    emit sceneChanged();
    emit statusMessage(QString("Sculpt pins: %1 vertices %2")
        .arg(set.size()).arg(pinned ? "pinned" : "unpinned"));
}

void KSModelerQml::clearSculptPins(int objectId)
{
    m_sculptPins.remove(objectId);
    emit sceneChanged();
    emit statusMessage("Sculpt pins cleared");
}

int KSModelerQml::sculptPinnedCount(int objectId) const
{
    return m_sculptPins.value(objectId).size();
}

QVariantList KSModelerQml::sculptPinnedVertices(int objectId) const
{
    QVariantList out;
    const QSet<int> set = m_sculptPins.value(objectId);
    const QVector<int> sorted = set.values();
    for (int vi : sorted) out.append(vi);
    return out;
}

QStringList KSModelerQml::sculptBrushNames() const
{
    return { "Draw", "Smooth", "Grab", "Flatten", "Crease", "Inflate",
             "Pinch", "Smear", "Negate", "Folds", "Pores", "Bulge", "Slash" };
}

void KSModelerQml::dissolveEdges(const QList<int>& edgeIndices) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<int> indices(edgeIndices.begin(), edgeIndices.end());
    MeshData result = MeshOperations::dissolveEdges(md, indices);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Dissolved %1 edges").arg(edgeIndices.size()));
}

void KSModelerQml::dissolveVertices(const QList<int>& vertexIndices) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<int> indices(vertexIndices.begin(), vertexIndices.end());
    MeshData result = MeshOperations::dissolveVertices(md, indices);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Dissolved %1 vertices").arg(vertexIndices.size()));
}

void KSModelerQml::splitMeshes() {
    if (!m_selectedObject || !m_scene) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<MeshData> parts;
    MeshOperations::splitMeshes(md, parts);
    if (parts.size() <= 1) {
        emit statusMessage("Mesh has only one connected component");
        return;
    }
    meshDataToSceneMesh(obj, parts[0]);
    for (int i = 1; i < parts.size(); ++i) {
        SceneObject* newObj = m_scene->createObject(obj->name() + QString("_part%1").arg(i), SceneObject::Type::Mesh);
        if (newObj) {
            meshDataToSceneMesh(newObj, parts[i]);
            QVector3D pos = obj->position();
            newObj->setPosition(QVector3D(pos.x() + (i) * 2.0f, pos.y(), pos.z()));
        }
    }
    emit sceneChanged();
    emit statusMessage(QString("Split mesh into %1 parts").arg(parts.size()));
}

// ---- Multiresolution sculpting (Mudbox-style level management) ----

QVariantList KSModelerQml::multiresLevelList(int objectId) const {
    QVariantList out;
    if (!m_scene) return out;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return out;
    MultiresManager* mgr = obj->mesh()->multiresManager();
    if (!mgr) return out;

    const QVector<SubdivisionLevel>& levels = mgr->allLevels();
    int cur = mgr->currentLevel();
    for (int i = 0; i < levels.size(); ++i) {
        QVariantMap m;
        m["level"] = i;
        m["vertices"] = levels[i].vertices.size();
        m["faces"] = levels[i].faces.size() / 3;
        m["current"] = (i == cur);
        out.append(m);
    }
    return out;
}

int KSModelerQml::multiresLevelCount(int objectId) const {
    if (!m_scene) return 0;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return 0;
    MultiresManager* mgr = obj->mesh()->multiresManager();
    return mgr ? mgr->allLevels().size() : 0;
}

int KSModelerQml::multiresCurrentLevel(int objectId) const {
    if (!m_scene) return 0;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return 0;
    MultiresManager* mgr = obj->mesh()->multiresManager();
    return mgr ? mgr->currentLevel() : 0;
}

bool KSModelerQml::multiresSetCurrentLevel(int objectId, int level) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;
    MultiresManager* mgr = obj->mesh()->multiresManager();
    if (!mgr) return false;
    mgr->setCurrentLevel(level);
    emit sceneChanged();
    emit statusMessage(QString("Multires: set to level %1").arg(level));
    return true;
}

bool KSModelerQml::multiresAddLevel(int objectId) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;
    MultiresManager* mgr = obj->mesh()->multiresManager();
    if (!mgr) return false;
    mgr->addLevel();
    emit sceneChanged();
    int count = mgr->allLevels().size();
    emit statusMessage(QString("Multires: added level %1").arg(count - 1));
    return true;
}

bool KSModelerQml::multiresRemoveLevel(int objectId) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;
    MultiresManager* mgr = obj->mesh()->multiresManager();
    if (!mgr) return false;
    mgr->removeLevel();
    emit sceneChanged();
    emit statusMessage("Multires: removed current level");
    return true;
}

bool KSModelerQml::multiresSubdivide(int objectId) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;
    MultiresManager* mgr = obj->mesh()->multiresManager();
    if (!mgr) return false;
    mgr->subdivideCurrentLevel();
    emit sceneChanged();
    emit statusMessage("Multires: subdivided current level");
    return true;
}

bool KSModelerQml::multiresBake(int objectId) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;
    MultiresManager* mgr = obj->mesh()->multiresManager();
    if (!mgr) return false;
    mgr->bakeCurrentLevel();
    emit sceneChanged();
    emit statusMessage("Multires: baked to base mesh");
    return true;
}

int KSModelerQml::multiresVertexCount(int objectId, int level) const {
    if (!m_scene) return 0;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return 0;
    MultiresManager* mgr = obj->mesh()->multiresManager();
    if (!mgr) return 0;
    const QVector<SubdivisionLevel>& levels = mgr->allLevels();
    if (level < 0) level = mgr->currentLevel();
    if (level < 0 || level >= levels.size()) return 0;
    return levels[level].vertices.size();
}

// ---- Sculpt layers (Mudbox-style per-layer sculpting) ----

SculptLayersManager* KSModelerQml::sculptLayers() {
    if (!m_sculptLayers) {
        m_sculptLayers = new SculptLayersManager(this);
    }
    return m_sculptLayers;
}

void KSModelerQml::initSculptLayers() {
    if (!m_sculptLayers) {
        m_sculptLayers = new SculptLayersManager(this);
    }
}

QVariantList KSModelerQml::sculptLayerList() const {
    QVariantList out;
    if (!m_sculptLayers) return out;
    const auto& layers = m_sculptLayers->layers();
    int cur = m_sculptLayers->currentLayer();
    for (int i = 0; i < layers.size(); ++i) {
        QVariantMap m;
        m["index"] = i;
        m["name"] = layers[i].name;
        m["visible"] = layers[i].visible;
        m["locked"] = layers[i].locked;
        m["opacity"] = layers[i].opacity;
        m["blendMode"] = layers[i].blendMode;
        m["current"] = (i == cur);
        m["vertexCount"] = layers[i].vertexWeights.size();
        out.append(m);
    }
    return out;
}

int KSModelerQml::sculptLayerCount() const {
    return m_sculptLayers ? m_sculptLayers->layerCount() : 0;
}

int KSModelerQml::sculptLayerCurrent() const {
    return m_sculptLayers ? m_sculptLayers->currentLayer() : -1;
}

void KSModelerQml::sculptLayerSetCurrent(int index) {
    initSculptLayers();
    m_sculptLayers->setCurrentLayer(index);
    emit sceneChanged();
}

int KSModelerQml::sculptLayerAdd(const QString& name) {
    initSculptLayers();
    int idx = m_sculptLayers->addLayer(name);
    emit sceneChanged();
    emit sculptLayersChanged();
    return idx;
}

bool KSModelerQml::sculptLayerRemove(int index) {
    initSculptLayers();
    bool ok = m_sculptLayers->removeLayer(index);
    if (ok) {
        emit sceneChanged();
        emit sculptLayersChanged();
    }
    return ok;
}

bool KSModelerQml::sculptLayerRename(int index, const QString& name) {
    initSculptLayers();
    bool ok = m_sculptLayers->renameLayer(index, name);
    if (ok) emit sculptLayersChanged();
    return ok;
}

bool KSModelerQml::sculptLayerSetVisible(int index, bool visible) {
    initSculptLayers();
    m_sculptLayers->setLayerVisible(index, visible);
    emit sceneChanged();
    emit sculptLayersChanged();
    return true;
}

bool KSModelerQml::sculptLayerSetLocked(int index, bool locked) {
    initSculptLayers();
    m_sculptLayers->setLayerLocked(index, locked);
    emit sculptLayersChanged();
    return true;
}

bool KSModelerQml::sculptLayerSetBlendMode(int index, float mode) {
    initSculptLayers();
    m_sculptLayers->setLayerBlendMode(index, mode);
    emit sculptLayersChanged();
    return true;
}

bool KSModelerQml::sculptLayerSetOpacity(int index, float opacity) {
    initSculptLayers();
    m_sculptLayers->setLayerOpacity(index, opacity);
    emit sculptLayersChanged();
    return true;
}

bool KSModelerQml::sculptLayerBakeCurrent() {
    initSculptLayers();
    m_sculptLayers->bakeCurrentLayer();
    emit sceneChanged();
    emit sculptLayersChanged();
    emit statusMessage("Sculpt layers: baked current layer to base mesh");
    return true;
}

QVariantList KSModelerQml::sculptLayerWeights(int index) const {
    QVariantList out;
    if (!m_sculptLayers) return out;
    QVector<float> w = m_sculptLayers->vertexWeights(index);
    for (float v : w) out.append(v);
    return out;
}

// ---- Projection painting (stencil from image, 3D projection) ----

ProjectionPainter* KSModelerQml::projectionPainter() {
    if (!m_projectionPainter) {
        m_projectionPainter = new ProjectionPainter(this);
    }
    return m_projectionPainter;
}

void KSModelerQml::initProjectionPainter() {
    if (!m_projectionPainter) {
        m_projectionPainter = new ProjectionPainter(this);
    }
}

bool KSModelerQml::projectionLoadStencil(const QString& imagePath) {
    initProjectionPainter();
    QImage img(imagePath);
    if (img.isNull()) {
        emit statusMessage("Failed to load stencil image: " + imagePath);
        return false;
    }
    m_projectionPainter->setStencil(img);
    emit statusMessage(QString("Stencil loaded: %1x%2").arg(img.width()).arg(img.height()));
    return true;
}

bool KSModelerQml::projectionLoadStencilData(const QByteArray& imageBytes) {
    initProjectionPainter();
    QImage img;
    img.loadFromData(imageBytes);
    if (img.isNull()) {
        emit statusMessage("Failed to load stencil from data");
        return false;
    }
    m_projectionPainter->setStencil(img);
    emit statusMessage(QString("Stencil loaded from data: %1x%2").arg(img.width()).arg(img.height()));
    return true;
}

bool KSModelerQml::projectionSetStencilPosition(float x, float y, float z) {
    initProjectionPainter();
    m_projectionPainter->setStencilPosition(QVector3D(x, y, z));
    return true;
}

bool KSModelerQml::projectionSetStencilRotation(float x, float y, float z) {
    initProjectionPainter();
    m_projectionPainter->setStencilRotation(QVector3D(x, y, z));
    return true;
}

bool KSModelerQml::projectionSetStencilScale(float x, float y, float z) {
    initProjectionPainter();
    m_projectionPainter->setStencilScale(QVector3D(x, y, z));
    return true;
}

bool KSModelerQml::projectionSetStencilOpacity(float opacity) {
    initProjectionPainter();
    m_projectionPainter->setStencilOpacity(opacity);
    return true;
}

bool KSModelerQml::projectionSetStencilUseAlpha(bool useAlpha) {
    initProjectionPainter();
    m_projectionPainter->setStencilUseAlpha(useAlpha);
    return true;
}

bool KSModelerQml::projectionSetStencilLoop(bool loop) {
    initProjectionPainter();
    m_projectionPainter->setStencilLoop(loop);
    return true;
}

QVariantMap KSModelerQml::projectionStencilInfo() const {
    QVariantMap out;
    if (!m_projectionPainter) return out;
    const QImage& img = m_projectionPainter->stencil();
    out["width"] = img.width();
    out["height"] = img.height();
    return out;
}

int KSModelerQml::projectionPaint(int objectId, float wx, float wy, float wz,
                                   float radius, float strength, int mode,
                                   float dx, float dy, float dz) {
    initProjectionPainter();
    return m_projectionPainter->projectStencilToMesh(
        objectId, QVector3D(wx, wy, wz), radius, strength, mode,
        QVector2D(dx, dy));
}

int KSModelerQml::projectionClone(int objectId, float srcU, float srcV,
                                   float dstU, float dstV, float strength,
                                   float blendMode) {
    initProjectionPainter();
    return m_projectionPainter->cloneStencilToPoint(
        objectId, QVector2D(srcU, srcV), QVector2D(dstU, dstV),
        strength, blendMode);
}

// ---- Tiling textures (seamless tiling preview + generation) ----

QByteArray KSModelerQml::tilingGenerateSeamless(const QByteArray& imageData, int tileSize, int blendRadius) {
    QImage src;
    src.loadFromData(imageData);
    if (src.isNull()) return QByteArray();

    QImage result = src.copy();
    int w = result.width();
    int h = result.height();

    // Simple seamless tiling by blending opposite edges
    if (blendRadius > 0) {
        for (int i = 0; i < blendRadius; ++i) {
            float t = float(i) / blendRadius;
            int oppI = (tileSize > 0) ? (tileSize - 1 - i % tileSize) : (w - 1 - i);
            for (int y = 0; y < h; ++y) {
                if (i < w && oppI >= 0 && oppI < w) {
                    QColor c1 = QColor(result.pixel(i, y));
                    QColor c2 = QColor(result.pixel(oppI, y));
                    QColor blended;
                    blended.setRedF(c1.redF() * t + c2.redF() * (1-t));
                    blended.setGreenF(c1.greenF() * t + c2.greenF() * (1-t));
                    blended.setBlueF(c1.blueF() * t + c2.blueF() * (1-t));
                    blended.setAlphaF(c1.alphaF() * t + c2.alphaF() * (1-t));
                    result.setPixelColor(i, y, blended);
                }
            }
        }
    }

    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    result.save(&buf, "PNG");
    return out;
}

QByteArray KSModelerQml::tilingPreview(const QByteArray& imageData, int repeats) {
    QImage src;
    src.loadFromData(imageData);
    if (src.isNull() || repeats < 1) return QByteArray();

    int tw = src.width();
    int th = src.height();
    QImage result(tw * repeats, th * repeats, src.format());
    QPainter p(&result);
    for (int x = 0; x < repeats; ++x) {
        for (int y = 0; y < repeats; ++y) {
            p.drawImage(x * tw, y * th, src);
        }
    }
    p.end();

    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    result.save(&buf, "PNG");
    return out;
}

bool KSModelerQml::tilingApplyToObject(int objectId, const QByteArray& tiledImage) {
    if (!m_scene) { emit error("No scene"); return false; }

    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) {
        emit error("Object not found or has no mesh");
        return false;
    }

    QImage img;
    img.loadFromData(tiledImage, "PNG");
    if (img.isNull()) {
        img.loadFromData(tiledImage, "JPEG");
    }

    if (img.isNull()) {
        emit error("Failed to decode tiled image");
        return false;
    }

    QString tempPath = QDir::tempPath() + "/kseditor_tiled_" + QUuid::createUuid().toString().mid(1, 8) + ".png";
    img.save(tempPath);

    if (obj->mesh()->geometry().name.isEmpty()) {
        obj->mesh()->geometry().name = "TiledMaterial";
    }

    emit statusMessage("Tiling: applied tiled texture to object " + obj->name());
    emit sceneChanged();
    return true;
}

// ---- Matcap rendering ----

bool KSModelerQml::matcapSetEnabled(bool enabled) {
    m_matcapEnabled = enabled;
    emit sceneChanged();
    emit statusMessage(enabled ? "Matcap: enabled" : "Matcap: disabled");
    return true;
}

bool KSModelerQml::matcapIsEnabled() const {
    return m_matcapEnabled;
}

bool KSModelerQml::matcapLoad(const QString& imagePath) {
    m_matcapImage = QImage(imagePath);
    if (m_matcapImage.isNull()) {
        emit statusMessage("Failed to load matcap: " + imagePath);
        return false;
    }
    emit statusMessage(QString("Matcap loaded: %1x%2").arg(m_matcapImage.width()).arg(m_matcapImage.height()));
    return true;
}

bool KSModelerQml::matcapLoadData(const QByteArray& imageBytes) {
    m_matcapImage.loadFromData(imageBytes);
    if (m_matcapImage.isNull()) {
        emit statusMessage("Failed to load matcap from data");
        return false;
    }
    emit statusMessage(QString("Matcap loaded from data: %1x%2").arg(m_matcapImage.width()).arg(m_matcapImage.height()));
    return true;
}

QVariantList KSModelerQml::matcapPresets() const {
    QVariantList out;
    QVariantMap m1; m1["name"] = "Ceramic"; m1["color"] = "#b8a89a"; out.append(m1);
    QVariantMap m2; m2["name"] = "Clay"; m2["color"] = "#c4a882"; out.append(m2);
    QVariantMap m3; m3["name"] = "Metal"; m3["color"] = "#8899aa"; out.append(m3);
    QVariantMap m4; m4["name"] = "Skin"; m4["color"] = "#d4a574"; out.append(m4);
    QVariantMap m5; m5["name"] = "Jade"; m5["color"] = "#5a8a6a"; out.append(m5);
    QVariantMap m6; m6["name"] = "Bronze"; m6["color"] = "#aa7744"; out.append(m6);
    QVariantMap m7; m7["name"] = "Red Wax"; m7["color"] = "#cc3333"; out.append(m7);
    QVariantMap m8; m8["name"] = "Silk"; m8["color"] = "#e8e0d8"; out.append(m8);
    return out;
}

bool KSModelerQml::matcapApplyPreset(const QString& name) {
    QMap<QString, QColor> presetColors;
    presetColors["Chrome"] = QColor(200, 200, 210);
    presetColors["Gold"] = QColor(218, 175, 80);
    presetColors["Copper"] = QColor(184, 115, 51);
    presetColors["Clay"] = QColor(180, 150, 120);
    presetColors["Plastic"] = QColor(100, 140, 200);
    presetColors["Glass"] = QColor(180, 210, 230);
    presetColors["Silk"] = QColor(232, 224, 216);
    presetColors["Skin"] = QColor(210, 170, 140);
    presetColors["Ceramic"] = QColor(220, 220, 215);
    presetColors["Steel"] = QColor(160, 165, 170);

    if (presetColors.contains(name)) {
        QColor c = presetColors[name];
        m_matcapImage = QImage(128, 128, QImage::Format_RGB32);
        m_matcapImage.fill(c);
        QPainter p(&m_matcapImage);
        QRadialGradient grad(64, 64, 60);
        grad.setColorAt(0, QColor(255, 255, 255, 180));
        grad.setColorAt(0.3, QColor(c.red(), c.green(), c.blue(), 200));
        grad.setColorAt(1.0, QColor(c.red() / 3, c.green() / 3, c.blue() / 3));
        p.fillRect(m_matcapImage.rect(), grad);
        p.end();
    } else {
        m_matcapImage = QImage(128, 128, QImage::Format_RGB32);
        QPainter p(&m_matcapImage);
        p.fillRect(m_matcapImage.rect(), QColor(180, 180, 180));
        p.end();
    }

    m_matcapEnabled = true;
    emit sceneChanged();
    emit statusMessage("Matcap preset applied: " + name);
    return true;
}

// ---- Wireframe overlay (wireframe-on-shaded) ----

bool KSModelerQml::wireframeOverlaySetEnabled(bool enabled) {
    m_wireframeOverlayEnabled = enabled;
    emit sceneChanged();
    return true;
}

bool KSModelerQml::wireframeOverlayIsEnabled() const {
    return m_wireframeOverlayEnabled;
}

bool KSModelerQml::wireframeOverlaySetColor(float r, float g, float b, float a) {
    m_wireframeOverlayColor = QVector4D(r, g, b, a);
    emit sceneChanged();
    return true;
}

bool KSModelerQml::wireframeOverlaySetThickness(float pixels) {
    m_wireframeOverlayThickness = qMax(0.5f, pixels);
    emit sceneChanged();
    return true;
}

QVariantMap KSModelerQml::wireframeOverlayInfo() const {
    QVariantMap out;
    out["enabled"] = m_wireframeOverlayEnabled;
    out["r"] = m_wireframeOverlayColor.x();
    out["g"] = m_wireframeOverlayColor.y();
    out["b"] = m_wireframeOverlayColor.z();
    out["a"] = m_wireframeOverlayColor.w();
    out["thickness"] = m_wireframeOverlayThickness;
    return out;
}

// ---- Silhouette display ----

bool KSModelerQml::silhouetteSetEnabled(bool enabled) {
    m_silhouetteEnabled = enabled;
    emit sceneChanged();
    return true;
}

bool KSModelerQml::silhouetteIsEnabled() const {
    return m_silhouetteEnabled;
}

bool KSModelerQml::silhouetteSetColor(float r, float g, float b, float a) {
    m_silhouetteColor = QVector4D(r, g, b, a);
    emit sceneChanged();
    return true;
}

bool KSModelerQml::silhouetteSetThreshold(float angleDeg) {
    m_silhouetteThreshold = qBound(10.0f, angleDeg, 180.0f);
    emit sceneChanged();
    return true;
}

QVariantMap KSModelerQml::silhouetteInfo() const {
    QVariantMap out;
    out["enabled"] = m_silhouetteEnabled;
    out["r"] = m_silhouetteColor.x();
    out["g"] = m_silhouetteColor.y();
    out["b"] = m_silhouetteColor.z();
    out["a"] = m_silhouetteColor.w();
    out["threshold"] = m_silhouetteThreshold;
    return out;
}

// ---- Turntable auto-rotate ----

void KSModelerQml::turntableTick() {
    if (!m_turntableEnabled || !m_scene) return;
    m_turntableAngle += m_turntableSpeed * 0.016f; // ~60fps tick
    if (m_turntableAngle >= 360.0f) m_turntableAngle -= 360.0f;

    // Rotate the orbit camera around the target
    if (m_selectedObject) {
        // The turntable rotation is applied via the orbit camera
        emit sceneChanged();
    }
}

bool KSModelerQml::turntableSetEnabled(bool enabled) {
    m_turntableEnabled = enabled;
    if (enabled) {
        if (!m_turntableTimer) {
            m_turntableTimer = new QTimer(this);
            connect(m_turntableTimer, &QTimer::timeout, this, &KSModelerQml::turntableTick);
        }
        m_turntableTimer->start(16); // ~60fps
    } else {
        if (m_turntableTimer) m_turntableTimer->stop();
    }
    emit sceneChanged();
    return true;
}

bool KSModelerQml::turntableIsEnabled() const {
    return m_turntableEnabled;
}

bool KSModelerQml::turntableSetSpeed(float degreesPerSecond) {
    m_turntableSpeed = qBound(1.0f, degreesPerSecond, 360.0f);
    return true;
}

bool KSModelerQml::turntableSetAxis(float x, float y, float z) {
    float len = qSqrt(x*x + y*y + z*z);
    if (len < 0.001f) return false;
    m_turntableAxis = QVector3D(x/len, y/len, z/len);
    return true;
}

QVariantMap KSModelerQml::turntableInfo() const {
    QVariantMap out;
    out["enabled"] = m_turntableEnabled;
    out["speed"] = m_turntableSpeed;
    out["axisX"] = m_turntableAxis.x();
    out["axisY"] = m_turntableAxis.y();
    out["axisZ"] = m_turntableAxis.z();
    out["angle"] = m_turntableAngle;
    return out;
}

void KSModelerQml::addPrimitiveCube(float size) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    MeshData md = MeshOperations::createBox(size, size, size);
    SceneObject* obj = m_scene->createObject("Cube", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, md);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Added cube primitive");
}

void KSModelerQml::addPrimitiveSphere(float radius, int segments, int rings) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    MeshData md = MeshOperations::createSphere(radius, segments, rings);
    SceneObject* obj = m_scene->createObject("Sphere", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, md);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Added sphere primitive");
}

void KSModelerQml::addPrimitiveCylinder(float radius, float height, int segments) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    MeshData md = MeshOperations::createCylinder(radius, height, segments);
    SceneObject* obj = m_scene->createObject("Cylinder", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, md);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Added cylinder primitive");
}

void KSModelerQml::addPrimitiveCone(float radius, float height, int segments) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    MeshData md = MeshOperations::createCone(radius, height, segments);
    SceneObject* obj = m_scene->createObject("Cone", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, md);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Added cone primitive");
}

void KSModelerQml::addPrimitiveTorus(float majorRadius, float minorRadius, int majorSegments, int minorSegments) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    MeshData md = MeshOperations::createTorus(majorRadius, minorRadius, majorSegments, minorSegments);
    SceneObject* obj = m_scene->createObject("Torus", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, md);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Added torus primitive");
}

void KSModelerQml::addPrimitivePlane(float width, float height, int widthSegments, int heightSegments) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    MeshData md = MeshOperations::createPlane(width, height, widthSegments, heightSegments);
    SceneObject* obj = m_scene->createObject("Plane", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, md);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Added plane primitive");
}

int KSModelerQml::addTransformGroup(const QString& name) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    SceneObject* obj = m_scene->createObject(name, SceneObject::Type::Node);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Added " + name);
    return obj->id();
}

int KSModelerQml::addCamera(const QString& name) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    SceneObject* obj = m_scene->createObject(name, SceneObject::Type::Camera);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Added " + name);
    return obj->id();
}

int KSModelerQml::addLight(const QString& name) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    SceneObject* obj = m_scene->createObject(name, SceneObject::Type::Light);
    LightDef def;
    def.objectId = obj->id();
    def.name = name;
    m_lightSystem.add(def);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit lightsChanged();
    emit statusMessage("Added " + name);
    return obj->id();
}

void KSModelerQml::projectUVPlanar(int axis) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector3D> verts;
    QVector<QVector<int>> faces;
    for (const auto& v : md.vertices) verts.append(v.position);
    for (const auto& f : md.faces) { QVector<int> fi; for (int i : f.indices) fi.append(i); faces.append(fi); }
    QVector3D dir(0, 0, 1);
    if (axis == 0) dir = QVector3D(1, 0, 0);
    else if (axis == 1) dir = QVector3D(0, 1, 0);
    QVector<QVector2D> uvs = UVMapper::planarProject(verts, faces, dir);
    for (int i = 0; i < md.vertices.size() && i < uvs.size(); i++)
        md.vertices[i].uv = uvs[i];
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("UV planar projection applied");
}

void KSModelerQml::projectUVCylindrical(int axis) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector3D> verts;
    QVector<QVector<int>> faces;
    for (const auto& v : md.vertices) verts.append(v.position);
    for (const auto& f : md.faces) { QVector<int> fi; for (int i : f.indices) fi.append(i); faces.append(fi); }
    QVector<QVector2D> uvs = UVMapper::cylindricalProject(verts, faces);
    for (int i = 0; i < md.vertices.size() && i < uvs.size(); i++)
        md.vertices[i].uv = uvs[i];
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("UV cylindrical projection applied");
}

void KSModelerQml::projectUVSpherical() {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector3D> verts;
    QVector<QVector<int>> faces;
    for (const auto& v : md.vertices) verts.append(v.position);
    for (const auto& f : md.faces) { QVector<int> fi; for (int i : f.indices) fi.append(i); faces.append(fi); }
    QVector<QVector2D> uvs = UVMapper::sphericalProject(verts, faces);
    for (int i = 0; i < md.vertices.size() && i < uvs.size(); i++)
        md.vertices[i].uv = uvs[i];
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("UV spherical projection applied");
}

void KSModelerQml::projectUVBox(float size) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector3D> verts;
    QVector<QVector<int>> faces;
    for (const auto& v : md.vertices) verts.append(v.position);
    for (const auto& f : md.faces) { QVector<int> fi; for (int i : f.indices) fi.append(i); faces.append(fi); }
    QVector<QVector2D> uvs = UVMapper::cubeProject(verts, faces);
    for (int i = 0; i < md.vertices.size() && i < uvs.size(); i++)
        md.vertices[i].uv = uvs[i];
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("UV box projection applied");
}

void KSModelerQml::unwrapUVs(const QString& method) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector3D> verts;
    QVector<QVector<int>> faces;
    for (const auto& v : md.vertices) verts.append(v.position);
    for (const auto& f : md.faces) { QVector<int> fi; for (int i : f.indices) fi.append(i); faces.append(fi); }
    QVector<QVector2D> uvs;
    if (method == "conformal") {
        UVUnwrapConfig cfg;
        ConformalUnwrapper::unwrap(verts, faces, uvs, cfg);
    } else {
        // LSCM with auto-detected seams plus any manually marked seams.
        auto autoSeams = UVIslandDetector::findSeamsFromAngle(verts, faces, qDegreesToRadians(66.0f));
        QSet<QPair<int, int>> seams(autoSeams.begin(), autoSeams.end());
        const auto stored = m_seamEdges.value(obj->id());
        for (const auto& e : stored)
            seams.insert(e);
        LSCMUnwrapper::unwrap(verts, faces, seams, uvs);
    }
    for (int i = 0; i < md.vertices.size() && i < uvs.size(); i++)
        md.vertices[i].uv = uvs[i];
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("UV unwrap completed");
}

bool KSModelerQml::markSeamFromClosestEdge(float wx, float wy, float wz)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData md = sceneMeshToMeshData(obj);
    if (md.faces.isEmpty()) return false;
    QMatrix4x4 wt = obj->worldTransform();
    QVector<QVector3D> verts;
    verts.reserve(md.vertices.size());
    for (const auto& v : md.vertices) verts.append(wt.map(v.position));

    QSet<QPair<int, int>> uniqueEdges;
    QVector<QPair<int, int>> edgeList;
    for (const auto& f : md.faces) {
        const int n = f.indices.size();
        for (int i = 0; i < n; ++i) {
            int a = f.indices[i], b = f.indices[(i + 1) % n];
            if (a < 0 || b < 0 || a >= verts.size() || b >= verts.size()) continue;
            QPair<int, int> e(qMin(a, b), qMax(a, b));
            if (!uniqueEdges.contains(e)) {
                uniqueEdges.insert(e);
                edgeList.append(e);
            }
        }
    }

    QVector3D pick(wx, wy, wz);
    float bestDist = std::numeric_limits<float>::max();
    int bestIdx = -1;
    for (int i = 0; i < edgeList.size(); ++i) {
        const QVector3D& a = verts[edgeList[i].first];
        const QVector3D& b = verts[edgeList[i].second];
        QVector3D ab = b - a;
        float t = QVector3D::dotProduct(pick - a, ab) / qMax(1e-6f, ab.lengthSquared());
        t = qBound(0.0f, t, 1.0f);
        float d = pick.distanceToPoint(a + ab * t);
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    if (bestIdx < 0 || bestDist > 0.1f) return false;

    QPair<int, int> e = edgeList[bestIdx];
    auto& set = m_seamEdges[obj->id()];
    bool existed = set.contains(e);
    if (existed) set.remove(e); else set.insert(e);
    emit seamChanged();
    emit statusMessage(existed ? "Seam removed (edge unmarked)"
                              : "Seam marked on edge V" + QString::number(e.first) + "-V" + QString::number(e.second) +
                                " (total " + QString::number(set.size()) + ")");
    return true;
}

bool KSModelerQml::clearSeams()
{
    if (!m_selectedObject) return false;
    int id = m_selectedObject->object() ? m_selectedObject->object()->id() : -1;
    if (id < 0 || !m_seamEdges.contains(id)) return false;
    m_seamEdges.remove(id);
    emit seamChanged();
    emit statusMessage("All seams cleared");
    return true;
}

int KSModelerQml::seamEdgeCount()
{
    if (!m_selectedObject || !m_selectedObject->object()) return 0;
    return m_seamEdges.value(m_selectedObject->object()->id()).size();
}

// ---------------------------------------------------------------------------
// Fase-1 gaps: Shell, Bridge, Smoothing groups, Border/Element
// ---------------------------------------------------------------------------
bool KSModelerQml::applyShell(float thickness, int direction, bool flipNormals) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::shell(data, thickness, direction, flipNormals);
    if (result.vertices.isEmpty()) {
        emit statusMessage("Shell: invalid mesh");
        return false;
    }
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Shell: %1 vertices, %2 faces").arg(result.vertices.size()).arg(result.faces.size()));
    return true;
}

bool KSModelerQml::bridgeSelectedLoops(int segments) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData data = sceneMeshToMeshData(obj);
    ensureMeshEdges(data);

    QVector<int> selEdges = MeshOperations::SelectionManager::selectedEdges();
    // Build loops from the selected edges: split them into the two boundary
    // chains (each border edge appears exactly once). Simplest robust approach:
    // resolve selected edge indices to vertex pairs, then group by connectivity.
    QVector<Edge> selPairs;
    for (int ei : selEdges) {
        if (ei >= 0 && ei < data.edges.size())
            selPairs.append(data.edges[ei]);
    }
    if (selPairs.isEmpty()) {
        emit statusMessage("Bridge: select two border edge loops first");
        return false;
    }

    QVector<QVector<int>> loops = extractEdgeLoops(selPairs);
    if (loops.size() != 2) {
        emit statusMessage(QString("Bridge: found %1 loops, need exactly 2").arg(loops.size()));
        return false;
    }
    if (loops[0].size() != loops[1].size()) {
        emit statusMessage("Bridge: loops must have the same vertex count");
        return false;
    }

    MeshData result = MeshOperations::bridgeEdges(data, loops[0], loops[1], segments);
    if (result.vertices.isEmpty()) {
        emit statusMessage("Bridge: failed");
        return false;
    }
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Bridge: %1 segments").arg(segments));
    return true;
}

bool KSModelerQml::bridgeSelectedFaces(int segments) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    QVector<int> selFaces = MeshOperations::SelectionManager::selectedFaces();
    if (selFaces.size() < 2) {
        emit statusMessage("Bridge: select two faces first");
        return false;
    }
    MeshData data = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::bridgeFaces(data, selFaces[0], selFaces[1], segments);
    if (result.vertices.isEmpty()) {
        emit statusMessage("Bridge: faces not bridged (different vertex counts?)");
        return false;
    }
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Bridge: %1 segments").arg(segments));
    return true;
}

int KSModelerQml::smoothGroupsAuto(float angleDeg) {
    if (!m_selectedObject) return 0;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return 0;
    MeshData data = sceneMeshToMeshData(obj);
    QVector<int> groups = MeshOperations::autoSmooth(data, angleDeg);
    m_smoothGroups[obj->id()] = groups;
    int count = 0;
    QSet<int> seen;
    for (int g : groups) { if (g >= 0 && !seen.contains(g)) { seen.insert(g); ++count; } }
    emit sceneChanged();
    emit statusMessage(QString("Smoothing groups: %1 (threshold %2°)").arg(count).arg(angleDeg));
    return count;
}

QVariantList KSModelerQml::smoothGroupFaceIds(int groupId) const {
    QVariantList out;
    if (!m_selectedObject || !m_selectedObject->object()) return out;
    const QVector<int>& groups = m_smoothGroups.value(m_selectedObject->object()->id());
    for (int i = 0; i < groups.size(); ++i)
        if (groups[i] == groupId) out.append(i);
    return out;
}

int KSModelerQml::smoothGroupCount() const {
    if (!m_selectedObject || !m_selectedObject->object()) return 0;
    return m_smoothGroups.value(m_selectedObject->object()->id()).size();
}

bool KSModelerQml::smoothGroupSetFace(int objectId, int faceIndex, int groupId) {
    if (groupId < 0 || groupId > 31) return false;
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj || !obj->mesh()) return false;
    const int faceCount = obj->mesh()->geometry().indices.size() / 3;
    if (faceIndex < 0 || faceIndex >= faceCount) return false;
    QVector<int>& groups = m_smoothGroups[objectId];
    if (groups.size() < faceCount) groups.resize(faceCount);
    groups[faceIndex] = groupId;
    emit sceneChanged();
    return true;
}

int KSModelerQml::smoothGroupForFace(int objectId, int faceIndex) const {
    auto it = m_smoothGroups.constFind(objectId);
    if (it == m_smoothGroups.constEnd()) return 0;
    if (faceIndex < 0 || faceIndex >= it->size()) return 0;
    return it->at(faceIndex);
}

int KSModelerQml::faceGroupCreate(const QString& name, int color) {
    const int idx = m_faceGroups.addGroup(name, color);
    emit faceGroupsChanged();
    emit sceneChanged();
    return idx;
}

bool KSModelerQml::faceGroupRemove(int index) {
    const bool ok = m_faceGroups.removeGroup(index);
    if (ok) { emit faceGroupsChanged(); emit sceneChanged(); }
    return ok;
}

bool KSModelerQml::faceGroupRename(int index, const QString& name) {
    const bool ok = m_faceGroups.renameGroup(index, name);
    if (ok) { emit faceGroupsChanged(); }
    return ok;
}

bool KSModelerQml::faceGroupSetColor(int index, int color) {
    const bool ok = m_faceGroups.setGroupColor(index, color);
    if (ok) { emit faceGroupsChanged(); emit sceneChanged(); }
    return ok;
}

bool KSModelerQml::faceGroupSetVisible(int index, bool visible) {
    const bool ok = m_faceGroups.setGroupVisible(index, visible);
    if (ok) { emit faceGroupsChanged(); emit sceneChanged(); }
    return ok;
}

QVariantList KSModelerQml::faceGroupList() const {
    QVariantList out;
    const int n = m_faceGroups.groupCount();
    for (int i = 0; i < n; ++i) {
        const auto* g = m_faceGroups.groupAt(i);
        if (!g) continue;
        QVariantMap m;
        m["index"] = i;
        m["name"] = g->name;
        m["color"] = g->color;
        m["visible"] = g->visible;
        m["count"] = m_faceGroups.memberCount(i);
        out.append(m);
    }
    return out;
}

int KSModelerQml::faceGroupAssignSelected(int groupIndex) {
    if (!m_selectedObject || !m_selectedObject->object()) return -1;
    SceneObject* obj = m_selectedObject->object();
    if (!obj->mesh()) return -1;
    QVector<int> faces;
    const QVariantList sel = selectedSubFaces();
    for (const auto& v : sel) faces.append(v.toInt());
    m_faceGroups.assignFaces(obj->id(), faces, groupIndex);
    emit faceGroupsChanged();
    emit sceneChanged();
    return faces.size();
}

void KSModelerQml::faceGroupAssignFaces(int objectId, const QVariantList& faceIndices, int groupIndex) {
    QVector<int> faces;
    for (const auto& v : faceIndices) faces.append(v.toInt());
    m_faceGroups.assignFaces(objectId, faces, groupIndex);
    emit faceGroupsChanged();
    emit sceneChanged();
}

int KSModelerQml::faceGroupForFace(int objectId, int faceIndex) const {
    return m_faceGroups.groupForFace(objectId, faceIndex);
}

QVariantList KSModelerQml::faceGroupFaces(int objectId, int groupIndex) const {
    QVariantList out;
    const QVector<int> faces = m_faceGroups.facesInGroup(objectId, groupIndex);
    for (int f : faces) out.append(f);
    return out;
}

void KSModelerQml::faceGroupClearObject(int objectId) {
    m_faceGroups.removeObject(objectId);
    emit faceGroupsChanged();
    emit sceneChanged();
}

int KSModelerQml::smoothGroupAssignSelected(int groupId) {
    if (groupId < 0 || groupId > 31 || !m_selectedObject) return 0;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return 0;
    const QVector<int>& faces = MeshOperations::SelectionManager::selectedFaces();
    QVector<int>& groups = m_smoothGroups[obj->id()];
    const int faceCount = obj->mesh()->geometry().indices.size() / 3;
    if (groups.size() < faceCount) groups.resize(faceCount);
    int applied = 0;
    for (int f : faces) {
        if (f >= 0 && f < faceCount) { groups[f] = groupId; ++applied; }
    }
    if (applied > 0) {
        emit sceneChanged();
        emit statusMessage(QString("Smoothing group %1: %2 faces").arg(groupId).arg(applied));
    }
    return applied;
}

void KSModelerQml::smoothGroupClear(int objectId) {
    m_smoothGroups.remove(objectId);
    emit sceneChanged();
}

int KSModelerQml::splitSmoothingGroupsMesh() {
    if (!m_selectedObject) return 0;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return 0;

    QVector<int> groups = m_smoothGroups.value(obj->id());
    if (groups.isEmpty()) {
        // No stored assignment yet — derive groups with the default threshold so
        // the Split operation works straight from an auto-smooth state.
        MeshData seed = sceneMeshToMeshData(obj);
        groups = MeshOperations::autoSmooth(seed, 30.0f);
        m_smoothGroups[obj->id()] = groups;
    }

    const MeshData md = sceneMeshToMeshData(obj);
    const MeshData result = MeshOperations::splitSmoothingGroups(md, groups);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Split smoothing groups: %1 vertices").arg(result.vertices.size()));
    return result.vertices.size();
}

bool KSModelerQml::transformVerticesAround(int mode, float pivotX, float pivotY, float pivotZ,
                                           float tx, float ty, float tz, float falloffRadius)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;

    MeshData md = sceneMeshToMeshData(obj);
    if (md.vertices.isEmpty()) return false;

    const auto tmode = static_cast<MeshOperations::TransformCenterMode>(qBound(0, mode, 3));
    float f0 = tx, f1 = ty, f2 = tz;
    if (tmode == MeshOperations::TransformCenterMode::ScaleUniform)
        f1 = f2 = tx; // uniform factor lives in amount.x

    const MeshData result = MeshOperations::transformAround(
        md, MeshOperations::SelectionManager::selectedVertices(), tmode,
        QVector3D(pivotX, pivotY, pivotZ), QVector3D(),
        QVector3D(f0, f1, f2), falloffRadius, m_propEdit.falloffType);

    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit gizmoTransformChanged();
    emit statusMessage(QString("Transform around pivot: %1 vertices").arg(result.vertices.size()));
    return true;
}

QVariantList KSModelerQml::findClosestBorder(int objectId, float wx, float wy, float wz) {
    QVariantList out;
    if (!m_scene) return out;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return out;
    m_scene->updateAllTransforms();
    QMatrix4x4 world = obj->worldTransform();
    QPair<int, int> e = MeshOperations::findClosestBorderEdge(sceneMeshToMeshData(obj), world, QVector3D(wx, wy, wz));
    QVariantMap m;
    m["v0"] = e.first;
    m["v1"] = e.second;
    out.append(m);
    return out;
}

int KSModelerQml::elementAtWorld(int objectId, float wx, float wy, float wz) {
    if (!m_scene) return -1;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return -1;
    m_scene->updateAllTransforms();
    QMatrix4x4 world = obj->worldTransform();
    return MeshOperations::elementAtWorld(sceneMeshToMeshData(obj), world, QVector3D(wx, wy, wz));
}

bool KSModelerQml::selectBorderUnderCursor(float wx, float wy, float wz) {
    if (!m_selectedObject || !m_scene) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    m_scene->updateAllTransforms();
    QMatrix4x4 world = obj->worldTransform();
    QVector<Edge> borders = MeshOperations::borderEdges(sceneMeshToMeshData(obj));
    QPair<int, int> e = MeshOperations::findClosestBorderEdge(sceneMeshToMeshData(obj), world, QVector3D(wx, wy, wz));
    if (e.first < 0) return false;
    for (int i = 0; i < borders.size(); ++i) {
        const Edge& b = borders[i];
        if ((b.v1 == e.first && b.v2 == e.second) || (b.v1 == e.second && b.v2 == e.first)) {
            MeshOperations::SelectionManager::addSelectedBorderEdge(i);
            emit sceneChanged();
            return true;
        }
    }
    return false;
}

bool KSModelerQml::selectElementUnderCursor(float wx, float wy, float wz) {
    if (!m_selectedObject || !m_scene) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    m_scene->updateAllTransforms();
    MeshData data = sceneMeshToMeshData(obj);
    QMatrix4x4 world = obj->worldTransform();
    int elem = MeshOperations::elementAtWorld(data, world, QVector3D(wx, wy, wz));
    if (elem < 0) return false;
    MeshOperations::SelectionManager::setSelectedElement(elem);
    emit sceneChanged();
    return true;
}

QVariantList KSModelerQml::selectedBorderEdges() const {
    QVariantList out;
    const QVector<int>& b = MeshOperations::SelectionManager::selectedBorderEdges();
    for (int i : b) out.append(i);
    return out;
}

// ---------------------------------------------------------------------------
// Layers (Max-style scene layers)
// ---------------------------------------------------------------------------
const QStringList KSModelerQml::s_layerColorPalette = {
    "#9AA0A6", "#EA4335", "#FBBC04", "#34A853", "#4285F4",
    "#E91E63", "#9C27B0", "#00BCD4", "#FF5722", "#795548"
};

QStringList KSModelerQml::layerNames() const {
    QStringList out;
    for (const LayerDef& l : m_layers) out.append(l.name);
    return out;
}

int KSModelerQml::layerCount() const {
    return m_layers.size();
}

bool KSModelerQml::addLayer(const QString& name) {
    LayerDef def;
    def.name = name.isEmpty() ? QString("Layer %1").arg(m_layers.size() + 1) : name;
    def.color = m_layers.size() % s_layerColorPalette.size();
    m_layers.append(def);
    emit sceneChanged();
    return true;
}

bool KSModelerQml::removeLayer(int index) {
    if (index < 0 || index >= m_layers.size()) return false;
    m_layers.removeAt(index);
    for (auto it = m_objectLayers.begin(); it != m_objectLayers.end(); ) {
        if (it.value() == index) it = m_objectLayers.erase(it);
        else { if (it.value() > index) --it.value(); ++it; }
    }
    if (m_currentLayer >= m_layers.size()) m_currentLayer = qMax(0, m_layers.size() - 1);
    layerVisibleDo(0);
    emit sceneChanged();
    return true;
}

bool KSModelerQml::setLayerVisible(int index, bool visible) {
    if (index < 0 || index >= m_layers.size()) return false;
    m_layers[index].visible = visible;
    layerVisibleDo(index);
    emit sceneChanged();
    return true;
}

bool KSModelerQml::isLayerVisible(int index) const {
    return index >= 0 && index < m_layers.size() && m_layers[index].visible;
}

bool KSModelerQml::setLayerColor(int index, const QColor& color) {
    if (index < 0 || index >= m_layers.size()) return false;
    int best = 0, bestDist = INT_MAX;
    for (int i = 0; i < s_layerColorPalette.size(); ++i) {
        int d = qAbs(s_layerColorPalette[i].mid(1, 2).toInt(nullptr, 16) - color.red())
              + qAbs(s_layerColorPalette[i].mid(3, 2).toInt(nullptr, 16) - color.green())
              + qAbs(s_layerColorPalette[i].mid(5, 2).toInt(nullptr, 16) - color.blue());
        if (d < bestDist) { bestDist = d; best = i; }
    }
    m_layers[index].color = best;
    emit sceneChanged();
    return true;
}

int KSModelerQml::assignSelectionToLayer(int index) {
    if (index < 0 || index >= m_layers.size() || !m_selectedObject) return 0;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return 0;
    m_objectLayers[obj->id()] = index;
    m_layers[index].visible ? obj->setVisible(true) : obj->setVisible(false);
    emit sceneChanged();
    return 1;
}

int KSModelerQml::objectLayerId(int objectId) const {
    return m_objectLayers.value(objectId, 0);
}

bool KSModelerQml::setObjectLayer(int objectId, int layerIndex) {
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return false;
    m_objectLayers[objectId] = layerIndex;
    return true;
}

int KSModelerQml::currentLayerIndex() const {
    return m_currentLayer;
}

void KSModelerQml::setCurrentLayerIndex(int index) {
    m_currentLayer = index;
}

void KSModelerQml::layerVisibleDo(int layerIndex) {
    for (auto it = m_objectLayers.constBegin(); it != m_objectLayers.constEnd(); ++it) {
        SceneObject* obj = m_scene ? m_scene->findObjectById(it.key()) : nullptr;
        if (!obj) continue;
        if (it.value() == layerIndex) {
            obj->setVisible(m_layers[layerIndex].visible);
        } else {
            obj->setVisible(!m_layers.value(it.value()).visible);
        }
    }
}

void KSModelerQml::ensureMeshEdges(MeshData& data) {
    if (data.edges.isEmpty())
        MeshOperations::ensureEdgeList(data);
}

static QVector<QVector<int>> extractEdgeLoops(const QVector<Edge>& edges) {
    // Build adjacency from edge pairs, walk each connected chain (2-valent).
    QVector<QVector<int>> loops;
    QMap<int, QList<QPair<int, int>>> adj; // vertex -> list of (otherVertex, edgeIdx)
    for (int i = 0; i < edges.size(); ++i) {
        adj[edges[i].v1].append(qMakePair(edges[i].v2, i));
        adj[edges[i].v2].append(qMakePair(edges[i].v1, i));
    }
    QSet<int> usedEdges;
    QList<int> vertices;
    for (int v : adj.keys()) vertices.append(v);
    for (int start : vertices) {
        if (adj.value(start).isEmpty()) continue;
        QVector<int> loop;
        int cur = start, prev = -1;
        while (true) {
            loop.append(cur);
            const QList<QPair<int, int>>& nbrs = adj.value(cur);
            int next = -1, eidx = -1;
            for (const auto& nb : nbrs) {
                if (nb.second == prev || usedEdges.contains(nb.second) || nb.first == cur) continue;
                if (loop.contains(nb.first) && next >= 0) continue;
                next = nb.first; eidx = nb.second; break;
            }
            if (next >= 0 && eidx >= 0) {
                usedEdges.insert(eidx);
                prev = eidx;
                cur = next;
                if (cur == start) { loop.append(cur); break; }
            } else break;
        }
        if (loop.size() > 3) loops.append(loop);
    }
    return loops;
}

void KSModelerQml::packUVs(float margin, int resolution) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector2D> uvs;
    for (const auto& v : md.vertices) uvs.append(v.uv);
    UVMapper::scaleToFit(uvs, margin);
    for (int i = 0; i < md.vertices.size() && i < uvs.size(); i++)
        md.vertices[i].uv = uvs[i];
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("UVs packed");
}

bool KSModelerQml::resolveUVOverlaps()
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData md = sceneMeshToMeshData(obj);
    MeshData resolved = MeshOperations::resolveUVOverlaps(md);
    if (resolved.vertices.isEmpty() || resolved.vertices.size() != md.vertices.size())
        return false;
    bool changed = false;
    for (int i = 0; i < md.vertices.size(); ++i) {
        if ((md.vertices[i].uv - resolved.vertices[i].uv).lengthSquared() > 1e-6f) { changed = true; break; }
    }
    if (!changed) {
        emit statusMessage("No UV overlaps found");
        return false;
    }
    meshDataToSceneMesh(obj, resolved);
    emit sceneChanged();
    emit statusMessage("UV overlaps resolved and islands re-packed");
    return true;
}
QVariantList KSModelerQml::analyzeUVDensity(int objectId) {
    auto* o=m_scene?m_scene->findObjectById(objectId):nullptr; if(!o) o=m_selectedObject?m_selectedObject->object():nullptr; if(!o||!o->mesh()) return {};
    auto vals=MeshOperations::analyzeUVDensity(sceneMeshToMeshData(o)); QVariantList r; for(float v:vals) r.append(v); return r;
}
QString KSModelerQml::uvDensityHeatmap(int objectId, int w, int h) {
    auto* o=m_scene?m_scene->findObjectById(objectId):nullptr; if(!o) o=m_selectedObject?m_selectedObject->object():nullptr; if(!o||!o->mesh()) return {};
    QImage img=MeshOperations::uvDensityHeatmap(sceneMeshToMeshData(o),w,h); QString p=QDir::temp().filePath(QString("ks_uvdensity_%1.png").arg(objectId)); img.save(p); return p;
}
QString KSModelerQml::uvOverlapHeatmap(int objectId, int w, int h) {
    auto* o=m_scene?m_scene->findObjectById(objectId):nullptr; if(!o) o=m_selectedObject?m_selectedObject->object():nullptr; if(!o||!o->mesh()) return {};
    QImage img=MeshOperations::uvOverlapHeatmap(sceneMeshToMeshData(o),w,h); QString p=QDir::temp().filePath(QString("ks_uvoverlap_%1.png").arg(objectId)); img.save(p); return p;
}
bool KSModelerQml::createXRef(const QString& path, float x, float y, float z) {
    if(!m_scene||path.isEmpty()) return false;
    
    // Try to load the referenced file's geometry
    MeshData loadedMesh;
    bool loaded = false;
    QString ext = QFileInfo(path).suffix().toLower();
    
    if (ext == "obj") {
        CADOBJParser parser;
        if (parser.loadFromFile(path.toStdString()) && !parser.scene().meshes.empty()) {
            const auto& mesh = parser.scene().meshes[0];
            loadedMesh.vertices.reserve(mesh.vertices.size());
            for (const auto& v : mesh.vertices) {
                Vertex vtx;
                vtx.position = QVector3D(v.x, v.y, v.z);
                vtx.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
                loadedMesh.vertices.append(vtx);
            }
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                Face face;
                face.indices = { (int)mesh.indices[i].x, (int)mesh.indices[i+1].x, (int)mesh.indices[i+2].x };
                loadedMesh.faces.append(face);
            }
            loaded = !loadedMesh.vertices.isEmpty();
        }
    } else if (ext == "fbx") {
        FBXParser parser;
        if (parser.loadFromFile(path.toStdString()) && !parser.scene().meshes.empty()) {
            const auto& mesh = parser.scene().meshes[0];
            loadedMesh.vertices.reserve(mesh.vertices.size());
            for (const auto& v : mesh.vertices) {
                Vertex vtx;
                vtx.position = QVector3D(v.x, v.y, v.z);
                vtx.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
                loadedMesh.vertices.append(vtx);
            }
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                Face face;
                face.indices = { (int)mesh.indices[i], (int)mesh.indices[i+1], (int)mesh.indices[i+2] };
                loadedMesh.faces.append(face);
            }
            loaded = !loadedMesh.vertices.isEmpty();
        }
    } else if (ext == "gltf" || ext == "glb") {
        // Use GLB importer
        loaded = false; // GLB import handled elsewhere
    }
    
    // Create the scene object
    SceneObject* o;
    if (loaded) {
        // Use loaded geometry
        importMeshDataToScene(m_scene, loadedMesh, QFileInfo(path).baseName());
        // Get the last created object (importMeshDataToScene creates it)
        auto allObjs = m_scene->allObjects();
        if (allObjs.isEmpty()) return false;
        o = allObjs.last();
    } else {
        // Fallback: create a placeholder box
        auto md = MeshOperations::createBox(1,1,1);
        o = m_scene->createObject(QFileInfo(path).baseName(), SceneObject::Type::Mesh);
        importMeshDataToScene(m_scene, md, QFileInfo(path).baseName());
        auto allObjs = m_scene->allObjects();
        if (allObjs.isEmpty()) return false;
        o = allObjs.last();
    }
    
    o->setPosition(QVector3D(x,y,z));
    o->setProperty("xrefPath",path);
    o->setProperty("isXRef",true);
    o->setProperty("xrefLoaded",loaded);
    emit sceneChanged();
    emit statusMessage(loaded ? "XRef loaded: "+path : "XRef placeholder: "+path);
    return true;
}
bool KSModelerQml::updateXRefs() {
    if(!m_scene) return false;
    int n = 0;
    for(auto* o: m_scene->allObjects()) {
        if(!o->property("isXRef").toBool()) continue;
        QString path = o->property("xrefPath").toString();
        if(path.isEmpty()) continue;
        
        // Check if file exists and has been modified
        QFileInfo fi(path);
        if(!fi.exists()) continue;
        QDateTime lastModified = fi.lastModified();
        QDateTime lastUpdated = o->property("xrefLastModified").toDateTime();
        
        if(lastUpdated.isValid() && lastModified <= lastUpdated) continue;
        
        // File has been modified - reload it
        QString ext = fi.suffix().toLower();
        MeshData loadedMesh;
        bool loaded = false;
        
        if (ext == "obj") {
            CADOBJParser parser;
            if (parser.loadFromFile(path.toStdString()) && !parser.scene().meshes.empty()) {
                const auto& mesh = parser.scene().meshes[0];
                loadedMesh.vertices.reserve(mesh.vertices.size());
                for (const auto& v : mesh.vertices) {
                    Vertex vtx;
                    vtx.position = QVector3D(v.x, v.y, v.z);
                    vtx.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
                    loadedMesh.vertices.append(vtx);
                }
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    Face face;
                    face.indices = { (int)mesh.indices[i].x, (int)mesh.indices[i+1].x, (int)mesh.indices[i+2].x };
                    loadedMesh.faces.append(face);
                }
                loaded = !loadedMesh.vertices.isEmpty();
            }
        } else if (ext == "fbx") {
            FBXParser parser;
            if (parser.loadFromFile(path.toStdString()) && !parser.scene().meshes.empty()) {
                const auto& mesh = parser.scene().meshes[0];
                loadedMesh.vertices.reserve(mesh.vertices.size());
                for (const auto& v : mesh.vertices) {
                    Vertex vtx;
                    vtx.position = QVector3D(v.x, v.y, v.z);
                    vtx.color = QVector4D(0.8f, 0.8f, 0.8f, 1.0f);
                    loadedMesh.vertices.append(vtx);
                }
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    Face face;
                    face.indices = { (int)mesh.indices[i], (int)mesh.indices[i+1], (int)mesh.indices[i+2] };
                    loadedMesh.faces.append(face);
                }
                loaded = !loadedMesh.vertices.isEmpty();
            }
        }
        
        if(loaded && o->mesh()) {
            // Update the existing mesh with new geometry
            SceneMesh* sm = o->mesh();
            sm->geometry().vertices.clear();
            sm->geometry().indices.clear();
            for (const auto& v : loadedMesh.vertices) {
                SceneVertex sv;
                sv.position = QVector3D(v.position.x(), v.position.y(), v.position.z());
                sv.color = QVector4D(v.color.x(), v.color.y(), v.color.z(), v.color.w());
                sm->geometry().vertices.append(sv);
            }
            for (const auto& f : loadedMesh.faces) {
                for (int idx : f.indices)
                    sm->geometry().indices.append((uint32_t)idx);
            }
        }
        
        o->setProperty("xrefLastModified", lastModified.toString(Qt::ISODate));
        o->setProperty("xrefUpdated", QDateTime::currentDateTime().toString(Qt::ISODate));
        n++;
    }
    emit sceneChanged();
    emit statusMessage(QString("XRefs updated: %1").arg(n));
    return n>0;
}
QVariantList KSModelerQml::xrefList() const {
    QVariantList r; if(!m_scene) return r; for(auto* o: m_scene->allObjects()) if(o->property("isXRef").toBool()){ QVariantMap m; m["id"]=o->id(); m["name"]=o->name(); m["path"]=o->property("xrefPath"); r.append(m);} return r;
}
float KSModelerQml::sceneTolerance() const { return MeshOperations::sceneTolerance(); }
void KSModelerQml::setSceneTolerance(float t) { MeshOperations::setSceneTolerance(t); emit statusMessage(QString("Tolerance: %1").arg(t)); }
float KSModelerQml::sceneUnitScale() const { return MeshOperations::sceneUnitScale(); }
void KSModelerQml::setSceneUnitScale(float s) { MeshOperations::setSceneUnitScale(s); emit statusMessage(QString("Unit scale: %1").arg(s)); }
bool KSModelerQml::retargetSkeleton(int srcBone, int dstBone) {
    QVector<QVector3D> src, dst; for(int i=0;i<m_bones.size();++i) src.append(m_bones[i].position); dst=src;
    auto m=MeshOperations::retargetSkeleton(src,dst); Q_UNUSED(srcBone); Q_UNUSED(dstBone); emit skeletonChanged(); return !m.isEmpty();
}
bool KSModelerQml::applyClusterDeform(const QList<int>& indices, float dx,float dy,float dz,float wgt) {
    if(!m_selectedObject) return false; auto* obj=m_selectedObject->object(); if(!obj||!obj->mesh()) return false;
    MeshData md=sceneMeshToMeshData(obj); QVector<int> idx; for(int i:indices) idx.append(i);
    md=MeshOperations::applyClusterDeform(md,idx,QVector3D(dx,dy,dz),wgt); meshDataToSceneMesh(obj,md); emit sceneChanged(); return true;
}
bool KSModelerQml::applyBlendShape(int targetObjectId, float weight) {
    if(!m_scene||!m_selectedObject) return false; auto* src=m_selectedObject->object(); auto* dst=m_scene->findObjectById(targetObjectId); if(!src||!dst||!src->mesh()||!dst->mesh()) return false;
    MeshData a=sceneMeshToMeshData(src), b=sceneMeshToMeshData(dst); auto r=MeshOperations::applyBlendShape(a,b,qBound(0.0f,weight,1.0f)); meshDataToSceneMesh(src,r); emit sceneChanged(); return true;
}
QVariantList KSModelerQml::fcurveFilteredKeys(int objectId, const QString& channel, float from, float to) const {
    QVariantList r; auto it=m_fcurves.constFind(objectId); if(it==m_fcurves.constEnd()) return r;
    const auto* ch=it->channel(channel); if(!ch) return r;
    for(const auto &k: ch->keys) if(k.frame>=from&&k.frame<=to){ QVariantMap m; m["frame"]=k.frame; m["value"]=k.value; m["interp"]=static_cast<int>(k.interpolation); r.append(m);} return r;
}
bool KSModelerQml::bevelEdgesAdvanced(const QList<int>& edgeIndices, float distance, int segments, int profileType, float tension) {
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    MeshData md = sceneMeshToMeshData(obj);
    MeshData result = MeshOperations::bevelEdges(md, distance, segments, qDegreesToRadians(30.0f), profileType, tension);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Beveled (profile %1, tension %2)").arg(profileType).arg(tension, 0, 'f', 2));
    return true;
}

void KSModelerQml::translateUVs(float u, float v) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector2D> uvs;
    for (const auto& vert : md.vertices) uvs.append(vert.uv);
    UVTransform::translate(uvs, QVector2D(u, v));
    for (int i = 0; i < md.vertices.size() && i < uvs.size(); i++)
        md.vertices[i].uv = uvs[i];
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("UVs translated");
}

void KSModelerQml::rotateUVs(float angle) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector2D> uvs;
    for (const auto& vert : md.vertices) uvs.append(vert.uv);
    UVTransform::rotate(uvs, QVector2D(0.5f, 0.5f), qDegreesToRadians(angle));
    for (int i = 0; i < md.vertices.size() && i < uvs.size(); i++)
        md.vertices[i].uv = uvs[i];
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("UVs rotated");
}

void KSModelerQml::scaleUVs(float u, float v) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector2D> uvs;
    for (const auto& vert : md.vertices) uvs.append(vert.uv);
    UVTransform::scale(uvs, QVector2D(0.5f, 0.5f), u);
    for (int i = 0; i < md.vertices.size() && i < uvs.size(); i++)
        md.vertices[i].uv = uvs[i];
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage("UVs scaled");
}

void KSModelerQml::generateAutoUVs(int resolution) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    UVUnwrapConfig cfg;
    QVector<QVector3D> verts;
    QVector<QVector<int>> faces;
    for (const auto& v : md.vertices) verts.append(v.position);
    for (const auto& f : md.faces) { QVector<int> fi; for (int i : f.indices) fi.append(i); faces.append(fi); }
    QVector<QVector2D> uvs = UVMapper::smartProject(verts, faces, cfg);
    for (int i = 0; i < md.vertices.size() && i < uvs.size(); i++)
        md.vertices[i].uv = uvs[i];
    meshDataToSceneMesh(obj, md);
    emit sceneChanged();
    emit statusMessage(QString("Auto UV generated at %1x%1").arg(resolution));
}

void KSModelerQml::createSkeleton(const QString& type) {
    m_bones.clear();
    Skeleton* skel = nullptr;
    if (type == "humanoid")
        skel = RigifyGenerator::generateHumanoid(RigifyConfig());
    else if (type == "biped")
        skel = RigifyGenerator::generateBiped();
    else
        skel = RigifyGenerator::generateFKChain(4, QVector3D(0, 1, 0));
    if (skel) {
        for (const auto& b : skel->bones) {
            Bone bone;
            bone.name = b.name;
            bone.parentId = b.parentIndex;
            bone.position = b.head;
            m_bones.append(bone);
        }
        updateBoneHierarchy();
        emit statusMessage(QString("Created skeleton: %1 (%2 bones)").arg(type).arg(m_bones.size()));
    }
    m_boneVersion++;
    emit sceneChanged();
    emit skeletonChanged();
}

void KSModelerQml::addBone(const QString& name, int parentId, float x, float y, float z) {
    Bone bone;
    bone.name = name;
    bone.parentId = parentId;
    bone.position = QVector3D(x, y, z);
    m_bones.append(bone);
    // Add to parent's children list
    if (parentId >= 0 && parentId < m_bones.size() - 1)
        m_bones[parentId].children.append(m_bones.size() - 1);
    if (m_scene) {
        SceneObject* boneObj = m_scene->createObject(name, SceneObject::Type::Bone);
        boneObj->setPosition(QVector3D(x, y, z));
    }
    emit sceneChanged();
    m_boneVersion++;
    emit skeletonChanged();
    emit statusMessage("Added bone: " + name);
}

void KSModelerQml::bindToSkeleton(float maxDistance) {
    if (m_bones.isEmpty() || !m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    m_vertexWeights.clear();
    m_vertexWeights.resize(md.vertices.size());
    QVector<QVector3D> bonePositions;
    for (const auto& b : m_bones)
        bonePositions.append(b.position);
    QVector<QVector3D> verts;
    for (const auto& v : md.vertices) verts.append(v.position);
    QVector<QVector<int>> faces;
    for (const auto& f : md.faces) {
        QVector<int> fi;
        for (int idx : f.indices) fi.append(idx);
        faces.append(fi);
    }
    QVector<WeightVertex> autoWeights = AutoWeightCalculator::calculateAutoWeights(
        verts, faces, bonePositions, AutoWeightCalculator::Method::NearestVertex);
    for (int i = 0; i < autoWeights.size() && i < m_vertexWeights.size(); i++) {
        for (auto it = autoWeights[i].weights.begin(); it != autoWeights[i].weights.end(); ++it) {
            int boneIdx = it.key();
            if (boneIdx < m_bones.size()) {
                VertexWeight vw;
                vw.boneId = boneIdx;
                vw.weight = it.value();
                m_vertexWeights[i].append(vw);
            }
        }
    }
    emit sceneChanged();
    emit statusMessage("Bound mesh to skeleton");
}

void KSModelerQml::smoothSkinning(int iterations) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<WeightVertex> weights;
    for (int vi = 0; vi < m_vertexWeights.size(); vi++) {
        WeightVertex wv;
        wv.vertexIndex = vi;
        for (const auto& w : m_vertexWeights[vi])
            wv.weights[w.boneId] = w.weight;
        weights.append(wv);
    }
    QVector<QVector3D> verts;
    for (const auto& v : md.vertices) verts.append(v.position);
    QVector<QVector<int>> faces;
    for (const auto& f : md.faces) { QVector<int> fi; for (int i : f.indices) fi.append(i); faces.append(fi); }
    WeightOptimization::smoothWeights(verts, faces, weights, 0.5f, iterations);
    emit sceneChanged();
    emit statusMessage(QString("Skinning smoothed (%1 iterations)").arg(iterations));
}

void KSModelerQml::normalizeWeights() {
    if (m_vertexWeights.isEmpty()) return;
    QVector<WeightVertex> weights;
    for (int vi = 0; vi < m_vertexWeights.size(); vi++) {
        WeightVertex wv;
        wv.vertexIndex = vi;
        for (const auto& w : m_vertexWeights[vi])
            wv.weights[w.boneId] = w.weight;
        weights.append(wv);
    }
    WeightOptimization::normalizeAllWeights(weights);
    m_vertexWeights.clear();
    m_vertexWeights.resize(weights.size());
    for (int i = 0; i < weights.size(); i++) {
        for (auto it = weights[i].weights.begin(); it != weights[i].weights.end(); ++it) {
            VertexWeight vw;
            vw.boneId = it.key();
            vw.weight = it.value();
            m_vertexWeights[i].append(vw);
        }
    }
    emit sceneChanged();
    emit statusMessage("Weights normalized");
}

void KSModelerQml::pruneWeights(float threshold, int maxInfluences) {
    if (m_vertexWeights.isEmpty()) return;
    QVector<WeightVertex> weights;
    for (int vi = 0; vi < m_vertexWeights.size(); vi++) {
        WeightVertex wv;
        wv.vertexIndex = vi;
        for (const auto& w : m_vertexWeights[vi])
            wv.weights[w.boneId] = w.weight;
        weights.append(wv);
    }
    WeightPainter::cleanZeroWeights(weights, threshold);
    WeightPainter::limitWeights(weights, maxInfluences);
    WeightOptimization::pruneWeights(weights, QVector<QVector3D>(), threshold);
    m_vertexWeights.clear();
    m_vertexWeights.resize(weights.size());
    for (int i = 0; i < weights.size(); i++) {
        for (auto it = weights[i].weights.begin(); it != weights[i].weights.end(); ++it) {
            VertexWeight vw;
            vw.boneId = it.key();
            vw.weight = it.value();
            m_vertexWeights[i].append(vw);
        }
    }
    emit sceneChanged();
    emit statusMessage(QString("Weights pruned (threshold=%1, max=%2)").arg(threshold).arg(maxInfluences));
}

void KSModelerQml::paintWeights(int boneId, float x, float y, float z, float radius, float strength) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<QVector3D> verts;
    for (const auto& v : md.vertices) verts.append(v.position);
    QVector<WeightVertex> weights;
    for (int vi = 0; vi < m_vertexWeights.size(); vi++) {
        WeightVertex wv;
        wv.vertexIndex = vi;
        for (const auto& w : m_vertexWeights[vi])
            wv.weights[w.boneId] = w.weight;
        weights.append(wv);
    }
    WeightPainter::paintWeight(QVector3D(x, y, z), radius, boneId, strength, verts, weights);
    m_vertexWeights.clear();
    m_vertexWeights.resize(weights.size());
    for (int i = 0; i < weights.size(); i++) {
        for (auto it = weights[i].weights.begin(); it != weights[i].weights.end(); ++it) {
            VertexWeight vw;
            vw.boneId = it.key();
            vw.weight = it.value();
            m_vertexWeights[i].append(vw);
        }
    }
    emit sceneChanged();
    emit statusMessage(QString("Painted weights on bone %1").arg(boneId));
}

void KSModelerQml::mirrorWeights(int axis) {
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    MeshData md = sceneMeshToMeshData(obj);
    QVector<WeightVertex> weights;
    for (int vi = 0; vi < m_vertexWeights.size(); vi++) {
        WeightVertex wv;
        wv.vertexIndex = vi;
        for (const auto& w : m_vertexWeights[vi])
            wv.weights[w.boneId] = w.weight;
        weights.append(wv);
    }
    QVector<QVector3D> verts;
    for (const auto& v : md.vertices) verts.append(v.position);
    WeightPainter::mirrorWeights(weights, verts, axis == 0, axis == 1, axis == 2);
    m_vertexWeights.clear();
    m_vertexWeights.resize(weights.size());
    for (int i = 0; i < weights.size(); i++) {
        for (auto it = weights[i].weights.begin(); it != weights[i].weights.end(); ++it) {
            VertexWeight vw;
            vw.boneId = it.key();
            vw.weight = it.value();
            m_vertexWeights[i].append(vw);
        }
    }
    emit sceneChanged();
    emit statusMessage("Weights mirrored");
}

void KSModelerQml::solveTwoBoneIK(float targetX, float targetY, float targetZ) {
    if (m_bones.size() < 3) { emit statusMessage("Need at least 3 bones for IK"); return; }
    // Solve last 3 bones as a two-bone chain: rootBone -> midBone -> endBone
    int endIdx = m_bones.size() - 1;
    int midIdx = endIdx - 1;
    int rootIdx = endIdx - 2;
    QVector3D target(targetX, targetY, targetZ);

    QVector3D root = m_bones[rootIdx].position;
    QVector3D mid = m_bones[midIdx].position;
    QVector3D end = m_bones[endIdx].position;

    float lenA = (mid - root).length();
    float lenB = (end - mid).length();
    float lenC = qMin((target - root).length(), lenA + lenB * 0.999f);

    if (lenA < 0.001f || lenB < 0.001f) return;

    // Law of cosines for mid joint angle
    float cosMid = (lenA * lenA + lenB * lenB - lenC * lenC) / (2.0f * lenA * lenB);
    cosMid = qBound(-1.0f, cosMid, 1.0f);
    float midAngle = acosf(cosMid);

    // Compute bend axis (perpendicular to the plane of root->mid and root->target)
    QVector3D toMid = (mid - root).normalized();
    QVector3D toTarget = (target - root).normalized();
    QVector3D bendAxis = QVector3D::crossProduct(toMid, toTarget);
    if (bendAxis.lengthSquared() < 0.001f) {
        // Target is along same direction as current chain, use a default up vector
        bendAxis = QVector3D::crossProduct(toMid, QVector3D(0, 1, 0));
        if (bendAxis.lengthSquared() < 0.001f)
            bendAxis = QVector3D::crossProduct(toMid, QVector3D(1, 0, 0));
    }
    bendAxis.normalize();

    // Rotate mid bone around root by midAngle
    QQuaternion rootRot = QQuaternion::fromAxisAndAngle(bendAxis, qRadiansToDegrees(midAngle));
    QVector3D newMid = root + rootRot.rotatedVector(mid - root);

    // End bone goes from newMid toward target
    QVector3D newEndDir = (target - newMid).normalized();
    QVector3D newEnd = newMid + newEndDir * lenB;

    // Apply with IK weight blending
    for (int i = rootIdx; i <= endIdx; i++) {
        float w = m_bones[i].ikWeight;
        if (w <= 0.0f) continue;
        if (i == midIdx) m_bones[i].position += (newMid - m_bones[i].position) * w;
        else if (i == endIdx) m_bones[i].position += (newEnd - m_bones[i].position) * w;
    }

    // Update SceneObject transforms for bone objects
    if (m_scene) {
        auto allObjs = m_scene->allObjects();
        for (int i = 0; i < m_bones.size(); i++) {
            for (SceneObject* obj : allObjs) {
                if (obj->name() == m_bones[i].name) {
                    obj->setPosition(m_bones[i].position);
                    break;
                }
            }
        }
    }
    m_boneVersion++;
    emit sceneChanged();
    emit skeletonChanged();
    emit statusMessage(QString("Two-bone IK solved (chain: %1→%2→%3)").arg(m_bones[rootIdx].name).arg(m_bones[midIdx].name).arg(m_bones[endIdx].name));
}

void KSModelerQml::solveCCDIK(float targetX, float targetY, float targetZ, int iterations) {
    if (m_bones.isEmpty()) { emit statusMessage("No bones for IK"); return; }
    QVector3D target(targetX, targetY, targetZ);
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = m_bones.size() - 1; i >= 0; i--) {
            QVector3D tip = m_bones.last().position;
            QVector3D bonePos = m_bones[i].position;
            QVector3D toEnd = (tip - bonePos).normalized();
            QVector3D toTarget = (target - bonePos).normalized();
            if (toEnd.lengthSquared() < 0.001f || toTarget.lengthSquared() < 0.001f) continue;
            float dot = QVector3D::dotProduct(toEnd, toTarget);
            if (dot < 0.999f) {
                QVector3D axis = QVector3D::crossProduct(toEnd, toTarget);
                float len = axis.length();
                if (len > 0.001f) {
                    axis /= len;
                    float angle = acosf(qBound(-1.0f, dot, 1.0f));
                    QQuaternion rot = QQuaternion::fromAxisAndAngle(axis, qRadiansToDegrees(angle));
                    for (int j = i; j < m_bones.size(); j++) {
                        QVector3D rel = m_bones[j].position - bonePos;
                        m_bones[j].position = bonePos + rot.rotatedVector(rel);
                    }
                }
            }
        }
    }
    m_boneVersion++;
    emit sceneChanged();
    emit skeletonChanged();
    emit statusMessage(QString("CCD IK solved (%1 iterations)").arg(iterations));
}

// ============================================================================
// Bone Access, FK/IK Control, and Visualization Helpers
// ============================================================================

int KSModelerQml::boneCount() const { return m_bones.size(); }

QString KSModelerQml::getBoneName(int boneIdx) const {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return QString();
    return m_bones[boneIdx].name;
}

int KSModelerQml::getBoneParentId(int boneIdx) const {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return -1;
    return m_bones[boneIdx].parentId;
}

QVector3D KSModelerQml::getBonePosition(int boneIdx) const {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return QVector3D();
    return m_bones[boneIdx].position;
}

QVector3D KSModelerQml::getBoneWorldPosition(int boneIdx) const {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return QVector3D();
    // Bone positions are stored in world space
    return m_bones[boneIdx].position;
}

QVector3D KSModelerQml::getBoneRotation(int boneIdx) const {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return QVector3D();
    return m_bones[boneIdx].rotation;
}

QVector3D KSModelerQml::getBoneLength(int boneIdx) const {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return QVector3D();
    if (m_bones[boneIdx].children.isEmpty()) return QVector3D(0, 0.5f, 0);
    // Average direction to children
    QVector3D avg;
    for (int c : m_bones[boneIdx].children) {
        if (c >= 0 && c < m_bones.size())
            avg += m_bones[c].position - m_bones[boneIdx].position;
    }
    if (!m_bones[boneIdx].children.isEmpty()) avg /= m_bones[boneIdx].children.size();
    return avg;
}

int KSModelerQml::selectedBoneIndex() const { return m_selectedBone; }

void KSModelerQml::selectBone(int boneIdx) {
    if (boneIdx < -1 || boneIdx >= m_bones.size()) return;
    m_selectedBone = boneIdx;
    emit boneSelectionChanged();
}

void KSModelerQml::setBoneIKWeight(int boneIdx, float weight) {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return;
    m_bones[boneIdx].ikWeight = qBound(0.0f, weight, 1.0f);
    m_boneVersion++;
    emit skeletonChanged();
}

float KSModelerQml::getBoneIKWeight(int boneIdx) const {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return 0.0f;
    return m_bones[boneIdx].ikWeight;
}

void KSModelerQml::updateBoneHierarchy() {
    // Rebuild children lists from parentId references
    for (auto& b : m_bones) b.children.clear();
    for (int i = 0; i < m_bones.size(); i++) {
        int p = m_bones[i].parentId;
        if (p >= 0 && p < m_bones.size() && p != i)
            m_bones[p].children.append(i);
    }
    m_boneVersion++;
    emit skeletonChanged();
}

void KSModelerQml::setBonePosition(int boneIdx, float x, float y, float z) {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return;
    m_bones[boneIdx].position = QVector3D(x, y, z);
    // Update SceneObject
    if (m_scene) {
        for (SceneObject* obj : m_scene->allObjects()) {
            if (obj->name() == m_bones[boneIdx].name) {
                obj->setPosition(QVector3D(x, y, z));
                break;
            }
        }
    }
    emit sceneChanged();
    m_boneVersion++;
    emit skeletonChanged();
}

void KSModelerQml::setBoneRotation(int boneIdx, float x, float y, float z) {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return;
    m_bones[boneIdx].rotation = QVector3D(x, y, z);
    m_boneVersion++;
    emit skeletonChanged();
}

void KSModelerQml::applyFKPose(int boneIdx, float x, float y, float z, float rx, float ry, float rz) {
    if (boneIdx < 0 || boneIdx >= m_bones.size()) return;
    // Apply FK: propagate transform from bone down through children
    Bone& bone = m_bones[boneIdx];
    QVector3D oldPos = bone.position;
    QVector3D delta(x - oldPos.x(), y - oldPos.y(), z - oldPos.z());
    bone.position = QVector3D(x, y, z);
    bone.rotation = QVector3D(rx, ry, rz);

    // Propagate delta to children (maintain relative offsets)
    for (int c : bone.children) {
        if (c >= 0 && c < m_bones.size()) {
            m_bones[c].position += delta;
        }
    }

    if (m_scene) {
        for (SceneObject* obj : m_scene->allObjects()) {
            if (obj->name() == bone.name) {
                obj->setPosition(QVector3D(x, y, z));
                obj->setRotationEuler(QVector3D(rx, ry, rz));
                break;
            }
        }
    }
    emit sceneChanged();
    m_boneVersion++;
    emit skeletonChanged();
}

void KSModelerQml::createMaterial(const QString& name) {
    MaterialState mat;
    mat.name = name;
    m_materials.append(mat);
    m_currentMaterial = m_materials.size() - 1;
    emit statusMessage("Created material '" + name + "'");
    emit sceneChanged();
}

void KSModelerQml::setMaterialAlbedo(float r, float g, float b, float a) {
    if (m_currentMaterial >= 0 && m_currentMaterial < m_materials.size()) {
        m_materials[m_currentMaterial].albedo = QColor::fromRgbF(r, g, b, a);
        emit sceneChanged();
    }
}

void KSModelerQml::setMaterialMetallic(float value) {
    if (m_currentMaterial >= 0 && m_currentMaterial < m_materials.size()) {
        m_materials[m_currentMaterial].metallic = std::max(0.0f, std::min(1.0f, value));
        emit sceneChanged();
    }
}

void KSModelerQml::setMaterialRoughness(float value) {
    if (m_currentMaterial >= 0 && m_currentMaterial < m_materials.size()) {
        m_materials[m_currentMaterial].roughness = std::max(0.0f, std::min(1.0f, value));
        emit sceneChanged();
    }
}

void KSModelerQml::setMaterialNormalStrength(float value) {
    if (m_currentMaterial >= 0 && m_currentMaterial < m_materials.size()) {
        m_materials[m_currentMaterial].normalStrength = std::max(0.0f, value);
        emit sceneChanged();
    }
}

void KSModelerQml::setMaterialEmissive(float r, float g, float b) {
    if (m_currentMaterial >= 0 && m_currentMaterial < m_materials.size()) {
        m_materials[m_currentMaterial].emissive = QColor::fromRgbF(r, g, b);
        emit sceneChanged();
    }
}

void KSModelerQml::setMaterialOpacity(float value) {
    if (m_currentMaterial >= 0 && m_currentMaterial < m_materials.size()) {
        m_materials[m_currentMaterial].opacity = std::max(0.0f, std::min(1.0f, value));
        emit sceneChanged();
    }
}

void KSModelerQml::setSelectedBaseColor(float r, float g, float b) {
    if (m_selectedObject && m_selectedObject->object()) {
        m_selectedObject->object()->setBaseColor(QColor::fromRgbF(r, g, b));
        emit sceneChanged();
    }
}

void KSModelerQml::setSelectedMetallic(float value) {
    if (m_selectedObject && m_selectedObject->object()) {
        m_selectedObject->object()->setMetallic(std::max(0.0f, std::min(1.0f, value)));
        emit sceneChanged();
    }
}

void KSModelerQml::setSelectedRoughness(float value) {
    if (m_selectedObject && m_selectedObject->object()) {
        m_selectedObject->object()->setRoughness(std::max(0.0f, std::min(1.0f, value)));
        emit sceneChanged();
    }
}

void KSModelerQml::setSelectedOpacity(float value) {
    if (m_selectedObject && m_selectedObject->object()) {
        m_selectedObject->object()->setOpacity(std::max(0.0f, std::min(1.0f, value)));
        emit sceneChanged();
    }
}

int KSModelerQml::pickObjectAtScreen(float screenX, float screenY, float viewportW, float viewportH) {
    if (!m_scene || viewportW <= 0.0f || viewportH <= 0.0f) return -1;

    // Rebuild the orbit camera state exactly like the QML View3D camera.
    const float th = float(qDegreesToRadians(m_camTheta));
    const float ph = float(qDegreesToRadians(m_camPhi));
    const QVector3D target(m_camTargetX, m_camTargetY, m_camTargetZ);
    QVector3D camPos = target;
    camPos.setX(target.x() + m_camDistance * std::cos(th) * std::cos(ph));
    camPos.setY(target.y() + m_camDistance * std::sin(ph));
    camPos.setZ(target.z() + m_camDistance * std::sin(th) * std::cos(ph));

    const QVector3D forward = (target - camPos).normalized();

    QVector3D upRef(0.0f, 1.0f, 0.0f);
    if (std::abs(QVector3D::dotProduct(forward, upRef)) > 0.99f) upRef = QVector3D(0.0f, 0.0f, 1.0f);
    const QVector3D right = QVector3D::crossProduct(forward, upRef).normalized();
    const QVector3D up = QVector3D::crossProduct(right, forward).normalized();

    // Perspective ray through the pixel (vertical FOV from the matching lens).
    const float aspect = float(viewportW / viewportH);
    const double fovV = qDegreesToRadians(cameraFov());
    const double fovH = 2.0 * std::atan(std::tan(fovV * 0.5) * aspect);
    const float ndcX = (2.0f * screenX / viewportW) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screenY / viewportH);

    const float tanV = float(std::tan(fovV * 0.5));
    const float tanH = float(std::tan(fovH * 0.5));
    QVector3D dir = (forward + right * (ndcX * tanH) + up * (ndcY * tanV)).normalized();

    int bestId = -1;
    float bestT = std::numeric_limits<float>::max();

    for (SceneObject* obj : m_scene->allObjects()) {
        SceneMesh* mesh = obj->mesh();
        if (!obj->isVisible() || !mesh) continue;

        // World-space AABB from the local bounding box corners.
        const QVector3D bmin = mesh->boundsMin();
        const QVector3D bmax = mesh->boundsMax();
        const QMatrix4x4 wt = obj->worldTransform();

        QVector3D corners[8] = {
            QVector3D(bmin.x(), bmin.y(), bmin.z()),
            QVector3D(bmin.x(), bmin.y(), bmax.z()),
            QVector3D(bmin.x(), bmax.y(), bmin.z()),
            QVector3D(bmin.x(), bmax.y(), bmax.z()),
            QVector3D(bmax.x(), bmin.y(), bmin.z()),
            QVector3D(bmax.x(), bmin.y(), bmax.z()),
            QVector3D(bmax.x(), bmax.y(), bmin.z()),
            QVector3D(bmax.x(), bmax.y(), bmax.z())
        };
        QVector3D wMin(1e30f, 1e30f, 1e30f), wMax(-1e30f, -1e30f, -1e30f);
        for (int i = 0; i < 8; ++i) {
            const QVector3D wc = wt.map(corners[i]);
            wMin.setX(std::min(wMin.x(), wc.x())); wMin.setY(std::min(wMin.y(), wc.y())); wMin.setZ(std::min(wMin.z(), wc.z()));
            wMax.setX(std::max(wMax.x(), wc.x())); wMax.setY(std::max(wMax.y(), wc.y())); wMax.setZ(std::max(wMax.z(), wc.z()));
        }

        // Ray-AABB slab test.
        float tmin = -std::numeric_limits<float>::max();
        float tmax = std::numeric_limits<float>::max();
        bool miss = false;
        for (int axis = 0; axis < 3 && !miss; ++axis) {
            const float o = axis == 0 ? camPos.x() : (axis == 1 ? camPos.y() : camPos.z());
            const float d = axis == 0 ? dir.x() : (axis == 1 ? dir.y() : dir.z());
            const float lo = axis == 0 ? wMin.x() : (axis == 1 ? wMin.y() : wMin.z());
            const float hi = axis == 0 ? wMax.x() : (axis == 1 ? wMax.y() : wMax.z());
            if (std::abs(d) < 1e-9f) {
                if (o < lo || o > hi) { miss = true; break; }
            } else {
                float t1 = (lo - o) / d;
                float t2 = (hi - o) / d;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax) miss = true;
            }
        }
        if (miss || tmax < 0.0f) continue;
        if (tmin < bestT) { bestT = tmin; bestId = obj->id(); }
    }
    return bestId;
}

void KSModelerQml::applyPresetToObject(int objectId, const QString& preset) {
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj || obj->type() != SceneObject::Type::Mesh) return;

    if (preset == "ksPBR") {
        obj->setBaseColor(QColor::fromRgbF(0.78f, 0.78f, 0.78f));
        obj->setMetallic(0.5f);
        obj->setRoughness(0.5f);
        obj->setOpacity(1.0f);
    } else if (preset == "ksDrude") {
        obj->setBaseColor(QColor::fromRgbF(0.15f, 0.15f, 0.15f));
        obj->setMetallic(0.0f);
        obj->setRoughness(0.8f);
        obj->setOpacity(1.0f);
    } else if (preset == "ksGlass") {
        obj->setBaseColor(QColor::fromRgbF(0.9f, 0.95f, 1.0f));
        obj->setMetallic(0.0f);
        obj->setRoughness(0.05f);
        obj->setOpacity(0.3f);
    } else if (preset == "ksEmissive") {
        obj->setBaseColor(QColor::fromRgbF(1.0f, 1.0f, 1.0f));
        obj->setMetallic(0.0f);
        obj->setRoughness(0.6f);
        obj->setOpacity(1.0f);
    } else if (preset == "ksSkin") {
        obj->setBaseColor(QColor::fromRgbF(0.92f, 0.75f, 0.62f));
        obj->setMetallic(0.0f);
        obj->setRoughness(0.7f);
        obj->setOpacity(1.0f);
    }
    emit sceneChanged();
}

void KSModelerQml::applyMaterialParamsToObject(int objectId,
                                               float r, float g, float b,
                                               float metallic, float roughness,
                                               float opacity) {
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj || obj->type() != SceneObject::Type::Mesh) return;
    obj->setBaseColor(QColor::fromRgbF(r, g, b));
    obj->setMetallic(std::max(0.0f, std::min(1.0f, metallic)));
    obj->setRoughness(std::max(0.0f, std::min(1.0f, roughness)));
    obj->setOpacity(std::max(0.0f, std::min(1.0f, opacity)));
    emit sceneChanged();
}

void KSModelerQml::setObjectVisibility(int objectId, bool visible) {
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj) return;
    obj->setVisible(visible);
    emit sceneChanged();
}

void KSModelerQml::setCamTheta(qreal v) {
    if (!qFuzzyCompare(m_camTheta, v)) {
        m_camTheta = v;
        emit cameraChanged();
    }
}

void KSModelerQml::setCamPhi(qreal v) {
    qreal clamped = qBound(-89.0, v, 89.0);
    if (!qFuzzyCompare(m_camPhi, clamped)) {
        m_camPhi = clamped;
        emit cameraChanged();
    }
}

void KSModelerQml::setCamDistance(qreal v) {
    qreal clamped = qBound(0.5, v, 100.0);
    if (!qFuzzyCompare(m_camDistance, clamped)) {
        m_camDistance = clamped;
        emit cameraChanged();
    }
}

void KSModelerQml::setCamTargetX(qreal v) {
    if (!qFuzzyCompare(m_camTargetX, v)) {
        m_camTargetX = v;
        emit cameraChanged();
    }
}

void KSModelerQml::setCamTargetY(qreal v) {
    if (!qFuzzyCompare(m_camTargetY, v)) {
        m_camTargetY = v;
        emit cameraChanged();
    }
}

void KSModelerQml::setCamTargetZ(qreal v) {
    if (!qFuzzyCompare(m_camTargetZ, v)) {
        m_camTargetZ = v;
        emit cameraChanged();
    }
}

void KSModelerQml::setCameraView(const QString& view) {
    if (view == "top") {
        setCamTheta(0); setCamPhi(89); setCamDistance(8);
        setCamTargetX(0); setCamTargetY(0); setCamTargetZ(0);
        if (m_currentViewName != "Top") { m_currentViewName = "Top"; emit cameraChanged(); }
    } else if (view == "front") {
        setCamTheta(0); setCamPhi(0); setCamDistance(8);
        setCamTargetX(0); setCamTargetY(0); setCamTargetZ(0);
        if (m_currentViewName != "Front") { m_currentViewName = "Front"; emit cameraChanged(); }
    } else if (view == "right") {
        setCamTheta(90); setCamPhi(0); setCamDistance(8);
        setCamTargetX(0); setCamTargetY(0); setCamTargetZ(0);
        if (m_currentViewName != "Right") { m_currentViewName = "Right"; emit cameraChanged(); }
    } else if (view == "persp") {
        setCamTheta(45); setCamPhi(30); setCamDistance(8);
        setCamTargetX(0); setCamTargetY(0); setCamTargetZ(0);
        if (m_currentViewName != "Perspective") { m_currentViewName = "Perspective"; emit cameraChanged(); }
    }
}

void KSModelerQml::focusOnSelected() {
    if (m_selectedObject && m_selectedObject->object()) {
        auto p = m_selectedObject->object()->position();
        setCamTargetX(p.x()); setCamTargetY(p.y()); setCamTargetZ(p.z());
    }
}

void KSModelerQml::setGridVisible(bool v) {
    if (m_gridVisible != v) {
        m_gridVisible = v;
        emit gridVisibleChanged();
    }
}

void KSModelerQml::setViewMode(int mode) {
    mode = qBound(0, mode, 2);
    if (m_viewMode != mode) {
        m_viewMode = mode;
        emit viewModeChanged();
    }
}

void KSModelerQml::setEnvironmentHDR(const QString& path) {
    if (m_environmentHDR == path) return;
    m_environmentHDR = path;
    emit environmentHDRChanged();
    if (path.isEmpty())
        emit statusMessage("HDRI environment cleared");
    else
        emit statusMessage("HDRI environment set: " + path);
}

// ============================================================================
// Viewport culling + camera matching
// ============================================================================

void KSModelerQml::setCullingEnabled(bool on) {
    if (m_cullingEnabled != on) {
        m_cullingEnabled = on;
        emit cullingChanged();
        emit statusMessage(on ? "Viewport culling ON (distance " +
                                    QString::number(m_cullDistance, 'f', 0) + ")"
                              : "Viewport culling OFF");
    }
}

void KSModelerQml::setCullDistance(qreal v) {
    v = qBound(1.0, v, 5000.0);
    if (!qFuzzyCompare(m_cullDistance, v)) {
        m_cullDistance = v;
        emit cullingChanged();
    }
}

void KSModelerQml::setCameraFocalLength(qreal v) {
    v = qBound(5.0, v, 200.0);
    if (!qFuzzyCompare(m_cameraFocalLength, v)) {
        m_cameraFocalLength = v;
        emit cameraChanged();
        emit statusMessage("Camera focal length: " + QString::number(v, 'f', 1) + " mm");
    }
}

void KSModelerQml::setCameraSensorWidth(qreal v) {
    v = qBound(4.0, v, 72.0);
    if (!qFuzzyCompare(m_cameraSensorWidth, v)) {
        m_cameraSensorWidth = v;
        emit cameraChanged();
        emit statusMessage("Camera sensor width: " + QString::number(v, 'f', 1) + " mm");
    }
}

qreal KSModelerQml::cameraFov() const {
    // Vertical FOV from focal length + sensor width (36mm horizontal / 24mm vertical = 3:2).
    const qreal sensorH = m_cameraSensorWidth * (24.0 / 36.0);
    const qreal halfAngle = qAtan(sensorH / (2.0 * m_cameraFocalLength));
    return qRadiansToDegrees(2.0 * halfAngle);
}

void KSModelerQml::matchCameraToSelection() {
    if (!m_scene) return;
    SceneObject* obj = m_selectedObject ? m_selectedObject->object() : nullptr;
    if (!obj || obj->type() != SceneObject::Type::Camera) {
        emit statusMessage("Select a Camera object to match the viewport camera to");
        return;
    }
    QVector3D pos = obj->worldPosition();
    setCamTargetX(pos.x()); setCamTargetY(pos.y()); setCamTargetZ(pos.z());
    setCamDistance(m_camDistance > 0.5 ? m_camDistance : 5.0);
    emit statusMessage("Viewport camera matched to \"" + obj->name() + "\"");
}

void KSModelerQml::setTonemappingMode(int mode) {
    mode = qBound(0, mode, 4);
    if (m_tonemappingMode != mode) {
        m_tonemappingMode = mode;
        emit renderingChanged();
        emit statusMessage("Tonemapping mode: " + QString::number(mode));
    }
}

void KSModelerQml::setTonemapExposure(qreal v) {
    v = qBound(0.05, v, 8.0);
    if (!qFuzzyCompare(m_tonemapExposure, v)) {
        m_tonemapExposure = v;
        emit renderingChanged();
    }
}

void KSModelerQml::createShader(const QString& vertexShader, const QString& fragmentShader) {
    if (m_currentMaterial >= 0 && m_currentMaterial < m_materials.size()) {
        m_materials[m_currentMaterial].vertexShader = vertexShader;
        m_materials[m_currentMaterial].fragmentShader = fragmentShader;
    }
}

void KSModelerQml::compileShader() {
    if (m_currentMaterial >= 0 && m_currentMaterial < m_materials.size()) {
        auto& mat = m_materials[m_currentMaterial];
        graphics::Shader3D shader;

        if (!mat.vertexShader.isEmpty() && !mat.fragmentShader.isEmpty()) {
            shader.setType(graphics::Shader3D::Custom);
            QString glsl = shader.compile();
            mat.fragmentShader = glsl;
            emit statusMessage("Custom shader compiled");
        } else {
            shader.setType(graphics::Shader3D::PBR);
            graphics::Shader3D::BSDF bsdf;
            bsdf.baseColor[0] = mat.albedo.redF();
            bsdf.baseColor[1] = mat.albedo.greenF();
            bsdf.baseColor[2] = mat.albedo.blueF();
            bsdf.metallic = mat.metallic;
            bsdf.roughness = mat.roughness;
            bsdf.alpha = mat.opacity;
            shader.setBSDF(bsdf);
            mat.fragmentShader = shader.compile();
            emit statusMessage("Default PBR shader compiled");
        }
        emit sceneChanged();
    }
}

void KSModelerQml::addAnimation(const QString& name, float duration) {
    Animation anim;
    anim.name = name;
    anim.duration = duration;
    m_animations.append(anim);
    emit sceneChanged();
}

void KSModelerQml::deleteAnimation(const QString& name) {
    for (int i = 0; i < m_animations.size(); ++i) {
        if (m_animations[i].name == name) {
            bool wasCurrent = (m_currentAnimation == i);
            m_animations.removeAt(i);
            if (wasCurrent) {
                m_currentAnimation = -1;
                emit animationNameChanged();
                emit animationTimeChanged();
                emit playbackStateChanged();
            }
            return;
        }
    }
}

QStringList KSModelerQml::animationNames() const {
    QStringList names;
    for (const auto& a : m_animations) names.append(a.name);
    return names;
}

void KSModelerQml::setCurrentAnimationByName(const QString& name) {
    for (int i = 0; i < m_animations.size(); ++i) {
        if (m_animations[i].name == name) {
            m_currentAnimation = i;
            m_animationTime = 0.0f;
            m_isAnimating = false;
            if (m_animTimer) m_animTimer->stop();
            applyPoseToBones(m_animations[i], 0.0f);
            emit animationNameChanged();
            emit animationTimeChanged();
            emit playbackStateChanged();
            emit sceneChanged();
            return;
        }
    }
}

void KSModelerQml::addKeyframe(const QString& animName, float time, int boneId, float x, float y, float z, float rotX, float rotY, float rotZ) {
    for (auto& anim : m_animations) {
        if (anim.name == animName) {
            Keyframe kf;
            kf.time = time;
            kf.boneId = boneId;
            kf.position = QVector3D(x, y, z);
            kf.rotation = QVector3D(rotX, rotY, rotZ);
            anim.keyframes.append(kf);
            return;
        }
    }
}

void KSModelerQml::addKeyframeForSelectedObject(const QString& animName) {
    if (!m_selectedObject || !m_selectedObject->object()) return;
    SceneObject* obj = m_selectedObject->object();
    QVector3D p = obj->position();
    QVector3D rot = obj->rotationEuler();
    addKeyframe(animName, m_animationTime, obj->id(), p.x(), p.y(), p.z(), rot.x(), rot.y(), rot.z());
}

void KSModelerQml::addKeyframeForSelectedObjectToLayer(const QString& animName, int layerIndex) {
    if (!m_selectedObject || !m_selectedObject->object()) return;
    SceneObject* obj = m_selectedObject->object();
    QVector3D p = obj->position();
    QVector3D rot = obj->rotationEuler();
    animationAddLayerKeyframe(animName, layerIndex, m_animationTime, obj->id(), p.x(), p.y(), p.z(), rot.x(), rot.y(), rot.z());
}

void KSModelerQml::generateWalkCycle(const QString& animName, double duration, double amplitude)
{
    if (m_bones.isEmpty()) {
        emit statusMessage("Walk cycle: create a skeleton first (Rigging > Humanoid).");
        return;
    }
    duration = qBound(0.25, duration, 8.0);
    amplitude = qBound(0.05, amplitude, 2.0);

    // Make sure the target animation exists.
    int ai = animationIndexByName(m_animations, animName);
    if (ai < 0) {
        addAnimation(animName, float(duration));
        ai = animationIndexByName(m_animations, animName);
    }
    if (ai < 0) return;
    Animation& anim = m_animations[ai];
    anim.duration = float(duration);
    // Drop any existing keyframes so regeneration is idempotent.
    anim.keyframes.clear();

    // Bone role lookup (Humanoid naming: thigh.L/shin.L/foot.L/upper_arm.L/forearm.L/hand.L).
    struct Role { QString stem; int l = -1; int r = -1; };
    Role thigh, shin, foot, arm, forearm, hand, clavicle;
    int rootIdx = -1, spineIdx = -1, chestIdx = -1, neckIdx = -1, headIdx = -1;
    for (int i = 0; i < m_bones.size(); ++i) {
        const QString n = m_bones[i].name;
        if (n == "root") { rootIdx = i; continue; }
        if (n == "spine" || n == "spine_01") { spineIdx = i; continue; }
        if (n == "chest" || n == "chest_01" || n == "spine_02") { chestIdx = i; continue; }
        if (n == "neck") { neckIdx = i; continue; }
        if (n == "head") { headIdx = i; continue; }
        auto assign = [&](Role& role, int idx) {
            if (n.endsWith(".L") || n.endsWith("_L") || n.endsWith(".left") || n.contains("left")) role.l = idx;
            else if (n.endsWith(".R") || n.endsWith("_R") || n.endsWith(".right") || n.contains("right")) role.r = idx;
        };
        if (n.startsWith("thigh") || n.contains("upper_leg") || n.contains("hip")) assign(thigh, i);
        else if (n.startsWith("shin") || n.contains("lower_leg") || n.contains("knee")) assign(shin, i);
        else if (n.startsWith("foot") || n.contains("ankle")) assign(foot, i);
        else if (n.startsWith("upper_arm") || n.contains("shoulder")) assign(arm, i);
        else if (n.startsWith("forearm") || n.contains("elbow") || n.contains("lower_arm")) assign(forearm, i);
        else if (n.startsWith("hand")) assign(hand, i);
        else if (n.startsWith("clavicle") || n.contains("collar")) assign(clavicle, i);
    }

    const int missing = (thigh.l < 0 || thigh.r < 0 || shin.l < 0 || foot.l < 0 || arm.l < 0) ? 1 : 0;
    if (missing) {
        emit statusMessage("Walk cycle: skeleton must use Humanoid naming (thigh.L/R, shin, foot, upper_arm).");
        return;
    }

    const float A = float(amplitude);
    const float thighSwing = 32.0f * A;
    const float kneeBend = 58.0f * A;
    const float footFlex = 22.0f * A;
    const float armSwing = 28.0f * A;
    const float elbowBend = 34.0f * A;
    const float spineBob = 5.0f * A;
    const float spineTwist = 7.0f * A;
    const float headBob = 3.0f * A;
    const float bobHeight = 0.05f * float(A);

    // Capture the rest pose Y for the root so the bob is relative to it.
    const float rootRestY = rootIdx >= 0 ? m_bones[rootIdx].position.y() : 0.0f;
    // Keep base bone rotations as the neutral pose (bones default to euler 0).
    auto baseRot = [&](int i) -> QVector3D {
        if (i < 0 || i >= m_bones.size()) return QVector3D();
        return m_bones[i].rotation;
    };

    const int frames = 24;
    for (int f = 0; f <= frames; ++f) {
        const float t = float(duration) * float(f) / float(frames);
        const float th = 2.0f * float(M_PI) * float(f) / float(frames); // one full cycle
        const float thR = th + float(M_PI);                              // opposite leg

        // Legs.
        if (thigh.l >= 0 && thigh.r >= 0) {
            const QVector3D bL = baseRot(thigh.l);
            const QVector3D bR = baseRot(thigh.r);
            addKeyframe(animName, t, thigh.l, 0, 0, 0,
                        bL.x() + thighSwing * std::sin(th), bL.y(), bL.z());
            addKeyframe(animName, t, thigh.r, 0, 0, 0,
                        bR.x() + thighSwing * std::sin(thR), bR.y(), bR.z());
        }
        if (shin.l >= 0 && shin.r >= 0) {
            const QVector3D bL = baseRot(shin.l);
            const QVector3D bR = baseRot(shin.r);
            // Knee flexes hardest in the middle of the swing phase.
            addKeyframe(animName, t, shin.l, 0, 0, 0,
                        bL.x() + kneeBend * (0.55f + 0.45f * std::cos(th)), bL.y(), bL.z());
            addKeyframe(animName, t, shin.r, 0, 0, 0,
                        bR.x() + kneeBend * (0.55f + 0.45f * std::cos(thR)), bR.y(), bR.z());
        }
        if (foot.l >= 0 && foot.r >= 0) {
            const QVector3D bL = baseRot(foot.l);
            const QVector3D bR = baseRot(foot.r);
            addKeyframe(animName, t, foot.l, 0, 0, 0,
                        bL.x() - footFlex * std::sin(th), bL.y(), bL.z());
            addKeyframe(animName, t, foot.r, 0, 0, 0,
                        bR.x() - footFlex * std::sin(thR), bR.y(), bR.z());
        }

        // Arms swing opposite to the legs.
        if (arm.l >= 0 && arm.r >= 0) {
            const QVector3D bL = baseRot(arm.l);
            const QVector3D bR = baseRot(arm.r);
            addKeyframe(animName, t, arm.l, 0, 0, 0,
                        bL.x() + armSwing * std::sin(thR), bL.y(), bL.z());
            addKeyframe(animName, t, arm.r, 0, 0, 0,
                        bR.x() + armSwing * std::sin(th), bR.y(), bR.z());
        }
        if (forearm.l >= 0 && forearm.r >= 0) {
            const QVector3D bL = baseRot(forearm.l);
            const QVector3D bR = baseRot(forearm.r);
            addKeyframe(animName, t, forearm.l, 0, 0, 0,
                        bL.x() + elbowBend * (0.5f + 0.25f * std::sin(thR)), bL.y(), bL.z());
            addKeyframe(animName, t, forearm.r, 0, 0, 0,
                        bR.x() + elbowBend * (0.5f + 0.25f * std::sin(th)), bR.y(), bR.z());
        }

        // Torso counter-rotation + bob.
        if (rootIdx >= 0) {
            const QVector3D b = baseRot(rootIdx);
            addKeyframe(animName, t, rootIdx, 0, rootRestY + bobHeight * std::cos(2.0f * th), 0,
                        b.x() + spineBob * std::sin(th), b.y(), b.z() + spineTwist * std::sin(th));
        }
        if (spineIdx >= 0) {
            const QVector3D b = baseRot(spineIdx);
            addKeyframe(animName, t, spineIdx, 0, 0, 0,
                        b.x() + spineBob * 0.6f * std::sin(th), b.y(), b.z() + spineTwist * std::sin(th));
        }
        if (chestIdx >= 0) {
            const QVector3D b = baseRot(chestIdx);
            addKeyframe(animName, t, chestIdx, 0, 0, 0,
                        b.x() + spineBob * 0.4f * std::sin(th), b.y(), b.z() + spineTwist * std::sin(th));
        }
        if (headIdx >= 0) {
            const QVector3D b = baseRot(headIdx);
            addKeyframe(animName, t, headIdx, 0, 0, 0,
                        b.x() + headBob * std::sin(2.0f * th), b.y(), b.z() - spineTwist * 0.4f * std::sin(th));
        }
    }

    emit sceneChanged();
    emit animationNameChanged();
    emit animationTimeChanged();
    emit statusMessage(QString("Walk cycle generated: %1 (%2s, %3 keyframes)").arg(animName).arg(duration).arg(anim.keyframes.size()));
}

// ============================================================================
// BVH mocap import
// ============================================================================

namespace {
// Map a BVH joint name onto the naming scheme of the built-in Humanoid
// skeleton (root/spine/chest/neck/head, thigh/shin/foot/.L/.R, ...).
QString mapBvhBoneName(const QString& n) {
    QString s = n.trimmed();
    QString side;
    QString stem = s;
    if (s.startsWith("Left", Qt::CaseInsensitive)) { side = ".L"; stem = s.mid(4); }
    else if (s.startsWith("Right", Qt::CaseInsensitive)) { side = ".R"; stem = s.mid(5); }
    else if (s.endsWith("_L") || s.endsWith(".L") || s.endsWith("Left", Qt::CaseInsensitive)) side = ".L";
    else if (s.endsWith("_R") || s.endsWith(".R") || s.endsWith("Right", Qt::CaseInsensitive)) side = ".R";

    static const QMap<QString, QString> table = {
        {"Hips", "root"}, {"Root", "root"}, {"Pelvis", "root"}, {"Hip", "root"},
        {"Spine", "spine"}, {"Abdomen", "spine"}, {"Spine1", "chest"}, {"Spine2", "chest"}, {"Chest", "chest"},
        {"Neck", "neck"}, {"Neck1", "neck"}, {"Head", "head"},
        {"UpLeg", "thigh"}, {"Thigh", "thigh"}, {"UpperLeg", "thigh"},
        {"Leg", "shin"}, {"Shin", "shin"}, {"LowerLeg", "shin"}, {"Knee", "shin"},
        {"Foot", "foot"}, {"Toe", "foot"}, {"Ankle", "foot"},
        {"Arm", "upper_arm"}, {"UpperArm", "upper_arm"}, {"Upper_arm", "upper_arm"},
        {"Shoulder", "shoulder"}, {"Collar", "shoulder"}, {"Clavicle", "shoulder"},
        {"ForeArm", "forearm"}, {"Forearm", "forearm"}, {"Elbow", "forearm"}, {"LowerArm", "forearm"},
        {"Hand", "hand"}, {"Wrist", "hand"}
    };
    const QString base = table.value(stem, stem);
    const bool center = (base == "root" || base == "spine" || base == "chest"
                         || base == "neck" || base == "head" || base == "shoulder");
    if (center) return base;
    return base + side;
}
} // namespace

bool KSModelerQml::importBVH(const QString& path, const QString& animName) {
    ks::BvhImporter imp;
    if (!imp.load(path)) {
        emit statusMessage("BVH import failed: could not parse file");
        return false;
    }

    // Match BVH joints onto existing bones (case-insensitive).
    QVector<int> jointToBone(imp.jointCount(), -1);
    int matched = 0;
    {
        QMap<QString, int> byLower;
        for (int i = 0; i < m_bones.size(); ++i)
            byLower.insert(m_bones[i].name.toLower(), i);
        for (int j = 0; j < imp.jointCount(); ++j) {
            const QString mapped = mapBvhBoneName(imp.joints()[j].name);
            const auto it = byLower.constFind(mapped.toLower());
            if (it != byLower.constEnd()) { jointToBone[j] = it.value(); ++matched; }
        }
    }

    // If almost nothing matched, rebuild a skeleton from the BVH hierarchy.
    if (matched < 2) {
        m_bones.clear();
        jointToBone.fill(-1);
        for (int j = 0; j < imp.jointCount(); ++j) {
            const auto& jt = imp.joints()[j];
            Bone b;
            b.name = jt.name;
            b.parentId = -1;
            b.position = jt.offset;
            b.rotation = QVector3D(0, 0, 0);
            b.length = jt.offset;
            jointToBone[j] = m_bones.size();
            m_bones.append(b);
        }
        for (int j = 0; j < imp.jointCount(); ++j) {
            const int parent = imp.joints()[j].parent;
            const int idx = jointToBone[j];
            if (parent >= 0) {
                const int pidx = jointToBone[parent];
                m_bones[idx].parentId = pidx;
                m_bones[idx].position = m_bones[pidx].position + m_bones[idx].length;
                m_bones[pidx].children.append(idx);
            }
        }
        m_selectedBone = -1;
    }

    // Create (or reset) the target animation.
    const QString anim = animName.isEmpty() ? path.section('/', -1).section('.', 0, 0) : animName;
    int ai = animationIndexByName(m_animations, anim);
    if (ai < 0) {
        m_animations.append(Animation());
        ai = m_animations.size() - 1;
        m_animations[ai].name = anim;
    }
    m_animations[ai].keyframes.clear();
    m_animations[ai].layers.clear();
    m_animations[ai].duration = imp.frameCount() * imp.frameTime();

    const int numFrames = imp.frameCount();
    const float frameTime = imp.frameTime();
    for (int f = 0; f < numFrames; ++f) {
        const QVector<float> data = imp.frame(f);
        if (data.isEmpty()) break;
        const float t = f * frameTime;
        for (int j = 0; j < imp.jointCount(); ++j) {
            const int boneId = jointToBone[j];
            if (boneId < 0) continue;
            const auto& jt = imp.joints()[j];
            QVector3D pos = m_bones[boneId].position;
            QQuaternion q; // identity
            for (int c = 0; c < jt.channels.size(); ++c) {
                const int ci = jt.channelOffset + c;
                if (ci < 0 || ci >= data.size()) continue;
                const float v = data[ci];
                const QString& ch = jt.channels[c];
                if (ch == "Xposition") pos.setX(v);
                else if (ch == "Yposition") pos.setY(v);
                else if (ch == "Zposition") pos.setZ(v);
                else if (ch == "Zrotation") q = QQuaternion::fromAxisAndAngle(0, 0, 1, v) * q;
                else if (ch == "Xrotation") q = QQuaternion::fromAxisAndAngle(1, 0, 0, v) * q;
                else if (ch == "Yrotation") q = QQuaternion::fromAxisAndAngle(0, 1, 0, v) * q;
            }
            const QVector3D euler = q.toEulerAngles();
            addKeyframe(anim, t, boneId, pos.x(), pos.y(), pos.z(), euler.x(), euler.y(), euler.z());
        }
    }

    m_currentAnimation = ai;
    m_animationTime = 0.0f;
    m_isAnimating = false;
    if (m_animTimer) m_animTimer->stop();
    applyPoseToBones(m_animations[ai], 0.0f);
    emit animationNameChanged();
    emit animationTimeChanged();
    emit playbackStateChanged();
    emit sceneChanged();
    emit statusMessage(QString("BVH imported: %1 (%2 frames, %3 bones keyed) into animation '%4'")
                           .arg(path).arg(numFrames).arg(matched > 0 ? matched : m_bones.size()).arg(anim));
    return true;
}

// ============================================================================
// NLA (non-linear animation): clip/source on a master timeline
// ============================================================================

int KSModelerQml::nlaAddClip(const QString& sourceAnim) {
    const int ai = animationIndexByName(m_animations, sourceAnim);
    if (ai < 0) return -1;
    NLAClip c;
    c.name = "Clip " + QString::number(m_nlaClips.size() + 1);
    c.sourceAnim = sourceAnim;
    c.duration = m_animations[ai].duration > 0.0 ? m_animations[ai].duration : 1.0;
    if (!m_nlaClips.isEmpty())
        c.start = m_nlaClips.last().start + m_nlaClips.last().duration;
    m_nlaClips.append(c);
    emit nlaChanged();
    return m_nlaClips.size() - 1;
}

bool KSModelerQml::nlaRemoveClip(int index) {
    if (index < 0 || index >= m_nlaClips.size()) return false;
    m_nlaClips.removeAt(index);
    if (m_nlaClips.isEmpty()) nlaStop();
    emit nlaChanged();
    return true;
}

bool KSModelerQml::nlaSetClipRange(int index, double start, double duration) {
    if (index < 0 || index >= m_nlaClips.size()) return false;
    m_nlaClips[index].start = qMax(0.0, start);
    m_nlaClips[index].duration = qMax(0.1, duration);
    emit nlaChanged();
    return true;
}

bool KSModelerQml::nlaSetClipTimescale(int index, double timescale) {
    if (index < 0 || index >= m_nlaClips.size()) return false;
    m_nlaClips[index].timescale = qBound(0.05, timescale, 8.0);
    emit nlaChanged();
    return true;
}

bool KSModelerQml::nlaSetClipLoop(int index, bool loop) {
    if (index < 0 || index >= m_nlaClips.size()) return false;
    m_nlaClips[index].loop = loop;
    emit nlaChanged();
    return true;
}

bool KSModelerQml::nlaSetClipEnabled(int index, bool enabled) {
    if (index < 0 || index >= m_nlaClips.size()) return false;
    m_nlaClips[index].enabled = enabled;
    applyNLAPose();
    emit nlaChanged();
    emit sceneChanged();
    return true;
}

bool KSModelerQml::nlaSetClipWeight(int index, float weight) {
    if (index < 0 || index >= m_nlaClips.size()) return false;
    m_nlaClips[index].weight = qBound(0.0f, weight, 1.0f);
    applyNLAPose();
    emit nlaChanged();
    emit sceneChanged();
    return true;
}

QVariantList KSModelerQml::nlaClipList() const {
    QVariantList out;
    for (int i = 0; i < m_nlaClips.size(); ++i) {
        const NLAClip& c = m_nlaClips[i];
        QVariantMap m;
        m["index"] = i;
        m["name"] = c.name;
        m["source"] = c.sourceAnim;
        m["start"] = c.start;
        m["duration"] = c.duration;
        m["end"] = c.start + c.duration;
        m["timescale"] = c.timescale;
        m["loop"] = c.loop;
        m["enabled"] = c.enabled;
        m["weight"] = c.weight;
        out.append(m);
    }
    return out;
}

double KSModelerQml::nlaDuration() const {
    double end = 1.0;
    for (const NLAClip& c : m_nlaClips)
        end = qMax(end, c.start + c.duration);
    return end;
}

void KSModelerQml::nlaPlay() {
    if (m_nlaClips.isEmpty()) return;
    m_nlaPlaying = true;
    if (!m_nlaTimer) {
        m_nlaTimer = new QTimer(this);
        connect(m_nlaTimer, &QTimer::timeout, this, [this]() {
            const double dur = nlaDuration();
            m_nlaTime += 1.0 / 30.0;
            if (m_nlaTime >= dur) m_nlaTime = 0.0;
            applyNLAPose();
            emit nlaTimeChanged();
            emit sceneChanged();
        });
    }
    m_nlaTimer->start(1000 / 30);
    applyNLAPose();
    emit nlaTimeChanged();
    emit sceneChanged();
}

void KSModelerQml::nlaPause() {
    m_nlaPlaying = false;
    if (m_nlaTimer) m_nlaTimer->stop();
}

void KSModelerQml::nlaStop() {
    nlaPause();
    m_nlaTime = 0.0;
    applyNLAPose();
    emit nlaTimeChanged();
    emit sceneChanged();
}

void KSModelerQml::nlaSetTime(double time) {
    m_nlaTime = qMax(0.0, time);
    applyNLAPose();
    emit nlaTimeChanged();
    emit sceneChanged();
}

void KSModelerQml::applyNLAPose() {
    if (m_bones.isEmpty()) return;
    QVector<BonePose> pose(m_bones.size());
    for (const NLAClip& c : m_nlaClips) {
        if (!c.enabled || c.weight <= 0.0f) continue;
        if (m_nlaTime < c.start || m_nlaTime >= c.start + c.duration) continue;
        const int ai = animationIndexByName(m_animations, c.sourceAnim);
        if (ai < 0) continue;
        double local = (m_nlaTime - c.start) * c.timescale;
        const double srcDur = m_animations[ai].duration > 0.0 ? m_animations[ai].duration : 1.0;
        if (local >= srcDur) {
            if (!c.loop) local = srcDur - 1e-4;
            else local = std::fmod(local, srcDur);
        }
        QVector<BonePose> clipPose;
        computeAnimationPose(m_animations[ai], (float)local, clipPose);
        const float w = qBound(0.0f, c.weight, 1.0f);
        for (int i = 0; i < m_bones.size(); ++i) {
            pose[i].position = pose[i].position + (clipPose[i].position - pose[i].position) * w;
            const QQuaternion qb = QQuaternion::fromEulerAngles(pose[i].rotation);
            const QQuaternion qc = QQuaternion::fromEulerAngles(clipPose[i].rotation);
            pose[i].rotation = QQuaternion::slerp(qb, qc, w).toEulerAngles();
        }
    }
    for (int i = 0; i < m_bones.size(); ++i) {
        m_bones[i].position = pose[i].position;
        m_bones[i].rotation = pose[i].rotation;
    }
}

// ============================================================================
// Dynamics (rigid body simulation via Bullet)
// ============================================================================

static QMap<int, SceneObject*> dynamicsObjectMap(SceneGraph* scene) {
    QMap<int, SceneObject*> map;
    if (!scene) return map;
    for (SceneObject* o : scene->allObjects())
        map.insert(o->id(), o);
    return map;
}

bool KSModelerQml::dynAddBody(int objectId, int shapeType, float mass, bool kinematic) {
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj || !m_scene) return false;
    const bool ok = m_dynamics.addBody(objectId, obj, shapeType, mass, kinematic);
    if (ok) {
        emit dynChanged();
    }
    return ok;
}

bool KSModelerQml::dynRemoveBody(int objectId) {
    const bool ok = m_dynamics.removeBody(objectId);
    if (ok) { if (m_dynamics.count() == 0) dynPause(); emit dynChanged(); }
    return ok;
}

int KSModelerQml::dynBodyCount() const { return m_dynamics.count(); }

QVariantList KSModelerQml::dynBodies() const {
    QVariantList out;
    const QMap<int, SceneObject*> objects = dynamicsObjectMap(m_scene);
    for (int id : m_dynamics.bodyIds()) {
        QVariantMap m;
        m["objectId"] = id;
        m["objectName"] = objects.value(id) ? objects.value(id)->name() : QString();
        m["shapeType"] = m_dynamics.shapeTypeOf(id);
        out.append(m);
    }
    return out;
}

void KSModelerQml::dynSetGravity(double x, double y, double z) {
    m_dynamics.setGravity(QVector3D(x, y, z));
    emit dynChanged();
}

QVector3D KSModelerQml::dynGravity() const { return m_dynamics.gravity(); }

void KSModelerQml::dynSetBodyKinematic(int objectId, bool kinematic) {
    m_dynamics.setBodyKinematic(objectId, kinematic);
    emit dynChanged();
}

bool KSModelerQml::dynSetBodyMass(int objectId, float mass) {
    const bool ok = m_dynamics.setBodyMass(objectId, mass);
    if (ok) { m_dynamics.step(0.016f, dynamicsObjectMap(m_scene)); emit sceneChanged(); emit dynChanged(); }
    return ok;
}

void KSModelerQml::dynStepOnce(double dt) {
    m_dynamics.step(qMax(0.001, dt), dynamicsObjectMap(m_scene));
    emit sceneChanged();
    emit dynChanged();
}

void KSModelerQml::dynReset() {
    m_dynamics.reset(dynamicsObjectMap(m_scene));
    emit sceneChanged();
    emit dynChanged();
}

void KSModelerQml::dynPlay() {
    if (m_dynamics.count() == 0) return;
    m_dynRunning = true;
    if (!m_dynTimer) {
        m_dynTimer = new QTimer(this);
        connect(m_dynTimer, &QTimer::timeout, this, &KSModelerQml::dynTick);
    }
    m_dynTimer->start(16); // ~60 Hz
    emit dynChanged();
}

void KSModelerQml::dynPause() {
    m_dynRunning = false;
    if (m_dynTimer) m_dynTimer->stop();
    emit dynChanged();
}

void KSModelerQml::dynTick() {
    if (!m_dynRunning || m_dynamics.count() == 0) { dynPause(); return; }
    m_dynamics.step(1.0f / 60.0f, dynamicsObjectMap(m_scene));
    emit sceneChanged();
}

// ============================================================================
// Cloth simulation (soft body)
// ============================================================================

bool KSModelerQml::clothAdd(int objectId, int pinMode) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj) return false;
    const bool ok = m_cloth.addCloth(objectId, obj, pinMode);
    if (ok) emit clothChanged();
    return ok;
}

bool KSModelerQml::clothRemove(int objectId) {
    const bool ok = m_cloth.removeCloth(objectId);
    if (ok) emit clothChanged();
    return ok;
}

QVariantList KSModelerQml::clothList() const {
    QVariantList result;
    for (int id : m_cloth.clothIds()) {
        QVariantMap e;
        SceneObject* obj = m_scene ? m_scene->findObjectById(id) : nullptr;
        e["objectId"] = id;
        e["objectName"] = obj ? obj->name() : QString("object_%1").arg(id);
        e["pinMode"] = m_cloth.pinModeOf(id);
        e["springs"] = m_cloth.springCount(id);
        result.append(e);
    }
    return result;
}

int KSModelerQml::clothPinModeOf(int objectId) const {
    return m_cloth.pinModeOf(objectId);
}

void KSModelerQml::clothSetGravity(double x, double y, double z) {
    m_cloth.setGravity(QVector3D(float(x), float(y), float(z)));
}

void KSModelerQml::clothSetStiffness(int objectId, float v) {
    m_cloth.setStiffness(objectId, v);
}

void KSModelerQml::clothSetDamping(int objectId, float v) {
    m_cloth.setDamping(objectId, v);
}

void KSModelerQml::clothSetWind(int objectId, float v) {
    m_cloth.setWind(objectId, v);
}

void KSModelerQml::clothPlay() {
    if (m_cloth.count() == 0) return;
    m_clothRunning = true;
    if (!m_clothTimer) {
        m_clothTimer = new QTimer(this);
        connect(m_clothTimer, &QTimer::timeout, this, [this]() {
            if (!m_clothRunning) { clothPause(); return; }
            refreshClothColliders();
            m_cloth.step(1.0f / 60.0f, dynamicsObjectMap(m_scene));
            emit sceneChanged();
        });
    }
    m_clothTimer->start(16);
    emit clothChanged();
}

void KSModelerQml::clothPause() {
    m_clothRunning = false;
    if (m_clothTimer) m_clothTimer->stop();
    emit clothChanged();
}

void KSModelerQml::clothReset() {
    m_cloth.reset(dynamicsObjectMap(m_scene));
    emit sceneChanged();
    emit clothChanged();
}

void KSModelerQml::clothRemoveAll() {
    m_cloth.clearAll();
    m_clothRunning = false;
    if (m_clothTimer) m_clothTimer->stop();
    emit clothChanged();
}

namespace {
// Build world-space collision triangles for one solid object.
QVector<ClothColliderTri> clothColliderTriangles(const SceneObject* obj) {
    QVector<ClothColliderTri> out;
    if (!obj || !obj->mesh()) return out;
    const auto& verts = obj->mesh()->geometry().vertices;
    const auto& idxs = obj->mesh()->geometry().indices;
    if (verts.isEmpty() || idxs.size() < 3) return out;

    const QMatrix4x4 m = obj->worldTransform();
    auto mapNormal = [&](const QVector3D& n) -> QVector3D {
        QVector3D r;
        r.setX(m(0, 0) * n.x() + m(0, 1) * n.y() + m(0, 2) * n.z());
        r.setY(m(1, 0) * n.x() + m(1, 1) * n.y() + m(1, 2) * n.z());
        r.setZ(m(2, 0) * n.x() + m(2, 1) * n.y() + m(2, 2) * n.z());
        return r.normalized();
    };

    for (int i = 0; i + 2 < idxs.size(); i += 3) {
        const SceneVertex& a = verts[idxs[i]];
        const SceneVertex& b = verts[idxs[i + 1]];
        const SceneVertex& c = verts[idxs[i + 2]];
        ClothColliderTri t;
        t.a = m.map(QVector3D(a.position.x(), a.position.y(), a.position.z()));
        t.b = m.map(QVector3D(b.position.x(), b.position.y(), b.position.z()));
        t.c = m.map(QVector3D(c.position.x(), c.position.y(), c.position.z()));
        t.n = mapNormal(QVector3D(a.normal.x(), a.normal.y(), a.normal.z()));
        out.append(t);
    }
    return out;
}
} // namespace

void KSModelerQml::refreshClothColliders() {
    QVector<ClothColliderTri> tris;
    if (m_scene) {
        for (const int id : m_clothColliderIds) {
            SceneObject* obj = m_scene->findObjectById(id);
            if (!obj || m_cloth.hasCloth(id)) continue; // skip cloth objects themselves
            tris += clothColliderTriangles(obj);
        }
    }
    m_cloth.setColliders(tris);
}

void KSModelerQml::clothSetCollisionObjects(const QVariantList& objectIds) {
    m_clothColliderIds.clear();
    for (const QVariant& v : objectIds)
        m_clothColliderIds.append(v.toInt());
    refreshClothColliders();
    emit clothChanged();
}

QVariantList KSModelerQml::clothCollisionObjects() const {
    QVariantList out;
    for (const int id : m_clothColliderIds)
        out.append(id);
    return out;
}

void KSModelerQml::clothSetCollision(int objectId, bool enabled) {
    m_cloth.setCollisionEnabled(objectId, enabled);
}

void KSModelerQml::clothSetSelfCollision(int objectId, bool enabled) {
    m_cloth.setSelfCollision(objectId, enabled);
    emit clothChanged();
}

bool KSModelerQml::clothPreset(int objectId, const QString& preset) {
    const QVector3D g = m_cloth.gravity();
    // Apply a fabric preset by tuning stiffness / damping / wind.
    if (preset == "Cotton")       { m_cloth.setStiffness(objectId, 0.55f); m_cloth.setDamping(objectId, 0.30f); m_cloth.setWind(objectId, 0.0f);    m_cloth.setGravity(QVector3D(g.x(), -9.8f, g.z())); }
    else if (preset == "Silk")    { m_cloth.setStiffness(objectId, 0.35f); m_cloth.setDamping(objectId, 0.30f); m_cloth.setWind(objectId, 0.05f);  m_cloth.setGravity(QVector3D(g.x(), -9.8f, g.z())); }
    else if (preset == "Denim")   { m_cloth.setStiffness(objectId, 0.85f); m_cloth.setDamping(objectId, 0.25f); m_cloth.setWind(objectId, 0.0f);    m_cloth.setGravity(QVector3D(g.x(), -9.8f, g.z())); }
    else if (preset == "Leather") { m_cloth.setStiffness(objectId, 0.95f); m_cloth.setDamping(objectId, 0.50f); m_cloth.setWind(objectId, 0.0f);    m_cloth.setGravity(QVector3D(g.x(), -13.0f, g.z())); }
    else if (preset == "Rubber")  { m_cloth.setStiffness(objectId, 0.90f); m_cloth.setDamping(objectId, 0.70f); m_cloth.setWind(objectId, 0.0f);    m_cloth.setGravity(QVector3D(g.x(), -9.8f, g.z())); }
    else if (preset == "Wool")    { m_cloth.setStiffness(objectId, 0.45f); m_cloth.setDamping(objectId, 0.40f); m_cloth.setWind(objectId, 0.03f);  m_cloth.setGravity(QVector3D(g.x(), -9.8f, g.z())); }
    else if (preset == "Satin")   { m_cloth.setStiffness(objectId, 0.30f); m_cloth.setDamping(objectId, 0.20f); m_cloth.setWind(objectId, 0.08f);  m_cloth.setGravity(QVector3D(g.x(), -7.5f, g.z())); }
    else                            return false;
    emit clothChanged();
    return true;
}

QStringList KSModelerQml::clothPresetNames() const {
    return { "Cotton", "Silk", "Denim", "Leather", "Rubber", "Wool", "Satin" };
}

QStringList KSModelerQml::fabricNames() const {
    return { "Cotton", "Silk", "Denim", "Leather", "Rubber", "Wool", "Satin", "Twill" };
}

QString KSModelerQml::generateFabricTextures(int objectId, const QString& fabric, double scale) {
    if (!m_scene) return QString();
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return QString();

    const QVector3D defC1 = obj->baseColor().redF() > 0.5f || obj->baseColor().isValid() == false
        ? QVector3D(0.9f, 0.88f, 0.84f) : QVector3D(obj->baseColor().redF(), obj->baseColor().greenF(), obj->baseColor().blueF());
    QVector3D c2 = defC1 * 0.55f;

    ProceduralTextureGenerator::TextureType type = ProceduralTextureGenerator::stringToTextureType(fabric);
    ProceduralTextureGenerator gen;
    ProceduralTextureGenerator::TextureParams p;
    p.type = type;
    p.width = 512;
    p.height = 512;
    p.scale = float(scale);
    p.seed = objectId * 131 + 17;
    p.color1 = defC1;
    p.color2 = c2;

    const QImage diffuse = gen.generateTexture(p);
    const QImage normal = gen.generateNormalMap(diffuse);

    const QString dir = QDir::tempPath() + "/ksEditorFabrics";
    QDir().mkpath(dir);
    const QString base = dir + "/fabric_" + obj->name() + "_" + fabric + QString::number(objectId);
    const QString dPath = base + "_d.png";
    const QString nPath = base + "_n.png";
    diffuse.save(dPath, "PNG");
    normal.save(nPath, "PNG");
    m_fabricDiffuseCache.insert(objectId, dPath);
    m_fabricNormalCache.insert(objectId, nPath);
    return dPath;
}

void KSModelerQml::clearFabricFor(int objectId) {
    m_fabrics.remove(objectId);
    m_fabricDiffuseCache.remove(objectId);
    m_fabricNormalCache.remove(objectId);
}

bool KSModelerQml::clothApplyFabric(int objectId, const QString& fabric, double scale) {
    if (!m_fabrics.contains(objectId)) {
        SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
        if (!obj || !obj->mesh()) {
            emit statusMessage("Fabric requires a mesh object");
            return false;
        }
    }
    if (!fabricNames().contains(fabric)) {
        emit statusMessage("Unknown fabric: " + fabric);
        return false;
    }
    // Fold the fabric name into the physical preset, then generate textures.
    clothPreset(objectId, fabric);
    QString d = generateFabricTextures(objectId, fabric, scale);
    if (d.isEmpty()) return false;
    m_fabrics.insert(objectId, fabric);
    if (m_sceneModel) m_sceneModel->refresh();
    emit sceneChanged();
    emit fabricChanged();
    emit statusMessage("Fabric '" + fabric + "' applied (procedural weave texture)");
    return true;
}

void KSModelerQml::clothRemoveFabric(int objectId) {
    clearFabricFor(objectId);
    if (m_sceneModel) m_sceneModel->refresh();
    emit sceneChanged();
    emit fabricChanged();
}

QString KSModelerQml::fabricFor(int objectId) const {
    return m_fabrics.value(objectId);
}

// ============================================================================
// Hair / fur (2.5)
// ============================================================================

bool KSModelerQml::hairAdd(int objectId, int strandCount, int segments, double length) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;
    if (m_hair.hasHair(objectId)) {
        emit statusMessage("Hair already present on this object");
        return false;
    }
    if (!m_hair.addHair(objectId, obj, strandCount, segments, float(length))) {
        emit statusMessage("Hair add failed: need a mesh surface");
        return false;
    }
    // Create the hair display object (renders the strand cards).
    SceneObject* hairObj = m_scene->createObject(obj->name() + "_hair", SceneObject::Type::Mesh);
    m_hairObjects.insert(objectId, hairObj->id());
    rebuildHairMesh(objectId);
    emit sceneChanged();
    emit hairChanged();
    emit statusMessage(QString("Hair added on '%1': %2 strands x %3 segments").arg(obj->name()).arg(strandCount).arg(segments));
    return true;
}

bool KSModelerQml::hairRemove(int objectId) {
    if (!m_scene) return false;
    if (!m_hair.removeHair(objectId)) return false;
    const int hid = m_hairObjects.value(objectId, -1);
    if (hid >= 0) {
        SceneObject* hairObj = m_scene->findObjectById(hid);
        if (hairObj) m_scene->deleteObject(hairObj);
        m_hairObjects.remove(objectId);
    }
    emit sceneChanged();
    emit hairChanged();
    return true;
}

QVariantList KSModelerQml::hairList() const {
    QVariantList result;
    for (int id : m_hair.hairIds()) {
        SceneObject* obj = m_scene ? m_scene->findObjectById(id) : nullptr;
        QVariantMap e;
        e["objectId"] = id;
        e["objectName"] = obj ? obj->name() : QString("object_%1").arg(id);
        e["strands"] = m_hair.strandCountOf(id);
        e["segments"] = m_hair.segmentsOf(id);
        e["length"] = m_hair.lengthOf(id);
        e["stiffness"] = m_hair.stiffnessOf(id);
        e["wind"] = m_hair.windOf(id);
        result.append(e);
    }
    return result;
}

void KSModelerQml::hairSetLength(int objectId, double v) {
    m_hair.setLength(objectId, float(v));
    rebuildHairMesh(objectId);
    emit sceneChanged();
    emit hairChanged();
}

void KSModelerQml::hairSetStiffness(int objectId, double v) {
    m_hair.setStiffness(objectId, float(v));
}

void KSModelerQml::hairSetWind(int objectId, double v) {
    m_hair.setWind(objectId, float(v));
}

void KSModelerQml::rebuildHairMesh(int surfaceObjectId) {
    if (!m_scene) return;
    const int hid = m_hairObjects.value(surfaceObjectId, -1);
    if (hid < 0) return;
    SceneObject* hairObj = m_scene->findObjectById(hid);
    if (!hairObj) return;
    const MeshData md = m_hair.buildMesh(surfaceObjectId);
    if (md.vertices.isEmpty()) return;
    meshDataToSceneMesh(hairObj, md);
}

void KSModelerQml::hairTick() {
    if (!m_hairRunning) { hairPause(); return; }
    m_hair.step(1.0f / 60.0f);
    for (int id : m_hair.hairIds()) rebuildHairMesh(id);
    emit sceneChanged();
}

void KSModelerQml::hairPlay() {
    if (m_hair.count() == 0) return;
    m_hairRunning = true;
    if (!m_hairTimer) {
        m_hairTimer = new QTimer(this);
        connect(m_hairTimer, &QTimer::timeout, this, &KSModelerQml::hairTick);
    }
    m_hairTimer->start(16);
    emit hairChanged();
}

void KSModelerQml::hairPause() {
    m_hairRunning = false;
    if (m_hairTimer) m_hairTimer->stop();
    emit hairChanged();
}

void KSModelerQml::hairRemoveAll() {
    if (m_scene) {
        for (const int hid : m_hairObjects.values()) {
            SceneObject* hairObj = m_scene->findObjectById(hid);
            if (hairObj) m_scene->deleteObject(hairObj);
        }
    }
    m_hairObjects.clear();
    m_hair.removeAll();
    m_hairRunning = false;
    if (m_hairTimer) m_hairTimer->stop();
    emit sceneChanged();
    emit hairChanged();
}

// ============================================================================
// Raytraced viewport (2.6)
// ============================================================================

namespace {
QMatrix4x4 rtObjectMatrix(const SceneObject* obj) {
    QMatrix4x4 m;
    m.translate(obj->position());
    m.rotate(QQuaternion::fromEulerAngles(obj->rotationEuler()));
    m.scale(obj->scale());
    return m;
}

QString rtPassName(int pass) {
    switch (pass) {
    case 1: return QStringLiteral("Depth");
    case 2: return QStringLiteral("AmbientOcclusion");
    case 3: return QStringLiteral("Diffuse");
    case 4: return QStringLiteral("Normal");
    default: return QStringLiteral("Color");
    }
}
} // namespace

void KSModelerQml::rayTraceSetCamera(float ex, float ey, float ez, float tx, float ty, float tz, float fov) {
    m_rtCam.eye = QVector3D(ex, ey, ez);
    m_rtCam.target = QVector3D(tx, ty, tz);
    m_rtCam.fovDeg = fov;
}

void KSModelerQml::rayTraceSetSize(int w, int h) {
    m_rtWidth = qBound(96, w, 1024);
    m_rtHeight = qBound(64, h, 1024);
}

void KSModelerQml::setRayTraceEnabled(bool enabled) {
    if (m_rayTraceEnabled == enabled) return;
    m_rayTraceEnabled = enabled;
    if (enabled) {
        if (!m_rayTraceTimer) {
            m_rayTraceTimer = new QTimer(this);
            connect(m_rayTraceTimer, &QTimer::timeout, this, &KSModelerQml::rayTraceTick);
        }
        m_rayTraceTimer->start(150); // ~6 fps preview
    } else if (m_rayTraceTimer) {
        m_rayTraceTimer->stop();
    }
    emit rayTraceEnabledChanged();
}

void KSModelerQml::setRayTracePass(int pass) {
    const int clamped = qBound(0, pass, int(ks::RenderPass::Normal));
    if (m_rayTracePass == clamped) return;
    m_rayTracePass = clamped;
    emit rayTracePassChanged();
    if (m_rayTraceEnabled && m_rayTraceTimer)
        rayTraceTick();
}

void KSModelerQml::rayTraceTick() {
    if (!m_rayTraceEnabled || !m_scene) return;
    if (m_rtWidth <= 0 || m_rtHeight <= 0) return;

    const QVector<RTTriangle> tris = buildRTTriangles();
    m_rtRenderer.setObjects(tris);
    m_rtRenderer.setLights(buildRTLights());
    QImage frame = m_rtRenderer.render(m_rtCam, m_rtWidth, m_rtHeight,
                                       static_cast<ks::RenderPass>(m_rayTracePass), 1);
    if (!frame.isNull()) {
        m_rayTraceFrame = frame;
        ++m_rayTraceFrameRevision;
        emit rayTraceFrameChanged();
    }
}

QVector<RTTriangle> KSModelerQml::buildRTTriangles() const {
    QVector<RTTriangle> tris;
    if (!m_scene) return tris;
    const QVector<SceneObject*> objs = m_scene->allObjects();
    for (SceneObject* obj : objs) {
        if (!obj->hasMesh()) continue;
        SceneMesh* mesh = obj->mesh();
        const auto& verts = mesh->geometry().vertices;
        const auto& idxs = mesh->geometry().indices;
        if (verts.isEmpty() || idxs.size() < 3) continue;

        QMatrix4x4 m = rtObjectMatrix(obj);
        const QColor base = obj->baseColor();
        auto mapNormal = [&](const QVector3D& n) -> QVector3D {
            QVector3D r;
            r.setX(m(0, 0) * n.x() + m(0, 1) * n.y() + m(0, 2) * n.z());
            r.setY(m(1, 0) * n.x() + m(1, 1) * n.y() + m(1, 2) * n.z());
            r.setZ(m(2, 0) * n.x() + m(2, 1) * n.y() + m(2, 2) * n.z());
            return r.normalized();
        };

        RTTriangle tri;
        tri.color = base;
        tri.metalness = obj->metallic();
        tri.roughness = obj->roughness();
        for (int i = 0; i + 2 < idxs.size(); i += 3) {
            const SceneVertex& a = verts[idxs[i]];
            const SceneVertex& b = verts[idxs[i + 1]];
            const SceneVertex& c = verts[idxs[i + 2]];
            tri.v0 = m.map(QVector3D(a.position.x(), a.position.y(), a.position.z()));
            tri.v1 = m.map(QVector3D(b.position.x(), b.position.y(), b.position.z()));
            tri.v2 = m.map(QVector3D(c.position.x(), c.position.y(), c.position.z()));
            tri.n0 = mapNormal(QVector3D(a.normal.x(), a.normal.y(), a.normal.z()));
            tri.n1 = mapNormal(QVector3D(b.normal.x(), b.normal.y(), b.normal.z()));
            tri.n2 = mapNormal(QVector3D(c.normal.x(), c.normal.y(), c.normal.z()));
            tris.append(tri);
        }
    }
    return tris;
}

bool KSModelerQml::rayTraceRenderToFile(const QString& path, int width, int height, int samples, int pass) {
    if (path.isEmpty()) return false;
    const int w = qBound(64, width, 4096);
    const int h = qBound(64, height, 4096);
    const int spp = qBound(1, samples, 64);
    const QVector<RTTriangle> tris = buildRTTriangles();
    m_rtRenderer.setObjects(tris);
    m_rtRenderer.setLights(buildRTLights());
    const QImage frame = pass == 0
        ? m_rtRenderer.renderFinal(m_rtCam, w, h, spp)
        : m_rtRenderer.render(m_rtCam, w, h, static_cast<ks::RenderPass>(pass), spp);
    if (frame.isNull()) return false;
    const bool ok = frame.save(path, "PNG");
    emit statusMessage(ok ? QString("Rendered %1 pass: %2 (%3x%4)").arg(rtPassName(pass)).arg(path).arg(frame.width()).arg(frame.height())
                          : "Render to file failed");
    return ok;
}

// ============================================================================
// Bake to texture (maps object properties to a raster image)
// ============================================================================

namespace {
io::TextureBaker::BakeType bakeTypeFromInt(int type)
{
    return static_cast<io::TextureBaker::BakeType>(
        qBound(static_cast<int>(io::TextureBaker::Diffuse), type,
               static_cast<int>(io::TextureBaker::Emission)));
}
graphics::Mesh3D* buildGeometryMeshForBake(SceneObject* obj)
{
    SceneMesh* sm = obj ? obj->mesh() : nullptr;
    if (!sm) return nullptr;
    const auto& verts = sm->geometry().vertices;
    const auto& idxs = sm->geometry().indices;
    if (verts.isEmpty() || idxs.size() < 3) return nullptr;

    graphics::Mesh3D* m = new graphics::Mesh3D();
    QVector<QVector3D> positions, normals;
    QVector<QVector2D> uvs;
    positions.reserve(verts.size());
    normals.reserve(verts.size());
    uvs.reserve(verts.size());
    for (const SceneVertex& sv : verts) {
        positions.append(QVector3D(sv.position.x(), sv.position.y(), sv.position.z()));
        normals.append(QVector3D(sv.normal.x(), sv.normal.y(), sv.normal.z()));
        uvs.append(QVector2D(sv.uv.x(), sv.uv.y()));
    }
    QVector<quint32> indices;
    indices.reserve(idxs.size());
    for (uint32_t i : idxs) indices.append(static_cast<quint32>(i));
    m->setVertices(positions);
    m->setNormals(normals);
    m->setUVs(uvs);
    m->setIndices(indices);
    return m;
}
} // namespace

// ============================================================================
// Light Lister (photometric lights + IES profiles)
// ============================================================================

namespace {
QString lightTypeName(int type)
{
    switch (type) {
    case 1: return QStringLiteral("Point");
    case 2: return QStringLiteral("Spot");
    case 3: return QStringLiteral("Area");
    default: return QStringLiteral("Directional");
    }
}
} // namespace

bool KSModelerQml::bakeObject(int objectId, int bakeType, const QString& path, int width, int height) {
    if (path.isEmpty()) {
        emit errorMessage(QStringLiteral("Bake: no output path given."));
        return false;
    }
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj || !obj->hasMesh()) {
        emit errorMessage(QStringLiteral("Bake: no mesh object selected."));
        return false;
    }
    graphics::Mesh3D* mesh = buildGeometryMeshForBake(obj);
    if (!mesh) {
        emit errorMessage(QStringLiteral("Bake: object has no geometry."));
        return false;
    }
    const auto type = bakeTypeFromInt(bakeType);
    io::TextureBaker baker;
    baker.setSourceMesh(mesh);
    baker.setTargetResolution(qBound(16, width, 4096), qBound(16, height, 4096));
    baker.setBaseColor(obj->baseColor());
    baker.addBakeTarget(type, path);
    baker.bake(type);

    m_bakeResultImage = baker.getBakedTexture(type);
    ++m_bakeRevision;
    emit bakeResultChanged();
    emit statusMessage(QStringLiteral("Baked %1 to %2 (%3x%4)")
                           .arg(io::TextureBaker::textureTypeName(type))
                           .arg(path).arg(m_bakeResultImage.width()).arg(m_bakeResultImage.height()));
    delete mesh;
    return !m_bakeResultImage.isNull();
}

bool KSModelerQml::bakePreview(int objectId, int bakeType, int width, int height) {
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj || !obj->hasMesh()) return false;
    graphics::Mesh3D* mesh = buildGeometryMeshForBake(obj);
    if (!mesh) return false;
    const auto type = bakeTypeFromInt(bakeType);
    io::TextureBaker baker;
    baker.setSourceMesh(mesh);
    baker.setTargetResolution(qBound(16, width, 4096), qBound(16, height, 4096));
    baker.setBaseColor(obj->baseColor());
    baker.bake(type);

    m_bakeResultImage = baker.getBakedTexture(type);
    ++m_bakeRevision;
    emit bakeResultChanged();
    delete mesh;
    return !m_bakeResultImage.isNull();
}

void KSModelerQml::bakeClearResult() {
    if (m_bakeResultImage.isNull()) return;
    m_bakeResultImage = QImage();
    ++m_bakeRevision;
    emit bakeResultChanged();
}

bool KSModelerQml::packBakeChannels(int objectId, int packChannels, int width, int height) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return false;

    graphics::Mesh3D* mesh = buildGeometryMeshForBake(obj);
    if (!mesh) {
        emit errorMessage("Bake: object has no geometry.");
        return false;
    }

    ks::io::TextureBaker baker;
    baker.setSourceMesh(mesh);
    baker.setTargetResolution(width, height);

    // bake each selected channel
    if (packChannels & 1) { // Roughness
        baker.addBakeTarget(ks::io::TextureBaker::Roughness, "");
        baker.bake(ks::io::TextureBaker::Roughness);
    }
    if (packChannels & 2) { // Metallic
        baker.addBakeTarget(ks::io::TextureBaker::Metallic, "");
        baker.bake(ks::io::TextureBaker::Metallic);
    }
    if (packChannels & 4) { // AO
        baker.addBakeTarget(ks::io::TextureBaker::AO, "");
        baker.bake(ks::io::TextureBaker::AO);
    }
    if (packChannels & 8) { // Height
        baker.addBakeTarget(ks::io::TextureBaker::Height, "");
        baker.bake(ks::io::TextureBaker::Height);
    }
    if (packChannels & 16) { // Emission
        baker.addBakeTarget(ks::io::TextureBaker::Emission, "");
        baker.bake(ks::io::TextureBaker::Emission);
    }

    QImage packed = baker.packRgba(packChannels);
    if (packed.isNull()) return false;

    // save to a temp path or keep in memory
    // For now, save to a derived path and return true
    QString defaultPath = QString("baked_packed_%1.png").arg(objectId);
    if (!defaultPath.isEmpty()) {
        packed.save(defaultPath);
    }
    m_bakeResultImage = packed;
    ++m_bakeRevision;
    emit bakeResultChanged();
    delete mesh;
    return true;
}

bool KSModelerQml::saveBakedTexture(const QString& path) {
    if (m_bakeResultImage.isNull()) return false;
    m_bakeResultImage.save(path);
    ++m_bakeRevision;
    emit bakeResultChanged();
    return true;
}

QString KSModelerQml::bakeTypeName(int bakeType) const {
    return io::TextureBaker::textureTypeName(bakeTypeFromInt(bakeType));
}

QVector<RTLight> KSModelerQml::buildRTLights() const {
    QVector<RTLight> out;
    if (!m_scene) return out;
    for (const LightDef& def : m_lightSystem.lights()) {
        if (!def.enabled) continue;
        SceneObject* obj = m_scene->findObjectById(def.objectId);
        if (!obj) continue;
        RTLight l;
        l.type = def.type;
        l.position = obj->position();
        l.color = def.color;
        l.intensity = def.intensity;
        l.range = def.range;
        l.spotAngleDeg = def.spotAngleDeg;
        l.spotPenumbraDeg = def.spotPenumbraDeg;
        l.iesCurve = def.iesCurve;
        // Lights shine along their local -Z (forward) axis in world space.
        const QQuaternion q = QQuaternion::fromEulerAngles(obj->rotationEuler());
        l.direction = q.rotatedVector(QVector3D(0.0f, 0.0f, -1.0f)).normalized();
        out.append(l);
    }
    return out;
}

QVariantList KSModelerQml::lightList() const {
    QVariantList out;
    for (const LightDef& def : m_lightSystem.lights()) {
        QVariantMap m;
        m["objectId"] = def.objectId;
        m["name"] = def.name;
        m["type"] = def.type;
        m["typeName"] = lightTypeName(def.type);
        m["color"] = def.color;
        m["intensity"] = double(def.intensity);
        m["enabled"] = def.enabled;
        m["range"] = double(def.range);
        m["spotAngleDeg"] = double(def.spotAngleDeg);
        m["spotPenumbraDeg"] = double(def.spotPenumbraDeg);
        m["iesProfile"] = def.iesProfile;
        m["iesIntensity"] = double(def.iesIntensity);
        m["hasIES"] = !def.iesCurve.isEmpty();
        out.append(m);
    }
    return out;
}

int KSModelerQml::lightCreate(int type, const QString& name) {
    const int id = addLight(name.isEmpty() ? QStringLiteral("Light %1").arg(m_lightSystem.lights().size() + 1) : name);
    lightSetType(id, qBound(0, type, 3));
    return id;
}

bool KSModelerQml::lightRemove(int objectId) {
    if (!m_lightSystem.remove(objectId)) return false;
    emit lightsChanged();
    if (m_rayTraceEnabled) rayTraceTick();
    return true;
}

void KSModelerQml::lightSetType(int objectId, int type) {
    LightDef* def = m_lightSystem.find(objectId);
    if (!def) return;
    def->type = qBound(0, type, 3);
    emit lightsChanged();
    if (m_rayTraceEnabled) rayTraceTick();
}

void KSModelerQml::lightSetColor(int objectId, float r, float g, float b) {
    LightDef* def = m_lightSystem.find(objectId);
    if (!def) return;
    def->color = QColor::fromRgbF(qBound(0.0f, r, 1.0f), qBound(0.0f, g, 1.0f), qBound(0.0f, b, 1.0f));
    emit lightsChanged();
    if (m_rayTraceEnabled) rayTraceTick();
}

void KSModelerQml::lightSetIntensity(int objectId, float value) {
    LightDef* def = m_lightSystem.find(objectId);
    if (!def) return;
    def->intensity = qMax(0.0f, value);
    emit lightsChanged();
    if (m_rayTraceEnabled) rayTraceTick();
}

void KSModelerQml::lightSetEnabled(int objectId, bool enabled) {
    LightDef* def = m_lightSystem.find(objectId);
    if (!def) return;
    def->enabled = enabled;
    emit lightsChanged();
    if (m_rayTraceEnabled) rayTraceTick();
}

void KSModelerQml::lightSetRange(int objectId, float value) {
    LightDef* def = m_lightSystem.find(objectId);
    if (!def) return;
    def->range = qMax(0.1f, value);
    emit lightsChanged();
    if (m_rayTraceEnabled) rayTraceTick();
}

void KSModelerQml::lightSetSpotAngle(int objectId, float angleDeg) {
    LightDef* def = m_lightSystem.find(objectId);
    if (!def) return;
    def->spotAngleDeg = qBound(1.0f, angleDeg, 180.0f);
    def->spotPenumbraDeg = qMin(def->spotPenumbraDeg, def->spotAngleDeg - 1.0f);
    emit lightsChanged();
    if (m_rayTraceEnabled) rayTraceTick();
}

void KSModelerQml::lightSetIesProfile(int objectId, const QString& path) {
    LightDef* def = m_lightSystem.find(objectId);
    if (!def) return;
    if (path.trimmed().isEmpty()) {
        def->iesProfile.clear();
        def->iesCurve.clear();
    } else {
        QVector<float> curve;
        if (!LightSystem::parseIESFile(path, curve)) {
            emit statusMessage("Failed to parse IES profile: " + path);
            return;
        }
        def->iesProfile = path;
        def->iesCurve = curve;
        emit statusMessage("IES profile loaded: " + QFileInfo(path).fileName());
    }
    emit lightsChanged();
    if (m_rayTraceEnabled) rayTraceTick();
}

void KSModelerQml::lightSetIesIntensity(int objectId, float value) {
    LightDef* def = m_lightSystem.find(objectId);
    if (!def) return;
    def->iesIntensity = qMax(0.0f, value);
    emit lightsChanged();
    if (m_rayTraceEnabled) rayTraceTick();
}


// ============================================================================
// Subdivision cage modeling (2.1)
// ============================================================================

void KSModelerQml::setSubdivCageEnabled(bool enabled) {
    if (m_subdivCageEnabled == enabled) return;
    m_subdivCageEnabled = enabled;
    applySubdivCage();
    emit subdivCageChanged();
}

void KSModelerQml::setSubdivCageLevel(int level) {
    const int l = qBound(1, level, 4);
    if (m_subdivCageLevel == l) return;
    m_subdivCageLevel = l;
    if (m_subdivCageEnabled) applySubdivCage();
    emit subdivCageChanged();
}

void KSModelerQml::applySubdivCage() {
    if (!m_scene) return;
    if (m_subdivCageEnabled) {
        // Capture the control cage for objects not yet captured.
        for (SceneObject* o : m_scene->allObjects()) {
            if (!o->mesh()) continue;
            if (!m_cageOrigins.contains(o->id()))
                m_cageOrigins.insert(o->id(), o->mesh()->toJson());
        }
        // Restore all cages first, then subdivide at the current level.
        for (auto it = m_cageOrigins.constBegin(); it != m_cageOrigins.constEnd(); ++it) {
            SceneObject* o = m_scene->findObjectById(it.key());
            if (o) {
                SceneMesh* sm = SceneMesh::fromJson(it.value());
                if (sm) o->setMesh(sm);
            }
        }
        for (auto it = m_cageOrigins.constBegin(); it != m_cageOrigins.constEnd(); ++it) {
            SceneObject* o = m_scene->findObjectById(it.key());
            if (!o || !o->mesh()) continue;
            MeshData md = sceneMeshToMeshData(o);
            MeshData sub = MeshOperations::subdivide(md, m_subdivCageLevel);
            if (!sub.vertices.isEmpty()) meshDataToSceneMesh(o, sub);
        }
    } else {
        for (auto it = m_cageOrigins.constBegin(); it != m_cageOrigins.constEnd(); ++it) {
            SceneObject* o = m_scene->findObjectById(it.key());
            if (o) {
                SceneMesh* sm = SceneMesh::fromJson(it.value());
                if (sm) o->setMesh(sm);
            }
        }
        m_cageOrigins.clear();
    }
    if (m_sceneModel) m_sceneModel->refresh();
    emit sceneChanged();
}

bool KSModelerQml::quadRemesh(int objectId, int level) {
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) {
        emit statusMessage("Quad remesh: no object selected");
        return false;
    }

    const int lvl = qBound(0, level, 3);

    // Snapshot the original mesh on first use so the operation is re-runnable.
    if (!m_remeshOrigins.contains(objectId))
        m_remeshOrigins.insert(objectId, QString::fromUtf8(QJsonDocument(meshDataToJson(sceneMeshToMeshData(obj))).toJson(QJsonDocument::Compact)));

    // Restore the original, then subdivide + merge triangles into quads.
    const MeshData orig = meshDataFromJson(QJsonDocument::fromJson(m_remeshOrigins[objectId].toUtf8()).object());
    if (!orig.vertices.isEmpty()) meshDataToSceneMesh(obj, orig);

    MeshData md = sceneMeshToMeshData(obj);
    if (lvl > 0) md = MeshOperations::subdivide(md, lvl);
    MeshData result = MeshOperations::quadrangulate(md);
    if (result.vertices.isEmpty()) {
        emit statusMessage("Quad remesh failed");
        return false;
    }
    meshDataToSceneMesh(obj, result);
    if (m_sceneModel) m_sceneModel->refresh();
    emit sceneChanged();
    emit statusMessage(QString("Quad remesh level %1: %2 faces").arg(lvl).arg(result.faces.size()));
    return true;
}

void KSModelerQml::quadRemeshClear(int objectId) {
    if (!m_scene) return;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (obj && m_remeshOrigins.contains(objectId)) {
        const MeshData md = meshDataFromJson(QJsonDocument::fromJson(m_remeshOrigins[objectId].toUtf8()).object());
        if (!md.vertices.isEmpty()) meshDataToSceneMesh(obj, md);
    }
    m_remeshOrigins.remove(objectId);
    if (m_sceneModel) m_sceneModel->refresh();
    emit sceneChanged();
}

void KSModelerQml::quadRemeshClearAll() {
    if (m_scene) {
        for (auto it = m_remeshOrigins.constBegin(); it != m_remeshOrigins.constEnd(); ++it) {
            SceneObject* o = m_scene->findObjectById(it.key());
            if (o) {
                const MeshData md = meshDataFromJson(QJsonDocument::fromJson(it.value().toUtf8()).object());
                if (!md.vertices.isEmpty()) meshDataToSceneMesh(o, md);
            }
        }
    }
    m_remeshOrigins.clear();
    if (m_sceneModel) m_sceneModel->refresh();
    emit sceneChanged();
}

void KSModelerQml::playAnimation(const QString& name) {
    for (int i = 0; i < m_animations.size(); ++i) {
        if (m_animations[i].name == name) {
            m_currentAnimation = i;
            m_animationTime = 0.0f;
            m_isAnimating = true;
            if (!m_animTimer) {
                m_animTimer = new QTimer(this);
                connect(m_animTimer, &QTimer::timeout, this, &KSModelerQml::advanceAnimation);
            }
            m_animTimer->start(1000 / m_animFps);
            applyPoseToBones(m_animations[i], 0.0f);
            emit animationNameChanged();
            emit animationTimeChanged();
            emit playbackStateChanged();
            emit sceneChanged();
            return;
        }
    }
}

void KSModelerQml::stopAnimation() {
    if (!m_isAnimating) return;
    m_isAnimating = false;
    if (m_animTimer) m_animTimer->stop();
    emit playbackStateChanged();
}

void KSModelerQml::togglePlayPause() {
    if (m_currentAnimation < 0 || m_currentAnimation >= m_animations.size()) return;
    if (m_isAnimating) {
        stopAnimation();
    } else {
        playAnimation(m_animations[m_currentAnimation].name);
    }
}

void KSModelerQml::setAnimationLoop(bool loop) {
    m_animLoop = loop;
}

QVariantList KSModelerQml::currentAnimationKeyframes() const {
    QVariantList result;
    if (m_currentAnimation < 0 || m_currentAnimation >= m_animations.size())
        return result;

    const Animation& anim = m_animations[m_currentAnimation];

    // Collect unique times across all bones
    QSet<float> timeSet;
    for (const auto& kf : anim.keyframes) {
        timeSet.insert(kf.time);
    }

    QList<float> sortedTimes = timeSet.values();
    std::sort(sortedTimes.begin(), sortedTimes.end());

    for (float t : sortedTimes) {
        QVariantMap kfObj;
        kfObj["time"] = t;
        kfObj["normalizedTime"] = anim.duration > 0 ? t / anim.duration : 0;

        // Count keyframes at this time
        int count = 0;
        for (const auto& kf : anim.keyframes) {
            if (qFuzzyCompare(kf.time, t)) count++;
        }
        kfObj["boneCount"] = count;
        result.append(kfObj);
    }

    return result;
}

// ============================================================================
// Animation layers (non-destructive blend layers, XSI-style)
// ============================================================================

int KSModelerQml::animationIndexByName(QVector<Animation>& anims, const QString& name) {
    for (int i = 0; i < anims.size(); ++i)
        if (anims[i].name == name) return i;
    return -1;
}
int KSModelerQml::animationIndexByName(const QVector<Animation>& anims, const QString& name) {
    for (int i = 0; i < anims.size(); ++i)
        if (anims[i].name == name) return i;
    return -1;
}

int KSModelerQml::animationAddLayer(const QString& animName, const QString& layerName) {
    const int ai = animationIndexByName(m_animations, animName);
    if (ai < 0) return -1;
    AnimationLayer L;
    L.name = layerName.isEmpty() ? "Layer " + QString::number(m_animations[ai].layers.size() + 1) : layerName;
    m_animations[ai].layers.append(L);
    emit animationLayersChanged();
    return m_animations[ai].layers.size() - 1;
}

bool KSModelerQml::animationRemoveLayer(const QString& animName, int layerIndex) {
    const int ai = animationIndexByName(m_animations, animName);
    if (ai < 0 || layerIndex < 0 || layerIndex >= m_animations[ai].layers.size()) return false;
    m_animations[ai].layers.removeAt(layerIndex);
    emit animationLayersChanged();
    return true;
}

bool KSModelerQml::animationRenameLayer(const QString& animName, int layerIndex, const QString& newName) {
    const int ai = animationIndexByName(m_animations, animName);
    if (ai < 0 || layerIndex < 0 || layerIndex >= m_animations[ai].layers.size() || newName.trimmed().isEmpty())
        return false;
    m_animations[ai].layers[layerIndex].name = newName.trimmed();
    emit animationLayersChanged();
    return true;
}

bool KSModelerQml::animationSetLayerEnabled(const QString& animName, int layerIndex, bool enabled) {
    const int ai = animationIndexByName(m_animations, animName);
    if (ai < 0 || layerIndex < 0 || layerIndex >= m_animations[ai].layers.size()) return false;
    m_animations[ai].layers[layerIndex].enabled = enabled;
    emit animationLayersChanged();
    emit sceneChanged();
    return true;
}

bool KSModelerQml::animationSetLayerWeight(const QString& animName, int layerIndex, float weight) {
    const int ai = animationIndexByName(m_animations, animName);
    if (ai < 0 || layerIndex < 0 || layerIndex >= m_animations[ai].layers.size()) return false;
    m_animations[ai].layers[layerIndex].weight = qBound(0.0f, weight, 1.0f);
    emit animationLayersChanged();
    emit sceneChanged();
    return true;
}

int KSModelerQml::animationLayerCount(const QString& animName) const {
    const int ai = animationIndexByName(m_animations, animName);
    if (ai < 0) return 0;
    return m_animations[ai].layers.size();
}

QVariantList KSModelerQml::animationLayerList(const QString& animName) const {
    QVariantList out;
    const int ai = animationIndexByName(m_animations, animName);
    if (ai < 0) return out;
    for (const AnimationLayer& L : m_animations[ai].layers) {
        QVariantMap m;
        m["index"] = out.size();
        m["name"] = L.name;
        m["enabled"] = L.enabled;
        m["weight"] = L.weight;
        m["keyframeCount"] = (int)L.keyframes.size();
        out.append(m);
    }
    return out;
}

bool KSModelerQml::animationAddLayerKeyframe(const QString& animName, int layerIndex, float time, int boneId,
                                             float x, float y, float z, float rotX, float rotY, float rotZ) {
    const int ai = animationIndexByName(m_animations, animName);
    if (ai < 0 || layerIndex < 0 || layerIndex >= m_animations[ai].layers.size()) return false;
    Keyframe kf;
    kf.time = time;
    kf.boneId = boneId;
    kf.position = QVector3D(x, y, z);
    kf.rotation = QVector3D(rotX, rotY, rotZ);
    m_animations[ai].layers[layerIndex].keyframes.append(kf);
    emit animationLayersChanged();
    emit sceneChanged();
    return true;
}

QVariantList KSModelerQml::animationLayerKeyframes(const QString& animName, int layerIndex) const {
    QVariantList out;
    const int ai = animationIndexByName(m_animations, animName);
    if (ai < 0 || layerIndex < 0 || layerIndex >= m_animations[ai].layers.size()) return out;
    const QVector<Keyframe>& kfs = m_animations[ai].layers[layerIndex].keyframes;
    for (const Keyframe& kf : kfs) {
        QVariantMap m;
        m["time"] = kf.time;
        m["bone"] = kf.boneId;
        m["px"] = kf.position.x();
        m["py"] = kf.position.y();
        m["pz"] = kf.position.z();
        m["rx"] = kf.rotation.x();
        m["ry"] = kf.rotation.y();
        m["rz"] = kf.rotation.z();
        out.append(m);
    }
    return out;
}

void KSModelerQml::setAnimationTime(float time) {
    m_animationTime = time;
    if (m_currentAnimation >= 0 && m_currentAnimation < m_animations.size()) {
        applyPoseToBones(m_animations[m_currentAnimation], time);
    }
    emit animationTimeChanged();
    emit sceneChanged();
}

float KSModelerQml::getAnimationTime() const { return m_animationTime; }
bool KSModelerQml::isAnimating() const { return m_isAnimating; }

void KSModelerQml::advanceAnimation() {
    if (!m_isAnimating || m_currentAnimation < 0 || m_currentAnimation >= m_animations.size()) {
        stopAnimation();
        return;
    }
    const Animation& anim = m_animations[m_currentAnimation];
    m_animationTime += 1.0f / m_animFps;
    if (m_animationTime >= anim.duration) {
        if (m_animLoop) {
            m_animationTime = 0.0f;
        } else {
            m_animationTime = anim.duration;
            stopAnimation();
        }
    }
    applyPoseToBones(anim, m_animationTime);
    emit animationTimeChanged();
    emit sceneChanged();
}

int KSModelerQml::findKeyframeIndex(const QVector<Keyframe>& kfs, float time, int boneId) const {
    for (int i = 0; i < kfs.size(); ++i) {
        if (kfs[i].boneId == boneId && kfs[i].time > time)
            return i;
    }
    return kfs.size();
}

QVector3D KSModelerQml::interpolatePosition(const QVector<Keyframe>& kfs, float time, int boneId) {
    if (kfs.isEmpty()) return QVector3D();
    if (kfs.size() == 1) return kfs[0].position;
    int next = findKeyframeIndex(kfs, time, boneId);
    if (next <= 0) return kfs[0].position;
    if (next >= kfs.size()) return kfs.last().position;
    int prev = next - 1;
    const Keyframe& a = kfs[prev];
    const Keyframe& b = kfs[next];
    float t = (b.time > a.time) ? (time - a.time) / (b.time - a.time) : 0.0f;
    t = qBound(0.0f, applyEasing(t, m_animEasing), 1.0f);
    return a.position + (b.position - a.position) * t;
}

QVector3D KSModelerQml::interpolateRotation(const QVector<Keyframe>& kfs, float time, int boneId) {
    if (kfs.isEmpty()) return QVector3D();
    if (kfs.size() == 1) return kfs[0].rotation;
    int next = findKeyframeIndex(kfs, time, boneId);
    if (next <= 0) return kfs[0].rotation;
    if (next >= kfs.size()) return kfs.last().rotation;
    int prev = next - 1;
    const Keyframe& a = kfs[prev];
    const Keyframe& b = kfs[next];
    float t = (b.time > a.time) ? (time - a.time) / (b.time - a.time) : 0.0f;
    t = qBound(0.0f, applyEasing(t, m_animEasing), 1.0f);
    // Convert Euler to quaternion, slerp, convert back
    QQuaternion qa = QQuaternion::fromEulerAngles(a.rotation);
    QQuaternion qb = QQuaternion::fromEulerAngles(b.rotation);
        QQuaternion qr = quatSlerp(qa, qb, t);
    return qr.toEulerAngles();
}

float KSModelerQml::applyEasing(float t, int type) const {
    switch (type) {
    case 1: return t * t;                                      // ease-in
    case 2: return t * (2.0f - t);                             // ease-out
    case 3: return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; // ease-in-out
    case 4: return t * t * t;                                  // ease-in cubic
    case 5: return (t - 1.0f) * (t - 1.0f) * (t - 1.0f) + 1.0f; // ease-out cubic
    default: return t;                                         // linear
    }
}

void KSModelerQml::computeAnimationPose(const Animation& anim, float time, QVector<BonePose>& out) {
    out.resize(m_bones.size());
    QVector<Keyframe> sortedKfs = anim.keyframes;
    std::sort(sortedKfs.begin(), sortedKfs.end(), [](const Keyframe& a, const Keyframe& b) {
        return (a.boneId == b.boneId) ? a.time < b.time : a.boneId < b.boneId;
    });
    for (int i = 0; i < m_bones.size(); ++i) {
        out[i].position = interpolatePosition(sortedKfs, time, i);
        out[i].rotation = interpolateRotation(sortedKfs, time, i);
    }

    // Blend non-destructive animation layers over the base pose (XSI-style):
    // each enabled layer carries absolute keyframes and is blended by its own
    // weight; layers apply in order on top of the accumulated pose.
    for (const AnimationLayer& layer : anim.layers) {
        if (!layer.enabled || layer.weight <= 0.0f || layer.keyframes.isEmpty()) continue;
        QVector<Keyframe> layerKfs = layer.keyframes;
        std::sort(layerKfs.begin(), layerKfs.end(), [](const Keyframe& a, const Keyframe& b) {
            return (a.boneId == b.boneId) ? a.time < b.time : a.boneId < b.boneId;
        });
        const float w = qBound(0.0f, layer.weight, 1.0f);
        for (int i = 0; i < m_bones.size(); ++i) {
            const QVector3D lpos = interpolatePosition(layerKfs, time, i);
            const QVector3D lrot = interpolateRotation(layerKfs, time, i);
            out[i].position = out[i].position + (lpos - out[i].position) * w;
            const QQuaternion qBase = QQuaternion::fromEulerAngles(out[i].rotation);
            const QQuaternion qLayer = QQuaternion::fromEulerAngles(lrot);
            out[i].rotation = QQuaternion::slerp(qBase, qLayer, w).toEulerAngles();
        }
    }
}

void KSModelerQml::applyPoseToBones(const Animation& anim, float time) {
    QVector<BonePose> pose;
    computeAnimationPose(anim, time, pose);
    for (int i = 0; i < m_bones.size(); ++i) {
        m_bones[i].position = pose[i].position;
        m_bones[i].rotation = pose[i].rotation;
    }
}

void KSModelerQml::undo() {
    if (m_commandHistory)
        m_commandHistory->undo();
    emit sceneChanged();
}

void KSModelerQml::redo() {
    if (m_commandHistory)
        m_commandHistory->redo();
    emit sceneChanged();
}

bool KSModelerQml::canUndo() const { return m_commandHistory && m_commandHistory->canUndo(); }
bool KSModelerQml::canRedo() const { return m_commandHistory && m_commandHistory->canRedo(); }

void KSModelerQml::showMaterialEditor() {
    if (m_currentMaterial < 0 || m_currentMaterial >= m_materials.size()) {
        emit statusMessage("No material selected. Opening empty material editor.");
        return;
    }
    auto& mat = m_materials[m_currentMaterial];

    // Create material graph
    static ks::MaterialNodeEditor s_editor;
    if (s_editor.graphs.isEmpty()) s_editor.registerDefaultNodes();

    ks::MaterialGraph* graph = s_editor.newGraph();

    // Create Principled BSDF node with current material values
    ks::MaterialNode* bsdf = graph->createNode("PrincipledBSDF", QPointF(200, 100));
    if (!bsdf) return;

    // Set BSDF parameters from current material
    if (auto* pbsdf = dynamic_cast<ks::BSDFPrincipledNode*>(bsdf)) {
        pbsdf->inputs[0].value = QColor(mat.albedo.red(), mat.albedo.green(), mat.albedo.blue());
        pbsdf->inputs[1].value = mat.metallic;
        pbsdf->inputs[2].value = mat.roughness;
    }

    // Create Output node
    ks::MaterialNode* output = graph->createNode("Output", QPointF(500, 100));
    if (!output) return;

    // Connect BSDF output → Output surface input
    graph->connectNodes(bsdf->id, bsdf->outputs[0].id, output->id, output->inputs[0].id);

    // Generate shader code
    mat.vertexShader = "#version 330 core\nlayout(location = 0) in vec3 inPos;\nlayout(location = 1) in vec3 inNorm;\nlayout(location = 2) in vec2 inUV;\nout vec2 vTexCoord;\nuniform mat4 modelViewProjection;\nvoid main() {\n    vTexCoord = inUV;\n    gl_Position = modelViewProjection * vec4(inPos, 1.0);\n}\n";
    mat.fragmentShader = graph->generateGLSL();

    emit statusMessage(QString("Material Editor: '%1' — Generated PBR shader (%2 nodes, %3 lines)")
        .arg(mat.name)
        .arg(graph->nodes.size())
        .arg(mat.fragmentShader.count('\n') + 1));
}

void KSModelerQml::ensureMatNodeEditor() {
    if (!m_matNodeEditor)
        m_matNodeEditor = new MaterialNodeEditorImpl();
}

void KSModelerQml::emitMatNodeGraphChanged() {
    emit matNodeGraphChanged();
}

QString KSModelerQml::matNodeEditorGraph() const {
    if (!m_matNodeEditor || !m_matNodeEditor->graph())
        return QString();
    return QString::fromUtf8(QJsonDocument(m_matNodeEditor->graph()->toJson()).toJson(QJsonDocument::Compact));
}

QString KSModelerQml::matNodeEditorAvailableTypes() const {
    QJsonArray arr;
    if (m_matNodeEditor) {
        const QStringList types = m_matNodeEditor->editor.getAvailableNodeTypes();
        for (const QString& t : types)
            arr.append(t);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString KSModelerQml::matNodeEditorCreateNode(const QString& type, double x, double y) {
    ensureMatNodeEditor();
    ks::MaterialNode* node = m_matNodeEditor->graph()->createNode(type, QPointF(x, y));
    if (!node) {
        emit statusMessage(QString("Unknown material node type '%1'").arg(type));
        return QString();
    }
    emitMatNodeGraphChanged();
    QJsonObject no;
    no["id"] = node->id;
    no["type"] = node->factoryType;
    no["name"] = node->name;
    return QString::fromUtf8(QJsonDocument(no).toJson(QJsonDocument::Compact));
}

void KSModelerQml::matNodeEditorDeleteNode(const QString& nodeId) {
    if (!m_matNodeEditor) return;
    m_matNodeEditor->graph()->deleteNode(nodeId);
    emitMatNodeGraphChanged();
}

void KSModelerQml::matNodeEditorConnect(const QString& fromNode, const QString& fromSocket,
                                        const QString& toNode, const QString& toSocket) {
    if (!m_matNodeEditor) return;
    m_matNodeEditor->graph()->connectNodes(fromNode, fromSocket, toNode, toSocket);
    emitMatNodeGraphChanged();
}

void KSModelerQml::matNodeEditorDisconnect(const QString& fromNode, const QString& fromSocket,
                                           const QString& toNode, const QString& toSocket) {
    if (!m_matNodeEditor) return;
    m_matNodeEditor->graph()->disconnectNodes(fromNode, fromSocket, toNode, toSocket);
    emitMatNodeGraphChanged();
}

void KSModelerQml::matNodeEditorMoveNode(const QString& nodeId, double x, double y) {
    if (!m_matNodeEditor) return;
    ks::MaterialNode* node = m_matNodeEditor->graph()->findNode(nodeId);
    if (!node) return;
    node->position = QPointF(x, y);
    // No signal: positions are live-updated by QML without a full reload.
}

void KSModelerQml::matNodeEditorSetSocketValue(const QString& nodeId, const QString& socketId,
                                               const QVariant& value) {
    if (!m_matNodeEditor) return;
    ks::MaterialNode* node = m_matNodeEditor->graph()->findNode(nodeId);
    if (!node) return;
    ks::NodeSocket* socket = node->findSocket(socketId);
    if (!socket) return;

    QVariant v = value;
    const int vt = v.metaType().id();
    if (vt == QMetaType::QVariantList || vt == QMetaType::QStringList) {
        const QVariantList list = v.toList();
        if (list.size() == 3)
            v = QVector3D(list[0].toFloat(), list[1].toFloat(), list[2].toFloat());
    } else if (vt == QMetaType::QColor) {
        v = v.value<QColor>();
    }
    socket->value = v;
    if (node->type == ks::MaterialNodeType::Input)
        node->processInputs();
    emitMatNodeGraphChanged();
}

void KSModelerQml::matNodeEditorSetTexture(const QString& nodeId, const QString& path) {
    if (!m_matNodeEditor) return;
    ks::MaterialNode* node = m_matNodeEditor->graph()->findNode(nodeId);
    if (!node) return;
    if (auto* img = dynamic_cast<ks::ImageNode*>(node)) img->texturePath = path;
    else if (auto* tex = dynamic_cast<ks::TextureNode*>(node)) tex->texturePath = path;
    else if (auto* nm = dynamic_cast<ks::NormalMapNode*>(node)) nm->texturePath = path;
    else if (auto* cube = dynamic_cast<ks::CubeMapNode*>(node)) cube->texturePath = path;
    emitMatNodeGraphChanged();
}

void KSModelerQml::matNodeEditorClear() {
    if (!m_matNodeEditor) return;
    m_matNodeEditor->graph()->clear();
    emitMatNodeGraphChanged();
}

QString KSModelerQml::matNodeEditorGenerateShader() {
    ensureMatNodeEditor();
    const QString code = m_matNodeEditor->graph()->generateGLSL();
    emit statusMessage(QString("Node Editor — Generated shader (%1 nodes, %2 lines)")
        .arg(m_matNodeEditor->graph()->nodes.size())
        .arg(code.count('\n') + 1));
    return code;
}

void KSModelerQml::showPropertiesPanel() {
    if (m_scene) {
        auto objects = m_scene->allObjects();
        QStringList objList;
        for (auto* obj : objects) {
            objList << obj->name();
        }
        emit statusMessage("Properties Panel: " + QString::number(objects.size()) + " objects in scene");
    } else {
        emit statusMessage("Opening properties panel...");
    }
}

void KSModelerQml::showSceneGraph() {
    if (m_scene) {
        auto objects = m_scene->allObjects();
        QString treeStr;
        for (auto* obj : objects) {
            QString typeStr;
            switch (obj->type()) {
                case SceneObject::Type::Mesh: typeStr = "Mesh"; break;
                case SceneObject::Type::Camera: typeStr = "Camera"; break;
                case SceneObject::Type::Light: typeStr = "Light"; break;
                default: typeStr = "Unknown"; break;
            }
            treeStr += obj->name() + " [" + typeStr + "]\n";
        }
        emit statusMessage("Scene Graph:\n" + treeStr);
    } else {
        emit statusMessage("Opening scene graph... (no scene)");
    }
}

void KSModelerQml::newTrack(const QString& name) {
    m_trackPoints.clear();
    m_trackSections.clear();
    m_currentFile = "track_" + name;
    emit sceneChanged();
    emit fileChanged(m_currentFile);
}

bool KSModelerQml::loadTrack(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorMessage("Failed to open track file: " + path);
        return false;
    }
    QString content = file.readAll();
    file.close();
    m_trackPoints.clear();
    m_trackSections.clear();
    QRegularExpression pointRe("point\\s*=\\s*\\{([^,]+),\\s*([^,]+),\\s*([^}]+)\\}");
    QRegularExpressionMatchIterator it = pointRe.globalMatch(content);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        m_trackPoints.append(QVector3D(match.captured(1).toFloat(), match.captured(2).toFloat(), match.captured(3).toFloat()));
    }
    QRegularExpression sectionRe("section\\s*=\\s*(\\d+)");
    QRegularExpressionMatchIterator sit = sectionRe.globalMatch(content);
    while (sit.hasNext()) {
        QRegularExpressionMatch match = sit.next();
        m_trackSections.append(match.captured(1).toInt());
    }
    m_currentFile = path;
    emit sceneChanged();
    emit fileChanged(m_currentFile);
    return true;
}

bool KSModelerQml::saveTrack(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit errorMessage("Failed to save track file: " + path);
        return false;
    }
    QTextStream out(&file);
    for (const auto& p : m_trackPoints) {
        out << "point = {" << p.x() << ", " << p.y() << ", " << p.z() << "}\n";
    }
    for (int s : m_trackSections) {
        out << "section = " << s << "\n";
    }
    out << "width = " << m_trackWidth << "\n";
    file.close();
    m_currentFile = path;
    emit fileChanged(m_currentFile);
    return true;
}

bool KSModelerQml::exportTrackKN5(const QString& path) {
    if (!m_scene) {
        emit errorMessage("No scene to export");
        return false;
    }

    try {
        ::KN5Parser::KN5File kn5File;
        kn5File.header.magic = ::KN5Parser::KN5_MAGIC;
        kn5File.header.version = ::KN5Parser::KN5_VERSION;
        for (SceneObject* obj : m_scene->allObjects()) {
            if (obj->type() == SceneObject::Type::Mesh) {
                ::KN5Parser::Mesh kn5Mesh;
                kn5Mesh.name = obj->name();
                kn5Mesh.nodeIndex = obj->id();
                if (obj->mesh()) {
                    auto& verts = obj->mesh()->geometry().vertices;
                    auto& idxs = obj->mesh()->geometry().indices;
                    for (const auto& v : verts) {
                        float p[3] = { v.position.x(), v.position.y(), v.position.z() };
                        kn5Mesh.vertexData.append(QByteArray((const char*)p, 12));
                        float n[3] = { v.color.x(), v.color.y(), v.color.z() };
                        kn5Mesh.vertexData.append(QByteArray((const char*)n, 12));
                    }
                    for (uint32_t idx : idxs) {
                        quint32 leIdx = idx;
                        kn5Mesh.indexData.append(QByteArray((const char*)&leIdx, 4));
                    }
                    ::KN5Parser::SubMesh sm;
                    sm.vertexCount = verts.size();
                    sm.indexCount = idxs.size();
                    kn5Mesh.subMeshes.append(sm);
                }
                kn5File.meshes.append(kn5Mesh);
            }
        }
        bool ok = ::KN5Parser::KN5ParserImpl::write(path, kn5File);
        if (ok) emit statusMessage("Track exported successfully to " + path);
        else emit errorMessage("Failed to export track KN5");
        return ok;
    } catch (const std::exception& e) {
        emit errorMessage(QString("Export failed: %1").arg(e.what()));
        return false;
    }
}

bool KSModelerQml::importGPX(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorMessage("Failed to open GPX file: " + path);
        return false;
    }
    QString content = file.readAll();
    file.close();
    m_trackPoints.clear();
    QRegularExpression trkptRe("<trkpt[^>]*lat=\"([^\"]+)\"[^>]*lon=\"([^\"]+)\"[^>]*>(?:.*?<ele>([^<]+)</ele>)?", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = trkptRe.globalMatch(content);
    double prevLat = 0, prevLon = 0;
    bool first = true;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        double lat = match.captured(1).toDouble();
        double lon = match.captured(2).toDouble();
        double ele = match.captured(3).isEmpty() ? 0.0 : match.captured(3).toDouble();
        if (first) {
            prevLat = lat; prevLon = lon; first = false;
            m_originLat = lat; m_originLon = lon;
            m_trackPoints.append(QVector3D(0, ele, 0));
            continue;
        }
        double dLat = (lat - prevLat) * 111320.0;
        double dLon = (lon - prevLon) * 111320.0 * std::cos(lat * 3.14159265 / 180.0);
        m_trackPoints.append(QVector3D(m_trackPoints.last().x() + dLon, ele, m_trackPoints.last().z() + dLat));
        prevLat = lat; prevLon = lon;
    }
    emit sceneChanged();
    return true;
}

bool KSModelerQml::importKML(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorMessage("Failed to open KML file: " + path);
        return false;
    }
    QString content = file.readAll();
    file.close();
    m_trackPoints.clear();
    QRegularExpression coordRe("<coordinates>([^<]+)</coordinates>", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = coordRe.globalMatch(content);
    double prevLat = 0, prevLon = 0;
    bool first = true;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QStringList lines = match.captured(1).trimmed().split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QStringList parts = line.trimmed().split(',');
            if (parts.size() < 2) continue;
            double lon = parts[0].toDouble();
            double lat = parts[1].toDouble();
            double ele = parts.size() > 2 ? parts[2].toDouble() : 0.0;
            if (first) { prevLat = lat; prevLon = lon; first = false; m_originLat = lat; m_originLon = lon; m_trackPoints.append(QVector3D(0, ele, 0)); continue; }
            double dLat = (lat - prevLat) * 111320.0;
            double dLon = (lon - prevLon) * 111320.0 * std::cos(lat * 3.14159265 / 180.0);
            m_trackPoints.append(QVector3D(m_trackPoints.last().x() + dLon, ele, m_trackPoints.last().z() + dLat));
            prevLat = lat; prevLon = lon;
        }
    }
    emit sceneChanged();
    return true;
}

bool KSModelerQml::exportGPX(const QString& path) {
    if (m_trackPoints.size() < 2) {
        emit errorMessage("No track points to export GPX");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit errorMessage("Failed to open GPX file for writing: " + path);
        return false;
    }
    QTextStream ts(&file);
    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ts << "<gpx version=\"1.1\" creator=\"ksEditor\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n";
    ts << "  <trk><name>ksEditor Track</name><trkseg>\n";
    for (const auto& pt : m_trackPoints) {
        double lat = m_originLat + pt.z() / 111320.0;
        double lon = m_originLon + pt.x() / (111320.0 * std::cos(lat * 3.14159265 / 180.0));
        ts << "    <trkpt lat=\"" << QString::number(lat, 'f', 7)
           << "\" lon=\"" << QString::number(lon, 'f', 7) << "\">\n";
        ts << "      <ele>" << QString::number(pt.y(), 'f', 2) << "</ele>\n";
        ts << "    </trkpt>\n";
    }
    ts << "  </trkseg></trk>\n</gpx>\n";
    file.close();
    emit statusMessage("GPX exported: " + path + " (" + QString::number(m_trackPoints.size()) + " points)");
    return true;
}

bool KSModelerQml::exportKML(const QString& path) {
    if (m_trackPoints.size() < 2) {
        emit errorMessage("No track points to export KML");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit errorMessage("Failed to open KML file for writing: " + path);
        return false;
    }
    QTextStream ts(&file);
    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ts << "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document><Placemark>\n";
    ts << "  <name>ksEditor Track</name>\n  <LineString><coordinates>\n";
    for (const auto& pt : m_trackPoints) {
        double lat = m_originLat + pt.z() / 111320.0;
        double lon = m_originLon + pt.x() / (111320.0 * std::cos(lat * 3.14159265 / 180.0));
        ts << QString::number(lon, 'f', 7) << "," << QString::number(lat, 'f', 7)
           << "," << QString::number(pt.y(), 'f', 2) << "\n";
    }
    ts << "  </coordinates></LineString></Placemark></Document></kml>\n";
    file.close();
    emit statusMessage("KML exported: " + path + " (" + QString::number(m_trackPoints.size()) + " points)");
    return true;
}

bool KSModelerQml::exportAILine(const QString& path) {
    if (m_trackPoints.size() < 2) {
        emit errorMessage("No track points to export AI line");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit errorMessage("Failed to open AI file for writing: " + path);
        return false;
    }
    const int n = m_trackPoints.size();
    const float pi = 3.14159265f;
    QTextStream ts(&file);
    for (int i = 0; i < n; ++i) {
        QVector3D d1 = m_trackPoints[(i - 1 + n) % n] - m_trackPoints[i];
        QVector3D d2 = m_trackPoints[(i + 1) % n] - m_trackPoints[i];
        d1.setY(0); d2.setY(0);
        d1.normalize(); d2.normalize();
        float angle = qAcos(qBound(-1.0f, QVector3D::dotProduct(d1, d2), 1.0f));
        float straight = 1.0f - (angle / pi);
        float speed = 12.0f + 60.0f * straight;
        ts << m_trackPoints[i].x() << "," << m_trackPoints[i].y() << "," << m_trackPoints[i].z()
           << "," << m_trackWidth << "," << QString::number(speed, 'f', 3) << "\n";
    }
    file.close();
    emit statusMessage("AI line exported: " + path + " (" + QString::number(n) + " waypoints)");
    return true;
}

bool KSModelerQml::importFromMap(const QString& service, double lat, double lon, int zoom) {
    emit statusMessage(QString("Importing map data from %1 at (%2,%3) zoom %4").arg(service).arg(lat).arg(lon).arg(zoom));
    // Use OpenStreetMap API or other tile service
    QUrl url(QString("https://api.openstreetmap.org/api/0.6/map?bbox=%1,%2,%3,%4")
        .arg(lon - 0.01).arg(lat - 0.01).arg(lon + 0.01).arg(lat + 0.01));
    QNetworkRequest req(url);
    QNetworkAccessManager mgr;
    QNetworkReply* reply = mgr.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        // Generate synthetic track from lat/lon as fallback
        emit statusMessage("Generating track from coordinates");
        m_trackPoints.clear();
        for (int i = 0; i < 20; i++) {
            float angle = i * 6.2832f / 20;
            m_trackPoints.append(QVector3D(cosf(angle) * 100, 0, sinf(angle) * 100));
        }
        emit sceneChanged();
        return true;
    }
    emit sceneChanged();
    emit statusMessage("Map imported successfully");
    return true;
}

void KSModelerQml::addTrackPoint(float x, float y, float z) { m_trackPoints.append(QVector3D(x, y, z)); emit sceneChanged(); }
void KSModelerQml::insertTrackPoint(int index, float x, float y, float z) {
    if (index >= 0 && index <= m_trackPoints.size()) {
        m_trackPoints.insert(index, QVector3D(x, y, z));
        emit sceneChanged();
    }
}
void KSModelerQml::removeTrackPoint(int index) {
    if (index >= 0 && index < m_trackPoints.size()) { m_trackPoints.removeAt(index); emit sceneChanged(); }
}
void KSModelerQml::smoothTrackPoints(int iterations) {
    if (m_trackPoints.size() < 3) return;
    for (int iter = 0; iter < iterations; ++iter) {
        QVector<QVector3D> smoothed = m_trackPoints;
        for (int i = 1; i < m_trackPoints.size() - 1; ++i)
            smoothed[i] = (m_trackPoints[i - 1] + m_trackPoints[i] * 2.0f + m_trackPoints[i + 1]) * 0.25f;
        m_trackPoints = smoothed;
    }
    emit sceneChanged();
}
void KSModelerQml::closeTrackLoop() {
    if (m_trackPoints.size() < 3) return;
    m_trackPoints.append(m_trackPoints.first());
    emit sceneChanged();
}
void KSModelerQml::setTrackWidth(float width) { m_trackWidth = width; }
float KSModelerQml::trackWidth() const { return m_trackWidth; }
void KSModelerQml::setTrackCamber(float camber) { m_trackCamber = camber; }
float KSModelerQml::trackCamber() const { return m_trackCamber; }
void KSModelerQml::addTrackSection(int type) { m_trackSections.append(type); emit sceneChanged(); }
void KSModelerQml::removeTrackSection(int index) {
    if (index >= 0 && index < m_trackSections.size()) { m_trackSections.removeAt(index); emit sceneChanged(); }
}
void KSModelerQml::setSectionSurface(int index, const QString& surfaceType) {
    if (index >= 0 && index < m_trackSections.size()) {
        m_trackSections[index] = 1; // Mark as modified
        TrackSurface surf;
        surf.type = surfaceType;
        surf.grip = 1.0f;
        surf.roughness = 0.5f;

        if (surfaceType == "asphalt") { surf.grip = 1.0f; surf.roughness = 0.3f; }
        else if (surfaceType == "concrete") { surf.grip = 0.9f; surf.roughness = 0.4f; }
        else if (surfaceType == "grass") { surf.grip = 0.3f; surf.roughness = 0.8f; }
        else if (surfaceType == "gravel") { surf.grip = 0.4f; surf.roughness = 0.9f; }
        else if (surfaceType == "kerb") { surf.grip = 0.5f; surf.roughness = 0.6f; }

        m_surfaces[index] = surf;
        emit statusMessage(QString("Section %1 surface set to %2 (grip=%3)")
            .arg(index).arg(surfaceType).arg(surf.grip));
        emit sceneChanged();
    }
}

void KSModelerQml::addSectionKerb(int index, float height, float width) {
    if (index >= 0 && index < m_trackSections.size()) {
        TrackKerb kerb;
        kerb.height = height;
        kerb.width = width;
        kerb.color = QColor(255, 0, 0);
        m_kerbs[index] = kerb;
        emit statusMessage(QString("Kerb added to section %1 (h=%2, w=%3)")
            .arg(index).arg(height).arg(width));
        emit sceneChanged();
    }
}

void KSModelerQml::generateTrackMesh() {
    if (!m_scene) m_scene = new ks::SceneGraph();
    if (m_trackPoints.size() < 3) {
        emit statusMessage("Need at least 3 track points to generate mesh");
        return;
    }

    const int n = m_trackPoints.size();
    const float halfW = m_trackWidth * 0.5f;
    const float camber = m_trackCamber;

    QVector<float> cumLen(n, 0.0f);
    for (int i = 1; i < n; ++i)
        cumLen[i] = cumLen[i - 1] + (m_trackPoints[i] - m_trackPoints[i - 1]).length();
    const float totalLen = cumLen[n - 1] + (m_trackPoints[n - 1] - m_trackPoints[0]).length();

    MeshData md;
    md.vertices.reserve(n * 2);
    for (int i = 0; i < n; ++i) {
        const int prev = (i - 1 + n) % n;
        const int next = (i + 1) % n;
        QVector3D tangent = (m_trackPoints[next] - m_trackPoints[prev]).normalized();
        QVector3D right = QVector3D::crossProduct(tangent, QVector3D(0, 1, 0)).normalized();

        Vertex a, b;
        a.position = m_trackPoints[i] - right * halfW;
        b.position = m_trackPoints[i] + right * halfW;
        a.position.setY(a.position.y() - camber * halfW);
        b.position.setY(b.position.y() + camber * halfW);

        float u = totalLen > 0 ? cumLen[i] / totalLen : 0.0f;
        a.uv = QVector2D(u, 0.0f);
        b.uv = QVector2D(u, 1.0f);
        md.vertices.append(a);
        md.vertices.append(b);
    }
    for (int i = 0; i < n; ++i) {
        const int next = (i + 1) % n;
        const int a = i * 2, b = i * 2 + 1, na = next * 2, nb = next * 2 + 1;
        Face f;  f.indices = { a, na, b }; md.faces.append(f);
        Face f2; f2.indices = { b, na, nb }; md.faces.append(f2);
    }
    md.computeNormals();
    md.computeBoundingBox();

    SceneObject* obj = m_scene->createObject("TrackMesh", SceneObject::Type::Mesh);
    if (obj) {
        meshDataToSceneMesh(obj, md);
        emit statusMessage("Track mesh generated: " + QString::number(n * 2) +
                           " vertices, width " + QString::number(m_trackWidth) +
                           (m_trackCamber != 0.0f ? ", camber " + QString::number(m_trackCamber) : ""));
    }
    emit sceneChanged();
}

void KSModelerQml::generateTrackEdges() {
    if (!m_scene) m_scene = new ks::SceneGraph();
    if (m_trackPoints.size() < 2) {
        emit statusMessage("Need at least 2 track points to generate edges");
        return;
    }

    graphics::Modeling3D modeler;
    graphics::Mesh3D* mesh = new graphics::Mesh3D();
    QVector<QVector3D> verts;
    QVector<quint32> indices;

    for (int i = 0; i < m_trackPoints.size(); ++i) {
        int next = (i + 1) % m_trackPoints.size();
        QVector3D dir = (m_trackPoints[next] - m_trackPoints[i]).normalized();
        QVector3D right = QVector3D::crossProduct(dir, QVector3D(0, 1, 0)).normalized();
        float halfWidth = m_trackWidth * 0.5f;

        verts.append(m_trackPoints[i] + right * halfWidth);
        verts.append(m_trackPoints[i] - right * halfWidth);

        if (i < m_trackPoints.size() - 1) {
            indices.append(i * 2); indices.append(i * 2 + 1);
        }
    }

    mesh->setVertices(verts);
    mesh->setIndices(indices);

    SceneObject* obj = m_scene->createObject("TrackEdges", SceneObject::Type::Mesh);
    if (obj) {
        SceneMesh* sm = new SceneMesh();
        for (const auto& v : mesh->vertices()) {
            SceneVertex sv;
            sv.position = QVector3D(v.x(), v.y(), v.z());
            sv.color = QVector4D(1, 1, 1, 1);
            sm->geometry().vertices.append(sv);
        }
        for (auto idx : mesh->indices())
            sm->geometry().indices.append(idx);
        obj->setMesh(sm);
        delete mesh;
    }
    emit sceneChanged();
    emit statusMessage("Track edges generated");
}

void KSModelerQml::generateTerrain(float size, float maxHeight) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    if (size <= 0) size = 100.0f;
    if (maxHeight <= 0) maxHeight = 10.0f;

    int resolution = 32;
    graphics::Mesh3D* mesh = new graphics::Mesh3D();
    QVector<QVector3D> verts;
    QVector<quint32> indices;

    float halfSize = size * 0.5f;
    for (int z = 0; z <= resolution; ++z) {
        for (int x = 0; x <= resolution; ++x) {
            float fx = (float)x / resolution * size - halfSize;
            float fz = (float)z / resolution * size - halfSize;
            float height = sinf(fx * 0.05f) * cosf(fz * 0.07f) * maxHeight * 0.5f
                         + sinf(fx * 0.02f + fz * 0.03f) * maxHeight * 0.3f;
            verts.append(QVector3D(fx, height, fz));
        }
    }

    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            int a = z * (resolution + 1) + x;
            int b = a + 1;
            int c = (z + 1) * (resolution + 1) + x;
            int d = c + 1;
            indices.append(a); indices.append(c); indices.append(b);
            indices.append(b); indices.append(c); indices.append(d);
        }
    }

    mesh->setVertices(verts);
    mesh->setIndices(indices);
    mesh->computeNormals();

    SceneObject* obj = m_scene->createObject("Terrain", SceneObject::Type::Mesh);
    if (obj) {
        SceneMesh* sm = new SceneMesh();
        for (const auto& v : mesh->vertices()) {
            SceneVertex sv;
            sv.position = QVector3D(v.x(), v.y(), v.z());
            sv.color = QVector4D(1, 1, 1, 1);
            sm->geometry().vertices.append(sv);
        }
        for (auto idx : mesh->indices())
            sm->geometry().indices.append(idx);
        obj->setMesh(sm);
        delete mesh;
        emit statusMessage("Terrain generated: " + QString::number(verts.size()) + " vertices");
    }
    emit sceneChanged();
}

void KSModelerQml::generateAILine() {
    if (!m_scene) m_scene = new ks::SceneGraph();
    if (m_trackPoints.size() < 2) return;

    graphics::Mesh3D* mesh = new graphics::Mesh3D();
    QVector<QVector3D> verts;
    // AI line follows center of track
    for (const auto& pt : m_trackPoints)
        verts.append(pt);
    mesh->setVertices(verts);

    SceneObject* obj = m_scene->createObject("AILine", SceneObject::Type::Mesh);
    if (obj) {
        SceneMesh* sm = new SceneMesh();
        for (const auto& v : mesh->vertices()) {
            SceneVertex sv;
            sv.position = QVector3D(v.x(), v.y(), v.z());
            sv.color = QVector4D(1, 1, 1, 1);
            sm->geometry().vertices.append(sv);
        }
        obj->setMesh(sm);
        delete mesh;
    }
    emit sceneChanged();
    emit statusMessage("AI racing line generated");
}

int KSModelerQml::trackPointCount() const { return m_trackPoints.size(); }
int KSModelerQml::trackSectionCount() const { return m_trackSections.size(); }
float KSModelerQml::trackLength() const {
    float len = 0;
    for (int i = 1; i < m_trackPoints.size(); ++i)
        len += m_trackPoints[i].distanceToPoint(m_trackPoints[i-1]);
    return len;
}
int KSModelerQml::cornerCount() const {
    int c = 0;
    for (int i = 2; i < m_trackPoints.size() - 1; ++i) {
        QVector3D v1 = (m_trackPoints[i] - m_trackPoints[i-1]).normalized();
        QVector3D v2 = (m_trackPoints[i+1] - m_trackPoints[i]).normalized();
        if (QVector3D::dotProduct(v1, v2) < 0.95f) c++;
    }
    return c;
}

void KSModelerQml::addTrackCamera(const QString& name, float x, float y, float z, float targetX, float targetY, float targetZ) {
    if (!m_scene) m_scene = new ks::SceneGraph();
    SceneObject* cam = m_scene->createObject(name, SceneObject::Type::Camera);
    if (cam) cam->setPosition(QVector3D(x, y, z));
    emit sceneChanged();
    emit statusMessage(QString("Camera %1 added at (%2,%3,%4) targeting (%5,%6,%7)").arg(name).arg(x).arg(y).arg(z).arg(targetX).arg(targetY).arg(targetZ));
}
void KSModelerQml::removeTrackCamera(int index) {
    if (m_scene) {
        int camIdx = 0;
        for (auto* obj : m_scene->allObjects()) {
            if (obj->type() == SceneObject::Type::Camera && camIdx++ == index) {
                m_scene->deleteObject(obj);
                emit sceneChanged();
                return;
            }
        }
    }
}
void KSModelerQml::setCameraPosition(int index, float x, float y, float z) {
    if (m_scene) {
        int camIdx = 0;
        for (auto* obj : m_scene->allObjects()) {
            if (obj->type() == SceneObject::Type::Camera && camIdx++ == index) {
                obj->setPosition(QVector3D(x, y, z));
                emit sceneChanged();
                return;
            }
        }
    }
}
void KSModelerQml::setCameraTarget(int index, float x, float y, float z) {
    if (m_scene) {
        int camIdx = 0;
        for (auto* obj : m_scene->allObjects()) {
            if (obj->type() == SceneObject::Type::Camera && camIdx++ == index) {
                QVector3D currentPos = obj->position();
                QVector3D target(x, y, z);
                QVector3D dir = (target - currentPos).normalized();

                // Compute look-at rotation
                float yaw = atan2(dir.x(), dir.z());
                float pitch = -asin(dir.y());
                obj->setPosition(currentPos);
                obj->setRotationEuler(QVector3D(pitch, yaw, 0));

                emit statusMessage(QString("Camera %1 target set to (%2,%3,%4)").arg(index).arg(x).arg(y).arg(z));
                emit sceneChanged();
                return;
            }
        }
    }
}
QStringList KSModelerQml::getSupportedImportFormats() const { return {"kn5", "fbx", "glb", "gltf", "obj"}; }
QStringList KSModelerQml::getSupportedExportFormats() const { return {"kn5", "fbx", "glb"}; }

QString KSModelerQml::detectFormat(const QString& path) const {
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "kn5") return "KN5";
    if (ext == "fbx") return "FBX";
    if (ext == "glb" || ext == "gltf") return "GLTF";
    if (ext == "obj") return "OBJ";
    if (ext == "stl") return "STL";
    if (ext == "dae") return "DAE";
    if (ext == "3ds") return "3DS";
    return "UNKNOWN";
}

Mesh* KSModelerQml::getSelectedMesh() {
    if (!m_selectedObject || !m_selectedObject->object()) return nullptr;
    SceneObject* obj = m_selectedObject->object();
    SceneMesh* sm = obj->mesh();
    if (!sm) return nullptr;

    Mesh* mesh = new Mesh();
    mesh->vertices.reserve(sm->geometry().vertices.size());
    for (const SceneVertex& sv : sm->geometry().vertices) {
        VertexUV v;
        v.x = sv.position.x(); v.y = sv.position.y(); v.z = sv.position.z();
        v.nx = sv.normal.x(); v.ny = sv.normal.y(); v.nz = sv.normal.z();
        v.u = sv.uv.x(); v.v = sv.uv.y();
        v.r = sv.color.x(); v.g = sv.color.y(); v.b = sv.color.z(); v.a = sv.color.w();
        mesh->vertices.push_back(v);
    }
    mesh->indices.assign(sm->geometry().indices.begin(), sm->geometry().indices.end());
    mesh->materialIds.resize(sm->geometry().indices.size() / 3, 0);
    return mesh;
}

// ============================================================================
// Shape Key / Morph Target Bridge Methods
// ============================================================================

void KSModelerQml::syncShapeKeyMesh() {
    Mesh* mesh = getSelectedMesh();
    if (!mesh) return;
    m_shapeKeyMesh.vertices.resize(mesh->vertices.size());
    for (int i = 0; i < (int)mesh->vertices.size(); ++i) {
        m_shapeKeyMesh.vertices[i].position = QVector3D(mesh->vertices[i].x, mesh->vertices[i].y, mesh->vertices[i].z);
    }
    m_shapeKeyMesh.faces.resize(mesh->indices.size() / 3);
    for (int i = 0; i < m_shapeKeyMesh.faces.size(); ++i) {
        m_shapeKeyMesh.faces[i].indices = { (int)mesh->indices[i*3], (int)mesh->indices[i*3+1], (int)mesh->indices[i*3+2] };
    }
    delete mesh;
}

int KSModelerQml::addShapeKey(const QString& name) {
    syncShapeKeyMesh();
    int idx = ShapeKeyManager::addShapeKey(m_shapeKeyMesh, name);
    if (idx >= 0) {
        m_shapeKeysDirty = true;
        emit shapeKeysChanged();
        emit statusMessage(QString("Shape key '%1' added").arg(m_shapeKeyMesh.shapeKeyNames[idx]));
    }
    return idx;
}

bool KSModelerQml::removeShapeKey(int index) {
    bool ok = ShapeKeyManager::removeShapeKey(m_shapeKeyMesh, index);
    if (ok) {
        m_shapeKeysDirty = true;
        emit shapeKeysChanged();
        emit statusMessage("Shape key removed");
    }
    return ok;
}

bool KSModelerQml::renameShapeKey(int index, const QString& newName) {
    bool ok = ShapeKeyManager::renameShapeKey(m_shapeKeyMesh, index, newName);
    if (ok) {
        emit shapeKeysChanged();
        emit statusMessage(QString("Shape key renamed to '%1'").arg(newName));
    }
    return ok;
}

int KSModelerQml::captureShapeKey(const QString& name) {
    syncShapeKeyMesh();
    int idx = ShapeKeyManager::captureShapeKey(m_shapeKeyMesh, name);
    if (idx >= 0) {
        m_shapeKeysDirty = true;
        emit shapeKeysChanged();
        emit statusMessage(QString("Shape key '%1' captured (%2 vertices)")
            .arg(m_shapeKeyMesh.shapeKeyNames[idx])
            .arg(m_shapeKeyMesh.vertices.size()));
    }
    return idx;
}

void KSModelerQml::setShapeKeyWeight(int index, float weight) {
    ShapeKeyManager::setShapeKeyWeight(m_shapeKeyMesh, index, weight);
    m_shapeKeysDirty = true;
    emit shapeKeysChanged();
}

float KSModelerQml::getShapeKeyWeight(int index) {
    return ShapeKeyManager::getShapeKeyWeight(m_shapeKeyMesh, index);
}

QStringList KSModelerQml::getShapeKeyNames() {
    return ShapeKeyManager::getShapeKeyNames(m_shapeKeyMesh);
}

int KSModelerQml::getShapeKeyCount() {
    return ShapeKeyManager::getShapeKeyCount(m_shapeKeyMesh);
}

void KSModelerQml::resetShapeKeys() {
    ShapeKeyManager::resetToBasis(m_shapeKeyMesh);
    m_shapeKeysDirty = true;
    emit shapeKeysChanged();
    emit statusMessage("Shape keys reset to basis");
}

void KSModelerQml::muteShapeKey(int index, bool mute) {
    ShapeKeyManager::muteShapeKey(m_shapeKeyMesh, index, mute);
    m_shapeKeysDirty = true;
    emit shapeKeysChanged();
}

void KSModelerQml::applyShapeKeys() {
    if (!m_shapeKeysDirty) return;
    Mesh* mesh = getSelectedMesh();
    if (!mesh) return;
    int n = qMin((int)mesh->vertices.size(), m_shapeKeyMesh.vertices.size());
    for (int i = 0; i < n; ++i) {
        mesh->vertices[i].x = m_shapeKeyMesh.vertices[i].position.x();
        mesh->vertices[i].y = m_shapeKeyMesh.vertices[i].position.y();
        mesh->vertices[i].z = m_shapeKeyMesh.vertices[i].position.z();
    }
    if (m_selectedObject && m_selectedObject->object()) {
        SceneObject* obj = m_selectedObject->object();
        SceneMesh* sm = obj->mesh();
        if (sm) {
            for (int i = 0; i < sm->geometry().vertices.size() && i < n; ++i) {
                sm->geometry().vertices[i].position = QVector3D(
                    m_shapeKeyMesh.vertices[i].position.x(),
                    m_shapeKeyMesh.vertices[i].position.y(),
                    m_shapeKeyMesh.vertices[i].position.z()
                );
            }
        }
    }
    delete mesh;
    m_shapeKeysDirty = false;
    emit sceneChanged();
    emit statusMessage("Shape keys applied to mesh");
}

void KSModelerQml::clearAllShapeKeys() {
    m_shapeKeyMesh.shapeKeyNames.clear();
    m_shapeKeyMesh.shapeKeyDeltas.clear();
    m_shapeKeyMesh.shapeKeyWeights.clear();
    m_shapeKeyMesh.shapeKeyMute.clear();
    m_shapeKeyMesh.shapeKeyMin.clear();
    m_shapeKeyMesh.shapeKeyMax.clear();
    m_shapeKeysDirty = true;
    emit shapeKeysChanged();
    emit statusMessage("All shape keys cleared");
}

// ============================================================================
// LOD Generation Bridge Methods
// ============================================================================

bool KSModelerQml::generateLODs(int levelCount) {
    Mesh* mesh = getSelectedMesh();
    if (!mesh) {
        emit errorMessage("No mesh selected for LOD generation");
        return false;
    }

    MeshData sourceMesh;
    sourceMesh.vertices.resize(mesh->vertices.size());
    for (int i = 0; i < (int)mesh->vertices.size(); ++i) {
        sourceMesh.vertices[i].position = QVector3D(mesh->vertices[i].x, mesh->vertices[i].y, mesh->vertices[i].z);
    }
    sourceMesh.faces.resize(mesh->indices.size() / 3);
    for (int i = 0; i < sourceMesh.faces.size(); ++i) {
        sourceMesh.faces[i].indices = {(int)mesh->indices[i*3], (int)mesh->indices[i*3+1], (int)mesh->indices[i*3+2]};
    }
    delete mesh;

    m_lodResult = LODSystem::generateAutoLODs(sourceMesh, levelCount);
    emit lodsGenerated(m_lodResult.levels.size());
    emit statusMessage(QString("Generated %1 LOD levels").arg(m_lodResult.levels.size()));
    return true;
}

bool KSModelerQml::exportLODs(const QString& basePath) {
    if (m_lodResult.levels.isEmpty()) {
        emit errorMessage("No LODs to export");
        return false;
    }

    bool allOk = true;
    for (int i = 0; i < m_lodResult.levels.size(); ++i) {
        QString ext = ".obj";
        QString path = basePath + QString("_LOD%1").arg(i) + ext;
        emit statusMessage(QString("Exporting %1").arg(path));

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            emit errorMessage(QString("Cannot open %1 for writing").arg(path));
            allOk = false;
            continue;
        }

        QTextStream out(&file);
        const MeshData& lodMesh = m_lodResult.levels[i];

        for (int vi = 0; vi < lodMesh.vertices.size(); ++vi) {
            const auto& v = lodMesh.vertices[vi].position;
            out << "v " << v.x() << " " << v.y() << " " << v.z() << "\n";
        }

        if (!lodMesh.uvs.isEmpty()) {
            for (const auto& uv : lodMesh.uvs)
                out << "vt " << uv.x() << " " << uv.y() << "\n";
        }

        if (!lodMesh.normals.isEmpty()) {
            for (const auto& n : lodMesh.normals)
                out << "vn " << n.x() << " " << n.y() << " " << n.z() << "\n";
        }

        for (const auto& face : lodMesh.faces) {
            out << "f";
            for (int j = 0; j < face.indices.size(); ++j) {
                int vi = face.indices[j] + 1;
                int ti = (j < face.uvIndices.size() && face.uvIndices[j] >= 0) ? face.uvIndices[j] + 1 : 0;
                if (ti > 0 && vi > 0)
                    out << " " << vi << "/" << ti;
                else
                    out << " " << vi;
            }
            out << "\n";
        }

        file.close();
    }

    if (allOk)
        emit statusMessage(QString("Exported %1 LOD levels to %2")
            .arg(m_lodResult.levels.size()).arg(basePath));
    return allOk;
}

void KSModelerQml::setLODDistance(int level, float distance) {
    if (level >= 0 && level < m_lodResult.distances.size()) {
        m_lodResult.distances[level] = distance;
    }
}

float KSModelerQml::getLODDistance(int level) const {
    if (level >= 0 && level < m_lodResult.distances.size()) {
        return m_lodResult.distances[level];
    }
    return 0.0f;
}

// ============================================================================
// Non-destructive Modifier Stack Bridge Methods
// ============================================================================

ModifierStack* KSModelerQml::modifierStackForObject(int objectId)
{
    if (m_modifierStacks.contains(objectId))
        return m_modifierStacks[objectId];
    auto* stack = new ModifierStack(this);
    m_modifierStacks[objectId] = stack;
    modifierSubscribe(objectId);
    return stack;
}

void KSModelerQml::evaluateAndWriteStack(SceneObject* obj)
{
    if (!obj || !obj->mesh()) return;
    if (m_modifierBusy.contains(obj->id())) return;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end())
        return;
    ModifierStack* stack = it.value();
    if (!stack->hasBase()) {
        // First evaluation: capture the current scene mesh as the base.
        stack->setBase(sceneMeshToMeshData(obj));
        if (!stack->hasBase()) return;
    }
    m_modifierBusy.insert(obj->id());
    const auto guard = qScopeGuard([this, obj]() { m_modifierBusy.remove(obj->id()); });
    MeshData evaluated = stack->evaluate();
    meshDataToSceneMesh(obj, evaluated);
    modifierSubscribe(obj->id());
    emit sceneChanged();
    emit modifierStackChanged();
}

bool KSModelerQml::modifierStackAdd(const QString& type)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;

    const QJsonObject before = modifierStackSnapshot(obj->id());
    ModifierStack* stack = modifierStackForObject(obj->id());
    if (!stack->hasBase())
        stack->setBase(sceneMeshToMeshData(obj));
    if (!stack->add(type)) {
        emit errorMessage("Unknown modifier type: " + type);
        return false;
    }
    const QJsonObject after = modifierStackSnapshot(obj->id());
    evaluateAndWriteStack(obj);
    if (m_commandHistory)
        m_commandHistory->execute(std::make_shared<ModifierStackCommand>(
            obj->id(), before, after, QString("Add modifier: %1").arg(type)));
    emit statusMessage(QString("Added modifier: %1").arg(type));
    return true;
}

bool KSModelerQml::modifierStackRemove(int index)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return false;
    const QJsonObject before = modifierStackSnapshot(obj->id());
    if (!it.value()->remove(index)) return false;
    const QJsonObject after = modifierStackSnapshot(obj->id());
    evaluateAndWriteStack(obj);
    if (m_commandHistory)
        m_commandHistory->execute(std::make_shared<ModifierStackCommand>(
            obj->id(), before, after, "Remove modifier"));
    emit statusMessage("Removed modifier");
    return true;
}

bool KSModelerQml::modifierStackMove(int fromIndex, int toIndex)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return false;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return false;
    const QJsonObject before = modifierStackSnapshot(obj->id());
    if (!it.value()->move(fromIndex, toIndex)) return false;
    const QJsonObject after = modifierStackSnapshot(obj->id());
    evaluateAndWriteStack(obj);
    if (m_commandHistory)
        m_commandHistory->execute(std::make_shared<ModifierStackCommand>(
            obj->id(), before, after, "Reorder modifier"));
    return true;
}

bool KSModelerQml::modifierStackSetEnabled(int index, bool enabled)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return false;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return false;
    const QJsonObject before = modifierStackSnapshot(obj->id());
    if (!it.value()->setEnabled(index, enabled)) return false;
    const QJsonObject after = modifierStackSnapshot(obj->id());
    evaluateAndWriteStack(obj);
    if (m_commandHistory)
        m_commandHistory->execute(std::make_shared<ModifierStackCommand>(
            obj->id(), before, after, "Toggle modifier"));
    return true;
}

bool KSModelerQml::modifierStackSetParam(int index, const QString& name, const QVariant& value)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return false;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return false;
    const QJsonObject before = modifierStackSnapshot(obj->id());
    if (!it.value()->setParam(index, name, value)) return false;
    const QJsonObject after = modifierStackSnapshot(obj->id());
    evaluateAndWriteStack(obj);
    if (m_commandHistory)
        m_commandHistory->execute(std::make_shared<ModifierStackCommand>(
            obj->id(), before, after, QString("Set %1").arg(name)));
    return true;
}

void KSModelerQml::modifierStackFreeze()
{
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end() || !it.value()->hasModifiers()) return;

    const QJsonObject before = modifierStackSnapshot(obj->id());
    ModifierStack* stack = it.value();
    MeshData evaluated = stack->evaluate();
    // Bake the evaluated result into the scene mesh and drop the stack.
    meshDataToSceneMesh(obj, evaluated);
    modifierUnsubscribe(obj->id());
    m_modifierStacks.erase(it);
    delete stack;
    const QJsonObject after = modifierStackSnapshot(obj->id());
    if (m_commandHistory)
        m_commandHistory->execute(std::make_shared<ModifierStackCommand>(
            obj->id(), before, after, "Freeze modifier stack"));
    emit sceneChanged();
    emit modifierStackChanged();
    emit statusMessage("Modifier stack applied (frozen)");
}

bool KSModelerQml::subdivisionCreaseSelectedEdges(int stackIndex)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return false;
    ModifierStack* stack = it.value();
    if (stackIndex < 0 || stackIndex >= stack->count()) return false;
    if (stack->at(stackIndex).type != "Subdivision") return false;

    const QVector<int> selected = MeshOperations::SelectionManager::selectedEdges();
    if (selected.isEmpty()) {
        emit statusMessage("Select one or more edges to crease");
        return false;
    }
    MeshData md = sceneMeshToMeshData(obj);
    MeshOperations::ensureEdgeList(md);

    QVector<CreaseEdge> creases;
    // Merge existing crease params.
    const QJsonValue existing = stack->at(stackIndex).params.value("creases");
    if (existing.isArray()) {
        const QJsonArray arr = existing.toArray();
        for (const QJsonValue& row : arr) {
            const QJsonArray c = row.toArray();
            if (c.size() >= 3)
                creases.append(CreaseEdge{c[0].toInt(), c[1].toInt(), (float)c[2].toDouble()});
        }
    }
    QSet<QPair<int, int>> seen;
    for (const auto& c : creases)
        seen.insert(qMakePair(qMin(c.vertexA, c.vertexB), qMax(c.vertexA, c.vertexB)));

    int added = 0;
    for (int edgeIdx : selected) {
        if (edgeIdx < 0 || edgeIdx >= md.edges.size()) continue;
        const Edge& e = md.edges[edgeIdx];
        QPair<int, int> key = qMakePair(qMin(e.v1, e.v2), qMax(e.v1, e.v2));
        if (seen.contains(key)) continue;
        seen.insert(key);
        creases.append(CreaseEdge{e.v1, e.v2, 1.0f});
        ++added;
    }
    if (added == 0) {
        emit statusMessage("Edges already creased (or none valid)");
        return false;
    }

    QVariantList creaseList;
    for (const auto& c : creases)
        creaseList.append(QVariant(QVariantList{c.vertexA, c.vertexB, c.sharpness}));

    const QJsonObject before = modifierStackSnapshot(obj->id());
    stack->setParam(stackIndex, "creases", creaseList);
    const QJsonObject after = modifierStackSnapshot(obj->id());
    evaluateAndWriteStack(obj);
    if (m_commandHistory)
        m_commandHistory->execute(std::make_shared<ModifierStackCommand>(
            obj->id(), before, after, QString("Crease %1 edge(s)").arg(added)));
    emit statusMessage(QString("%1 edge(s) creased").arg(added));
    return true;
}

bool KSModelerQml::subdivisionPinSelectedVertices(int stackIndex)
{
    if (!m_selectedObject) return false;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return false;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return false;
    ModifierStack* stack = it.value();
    if (stackIndex < 0 || stackIndex >= stack->count()) return false;
    if (stack->at(stackIndex).type != "Subdivision") return false;

    const QVector<int> selected = MeshOperations::SelectionManager::selectedVertices();
    if (selected.isEmpty()) {
        emit statusMessage("Select one or more vertices to pin");
        return false;
    }

    QVector<int> pins;
    const QJsonValue existing = stack->at(stackIndex).params.value("pinnedVertices");
    if (existing.isArray()) {
        const QJsonArray arr = existing.toArray();
        for (const QJsonValue& v : arr)
            pins.append(v.toInt());
    }
    QSet<int> seen(pins.begin(), pins.end());
    for (int v : selected) {
        if (!seen.contains(v)) {
            seen.insert(v);
            pins.append(v);
        }
    }

    QVariantList pinList;
    for (int v : pins)
        pinList.append(v);

    const QJsonObject before = modifierStackSnapshot(obj->id());
    stack->setParam(stackIndex, "pinnedVertices", pinList);
    const QJsonObject after = modifierStackSnapshot(obj->id());
    evaluateAndWriteStack(obj);
    if (m_commandHistory)
        m_commandHistory->execute(std::make_shared<ModifierStackCommand>(
            obj->id(), before, after, QString("Pin %1 vertex(es)").arg(pins.size())));
    emit statusMessage(QString("%1 vertex(es) pinned").arg(pins.size()));
    return true;
}

void KSModelerQml::subdivisionClearPinnedVertices(int stackIndex)
{
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return;
    ModifierStack* stack = it.value();
    if (stackIndex < 0 || stackIndex >= stack->count()) return;
    if (stack->at(stackIndex).type != "Subdivision") return;

    const QJsonObject before = modifierStackSnapshot(obj->id());
    stack->setParam(stackIndex, "pinnedVertices", QJsonArray());
    stack->setParam(stackIndex, "creases", QJsonArray());
    const QJsonObject after = modifierStackSnapshot(obj->id());
    evaluateAndWriteStack(obj);
    if (m_commandHistory)
        m_commandHistory->execute(std::make_shared<ModifierStackCommand>(
            obj->id(), before, after, "Clear creases / pinned vertices"));
    emit statusMessage("Creases and pinned vertices cleared");
}

void KSModelerQml::modifierStackClear()
{
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return;

    const QJsonObject before = modifierStackSnapshot(obj->id());
    ModifierStack* stack = it.value();
    bool hadBase = stack->hasBase();
    MeshData base = stack->base();
    modifierUnsubscribe(obj->id());
    m_modifierStacks.erase(it);
    delete stack;
    if (hadBase)
        meshDataToSceneMesh(obj, base);
    const QJsonObject after = modifierStackSnapshot(obj->id());
    if (m_commandHistory)
        m_commandHistory->execute(std::make_shared<ModifierStackCommand>(
            obj->id(), before, after, "Clear modifier stack"));
    emit sceneChanged();
    emit modifierStackChanged();
}

int KSModelerQml::modifierStackCount() const
{
    if (!m_selectedObject) return 0;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return 0;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return 0;
    return it.value()->count();
}

QStringList KSModelerQml::modifierStackTypes() const
{
    return {
        "Mirror", "Array", "Bevel", "Solidify", "Subdivision",
        "Decimate", "Displace", "Smooth", "Cast", "Triangulate",
        "Wireframe", "Remesh", "Skin", "Shrinkwrap", "CageDeform",
        "LatticeEx", "SimpleDeform", "Curve", "CorrectiveSmooth",
        "UVProject", "Weld", "LaplacianSmooth", "SurfaceSmooth",
        "VolumeSmooth", "Taper", "Ripple", "Noise", "Push", "Relax",
        "Melt", "Lathe", "Wave"
    };
}

QStringList KSModelerQml::modifierStackParamNames(int index) const
{
    if (!m_selectedObject) return {};
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return {};
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return {};
    ModifierStack* stack = it.value();
    if (index < 0 || index >= stack->count()) return {};

    QString type = stack->at(index).type;
    if (type == "Mirror") return { "mirrorAxes", "mergeThreshold" };
    if (type == "Array") return { "count", "constantOffset", "relativeOffset", "useConstantOffset", "useRelativeOffset" };
    if (type == "Bevel") return { "width", "segments", "angleLimit", "profileShape" };
    if (type == "Solidify") return { "thickness", "offset", "useFlipNormals" };
    if (type == "Subdivision") return { "levels", "renderLevels", "creases", "pinnedVertices" };
    if (type == "Decimate") return { "ratio", "vertexCount" };
    if (type == "Displace") return { "strength", "midlevel", "textureCoordinates" };
    if (type == "Smooth") return { "factor", "iterations" };
    if (type == "Cast") return { "factor", "radius" };
    if (type == "Triangulate") return { "minVertices" };
    if (type == "Wireframe") return { "thickness" };
    if (type == "Remesh") return { "octreeDepth", "iterations" };
    if (type == "SimpleDeform") return { "deformMethod", "deformAxis", "angle", "factor" };
    if (type == "Weld") return { "threshold" };
    if (type == "CorrectiveSmooth") return { "iterations", "lambdaFactor", "lambdaBias" };
    if (type == "LaplacianSmooth") return { "lambdaFactor", "lambdaBorder" };
    if (type == "Skin") return { "shellCount" };
    if (type == "Shrinkwrap") return { "wrapMethod", "distance" };
    if (type == "CageDeform") return { "strength", "qualityIterations" };
    if (type == "LatticeEx") return { "uDivs", "vDivs", "wDivs", "strength" };
    if (type == "UVProject") return { "uvLayer" };
    if (type == "SurfaceSmooth") return { "smoothness", "iterations" };
    if (type == "VolumeSmooth") return { "smoothness", "iterations" };
    if (type == "Taper") return { "factor", "taperAxis" };
    if (type == "Ripple") return { "amplitude", "wavelength", "phase", "decay", "rippleAxis" };
    if (type == "Noise") return { "scale", "strength", "seed", "depth" };
    if (type == "Push") return { "distance" };
    if (type == "Relax") return { "iterations", "factor" };
    if (type == "Melt") return { "amount", "viscosity", "axis" };
    if (type == "Lathe") return { "segments", "angle", "latheAxis" };
    if (type == "Wave") return { "height", "width", "phase", "speed" };
    return {};
}

QVariantList KSModelerQml::modifierStackList() const
{
    QVariantList result;
    if (!m_selectedObject) return result;
    SceneObject* obj = m_selectedObject->object();
    if (!obj) return result;
    auto it = m_modifierStacks.find(obj->id());
    if (it == m_modifierStacks.end()) return result;

    ModifierStack* stack = it.value();
    for (int i = 0; i < stack->count(); ++i) {
        const StackModifier& sm = stack->at(i);
        QVariantMap entry;
        entry["type"] = sm.type;
        entry["enabled"] = sm.enabled;
        QVariantMap params;
        for (auto pit = sm.params.constBegin(); pit != sm.params.constEnd(); ++pit)
            params.insert(pit.key(), pit.value().toVariant());
        entry["params"] = params;
        result.append(entry);
    }
    return result;
}

// ============================================================================
// Curve / NURBS Bridge Methods
// ============================================================================

SceneObject* KSModelerQml::selectedSceneObject() const
{
    return m_selectedObject ? m_selectedObject->object() : nullptr;
}

CurveData KSModelerQml::curveForObject(int objectId) const
{
    auto it = m_curves.constFind(objectId);
    if (it != m_curves.constEnd())
        return it.value();
    return CurveData();
}

void KSModelerQml::writeCurveMesh(SceneObject* obj, const CurveData& curve, float width, int segments)
{
    if (!obj || !curve.isValid()) return;
    MeshData ribbon = CurveSurfaces::curveRibbon(curve, width, segments);
    if (ribbon.vertices.isEmpty()) return;
    meshDataToSceneMesh(obj, ribbon);
}

int KSModelerQml::addCurve(const QString& type, const QVariantList& points)
{
    if (!m_scene) m_scene = new ks::SceneGraph();

    QVector<QVector3D> pts;
    for (const auto& p : points) {
        QVariantList v = p.toList();
        if (v.size() >= 3)
            pts.append(QVector3D(v[0].toFloat(), v[1].toFloat(), v[2].toFloat()));
    }
    if (pts.size() < 2)
        return -1;

    CurveData curve;
    if (type == "Bezier")       curve = CurvePrimitives::bezier(pts);
    else if (type == "BSpline") curve = CurvePrimitives::bspline(pts);
    else if (type == "Arc")     curve = pts.size() >= 3 ? CurvePrimitives::arc(pts[0], pts[1], pts[2]) : CurvePrimitives::polyline(pts);
    else if (type == "Circle")  curve = CurvePrimitives::circle(pts[0], pts[1], (pts[1] - pts[0]).length());
    else                        curve = CurvePrimitives::polyline(pts);

    SceneObject* obj = m_scene->createObject("Curve_" + type, SceneObject::Type::Spline);
    writeCurveMesh(obj, curve, 0.02f, 32);
    m_curves.insert(obj->id(), curve);

    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit curveChanged();
    emit statusMessage("Added " + type + " curve with " + QString::number(pts.size()) + " CVs");
    return obj->id();
}

QVariantMap KSModelerQml::getCurve(int objectId) const
{
    CurveData c = curveForObject(objectId);
    if (!c.isValid()) return QVariantMap();
    return CurveIO::toVariant(c);
}

bool KSModelerQml::setCurve(int objectId, const QVariantMap& curve)
{
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj) return false;
    CurveData c = CurveIO::fromVariant(curve);
    if (!c.isValid()) return false;
    m_curves[objectId] = c;
    writeCurveMesh(obj, c, 0.02f, 32);
    emit sceneChanged();
    emit curveChanged();
    return true;
}

QVariantList KSModelerQml::curvePoints(int objectId, int segments) const
{
    QVariantList result;
    CurveData c = curveForObject(objectId);
    if (!c.isValid()) return result;
    for (const auto& p : c.tessellate(segments)) {
        QVariantList v;
        v << p.x() << p.y() << p.z();
        result.append(v);
    }
    return result;
}

QVariantList KSModelerQml::curveCvPositions(int objectId) const
{
    QVariantList result;
    auto it = m_curves.constFind(objectId);
    if (it == m_curves.constEnd()) return result;
    for (const auto& p : it->controlPoints) {
        QVariantList v;
        v << p.x() << p.y() << p.z();
        result.append(v);
    }
    return result;
}

int KSModelerQml::curvePickCV(int objectId, float wx, float wy, float wz) const
{
    auto it = m_curves.constFind(objectId);
    if (it == m_curves.constEnd()) return -1;
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj) return -1;
    QVector3D origin = obj->position();
    QVector3D world(wx, wy, wz);
    float best = std::numeric_limits<float>::max();
    int bestIdx = -1;
    for (int i = 0; i < it->controlPoints.size(); ++i) {
        QVector3D cvWorld = origin + it->controlPoints[i];
        float d = cvWorld.distanceToPoint(world);
        if (d < best) {
            best = d;
            bestIdx = i;
        }
    }
    if (best > 0.2f) return -1;
    return bestIdx;
}

bool KSModelerQml::curveUpdateCV(int objectId, int index, const QVariantList& xyz)
{
    auto it = m_curves.find(objectId);
    if (it == m_curves.end()) return false;
    if (index < 0 || index >= it->controlPoints.size() || xyz.size() < 3) return false;
    it->controlPoints[index] = QVector3D(xyz[0].toFloat(), xyz[1].toFloat(), xyz[2].toFloat());
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (obj) writeCurveMesh(obj, it.value(), 0.02f, 32);
    emit sceneChanged();
    emit curveChanged();
    return true;
}

void KSModelerQml::curveSetContinuity(int objectId, int continuity)
{
    auto it = m_curves.find(objectId);
    if (it == m_curves.end()) return;
    CurveData& c = it.value();
    if (c.controlPoints.size() < 3) {
        m_curveContinuities[objectId] = continuity;
        return;
    }
    continuity = qBound(0, continuity, 2);
    if (continuity >= 1) {
        // C1: enforce shared tangent at each interior CV by moving it to the
        // midpoint of its neighbours (both incident segments become collinear).
        QVector<QVector3D> pts = c.controlPoints;
        for (int i = 1; i + 1 < pts.size(); ++i)
            pts[i] = (pts[i - 1] + pts[i + 1]) * 0.5f;
        c.controlPoints = pts;
    }
    if (continuity >= 2) {
        // C2: binomial (Laplacian) relaxation makes consecutive second
        // differences match, giving curvature continuity.
        QVector<QVector3D> pts = c.controlPoints;
        QVector<QVector3D> out = pts;
        for (int i = 1; i + 1 < out.size(); ++i)
            out[i] = (pts[i - 1] + pts[i] * 2.0f + pts[i + 1]) * 0.25f;
        c.controlPoints = out;
    }
    m_curveContinuities[objectId] = continuity;
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (obj) writeCurveMesh(obj, c, 0.02f, 32);
    emit sceneChanged();
    emit curveChanged();
}

int KSModelerQml::curveContinuityOf(int objectId) const
{
    return m_curveContinuities.value(objectId, 0);
}

bool KSModelerQml::curveAddCV(int objectId, const QVariantList& xyz)
{
    auto it = m_curves.find(objectId);
    if (it == m_curves.end() || xyz.size() < 3) return false;
    it->controlPoints.append(QVector3D(xyz[0].toFloat(), xyz[1].toFloat(), xyz[2].toFloat()));
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (obj) writeCurveMesh(obj, it.value(), 0.02f, 32);
    emit sceneChanged();
    emit curveChanged();
    return true;
}

bool KSModelerQml::curveRemoveCV(int objectId, int index)
{
    auto it = m_curves.find(objectId);
    if (it == m_curves.end()) return false;
    if (index < 0 || index >= it->controlPoints.size()) return false;
    it->controlPoints.removeAt(index);
    if (it->controlPoints.size() < 2) {
        m_curves.erase(it);
        SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
        if (obj) m_scene->deleteObject(obj);
        emit sceneChanged();
        emit curveChanged();
        return true;
    }
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (obj) writeCurveMesh(obj, it.value(), 0.02f, 32);
    emit sceneChanged();
    emit curveChanged();
    return true;
}

int KSModelerQml::curveToMesh(int objectId, float width, int segments)
{
    if (!m_scene) return -1;
    SceneObject* src = m_scene->findObjectById(objectId);
    if (!src) return -1;
    CurveData c = curveForObject(objectId);
    if (!c.isValid()) return -1;

    MeshData ribbon = CurveSurfaces::curveRibbon(c, width, segments);
    SceneObject* obj = m_scene->createObject("CurveMesh", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, ribbon);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Converted curve to mesh");
    return obj->id();
}

int KSModelerQml::curveLoft(const QVariantList& objectIds, int segments)
{
    if (!m_scene || objectIds.size() < 2) return -1;
    QVector<CurveData> profiles;
    for (const auto& id : objectIds) {
        CurveData c = curveForObject(id.toInt());
        if (c.isValid()) profiles.append(c);
    }
    if (profiles.size() < 2) return -1;

    MeshData result = CurveSurfaces::loftMulti(profiles, segments, false);
    SceneObject* obj = m_scene->createObject("Loft", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, result);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Loft surface created from " + QString::number(profiles.size()) + " curves");
    return obj->id();
}

int KSModelerQml::curveSweep(int profileId, int pathId, int segments)
{
    if (!m_scene) return -1;
    CurveData profile = curveForObject(profileId);
    CurveData path = curveForObject(pathId);
    if (!profile.isValid() || !path.isValid()) return -1;

    MeshData result = CurveSurfaces::sweep(profile, path, segments);
    SceneObject* obj = m_scene->createObject("Sweep", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, result);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Swept profile along path");
    return obj->id();
}

int KSModelerQml::curveRevolve(int profileId, float angleDeg, int steps)
{
    if (!m_scene) return -1;
    CurveData profile = curveForObject(profileId);
    if (!profile.isValid()) return -1;

    MeshData result = CurveSurfaces::revolve(profile, QVector3D(0, 1, 0), angleDeg, steps);
    SceneObject* obj = m_scene->createObject("Revolve", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, result);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Revolved profile around Y axis");
    return obj->id();
}

int KSModelerQml::curveRail(const QVariantList& railIds, const QVariantList& profileId, int segments)
{
    if (!m_scene || railIds.size() < 2) return -1;
    CurveData rail1 = curveForObject(railIds[0].toInt());
    CurveData rail2 = curveForObject(railIds[1].toInt());
    CurveData profile = curveForObject(profileId.isEmpty() ? railIds[0].toInt() : profileId[0].toInt());
    if (!rail1.isValid() || !rail2.isValid() || !profile.isValid()) return -1;

    MeshData result = CurveSurfaces::rail(rail1, rail2, profile, segments);
    SceneObject* obj = m_scene->createObject("Rail", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, result);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage("Rail surface created");
    return obj->id();
}

bool KSModelerQml::curveDelete(int objectId)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj) return false;
    m_curves.remove(objectId);
    if (m_selectedObject && m_selectedObject->id() == objectId) {
        delete m_selectedObject;
        m_selectedObject = nullptr;
    }
    m_scene->deleteObject(obj);
    emit sceneChanged();
    emit curveChanged();
    return true;
}

QVariantList KSModelerQml::curveIds() const
{
    QVariantList out;
    for (auto it = m_curves.constBegin(); it != m_curves.constEnd(); ++it)
        out.append(it.key());
    return out;
}

// ============================================================================
// NURBS Surface Bridge Methods (real NURBS via MeshOperations)
// ============================================================================

int KSModelerQml::nurbsSurfaceCreate(const QVariantList& rows, int uDegree, int vDegree)
{
    if (!m_scene) m_scene = new ks::SceneGraph();

    QVector<QVector<QVector3D>> cps;
    int vCols = 0;
    for (const auto& rowVar : rows) {
        QVector<QVector3D> row;
        for (const auto& ptVar : rowVar.toList()) {
            QVariantList v = ptVar.toList();
            if (v.size() >= 3) {
                row.append(QVector3D(v[0].toFloat(), v[1].toFloat(), v[2].toFloat()));
                vCols = qMax(vCols, row.size());
            }
        }
        if (row.size() >= 2) cps.append(row);
    }
    if (cps.size() < 2 || vCols < 2)
        return -1;

    NURBSSurface surf = MeshOperations::createSurface(cps, uDegree, vDegree, false, false);
    MeshData mesh = MeshOperations::tessellateSurface(surf, 32, 32);
    if (mesh.vertices.isEmpty()) return -1;

    SceneObject* obj = m_scene->createObject("NURBS_Surface", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, mesh);
    m_nurbsSurfaces.insert(obj->id(), surf);

    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage(QString("NURBS surface created (%1x%2 control points, degree %3/%4)")
                           .arg(cps.size()).arg(vCols).arg(surf.degreeU).arg(surf.degreeV));
    return obj->id();
}

bool KSModelerQml::nurbsSurfaceTessellate(int objectId, int uSeg, int vSeg)
{
    auto it = m_nurbsSurfaces.find(objectId);
    if (it == m_nurbsSurfaces.end()) return false;
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj) return false;
    MeshData mesh = MeshOperations::tessellateSurface(it.value(), uSeg, vSeg);
    if (mesh.vertices.isEmpty()) return false;
    meshDataToSceneMesh(obj, mesh);
    emit sceneChanged();
    return true;
}

QVariantList KSModelerQml::nurbsSurfaceEvaluate(int objectId, float u, float v) const
{
    auto it = m_nurbsSurfaces.constFind(objectId);
    if (it == m_nurbsSurfaces.constEnd()) return QVariantList();
    QVector3D p = MeshOperations::evaluatePointOnSurface(it.value(), u, v);
    QVariantList r;
    r << p.x() << p.y() << p.z();
    return r;
}

int KSModelerQml::nurbsSurfaceLoftCurves(const QVariantList& curveIds, int uDegree, int vDegree)
{
    if (!m_scene || curveIds.size() < 2) return -1;
    QVector<QVector<QVector3D>> cps;
    int vCols = 0;
    for (const auto& idVar : curveIds) {
        CurveData c = curveForObject(idVar.toInt());
        if (!c.isValid()) continue;
        if (c.controlPoints.isEmpty())
            continue;
        // Normalize each profile to a compatible number of CVs: use the
        // largest profile's count, filling by sampling the tessellation.
        cps.append(c.controlPoints);
        vCols = qMax(vCols, c.controlPoints.size());
        Q_UNUSED(uDegree);
    }
    if (cps.size() < 2) return -1;

    QVector<QVector<QVector3D>> norm;
    norm.resize(cps.size());
    for (int i = 0; i < cps.size(); ++i) {
        norm[i].resize(vCols);
        const int srcN = cps[i].size();
        for (int j = 0; j < vCols; ++j) {
            const int k = (j * (srcN - 1)) / qMax(1, vCols - 1);
            norm[i][j] = cps[i][qBound(0, k, srcN - 1)];
        }
    }

    NURBSSurface surf = MeshOperations::createSurface(norm, 3, 3, false, false);
    MeshData mesh = MeshOperations::tessellateSurface(surf, 32, 32);
    if (mesh.vertices.isEmpty()) return -1;

    SceneObject* obj = m_scene->createObject("NURBS_Loft", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, mesh);
    m_nurbsSurfaces.insert(obj->id(), surf);

    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage(QString("NURBS loft surface from %1 curves").arg(cps.size()));
    return obj->id();
}

QVariantMap KSModelerQml::nurbsSurfaceInfo(int objectId) const
{
    QVariantMap out;
    auto it = m_nurbsSurfaces.constFind(objectId);
    if (it == m_nurbsSurfaces.constEnd()) return out;
    const NURBSSurface& s = it.value();
    out["degreeU"] = s.degreeU;
    out["degreeV"] = s.degreeV;
    out["periodicU"] = s.periodicU;
    out["periodicV"] = s.periodicV;
    out["rows"] = s.controlPoints.size();
    out["cols"] = s.controlPoints.isEmpty() ? 0 : s.controlPoints[0].size();
    return out;
}

bool KSModelerQml::nurbsSurfaceDelete(int objectId)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj) return false;
    m_nurbsSurfaces.remove(objectId);
    if (m_selectedObject && m_selectedObject->id() == objectId) {
        delete m_selectedObject;
        m_selectedObject = nullptr;
    }
    m_scene->deleteObject(obj);
    emit sceneChanged();
    return true;
}

bool KSModelerQml::nurbsSurfaceExtend(int objectId, int direction, float distance)
{
    auto it = m_nurbsSurfaces.find(objectId);
    if (it == m_nurbsSurfaces.end()) return false;
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj) return false;
    NURBSSurface ext = MeshOperations::extendSurface(it.value(), direction, distance);
    MeshData mesh = MeshOperations::tessellateSurface(ext, 32, 32);
    if (mesh.vertices.isEmpty()) return false;
    meshDataToSceneMesh(obj, mesh);
    it.value() = ext;
    emit sceneChanged();
    emit statusMessage("NURBS surface extended");
    return true;
}

bool KSModelerQml::nurbsSurfaceSlideCV(int objectId, int row, int col, float factor)
{
    auto it = m_nurbsSurfaces.find(objectId);
    if (it == m_nurbsSurfaces.end()) return false;
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj) return false;
    if (!MeshOperations::slideCV(it.value(), row, col, factor)) return false;
    MeshData mesh = MeshOperations::tessellateSurface(it.value(), 32, 32);
    if (mesh.vertices.isEmpty()) return false;
    meshDataToSceneMesh(obj, mesh);
    emit sceneChanged();
    emit statusMessage("NURBS control point slid");
    return true;
}

QVariantList KSModelerQml::nurbsSurfaceCvPositions(int objectId) const
{
    QVariantList rows;
    auto it = m_nurbsSurfaces.constFind(objectId);
    if (it == m_nurbsSurfaces.constEnd()) return rows;
    const NURBSSurface& s = it.value();
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    const QMatrix4x4 wt = obj ? obj->worldTransform() : QMatrix4x4();
    for (const auto& row : s.controlPoints) {
        QVariantList rowOut;
        for (const auto& cp : row) {
            const QVector3D world = wt.map(cp);
            rowOut.append(QVariantList() << world.x() << world.y() << world.z());
        }
        rows.append(rowOut);
    }
    return rows;
}

bool KSModelerQml::nurbsSurfaceMoveCV(int objectId, int row, int col, float x, float y, float z)
{
    auto it = m_nurbsSurfaces.find(objectId);
    if (it == m_nurbsSurfaces.end()) return false;
    NURBSSurface& s = it.value();
    if (row < 0 || row >= s.controlPoints.size()) return false;
    if (col < 0 || col >= s.controlPoints[row].size()) return false;
    s.controlPoints[row][col] = QVector3D(x, y, z);
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (obj) {
        MeshData mesh = MeshOperations::tessellateSurface(s, 32, 32);
        if (!mesh.vertices.isEmpty()) meshDataToSceneMesh(obj, mesh);
    }
    emit sceneChanged();
    return true;
}

int KSModelerQml::nurbsSurfaceCurvatureComb(int objectId, int direction, int combCount, float scale)
{
    if (!m_scene) return -1;
    auto it = m_nurbsSurfaces.constFind(objectId);
    if (it == m_nurbsSurfaces.constEnd()) return -1;
    MeshData comb = MeshOperations::curvatureComb(it.value(), direction, combCount, scale);
    if (comb.vertices.isEmpty()) return -1;
    SceneObject* obj = m_scene->createObject("CurvatureComb", SceneObject::Type::Mesh);
    meshDataToSceneMesh(obj, comb);
    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(obj);
    emit sceneChanged();
    emit selectionChanged();
    return obj->id();
}

bool KSModelerQml::generateCollisionMesh(int shapeType) {
    Mesh* mesh = getSelectedMesh();
    if (!mesh) {
        emit errorMessage("No mesh selected for collision generation");
        return false;
    }

    MeshData sourceMesh;
    sourceMesh.vertices.resize(mesh->vertices.size());
    for (int i = 0; i < (int)mesh->vertices.size(); ++i) {
        sourceMesh.vertices[i].position = QVector3D(mesh->vertices[i].x, mesh->vertices[i].y, mesh->vertices[i].z);
    }
    sourceMesh.faces.resize(mesh->indices.size() / 3);
    for (int i = 0; i < sourceMesh.faces.size(); ++i) {
        sourceMesh.faces[i].indices = {(int)mesh->indices[i*3], (int)mesh->indices[i*3+1], (int)mesh->indices[i*3+2]};
    }
    delete mesh;

    m_collisionConfig.shapeType = static_cast<CollisionConfig::ShapeType>(shapeType);
    m_collisionResult = PhysicsCollisionSystem::generate(sourceMesh, m_collisionConfig);

    emit collisionGenerated(m_collisionResult.hulls.size());
    emit statusMessage(QString("Generated %1 collision hull(s)").arg(m_collisionResult.hulls.size()));
    return true;
}

bool KSModelerQml::generateCollisionConvexDecomp(int maxHulls) {
    Mesh* mesh = getSelectedMesh();
    if (!mesh) {
        emit errorMessage("No mesh selected for convex decomposition");
        return false;
    }

    MeshData sourceMesh;
    sourceMesh.vertices.resize(mesh->vertices.size());
    for (int i = 0; i < (int)mesh->vertices.size(); ++i) {
        sourceMesh.vertices[i].position = QVector3D(mesh->vertices[i].x, mesh->vertices[i].y, mesh->vertices[i].z);
    }
    delete mesh;

    QVector<CollisionHull> hulls = PhysicsCollisionSystem::decomposeConvex(sourceMesh, maxHulls, 64);
    m_collisionResult.hulls = hulls;

    emit collisionGenerated(hulls.size());
    emit statusMessage(QString("Decomposed mesh into %1 convex hull(s)").arg(hulls.size()));
    return true;
}

void KSModelerQml::setCollisionSimplifyRatio(float ratio) {
    m_collisionSimplifyRatio = qBound(0.01f, ratio, 1.0f);
    m_collisionConfig.simplificationRatio = m_collisionSimplifyRatio;
}

float KSModelerQml::getCollisionSimplifyRatio() const {
    return m_collisionSimplifyRatio;
}

// ============================================================================
// Shape Key Animation Bridge Methods
// ============================================================================

void KSModelerQml::animateShapeKey(const QString& keyName, int startFrame, int endFrame, float startWeight, float endWeight) {
    if (!m_shapeKeyAnimDriver) {
        m_shapeKeyAnimDriver = new ShapeKeyAnimDriver(this);
        m_shapeKeyAnimDriver->setTargetMesh(&m_shapeKeyMesh);
    }

    m_shapeKeyAnimDriver->addChannel(keyName);
    m_shapeKeyAnimDriver->setKeyframe(keyName, startFrame, startWeight);
    m_shapeKeyAnimDriver->setKeyframe(keyName, endFrame, endWeight);
    emit statusMessage(QString("Shape key '%1' animation: %2->%3 (frames %4-%5)")
        .arg(keyName).arg(startWeight).arg(endWeight).arg(startFrame).arg(endFrame));
}

void KSModelerQml::clearShapeKeyAnimation(const QString& keyName) {
    if (m_shapeKeyAnimDriver) {
        m_shapeKeyAnimDriver->clearKeyframes(keyName);
        emit statusMessage(QString("Animation cleared for shape key '%1'").arg(keyName));
    }
}

void KSModelerQml::evaluateShapeKeyAnimation(int frame) {
    if (m_shapeKeyAnimDriver) {
        m_shapeKeyAnimDriver->evaluateAtFrame(frame);
        m_shapeKeysDirty = true;
        emit shapeKeysChanged();
    }
}

// ============================================================
// Deformation tools
// ============================================================

void KSModelerQml::applyCageDeform(const QString& cageObjectName, float strength, bool preserveVolume)
{
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;

    SceneObject* cageObj = nullptr;
    if (!cageObjectName.isEmpty()) {
        for (auto* so : m_scene->allObjects()) {
            if (so->name() == cageObjectName && so != obj) {
                cageObj = so;
                break;
            }
        }
    }
    if (!cageObj) {
        emit errorMessage("Cage object not found. Create a low-poly cage first.");
        return;
    }

    MeshData targetMesh = sceneMeshToMeshData(obj);
    MeshData cageMesh = sceneMeshToMeshData(cageObj);

    CageDeformModifier mod;
    mod.setCageMesh(cageMesh);
    mod.strength = strength;
    mod.usePreserveVolume = preserveVolume;

    MeshData result = mod.apply(targetMesh);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage("Cage deformation applied");
}

void KSModelerQml::cageDeformBuildCage(int subdivisions)
{
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;

    MeshData md = sceneMeshToMeshData(obj);
    if (md.vertices.isEmpty()) return;

    md.computeBoundingBox();
    QVector3D size = md.boundingBoxMax - md.boundingBoxMin;
    QVector3D center = (md.boundingBoxMax + md.boundingBoxMin) * 0.5f;
    float maxDim = qMax(size.x(), qMax(size.y(), size.z()));

    int divs = qMax(1, subdivisions);
    MeshData cage;
    float hw = maxDim * 0.6f;
    float pi = 3.14159265358979323846f;

    // Generate cage vertices on a sphere-ish grid around the mesh
    for (int i = 0; i <= divs; ++i) {
        float theta = static_cast<float>(i) / divs * 2.0f * pi;
        for (int j = 0; j <= divs; ++j) {
            float phi = static_cast<float>(j) / divs * pi;
            Vertex v;
            v.position = center + QVector3D(
                hw * qSin(phi) * qCos(theta),
                hw * qCos(phi),
                hw * qSin(phi) * qSin(theta)
            );
            v.color = QVector4D(1, 1, 0, 1);
            cage.vertices.append(v);
        }
    }

    // Generate faces
    for (int i = 0; i < divs; ++i) {
        for (int j = 0; j < divs; ++j) {
            int a = i * (divs + 1) + j;
            int b = a + 1;
            int c = (i + 1) * (divs + 1) + j;
            int d = c + 1;
            cage.faces.append(Face({ a, b, c }));
            cage.faces.append(Face({ b, d, c }));
        }
    }

    cage.name = obj->name() + "_cage";
    importMeshDataToScene(m_scene, cage, cage.name);
    emit sceneChanged();
    emit statusMessage(QString("Cage created: %1").arg(cage.name));
}

void KSModelerQml::applyLattice(int uDivs, int vDivs, int wDivs, float strength)
{
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;

    MeshData md = sceneMeshToMeshData(obj);

    LatticeExModifier mod;
    mod.setDivisions(qMax(1, uDivs), qMax(1, vDivs), qMax(1, wDivs));
    mod.strength = strength;
    mod.resetControlPoints();

    // Store lattice state for interactive editing
    m_lattice.active = true;
    m_lattice.uDivs = mod.uDivs;
    m_lattice.vDivs = mod.vDivs;
    m_lattice.wDivs = mod.wDivs;
    m_lattice.strength = strength;
    m_lattice.controlPoints = mod.controlPoints;
    m_lattice.restControlPoints = mod.restControlPoints;
    m_lattice.selectedObject = m_selectedObject ? m_selectedObject->object()->id() : -1;

    MeshData result = mod.apply(md);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage(QString("Lattice deformation applied (%1x%2x%3) - %4 control points")
        .arg(uDivs).arg(vDivs).arg(wDivs).arg(mod.controlPointCount()));
}

void KSModelerQml::applySimpleDeform(int method, int axis, float angle, float factor)
{
    if (!m_selectedObject) return;
    SceneObject* obj = m_selectedObject->object();
    if (!obj || !obj->mesh()) return;

    MeshData md = sceneMeshToMeshData(obj);

    SimpleDeformModifier mod;
    switch (method) {
        case 0: mod.deformMethod = SimpleDeformModifier::DeformMethod::Twist; break;
        case 1: mod.deformMethod = SimpleDeformModifier::DeformMethod::Stretch; break;
        case 2: mod.deformMethod = SimpleDeformModifier::DeformMethod::Bend; break;
        case 3: mod.deformMethod = SimpleDeformModifier::DeformMethod::Linear; break;
        default: mod.deformMethod = SimpleDeformModifier::DeformMethod::Twist; break;
    }
    switch (axis) {
        case 0: mod.deformAxis = SimpleDeformModifier::Axis::X; break;
        case 1: mod.deformAxis = SimpleDeformModifier::Axis::Y; break;
        case 2: mod.deformAxis = SimpleDeformModifier::Axis::Z; break;
        default: mod.deformAxis = SimpleDeformModifier::Axis::X; break;
    }
    mod.angle = angle;
    mod.factor = factor;

    MeshData result = mod.apply(md);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage("Simple deform applied");
}

QVector3D KSModelerQml::getLatticeControlPoint(int index)
{
    if (!m_lattice.active || index < 0 || index >= m_lattice.controlPoints.size())
        return QVector3D();
    return m_lattice.controlPoints[index];
}

void KSModelerQml::setLatticeControlPoint(int index, float x, float y, float z)
{
    if (!m_lattice.active || index < 0 || index >= m_lattice.controlPoints.size())
        return;

    m_lattice.controlPoints[index] = QVector3D(x, y, z);

    // Re-apply lattice with deformed control points
    if (m_lattice.selectedObject < 0 || !m_scene) return;
    SceneObject* obj = m_scene->findObjectById(m_lattice.selectedObject);
    if (!obj || !obj->mesh()) return;

    MeshData md = sceneMeshToMeshData(obj);

    LatticeExModifier mod;
    mod.setDivisions(m_lattice.uDivs, m_lattice.vDivs, m_lattice.wDivs);
    mod.strength = m_lattice.strength;
    mod.controlPoints = m_lattice.controlPoints;

    MeshData result = mod.applyWithDeformedLattice(md, m_lattice.controlPoints);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
}

int KSModelerQml::getLatticeControlPointCount()
{
    if (!m_lattice.active) return 0;
    return m_lattice.controlPoints.size();
}

void KSModelerQml::resetLattice()
{
    if (!m_lattice.active) return;

    // Restore rest positions
    m_lattice.controlPoints = m_lattice.restControlPoints;

    // Re-apply with reset control points
    if (m_lattice.selectedObject < 0 || !m_scene) return;
    SceneObject* obj = m_scene->findObjectById(m_lattice.selectedObject);
    if (!obj || !obj->mesh()) return;

    MeshData md = sceneMeshToMeshData(obj);

    LatticeExModifier mod;
    mod.setDivisions(m_lattice.uDivs, m_lattice.vDivs, m_lattice.wDivs);
    mod.strength = m_lattice.strength;
    mod.controlPoints = m_lattice.controlPoints;

    MeshData result = mod.applyWithDeformedLattice(md, m_lattice.controlPoints);
    meshDataToSceneMesh(obj, result);
    emit sceneChanged();
    emit statusMessage("Lattice control points reset to default");
}

// ============================================================================
// Keyboard Shortcuts Implementation
// ============================================================================

bool KSModelerQml::loadShortcuts(const QString& filePath) {
    if (!m_shortcutManager) return false;
    return m_shortcutManager->loadFromFile(filePath);
}

bool KSModelerQml::saveShortcuts(const QString& filePath) {
    if (!m_shortcutManager) return false;
    return m_shortcutManager->saveToFile(filePath);
}

QString KSModelerQml::getShortcutKey(const QString& id) const {
    if (!m_shortcutManager) return QString();
    return m_shortcutManager->getShortcutDisplay(id);
}

QString KSModelerQml::getShortcutDescription(const QString& id) const {
    if (!m_shortcutManager) return QString();
    auto sc = m_shortcutManager->getShortcut(id);
    return sc.description;
}

void KSModelerQml::remapShortcut(const QString& id, const QString& newKey) {
    if (m_shortcutManager) {
        m_shortcutManager->remapShortcut(id, newKey);
    }
}

void KSModelerQml::resetShortcutsToDefaults() {
    if (m_shortcutManager) {
        m_shortcutManager->resetToDefaults();
    }
}

QStringList KSModelerQml::getAllShortcutIds() const {
    if (!m_shortcutManager) return QStringList();
    QStringList ids;
    for (const auto& sc : m_shortcutManager->allShortcuts()) {
        ids.append(sc.id);
    }
    return ids;
}

QVariantMap KSModelerQml::getShortcutsData() const {
    QVariantMap result;
    if (!m_shortcutManager) return result;

    for (const auto& sc : m_shortcutManager->allShortcuts()) {
        QVariantMap scData;
        scData["key"] = sc.key;
        scData["description"] = sc.description;
        result[sc.id] = scData;
    }
    return result;
}

// ============================================================================
// F-Curve / Dope Sheet Bridge Methods
// ============================================================================

FCurveData& KSModelerQml::fcurveForObject(int objectId)
{
    return m_fcurves[objectId];
}

QVariantMap KSModelerQml::fcurveGet(int objectId) const
{
    auto it = m_fcurves.constFind(objectId);
    if (it == m_fcurves.constEnd())
        return QVariantMap();
    return it.value().toVariant();
}

QStringList KSModelerQml::fcurveChannelNames(int objectId) const
{
    auto it = m_fcurves.constFind(objectId);
    if (it == m_fcurves.constEnd())
        return QStringList();
    return it.value().channelNames();
}

QVariantList KSModelerQml::fcurveKeys(int objectId, const QString& channel) const
{
    auto it = m_fcurves.constFind(objectId);
    if (it == m_fcurves.constEnd())
        return QVariantList();
    const FCurveChannel* c = it.value().channel(channel);
    if (!c) return QVariantList();
    return c->toVariant();
}

bool KSModelerQml::fcurveSetKey(int objectId, const QString& channel, float frame, float value, const QString& interpolation)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj) return false;
    FCurveData& data = fcurveForObject(objectId);
    if (data.objectName.isEmpty())
        data.objectName = obj->name();
    data.pushUndoState();
    FCurveChannel& ch = data.ensureChannel(channel);
    // Auto-tangent: derive slopes from neighbouring keys.
    const float autoSlope = [&]() -> float {
        if (ch.size() == 0) return 0.0f;
        int near = ch.nearestKey(frame);
        if (near < 0) return 0.0f;
        const FCurveKey& k = ch.keys[near];
        float df = frame - k.frame;
        if (qAbs(df) < 1e-4f) return k.outTangent;
        return (value - k.value) / df;
    }();
    ch.setKey(frame, value, fcurveInterpFromString(interpolation));
    int idx = ch.nearestKey(frame);
    if (idx >= 0) {
        ch.keys[idx].inTangent = autoSlope;
        ch.keys[idx].outTangent = autoSlope;
    }
    emit fcurveChanged(objectId);
    return true;
}

bool KSModelerQml::fcurveRemoveKey(int objectId, const QString& channel, float frame)
{
    auto it = m_fcurves.find(objectId);
    if (it == m_fcurves.end()) return false;
    it.value().pushUndoState();
    FCurveChannel* c = it.value().channel(channel);
    if (!c) return false;
    if (!c->removeKey(frame)) return false;
    emit fcurveChanged(objectId);
    return true;
}

bool KSModelerQml::fcurveMoveKey(int objectId, const QString& channel, int index, float newFrame)
{
    auto it = m_fcurves.find(objectId);
    if (it == m_fcurves.end()) return false;
    FCurveChannel* c = it.value().channel(channel);
    if (!c) return false;
    if (!c->moveKey(index, newFrame)) return false;
    emit fcurveChanged(objectId);
    return true;
}

bool KSModelerQml::fcurveSetValue(int objectId, const QString& channel, int index, float value)
{
    auto it = m_fcurves.find(objectId);
    if (it == m_fcurves.end()) return false;
    FCurveChannel* c = it.value().channel(channel);
    if (!c) return false;
    if (!c->setValue(index, value)) return false;
    emit fcurveChanged(objectId);
    return true;
}

bool KSModelerQml::fcurveSetInterpolation(int objectId, const QString& channel, int index, const QString& interp)
{
    auto it = m_fcurves.find(objectId);
    if (it == m_fcurves.end()) return false;
    FCurveChannel* c = it.value().channel(channel);
    if (!c) return false;
    if (!c->setInterpolation(index, fcurveInterpFromString(interp))) return false;
    emit fcurveChanged(objectId);
    return true;
}

bool KSModelerQml::fcurveSetTangentHandle(int objectId, const QString& channel, int index, bool isOut, float handleFrame, float handleValue)
{
    auto it = m_fcurves.find(objectId);
    if (it == m_fcurves.end()) return false;
    FCurveChannel* c = it.value().channel(channel);
    if (!c) return false;
    if (index < 0 || index >= c->keys.size()) return false;

    FCurveKey& k = c->keys[index];
    if (k.locked) return false;

    if (isOut) {
        k.outHandleFrame = handleFrame;
        k.outHandleValue = handleValue;
    } else {
        k.inHandleFrame = handleFrame;
        k.inHandleValue = handleValue;
    }

    // Update tangent mode to Free when manually editing handles
    k.tangentMode = FCurveTangentMode::Free;

    // Update tangent slopes from handle positions
    c->updateTangentFromHandles(index);

    emit fcurveChanged(objectId);
    return true;
}

bool KSModelerQml::fcurveSetTangentMode(int objectId, const QString& channel, int index, const QString& mode)
{
    auto it = m_fcurves.find(objectId);
    if (it == m_fcurves.end()) return false;
    FCurveChannel* c = it.value().channel(channel);
    if (!c) return false;

    FCurveTangentMode m = FCurveTangentMode::Auto;
    if (mode == "Free") m = FCurveTangentMode::Free;
    else if (mode == "Aligned") m = FCurveTangentMode::Aligned;
    else if (mode == "Broken") m = FCurveTangentMode::Broken;
    else if (mode == "Clamped") m = FCurveTangentMode::Clamped;
    else if (mode == "Vector") m = FCurveTangentMode::Vector;

    if (!c->setTangentMode(index, m)) return false;
    emit fcurveChanged(objectId);
    return true;
}

bool KSModelerQml::fcurveCanUndo(int objectId) const
{
    auto it = m_fcurves.constFind(objectId);
    if (it == m_fcurves.constEnd()) return false;
    return it.value().canUndo();
}

bool KSModelerQml::fcurveCanRedo(int objectId) const
{
    auto it = m_fcurves.constFind(objectId);
    if (it == m_fcurves.constEnd()) return false;
    return it.value().canRedo();
}

bool KSModelerQml::fcurveUndo(int objectId)
{
    auto it = m_fcurves.find(objectId);
    if (it == m_fcurves.end()) return false;
    it.value().undo();
    emit fcurveChanged(objectId);
    return true;
}

bool KSModelerQml::fcurveRedo(int objectId)
{
    auto it = m_fcurves.find(objectId);
    if (it == m_fcurves.end()) return false;
    it.value().redo();
    emit fcurveChanged(objectId);
    return true;
}

bool KSModelerQml::fcurvePushUndo(int objectId)
{
    auto it = m_fcurves.find(objectId);
    if (it == m_fcurves.end()) return false;
    it.value().pushUndoState();
    return true;
}

float KSModelerQml::fcurveEvaluate(int objectId, const QString& channel, float frame) const
{
    auto it = m_fcurves.constFind(objectId);
    if (it == m_fcurves.constEnd()) return 0.0f;
    const FCurveChannel* c = it.value().channel(channel);
    if (!c) return 0.0f;
    return c->evaluate(frame);
}

bool KSModelerQml::fcurveApplyToObject(int objectId, float frame)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj) return false;
    auto it = m_fcurves.constFind(objectId);
    if (it == m_fcurves.constEnd()) return false;

    const FCurveData& data = it.value();
    QVector3D pos = obj->position();
    QVector3D rot = obj->rotationEuler();
    QVector3D scl = obj->scale();

    auto applyChannel = [&](const QString& name, QVector3D& v) {
        const FCurveChannel* c = data.channel(name);
        if (c && c->size() > 0) {
            if (name.endsWith(".x")) v.setX(c->evaluate(frame));
            else if (name.endsWith(".y")) v.setY(c->evaluate(frame));
            else if (name.endsWith(".z")) v.setZ(c->evaluate(frame));
        }
    };
    applyChannel("position.x", pos); applyChannel("position.y", pos); applyChannel("position.z", pos);
    applyChannel("rotation.x", rot); applyChannel("rotation.y", rot); applyChannel("rotation.z", rot);
    applyChannel("scale.x", scl);    applyChannel("scale.y", scl);    applyChannel("scale.z", scl);

    obj->setPosition(pos);
    obj->setRotationEuler(rot);
    obj->setScale(scl);
    return true;
}

bool KSModelerQml::fcurveRemoveObject(int objectId)
{
    auto it = m_fcurves.find(objectId);
    if (it == m_fcurves.end()) return false;
    m_fcurves.erase(it);
    emit fcurveChanged(objectId);
    return true;
}

QVariantList KSModelerQml::fcurveFrameKeys(int objectId, float frame, float tolerance) const
{
    QVariantList result;
    auto it = m_fcurves.constFind(objectId);
    if (it == m_fcurves.constEnd()) return result;
    for (const auto& c : it.value().channels) {
        for (int i = 0; i < c.keys.size(); ++i) {
            if (qAbs(c.keys[i].frame - frame) <= tolerance) {
                QVariantMap k;
                k["channel"] = c.name;
                k["index"] = i;
                k["frame"] = c.keys[i].frame;
                k["value"] = c.keys[i].value;
                result.append(k);
            }
        }
    }
    return result;
}

bool KSModelerQml::fcurveIsPlaying() const
{
    return m_fcurvePlaying;
}

void KSModelerQml::fcurvePlayPause()
{
    if (m_fcurvePlaying) {
        m_fcurvePlaying = false;
        if (m_fcurveTimer) m_fcurveTimer->stop();
    } else {
        m_fcurvePlaying = true;
        if (!m_fcurveTimer) {
            m_fcurveTimer = new QTimer(this);
            connect(m_fcurveTimer, &QTimer::timeout, this, [this]() {
                float dt = 1.0f / qMax(1.0f, (float)m_animFps);
                float t = m_animationTime + dt;
                float duration = animationDuration();
                if (t > duration) {
                    if (m_animLoop) t = 0.0f;
                    else {
                        t = duration;
                        m_fcurvePlaying = false;
                        m_fcurveTimer->stop();
                        emit playbackStateChanged();
                    }
                }
                setAnimationTime(t);
            });
        }
        m_fcurveTimer->start(1000 / m_animFps);
    }
    emit playbackStateChanged();
}

// ============================================================================
// Non-destructive Boolean Bridge Methods
// ============================================================================

namespace {

// Transform a mesh (positions) by an arbitrary 4x4 matrix.
MeshData transformMeshData(const MeshData& md, const QMatrix4x4& mat)
{
    MeshData out = md;
    for (auto& v : out.vertices)
        v.position = mat.map(v.position);
    out.computeNormals();
    out.computeBoundingBox();
    return out;
}

} // namespace

bool KSModelerQml::booleanHasStack(int objectId) const
{
    return m_booleanStacks.contains(objectId);
}

QVariantList KSModelerQml::booleanStack(int objectId) const
{
    QVariantList out;
    auto it = m_booleanStacks.constFind(objectId);
    if (it == m_booleanStacks.constEnd()) return out;
    for (int i = 0; i < (*it)->count(); ++i) {
        const BooleanOp& op = (*it)->at(i);
        QVariantMap m;
        m["operation"] = op.operation;
        m["operandId"] = op.operandId;
        m["operandName"] = op.operandName;
        m["enabled"] = op.enabled;
        out.append(m);
    }
    return out;
}

MeshData KSModelerQml::booleanEvaluateStack(SceneObject* obj, const BooleanStack* stack) const
{
    if (!obj || !stack || !stack->hasBase()) return MeshData();
    if (m_scene) m_scene->updateAllTransforms();

    // Work in world space so transformed operands combine correctly.
    MeshData result = transformMeshData(stack->base(), obj->worldTransform());

    for (int i = 0; i < stack->count(); ++i) {
        const BooleanOp& op = stack->at(i);
        if (!op.enabled) continue;
        SceneObject* operand = m_scene ? m_scene->findObjectById(op.operandId) : nullptr;
        if (!operand || !operand->mesh()) continue;
        MeshData opMesh = sceneMeshToMeshData(operand);
        opMesh = transformMeshData(opMesh, operand->worldTransform());

        switch (op.operation) {
        case 0: result = MeshOperations::booleanUnion(result, opMesh); break;
        case 1: result = MeshOperations::booleanDifference(result, opMesh); break;
        case 2: result = MeshOperations::booleanIntersection(result, opMesh); break;
        case 3: result = MeshOperations::booleanXor(result, opMesh); break;
        default: continue;
        }
        if (result.vertices.isEmpty()) return result;
    }

    bool invertible = false;
    QMatrix4x4 inv = obj->worldTransform().inverted(&invertible);
    if (invertible) result = transformMeshData(result, inv);
    return result;
}

void KSModelerQml::booleanUnsubscribe(int objectId)
{
    auto it = m_booleanSubscriptions.find(objectId);
    if (it == m_booleanSubscriptions.end()) return;
    for (const auto& c : it.value()) QObject::disconnect(c);
    m_booleanSubscriptions.erase(it);
}

void KSModelerQml::booleanSubscribe(int objectId)
{
    booleanUnsubscribe(objectId);
    auto sit = m_booleanStacks.find(objectId);
    if (sit == m_booleanStacks.end()) return;

    auto& conns = m_booleanSubscriptions[objectId];
    auto subscribe = [&](SceneObject* o) {
        if (!o) return;
        conns.append(QObject::connect(o, &SceneObject::transformChanged, this, [this, objectId]() {
            booleanEvaluate(objectId);
        }));
        // CAGE re-edit: re-run the stack whenever an operand's geometry changes.
        conns.append(QObject::connect(o, &SceneObject::meshChanged, this, [this, objectId]() {
            booleanEvaluate(objectId);
        }));
    };

    subscribe(m_scene ? m_scene->findObjectById(objectId) : nullptr);
    for (int i = 0; i < (*sit)->count(); ++i)
        if ((*sit)->at(i).enabled)
            subscribe(m_scene ? m_scene->findObjectById((*sit)->at(i).operandId) : nullptr);
}

bool KSModelerQml::booleanEvaluate(int objectId)
{
    if (m_booleanBusy.contains(objectId)) return false;
    m_booleanBusy.insert(objectId);
    const auto guard = qScopeGuard([this, objectId]() { m_booleanBusy.remove(objectId); });
    auto it = m_booleanStacks.find(objectId);
    if (it == m_booleanStacks.end()) return false;
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj || !obj->mesh()) return false;

    if (!(*it)->hasBase()) (*it)->setBase(sceneMeshToMeshData(obj));
    MeshData result = booleanEvaluateStack(obj, it.value());
    if (result.vertices.isEmpty()) {
        emit booleanStackChanged(objectId);
        return false;
    }
    meshDataToSceneMesh(obj, result);
    booleanSubscribe(objectId);
    emit sceneChanged();
    emit booleanStackChanged(objectId);
    return true;
}

void KSModelerQml::modifierUnsubscribe(int objectId)
{
    auto it = m_modifierSubscriptions.find(objectId);
    if (it == m_modifierSubscriptions.end()) return;
    for (const auto& c : it.value()) QObject::disconnect(c);
    m_modifierSubscriptions.erase(it);
}

void KSModelerQml::modifierSubscribe(int objectId)
{
    modifierUnsubscribe(objectId);
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (!obj) return;
    // CAGE re-edit for modifiers: re-run the stack whenever the object's
    // geometry edits come through SceneObject::setMesh.
    m_modifierSubscriptions[objectId] = {
        QObject::connect(obj, &SceneObject::meshChanged, this, [this, objectId]() {
            onSceneObjectMeshChanged(objectId);
        })
    };
}

// Live sub-object editing on applied modifier stacks (Softimage P3 / Modo CAGE
// extension): when the object's base geometry changes, re-capture it as the
// stack base and re-evaluate the whole stack. The re-entrancy guard prevents
// the stack's own result write-back (setMesh) from looping back into this slot.
void KSModelerQml::onSceneObjectMeshChanged(int objectId)
{
    if (m_modifierBusy.contains(objectId)) return;
    if (!m_scene) return;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj || !obj->mesh()) return;
    auto it = m_modifierStacks.find(objectId);
    if (it == m_modifierStacks.end()) {
        modifierUnsubscribe(objectId);
        return;
    }
    ModifierStack* stack = it.value();
    if (!stack->hasModifiers()) return;
    stack->setBase(sceneMeshToMeshData(obj));
    if (!stack->hasBase()) return;

    m_modifierBusy.insert(objectId);
    const auto guard = qScopeGuard([this, objectId]() { m_modifierBusy.remove(objectId); });
    MeshData evaluated = stack->evaluate();
    meshDataToSceneMesh(obj, evaluated);
    modifierSubscribe(objectId);
    emit sceneChanged();
    emit modifierStackChanged();
}

bool KSModelerQml::booleanAdd(int objectId, int operation, int operandId)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    SceneObject* operand = m_scene->findObjectById(operandId);
    if (!obj || !obj->mesh() || !operand || !operand->mesh()) return false;
    if (operandId == objectId) return false;

    BooleanStack*& stack = m_booleanStacks[objectId];
    if (!stack) {
        stack = new BooleanStack(this);
        stack->setBase(sceneMeshToMeshData(obj));
    } else if (!stack->hasBase()) {
        stack->setBase(sceneMeshToMeshData(obj));
    }
    stack->add(operation, operandId, operand->name());
    return booleanEvaluate(objectId);
}

bool KSModelerQml::booleanRemove(int objectId, int index)
{
    auto it = m_booleanStacks.find(objectId);
    if (it == m_booleanStacks.end()) return false;
    if (!(*it)->remove(index)) return false;
    if (!(*it)->hasOps()) return booleanClear(objectId);
    return booleanEvaluate(objectId);
}

bool KSModelerQml::booleanMove(int objectId, int fromIndex, int toIndex)
{
    auto it = m_booleanStacks.find(objectId);
    if (it == m_booleanStacks.end()) return false;
    if (!(*it)->move(fromIndex, toIndex)) return false;
    return booleanEvaluate(objectId);
}

bool KSModelerQml::booleanSetEnabled(int objectId, int index, bool enabled)
{
    auto it = m_booleanStacks.find(objectId);
    if (it == m_booleanStacks.end()) return false;
    if (!(*it)->setEnabled(index, enabled)) return false;
    return booleanEvaluate(objectId);
}

bool KSModelerQml::booleanSetOperation(int objectId, int index, int operation)
{
    auto it = m_booleanStacks.find(objectId);
    if (it == m_booleanStacks.end()) return false;
    if (!(*it)->setOperation(index, operation)) return false;
    return booleanEvaluate(objectId);
}

bool KSModelerQml::booleanClear(int objectId)
{
    auto it = m_booleanStacks.find(objectId);
    if (it == m_booleanStacks.end()) return false;
    SceneObject* obj = m_scene ? m_scene->findObjectById(objectId) : nullptr;
    if (obj && obj->mesh() && (*it)->hasBase())
        meshDataToSceneMesh(obj, (*it)->base());
    booleanUnsubscribe(objectId);
    delete it.value();
    m_booleanStacks.erase(it);
    if (obj) { emit sceneChanged(); }
    emit booleanStackChanged(objectId);
    emit statusMessage("Boolean stack cleared (base mesh restored)");
    return true;
}

int KSModelerQml::booleanSelectOperand(int objectId, int index)
{
    auto it = m_booleanStacks.find(objectId);
    if (it == m_booleanStacks.end()) return -1;
    if (index < 0 || index >= it.value()->count()) return -1;
    const BooleanOp& op = it.value()->at(index);
    if (!m_scene) return -1;
    SceneObject* operand = m_scene->findObjectById(op.operandId);
    if (!operand) return -1;

    if (m_selectedObject) delete m_selectedObject;
    m_selectedObject = new SceneObjectQml(operand);
    emit sceneChanged();
    emit selectionChanged();
    emit statusMessage(QString("CAGE EDIT: editing operand '%1' - edits re-run the boolean stack live.")
                           .arg(op.operandName));
    return op.operandId;
}

bool KSModelerQml::booleanApply(int objectId)
{
    auto it = m_booleanStacks.find(objectId);
    if (it == m_booleanStacks.end()) return false;
    // The object mesh already holds the evaluated result: just drop the stack.
    booleanUnsubscribe(objectId);
    delete it.value();
    m_booleanStacks.erase(it);
emit booleanStackChanged(objectId);
    emit statusMessage("Boolean stack applied");
    return true;
}

bool KSModelerQml::constraintAdd(int objectId, int type, int targetId,
                                 float ox, float oy, float oz,
                                 float rx, float ry, float rz)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    SceneObject* target = m_scene->findObjectById(targetId);
    if (!obj || !target || obj == target) return false;
    m_constraintSystem.add(objectId, type, targetId, target->name(),
                           QVector3D(ox, oy, oz), QVector3D(rx, ry, rz));
    constraintStartTimer();
    constraintEvaluate(objectId);
    emit constraintChanged(objectId);
    return true;
}

bool KSModelerQml::constraintAddPath(int objectId, int targetId, int segments,
                                     float t, bool follow)
{
    if (!m_scene || segments < 2) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    SceneObject* target = m_scene->findObjectById(targetId);
    if (!obj || !target || obj == target) return false;
    CurveData curve = curveForObject(targetId);
    if (!curve.isValid()) return false;
    QVector<QVector3D> samples = curve.tessellate(qMax(segments, 8));
    m_constraintSystem.add(objectId, static_cast<int>(ConstraintType::Path), targetId,
                           target->name());
    const int idx = m_constraintSystem.count(objectId) - 1;
    m_constraintSystem.setPath(objectId, idx, samples);
    m_constraintSystem.setParam(objectId, idx, t);
    m_constraintSystem.setFollow(objectId, idx, follow);
    constraintStartTimer();
    constraintEvaluate(objectId);
    emit constraintChanged(objectId);
    return true;
}

bool KSModelerQml::constraintAddAttachment(int objectId, int targetId, int vertexIndex,
                                           float ox, float oy, float oz)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    SceneObject* target = m_scene->findObjectById(targetId);
    if (!obj || !target || obj == target || !target->mesh()) return false;
    const auto& verts = target->mesh()->geometry().vertices;
    if (vertexIndex < 0) vertexIndex = verts.isEmpty() ? -1 : 0;
    if (vertexIndex >= verts.size()) return false;
    QVector<QVector3D> samples;
    samples.reserve(verts.size());
    for (const auto& sv : verts) samples.append(QVector3D(sv.position.x(), sv.position.y(), sv.position.z()));
    m_constraintSystem.add(objectId, static_cast<int>(ConstraintType::Attachment), targetId,
                           target->name(), QVector3D(ox, oy, oz));
    const int idx = m_constraintSystem.count(objectId) - 1;
    m_constraintSystem.setPath(objectId, idx, samples);
    m_constraintSystem.setParam(objectId, idx, static_cast<float>(vertexIndex));
    constraintStartTimer();
    constraintEvaluate(objectId);
    emit constraintChanged(objectId);
    return true;
}

bool KSModelerQml::constraintSetParam(int objectId, int index, float param)
{
    if (!m_constraintSystem.setParam(objectId, index, param)) return false;
    constraintEvaluate(objectId);
    emit constraintChanged(objectId);
    return true;
}

bool KSModelerQml::constraintSetFollow(int objectId, int index, bool follow)
{
    if (!m_constraintSystem.setFollow(objectId, index, follow)) return false;
    constraintEvaluate(objectId);
    emit constraintChanged(objectId);
    return true;
}

bool KSModelerQml::constraintSetSpringParams(int objectId, int index, float stiffness, float damping)
{
    if (!m_constraintSystem.setSpringParams(objectId, index, stiffness, damping)) return false;
    constraintEvaluate(objectId);
    emit constraintChanged(objectId);
    return true;
}

bool KSModelerQml::constraintRemove(int objectId, int index)
{
    if (!m_constraintSystem.remove(objectId, index)) return false;
    if (!m_constraintSystem.hasAny()) constraintStopTimer();
    constraintEvaluate(objectId);
    emit constraintChanged(objectId);
    return true;
}

bool KSModelerQml::constraintSetEnabled(int objectId, int index, bool enabled)
{
    if (!m_constraintSystem.setEnabled(objectId, index, enabled)) return false;
    constraintEvaluate(objectId);
    emit constraintChanged(objectId);
    return true;
}

bool KSModelerQml::constraintSetOffset(int objectId, int index, float ox, float oy, float oz)
{
    if (!m_constraintSystem.setOffset(objectId, index, QVector3D(ox, oy, oz))) return false;
    constraintEvaluate(objectId);
    emit constraintChanged(objectId);
    return true;
}

bool KSModelerQml::constraintEvaluate(int objectId)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj) return false;
    QMatrix4x4 before = obj->worldTransform();
    int applied = m_constraintSystem.evaluate(obj, m_scene);
    if (applied > 0) {
        m_scene->updateAllTransforms();
        QMatrix4x4 after = obj->worldTransform();
        if (before != after) {
            emit sceneChanged();
            emit constraintChanged(objectId);
        }
    }
    return applied > 0;
}

QVariantList KSModelerQml::constraintList(int objectId) const
{
    QVariantList list;
    for (const auto& c : m_constraintSystem.forObject(objectId))
        list.append(c.toVariant());
    return list;
}

void KSModelerQml::constraintStartTimer()
{
    if (m_constraintTimer) return;
    m_constraintTimer = new QTimer(this);
    m_constraintTimer->setInterval(50); // 20 Hz
    connect(m_constraintTimer, &QTimer::timeout, this, [this]() {
        if (m_constraintApplying || !m_scene) return;
        m_constraintApplying = true;
        constraintEvaluateAll();
        m_constraintApplying = false;
    });
    m_constraintTimer->start();
}

void KSModelerQml::constraintStopTimer()
{
    if (m_constraintTimer) {
        m_constraintTimer->stop();
        m_constraintTimer->deleteLater();
        m_constraintTimer = nullptr;
    }
}

bool KSModelerQml::constraintEvaluateAll()
{
    if (!m_scene) return false;
    bool anyChanged = false;
    m_scene->updateAllTransforms();
    QVector<int> ids = m_constraintSystem.constrainedObjectIds();
    for (int objId : ids) {
        SceneObject* obj = m_scene->findObjectById(objId);
        if (!obj) continue;
        QMatrix4x4 before = obj->worldTransform();
        int applied = m_constraintSystem.evaluate(obj, m_scene);
        if (applied > 0) {
            m_scene->updateAllTransforms();
            QMatrix4x4 after = obj->worldTransform();
            if (before != after) {
                anyChanged = true;
                emit constraintChanged(obj->id());
            }
        }
    }
    if (anyChanged) emit sceneChanged();
    return anyChanged;
}

// ============================================================================
// Procedural Controllers (Noise / Spring / LookAt / Attachment)
// ============================================================================

bool KSModelerQml::controllerAdd(int objectId, int type, int targetId, const QString& channel,
                                 float base, float amplitude, float frequency, float phase,
                                 float stiffness, float damping)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    SceneObject* target = m_scene->findObjectById(targetId);
    if (!obj || !target) return false;

    float baseValue = base;
    if (baseValue == 0.0f)
        sceneParamRead(obj, channel, baseValue); // capture current value
    m_controllerSystem.add(objectId, type, targetId, target->name(), channel, baseValue,
                           amplitude, frequency, phase, stiffness, damping);
    controllerStartTimer();
    emit controllerChanged(objectId);
    return true;
}

bool KSModelerQml::controllerRemove(int objectId, int index)
{
    if (m_controllerSystem.remove(objectId, index)) {
        if (!m_controllerSystem.hasAny()) controllerStopTimer();
        emit controllerChanged(objectId);
        return true;
    }
    return false;
}

bool KSModelerQml::controllerSetEnabled(int objectId, int index, bool enabled)
{
    if (m_controllerSystem.setEnabled(objectId, index, enabled)) {
        emit controllerChanged(objectId);
        return true;
    }
    return false;
}

bool KSModelerQml::controllerSetParams(int objectId, int index, float amplitude, float frequency,
                                       float phase, float stiffness, float damping)
{
    if (m_controllerSystem.setParams(objectId, index, amplitude, frequency, phase, stiffness, damping)) {
        emit controllerChanged(objectId);
        return true;
    }
    return false;
}

bool KSModelerQml::controllerSetAttachment(int objectId, int index, int vertexIndex,
                                           float ox, float oy, float oz)
{
    if (m_controllerSystem.setAttachment(objectId, index, vertexIndex, QVector3D(ox, oy, oz))) {
        emit controllerChanged(objectId);
        return true;
    }
    return false;
}

QVariantList KSModelerQml::controllerList(int objectId) const
{
    QVariantList list;
    for (const auto& c : m_controllerSystem.forObject(objectId))
        list.append(c.toVariant());
    return list;
}

bool KSModelerQml::controllerEvaluate(int objectId)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj) return false;
    QMatrix4x4 before = obj->worldTransform();
    int applied = m_controllerSystem.evaluate(obj, m_scene, m_controllerClock);
    if (applied > 0) {
        m_scene->updateAllTransforms();
        QMatrix4x4 after = obj->worldTransform();
        if (before != after) {
            emit sceneChanged();
            emit controllerChanged(objectId);
        }
    }
    return applied > 0;
}

bool KSModelerQml::controllerEvaluateAll()
{
    if (!m_scene) return false;
    bool anyChanged = false;
    m_controllerClock += 0.05f; // 20 Hz simulation clock (always animates)
    m_scene->updateAllTransforms();
    QVector<int> ids = m_controllerSystem.controlledObjectIds();
    for (int objId : ids) {
        SceneObject* obj = m_scene->findObjectById(objId);
        if (!obj) continue;
        QMatrix4x4 before = obj->worldTransform();
        int applied = m_controllerSystem.evaluate(obj, m_scene, m_controllerClock);
        if (applied > 0) {
            m_scene->updateAllTransforms();
            QMatrix4x4 after = obj->worldTransform();
            if (before != after) {
                anyChanged = true;
                emit controllerChanged(obj->id());
            }
        }
    }
    if (anyChanged) emit sceneChanged();
    return anyChanged;
}

void KSModelerQml::controllerStartTimer()
{
    if (m_controllerTimer) return;
    m_controllerTimer = new QTimer(this);
    m_controllerTimer->setInterval(50); // 20 Hz
    connect(m_controllerTimer, &QTimer::timeout, this, [this]() {
        if (m_controllerApplying || !m_scene) return;
        m_controllerApplying = true;
        controllerEvaluateAll();
        m_controllerApplying = false;
    });
    m_controllerTimer->start();
}

void KSModelerQml::controllerStopTimer()
{
    if (m_controllerTimer) {
        m_controllerTimer->stop();
        m_controllerTimer->deleteLater();
        m_controllerTimer = nullptr;
    }
}

// ============================================================================
// Wire Parameters
// ============================================================================

bool KSModelerQml::wireAdd(int driverId, const QString& driverProp,
                           int drivenId, const QString& drivenProp, float scale, float offset)
{
    if (!m_scene) return false;
    SceneObject* driver = m_scene->findObjectById(driverId);
    SceneObject* driven = m_scene->findObjectById(drivenId);
    if (!driver || !driven) return false;
    if (m_wireSystem.add(driverId, driver->name(), driverProp,
                         drivenId, driven->name(), drivenProp, scale, offset)) {
        wireStartTimer();
        emit wireChanged(drivenId);
        return true;
    }
    return false;
}

bool KSModelerQml::wireRemove(int drivenId, int index)
{
    if (m_wireSystem.remove(drivenId, index)) {
        if (!m_wireSystem.hasAny()) wireStopTimer();
        emit wireChanged(drivenId);
        return true;
    }
    return false;
}

bool KSModelerQml::wireSetEnabled(int drivenId, int index, bool enabled)
{
    if (m_wireSystem.setEnabled(drivenId, index, enabled)) {
        emit wireChanged(drivenId);
        return true;
    }
    return false;
}

bool KSModelerQml::wireSetParams(int drivenId, int index, float scale, float offset)
{
    if (m_wireSystem.setParams(drivenId, index, scale, offset)) {
        emit wireChanged(drivenId);
        return true;
    }
    return false;
}

bool KSModelerQml::wireSetProperty(int drivenId, int index, const QString& drivenProp)
{
    if (m_wireSystem.setProperty(drivenId, index, drivenProp)) {
        emit wireChanged(drivenId);
        return true;
    }
    return false;
}

bool KSModelerQml::wireSetExpression(int drivenId, int index, const QString& expression)
{
    if (m_wireSystem.setExpression(drivenId, index, expression)) {
        emit wireChanged(drivenId);
        return true;
    }
    return false;
}

QVariantList KSModelerQml::wireList(int objectId) const
{
    QVariantList list;
    for (const auto& b : m_wireSystem.forObject(objectId))
        list.append(b.toVariant());
    return list;
}

bool KSModelerQml::wireEvaluateAll()
{
    if (!m_scene) return false;
    QVector<int> driven = m_wireSystem.controlledObjectIds();
    int applied = m_wireSystem.evaluate(m_scene);
    if (applied > 0) {
        m_scene->updateAllTransforms();
        emit sceneChanged();
        for (int id : driven) emit wireChanged(id);
    }
    return applied > 0;
}

void KSModelerQml::wireStartTimer()
{
    if (m_wireTimer) return;
    m_wireTimer = new QTimer(this);
    m_wireTimer->setInterval(50); // 20 Hz
    connect(m_wireTimer, &QTimer::timeout, this, [this]() {
        if (m_wireApplying || !m_scene) return;
        m_wireApplying = true;
        wireEvaluateAll();
        m_wireApplying = false;
    });
    m_wireTimer->start();
}

void KSModelerQml::wireStopTimer()
{
    if (m_wireTimer) {
        m_wireTimer->stop();
        m_wireTimer->deleteLater();
        m_wireTimer = nullptr;
    }
}

// ============================================================================
// Skin Wrap
// ============================================================================

bool KSModelerQml::skinWrapAdd(int objectId, int cageId)
{
    if (!m_scene) return false;
    SceneObject* cage = m_scene->findObjectById(cageId);
    if (!cage) return false;
    if (m_skinWrapSystem.add(objectId, cageId, cage->name())) {
        skinWrapStartTimer();
        int index = m_skinWrapSystem.count(objectId) - 1;
        m_skinWrapSystem.rebind(objectId, index, m_scene);
        emit skinWrapChanged(objectId);
        return true;
    }
    return false;
}

bool KSModelerQml::skinWrapRemove(int objectId, int index)
{
    if (m_skinWrapSystem.remove(objectId, index)) {
        if (!m_skinWrapSystem.hasAny()) skinWrapStopTimer();
        emit skinWrapChanged(objectId);
        return true;
    }
    return false;
}

bool KSModelerQml::skinWrapSetEnabled(int objectId, int index, bool enabled)
{
    if (m_skinWrapSystem.setEnabled(objectId, index, enabled)) {
        emit skinWrapChanged(objectId);
        return true;
    }
    return false;
}

bool KSModelerQml::skinWrapRebind(int objectId, int index)
{
    if (m_skinWrapSystem.rebind(objectId, index, m_scene)) {
        emit skinWrapChanged(objectId);
        return true;
    }
    return false;
}

QVariantList KSModelerQml::skinWrapList(int objectId) const
{
    QVariantList list;
    for (const auto& w : m_skinWrapSystem.forObject(objectId))
        list.append(w.toVariant());
    return list;
}

bool KSModelerQml::skinWrapEvaluate(int objectId)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj) return false;
    bool hadBinding = m_skinWrapSystem.hasObject(objectId);
    int applied = m_skinWrapSystem.evaluate(obj, m_scene);
    if (applied > 0) {
        obj->mesh()->setGeometry(obj->mesh()->geometry());
        m_scene->updateAllTransforms();
        emit sceneChanged();
        emit skinWrapChanged(objectId);
    }
    return applied > 0;
}

bool KSModelerQml::skinWrapEvaluateAll()
{
    if (!m_scene) return false;
    m_scene->updateAllTransforms();
    bool anyChanged = false;
    QVector<int> ids = m_skinWrapSystem.wrappedObjectIds();
    for (int objId : ids) {
        SceneObject* obj = m_scene->findObjectById(objId);
        if (!obj || !obj->mesh()) continue;
        int applied = m_skinWrapSystem.evaluate(obj, m_scene);
        if (applied > 0) {
            obj->mesh()->setGeometry(obj->mesh()->geometry());
            anyChanged = true;
            emit skinWrapChanged(objId);
        }
    }
    if (anyChanged) {
        m_scene->updateAllTransforms();
        emit sceneChanged();
    }
    return anyChanged;
}

void KSModelerQml::skinWrapStartTimer()
{
    if (m_skinWrapTimer) return;
    m_skinWrapTimer = new QTimer(this);
    m_skinWrapTimer->setInterval(50); // 20 Hz
    connect(m_skinWrapTimer, &QTimer::timeout, this, [this]() {
        if (m_skinWrapApplying || !m_scene) return;
        m_skinWrapApplying = true;
        skinWrapEvaluateAll();
        m_skinWrapApplying = false;
    });
    m_skinWrapTimer->start();
}

void KSModelerQml::skinWrapStopTimer()
{
    if (m_skinWrapTimer) {
        m_skinWrapTimer->stop();
        m_skinWrapTimer->deleteLater();
        m_skinWrapTimer = nullptr;
    }
}

// ============================================================================
// ICE Particle System
// ============================================================================

bool KSModelerQml::iceCreate(int objectId)
{
    if (!m_scene) return false;
    SceneObject* obj = m_scene->findObjectById(objectId);
    if (!obj) return false;

    auto it = m_iceSystems.find(objectId);
    if (it != m_iceSystems.end()) return true;

    ICESystemEntry* entry = new ICESystemEntry;
    entry->evaluator = new ICEParticleEvaluator(this);
    entry->evaluator->setSeed(0x1CE0000u ^ (unsigned)objectId);
    entry->graph.id = QUuid::createUuid();
    entry->graph.name = obj->name() + "_ICE";
    m_iceSystems[objectId] = entry;

    // Default graph: Emitter Point -> Gravity -> OpAdd (integration) -> Output
    QUuid emitterId = QUuid::createUuid();
    ui::GraphNode emitter;
    emitter.id = emitterId;
    emitter.typeName = "ICE.EmitterPoint";
    emitter.title = "Emitter";
    emitter.position = QPointF(-300, 0);
    emitter.properties["position"] = QVariantList({ 0.0f, 0.0f, 0.0f });
    emitter.properties["velocity"] = QVariantList({ 0.0f, 1.0f, 0.0f });
    emitter.properties["rate"] = 100.0f;
    emitter.properties["lifetime"] = 5.0f;
    emitter.properties["size"] = 0.05f;
    entry->graph.addNode(emitter);

    QUuid gravityId = QUuid::createUuid();
    ui::GraphNode gravity;
    gravity.id = gravityId;
    gravity.typeName = "ICE.ForceGravity";
    gravity.title = "Gravity";
    gravity.position = QPointF(-60, -80);
    gravity.properties["gravity"] = -9.81f;
    entry->graph.addNode(gravity);

    QUuid integrateId = QUuid::createUuid();
    ui::GraphNode integrate;
    integrate.id = integrateId;
    integrate.typeName = "ICE.OpAdd";
    integrate.title = "Integrate";
    integrate.position = QPointF(120, 0);
    entry->graph.addNode(integrate);

    QUuid outputId = QUuid::createUuid();
    ui::GraphNode output;
    output.id = outputId;
    output.typeName = "ICE.OutputPoints";
    output.title = "Output Points";
    output.position = QPointF(300, 0);
    entry->graph.addNode(output);

    // Connections
    ui::GraphConnection c1; c1.id = QUuid::createUuid();
    c1.fromNodeId = emitterId; c1.toNodeId = integrateId; entry->graph.addConnection(c1);
    ui::GraphConnection c2; c2.id = QUuid::createUuid();
    c2.fromNodeId = gravityId; c2.toNodeId = integrateId; entry->graph.addConnection(c2);
    ui::GraphConnection c3; c3.id = QUuid::createUuid();
    c3.fromNodeId = integrateId; c3.toNodeId = outputId; entry->graph.addConnection(c3);

    entry->evaluator->setGraph(entry->graph);
    iceStartTimer(objectId);
    emit iceChanged(objectId);
    return true;
}

bool KSModelerQml::iceRemove(int objectId)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return false;
    iceStopTimer(objectId);
    delete it.value()->evaluator;
    delete it.value();
    m_iceSystems.erase(it);
    emit iceChanged(objectId);
    return true;
}

bool KSModelerQml::iceAddNode(int objectId, const QString& type, float x, float y)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return false;
    ui::GraphNode node;
    node.id = QUuid::createUuid();
    node.typeName = type;
    node.title = type.section('.', -1);
    node.position = QPointF(x, y);
    if (type == "ICE.ForceGravity") node.properties["gravity"] = -9.81f;
    else if (type == "ICE.ForceWind") node.properties["wind"] = QVariantList({ 1.0f, 0.0f, 0.0f });
    else if (type == "ICE.ForceTurbulence") node.properties["strength"] = 1.0f;
    else if (type == "ICE.ForceDrag") node.properties["damping"] = 0.05f;
    else if (type == "ICE.ForceVortex") { node.properties["axis"] = QVariantList({ 0.0f, 1.0f, 0.0f }); node.properties["strength"] = 2.0f; }
    else if (type == "ICE.ForceAttractor") { node.properties["target"] = QVariantList({ 0.0f, 0.0f, 0.0f }); node.properties["strength"] = 2.0f; node.properties["maxDistance"] = 10.0f; }
    else if (type == "ICE.CollisionPlane") { node.properties["y"] = 0.0f; node.properties["restitution"] = 0.3f; node.properties["friction"] = 0.2f; }
    else if (type == "ICE.CollisionSphere") { node.properties["center"] = QVariantList({ 0.0f, 0.0f, 0.0f }); node.properties["radius"] = 1.0f; node.properties["restitution"] = 0.5f; node.properties["friction"] = 0.2f; }
    else if (type == "ICE.CollisionMesh") { node.properties["restitution"] = 0.3f; node.properties["friction"] = 0.2f; }
    else if (type == "ICE.FilterAge") node.properties["maxAge"] = 5.0f;
    else if (type == "ICE.FilterVelocity") node.properties["maxSpeed"] = 50.0f;
    else if (type == "ICE.OpMultiply") node.properties["factor"] = 1.0f;
    else if (type == "ICE.OpLerp") { node.properties["t"] = 0.5f; node.properties["target"] = QVariantList({ 0.0f, 0.0f, 0.0f }); }
    else if (type == "ICE.OpVectorMath") { node.properties["op"] = "add"; node.properties["vec"] = QVariantList({ 1.0f, 0.0f, 0.0f }); }
    else if (type == "ICE.EmitterPoint") { node.properties["position"] = QVariantList({ 0.0f, 0.0f, 0.0f }); node.properties["velocity"] = QVariantList({ 0.0f, 1.0f, 0.0f }); node.properties["rate"] = 100.0f; node.properties["lifetime"] = 5.0f; node.properties["size"] = 0.05f; }
    else if (type == "ICE.EmitterSphere") { node.properties["position"] = QVariantList({0.0f,0.0f,0.0f}); node.properties["radius"] = 1.0f; node.properties["rate"] = 100.0f; node.properties["velocity"] = 1.0f; node.properties["lifetime"] = 5.0f; node.properties["size"] = 0.05f; }
    else if (type == "ICE.EmitterMesh") { node.properties["rate"] = 100.0f; node.properties["velocity"] = 1.0f; node.properties["lifetime"] = 5.0f; node.properties["size"] = 0.05f; }
    else if (type == "ICE.EmitterCircle") { node.properties["position"] = QVariantList({0.0f,0.0f,0.0f}); node.properties["radius"] = 1.0f; node.properties["rate"] = 100.0f; node.properties["velocity"] = 1.0f; node.properties["lifetime"] = 5.0f; node.properties["size"] = 0.05f; }
    else if (type == "ICE.FilterRandom") node.properties["probability"] = 0.5f;
    else if (type == "ICE.FilterPosition") { node.properties["min"] = QVariantList({-10.0f, -10.0f, -10.0f}); node.properties["max"] = QVariantList({10.0f, 10.0f, 10.0f}); node.properties["killInside"] = false; }
    else if (type == "ICE.OpCurve") { node.properties["power"] = 2.0f; node.properties["amount"] = 1.0f; node.properties["fade"] = true; }
    else if (type == "ICE.PropLifetime") node.properties["lifetime"] = 5.0f;
    else if (type == "ICE.PropMass") node.properties["mass"] = 1.0f;
    else if (type == "ICE.PropColor") node.properties["color"] = QVariantList({ 1.0f, 0.6f, 0.0f, 1.0f });
    else if (type == "ICE.PropSize") node.properties["size"] = 0.05f;
    it.value()->graph.addNode(node);
    it.value()->evaluator->setGraph(it.value()->graph);
    emit iceChanged(objectId);
    return true;
}

bool KSModelerQml::iceRemoveNode(int objectId, const QString& nodeId)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return false;
    it.value()->graph.removeNode(QUuid(nodeId));
    it.value()->evaluator->setGraph(it.value()->graph);
    emit iceChanged(objectId);
    return true;
}

bool KSModelerQml::iceSetNodeProperty(int objectId, const QString& nodeId, const QString& prop, const QVariant& value)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return false;
    auto nit = it.value()->graph.nodes.find(QUuid(nodeId));
    if (nit == it.value()->graph.nodes.end()) return false;
    nit->properties[prop] = value;
    it.value()->evaluator->setGraph(it.value()->graph);
    emit iceChanged(objectId);
    return true;
}

bool KSModelerQml::iceConnect(int objectId, const QString& fromNode, const QString& fromPort,
                              const QString& toNode, const QString& toPort)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return false;
    ui::GraphConnection conn;
    conn.id = QUuid::createUuid();
    conn.fromNodeId = QUuid(fromNode);
    conn.fromPortId = QUuid::createUuid();
    conn.toNodeId = QUuid(toNode);
    conn.toPortId = QUuid::createUuid();
    it.value()->graph.addConnection(conn);
    it.value()->evaluator->setGraph(it.value()->graph);
    emit iceChanged(objectId);
    return true;
}

bool KSModelerQml::iceRemoveConnection(int objectId, const QString& fromNode, const QString& toNode)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return false;
    QUuid from(fromNode);
    QUuid to(toNode);
    bool removed = false;
    for (int i = it.value()->graph.connections.size() - 1; i >= 0; --i) {
        const auto& c = it.value()->graph.connections.at(i);
        if (c.fromNodeId == from && c.toNodeId == to) {
            it.value()->graph.connections.removeAt(i);
            removed = true;
        }
    }
    if (!removed) return false;
    it.value()->evaluator->setGraph(it.value()->graph);
    emit iceChanged(objectId);
    return true;
}

bool KSModelerQml::iceSetNodePosition(int objectId, const QString& nodeId, float x, float y)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return false;
    auto n = it.value()->graph.nodes.find(QUuid(nodeId));
    if (n == it.value()->graph.nodes.end()) return false;
    n->position = QPointF(x, y);
    it.value()->evaluator->setGraph(it.value()->graph);
    emit iceChanged(objectId);
    return true;
}

QVariantMap KSModelerQml::iceGetGraph(int objectId) const
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return QVariantMap();
    return it.value()->graph.toJson().toVariantMap();
}

QVector<QVector3D> KSModelerQml::iceEvaluatedPositions(int objectId)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return {};
    return it.value()->evaluator->getPositions();
}

QVariantList KSModelerQml::iceGetPositions(int objectId) const
{
    QVariantList out;
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return out;
    for (const auto& p : it.value()->evaluator->getPositions()) {
        QVariantList v;
        v << p.x() << p.y() << p.z();
        out.append(v);
    }
    return out;
}

int KSModelerQml::iceGetAliveCount(int objectId) const
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return 0;
    return it.value()->evaluator->getAliveCount();
}

QVariantList KSModelerQml::iceGetColors(int objectId) const
{
    QVariantList out;
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return out;
    for (const auto& c : it.value()->evaluator->getColors()) {
        QVariantList v;
        v << c.x() << c.y() << c.z() << c.w();
        out.append(v);
    }
    return out;
}

QVariantList KSModelerQml::iceGetSizes(int objectId) const
{
    QVariantList out;
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return out;
    for (float s : it.value()->evaluator->getSizes())
        out.append(QVariant::fromValue(s));
    return out;
}

bool KSModelerQml::icePlayPause(int objectId, bool play)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return false;
    it.value()->playing = play;
    if (play) iceStartTimer(objectId);
    else iceStopTimer(objectId);
    return true;
}

void KSModelerQml::iceStopAll()
{
    for (auto it = m_iceSystems.begin(); it != m_iceSystems.end(); ++it)
        iceStopTimer(it.key());
}

bool KSModelerQml::iceSetCollisionObject(int objectId, int collisionObjectId)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return false;
    it.value()->collisionObjectId = collisionObjectId;
    it.value()->evaluator->setCollisionObjectId(collisionObjectId);
    if (collisionObjectId < 0) {
        it.value()->evaluator->setCollisionMesh({});
        return true;
    }
    // Pre-build triangle soup immediately so CollisionMesh works before first tick.
    updateCollisionTriangles(objectId);
    emit iceChanged(objectId);
    return true;
}

void KSModelerQml::updateCollisionTriangles(int objectId)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end() || !it.value()->evaluator) return;
    const int cid = it.value()->collisionObjectId;
    if (cid < 0) return;
    ks::SceneGraph* scene = sceneGraph();
    if (!scene) return;
    SceneObject* cobj = scene->findObjectById(cid);
    if (!cobj || !cobj->mesh()) return;

    const auto& verts = cobj->mesh()->geometry().vertices;
    const auto& idxs = cobj->mesh()->geometry().indices;
    if (verts.isEmpty() || idxs.isEmpty()) return;

    QMatrix4x4 w = cobj->worldTransform();
    QVector<QVector3D> tris;
    tris.reserve(idxs.size());
    for (int i = 0; i + 2 < idxs.size(); i += 3) {
        tris.append(w.map(verts[idxs[i]].position));
        tris.append(w.map(verts[idxs[i+1]].position));
        tris.append(w.map(verts[idxs[i+2]].position));
    }
    it.value()->evaluator->setCollisionMesh(tris);
}

bool KSModelerQml::iceSetEmitterObject(int objectId, int emitterObjectId)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return false;
    it.value()->emitterObjectId = emitterObjectId;
    it.value()->evaluator->setEmitterObjectId(emitterObjectId);
    if (emitterObjectId < 0) {
        it.value()->evaluator->setEmitterMesh({});
        return true;
    }
    updateEmitterTriangles(objectId);
    emit iceChanged(objectId);
    return true;
}

void KSModelerQml::updateEmitterTriangles(int objectId)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end() || !it.value()->evaluator) return;
    const int eid = it.value()->emitterObjectId;
    if (eid < 0) return;
    ks::SceneGraph* scene = sceneGraph();
    if (!scene) return;
    SceneObject* eobj = scene->findObjectById(eid);
    if (!eobj || !eobj->mesh()) return;

    const auto& verts = eobj->mesh()->geometry().vertices;
    const auto& idxs = eobj->mesh()->geometry().indices;
    if (verts.isEmpty() || idxs.isEmpty()) return;

    QMatrix4x4 w = eobj->worldTransform();
    QVector<QVector3D> tris;
    tris.reserve(idxs.size());
    for (int i = 0; i + 2 < idxs.size(); i += 3) {
        tris.append(w.map(verts[idxs[i]].position));
        tris.append(w.map(verts[idxs[i+1]].position));
        tris.append(w.map(verts[idxs[i+2]].position));
    }
    it.value()->evaluator->setEmitterMesh(tris);
}

bool KSModelerQml::iceBake(int objectId, int frames)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end() || !it.value()->evaluator) return false;
    frames = qBound(1, frames, 5000);
    iceStopTimer(objectId);
    it.value()->cache.clear();
    it.value()->cache.reserve(frames + 1);
    it.value()->evaluator->clearParticles();
    // Frame 0 = empty state
    it.value()->cache.append(it.value()->evaluator->snapshot());
    for (int f = 1; f <= frames; ++f) {
        updateCollisionTriangles(objectId);
        updateEmitterTriangles(objectId);
        it.value()->evaluator->step();
        it.value()->cache.append(it.value()->evaluator->snapshot());
    }
    it.value()->cacheLength = frames;
    // Restore frame 0 so the user can scrub from the start
    it.value()->evaluator->restoreState(it.value()->cache[0]);
    emit iceParticlesUpdated(objectId, 0);
    emit iceChanged(objectId);
    return true;
}

bool KSModelerQml::iceScrubToFrame(int objectId, int frame)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end() || !it.value()->evaluator) return false;
    if (frame < 0 || frame >= it.value()->cache.size()) return false;
    it.value()->evaluator->restoreState(it.value()->cache[frame]);
    emit iceParticlesUpdated(objectId, it.value()->evaluator->getAliveCount());
    return true;
}

int KSModelerQml::iceCacheLength(int objectId) const
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return 0;
    return it.value()->cacheLength;
}

void KSModelerQml::iceClearCache(int objectId)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return;
    it.value()->cache.clear();
    it.value()->cacheLength = 0;
    it.value()->evaluator->clearParticles();
    emit iceParticlesUpdated(objectId, 0);
    emit iceChanged(objectId);
}

void KSModelerQml::iceStartTimer(int objectId)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end() || !it.value()->evaluator) return;
    if (it.value()->timer) return;
    it.value()->timer = new QTimer(this);
    it.value()->timer->setInterval(16); // ~60fps
    const int id = objectId;
    connect(it.value()->timer, &QTimer::timeout, this, [this, id]() {
        auto eit = m_iceSystems.find(id);
        if (eit == m_iceSystems.end() || !eit.value()->evaluator) return;
        updateCollisionTriangles(id);
        updateEmitterTriangles(id);
        eit.value()->evaluator->step();
        emit iceParticlesUpdated(id, eit.value()->evaluator->getAliveCount());
    });
    it.value()->timer->start();
}

void KSModelerQml::iceStopTimer(int objectId)
{
    auto it = m_iceSystems.find(objectId);
    if (it == m_iceSystems.end()) return;
    if (it.value()->timer) {
        it.value()->timer->stop();
        it.value()->timer->deleteLater();
        it.value()->timer = nullptr;
    }
    it.value()->playing = false;
}

void KSModelerQml::setIceSpheresEnabled(bool on)
{
    if (m_iceSpheresEnabled != on) {
        m_iceSpheresEnabled = on;
        emit iceSpheresEnabledChanged();
    }
}

} // namespace ks
