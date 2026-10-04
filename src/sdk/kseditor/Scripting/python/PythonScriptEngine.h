#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QVariant>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QThread>
#include <QCoreApplication>

namespace ks {

class PythonScriptEngine : public QObject
{
    Q_OBJECT

public:
    explicit PythonScriptEngine(QObject* parent = nullptr);
    ~PythonScriptEngine();

    struct ScriptInfo {
        QString name;
        QString path;
        QString description;
        QString author;
        QString version;
        QStringList tags;
        bool isEnabled = true;
    };

    struct ScriptResult {
        bool success = false;
        QString output;
        QString error;
        int exitCode = 0;
    };

    bool initialize();
    void shutdown();

    bool loadScript(const QString& path);
    void unloadScript(const QString& scriptId);
    void unloadAllScripts();

    QVector<ScriptInfo> getLoadedScripts() const;
    ScriptInfo getScriptInfo(const QString& scriptId) const;

    ScriptResult runScript(const QString& scriptId, const QJsonObject& args = QJsonObject());
    ScriptResult runScriptFile(const QString& path, const QJsonObject& args = QJsonObject());

    bool registerFunction(const QString& name, const QVariant& callable);
    void unregisterFunction(const QString& name);

    void setScriptsDirectory(const QString& dir);
    QString scriptsDirectory() const { return m_scriptsDir; }

    QVector<ScriptInfo> discoverScripts();

    void setScriptEnabled(const QString& scriptId, bool enabled);
    bool isScriptEnabled(const QString& scriptId) const;

signals:
    void scriptLoaded(const QString& scriptId, const ScriptInfo& info);
    void scriptUnloaded(const QString& scriptId);
    void scriptError(const QString& scriptId, const QString& error);
    void scriptOutput(const QString& scriptId, const QString& output);
    void functionRegistered(const QString& name);
    void functionUnregistered(const QString& name);

private:
    struct LoadedScript {
        ScriptInfo info;
        QVariant scriptObject;
    };

    QString m_scriptsDir;
    QMap<QString, LoadedScript> m_scripts;
    QMap<QString, QVariant> m_registeredFunctions;
};

class ScriptAPI : public QObject
{
    Q_OBJECT

public:
    explicit ScriptAPI(QObject* parent = nullptr);
    ~ScriptAPI();

    static ScriptAPI* instance();
    static void setInstance(ScriptAPI* api);

    Q_INVOKABLE QString readFile(const QString& path);
    Q_INVOKABLE bool writeFile(const QString& path, const QString& content);
    Q_INVOKABLE bool fileExists(const QString& path);
    Q_INVOKABLE bool deleteFile(const QString& path);

    Q_INVOKABLE QStringList listDirectory(const QString& path);
    Q_INVOKABLE bool createDirectory(const QString& path);
    Q_INVOKABLE bool deleteDirectory(const QString& path);

    Q_INVOKABLE QJsonObject getProjectInfo();
    Q_INVOKABLE bool saveProject(const QString& path);
    Q_INVOKABLE bool loadProject(const QString& path);

    Q_INVOKABLE void log(const QString& message);
    Q_INVOKABLE void logError(const QString& message);
    Q_INVOKABLE void logWarning(const QString& message);

    Q_INVOKABLE QJsonObject getSettings();
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);

    Q_INVOKABLE QJsonObject getSelectedObjects();
    Q_INVOKABLE void selectObjects(const QStringList& objectIds);

    Q_INVOKABLE QString importAsset(const QString& path);
    Q_INVOKABLE bool exportAsset(const QString& objectId, const QString& path);

    Q_INVOKABLE QString runCommand(const QString& command, const QJsonObject& args = QJsonObject());

    Q_INVOKABLE void showMessage(const QString& title, const QString& message);
    Q_INVOKABLE bool showConfirmation(const QString& title, const QString& message);

    Q_INVOKABLE QJsonObject getAudioDeviceInfo();
    Q_INVOKABLE bool playAudio(const QString& path);
    Q_INVOKABLE bool stopAudio();
    Q_INVOKABLE bool recordAudio(int durationMs);

signals:
    void fileChanged(const QString& path);
    void projectLoaded(const QString& path);
    void projectSaved(const QString& path);
    void selectionChanged(const QStringList& objectIds);
    void settingChanged(const QString& key, const QVariant& value);
    void assetImported(const QString& path);
    void assetExported(const QString& objectId, const QString& path);
    void messageShown(const QString& title, const QString& message);
    void confirmationRequested(const QString& title, const QString& message);
    void audioPlayed(const QString& path);
    void audioStopped();
    void audioRecordingStarted(int durationMs);
    void commandExecuted(const QString& command, const QJsonObject& args);
    void functionRegistered(const QString& name);
    void functionUnregistered(const QString& name);

private:
    static ScriptAPI* s_instance;
    QString m_currentProjectPath;
    QMap<QString, QVariant> m_settings;
    QStringList m_selectedObjects;
    QStringList m_commandHistory;
};

class ScriptManager : public QObject
{
    Q_OBJECT

public:
    explicit ScriptManager(QObject* parent = nullptr);
    ~ScriptManager();

    void setScriptEngine(PythonScriptEngine* engine);

    void addToMenu(const QString& menuPath, const QString& actionName,
                  const QString& scriptId, const QString& shortcut = QString());

    void addToToolbar(const QString& toolbarName, const QString& actionName,
                     const QString& scriptId, const QString& iconPath = QString());

    void addShortcut(const QString& keySequence, const QString& scriptId);

    void registerMenuExtension(const QString& menuPath, const QString& extensionId,
                              const QString& scriptId);

    struct MenuAction {
        QString path;
        QString name;
        QString scriptId;
        QString shortcut;
    };

    struct MenuExtension {
        QString menuPath;
        QString extensionId;
        QString scriptId;
    };

    QVector<MenuAction> getMenuActions() const { return m_menuActions; }
    QVector<MenuAction> getToolbarActions() const { return m_toolbarActions; }
    QMap<QString, QString> getShortcuts() const { return m_shortcuts; }

    void saveConfiguration(const QString& path);
    void loadConfiguration(const QString& path);

signals:
    void menuActionTriggered(const QString& scriptId);
    void toolbarActionTriggered(const QString& scriptId);
    void shortcutTriggered(const QString& scriptId);
    void menuExtensionRegistered(const QString& menuPath, const QString& extensionId);

private:
    PythonScriptEngine* m_engine = nullptr;
    QVector<MenuAction> m_menuActions;
    QVector<MenuAction> m_toolbarActions;
    QMap<QString, QString> m_shortcuts;
    QVector<MenuExtension> m_menuExtensions;
};

class ScriptRecorder : public QObject
{
    Q_OBJECT

public:
    explicit ScriptRecorder(QObject* parent = nullptr);
    ~ScriptRecorder();

    void startRecording();
    void stopRecording();
    void pauseRecording();

    void recordAction(const QString& actionType, const QJsonObject& params);
    void recordSelection(const QStringList& selectedObjects);
    void recordEdit(const QString& editType, const QJsonObject& before, const QJsonObject& after);

    QJsonArray getRecordedActions() const { return m_recordedActions; }

    void saveRecording(const QString& path);
    void loadRecording(const QString& path);

    QString generateScript();

signals:
    void recordingStarted();
    void recordingStopped();
    void recordingPaused();
    void actionRecorded(const QString& actionType);

private:
    bool m_recording = false;
    bool m_paused = false;
    QJsonArray m_recordedActions;
};

class MacroRunner : public QObject
{
    Q_OBJECT

public:
    explicit MacroRunner(QObject* parent = nullptr);
    ~MacroRunner();

    struct Macro {
        QString id;
        QString name;
        QString description;
        QJsonArray actions;
        int repeatCount = 1;
        int delayBetweenActions = 0;
    };

    void addMacro(const Macro& macro);
    void removeMacro(const QString& macroId);
    Macro getMacro(const QString& macroId) const;

    QVector<Macro> getAllMacros() const { return m_macros; }

    void runMacro(const QString& macroId);
    void stopMacro();

    bool isRunning() const { return m_running; }

signals:
    void macroStarted(const QString& macroId);
    void macroProgress(float percent);
    void macroCompleted(const QString& macroId);
    void macroError(const QString& macroId, const QString& error);

private:
    QVector<Macro> m_macros;
    bool m_running = false;
    int m_currentAction = 0;
};

} // namespace ks