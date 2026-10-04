#include "SettingsManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

SettingsManager::SettingsManager(QObject* parent)
    : QObject(parent)
{
    m_settings = new QSettings("ksEditor", "ksEditorQt", this);
    setupDefaults();
    applyDefaults();
}

SettingsManager::~SettingsManager() = default;

QVariant SettingsManager::value(const QString& key, const QVariant& defaultValue) const
{
    if (m_overrides.contains(key)) return m_overrides.value(key);
    return m_settings->value(key, defaultValue);
}

void SettingsManager::setValue(const QString& key, const QVariant& value)
{
    QVariant oldValue = m_settings->value(key);

    if (oldValue.isValid() && oldValue == value) return;

    m_settings->setValue(key, value);
    QString fullKey = m_currentGroup.isEmpty() ? key : m_currentGroup + "/" + key;
    emit settingChanged(fullKey, value);
}

QString SettingsManager::string(const QString& key, const QString& defaultValue) const
{
    return value(key, defaultValue).toString();
}

int SettingsManager::integer(const QString& key, int defaultValue) const
{
    return value(key, defaultValue).toInt();
}

bool SettingsManager::boolean(const QString& key, bool defaultValue) const
{
    return value(key, defaultValue).toBool();
}

double SettingsManager::real(const QString& key, double defaultValue) const
{
    return value(key, defaultValue).toDouble();
}

void SettingsManager::beginGroup(const QString& group)
{
    if (!m_currentGroup.isEmpty()) {
        m_currentGroup += "/";
    }
    m_currentGroup += group;
    m_settings->beginGroup(group);
}

void SettingsManager::endGroup()
{
    int lastSlash = m_currentGroup.lastIndexOf('/');
    if (lastSlash > 0) {
        m_currentGroup = m_currentGroup.left(lastSlash);
    } else {
        m_currentGroup.clear();
    }
    m_settings->endGroup();
}

QString SettingsManager::group() const
{
    return m_currentGroup;
}

bool SettingsManager::contains(const QString& key) const
{
    if (m_overrides.contains(key)) return true;
    return m_settings->contains(key);
}

void SettingsManager::remove(const QString& key)
{
    m_settings->remove(key);
}

QStringList SettingsManager::childKeys() const
{
    return m_settings->childKeys();
}

QStringList SettingsManager::childGroups() const
{
    return m_settings->childGroups();
}

void SettingsManager::reset(const QString& key)
{
    m_settings->remove(key);
    QString fullKey = m_currentGroup.isEmpty() ? key : m_currentGroup + "/" + key;
    emit settingChanged(fullKey, QVariant());
}

void SettingsManager::resetAll()
{
    m_settings->clear();
    emit groupChanged(QString());
}

void SettingsManager::resetToDefaults()
{
    m_settings->clear();
    m_settings->sync();
    applyDefaults();
    emit settingsReset();
}

void SettingsManager::setTemporaryOverride(const QString& key, const QVariant& value)
{
    m_overrides.insert(key, value);
}

void SettingsManager::clearOverride(const QString& key)
{
    m_overrides.remove(key);
}

void SettingsManager::sync()
{
    m_settings->sync();
}

QString SettingsManager::fileName() const
{
    return m_settings->fileName();
}

bool SettingsManager::exportToFile(const QString& path) const
{
    if (!m_settings) return false;

    QJsonObject root;
    QStringList groups = m_settings->childGroups();
    for (const auto& group : groups) {
        m_settings->beginGroup(group);
        QStringList keys = m_settings->childKeys();
        QJsonObject groupObj;
        for (const auto& key : keys) {
            groupObj[key] = QJsonValue::fromVariant(m_settings->value(key));
        }
        m_settings->endGroup();
        root[group] = groupObj;
    }

    QStringList topKeys = m_settings->childKeys();
    QJsonObject topObj;
    for (const auto& key : topKeys) {
        topObj[key] = QJsonValue::fromVariant(m_settings->value(key));
    }
    if (!topObj.isEmpty()) root["_topLevel"] = topObj;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson());
    file.close();
    return true;
}

bool SettingsManager::importFromFile(const QString& path)
{
    if (!m_settings) return false;

    QFile file(path);
    if (!file.exists()) return false;
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) return false;

    QJsonObject root = doc.object();

    if (root.contains("_topLevel")) {
        QJsonObject topObj = root["_topLevel"].toObject();
        for (auto it = topObj.begin(); it != topObj.end(); ++it) {
            m_settings->setValue(it.key(), it.value().toVariant());
        }
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() == "_topLevel") continue;
        if (!it.value().isObject()) continue;

        m_settings->beginGroup(it.key());
        QJsonObject groupObj = it.value().toObject();
        for (auto git = groupObj.begin(); git != groupObj.end(); ++git) {
            m_settings->setValue(git.key(), git.value().toVariant());
        }
        m_settings->endGroup();
    }

    m_settings->sync();
    emit settingsReset();
    return true;
}

void SettingsManager::setupDefaults()
{
    if (!m_settings->contains("theme")) {
        m_settings->setValue("theme", "dark");
    }

    if (!m_settings->contains("language")) {
        m_settings->setValue("language", "en");
    }

    m_settings->beginGroup("editor");
    if (!m_settings->contains("autoSave")) {
        m_settings->setValue("autoSave", true);
    }
    if (!m_settings->contains("autoSaveInterval")) {
        m_settings->setValue("autoSaveInterval", 300);
    }
    if (!m_settings->contains("showLineNumbers")) {
        m_settings->setValue("showLineNumbers", true);
    }
    if (!m_settings->contains("fontSize")) {
        m_settings->setValue("fontSize", 12);
    }
    if (!m_settings->contains("tabSize")) {
        m_settings->setValue("tabSize", 4);
    }
    m_settings->endGroup();

    m_settings->beginGroup("3dview");
    if (!m_settings->contains("gridSize")) {
        m_settings->setValue("gridSize", 1.0);
    }
    if (!m_settings->contains("showGrid")) {
        m_settings->setValue("showGrid", true);
    }
    if (!m_settings->contains("ambientOcclusion")) {
        m_settings->setValue("ambientOcclusion", true);
    }
    if (!m_settings->contains("antiAliasing")) {
        m_settings->setValue("antiAliasing", true);
    }
    if (!m_settings->contains("shadowQuality")) {
        m_settings->setValue("shadowQuality", 2);
    }
    if (!m_settings->contains("textureQuality")) {
        m_settings->setValue("textureQuality", 2);
    }
    m_settings->endGroup();

    m_settings->beginGroup("project");
    if (!m_settings->contains("defaultProjectPath")) {
        m_settings->setValue("defaultProjectPath", QDir::homePath() + "/ksEditor Projects");
    }
    if (!m_settings->contains("recentProjectsLimit")) {
        m_settings->setValue("recentProjectsLimit", 10);
    }
    m_settings->endGroup();

    m_settings->beginGroup("export");
    if (!m_settings->contains("compressionLevel")) {
        m_settings->setValue("compressionLevel", 5);
    }
    if (!m_settings->contains("exportNormals")) {
        m_settings->setValue("exportNormals", true);
    }
    if (!m_settings->contains("optimizeMeshes")) {
        m_settings->setValue("optimizeMeshes", true);
    }
    m_settings->endGroup();
}

void SettingsManager::applyDefaults()
{
    auto setDefault = [this](const QString& key, const QVariant& value) {
        if (!m_settings->contains(key))
            m_settings->setValue(key, value);
    };
    setDefault("UI/Theme", "dark");
    setDefault("UI/Language", "en");
    setDefault("Editor/AutoSave", true);
    setDefault("Viewport/FOV", 75);
    setDefault("Viewport/MSAA", 4);
}

static SettingsManager* g_settings = nullptr;

SettingsManager* globalSettings()
{
    if (!g_settings) {
        g_settings = new SettingsManager();
    }
    return g_settings;
}
