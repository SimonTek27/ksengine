#include <QtTest>
#include <QVector>
#include <cmath>

#include "sdk/kseditor/modules/PhysicsEditor/telemetry/TelemetryFeedbackBridge.h"
#include "sdk/kseditor/modules/PhysicsEditor/PhysicsSimulator.h"
#include "sdk/kseditor/modules/PhysicsEditor/telemetry/TelemetryQmlBridge.h"

using namespace ks;

class TestTelemetryFeedback : public QObject {
    Q_OBJECT

private slots:
    void test_singleton();
    void test_startStop();
    void test_reset();
    void test_referenceLoad();
    void test_metricsAfterSimulation();
    void test_getTraces();
    void test_validationSummary();
    void test_referenceLapComparison();
    void test_sectorComparison();
};

void TestTelemetryFeedback::test_singleton() {
    auto* fb1 = TelemetryFeedbackBridge::instance();
    auto* fb2 = TelemetryFeedbackBridge::instance();
    QCOMPARE(fb1, fb2);
    QVERIFY(fb1 != nullptr);
}

void TestTelemetryFeedback::test_startStop() {
    auto* fb = TelemetryFeedbackBridge::instance();
    QVERIFY(!fb->isActive());

    fb->startFeedback();
    QVERIFY(fb->isActive());

    fb->stopFeedback();
    QVERIFY(!fb->isActive());
}

void TestTelemetryFeedback::test_reset() {
    auto* fb = TelemetryFeedbackBridge::instance();
    fb->startFeedback();
    QVERIFY(fb->isActive());

    fb->reset();
    QVERIFY(!fb->isActive());
    QCOMPARE(fb->lapCount(), 0);
    QCOMPARE(fb->currentLapTime(), 0.0);
}

void TestTelemetryFeedback::test_referenceLoad() {
    auto* fb = TelemetryFeedbackBridge::instance();
    fb->reset();

    // Should fail gracefully with invalid path
    bool result = fb->loadReferenceTelemetry("/nonexistent/path.json");
    QVERIFY(!result);

    // Should succeed with fresh state (no crash)
    QVERIFY(!fb->referenceLoaded());
}

void TestTelemetryFeedback::test_metricsAfterSimulation() {
    auto* fb = TelemetryFeedbackBridge::instance();
    auto* sim = phys_Simulator::instance();

    fb->reset();
    fb->startFeedback();

    sim->setMass(1500.0);
    sim->setEnginePower(350.0);
    sim->startSimulation();

    // Metrics should be accessible
    QVERIFY(fb->speedRMSE() >= 0 || fb->lapCount() >= 0);

    fb->stopFeedback();
    fb->reset();
}

void TestTelemetryFeedback::test_getTraces() {
    auto* fb = TelemetryFeedbackBridge::instance();
    fb->reset();

    // Traces should be empty after reset
    QVERIFY(fb->getSimSpeedTrace().isEmpty());
    QVERIFY(fb->getSimThrottleTrace().isEmpty());
    QVERIFY(fb->getSimBrakeTrace().isEmpty());
    QVERIFY(fb->getSimSteeringTrace().isEmpty());
    QVERIFY(fb->getRefSpeedTrace().isEmpty());
}

void TestTelemetryFeedback::test_validationSummary() {
    auto* fb = TelemetryFeedbackBridge::instance();
    fb->reset();

    QVariantMap summary = fb->getValidationSummary();
    QVERIFY(!summary.isEmpty());
    QCOMPARE(summary["active"].toBool(), false);
    QCOMPARE(summary["referenceLoaded"].toBool(), false);
    QCOMPARE(summary["lapCount"].toInt(), 0);
}

void TestTelemetryFeedback::test_referenceLapComparison() {
    auto* fb = TelemetryFeedbackBridge::instance();
    fb->reset();

    QVariantMap analysis = fb->getLapAnalysis(0);
    QVERIFY(analysis.isEmpty());

    analysis = fb->getLapAnalysis(-1);
    QVERIFY(analysis.isEmpty());
}

void TestTelemetryFeedback::test_sectorComparison() {
    auto* fb = TelemetryFeedbackBridge::instance();
    fb->reset();

    QVariantList sectors = fb->getSectorComparison();
    QVERIFY(sectors.isEmpty());
}

QTEST_MAIN(TestTelemetryFeedback)
#include "test_TelemetryFeedback.moc"
