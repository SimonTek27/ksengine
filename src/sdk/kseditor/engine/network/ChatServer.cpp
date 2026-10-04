#include "ChatServer.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QDebug>
#include <QRandomGenerator>

namespace ks::chat {

ChatServer::ChatServer(QObject* parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("ksChatServer"),
        QWebSocketServer::NonSecureMode, this))
{
    connect(m_server, &QWebSocketServer::newConnection, this, &ChatServer::onNewConnection);
    connect(m_server, &QWebSocketServer::closed, this, [this]() {
        m_running = false;
        emit stopped();
    });
}

ChatServer::~ChatServer() {
    stop();
}

bool ChatServer::start(quint16 port) {
    if (m_running) return false;

    m_port = port;
    if (!m_server->listen(QHostAddress::Any, port)) {
        emit error(m_server->errorString());
        return false;
    }

    m_running = true;

    // Create default channels
    bool hasGeneral = false;
    for (const auto& ch : m_channels) {
        if (ch.name == "general") { hasGeneral = true; break; }
    }
    if (!hasGeneral) {
        ChatChannel general;
        general.id = generateId();
        general.name = "general";
        general.description = "General discussion";
        general.type = ChannelType::Group;
        general.owner = "system";
        general.createdAt = QDateTime::currentDateTime();
        m_channels.append(general);
    }

    emit started();
    qDebug() << "ksChat Server: Started on port" << port;
    return true;
}

void ChatServer::stop() {
    if (!m_running) return;

    m_server->close();
    for (auto socket : m_userSockets.values()) {
        socket->close();
    }
    m_users.clear();
    m_userSockets.clear();
    m_socketToUser.clear();
    m_running = false;

    emit stopped();
    qDebug() << "ksChat Server: Stopped";
}

void ChatServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QWebSocket* socket = m_server->nextPendingConnection();

        if (m_users.size() >= m_maxUsers) {
            sendToSocket(socket, ProtocolMessage::make("error", {{"message", "Server is full"}}));
            socket->close();
            continue;
        }

        connect(socket, &QWebSocket::textMessageReceived, this, &ChatServer::onTextMessageReceived);
        connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
            onClientDisconnected(socket);
        });
        connect(socket, &QWebSocket::sslErrors, this, &ChatServer::onSslErrors);

        qDebug() << "ksChat Server: New connection from" << socket->peerAddress().toString();
    }
}

void ChatServer::onClientDisconnected(QWebSocket* socket) {
    QString userId = m_socketToUser.value(socket);
    if (!userId.isEmpty()) {
        m_users.remove(userId);
        m_userSockets.remove(userId);
        m_socketToUser.remove(socket);

        // Notify others
        broadcast(ProtocolMessage::make("user_presence", {
            {"userId", userId},
            {"status", static_cast<int>(UserStatus::Offline)}
        }), userId);

        // Remove from channels
        for (auto& ch : m_channels) {
            ch.members.removeOne(userId);
        }

        emit userDisconnected(userId);
        qDebug() << "ksChat Server: User" << userId << "disconnected";
    }

    socket->deleteLater();
}

void ChatServer::onTextMessageReceived(const QString& message) {
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket) return;

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull() || !doc.isObject()) return;

    ProtocolMessage msg = ProtocolMessage::fromJson(doc.object());
    handlePacket(socket, msg);
}

void ChatServer::onSslErrors(const QList<QSslError>& errors) {
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (socket) socket->ignoreSslErrors();
}

void ChatServer::handlePacket(QWebSocket* socket, const ProtocolMessage& msg) {
    QString userId = findUserBySocket(socket);

    if (msg.type == "auth") {
        handleAuth(socket, msg.payload);
    } else if (userId.isEmpty()) {
        sendToSocket(socket, ProtocolMessage::make("error", {{"message", "Not authenticated"}}));
        return;
    } else if (msg.type == "sync") {
        handleSync(socket);
    } else if (msg.type == "create_channel") {
        handleCreateChannel(msg.payload);
    } else if (msg.type == "join_channel") {
        handleJoinChannel(msg.payload, userId);
    } else if (msg.type == "leave_channel") {
        handleLeaveChannel(msg.payload, userId);
    } else if (msg.type == "message") {
        handleMessage(msg.payload, userId);
    } else if (msg.type == "edit_message") {
        handleEditMessage(msg.payload, userId);
    } else if (msg.type == "delete_message") {
        handleDeleteMessage(msg.payload, userId);
    } else if (msg.type == "reaction") {
        handleReaction(msg.payload, userId);
    } else if (msg.type == "typing") {
        handleTyping(msg.payload, userId);
    }
}

void ChatServer::handleAuth(QWebSocket* socket, const QJsonObject& payload) {
    QString userId = payload["userId"].toString();
    QString userName = payload["userName"].toString();

    if (userId.isEmpty() || userName.isEmpty()) {
        sendToSocket(socket, ProtocolMessage::make("error", {{"message", "Invalid auth"}}));
        return;
    }

    ChatUser user;
    user.id = userId;
    user.name = userName;
    user.color = QColor::fromHsv(QRandomGenerator::global()->bounded(360), 180, 200);
    user.status = UserStatus::Online;
    user.lastSeen = QDateTime::currentMSecsSinceEpoch();

    m_users[userId] = user;
    m_userSockets[userId] = socket;
    m_socketToUser[socket] = userId;

    // Send auth response with current users
    QJsonArray usersArr;
    for (const auto& u : m_users) {
        usersArr.append(u.toJson());
    }

    sendToSocket(socket, ProtocolMessage::make("auth_ok", {
        {"users", usersArr}
    }));

    // Notify others
    broadcast(ProtocolMessage::make("user_presence", {
        {"userId", userId},
        {"status", static_cast<int>(UserStatus::Online)},
        {"userName", userName}
    }), userId);

    emit userConnected(userId, userName);
    qDebug() << "ksChat Server: User" << userName << "authenticated";
}

void ChatServer::handleSync(QWebSocket* socket) {
    QJsonArray channelsArr;
    for (const auto& ch : m_channels) {
        channelsArr.append(ch.toJson());
    }

    sendToSocket(socket, ProtocolMessage::make("sync", {
        {"channels", channelsArr}
    }));
}

void ChatServer::handleCreateChannel(const QJsonObject& payload) {
    ChatChannel ch;
    ch.id = generateId();
    ch.name = payload["name"].toString();
    ch.type = static_cast<ChannelType>(payload["type"].toInt());
    ch.owner = "system";
    ch.createdAt = QDateTime::currentDateTime();

    m_channels.append(ch);
    broadcast(ProtocolMessage::make("channel_created", ch.toJson()));
}

void ChatServer::handleJoinChannel(const QJsonObject& payload, const QString& userId) {
    QString channelId = payload["channelId"].toString();

    for (auto& ch : m_channels) {
        if (ch.id == channelId && !ch.members.contains(userId)) {
            ch.members.append(userId);
            broadcast(ProtocolMessage::make("channel_joined", {
                {"channelId", channelId},
                {"userId", userId},
                {"userName", m_users.value(userId).name}
            }));
            break;
        }
    }
}

void ChatServer::handleLeaveChannel(const QJsonObject& payload, const QString& userId) {
    QString channelId = payload["channelId"].toString();

    for (auto& ch : m_channels) {
        if (ch.id == channelId) {
            ch.members.removeOne(userId);
            broadcast(ProtocolMessage::make("channel_left", {
                {"channelId", channelId},
                {"userId", userId}
            }));
            break;
        }
    }
}

void ChatServer::handleMessage(const QJsonObject& payload, const QString& userId) {
    ChatMessage msg = ChatMessage::fromJson(payload);
    msg.authorId = userId;
    msg.authorName = m_users.value(userId).name;
    msg.timestamp = QDateTime::currentDateTime();

    m_messages[msg.channelId].append(msg);
    broadcast(ProtocolMessage::make("message", msg.toJson()));
    emit messageSent(msg.channelId, userId, msg.content);
}

void ChatServer::handleEditMessage(const QJsonObject& payload, const QString& userId) {
    QString msgId = payload["id"].toString();
    QString channelId = payload["channelId"].toString();
    QString newContent = payload["content"].toString();

    auto& msgs = m_messages[channelId];
    for (auto& m : msgs) {
        if (m.id == msgId && m.authorId == userId) {
            m.content = newContent;
            m.editedAt = QDateTime::currentDateTime();
            broadcast(ProtocolMessage::make("message_edited", m.toJson()));
            return;
        }
    }
}

void ChatServer::handleDeleteMessage(const QJsonObject& payload, const QString& userId) {
    QString msgId = payload["messageId"].toString();
    QString channelId = payload["channelId"].toString();

    auto& msgs = m_messages[channelId];
    for (auto& m : msgs) {
        if (m.id == msgId && m.authorId == userId) {
            m.isDeleted = true;
            broadcast(ProtocolMessage::make("message_deleted", {
                {"messageId", msgId},
                {"channelId", channelId}
            }));
            return;
        }
    }
}

void ChatServer::handleReaction(const QJsonObject& payload, const QString& userId) {
    QString msgId = payload["messageId"].toString();
    QString emoji = payload["emoji"].toString();
    bool added = payload["added"].toBool();

    // Update local state and broadcast
    for (auto& msgs : m_messages) {
        for (auto& m : msgs) {
            if (m.id == msgId) {
                if (added) {
                    if (!m.reactions[emoji].contains(userId))
                        m.reactions[emoji].append(userId);
                } else {
                    m.reactions[emoji].removeOne(userId);
                    if (m.reactions[emoji].isEmpty())
                        m.reactions.remove(emoji);
                }
                break;
            }
        }
    }

    broadcast(ProtocolMessage::make("reaction", {
        {"messageId", msgId},
        {"emoji", emoji},
        {"userId", userId},
        {"added", added}
    }));
}

void ChatServer::handleTyping(const QJsonObject& payload, const QString& userId) {
    QString channelId = payload["channelId"].toString();

    for (auto& ch : m_channels) {
        if (ch.id == channelId) {
            for (const auto& memberId : ch.members) {
                if (memberId != userId) {
                    sendTo(memberId, ProtocolMessage::make("typing", {
                        {"channelId", channelId},
                        {"userId", userId}
                    }));
                }
            }
            break;
        }
    }
}

void ChatServer::broadcast(const ProtocolMessage& msg, const QString& excludeUserId) {
    QJsonDocument doc(msg.toJson());
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    for (auto it = m_userSockets.begin(); it != m_userSockets.end(); ++it) {
        if (it.key() != excludeUserId && it.value()) {
            it.value()->sendTextMessage(data);
        }
    }
}

void ChatServer::sendTo(const QString& userId, const ProtocolMessage& msg) {
    QWebSocket* socket = m_userSockets.value(userId);
    if (socket) {
        sendToSocket(socket, msg);
    }
}

void ChatServer::sendToSocket(QWebSocket* socket, const ProtocolMessage& msg) {
    if (socket && socket->isValid()) {
        QJsonDocument doc(msg.toJson());
        socket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
    }
}

QString ChatServer::findUserBySocket(QWebSocket* socket) const {
    return m_socketToUser.value(socket);
}

QVector<ChatUser> ChatServer::getConnectedUsers() const {
    return m_users.values().toVector();
}

void ChatServer::createChannel(const QString& name, ChannelType type) {
    ChatChannel ch;
    ch.id = generateId();
    ch.name = name;
    ch.type = type;
    ch.owner = "system";
    ch.createdAt = QDateTime::currentDateTime();

    m_channels.append(ch);
    broadcast(ProtocolMessage::make("channel_created", ch.toJson()));
}

QString ChatServer::generateId() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
}

} // namespace ks::chat
