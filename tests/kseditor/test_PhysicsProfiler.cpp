#include <QtTest>`r`n#include <cstdio>`r`n#include <cstdlib>
#include <QThread>

#include "sdk/kseditor/modules/PhysicsEditor/PhysicsProfiler.h"

using namespace ks;

class TestPhysicsProfiler : public QObject {
    Q_OBJECT

private slots:
    void test_singleton();
    void test_frameTiming();
    void test_subsystemTiming();
    void test_enableDisable();
    void test_reset();
    void test_bottleneckDetection();
    void test_allSubsystemTimes();
    void test_allSubsystemPercentages();
    void test_scopedProfile();
    void test_multipleFrames();
};

void TestPhysicsProfiler::test_singleton() {
    auto* p1 = PhysicsProfiler::instance();
    auto* p2 = PhysicsProfiler::instance();
    QCOMPARE(p1, p2);
    QVERIFY(p1 != nullptr);
}

void TestPhysicsProfiler::test_frameTiming() {
    auto* prof = PhysicsProfiler::instance();
    prof->reset();

    prof->beginFrame();
    QThread::msleep(5);
    prof->endFrame();

    QVERIFY(prof->frameTimeMs() >= 4.0);
    QVERIFY(prof->frameTimeMs() <= 50.0);
    QVERIFY(prof->frameCount() > 0);
    QVERIFY(prof->peakFrameTimeMs() >= prof->frameTimeMs());
}

void TestPhysicsProfiler::test_subsystemTiming() {
    auto* prof = PhysicsProfiler::instance();
    prof->reset();

    prof->beginFrame();
    {
        prof->beginSubsystem(PhysicsProfiler::Engine);
        QThread::msleep(40);
        prof->endSubsystem(PhysicsProfiler::Engine);
    }
    {
        prof->beginSubsystem(PhysicsProfiler::Brakes);
        QThread::msleep(5);
        prof->endSubsystem(PhysicsProfiler::Brakes);
    }
    prof->endFrame();

    double engineTime = prof->subsystemTimeMs(PhysicsProfiler::Engine);
    double brakeTime = prof->subsystemTimeMs(PhysicsProfiler::Brakes);
    QVERIFY(engineTime >= 1.5);
    QVERIFY(brakeTime >= 0.5);
    QVERIFY(engineTime > brakeTime);
}

void TestPhysicsProfiler::test_enableDisable() {
    auto* prof = PhysicsProfiler::instance();
    prof->reset();

    // When disabled, frame count should not increase
    prof->setEnabled(false);
    QVERIFY(!prof->isEnabled());
    prof->beginFrame();
    QThread::msleep(2);
    prof->endFrame();
    QCOMPARE(prof->frameCount(), 0);

    // When enabled, frame count increases
    prof->setEnabled(true);
    QVERIFY(prof->isEnabled());
    prof->beginFrame();
    QThread::msleep(2);
    prof->endFrame();
    QCOMPARE(prof->frameCount(), 1);
}

void TestPhysicsProfiler::test_reset() {
    auto* prof = PhysicsProfiler::instance();

    prof->beginFrame();
    QThread::msleep(2);
    prof->endFrame();
    QVERIFY(prof->frameCount() > 0);

    prof->reset();
    QCOMPARE(prof->frameCount(), 0);
    QCOMPARE(prof->frameTimeMs(), 0.0);
    QCOMPARE(prof->peakFrameTimeMs(), 0.0);
    QCOMPARE(prof->avgFrameTimeMs(), 0.0);
    QCOMPARE(prof->totalSimTimeMs(), 0.0);
}

void TestPhysicsProfiler::test_bottleneckDetection() {
    auto* prof = PhysicsProfiler::instance();
    prof->reset();

    prof->beginFrame();

    prof->beginSubsystem(PhysicsProfiler::Engine);
    QThread::msleep(30);
    prof->endSubsystem(PhysicsProfiler::Engine);

    prof->beginSubsystem(PhysicsProfiler::Aero);
    // minimal sleep so Engine is clearly the bottleneck
    prof->endSubsystem(PhysicsProfiler::Aero);

    bool caught = false;
    const auto conn = QObject::connect(prof, &PhysicsProfiler::bottleneckDetected,
                     [&](const QString& sub, double pct) {
                         if (sub == "Engine" && pct > 50.0) caught = true;
                     });

    prof->endFrame();
    // The lambda captures locals: disconnect before this slot frame goes away.
    QObject::disconnect(conn);

    QVERIFY(caught);
}

void TestPhysicsProfiler::test_allSubsystemTimes() {
    auto* prof = PhysicsProfiler::instance();
    prof->reset();

    prof->beginFrame();
    prof->beginSubsystem(PhysicsProfiler::TireThermal);
    QThread::msleep(1);
    prof->endSubsystem(PhysicsProfiler::TireThermal);
    prof->beginSubsystem(PhysicsProfiler::Fuel);
    QThread::msleep(1);
    prof->endSubsystem(PhysicsProfiler::Fuel);
    prof->endFrame();

    auto times = prof->allSubsystemTimes();
    QVERIFY(times.size() >= 2);
    QVERIFY(!times[0].first.isEmpty());
    QVERIFY(times[0].second > 0);
}

void TestPhysicsProfiler::test_allSubsystemPercentages() {
    auto* prof = PhysicsProfiler::instance();
    prof->reset();

    prof->beginFrame();
    prof->beginSubsystem(PhysicsProfiler::Engine);
    QThread::msleep(3);
    prof->endSubsystem(PhysicsProfiler::Engine);
    prof->endFrame();

    auto pcts = prof->allSubsystemPercentages();
    QVERIFY(!pcts.isEmpty());
    for (const auto& p : pcts) {
        QVERIFY(p.toMap()["value"].toDouble() >= 0.0);
    }
}

void TestPhysicsProfiler::test_scopedProfile() {
    auto* prof = PhysicsProfiler::instance();
    prof->reset();

    prof->beginFrame();
    {
        ScopedProfile s(PhysicsProfiler::Drivetrain);
        QThread::msleep(2);
    }
    prof->endFrame();

    QVERIFY(prof->subsystemTimeMs(PhysicsProfiler::Drivetrain) >= 1.0);
}

void TestPhysicsProfiler::test_multipleFrames() {
    auto* prof = PhysicsProfiler::instance();
    prof->reset();

    for (int i = 0; i < 5; ++i) {
        prof->beginFrame();
        {
            ScopedProfile s(PhysicsProfiler::Engine);
            QThread::msleep(1);
        }
        prof->endFrame();
    }

    QCOMPARE(prof->frameCount(), 5);
    QVERIFY(prof->avgFrameTimeMs() > 0);
    QVERIFY(prof->fps() > 0);
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestPhysicsProfiler tc;
    const int rc = QTest::qExec(&tc, argc, argv);
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(rc);
}
#include "test_PhysicsProfiler.moc"
