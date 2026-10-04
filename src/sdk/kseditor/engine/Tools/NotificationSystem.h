#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QDateTime>
#include <QVector>

namespace ks {

enum class NotificationType {
    Info,
    Success,
    Warning,
    Error
};

class NotificationCenter : public QObject
{
    Q_OBJECT

public:
    static NotificationCenter* instance();

    struct Notification {
        QString id;
        QString title;
        QString message;
        NotificationType type;
        QDateTime timestamp;
        int duration = 0;
        bool isRead = false;
    };

    QString notify(const QString& title, const QString& message,
                   NotificationType type = NotificationType::Info,
                   int durationMs = 0);

    void dismiss(const QString& id);
    void dismissAll();
    void markRead(const QString& id);
    void markAllRead();

    QVector<Notification> getAll() const;
    QVector<Notification> getUnread() const;
    int getUnreadCount() const;

signals:
    void notificationAdded(const Notification& notification);
    void notificationDismissed(const QString& notificationId);
    void allDismissed();
    void notificationRead(const QString& notificationId);

private:
    NotificationCenter(QObject* parent = nullptr);
    ~NotificationCenter();
    Q_DISABLE_COPY(NotificationCenter)

    static NotificationCenter* s_instance;

    QMap<QString, Notification> m_notifications;
};

} // namespace ks
