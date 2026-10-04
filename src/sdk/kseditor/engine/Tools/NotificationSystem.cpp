#include "NotificationSystem.h"

#include <QUuid>
#include <QDebug>
#include <QTimer>

namespace ks {

// ─── Singleton ───────────────────────────────────────────────────────────────
NotificationCenter* NotificationCenter::s_instance = nullptr;

NotificationCenter* NotificationCenter::instance()
{
    if (!s_instance)
        s_instance = new NotificationCenter();
    return s_instance;
}

NotificationCenter::NotificationCenter(QObject* parent)
    : QObject(parent)
{}

NotificationCenter::~NotificationCenter()
{
    s_instance = nullptr;
}

// ─── Public API ──────────────────────────────────────────────────────────────

QString NotificationCenter::notify(const QString& title,
                                    const QString& message,
                                    NotificationType type,
                                    int durationMs)
{
    Notification n;
    n.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
    n.title     = title;
    n.message   = message;
    n.type      = type;
    n.timestamp = QDateTime::currentDateTime();
    n.duration  = durationMs;
    n.isRead    = false;

    m_notifications.insert(n.id, n);
    emit notificationAdded(n);

    qDebug() << "[Notification]" << title << "–" << message;

    // Auto-dismiss after duration (0 = persistent)
    if (durationMs > 0) {
        QTimer::singleShot(durationMs, this, [this, id = n.id]() {
            dismiss(id);
        });
    }

    return n.id;
}

void NotificationCenter::dismiss(const QString& id)
{
    if (!m_notifications.contains(id)) return;
    m_notifications.remove(id);
    emit notificationDismissed(id);
}

void NotificationCenter::dismissAll()
{
    m_notifications.clear();
    emit allDismissed();
}

void NotificationCenter::markRead(const QString& id)
{
    if (m_notifications.contains(id)) {
        m_notifications[id].isRead = true;
        emit notificationRead(id);
    }
}

void NotificationCenter::markAllRead()
{
    for (auto& n : m_notifications) n.isRead = true;
}

QVector<NotificationCenter::Notification> NotificationCenter::getAll() const
{
    return m_notifications.values().toVector();
}

QVector<NotificationCenter::Notification> NotificationCenter::getUnread() const
{
    QVector<Notification> result;
    for (const auto& n : m_notifications)
        if (!n.isRead) result << n;
    return result;
}

int NotificationCenter::getUnreadCount() const
{
    int count = 0;
    for (const auto& n : m_notifications)
        if (!n.isRead) ++count;
    return count;
}

} // namespace ks
