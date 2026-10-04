#include "CrashRecovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QStandardPaths>

namespace ks {

// ============================================================================
// CrashRecovery
// ============================================================================

CrashRecovery::CrashRecovery(QObject* parent)
    : QObject(parent)
{
    connect(&m_sessionTimer, &QTimer::timeout, this, &CrashRecovery::saveSession);
    m_sessionTimer.setInterval(30000); // every 30 s
}

CrashRecovery::~CrashRecovery() {}

void CrashRecovery::startSession()
{
    m_currentSession.id          = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_currentSession.timestamp   = QDateTime::currentSecsSinceEpoch();
    m_currentSession.isValid     = true;
    m_sessionStartTime           = m_currentSession.timestamp;
    m_sessionTimer.start();
    emit sessionStarted();
}

void CrashRecovery::endSession()
{
    m_sessionTimer.stop();
    clearSession();
    emit sessionEnded();
}

void CrashRecovery::addOpenDocument(const QString& path)
{
    if (!m_currentSession.openDocuments.contains(path))
        m_currentSession.openDocuments << path;
}

void CrashRecovery::setActiveDocument(const QString& path)
{
    m_currentSession.lastActiveDocument = path;
}

void CrashRecovery::saveSession()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/sessions";
    QDir().mkpath(dir);
    QString path = dir + "/" + m_currentSession.id + ".json";

    QJsonObject obj;
    obj["id"]                 = m_currentSession.id;
    obj["openDocuments"]      = QJsonArray::fromStringList(m_currentSession.openDocuments);
    obj["lastActiveDocument"] = m_currentSession.lastActiveDocument;
    obj["timestamp"]          = m_currentSession.timestamp;
    obj["isValid"]            = true;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());
}

void CrashRecovery::saveSessionState(const QJsonObject& state)
{
    QJsonObject obj;
    obj["id"]                 = m_currentSession.id;
    obj["openDocuments"]      = QJsonArray::fromStringList(m_currentSession.openDocuments);
    obj["lastActiveDocument"] = m_currentSession.lastActiveDocument;
    obj["timestamp"]          = m_currentSession.timestamp;
    obj["windowState"]        = state.value("windowState").toObject();
    obj["editorState"]        = state.value("editorState").toObject();
    obj["isValid"]            = true;

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/sessions";
    QDir().mkpath(dir);
    QString path = dir + "/" + m_currentSession.id + ".json";

    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());
}

QJsonObject CrashRecovery::getSessionState() const
{
    if (!m_currentSession.isValid) return QJsonObject();
    
    QJsonObject obj;
    obj["id"]                 = m_currentSession.id;
    obj["openDocuments"]      = QJsonArray::fromStringList(m_currentSession.openDocuments);
    obj["lastActiveDocument"] = m_currentSession.lastActiveDocument;
    obj["timestamp"]          = m_currentSession.timestamp;
    obj["isValid"]            = true;
    
    return obj;
}

CrashRecovery::Session CrashRecovery::getSession() const
{
    return m_currentSession;
}

bool CrashRecovery::hasSession() const
{
    if (!m_currentSession.id.isEmpty()) return true;
    return !getAbandonedSessions().isEmpty();
}

void CrashRecovery::clearSession()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/sessions";
    QFile::remove(dir + "/" + m_currentSession.id + ".json");
    m_currentSession = {};
}

bool CrashRecovery::recover()
{
    auto sessions = getAbandonedSessions();
    if (sessions.isEmpty()) return false;
    emit recoveryNeeded(sessions);
    return true;
}

bool CrashRecovery::recoverSession(const Session& session)
{
    // Restore files from the session's backup directory
    QString backupDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/backups/" + session.id;
    
    QDir dir(backupDir);
    if (!dir.exists()) return false;
    
    bool success = true;
    for (const auto& file : session.files) {
        QString backupPath = backupDir + "/" + QFileInfo(file).fileName();
        if (QFile::exists(backupPath)) {
            // Restore the backup file to its original location
            QFile::remove(file);
            if (!QFile::copy(backupPath, file)) {
                success = false;
            }
        }
    }
    
    return success;
}

bool CrashRecovery::recoverSessionState(const QJsonObject& sessionState)
{
    if (!sessionState.value("isValid").toBool()) return false;
    
    Session s;
    s.id                 = sessionState["id"].toString();
    s.lastActiveDocument = sessionState["lastActiveDocument"].toString();
    s.timestamp          = static_cast<qint64>(sessionState["timestamp"].toDouble());
    s.isValid            = true;
    for (const auto& v : sessionState["openDocuments"].toArray())
        s.openDocuments << v.toString();
    
    // Load window state
    QJsonObject windowState = sessionState["windowState"].toObject();
    if (windowState.isEmpty()) {
        return true; // Still recovered without state
    }
    
    // Load editor state
    QJsonObject editorState = sessionState["editorState"].toObject();
    
    return true;
}

QVector<CrashRecovery::Session> CrashRecovery::getAbandonedSessions() const
{
    QVector<Session> sessions;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/sessions";
    QDir d(dir);
    for (const auto& fi : d.entryInfoList({"*.json"}, QDir::Files)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        if (!obj.value("isValid").toBool()) continue;
        Session s;
        s.id                 = obj["id"].toString();
        s.lastActiveDocument = obj["lastActiveDocument"].toString();
        s.timestamp          = static_cast<qint64>(obj["timestamp"].toDouble());
        s.isValid            = true;
        for (const auto& v : obj["openDocuments"].toArray())
            s.openDocuments << v.toString();
        sessions << s;
    }
    return sessions;
}

bool CrashRecovery::recoverAbandonedSession(const QString& sessionId)
{
    auto sessions = getAbandonedSessions();
    for (const auto& s : sessions) {
        if (s.id == sessionId) return recoverSession(s);
    }
    return false;
}

void CrashRecovery::dismissSession(const QString& sessionId)
{
    if (sessionId.isEmpty()) return;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/sessions";
    QFile::remove(dir + "/" + sessionId + ".json");
}

void CrashRecovery::loadSession()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/sessions";
    QString path = dir + "/" + m_currentSession.id + ".json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    m_currentSession.id = obj["id"].toString();
    m_currentSession.lastActiveDocument = obj["lastActiveDocument"].toString();
    m_currentSession.timestamp = static_cast<qint64>(obj["timestamp"].toDouble());
    m_currentSession.isValid = obj["isValid"].toBool();
    m_currentSession.openDocuments.clear();
    for (const auto& v : obj["openDocuments"].toArray())
        m_currentSession.openDocuments << v.toString();
}
void CrashRecovery::cleanupOldSessions()
{
    // Keep max 5 abandoned sessions
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/sessions";
    auto files = QDir(dir).entryInfoList({"*.json"}, QDir::Files, QDir::Time);
    while (files.size() > 5) {
        QFile::remove(files.takeLast().absoluteFilePath());
    }
}

} // namespace ks
