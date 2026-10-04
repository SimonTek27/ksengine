#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QKeySequence>
#include <QJsonObject>
#include <QAction>
#include <QShortcut>
#include <QWidget>
#include <QMenuBar>
#include <QToolBar>

namespace ks {

class Action : public QObject
{
    Q_OBJECT

public:
    explicit Action(QObject* parent = nullptr);
    explicit Action(const QString& id, const QString& text, QObject* parent = nullptr);
    ~Action();

    QString getId() const { return m_id; }
    void setId(const QString& id) { m_id = id; }

    QString getText() const { return m_text; }
    void setText(const QString& text) { m_text = text; }

    QString getCategory() const { return m_category; }
    void setCategory(const QString& cat) { m_category = cat; }

    QString getDescription() const { return m_description; }
    void setDescription(const QString& desc) { m_description = desc; }

    QString getIcon() const { return m_icon; }
    void setIcon(const QString& icon) { m_icon = icon; }

    void setShortcut(const QKeySequence& shortcut);
    QKeySequence getShortcut() const { return m_shortcut; }

    void setShortcutContext(Qt::ShortcutContext context);
    Qt::ShortcutContext getShortcutContext() const { return m_context; }

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setCheckable(bool checkable);
    bool isCheckable() const { return m_checkable; }

    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }

    void setData(const QJsonObject& data);
    QJsonObject getData() const { return m_data; }

    void trigger();
    void triggerWithData(const QJsonObject& data);

    QAction* qAction() const;

signals:
    void triggered();
    void triggeredWithData(const QJsonObject& data);
    void toggled(bool checked);
    void changed();

private:
    QString m_id;
    QString m_text;
    QString m_category;
    QString m_description;
    QString m_icon;
    QKeySequence m_shortcut;
    Qt::ShortcutContext m_context = Qt::WidgetShortcut;
    bool m_enabled = true;
    bool m_checkable = false;
    bool m_checked = false;
    QJsonObject m_data;
};

class ActionManager : public QObject
{
    Q_OBJECT

public:
    static ActionManager* instance();

    explicit ActionManager(QObject* parent = nullptr);
    ~ActionManager();

    Action* registerAction(const QString& id, const QString& text,
                          const QString& category,
                          const QKeySequence& shortcut = QKeySequence());
    void unregisterAction(const QString& actionId);

    Action* getAction(const QString& actionId) const;
    bool hasAction(const QString& id) const;

    QVector<Action*> getActions(const QString& category = QString()) const;
    QStringList getCategories() const;

    bool trigger(const QString& id);

    bool setShortcut(const QString& actionId, const QKeySequence& shortcut);
    bool isShortcutInUse(const QKeySequence& shortcut, const QString& excludeId = QString()) const;

    bool saveToFile(const QString& path) const;
    bool loadFromFile(const QString& path);

signals:
    void actionTriggered(const QString& actionId);
    void actionRegistered(const QString& actionId);
    void actionUnregistered(const QString& actionId);

private:
    QMap<QString, Action*> m_actions;
    QMap<QString, QShortcut*> m_shortcuts;
};

class ActionGroup : public QObject
{
    Q_OBJECT

public:
    explicit ActionGroup(const QString& id, QObject* parent = nullptr);
    ~ActionGroup();

    QString getId() const { return m_id; }

    void setExclusive(bool exclusive);
    bool isExclusive() const { return m_exclusive; }

    void addAction(Action* action);
    void removeAction(const QString& actionId);

    QVector<Action*> getActions() const { return m_actions; }
    Action* getActiveAction() const { return m_activeAction; }

    void trigger(const QString& actionId);

signals:
    void triggered(const QString& actionId);
    void activeActionChanged(const QString& actionId);

private:
    QString m_id;
    bool m_exclusive = false;
    QVector<Action*> m_actions;
    Action* m_activeAction = nullptr;
};

class GlobalShortcutManager : public QObject
{
    Q_OBJECT

public:
    explicit GlobalShortcutManager(QObject* parent = nullptr);
    ~GlobalShortcutManager();

    void setWidget(QWidget* widget);
    QWidget* getWidget() const { return m_widget; }

    void registerShortcut(QShortcut* shortcut, const QString& actionId);
    void unregisterShortcut(const QString& actionId);

    bool isShortcutInUse(const QKeySequence& shortcut, const QString& excludeAction = QString()) const;
    QString getActionForShortcut(const QKeySequence& shortcut) const;

    void setGlobalShortcut(const QString& actionId, const QKeySequence& shortcut);
    QKeySequence getGlobalShortcut(const QString& actionId) const;

    void clearShortcuts();
    void resetToDefaults();

    QMap<QString, QKeySequence> getAllShortcuts() const { return m_globalShortcuts; }

signals:
    void shortcutInUse(const QString& actionId);

private:
    QWidget* m_widget = nullptr;
    QMap<QString, QShortcut*> m_shortcuts;
    QMap<QKeySequence, QString> m_shortcutToAction;
    QMap<QString, QKeySequence> m_globalShortcuts;
};

class ActionGroupManager;
class MenuBuilder : public QObject
{
    Q_OBJECT

public:
    explicit MenuBuilder(QObject* parent = nullptr);
    ~MenuBuilder();

    void setActionManager(ActionManager* manager);
    void setActionGroupManager(ActionGroupManager* manager);

    QMenu* buildMenu(const QString& menuId);
    QMenuBar* buildMenuBar();

    void buildMenu(const QString& menuId, QMenu* menu);
    void buildMenuBar(QMenuBar* menuBar);

    void registerMenu(const QString& menuId, const QString& title);
    void registerMenuItem(const QString& menuId, Action* action);
    void registerMenuSeparator(const QString& menuId);
    void registerSubmenu(const QString& menuId, QMenu* submenu);

    void unregisterMenu(const QString& menuId);

signals:
    void menuBuilt(QMenu* menu);

private:
    ActionManager* m_actionManager = nullptr;
    ActionGroupManager* m_groupManager = nullptr;

    struct MenuItem {
        enum class Type { Action, Separator, Submenu };
        Type type;
        QString actionId;
        QMenu* submenu;
    };

    QMap<QString, QString> m_menuTitles;
    QMap<QString, QVector<MenuItem>> m_menuItems;
};

class ToolbarBuilder : public QObject
{
    Q_OBJECT

public:
    explicit ToolbarBuilder(QObject* parent = nullptr);
    ~ToolbarBuilder();

    void setActionManager(ActionManager* manager);

    QToolBar* buildToolbar(const QString& toolbarId);
    void buildToolbar(const QString& toolbarId, QToolBar* toolbar);

    void registerToolbar(const QString& toolbarId, const QString& title);
    void registerToolbarItem(const QString& toolbarId, const QString& actionId, int index = -1);
    void registerToolbarSeparator(const QString& toolbarId);

    void unregisterToolbar(const QString& toolbarId);

signals:
    void toolbarBuilt(QToolBar* toolbar);

private:
    ActionManager* m_actionManager = nullptr;

    struct ToolbarItem {
        enum class Type { Action, Separator };
        Type type;
        QString actionId;
    };

    QMap<QString, QString> m_toolbarTitles;
    QMap<QString, QVector<ToolbarItem>> m_toolbarItems;
};

} // namespace ks