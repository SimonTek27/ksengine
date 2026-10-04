#include "MeshDataBridge.h"
#include "AdvancedMeshOps.h"
#include <QDebug>
#include <QVector>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QQuaternion>

namespace ks {

namespace MeshOpsQml {

QVector3D variantToVector3D(const QVariant& v)
{
    QJsonArray a = QJsonDocument::fromJson(v.toString().toUtf8()).array();
    if (a.size() >= 3)
        return QVector3D((float)a[0].toDouble(), (float)a[1].toDouble(), (float)a[2].toDouble());
    return QVector3D();
}

QVariant vector3DToVariant(const QVector3D& v)
{
    QJsonArray a;
    a.append(v.x()); a.append(v.y()); a.append(v.z());
    return QVariant(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

MeshData variantToMesh(const QVariant& v)
{
    MeshData mesh;
    QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
    QJsonObject obj = doc.object();
    QJsonArray verts = obj["vertices"].toArray();
    for (const auto& vv : verts) {
        QJsonArray a = vv.toArray();
        Vertex vert;
        if (a.size() >= 3)
            vert.position = QVector3D((float)a[0].toDouble(), (float)a[1].toDouble(), (float)a[2].toDouble());
        mesh.vertices.append(vert);
    }
    QJsonArray faces = obj["faces"].toArray();
    for (const auto& fv : faces) {
        QJsonArray a = fv.toArray();
        Face face;
        for (const auto& idx : a)
            face.indices.append(idx.toInt());
        mesh.faces.append(face);
    }
    return mesh;
}

QVariant meshToVariant(const MeshData& mesh)
{
    QJsonObject obj;
    QJsonArray verts;
    for (const auto& v : mesh.vertices) {
        QJsonArray a;
        a.append(v.position.x()); a.append(v.position.y()); a.append(v.position.z());
        verts.append(a);
    }
    obj["vertices"] = verts;
    QJsonArray faces;
    for (const auto& f : mesh.faces) {
        QJsonArray a;
        for (int idx : f.indices)
            a.append(idx);
        faces.append(a);
    }
    obj["faces"] = faces;
    return QVariant(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace MeshOpsQml



static MeshDataBridge* s_meshDataBridge = nullptr;

MeshDataBridge::MeshDataBridge(QObject* parent)
    : QObject(parent)
{
}

MeshDataBridge::~MeshDataBridge() {
    s_meshDataBridge = nullptr;
}

MeshDataBridge* MeshDataBridge::instance() {
    if (!s_meshDataBridge) {
        s_meshDataBridge = new MeshDataBridge();
    }
    return s_meshDataBridge;
}

void MeshDataBridge::setCurrentMesh(const QVariant& meshData) {
    m_currentMesh = meshData;
    m_meshData = MeshOpsQml::variantToMesh(meshData);
    qDebug() << "[MeshDataBridge] Mesh set:" << m_meshData.vertices.size() << "verts," << m_meshData.faces.size() << "faces";
    emit meshModified(m_currentMeshName);
}

void MeshDataBridge::updateFromViewport(const QString& meshName) {
    m_currentMeshName = meshName;
    qDebug() << "[MeshDataBridge] Updated from viewport:" << meshName;
}

void MeshDataBridge::pushToViewport(const QString& meshName) {
    m_currentMeshName = meshName;
    m_currentMesh = MeshOpsQml::meshToVariant(m_meshData);
    emit meshModified(meshName);
    qDebug() << "[MeshDataBridge] Pushed to viewport:" << meshName;
}

QVariant MeshDataBridge::loopCut(int cuts, const QVariant& center, const QVariant& normal) {
    QVector3D c = MeshOpsQml::variantToVector3D(center);
    QVector3D n = MeshOpsQml::variantToVector3D(normal);

    MeshData result = LoopCut::cut(m_meshData, cuts, c, n);
    m_meshData = result;

    QVariantMap map;
    map["mesh"] = MeshOpsQml::meshToVariant(result);
    map["vertexCount"] = result.vertices.size();
    map["faceCount"] = result.faces.size();

    emitMeshModified();
    return map;
}

QVariant MeshDataBridge::knifeCut(const QVariant& start, const QVariant& end, bool snapToVertex) {
    QVector3D s = MeshOpsQml::variantToVector3D(start);
    QVector3D e = MeshOpsQml::variantToVector3D(end);

    MeshData result = KnifeTool::cut(m_meshData, s, e, snapToVertex);
    m_meshData = result;

    QVariantMap map;
    map["mesh"] = MeshOpsQml::meshToVariant(result);
    map["vertexCount"] = result.vertices.size();
    map["faceCount"] = result.faces.size();

    emitMeshModified();
    return map;
}

QVariant MeshDataBridge::bisectCut(const QVariant& planePoint, const QVariant& planeNormal, bool cutCenter) {
    QVector3D p = MeshOpsQml::variantToVector3D(planePoint);
    QVector3D n = MeshOpsQml::variantToVector3D(planeNormal);

    MeshData result = Bisect::cut(m_meshData, p, n, cutCenter);
    m_meshData = result;

    QVariantMap map;
    map["mesh"] = MeshOpsQml::meshToVariant(result);
    map["vertexCount"] = result.vertices.size();
    map["faceCount"] = result.faces.size();

    emitMeshModified();
    return map;
}

QVariant MeshDataBridge::subdivide(int levels) {
    MeshData result = MeshOperations::subdivide(m_meshData, levels);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

QVariant MeshDataBridge::triangulate() {
    MeshData result = MeshOperations::triangulate(m_meshData);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

QVariant MeshDataBridge::quadrangulate() {
    MeshData result = MeshOperations::quadrangulate(m_meshData);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

QVariant MeshDataBridge::extrude(float distance, bool individual) {
    QVector3D dir(0, 0, 1);
    MeshData result = MeshOperations::extrude(m_meshData, dir, distance, individual);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

QVariant MeshDataBridge::bevel(float distance, int segments) {
    MeshData result = MeshOperations::bevelEdges(m_meshData, distance, segments);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

QVariant MeshDataBridge::inset(float distance) {
    MeshData result = MeshOperations::insetFaces(m_meshData, distance);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

QVariant MeshDataBridge::decimate(float targetRatio) {
    MeshData result = Decimation::simplify(m_meshData, targetRatio);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

QVariant MeshDataBridge::triRemesh() {
    MeshData result = Remeshing::triRemesh(m_meshData);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

QVariant MeshDataBridge::quadRemesh(int targetCount) {
    MeshData result = Remeshing::quadRemesh(m_meshData, targetCount);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

QVariant MeshDataBridge::fillHoles(int maxSize) {
    MeshData result = PolygonOperations::fillHoles(m_meshData, maxSize);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

QVariant MeshDataBridge::planarFaces(float threshold) {
    MeshData result = PolygonOperations::planarFaces(m_meshData, threshold);
    m_meshData = result;

    emitMeshModified();
    return MeshOpsQml::meshToVariant(result);
}

void MeshDataBridge::translate(const QVariant& delta) {
    QVector3D d = MeshOpsQml::variantToVector3D(delta);

    for (Vertex& v : m_meshData.vertices) {
        v.position += d;
    }

    emitMeshModified();
}

void MeshDataBridge::rotate(const QVariant& euler) {
    QVector3D e = MeshOpsQml::variantToVector3D(euler);

    QMatrix4x4 mat;
    mat.setToIdentity();
    mat.rotate(QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, e.x()));
    mat.rotate(QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, e.y()));
    mat.rotate(QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, e.z()));

    for (Vertex& v : m_meshData.vertices) {
        v.position = mat.map(v.position);
    }

    emitMeshModified();
}

void MeshDataBridge::scale(const QVariant& factors) {
    QVector3D f = MeshOpsQml::variantToVector3D(factors);

    for (Vertex& v : m_meshData.vertices) {
        v.position.setX(v.position.x() * f.x());
        v.position.setY(v.position.y() * f.y());
        v.position.setZ(v.position.z() * f.z());
    }

    emitMeshModified();
}

void MeshDataBridge::duplicate() {
    MeshData copy = m_meshData;

    for (Vertex& v : copy.vertices) {
        v.position += QVector3D(1, 0, 0);
    }

    m_meshData = copy;
    emitMeshModified();
}

void MeshDataBridge::deleteSelected() {
    if (m_selectedVertices.isEmpty() && m_selectedFaces.isEmpty()) {
        return;
    }

    QVector<int> toKeep;
    QSet<int> selectedSet(m_selectedVertices.begin(), m_selectedVertices.end());

    for (int i = 0; i < m_meshData.vertices.size(); ++i) {
        if (!selectedSet.contains(i)) {
            toKeep.append(i);
        }
    }

    MeshData result;
    // Build a new index map: old vertex index → new vertex index (-1 = deleted)
    QVector<int> vertMap(m_meshData.vertices.size(), -1);
    int newIdx = 0;
    for (int idx : toKeep) {
        result.vertices.append(m_meshData.vertices[idx]);
        vertMap[idx] = newIdx++;
    }

    // Remap faces — drop any face that references a deleted vertex
    for (const Face& face : m_meshData.faces) {
        Face newFace;
        bool valid = true;
        for (int idx : face.indices) {
            int mapped = (idx >= 0 && idx < vertMap.size()) ? vertMap[idx] : -1;
            if (mapped < 0) { valid = false; break; }
            newFace.indices.append(mapped);
        }
        if (valid && newFace.indices.size() >= 3)
            result.faces.append(newFace);
    }

    m_meshData = result;
    m_selectedVertices.clear();
    emitMeshModified();
    emit selectionChanged();
}

void MeshDataBridge::mirror(const QVariant& axis) {
    QVector3D a = MeshOpsQml::variantToVector3D(axis);
    a.normalize();

    for (Vertex& v : m_meshData.vertices) {
        float dot = QVector3D::dotProduct(v.position, a);
        v.position = v.position - 2 * dot * a;
    }

    emitMeshModified();
}

QVariant MeshDataBridge::getSelectionBounds() {
    if (m_meshData.vertices.isEmpty()) {
        return QVariant();
    }

    QVector3D min(1e9, 1e9, 1e9);
    QVector3D max(-1e9, -1e9, -1e9);

    for (const Vertex& v : m_meshData.vertices) {
        min.setX(qMin(min.x(), v.position.x()));
        min.setY(qMin(min.y(), v.position.y()));
        min.setZ(qMin(min.z(), v.position.z()));
        max.setX(qMax(max.x(), v.position.x()));
        max.setY(qMax(max.y(), v.position.y()));
        max.setZ(qMax(max.z(), v.position.z()));
    }

    QVariantMap bounds;
    bounds["min"] = MeshOpsQml::vector3DToVariant(min);
    bounds["max"] = MeshOpsQml::vector3DToVariant(max);
    bounds["center"] = MeshOpsQml::vector3DToVariant((min + max) / 2);
    bounds["size"] = MeshOpsQml::vector3DToVariant(max - min);

    return bounds;
}

QVariant MeshDataBridge::getVertexPositions() {
    QVariantList positions;
    for (const Vertex& v : m_meshData.vertices) {
        positions.append(MeshOpsQml::vector3DToVariant(v.position));
    }
    return positions;
}

QVariant MeshDataBridge::getFacePositions() {
    QVariantList faces;
    for (const Face& f : m_meshData.faces) {
        QVariantList face;
        for (int idx : f.indices) {
            if (idx < m_meshData.vertices.size()) {
                face.append(MeshOpsQml::vector3DToVariant(m_meshData.vertices[idx].position));
            }
        }
        faces.append(face);
    }
    return faces;
}

QVariant MeshDataBridge::getNormals() {
    m_meshData.computeNormals();
    QVariantList normals;
    for (const Vertex& v : m_meshData.vertices) {
        normals.append(MeshOpsQml::vector3DToVariant(v.normal));
    }
    return normals;
}

int MeshDataBridge::getVertexCount() {
    return m_meshData.vertices.size();
}

int MeshDataBridge::getFaceCount() {
    return m_meshData.faces.size();
}

int MeshDataBridge::getEdgeCount() {
    m_meshData.computeNormals();
    return m_meshData.edges.size();
}

void MeshDataBridge::updateFromMeshData() {
    m_currentMesh = MeshOpsQml::meshToVariant(m_meshData);
}

void MeshDataBridge::updateMeshDataFromVariant() {
    m_meshData = MeshOpsQml::variantToMesh(m_currentMesh);
}

void MeshDataBridge::emitMeshModified() {
    updateFromMeshData();
    emit meshModified(m_currentMeshName);
    emit boundsChanged(getSelectionBounds());
}

}