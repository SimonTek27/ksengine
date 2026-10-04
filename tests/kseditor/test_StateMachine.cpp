#include "engine/sys/StateMachine.h"
#include <QtTest/QtTest>

using namespace ks;

class TestStateMachine : public QObject {
    Q_OBJECT

private slots:
    void test_instance();
    void test_addState();
    void test_removeState();
    void test_addTransition();
    void test_startStop();
    void test_postEvent();
    void test_initialState();
    void test_getStates();
};

void TestStateMachine::test_instance()
{
    StateMachine* sm = StateMachine::instance();
    QVERIFY(sm != nullptr);
    QCOMPARE(StateMachine::instance(), sm);
}

void TestStateMachine::test_addState()
{
    StateMachine* sm = StateMachine::instance();
    sm->reset();

    sm->addState("idle", "Idle", "normal");
    auto states = sm->getStates();
    bool found = false;
    for (const auto& s : states) {
        if (s.id == "idle") { found = true; break; }
    }
    QVERIFY(found);
}

void TestStateMachine::test_removeState()
{
    StateMachine* sm = StateMachine::instance();
    sm->reset();

    sm->addState("temp", "Temporary", "normal");
    sm->removeState("temp");
    auto states = sm->getStates();
    for (const auto& s : states) {
        QVERIFY(s.id != "temp");
    }
}

void TestStateMachine::test_addTransition()
{
    StateMachine* sm = StateMachine::instance();
    sm->reset();

    sm->addState("a", "State A", "normal");
    sm->addState("b", "State B", "normal");
    sm->addTransition("a", "b", "go");

    auto transitions = sm->getTransitions();
    bool found = false;
    for (const auto& t : transitions) {
        if (t.sourceState == "a" && t.targetState == "b") { found = true; break; }
    }
    QVERIFY(found);
}

void TestStateMachine::test_startStop()
{
    StateMachine* sm = StateMachine::instance();
    sm->reset();

    sm->addState("start", "Start", "normal");
    sm->setInitialState("start");
    QVERIFY(!sm->isRunning());
    sm->start();
    QVERIFY(sm->isRunning());
    QCOMPARE(sm->getCurrentState(), QString("start"));
    sm->stop();
    QVERIFY(!sm->isRunning());
}

void TestStateMachine::test_postEvent()
{
    StateMachine* sm = StateMachine::instance();
    sm->reset();

    sm->addState("s1", "State 1", "normal");
    sm->addState("s2", "State 2", "normal");
    sm->addTransition("s1", "s2", "next");
    sm->setInitialState("s1");
    sm->start();
    QCOMPARE(sm->getCurrentState(), QString("s1"));
    sm->postEvent("next");
    QCOMPARE(sm->getCurrentState(), QString("s2"));
}

void TestStateMachine::test_initialState()
{
    StateMachine* sm = StateMachine::instance();
    sm->reset();

    QVERIFY(sm->getInitialState().isEmpty());
    sm->addState("first", "First", "normal");
    QCOMPARE(sm->getInitialState(), QString("first"));
    sm->setInitialState("first");
    QCOMPARE(sm->getInitialState(), QString("first"));
}

void TestStateMachine::test_getStates()
{
    StateMachine* sm = StateMachine::instance();
    sm->reset();

    sm->addState("one", "One", "normal");
    sm->addState("two", "Two", "final");
    auto states = sm->getStates();
    QCOMPARE(states.size(), 2);
    auto events = sm->getAvailableEvents();
    QVERIFY(events.isEmpty());
}

QTEST_MAIN(TestStateMachine)
#include "test_StateMachine.moc"
