#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QDir>

namespace ks { namespace fileformat {

class KSAudioProject;

// ============================================================================
// KSAudioImporter — high-level import into .ksaudio projects
//
// Supports importing:
//   - Audio files (WAV, OGG, MP3, FLAC, AIFF) → events + sound assets
//   - FMOD .bank files → events, buses, VCAs, sounds
//   - Directories (recursive scan for audio files)
//   - Multiple files in batch
// ============================================================================

class KSAudioImporter : public QObject {
    Q_OBJECT
public:
    explicit KSAudioImporter(QObject* parent = nullptr);

    enum ImportMode {
        ImportAsNewBank,       // Create a new bank for the imported files
        ImportIntoBank,        // Add to an existing bank
        ImportAsEventGroup     // Create an event group hierarchy
    };

    struct ImportOptions {
        ImportMode mode = ImportAsNewBank;
        QString targetBank;        // Used with ImportIntoBank
        QString eventGroup;        // Parent event group name (for ImportAsEventGroup)
        bool copyAssets = true;    // Copy audio files to project Assets/audio/
        bool detectLoopPoints = true;
        int targetSampleRate = 0;  // 0 = keep original
        int targetChannels = 0;    // 0 = keep original
    };

    struct ImportedEvent {
        QString guid;
        QString name;
        QString audioFile;
        QString bankName;
        int sampleRate = 44100;
        int channels = 2;
        double durationMs = 0.0;
    };

    // Import audio files into a .ksaudio project
    bool importAudioFiles(const QString& projectPath,
                          const QStringList& audioFiles,
                          const ImportOptions& options = {});

    // Import a single audio file
    ImportedEvent importAudioFile(const QString& projectPath,
                                  const QString& audioFile,
                                  const ImportOptions& options = {});

    // Import an FMOD .bank file into a .ksaudio project
    bool importBank(const QString& projectPath,
                    const QString& bankPath,
                    const ImportOptions& options = {});

    // Import all audio files from a directory
    bool importDirectory(const QString& projectPath,
                         const QString& dirPath,
                         const ImportOptions& options = {},
                         bool recursive = true);

    // Batch import: import multiple sources at once
    struct ImportSource {
        enum Type { AudioFile, BankFile, Directory };
        Type type;
        QString path;
    };
    bool importBatch(const QString& projectPath,
                     const QVector<ImportSource>& sources,
                     const ImportOptions& options = {});

    // Create a new .ksaudio project from scratch
    bool createProject(const QString& projectPath,
                       const QString& projectName = "Untitled");

    // Get the list of supported import formats
    static QStringList supportedAudioExtensions();
    static QStringList supportedImportExtensions();

    // Results
    QVector<ImportedEvent> lastImportedEvents() const { return m_importedEvents; }
    QString lastError() const { return m_lastError; }

signals:
    void importStarted(const QString& source);
    void importProgress(int percent);
    void importCompleted(int eventCount);
    void importFailed(const QString& error);

private:
    QString m_lastError;
    QVector<ImportedEvent> m_importedEvents;

    bool addAudioFileToProject(const QString& projectPath,
                                const QString& audioFilePath,
                                const QString& bankName,
                                const QString& eventGroup);

    bool copyAssetToProject(const QString& sourcePath,
                            const QString& projectDir);

    int detectLoopPoint(const QString& wavPath) const;

    QString buildEventName(const QString& filePath) const;

    bool saveProject(const QString& projectPath, const QJsonObject& root);

    QJsonObject loadProject(const QString& projectPath);
};

}} // namespace ks::fileformat
