#include "UndoStack.h"
#include <QDebug>

namespace ks {

// ============================================================================
// UndoStack
// ============================================================================

UndoStack::UndoStack(QObject* parent)
    : QObject(parent)
{}

UndoStack::~UndoStack()
{
    clear();
}

void UndoStack::push(UndoCommand* command)
{
    if (!command) return;

    // Try to merge with top of undo stack
    if (command->isMergable() && !m_undoStack.isEmpty()) {
        UndoCommand* top = m_undoStack.top();
        if (top->isMergable() && top->merge(command)) {
            delete command;
            emitSignals();
            return;
        }
    }

    // Clear redo stack on new command
    while (!m_redoStack.isEmpty()) {
        delete m_redoStack.pop();
    }

    // Enforce limit
    while (m_limit > 0 && m_undoStack.size() >= m_limit) {
        delete m_undoStack.takeFirst();
        --m_index;
        if (m_cleanIndex >= 0) m_cleanIndex = qMax(0, m_cleanIndex - 1);
    }

    command->redo();
    m_undoStack.push(command);
    ++m_index;
    emitSignals();
}

void UndoStack::undo()
{
    if (m_undoStack.isEmpty()) return;
    UndoCommand* cmd = m_undoStack.pop();
    cmd->undo();
    m_redoStack.push(cmd);
    --m_index;
    emitSignals();
}

void UndoStack::redo()
{
    if (m_redoStack.isEmpty()) return;
    UndoCommand* cmd = m_redoStack.pop();
    cmd->redo();
    m_undoStack.push(cmd);
    ++m_index;
    emitSignals();
}

QString UndoStack::getUndoText() const
{
    return m_undoStack.isEmpty() ? QString() : m_undoStack.top()->getDescription();
}

QString UndoStack::getRedoText() const
{
    return m_redoStack.isEmpty() ? QString() : m_redoStack.top()->getDescription();
}

void UndoStack::clear()
{
    while (!m_undoStack.isEmpty()) delete m_undoStack.pop();
    while (!m_redoStack.isEmpty()) delete m_redoStack.pop();
    m_cleanIndex = 0;
    m_index = 0;
    emitSignals();
}

void UndoStack::setClean()
{
    m_cleanIndex = m_index;
    emit cleanChanged(true);
}

bool UndoStack::isClean() const
{
    return m_cleanIndex == m_index;
}

void UndoStack::emitSignals()
{
    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
    emit undoTextChanged(getUndoText());
    emit redoTextChanged(getRedoText());
    emit cleanChanged(isClean());
    emit indexChanged(m_index, m_undoStack.size() + m_redoStack.size());
}

// ============================================================================
// CommandHistoryBrowser
// ============================================================================

CommandHistoryBrowser::CommandHistoryBrowser(QObject* parent)
    : QObject(parent)
{}

CommandHistoryBrowser::~CommandHistoryBrowser() = default;

void CommandHistoryBrowser::setStack(UndoStack* stack)
{
    m_stack = stack;
    if (m_stack) {
        connect(m_stack, &UndoStack::indexChanged, this, [this](int, int) {
            emit historyChanged(getUndoActions().toVector(), getRedoActions().toVector());
        });
    }
}

QStringList CommandHistoryBrowser::getUndoActions() const
{
    if (!m_stack) return {};
    // Reflect UndoStack internals via public API only
    QStringList actions;
    if (m_stack->canUndo()) actions << m_stack->getUndoText();
    return actions;
}

QStringList CommandHistoryBrowser::getRedoActions() const
{
    if (!m_stack) return {};
    QStringList actions;
    if (m_stack->canRedo()) actions << m_stack->getRedoText();
    return actions;
}

QVariantMap CommandHistoryBrowser::getActionInfo(int index) const
{
    if (!m_stack || index < 0) return {};

    // Determine if the index refers to an undo or redo action
    QVariantMap info;
    int undoCount = m_stack->getUndoCount();

    if (index < undoCount) {
        // Undo action
        info["type"] = "undo";
        info["description"] = m_stack->getUndoText();
    } else {
        // Redo action
        int redoIdx = index - undoCount;
        if (redoIdx < m_stack->getRedoCount()) {
            info["type"] = "redo";
            info["description"] = m_stack->getRedoText();
        }
    }

    info["index"] = index;
    info["total"] = undoCount + m_stack->getRedoCount();
    return info;
}

void CommandHistoryBrowser::gotoIndex(int index)
{
    if (!m_stack) return;
    // Walk forward or backward to reach target index
    int current = m_stack->getUndoCount();
    while (current > index && m_stack->canUndo()) { m_stack->undo(); --current; }
    while (current < index && m_stack->canRedo()) { m_stack->redo(); ++current; }
}

bool CommandHistoryBrowser::canGoBack() const { return m_stack && m_stack->canUndo(); }
bool CommandHistoryBrowser::canGoForward() const { return m_stack && m_stack->canRedo(); }

void CommandHistoryBrowser::markGroupStart() { m_inGroup = true; }
void CommandHistoryBrowser::markGroupEnd()   { m_inGroup = false; }

CommandBuilder& CommandBuilder::addProperty(const QString& objId,
                                            const QString& prop,
                                            const QVariant& oldVal,
                                            const QVariant& newVal)
{
    auto* cmd = new PropertyCommand<QVariant, QVariant>(
        [oldVal]() -> QVariant { return oldVal; },
        [](const QVariant&) {},
        newVal);
    cmd->setDescription(QString("Change %1.%2").arg(objId, prop));
    addChild(cmd);
    return *this;
}

} // namespace ks
