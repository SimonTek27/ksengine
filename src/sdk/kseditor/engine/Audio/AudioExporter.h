#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVector>
#include <QMap>
#include <QByteArray>
#include <functional>

namespace ks { namespace audio {

// ============================================================================
// Export checksum — used for round-trip validation
// ============================================================================

struct ExportChecksum {
    QByteArray md5Hash;
    QByteArray sha256Hash;
    qint64 fileSize = 0;
    QString filePath;
    qint64 timestamp = 0;   // QDateTime::currentMSecsSinceEpoch()

    bool isValid() const { return !md5Hash.isEmpty() && !sha256Hash.isEmpty(); }

    static ExportChecksum computeForFile(const QString& filePath);
    static ExportChecksum computeForData(const QByteArray& data, const QString& name);

    bool operator==(const ExportChecksum& other) const {
        return md5Hash == other.md5Hash;
    }
};

// ============================================================================
// Export result
// ============================================================================

struct ExportResult {
    bool success = false;
    QString outputPath;
    QString format;
    ExportChecksum checksum;
    QString errorMessage;
    qint64 exportTimeMs = 0;

    static ExportResult failure(const QString& error) {
        ExportResult r;
        r.success = false;
        r.errorMessage = error;
        return r;
    }
};

// ============================================================================
// IExporter — unified exporter interface
//
// All export targets (FMOD .bank, .fspro, stems, game-specific) implement
// this interface. The checksum system enables round-trip validation:
//   export(original) → checksum1
//   import(exported) → re-export → checksum2
//   checksum1 == checksum2 → lossless round-trip
// ============================================================================

class IExporter {
public:
    virtual ~IExporter() = default;

    virtual QString formatName() const = 0;
    virtual QString formatExtension() const = 0;
    virtual QStringList supportedExtensions() const = 0;

    // Export from a .ksaudio project
    virtual ExportResult exportProject(const QString& projectPath,
                                        const QString& outputPath,
                                        const QJsonObject& options = {}) = 0;

    // Export a single event
    virtual ExportResult exportEvent(const QString& projectPath,
                                      const QString& eventGuid,
                                      const QString& outputPath,
                                      const QJsonObject& options = {}) = 0;

    // Round-trip validation: import → export → compare checksums
    virtual bool validateRoundTrip(const QString& originalPath,
                                    const QString& reExportedPath) const;

    // Get export options/schema for this format
    virtual QJsonObject exportOptionsSchema() const { return {}; }
};

// ============================================================================
// ExporterRegistry — manages all registered exporters
// ============================================================================

class ExporterRegistry : public QObject {
    Q_OBJECT
public:
    static ExporterRegistry* instance();

    void registerExporter(IExporter* exporter);
    IExporter* getExporter(const QString& formatName) const;
    QStringList availableFormats() const;
    QVector<IExporter*> allExporters() const;

    // Export using a named format
    ExportResult exportWith(const QString& formatName,
                            const QString& projectPath,
                            const QString& outputPath,
                            const QJsonObject& options = {});

    // Round-trip validation across all formats
    struct RoundTripResult {
        bool allPassed = true;
        QVector<QPair<QString, bool>> formatResults;  // format name → passed
    };
    RoundTripResult validateAllRoundTrips(const QString& projectPath,
                                           const QString& outputDir);

signals:
    void exporterRegistered(const QString& formatName);
    void exportCompleted(const QString& formatName, bool success);

private:
    explicit ExporterRegistry(QObject* parent = nullptr);
    QMap<QString, IExporter*> m_exporters;
};

// ============================================================================
// GameTarget — per-game adapter interface
// ============================================================================

enum class GameTarget {
    AutoDetect,
    AssettoCorsa,        // AC1
    AssettoCorsaCompetizione,  // ACC
    AssettoCorsaEVO,     // ACE
    AssettoCorsaRally,   // ACR
    Custom
};

struct GameAdapterInfo {
    GameTarget target = GameTarget::Custom;
    QString name;
    QString description;
    QStringList supportedBankFormats;
    int maxEventsPerBank = 256;
    bool supportsSpatialAudio = true;
    bool supportsRTPC = true;
    bool supportsAutomation = true;
    QString bankVersion;  // e.g. "1.08.12"
};

class IGameAdapter {
public:
    virtual ~IGameAdapter() = default;

    virtual GameAdapterInfo adapterInfo() const = 0;

    // Prepare a project for this game target
    virtual bool prepareForGame(const QString& projectPath,
                                 const QString& outputDir,
                                 const QJsonObject& options = {}) = 0;

    // Validate that a project is compatible with this game
    virtual bool validateCompatibility(const QString& projectPath,
                                        QString* errorMessage = nullptr) const = 0;

    // Get game-specific export options
    virtual QJsonObject gameSpecificOptions() const { return {}; }
};

// ============================================================================
// GameAdapterRegistry — manages per-game adapters
// ============================================================================

class GameAdapterRegistry : public QObject {
    Q_OBJECT
public:
    static GameAdapterRegistry* instance();

    void registerAdapter(IGameAdapter* adapter);
    IGameAdapter* getAdapter(GameTarget target) const;
    IGameAdapter* getAdapter(const QString& name) const;
    QStringList availableAdapters() const;

    // Auto-detect the best adapter for a project
    IGameAdapter* detectAdapter(const QString& projectPath) const;

signals:
    void adapterRegistered(const QString& name);

private:
    explicit GameAdapterRegistry(QObject* parent = nullptr);
    QMap<GameTarget, IGameAdapter*> m_adaptersByName;
};

}} // namespace ks::audio
