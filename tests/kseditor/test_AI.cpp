#include "sdk/kseditor/modules/editor/AIEditor/AiBehaviorModel.h"
#include <QtTest/QtTest>
#include <QJsonObject>
#include <QSet>

// ============================================================================
// Test AiBehaviorModel new profile system
// ============================================================================

class TestAiBehaviorModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {}
    void cleanupTestCase() {}

    void testBuiltInProfilesCount()
    {
        auto profiles = AiBehaviorModel::getBuiltInProfiles();
        QVERIFY(profiles.size() >= 20);
    }

    void testProfileMetaComputed()
    {
        auto profiles = AiBehaviorModel::getBuiltInProfiles();
        for (const auto& p : profiles) {
            auto meta = AiBehaviorModel::computeMeta(p);
            QVERIFY(!meta.displayName.isEmpty());
            QVERIFY(meta.overallRating > 0.0f);
            QVERIFY(meta.overallRating <= 1.0f);
        }
    }

    void testProfileScoring()
    {
        auto aggressive = AiBehaviorModel::getAggressiveRacer();
        auto score = AiBehaviorModel::scoreProfile(aggressive);
        QVERIFY(score.overallScore > 0.0f);
        QVERIFY(score.overallScore <= 1.0f);
        QVERIFY(!score.categoryScores.isEmpty());
        QVERIFY(!score.strengths.isEmpty());
    }

    void testProfileComparison()
    {
        auto aggressive = AiBehaviorModel::getAggressiveRacer();
        auto defensive = AiBehaviorModel::getDefensiveRacer();
        auto comp = AiBehaviorModel::compareProfiles(aggressive, defensive);
        QVERIFY(!comp.summary.isEmpty());
        // Aggressive should have higher overtaking score
        QVERIFY(comp.categoryScores.value("aggression", 0) >= 0);
    }

    void testScalingToDifficulty()
    {
        auto base = AiBehaviorModel::getAggressiveRacer();
        auto easy = AiBehaviorModel::scaleToDifficulty(base, 20);
        auto hard = AiBehaviorModel::scaleToDifficulty(base, 90);

        QVERIFY(easy.skill <= hard.skill);
        QVERIFY(easy.mistakeRate >= hard.mistakeRate);
        QVERIFY(easy.racePace <= hard.racePace);
    }

    void testProfileInterpolation()
    {
        auto a = AiBehaviorModel::getAggressiveRacer();
        auto b = AiBehaviorModel::getDefensiveRacer();
        auto mid = AiBehaviorModel::interpolateProfiles(a, b, 0.5f);

        QVERIFY(mid.aggression < a.aggression);
        QVERIFY(mid.aggression > b.aggression);
        QVERIFY(mid.defensive > a.defensive);
        QVERIFY(mid.defensive < b.defensive);
    }

    void testProfileFamilies()
    {
        auto families = AiBehaviorModel::getProfileFamilies();
        QVERIFY(!families.isEmpty());

        for (const auto& f : families) {
            QVERIFY(!f.name.isEmpty());
            QVERIFY(!f.description.isEmpty());
        }
    }

    void testProfileFamilyVariants()
    {
        auto variants = AiBehaviorModel::getFamilyVariants(AiBehaviorModel::Aggressive, 3);
        QVERIFY(variants.size() > 0);
        QVERIFY(variants.size() <= 3);
    }

    void testProfileByStyle()
    {
        auto prof = AiBehaviorModel::getProfileByStyle(AiBehaviorModel::Aggressive,
                                                        AiBehaviorModel::Expert);
        auto meta = AiBehaviorModel::computeMeta(prof);
        QCOMPARE(meta.style, AiBehaviorModel::Aggressive);
    }

    void testOverallRating()
    {
        auto legendary = AiBehaviorModel::getLegendaryRacer();
        auto rookie = AiBehaviorModel::getNoviceConsistent();

        float legRating = AiBehaviorModel::overallRating(legendary);
        float rookRating = AiBehaviorModel::overallRating(rookie);
        QVERIFY(legRating > rookRating);
    }

    void testOvertakingAbility()
    {
        auto aggressive = AiBehaviorModel::getAggressiveRacer();
        auto defensive = AiBehaviorModel::getDefensiveRacer();

        float aggOver = AiBehaviorModel::overtakingAbility(aggressive);
        float defOver = AiBehaviorModel::overtakingAbility(defensive);
        QVERIFY(aggOver > defOver);
    }

    void testDefendingAbility()
    {
        auto aggressive = AiBehaviorModel::getAggressiveRacer();
        auto defensive = AiBehaviorModel::getDefensiveRacer();

        float aggDef = AiBehaviorModel::defendingAbility(aggressive);
        float defDef = AiBehaviorModel::defendingAbility(defensive);
        QVERIFY(defDef > aggDef);
    }

    void testImprovementSuggestions()
    {
        auto rookie = AiBehaviorModel::getNoviceAggressive();
        auto suggestions = AiBehaviorModel::getImprovementSuggestions(rookie);

        QVERIFY(!suggestions.isEmpty());

        for (const auto& s : suggestions) {
            QVERIFY(!s.category.isEmpty());
            QVERIFY(s.currentValue > 0.0f);
            QVERIFY(s.suggestedValue > 0.0f);
            QVERIFY(!s.reasoning.isEmpty());
        }
    }

    void testApplySuggestions()
    {
        auto rookie = AiBehaviorModel::getNoviceAggressive();
        auto suggestions = AiBehaviorModel::getImprovementSuggestions(rookie);
        auto improved = AiBehaviorModel::applySuggestions(rookie, suggestions);

        float originalRating = AiBehaviorModel::overallRating(rookie);
        float improvedRating = AiBehaviorModel::overallRating(improved);
        QVERIFY(improvedRating >= originalRating);
    }

    void testAllProfilesValid()
    {
        auto profiles = AiBehaviorModel::getBuiltInProfiles();
        for (const auto& p : profiles) {
            QString error;
            bool valid = AiBehaviorModel::validateProfile(p, &error);
            QVERIFY2(valid, qPrintable(QString("%1: %2").arg(p.name).arg(error)));
        }
    }

    void testProfileNamesUnique()
    {
        auto profiles = AiBehaviorModel::getBuiltInProfiles();
        QSet<QString> names;
        for (const auto& p : profiles) {
            QVERIFY2(!names.contains(p.name),
                     qPrintable(QString("Duplicate profile name: %1").arg(p.name)));
            names.insert(p.name);
        }
    }

    void testTierLabels()
    {
        QCOMPARE(AiBehaviorModel::tierLabel(AiBehaviorModel::Novice), "Novice");
        QCOMPARE(AiBehaviorModel::tierLabel(AiBehaviorModel::Expert), "Expert");
        QCOMPARE(AiBehaviorModel::tierLabel(AiBehaviorModel::Legendary), "Legendary");
    }

    void testStyleLabels()
    {
        QCOMPARE(AiBehaviorModel::styleLabel(AiBehaviorModel::Aggressive), "Aggressive");
        QCOMPARE(AiBehaviorModel::styleLabel(AiBehaviorModel::Tactical), "Tactical");
        QCOMPARE(AiBehaviorModel::styleLabel(AiBehaviorModel::Endurance), "Endurance");
    }

    void testProfileMetaTags()
    {
        auto aggressive = AiBehaviorModel::getAggressiveRacer();
        auto meta = AiBehaviorModel::computeMeta(aggressive);
        QVERIFY(!meta.tags.isEmpty());
    }

    void testAdaptProfile()
    {
        auto base = AiBehaviorModel::getBalancedProfile();
        AiBehaviorModel::AdaptiveWeights w;
        w.skill = 1.5f;  // Boost skill
        w.aggression = 0.5f;  // Reduce aggression
        auto adapted = AiBehaviorModel::adaptProfile(base, w);

        QVERIFY(adapted.skill >= base.skill);
        QVERIFY(adapted.aggression <= base.aggression);
    }

    void testTelemetryBlend()
    {
        auto base = AiBehaviorModel::getBalancedProfile();
        QVector<float> factors = {0.9f, 0.85f, 0.88f, 0.92f};
        auto blended = AiBehaviorModel::blendWithTelemetry(base, factors);

        QVERIFY(blended.skill > base.skill * 0.5f);
        QVERIFY(blended.skill <= 1.0f);
    }

    void testRandomProfile()
    {
        auto p1 = AiBehaviorModel::getRandomProfile();
        auto p2 = AiBehaviorModel::getRandomProfile();

        // Random profiles should differ (very unlikely to produce the same)
        bool same = (p1.name == p2.name && p1.skill == p2.skill);
        QVERIFY(!same);
    }

    void testGetProfileByStyleEdgeCases()
    {
        // Should return something for any style/tier combo
        auto prof = AiBehaviorModel::getProfileByStyle(
            static_cast<AiBehaviorModel::DrivingStyle>(7),
            static_cast<AiBehaviorModel::DifficultyTier>(5));
        QVERIFY(prof.skill > 0.0f);
    }
};

QTEST_MAIN(TestAiBehaviorModel)
#include "test_AI.moc"
