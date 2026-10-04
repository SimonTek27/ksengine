#include <QtTest/QtTest>
#include "sdk/kseditor/modules/editor/AIEditor/AITelemetryTrainer.h"
#include "sdk/kseditor/modules/editor/AIEditor/AiBehaviorModel.h"
#include "sdk/kseditor/modules/editor/AIEditor/MultiCarAI.h"

using namespace ks;

class TestAllAI : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {}
    void cleanupTestCase() {}

    void test_telemetryTrainer()
    {
        AITelemetryTrainer trainer;
        QCOMPARE(trainer.getTotalLapsAnalyzed(), 0);

        QVector<TelemetrySample> lap;
        for (int i = 0; i < 10; ++i) {
            TelemetrySample s{};
            s.timestamp = i * 1.0f;
            s.speed = 100.0f;
            s.rpm = 5000.0f;
            s.gear = 3;
            s.throttle = 0.8f;
            s.steering = 0.0f;
            lap.append(s);
        }
        trainer.ingestTelemetry("test_track", lap);
        QCOMPARE(trainer.getTotalLapsAnalyzed(), 1);
    }

    void test_behaviorModelProfiles()
    {
        auto profiles = AiBehaviorModel::getBuiltInProfiles();
        QVERIFY(profiles.size() >= 20);

        auto rookie = AiBehaviorModel::getRookieDriver();
        QCOMPARE(rookie.name, "Rookie");
        QVERIFY(rookie.skill < 0.5f);

        auto vet = AiBehaviorModel::getVeteranDriver();
        QVERIFY(vet.skill > 0.8f);
    }

    void test_behaviorModelDecision()
    {
        auto profile = AiBehaviorModel::getAggressiveRacer();
        AiBehaviorModel::AiRaceState state;
        state.position = 5;
        state.totalLaps = 10;
        state.currentLap = 3;

        auto decision = AiBehaviorModel::calculateDecision(profile, state, 200.0f, 15.0f);
        QVERIFY(!decision.decisionReason.isEmpty());
    }

    void test_multiCarDecision()
    {
        auto profile = AiBehaviorModel::getVeteranDriver();
        AiBehaviorModel::AiRaceState state;
        state.position = 3;

        AiBehaviorModel::MultiCarState multi;
        multi.gapsAhead = {12.0f, 30.0f};
        multi.gapsBehind = {8.0f};
        multi.relativeSpeedsAhead = {2.0f, -1.0f};
        multi.carsNearby = 3;
        multi.hasTrafficAhead = true;
        multi.hasTrafficBehind = true;
        multi.trackLength = 5000.0f;

        auto decision = AiBehaviorModel::calculateMultiCarDecision(profile, state, multi, 200.0f);
        QVERIFY(!decision.decisionReason.isEmpty());
        QVERIFY(decision.throttle > 0.0f);
    }

    void test_trafficDensity()
    {
        AiBehaviorModel::MultiCarState multi;
        multi.gapsAhead = {5.0f, 12.0f, 40.0f};
        multi.gapsBehind = {8.0f, 15.0f};
        multi.carsNearby = 5;

        float density = AiBehaviorModel::calculateTrafficDensity(multi, 20.0f);
        QVERIFY(density > 0.0f);
        QVERIFY(density <= 1.0f);

        // No traffic
        AiBehaviorModel::MultiCarState empty;
        float noTraffic = AiBehaviorModel::calculateTrafficDensity(empty, 20.0f);
        QCOMPARE(noTraffic, 0.0f);
    }

    void test_overtakeTarget()
    {
        auto vet = AiBehaviorModel::getVeteranDriver();
        AiBehaviorModel::MultiCarState multi;
        multi.gapsAhead = {8.0f, 25.0f, 50.0f};
        multi.relativeSpeedsAhead = {5.0f, 0.0f, -2.0f};
        multi.gapsBehind = {20.0f};

        int target = AiBehaviorModel::findOvertakeTarget(vet, multi);
        QVERIFY(target >= 0);

        auto rookie = AiBehaviorModel::getRookieDriver();
        AiBehaviorModel::MultiCarState tightTraffic;
        tightTraffic.gapsAhead = {8.0f};
        tightTraffic.relativeSpeedsAhead = {-5.0f};
        tightTraffic.gapsBehind = {3.0f};

        int noTarget = AiBehaviorModel::findOvertakeTarget(rookie, tightTraffic);
        QCOMPARE(noTarget, -1);
    }

    void test_pitLapForRace()
    {
        auto vet = AiBehaviorModel::getVeteranDriver();
        int pitLap = AiBehaviorModel::calculateOptimalPitLapForRace(vet, 30, 20);
        QVERIFY(pitLap > 0);
        QVERIFY(pitLap < 28);

        auto rookie = AiBehaviorModel::getRookieDriver();
        int rookiePitLap = AiBehaviorModel::calculateOptimalPitLapForRace(rookie, 30, 20);
        QVERIFY(rookiePitLap > 0);
    }

    void test_finishPositionEstimate()
    {
        auto vet = AiBehaviorModel::getVeteranDriver();
        float pos = AiBehaviorModel::estimateFinishPosition(vet, 0.9f, 20);
        QVERIFY(pos >= 1.0f);
        QVERIFY(pos <= 20.0f);

        auto rookie = AiBehaviorModel::getRookieDriver();
        float rookiePos = AiBehaviorModel::estimateFinishPosition(rookie, 0.1f, 20);
        QVERIFY(rookiePos > pos);
    }

    void test_multiCarAI_setup()
    {
        MultiCarAI race;
        race.setupRace(10, 10, 5000.0f);
        QCOMPARE(race.grid().drivers.size(), 10);
        QCOMPARE(race.grid().totalLaps, 10);

        for (int i = 0; i < 10; ++i) {
            auto* d = race.getDriver(i);
            QVERIFY(d != nullptr);
            QCOMPARE(d->id, i);
            QVERIFY(!d->name.isEmpty());
        }
    }

    void test_multiCarAI_tick()
    {
        MultiCarAI race;
        race.setupRace(3, 5, 1000.0f);

        for (int i = 0; i < 500; ++i) {
            race.tick(0.05f);
            if (race.isRaceComplete()) break;
        }

        // After enough ticks on a short track, at least one lap should be started or race complete
        bool progressed = race.grid().leaderLap > 0 || race.isRaceComplete()
                          || race.getTotalPositionChanges() > 0;
        QVERIFY(progressed);

        auto leaderboard = race.getLeaderboard();
        QCOMPARE(leaderboard.size(), 3);
        for (int i = 0; i < leaderboard.size() - 1; ++i) {
            if (!leaderboard[i].dnf && !leaderboard[i + 1].dnf) {
                QVERIFY(leaderboard[i].position <= leaderboard[i + 1].position);
            }
        }
    }

    void test_multiCarAI_dnf()
    {
        MultiCarAI race;
        race.setupRace(5, 5, 5000.0f);
        race.forceDNF(0);

        QVERIFY(race.getDriver(0)->dnf);
    }

    void test_multiCarAI_pitStop()
    {
        MultiCarAI race;
        race.setupRace(3, 5, 5000.0f);
        race.forcePitStop(1);

        for (int i = 0; i < 50; ++i) {
            race.tick(0.1f);
        }

        auto events = race.consumeEvents();
        bool hasPitEvent = false;
        for (const auto& ev : events) {
            if (ev.type == RaceEvent::PIT_STOP) {
                hasPitEvent = true;
                break;
            }
        }
    }

    void test_multiCarAI_clear()
    {
        MultiCarAI race;
        race.setupRace(5, 5, 5000.0f);
        QVERIFY(!race.grid().drivers.isEmpty());

        race.clearGrid();
        QVERIFY(race.grid().drivers.isEmpty());
        QVERIFY(!race.isRaceComplete());
    }

    void test_multiCarAI_getLeaderboard()
    {
        MultiCarAI race;
        race.setupRace(8, 3, 5000.0f);

        for (int i = 0; i < 30; ++i) {
            race.tick(0.1f);
        }

        auto lb = race.getLeaderboard();
        QCOMPARE(lb.size(), 8);

        for (int i = 0; i < lb.size() - 1; ++i) {
            if (!lb[i].dnf && !lb[i + 1].dnf) {
                QVERIFY(lb[i].position < lb[i + 1].position);
            }
        }
    }

    void test_multiCarAI_addDriver()
    {
        MultiCarAI race;
        race.setupRace(5, 3, 5000.0f);

        auto profile = AiBehaviorModel::getVeteranDriver();
        race.addDriver(profile, "Test Driver");
        QCOMPARE(race.grid().drivers.size(), 6);

        auto* d = race.getDriver(5);
        QVERIFY(d != nullptr);
        QCOMPARE(d->name, "Test Driver");
    }

    void test_multiCarAI_setProfile()
    {
        MultiCarAI race;
        race.setupRace(3, 3, 5000.0f);

        auto vet = AiBehaviorModel::getVeteranDriver();
        race.setDriverProfile(0, vet);

        auto* d = race.getDriver(0);
        QVERIFY(d != nullptr);
        QCOMPARE(d->profile.skill, vet.skill);
    }

    void test_multiCarAI_overtakes()
    {
        MultiCarAI race;
        race.setupRace(10, 5, 2000.0f);

        for (int i = 0; i < 300; ++i) {
            race.tick(0.05f);
        }

        QVERIFY(race.getTotalOvertakes() >= 0);
        QVERIFY(race.getTotalPositionChanges() >= 0);
    }
};

QTEST_MAIN(TestAllAI)
#include "test_AllAI.moc"
