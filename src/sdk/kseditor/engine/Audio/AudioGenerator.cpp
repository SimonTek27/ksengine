#include "AudioGenerator.h"
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

KSAudioGenerator::KSAudioGenerator(QObject* parent)
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

void KSAudioGenerator::setProjectPath(const QString& path) {
    m_projectPath = path;
}

void KSAudioGenerator::setCarName(const QString& name) {
    m_carName = name;
    m_engineConfig.carName = name;
}

void KSAudioGenerator::setGenerationMode(GenerationMode mode) {
    m_mode = mode;
}

void KSAudioGenerator::setTemplateCarName(const QString& name) {
    m_templateCarName = name;
}

void KSAudioGenerator::setSampleDirectory(const QString& dir) {
    m_sampleDirectory = dir;
}

void KSAudioGenerator::setEngineConfig(const EngineConfig& config) {
    m_engineConfig = config;
}

void KSAudioGenerator::addRPMSample(int rpm, const QString& onLoadFile, const QString& offLoadFile) {
    RPMSample sample(rpm);
    sample.onLoadFile = onLoadFile;
    sample.offLoadFile = offLoadFile;

    float baseRPM = static_cast<float>(m_engineConfig.idleRPM);
    sample.pitchOffset = 12.0f * log2f(static_cast<float>(rpm) / baseRPM);
    sample.volumeOffset = calculateVolume(rpm, m_engineConfig.idleRPM, m_engineConfig.redlineRPM);

    m_samples.append(sample);
}

void KSAudioGenerator::clearRPMSamples() {
    m_samples.clear();
    m_eventSamples.clear();
}

QString KSAudioGenerator::generateKSAudioProject() {
    QJsonObject root;
    root["_schema"]  = QStringLiteral("ksaudio");
    root["_version"] = QStringLiteral("2.0");
    root["name"]     = m_carName;
    root["format"]   = QStringLiteral("fmod.fspro.1.08.12");

    QVector<RPMSample> sortedSamples = m_samples;
    std::sort(sortedSamples.begin(), sortedSamples.end(), [](const RPMSample& a, const RPMSample& b) {
        return a.rpm < b.rpm;
    });

    // Events with GUIDs
    QJsonArray eventsArr;
    for (const auto& sample : sortedSamples) {
        QJsonObject ev;
        ev["guid"] = QUuid::createUuid().toString();
        ev["name"] = QString("rpm_%1").arg(sample.rpm);
        ev["type"] = QStringLiteral("2D");
        ev["audioFile"] = QFileInfo(sample.onLoadFile).fileName();
        ev["volume"] = static_cast<double>(sample.volumeOffset);
        ev["pitch"]  = static_cast<double>(sample.pitchOffset);
        ev["loop"]   = true;
        eventsArr.append(ev);
    }

    // Event group
    QJsonObject engineGroup;
    engineGroup["guid"] = QUuid::createUuid().toString();
    engineGroup["name"] = m_carName;
    engineGroup["events"] = eventsArr;
    QJsonArray groupsArr;
    groupsArr.append(engineGroup);
    root["eventGroups"] = groupsArr;

    // Banks referencing events by GUID
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

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool KSAudioGenerator::copySamplesToAssets(const QString& assetsDir) {
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

bool KSAudioGenerator::createDirectoryStructure() {
    if (m_projectPath.isEmpty()) {
        m_lastError = "Project path not set";
        return false;
    }

    QStringList dirs = {
        "Assets",
        "Assets/audio",
    };

    for (const QString& dir : dirs) {
        QString fullPath = m_projectPath + "/" + dir;
        if (!QDir().mkpath(fullPath)) {
            m_lastError = "Failed to create directory: " + dir;
            return false;
        }
    }

    return true;
}

QString KSAudioGenerator::generateEventPath(EventType type) const {
    return m_carName + "/" + getEventName(type);
}

QString KSAudioGenerator::getEventName(EventType type) const {
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

KSAudioGenerator::EventType KSAudioGenerator::eventTypeFromString(const QString& str) {
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

QString KSAudioGenerator::eventTypeToString(EventType type) {
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

int KSAudioGenerator::calculateRPMParameter(float rpm, int minRPM, int maxRPM) const {
    if (maxRPM <= minRPM) return 1000;
    return static_cast<int>(1000.0f + 7000.0f * (rpm - minRPM) / (maxRPM - minRPM));
}

float KSAudioGenerator::calculatePitch(float rpm, int baseRPM) const {
    if (baseRPM <= 0) baseRPM = m_engineConfig.idleRPM;
    return 12.0f * log2f(rpm / baseRPM);
}

float KSAudioGenerator::calculateVolume(float rpm, int idleRPM, int redlineRPM) const {
    if (redlineRPM <= idleRPM) return 1.0f;
    float normalized = (rpm - idleRPM) / (redlineRPM - idleRPM);
    normalized = qBound(0.0f, normalized, 1.0f);
    return 0.5f + 0.5f * normalized;
}

bool KSAudioGenerator::generate() {
    emit generationStarted();

    if (m_carName.isEmpty()) {
        m_lastError = "Car name not set";
        emit generationFailed(m_lastError);
        return false;
    }

    if (m_projectPath.isEmpty()) {
        m_lastError = "Project path not set";
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

    QString assetsDir = m_projectPath + "/Assets/audio";
    if (!m_sampleDirectory.isEmpty()) {
        copySamplesToAssets(assetsDir);
    }

    emit generationProgress(50);

    QString projectJson = generateKSAudioProject();

    emit generationProgress(70);

    QString projectPath = m_projectPath + "/" + m_carName + ".ksaudio";
    QFile projectFile(projectPath);
    if (!projectFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastError = "Failed to create project file";
        emit generationFailed(m_lastError);
        return false;
    }

    QTextStream out(&projectFile);
    out << projectJson;
    projectFile.close();

    m_lastGeneratedPath = projectPath;

    emit generationProgress(100);
    emit generationCompleted(projectPath);
    return true;
}

}
}
