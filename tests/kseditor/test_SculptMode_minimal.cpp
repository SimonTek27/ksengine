#include <QtTest/QtTest>
#include <QObject>

class TestMinimal : public QObject {
    Q_OBJECT
private slots:
    void testPass() { QVERIFY(true); }
};

QTEST_MAIN(TestMinimal)
#include "test_SculptMode_minimal.moc"
