#include "HotkeyActionSystem.h"
#include <QShortcut>
#include <QApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QFile>
#include <algorithm>

namespace ks {

// ─── Action ──────────────────────────────────────────────────────────────────

Action::Action(QObject* parent) : QObject(parent) {}
Action::Action(const QString& id, const QString& text, QObject* parent)
    : QObject(parent), m_id(id), m_text(text) {}
Action::~Action() = default;

void Action::setShortcut(const QKeySequence& shortcut)
{
    m_shortcut = shortcut;
    emit changed();
}

void Action::setShortcutContext(Qt::ShortcutContext ctx) { m_context = ctx; }

void Action::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    emit changed();
}

void Action::setCheckable(bool checkable) { m_checkable = checkable; }

void Action::setChecked(bool checked)
{
    if (!m_checkable || m_checked == checked) return;
    m_checked = checked;
    emit toggled(checked);
    emit changed();
}

void Action::setData(const QJsonObject& data) { m_data = data; }

void Action::trigger()
{
    if (!m_enabled) return;
    if (m_checkable) setChecked(!m_checked);
    emit triggered();
}

void Action::triggerWithData(const QJsonObject& data)
{
    if (!m_enabled) return;
    emit triggeredWithData(data);
}

QAction* Action::qAction() const
{
    QAction* a = new QAction(m_text);
    a->setShortcut(m_shortcut);
    a->setEnabled(m_enabled);
    a->setCheckable(m_checkable);
    a->setChecked(m_checked);
    QObject::connect(a, &QAction::triggered, const_cast<Action*>(this), &Action::trigger);
    return a;
}

// ─── ActionManager ────────────────────────────────────────────────────────────

static ActionManager* s_instance = nullptr;

ActionManager* ActionManager::instance()
{
    if (!s_instance) s_instance = new ActionManager();
    return s_instance;
}

ActionManager::ActionManager(QObject* parent) : QObject(parent) {}
ActionManager::~ActionManager() { s_instance = nullptr; }

Action* ActionManager::registerAction(const QString& id,
                                       const QString& text,
                                       const QString& category,
                                       const QKeySequence& shortcut)
{
    if (m_actions.contains(id)) return m_actions[id];

    auto* a = new Action(this);
    a->setId(id);
    a->setText(text);
    a->setCategory(category);
    a->setShortcut(shortcut);
    m_actions.insert(id, a);

    // Register global shortcut via QShortcut if main window exists
    if (!shortcut.isEmpty() && qApp->activeWindow()) {
        auto* sc = new QShortcut(shortcut, qApp->activeWindow());
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, a, &Action::trigger);
        m_shortcuts.insert(id, sc);
    }

    emit actionRegistered(id);
    return a;
}

void ActionManager::unregisterAction(const QString& id)
{
    if (!m_actions.contains(id)) return;
    delete m_shortcuts.take(id);
    delete m_actions.take(id);
    emit actionUnregistered(id);
}

Action* ActionManager::getAction(const QString& id) const
{
    return m_actions.value(id, nullptr);
}

bool ActionManager::hasAction(const QString& id) const
{
    return m_actions.contains(id);
}

bool ActionManager::trigger(const QString& id)
{
    Action* a = getAction(id);
    if (!a) { qWarning() << "ActionManager: unknown action" << id; return false; }
    a->trigger();
    return true;
}

QVector<Action*> ActionManager::getActions(const QString& category) const
{
    QVector<Action*> result;
    for (auto* a : m_actions)
        if (category.isEmpty() || a->getCategory() == category) result << a;
    return result;
}

QStringList ActionManager::getCategories() const
{
    QStringList cats;
    for (auto* a : m_actions)
        if (!cats.contains(a->getCategory())) cats << a->getCategory();
    cats.sort();
    return cats;
}

bool ActionManager::setShortcut(const QString& id, const QKeySequence& shortcut)
{
    Action* a = getAction(id);
    if (!a) return false;
    a->setShortcut(shortcut);

    // Update QShortcut
    if (m_shortcuts.contains(id)) {
        m_shortcuts[id]->setKey(shortcut);
    } else if (!shortcut.isEmpty() && qApp->activeWindow()) {
        auto* sc = new QShortcut(shortcut, qApp->activeWindow());
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, a, &Action::trigger);
        m_shortcuts.insert(id, sc);
    }
    return true;
}

bool ActionManager::isShortcutInUse(const QKeySequence& shortcut, const QString& excludeId) const
{
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it)
        if (it.key() != excludeId && it.value()->getShortcut() == shortcut) return true;
    return false;
}

bool ActionManager::saveToFile(const QString& path) const
{
    QJsonObject root;
    for (auto* a : m_actions) {
        QJsonObject obj;
        obj["text"]     = a->getText();
        obj["category"] = a->getCategory();
        obj["shortcut"] = a->getShortcut().toString();
        obj["enabled"]  = a->isEnabled();
        root[a->getId()] = obj;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson());
    return true;
}

bool ActionManager::loadFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        QJsonObject obj = it.value().toObject();
        QString id = it.key();
        if (!m_actions.contains(id)) {
            registerAction(id, obj["text"].toString(), obj["category"].toString(),
                           QKeySequence(obj["shortcut"].toString()));
        } else {
            setShortcut(id, QKeySequence(obj["shortcut"].toString()));
        }
    }
    return true;
}

// ─── ActionGroup ─────────────────────────────────────────────────────────────

ActionGroup::ActionGroup(const QString& id, QObject* parent)
    : QObject(parent), m_id(id) {}
ActionGroup::~ActionGroup() = default;

void ActionGroup::setExclusive(bool exclusive) { m_exclusive = exclusive; }

void ActionGroup::addAction(Action* action)
{
    if (!action || m_actions.contains(action)) return;
    m_actions.append(action);
    if (m_exclusive && !m_activeAction) {
        m_activeAction = action;
        emit activeActionChanged(action->getId());
    }
}

void ActionGroup::removeAction(const QString& actionId)
{
    m_actions.erase(std::remove_if(m_actions.begin(), m_actions.end(),
        [&](Action* a){ return a->getId() == actionId; }), m_actions.end());
}

void ActionGroup::trigger(const QString& actionId)
{
    for (auto* a : m_actions) {
        if (a->getId() == actionId) {
            if (m_exclusive) m_activeAction = a;
            emit triggered(actionId);
            emit activeActionChanged(actionId);
            return;
        }
    }
}

// ─── GlobalShortcutManager ─────────────────────────────────────────────────────────

GlobalShortcutManager::GlobalShortcutManager(QObject* parent) : QObject(parent) {}
GlobalShortcutManager::~GlobalShortcutManager() = default;

void GlobalShortcutManager::setWidget(QWidget* widget) { m_widget = widget; }

void GlobalShortcutManager::registerShortcut(QShortcut* shortcut, const QString& actionId)
{
    m_shortcuts[actionId] = shortcut;
    if (shortcut) m_shortcutToAction[shortcut->key()] = actionId;
}

void GlobalShortcutManager::unregisterShortcut(const QString& actionId)
{
    if (m_shortcuts.contains(actionId)) {
        m_shortcutToAction.remove(m_shortcuts[actionId]->key());
        m_shortcuts.remove(actionId);
    }
}

bool GlobalShortcutManager::isShortcutInUse(const QKeySequence& shortcut, const QString& excludeAction) const
{
    return m_shortcutToAction.contains(shortcut) && m_shortcutToAction[shortcut] != excludeAction;
}

QString GlobalShortcutManager::getActionForShortcut(const QKeySequence& shortcut) const
{
    return m_shortcutToAction.value(shortcut);
}

void GlobalShortcutManager::setGlobalShortcut(const QString& actionId, const QKeySequence& shortcut)
{
    m_globalShortcuts[actionId] = shortcut;
}

QKeySequence GlobalShortcutManager::getGlobalShortcut(const QString& actionId) const
{
    return m_globalShortcuts.value(actionId);
}

void GlobalShortcutManager::clearShortcuts()
{
    m_shortcuts.clear();
    m_shortcutToAction.clear();
    m_globalShortcuts.clear();
}

void GlobalShortcutManager::resetToDefaults() { clearShortcuts(); }

// ─── MenuBuilder ─────────────────────────────────────────────────────────────

MenuBuilder::MenuBuilder(QObject* parent) : QObject(parent) {}
MenuBuilder::~MenuBuilder() = default;

void MenuBuilder::setActionManager(ActionManager* manager) { m_actionManager = manager; }
void MenuBuilder::setActionGroupManager(ActionGroupManager* manager) { m_groupManager = manager; }

QMenu* MenuBuilder::buildMenu(const QString& menuId)
{
    if (!m_menuTitles.contains(menuId)) return nullptr;
    QMenu* menu = new QMenu(m_menuTitles[menuId]);
    buildMenu(menuId, menu);
    return menu;
}

QMenuBar* MenuBuilder::buildMenuBar()
{
    QMenuBar* menuBar = new QMenuBar();
    buildMenuBar(menuBar);
    return menuBar;
}

void MenuBuilder::buildMenu(const QString& menuId, QMenu* menu)
{
    if (!menu) return;
    menu->clear();

    const auto& items = m_menuItems[menuId];
    for (const auto& item : items) {
        switch (item.type) {
        case MenuItem::Type::Separator:
            menu->addSeparator();
            break;
        case MenuItem::Type::Action:
            if (m_actionManager) {
                Action* action = m_actionManager->getAction(item.actionId);
                if (action) menu->addAction(action->qAction());
            }
            break;
        case MenuItem::Type::Submenu:
            if (item.submenu) menu->addMenu(item.submenu);
            break;
        }
    }

    emit menuBuilt(menu);
}

void MenuBuilder::buildMenuBar(QMenuBar* menuBar)
{
    if (!menuBar) return;
    menuBar->clear();

    // Build each registered menu and add it to the menu bar
    for (auto it = m_menuTitles.constBegin(); it != m_menuTitles.constEnd(); ++it) {
        QMenu* menu = buildMenu(it.key());
        if (menu) menuBar->addMenu(menu);
    }
}

void MenuBuilder::registerMenu(const QString& menuId, const QString& title) { m_menuTitles[menuId] = title; }

void MenuBuilder::registerMenuItem(const QString& menuId, Action* action)
{
    if (!action) return;
    MenuItem item;
    item.type = MenuItem::Type::Action;
    item.actionId = action->getId();
    item.submenu = nullptr;
    m_menuItems[menuId].append(item);
}

void MenuBuilder::registerMenuSeparator(const QString& menuId)
{
    MenuItem item;
    item.type = MenuItem::Type::Separator;
    item.submenu = nullptr;
    m_menuItems[menuId].append(item);
}

void MenuBuilder::registerSubmenu(const QString& menuId, QMenu* submenu)
{
    MenuItem item;
    item.type = MenuItem::Type::Submenu;
    item.submenu = submenu;
    m_menuItems[menuId].append(item);
}

void MenuBuilder::unregisterMenu(const QString& menuId) { m_menuTitles.remove(menuId); m_menuItems.remove(menuId); }

// ─── ToolbarBuilder ──────────────────────────────────────────────────────────

ToolbarBuilder::ToolbarBuilder(QObject* parent) : QObject(parent) {}
ToolbarBuilder::~ToolbarBuilder() = default;

void ToolbarBuilder::setActionManager(ActionManager* manager) { m_actionManager = manager; }

QToolBar* ToolbarBuilder::buildToolbar(const QString& toolbarId)
{
    if (!m_toolbarTitles.contains(toolbarId)) return nullptr;
    QToolBar* toolbar = new QToolBar(m_toolbarTitles[toolbarId]);
    buildToolbar(toolbarId, toolbar);
    return toolbar;
}

void ToolbarBuilder::buildToolbar(const QString& toolbarId, QToolBar* toolbar)
{
    if (!toolbar) return;
    toolbar->clear();

    const auto& items = m_toolbarItems[toolbarId];
    for (const auto& item : items) {
        switch (item.type) {
        case ToolbarItem::Type::Separator:
            toolbar->addSeparator();
            break;
        case ToolbarItem::Type::Action:
            if (m_actionManager) {
                Action* action = m_actionManager->getAction(item.actionId);
                if (action) toolbar->addAction(action->qAction());
            }
            break;
        }
    }

    emit toolbarBuilt(toolbar);
}

void ToolbarBuilder::registerToolbar(const QString& toolbarId, const QString& title) { m_toolbarTitles[toolbarId] = title; }
void ToolbarBuilder::registerToolbarItem(const QString& toolbarId, const QString& actionId, int index)
{
    ToolbarItem item;
    item.type = ToolbarItem::Type::Action;
    item.actionId = actionId;
    auto& items = m_toolbarItems[toolbarId];
    if (index >= 0 && index < items.size())
        items.insert(index, item);
    else
        items.append(item);
}
void ToolbarBuilder::registerToolbarSeparator(const QString& toolbarId)
{
    ToolbarItem item;
    item.type = ToolbarItem::Type::Separator;
    m_toolbarItems[toolbarId].append(item);
}
void ToolbarBuilder::unregisterToolbar(const QString& toolbarId) { m_toolbarTitles.remove(toolbarId); m_toolbarItems.remove(toolbarId); }

} // namespace ks
