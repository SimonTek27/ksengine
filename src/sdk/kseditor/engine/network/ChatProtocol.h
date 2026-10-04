#pragma once

#include <QString>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QVector>
#include <QMap>

namespace ks::chat {

enum class MessageType {
    Text,
    Code,
    File,
    Image,
    System,
    Edit,
    Delete,
    Reaction,
    Typing,
    ReadReceipt
};

enum class ChannelType {
    Direct,
    Group,
    Project
};

enum class UserStatus {
    Online,
    Idle,
    Dnd,
    Offline
};

enum class Permission {
    Read,
    Write,
    Invite,
    Kick,
    Ban,
    ManageChannel,
    Admin
};

struct ChatUser {
    QString id;
    QString name;
    QString avatar;
    QColor color;
    UserStatus status;
    QString statusText;
    qint64 lastSeen;
    bool isBot = false;

    QJsonObject toJson() const;
    static ChatUser fromJson(const QJsonObject& obj);
};

struct ChatMessage {
    QString id;
    QString channelId;
    QString authorId;
    QString authorName;
    MessageType type;
    QString content;
    QDateTime timestamp;
    QDateTime editedAt;
    QVector<QString> attachments;
    QMap<QString, QVector<QString>> reactions;
    QString replyToId;
    bool isDeleted = false;

    QJsonObject toJson() const;
    static ChatMessage fromJson(const QJsonObject& obj);
};

struct ChatChannel {
    QString id;
    QString name;
    QString description;
    ChannelType type;
    QVector<QString> members;
    QString owner;
    QDateTime createdAt;
    QDateTime lastMessageAt;
    int unreadCount = 0;
    bool isPinned = false;

    QJsonObject toJson() const;
    static ChatChannel fromJson(const QJsonObject& obj);
};

struct ChatServerInfo {
    QString id;
    QString name;
    QString icon;
    QString ownerId;
    QVector<ChatChannel> channels;
    QVector<QString> members;
    QDateTime createdAt;

    QJsonObject toJson() const;
    static ChatServerInfo fromJson(const QJsonObject& obj);
};

struct ProtocolMessage {
    QString type;
    QJsonObject payload;
    qint64 timestamp;

    QJsonObject toJson() const;
    static ProtocolMessage fromJson(const QJsonObject& obj);
    static ProtocolMessage make(const QString& type, const QJsonObject& payload);
};

} // namespace ks::chat
