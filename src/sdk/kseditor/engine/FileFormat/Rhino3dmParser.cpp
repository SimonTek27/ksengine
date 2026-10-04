#include "Rhino3dmParser.h"
#include <QDebug>
#include <QFileInfo>
#include <QBuffer>

namespace ks {

// Mesh UUID from OpenNURBS (ON_Mesh class)
const quint8 Rhino3dmParser::MESH_UUID[16] = {
    0x4E, 0xD0, 0x6E, 0x2C, 0xB4, 0x57, 0x8C, 0x46,
    0x97, 0x04, 0x93, 0xCB, 0xB5, 0x88, 0xB4, 0x6F
};

Rhino3dmParser::Rhino3dmParser()
    : m_fileVersion(0)
    , m_openNurbsVersion(0)
{
}

Rhino3dmParser::~Rhino3dmParser() {
    if (m_file.isOpen()) {
        m_file.close();
    }
}

bool Rhino3dmParser::is3dmFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    if (file.size() < 32) return false;

    char header[32];
    if (file.read(header, 32) != 32) return false;

    // Check for "3D Geometry File Format" signature
    // or version number in first 4 bytes (little-endian quint32)
    quint32 version = *reinterpret_cast<const quint32*>(header);
    return (version >= 1 && version <= 8);
}

bool Rhino3dmParser::parse(const QString& filePath) {
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::ReadOnly)) {
        m_lastError = QString("Cannot open file: %1").arg(filePath);
        return false;
    }

    m_stream.setDevice(&m_file);
    m_stream.setByteOrder(QDataStream::LittleEndian);

    if (!readHeader()) {
        m_lastError = "Failed to read file header";
        return false;
    }

    // Read chunks until end of file
    while (!m_stream.atEnd()) {
        ChunkHeader chunk;
        m_stream >> chunk.typeCode >> chunk.length;

        if (m_stream.status() != QDataStream::Ok) break;

        if (chunk.typeCode == TCODE_ENDOFFILE) {
            break;
        }

        if (chunk.typeCode == TCODE_COMMENTBLOCK) {
            skipChunk(chunk.length);
            continue;
        }

        if ((chunk.typeCode & 0x70000000) == TCODE_TABLE) {
            readTable(chunk.typeCode & 0x0FFF);
            continue;
        }

        if ((chunk.typeCode & 0x70000000) == TCODE_TABLEREC) {
            skipChunk(chunk.length);
            continue;
        }

        if (chunk.typeCode == TCODE_OPENNURBS_CLASS) {
            readObjectRecord();
            continue;
        }

        skipChunk(chunk.length);
    }

    m_file.close();
    return m_meshes.size() > 0;
}

bool Rhino3dmParser::parseFromData(const QByteArray& data) {
    QBuffer buffer(const_cast<QByteArray*>(&data));
    if (!buffer.open(QIODevice::ReadOnly)) {
        m_lastError = "Cannot open buffer";
        return false;
    }

    m_stream.setDevice(&buffer);
    m_stream.setByteOrder(QDataStream::LittleEndian);

    if (!readHeader()) {
        m_lastError = "Failed to read header from buffer";
        return false;
    }

    while (!m_stream.atEnd()) {
        ChunkHeader chunk;
        m_stream >> chunk.typeCode >> chunk.length;

        if (m_stream.status() != QDataStream::Ok) break;

        if (chunk.typeCode == TCODE_ENDOFFILE) break;

        if (chunk.typeCode == TCODE_COMMENTBLOCK) {
            skipChunk(chunk.length);
            continue;
        }

        if ((chunk.typeCode & 0x70000000) == TCODE_TABLE) {
            readTable(chunk.typeCode & 0x0FFF);
            continue;
        }

        if ((chunk.typeCode & 0x70000000) == TCODE_TABLEREC) {
            skipChunk(chunk.length);
            continue;
        }

        if (chunk.typeCode == TCODE_OPENNURBS_CLASS) {
            readObjectRecord();
            continue;
        }

        skipChunk(chunk.length);
    }

    buffer.close();
    return m_meshes.size() > 0;
}

bool Rhino3dmParser::readHeader() {
    m_stream >> m_fileVersion;
    m_stream >> m_openNurbsVersion;

    // Skip rest of header (32 bytes total)
    qint32 headerSize = 32;
    qint32 bytesRead = 8; // version + opennurbs version
    if (headerSize > bytesRead) {
        m_stream.skipRawData(headerSize - bytesRead);
    }

    // Validate version
    if (m_fileVersion < 1 || m_fileVersion > 8) {
        m_lastError = QString("Unsupported 3dm version: %1").arg(m_fileVersion);
        return false;
    }

    return true;
}

bool Rhino3dmParser::readTable(int tableType) {
    Q_UNUSED(tableType);
    // For now, skip tables
    return true;
}

bool Rhino3dmParser::readObjectRecord() {
    // Read object chunk
    quint32 uuidLength;
    m_stream >> uuidLength;

    if (uuidLength != 16 + 4) { // 16 UUID + 4 CRC
        return false;
    }

    // Read UUID
    quint8 uuid[16];
    m_stream.readRawData(reinterpret_cast<char*>(uuid), 16);

    quint32 crc;
    m_stream >> crc;

    // Check if this is a mesh object
    bool isMesh = (memcmp(uuid, MESH_UUID, 16) == 0);

    // Read class data chunk
    quint32 dataTcode, dataLength;
    m_stream >> dataTcode >> dataLength;

    if (dataTcode != (TCODE_OPENNURBS_CLASS_DATA | TCODE_CRC)) {
        skipChunk(dataLength);
        return false;
    }

    if (isMesh) {
        readMeshChunk(dataLength);
    } else {
        skipChunk(dataLength);
    }

    // Read end chunk
    quint32 endTcode, endLength;
    m_stream >> endTcode >> endLength;
    if (endLength > 0) {
        m_stream.skipRawData(endLength);
    }

    return true;
}

bool Rhino3dmParser::readMeshChunk(quint32 length) {
    Q_UNUSED(length);
    Rhino3dmMesh mesh;

    quint32 crc;
    m_stream >> crc;

    quint32 version;
    m_stream >> version;

    // Read bounding box
    double bboxMin[3], bboxMax[3];
    for (int i = 0; i < 3; ++i) m_stream >> bboxMin[i];
    for (int i = 0; i < 3; ++i) m_stream >> bboxMax[i];

    // Read mesh flags
    quint32 flags;
    m_stream >> flags;

    // Read vertex count
    quint32 vertexCount;
    m_stream >> vertexCount;

    // Read vertices (3 doubles each)
    mesh.vertices.reserve(vertexCount);
    for (quint32 i = 0; i < vertexCount; ++i) {
        double x, y, z;
        m_stream >> x >> y >> z;
        mesh.vertices.append(QVector3D(float(x), float(y), float(z)));
    }

    // Read face count
    quint32 faceCount;
    m_stream >> faceCount;

    // Read faces (3 or 4 indices per face)
    mesh.faces.reserve(faceCount);
    for (quint32 i = 0; i < faceCount; ++i) {
        quint32 a, b, c, d;
        m_stream >> a >> b >> c >> d;

        QVector<int> face;
        face.append(int(a));
        face.append(int(b));
        face.append(int(c));
        if (d != 0xFFFFFFFF) {
            face.append(int(d));
        }
        mesh.faces.append(face);
    }

    // Read normals if present (based on flags)
    if (flags & 0x01) { // Has normals
        quint32 normalCount;
        m_stream >> normalCount;
        mesh.normals.reserve(normalCount);
        for (quint32 i = 0; i < normalCount; ++i) {
            double nx, ny, nz;
            m_stream >> nx >> ny >> nz;
            mesh.normals.append(QVector3D(float(nx), float(ny), float(nz)));
        }
    }

    // Read UVs if present
    if (flags & 0x02) { // Has texture coordinates
        quint32 uvCount;
        m_stream >> uvCount;
        mesh.uvs.reserve(uvCount);
        for (quint32 i = 0; i < uvCount; ++i) {
            double u, v;
            m_stream >> u >> v;
            mesh.uvs.append(QVector2D(float(u), float(v)));
        }
    }

    m_meshes.append(mesh);
    return true;
}

bool Rhino3dmParser::readNurbsSurfaceChunk(quint32 length) {
    Q_UNUSED(length);
    // Skip NURBS surfaces for now
    return true;
}

bool Rhino3dmParser::skipChunk(quint32 length) {
    if (length > 0) {
        m_stream.skipRawData(length);
    }
    return true;
}

} // namespace ks
