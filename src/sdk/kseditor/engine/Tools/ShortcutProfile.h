#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <QKeySequence>

namespace ks {

class ShortcutProfile : public QObject
{
    Q_OBJECT

public:
    static ShortcutProfile* instance();

    struct Shortcut {
        QString keySequence;
        QString context;
        bool enabled;
    };

    void createProfile(const QString& profileId, const QString& name);
    void deleteProfile(const QString& profileId);
    void renameProfile(const QString& profileId, const QString& newName);

    QString getProfileId() const { return m_profileId; }
    void setProfileId(const QString& profileId);

    QVector<QString> getProfiles() const { return m_profiles.keys(); }

    void setShortcut(const QString& action, const QString& keySequence,
                      const QString& context = "global");
    QString getShortcut(const QString& action) const;

    void resetToDefaults();
    void resetShortcut(const QString& action);
    void buildDefaults();

    void saveProfile();
    void loadProfile(const QString& profileId);

    void importProfile(const QString& path);
    void exportProfile(const QString& path) const;
    QMap<QString, Shortcut> getAllShortcuts() const;

    void setActiveProfile(const QString& profileId);
    QString getActiveProfileId() const { return m_activeProfileId; }

    bool isShortcutInUse(const QString& keySequence, const QString& excludeAction = QString()) const;

signals:
    void profileCreated(const QString& profileId);
    void profileDeleted(const QString& profileId);
    void profileChanged(const QString& profileId);
    void shortcutChanged(const QString& action, const QString& keySequence);

private:
    ShortcutProfile(QObject* parent = nullptr);
    ~ShortcutProfile();
    Q_DISABLE_COPY(ShortcutProfile)

    QString profilePath(const QString& profileId) const;

    static ShortcutProfile* s_instance;

    QString m_profileId;
    QString m_activeProfileId;

    struct ProfileMeta {
        QString id;
        QString name;
    };

    QMap<QString, ProfileMeta> m_profiles;
    QMap<QString, Shortcut> m_shortcuts;
    QMap<QString, QMap<QString, Shortcut>> m_profileShortcuts;
    QMap<QString, Shortcut> m_defaults;
};

class ShortcutRecorder : public QObject
{
    Q_OBJECT

public:
    static ShortcutRecorder* instance();

    enum class RecordState {
        Idle,
        Recording,
        Waiting
    };

    void startRecording(const QString& action);
    void stopRecording();

    RecordState getState() const { return m_state; }
    QString getRecordingAction() const { return m_recordingAction; }

    void setCaptureContext(bool enabled);
    bool isCaptureContextEnabled() const { return m_captureContext; }

    void setAllowOverride(bool allowed);
    bool isOverrideAllowed() const { return m_allowOverride; }

    void acceptCurrentShortcut();
    void rejectCurrentShortcut();

signals:
    void recordingStarted(const QString& action);
    void recordingCompleted(const QString& action, const QString& keySequence);
    void recordingCancelled();

private:
    ShortcutRecorder(QObject* parent = nullptr);
    ~ShortcutRecorder();
    Q_DISABLE_COPY(ShortcutRecorder)

    static ShortcutRecorder* s_instance;

    RecordState m_state = RecordState::Idle;
    QString m_recordingAction;
    bool m_captureContext = true;
    bool m_allowOverride = false;
};

class ShortcutConflicts : public QObject
{
    Q_OBJECT

public:
    static ShortcutConflicts* instance();

    struct Conflict {
        QString action1;
        QString action2;
        QString keySequence;
    };

    QVector<Conflict> findConflicts() const;
    QVector<Conflict> findConflictsForAction(const QString& action) const;

    bool hasConflict(const QString& action) const;
    bool hasConflictWith(const QString& action, const QString& keySequence) const;

    void resolveConflict(const QString& action, const QString& resolution);

    void autoResolve();

signals:
    void conflictsFound(const QVector<Conflict>& conflicts);
    void conflictResolved(const QString& action);

private:
    ShortcutConflicts(QObject* parent = nullptr);
    ~ShortcutConflicts();
    Q_DISABLE_COPY(ShortcutConflicts)

    static ShortcutConflicts* s_instance;

    QVector<Conflict> m_conflicts;
};

} // namespace ks