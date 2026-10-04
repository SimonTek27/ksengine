#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QTimer>
#include <QVector>

namespace ks {

class CrashRecovery : public QObject
{
    Q_OBJECT

public:
    explicit CrashRecovery(QObject* parent = nullptr);
    ~CrashRecovery();

    struct Session {
        QString id;
        QStringList openDocuments;
        QStringList files;
        QString lastActiveDocument;
        QJsonObject windowState;
        QJsonObject editorState;
        QJsonObject recentFiles;
        qint64 timestamp;
        bool isValid = true;
    };

    void startSession();
    void endSession();

    void addOpenDocument(const QString& path);
    void setActiveDocument(const QString& path);

    void saveSession();
    void saveSessionState(const QJsonObject& state);
    bool hasSession() const;
    Session getSession() const;
    QJsonObject getSessionState() const;
    void clearSession();
    bool recoverSessionState(const QJsonObject& sessionState);

    bool recover();
    bool recoverSession(const Session& session);

    QVector<Session> getAbandonedSessions() const;
    bool recoverAbandonedSession(const QString& sessionId);

    // Removes the on-disk marker for an abandoned session (after the user has
    // been informed), so it is not offered again on the next launch.
    void dismissSession(const QString& sessionId);

signals:
    void sessionStarted();
    void sessionEnded();
    void recoveryNeeded(const QVector<Session>& sessions);

private:
    void loadSession();
    void cleanupOldSessions();

    Session m_currentSession;
    QTimer m_sessionTimer;
    qint64 m_sessionStartTime;
};

} // namespace ks
