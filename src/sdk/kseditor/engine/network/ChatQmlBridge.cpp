#include "ChatQmlBridge.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace ks::chat {

ChatQmlBridge* ChatQmlBridge::s_instance = nullptr;

ChatQmlBridge* ChatQmlBridge::instance() {
    if (!s_instance) {
        s_instance = new ChatQmlBridge();
    }
    return s_instance;
}

ChatQmlBridge::ChatQmlBridge(QObject* parent)
    : QObject(parent)
    , m_manager(ChatManager::instance())
    , m_server(new ChatServer(this))
{
    connect(m_manager, &ChatManager::connected, this, [this]() {
        emit connectedChanged();
        emit connectionStateChanged();
        emit usersChanged();
    });
    connect(m_manager, &ChatManager::disconnected, this, [this]() {
        emit connectedChanged();
        emit connectionStateChanged();
    });
    connect(m_manager, &ChatManager::connectionError, this, [this](const QString& err) {
        emit errorOccurred(err);
    });

    connect(m_manager, &ChatManager::channelCreated, this, [this](const ChatChannel&) {
        emit channelsChanged();
    });
    connect(m_manager, &ChatManager::activeChannelChanged, this, [this](const QString&) {
        emit activeChannelChanged();
        emit messagesChanged();
    });

    connect(m_manager, &ChatManager::messageReceived, this, [this](const ChatMessage& msg) {
        emit messagesChanged();
        emit messageReceived(msg.channelId, msg.authorId, msg.authorName,
                           msg.content, msg.timestamp.toString(Qt::ISODate));
    });

    connect(m_manager, &ChatManager::userJoined, this, [this](const ChatUser& user) {
        emit usersChanged();
        emit userCountChanged();
        emit userJoinedChat(user.id, user.name);
    });
    connect(m_manager, &ChatManager::userLeft, this, [this](const QString& userId) {
        emit usersChanged();
        emit userCountChanged();
        emit userLeftChat(userId, userId);
    });

    connect(m_manager, &ChatManager::messageEdited, this, [this](const ChatMessage&) {
        emit messagesChanged();
    });
    connect(m_manager, &ChatManager::messageDeleted, this, [this](const QString&) {
        emit messagesChanged();
    });

    connect(m_manager, &ChatManager::typingReceived, this, [this](const QString& chId, const QString& userId) {
        emit typingIndicator(chId, userId);
    });

    connect(m_server, &ChatServer::started, this, [this]() {
        emit serverStateChanged();
    });
    connect(m_server, &ChatServer::stopped, this, [this]() {
        emit serverStateChanged();
    });
    connect(m_server, &ChatServer::error, this, [this](const QString& err) {
        emit errorOccurred(err);
    });
}

bool ChatQmlBridge::isConnected() const {
    return m_manager->isConnected();
}

QString ChatQmlBridge::connectionState() const {
    return m_manager->isConnected() ? "Connected" : "Disconnected";
}

QString ChatQmlBridge::userName() const {
    return m_manager->myUserName();
}

QString ChatQmlBridge::host() const { return m_host; }
int ChatQmlBridge::port() const { return m_port; }

QString ChatQmlBridge::activeChannelId() const {
    return m_manager->activeChannel();
}

bool ChatQmlBridge::isServerRunning() const {
    return m_server->isRunning();
}

int ChatQmlBridge::userCount() const {
    return m_manager->getUsers().size();
}

QVariantList ChatQmlBridge::channels() const {
    QVariantList result;
    for (const auto& ch : m_manager->getChannels()) {
        QVariantMap m;
        m["id"] = ch.id;
        m["name"] = ch.name;
        m["description"] = ch.description;
        m["type"] = static_cast<int>(ch.type);
        m["memberCount"] = ch.members.size();
        m["unreadCount"] = ch.unreadCount;
        m["isPinned"] = ch.isPinned;
        result.append(m);
    }
    return result;
}

QVariantList ChatQmlBridge::users() const {
    QVariantList result;
    for (const auto& u : m_manager->getUsers()) {
        QVariantMap m;
        m["id"] = u.id;
        m["name"] = u.name;
        m["avatar"] = u.avatar;
        m["color"] = u.color.name();
        m["status"] = static_cast<int>(u.status);
        m["statusText"] = u.statusText;
        m["isBot"] = u.isBot;
        result.append(m);
    }
    return result;
}

QVariantList ChatQmlBridge::messages() const {
    QVariantList result;
    QString activeId = m_manager->activeChannel();
    if (activeId.isEmpty()) return result;

    for (const auto& msg : m_manager->getMessages(activeId)) {
        QVariantMap m;
        m["id"] = msg.id;
        m["channelId"] = msg.channelId;
        m["authorId"] = msg.authorId;
        m["authorName"] = msg.authorName;
        m["type"] = static_cast<int>(msg.type);
        m["content"] = msg.content;
        m["timestamp"] = msg.timestamp.toString("hh:mm:ss");
        m["editedAt"] = msg.editedAt.toString("hh:mm:ss");
        m["replyToId"] = msg.replyToId;
        m["isDeleted"] = msg.isDeleted;

        QVariantMap reactions;
        for (auto it = msg.reactions.begin(); it != msg.reactions.end(); ++it) {
            reactions[it.key()] = it.value().size();
        }
        m["reactions"] = reactions;

        result.append(m);
    }
    return result;
}

void ChatQmlBridge::setUserName(const QString& name) {
    if (m_manager->myUserName() != name) {
        m_manager->setUserName(name);
        emit userNameChanged();
    }
}

void ChatQmlBridge::setHost(const QString& h) {
    if (m_host != h) { m_host = h; emit hostChanged(); }
}

void ChatQmlBridge::setPort(int p) {
    if (m_port != p) { m_port = p; emit portChanged(); }
}

void ChatQmlBridge::setActiveChannelId(const QString& id) {
    if (m_manager->activeChannel() != id) {
        m_manager->setActiveChannel(id);
    }
}

void ChatQmlBridge::connectToServer() {
    m_manager->connectToServer(m_host, static_cast<quint16>(m_port), m_manager->myUserName());
}

void ChatQmlBridge::disconnectFromServer() {
    m_manager->disconnectFromServer();
}

void ChatQmlBridge::startServer() {
    if (m_server->start(static_cast<quint16>(m_port))) {
        // Auto-connect to local server
        m_manager->connectToServer("localhost", static_cast<quint16>(m_port), m_manager->myUserName());
    }
}

void ChatQmlBridge::stopServer() {
    m_manager->disconnectFromServer();
    m_server->stop();
}

void ChatQmlBridge::createChannel(const QString& name, int type) {
    m_manager->createChannel(name, static_cast<ChannelType>(type));
}

void ChatQmlBridge::joinChannel(const QString& channelId) {
    m_manager->joinChannel(channelId);
}

void ChatQmlBridge::leaveChannel(const QString& channelId) {
    m_manager->leaveChannel(channelId);
}

void ChatQmlBridge::sendMessage(const QString& content, const QString& replyTo) {
    QString activeId = m_manager->activeChannel();
    if (!activeId.isEmpty() && !content.isEmpty()) {
        m_manager->sendMessage(activeId, content, MessageType::Text, replyTo);
    }
}

void ChatQmlBridge::editMessage(const QString& messageId, const QString& newContent) {
    m_manager->editMessage(messageId, newContent);
}

void ChatQmlBridge::deleteMessage(const QString& messageId) {
    m_manager->deleteMessage(messageId);
}

void ChatQmlBridge::addReaction(const QString& messageId, const QString& emoji) {
    m_manager->addReaction(messageId, emoji);
}

void ChatQmlBridge::removeReaction(const QString& messageId, const QString& emoji) {
    m_manager->removeReaction(messageId, emoji);
}

void ChatQmlBridge::sendTyping() {
    QString activeId = m_manager->activeChannel();
    if (!activeId.isEmpty()) {
        m_manager->sendTyping(activeId);
    }
}

QVariantMap ChatQmlBridge::getUser(const QString& userId) const {
    ChatUser u = m_manager->getUser(userId);
    QVariantMap m;
    m["id"] = u.id;
    m["name"] = u.name;
    m["avatar"] = u.avatar;
    m["color"] = u.color.name();
    m["status"] = static_cast<int>(u.status);
    m["statusText"] = u.statusText;
    m["isBot"] = u.isBot;
    return m;
}

QVariantList ChatQmlBridge::getChannelMessages(const QString& channelId) const {
    QVariantList result;
    for (const auto& msg : m_manager->getMessages(channelId)) {
        QVariantMap m;
        m["id"] = msg.id;
        m["authorId"] = msg.authorId;
        m["authorName"] = msg.authorName;
        m["content"] = msg.content;
        m["timestamp"] = msg.timestamp.toString("hh:mm:ss");
        m["isDeleted"] = msg.isDeleted;
        result.append(m);
    }
    return result;
}

void ChatQmlBridge::refreshChannels() { emit channelsChanged(); }
void ChatQmlBridge::refreshMessages() { emit messagesChanged(); }
void ChatQmlBridge::refreshUsers() { emit usersChanged(); }

} // namespace ks::chat
