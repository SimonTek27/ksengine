#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QVariantMap>
#include <QJsonObject>

namespace ks {

class Macro : public QObject
{
    Q_OBJECT

public:
    explicit Macro(QObject* parent = nullptr);

    QString getId() const { return m_id; }
    void setName(const QString& name);
    QString getName() const { return m_name; }

    void setDescription(const QString& description);
    QString getDescription() const { return m_description; }

    struct Action {
        QString id;
        QString type;
        QVariantMap params;
    };

    void addAction(const Action& action);
    void removeAction(const QString& actionId);
    QVector<Action> getActions() const { return m_actions; }

    void execute();
    void setRepeatCount(int count);
    int getRepeatCount() const { return m_repeatCount; }
    void setRepeatDelay(int ms);
    int getRepeatDelay() const { return m_repeatDelay; }
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

signals:
    void started();
    void actionExecuted(const QString& actionId);
    void completed();
    void error(const QString& error);

private:
    void executeRun();
    void executeNextAction();

    QString m_id;
    QString m_name;
    QString m_description;
    QVector<Action> m_actions;
    int m_repeatCount = 1;
    int m_repeatDelay = 0;
    bool m_enabled = true;
    bool m_running = false;
    int m_currentAction = 0;
    int m_currentRun = 0;
};

class MacroManager : public QObject
{
    Q_OBJECT

public:
    static MacroManager* instance();

    Macro* createMacro(const QString& name);
    void deleteMacro(const QString& id);
    Macro* getMacro(const QString& id) const;
    QVector<Macro*> getMacros() const;
    void executeMacro(const QString& id);
    bool saveToFile(const QString& path) const;
    bool loadFromFile(const QString& path);

signals:
    void macroCreated(const QString& id);
    void macroDeleted(const QString& id);
    void macroExecuted(const QString& id);

private:
    MacroManager(QObject* parent = nullptr);
    ~MacroManager();
    Q_DISABLE_COPY(MacroManager)

    static MacroManager* s_instance;
    QMap<QString, Macro*> m_macros;
};

} // namespace ks
