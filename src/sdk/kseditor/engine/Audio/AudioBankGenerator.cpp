#include "AudioBankGenerator.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>
#include <QtMath>
#include <QUuid>

namespace ks {
namespace audio {

KSAudioBankGenerator::KSAudioBankGenerator(QObject* parent)
    : QObject(parent)
{
    m_engineConfig.minRPM = 800;
    m_engineConfig.maxRPM = 8000;
    m_engineConfig.idleRPM = 900;
    m_engineConfig.redlineRPM = 7000;
    m_engineConfig.limiterRPM = 8000;
    m_engineConfig.limiterVolume = 0.9f;
    m_engineConfig.displacement = 4.0f;
    m_engineConfig.cylinders = 8;
    m_engineConfig.engineType = "V8";
    m_engineConfig.carName = "DefaultCar";
}

void KSAudioBankGenerator::setAudioProjectPath(const QString& path) {
    m_audioProjectPath = path;
}

void KSAudioBankGenerator::setCarName(const QString& name) {
    m_carName = name;
    m_engineConfig.carName = name;
}

void KSAudioBankGenerator::setGenerationMode(GenerationMode mode) {
    m_mode = mode;
}

void KSAudioBankGenerator::setTemplateCarName(const QString& name) {
    m_templateCarName = name;
}

void KSAudioBankGenerator::setSampleDirectory(const QString& dir) {
    m_sampleDirectory = dir;
}

void KSAudioBankGenerator::setEngineConfig(const EngineConfig& config) {
    m_engineConfig = config;
}

void KSAudioBankGenerator::addRPMSample(int rpm, const QString& onLoadFile, const QString& offLoadFile) {
    RPMSample sample(rpm);
    sample.onLoadFile = onLoadFile;
    sample.offLoadFile = offLoadFile;

    float baseRPM = static_cast<float>(m_engineConfig.idleRPM);
    sample.pitchOffset = 12.0f * log2f(static_cast<float>(rpm) / baseRPM);
    sample.volumeOffset = calculateVolume(rpm, m_engineConfig.idleRPM, m_engineConfig.redlineRPM);

    m_samples.append(sample);
}

void KSAudioBankGenerator::clearRPMSamples() {
    m_samples.clear();
    m_eventSamples.clear();
}

bool KSAudioBankGenerator::copySamplesToAssets(const QString& assetsDir) {
    if (m_sampleDirectory.isEmpty() || assetsDir.isEmpty()) {
        m_lastError = "Sample directory or assets directory not set";
        return false;
    }

    QDir sourceDir(m_sampleDirectory);
    QDir targetDir(assetsDir);

    if (!targetDir.exists()) {
        if (!targetDir.mkpath(".")) {
            m_lastError = "Failed to create assets directory";
            return false;
        }
    }

    for (const auto& sample : m_samples) {
        QString sourceFile = sample.onLoadFile;
        if (!QFile::exists(sourceFile)) {
            qWarning() << "Source file not found:" << sourceFile;
            continue;
        }

        QString targetFile = assetsDir + "/" + QFileInfo(sourceFile).fileName();
        if (!QFile::copy(sourceFile, targetFile)) {
            qWarning() << "Failed to copy:" << sourceFile << "to" << targetFile;
        }
    }

    return true;
}

bool KSAudioBankGenerator::createDirectoryStructure() {
    if (m_audioProjectPath.isEmpty()) {
        m_lastError = "Audio project path not set";
        return false;
    }

    QDir baseDir(m_audioProjectPath);

    QStringList dirs = {
        "Assets",
        "Assets/audio",
    };

    for (const QString& dir : dirs) {
        QString fullPath = m_audioProjectPath + "/" + dir;
        if (!QDir().mkpath(fullPath)) {
            m_lastError = "Failed to create directory: " + dir;
            return false;
        }
    }

    return true;
}

QString KSAudioBankGenerator::generateEventPath(EventType type) const {
    return m_carName + "/" + getEventName(type);
}

QString KSAudioBankGenerator::getEventName(EventType type) const {
    switch (type) {
    case EventType::EngineInterior: return "engine_int";
    case EventType::EngineExterior: return "engine_ext";
    case EventType::Turbo: return "turbo";
    case EventType::Wastegate: return "wastegate";
    case EventType::Blowoff: return "blowoff";
    case EventType::TireRoll: return "tyre_roll_front";
    case EventType::TireSlip: return "tyreslip_front";
    case EventType::Wind: return "wind";
    case EventType::Brake: return "brake_duct";
    default: return "unknown";
    }
}

KSAudioBankGenerator::EventType KSAudioBankGenerator::eventTypeFromString(const QString& str) {
    QMap<QString, EventType> map = {
        {"engine_int", EventType::EngineInterior},
        {"engine_ext", EventType::EngineExterior},
        {"turbo", EventType::Turbo},
        {"wastegate", EventType::Wastegate},
        {"blowoff", EventType::Blowoff},
        {"tyre_roll", EventType::TireRoll},
        {"tyreslip", EventType::TireSlip},
        {"wind", EventType::Wind},
        {"brake", EventType::Brake}
    };
    return map.value(str.toLower(), EventType::EngineInterior);
}

QString KSAudioBankGenerator::eventTypeToString(EventType type) {
    switch (type) {
    case EventType::EngineInterior: return "Engine Interior";
    case EventType::EngineExterior: return "Engine Exterior";
    case EventType::Turbo: return "Turbo";
    case EventType::Wastegate: return "Wastegate";
    case EventType::Blowoff: return "Blowoff";
    case EventType::TireRoll: return "Tire Roll";
    case EventType::TireSlip: return "Tire Slip";
    case EventType::Wind: return "Wind";
    case EventType::Brake: return "Brake";
    default: return "Unknown";
    }
}

int KSAudioBankGenerator::calculateRPMParameter(float rpm, int minRPM, int maxRPM) const {
    if (maxRPM <= minRPM) return 1000;
    return static_cast<int>(1000.0f + 7000.0f * (rpm - minRPM) / (maxRPM - minRPM));
}

float KSAudioBankGenerator::calculatePitch(float rpm, int baseRPM) const {
    if (baseRPM <= 0) baseRPM = m_engineConfig.idleRPM;
    return 12.0f * log2f(rpm / baseRPM);
}

float KSAudioBankGenerator::calculateVolume(float rpm, int idleRPM, int redlineRPM) const {
    if (redlineRPM <= idleRPM) return 1.0f;
    float normalized = (rpm - idleRPM) / (redlineRPM - idleRPM);
    normalized = qBound(0.0f, normalized, 1.0f);
    return 0.5f + 0.5f * normalized;
}

bool KSAudioBankGenerator::generate() {
    emit generationStarted();

    if (m_carName.isEmpty()) {
        m_lastError = "Car name not set";
        emit generationFailed(m_lastError);
        return false;
    }
    if (m_audioProjectPath.isEmpty()) {
        m_lastError = "Audio project path not set";
        emit generationFailed(m_lastError);
        return false;
    }
    if (m_samples.isEmpty()) {
        m_lastError = "No RPM samples configured";
        emit generationFailed(m_lastError);
        return false;
    }

    emit generationProgress(10);

    if (!createDirectoryStructure()) {
        emit generationFailed(m_lastError);
        return false;
    }

    emit generationProgress(30);

    // 1. Auto-detect loop points for each sample
    QString assetsDir = m_audioProjectPath + "/Assets/audio";
    if (!m_sampleDirectory.isEmpty()) {
        copySamplesToAssets(assetsDir);
        for (auto& sample : m_samples) {
            if (!sample.onLoadFile.isEmpty())
                sample.loopStart = detectLoopPoint(sample.onLoadFile);
            if (!sample.offLoadFile.isEmpty())
                sample.offLoopStart = detectLoopPoint(sample.offLoadFile);
        }
    }

    emit generationProgress(60);

    // 2. Generate .ksaudio project file (v2 FSPRO-compatible format)
    QString ksaudioPath = m_audioProjectPath + "/" + m_carName + ".ksaudio";
    QFile ksaudioFile(ksaudioPath);
    if (ksaudioFile.open(QIODevice::WriteOnly)) {
        QJsonObject root;
        root["_schema"]  = QStringLiteral("ksaudio");
        root["_version"] = QStringLiteral("2.0.0");
        root["name"]     = m_carName;
        root["format"]   = QStringLiteral("fmod.fspro.1.08.12");

        // Build event group hierarchy for car audio
        QJsonArray eventTypes;
        QStringList typeNames = {"engine_int", "engine_ext", "turbo", "wastegate",
                                 "blowoff", "tyre_roll_front", "wind", "brake_duct"};

        // Events with RPM timeline data
        QJsonArray eventsArr;
        for (const auto& sample : m_samples) {
            QJsonObject ev;
            ev["guid"] = QUuid::createUuid().toString();
            ev["name"] = QString("rpm_%1").arg(sample.rpm);
            ev["type"] = QStringLiteral("2D");
            ev["audioFile"] = QFileInfo(sample.onLoadFile).fileName();
            ev["volume"] = static_cast<double>(sample.volumeOffset);
            ev["pitch"]  = static_cast<double>(sample.pitchOffset);
            ev["loop"]   = true;
            ev["loopStart"] = sample.loopStart;
            eventsArr.append(ev);
        }

        // Wrap events in a named event group
        QJsonObject engineGroup;
        engineGroup["guid"] = QUuid::createUuid().toString();
        engineGroup["name"] = m_carName;
        engineGroup["events"] = eventsArr;

        QJsonArray groupsArr;
        groupsArr.append(engineGroup);
        root["eventGroups"] = groupsArr;

        // Banks section — references the events by GUID
        QJsonArray banksArr;
        QJsonObject bankObj;
        bankObj["guid"] = QUuid::createUuid().toString();
        bankObj["name"] = m_carName;
        QJsonArray bankEventGuids;
        for (const auto& ev : eventsArr)
            bankEventGuids.append(ev.toObject().value("guid"));
        bankObj["eventGuids"] = bankEventGuids;
        banksArr.append(bankObj);
        root["banks"] = banksArr;

        ksaudioFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        ksaudioFile.close();
        qInfo() << "KSAudioBankGenerator: Generated ksaudio project:" << ksaudioPath;
    }

    m_lastGeneratedPath = ksaudioPath;

    emit generationProgress(100);
    emit generationCompleted(ksaudioPath);
    return true;
}

int KSAudioBankGenerator::detectLoopPoint(const QString& wavPath) const {
    QFile f(wavPath);
    if (!f.open(QIODevice::ReadOnly)) return 0;

    QDataStream s(&f);
    s.setByteOrder(QDataStream::LittleEndian);

    quint32 riff, fileSize, wave;
    s >> riff >> fileSize >> wave;
    if (riff != 0x46464952 /*RIFF*/ || wave != 0x45564157 /*WAVE*/) return 0;

    quint16 channels = 1, bitsPerSample = 16;
    quint32 dataOffset = 0, dataSize = 0;

    while (!s.atEnd()) {
        quint32 chunkId, chunkSize;
        s >> chunkId >> chunkSize;
        qint64 next = f.pos() + chunkSize;

        if (chunkId == 0x20746D66) { // "fmt "
            quint16 audioFmt; s >> audioFmt;
            s >> channels;
            quint32 sampleRate, byteRate; s >> sampleRate >> byteRate;
            quint16 blockAlign; s >> blockAlign;
            s >> bitsPerSample;
        } else if (chunkId == 0x61746164) { // "data"
            dataOffset = (quint32)f.pos();
            dataSize   = chunkSize;
            break;
        }
        f.seek(next);
    }

    if (dataSize == 0 || bitsPerSample != 16) return 0;

    int bytesPerSample = (bitsPerSample / 8) * channels;
    int totalSamples   = dataSize / bytesPerSample;
    if (totalSamples < 1024) return 0;

    int searchSamples = qMin(4096, totalSamples / 2);
    int startSample   = totalSamples - searchSamples;
    f.seek(dataOffset + startSample * bytesPerSample);

    QVector<qint16> buf(searchSamples * channels);
    f.read(reinterpret_cast<char*>(buf.data()), searchSamples * channels * 2);

    for (int i = searchSamples - 2; i >= 0; --i) {
        qint16 cur  = buf[i * channels];
        qint16 next = buf[(i+1) * channels];
        if ((cur <= 0 && next >= 0) || (cur >= 0 && next <= 0)) {
            return startSample + i;
        }
    }
    return startSample;
}

}
}
