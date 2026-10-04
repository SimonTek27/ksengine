#include "engine/Tools/NotificationSystem.h"
#include <QtTest/QtTest>

using namespace ks;

class TestNotificationSystem : public QObject {
    Q_OBJECT

private slots:
    void test_instance();
    void test_notify();
    void test_dismiss();
    void test_dismissAll();
    void test_markRead();
    void test_getUnread();
    void test_getUnreadCount();
};

void TestNotificationSystem::test_instance()
{
    NotificationCenter* nc = NotificationCenter::instance();
    QVERIFY(nc != nullptr);
    QCOMPARE(NotificationCenter::instance(), nc);
}

void TestNotificationSystem::test_notify()
{
    NotificationCenter* nc = NotificationCenter::instance();
    nc->dismissAll();

    QString id = nc->notify("Test Title", "Test Message", NotificationType::Info, 5000);
    QVERIFY(!id.isEmpty());

    auto all = nc->getAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].title, QString("Test Title"));
    QCOMPARE(all[0].message, QString("Test Message"));
    QCOMPARE(all[0].type, NotificationType::Info);
}

void TestNotificationSystem::test_dismiss()
{
    NotificationCenter* nc = NotificationCenter::instance();
    nc->dismissAll();

    QString id = nc->notify("Dismiss Test", "Will be dismissed");
    QCOMPARE(nc->getAll().size(), 1);

    nc->dismiss(id);
    QCOMPARE(nc->getAll().size(), 0);
}

void TestNotificationSystem::test_dismissAll()
{
    NotificationCenter* nc = NotificationCenter::instance();
    nc->dismissAll();

    nc->notify("One", "First");
    nc->notify("Two", "Second");
    QCOMPARE(nc->getAll().size(), 2);

    nc->dismissAll();
    QCOMPARE(nc->getAll().size(), 0);
}

void TestNotificationSystem::test_markRead()
{
    NotificationCenter* nc = NotificationCenter::instance();
    nc->dismissAll();

    QString id = nc->notify("Read Test", "Mark as read");
    auto unread = nc->getUnread();
    QCOMPARE(unread.size(), 1);

    nc->markRead(id);
    unread = nc->getUnread();
    QCOMPARE(unread.size(), 0);
}

void TestNotificationSystem::test_getUnread()
{
    NotificationCenter* nc = NotificationCenter::instance();
    nc->dismissAll();

    nc->notify("A", "Unread A");
    nc->notify("B", "Unread B");

    auto unread = nc->getUnread();
    QCOMPARE(unread.size(), 2);
}

void TestNotificationSystem::test_getUnreadCount()
{
    NotificationCenter* nc = NotificationCenter::instance();
    nc->dismissAll();

    QCOMPARE(nc->getUnreadCount(), 0);

    nc->notify("Count Test", "Check count");
    QCOMPARE(nc->getUnreadCount(), 1);

    QString id = nc->notify("Another", "Also unread");
    QCOMPARE(nc->getUnreadCount(), 2);

    nc->markRead(id);
    QCOMPARE(nc->getUnreadCount(), 1);

    nc->dismissAll();
    QCOMPARE(nc->getUnreadCount(), 0);
}

QTEST_MAIN(TestNotificationSystem)
#include "test_NotificationSystem.moc"
