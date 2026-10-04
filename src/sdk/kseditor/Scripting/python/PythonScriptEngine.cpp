#include "PythonScriptEngine.h"
#include "tools/PythonBridge.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace ks {

ScriptAPI* ScriptAPI::s_instance = nullptr;

ScriptAPI::ScriptAPI(QObject* parent)
    : QObject(parent)
{
}

ScriptAPI::~ScriptAPI()
{
}

ScriptAPI* ScriptAPI::instance()
{
    return s_instance;
}

void ScriptAPI::setInstance(ScriptAPI* api)
{
    s_instance = api;
}

QString ScriptAPI::readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QString content = file.readAll();
    file.close();
    return content;
}

bool ScriptAPI::writeFile(const QString& path, const QString& content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(content.toUtf8());
    file.close();
    return true;
}

bool ScriptAPI::fileExists(const QString& path)
{
    return QFile::exists(path);
}

bool ScriptAPI::deleteFile(const QString& path)
{
    return QFile::remove(path);
}

QStringList ScriptAPI::listDirectory(const QString& path)
{
    QDir dir(path);
    return dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
}

bool ScriptAPI::createDirectory(const QString& path)
{
    QDir dir;
    return dir.mkpath(path);
}

bool ScriptAPI::deleteDirectory(const QString& path)
{
    QDir dir(path);
    return dir.removeRecursively();
}

QJsonObject ScriptAPI::getProjectInfo()
{
    QJsonObject info;
    info["path"] = m_currentProjectPath;
    info["name"] = QFileInfo(m_currentProjectPath).baseName();
    return info;
}

bool ScriptAPI::saveProject(const QString& path)
{
    m_currentProjectPath = path;
    emit projectSaved(path);
    return true;
}

bool ScriptAPI::loadProject(const QString& path)
{
    m_currentProjectPath = path;
    emit projectLoaded(path);
    return true;
}

void ScriptAPI::log(const QString& message)
{
    qDebug() << "[Script]" << message;
}

void ScriptAPI::logError(const QString& message)
{
    qWarning() << "[Script Error]" << message;
}

void ScriptAPI::logWarning(const QString& message)
{
    qWarning() << "[Script Warning]" << message;
}

QJsonObject ScriptAPI::getSettings()
{
    QJsonObject obj;
    for (auto it = m_settings.constBegin(); it != m_settings.constEnd(); ++it) {
        obj[it.key()] = QJsonValue::fromVariant(it.value());
    }
    return obj;
}

void ScriptAPI::setSetting(const QString& key, const QVariant& value)
{
    m_settings[key] = value;
    emit settingChanged(key, value);
}

QJsonObject ScriptAPI::getSelectedObjects()
{
    QJsonObject result;
    QJsonArray arr;
    for (const auto& id : m_selectedObjects) {
        arr.append(id);
    }
    result["selected"] = arr;
    return result;
}

void ScriptAPI::selectObjects(const QStringList& objectIds)
{
    m_selectedObjects = objectIds;
    emit selectionChanged(objectIds);
}

QString ScriptAPI::importAsset(const QString& path)
{
    if (!QFile::exists(path)) return QString();

    QFileInfo fi(path);
    QString assetDir = QCoreApplication::applicationDirPath() + "/assets/";
    QDir().mkpath(assetDir);
    QString destPath = assetDir + fi.fileName();

    if (QFile::copy(path, destPath)) {
        emit assetImported(destPath);
        return destPath;
    }
    return QString();
}

bool ScriptAPI::exportAsset(const QString& objectId, const QString& path)
{
    QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists()) dir.mkpath(".");

    emit assetExported(objectId, path);
    return true;
}

QString ScriptAPI::runCommand(const QString& command, const QJsonObject& args)
{
    m_commandHistory.append(command);
    emit commandExecuted(command, args);

    if (command == "newProject") {
        return "Project created: " + args.value("name").toString();
    } else if (command == "import") {
        return "Imported: " + args.value("path").toString();
    } else if (command == "export") {
        return "Exported: " + args.value("path").toString();
    }

    return "Command executed: " + command;
}

void ScriptAPI::showMessage(const QString& title, const QString& message)
{
    qDebug() << title << ":" << message;
    emit messageShown(title, message);
}

bool ScriptAPI::showConfirmation(const QString& title, const QString& message)
{
    emit confirmationRequested(title, message);
    return true;
}

bool ScriptAPI::playAudio(const QString& path)
{
    if (!QFile::exists(path)) return false;
    emit audioPlayed(path);
    return true;
}

bool ScriptAPI::stopAudio()
{
    emit audioStopped();
    return true;
}

bool ScriptAPI::recordAudio(int durationMs)
{
    emit audioRecordingStarted(durationMs);
    return true;
}

QJsonObject ScriptAPI::getAudioDeviceInfo()
{
    QJsonObject info;
    QJsonArray devices;
    QJsonObject defaultDevice;
    defaultDevice["name"] = "Default";
    defaultDevice["isDefault"] = true;
    devices.append(defaultDevice);
    info["devices"] = devices;
    return info;
}

PythonScriptEngine::PythonScriptEngine(QObject* parent)
    : QObject(parent)
{
    m_scriptsDir = QDir::homePath() + "/.kseditor/scripts";
    QDir().mkpath(m_scriptsDir);
}

PythonScriptEngine::~PythonScriptEngine()
{
    shutdown();
}

bool PythonScriptEngine::initialize()
{
    return ks::PythonBridge::instance()->isAvailable();
}

void PythonScriptEngine::shutdown()
{
    unloadAllScripts();
}

bool PythonScriptEngine::loadScript(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit scriptError(path, "Cannot open file");
        return false;
    }

    ScriptInfo info;
    info.path = path;
    info.name = QFileInfo(path).baseName();

    QFileInfo fi(path);
    QString dirPath = fi.absolutePath();
    QString metaPath = dirPath + "/meta.json";
    if (QFile::exists(metaPath)) {
        QByteArray metaData;
        ScriptAPI* api = ScriptAPI::instance();
        if (api) {
            metaData = api->readFile(metaPath).toUtf8();
        } else {
            QFile mf(metaPath);
            if (mf.open(QIODevice::ReadOnly))
                metaData = mf.readAll();
        }
        QJsonDocument doc = QJsonDocument::fromJson(metaData);
        QJsonObject meta = doc.object();
        info.description = meta["description"].toString();
        info.author = meta["author"].toString();
        info.version = meta["version"].toString();

        QJsonArray tagsArray = meta["tags"].toArray();
        for (const QJsonValue& tag : tagsArray) {
            info.tags.append(tag.toString());
        }
    }

    QString scriptId = info.name;
    m_scripts[scriptId] = { info, QVariant() };

    emit scriptLoaded(scriptId, info);
    return true;
}

void PythonScriptEngine::unloadScript(const QString& scriptId)
{
    if (m_scripts.contains(scriptId)) {
        m_scripts.remove(scriptId);
        emit scriptUnloaded(scriptId);
    }
}

void PythonScriptEngine::unloadAllScripts()
{
    QStringList ids = m_scripts.keys();
    for (const QString& id : ids) {
        unloadScript(id);
    }
}

QVector<PythonScriptEngine::ScriptInfo> PythonScriptEngine::getLoadedScripts() const
{
    QVector<ScriptInfo> infos;
    for (const auto& script : m_scripts.values()) {
        infos.append(script.info);
    }
    return infos;
}

PythonScriptEngine::ScriptInfo PythonScriptEngine::getScriptInfo(const QString& scriptId) const
{
    if (m_scripts.contains(scriptId)) {
        return m_scripts[scriptId].info;
    }
    return ScriptInfo();
}

PythonScriptEngine::ScriptResult PythonScriptEngine::runScript(const QString& scriptId, const QJsonObject& args)
{
    ScriptResult result;

    if (!m_scripts.contains(scriptId)) {
        result.error = "Script not loaded";
        return result;
    }

    const ScriptInfo& info = m_scripts[scriptId].info;
    if (!info.isEnabled) {
        result.error = "Script is disabled";
        return result;
    }

    QFile file(info.path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = "Cannot read script file";
        emit scriptError(scriptId, result.error);
        return result;
    }
    QString code = QString::fromUtf8(file.readAll());
    file.close();

    auto bridge = PythonBridge::instance();
    QVariant bridgeResult = bridge->evaluate(code);
    QVariantMap map = bridgeResult.toMap();

    result.success = map["success"].toBool();
    result.output = map["result"].toString();
    result.error = map["error"].toString();

    if (!result.output.isEmpty())
        emit scriptOutput(scriptId, result.output);
    if (!result.error.isEmpty())
        emit scriptError(scriptId, result.error);

    return result;
}

PythonScriptEngine::ScriptResult PythonScriptEngine::runScriptFile(const QString& path, const QJsonObject& args)
{
    ScriptResult result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = "Cannot open file";
        return result;
    }
    QString code = QString::fromUtf8(file.readAll());
    file.close();

    auto bridge = PythonBridge::instance();
    QVariant bridgeResult = bridge->evaluate(code);
    QVariantMap map = bridgeResult.toMap();

    result.success = map["success"].toBool();
    result.output = map["result"].toString();
    result.error = map["error"].toString();

    if (!result.output.isEmpty())
        emit scriptOutput(path, result.output);

    return result;
}

bool PythonScriptEngine::registerFunction(const QString& name, const QVariant& callable)
{
    m_registeredFunctions[name] = callable;
    emit functionRegistered(name);
    return true;
}

void PythonScriptEngine::unregisterFunction(const QString& name)
{
    m_registeredFunctions.remove(name);
    emit functionUnregistered(name);
}

void PythonScriptEngine::setScriptsDirectory(const QString& dir)
{
    m_scriptsDir = dir;
    QDir().mkpath(m_scriptsDir);
}

QVector<PythonScriptEngine::ScriptInfo> PythonScriptEngine::discoverScripts()
{
    QVector<ScriptInfo> discovered;

    QDir scriptsDir(m_scriptsDir);
    QFileInfoList entries = scriptsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo& entry : entries) {
        ScriptInfo info;
        info.path = entry.absoluteFilePath();
        info.name = entry.baseName();

        QString metaPath = entry.absoluteFilePath() + "/meta.json";
        if (QFile::exists(metaPath)) {
            QByteArray metaData;
            ScriptAPI* api = ScriptAPI::instance();
            if (api) {
                metaData = api->readFile(metaPath).toUtf8();
            } else {
                QFile mf(metaPath);
                if (mf.open(QIODevice::ReadOnly))
                    metaData = mf.readAll();
            }
            QJsonDocument doc = QJsonDocument::fromJson(metaData);
            QJsonObject meta = doc.object();
            info.description = meta["description"].toString();
            info.author = meta["author"].toString();
            info.version = meta["version"].toString();
        }

        discovered.append(info);
    }

    return discovered;
}

void PythonScriptEngine::setScriptEnabled(const QString& scriptId, bool enabled)
{
    if (m_scripts.contains(scriptId)) {
        m_scripts[scriptId].info.isEnabled = enabled;
    }
}

bool PythonScriptEngine::isScriptEnabled(const QString& scriptId) const
{
    if (m_scripts.contains(scriptId)) {
        return m_scripts[scriptId].info.isEnabled;
    }
    return false;
}

ScriptManager::ScriptManager(QObject* parent)
    : QObject(parent)
{
}

ScriptManager::~ScriptManager()
{
}

void ScriptManager::setScriptEngine(PythonScriptEngine* engine)
{
    m_engine = engine;
}

void ScriptManager::addToMenu(const QString& menuPath, const QString& actionName,
                              const QString& scriptId, const QString& shortcut)
{
    MenuAction action;
    action.path = menuPath;
    action.name = actionName;
    action.scriptId = scriptId;
    action.shortcut = shortcut;
    m_menuActions.append(action);
}

void ScriptManager::addToToolbar(const QString& toolbarName, const QString& actionName,
                               const QString& scriptId, const QString& iconPath)
{
    MenuAction action;
    action.path = toolbarName;
    action.name = actionName;
    action.scriptId = scriptId;
    m_toolbarActions.append(action);
}

void ScriptManager::addShortcut(const QString& keySequence, const QString& scriptId)
{
    m_shortcuts[keySequence] = scriptId;
}

void ScriptManager::registerMenuExtension(const QString& menuPath, const QString& extensionId,
                                         const QString& scriptId)
{
    MenuExtension ext;
    ext.menuPath = menuPath;
    ext.extensionId = extensionId;
    ext.scriptId = scriptId;
    m_menuExtensions.append(ext);
    emit menuExtensionRegistered(menuPath, extensionId);
}

void ScriptManager::saveConfiguration(const QString& path)
{
    QJsonObject config;

    QJsonArray menus;
    for (const auto& action : m_menuActions) {
        QJsonObject obj;
        obj["path"] = action.path;
        obj["name"] = action.name;
        obj["scriptId"] = action.scriptId;
        obj["shortcut"] = action.shortcut;
        menus.append(obj);
    }
    config["menuActions"] = menus;

    QJsonObject shortcuts;
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        shortcuts[it.key()] = it.value();
    }
    config["shortcuts"] = shortcuts;

    QJsonDocument doc(config);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}

void ScriptManager::loadConfiguration(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject config = doc.object();

    QJsonArray menus = config["menuActions"].toArray();
    for (const QJsonValue& val : menus) {
        QJsonObject obj = val.toObject();
        MenuAction action;
        action.path = obj["path"].toString();
        action.name = obj["name"].toString();
        action.scriptId = obj["scriptId"].toString();
        action.shortcut = obj["shortcut"].toString();
        m_menuActions.append(action);
    }

    QJsonObject shortcuts = config["shortcuts"].toObject();
    for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it) {
        m_shortcuts[it.key()] = it.value().toString();
    }
}

ScriptRecorder::ScriptRecorder(QObject* parent)
    : QObject(parent)
{
}

ScriptRecorder::~ScriptRecorder()
{
}

void ScriptRecorder::startRecording()
{
    m_recording = true;
    m_paused = false;
    m_recordedActions = QJsonArray();
    emit recordingStarted();
}

void ScriptRecorder::stopRecording()
{
    m_recording = false;
    m_paused = false;
    emit recordingStopped();
}

void ScriptRecorder::pauseRecording()
{
    m_paused = true;
    emit recordingPaused();
}

void ScriptRecorder::recordAction(const QString& actionType, const QJsonObject& params)
{
    if (!m_recording || m_paused) return;

    QJsonObject action;
    action["type"] = actionType;
    action["params"] = params;
    action["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    m_recordedActions.append(action);
    emit actionRecorded(actionType);
}

void ScriptRecorder::recordSelection(const QStringList& selectedObjects)
{
    QJsonObject params;
    params["objects"] = QJsonArray::fromStringList(selectedObjects);
    recordAction("selection", params);
}

void ScriptRecorder::recordEdit(const QString& editType, const QJsonObject& before, const QJsonObject& after)
{
    QJsonObject params;
    params["editType"] = editType;
    params["before"] = before;
    params["after"] = after;
    recordAction("edit", params);
}

void ScriptRecorder::saveRecording(const QString& path)
{
    QJsonDocument doc(m_recordedActions);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}

void ScriptRecorder::loadRecording(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    m_recordedActions = doc.array();
    file.close();
}

QString ScriptRecorder::generateScript()
{
    QString script = "# Auto-generated script from recorded actions\n\n";
    script += "from kseditor import api\n\n";

    script += "def run_macro():\n";

    for (const QJsonValue& val : m_recordedActions) {
        QJsonObject action = val.toObject();
        QString actionType = action["type"].toString();

        if (actionType == "selection") {
            QJsonArray objects = action["params"].toObject()["objects"].toArray();
            QStringList ids;
            for (const QJsonValue& obj : objects) {
                ids.append(obj.toString());
            }
            script += "    api.selectObjects(" + ids.join(", ") + ")\n";
        } else if (actionType == "edit") {
            QString editType = action["params"].toObject()["editType"].toString();
            script += "    # " + editType + " action\n";
        }
    }

    script += "\nif __name__ == '__main__':\n    run_macro()\n";

    return script;
}

MacroRunner::MacroRunner(QObject* parent)
    : QObject(parent)
{
}

MacroRunner::~MacroRunner()
{
}

void MacroRunner::addMacro(const Macro& macro)
{
    m_macros.append(macro);
}

void MacroRunner::removeMacro(const QString& macroId)
{
    for (int i = 0; i < m_macros.size(); ++i) {
        if (m_macros[i].id == macroId) {
            m_macros.removeAt(i);
            break;
        }
    }
}

MacroRunner::Macro MacroRunner::getMacro(const QString& macroId) const
{
    for (const auto& macro : m_macros) {
        if (macro.id == macroId) {
            return macro;
        }
    }
    return Macro();
}

void MacroRunner::runMacro(const QString& macroId)
{
    Macro macro = getMacro(macroId);
    if (macro.id.isEmpty()) {
        emit macroError(macroId, "Macro not found");
        return;
    }

    m_running = true;
    m_currentAction = 0;
    emit macroStarted(macroId);

    for (int repeat = 0; repeat < macro.repeatCount; ++repeat) {
        for (int i = 0; i < macro.actions.size(); ++i) {
            if (!m_running) break;

            m_currentAction = i;
            float progress = ((repeat * macro.actions.size() + i) * 100.0f) /
                           (macro.repeatCount * macro.actions.size());
            emit macroProgress(progress);

            if (macro.delayBetweenActions > 0) {
                QThread::msleep(macro.delayBetweenActions);
            }
        }
    }

    m_running = false;
    emit macroCompleted(macroId);
}

void MacroRunner::stopMacro()
{
    m_running = false;
}

} // namespace ks