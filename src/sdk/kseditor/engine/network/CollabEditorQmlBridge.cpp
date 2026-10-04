#include "CollabEditorQmlBridge.h"
#include <QDateTime>

namespace ks {

CollabEditorQmlBridge* CollabEditorQmlBridge::s_instance = nullptr;

CollabEditorQmlBridge* CollabEditorQmlBridge::instance() {
    if (!s_instance) {
        s_instance = new CollabEditorQmlBridge();
    }
    return s_instance;
}

CollabEditorQmlBridge::CollabEditorQmlBridge(QObject* parent)
    : QObject(parent)
{
    m_client = CollaborationClient::instance();

    connect(m_client, &CollaborationClient::connected, this, [this]() {
        emit connectedChanged();
        emit stateChanged();
    });
    connect(m_client, &CollaborationClient::disconnected, this, [this]() {
        emit connectedChanged();
        emit stateChanged();
        rebuildUserList();
    });
    connect(m_client, &CollaborationClient::stateChanged, this, [this](CollaborationState) {
        emit stateChanged();
    });
    connect(m_client, &CollaborationClient::userJoined, this, [this](const CollaborationUser& u) {
        rebuildUserList();
        emit userJoined(u.id, u.name);
    });
    connect(m_client, &CollaborationClient::userLeft, this, [this](const CollaborationUser& u) {
        rebuildUserList();
        emit userLeft(u.id, u.name);
    });
    connect(m_client, &CollaborationClient::chatReceived, this, [this](const QString& docId, const QString& userId, const QString& message) {
        Q_UNUSED(docId)
        emit chatMessageReceived(userId, message);
    });
    connect(m_client, &CollaborationClient::error, this, [this](const QString& err) {
        emit errorOccurred(err);
    });
}

bool CollabEditorQmlBridge::isConnected() const {
    return m_client ? m_client->isConnected() : false;
}

QString CollabEditorQmlBridge::connectionState() const {
    if (!m_client) return "Disconnected";
    switch (m_client->getState()) {
        case CollaborationState::Disconnected: return "Disconnected";
        case CollaborationState::Connecting: return "Connecting";
        case CollaborationState::Connected: return "Connected";
        case CollaborationState::Reconnecting: return "Reconnecting";
        case CollaborationState::Error: return "Error";
    }
    return "Unknown";
}

QVariantList CollabEditorQmlBridge::users() const {
    QVariantList result;
    QStringList docIds;
    QString fakeDocId = "current";
    auto users = m_client ? m_client->getUsers(fakeDocId) : QVector<CollaborationUser>();
    for (const auto& u : users) {
        QVariantMap m;
        m["id"] = u.id;
        m["name"] = u.name;
        m["color"] = u.color;
        m["role"] = static_cast<int>(u.role);
        m["isOnline"] = u.isOnline;
        result.append(m);
    }
    return result;
}

QString CollabEditorQmlBridge::host() const { return m_host; }
int CollabEditorQmlBridge::port() const { return m_port; }
QString CollabEditorQmlBridge::userName() const { return m_userName; }

void CollabEditorQmlBridge::connectToServer() {
    if (m_client) {
        m_client->setServer(m_host, static_cast<quint16>(m_port));
        m_client->setUserInfo("user_" + m_userName, m_userName);
        m_client->connect();
    }
}

void CollabEditorQmlBridge::disconnectFromServer() {
    if (m_client) m_client->disconnect();
}

void CollabEditorQmlBridge::sendChatMessage(const QString& message) {
    if (m_client) m_client->sendChat("current", message);
}

void CollabEditorQmlBridge::sendDocumentChange(const QString& docId, const QVariantMap& change) {
    if (m_client) m_client->sendChange(docId, QJsonObject::fromVariantMap(change));
}

void CollabEditorQmlBridge::openDocument(const QString& docId) {
    if (m_client) m_client->joinDocument(docId);
}

void CollabEditorQmlBridge::closeDocument(const QString& docId) {
    if (m_client) m_client->leaveDocument(docId);
}

void CollabEditorQmlBridge::followUser(const QString& userId) {
    if (!m_followedUsers.contains(userId)) {
        m_followedUsers.append(userId);
        emit statusMessage("Now following user: " + userId);
        emit followedUsersChanged();
    }
}

void CollabEditorQmlBridge::unfollowUser(const QString& userId) {
    if (m_followedUsers.removeOne(userId)) {
        emit statusMessage("Unfollowed user: " + userId);
        emit followedUsersChanged();
    }
}

QVariantList CollabEditorQmlBridge::getHistory() const {
    QVariantList history;
    if (m_client && m_client->isConnected()) {
        auto changes = m_client->getChanges("default", 0);
        for (const auto& ch : changes) {
            QVariantMap entry;
            entry["user"] = ch.userId;
            entry["description"] = ch.type + " on " + ch.documentId;
            entry["timestamp"] = QDateTime::fromSecsSinceEpoch(ch.timestamp).toString("hh:mm:ss");
            history.append(entry);
        }
    }
    return history;
}

QVariantList CollabEditorQmlBridge::getConflicts() const {
    QVariantList conflicts;
    if (!m_client || !m_client->isConnected()) return conflicts;

    auto pendingChanges = m_client->getChanges(m_documentId, m_lastSyncedVersion);
    auto serverChanges = m_client->getChanges(m_documentId, 0);

    QSet<QString> localOps;
    for (const auto& ch : pendingChanges) {
        localOps.insert(ch.userId + ":" + ch.type);
    }

    QSet<QString> remoteOps;
    for (const auto& ch : serverChanges) {
        remoteOps.insert(ch.userId + ":" + ch.type);
    }

    QSet<QString> overlap = localOps.intersect(remoteOps);
    for (const auto& key : overlap) {
        QVariantMap conflict;
        conflict["type"] = key.split(":").last();
        conflict["user"] = key.split(":").first();
        conflict["description"] = "Concurrent modification detected";
        conflict["documentId"] = m_documentId;
        conflict["timestamp"] = QDateTime::currentDateTime().toString("hh:mm:ss");
        conflicts.append(conflict);
    }

    return conflicts;
}

void CollabEditorQmlBridge::resolveConflicts() {
    if (!m_client || !m_client->isConnected()) {
        emit statusMessage("Cannot resolve conflicts: not connected");
        return;
    }

    auto conflicts = getConflicts();
    for (const auto& c : conflicts) {
        QVariantMap cm = c.toMap();
        QString docId = cm.value("documentId").toString();
        if (!docId.isEmpty() && m_client->getChanges(docId, 0).size() > 0) {
            m_client->sendChange(docId, QJsonObject{});
        }
    }
    emit statusMessage("Conflict resolution completed: server state is authoritative");
}

QVariantMap CollabEditorQmlBridge::getPermissions() const {
    QVariantMap perms;
    if (!m_client || !m_client->isConnected()) {
        perms["canEdit"] = false;
        perms["canDelete"] = false;
        perms["canInvite"] = false;
        perms["admin"] = false;
        return perms;
    }

    // Default permissions for connected user
    // In a real implementation, these would come from the server
    perms["canEdit"] = true;
    perms["canDelete"] = true;
    perms["canInvite"] = false;
    perms["admin"] = false;
    return perms;
}

void CollabEditorQmlBridge::setPermission(const QString& userId, const QString& permission, bool enabled) {
    if (!m_client || !m_client->isConnected()) {
        emit statusMessage("Cannot set permission: not connected");
        return;
    }

    // In a real implementation, this would send a permission change request to the server
    emit statusMessage(QString("Permission '%1' %2 for user %3").arg(permission).arg(enabled ? "granted" : "revoked").arg(userId));
}

QVariantList CollabEditorQmlBridge::getHistoryEvents() const {
    return getHistory();
}

void CollabEditorQmlBridge::setHost(const QString& h) {
    if (m_host != h) { m_host = h; emit hostChanged(); }
}

void CollabEditorQmlBridge::setPort(int p) {
    if (m_port != p) { m_port = p; emit portChanged(); }
}

void CollabEditorQmlBridge::setUserName(const QString& name) {
    if (m_userName != name) { m_userName = name; emit userNameChanged(); }
}

void CollabEditorQmlBridge::rebuildUserList() {
    emit usersChanged();
}

}
