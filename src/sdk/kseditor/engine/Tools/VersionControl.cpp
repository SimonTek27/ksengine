#include "VersionControl.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QDateTime>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

namespace ks {

VersionControl* VersionControl::s_instance = nullptr;

VersionControl* VersionControl::instance()
{
    if (!s_instance) s_instance = new VersionControl();
    return s_instance;
}

VersionControl::VersionControl(QObject* parent)
    : QObject(parent)
    , m_state(VCSState::Uninitialized)
{
    // Detect git executable
    m_gitPath = "git"; // assume it's in PATH on Win11
}

VersionControl::~VersionControl() { s_instance = nullptr; }

bool VersionControl::isGitAvailable() const
{
    QProcess p;
    p.start(m_gitPath, {"--version"});
    if (!p.waitForStarted(3000)) {
        return false;
    }
    p.waitForFinished(3000);
    return p.exitCode() == 0;
}

bool VersionControl::initRepository(const QString& path)
{
    m_repoPath = path;
    return runGit({"init", path});
}

bool VersionControl::openRepository(const QString& path)
{
    QDir d(path);
    while (!d.exists(".git")) {
        if (!d.cdUp()) {
            m_state = VCSState::NotARepository;
            emit stateChanged(m_state);
            return false;
        }
    }
    m_repoPath = d.absolutePath();
    m_state = VCSState::Clean;
    emit stateChanged(m_state);
    return true;
}

bool VersionControl::isRepository() const
{
    return m_state != VCSState::Uninitialized && m_state != VCSState::NotARepository;
}

QString VersionControl::getRepositoryPath() const { return m_repoPath; }

QVector<VCFile> VersionControl::getStatus() const
{
    QVector<VCFile> files;
    if (!isRepository()) return files;

    QProcess p;
    p.setWorkingDirectory(m_repoPath);
    p.start(m_gitPath, {"status", "--porcelain"});
    p.waitForFinished(5000);

    const QString out = p.readAllStandardOutput();
    for (const QString& line : out.split('\n', Qt::SkipEmptyParts)) {
        if (line.size() < 3) continue;
        VCFile f;
        QChar x = line[0], y = line[1];
        f.path      = line.mid(3).trimmed();
        f.isNew     = (x == '?' || x == 'A');
        f.isDeleted = (x == 'D' || y == 'D');
        f.isModified = (x == 'M' || y == 'M');
        files << f;
    }
    return files;
}

bool VersionControl::stageFile(const QString& path)
{
    return runGit({"add", path});
}

bool VersionControl::stageAll()
{
    return runGit({"add", "-A"});
}

bool VersionControl::unstageFile(const QString& path)
{
    return runGit({"reset", "HEAD", path});
}

bool VersionControl::commit(const QString& message, const QString& author)
{
    QStringList args = {"commit", "-m", message};
    if (!author.isEmpty()) {
        args << "--author" << author;
    }
    bool ok = runGit(args);
    if (ok) emit commitCreated(message);
    return ok;
}

bool VersionControl::revertFile(const QString& path)
{
    return runGit({"checkout", "--", path});
}

bool VersionControl::revertAll()
{
    return runGit({"checkout", "--", "."});
}

QVector<VCCommit> VersionControl::getLog(int maxCount) const
{
    QVector<VCCommit> commits;
    if (!isRepository()) return commits;

    QProcess p;
    p.setWorkingDirectory(m_repoPath);
    p.start(m_gitPath, {"log",
        QString("--max-count=%1").arg(maxCount),
        "--pretty=format:%H|%an|%ae|%s|%ci"});
    p.waitForFinished(5000);

    const QString output = QString::fromUtf8(p.readAllStandardOutput());
    for (const QString& line : output.split('\n', Qt::SkipEmptyParts)) {
        QStringList parts = line.split('|');
        if (parts.size() < 5) continue;
        VCCommit c;
        c.hash    = parts[0];
        c.author  = parts[1];
        c.email   = parts[2];
        c.message = parts[3];
        c.date    = QDateTime::fromString(parts[4].left(19), "yyyy-MM-dd HH:mm:ss");
        commits << c;
    }
    return commits;
}

QVector<VCBranch> VersionControl::getBranches() const
{
    QVector<VCBranch> branches;
    if (!isRepository()) return branches;

    QProcess p;
    p.setWorkingDirectory(m_repoPath);
    p.start(m_gitPath, {"branch", "-a", "--format=%(refname:short)|%(HEAD)|%(objectname:short)"});
    p.waitForFinished(5000);

    const QString output = QString::fromUtf8(p.readAllStandardOutput());
    for (const QString& line : output.split('\n', Qt::SkipEmptyParts)) {
        QStringList parts = line.split('|');
        if (parts.isEmpty()) continue;
        VCBranch b;
        b.name     = parts.value(0).trimmed();
        b.isCurrent = parts.value(1).trimmed() == "*";
        b.isRemote  = b.name.startsWith("remotes/");
        b.head      = parts.value(2).trimmed();
        branches << b;
    }
    return branches;
}

bool VersionControl::createBranch(const QString& name)
{
    return runGit({"checkout", "-b", name});
}

bool VersionControl::switchBranch(const QString& name)
{
    bool ok = runGit({"checkout", name});
    if (ok) emit branchChanged(name);
    return ok;
}

bool VersionControl::deleteBranch(const QString& name)
{
    return runGit({"branch", "-d", name});
}

bool VersionControl::mergeBranch(const QString& name)
{
    return runGit({"merge", name});
}

QString VersionControl::getCurrentBranch() const
{
    if (!isRepository()) return {};
    QProcess p;
    p.setWorkingDirectory(m_repoPath);
    p.start(m_gitPath, {"rev-parse", "--abbrev-ref", "HEAD"});
    p.waitForFinished(3000);
    return p.readAllStandardOutput().trimmed();
}

bool VersionControl::runGit(const QStringList& args) const
{
    if (m_repoPath.isEmpty()) return false;
    QProcess p;
    p.setWorkingDirectory(m_repoPath);
    p.start(m_gitPath, args);
    if (!p.waitForStarted(5000)) {
        qWarning() << "[VersionControl] git" << args.join(' ') << "failed to start:" << p.errorString();
        return false;
    }
    p.waitForFinished(10000);
    bool ok = p.exitCode() == 0;
    if (!ok)
        qWarning() << "[VersionControl] git" << args.join(' ')
                   << "failed:" << p.readAllStandardError();
    return ok;
}

// ─── VersionControlUI ────────────────────────────────────────────────────────

VersionControlUI::VersionControlUI(QObject* parent) : QObject(parent) {}
VersionControlUI::~VersionControlUI() = default;

void VersionControlUI::setVersionControl(VersionControl* vc) { m_vc = vc; }
void VersionControlUI::showCommitDialog()
{
    if (!m_vc || !m_vc->isRepository()) return;

    QDialog dialog;
    dialog.setWindowTitle("Commit Changes");
    dialog.setMinimumSize(500, 400);

    auto* layout = new QVBoxLayout(&dialog);

    // Status list
    auto* statusList = new QTreeWidget(&dialog);
    statusList->setHeaderLabels({"Status", "File"});
    auto status = m_vc->getStatus();
    for (const auto& f : status) {
        auto* item = new QTreeWidgetItem(statusList);
        QString statusStr;
        if (f.isNew) statusStr = "A";
        else if (f.isDeleted) statusStr = "D";
        else if (f.isModified) statusStr = "M";
        else if (f.isRenamed) statusStr = "R";
        else statusStr = "?";
        item->setText(0, statusStr);
        item->setText(1, f.path);
    }
    layout->addWidget(statusList);

    // Commit message
    auto* msgEdit = new QTextEdit(&dialog);
    msgEdit->setPlaceholderText("Enter commit message...");
    msgEdit->setMaximumHeight(80);
    layout->addWidget(msgEdit);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    auto* stageAllBtn = new QPushButton("Stage All", &dialog);
    auto* commitBtn = new QPushButton("Commit", &dialog);
    auto* cancelBtn = new QPushButton("Cancel", &dialog);
    btnLayout->addWidget(stageAllBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(commitBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(stageAllBtn, &QPushButton::clicked, m_vc, &VersionControl::stageAll);
    connect(commitBtn, &QPushButton::clicked, [&]() {
        QString msg = msgEdit->toPlainText().trimmed();
        if (!msg.isEmpty()) {
            m_vc->commit(msg);
            emit committed();
            dialog.accept();
        }
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

void VersionControlUI::showBranchDialog()
{
    if (!m_vc || !m_vc->isRepository()) return;

    QDialog dialog;
    dialog.setWindowTitle("Branches");
    dialog.setMinimumSize(400, 300);

    auto* layout = new QVBoxLayout(&dialog);

    auto* branchList = new QTreeWidget(&dialog);
    branchList->setHeaderLabels({"Branch", "Current"});
    auto branches = m_vc->getBranches();
    QString current = m_vc->getCurrentBranch();
    for (const auto& b : branches) {
        auto* item = new QTreeWidgetItem(branchList);
        item->setText(0, b.name);
        item->setText(1, b.name == current ? "*" : "");
    }
    layout->addWidget(branchList);

    // Create branch
    auto* createLayout = new QHBoxLayout();
    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText("New branch name...");
    auto* createBtn = new QPushButton("Create", &dialog);
    createLayout->addWidget(nameEdit);
    createLayout->addWidget(createBtn);
    layout->addLayout(createLayout);

    // Action buttons
    auto* btnLayout = new QHBoxLayout();
    auto* switchBtn = new QPushButton("Switch", &dialog);
    auto* deleteBtn = new QPushButton("Delete", &dialog);
    auto* closeBtn = new QPushButton("Close", &dialog);
    btnLayout->addWidget(switchBtn);
    btnLayout->addWidget(deleteBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    connect(createBtn, &QPushButton::clicked, [&]() {
        QString name = nameEdit->text().trimmed();
        if (!name.isEmpty()) {
            m_vc->createBranch(name);
            nameEdit->clear();
            branchList->clear();
            for (const auto& b : m_vc->getBranches()) {
                auto* item = new QTreeWidgetItem(branchList);
                item->setText(0, b.name);
                item->setText(1, b.name == m_vc->getCurrentBranch() ? "*" : "");
            }
        }
    });

    connect(switchBtn, &QPushButton::clicked, [&]() {
        auto* sel = branchList->currentItem();
        if (sel) {
            m_vc->switchBranch(sel->text(0));
            emit switched();
            dialog.accept();
        }
    });

    connect(deleteBtn, &QPushButton::clicked, [&]() {
        auto* sel = branchList->currentItem();
        if (sel) m_vc->deleteBranch(sel->text(0));
    });

    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

void VersionControlUI::showDiffViewer(const QString& path)
{
    if (!m_vc || !m_vc->isRepository()) return;

    QDialog dialog;
    dialog.setWindowTitle("Diff: " + QFileInfo(path).fileName());
    dialog.setMinimumSize(700, 500);

    auto* layout = new QVBoxLayout(&dialog);

    auto* diffView = new QTextEdit(&dialog);
    diffView->setReadOnly(true);
    diffView->setFont(QFont("Consolas", 9));

    // Run git diff
    QProcess proc;
    proc.setWorkingDirectory(m_vc->getRepositoryPath());
    proc.start("git", {"diff", "--", path});
    proc.waitForFinished(5000);
    QString diff = QString::fromUtf8(proc.readAllStandardOutput());

    if (diff.isEmpty()) {
        diffView->setPlainText("No changes or file not tracked.");
    } else {
        diffView->setPlainText(diff);
    }

    layout->addWidget(diffView);

    auto* closeBtn = new QPushButton("Close", &dialog);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    layout->addWidget(closeBtn);

    dialog.exec();
}

void VersionControlUI::showHistoryViewer()
{
    if (!m_vc || !m_vc->isRepository()) return;

    QDialog dialog;
    dialog.setWindowTitle("Commit History");
    dialog.setMinimumSize(600, 400);

    auto* layout = new QVBoxLayout(&dialog);

    auto* logList = new QTreeWidget(&dialog);
    logList->setHeaderLabels({"Hash", "Author", "Date", "Message"});
    logList->setColumnWidth(0, 80);
    logList->setColumnWidth(1, 120);
    logList->setColumnWidth(2, 140);

    auto log = m_vc->getLog(100);
    for (const auto& c : log) {
        auto* item = new QTreeWidgetItem(logList);
        item->setText(0, c.hash.left(8));
        item->setText(1, c.author);
        item->setText(2, c.date.toString("yyyy-MM-dd HH:mm"));
        item->setText(3, c.message);
    }
    layout->addWidget(logList);

    auto* closeBtn = new QPushButton("Close", &dialog);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    layout->addWidget(closeBtn);

    dialog.exec();
}

void VersionControlUI::refresh()
{
    emit requested();
}

// ─── Integration ──────────────────────────────────────────────────────────

bool Integration::isPathIgnored(const QString& path)
{
    QProcess p;
    p.setWorkingDirectory(QFileInfo(path).absolutePath());
    p.start("git", {"check-ignore", "-q", path});
    p.waitForFinished(3000);
    return p.exitCode() == 0;
}

bool Integration::isPathVersionControlled(const QString& path)
{
    QProcess p;
    p.setWorkingDirectory(QFileInfo(path).absolutePath());
    p.start("git", {"ls-files", "--error-unmatch", path});
    p.waitForFinished(3000);
    return p.exitCode() == 0;
}

QString Integration::getRepoRoot(const QString& path)
{
    QProcess p;
    p.setWorkingDirectory(QFileInfo(path).absolutePath());
    p.start("git", {"rev-parse", "--show-toplevel"});
    p.waitForFinished(3000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString Integration::getRelativePath(const QString& path)
{
    QString repoRoot = getRepoRoot(path);
    if (repoRoot.isEmpty()) return path;
    QFileInfo fi(path);
    return repoRoot.isEmpty() ? path : fi.absoluteFilePath().mid(repoRoot.length() + 1);
}

bool Integration::hasUncommittedChanges(const QString& repoPath)
{
    QProcess p;
    p.setWorkingDirectory(repoPath);
    p.start("git", {"status", "--porcelain"});
    p.waitForFinished(3000);
    return !p.readAllStandardOutput().trimmed().isEmpty();
}

bool Integration::hasConflicts(const QString& repoPath)
{
    QProcess p;
    p.setWorkingDirectory(repoPath);
    p.start("git", {"diff", "--name-only", "--diff-filter=U"});
    p.waitForFinished(3000);
    return !p.readAllStandardOutput().trimmed().isEmpty();
}

QString Integration::getCurrentBranch(const QString& repoPath)
{
    QProcess p;
    p.setWorkingDirectory(repoPath);
    p.start("git", {"rev-parse", "--abbrev-ref", "HEAD"});
    p.waitForFinished(3000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QDateTime Integration::getLastCommitTime(const QString& repoPath)
{
    QProcess p;
    p.setWorkingDirectory(repoPath);
    p.start("git", {"log", "-1", "--format=%ci"});
    p.waitForFinished(3000);
    QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    return QDateTime::fromString(out.left(19), "yyyy-MM-dd HH:mm:ss");
}

} // namespace ks
