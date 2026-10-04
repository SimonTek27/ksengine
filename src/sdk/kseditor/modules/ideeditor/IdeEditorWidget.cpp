#include "IdeEditorWidget.h"
#include "IdeEditorFileBrowser.h"
#include "IdeEditorSearchPanel.h"
#include "editor/textEditor/CodeEditor.h"
#include "editor/textEditor/SyntaxHighlighter.h"
#include "editor/textEditor/FindReplaceDialog.h"
#include "sdk/kseditor/engine/sys/LogManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QFile>
#include <QShortcut>
#include <QApplication>
#include <QFont>
#include <QTextCursor>
#include <functional>

namespace ks {

IdeEditorWidget::IdeEditorWidget(QWidget* parent)
    : QWidget(parent)
    , m_findDialog(nullptr)
{
    setupUI();
}

IdeEditorWidget::~IdeEditorWidget()
{
    delete m_findDialog;
}

void IdeEditorWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupToolbar(mainLayout);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Left: file browser
    m_fileBrowser = new IdeEditorFileBrowser(this);
    m_fileBrowser->setMinimumWidth(180);
    m_fileBrowser->setMaximumWidth(400);
    m_splitter->addWidget(m_fileBrowser);

    // Center: editor tabs
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 0; }"
        "QTabBar::tab {"
        "  background: #2d2d2d; color: #aaa; border: 1px solid #3a3a3a;"
        "  padding: 4px 12px; margin: 0; min-width: 80px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #1e1e1e; color: #fff; border-bottom: 2px solid #569cd6;"
        "}"
        "QTabBar::tab:hover { background: #3a3a3a; }");
    m_splitter->addWidget(m_tabWidget);

    // Right: search panel
    m_searchPanel = new IdeEditorSearchPanel(this);
    m_searchPanel->setMinimumWidth(200);
    m_searchPanel->setMaximumWidth(400);
    m_splitter->addWidget(m_searchPanel);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);

    mainLayout->addWidget(m_splitter, 1);

    // Status bar
    auto* statusBar = new QWidget(this);
    statusBar->setFixedHeight(24);
    statusBar->setStyleSheet("background: #007acc;");
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(8, 0, 8, 0);
    m_statusLabel = new QLabel("IDE Editor ready", statusBar);
    m_statusLabel->setStyleSheet("color: #fff; font-size: 11px;");
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    mainLayout->addWidget(statusBar);

    // Connections
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &IdeEditorWidget::onTabCloseRequested);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &IdeEditorWidget::onTabChanged);
    connect(m_fileBrowser, &IdeEditorFileBrowser::fileDoubleClicked,
            this, &IdeEditorWidget::onFileBrowserDoubleClick);
    connect(m_searchPanel, &IdeEditorSearchPanel::navigateToResult,
            this, &IdeEditorWidget::onSearchNavigate);

    // Keyboard shortcuts
    auto* findShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(findShortcut, &QShortcut::activated, this, &IdeEditorWidget::find);
    auto* saveShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this);
    connect(saveShortcut, &QShortcut::activated, this, &IdeEditorWidget::saveCurrent);
    auto* openShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_O), this);
    connect(openShortcut, &QShortcut::activated, this, &IdeEditorWidget::openFiles);
}

void IdeEditorWidget::setupToolbar(QBoxLayout* mainLayout)
{
    m_toolbar = new QToolBar("IDE Editor Toolbar", this);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setStyleSheet(
        "QToolBar { background: #2d2d2d; border: none; border-bottom: 1px solid #3a3a3a; padding: 2px; spacing: 2px; }"
        "QToolButton { padding: 3px 8px; border-radius: 3px; color: #ccc; }"
        "QToolButton:hover { background: #3a3a3a; }"
        "QToolButton:pressed { background: #4a4a4a; }");

    auto addBtn = [&](const QString& text, std::function<void()> slot) {
        auto* btn = new QPushButton(text, m_toolbar);
        btn->setStyleSheet(
            "QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 3px 10px; }"
            "QPushButton:hover { background: #4a6a9a; }");
        connect(btn, &QPushButton::clicked, this, slot);
        m_toolbar->addWidget(btn);
        return btn;
    };

    addBtn("New", [this](){ newFile(); });
    addBtn("Open", [this](){ openFiles(); });
    addBtn("Save", [this](){ saveCurrent(); });
    addBtn("Save As", [this](){ saveCurrentAs(); });
    m_toolbar->addSeparator();
    addBtn("Cut", [this](){ cut(); });
    addBtn("Copy", [this](){ copy(); });
    addBtn("Paste", [this](){ paste(); });
    m_toolbar->addSeparator();
    addBtn("Find", [this](){ find(); });
    addBtn("Go To", [this](){ gotoLine(); });
    m_toolbar->addSeparator();
    addBtn("Zoom +", [this](){ zoomIn(); });
    addBtn("Zoom -", [this](){ zoomOut(); });

    mainLayout->addWidget(m_toolbar);
}

void IdeEditorWidget::newFile()
{
    auto* editor = new CodeEditor(this);
    int index = m_tabWidget->addTab(editor, "untitled");
    m_tabPaths[index] = QString();
    m_tabModified[index] = false;
    m_tabWidget->setCurrentIndex(index);
    editor->setFocus();
    connect(editor, &CodeEditor::textChanged, this, &IdeEditorWidget::onDocumentModified);
    updateTitle(index);
}

void IdeEditorWidget::openFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "Open File(s)",
        QString(), "All Files (*)");
    for (const QString& path : files) {
        openFile(path);
    }
}

void IdeEditorWidget::openFile(const QString& filePath)
{
    if (filePath.isEmpty()) return;

    int existingTab = findTabByPath(filePath);
    if (existingTab >= 0) {
        m_tabWidget->setCurrentIndex(existingTab);
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR("IdeEditor", "Failed to open file: " + filePath);
        return;
    }

    auto* editor = new CodeEditor(this);
    QTextStream in(&file);
    editor->setPlainText(in.readAll());
    file.close();

    SyntaxHighlighter::Language lang = SyntaxHighlighter::detectLanguage(filePath);
    if (lang != SyntaxHighlighter::None) {
        auto* highlighter = new SyntaxHighlighter();
        highlighter->setLanguage(lang);
        editor->setSyntaxHighlighter(highlighter);
    }

    QString fileName = QFileInfo(filePath).fileName();
    int index = m_tabWidget->addTab(editor, fileName);
    m_tabPaths[index] = filePath;
    m_tabModified[index] = false;
    m_tabWidget->setCurrentIndex(index);

    connect(editor, &CodeEditor::textChanged, this, &IdeEditorWidget::onDocumentModified);
    if (m_findDialog) m_findDialog->setEditor(editor);

    updateTitle(index);
    emit fileOpened(filePath);
    LOG_INFO("IdeEditor", "Opened file: " + filePath);
}

void IdeEditorWidget::saveCurrent()
{
    int index = m_tabWidget->currentIndex();
    if (index < 0) return;

    QString path = m_tabPaths.value(index);
    if (path.isEmpty()) {
        saveCurrentAs();
        return;
    }

    auto* editor = editorAt(index);
    if (!editor) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR("IdeEditor", "Failed to save file: " + path);
        return;
    }

    file.write(editor->toPlainText().toUtf8());
    file.close();

    m_tabModified[index] = false;
    QString text = m_tabWidget->tabText(index);
    if (text.endsWith(" *")) {
        m_tabWidget->setTabText(index, text.left(text.length() - 2));
    }
    updateTitle(index);
    emit fileSaved(path);
    LOG_INFO("IdeEditor", "Saved file: " + path);
}

void IdeEditorWidget::saveCurrentAs()
{
    int index = m_tabWidget->currentIndex();
    if (index < 0) return;

    auto* editor = editorAt(index);
    if (!editor) return;

    QString path = QFileDialog::getSaveFileName(this, "Save File As",
        m_tabPaths.value(index), "All Files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    file.write(editor->toPlainText().toUtf8());
    file.close();

    m_tabPaths[index] = path;
    m_tabModified[index] = false;
    m_tabWidget->setTabText(index, QFileInfo(path).fileName());
    updateTitle(index);

    SyntaxHighlighter::Language lang = SyntaxHighlighter::detectLanguage(path);
    if (lang != SyntaxHighlighter::None) {
        auto* highlighter = new SyntaxHighlighter();
        highlighter->setLanguage(lang);
        editor->setSyntaxHighlighter(highlighter);
    }

    emit fileSaved(path);
}

void IdeEditorWidget::closeCurrentTab()
{
    closeTab(m_tabWidget->currentIndex());
}

void IdeEditorWidget::closeTab(int index)
{
    if (index < 0 || index >= m_tabWidget->count()) return;

    if (m_tabModified.value(index, false)) {
        QString name = m_tabWidget->tabText(index);
        auto reply = QMessageBox::question(this, "Unsaved Changes",
            QString("Save changes to '%1'?").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Save) {
            m_tabWidget->setCurrentIndex(index);
            saveCurrent();
        } else if (reply == QMessageBox::Cancel) {
            return;
        }
    }

    QString path = m_tabPaths.value(index);
    m_tabPaths.remove(index);
    m_tabModified.remove(index);
    m_tabWidget->removeTab(index);

    if (!path.isEmpty()) emit fileClosed(path);
}

void IdeEditorWidget::onTabChanged(int index)
{
    if (index >= 0) {
        auto* editor = editorAt(index);
        if (editor && m_findDialog) m_findDialog->setEditor(editor);
        updateTitle(index);
    } else {
        m_statusLabel->setText("No file open");
    }
}

void IdeEditorWidget::onTabCloseRequested(int index)
{
    closeTab(index);
}

void IdeEditorWidget::onDocumentModified()
{
    auto* editor = qobject_cast<CodeEditor*>(sender());
    if (!editor) return;

    int index = m_tabWidget->indexOf(editor);
    if (index < 0) return;

    bool wasModified = m_tabModified.value(index, false);
    if (!wasModified) {
        m_tabModified[index] = true;
        QString text = m_tabWidget->tabText(index);
        if (!text.endsWith(" *")) {
            m_tabWidget->setTabText(index, text + " *");
        }
    }
    updateTitle(index);
}

void IdeEditorWidget::onFileBrowserDoubleClick(const QString& filePath)
{
    openFile(filePath);
}

void IdeEditorWidget::onSearchNavigate(const QString& filePath, int line)
{
    openFile(filePath);
    auto* editor = currentEditor();
    if (editor && line > 0) {
        editor->gotoLine(line);
        editor->setFocus();
    }
}

CodeEditor* IdeEditorWidget::currentEditor() const
{
    return editorAt(m_tabWidget->currentIndex());
}

CodeEditor* IdeEditorWidget::editorAt(int index) const
{
    return qobject_cast<CodeEditor*>(m_tabWidget->widget(index));
}

int IdeEditorWidget::findTabByPath(const QString& path) const
{
    for (auto it = m_tabPaths.begin(); it != m_tabPaths.end(); ++it) {
        if (it.value() == path) return it.key();
    }
    return -1;
}

void IdeEditorWidget::updateTitle(int index)
{
    if (index < 0) {
        m_statusLabel->setText("No file open");
        emit statusChanged("No file open");
        return;
    }

    QString path = m_tabPaths.value(index);
    QString lineInfo;
    auto* editor = editorAt(index);
    if (editor) {
        lineInfo = QString("  |  Line %1").arg(editor->currentLineNumber());
    }

    QString status;
    if (path.isEmpty()) {
        status = QString("untitled%1").arg(lineInfo);
    } else {
        status = QString("%1%2").arg(path, lineInfo);
    }

    SyntaxHighlighter::Language lang = SyntaxHighlighter::detectLanguage(path);
    if (lang != SyntaxHighlighter::None) {
        status += QString("  [%1]").arg(SyntaxHighlighter::languageName(lang));
    }

    m_statusLabel->setText(status);
    emit statusChanged(status);
}

bool IdeEditorWidget::canCut() const { return currentEditor() != nullptr; }
bool IdeEditorWidget::canCopy() const { return currentEditor() != nullptr; }
bool IdeEditorWidget::canPaste() const { return currentEditor() != nullptr; }
bool IdeEditorWidget::canDelete() const { return currentEditor() != nullptr; }

void IdeEditorWidget::cut()
{
    if (auto* editor = currentEditor()) editor->cut();
}

void IdeEditorWidget::copy()
{
    if (auto* editor = currentEditor()) editor->copy();
}

void IdeEditorWidget::paste()
{
    if (auto* editor = currentEditor()) editor->paste();
}

void IdeEditorWidget::deleteSelected()
{
    auto* editor = currentEditor();
    if (editor) {
        QTextCursor cursor = editor->textCursor();
        if (cursor.hasSelection()) cursor.removeSelectedText();
    }
}

void IdeEditorWidget::gotoLine()
{
    auto* editor = currentEditor();
    if (!editor) return;

    bool ok;
    int line = QInputDialog::getInt(this, "Go to Line", "Line number:",
        editor->currentLineNumber(), 1, 1000000, 1, &ok);
    if (ok) editor->gotoLine(line);
}

void IdeEditorWidget::find()
{
    if (auto* editor = currentEditor()) {
        if (!m_findDialog) {
            m_findDialog = new FindReplaceDialog(nullptr);
        }
        m_findDialog->setEditor(editor);
        if (m_findDialog->isHidden()) {
            m_findDialog->show();
            m_findDialog->raise();
        }
        m_findDialog->activateWindow();
    }
}

void IdeEditorWidget::replace()
{
    find();
}

void IdeEditorWidget::zoomIn()
{
    m_currentFontSize = qMin(m_currentFontSize + 1, kMaxZoom);
    QFont font("Consolas", m_currentFontSize);
    font.setStyleHint(QFont::Monospace);
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (auto* editor = editorAt(i)) {
            editor->setFont(font);
            editor->setTabStopDistance(QFontMetrics(font).horizontalAdvance(' ') * 4);
        }
    }
}

void IdeEditorWidget::zoomOut()
{
    m_currentFontSize = qMax(m_currentFontSize - 1, kMinZoom);
    QFont font("Consolas", m_currentFontSize);
    font.setStyleHint(QFont::Monospace);
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (auto* editor = editorAt(i)) {
            editor->setFont(font);
            editor->setTabStopDistance(QFontMetrics(font).horizontalAdvance(' ') * 4);
        }
    }
}

void IdeEditorWidget::resetZoom()
{
    m_currentFontSize = kBaseFontSize;
    QFont font("Consolas", m_currentFontSize);
    font.setStyleHint(QFont::Monospace);
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (auto* editor = editorAt(i)) {
            editor->setFont(font);
            editor->setTabStopDistance(QFontMetrics(font).horizontalAdvance(' ') * 4);
        }
    }
}

void IdeEditorWidget::setStatusText(const QString& text)
{
    m_statusLabel->setText(text);
    emit statusChanged(text);
}

} // namespace ks
