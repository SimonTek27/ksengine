#include "engine/sys/UndoStack.h"
#include <QtTest/QtTest>

using namespace ks;

class TestUndoRedo : public QObject {
    Q_OBJECT

private slots:
    void test_defaultState();
    void test_pushAndUndo();
    void test_redo();
    void test_clear();
    void test_limit();
    void test_cleanState();
    void test_mergeableCommands();
    void test_macroCommand();
    void test_commandHistoryBrowser();
};

// Helper command for testing
class TestCommand : public UndoCommand {
public:
    int& m_value;
    int m_oldVal;
    int m_newVal;

    TestCommand(int& value, int newVal, const QString& desc = QString())
        : UndoCommand(desc), m_value(value), m_oldVal(value), m_newVal(newVal) {}

    void undo() override { m_value = m_oldVal; }
    void redo() override { m_value = m_newVal; }
};

class MergeableCommand : public UndoCommand {
public:
    int& m_value;
    int m_delta;

    MergeableCommand(int& value, int delta)
        : UndoCommand("merge"), m_value(value), m_delta(delta) {
        setMergable(true);
    }

    void undo() override { m_value -= m_delta; }
    void redo() override { m_value += m_delta; }

    bool merge(UndoCommand* other) override {
        auto* mc = dynamic_cast<MergeableCommand*>(other);
        if (mc && &mc->m_value == &m_value) {
            m_delta += mc->m_delta;
            m_value += mc->m_delta;
            return true;
        }
        return false;
    }
};

void TestUndoRedo::test_defaultState()
{
    UndoStack stack;
    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
    QCOMPARE(stack.getCount(), 0);
    QVERIFY(stack.isClean());
    QVERIFY(stack.getUndoText().isEmpty());
    QVERIFY(stack.getRedoText().isEmpty());
}

void TestUndoRedo::test_pushAndUndo()
{
    UndoStack stack;
    int value = 0;

    stack.push(new TestCommand(value, 42, "set to 42"));
    QCOMPARE(value, 42);
    QVERIFY(stack.canUndo());
    QCOMPARE(stack.getUndoText(), QString("set to 42"));
    QCOMPARE(stack.getUndoCount(), 1);

    stack.undo();
    QCOMPARE(value, 0);
    QVERIFY(stack.canRedo());
    QCOMPARE(stack.getRedoText(), QString("set to 42"));
    QCOMPARE(stack.getUndoCount(), 0);
    QCOMPARE(stack.getRedoCount(), 1);
}

void TestUndoRedo::test_redo()
{
    UndoStack stack;
    int value = 10;

    stack.push(new TestCommand(value, 20));
    QCOMPARE(value, 20);
    stack.undo();
    QCOMPARE(value, 10);
    stack.redo();
    QCOMPARE(value, 20);
    QVERIFY(!stack.canRedo());
}

void TestUndoRedo::test_clear()
{
    UndoStack stack;
    int value = 0;
    stack.push(new TestCommand(value, 1));
    stack.push(new TestCommand(value, 2));
    QCOMPARE(stack.getCount(), 2);
    stack.clear();
    QCOMPARE(stack.getCount(), 0);
    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
    QVERIFY(stack.isClean());
}

void TestUndoRedo::test_limit()
{
    UndoStack stack;
    stack.setLimit(2);
    int value = 0;
    stack.push(new TestCommand(value, 1));
    stack.push(new TestCommand(value, 2));
    stack.push(new TestCommand(value, 3));
    QVERIFY(stack.getUndoCount() <= 2);
}

void TestUndoRedo::test_cleanState()
{
    UndoStack stack;
    int value = 0;
    QVERIFY(stack.isClean());
    stack.push(new TestCommand(value, 1));
    QVERIFY(!stack.isClean());
    stack.setClean();
    QVERIFY(stack.isClean());
    stack.undo();
    QVERIFY(!stack.isClean());
    stack.redo();
    QVERIFY(stack.isClean());
}

void TestUndoRedo::test_mergeableCommands()
{
    UndoStack stack;
    int value = 0;

    auto* cmd1 = new MergeableCommand(value, 5);
    auto* cmd2 = new MergeableCommand(value, 3);
    stack.push(cmd1);
    stack.push(cmd2);
    QCOMPARE(value, 8);
    stack.undo();
    QCOMPARE(value, 0);
    QCOMPARE(stack.getUndoCount(), 0);
}

void TestUndoRedo::test_macroCommand()
{
    UndoStack stack;
    int value = 10;

    auto* macro = new MacroCommand("macro");
    macro->addCommand(new TestCommand(value, 20));
    macro->addCommand(new TestCommand(value, 30));
    stack.push(macro);
    QCOMPARE(value, 30);
    stack.undo();
    QCOMPARE(value, 10);
}

void TestUndoRedo::test_commandHistoryBrowser()
{
    UndoStack stack;
    int value = 0;
    stack.push(new TestCommand(value, 5, "step1"));
    stack.push(new TestCommand(value, 10, "step2"));

    CommandHistoryBrowser browser;
    browser.setStack(&stack);
    QVERIFY(browser.canGoBack());
    browser.gotoIndex(0);
    QCOMPARE(value, 0);
    QVERIFY(browser.canGoForward());
    browser.gotoIndex(2);
    QCOMPARE(value, 10);
}

QTEST_MAIN(TestUndoRedo)
#include "test_UndoRedo.moc"
