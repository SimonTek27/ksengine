#include "KSAudioImporter.h"
#include "../Audio/AudioTypes.h"
#include "../Audio/AudioFormatConverter.h"
#include "BankParser.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QDataStream>
#include <QDebug>
#include <QTextStream>
#include <QRegularExpression>
#include <algorithm>

namespace ks { namespace fileformat {

KSAudioImporter::KSAudioImporter(QObject* parent)
    : QObject(parent) {}

// ============================================================================
// Public API
// ============================================================================

QStringList KSAudioImporter::supportedAudioExtensions()
{
    return {"wav", "ogg", "mp3", "flac", "aiff"};
}

QStringList KSAudioImporter::supportedImportExtensions()
{
    return {"wav", "ogg", "mp3", "flac", "aiff", "bank"};
}

bool KSAudioImporter::createProject(const QString& projectPath,
                                     const QString& projectName)
{
    QJsonObject root;
    root["_schema"]  = QStringLiteral("ksaudio");
    root["_version"] = QStringLiteral("2.0.0");
    root["name"]     = projectName;
    root["guid"]     = QUuid::createUuid().toString(QUuid::WithoutBraces);
    root["format"]   = QStringLiteral("fmod.fspro.1.08.12");

    QJsonArray emptyArr;
    root["eventGroups"] = emptyArr;
    root["buses"] = emptyArr;
    root["banks"] = emptyArr;

    return saveProject(projectPath, root);
}

bool KSAudioImporter::importAudioFiles(const QString& projectPath,
                                        const QStringList& audioFiles,
                                        const ImportOptions& options)
{
    m_importedEvents.clear();
    m_lastError.clear();

    if (audioFiles.isEmpty()) {
        m_lastError = "No files to import";
        emit importFailed(m_lastError);
        return false;
    }

    emit importStarted(audioFiles.first());

    QJsonObject root = loadProject(projectPath);
    if (root.isEmpty()) {
        m_lastError = "Failed to load project: " + projectPath;
        emit importFailed(m_lastError);
        return false;
    }

    QString projectDir = QFileInfo(projectPath).absolutePath();
    QString bankName = options.targetBank.isEmpty()
        ? "Imported Audio"
        : options.targetBank;

    if (options.mode == ImportIntoBank && !options.targetBank.isEmpty()) {
        bankName = options.targetBank;
    }

    int total = audioFiles.size();
    int processed = 0;

    for (const QString& audioFile : audioFiles) {
        if (!addAudioFileToProject(projectPath, audioFile, bankName, options.eventGroup)) {
            qWarning() << "Failed to import:" << audioFile;
        }
        processed++;
        emit importProgress(processed * 100 / total);
    }

    // Reload the project with updated data
    root = loadProject(projectPath);

    if (!saveProject(projectPath, root)) {
        m_lastError = "Failed to save project";
        emit importFailed(m_lastError);
        return false;
    }

    emit importCompleted(m_importedEvents.size());
    return true;
}

KSAudioImporter::ImportedEvent KSAudioImporter::importAudioFile(
    const QString& projectPath,
    const QString& audioFile,
    const ImportOptions& options)
{
    m_importedEvents.clear();
    m_lastError.clear();

    addAudioFileToProject(projectPath, audioFile, options.targetBank.isEmpty() ? "Imported Audio" : options.targetBank, options.eventGroup);

    if (!m_importedEvents.isEmpty()) {
        QJsonObject root = loadProject(projectPath);
        saveProject(projectPath, root);
    }

    return m_importedEvents.isEmpty() ? ImportedEvent{} : m_importedEvents.first();
}

bool KSAudioImporter::importBank(const QString& projectPath,
                                  const QString& bankPath,
                                  const ImportOptions& options)
{
    m_importedEvents.clear();
    m_lastError.clear();

    emit importStarted(bankPath);

    KSBankParser parser;
    ParsedBankData data = parser.parse(bankPath);
    if (!data.isValid) {
        m_lastError = "Failed to parse bank: Invalid bank format";
        emit importFailed(m_lastError);
        return false;
    }

    QJsonObject root = loadProject(projectPath);
    if (root.isEmpty()) {
        m_lastError = "Failed to load project: " + projectPath;
        emit importFailed(m_lastError);
        return false;
    }

    QString projectDir = QFileInfo(projectPath).absolutePath();
    QString bankName = data.name.isEmpty()
        ? QFileInfo(bankPath).baseName()
        : data.name;

    // Create assets directory
    QString assetsDir = projectDir + "/Assets/audio";
    QDir().mkpath(assetsDir);

    // Build event group from bank events
    QJsonArray eventsArr;
    QJsonArray bankEventGuids;

    for (const auto& ev : data.events) {
        QJsonObject evObj;
        evObj["guid"] = ev.guid.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : ev.guid;
        evObj["name"] = ev.name;
        evObj["type"] = QStringLiteral("2D");
        evObj["audioFile"] = ev.name;
        evObj["volume"] = 1.0;
        evObj["pitch"] = 1.0;
        evObj["loop"] = false;
        evObj["maxInstances"] = static_cast<int>(ev.maxInstances);
        evObj["priority"] = 0;

        // Parameters
        if (!ev.parameterNames.isEmpty()) {
            QJsonArray paramsArr;
            for (int i = 0; i < ev.parameterNames.size(); ++i) {
                QJsonObject p;
                p["name"] = ev.parameterNames[i];
                p["type"] = "float";
                p["scope"] = "local";
                if (i < ev.parameterDefaults.size())
                    p["default"] = static_cast<double>(ev.parameterDefaults[i]);
                paramsArr.append(p);
            }
            evObj["parameters"] = paramsArr;
        }

        eventsArr.append(evObj);
        bankEventGuids.append(evObj["guid"]);

        ImportedEvent ie;
        ie.guid = evObj["guid"].toString();
        ie.name = ev.name;
        ie.audioFile = ev.name;
        ie.bankName = bankName;
        m_importedEvents.append(ie);
    }

    // Create event group
    QJsonObject eventGroup;
    eventGroup["guid"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    eventGroup["name"] = bankName;
    eventGroup["events"] = eventsArr;

    // Append to existing eventGroups
    QJsonArray groupsArr = root["eventGroups"].toArray();
    groupsArr.append(eventGroup);
    root["eventGroups"] = groupsArr;

    // Create bank
    QJsonObject bankObj;
    bankObj["guid"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bankObj["name"] = bankName;
    bankObj["eventGuids"] = bankEventGuids;

    QJsonArray banksArr = root["banks"].toArray();
    banksArr.append(bankObj);
    root["banks"] = banksArr;

    // Import buses
    if (!data.buses.isEmpty()) {
        QJsonArray busesArr = root["buses"].toArray();
        for (const auto& bus : data.buses) {
            QJsonObject busObj;
            busObj["guid"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            busObj["name"] = bus.name;
            busObj["volume"] = static_cast<double>(bus.volume);
            busObj["mute"] = bus.muted;
            busObj["solo"] = bus.solo;
            busesArr.append(busObj);
        }
        root["buses"] = busesArr;
    }

    // Import VCAs
    if (!data.vcas.isEmpty()) {
        QJsonArray vcasArr = root["vcas"].toArray();
        for (const auto& vca : data.vcas) {
            QJsonObject vcaObj;
            vcaObj["guid"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            vcaObj["name"] = vca.name;
            vcaObj["volume"] = static_cast<double>(vca.volume);
            vcasArr.append(vcaObj);
        }
        root["vcas"] = vcasArr;
    }

    // Import sound data as sounds section
    if (!data.sounds.isEmpty()) {
        QJsonArray soundsArr = root["sounds"].toArray();
        for (const auto& snd : data.sounds) {
            QJsonObject sndObj;
            sndObj["name"] = snd.name;
            sndObj["sampleRate"] = static_cast<int>(snd.sampleRate);
            sndObj["channels"] = static_cast<int>(snd.channels);
            sndObj["format"] = (snd.format == 6) ? "vorbis" : "pcm";
            soundsArr.append(sndObj);
        }
        root["sounds"] = soundsArr;
    }

    // Copy decoded audio samples as WAV files
    for (const auto& snd : data.sounds) {
        if (snd.hasAudioData && !snd.samples.isEmpty()) {
            QString wavPath = assetsDir + "/" + snd.name + ".wav";
            QFile wavFile(wavPath);
            if (wavFile.open(QIODevice::WriteOnly)) {
                QByteArray wavData = AudioBuffer::samplesToWav(
                    snd.samples, snd.channels, snd.sampleRate, 16);
                wavFile.write(wavData);
                wavFile.close();
                qInfo() << "Exported sound from bank:" << wavPath;
            }
        }
    }

    if (!saveProject(projectPath, root)) {
        m_lastError = "Failed to save project";
        emit importFailed(m_lastError);
        return false;
    }

    emit importCompleted(m_importedEvents.size());
    return true;
}

bool KSAudioImporter::importDirectory(const QString& projectPath,
                                       const QString& dirPath,
                                       const ImportOptions& options,
                                       bool recursive)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        m_lastError = "Directory not found: " + dirPath;
        emit importFailed(m_lastError);
        return false;
    }

    QStringList nameFilters;
    for (const auto& ext : supportedAudioExtensions())
        nameFilters << "*." + ext;

    QDir::Filters filters = QDir::Files;
    if (recursive) filters |= QDir::Dirs;

    QStringList files = dir.entryList(nameFilters, filters, QDir::Name);

    // Also recurse into subdirectories
    if (recursive) {
        QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto& subDir : dirs) {
            QStringList subFiles = QDir(dirPath + "/" + subDir).entryList(nameFilters, QDir::Files, QDir::Name);
            for (const auto& f : subFiles)
                files << dirPath + "/" + subDir + "/" + f;
        }
    }

    // Convert relative to absolute paths
    QStringList absoluteFiles;
    for (const auto& f : files) {
        QFileInfo fi(f);
        if (fi.isRelative()) {
            absoluteFiles << dir.absoluteFilePath(f);
        } else {
            absoluteFiles << f;
        }
    }

    return importAudioFiles(projectPath, absoluteFiles, options);
}

bool KSAudioImporter::importBatch(const QString& projectPath,
                                   const QVector<ImportSource>& sources,
                                   const ImportOptions& options)
{
    m_importedEvents.clear();
    m_lastError.clear();

    for (const auto& source : sources) {
        switch (source.type) {
        case ImportSource::AudioFile: {
            QStringList files;
            files << source.path;
            if (!importAudioFiles(projectPath, files, options))
                return false;
            break;
        }
        case ImportSource::BankFile:
            if (!importBank(projectPath, source.path, options))
                return false;
            break;
        case ImportSource::Directory:
            if (!importDirectory(projectPath, source.path, options, true))
                return false;
            break;
        }
    }

    return true;
}

// ============================================================================
// Private helpers
// ============================================================================

bool KSAudioImporter::addAudioFileToProject(const QString& projectPath,
                                              const QString& audioFilePath,
                                              const QString& bankName,
                                              const QString& eventGroup)
{
    QJsonObject root = loadProject(projectPath);
    if (root.isEmpty()) return false;

    QString projectDir = QFileInfo(projectPath).absolutePath();
    QFileInfo fi(audioFilePath);
    QString baseName = fi.completeBaseName();
    QString ext = fi.suffix().toLower();

    // Verify the file is a supported format
    if (!supportedAudioExtensions().contains(ext)) {
        qWarning() << "Unsupported format:" << ext << "for file" << audioFilePath;
        return false;
    }

    // Copy asset to project
    QString assetsDir = projectDir + "/Assets/audio";
    QDir().mkpath(assetsDir);

    QString targetFileName = fi.fileName();
    QString targetPath = assetsDir + "/" + targetFileName;

    if (!QFile::exists(targetPath)) {
        if (!QFile::copy(audioFilePath, targetPath)) {
            qWarning() << "Failed to copy" << audioFilePath << "to" << targetPath;
            return false;
        }
    }

    // Decode to get metadata
    ::AudioFormatConverter converter;
    QVector<float> samples;
    QAudioFormat format;
    ::AudioFormatConverter::AudioMetadata metadata;
    bool decoded = false;

    switch (::AudioFormatConverter::formatFromExtension(ext)) {
    case ::AudioFormatConverter::FORMAT_WAV: {
        QFile f(audioFilePath);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray data = f.readAll();
            int ch = 2, sr = 44100, bps = 16;
            ::AudioBuffer::wavToSamples(data, samples, ch, sr, bps);
            format.setChannelCount(ch);
            format.setSampleRate(sr);
            format.setSampleFormat(QAudioFormat::Float);
            metadata.sampleRate = sr;
            metadata.channels = ch;
            metadata.durationMs = samples.size() / ch * 1000 / sr;
            decoded = !samples.isEmpty();
        }
        break;
    }
    case ::AudioFormatConverter::FORMAT_OGG:
        decoded = converter.decodeOgg(audioFilePath, samples, format, metadata);
        break;
    case ::AudioFormatConverter::FORMAT_MP3:
        decoded = converter.decodeMp3(audioFilePath, samples, format, metadata);
        break;
    case ::AudioFormatConverter::FORMAT_FLAC:
        decoded = converter.decodeFlac(audioFilePath, samples, format, metadata);
        break;
    default:
        break;
    }

    int sampleRate = decoded ? format.sampleRate() : 44100;
    int channels = decoded ? format.channelCount() : 2;
    double durationMs = decoded ? metadata.durationMs : 0.0;

    // Detect loop points for WAV files
    int loopStart = 0;
    if (ext == "wav") {
        loopStart = detectLoopPoint(audioFilePath);
    }

    // Create event object
    QString eventGuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString eventName = buildEventName(audioFilePath);

    QJsonObject evObj;
    evObj["guid"] = eventGuid;
    evObj["name"] = eventName;
    evObj["type"] = QStringLiteral("2D");
    evObj["audioFile"] = targetFileName;
    evObj["volume"] = 1.0;
    evObj["pitch"] = 1.0;
    evObj["loop"] = false;
    if (loopStart > 0) {
        evObj["loopStart"] = loopStart;
    }

    // Add to event group
    QJsonArray groupsArr = root["eventGroups"].toArray();
    bool groupFound = false;

    for (int i = 0; i < groupsArr.size(); ++i) {
        QJsonObject group = groupsArr[i].toObject();
        if (group["name"].toString() == bankName || group["name"].toString() == eventGroup) {
            QJsonArray evs = group["events"].toArray();
            evs.append(evObj);
            group["events"] = evs;
            groupsArr[i] = group;
            groupFound = true;
            break;
        }
    }

    if (!groupFound) {
        QJsonObject newGroup;
        newGroup["guid"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        newGroup["name"] = eventGroup.isEmpty() ? bankName : eventGroup;
        QJsonArray evs;
        evs.append(evObj);
        newGroup["events"] = evs;
        groupsArr.append(newGroup);
    }

    root["eventGroups"] = groupsArr;

    // Add sound metadata
    QJsonArray soundsArr = root["sounds"].toArray();
    QJsonObject sndObj;
    sndObj["name"] = baseName;
    sndObj["file"] = targetFileName;
    sndObj["sampleRate"] = sampleRate;
    sndObj["channels"] = channels;
    sndObj["format"] = ext;
    sndObj["loopStart"] = loopStart;
    soundsArr.append(sndObj);
    root["sounds"] = soundsArr;

    // Ensure bank exists and references this event
    QJsonArray banksArr = root["banks"].toArray();
    bool bankFound = false;
    for (int i = 0; i < banksArr.size(); ++i) {
        QJsonObject bank = banksArr[i].toObject();
        if (bank["name"].toString() == bankName) {
            QJsonArray guids = bank["eventGuids"].toArray();
            guids.append(eventGuid);
            bank["eventGuids"] = guids;
            banksArr[i] = bank;
            bankFound = true;
            break;
        }
    }

    if (!bankFound) {
        QJsonObject newBank;
        newBank["guid"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        newBank["name"] = bankName;
        QJsonArray guids;
        guids.append(eventGuid);
        newBank["eventGuids"] = guids;
        banksArr.append(newBank);
    }

    root["banks"] = banksArr;

    // Save updated project
    if (!saveProject(projectPath, root)) {
        return false;
    }

    ImportedEvent ie;
    ie.guid = eventGuid;
    ie.name = eventName;
    ie.audioFile = targetFileName;
    ie.bankName = bankName;
    ie.sampleRate = sampleRate;
    ie.channels = channels;
    ie.durationMs = durationMs;
    m_importedEvents.append(ie);

    return true;
}

bool KSAudioImporter::copyAssetToProject(const QString& sourcePath,
                                          const QString& projectDir)
{
    QString assetsDir = projectDir + "/Assets/audio";
    QDir().mkpath(assetsDir);

    QFileInfo fi(sourcePath);
    QString targetPath = assetsDir + "/" + fi.fileName();

    if (QFile::exists(targetPath)) return true;

    return QFile::copy(sourcePath, targetPath);
}

int KSAudioImporter::detectLoopPoint(const QString& wavPath) const
{
    QFile f(wavPath);
    if (!f.open(QIODevice::ReadOnly)) return 0;

    QDataStream s(&f);
    s.setByteOrder(QDataStream::LittleEndian);

    quint32 riff, fileSize, wave;
    s >> riff >> fileSize >> wave;
    if (riff != 0x46464952 || wave != 0x45564157) return 0;

    quint16 channels = 1, bitsPerSample = 16;
    quint32 dataOffset = 0, dataSize = 0;

    while (!s.atEnd()) {
        quint32 chunkId, chunkSize;
        s >> chunkId >> chunkSize;
        qint64 next = f.pos() + chunkSize;

        if (chunkId == 0x20746D66) {
            quint16 audioFmt;
            s >> audioFmt;
            s >> channels;
            quint32 sampleRate, byteRate;
            s >> sampleRate >> byteRate;
            quint16 blockAlign;
            s >> blockAlign;
            s >> bitsPerSample;
        } else if (chunkId == 0x61746164) {
            dataOffset = static_cast<quint32>(f.pos());
            dataSize = chunkSize;
            break;
        }
        f.seek(next);
    }

    if (dataSize == 0 || bitsPerSample != 16) return 0;

    int bytesPerSample = (bitsPerSample / 8) * channels;
    int totalSamples = dataSize / bytesPerSample;
    if (totalSamples < 1024) return 0;

    int searchSamples = qMin(4096, totalSamples / 2);
    int startSample = totalSamples - searchSamples;
    f.seek(dataOffset + startSample * bytesPerSample);

    QVector<qint16> buf(searchSamples * channels);
    f.read(reinterpret_cast<char*>(buf.data()), searchSamples * channels * 2);

    for (int i = searchSamples - 2; i >= 0; --i) {
        qint16 cur  = buf[i * channels];
        qint16 next = buf[(i + 1) * channels];
        if ((cur <= 0 && next >= 0) || (cur >= 0 && next <= 0)) {
            return startSample + i;
        }
    }
    return startSample;
}

QString KSAudioImporter::buildEventName(const QString& filePath) const
{
    QString baseName = QFileInfo(filePath).completeBaseName();
    // Clean up name: replace spaces/special chars with underscores
    baseName.replace(QRegularExpression("[^a-zA-Z0-9_]"), "_");
    baseName.replace(QRegularExpression("_+"), "_");
    while (baseName.startsWith('_')) baseName = baseName.mid(1);
    while (baseName.endsWith('_')) baseName.chop(1);
    if (baseName.isEmpty()) baseName = "event";
    return baseName;
}

bool KSAudioImporter::saveProject(const QString& projectPath, const QJsonObject& root)
{
    QFile f(projectPath);
    if (!f.open(QIODevice::WriteOnly)) {
        m_lastError = "Cannot write: " + projectPath;
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

QJsonObject KSAudioImporter::loadProject(const QString& projectPath)
{
    QFile f(projectPath);
    if (!f.open(QIODevice::ReadOnly)) {
        // Create new project if it doesn't exist
        QJsonObject root;
        root["_schema"]  = QStringLiteral("ksaudio");
        root["_version"] = QStringLiteral("2.0.0");
        root["name"]     = QFileInfo(projectPath).completeBaseName();
        root["guid"]     = QUuid::createUuid().toString(QUuid::WithoutBraces);
        root["format"]   = QStringLiteral("fmod.fspro.1.08.12");

        QJsonArray emptyArr;
        root["eventGroups"] = emptyArr;
        root["buses"] = emptyArr;
        root["banks"] = emptyArr;
        root["sounds"] = emptyArr;
        return root;
    }

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (doc.isNull() || !doc.isObject()) {
        return QJsonObject();
    }
    return doc.object();
}

}} // namespace ks::fileformat
