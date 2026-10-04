#include "engine/animation/AnimationSystem.h"
#include "engine/animation/AnimationTimeline.h"
#include <QtTest/QtTest>

using namespace ks;
using namespace ks::animation;

class TestAnimationSystem : public QObject {
    Q_OBJECT

private slots:
    void testAnimationCurveKeyframes();
    void testAnimationCurveEvaluate();
    void testStateMachine();
    void testAnimationTimelineCreateAnimations();
    void testAnimationTimelineTracks();
    void testAnimationTimelineKeyframes();
    void testAnimationTimelinePlayback();
};

void TestAnimationSystem::testAnimationCurveKeyframes()
{
    AnimationCurve curve;
    AnimationCurve::Keyframe kf;

    kf.time = 30.0; kf.value = 3.0; curve.addKeyframe(kf);
    kf.time = 10.0; kf.value = 1.0; curve.addKeyframe(kf);
    kf.time = 20.0; kf.value = 2.0; curve.addKeyframe(kf);

    QCOMPARE(curve.keyframes().size(), 3);
    QCOMPARE(curve.keyframes()[0].time, 10.0);
    QCOMPARE(curve.keyframes()[1].time, 20.0);
    QCOMPARE(curve.keyframes()[2].time, 30.0);

    curve.removeKeyframe(20.0);
    QCOMPARE(curve.keyframes().size(), 2);

    curve.clearKeyframes();
    QCOMPARE(curve.keyframes().size(), 0);
}

void TestAnimationSystem::testAnimationCurveEvaluate()
{
    AnimationCurve curve;
    AnimationCurve::Keyframe kf;

    kf.time = 0.0;  kf.value = 0.0;   curve.addKeyframe(kf);
    kf.time = 10.0; kf.value = 100.0; curve.addKeyframe(kf);

    QCOMPARE(curve.evaluate(0.0).toDouble(), 0.0);
    QCOMPARE(curve.evaluate(10.0).toDouble(), 100.0);

    // Between two keyframes uses linear interpolation (default).
    QCOMPARE(curve.evaluate(5.0).toDouble(), 50.0);

    // Out-of-range with default Constant extrapolation clamps to endpoint.
    QCOMPARE(curve.evaluate(-5.0).toDouble(), 0.0);
    QCOMPARE(curve.evaluate(999.0).toDouble(), 100.0);

    // Constant interpolation holds the previous keyframe value.
    AnimationCurve constCurve;
    AnimationCurve::Keyframe ckf;
    ckf.time = 0;  ckf.value = 10.0; ckf.interpolation = "constant";
    constCurve.addKeyframe(ckf);
    ckf.time = 10; ckf.value = 20.0; ckf.interpolation = "constant";
    constCurve.addKeyframe(ckf);
    QCOMPARE(constCurve.evaluate(0.0).toDouble(), 10.0);
    QCOMPARE(constCurve.evaluate(5.0).toDouble(), 10.0);
    QCOMPARE(constCurve.evaluate(10.0).toDouble(), 20.0);
}

void TestAnimationSystem::testStateMachine()
{
    StateMachine sm;

    StateMachine::State a; a.id = "A"; a.name = "Idle";
    StateMachine::State b; b.id = "B"; b.name = "Run";
    sm.addState(a);
    sm.addState(b);
    QCOMPARE(sm.states().size(), 2);

    StateMachine::Transition tr;
    tr.id = "AtoB";
    tr.fromState = "A";
    tr.toState = "B";
    tr.condition = "";
    tr.duration = 0.3;
    tr.easing = "linear";
    sm.addTransition(tr);
    QCOMPARE(sm.transitions().size(), 1);

    sm.start("A");
    QCOMPARE(sm.currentState(), QString("A"));

    // One update step longer than the transition duration completes it.
    sm.update(0.5);
    QCOMPARE(sm.currentState(), QString("B"));

    sm.removeState("B");
    QCOMPARE(sm.states().size(), 1);
}

void TestAnimationSystem::testAnimationTimelineCreateAnimations()
{
    AnimationTimeline* tl = AnimationTimeline::instance();
    tl->createAnimation("WalkAS");
    tl->createAnimation("RunAS");

    QVector<AnimationTimeline::Animation> anims = tl->getAnimations();
    bool foundWalk = false, foundRun = false;
    for (const auto& a : anims) {
        if (a.name == "WalkAS") foundWalk = true;
        if (a.name == "RunAS")  foundRun = true;
    }
    QVERIFY(foundWalk);
    QVERIFY(foundRun);
}

void TestAnimationSystem::testAnimationTimelineTracks()
{
    AnimationTimeline* tl = AnimationTimeline::instance();
    tl->createAnimation("TestAS");

    AnimationTimeline::Track track;
    track.id = "posX";
    track.name = "Position X";
    track.type = "float";
    tl->addTrack("TestAS", track);

    QVector<AnimationTimeline::Track> tracks = tl->getTracks("TestAS");
    QCOMPARE(tracks.size(), 1);
    QCOMPARE(tracks[0].name, QString("Position X"));

    tl->removeTrack("TestAS", "posX");
    tracks = tl->getTracks("TestAS");
    QCOMPARE(tracks.size(), 0);
}

void TestAnimationSystem::testAnimationTimelineKeyframes()
{
    AnimationTimeline* tl = AnimationTimeline::instance();
    tl->createAnimation("BounceAS");

    AnimationTimeline::Track track;
    track.id = "height";
    track.name = "Height";
    track.type = "float";
    tl->addTrack("BounceAS", track);

    AnimationTimeline::Keyframe kf;
    kf.frame = 0;  kf.value = 0.0f;   kf.interpolation = "linear";
    tl->addKeyframe("BounceAS", "height", kf);
    kf.frame = 10; kf.value = 100.0f; kf.interpolation = "linear";
    tl->addKeyframe("BounceAS", "height", kf);

    QCOMPARE(tl->evaluateTrack("BounceAS", "height", 0), 0.0f);
    QCOMPARE(tl->evaluateTrack("BounceAS", "height", 10), 100.0f);
    QVERIFY(qAbs(tl->evaluateTrack("BounceAS", "height", 5) - 50.0f) < 0.001f);
}

void TestAnimationSystem::testAnimationTimelinePlayback()
{
    AnimationTimeline* tl = AnimationTimeline::instance();
    tl->createAnimation("WalkCycleAS");
    tl->setCurrentAnimation("WalkCycleAS");

    tl->setFrameRate(30);
    QCOMPARE(tl->frameRate(), 30);

    tl->setLoop(true);
    QVERIFY(tl->isLooping());

    tl->setPlayRange(0, 60);
    int start = 0, end = 0;
    tl->getPlayRange(start, end);
    QCOMPARE(start, 0);
    QCOMPARE(end, 60);
}

QTEST_MAIN(TestAnimationSystem)
#include "test_AnimationSystem.moc"
