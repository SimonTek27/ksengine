#include "MacroSystem.h"
#include <QUuid>
#include <QTimer>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>

namespace ks {

// ─── Macro ───────────────────────────────────────────────────────────────────

Macro::Macro(QObject* parent) : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

void Macro::setName(const QString& name) { m_name = name; }
void Macro::setDescription(const QString& d) { m_description = d; }
void Macro::setRepeatCount(int c) { m_repeatCount = qMax(1, c); }
void Macro::setRepeatDelay(int ms) { m_repeatDelay = qMax(0, ms); }
void Macro::setEnabled(bool e) { m_enabled = e; }

void Macro::addAction(const Action& action)
{
    m_actions.append(action);
}

void Macro::removeAction(const QString& actionId)
{
    m_actions.removeIf([&](const Action& a){ return a.id == actionId; });
}

void Macro::execute()
{
    if (!m_enabled || m_running) return;
    m_running = true;
    m_currentRun = 0;
    emit started();
    executeRun();
}

void Macro::executeRun()
{
    if (!m_running || m_currentRun >= m_repeatCount) {
        m_running = false;
        emit completed();
        return;
    }
    ++m_currentRun;
    m_currentAction = 0;
    executeNextAction();
}

void Macro::executeNextAction()
{
    if (m_currentAction >= m_actions.size()) {
        if (m_repeatDelay > 0) {
            QTimer::singleShot(m_repeatDelay, this, &Macro::executeRun);
        } else {
            executeRun();
        }
        return;
    }

    const Action& a = m_actions[m_currentAction++];
    emit actionExecuted(a.id);

    int delay = a.params.value("delay_ms").toInt(0);
    if (delay > 0) {
        QTimer::singleShot(delay, this, &Macro::executeNextAction);
    } else {
        executeNextAction();
    }
}

// ─── MacroManager ─────────────────────────────────────────────────────────────

MacroManager* MacroManager::s_instance = nullptr;

MacroManager* MacroManager::instance()
{
    if (!s_instance) s_instance = new MacroManager();
    return s_instance;
}

MacroManager::MacroManager(QObject* parent) : QObject(parent) {}
MacroManager::~MacroManager() { s_instance = nullptr; }

Macro* MacroManager::createMacro(const QString& name)
{
    auto* m = new Macro(this);
    m->setName(name);
    m_macros.insert(m->getId(), m);
    emit macroCreated(m->getId());
    return m;
}

void MacroManager::deleteMacro(const QString& id)
{
    if (auto* m = m_macros.take(id)) {
        m->deleteLater();
        emit macroDeleted(id);
    }
}

Macro* MacroManager::getMacro(const QString& id) const
{
    return m_macros.value(id, nullptr);
}

QVector<Macro*> MacroManager::getMacros() const
{
    return m_macros.values().toVector();
}

void MacroManager::executeMacro(const QString& id)
{
    if (auto* m = getMacro(id)) {
        m->execute();
        emit macroExecuted(id);
    }
}

bool MacroManager::saveToFile(const QString& path) const
{
    QJsonArray arr;
    for (auto* m : m_macros) {
        QJsonObject obj;
        obj["id"]          = m->getId();
        obj["name"]        = m->getName();
        obj["description"] = m->getDescription();
        obj["repeatCount"] = m->getRepeatCount();
        obj["repeatDelay"] = m->getRepeatDelay();
        obj["enabled"]     = m->isEnabled();
        QJsonArray actions;
        for (const auto& a : m->getActions()) {
            QJsonObject ao;
            ao["id"]   = a.id;
            ao["type"] = a.type;
            ao["params"] = QJsonObject::fromVariantMap(a.params);
            actions.append(ao);
        }
        obj["actions"] = actions;
        arr.append(obj);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(arr).toJson());
    return true;
}

bool MacroManager::loadFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto& v : arr) {
        QJsonObject obj = v.toObject();
        auto* m = createMacro(obj["name"].toString());
        m->setDescription(obj["description"].toString());
        m->setRepeatCount(obj["repeatCount"].toInt(1));
        m->setRepeatDelay(obj["repeatDelay"].toInt(0));
        m->setEnabled(obj["enabled"].toBool(true));
        for (const auto& av : obj["actions"].toArray()) {
            QJsonObject ao = av.toObject();
            Macro::Action a;
            a.id     = ao["id"].toString();
            a.type   = ao["type"].toString();
            a.params = ao["params"].toObject().toVariantMap();
            m->addAction(a);
        }
    }
    return true;
}

} // namespace ks
