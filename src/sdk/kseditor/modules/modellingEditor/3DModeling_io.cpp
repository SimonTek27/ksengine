#include "3DModeling_io.h"
#include "3DModeling.h"
#include "sdk/kseditor/engine/FileFormat/KS3DReader.h"
#include "sdk/kseditor/engine/FileFormat/KS3DWriter.h"
#include "sdk/kseditor/engine/FileFormat/Rhino3dmParser.h"
#include <QDebug>
#include <QFile>
#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTextStream>
#include <QRandomGenerator>
#include <QTimer>
#include <QUuid>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <unordered_map>
#include "sdk/kseditor/engine/FileFormat/FBXParser.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/acFiles/FBXExporter.h"
#if HAS_QT3D
#include <Qt3DRender/qgeometryrenderer.h>
#include <Qt3DCore/qattribute.h>
#include <Qt3DCore/qbuffer.h>
#else
namespace Qt3DCore { class QEntity; }
#endif

namespace ks {

// ============================================================================
// ImportExport3D Implementation
// ============================================================================

namespace io {

graphics::Mesh3D* ImportExport3D::importMesh(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists()) return nullptr;

    QString ext = info.suffix().toLower();
    auto* scene = new graphics::Scene3D();
    bool ok = false;

    if (ext == "obj") ok = importOBJ(path, scene);
    else if (ext == "stl") ok = importSTL(path, scene);
    else if (ext == "gltf" || ext == "glb") ok = importGLTF(path, scene);
    else if (ext == "fbx") ok = importFBX(path, scene);
    else if (ext == "ks3d") ok = importKS3D(path, scene);

    if (ok && !scene->allObjects().isEmpty()) {
        auto* mesh = scene->allObjects().first()->mesh;
        if (mesh) {
            mesh->setParent(nullptr);
            scene->allObjects().first()->mesh = nullptr;
        }
        delete scene;
        return mesh;
    }
    delete scene;
    return nullptr;
}

bool ImportExport3D::exportMesh(graphics::Mesh3D* mesh, const QString& path, Format format)
{
    if (!mesh) return false;

    auto* scene = new graphics::Scene3D();
    scene->addObject("Mesh", mesh);

    bool ok = false;
    switch (format) {
    case OBJ:  ok = exportOBJ(scene, path); break;
    case STL:  ok = exportSTL(scene, path); break;
    case GLTF: ok = exportGLTF(scene, path); break;
    case KS3D: ok = exportKS3D(scene, path); break;
    default:
        qWarning() << "[ImportExport3D] Unsupported export format:" << format;
        break;
    }

    // Don't delete mesh since it was added to scene
    delete scene;
    return ok;
}

graphics::Scene3D* ImportExport3D::importScene(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists()) {
        qWarning() << "[ImportExport3D] File not found:" << path;
        return nullptr;
    }

    auto* scene = new graphics::Scene3D();
    QString ext = info.suffix().toLower();
    bool ok = false;

    if (ext == "obj") ok = importOBJ(path, scene);
    else if (ext == "stl") ok = importSTL(path, scene);
    else if (ext == "gltf" || ext == "glb") ok = importGLTF(path, scene);
    else if (ext == "fbx") ok = importFBX(path, scene);
    else if (ext == "ks3d") ok = importKS3D(path, scene);
    else qWarning() << "[ImportExport3D] Unsupported format for scene import:" << ext;

    if (!ok) {
        delete scene;
        return nullptr;
    }

    qDebug() << "Importing scene from:" << path;
    return scene;
}

bool ImportExport3D::exportScene(graphics::Scene3D* scene, const QString& path, Format format)
{
    if (!scene) return false;

    switch (format) {
    case OBJ:  return exportOBJ(scene, path);
    case STL:  return exportSTL(scene, path);
    case GLTF: return exportGLTF(scene, path);
    case KS3D: return exportKS3D(scene, path);
    default:
        qWarning() << "[ImportExport3D] Unsupported export format:" << format;
        return false;
    }
}

ImportExport3D::ImportResult ImportExport3D::import(const QString& path)
{
    ImportResult result;
    QFileInfo info(path);
    if (!info.exists()) {
        result.success = false;
        result.error = "File not found: " + path;
        return result;
    }

    QString ext = info.suffix().toLower();
    graphics::Scene3D* scene = new graphics::Scene3D();

    bool ok = false;
    if (ext == "obj") ok = importOBJ(path, scene);
    else if (ext == "stl") ok = importSTL(path, scene);
    else if (ext == "gltf" || ext == "glb") ok = importGLTF(path, scene);
    else if (ext == "fbx") ok = importFBX(path, scene);
    else if (ext == "ks3d") ok = importKS3D(path, scene);
    else {
        result.success = false;
        result.error = "Unsupported format: " + ext;
        delete scene;
        return result;
    }

    if (ok) {
        result.success = true;
        result.scene = scene;
        emit importComplete(true);
    } else {
        result.success = false;
        result.error = "Failed to import " + ext + " file";
        delete scene;
        emit importComplete(false);
    }
    return result;
}

bool ImportExport3D::importOBJ(const QString& path, graphics::Scene3D* scene)
{
    if (!scene || !QFileInfo(path).exists()) return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[ImportExport3D] Cannot open OBJ file:" << path;
        return false;
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> uvs;
    QVector<QVector<int>> faceIndices;
    QVector<QVector<int>> faceUVIndices;
    QVector<QVector<int>> faceNormalIndices;
    int vertexOffset = 0;

    graphics::Mesh3D* currentMesh = new graphics::Mesh3D();
    QString currentMaterial;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        if (parts[0] == "v" && parts.size() >= 4) {
            positions.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
        } else if (parts[0] == "vn" && parts.size() >= 4) {
            normals.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
        } else if (parts[0] == "vt" && parts.size() >= 3) {
            uvs.append(QVector2D(parts[1].toFloat(), parts[2].toFloat()));
        } else if (parts[0] == "f") {
            QVector<int> fi, fui, fni;
            for (int i = 1; i < parts.size(); ++i) {
                QStringList verts = parts[i].split('/');
                fi.append(verts[0].toInt() - 1);
                if (verts.size() > 1 && !verts[1].isEmpty())
                    fui.append(verts[1].toInt() - 1);
                if (verts.size() > 2 && !verts[2].isEmpty())
                    fni.append(verts[2].toInt() - 1);
            }
            // Triangulate n-gons
            for (int i = 1; i + 1 < fi.size(); ++i) {
                faceIndices.append({fi[0], fi[i], fi[i + 1]});
                if (!fui.isEmpty())
                    faceUVIndices.append({fui[0], fui[i], fui[i + 1]});
                if (!fni.isEmpty())
                    faceNormalIndices.append({fni[0], fni[i], fni[i + 1]});
            }
        } else if (parts[0] == "o" || parts[0] == "g") {
            // Flush current mesh if it has data
            if (!currentMesh->indices().isEmpty()) {
                currentMesh->setVertices(positions);
                currentMesh->setNormals(normals);
                currentMesh->setUVs(uvs);
                scene->addObject(parts.size() > 1 ? parts[1] : "Object", currentMesh);
                currentMesh = new graphics::Mesh3D();
            }
        } else if (parts[0] == "usemtl") {
            currentMaterial = parts.size() > 1 ? parts[1] : "";
        }
    }

    // Build final index arrays
    QVector<quint32> finalIndices;
    QVector<QVector3D> finalNormals;
    QVector<QVector2D> finalUVs;
    QVector<QVector3D> finalPositions;

    for (int i = 0; i < faceIndices.size(); ++i) {
        for (int j = 0; j < faceIndices[i].size(); ++j) {
            int pi = faceIndices[i][j];
            if (pi >= 0 && pi < positions.size())
                finalPositions.append(positions[pi]);

            int ni = (i < faceNormalIndices.size() && j < faceNormalIndices[i].size()) ?
                     faceNormalIndices[i][j] : -1;
            if (ni >= 0 && ni < normals.size())
                finalNormals.append(normals[ni]);
            else
                finalNormals.append(QVector3D(0, 1, 0));

            int uvi = (i < faceUVIndices.size() && j < faceUVIndices[i].size()) ?
                      faceUVIndices[i][j] : -1;
            if (uvi >= 0 && uvi < uvs.size())
                finalUVs.append(uvs[uvi]);
            else
                finalUVs.append(QVector2D(0, 0));

            finalIndices.append(finalPositions.size() - 1);
        }
    }

    currentMesh->setVertices(finalPositions);
    currentMesh->setNormals(finalNormals);
    currentMesh->setUVs(finalUVs);
    currentMesh->setIndices(finalIndices);
    if (!currentMaterial.isEmpty())
        currentMesh->setMaterialId(currentMaterial);

    scene->addObject(QFileInfo(path).baseName(), currentMesh);

    qDebug() << "[ImportExport3D] Imported OBJ:" << finalPositions.size() << "vertices,"
             << finalIndices.size() / 3 << "triangles";
    emit importProgress(100);
    return true;
}

bool ImportExport3D::importGLTF(const QString& path, graphics::Scene3D* scene)
{
    if (!scene || !QFileInfo(path).exists()) return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[ImportExport3D] Cannot open GLTF file:" << path;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc;
    if (path.endsWith(".glb", Qt::CaseInsensitive)) {
        // GLB binary format: 12-byte header + JSON chunk + BIN chunk
        if (data.size() < 12) return false;
        quint32 magic = *reinterpret_cast<const quint32*>(data.constData());
        if (magic != 0x46546C67) return false; // "glTF"
        quint32 version = *reinterpret_cast<const quint32*>(data.constData() + 4);
        if (version != 2) return false; // only GLB v2 supported
        quint32 length = *reinterpret_cast<const quint32*>(data.constData() + 8);
        if (length != (quint32)data.size()) return false;
        // Skip header, find first chunk (JSON)
        quint32 offset = 12;
        while (offset + 8 < (quint32)data.size()) {
            quint32 chunkLen = *reinterpret_cast<const quint32*>(data.constData() + offset);
            quint32 chunkType = *reinterpret_cast<const quint32*>(data.constData() + offset + 4);
            if (chunkType == 0x4E4F534A) { // JSON chunk
                doc = QJsonDocument::fromJson(data.mid(offset + 8, chunkLen));
                break;
            }
            offset += 8 + chunkLen;
        }
    } else {
        doc = QJsonDocument::fromJson(data);
    }

    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "[ImportExport3D] Invalid GLTF JSON";
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray accessors = root["accessors"].toArray();
    QJsonArray bufferViews = root["bufferViews"].toArray();
    QJsonArray buffers = root["buffers"].toArray();
    QJsonArray meshes = root["meshes"].toArray();

    // Read binary buffer data
    QByteArray binData;
    if (path.endsWith(".glb", Qt::CaseInsensitive)) {
        quint32 offset = 12;
        while (offset + 8 < (quint32)data.size()) {
            quint32 chunkLen = *reinterpret_cast<const quint32*>(data.constData() + offset);
            quint32 chunkType = *reinterpret_cast<const quint32*>(data.constData() + offset + 4);
            if (chunkType == 0x004E4942) { // BIN chunk
                binData = data.mid(offset + 8, chunkLen);
                break;
            }
            offset += 8 + chunkLen;
        }
    } else {
        for (const auto& buf : buffers) {
            QJsonObject bufObj = buf.toObject();
            QString uri = bufObj["uri"].toString();
            if (!uri.isEmpty()) {
                QFileInfo fi(path);
                QFile binFile(fi.absolutePath() + "/" + uri);
                if (binFile.open(QIODevice::ReadOnly)) {
                    binData = binFile.readAll();
                    binFile.close();
                }
            }
        }
    }

    // Parse meshes and primitives
    for (const auto& meshVal : meshes) {
        QJsonObject meshObj = meshVal.toObject();
        auto* mesh3d = new graphics::Mesh3D();
        QJsonArray primitives = meshObj["primitives"].toArray();

        for (const auto& primVal : primitives) {
            QJsonObject prim = primVal.toObject();
            QJsonObject attributes = prim["attributes"].toObject();

            int posAccIdx = attributes["POSITION"].toInt(-1);
            int normAccIdx = attributes["NORMAL"].toInt(-1);
            int uvAccIdx = attributes["TEXCOORD_0"].toInt(-1);
            int idxAccIdx = prim["indices"].toInt(-1);

            auto readAccessor = [&](int accIdx, int componentCount) -> QVector<float> {
                if (accIdx < 0 || accIdx >= accessors.size()) return {};
                QJsonObject acc = accessors[accIdx].toObject();
                int bvIdx = acc["bufferView"].toInt(-1);
                if (bvIdx < 0 || bvIdx >= bufferViews.size()) return {};
                QJsonObject bv = bufferViews[bvIdx].toObject();
                int byteOffset = bv["byteOffset"].toInt(0);
                int byteLength = bv["byteLength"].toInt(0);
                int count = acc["count"].toInt(0);
                QByteArray chunk = binData.mid(byteOffset, byteLength);
                QVector<float> result;
                const float* floats = reinterpret_cast<const float*>(chunk.constData());
                for (int i = 0; i < count * componentCount && i < chunk.size() / 4; ++i)
                    result.append(floats[i]);
                return result;
            };

            auto readIndices = [&](int accIdx) -> QVector<quint32> {
                if (accIdx < 0 || accIdx >= accessors.size()) return {};
                QJsonObject acc = accessors[accIdx].toObject();
                int bvIdx = acc["bufferView"].toInt(-1);
                int count = acc["count"].toInt(0);
                if (bvIdx < 0 || bvIdx >= bufferViews.size()) return {};
                QJsonObject bv = bufferViews[bvIdx].toObject();
                int byteOffset = bv["byteOffset"].toInt(0);
                int byteLength = bv["byteLength"].toInt(0);
                QByteArray chunk = binData.mid(byteOffset, byteLength);
                QVector<quint32> result;
                const quint16* indices16 = reinterpret_cast<const quint16*>(chunk.constData());
                const quint32* indices32 = reinterpret_cast<const quint32*>(chunk.constData());
                for (int i = 0; i < count; ++i) {
                    if (byteLength >= (i + 1) * 4)
                        result.append(indices32[i]);
                    else if (byteLength >= (i + 1) * 2)
                        result.append(indices16[i]);
                }
                return result;
            };

            QVector<float> posData = readAccessor(posAccIdx, 3);
            QVector<float> normData = readAccessor(normAccIdx, 3);
            QVector<float> uvData = readAccessor(uvAccIdx, 2);
            QVector<quint32> idxData = readIndices(idxAccIdx);

            if (posData.isEmpty() || idxData.isEmpty()) continue;

            QVector<QVector3D> verts;
            for (int i = 0; i + 2 < posData.size(); i += 3)
                verts.append(QVector3D(posData[i], posData[i + 1], posData[i + 2]));

            QVector<QVector3D> norms;
            for (int i = 0; i + 2 < normData.size(); i += 3)
                norms.append(QVector3D(normData[i], normData[i + 1], normData[i + 2]));

            QVector<QVector2D> uvs;
            for (int i = 0; i + 1 < uvData.size(); i += 2)
                uvs.append(QVector2D(uvData[i], uvData[i + 1]));

            mesh3d->setVertices(verts);
            if (!norms.isEmpty()) mesh3d->setNormals(norms);
            if (!uvs.isEmpty()) mesh3d->setUVs(uvs);
            mesh3d->setIndices(idxData);
        }

        scene->addObject(meshObj["name"].toString(), mesh3d);
    }

    if (scene->allObjects().isEmpty()) return false;

    qDebug() << "[ImportExport3D] Imported GLTF:" << scene->allObjects().size() << "meshes";
    emit importProgress(100);
    return true;
}

bool ImportExport3D::importFBX(const QString& path, graphics::Scene3D* scene)
{
    if (!scene || !QFileInfo(path).exists()) return false;

    FBXParser parser;
    bool parsed = parser.loadFromFile(path.toStdString());

    // If parser returned no meshes, try the AC plugin's FBXImporter as fallback
    if (parsed && parser.scene().meshes.empty()) {
        MeshData fallbackMesh;
        auto settings = ks::FBXImporter::getDefaultImportSettings();
        settings.importMaterials = true;
        settings.importTextures = false;
        settings.importAnimations = false;
        settings.importSkinning = false;
        if (ks::FBXImporter::importFromFBX(path, fallbackMesh, settings) && !fallbackMesh.vertices.isEmpty()) {
            auto* mesh3d = new graphics::Mesh3D(scene);
            QVector<QVector3D> verts;
            QVector<QVector3D> norms;
            QVector<QVector2D> uvs;
            QVector<quint32> idxs;
            verts.reserve(fallbackMesh.vertices.size());
            for (const auto& v : fallbackMesh.vertices)
                verts.append(v.position);
            norms.reserve(fallbackMesh.normals.size());
            for (const auto& n : fallbackMesh.normals)
                norms.append(n);
            uvs.reserve(fallbackMesh.uvs.size());
            for (const auto& uv : fallbackMesh.uvs)
                uvs.append(uv);
            for (const auto& f : fallbackMesh.faces)
                for (int idx : f.indices)
                    idxs.append(static_cast<quint32>(idx));
            mesh3d->setVertices(verts);
            mesh3d->setNormals(norms);
            mesh3d->setUVs(uvs);
            mesh3d->setIndices(idxs);
            scene->addObject(QFileInfo(path).baseName(), mesh3d);
            qDebug() << "[ImportExport3D] Imported FBX via fallback importer:" << verts.size() << "vertices";
            emit importProgress(100);
            return true;
        }
    }

    if (!parsed) {
        qWarning() << "[ImportExport3D] Failed to parse FBX:" << path;
        return false;
    }

    const auto& fbxScene = parser.scene();
    const auto& fbxMaterials = fbxScene.materials;
    const auto& fbxMeshes = fbxScene.meshes;

    QMap<QString, graphics::Material3D*> materialMap;

    for (const auto& [name, mat] : fbxMaterials) {
        auto* mat3d = new graphics::Material3D(scene);
        mat3d->setName(QString::fromStdString(mat.name));
        mat3d->setDiffuse(QVector3D(mat.diffuseColor.x, mat.diffuseColor.y, mat.diffuseColor.z));
        mat3d->setSpecular(QVector3D(mat.specularColor.x, mat.specularColor.y, mat.specularColor.z));
        mat3d->setAmbient(QVector3D(mat.ambientColor.x, mat.ambientColor.y, mat.ambientColor.z));
        mat3d->setOpacity(mat.opacity);

        if (!mat.diffuseTexture.empty())
            mat3d->setTexture("diffuse", QString::fromStdString(mat.diffuseTexture));
        if (!mat.normalTexture.empty())
            mat3d->setTexture("normal", QString::fromStdString(mat.normalTexture));

        materialMap[QString::fromStdString(name)] = mat3d;
    }

    for (const auto& fbxMesh : fbxMeshes) {
        auto* mesh3d = new graphics::Mesh3D(scene);

        QVector<QVector3D> verts;
        QVector<QVector3D> norms;
        QVector<QVector2D> uvs;
        QVector<quint32> idxs;

        verts.reserve(fbxMesh.vertices.size());
        for (const auto& v : fbxMesh.vertices)
            verts.append(QVector3D(v.x, v.y, v.z));

        norms.reserve(fbxMesh.normals.size());
        for (const auto& n : fbxMesh.normals)
            norms.append(QVector3D(n.x, n.y, n.z));

        uvs.reserve(fbxMesh.texCoords.size());
        for (const auto& uv : fbxMesh.texCoords)
            uvs.append(QVector2D(uv.x, uv.y));

        idxs.reserve(fbxMesh.indices.size());
        for (uint32_t i : fbxMesh.indices)
            idxs.append(i);

        mesh3d->setVertices(verts);
        mesh3d->setNormals(norms);
        mesh3d->setUVs(uvs);
        mesh3d->setIndices(idxs);

        QString materialName = QString::fromStdString(fbxMesh.materialName);
        if (!materialName.isEmpty() && materialMap.contains(materialName)) {
            mesh3d->setMaterialId(materialName);
        }

        QString objName = QString::fromStdString(fbxMesh.name);
        if (objName.isEmpty())
            objName = QFileInfo(path).baseName();

        scene->addObject(objName, mesh3d);
    }

    qDebug() << "[ImportExport3D] Imported FBX:" << fbxMeshes.size() << "meshes,"
             << materialMap.size() << "materials";
    emit importProgress(100);
    return true;
}

bool ImportExport3D::importSTL(const QString& path, graphics::Scene3D* scene)
{
    if (!scene || !QFileInfo(path).exists()) return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[ImportExport3D] Cannot open STL file:" << path;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    // Check if binary or ASCII STL
    bool isBinary = false;
    if (data.size() > 84) {
        // Binary STL: 80 byte header + 4 byte triangle count + N * 50 bytes
        quint32 numTriangles = *reinterpret_cast<const quint32*>(data.constData() + 80);
        if (numTriangles > 0 && numTriangles < 10000000 &&
            data.size() == 84 + numTriangles * 50) {
            isBinary = true;
        }
    }

    QVector<QVector3D> positions;
    QVector<quint32> indices;

    if (isBinary) {
        quint32 numTriangles = *reinterpret_cast<const quint32*>(data.constData() + 80);
        const char* ptr = data.constData() + 84;

        for (quint32 i = 0; i < numTriangles; ++i) {
            // Skip normal (12 bytes)
            ptr += 12;
            for (int v = 0; v < 3; ++v) {
                float x = *reinterpret_cast<const float*>(ptr);
                float y = *reinterpret_cast<const float*>(ptr + 4);
                float z = *reinterpret_cast<const float*>(ptr + 8);
                positions.append(QVector3D(x, y, z));
                indices.append(positions.size() - 1);
                ptr += 12;
            }
            ptr += 2; // Skip attribute byte count
        }
    } else {
        // ASCII STL
        QTextStream in(&data);
        QVector<QVector3D> facePositions;
        bool inTriangle = false;

        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith("facet normal")) {
                facePositions.clear();
                inTriangle = true;
            } else if (line == "endfacet" && inTriangle) {
                for (const auto& p : facePositions) {
                    positions.append(p);
                    indices.append(positions.size() - 1);
                }
                inTriangle = false;
            } else if (line.startsWith("vertex") && inTriangle) {
                QStringList parts = line.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 4) {
                    facePositions.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
                }
            }
        }
    }

    if (positions.isEmpty()) {
        qWarning() << "[ImportExport3D] STL file contains no triangles:" << path;
        return false;
    }

    graphics::Mesh3D* mesh = new graphics::Mesh3D();
    mesh->setVertices(positions);
    mesh->setIndices(indices);

    // Compute flat normals
    QVector<QVector3D> normals;
    normals.resize(positions.size());
    for (int i = 0; i + 2 < indices.size(); i += 3) {
        QVector3D v0 = positions[indices[i]];
        QVector3D v1 = positions[indices[i + 1]];
        QVector3D v2 = positions[indices[i + 2]];
        QVector3D n = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();
        normals[indices[i]] = n;
        normals[indices[i + 1]] = n;
        normals[indices[i + 2]] = n;
    }
    mesh->setNormals(normals);

    scene->addObject(QFileInfo(path).baseName(), mesh);

    qDebug() << "[ImportExport3D] Imported STL:" << positions.size() << "vertices,"
             << indices.size() / 3 << "triangles" << (isBinary ? "(binary)" : "(ASCII)");
    emit importProgress(100);
    return true;
}

bool ImportExport3D::exportOBJ(graphics::Scene3D* scene, const QString& path)
{
    if (!scene) return false;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[ImportExport3D] Cannot write OBJ file:" << path;
        return false;
    }

    QTextStream out(&file);
    out << "# OBJ exported by ksEditor\n";
    out << "# " << scene->allObjects().size() << " objects\n\n";

    int vertexOffset = 0;
    int uvOffset = 0;
    int normalOffset = 0;

    for (auto* obj : scene->allObjects()) {
        if (!obj->mesh || !obj->visible) continue;

        out << "o " << obj->name << "\n";

        auto verts = obj->mesh->vertices();
        auto normals = obj->mesh->normals();
        auto uvs = obj->mesh->uvs();
        auto indices = obj->mesh->indices();

        // Apply transform
        QMatrix4x4 xform = obj->transform;
        for (auto& v : verts) {
            QVector3D tv = xform.map(v);
            out << "v " << tv.x() << " " << tv.y() << " " << tv.z() << "\n";
        }

        for (const auto& n : normals) {
            QVector3D tn = xform.map(n).normalized();
            out << "vn " << tn.x() << " " << tn.y() << " " << tn.z() << "\n";
        }

        for (const auto& uv : uvs) {
            out << "vt " << uv.x() << " " << uv.y() << "\n";
        }

        // Write faces (triangles)
        for (int i = 0; i + 2 < indices.size(); i += 3) {
            out << "f";
            for (int j = 0; j < 3; ++j) {
                int vi = indices[i + j] + 1 + vertexOffset;
                int ti = (i + j < uvs.size()) ? (i + j + 1 + uvOffset) : vi;
                int ni = (i + j < normals.size()) ? (i + j + 1 + normalOffset) : vi;
                out << " " << vi << "/" << ti << "/" << ni;
            }
            out << "\n";
        }

        vertexOffset += verts.size();
        uvOffset += uvs.size();
        normalOffset += normals.size();
        out << "\n";
    }

    qDebug() << "[ImportExport3D] Exported OBJ to:" << path;
    emit exportProgress(100);
    return true;
}

bool ImportExport3D::exportGLTF(graphics::Scene3D* scene, const QString& path)
{
    if (!scene) return false;

    QJsonObject root;

    // Build scene
    QJsonObject sceneObj;
    QJsonArray sceneNodes;
    int nodeIndex = 0;
    int meshIndex = 0;

    QJsonArray nodes;
    QJsonArray meshes;
    QJsonArray accessors;
    QJsonArray bufferViews;
    QByteArray binBuffer;

    for (auto* obj : scene->allObjects()) {
        if (!obj->mesh || !obj->visible) continue;

        auto verts = obj->mesh->vertices();
        auto indices = obj->mesh->indices();
        auto norms = obj->mesh->normals();
        auto uvs = obj->mesh->uvs();
        QMatrix4x4 xform = obj->transform;

        if (verts.isEmpty() || indices.isEmpty()) continue;

        QByteArray posData, normData, uvData, idxData;
        int posOffset = binBuffer.size();

        for (const auto& v : verts) {
            QVector3D tv = xform.map(v);
            float p[3] = { tv.x(), tv.y(), tv.z() };
            posData.append(reinterpret_cast<const char*>(p), 12);
        }

        int normOffset = binBuffer.size() + posData.size();
        for (const auto& n : norms) {
            QVector3D tn = xform.map(n).normalized();
            float d[3] = { tn.x(), tn.y(), tn.z() };
            normData.append(reinterpret_cast<const char*>(d), 12);
        }

        int uvOffset = normOffset + normData.size();
        for (const auto& uv : uvs) {
            float t[2] = { uv.x(), uv.y() };
            uvData.append(reinterpret_cast<const char*>(t), 8);
        }

        QJsonObject bufferViewPos;
        bufferViewPos["buffer"] = 0;
        bufferViewPos["byteOffset"] = posOffset;
        bufferViewPos["byteLength"] = posData.size();
        bufferViewPos["target"] = 34962; // ARRAY_BUFFER
        int bvPosIdx = bufferViews.size();
        bufferViews.append(bufferViewPos);

        QJsonObject bufferViewNorm;
        bufferViewNorm["buffer"] = 0;
        bufferViewNorm["byteOffset"] = normOffset;
        bufferViewNorm["byteLength"] = normData.size();
        bufferViewNorm["target"] = 34962;
        int bvNormIdx = bufferViews.size();
        bufferViews.append(bufferViewNorm);

        int bvUvIdx = -1;
        if (!uvData.isEmpty()) {
            QJsonObject bufferViewUv;
            bufferViewUv["buffer"] = 0;
            bufferViewUv["byteOffset"] = uvOffset;
            bufferViewUv["byteLength"] = uvData.size();
            bufferViewUv["target"] = 34962;
            bvUvIdx = bufferViews.size();
            bufferViews.append(bufferViewUv);
        }

        int idxOffset = uvOffset + uvData.size();

        QJsonObject bufferViewIdx;
        bool use16Bit = verts.size() <= 65535;
        int idxElementSize = use16Bit ? 2 : 4;
        for (quint32 idx : indices) {
            if (use16Bit) {
                quint16 i16 = (quint16)idx;
                idxData.append(reinterpret_cast<const char*>(&i16), 2);
            } else {
                idxData.append(reinterpret_cast<const char*>(&idx), 4);
            }
        }
        bufferViewIdx["buffer"] = 0;
        bufferViewIdx["byteOffset"] = idxOffset;
        bufferViewIdx["byteLength"] = idxData.size();
        bufferViewIdx["target"] = 34963; // ELEMENT_ARRAY_BUFFER
        int bvIdxIdx = bufferViews.size();
        bufferViews.append(bufferViewIdx);

        // Accessors
        QJsonObject accPos;
        accPos["bufferView"] = bvPosIdx;
        accPos["componentType"] = 5126; // FLOAT
        accPos["count"] = verts.size();
        accPos["type"] = "VEC3";
        int accPosIdx = accessors.size();
        accessors.append(accPos);

        QJsonObject accNorm;
        accNorm["bufferView"] = bvNormIdx;
        accNorm["componentType"] = 5126;
        accNorm["count"] = norms.isEmpty() ? verts.size() : norms.size();
        accNorm["type"] = "VEC3";
        int accNormIdx = accessors.size();
        accessors.append(accNorm);

        int accUvIdx = -1;
        if (bvUvIdx >= 0) {
            QJsonObject accUv;
            accUv["bufferView"] = bvUvIdx;
            accUv["componentType"] = 5126;
            accUv["count"] = uvs.size();
            accUv["type"] = "VEC2";
            accUvIdx = accessors.size();
            accessors.append(accUv);
        }

        QJsonObject accIdx;
        accIdx["bufferView"] = bvIdxIdx;
        accIdx["componentType"] = use16Bit ? 5123 : 5125; // UNSIGNED_SHORT or UNSIGNED_INT
        accIdx["count"] = indices.size();
        accIdx["type"] = "SCALAR";
        int accIdxIdx = accessors.size();
        accessors.append(accIdx);

        QJsonObject prim;
        QJsonObject attrs;
        attrs["POSITION"] = accPosIdx;
        attrs["NORMAL"] = accNormIdx;
        if (accUvIdx >= 0) attrs["TEXCOORD_0"] = accUvIdx;
        prim["attributes"] = attrs;
        prim["indices"] = accIdxIdx;
        prim["mode"] = 4; // TRIANGLES

        QJsonArray prims;
        prims.append(prim);

        QJsonObject meshObj;
        meshObj["name"] = obj->name;
        meshObj["primitives"] = prims;
        int meshIdxVal = meshes.size();
        meshes.append(meshObj);

        QJsonObject node;
        node["mesh"] = meshIdxVal;
        node["name"] = obj->name;
        nodes.append(node);

        sceneNodes.append(nodeIndex);
        nodeIndex++;

        binBuffer.append(posData);
        binBuffer.append(normData);
        binBuffer.append(uvData);
        binBuffer.append(idxData);
    }

    sceneObj["nodes"] = sceneNodes;
    QJsonArray scenes;
    scenes.append(sceneObj);
    root["scene"] = 0;
    root["scenes"] = scenes;
    root["nodes"] = nodes;
    root["meshes"] = meshes;
    root["accessors"] = accessors;
    root["bufferViews"] = bufferViews;

    // Asset info
    QJsonObject asset;
    asset["version"] = "2.0";
    asset["generator"] = "ksEditor";
    root["asset"] = asset;

    // Buffer
    QJsonObject buffer;
    buffer["byteLength"] = binBuffer.size();
    QJsonArray buffers;
    buffers.append(buffer);
    root["buffers"] = buffers;

    QJsonDocument doc(root);

    bool isGLB = path.endsWith(".glb", Qt::CaseInsensitive);

    if (isGLB) {
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
        // Pad to 4-byte alignment
        while (jsonData.size() % 4) jsonData.append(' ');
        while (binBuffer.size() % 4) binBuffer.append('\0');

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "[ImportExport3D] Cannot write GLB file:" << path;
            return false;
        }

        // Header: magic (4), version (4), total length (4)
        quint32 magic = 0x46546C67;
        quint32 version = 2;
        quint32 totalLen = 12 + 8 + jsonData.size() + 8 + binBuffer.size();

        file.write(reinterpret_cast<const char*>(&magic), 4);
        file.write(reinterpret_cast<const char*>(&version), 4);
        file.write(reinterpret_cast<const char*>(&totalLen), 4);

        // JSON chunk
        quint32 jsonChunkLen = jsonData.size();
        quint32 jsonChunkType = 0x4E4F534A;
        file.write(reinterpret_cast<const char*>(&jsonChunkLen), 4);
        file.write(reinterpret_cast<const char*>(&jsonChunkType), 4);
        file.write(jsonData);

        // BIN chunk
        quint32 binChunkLen = binBuffer.size();
        quint32 binChunkType = 0x004E4942;
        file.write(reinterpret_cast<const char*>(&binChunkLen), 4);
        file.write(reinterpret_cast<const char*>(&binChunkType), 4);
        file.write(binBuffer);

        file.close();
    } else {
        // GLTF separate file
        QJsonDocument doc(root);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "[ImportExport3D] Cannot write GLTF file:" << path;
            return false;
        }
        file.write(doc.toJson());
        file.close();

        // Write .bin file
        QString binPath = path;
        QRegularExpression re("\\.gltf$", QRegularExpression::CaseInsensitiveOption);
        binPath.replace(re, ".bin");
        QFile binFile(binPath);
        if (binFile.open(QIODevice::WriteOnly)) {
            binFile.write(binBuffer);
            binFile.close();
        }
    }

    qDebug() << "[ImportExport3D] Exported GLTF/GLB to:" << path;
    emit exportProgress(100);
    return true;
}

bool ImportExport3D::exportSTL(graphics::Scene3D* scene, const QString& path)
{
    if (!scene) return false;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[ImportExport3D] Cannot write STL file:" << path;
        return false;
    }

    QTextStream out(&file);
    out << "solid ksEditor_export\n";

    int totalTriangles = 0;
    for (auto* obj : scene->allObjects()) {
        if (!obj->mesh || !obj->visible) continue;

        auto verts = obj->mesh->vertices();
        auto indices = obj->mesh->indices();
        QMatrix4x4 xform = obj->transform;

        for (int i = 0; i + 2 < indices.size(); i += 3) {
            QVector3D v0 = xform.map(verts[indices[i]]);
            QVector3D v1 = xform.map(verts[indices[i + 1]]);
            QVector3D v2 = xform.map(verts[indices[i + 2]]);

            QVector3D normal = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();

            out << "  facet normal " << normal.x() << " " << normal.y() << " " << normal.z() << "\n";
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

    qDebug() << "[ImportExport3D] Exported STL:" << totalTriangles << "triangles to" << path;
    emit exportProgress(100);
    return true;
}


} // namespace io

// ─── TexturingSystem ─────────────────────────────────────────────────────────

TexturingSystem* TexturingSystem::s_instance = nullptr;

TexturingSystem::TexturingSystem(QObject* parent) : QObject(parent) {}

TexturingSystem* TexturingSystem::instance() {
    if (!s_instance) s_instance = new TexturingSystem();
    return s_instance;
}

void TexturingSystem::createMaterial(const QString& id, const QString& name) {
    Material mat;
    mat.id = id;
    mat.name = name;
    m_materials.insert(id, mat);
    emit materialCreated(id);
}

void TexturingSystem::deleteMaterial(const QString& id) {
    if (m_materials.remove(id)) {
        if (m_currentMaterial == id) m_currentMaterial.clear();
        emit materialDeleted(id);
    }
}

void TexturingSystem::addLayer(const QString& matId, const TextureLayer& layer) {
    auto it = m_materials.find(matId);
    if (it != m_materials.end()) {
        it->layers.append(layer);
        emit layerAdded(matId, layer.id);
    }
}

void TexturingSystem::removeLayer(const QString& matId, const QString& layerId) {
    auto it = m_materials.find(matId);
    if (it == m_materials.end()) return;
    for (int i = 0; i < it->layers.size(); ++i) {
        if (it->layers[i].id == layerId) {
            it->layers.removeAt(i);
            emit layerRemoved(matId, layerId);
            break;
        }
    }
}

void TexturingSystem::updateLayer(const QString& matId, const QString& layerId, const TextureLayer& layer) {
    auto it = m_materials.find(matId);
    if (it == m_materials.end()) return;
    for (int i = 0; i < it->layers.size(); ++i) {
        if (it->layers[i].id == layerId) {
            it->layers[i] = layer;
            emit textureModified(matId, layerId);
            break;
        }
    }
}

void TexturingSystem::paint(const QString& matId, const QString& layerId, const QImage& image, const QPoint& pos) {
    auto it = m_materials.find(matId);
    if (it == m_materials.end()) return;
    for (auto& l : it->layers) {
        if (l.id == layerId) {
            if (l.texture.isNull()) {
                l.texture = QImage(2048, 2048, QImage::Format_ARGB32);
                l.texture.fill(Qt::transparent);
            }
            for (int y = 0; y < image.height() && pos.y() + y < l.texture.height(); ++y)
                for (int x = 0; x < image.width() && pos.x() + x < l.texture.width(); ++x)
                    l.texture.setPixelColor(pos.x() + x, pos.y() + y, image.pixelColor(x, y));
            emit textureModified(matId, layerId);
            break;
        }
    }
}

void TexturingSystem::applyDecal(const QString& matId, const QString& layerId, const QImage& decal, const QPoint& pos, float angle) {
    QTransform transform;
    transform.translate(pos.x(), pos.y());
    transform.rotate(angle);
    QImage rotated = decal.transformed(transform, Qt::SmoothTransformation);
    paint(matId, layerId, rotated, QPoint(0, 0));
}

bool TexturingSystem::exportTextures(const QString& outputDir) {
    QDir dir(outputDir);
    if (!dir.exists()) dir.mkpath(".");
    for (auto it = m_materials.begin(); it != m_materials.end(); ++it) {
        for (int i = 0; i < it->layers.size(); ++i) {
            const auto& layer = it->layers[i];
            if (layer.texture.isNull()) continue;
            QString path = dir.filePath(it->id + "_" + layer.id + ".png");
            layer.texture.save(path);
        }
    }
    return true;
}

bool TexturingSystem::importTextures(const QString& inputDir) {
    QDir dir(inputDir);
    if (!dir.exists()) return false;
    for (const auto& fi : dir.entryInfoList({"*.png", "*.jpg", "*.tga"}, QDir::Files)) {
        QString base = fi.baseName();
        QStringList parts = base.split("_");
        if (parts.size() >= 2) {
            QString matId = parts[0];
            QString layerId = parts.mid(1).join("_");
            auto it = m_materials.find(matId);
            if (it == m_materials.end()) continue;
            for (auto& l : it->layers) {
                if (l.id == layerId) {
                    l.texture = QImage(fi.absoluteFilePath());
                    l.imagePath = fi.absoluteFilePath();
                    emit textureModified(matId, layerId);
                    break;
                }
            }
        }
    }
    return true;
}

QString TexturingSystem::textureTypeName(TextureType type) {
    switch (type) {
        case Diffuse: return "diffuse";
        case Normal: return "normal";
        case Specular: return "specular";
        case Emissive: return "emissive";
        case Ambient: return "ambient";
        case Height: return "height";
        case Opacity: return "opacity";
        case Metallic: return "metallic";
        case Roughness: return "roughness";
    }
    return "unknown";
}

TexturingSystem::TextureType TexturingSystem::stringToTextureType(const QString& name) {
    QString n = name.toLower();
    if (n == "diffuse") return Diffuse;
    if (n == "normal") return Normal;
    if (n == "specular") return Specular;
    if (n == "emissive") return Emissive;
    if (n == "ambient") return Ambient;
    if (n == "height") return Height;
    if (n == "opacity") return Opacity;
    if (n == "metallic") return Metallic;
    if (n == "roughness") return Roughness;
    return Diffuse;
}

namespace io {

bool ImportExport3D::importKS3D(const QString& path, graphics::Scene3D* scene)
{
    if (!scene || !QFileInfo(path).exists()) return false;

    KS3DReader reader;
    if (!reader.readFromFile(path.toStdString())) {
        qWarning() << "[ImportExport3D] Failed to load .ks3d file:" << reader.lastError().c_str();
        return false;
    }

    const auto& ksScene = reader.scene();

    // Import materials
    QVector<graphics::Material3D*> materials;
    for (const auto& ksMat : ksScene.materials) {
        auto* mat = new graphics::Material3D();
        mat->setName(QString::fromStdString(ksMat.name));
        mat->setDiffuse(QVector3D(ksMat.baseColor[0], ksMat.baseColor[1], ksMat.baseColor[2]));
        mat->setMetallic(ksMat.metallic);
        mat->setRoughness(ksMat.roughness);
        mat->setOpacity(ksMat.opacity);
        mat->setEmissive(QVector3D(ksMat.emissive[0], ksMat.emissive[1], ksMat.emissive[2]));
        materials.append(mat);
    }

    // Import meshes
    QVector<graphics::Mesh3D*> meshes;
    for (const auto& ksMesh : ksScene.meshes) {
        auto* mesh = new graphics::Mesh3D();
        mesh->setObjectName(QString::fromStdString(ksMesh.name));

        auto positions = KS3DReader::getVertexPositions(ksMesh);
        auto normals = KS3DReader::getVertexNormals(ksMesh);
        auto uvs = KS3DReader::getVertexUVs(ksMesh);

        QVector<QVector3D> verts;
        verts.reserve(static_cast<int>(positions.size() / 3));
        for (size_t i = 0; i + 2 < positions.size(); i += 3)
            verts.append(QVector3D(positions[i], positions[i + 1], positions[i + 2]));
        mesh->setVertices(verts);

        QVector<QVector3D> norms;
        norms.reserve(static_cast<int>(normals.size() / 3));
        for (size_t i = 0; i + 2 < normals.size(); i += 3)
            norms.append(QVector3D(normals[i], normals[i + 1], normals[i + 2]));
        mesh->setNormals(norms);

        QVector<QVector2D> uvVec;
        uvVec.reserve(static_cast<int>(uvs.size() / 2));
        for (size_t i = 0; i + 1 < uvs.size(); i += 2)
            uvVec.append(QVector2D(uvs[i], uvs[i + 1]));
        mesh->setUVs(uvVec);

        QVector<quint32> indices;
        indices.reserve(static_cast<int>(ksMesh.indices.size()));
        for (auto idx : ksMesh.indices)
            indices.append(static_cast<quint32>(idx));
        mesh->setIndices(indices);

        meshes.append(mesh);
    }

    // Import scene nodes
    for (int i = 0; i < ksScene.nodes.size(); i++) {
        const auto& ksNode = ksScene.nodes[i];
        auto* mesh = (ksNode.meshIndex >= 0 && ksNode.meshIndex < meshes.size())
            ? meshes[ksNode.meshIndex] : nullptr;
        auto* material = (ksNode.materialIndex >= 0 && ksNode.materialIndex < materials.size())
            ? materials[ksNode.materialIndex] : nullptr;

        QString name = QString::fromStdString(ksNode.name);
        if (name.isEmpty()) name = "Object_" + QString::number(i);

        QString objId = scene->addObject(name, mesh);

        if (auto* obj = scene->getObject(objId)) {
            QMatrix4x4 xform;
            xform.translate(QVector3D(ksNode.position[0], ksNode.position[1], ksNode.position[2]));
            xform.rotate(QQuaternion(ksNode.rotationQuat[3], ksNode.rotationQuat[0],
                                     ksNode.rotationQuat[1], ksNode.rotationQuat[2]));
            xform.scale(QVector3D(ksNode.scale[0], ksNode.scale[1], ksNode.scale[2]));
            scene->setObjectTransform(objId, xform);
            obj->visible = ksNode.visible;
        }
    }

    return true;
}

bool ImportExport3D::exportKS3D(graphics::Scene3D* scene, const QString& path)
{
    if (!scene) return false;

    KS3DScene ksScene;

    // Export materials
    QMap<QString, int> materialMap;
    QVector<graphics::Material3D*> sceneMaterials;
    for (auto* obj : scene->allObjects()) {
        if (obj->material && !materialMap.contains(obj->material->name())) {
            materialMap[obj->material->name()] = sceneMaterials.size();
            sceneMaterials.append(obj->material);
        }
    }

    for (auto* mat : sceneMaterials) {
        KS3DMaterial ksMat;
        ksMat.name = mat->name().toStdString();
        auto diff = mat->diffuse();
        ksMat.baseColor[0] = diff.x();
        ksMat.baseColor[1] = diff.y();
        ksMat.baseColor[2] = diff.z();
        ksMat.metallic = mat->metallic();
        ksMat.roughness = mat->roughness();
        ksMat.opacity = mat->opacity();
        auto emiss = mat->emissive();
        ksMat.emissive[0] = emiss.x();
        ksMat.emissive[1] = emiss.y();
        ksMat.emissive[2] = emiss.z();
        ksScene.materials.push_back(ksMat);
    }

    // Export meshes
    QMap<QString, int> meshMap;
    QVector<graphics::Mesh3D*> sceneMeshes;
    for (auto* obj : scene->allObjects()) {
        if (obj->mesh && !meshMap.contains(obj->mesh->objectName())) {
            meshMap[obj->mesh->objectName()] = sceneMeshes.size();
            sceneMeshes.append(obj->mesh);
        }
    }

    for (auto* mesh : sceneMeshes) {
        KS3DMesh ksMesh;
        ksMesh.name = mesh->objectName().toStdString();
        ksMesh.vertexFlags = static_cast<uint32_t>(ks3d::VertexFlags::Position)
                           | static_cast<uint32_t>(ks3d::VertexFlags::Normal)
                           | static_cast<uint32_t>(ks3d::VertexFlags::UV0);

        auto verts = mesh->vertices();
        auto norms = mesh->normals();
        auto uvs = mesh->uvs();
        auto indices = mesh->indices();

        int vertCount = verts.size();
        ksMesh.vertices.reserve(vertCount * 19);  // 19 floats per vertex
        for (int i = 0; i < vertCount; i++) {
            ksMesh.vertices.push_back(verts[i].x());
            ksMesh.vertices.push_back(verts[i].y());
            ksMesh.vertices.push_back(verts[i].z());
            if (i < norms.size()) {
                ksMesh.vertices.push_back(norms[i].x());
                ksMesh.vertices.push_back(norms[i].y());
                ksMesh.vertices.push_back(norms[i].z());
            } else {
                ksMesh.vertices.insert(ksMesh.vertices.end(), {0, 0, 1});
            }
            if (i < uvs.size()) {
                ksMesh.vertices.push_back(uvs[i].x());
                ksMesh.vertices.push_back(uvs[i].y());
            } else {
                ksMesh.vertices.insert(ksMesh.vertices.end(), {0, 0});
            }
            // tangent, bitangent, bone weights, bone index (zeroed)
            ksMesh.vertices.insert(ksMesh.vertices.end(), {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0});
        }

        ksMesh.indices.reserve(indices.size());
        for (auto idx : indices)
            ksMesh.indices.push_back(static_cast<uint32_t>(idx));

        if (!indices.isEmpty()) {
            KS3DSubmesh sub;
            sub.materialIndex = 0;
            sub.indexOffset = 0;
            sub.indexCount = static_cast<uint32_t>(indices.size());
            ksMesh.submeshes.push_back(sub);
        }

        ksScene.meshes.push_back(ksMesh);
    }

    // Export nodes
    for (auto* obj : scene->allObjects()) {
        KS3DNode ksNode;
        ksNode.name = obj->name.toStdString();

        auto xform = obj->transform;
        ksNode.position[0] = xform(0, 3);
        ksNode.position[1] = xform(1, 3);
        ksNode.position[2] = xform(2, 3);

        QQuaternion q = QQuaternion::fromRotationMatrix(xform.normalMatrix());
        // Simplified: extract rotation from matrix
        ksNode.rotationQuat[0] = q.x(); ksNode.rotationQuat[1] = q.y();
        ksNode.rotationQuat[2] = q.z(); ksNode.rotationQuat[3] = q.scalar();

        ksNode.scale[0] = xform(0, 0);
        ksNode.scale[1] = xform(1, 1);
        ksNode.scale[2] = xform(2, 2);

        if (obj->mesh && meshMap.contains(obj->mesh->objectName()))
            ksNode.meshIndex = meshMap[obj->mesh->objectName()];
        else
            ksNode.meshIndex = -1;

        if (obj->material && materialMap.contains(obj->material->name()))
            ksNode.materialIndex = materialMap[obj->material->name()];
        else
            ksNode.materialIndex = -1;

        ksNode.visible = obj->visible;
        ksNode.parentIndex = -1;

        ksScene.nodes.push_back(ksNode);
    }

    KS3DWriter writer;
    if (!writer.writeToFile(path.toStdString(), ksScene)) {
        qWarning() << "[ImportExport3D] Failed to write .ks3d file:" << writer.lastError().c_str();
        return false;
    }

    return true;
}

bool ImportExport3D::import3DM(const QString& path, graphics::Scene3D* scene) {
    if (!scene || !QFile::exists(path)) { qWarning() << "[ImportExport3D] File not found:" << path; return false; }
    qInfo() << "[ImportExport3D] Importing 3DM:" << path;

    // Try native3dm parsing first
    ks::Rhino3dmParser parser;
    if (parser.parse(path)) {
        int meshCount = parser.meshCount();
        if (meshCount > 0) {
            for (int i = 0; i < meshCount; ++i) {
                const auto& rhinoMesh = parser.meshes()[i];

                auto* mesh3d = new graphics::Mesh3D();
                QVector<QVector3D> verts;
                QVector<quint32> indices;

                for (const auto& v : rhinoMesh.vertices) {
                    verts.append(v);
                }

                for (const auto& face : rhinoMesh.faces) {
                    for (int idx : face) {
                        indices.append(quint32(idx));
                    }
                }

                mesh3d->setVertices(verts);
                mesh3d->setIndices(indices);
                mesh3d->computeNormals();

                QString objName = rhinoMesh.name.isEmpty() ?
                    QString("3DM_Mesh_%1").arg(i) : rhinoMesh.name;
                scene->addObject(objName, mesh3d);
            }

            qInfo() << "[ImportExport3D] Imported" << meshCount << "meshes from 3DM file (native parser)";
            return true;
        }
    }

    // Fallback to mesh conversion if native parsing fails
    qWarning() << "[ImportExport3D] Native 3DM parsing failed or no meshes found; attempting mesh fallback via Assimp.";
    return importOBJ(path, scene) || importSTL(path, scene);
}

bool ImportExport3D::export3DM(graphics::Scene3D* scene, const QString& path) {
    if (!scene) return false;
    qInfo() << "[ImportExport3D] Exporting 3DM:" << path;
    QString objPath = path; objPath.replace(QRegularExpression("\\.3dm$", QRegularExpression::CaseInsensitiveOption), ".obj");
    bool ok = exportOBJ(scene, objPath);
    if (ok) qInfo() << "[ImportExport3D] Exported 3DM as OBJ to:" << objPath;
    return ok;
}

} // namespace io
} // namespace ks
