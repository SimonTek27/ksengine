#include "ChatProtocol.h"
#include <QJsonDocument>

namespace ks::chat {

// --- ChatUser ---

QJsonObject ChatUser::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["avatar"] = avatar;
    obj["color"] = color.name();
    obj["status"] = static_cast<int>(status);
    obj["statusText"] = statusText;
    obj["lastSeen"] = lastSeen;
    obj["isBot"] = isBot;
    return obj;
}

ChatUser ChatUser::fromJson(const QJsonObject& obj) {
    ChatUser u;
    u.id = obj["id"].toString();
    u.name = obj["name"].toString();
    u.avatar = obj["avatar"].toString();
    u.color = QColor(obj["color"].toString());
    u.status = static_cast<UserStatus>(obj["status"].toInt());
    u.statusText = obj["statusText"].toString();
    u.lastSeen = obj["lastSeen"].toInteger();
    u.isBot = obj["isBot"].toBool();
    return u;
}

// --- ChatMessage ---

QJsonObject ChatMessage::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["channelId"] = channelId;
    obj["authorId"] = authorId;
    obj["authorName"] = authorName;
    obj["type"] = static_cast<int>(type);
    obj["content"] = content;
    obj["timestamp"] = timestamp.toMSecsSinceEpoch();
    obj["editedAt"] = editedAt.toMSecsSinceEpoch();
    obj["replyToId"] = replyToId;
    obj["isDeleted"] = isDeleted;

    QJsonArray attArr;
    for (const auto& a : attachments) attArr.append(a);
    obj["attachments"] = attArr;

    QJsonObject reactObj;
    for (auto it = reactions.begin(); it != reactions.end(); ++it) {
        QJsonArray arr;
        for (const auto& uid : it.value()) arr.append(uid);
        reactObj[it.key()] = arr;
    }
    obj["reactions"] = reactObj;

    return obj;
}

ChatMessage ChatMessage::fromJson(const QJsonObject& obj) {
    ChatMessage m;
    m.id = obj["id"].toString();
    m.channelId = obj["channelId"].toString();
    m.authorId = obj["authorId"].toString();
    m.authorName = obj["authorName"].toString();
    m.type = static_cast<MessageType>(obj["type"].toInt());
    m.content = obj["content"].toString();
    m.timestamp = QDateTime::fromMSecsSinceEpoch(obj["timestamp"].toInteger());
    m.editedAt = QDateTime::fromMSecsSinceEpoch(obj["editedAt"].toInteger());
    m.replyToId = obj["replyToId"].toString();
    m.isDeleted = obj["isDeleted"].toBool();

    for (const auto& v : obj["attachments"].toArray())
        m.attachments.append(v.toString());

    QJsonObject reactObj = obj["reactions"].toObject();
    for (auto it = reactObj.begin(); it != reactObj.end(); ++it) {
        QVector<QString> uids;
        for (const auto& v : it.value().toArray())
            uids.append(v.toString());
        m.reactions[it.key()] = uids;
    }

    return m;
}

// --- ChatChannel ---

QJsonObject ChatChannel::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["description"] = description;
    obj["type"] = static_cast<int>(type);
    obj["owner"] = owner;
    obj["createdAt"] = createdAt.toMSecsSinceEpoch();
    obj["lastMessageAt"] = lastMessageAt.toMSecsSinceEpoch();
    obj["unreadCount"] = unreadCount;
    obj["isPinned"] = isPinned;

    QJsonArray memArr;
    for (const auto& m : members) memArr.append(m);
    obj["members"] = memArr;

    return obj;
}

ChatChannel ChatChannel::fromJson(const QJsonObject& obj) {
    ChatChannel c;
    c.id = obj["id"].toString();
    c.name = obj["name"].toString();
    c.description = obj["description"].toString();
    c.type = static_cast<ChannelType>(obj["type"].toInt());
    c.owner = obj["owner"].toString();
    c.createdAt = QDateTime::fromMSecsSinceEpoch(obj["createdAt"].toInteger());
    c.lastMessageAt = QDateTime::fromMSecsSinceEpoch(obj["lastMessageAt"].toInteger());
    c.unreadCount = obj["unreadCount"].toInt();
    c.isPinned = obj["isPinned"].toBool();

    for (const auto& v : obj["members"].toArray())
        c.members.append(v.toString());

    return c;
}

// --- ChatServerInfo ---

QJsonObject ChatServerInfo::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["icon"] = icon;
    obj["ownerId"] = ownerId;
    obj["createdAt"] = createdAt.toMSecsSinceEpoch();

    QJsonArray chArr;
    for (const auto& ch : channels) chArr.append(ch.toJson());
    obj["channels"] = chArr;

    QJsonArray memArr;
    for (const auto& m : members) memArr.append(m);
    obj["members"] = memArr;

    return obj;
}

ChatServerInfo ChatServerInfo::fromJson(const QJsonObject& obj) {
    ChatServerInfo s;
    s.id = obj["id"].toString();
    s.name = obj["name"].toString();
    s.icon = obj["icon"].toString();
    s.ownerId = obj["ownerId"].toString();
    s.createdAt = QDateTime::fromMSecsSinceEpoch(obj["createdAt"].toInteger());

    for (const auto& v : obj["channels"].toArray())
        s.channels.append(ChatChannel::fromJson(v.toObject()));

    for (const auto& v : obj["members"].toArray())
        s.members.append(v.toString());

    return s;
}

// --- ProtocolMessage ---

QJsonObject ProtocolMessage::toJson() const {
    QJsonObject obj;
    obj["type"] = type;
    obj["payload"] = payload;
    obj["timestamp"] = timestamp;
    return obj;
}

ProtocolMessage ProtocolMessage::fromJson(const QJsonObject& obj) {
    ProtocolMessage pm;
    pm.type = obj["type"].toString();
    pm.payload = obj["payload"].toObject();
    pm.timestamp = obj["timestamp"].toInteger();
    return pm;
}

ProtocolMessage ProtocolMessage::make(const QString& type, const QJsonObject& payload) {
    ProtocolMessage pm;
    pm.type = type;
    pm.payload = payload;
    pm.timestamp = QDateTime::currentMSecsSinceEpoch();
    return pm;
}

} // namespace ks::chat
