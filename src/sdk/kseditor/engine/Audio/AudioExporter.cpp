#include "AudioExporter.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>

namespace ks { namespace audio {

// ============================================================================
// ExportChecksum
// ============================================================================

ExportChecksum ExportChecksum::computeForFile(const QString& filePath)
{
    ExportChecksum checksum;
    checksum.filePath = filePath;
    checksum.timestamp = QDateTime::currentMSecsSinceEpoch();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return checksum;
    }

    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha256(QCryptographicHash::Sha256);

    constexpr qint64 chunkSize = 64 * 1024;
    qint64 totalRead = 0;

    while (!file.atEnd()) {
        QByteArray chunk = file.read(chunkSize);
        md5.addData(chunk);
        sha256.addData(chunk);
        totalRead += chunk.size();
    }

    file.close();

    checksum.md5Hash = md5.result().toHex();
    checksum.sha256Hash = sha256.result().toHex();
    checksum.fileSize = totalRead;

    return checksum;
}

ExportChecksum ExportChecksum::computeForData(const QByteArray& data, const QString& name)
{
    ExportChecksum checksum;
    checksum.filePath = name;
    checksum.timestamp = QDateTime::currentMSecsSinceEpoch();
    checksum.fileSize = data.size();

    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha256(QCryptographicHash::Sha256);

    md5.addData(data);
    sha256.addData(data);

    checksum.md5Hash = md5.result().toHex();
    checksum.sha256Hash = sha256.result().toHex();

    return checksum;
}

// ============================================================================
// IExporter
// ============================================================================

bool IExporter::validateRoundTrip(const QString& originalPath,
                                   const QString& reExportedPath) const
{
    ExportChecksum original = ExportChecksum::computeForFile(originalPath);
    ExportChecksum reExported = ExportChecksum::computeForFile(reExportedPath);

    if (!original.isValid() || !reExported.isValid()) {
        qWarning() << "Round-trip validation: failed to compute checksums";
        return false;
    }

    bool match = (original == reExported);
    if (!match) {
        qWarning() << "Round-trip validation FAILED for" << formatName();
        qWarning() << "  Original MD5:" << original.md5Hash;
        qWarning() << "  Re-exported MD5:" << reExported.md5Hash;
    }
    return match;
}

// ============================================================================
// ExporterRegistry
// ============================================================================

ExporterRegistry* ExporterRegistry::instance()
{
    static ExporterRegistry s_instance;
    return &s_instance;
}

ExporterRegistry::ExporterRegistry(QObject* parent)
    : QObject(parent) {}

void ExporterRegistry::registerExporter(IExporter* exporter)
{
    if (!exporter) return;
    QString name = exporter->formatName();
    m_exporters[name] = exporter;
    emit exporterRegistered(name);
    qInfo() << "Exporter registered:" << name;
}

IExporter* ExporterRegistry::getExporter(const QString& formatName) const
{
    return m_exporters.value(formatName, nullptr);
}

QStringList ExporterRegistry::availableFormats() const
{
    return m_exporters.keys();
}

QVector<IExporter*> ExporterRegistry::allExporters() const
{
    QVector<IExporter*> result;
    for (auto* exporter : m_exporters) {
        result.append(exporter);
    }
    return result;
}

ExportResult ExporterRegistry::exportWith(const QString& formatName,
                                           const QString& projectPath,
                                           const QString& outputPath,
                                           const QJsonObject& options)
{
    IExporter* exporter = getExporter(formatName);
    if (!exporter) {
        return ExportResult::failure("Unknown export format: " + formatName);
    }

    qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    ExportResult result = exporter->exportProject(projectPath, outputPath, options);
    result.exportTimeMs = QDateTime::currentMSecsSinceEpoch() - startTime;
    result.format = formatName;

    emit exportCompleted(formatName, result.success);
    return result;
}

ExporterRegistry::RoundTripResult ExporterRegistry::validateAllRoundTrips(
    const QString& projectPath, const QString& outputDir)
{
    RoundTripResult result;

    for (auto it = m_exporters.constBegin(); it != m_exporters.constEnd(); ++it) {
        IExporter* exporter = it.value();
        QString ext = exporter->formatExtension();
        QString tempPath = outputDir + "/roundtrip_test." + ext;

        // Export
        ExportResult exportResult = exporter->exportProject(projectPath, tempPath);
        if (!exportResult.success) {
            result.formatResults.append(qMakePair(it.key(), false));
            result.allPassed = false;
            continue;
        }

        // Validate
        bool passed = exporter->validateRoundTrip(projectPath, tempPath);
        result.formatResults.append(qMakePair(it.key(), passed));
        if (!passed) result.allPassed = false;

        // Clean up temp file
        QFile::remove(tempPath);
    }

    return result;
}

// ============================================================================
// GameAdapterRegistry
// ============================================================================

GameAdapterRegistry* GameAdapterRegistry::instance()
{
    static GameAdapterRegistry s_instance;
    return &s_instance;
}

GameAdapterRegistry::GameAdapterRegistry(QObject* parent)
    : QObject(parent) {}

void GameAdapterRegistry::registerAdapter(IGameAdapter* adapter)
{
    if (!adapter) return;
    GameAdapterInfo info = adapter->adapterInfo();
    m_adaptersByName[info.target] = adapter;
    emit adapterRegistered(info.name);
    qInfo() << "Game adapter registered:" << info.name;
}

IGameAdapter* GameAdapterRegistry::getAdapter(GameTarget target) const
{
    return m_adaptersByName.value(target, nullptr);
}

IGameAdapter* GameAdapterRegistry::getAdapter(const QString& name) const
{
    for (auto* adapter : m_adaptersByName) {
        if (adapter->adapterInfo().name == name) return adapter;
    }
    return nullptr;
}

QStringList GameAdapterRegistry::availableAdapters() const
{
    QStringList names;
    for (auto* adapter : m_adaptersByName) {
        names.append(adapter->adapterInfo().name);
    }
    return names;
}

IGameAdapter* GameAdapterRegistry::detectAdapter(const QString& projectPath) const
{
    // Auto-detect based on project content
    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly)) return nullptr;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject root = doc.object();
    QString format = root["format"].toString();

    if (format.contains("fmod.fspro.1.08.12")) {
        // FMOD 1.08 → Assetto Corsa
        return getAdapter(GameTarget::AssettoCorsa);
    }

    // Default: return first available adapter
    if (!m_adaptersByName.isEmpty()) {
        return m_adaptersByName.constBegin().value();
    }

    return nullptr;
}

}} // namespace ks::audio
