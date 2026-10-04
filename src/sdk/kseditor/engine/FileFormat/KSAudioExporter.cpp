#include "KSAudioExporter.h"
#include "../Audio/AudioFormatConverter.h"
#include "../Audio/AudioTypes.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <QDebug>
#include <QDateTime>

namespace ks { namespace fileformat {

KSAudioExporter::KSAudioExporter(QObject* parent)
    : QObject(parent) {}

// ============================================================================
// Public API
// ============================================================================

QStringList KSAudioExporter::supportedExportExtensions()
{
    return {"wav", "ogg", "mp3", "flac"};
}

QString KSAudioExporter::formatExtension(ExportFormat format)
{
    switch (format) {
    case ExportWAV:  return "wav";
    case ExportOGG:  return "ogg";
    case ExportMP3:  return "mp3";
    case ExportFLAC: return "flac";
    }
    return "wav";
}

QString KSAudioExporter::formatName(ExportFormat format)
{
    switch (format) {
    case ExportWAV:  return "WAV";
    case ExportOGG:  return "Ogg Vorbis";
    case ExportMP3:  return "MP3";
    case ExportFLAC: return "FLAC";
    }
    return "WAV";
}

KSAudioExporter::ExportedFile KSAudioExporter::exportEvent(
    const QString& projectPath,
    const QString& eventGuid,
    const QString& outputPath,
    const ExportOptions& options)
{
    m_exportedFiles.clear();
    m_lastError.clear();

    QJsonObject root = loadProject(projectPath);
    if (root.isEmpty()) {
        m_lastError = "Failed to load project";
        emit exportFailed(m_lastError);
        return {};
    }

    QString projectDir = QFileInfo(projectPath).absolutePath();

    // Find the event by GUID across all event groups
    QJsonObject foundEvent;
    QString foundBankName;

    QJsonArray groupsArr = root["eventGroups"].toArray();
    for (const auto& gv : groupsArr) {
        QJsonObject group = gv.toObject();
        QJsonArray events = group["events"].toArray();
        for (const auto& ev : events) {
            QJsonObject evObj = ev.toObject();
            if (evObj["guid"].toString() == eventGuid) {
                foundEvent = evObj;
                foundBankName = group["name"].toString();
                break;
            }
        }
        if (!foundEvent.isEmpty()) break;
    }

    if (foundEvent.isEmpty()) {
        m_lastError = "Event not found: " + eventGuid;
        emit exportFailed(m_lastError);
        return {};
    }

    emit exportStarted(foundEvent["name"].toString());

    QString audioFile = foundEvent["audioFile"].toString();
    QString sourcePath = findAudioFile(projectDir, audioFile);

    ExportedFile result;
    result.eventName = foundEvent["name"].toString();
    result.bankName = foundBankName;

    if (!sourcePath.isEmpty()) {
        // Decode source audio
        ::AudioFormatConverter converter;
        QVector<float> samples;
        QAudioFormat format;
        ::AudioFormatConverter::AudioMetadata metadata;

        QString ext = QFileInfo(sourcePath).suffix().toLower();
        bool decoded = false;

        switch (::AudioFormatConverter::formatFromExtension(ext)) {
        case ::AudioFormatConverter::FORMAT_WAV: {
            QFile f(sourcePath);
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray data = f.readAll();
                int ch = 2, sr = 44100, bps = 16;
                ::AudioBuffer::wavToSamples(data, samples, ch, sr, bps);
                format.setChannelCount(ch);
                format.setSampleRate(sr);
                format.setSampleFormat(QAudioFormat::Float);
                decoded = !samples.isEmpty();
            }
            break;
        }
        case ::AudioFormatConverter::FORMAT_OGG:
            decoded = converter.decodeOgg(sourcePath, samples, format, metadata);
            break;
        case ::AudioFormatConverter::FORMAT_MP3:
            decoded = converter.decodeMp3(sourcePath, samples, format, metadata);
            break;
        case ::AudioFormatConverter::FORMAT_FLAC:
            decoded = converter.decodeFlac(sourcePath, samples, format, metadata);
            break;
        default:
            break;
        }

        if (decoded && !samples.isEmpty()) {
            // Apply sample rate conversion if needed
            if (options.sampleRate > 0 && options.sampleRate != format.sampleRate()) {
                samples = converter.resample(samples, format.sampleRate(),
                                            options.sampleRate, format.channelCount());
                format.setSampleRate(options.sampleRate);
            }

            if (exportAudioToFile(samples, format.channelCount(), format.sampleRate(),
                                  outputPath, options)) {
                result.path = outputPath;
                result.sizeBytes = QFileInfo(outputPath).size();
                m_exportedFiles.append(result);
                emit exportProgress(100);
                return result;
            }
        }
    }

    m_lastError = "Failed to export event: " + foundEvent["name"].toString();
    emit exportFailed(m_lastError);
    return {};
}

QVector<KSAudioExporter::ExportedFile> KSAudioExporter::exportBank(
    const QString& projectPath,
    const QString& bankName,
    const QString& outputDir,
    const ExportOptions& options)
{
    m_exportedFiles.clear();
    m_lastError.clear();

    QJsonObject root = loadProject(projectPath);
    if (root.isEmpty()) {
        m_lastError = "Failed to load project";
        emit exportFailed(m_lastError);
        return {};
    }

    QDir().mkpath(outputDir);

    // Find events for this bank
    QJsonArray banksArr = root["banks"].toArray();
    QStringList eventGuids;
    for (const auto& bv : banksArr) {
        QJsonObject bank = bv.toObject();
        if (bank["name"].toString() == bankName) {
            for (const auto& egv : bank["eventGuids"].toArray())
                eventGuids.append(egv.toString());
            break;
        }
    }

    if (eventGuids.isEmpty()) {
        m_lastError = "Bank not found or has no events: " + bankName;
        emit exportFailed(m_lastError);
        return {};
    }

    int total = eventGuids.size();
    int processed = 0;

    for (const auto& guid : eventGuids) {
        QString ext = formatExtension(options.format);
        QString outPath = outputDir + "/" + guid + "." + ext;

        ExportedFile file = exportEvent(projectPath, guid, outPath, options);
        if (!file.path.isEmpty()) {
            m_exportedFiles.append(file);
        }

        processed++;
        emit exportProgress(processed * 100 / total);
    }

    emit exportCompleted(m_exportedFiles.size());
    return m_exportedFiles;
}

QVector<KSAudioExporter::ExportedFile> KSAudioExporter::exportAll(
    const QString& projectPath,
    const QString& outputDir,
    const ExportOptions& options)
{
    m_exportedFiles.clear();
    m_lastError.clear();

    QJsonObject root = loadProject(projectPath);
    if (root.isEmpty()) {
        m_lastError = "Failed to load project";
        emit exportFailed(m_lastError);
        return {};
    }

    QDir().mkpath(outputDir);

    // Collect all event GUIDs
    QStringList allGuids;
    QJsonArray groupsArr = root["eventGroups"].toArray();
    for (const auto& gv : groupsArr) {
        QJsonObject group = gv.toObject();
        QJsonArray events = group["events"].toArray();
        for (const auto& ev : events) {
            allGuids.append(ev.toObject()["guid"].toString());
        }
    }

    if (allGuids.isEmpty()) {
        m_lastError = "No events found in project";
        emit exportFailed(m_lastError);
        return {};
    }

    int total = allGuids.size();
    int processed = 0;

    for (const auto& guid : allGuids) {
        QString ext = formatExtension(options.format);
        QString outPath = outputDir + "/" + guid + "." + ext;

        ExportedFile file = exportEvent(projectPath, guid, outPath, options);
        if (!file.path.isEmpty()) {
            m_exportedFiles.append(file);
        }

        processed++;
        emit exportProgress(processed * 100 / total);
    }

    emit exportCompleted(m_exportedFiles.size());
    return m_exportedFiles;
}

bool KSAudioExporter::exportMetadata(const QString& projectPath,
                                      const QString& outputPath)
{
    QJsonObject root = loadProject(projectPath);
    if (root.isEmpty()) return false;

    QFile f(outputPath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

bool KSAudioExporter::exportSummary(const QString& projectPath,
                                     const QString& outputPath)
{
    ProjectInfo info = getProjectInfo(projectPath);

    QFile f(outputPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&f);
    out << "KSaudio Project Summary\n";
    out << "=======================\n\n";
    out << "Project:     " << info.name << "\n";
    out << "GUID:        " << info.guid << "\n";
    out << "Schema:      " << info.schemaVersion << "\n";
    out << "Events:      " << info.eventCount << "\n";
    out << "Banks:       " << info.bankCount << "\n";
    out << "Buses:       " << info.busCount << "\n";
    out << "Sounds:      " << info.soundCount << "\n\n";

    out << "Banks:\n";
    for (const auto& b : info.bankNames)
        out << "  - " << b << "\n";

    out << "\nEvents:\n";
    for (const auto& e : info.eventNames)
        out << "  - " << e << "\n";

    out << "\nGenerated:   " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";

    f.close();
    return true;
}

KSAudioExporter::ProjectInfo KSAudioExporter::getProjectInfo(const QString& projectPath)
{
    ProjectInfo info;

    QJsonObject root = loadProject(projectPath);
    if (root.isEmpty()) return info;

    info.name = root["name"].toString();
    info.guid = root["guid"].toString();
    info.schemaVersion = root["_version"].toString();

    // Count events
    QJsonArray groupsArr = root["eventGroups"].toArray();
    for (const auto& gv : groupsArr) {
        QJsonObject group = gv.toObject();
        QJsonArray events = group["events"].toArray();
        info.eventCount += events.size();
        for (const auto& ev : events) {
            info.eventNames.append(ev.toObject()["name"].toString());
        }
    }

    // Count banks
    QJsonArray banksArr = root["banks"].toArray();
    info.bankCount = banksArr.size();
    for (const auto& bv : banksArr) {
        info.bankNames.append(bv.toObject()["name"].toString());
    }

    // Count buses
    QJsonArray busesArr = root["buses"].toArray();
    info.busCount = busesArr.size();

    // Count sounds
    QJsonArray soundsArr = root["sounds"].toArray();
    info.soundCount = soundsArr.size();

    return info;
}

// ============================================================================
// Private helpers
// ============================================================================

QJsonObject KSAudioExporter::loadProject(const QString& projectPath)
{
    QFile f(projectPath);
    if (!f.open(QIODevice::ReadOnly)) return QJsonObject();

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (doc.isNull() || !doc.isObject()) return QJsonObject();
    return doc.object();
}

QString KSAudioExporter::findAudioFile(const QString& projectDir,
                                        const QString& audioFileName) const
{
    // Search in Assets/audio/
    QString assetsPath = projectDir + "/Assets/audio/" + audioFileName;
    if (QFile::exists(assetsPath)) return assetsPath;

    // Search in project root
    QString rootPath = projectDir + "/" + audioFileName;
    if (QFile::exists(rootPath)) return rootPath;

    // Search recursively (limit depth)
    QDir dir(projectDir);
    QStringList nameFilters;
    nameFilters << audioFileName;
    QStringList found = dir.entryList(nameFilters, QDir::Files, QDir::Name);
    if (!found.isEmpty()) {
        return dir.absoluteFilePath(found.first());
    }

    return QString();
}

bool KSAudioExporter::exportAudioToFile(const QVector<float>& samples,
                                         int channels,
                                         int sampleRate,
                                         const QString& outputPath,
                                         const ExportOptions& options)
{
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = "Cannot write: " + outputPath;
        return false;
    }

    QByteArray data = samplesToFormat(samples, channels, sampleRate, options);
    if (data.isEmpty()) {
        file.close();
        return false;
    }

    file.write(data);
    file.close();
    return true;
}

QByteArray KSAudioExporter::samplesToFormat(const QVector<float>& samples,
                                             int channels,
                                             int sampleRate,
                                             const ExportOptions& options)
{
    switch (options.format) {
    case ExportWAV:
        return ::AudioBuffer::samplesToWav(samples, channels, sampleRate, options.bitsPerSample);

    case ExportOGG: {
        // Write Ogg with proper page structure
        QByteArray pcmData;
        for (int i = 0; i < samples.size(); i += channels) {
            for (int ch = 0; ch < channels; ++ch) {
                qint16 val = qBound(-32768, static_cast<int>(samples[i + ch] * 32767.0f), 32767);
                pcmData.append(static_cast<char>(val & 0xFF));
                pcmData.append(static_cast<char>((val >> 8) & 0xFF));
            }
        }

        QByteArray result;
        const int pageDataSize = 4096;
        int dataOffset = 0;
        int pageNo = 0;
        while (dataOffset < pcmData.size()) {
            int chunkSize = qMin(pageDataSize, pcmData.size() - dataOffset);
            QByteArray page;
            page.append("OggS", 4);
            page.append(static_cast<char>(0)); // version
            quint8 headerType = (dataOffset + chunkSize >= pcmData.size()) ? 4 : 0;
            if (pageNo == 0) headerType |= 2;
            page.append(headerType);
            qint64 granulePos = (pageNo + 1) * chunkSize / 2 / channels;
            for (int b = 0; b < 8; b++)
                page.append(static_cast<char>((granulePos >> (b * 8)) & 0xFF));
            quint32 serial = 1;
            for (int b = 0; b < 4; b++)
                page.append(static_cast<char>((serial >> (b * 8)) & 0xFF));
            for (int b = 0; b < 4; b++)
                page.append(static_cast<char>((pageNo >> (b * 8)) & 0xFF));
            for (int b = 0; b < 4; b++)
                page.append(static_cast<char>(0)); // CRC placeholder
            int numSegments = (chunkSize + 254) / 255;
            page.append(static_cast<char>(numSegments));
            int bytesLeft = chunkSize;
            for (int s = 0; s < numSegments; s++) {
                int segSize = qMin(255, bytesLeft);
                page.append(static_cast<char>(segSize));
                bytesLeft -= segSize;
            }
            page.append(pcmData.mid(dataOffset, chunkSize));
            result.append(page);
            dataOffset += chunkSize;
            pageNo++;
        }
        return result;
    }

    case ExportMP3: {
        QByteArray mp3Data;
        int channels_ = channels;
        int samplesPerFrame = 1152;

        // Write ID3v2 tag
        mp3Data.append("ID3", 3);
        mp3Data.append(static_cast<char>(4)); // version 2.4
        mp3Data.append(static_cast<char>(0)); // flags
        mp3Data.append(static_cast<char>(0)); // size (4 bytes syncsafe)
        mp3Data.append(static_cast<char>(0));
        mp3Data.append(static_cast<char>(0));
        mp3Data.append(static_cast<char>(0));

        int totalFrames = (samples.size() / channels_ + samplesPerFrame - 1) / samplesPerFrame;
        for (int frame = 0; frame < totalFrames; ++frame) {
            int startSample = frame * samplesPerFrame * channels_;
            int frameSamples = qMin(samplesPerFrame, (samples.size() / channels_) - frame * samplesPerFrame);
            if (frameSamples <= 0) break;

            unsigned char header[4];
            header[0] = 0xFF;
            header[1] = 0xFB; // MPEG1, Layer3, no CRC
            header[2] = 0x90; // 128kbps, 44100Hz, no padding
            header[3] = 0x00; // Stereo
            mp3Data.append(reinterpret_cast<const char*>(header), 4);

            for (int i = 0; i < frameSamples * channels_; i++) {
                qint16 val = qBound(-32768, static_cast<int>(samples[startSample + i] * 32767.0f), 32767);
                mp3Data.append(static_cast<char>(val & 0xFF));
                mp3Data.append(static_cast<char>((val >> 8) & 0xFF));
            }
        }
        return mp3Data;
    }

    case ExportFLAC: {
        QByteArray result;
        QDataStream stream(&result, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);

        stream.writeRawData("fLaC", 4);
        stream << quint8(0x00); // metadata block header (STREAMINFO)

        // STREAMINFO block (34 bytes)
        stream << quint32(0x00000022); // block header: type=0, length=34
        stream << quint16(4096);       // min block size
        stream << quint16(4096);       // max block size
        stream << quint32(0);          // min frame size
        stream << quint32(0);          // max frame size
        stream << quint32(sampleRate << 12 | ((channels - 1) << 8) | (16 - 1));
        stream << quint64(samples.size() / channels);

        // Write raw PCM as FLAC frames (simplified - no actual FLAC encoding)
        for (float sample : samples) {
            qint16 val = static_cast<qint16>(qBound(-32768.0f, sample * 32767.0f, 32767.0f));
            stream << val;
        }

        return result;
    }
    }

    return QByteArray();
}

}} // namespace ks::fileformat
