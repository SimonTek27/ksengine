#include "ShortcutProfile.h"
#include <QSettings>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

namespace ks {

ShortcutProfile* ShortcutProfile::s_instance = nullptr;

ShortcutRecorder* ShortcutRecorder::s_instance = nullptr;

ShortcutConflicts* ShortcutConflicts::s_instance = nullptr;

ShortcutProfile* ShortcutProfile::instance()
{
    if (!s_instance) s_instance = new ShortcutProfile();
    return s_instance;
}

ShortcutRecorder* ShortcutRecorder::instance()
{
    if (!s_instance) s_instance = new ShortcutRecorder();
    return s_instance;
}

ShortcutConflicts* ShortcutConflicts::instance()
{
    if (!s_instance) s_instance = new ShortcutConflicts();
    return s_instance;
}

ShortcutProfile::ShortcutProfile(QObject* parent) : QObject(parent)
{
    buildDefaults();
}

ShortcutProfile::~ShortcutProfile() { s_instance = nullptr; }

ShortcutRecorder::ShortcutRecorder(QObject* parent) : QObject(parent) {}
ShortcutRecorder::~ShortcutRecorder() { s_instance = nullptr; }

ShortcutConflicts::ShortcutConflicts(QObject* parent) : QObject(parent) {}
ShortcutConflicts::~ShortcutConflicts() { s_instance = nullptr; }

void ShortcutProfile::buildDefaults()
{
    // File actions
    m_defaults["file.new"]    = {"Ctrl+N", "application", true};
    m_defaults["file.open"]   = {"Ctrl+O", "application", true};
    m_defaults["file.save"]   = {"Ctrl+S", "application", true};
    m_defaults["file.saveAs"] = {"Ctrl+Shift+S", "application", true};
    m_defaults["file.close"]  = {"Ctrl+W", "application", true};

    // Edit actions
    m_defaults["edit.undo"]   = {"Ctrl+Z", "application", true};
    m_defaults["edit.redo"]   = {"Ctrl+Y", "application", true};
    m_defaults["edit.cut"]    = {"Ctrl+X", "application", true};
    m_defaults["edit.copy"]   = {"Ctrl+C", "application", true};
    m_defaults["edit.paste"]  = {"Ctrl+V", "application", true};
    m_defaults["edit.delete"] = {"Delete",  "application", true};
    m_defaults["edit.selectAll"] = {"Ctrl+A", "application", true};

    // View actions
    m_defaults["view.fullscreen"] = {"F11", "application", true};
    m_defaults["view.resetCamera"] = {"Numpad0", "viewport", true};
    m_defaults["view.front"]      = {"Numpad1", "viewport", true};
    m_defaults["view.right"]      = {"Numpad3", "viewport", true};
    m_defaults["view.top"]        = {"Numpad7", "viewport", true};

    // Module switching
    m_defaults["module.modeler"] = {"Ctrl+1", "application", true};
    m_defaults["module.physics"] = {"Ctrl+2", "application", true};
    m_defaults["module.sound"]   = {"Ctrl+3", "application", true};
    m_defaults["module.font"]    = {"Ctrl+4", "application", true};
    m_defaults["module.assets"]  = {"Ctrl+5", "application", true};

    // Tools
    m_defaults["tool.select"]    = {"Q", "viewport", true};
    m_defaults["tool.move"]      = {"W", "viewport", true};
    m_defaults["tool.rotate"]    = {"E", "viewport", true};
    m_defaults["tool.scale"]     = {"R", "viewport", true};

    // Misc
    m_defaults["help.open"]      = {"F1", "application", true};
    m_defaults["palette.open"]   = {"Ctrl+Shift+P", "application", true};
}

void ShortcutProfile::createProfile(const QString& profileId, const QString& name)
{
    ProfileMeta meta;
    meta.id   = profileId;
    meta.name = name;
    m_profiles.insert(profileId, meta);
    emit profileCreated(profileId);
}

void ShortcutProfile::deleteProfile(const QString& profileId)
{
    if (profileId == "default") return; // can't delete default
    m_profiles.remove(profileId);
    m_profileShortcuts.remove(profileId);
    emit profileDeleted(profileId);
}

void ShortcutProfile::renameProfile(const QString& profileId, const QString& newName)
{
    if (m_profiles.contains(profileId))
        m_profiles[profileId].name = newName;
}

void ShortcutProfile::setActiveProfile(const QString& profileId)
{
    m_activeProfileId = profileId;
    m_shortcuts = m_profileShortcuts.value(profileId, m_defaults);
    emit profileChanged(profileId);
}

void ShortcutProfile::setShortcut(const QString& action, const QString& keySequence,
                                   const QString& context)
{
    Shortcut sc;
    sc.keySequence = keySequence;
    sc.context     = context.isEmpty() ? "application" : context;
    sc.enabled     = true;
    m_shortcuts.insert(action, sc);
    m_profileShortcuts[m_activeProfileId] = m_shortcuts;
    emit shortcutChanged(action, keySequence);
}

QString ShortcutProfile::getShortcut(const QString& action) const
{
    return m_shortcuts.value(action, m_defaults.value(action)).keySequence;
}

void ShortcutProfile::resetShortcut(const QString& action)
{
    if (m_defaults.contains(action)) {
        m_shortcuts.insert(action, m_defaults[action]);
        m_profileShortcuts[m_activeProfileId] = m_shortcuts;
        emit shortcutChanged(action, m_defaults[action].keySequence);
    }
}

void ShortcutProfile::resetToDefaults()
{
    m_shortcuts = m_defaults;
    m_profileShortcuts[m_activeProfileId] = m_shortcuts;
    emit profileChanged(m_activeProfileId);
}

bool ShortcutProfile::isShortcutInUse(const QString& keySequence, const QString& excludeAction) const
{
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it)
        if (it.key() != excludeAction && it.value().keySequence == keySequence) return true;
    return false;
}

void ShortcutProfile::saveProfile()
{
    exportProfile(profilePath(m_activeProfileId));
}

void ShortcutProfile::loadProfile(const QString& profileId)
{
    importProfile(profilePath(profileId));
}

void ShortcutProfile::importProfile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QString profileId = root.value("profileId").toString(m_activeProfileId);
    QJsonObject shortcuts = root["shortcuts"].toObject();
    for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it) {
        QJsonObject sc = it.value().toObject();
        Shortcut s;
        s.keySequence = sc["key"].toString();
        s.context     = sc["context"].toString("application");
        s.enabled     = sc["enabled"].toBool(true);
        m_shortcuts.insert(it.key(), s);
    }
    m_profileShortcuts[profileId] = m_shortcuts;
    emit profileChanged(profileId);
}

void ShortcutProfile::exportProfile(const QString& path) const
{
    QJsonObject root;
    root["profileId"] = m_activeProfileId;
    QJsonObject shortcuts;
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        QJsonObject sc;
        sc["key"]     = it.value().keySequence;
        sc["context"] = it.value().context;
        sc["enabled"] = it.value().enabled;
        shortcuts[it.key()] = sc;
    }
    root["shortcuts"] = shortcuts;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(root).toJson());
}

QString ShortcutProfile::profilePath(const QString& profileId) const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/shortcuts";
    QDir().mkpath(dir);
    return dir + "/" + profileId + ".json";
}

QMap<QString, ShortcutProfile::Shortcut> ShortcutProfile::getAllShortcuts() const
{
    return m_shortcuts;
}

} // namespace ks
