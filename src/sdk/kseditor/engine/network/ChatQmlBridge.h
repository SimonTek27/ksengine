#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVariant>
#include "ChatManager.h"
#include "ChatServer.h"

namespace ks::chat {

class ChatQmlBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY userNameChanged)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(QVariantList users READ users NOTIFY usersChanged)
    Q_PROPERTY(QString activeChannelId READ activeChannelId WRITE setActiveChannelId NOTIFY activeChannelChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool isServerRunning READ isServerRunning NOTIFY serverStateChanged)
    Q_PROPERTY(int userCount READ userCount NOTIFY userCountChanged)

public:
    static ChatQmlBridge* instance();

    bool isConnected() const;
    QString connectionState() const;
    QString userName() const;
    QString host() const;
    int port() const;
    QVariantList channels() const;
    QVariantList users() const;
    QString activeChannelId() const;
    QVariantList messages() const;
    bool isServerRunning() const;
    int userCount() const;

    void setUserName(const QString& name);
    void setHost(const QString& h);
    void setPort(int p);
    void setActiveChannelId(const QString& id);

    Q_INVOKABLE void connectToServer();
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void startServer();
    Q_INVOKABLE void stopServer();

    Q_INVOKABLE void createChannel(const QString& name, int type = 1);
    Q_INVOKABLE void joinChannel(const QString& channelId);
    Q_INVOKABLE void leaveChannel(const QString& channelId);

    Q_INVOKABLE void sendMessage(const QString& content, const QString& replyTo = QString());
    Q_INVOKABLE void editMessage(const QString& messageId, const QString& newContent);
    Q_INVOKABLE void deleteMessage(const QString& messageId);
    Q_INVOKABLE void addReaction(const QString& messageId, const QString& emoji);
    Q_INVOKABLE void removeReaction(const QString& messageId, const QString& emoji);
    Q_INVOKABLE void sendTyping();

    Q_INVOKABLE QVariantMap getUser(const QString& userId) const;
    Q_INVOKABLE QVariantList getChannelMessages(const QString& channelId) const;

    Q_INVOKABLE void refreshChannels();
    Q_INVOKABLE void refreshMessages();
    Q_INVOKABLE void refreshUsers();

signals:
    void connectedChanged();
    void connectionStateChanged();
    void userNameChanged();
    void hostChanged();
    void portChanged();
    void channelsChanged();
    void usersChanged();
    void activeChannelChanged();
    void messagesChanged();
    void serverStateChanged();
    void userCountChanged();

    void messageReceived(const QString& channelId, const QString& userId,
                        const QString& userName, const QString& content,
                        const QString& timestamp);
    void userJoinedChat(const QString& userId, const QString& userName);
    void userLeftChat(const QString& userId, const QString& userName);
    void channelCreatedSignal(const QString& channelId, const QString& name);
    void errorOccurred(const QString& error);
    void typingIndicator(const QString& channelId, const QString& userId);

private:
    explicit ChatQmlBridge(QObject* parent = nullptr);
    static ChatQmlBridge* s_instance;

    ChatManager* m_manager = nullptr;
    ChatServer* m_server = nullptr;
    QString m_host = "localhost";
    int m_port = 9090;
};

} // namespace ks::chat
