#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <QDateTime>

namespace ks {

enum class VCSState {
    Uninitialized,
    Clean,
    NotARepository,
    Untracked,
    Modified,
    Staged,
    Committed,
    Conflict,
    Ignored
};

struct VCFile {
    QString path;
    VCSState state = VCSState::Untracked;
    bool isNew = false;
    bool isDeleted = false;
    bool isModified = false;
    bool isRenamed = false;
    QString oldPath;
};

struct VCCommit {
    QString hash;
    QString author;
    QString email;
    QString message;
    QDateTime date;
};

struct VCBranch {
    QString name;
    QString head;
    bool isRemote = false;
    bool isCurrent = false;
};

class VersionControl : public QObject
{
    Q_OBJECT

public:
    static VersionControl* instance();

    QString getRepositoryPath() const;
    bool isRepository() const;

    bool isGitAvailable() const;
    bool initRepository(const QString& path);
    bool openRepository(const QString& path);

    bool stageFile(const QString& path);
    bool stageAll();
    bool unstageFile(const QString& path);

    bool commit(const QString& message, const QString& author = QString());

    QVector<VCFile> getStatus() const;

    bool revertFile(const QString& path);
    bool revertAll();

    QVector<VCCommit> getLog(int maxCount = 50) const;

    QVector<VCBranch> getBranches() const;
    bool createBranch(const QString& name);
    bool switchBranch(const QString& name);
    bool deleteBranch(const QString& name);
    bool mergeBranch(const QString& name);
    QString getCurrentBranch() const;

signals:
    void stateChanged(VCSState state);
    void commitCreated(const QString& message);
    void branchChanged(const QString& name);

private:
    VersionControl(QObject* parent = nullptr);
    ~VersionControl();
    Q_DISABLE_COPY(VersionControl)

    bool runGit(const QStringList& args) const;

    static VersionControl* s_instance;

    QString m_repoPath;
    QString m_gitPath;
    VCSState m_state = VCSState::Uninitialized;
};

class VersionControlUI : public QObject
{
    Q_OBJECT

public:
    explicit VersionControlUI(QObject* parent = nullptr);
    ~VersionControlUI();

    void setVersionControl(VersionControl* vc);

    void showCommitDialog();
    void showBranchDialog();
    void showDiffViewer(const QString& path);
    void showHistoryViewer();

    void refresh();

signals:
    void requested();
    void committed();
    void switched();

private:
    VersionControl* m_vc = nullptr;
};

class Integration {
public:
    static bool isPathIgnored(const QString& path);
    static bool isPathVersionControlled(const QString& path);

    static QString getRepoRoot(const QString& path);
    static QString getRelativePath(const QString& path);

    static bool hasUncommittedChanges(const QString& repoPath);
    static bool hasConflicts(const QString& repoPath);

    static QString getCurrentBranch(const QString& repoPath);
    static QDateTime getLastCommitTime(const QString& repoPath);
};

} // namespace ks