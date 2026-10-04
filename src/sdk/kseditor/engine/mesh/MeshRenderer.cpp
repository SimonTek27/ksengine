#include "MeshRenderer.h"
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QDebug>
#include <QVector3D>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QQuaternion>

MeshRenderer::MeshRenderer(QObject *parent)
    : QObject(parent)
    , m_scale(1.0f, 1.0f, 1.0f)
{
}

MeshRenderer::~MeshRenderer() = default;

bool MeshRenderer::parseKN5Header(QDataStream &stream, KN5Header &header)
{
    stream.readRawData(header.magic, 4);
    stream >> header.version >> header.flags >> header.headerSize;

    if (QLatin1String(header.magic, 4) != "KN5") {
        return false;
    }

    if (header.version != 5) {
        qWarning() << "KN5 version" << header.version << "may not be fully supported";
    }

    return true;
}

bool MeshRenderer::parseKN5Materials(QDataStream &stream, const KN5Header &header)
{
    quint32 meshCount, materialCount, textureCount;
    stream >> meshCount >> materialCount >> textureCount;

    for (quint32 i = 0; i < textureCount; ++i) {
        quint8 strLen;
        stream >> strLen;
        QByteArray texName;
        texName.resize(strLen);
        stream.readRawData(texName.data(), strLen);
        m_textures.append(QString::fromUtf8(texName));
    }

    for (quint32 i = 0; i < materialCount; ++i) {
        quint8 strLen;
        stream >> strLen;
        QByteArray matName;
        matName.resize(strLen);
        stream.readRawData(matName.data(), strLen);
        m_materials.append(QString::fromUtf8(matName));
    }

    return true;
}

bool MeshRenderer::parseKN5Meshes(QDataStream &stream, const KN5Header &header)
{
    struct MeshInfo {
        quint32 vertexCount;
        quint32 indexCount;
        quint32 vertexStride;
        quint32 flags;
    };
    QVector<MeshInfo> meshInfos;

    quint32 meshCount;
    stream >> meshCount;

    for (quint32 i = 0; i < meshCount; ++i) {
        quint8 nameLen;
        stream >> nameLen;
        stream.skipRawData(nameLen);

        MeshInfo info;
        stream >> info.vertexCount >> info.indexCount >> info.vertexStride >> info.flags;
        meshInfos.append(info);
    }

    quint64 dataStart;
    stream >> dataStart;

    for (const MeshInfo &info : meshInfos) {
        quint32 baseVertex = m_vertices.size();

        for (quint32 v = 0; v < info.vertexCount; ++v) {
            MeshVertex vertex;
            float x, y, z;
            stream >> x >> y >> z;
            vertex.position = QVector3D(x, y, z);

            float nx, ny, nz;
            stream >> nx >> ny >> nz;
            vertex.normal = QVector3D(nx, ny, nz);

            float texU, texV;
            stream >> texU >> texV;
            vertex.texCoord = QVector2D(texU, texV);

            if (info.vertexStride >= 32) {
                float tx, ty, tz, tw;
                stream >> tx >> ty >> tz >> tw;
            }

            if (info.vertexStride >= 36) {
                quint32 color;
                stream >> color;
                uchar r = (color >> 0) & 0xFF;
                uchar g = (color >> 8) & 0xFF;
                uchar b = (color >> 16) & 0xFF;
                uchar a = (color >> 24) & 0xFF;
                vertex.color = QVector4D(r/255.0f, g/255.0f, b/255.0f, a/255.0f);
            } else {
                vertex.color = QVector4D(1, 1, 1, 1);
            }

            m_vertices.append(vertex);
        }

        for (quint32 idx = 0; idx < info.indexCount; ++idx) {
            quint32 index;
            stream >> index;
            m_indices.append(baseVertex + index);
        }
    }

    return true;
}

bool MeshRenderer::loadFromKN5(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit loadError("Cannot open file: " + filePath);
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    KN5Header header;
    if (!parseKN5Header(stream, header)) {
        file.close();
        emit loadError("Invalid KN5 file: bad magic");
        return false;
    }

    m_vertices.clear();
    m_indices.clear();
    m_materials.clear();
    m_textures.clear();

    if (!parseKN5Materials(stream, header)) {
        file.close();
        emit loadError("Failed to parse KN5 materials");
        return false;
    }

    if (!parseKN5Meshes(stream, header)) {
        file.close();
        emit loadError("Failed to parse KN5 meshes");
        return false;
    }

    file.close();

    m_meshName = QFileInfo(filePath).baseName();
    emit meshLoaded(m_meshName, m_vertices.size(), m_indices.size() / 3);
    return true;
}

bool MeshRenderer::loadFromOBJ(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit loadError("Cannot open file: " + filePath);
        return false;
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> texCoords;
    m_vertices.clear();
    m_indices.clear();

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        QString cmd = parts[0];
        if (cmd == "v") {
            if (parts.size() >= 4) {
                positions.append(QVector3D(
                    parts[1].toFloat(),
                    parts[2].toFloat(),
                    parts[3].toFloat()
                ));
            }
        } else if (cmd == "vn") {
            if (parts.size() >= 4) {
                normals.append(QVector3D(
                    parts[1].toFloat(),
                    parts[2].toFloat(),
                    parts[3].toFloat()
                ));
            }
        } else if (cmd == "vt") {
            if (parts.size() >= 3) {
                texCoords.append(QVector2D(
                    parts[1].toFloat(),
                    parts[2].toFloat()
                ));
            }
        } else if (cmd == "f") {
            QVector<QVector3D> faceIndices;
            for (int i = 1; i < parts.size(); ++i) {
                QStringList idxParts = parts[i].split('/');
                int posIdx = idxParts[0].toInt() - 1;
                int texIdx = idxParts.size() > 1 && !idxParts[1].isEmpty() ? idxParts[1].toInt() - 1 : -1;
                int normIdx = idxParts.size() > 2 && !idxParts[2].isEmpty() ? idxParts[2].toInt() - 1 : -1;

                MeshVertex vertex;
                vertex.position = (posIdx >= 0 && posIdx < positions.size()) ? positions[posIdx] : QVector3D();
                vertex.texCoord = (texIdx >= 0 && texIdx < texCoords.size()) ? texCoords[texIdx] : QVector2D();
                vertex.normal = (normIdx >= 0 && normIdx < normals.size()) ? normals[normIdx] : QVector3D(0, 1, 0);
                vertex.color = QVector4D(1, 1, 1, 1);

                faceIndices.append(QVector3D(m_vertices.size(), texIdx, normIdx));
                m_vertices.append(vertex);
            }

            for (int i = 1; i < faceIndices.size() - 1; ++i) {
                m_indices.append(faceIndices[0].x());
                m_indices.append(faceIndices[i].x());
                m_indices.append(faceIndices[i + 1].x());
            }
        }
    }

    file.close();

    m_meshName = QFileInfo(filePath).baseName();
    emit meshLoaded(m_meshName, m_vertices.size(), m_indices.size() / 3);
    return true;
}

bool MeshRenderer::loadFromGLTF(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit loadError("Cannot open file: " + filePath);
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull()) {
        emit loadError("Invalid GLTF JSON");
        return false;
    }

    QJsonObject gltf = doc.object();

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> texCoords;

    auto readAccessor = [&](const QJsonObject &accessor) -> QVariantList {
        int bufferViewIdx = accessor["bufferView"].toInt();
        int count = accessor["count"].toInt();
        QString type = accessor["type"].toString();
        QString componentTypeStr = accessor["componentType"].toString();

        QJsonArray bufferViews = gltf["bufferViews"].toArray();
        QJsonObject bufferView = bufferViews[bufferViewIdx].toObject();
        int byteOffset = bufferView["byteOffset"].toInt();
        int byteLength = bufferView["byteLength"].toInt();
        int byteStride = 0;
        if (bufferView.contains("byteStride")) {
            byteStride = bufferView["byteStride"].toInt();
        }
        
        // Read raw buffer data
        QJsonArray buffers = gltf["buffers"].toArray();
        int bufIdx = bufferView["buffer"].toInt();
        QString uri = buffers[bufIdx].toObject()["uri"].toString();
        
        QFile bufFile(QFileInfo(file.fileName()).absolutePath() + "/" + uri);
        if (!bufFile.open(QIODevice::ReadOnly)) return {};
        
        QByteArray data = bufFile.readAll();
        if (byteOffset + byteLength > data.size()) return {};
        
        QByteArray chunk = data.mid(byteOffset, byteLength);
        
        int componentSize = 1;
        if (componentTypeStr == "FLOAT" || componentTypeStr == "5126") componentSize = 4;
        else if (componentTypeStr == "UNSIGNED_INT" || componentTypeStr == "5125") componentSize = 4;
        else if (componentTypeStr == "UNSIGNED_SHORT" || componentTypeStr == "5123") componentSize = 2;
        else if (componentTypeStr == "UNSIGNED_BYTE" || componentTypeStr == "5121") componentSize = 1;
        
        int numComponents = 1;
        if (type == "VEC2") numComponents = 2;
        else if (type == "VEC3") numComponents = 3;
        else if (type == "VEC4" || type == "MAT2") numComponents = 4;
        else if (type == "MAT3") numComponents = 9;
        else if (type == "MAT4") numComponents = 16;
        
        QDataStream stream(chunk);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        
        QVariantList result;
        for (int i = 0; i < count && !stream.atEnd(); ++i) {
            if (byteStride > 0 && i > 0) {
                // Skip padding between elements
                int elementSize = numComponents * componentSize;
                int padding = byteStride - elementSize;
                if (padding > 0) stream.skipRawData(padding);
            }
            for (int c = 0; c < numComponents; ++c) {
                if (componentSize == 4) {
                    float val;
                    stream >> val;
                    result.append(val);
                } else if (componentSize == 2) {
                    quint16 val;
                    stream >> val;
                    result.append(val);
                } else {
                    quint8 val;
                    stream >> val;
                    result.append(val);
                }
            }
        }
        
        return result;
    };

    QJsonArray meshes = gltf["meshes"].toArray();
    for (const QJsonValue &meshVal : meshes) {
        QJsonObject mesh = meshVal.toObject();
        QJsonArray primitives = mesh["primitives"].toArray();

        for (const QJsonValue &primVal : primitives) {
            QJsonObject prim = primVal.toObject();
            QJsonObject attributes = prim["attributes"].toObject();

            if (attributes.contains("POSITION")) {
                QVariantList posData = readAccessor(gltf["accessors"].toArray()[attributes["POSITION"].toInt()].toObject());
                for (int i = 0; i + 2 < posData.size(); i += 3) {
                    QVector3D pos(posData[i].toFloat(), posData[i+1].toFloat(), posData[i+2].toFloat());
                    positions.append(pos);
                    MeshVertex mv;
                    mv.position = pos;
                    m_vertices.append(mv);
                }
            }
            
            if (attributes.contains("NORMAL")) {
                QVariantList normData = readAccessor(gltf["accessors"].toArray()[attributes["NORMAL"].toInt()].toObject());
                for (int i = 0; i + 2 < normData.size(); i += 3) {
                    normals.append(QVector3D(normData[i].toFloat(), normData[i+1].toFloat(), normData[i+2].toFloat()));
                }
            }
            
            if (attributes.contains("TEXCOORD_0")) {
                QVariantList uvData = readAccessor(gltf["accessors"].toArray()[attributes["TEXCOORD_0"].toInt()].toObject());
                for (int i = 0; i + 1 < uvData.size(); i += 2) {
                    texCoords.append(QVector2D(uvData[i].toFloat(), uvData[i+1].toFloat()));
                }
            }
            
            if (prim.contains("indices")) {
                QJsonObject idxAccessor = gltf["accessors"].toArray()[prim["indices"].toInt()].toObject();
                QVariantList idxData = readAccessor(idxAccessor);
                for (int i = 0; i < idxData.size(); ++i) {
                    m_indices.append(idxData[i].toInt());
                }
            }

            QJsonValue materialVal = prim["material"];
            if (!materialVal.isUndefined()) {
                int matIdx = materialVal.toInt();
            }
        }
    }

    for (int i = 0; i < positions.size(); ++i) {
        MeshVertex v;
        v.position = positions[i];
        v.normal = i < normals.size() ? normals[i] : QVector3D(0, 1, 0);
        v.texCoord = i < texCoords.size() ? texCoords[i] : QVector2D();
        v.color = QVector4D(1, 1, 1, 1);
        m_vertices.append(v);
    }

    m_meshName = QFileInfo(filePath).baseName();
    emit meshLoaded(m_meshName, m_vertices.size(), m_indices.size() / 3);
    return true;
}

QMatrix4x4 MeshRenderer::getTransformMatrix() const
{
    QMatrix4x4 matrix;
    matrix.translate(m_position);

    if (!qFuzzyIsNull(m_rotation.x())) matrix.rotate(QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, m_rotation.x()));
    if (!qFuzzyIsNull(m_rotation.y())) matrix.rotate(QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, m_rotation.y()));
    if (!qFuzzyIsNull(m_rotation.z())) matrix.rotate(QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, m_rotation.z()));

    matrix.scale(m_scale);
    return matrix;
}