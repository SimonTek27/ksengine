#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTimer>
#include "ChatProtocol.h"

namespace ks::chat {

class ChatServer : public QObject
{
    Q_OBJECT

public:
    explicit ChatServer(QObject* parent = nullptr);
    ~ChatServer();

    bool start(quint16 port);
    void stop();
    bool isRunning() const { return m_running; }
    quint16 port() const { return m_port; }

    void setMaxUsers(int max) { m_maxUsers = max; }
    void setPassword(const QString& password) { m_password = password; }

    QVector<ChatChannel> getChannels() const { return m_channels; }
    QVector<ChatUser> getConnectedUsers() const;
    int userCount() const { return m_users.size(); }

    void createChannel(const QString& name, ChannelType type = ChannelType::Group);

signals:
    void started();
    void stopped();
    void error(const QString& error);
    void userConnected(const QString& userId, const QString& userName);
    void userDisconnected(const QString& userId);
    void messageSent(const QString& channelId, const QString& userId, const QString& content);

private slots:
    void onNewConnection();
    void onClientDisconnected(QWebSocket* socket);
    void onTextMessageReceived(const QString& message);
    void onSslErrors(const QList<QSslError>& errors);

private:
    void handlePacket(QWebSocket* socket, const ProtocolMessage& msg);
    void handleAuth(QWebSocket* socket, const QJsonObject& payload);
    void handleSync(QWebSocket* socket);
    void handleCreateChannel(const QJsonObject& payload);
    void handleJoinChannel(const QJsonObject& payload, const QString& userId);
    void handleLeaveChannel(const QJsonObject& payload, const QString& userId);
    void handleMessage(const QJsonObject& payload, const QString& userId);
    void handleEditMessage(const QJsonObject& payload, const QString& userId);
    void handleDeleteMessage(const QJsonObject& payload, const QString& userId);
    void handleReaction(const QJsonObject& payload, const QString& userId);
    void handleTyping(const QJsonObject& payload, const QString& userId);

    void broadcast(const ProtocolMessage& msg, const QString& excludeUserId = QString());
    void sendTo(const QString& userId, const ProtocolMessage& msg);
    void sendToSocket(QWebSocket* socket, const ProtocolMessage& msg);
    QString findUserBySocket(QWebSocket* socket) const;
    QString generateId() const;

    bool m_running = false;
    quint16 m_port = 0;
    int m_maxUsers = 50;
    QString m_password;

    QWebSocketServer* m_server = nullptr;
    QMap<QString, ChatUser> m_users;
    QMap<QString, QWebSocket*> m_userSockets;
    QMap<QWebSocket*, QString> m_socketToUser;
    QVector<ChatChannel> m_channels;
    QMap<QString, QVector<ChatMessage>> m_messages;
};

} // namespace ks::chat
