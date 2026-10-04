#pragma once

#include <QString>
#include <QStringList>
#include <QObject>
#include <QVector>
#include <QMap>

#include "sdk/kseditor/engine/FileFormat/MeshData.h"

namespace ks {

struct ConversionOptions {
    bool preserveMaterials = true;
    bool preserveUVs = true;
    bool preserveNormals = true;
    bool preserveSkinning = true;
    bool preserveAnimations = true;
    bool flipYAxis = true;
    bool optimizeMesh = false;
    int subdivisionLevel = 0;
};

class FormatConverter : public QObject {
    Q_OBJECT

public:
    explicit FormatConverter(QObject* parent = nullptr);
    ~FormatConverter();

    enum class ConversionType {
        None,
        KN5toFBX,
        FBXtoKN5,
        KN5toGLB,
        GLBtoKN5,
        FBXtoGLB,
        GLBtoFBX,
        OBJtoKN5,
        KN5toOBJ,
        FBXtoOBJ,
        OBJtoFBX
    };

    static QStringList supportedConversions();
    static ConversionType detectConversion(const QString& fromExt, const QString& toExt);

    bool convert(const QString& inputPath, const QString& outputPath,
               const ConversionOptions& options = ConversionOptions());

    bool convertToMeshData(const QString& inputPath, ks::fileformat::MeshData& outData,
                           QString* error = nullptr);
    bool convertFromMeshData(const ks::fileformat::MeshData& data, const QString& outputPath,
                             QString* error = nullptr);

    bool convertKN5ToFBX(const QString& inputPath, const QString& outputPath,
                         const ConversionOptions& options = ConversionOptions());
    bool convertFBXToKN5(const QString& inputPath, const QString& outputPath,
                         const ConversionOptions& options = ConversionOptions());
    bool convertKN5ToGLB(const QString& inputPath, const QString& outputPath,
                         const ConversionOptions& options = ConversionOptions());
    bool convertGLBToKN5(const QString& inputPath, const QString& outputPath,
                         const ConversionOptions& options = ConversionOptions());
    bool convertFBXToGLB(const QString& inputPath, const QString& outputPath,
                         const ConversionOptions& options = ConversionOptions());
    bool convertGLBToFBX(const QString& inputPath, const QString& outputPath,
                         const ConversionOptions& options = ConversionOptions());
    bool convertOBJToKN5(const QString& inputPath, const QString& outputPath,
                         const ConversionOptions& options = ConversionOptions());
    bool convertKN5ToOBJ(const QString& inputPath, const QString& outputPath,
                         const ConversionOptions& options = ConversionOptions());
    bool convertFBXToOBJ(const QString& inputPath, const QString& outputPath,
                         const ConversionOptions& options = ConversionOptions());
    bool convertOBJToFBX(const QString& inputPath, const QString& outputPath,
                         const ConversionOptions& options = ConversionOptions());

    float getProgress() const { return m_progress; }

signals:
    void progressChanged(float progress);
    void conversionComplete();
    void conversionError(const QString& error);

private:
    float m_progress = 0.0f;

    bool modelToKN5(const ks::fileformat::MeshData& model, const QString& outputPath);
    bool modelToOBJ(const ks::fileformat::MeshData& model, const QString& outputPath);
    bool modelToFBX(const ks::fileformat::MeshData& model, const QString& outputPath);
    bool modelToGLTF(const ks::fileformat::MeshData& model, const QString& outputPath);
    bool kn5ToModel(const QString& inputPath, ks::fileformat::MeshData& model);
};

class BatchConverter : public QObject {
    Q_OBJECT

public:
    explicit BatchConverter(QObject* parent = nullptr);
    ~BatchConverter();

    void addFile(const QString& path);
    void addDirectory(const QString& path, bool recursive = false);
    void clearQueue();

    void setOutputFormat(const QString& format);
    void setOutputDirectory(const QString& dir);
    void setOptions(const ConversionOptions& options);

    int queueCount() const { return m_queue.size(); }

public slots:
    void startConversion();
    void cancelConversion();

signals:
    void progressChanged(float progress, const QString& currentFile);
    void fileConverted(const QString& input, const QString& output);
    void conversionComplete(int successCount, int failureCount);
    void conversionError(const QString& input, const QString& error);

private:
    QStringList m_queue;
    QString m_outputFormat;
    QString m_outputDir;
    ConversionOptions m_options;
    bool m_cancelled = false;
    int m_successCount = 0;
    int m_failureCount = 0;
};

} // namespace ks
