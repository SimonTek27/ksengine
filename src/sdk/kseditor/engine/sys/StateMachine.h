#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <QQueue>
#include <QPointF>

namespace ks {

class StateMachine : public QObject
{
    Q_OBJECT

public:
    static StateMachine* instance();

    struct State {
        QString id;
        QString name;
        QString type;
        QJsonObject properties;
    };

    struct Transition {
        QString id;
        QString sourceState;
        QString targetState;
        QString event;
        QString condition;
    };

    void addState(const QString& stateId, const QString& name, const QString& type = "normal");
    void removeState(const QString& stateId);

    void addTransition(const QString& sourceId, const QString& targetId,
                       const QString& event = QString(), const QString& condition = QString());
    void removeTransition(const QString& transitionId);

    void start();
    void stop();
    void reset();

    bool isRunning() const { return m_running; }

    QString getCurrentState() const { return m_currentStateId; }

    void postEvent(const QString& event);
    void postEvent(const QString& event, const QJsonObject& data);

    void setInitialState(const QString& stateId);
    QString getInitialState() const { return m_initialStateId; }

    void setStateData(const QString& stateId, const QJsonObject& data);
    QJsonObject getStateData(const QString& stateId) const;

    QVector<State> getStates() const;
    QVector<Transition> getTransitions() const;
    QStringList getAvailableEvents() const;

signals:
    void stateEntered(const QString& stateId);
    void stateExited(const QString& stateId);
    void eventProcessed(const QString& event);
    void machineStarted();
    void machineStopped();
    void machineError(const QString& error);

private:
    StateMachine(QObject* parent = nullptr);
    ~StateMachine();
    Q_DISABLE_COPY(StateMachine)

    static StateMachine* s_instance;

    bool m_running = false;
    QString m_currentStateId;
    QString m_initialStateId;

    QMap<QString, State> m_states;
    QMap<QString, Transition> m_transitions;
    QMap<QString, QJsonObject> m_stateData;
    QQueue<QString> m_eventQueue;
};

class StateMachineEditor : public QObject
{
    Q_OBJECT

public:
    static StateMachineEditor* instance();

    void setStateMachine(StateMachine* machine);

    void setStatePosition(const QString& stateId, qreal x, qreal y);
    QPointF getStatePosition(const QString& stateId) const;

    void setTransitionPosition(const QString& transitionId, const QList<QPointF>& points);
    QList<QPointF> getTransitionPosition(const QString& transitionId) const;

    void setGridVisible(bool visible);
    bool isGridVisible() const { return m_gridVisible; }

    void setSnapToGrid(bool snap);
    bool isSnapToGridEnabled() const { return m_snapToGrid; }

    void setGridSize(int size);
    int getGridSize() const { return m_gridSize; }

    struct Layout {
        QMap<QString, QPointF> statePositions;
        QMap<QString, QList<QPointF>> transitionPositions;
    };

    void saveLayout(Layout& layout);
    void restoreLayout(const Layout& layout);

signals:
    void layoutChanged();

private:
    StateMachineEditor(QObject* parent = nullptr);
    ~StateMachineEditor();
    Q_DISABLE_COPY(StateMachineEditor)

    static StateMachineEditor* s_instance;

    StateMachine* m_machine = nullptr;
    bool m_gridVisible = true;
    bool m_snapToGrid = true;
    int m_gridSize = 10;
    QMap<QString, QPointF> m_statePositions;
    QMap<QString, QList<QPointF>> m_transitionPositions;
};

class StateMachineRunner : public QObject
{
    Q_OBJECT

public:
    static StateMachineRunner* instance();

    void setStateMachine(StateMachine* machine);

    void executeStateMachine();
    void step();

    void setExecutionSpeed(int stepsPerSecond);
    int getExecutionSpeed() const { return m_speed; }

    void pause();
    void resume();

    bool isPaused() const { return m_paused; }

    void setBreakpoint(const QString& stateId);
    void removeBreakpoint(const QString& stateId);
    bool hasBreakpoint(const QString& stateId) const;

    struct ExecutionState {
        QString stateId;
        int stepCount;
        qint64 elapsedTime;
    };

    QVector<ExecutionState> getExecutionHistory() const;

signals:
    void breakpointHit(const QString& stateId);
    void executionCompleted();
    void executionError(const QString& error);

private:
    StateMachineRunner(QObject* parent = nullptr);
    ~StateMachineRunner();
    Q_DISABLE_COPY(StateMachineRunner)

    static StateMachineRunner* s_instance;

    StateMachine* m_machine = nullptr;
    int m_speed = 1;
    bool m_paused = false;

    QSet<QString> m_breakpoints;
    QVector<ExecutionState> m_history;
};

} // namespace ks