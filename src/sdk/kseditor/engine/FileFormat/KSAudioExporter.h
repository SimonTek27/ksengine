#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

namespace ks { namespace fileformat {

// ============================================================================
// KSAudioExporter — export from .ksaudio projects
//
// Supports exporting:
//   - Individual events as WAV/OGG/MP3/FLAC stems
//   - All events in a bank as stems
//   - All events in the project as stems
//   - The full mix as a single file
//   - Project metadata as JSON/XML
//   - FMOD .bank files (delegates to KSBankWriter)
// ============================================================================

class KSAudioExporter : public QObject {
    Q_OBJECT
public:
    explicit KSAudioExporter(QObject* parent = nullptr);

    enum ExportFormat {
        ExportWAV,
        ExportOGG,
        ExportMP3,
        ExportFLAC
    };

    struct ExportOptions {
        ExportFormat format = ExportWAV;
        int sampleRate = 44100;
        int channels = 2;
        int bitsPerSample = 16;
        bool normalize = false;
        bool includeMetadata = true;
    };

    struct ExportedFile {
        QString path;
        QString eventName;
        QString bankName;
        qint64 sizeBytes = 0;
    };

    // Export a single event's audio as a stem file
    ExportedFile exportEvent(const QString& projectPath,
                             const QString& eventGuid,
                             const QString& outputPath,
                             const ExportOptions& options = {});

    // Export all events in a bank as stem files
    QVector<ExportedFile> exportBank(const QString& projectPath,
                                     const QString& bankName,
                                     const QString& outputDir,
                                     const ExportOptions& options = {});

    // Export all events in the project as stem files
    QVector<ExportedFile> exportAll(const QString& projectPath,
                                    const QString& outputDir,
                                    const ExportOptions& options = {});

    // Export project metadata as JSON
    bool exportMetadata(const QString& projectPath,
                        const QString& outputPath);

    // Export project summary as human-readable text
    bool exportSummary(const QString& projectPath,
                       const QString& outputPath);

    // Get project info without full export
    struct ProjectInfo {
        QString name;
        QString guid;
        QString schemaVersion;
        int eventCount = 0;
        int bankCount = 0;
        int busCount = 0;
        int soundCount = 0;
        QStringList eventNames;
        QStringList bankNames;
    };

    ProjectInfo getProjectInfo(const QString& projectPath);

    // Supported export formats
    static QStringList supportedExportExtensions();
    static QString formatExtension(ExportFormat format);
    static QString formatName(ExportFormat format);

    // Results
    QVector<ExportedFile> lastExportedFiles() const { return m_exportedFiles; }
    QString lastError() const { return m_lastError; }

signals:
    void exportStarted(const QString& eventName);
    void exportProgress(int percent);
    void exportCompleted(int fileCount);
    void exportFailed(const QString& error);

private:
    QString m_lastError;
    QVector<ExportedFile> m_exportedFiles;

    QJsonObject loadProject(const QString& projectPath);

    QString findAudioFile(const QString& projectDir,
                          const QString& audioFileName) const;

    bool exportAudioToFile(const QVector<float>& samples,
                           int channels,
                           int sampleRate,
                           const QString& outputPath,
                           const ExportOptions& options);

    QByteArray samplesToFormat(const QVector<float>& samples,
                               int channels,
                               int sampleRate,
                               const ExportOptions& options);
};

}} // namespace ks::fileformat
