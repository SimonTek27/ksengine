#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
#include "Collaboration.h"

namespace ks {

class CollabEditorQmlBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY stateChanged)
    Q_PROPERTY(QVariantList users READ users NOTIFY usersChanged)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY userNameChanged)

public:
    static CollabEditorQmlBridge* instance();

    bool isConnected() const;
    QString connectionState() const;
    QVariantList users() const;
    QString host() const;
    int port() const;
    QString userName() const;

    Q_INVOKABLE void connectToServer();
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void sendChatMessage(const QString& message);
    Q_INVOKABLE void sendDocumentChange(const QString& docId, const QVariantMap& change);
    Q_INVOKABLE void openDocument(const QString& docId);
    Q_INVOKABLE void closeDocument(const QString& docId);
    Q_INVOKABLE void followUser(const QString& userId);
    Q_INVOKABLE void unfollowUser(const QString& userId);

    Q_INVOKABLE QVariantList getHistory() const;
    Q_INVOKABLE QVariantList getConflicts() const;
    Q_INVOKABLE void resolveConflicts();
    Q_INVOKABLE QVariantMap getPermissions() const;
    Q_INVOKABLE void setPermission(const QString& userId, const QString& permission, bool enabled);
    Q_INVOKABLE QVariantList getHistoryEvents() const;

    void setHost(const QString& h);
    void setPort(int p);
    void setUserName(const QString& name);

signals:
    void connectedChanged();
    void stateChanged();
    void usersChanged();
    void hostChanged();
    void portChanged();
    void userNameChanged();
    void chatMessageReceived(const QString& userId, const QString& message);
    void userJoined(const QString& userId, const QString& userName);
    void userLeft(const QString& userId, const QString& userName);
    void errorOccurred(const QString& message);
    void statusMessage(const QString& message);
    void followedUsersChanged();

private:
    explicit CollabEditorQmlBridge(QObject* parent = nullptr);
    static CollabEditorQmlBridge* s_instance;

    void rebuildUserList();

    CollaborationClient* m_client = nullptr;
    QString m_host;
    int m_port = 8080;
    QString m_userName = "User";
    QStringList m_followedUsers;
    QString m_documentId;
    int m_lastSyncedVersion = 0;
};

}

