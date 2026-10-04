#include "RPMProfile.h"
#include <QFile>
#include <QJsonArray>
#include <QDebug>

namespace ks {
namespace audio {

KSRPMProfile::KSRPMProfile(QObject* parent)
    : QObject(parent)
{
}

void KSRPMProfile::setEngineType(EngineType type) {
    if (m_engineType == type) return;
    m_engineType = type;

    switch (type) {
    case EngineType::Economy4:
        setRPMRange(800, 6500, 500);
        break;
    case EngineType::Sport6:
        setRPMRange(1000, 8000, 500);
        break;
    case EngineType::V8:
        setRPMRange(1000, 7500, 500);
        break;
    case EngineType::V10:
        setRPMRange(1500, 9000, 500);
        break;
    case EngineType::V12:
        setRPMRange(1000, 9000, 500);
        break;
    case EngineType::Flat6:
        setRPMRange(1000, 7500, 500);
        break;
    case EngineType::Rotary:
        setRPMRange(1000, 9000, 500);
        break;
    case EngineType::Race:
        setRPMRange(2000, 12000, 1000);
        break;
    }

    emit engineTypeChanged(type);
}

void KSRPMProfile::setRPMRange(int min, int max, int step) {
    m_minRPM = min;
    m_maxRPM = max;
    m_step = step;
}

void KSRPMProfile::generateLinearRange(int startRPM, int endRPM, int stepRPM, int holdMs) {
    generateFromConfig(startRPM, endRPM, stepRPM, holdMs);
}

void KSRPMProfile::generateEngineRange(EngineType type, int holdMs) {
    switch (type) {
    case EngineType::Economy4:
        generateFromConfig(800, 6500, 500, holdMs);
        break;
    case EngineType::Sport6:
        generateFromConfig(1000, 8000, 500, holdMs);
        break;
    case EngineType::V8:
        generateFromConfig(1000, 7500, 500, holdMs);
        break;
    case EngineType::V10:
        generateFromConfig(1500, 9000, 500, holdMs);
        break;
    case EngineType::V12:
        generateFromConfig(1000, 9000, 500, holdMs);
        break;
    case EngineType::Flat6:
        generateFromConfig(1000, 7500, 500, holdMs);
        break;
    case EngineType::Rotary:
        generateFromConfig(1000, 9000, 500, holdMs);
        break;
    case EngineType::Race:
        generateFromConfig(2000, 12000, 1000, holdMs);
        break;
    }
}

void KSRPMProfile::generateFromConfig(int minRPM, int maxRPM, int step, int holdMs) {
    m_points.clear();
    m_minRPM = minRPM;
    m_maxRPM = maxRPM;
    m_step = step;

    bool isRaceEngine = (typeid(*this) == typeid(KSRPMProfile) && m_engineType == EngineType::Race);

    for (int rpm = minRPM; rpm <= maxRPM; rpm += step) {
        bool skip = false;

        if (isRaceEngine) {
            skip = (rpm < 3000);
        }

        m_points.append(RPMPoint(rpm, holdMs, skip));
    }

    emit pointsChanged();
}

void KSRPMProfile::addPoint(int rpm, int durationMs, bool skip) {
    m_points.append(RPMPoint(rpm, durationMs, skip));
    emit pointsChanged();
}

void KSRPMProfile::removePoint(int index) {
    if (index >= 0 && index < m_points.size()) {
        m_points.removeAt(index);
        emit pointsChanged();
    }
}

void KSRPMProfile::clearPoints() {
    m_points.clear();
    emit pointsChanged();
}

QJsonObject KSRPMProfile::toJson() const {
    QJsonObject json;
    json["name"] = m_name;
    json["description"] = m_description;
    json["engineType"] = engineTypeToString(m_engineType);
    json["minRPM"] = m_minRPM;
    json["maxRPM"] = m_maxRPM;
    json["step"] = m_step;

    QJsonArray pointsArray;
    for (const auto& point : m_points) {
        QJsonObject pt;
        pt["rpm"] = point.rpm;
        pt["durationMs"] = point.durationMs;
        pt["skip"] = point.skip;
        pointsArray.append(pt);
    }
    json["points"] = pointsArray;

    return json;
}

void KSRPMProfile::fromJson(const QJsonObject& json) {
    m_name = json["name"].toString();
    m_description = json["description"].toString();

    QString typeStr = json["engineType"].toString();
    if (!typeStr.isEmpty()) {
        m_engineType = engineTypeFromString(typeStr);
    }

    m_minRPM = json["minRPM"].toInt(1000);
    m_maxRPM = json["maxRPM"].toInt(8000);
    m_step = json["step"].toInt(500);

    m_points.clear();
    QJsonArray pointsArray = json["points"].toArray();
    for (const auto& val : pointsArray) {
        QJsonObject pt = val.toObject();
        RPMPoint point;
        point.rpm = pt["rpm"].toInt();
        point.durationMs = pt["durationMs"].toInt(3000);
        point.skip = pt["skip"].toBool(false);
        m_points.append(point);
    }
}

bool KSRPMProfile::save(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool KSRPMProfile::load(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    fromJson(doc.object());
    return true;
}

KSRPMProfile::EngineType KSRPMProfile::engineTypeFromString(const QString& str) {
    static QMap<QString, EngineType> map = {
        {"economy4", EngineType::Economy4},
        {"sport6", EngineType::Sport6},
        {"v8", EngineType::V8},
        {"v10", EngineType::V10},
        {"v12", EngineType::V12},
        {"flat6", EngineType::Flat6},
        {"rotary", EngineType::Rotary},
        {"race", EngineType::Race}
    };
    return map.value(str.toLower(), EngineType::Sport6);
}

QString KSRPMProfile::engineTypeToString(EngineType type) {
    switch (type) {
    case EngineType::Economy4: return "Economy 4-cyl";
    case EngineType::Sport6: return "Sport 6-cyl";
    case EngineType::V8: return "V8";
    case EngineType::V10: return "V10";
    case EngineType::V12: return "V12";
    case EngineType::Flat6: return "Flat-6";
    case EngineType::Rotary: return "Rotary";
    case EngineType::Race: return "Race Car";
    default: return "Unknown";
    }
}

KSRPMProfile* KSRPMProfile::createPreset(EngineType type) {
    KSRPMProfile* profile = new KSRPMProfile();
    profile->setName(engineTypeToString(type));
    profile->setEngineType(type);
    profile->generateEngineRange(type);
    return profile;
}

QVector<KSRPMProfile*> KSRPMProfile::createAllPresets() {
    QVector<KSRPMProfile*> presets;
    presets.append(createPreset(EngineType::Economy4));
    presets.append(createPreset(EngineType::Sport6));
    presets.append(createPreset(EngineType::V8));
    presets.append(createPreset(EngineType::V10));
    presets.append(createPreset(EngineType::V12));
    presets.append(createPreset(EngineType::Flat6));
    presets.append(createPreset(EngineType::Rotary));
    presets.append(createPreset(EngineType::Race));
    return presets;
}

}
}
