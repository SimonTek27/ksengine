#include "StateMachine.h"
#include <QDebug>
#include <QPointF>

namespace ks {

StateMachine* StateMachine::s_instance = nullptr;

StateMachine* StateMachine::instance()
{
    if (!s_instance) s_instance = new StateMachine();
    return s_instance;
}

StateMachine::StateMachine(QObject* parent) : QObject(parent) {}
StateMachine::~StateMachine() { s_instance = nullptr; }

void StateMachine::addState(const QString& stateId, const QString& name, const QString& type)
{
    State s;
    s.id   = stateId;
    s.name = name;
    s.type = type;
    m_states.insert(stateId, s);
    if (m_initialStateId.isEmpty()) m_initialStateId = stateId;
}

void StateMachine::removeState(const QString& stateId)
{
    m_states.remove(stateId);
    // Remove transitions involving this state
    QStringList toRemove;
    for (auto it = m_transitions.begin(); it != m_transitions.end(); ++it)
        if (it->sourceState == stateId || it->targetState == stateId)
            toRemove << it.key();
    for (const auto& k : toRemove) m_transitions.remove(k);
}

void StateMachine::addTransition(const QString& sourceId,
                                  const QString& targetId,
                                  const QString& event,
                                  const QString& condition)
{
    Transition t;
    t.id          = sourceId + "_" + event + "_" + targetId;
    t.sourceState = sourceId;
    t.targetState = targetId;
    t.event       = event;
    t.condition   = condition;
    m_transitions.insert(t.id, t);
}

void StateMachine::removeTransition(const QString& transitionId)
{
    QStringList toRemove;
    for (auto it = m_transitions.begin(); it != m_transitions.end(); ++it)
        if (it->id == transitionId)
            toRemove << it.key();
    for (const auto& k : toRemove) m_transitions.remove(k);
}

void StateMachine::setInitialState(const QString& stateId)
{
    m_initialStateId = stateId;
}

void StateMachine::start()
{
    if (m_running) return;
    m_currentStateId = m_initialStateId;
    m_running = true;
    emit stateEntered(m_currentStateId);
    emit machineStarted();
}

void StateMachine::stop()
{
    if (!m_running) return;
    m_running = false;
    emit stateExited(m_currentStateId);
    emit machineStopped();
}

void StateMachine::reset()
{
    m_running = false;
    m_states.clear();
    m_transitions.clear();
    m_initialStateId.clear();
    m_currentStateId.clear();
    m_eventQueue.clear();
}

void StateMachine::postEvent(const QString& event)
{
    postEvent(event, QJsonObject());
}

void StateMachine::postEvent(const QString& event, const QJsonObject& data)
{
    if (!m_running) return;

    // Find matching transition from current state
    for (const auto& t : m_transitions) {
        if (t.sourceState != m_currentStateId) continue;
        if (t.event != event && !t.event.isEmpty()) continue;
        // Condition check: if set, evaluate as simple key=value from data
        if (!t.condition.isEmpty()) {
            // Simple: "key=value"
            auto parts = t.condition.split('=');
            if (parts.size() == 2) {
                if (data.value(parts[0]).toVariant().toString() != parts[1]) continue;
            }
        }

        QString fromState = m_currentStateId;
        emit stateExited(fromState);
        m_currentStateId = t.targetState;
        emit eventProcessed(event);
        emit stateEntered(m_currentStateId);

        // Check for final state
        if (m_states.contains(m_currentStateId) &&
            m_states[m_currentStateId].type == "final") {
            m_running = false;
            emit machineStopped();
        }
        return;
    }

    qDebug() << "StateMachine: no transition for event" << event
             << "from state" << m_currentStateId;
}

QStringList StateMachine::getAvailableEvents() const
{
    QStringList events;
    for (const auto& t : m_transitions)
        if (t.sourceState == m_currentStateId && !events.contains(t.event))
            events << t.event;
    return events;
}

QVector<StateMachine::State> StateMachine::getStates() const
{
    return m_states.values().toVector();
}

QVector<StateMachine::Transition> StateMachine::getTransitions() const
{
    return m_transitions.values().toVector();
}

void StateMachine::setStateData(const QString& stateId, const QJsonObject& data)
{
    m_stateData[stateId] = data;
}

QJsonObject StateMachine::getStateData(const QString& stateId) const
{
    return m_stateData.value(stateId);
}

// ─── StateMachineEditor ──────────────────────────────────────────────────────

static StateMachineEditor* s_editorInstance = nullptr;

StateMachineEditor* StateMachineEditor::instance()
{
    if (!s_editorInstance) s_editorInstance = new StateMachineEditor();
    return s_editorInstance;
}

StateMachineEditor::StateMachineEditor(QObject* parent) : QObject(parent) {}
StateMachineEditor::~StateMachineEditor() { s_editorInstance = nullptr; }

void StateMachineEditor::setStateMachine(StateMachine* machine) { m_machine = machine; }
void StateMachineEditor::setStatePosition(const QString& stateId, qreal x, qreal y)
{
    if (m_snapToGrid) {
        x = qRound(x / m_gridSize) * m_gridSize;
        y = qRound(y / m_gridSize) * m_gridSize;
    }
    m_statePositions[stateId] = QPointF(x, y);
    emit layoutChanged();
}

QPointF StateMachineEditor::getStatePosition(const QString& stateId) const
{
    return m_statePositions.value(stateId, QPointF(0, 0));
}

void StateMachineEditor::setTransitionPosition(const QString& transitionId, const QList<QPointF>& points)
{
    m_transitionPositions[transitionId] = points;
    emit layoutChanged();
}

QList<QPointF> StateMachineEditor::getTransitionPosition(const QString& transitionId) const
{
    return m_transitionPositions.value(transitionId);
}

void StateMachineEditor::saveLayout(Layout& layout)
{
    layout.statePositions = m_statePositions;
    layout.transitionPositions = m_transitionPositions;
}

void StateMachineEditor::restoreLayout(const Layout& layout)
{
    m_statePositions = layout.statePositions;
    m_transitionPositions = layout.transitionPositions;
    emit layoutChanged();
}

// ─── StateMachineRunner ──────────────────────────────────────────────────────

static StateMachineRunner* s_runnerInstance = nullptr;

StateMachineRunner* StateMachineRunner::instance()
{
    if (!s_runnerInstance) s_runnerInstance = new StateMachineRunner();
    return s_runnerInstance;
}

StateMachineRunner::StateMachineRunner(QObject* parent) : QObject(parent) {}
StateMachineRunner::~StateMachineRunner() { s_runnerInstance = nullptr; }

void StateMachineRunner::setStateMachine(StateMachine* machine) { m_machine = machine; }
void StateMachineRunner::executeStateMachine() {
    if (!m_machine || m_paused) return;
    m_machine->start();
    ExecutionState es;
    es.stateId = m_machine->getCurrentState();
    es.stepCount = 0;
    es.elapsedTime = 0;
    m_history.append(es);

    while (m_machine->isRunning() && !m_paused) {
        if (m_breakpoints.contains(m_machine->getCurrentState())) {
            emit breakpointHit(m_machine->getCurrentState());
            return;
        }
        QStringList events = m_machine->getAvailableEvents();
        if (events.isEmpty()) {
            emit executionError("No available events from state: " + m_machine->getCurrentState());
            return;
        }
        m_machine->postEvent(events.first());
        es.stateId = m_machine->getCurrentState();
        es.stepCount++;
        m_history.append(es);
    }
    emit executionCompleted();
}

void StateMachineRunner::step() {
    if (!m_machine || m_paused) return;
    if (!m_machine->isRunning()) {
        m_machine->start();
    }
    ExecutionState es;
    es.stateId = m_machine->getCurrentState();
    es.stepCount = m_history.isEmpty() ? 0 : m_history.last().stepCount + 1;
    es.elapsedTime = 0;

    if (m_breakpoints.contains(es.stateId)) {
        emit breakpointHit(es.stateId);
        return;
    }

    QStringList events = m_machine->getAvailableEvents();
    if (events.isEmpty()) {
        emit executionError("No available events from state: " + es.stateId);
        return;
    }
    m_machine->postEvent(events.first());
    es.stateId = m_machine->getCurrentState();
    m_history.append(es);

    if (!m_machine->isRunning()) {
        emit executionCompleted();
    }
}
void StateMachineRunner::setExecutionSpeed(int stepsPerSecond) { m_speed = stepsPerSecond; }
void StateMachineRunner::pause() { m_paused = true; }
void StateMachineRunner::resume() { m_paused = false; }
void StateMachineRunner::setBreakpoint(const QString& stateId) { m_breakpoints.insert(stateId); }
void StateMachineRunner::removeBreakpoint(const QString& stateId) { m_breakpoints.remove(stateId); }
bool StateMachineRunner::hasBreakpoint(const QString& stateId) const { return m_breakpoints.contains(stateId); }
QVector<StateMachineRunner::ExecutionState> StateMachineRunner::getExecutionHistory() const { return m_history; }

} // namespace ks
