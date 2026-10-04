#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QStack>
#include <QVariantMap>
#include <QStringList>
#include <QVariant>
#include <functional>
#include <type_traits>

namespace ks {

class UndoCommand {
public:
    UndoCommand(const QString &description = QString())
        : m_description(description)
    {}
    virtual ~UndoCommand() {}

    virtual void undo() = 0;
    virtual void redo() = 0;

    virtual QString getDescription() const { return m_description; }
    void setDescription(const QString &desc) { m_description = desc; }

    virtual bool isMergable() const { return m_mergable; }
    void setMergable(bool mergable) { m_mergable = mergable; }

    virtual bool merge(UndoCommand *other) { Q_UNUSED(other); return false; }

private:
    QString m_description;
    bool m_mergable = false;
};

class MacroCommand : public UndoCommand
{
public:
    MacroCommand(const QString &description = QString())
        : UndoCommand(description)
    {}

    ~MacroCommand() override {
        qDeleteAll(m_commands);
    }

    void addCommand(UndoCommand *cmd) { m_commands.append(cmd); }

    void undo() override {
        for (int i = m_commands.size() - 1; i >= 0; --i)
            m_commands[i]->undo();
    }

    void redo() override {
        for (UndoCommand *cmd : m_commands)
            cmd->redo();
    }

private:
    QVector<UndoCommand*> m_commands;
};

class UndoStack : public QObject
{
    Q_OBJECT

public:
    explicit UndoStack(QObject *parent = nullptr);
    ~UndoStack();

    void push(UndoCommand *command);

    void undo();
    void redo();

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    QString getUndoText() const;
    QString getRedoText() const;

    void clear();

    int getCount() const { return m_undoStack.size() + m_redoStack.size(); }
    int getUndoCount() const { return m_undoStack.size(); }
    int getRedoCount() const { return m_redoStack.size(); }

    void setLimit(int limit) { m_limit = limit; }
    int getLimit() const { return m_limit; }

    void setClean();
    bool isClean() const;
    int getCleanIndex() const { return m_cleanIndex; }
    int getIndex() const { return m_index; }

signals:
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);
    void undoTextChanged(const QString &text);
    void redoTextChanged(const QString &text);
    void cleanChanged(bool clean);
    void indexChanged(int idx, int total);

private:
    void emitSignals();

    QStack<UndoCommand*> m_undoStack;
    QStack<UndoCommand*> m_redoStack;
    int m_limit = 100;
    int m_cleanIndex = 0;
    int m_index = 0;
    bool m_clean = true;
};

class CommandBuilder
{
public:
    CommandBuilder() = default;

    CommandBuilder &setDescription(const QString &desc) {
        if (m_command) m_command->setDescription(desc);
        return *this;
    }

    CommandBuilder &addChild(UndoCommand *cmd) {
        if (!m_macro) m_macro = new MacroCommand();
        m_macro->addCommand(cmd);
        m_command = m_macro;
        return *this;
    }

    CommandBuilder &addProperty(const QString &objId,
                                const QString &prop,
                                const QVariant &oldVal,
                                const QVariant &newVal);

    void execute() {
        if (m_command)
            m_command->redo();
    }

private:
    UndoCommand *m_command = nullptr;
    MacroCommand *m_macro = nullptr;
};

template<typename T, typename V>
class PropertyCommand : public UndoCommand
{
public:
    using Setter = void (T::*)(const V &);
    using Getter = V (T::*)() const;
    using SetterFunc = std::function<void(const V&)>;
    using GetterFunc = std::function<V()>;

    PropertyCommand(T *object,
                    Setter setter,
                    Getter getter,
                    const V &newValue,
                    const QString &description = QString())
        : UndoCommand(description)
        , m_object(object)
        , m_setter(setter)
        , m_getter(getter)
        , m_newValue(newValue)
        , m_useFunc(false)
    {
        m_oldValue = (m_object->*m_getter)();
    }

    PropertyCommand(GetterFunc getter, SetterFunc setter, const V &newValue,
                    const QString &description = QString())
        : UndoCommand(description)
        , m_setterFunc(setter)
        , m_getterFunc(getter)
        , m_newValue(newValue)
        , m_useFunc(true)
    {
        m_oldValue = m_getterFunc();
    }

    void undo() override {
        if (m_useFunc) m_setterFunc(m_oldValue);
        else (m_object->*m_setter)(m_oldValue);
    }

    void redo() override {
        if (m_useFunc) m_setterFunc(m_newValue);
        else (m_object->*m_setter)(m_newValue);
    }

    bool merge(UndoCommand *other) override {
        auto *propCmd = dynamic_cast<PropertyCommand<T, V>*>(other);
        if (propCmd && propCmd->m_useFunc == m_useFunc) {
            if (m_useFunc) {
                m_newValue = propCmd->m_newValue;
                return true;
            } else if (propCmd->m_object == m_object && propCmd->m_setter == m_setter) {
                m_newValue = propCmd->m_newValue;
                return true;
            }
        }
        return false;
    }

private:
    T *m_object = nullptr;
    Setter m_setter = nullptr;
    Getter m_getter = nullptr;
    SetterFunc m_setterFunc;
    GetterFunc m_getterFunc;
    bool m_useFunc = false;
    V m_oldValue;
    V m_newValue;
};

class CommandHistoryBrowser : public QObject
{
    Q_OBJECT

public:
    explicit CommandHistoryBrowser(QObject *parent = nullptr);
    ~CommandHistoryBrowser();

    void setStack(UndoStack *stack);

    QStringList getUndoActions() const;
    QStringList getRedoActions() const;

    QVariantMap getActionInfo(int index) const;

    void gotoIndex(int index);

    bool canGoBack() const;
    bool canGoForward() const;

    void markGroupStart();
    void markGroupEnd();

signals:
    void historyChanged(const QVector<QString> &undoActions,
                        const QVector<QString> &redoActions);

private:
    UndoStack *m_stack = nullptr;
    QVector<QString> m_groupedActions;
    bool m_inGroup = false;
};

} // namespace ks
