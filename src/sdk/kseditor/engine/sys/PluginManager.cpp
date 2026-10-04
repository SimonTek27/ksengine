#include "PluginManager.h"
#include "../../plugins/base/PluginBase.h"
#include "../../plugins/simulators/kunos/KsPlugin.h"

#include <QDir>
#include <QFile>
#include <QPluginLoader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QDebug>
#include <QStandardPaths>

namespace ks {

// Provide the storage for PluginManagerBase's singleton pointer
PluginManagerBase* PluginManagerBase::s_instance = nullptr;
PluginManagerBase* PluginManagerBase::instance() { return s_instance; }

static PluginManager* s_pmInstance = nullptr;

PluginManager* PluginManager::instance()
{
    if (!s_pmInstance) s_pmInstance = new PluginManager();
    return s_pmInstance;
}

PluginManager::PluginManager(QObject* parent) : QObject(parent)
{
    PluginManagerBase::s_instance = this;
    m_pluginDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/plugins";
    QDir().mkpath(m_pluginDir);
    
    m_reloadTimer = new QTimer(this);
    connect(m_reloadTimer, &QTimer::timeout, this, &PluginManager::checkForChanges);
}

PluginManager::~PluginManager()
{
    // Shutdown all loaded plugins
    for (auto& entry : m_loaded) {
        if (entry.pluginInstance) entry.pluginInstance->shutdown();
        if (entry.loader)    entry.loader->unload();
    }
    s_instance = nullptr;
    PluginManagerBase::s_instance = nullptr;
}

void PluginManager::setPluginDirectory(const QString& dir)
{
    m_pluginDir = dir;
    QDir().mkpath(dir);
}

void PluginManager::scan()
{
    m_available.clear();
    QDir dir(m_pluginDir);

    // Native Qt plugins (.dll on Win11)
    for (const auto& fi : dir.entryInfoList({"*.dll"}, QDir::Files)) {
        PluginInfo info = readManifest(fi.absoluteFilePath());
        if (!info.id.isEmpty()) m_available.insert(info.id, info);
    }

    // Python script plugins
    for (const auto& fi : dir.entryInfoList({"*.py"}, QDir::Files)) {
        PluginInfo info;
        info.id       = fi.baseName();
        info.name     = fi.baseName();
        info.filePath = fi.absoluteFilePath();
        info.type     = PluginType::Script;
        info.state    = PluginState::Discovered;
        m_available.insert(info.id, info);
    }

    emit scanComplete(m_available.size());
    qInfo() << "[PluginManager] Found" << m_available.size() << "plugins in" << m_pluginDir;
}

PluginInfo PluginManager::readManifest(const QString& dllPath) const
{
    // Load the Qt plugin just to read metadata, then unload
    QPluginLoader loader(dllPath);
    QJsonObject meta = loader.metaData().value("MetaData").toObject();

    PluginInfo info;
    info.id          = meta.value("id").toString(QFileInfo(dllPath).baseName());
    info.name        = meta.value("name").toString(info.id);
    info.author      = meta.value("author").toString();
    info.version     = meta.value("version").toString("1.0");
    info.description = meta.value("description").toString();
    info.filePath    = dllPath;
    info.type        = PluginType::Native;
    info.state       = PluginState::Discovered;
    return info;
}

void PluginManager::loadQtPlugin(const QString& pluginId)
{
    if (m_loaded.contains(pluginId)) { qWarning() << "Already loaded:" << pluginId; return; }
    if (!m_available.contains(pluginId)) { emit pluginError(pluginId, "Not found"); return; }

    const PluginInfo& info = m_available[pluginId];

    if (info.type == PluginType::Native) {
        auto* loader = new QPluginLoader(info.filePath, this);
        QObject* obj = loader->instance();
        if (!obj) {
            emit pluginError(pluginId, loader->errorString());
            delete loader;
            return;
        }

        auto* iface = qobject_cast<PluginInterface*>(obj);
        if (!iface) {
            emit pluginError(pluginId, "Does not implement PluginInterface");
            loader->unload();
            delete loader;
            return;
        }

        if (!iface->initialize()) {
            emit pluginError(pluginId, "initialize() returned false");
            loader->unload();
            delete loader;
            return;
        }

        LoadedEntry entry;
        entry.info      = info;
        entry.loader    = loader;
        entry.pluginInstance = iface;
        m_loaded.insert(pluginId, entry);

        // Apply saved settings
        QSettings s("kseditor", "kseditor");
        QByteArray saved = s.value("Plugins/" + pluginId + "/settings").toByteArray();
        if (!saved.isEmpty()) {
            QJsonObject settings = QJsonDocument::fromJson(saved).object();
            iface->setSettings(settings);
        }

        m_available[pluginId].state = PluginState::Active;
        emit pluginLoaded(pluginId);
        qInfo() << "[PluginManager] Loaded native plugin:" << pluginId;

    } else if (info.type == PluginType::Script) {
        // Python plugins are loaded and executed via the Python console bridge
        // For now we mark them as active; the ScriptConsole handles execution
        LoadedEntry entry;
        entry.info   = info;
        m_loaded.insert(pluginId, entry);
        m_available[pluginId].state = PluginState::Active;
        emit pluginLoaded(pluginId);
        qInfo() << "[PluginManager] Registered script plugin:" << pluginId;
    }
}

void PluginManager::unloadQtPlugin(const QString& pluginId)
{
    if (!m_loaded.contains(pluginId)) return;
    auto& entry = m_loaded[pluginId];

    if (entry.pluginInstance) {
        // Save settings before unloading
        QJsonObject settings = entry.pluginInstance->getSettings();
        if (!settings.isEmpty()) {
            QSettings s("kseditor", "kseditor");
            s.setValue("Plugins/" + pluginId + "/settings",
                       QJsonDocument(settings).toJson(QJsonDocument::Compact));
        }
        entry.pluginInstance->shutdown();
    }
    if (entry.loader) {
        entry.loader->unload();
        entry.loader->deleteLater();
    }

    m_loaded.remove(pluginId);
    if (m_available.contains(pluginId))
        m_available[pluginId].state = PluginState::Inactive;

    emit pluginUnloaded(pluginId);
    qInfo() << "[PluginManager] Unloaded plugin:" << pluginId;
}

void PluginManager::reloadPlugin(const QString& pluginId)
{
    unloadQtPlugin(pluginId);
    loadQtPlugin(pluginId);
}

bool PluginManager::isPluginLoaded(const QString& id) const { return m_loaded.contains(id); }

void PluginManager::enablePlugin(const QString& id)
{
    if (!m_available.contains(id)) return;
    m_available[id].enabled = true;
    if (!isPluginLoaded(id)) loadPlugin(id);
}

void PluginManager::disablePlugin(const QString& id)
{
    m_available[id].enabled = false;
    if (isPluginLoaded(id)) unloadPlugin(id);
}

bool PluginManager::isPluginEnabled(const QString& id) const
{
    return m_available.value(id).enabled;
}

void PluginManager::setPluginSettings(const QString& id, const QJsonObject& settings)
{
    if (m_loaded.contains(id) && m_loaded[id].pluginInstance)
        m_loaded[id].pluginInstance->setSettings(settings);
}

QJsonObject PluginManager::getPluginSettings(const QString& id) const
{
    if (m_loaded.contains(id) && m_loaded[id].pluginInstance)
        return m_loaded[id].pluginInstance->getSettings();
    return {};
}

bool PluginManager::registerImporter(const QString& pluginId, const QStringList& extensions)
{
    if (!isPluginLoaded(pluginId)) return false;
    for (const auto& ext : extensions)
        m_importers.insert(ext.toLower(), pluginId);
    emit importerRegistered(pluginId, extensions);
    return true;
}

bool PluginManager::registerExporter(const QString& pluginId, const QStringList& extensions)
{
    if (!isPluginLoaded(pluginId)) return false;
    for (const auto& ext : extensions)
        m_exporters.insert(ext.toLower(), pluginId);
    emit exporterRegistered(pluginId, extensions);
    return true;
}

QVector<PluginInfo> PluginManager::getAvailablePlugins() const
{
    return m_available.values().toVector();
}

QVector<PluginInfo> PluginManager::getLoadedPlugins() const
{
    QVector<PluginInfo> out;
    for (const auto& e : m_loaded) out << e.info;
    return out;
}

PluginInfo PluginManager::getPluginInfo(const QString& id) const
{
    return m_available.value(id);
}

void PluginManager::saveLoadedList() const
{
    QJsonArray arr;
    for (const auto& id : m_loaded.keys()) arr.append(id);
    QSettings s("kseditor", "kseditor");
    s.setValue("Plugins/loaded", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void PluginManager::restoreLoadedList()
{
    QSettings s("kseditor", "kseditor");
    QByteArray data = s.value("Plugins/loaded").toByteArray();
    if (data.isEmpty()) return;
    for (const auto& v : QJsonDocument::fromJson(data).array())
        loadQtPlugin(v.toString());
}

void PluginManager::enableHotReload(bool enabled)
{
    m_hotReloadEnabled = enabled;
    if (enabled) {
        m_reloadTimer->start(m_hotReloadInterval);
        setupFileWatchers();
    } else {
        m_reloadTimer->stop();
    }
}

void PluginManager::setHotReloadInterval(int ms)
{
    m_hotReloadInterval = ms;
    if (m_hotReloadEnabled) {
        m_reloadTimer->setInterval(ms);
    }
}

void PluginManager::checkForChanges()
{
    if (!m_hotReloadEnabled) return;
    
    QDir dir(m_pluginDir);
    for (const auto& fi : dir.entryInfoList({"*.dll"}, QDir::Files)) {
        QString path = fi.absoluteFilePath();
        if (m_watchedFiles.contains(path)) {
            QFileInfo oldInfo = m_watchedFiles[path];
            if (fi.lastModified() > oldInfo.lastModified()) {
                QString pluginId;
                for (auto it = m_available.constBegin(); it != m_available.constEnd(); ++it) {
                    if (it.value().filePath == path) {
                        pluginId = it.key();
                        break;
                    }
                }
                if (!pluginId.isEmpty() && m_loaded.contains(pluginId)) {
                    emit pluginAboutToReload(pluginId);
                    reloadPlugin(pluginId);
                    emit pluginReloaded(pluginId);
                }
                m_watchedFiles[path] = fi;
            }
        } else {
            m_watchedFiles[path] = fi;
        }
    }
}

void PluginManager::setupFileWatchers()
{
    QDir dir(m_pluginDir);
    for (const auto& fi : dir.entryInfoList({"*.dll"}, QDir::Files)) {
        m_watchedFiles[fi.absoluteFilePath()] = fi;
    }
}

// ── PluginManagerBase interface ────────────────────────────────────────────

void PluginManager::registerPlugin(PluginBase* plugin) {
    if (plugin && !m_simPlugins.contains(plugin->id())) {
        m_simPlugins[plugin->id()] = plugin;
    }
}

void PluginManager::unregisterPlugin(const QString& pluginId) {
    m_simPlugins.remove(pluginId);
}

PluginBase* PluginManager::getPlugin(const QString& pluginId) {
    return m_simPlugins.value(pluginId, nullptr);
}

QList<PluginBase*> PluginManager::allPlugins() const {
    return QList<PluginBase*>(m_simPlugins.values());
}

QStringList PluginManager::availablePlugins() const {
    return QStringList(m_simPlugins.keys());
}

bool PluginManager::loadPlugin(const QString& pluginId) {
    // For simulator plugins this is a no-op (they are registered, not loaded from DLL).
    // Returns true if the plugin is known.
    return m_simPlugins.contains(pluginId);
}

bool PluginManager::unloadPlugin(const QString& pluginId) {
    unregisterPlugin(pluginId);
    return true;
}

QString PluginManager::getContentDirectory(const QString& pluginId) const {
    auto* plugin = m_simPlugins.value(pluginId, nullptr);
    if (auto* sp = dynamic_cast<plugins::kunos::KsPlugin*>(plugin))
        return sp->getContentDirectory();
    return plugin ? plugin->installPath() : QString();
}

QString PluginManager::getCarsDirectory(const QString& pluginId) const {
    auto* plugin = m_simPlugins.value(pluginId, nullptr);
    if (auto* sp = dynamic_cast<plugins::kunos::KsPlugin*>(plugin))
        return sp->getCarsDirectory();
    return plugin ? plugin->installPath() + "/cars" : QString();
}

QString PluginManager::getTracksDirectory(const QString& pluginId) const {
    auto* plugin = m_simPlugins.value(pluginId, nullptr);
    if (auto* sp = dynamic_cast<plugins::kunos::KsPlugin*>(plugin))
        return sp->getTracksDirectory();
    return plugin ? plugin->installPath() + "/tracks" : QString();
}

QStringList PluginManager::getCarList(const QString& pluginId) const {
    auto* plugin = m_simPlugins.value(pluginId, nullptr);
    if (auto* sp = dynamic_cast<plugins::kunos::KsPlugin*>(plugin))
        return sp->getCarList();
    return {};
}

QStringList PluginManager::getTrackList(const QString& pluginId) const {
    auto* plugin = m_simPlugins.value(pluginId, nullptr);
    if (auto* sp = dynamic_cast<plugins::kunos::KsPlugin*>(plugin))
        return sp->getTrackList();
    return {};
}

} // namespace ks
