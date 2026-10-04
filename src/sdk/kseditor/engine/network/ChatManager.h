#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QTimer>
#include <QWebSocket>
#include <QUuid>
#include "ChatProtocol.h"

namespace ks::chat {

class ChatManager : public QObject
{
    Q_OBJECT

public:
    static ChatManager* instance();

    explicit ChatManager(QObject* parent = nullptr);
    ~ChatManager();

    void connectToServer(const QString& host, quint16 port, const QString& userName);
    void disconnectFromServer();
    bool isConnected() const { return m_connected; }

    void createChannel(const QString& name, ChannelType type = ChannelType::Group);
    void joinChannel(const QString& channelId);
    void leaveChannel(const QString& channelId);
    void setActiveChannel(const QString& channelId);
    QString activeChannel() const { return m_activeChannelId; }

    void sendMessage(const QString& channelId, const QString& content,
                     MessageType type = MessageType::Text, const QString& replyTo = QString());
    void editMessage(const QString& messageId, const QString& newContent);
    void deleteMessage(const QString& messageId);
    void addReaction(const QString& messageId, const QString& emoji);
    void removeReaction(const QString& messageId, const QString& emoji);
    void sendTyping(const QString& channelId);

    QVector<ChatChannel> getChannels() const { return m_channels; }
    QVector<ChatMessage> getMessages(const QString& channelId) const;
    QVector<ChatUser> getUsers() const { return m_users.values(); }
    ChatUser getUser(const QString& userId) const;
    QString myUserId() const { return m_userId; }
    QString myUserName() const { return m_userName; }

    void setUserName(const QString& name);

    QVector<ChatChannel> getDirectChannels() const;
    QVector<ChatChannel> getGroupChannels() const;
    QVector<ChatChannel> getProjectChannels() const;

signals:
    void connected();
    void disconnected();
    void connectionError(const QString& error);

    void channelCreated(const ChatChannel& channel);
    void channelJoined(const ChatChannel& channel);
    void channelLeft(const QString& channelId);
    void activeChannelChanged(const QString& channelId);

    void messageReceived(const ChatMessage& message);
    void messageEdited(const ChatMessage& message);
    void messageDeleted(const QString& messageId);

    void userJoined(const ChatUser& user);
    void userLeft(const QString& userId);
    void userStatusChanged(const QString& userId, UserStatus status);

    void reactionAdded(const QString& messageId, const QString& emoji, const QString& userId);
    void reactionRemoved(const QString& messageId, const QString& emoji, const QString& userId);

    void typingReceived(const QString& channelId, const QString& userId);
    void unreadCountChanged(const QString& channelId, int count);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString& message);
    void onSslErrors(const QList<QSslError>& errors);

private:
    void handleMessage(const ProtocolMessage& msg);
    void handleAuthResponse(const QJsonObject& payload);
    void handleChannelCreated(const QJsonObject& payload);
    void handleChannelJoined(const QJsonObject& payload);
    void handleMessageReceived(const QJsonObject& payload);
    void handleMessageEdited(const QJsonObject& payload);
    void handleMessageDeleted(const QJsonObject& payload);
    void handleUserPresence(const QJsonObject& payload);
    void handleReaction(const QJsonObject& payload);
    void handleTyping(const QJsonObject& payload);

    void sendPacket(const ProtocolMessage& msg);
    void requestSync();
    void saveLocalData();
    void loadLocalData();

    QString generateId() const;

    static ChatManager* s_instance;

    QWebSocket* m_socket = nullptr;
    bool m_connected = false;
    QString m_userId;
    QString m_userName;
    QString m_activeChannelId;
    QTimer* m_reconnectTimer = nullptr;
    int m_reconnectAttempts = 0;
    bool m_autoReconnect = true;
    QString m_lastHost;
    quint16 m_lastPort = 0;

    QVector<ChatChannel> m_channels;
    QMap<QString, QVector<ChatMessage>> m_messages;
    QMap<QString, ChatUser> m_users;
};

} // namespace ks::chat
