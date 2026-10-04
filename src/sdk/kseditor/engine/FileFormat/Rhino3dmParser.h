#ifndef RHINO3DMPARSER_H
#define RHINO3DMPARSER_H

#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QFile>
#include <QDataStream>

namespace ks {

struct Rhino3dmMesh {
    QString name;
    QVector<QVector3D> vertices;
    QVector<QVector3D> normals;
    QVector<QVector2D> uvs;
    QVector<QVector<int>> faces;
    QVector<int> faceMaterials;
};

class Rhino3dmParser {
public:
    Rhino3dmParser();
    ~Rhino3dmParser();

    bool parse(const QString& filePath);
    bool parseFromData(const QByteArray& data);

    QVector<Rhino3dmMesh> meshes() const { return m_meshes; }
    int meshCount() const { return m_meshes.size(); }
    QString lastError() const { return m_lastError; }

    static bool is3dmFile(const QString& filePath);

private:
    struct ChunkHeader {
        quint32 typeCode;
        quint32 length;
    };

    bool readHeader();
    bool readTable(int tableType);
    bool readObjectRecord();
    bool readMeshChunk(quint32 length);
    bool readNurbsSurfaceChunk(quint32 length);
    bool skipChunk(quint32 length);

    QFile m_file;
    QDataStream m_stream;
    quint32 m_fileVersion;
    quint32 m_openNurbsVersion;

    QVector<Rhino3dmMesh> m_meshes;
    QString m_lastError;

    // Type codes
    static const quint32 TCODE_ENDOFFILE = 0x00007FFF;
    static const quint32 TCODE_COMMENTBLOCK = 0x00000001;
    static const quint32 TCODE_TABLE = 0x10000000;
    static const quint32 TCODE_TABLEREC = 0x20000000;
    static const quint32 TCODE_OPENNURBS_OBJECT = 0x00020000;
    static const quint32 TCODE_OPENNURBS_CLASS = 0x00027FFA;
    static const quint32 TCODE_OPENNURBS_CLASS_UUID = 0x0002FFFB;
    static const quint32 TCODE_OPENNURBS_CLASS_DATA = 0x0002FFFC;
    static const quint32 TCODE_OPENNURBS_CLASS_END = 0x0002FFFF;
    static const quint32 TCODE_SHORT = 0x80000000;
    static const quint32 TCODE_CRC = 0x00008000;

    // Mesh UUID (from OpenNURBS)
    static const quint8 MESH_UUID[16];
};

} // namespace ks

#endif // RHINO3DMPARSER_H
