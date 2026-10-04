#include "Collaboration.h"

#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QUuid>
#include <QDebug>

namespace ks {

CollaborationClient* CollaborationClient::s_instance = nullptr;

CollaborationClient* CollaborationClient::instance()
{
    if (!s_instance) s_instance = new CollaborationClient();
    return s_instance;
}

CollaborationClient::CollaborationClient(QObject* parent)
    : QObject(parent)
    , m_state(CollaborationState::Disconnected)
{
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    QObject::connect(m_socket, &QWebSocket::connected, this, [this]() {
        m_state = CollaborationState::Connected;
        m_reconnectAttempts = 0;
        sendAuth();
        emit stateChanged(m_state);
        emit connected();
        qInfo() << "[Collaboration] Connected to" << m_host << ":" << m_port;
    });

    QObject::connect(m_socket, &QWebSocket::disconnected, this, [this]() {
        m_state = CollaborationState::Disconnected;
        emit stateChanged(m_state);
        emit disconnected();
        if (m_autoReconnect) scheduleReconnect();
    });

    QObject::connect(m_socket, &QWebSocket::textMessageReceived,
            this, &CollaborationClient::onMessage);

    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(30000);
    QObject::connect(m_pingTimer, &QTimer::timeout, this, [this]() {
        if (m_state == CollaborationState::Connected)
            sendPacket("ping", QJsonObject());
    });

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    QObject::connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_state != CollaborationState::Connected) doConnect();
    });
}

CollaborationClient::~CollaborationClient() { s_instance = nullptr; }

void CollaborationClient::setServer(const QString& host, quint16 port)
{
    m_host = host;
    m_port = port;
}

void CollaborationClient::setUserInfo(const QString& userId, const QString& userName)
{
    m_userId   = userId;
    m_userName = userName;
}

bool CollaborationClient::connect()
{
    if (m_state == CollaborationState::Connected) return true;
    m_state = CollaborationState::Connecting;
    emit stateChanged(m_state);
    doConnect();
    return true;
}

void CollaborationClient::doConnect()
{
    if (m_host.isEmpty()) { emit error("Server host not set"); return; }
    QString url = QString("ws://%1:%2/collab").arg(m_host).arg(m_port);
    m_socket->open(QUrl(url));
}

void CollaborationClient::disconnect()
{
    m_autoReconnect = false;
    m_reconnectTimer->stop();
    m_pingTimer->stop();
    m_socket->close();
}

void CollaborationClient::joinDocument(const QString& docId)
{
    if (!isConnected()) return;
    m_openDocuments.insert(docId);
    QJsonObject payload;
    payload["docId"] = docId;
    sendPacket("join", payload);
}

void CollaborationClient::leaveDocument(const QString& docId)
{
    if (!isConnected()) return;
    m_openDocuments.remove(docId);
    QJsonObject payload;
    payload["docId"] = docId;
    sendPacket("leave", payload);
}

void CollaborationClient::sendChange(const QString& docId, const QJsonObject& change)
{
    if (!isConnected()) return;
    QJsonObject payload;
    payload["docId"]  = docId;
    payload["change"] = change;
    payload["version"] = ++m_localVersion;
    sendPacket("change", payload);
}

void CollaborationClient::sendCursor(const QString& docId, const QJsonObject& cursor)
{
    if (!isConnected()) return;
    QJsonObject payload;
    payload["docId"]  = docId;
    payload["cursor"] = cursor;
    sendPacket("cursor", payload);
}

void CollaborationClient::sendSelection(const QString& docId, const QJsonObject& selection)
{
    if (!isConnected()) return;
    QJsonObject payload;
    payload["docId"]     = docId;
    payload["selection"] = selection;
    sendPacket("selection", payload);
}

void CollaborationClient::sendChat(const QString& docId, const QString& message)
{
    if (!isConnected()) return;
    QJsonObject payload;
    payload["docId"]   = docId;
    payload["message"] = message;
    sendPacket("chat", payload);
}

void CollaborationClient::setPresence(const QString& status)
{
    m_presence = status;
    if (isConnected()) {
        QJsonObject payload;
        payload["status"] = status;
        sendPacket("presence", payload);
    }
}

void CollaborationClient::setAutoReconnect(bool enabled)
{
    m_autoReconnect = enabled;
}

QVector<CollaborationUser> CollaborationClient::getUsers(const QString& docId) const
{
    QVector<CollaborationUser> users;
    for (const auto& u : m_activeUsers)
        if (m_userDocuments.value(u.id) == docId) users << u;
    return users;
}

QVector<Change> CollaborationClient::getChanges(const QString& docId, int fromVersion) const
{
    if (!m_documents.contains(docId)) return {};
    
    const CollaborationDocument& doc = m_documents[docId];
    QVector<Change> filtered;
    
    for (const Change& change : doc.changes) {
        if (change.version > fromVersion) {
            filtered.append(change);
        }
    }
    
    return filtered;
}

// ─── Private ──────────────────────────────────────────────────────────────────

void CollaborationClient::sendAuth()
{
    QJsonObject payload;
    payload["userId"]   = m_userId;
    payload["userName"] = m_userName;
    payload["version"]  = 1;
    sendPacket("auth", payload);
    m_pingTimer->start();
}

void CollaborationClient::sendPacket(const QString& type, const QJsonObject& payload)
{
    QJsonObject pkt;
    pkt["type"]      = type;
    pkt["userId"]    = m_userId;
    pkt["timestamp"] = QDateTime::currentSecsSinceEpoch();
    pkt["payload"]   = payload;
    m_socket->sendTextMessage(QJsonDocument(pkt).toJson(QJsonDocument::Compact));
}

void CollaborationClient::onMessage(const QString& message)
{
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        qWarning() << "[Collaboration] Invalid JSON message";
        return;
    }
    QJsonObject pkt = doc.object();
    const QString type = pkt["type"].toString();
    QJsonObject payload = pkt["payload"].toObject();

    if (type == "auth_ok") {
        qDebug() << "[Collaboration] Auth OK";
    } else if (type == "user_joined") {
        CollaborationUser u;
        u.id       = payload["userId"].toString();
        u.name     = payload["userName"].toString();
        u.color    = payload["color"].toString("#4488ff");
        u.isOnline = true;
        m_activeUsers.insert(u.id, u);
        m_userDocuments[u.id] = payload["docId"].toString();
        emit userJoined(u);
    } else if (type == "user_left") {
        QString uid = payload["userId"].toString();
        CollaborationUser u = m_activeUsers.take(uid);
        m_userDocuments.remove(uid);
        emit userLeft(u);
    } else if (type == "change") {
        Change ch;
        ch.id         = payload["changeId"].toString();
        ch.userId     = pkt["userId"].toString();
        ch.documentId = payload["docId"].toString();
        ch.data       = payload["change"].toObject();
        ch.version    = payload["version"].toInt();
        ch.timestamp  = static_cast<qint64>(pkt["timestamp"].toDouble());
        emit changeReceived(ch);
    } else if (type == "cursor") {
        emit cursorReceived(payload["docId"].toString(),
                         pkt["userId"].toString(),
                         payload["cursor"].toObject());
    } else if (type == "chat") {
        emit chatReceived(payload["docId"].toString(),
                                   pkt["userId"].toString(),
                                   payload["message"].toString());
    } else if (type == "pong") {
    } else if (type == "error") {
        emit error(payload["message"].toString());
    }
}

void CollaborationClient::scheduleReconnect()
{
    if (m_reconnectAttempts >= 10) {
        qWarning() << "[Collaboration] Max reconnect attempts reached";
        return;
    }
    int delay = qMin(1000 * (1 << m_reconnectAttempts), 30000);
    ++m_reconnectAttempts;
    qDebug() << "[Collaboration] Reconnecting in" << delay << "ms (attempt" << m_reconnectAttempts << ")";
    m_reconnectTimer->start(delay);
}

// ═════════════════════════════════════════════════════════════════════════════
// CollaborationServer
// ═════════════════════════════════════════════════════════════════════════════

const QStringList CollaborationServer::s_userColors = {
    "#e74c3c", "#3498db", "#2ecc71", "#f39c12", "#9b59b6",
    "#1abc9c", "#e67e22", "#34495e", "#e91e63", "#00bcd4",
    "#8bc34a", "#ff5722", "#607d8b", "#795548", "#cddc39"
};

CollaborationServer::CollaborationServer(QObject* parent) : QObject(parent)
{
    m_server = new QWebSocketServer(QStringLiteral("ksEditor Collaboration Server"),
                                    QWebSocketServer::NonSecureMode, this);

    QObject::connect(m_server, &QWebSocketServer::newConnection, this, &CollaborationServer::onNewConnection);
    QObject::connect(m_server, &QWebSocketServer::closed, this, [this]() {
        m_running = false;
        emit stopped();
    });
    QObject::connect(m_server, &QWebSocketServer::serverError, this, [this](QWebSocketProtocol::CloseCode) {
        emit error(m_server->errorString());
    });
}

CollaborationServer::~CollaborationServer()
{
    stop();
}

bool CollaborationServer::start(quint16 port)
{
    if (m_running) return true;

    m_port = port;
    if (!m_server->listen(QHostAddress::Any, port)) {
        emit error(m_server->errorString());
        return false;
    }

    m_running = true;
    emit started();
    qInfo() << "[CollabServer] Listening on port" << port;
    return true;
}

void CollaborationServer::stop()
{
    if (!m_running) return;

    for (auto it = m_users.begin(); it != m_users.end(); ++it) {
        for (auto sit = m_socketToUser.begin(); sit != m_socketToUser.end(); ++sit) {
            if (sit.value() == it.key()) {
                sit.key()->close();
                break;
            }
        }
    }

    // Close all client sockets
    for (auto it = m_socketToUser.begin(); it != m_socketToUser.end(); ++it) {
        it.key()->close();
    }

    m_users.clear();
    m_socketToUser.clear();
    m_userLastActivity.clear();
    m_documentChanges.clear();
    m_server->close();
    m_running = false;
    emit stopped();
    qInfo() << "[CollabServer] Stopped";
}

void CollaborationServer::setMaxUsers(int max)
{
    m_maxUsers = max;
}

void CollaborationServer::setPassword(const QString& password)
{
    m_password = password;
}

void CollaborationServer::clearPassword()
{
    m_password.clear();
}

void CollaborationServer::kickUser(const QString& userId)
{
    if (!m_users.contains(userId)) return;

    // Find and close the socket
    for (auto it = m_socketToUser.begin(); it != m_socketToUser.end(); ++it) {
        if (it.value() == userId) {
            QJsonObject msg;
            msg["type"] = "kicked";
            msg["payload"] = QJsonObject{{"reason", "Kicked by admin"}};
            it.key()->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
            it.key()->close();
            break;
        }
    }

    m_users.remove(userId);
    qInfo() << "[CollabServer] Kicked user:" << userId;
}

void CollaborationServer::banUser(const QString& userId)
{
    if (!m_bannedUsers.contains(userId)) {
        m_bannedUsers.append(userId);
    }
    kickUser(userId);
    qInfo() << "[CollabServer] Banned user:" << userId;
}

void CollaborationServer::unbanUser(const QString& userId)
{
    m_bannedUsers.removeAll(userId);
    qInfo() << "[CollabServer] Unbanned user:" << userId;
}

QVector<CollaborationUser> CollaborationServer::getConnectedUsers() const
{
    return m_users.values().toVector();
}

QJsonObject CollaborationServer::getStatistics() const
{
    QJsonObject stats;
    stats["userCount"] = m_users.size();
    stats["maxUsers"] = m_maxUsers;
    stats["bannedCount"] = m_bannedUsers.size();
    stats["running"] = m_running;
    stats["port"] = m_port;
    stats["hasPassword"] = !m_password.isEmpty();

    int docCount = 0;
    for (auto it = m_documentChanges.begin(); it != m_documentChanges.end(); ++it)
        if (!it.value().isEmpty()) docCount++;
    stats["activeDocuments"] = docCount;

    return stats;
}

// ─── Private slots ──────────────────────────────────────────────────────────

void CollaborationServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QWebSocket* socket = m_server->nextPendingConnection();

        if (m_users.size() >= m_maxUsers) {
            QJsonObject msg;
            msg["type"] = "error";
            msg["payload"] = QJsonObject{{"message", "Server full"}};
            socket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
            socket->close();
            continue;
        }

        QObject::connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString& message) {
            QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
            if (!doc.isObject()) {
                qWarning() << "[Collaboration] Invalid JSON from client";
                return;
            }
            QJsonObject pkt = doc.object();
            QString userId = m_socketToUser.value(socket);
            if (!userId.isEmpty()) {
                handleMessage(userId, pkt);
            } else {
                // First message must be auth
                QJsonObject payload = pkt["payload"].toObject();
                QString uid = payload["userId"].toString();
                QString userName = payload["userName"].toString();

                if (uid.isEmpty() || userName.isEmpty()) {
                    socket->close();
                    return;
                }

                if (m_bannedUsers.contains(uid)) {
                    QJsonObject errMsg;
                    errMsg["type"] = "error";
                    errMsg["payload"] = QJsonObject{{"message", "You are banned from this server"}};
                    socket->sendTextMessage(QJsonDocument(errMsg).toJson(QJsonDocument::Compact));
                    socket->close();
                    return;
                }

                // Register user
                CollaborationUser user;
                user.id = uid;
                user.name = userName;
                user.color = s_userColors[m_nextColorIndex % s_userColors.size()];
                m_nextColorIndex++;
                user.role = m_users.isEmpty() ? UserRole::Owner : UserRole::Editor;
                user.isOnline = true;
                user.lastActivity = QDateTime::currentSecsSinceEpoch();

                m_users.insert(uid, user);
                m_socketToUser.insert(socket, uid);
                m_userLastActivity[uid] = QDateTime::currentSecsSinceEpoch();

                // Send auth OK
                QJsonObject authOk;
                authOk["type"] = "auth_ok";
                authOk["payload"] = QJsonObject{
                    {"userId", uid},
                    {"color", user.color},
                    {"role", static_cast<int>(user.role)}
                };
                socket->sendTextMessage(QJsonDocument(authOk).toJson(QJsonDocument::Compact));

                // Notify others
                QJsonObject joinMsg;
                joinMsg["type"] = "user_joined";
                joinMsg["payload"] = QJsonObject{
                    {"userId", uid},
                    {"userName", userName},
                    {"color", user.color}
                };
                broadcast(joinMsg, uid);

                emit userConnected(user);
                qInfo() << "[CollabServer] User connected:" << userName << "(" << uid << ")";
            }
        });

        QObject::connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
            onClientDisconnected(socket);
        });
    }
}

void CollaborationServer::onClientDisconnected(QWebSocket* socket)
{
    QString userId = m_socketToUser.take(socket);
    if (userId.isEmpty()) return;

    CollaborationUser user = m_users.take(userId);
    m_userLastActivity.remove(userId);
    socket->deleteLater();

    // Notify others
    QJsonObject msg;
    msg["type"] = "user_left";
    msg["payload"] = QJsonObject{{"userId", userId}};
    broadcast(msg);

    emit userDisconnected(userId);
    qInfo() << "[CollabServer] User disconnected:" << user.name;
}

void CollaborationServer::onClientError()
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket) return;

    qWarning() << "[CollabServer] Client error on socket" << socket;
    socket->close();
    onClientDisconnected(socket);
}

// ─── Private ────────────────────────────────────────────────────────────────

void CollaborationServer::broadcast(const QJsonObject& message, const QString& excludeUser)
{
    QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact);
    for (auto it = m_socketToUser.begin(); it != m_socketToUser.end(); ++it) {
        if (it.value() != excludeUser) {
            it.key()->sendTextMessage(data);
        }
    }
}

void CollaborationServer::sendTo(const QString& userId, const QJsonObject& message)
{
    QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact);
    for (auto it = m_socketToUser.begin(); it != m_socketToUser.end(); ++it) {
        if (it.value() == userId) {
            it.key()->sendTextMessage(data);
            break;
        }
    }
}

void CollaborationServer::handleMessage(const QString& userId, const QJsonObject& pkt)
{
    const QString type = pkt["type"].toString();
    QJsonObject payload = pkt["payload"].toObject();

    m_userLastActivity[userId] = QDateTime::currentSecsSinceEpoch();

    if (type == "ping") {
        QJsonObject pong;
        pong["type"] = "pong";
        sendTo(userId, pong);
    }
    else if (type == "join") {
        QString docId = payload["docId"].toString();
        if (docId.isEmpty()) return;

        // Send existing changes to the joining user
        if (m_documentChanges.contains(docId)) {
            for (const Change& change : m_documentChanges[docId]) {
                QJsonObject changeMsg;
                changeMsg["type"] = "change";
                changeMsg["userId"] = change.userId;
                changeMsg["payload"] = QJsonObject{
                    {"changeId", change.id},
                    {"docId", change.documentId},
                    {"change", change.data},
                    {"version", change.version}
                };
                sendTo(userId, changeMsg);
            }
        }

        // Notify others in the document
        QJsonObject msg;
        msg["type"] = "user_joined";
        msg["payload"] = QJsonObject{
            {"userId", userId},
            {"userName", m_users[userId].name},
            {"color", m_users[userId].color},
            {"docId", docId}
        };
        broadcast(msg, userId);
    }
    else if (type == "leave") {
        QString docId = payload["docId"].toString();
        QJsonObject msg;
        msg["type"] = "user_left";
        msg["payload"] = QJsonObject{
            {"userId", userId},
            {"docId", docId}
        };
        broadcast(msg, userId);
    }
    else if (type == "change") {
        QString docId = payload["docId"].toString();
        QJsonObject changeData = payload["change"].toObject();
        int version = payload["version"].toInt();

        Change change;
        change.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        change.userId = userId;
        change.documentId = docId;
        change.data = changeData;
        change.version = version;
        change.timestamp = QDateTime::currentSecsSinceEpoch();

        if (!m_documentChanges.contains(docId))
            m_documentChanges[docId] = QVector<Change>();
        m_documentChanges[docId].append(change);

        // Broadcast change to all other users
        QJsonObject msg;
        msg["type"] = "change";
        msg["userId"] = userId;
        msg["payload"] = QJsonObject{
            {"changeId", change.id},
            {"docId", docId},
            {"change", changeData},
            {"version", version}
        };
        broadcast(msg, userId);
    }
    else if (type == "cursor") {
        QJsonObject msg;
        msg["type"] = "cursor";
        msg["userId"] = userId;
        msg["payload"] = payload;
        broadcast(msg, userId);
    }
    else if (type == "selection") {
        QJsonObject msg;
        msg["type"] = "selection";
        msg["userId"] = userId;
        msg["payload"] = payload;
        broadcast(msg, userId);
    }
    else if (type == "chat") {
        QString message = payload["message"].toString();
        QJsonObject msg;
        msg["type"] = "chat";
        msg["userId"] = userId;
        msg["payload"] = QJsonObject{
            {"message", message}
        };
        broadcast(msg);
        emit chatReceived(userId, message);
    }
    else if (type == "presence") {
        QJsonObject msg;
        msg["type"] = "presence";
        msg["userId"] = userId;
        msg["payload"] = payload;
        if (m_users.contains(userId)) {
            QString status = payload["status"].toString("online");
            m_users[userId].isOnline = (status != "offline");
            emit presenceChanged(userId, status);
        }
    }
    else {
        qWarning() << "[CollabServer] Unknown message type:" << type;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// PresenceManager
// ═════════════════════════════════════════════════════════════════════════════

PresenceManager::PresenceManager(QObject* parent)
    : QObject(parent)
{
    connect(&m_activityTimer, &QTimer::timeout, this, &PresenceManager::onUserActivityTimeout);
    m_activityTimer.start(30000);
}

PresenceManager::~PresenceManager() {}

void PresenceManager::setCollaborationClient(CollaborationClient* client)
{
    m_client = client;
}

void PresenceManager::updatePresence(const QString& status, const QJsonObject& data)
{
    if (m_client) m_client->setPresence(status);
    if (!data.isEmpty()) m_userData.insert("_self", data);
    emit presenceChanged("_self", status);
}

void PresenceManager::followUser(const QString& userId)
{
    m_following.insert(userId);
}

void PresenceManager::unfollowUser(const QString& userId)
{
    m_following.remove(userId);
}

QString PresenceManager::getUserStatus(const QString& userId) const
{
    return m_userStatus.value(userId);
}

QJsonObject PresenceManager::getUserData(const QString& userId) const
{
    return m_userData.value(userId);
}

QVector<CollaborationUser> PresenceManager::getOnlineUsers() const
{
    return m_client ? m_client->getUsers(QString()) : QVector<CollaborationUser>();
}

void PresenceManager::onUserActivityTimeout()
{
    updatePresence("away");
}

// ═════════════════════════════════════════════════════════════════════════════
// Annotations
// ═════════════════════════════════════════════════════════════════════════════

Annotations::Annotations(QObject* parent)
    : QObject(parent) {}

Annotations::~Annotations() {}

void Annotations::setDocument(const QString& docId)
{
    m_docId = docId;
}

QString Annotations::addAnnotation(const QString& text, const QJsonObject& position)
{
    Annotation a;
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    a.text = text;
    a.position = position;
    a.created = QDateTime::currentSecsSinceEpoch();
    a.modified = a.created;
    m_annotations.insert(a.id, a);
    emit annotationAdded(a);
    return a.id;
}

void Annotations::updateAnnotation(const QString& annotationId, const QString& text)
{
    if (m_annotations.contains(annotationId)) {
        m_annotations[annotationId].text = text;
        m_annotations[annotationId].modified = QDateTime::currentSecsSinceEpoch();
        emit annotationUpdated(m_annotations[annotationId]);
    }
}

void Annotations::resolveAnnotation(const QString& annotationId)
{
    if (m_annotations.contains(annotationId)) {
        m_annotations[annotationId].isResolved = true;
        emit annotationResolved(annotationId);
    }
}

void Annotations::deleteAnnotation(const QString& annotationId)
{
    if (m_annotations.remove(annotationId))
        emit annotationDeleted(annotationId);
}

QVector<Annotation> Annotations::getAnnotations() const
{
    return m_annotations.values().toVector();
}

QVector<Annotation> Annotations::getUnresolved() const
{
    QVector<Annotation> result;
    for (const auto& a : m_annotations)
        if (!a.isResolved) result.append(a);
    return result;
}

QVector<Annotation> Annotations::getByAuthor(const QString& authorId) const
{
    QVector<Annotation> result;
    for (const auto& a : m_annotations)
        if (a.authorId == authorId) result.append(a);
    return result;
}

int Annotations::getUnresolvedCount() const
{
    int count = 0;
    for (const auto& a : m_annotations)
        if (!a.isResolved) ++count;
    return count;
}

} // namespace ks
