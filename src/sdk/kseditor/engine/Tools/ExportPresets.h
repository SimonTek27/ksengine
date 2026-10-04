#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>

namespace ks {

struct ExportPreset {
    QString id;
    QString name;
    QString category;
    QString description;
    QString outputFormat;
    QJsonObject parameters;
    bool isBuiltIn = false;
    bool isDefault = false;
};

class ExportPresets : public QObject
{
    Q_OBJECT

public:
    static ExportPresets* instance();
    explicit ExportPresets(QObject* parent = nullptr);
    ~ExportPresets();

    void loadPresets();
    void loadFromFile(const QString& path);
    void saveToFile(const QString& path);

    void registerPreset(const ExportPreset& preset);
    void unregisterPreset(const QString& presetId);
    void updatePreset(const ExportPreset& preset);

    bool hasPreset(const QString& presetId) const;
    ExportPreset getPreset(const QString& presetId) const;
    QVector<ExportPreset> getPresets(const QString& category = QString()) const;
    QVector<ExportPreset> getPresetsByFormat(const QString& format) const;

    QStringList getCategories() const;
    QStringList getFormats() const;

    void setDefaultPreset(const QString& presetId);
    QString getDefaultPresetId() const { return m_defaultPresetId; }
    ExportPreset getDefaultPreset() const;

    QString createPreset(const QString& name, const QString& category,
                    const QString& format);
    void deletePreset(const QString& presetId);
    void duplicatePreset(const QString& presetId, const QString& newName);

    bool exportPresetToFile(const QString& presetId, const QString& path) const;
    bool importPresetFromFile(const QString& path);

signals:
    void presetAdded(const QString& presetId);
    void presetRemoved(const QString& presetId);
    void presetUpdated(const QString& presetId);
    void defaultPresetChanged(const QString& presetId);

private:
    void buildBuiltins();
    void createDefaultPresets();

    static ExportPresets* s_instance;

    QMap<QString, ExportPreset> m_presets;
    QMap<QString, QStringList> m_categories;
    QString m_defaultPresetId;
    QString m_nextPresetId;
};

class ExportManager : public QObject
{
    Q_OBJECT

public:
    explicit ExportManager(QObject* parent = nullptr);
    ~ExportManager();

    void setPresets(ExportPresets* presets);

    void setOutputDirectory(const QString& dir);
    QString getOutputDirectory() const { return m_outputDir; }

    void setNamingPattern(const QString& pattern);
    QString getNamingPattern() const { return m_namingPattern; }

    bool exportFile(const QString& inputPath, const QString& presetId);
    bool exportFile(const QString& inputPath, const ExportPreset& preset);
    bool exportFile(const QString& inputPath, const QString& format, const QJsonObject& params);

    bool exportBatch(const QVector<QString>& inputPaths, const QString& presetId,
                 bool separateFolders = false);

    bool exportWithProgress(const QString& inputPath, const QString& presetId,
                      std::function<void(float)> progressCallback);

    QString generateOutputPath(const QString& inputPath, const QString& format,
                         const QJsonObject& params) const;

signals:
    void exportStarted(const QString& inputPath);
    void exportProgress(float progress);
    void exportCompleted(const QString& outputPath);
    void exportError(const QString& error);

private:
    QString getUniqueOutputPath(const QString& path) const;

    ExportPresets* m_presets = nullptr;
    QString m_outputDir;
    QString m_namingPattern = "{name}.{format}";
};

class ImportPresets : public QObject
{
    Q_OBJECT

public:
    explicit ImportPresets(QObject* parent = nullptr);
    ~ImportPresets();

    struct ImportPreset {
        QString id;
        QString name;
        QStringList extensions;
        QJsonObject parameters;
    };

    void registerPreset(const ImportPreset& preset);
    void unregisterPreset(const QString& presetId);

    QVector<ImportPreset> getPresets(const QString& extension) const;
    ImportPreset getPreset(const QString& presetId) const;

    bool importFile(const QString& path, const QString& presetId,
                  QJsonObject& result);

signals:
    void importStarted();
    void importCompleted();
    void importError(const QString& error);

private:
    QMap<QString, ImportPreset> m_presets;
};

class FormatOptimizer : public QObject
{
    Q_OBJECT

public:
    explicit FormatOptimizer(QObject* parent = nullptr);
    ~FormatOptimizer();

    enum class OptimizationLevel {
        None,
        Fast,
        Normal,
        High,
        Aggressive
    };

    void setOptimizationLevel(OptimizationLevel level);
    OptimizationLevel getOptimizationLevel() const { return m_level; }

    struct MeshStats {
        int vertices;
        int faces;
        int materials;
        int textures;
        qint64 estimatedSize;
    };

    struct OptimizationResult {
        int removedVertices;
        int removedFaces;
        int removedMaterials;
        int removedTextures;
        qint64 originalSize;
        qint64 optimizedSize;
        float time;
    };

    MeshStats analyzeMesh(const QString& path);
    OptimizationResult optimizeMesh(const QString& inputPath, const QString& outputPath,
                                 OptimizationLevel level = OptimizationLevel::Normal);

    bool validateMesh(const QString& path, QString& error);
    QVector<QString> validateBatch(const QVector<QString>& paths);

signals:
    void optimizationProgress(float progress);

private:
    OptimizationLevel m_level = OptimizationLevel::Normal;
};

} // namespace ks