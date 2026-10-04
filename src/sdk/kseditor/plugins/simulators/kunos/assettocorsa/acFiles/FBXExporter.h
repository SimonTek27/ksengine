#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include "sdk/kseditor/engine/mesh/MeshOperations.h"

namespace ks {

struct FBXNode {
    QString name;
    QMap<QString, QString> properties;
    QVector<FBXNode> children;
    QByteArray data;
};

struct FBXImportSettings {
    bool importMaterials;
    bool importTextures;
    bool importAnimations;
    bool importSkinning;
    bool flipY;
    bool convertToYUp;
    float scale;
    QString texturePath;
};

struct FBXExportSettings {
    bool exportMaterials  = true;
    bool exportTextures   = true;
    bool exportAnimations = false;
    bool exportSkinning   = false;
    bool embedTextures    = false;
    bool binaryFormat     = false;
    bool exportUV2        = true;   // UV channel 2 (damage maps, lightmaps)
    bool exportTangents   = true;   // tangent + binormal (needed for NM shaders)
    bool exportNormals    = true;
    float scale           = 1.0f;
    int   axisForward     = 2;
    int   axisUp          = 1;
};

class FBXExporter {
public:
    static bool exportToFBX(const QString& path, const MeshData& mesh, const FBXExportSettings& settings = FBXExportSettings());
    static bool exportToFBX(const QString& path, const QVector<MeshData>& meshes, const FBXExportSettings& settings = FBXExportSettings());

    static QString validateExportSettings(const FBXExportSettings& settings);
    static FBXExportSettings getDefaultExportSettings();

    static QByteArray generateFBXHeader(const QString& exporter, const QString& version);
    static QByteArray generateMeshNode(const QString& name, const MeshData& mesh, const FBXExportSettings& settings);
    static QByteArray generateMaterialNode(const QString& name, const QVector4D& color, float metallic, float roughness);
};

class FBXImporter {
public:
    static bool importFromFBX(const QString& path, MeshData& mesh, const FBXImportSettings& settings = FBXImportSettings());
    static bool importFromFBX(const QString& path, QVector<MeshData>& meshes, const FBXImportSettings& settings = FBXImportSettings());

    static QString validateImportSettings(const FBXImportSettings& settings);
    static FBXImportSettings getDefaultImportSettings();

    static bool parseFBXHeader(const QByteArray& data, QString& version);
    static bool parseMeshNode(const FBXNode& node, MeshData& mesh);
    static QVector4D parseMaterialColor(const FBXNode& materialNode);

    static QByteArray readFileBinary(const QString& path);
    static FBXNode parseNode(const QByteArray& data, int& offset);
};

}