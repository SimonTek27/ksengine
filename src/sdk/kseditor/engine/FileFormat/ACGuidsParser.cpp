#include "ACGuidsParser.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

namespace ks {
namespace fileformat {

ACGuidsParser::ACGuidsParser(QObject* parent)
    : QObject(parent) {}

bool ACGuidsParser::parseFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ACGuidsParser: Cannot open" << filePath;
        return false;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    return parseFromString(content);
}

bool ACGuidsParser::parseFromString(const QString& content) {
    m_eventPaths.clear();
    m_bankPaths.clear();
    m_guidToEvent.clear();
    m_guidToBank.clear();

    QStringList lines = content.split('\n', Qt::SkipEmptyParts);
    auto& re = guidRegex();

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')) || trimmed.startsWith(QStringLiteral("//"))) {
            continue;
        }

        QRegularExpressionMatch match = re.match(trimmed);
        if (match.hasMatch()) {
            QString guid = match.captured(1);
            QString type = match.captured(2);
            QString path = match.captured(3);

            if (type == "event") {
                QString fullPath = "event:/" + path;
                m_guidToEvent[guid] = fullPath;
                if (!m_eventPaths.contains(fullPath)) {
                    m_eventPaths.append(fullPath);
                }
            } else if (type == "bank") {
                QString fullPath = "bank:/" + path;
                m_guidToBank[guid] = fullPath;
                if (!m_bankPaths.contains(fullPath)) {
                    m_bankPaths.append(fullPath);
                }
            }
        }
    }

    emit parsed(m_eventPaths.size(), m_bankPaths.size());
    qInfo() << "ACGuidsParser: Parsed" << m_eventPaths.size() << "events," << m_bankPaths.size() << "banks";
    return true;
}

QStringList ACGuidsParser::carEventPaths(const QString& carId) const {
    QStringList paths;
    QString prefix = QString("/cars/%1/").arg(carId);
    for (const QString& path : m_eventPaths) {
        if (path.contains(prefix)) {
            paths.append(path);
        }
    }
    return paths;
}

QStringList ACGuidsParser::carBankPaths(const QString& carId) const {
    QStringList paths;
    for (const QString& path : m_bankPaths) {
        if (path.contains(carId)) {
            paths.append(path);
        }
    }
    return paths;
}

QString ACGuidsParser::globalGuidsPath(const QString& acRoot) {
    return QString("%1/content/sfx/GUIDs.txt").arg(acRoot);
}

QString ACGuidsParser::carGuidsPath(const QString& carDir) {
    return QString("%1/sfx/GUIDs.txt").arg(carDir);
}

void ACGuidsParser::mergeWith(const ACGuidsParser& other) {
    for (auto it = other.m_guidToEvent.constBegin(); it != other.m_guidToEvent.constEnd(); ++it) {
        if (!m_guidToEvent.contains(it.key())) {
            m_guidToEvent[it.key()] = it.value();
            if (!m_eventPaths.contains(it.value())) {
                m_eventPaths.append(it.value());
            }
        }
    }
    for (auto it = other.m_guidToBank.constBegin(); it != other.m_guidToBank.constEnd(); ++it) {
        if (!m_guidToBank.contains(it.key())) {
            m_guidToBank[it.key()] = it.value();
            if (!m_bankPaths.contains(it.value())) {
                m_bankPaths.append(it.value());
            }
        }
    }
}

void ACGuidsParser::remapCarId(const QString& oldId, const QString& newId) {
    QMap<QString, QString> newEvents;
    for (auto it = m_guidToEvent.constBegin(); it != m_guidToEvent.constEnd(); ++it) {
        QString val = it.value();
        val.replace(oldId, newId);
        newEvents[it.key()] = val;
    }
    m_guidToEvent = newEvents;

    m_eventPaths.clear();
    for (const auto& path : m_guidToEvent) {
        if (!m_eventPaths.contains(path)) {
            m_eventPaths.append(path);
        }
    }

    QMap<QString, QString> newBanks;
    for (auto it = m_guidToBank.constBegin(); it != m_guidToBank.constEnd(); ++it) {
        QString val = it.value();
        val.replace(oldId, newId);
        newBanks[it.key()] = val;
    }
    m_guidToBank = newBanks;

    m_bankPaths.clear();
    for (const auto& path : m_guidToBank) {
        if (!m_bankPaths.contains(path)) {
            m_bankPaths.append(path);
        }
    }
}

} // namespace fileformat
} // namespace ks
