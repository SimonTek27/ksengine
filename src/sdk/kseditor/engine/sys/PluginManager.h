#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QLibrary>
#include <QPluginLoader>
#include <QFileInfo>
#include <QTimer>
#include "../../plugins/base/PluginBase.h"

namespace ks {

enum class PluginType {
    Native,
    Script,
    Unknown
};

enum class PluginState {
    Discovered,
    Active,
    Inactive,
    Error
};

struct PluginInfo {
    QString id;
    QString name;
    QString author;
    QString version;
    QString description;
    PluginType type = PluginType::Unknown;
    QString filePath;
    PluginState state = PluginState::Inactive;
    bool enabled = true;
    bool isBuiltIn = false;
    QString errorMessage;
};

class PluginInterface : public QObject
{
    Q_OBJECT

public:
    explicit PluginInterface(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~PluginInterface() {}

    virtual QString getId() const = 0;
    virtual QString getName() const = 0;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual QJsonObject getSettings() const { return QJsonObject(); }
    virtual void setSettings(const QJsonObject& settings) { Q_UNUSED(settings); }
};

class PluginManager : public QObject, public PluginManagerBase
{
    Q_OBJECT

public:
    static PluginManager* instance();

    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager();

    // PluginManagerBase interface
    void registerPlugin(PluginBase* plugin) override;
    void unregisterPlugin(const QString& pluginId) override;
    PluginBase* getPlugin(const QString& pluginId) override;
    QList<PluginBase*> allPlugins() const override;
    QStringList availablePlugins() const override;
    bool loadPlugin(const QString& pluginId) override;
    bool unloadPlugin(const QString& pluginId) override;
    QString getContentDirectory(const QString& pluginId) const override;
    QString getCarsDirectory(const QString& pluginId) const override;
    QString getTracksDirectory(const QString& pluginId) const override;
    QStringList getCarList(const QString& pluginId) const override;
    QStringList getTrackList(const QString& pluginId) const override;

    void setPluginDirectory(const QString& dir);
    QString getPluginDirectory() const { return m_pluginDir; }

    void scan();
    void loadQtPlugin(const QString& pluginId);
    void unloadQtPlugin(const QString& pluginId);
    void reloadPlugin(const QString& pluginId);

    // Hot-reload support
    void enableHotReload(bool enabled);
    bool isHotReloadEnabled() const { return m_hotReloadEnabled; }
    void setHotReloadInterval(int ms);
    void checkForChanges();

    bool isPluginLoaded(const QString& pluginId) const;
    PluginInfo getPluginInfo(const QString& pluginId) const;
    QVector<PluginInfo> getAvailablePlugins() const;
    QVector<PluginInfo> getLoadedPlugins() const;

    void enablePlugin(const QString& pluginId);
    void disablePlugin(const QString& pluginId);
    bool isPluginEnabled(const QString& pluginId) const;

    QJsonObject getPluginSettings(const QString& pluginId) const;
    void setPluginSettings(const QString& pluginId, const QJsonObject& settings);

    bool registerImporter(const QString& pluginId, const QStringList& extensions);
    bool registerExporter(const QString& pluginId, const QStringList& extensions);

    void saveLoadedList() const;
    void restoreLoadedList();

signals:
    void pluginLoaded(const QString& pluginId);
    void pluginUnloaded(const QString& pluginId);
    void pluginError(const QString& pluginId, const QString& error);
    void scanComplete(int count);
    void importerRegistered(const QString& pluginId, const QStringList& extensions);
    void exporterRegistered(const QString& pluginId, const QStringList& extensions);
    void pluginAboutToReload(const QString& pluginId);
    void pluginReloaded(const QString& pluginId);
    void pluginReloadFailed(const QString& pluginId, const QString& error);

private:
    PluginInfo readManifest(const QString& dllPath) const;
    void setupFileWatchers();
    void onFileChanged(const QString& path);

    QString m_pluginDir;
    bool m_hotReloadEnabled = false;
    int m_hotReloadInterval = 1000;
    QTimer* m_reloadTimer = nullptr;
    QMap<QString, QFileInfo> m_watchedFiles;

    struct LoadedEntry {
        PluginInfo info;
        QPluginLoader* loader = nullptr;
        PluginInterface* pluginInstance = nullptr;
    };

    QMap<QString, LoadedEntry> m_loaded;
    QMap<QString, PluginInfo> m_available;
    QMultiMap<QString, QString> m_importers;
    QMultiMap<QString, QString> m_exporters;
    QMap<QString, PluginBase*> m_simPlugins;
};

} // namespace ks
