#include "AudioStudioTypes.h"
#include "AudioFormatConverter.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QDebug>

namespace ks { namespace audio {

// ============================================================================
// AudioManager implementation (declared in AudioStudioTypes.h)
// ============================================================================

bool AudioManager::importAudio(const QString& path, const QString& destDir)
{
    QFileInfo fi(path);
    if (!fi.exists()) {
        qWarning() << "AudioManager: File not found:" << path;
        return false;
    }

    QDir().mkpath(destDir);

    QString ext = fi.suffix().toLower();
    if (!supportedImportFormats().contains(ext)) {
        qWarning() << "AudioManager: Unsupported format:" << ext;
        return false;
    }

    QString targetPath = destDir + "/" + fi.fileName();
    if (QFile::exists(targetPath)) {
        qInfo() << "AudioManager: File already exists:" << targetPath;
        emit audioImported(targetPath);
        return true;
    }

    if (!QFile::copy(path, targetPath)) {
        qWarning() << "AudioManager: Failed to copy" << path << "to" << targetPath;
        return false;
    }

    qInfo() << "AudioManager: Imported" << path << "to" << targetPath;
    emit audioImported(targetPath);
    emit importComplete(true);
    return true;
}

bool AudioManager::exportAudio(const QString& sourcePath, const QString& destPath,
                                const QString& format)
{
    QFileInfo fi(sourcePath);
    if (!fi.exists()) {
        qWarning() << "AudioManager: Source not found:" << sourcePath;
        return false;
    }

    if (!supportedExportFormats().contains(format.toLower())) {
        qWarning() << "AudioManager: Unsupported export format:" << format;
        return false;
    }

    QString sourceExt = fi.suffix().toLower();
    QString targetExt = format.toLower();

    // If same format, just copy
    if (sourceExt == targetExt) {
        if (!QFile::copy(sourcePath, destPath)) {
            qWarning() << "AudioManager: Failed to copy" << sourcePath;
            return false;
        }
        emit exportComplete(true);
        return true;
    }

    // Convert format using global AudioFormatConverter
    ::AudioFormatConverter converter;
    bool success = converter.convert(sourcePath, destPath);
    if (success) {
        qInfo() << "AudioManager: Exported" << sourcePath << "to" << destPath;
        emit exportComplete(true);
    } else {
        qWarning() << "AudioManager: Export failed for" << sourcePath;
        emit exportComplete(false);
    }

    return success;
}

AudioManager::AudioInfo AudioManager::getAudioInfo(const QString& path) const
{
    AudioInfo info;
    info.path = path;

    QFileInfo fi(path);
    if (!fi.exists()) return info;

    info.format = fi.suffix().toLower();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return info;

    QByteArray data = file.readAll();
    file.close();

    // Try to parse as WAV using global AudioBuffer
    if (info.format == "wav") {
        int channels, sampleRate, bitsPerSample;
        QVector<float> samples;
        if (::AudioBuffer::wavToSamples(data, samples, channels, sampleRate, bitsPerSample)) {
            info.sampleRate = sampleRate;
            info.channels = channels;
            info.bitsPerSample = bitsPerSample;
            info.duration = static_cast<double>(samples.size()) / channels / sampleRate;
            return info;
        }
    }

    // For other formats, use global AudioFormatConverter
    ::AudioFormatConverter converter;
    QVector<float> samples;
    QAudioFormat audioFormat;
    ::AudioFormatConverter::AudioMetadata metadata;

    bool decoded = false;
    switch (::AudioFormatConverter::formatFromExtension(info.format)) {
    case ::AudioFormatConverter::FORMAT_OGG:
        decoded = converter.decodeOgg(path, samples, audioFormat, metadata);
        break;
    case ::AudioFormatConverter::FORMAT_MP3:
        decoded = converter.decodeMp3(path, samples, audioFormat, metadata);
        break;
    case ::AudioFormatConverter::FORMAT_FLAC:
        decoded = converter.decodeFlac(path, samples, audioFormat, metadata);
        break;
    default:
        break;
    }

    if (decoded) {
        info.sampleRate = audioFormat.sampleRate();
        info.channels = audioFormat.channelCount();
        info.duration = metadata.durationMs / 1000.0;
    }

    return info;
}

}} // namespace ks::audio
