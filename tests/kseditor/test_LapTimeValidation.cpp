#include <QtTest>
#include <QVector>
#include <cmath>

#include "LapTimeValidation.h"

using namespace ks;

class TestLapTimeValidation : public QObject {
    Q_OBJECT

private slots:
    void test_singleton();
    void test_basicValidation();
    void test_fuelWeightPenalty();
    void test_tireWearPenalty();
    void test_historicalLaps();
    void test_sectorRecording();
    void test_sectorProjection();
    void test_resetHistory();
    void test_confidenceScoring();
    void test_validationReport();

    void initTestCase();
};

void TestLapTimeValidation::initTestCase() {
}

void TestLapTimeValidation::test_singleton() {
    auto* v1 = LapTimeValidation::instance();
    auto* v2 = LapTimeValidation::instance();
    QCOMPARE(v1, v2);
    QVERIFY(v1 != nullptr);
}

void TestLapTimeValidation::test_basicValidation() {
    auto* val = LapTimeValidation::instance();
    val->resetHistory();

    double baseLap = 95.0;
    QVector<double> history = {94.8, 95.2, 95.0, 94.9};

    auto result = val->validateEstimate(baseLap, 40.0, 4.0, history, 15.0, 0.02);

    QVERIFY(result.predictedLapTime > baseLap);
    QVERIFY(result.confidencePercent > 0);
    QVERIFY(result.confidencePercent <= 100);
    QVERIFY(result.fuelWeightPenalty > 0);
    QVERIFY(result.tireWearPenalty > 0);
}

void TestLapTimeValidation::test_fuelWeightPenalty() {
    auto* val = LapTimeValidation::instance();
    val->resetHistory();

    double baseLap = 100.0;
    double penaltyLight = val->estimateWithFuelWeight(baseLap, 10.0, 4.0);
    double penaltyHeavy = val->estimateWithFuelWeight(baseLap, 80.0, 4.0);

    QVERIFY(penaltyHeavy > penaltyLight);
    QVERIFY(penaltyLight > baseLap);
}

void TestLapTimeValidation::test_tireWearPenalty() {
    auto* val = LapTimeValidation::instance();
    val->resetHistory();

    double baseLap = 100.0;
    double penaltyLow = val->estimateWithTireWear(baseLap, 10.0);
    double penaltyHigh = val->estimateWithTireWear(baseLap, 80.0);

    QVERIFY(penaltyHigh > penaltyLow);
    QVERIFY(penaltyLow > baseLap);
}

void TestLapTimeValidation::test_historicalLaps() {
    auto* val = LapTimeValidation::instance();
    val->resetHistory();

    double baseLap = 100.0;

    // Consistent laps -> higher confidence
    QVector<double> consistent = {100.0, 100.1, 99.9, 100.0};
    auto r1 = val->validateEstimate(baseLap, 0, 1.0, consistent);

    // Inconsistent laps -> lower confidence
    QVector<double> erratic = {95.0, 105.0, 98.0, 110.0};
    auto r2 = val->validateEstimate(baseLap, 0, 1.0, erratic);

    QVERIFY(r1.confidencePercent >= r2.confidencePercent);
}

void TestLapTimeValidation::test_sectorRecording() {
    auto* val = LapTimeValidation::instance();
    val->resetHistory();

    val->recordSectorTimes(30.0, 32.0, 33.0);
    val->recordSectorTimes(29.5, 31.8, 32.5);

    auto hist = val->sectorHistory();
    QCOMPARE(hist.sector1.size(), 2);
    QCOMPARE(hist.sector2.size(), 2);
    QCOMPARE(hist.sector3.size(), 2);
    QCOMPARE(hist.bestSector1, 29.5);
    QCOMPARE(hist.bestSector2, 31.8);
    QCOMPARE(hist.bestSector3, 32.5);
}

void TestLapTimeValidation::test_sectorProjection() {
    auto* val = LapTimeValidation::instance();
    val->resetHistory();

    val->recordSectorTimes(30.0, 32.0, 33.0);
    val->recordSectorTimes(29.5, 31.5, 32.5);

    double projected = val->sectorHistory().projectedBestLap();
    QCOMPARE(projected, 29.5 + 31.5 + 32.5);
}

void TestLapTimeValidation::test_resetHistory() {
    auto* val = LapTimeValidation::instance();

    val->recordSectorTimes(30.0, 32.0, 33.0);
    QVERIFY(!val->sectorHistory().sector1.isEmpty());

    val->resetHistory();
    QVERIFY(val->sectorHistory().sector1.isEmpty());
    QCOMPARE(val->sectorHistory().bestSector1, 1e9);
}

void TestLapTimeValidation::test_confidenceScoring() {
    auto* val = LapTimeValidation::instance();
    val->resetHistory();
    val->recordSectorTimes(30.0, 32.0, 33.0);

    // Many historical laps + no factors = high confidence
    QVector<double> many(20, 100.0);
    double c1 = val->calculateConfidence(many, 0);
    QVERIFY(c1 > 90.0);

    // Few laps + many factors = lower confidence
    QVector<double> few = {100.0};
    double c2 = val->calculateConfidence(few, 4);
    QVERIFY(c2 < c1);
}

void TestLapTimeValidation::test_validationReport() {
    auto* val = LapTimeValidation::instance();
    val->resetHistory();

    QVector<double> history = {100.0, 99.5, 100.2};
    auto result = val->validateEstimate(100.0, 60.0, 4.5, history, 30.0, 0.05);

    QJsonObject report = val->generateValidationReport(result);
    QVERIFY(report.contains("predictedLapTime"));
    QVERIFY(report.contains("confidencePercent"));
    QVERIFY(report.contains("fuelWeightPenalty"));
    QVERIFY(report.contains("limitingFactors"));
    QVERIFY(report["limitingFactors"].toArray().size() > 0);
}

QTEST_MAIN(TestLapTimeValidation)
#include "test_LapTimeValidation.moc"
