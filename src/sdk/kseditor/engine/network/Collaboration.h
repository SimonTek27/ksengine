#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QJsonObject>
#include <QHostAddress>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

namespace ks {

enum class CollaborationState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Error
};

enum class UserRole {
    Viewer,
    Editor,
    Admin,
    Owner
};

struct CollaborationUser {
    QString id;
    QString name;
    QString color;
    UserRole role;
    QHostAddress address;
    bool isOnline;
    qint64 lastActivity;
};

struct Change {
    QString id;
    QString userId;
    QString documentId;
    QString type;
    QJsonObject data;
    qint64 timestamp;
    int version;

    bool operator<(const Change& other) const { return timestamp < other.timestamp; }
};

struct CollaborationDocument {
    QVector<Change> changes;
};

class CollaborationClient : public QObject
{
    Q_OBJECT

public:
    static CollaborationClient* instance();
    explicit CollaborationClient(QObject* parent = nullptr);
    ~CollaborationClient();

    void setServer(const QString& host, quint16 port);
    void setUserInfo(const QString& userId, const QString& userName);

    bool connect();
    void disconnect();
    bool isConnected() const { return m_state == CollaborationState::Connected; }
    CollaborationState getState() const { return m_state; }

    void joinDocument(const QString& docId);
    void leaveDocument(const QString& docId);

    void sendChange(const QString& docId, const QJsonObject& change);
    void sendCursor(const QString& docId, const QJsonObject& cursor);
    void sendSelection(const QString& docId, const QJsonObject& selection);
    void sendChat(const QString& docId, const QString& message);

    QVector<CollaborationUser> getUsers(const QString& docId) const;
    QVector<Change> getChanges(const QString& docId, int fromVersion) const;

    void setAutoReconnect(bool enabled);
    bool isAutoReconnect() const { return m_autoReconnect; }

    void setPresence(const QString& status);
    QString getPresence() const { return m_presence; }

signals:
    void stateChanged(CollaborationState state);
    void connected();
    void disconnected();
    void error(const QString& error);

    void userJoined(const CollaborationUser& user);
    void userLeft(const CollaborationUser& user);

    void changeReceived(const Change& change);
    void cursorReceived(const QString& docId, const QString& userId, const QJsonObject& cursor);
    void selectionReceived(const QString& docId, const QString& userId, const QJsonObject& selection);
    void chatReceived(const QString& docId, const QString& userId, const QString& message);

    void versionUpdated(const QString& docId, int version);

private slots:
    void onMessage(const QString& message);
    void doConnect();
    void scheduleReconnect();

private:
    void sendAuth();
    void sendPacket(const QString& type, const QJsonObject& payload = QJsonObject());

    static CollaborationClient* s_instance;

    QString m_host;
    quint16 m_port = 0;
    QString m_userId;
    QString m_userName;
    QString m_presence = "available";

    CollaborationState m_state = CollaborationState::Disconnected;
    bool m_autoReconnect = true;
    int m_reconnectAttempts = 0;
    QTimer* m_reconnectTimer = nullptr;
    QTimer* m_pingTimer = nullptr;
    QWebSocket* m_socket = nullptr;
    int m_localVersion = 0;
    QSet<QString> m_openDocuments;
    QMap<QString, CollaborationUser> m_activeUsers;
    QMap<QString, QString> m_userDocuments;
    QMap<QString, CollaborationDocument> m_documents;
};

class CollaborationServer : public QObject
{
    Q_OBJECT

public:
    explicit CollaborationServer(QObject* parent = nullptr);
    ~CollaborationServer();

    bool start(quint16 port);
    void stop();

    bool isRunning() const { return m_running; }
    quint16 getPort() const { return m_port; }

    void setMaxUsers(int max);
    int getMaxUsers() const { return m_maxUsers; }

    void setPassword(const QString& password);
    void clearPassword();

    void kickUser(const QString& userId);
    void banUser(const QString& userId);
    void unbanUser(const QString& userId);

    QVector<CollaborationUser> getConnectedUsers() const;
    int getUserCount() const { return m_users.size(); }

    QStringList getBannedUsers() const { return m_bannedUsers; }

    QJsonObject getStatistics() const;

signals:
    void started();
    void stopped();
    void error(const QString& error);

    void userConnected(const CollaborationUser& user);
    void userDisconnected(const QString& userId);

    void messageReceived(const QString& userId, const QJsonObject& message);
    void chatReceived(const QString& userId, const QString& message);
    void presenceChanged(const QString& userId, const QString& status);

private slots:
    void onNewConnection();
    void onClientDisconnected(QWebSocket* socket);
    void onClientError();

private:
    void broadcast(const QJsonObject& message, const QString& excludeUser = QString());
    void sendTo(const QString& userId, const QJsonObject& message);
    void handleMessage(const QString& userId, const QJsonObject& message);

    bool m_running = false;
    quint16 m_port = 0;
    int m_maxUsers = 10;
    QString m_password;

    QWebSocketServer* m_server = nullptr;
    QMap<QString, CollaborationUser> m_users;
    QMap<QWebSocket*, QString> m_socketToUser;
    QStringList m_bannedUsers;
    QMap<QString, qint64> m_userLastActivity;
    QMap<QString, QVector<Change>> m_documentChanges;
    int m_nextColorIndex = 0;

    static const QStringList s_userColors;
};

class PresenceManager : public QObject
{
    Q_OBJECT

public:
    explicit PresenceManager(QObject* parent = nullptr);
    ~PresenceManager();

    void setCollaborationClient(CollaborationClient* client);

    void updatePresence(const QString& status, const QJsonObject& data = QJsonObject());

    void followUser(const QString& userId);
    void unfollowUser(const QString& userId);

    QString getUserStatus(const QString& userId) const;
    QJsonObject getUserData(const QString& userId) const;

    QVector<CollaborationUser> getOnlineUsers() const;

signals:
    void presenceChanged(const QString& userId, const QString& status);
    void userDataChanged(const QString& userId, const QJsonObject& data);
    void userWentOnline(const QString& userId);
    void userWentOffline(const QString& userId);

private slots:
    void onUserActivityTimeout();

private:
    CollaborationClient* m_client = nullptr;
    QMap<QString, QString> m_userStatus;
    QMap<QString, QJsonObject> m_userData;
    QSet<QString> m_following;
    QTimer m_activityTimer;
};

struct Cursor {
    qreal x = 0;
    qreal y = 0;
    int line = 0;
    int column = 0;
    QString selection;
};

struct Annotation {
    QString id;
    QString authorId;
    QString authorName;
    QString text;
    QString color;
    QJsonObject position;
    qint64 created;
    qint64 modified;
    bool isResolved = false;
};

class Annotations : public QObject
{
    Q_OBJECT

public:
    explicit Annotations(QObject* parent = nullptr);
    ~Annotations();

    void setDocument(const QString& docId);
    QString getDocument() const { return m_docId; }

    QString addAnnotation(const QString& text, const QJsonObject& position);
    void updateAnnotation(const QString& annotationId, const QString& text);
    void resolveAnnotation(const QString& annotationId);
    void deleteAnnotation(const QString& annotationId);

    QVector<Annotation> getAnnotations() const;
    QVector<Annotation> getUnresolved() const;
    QVector<Annotation> getByAuthor(const QString& authorId) const;

    int getCount() const { return m_annotations.size(); }
    int getUnresolvedCount() const;

signals:
    void annotationAdded(const Annotation& annotation);
    void annotationUpdated(const Annotation& annotation);
    void annotationResolved(const QString& annotationId);
    void annotationDeleted(const QString& annotationId);

private:
    QString m_docId;
    QMap<QString, Annotation> m_annotations;
    QString m_nextAnnotationId;
};

} // namespace ks