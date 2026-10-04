#include "PythonBridge.h"
#include "../sys/SettingsManager.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QDir>

namespace ks {

static PythonBridge* s_pythonBridge = nullptr;
static PythonMacroManager* s_macroManager = nullptr;

static QString detectPython()
{
    QStringList candidates = {"python3", "python"};
    for (const auto& cmd : candidates) {
        QProcess proc;
        proc.start(cmd, {"--version"});
        if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
            return cmd;
        }
    }
    return {};
}

static QString runPython(const QString& pythonCmd, const QString& script,
                         const QStringList& extraArgs = {})
{
    QProcess proc;
    proc.start(pythonCmd, extraArgs);
    if (!proc.waitForStarted(3000))
        return {};
    proc.write(script.toUtf8());
    proc.closeWriteChannel();
    if (!proc.waitForFinished(10000))
        return {};
    return proc.readAllStandardOutput() + proc.readAllStandardError();
}

static QString runPersistentPython(QProcess* proc, const QString& pythonCmd, const QString& script)
{
    if (!proc || proc->state() != QProcess::Running) {
        proc->start(pythonCmd, {"-u", "-c", R"(
import sys, json
def _ks_eval(code):
    try:
        exec(code)
        sys.stdout.write("__KS_OK__\n")
    except Exception as e:
        sys.stdout.write("__KS_ERR__:" + str(e) + "\n")
    sys.stdout.flush()
while True:
    line = sys.stdin.readline()
    if not line: break
    _ks_eval(line)
)"});
        if (!proc->waitForStarted(5000)) return {};
    }

    proc->write((script + "\n").toUtf8());
    proc->waitForBytesWritten(2000);

    QByteArray output;
    while (!proc->waitForReadyRead(500)) {
        if (proc->state() != QProcess::Running) break;
    }
    while (proc->bytesAvailable() > 0 || proc->waitForReadyRead(200)) {
        output += proc->readAllStandardOutput();
        if (output.contains("__KS_OK__") || output.contains("__KS_ERR__")) break;
    }

    QString result = QString::fromUtf8(output);
    result.remove(QRegularExpression("__KS_OK__|__KS_ERR__:?"));
    result.replace(QRegularExpression("__KS_ERR__:"), "Error: ");
    return result.trimmed();
}

PythonBridge::PythonBridge(QObject* parent)
    : QObject(parent)
    , m_pythonAvailable(false)
    , m_process(new QProcess(this))
{
    m_searchPaths.append(".");
    m_searchPaths.append("./scripts");
    QString python = detectPython();
    if (!python.isEmpty()) {
        m_pythonAvailable = true;
        m_pythonPath = python;
        qDebug() << "[PythonBridge] Python detected:" << python;
    } else {
        qDebug() << "[PythonBridge] Python not found";
    }
}

PythonBridge::~PythonBridge() {
    if (m_process && m_process->state() == QProcess::Running) {
        m_process->write("exit()\n");
        m_process->waitForFinished(3000);
    }
    s_pythonBridge = nullptr;
}

PythonBridge* PythonBridge::instance() {
    if (!s_pythonBridge) {
        s_pythonBridge = new PythonBridge();
    }
    return s_pythonBridge;
}

bool PythonBridge::isAvailable() {
    return m_pythonAvailable;
}

QString PythonBridge::getVersion() {
    if (!m_pythonAvailable)
        return "Python not available";
    QProcess proc;
    proc.start(m_pythonPath, {"--version"});
    if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
        return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    }
    return "Python not available";
}

QString PythonBridge::getPythonPath() {
    return m_pythonPath;
}

QVariant PythonBridge::evaluate(const QString& script) {
    QVariantMap result;
    result["success"] = false;
    result["result"] = QVariant();
    result["error"] = QString();

    if (script.trimmed().isEmpty()) {
        result["success"] = true;
        return result;
    }

    if (!m_pythonAvailable) {
        result["error"] = "Python not available";
        emit errorOccurred("Python not available");
        return result;
    }

    QString output = runPersistentPython(m_process, m_pythonPath, script);
    if (output.startsWith("Error:")) {
        result["error"] = output;
        emit errorOccurred(output);
    } else {
        result["success"] = true;
        result["result"] = output.trimmed();
        if (!output.trimmed().isEmpty())
            emit outputGenerated(output.trimmed());
    }
    return result;
}

bool PythonBridge::executeFile(const QString& path) {
    if (path.isEmpty()) return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred("Cannot open file: " + path);
        return false;
    }
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    QVariant evalResult = evaluate(content);
    return evalResult.toMap()["success"].toBool();
}

bool PythonBridge::reloadModules() {
    if (!m_pythonAvailable) return false;
    qDebug() << "[PythonBridge] Reload modules";
    emit moduleLoaded("ksmodeler");
    return true;
}

void PythonBridge::setVariable(const QString& name, const QVariant& value) {
    m_variables[name] = value;
    emit variableChanged(name, value);
    qDebug() << "[PythonBridge] Set variable:" << name;
}

QVariant PythonBridge::getVariable(const QString& name) {
    return m_variables.value(name, QVariant());
}

void PythonBridge::clearVariables() {
    m_variables.clear();
    qDebug() << "[PythonBridge] Variables cleared";
}

QStringList PythonBridge::getAutoComplete(const QString& prefix) {
    QStringList completions;

    QStringList builtins = {
        "mesh", "vertices", "faces", "normals", "uvs",
        "translate", "rotate", "scale", "extrude", "subdivide",
        "create_box", "create_sphere", "create_cylinder",
        "select_vert", "select_edge", "select_face",
        "delete", "duplicate", "mirror",
        "set_material", "set_uv", "set_color",
        "get_selection", "set_selection",
        "add_keyframe", "remove_keyframe", "get_keyframes",
        "scene", "nodes", "objects", "selected"
    };

    for (const QString& name : builtins) {
        if (name.startsWith(prefix, Qt::CaseInsensitive)) {
            completions.append(name);
        }
    }

    for (const QString& varName : m_variables.keys()) {
        if (varName.startsWith(prefix, Qt::CaseInsensitive)) {
            completions.append(varName);
        }
    }

    return completions;
}

QStringList PythonBridge::getModuleList() {
    return QStringList() << "ksmodeler" << "mesh" << "scene" << "animation";
}

QStringList PythonBridge::getFunctionList(const QString& module) {
    if (module == "ksmodeler") {
        return QStringList() << "create_primitive" << "modify_mesh" << "query_mesh" << "transform";
    } else if (module == "mesh") {
        return QStringList() << "create" << "edit" << "boolean" << "deform";
    } else if (module == "scene") {
        return QStringList() << "new" << "open" << "save" << "export";
    } else if (module == "animation") {
        return QStringList() << "create_keyframe" << "set_animation" << "play" << "stop";
    }
    return QStringList() << "unknown_module";
}

void PythonBridge::addSearchPath(const QString& path) {
    if (!m_searchPaths.contains(path)) {
        m_searchPaths.append(path);
    }
}

void PythonBridge::clearSearchPaths() {
    m_searchPaths.clear();
    m_searchPaths.append(".");
}

QStringList PythonBridge::getSearchPaths() {
    return m_searchPaths;
}

QString PythonBridge::getDocumentation(const QString& function) {
    QMap<QString, QString> docs = {
        {"mesh", "Access the current mesh data"},
        {"vertices", "Get mesh vertices as list"},
        {"faces", "Get mesh faces as list"},
        {"translate", "Translate selection by vector: translate(x, y, z)"},
        {"rotate", "Rotate selection by euler: rotate(x, y, z)"},
        {"scale", "Scale selection by factor: scale(sx, sy, sz)"},
        {"extrude", "Extrude selection: extrude(distance)"},
        {"subdivide", "Subdivide mesh: subdivide(levels)"},
        {"select_vert", "Select vertices by index: select_vert([0, 1, 2])"},
        {"select_face", "Select faces by index: select_face([0, 1, 2])"},
        {"delete", "Delete selected geometry"},
        {"duplicate", "Duplicate selection"},
        {"mirror", "Mirror selection: mirror(axis='X')"},
        {"set_material", "Set material on selection"},
        {"add_keyframe", "Add animation keyframe: add_keyframe(frame, value)"}
    };

    return docs.value(function, "No documentation available");
}

QString PythonBridge::getSignature(const QString& function) {
    QMap<QString, QString> signatures = {
        {"translate", "translate(x: float, y: float, z: float)"},
        {"rotate", "rotate(x: float, y: float, z: float)"},
        {"scale", "scale(sx: float, sy: float, sz: float)"},
        {"extrude", "extrude(distance: float, individual: bool = false)"},
        {"subdivide", "subdivide(levels: int = 1)"},
        {"select_vert", "select_vert(indices: list)"},
        {"select_face", "select_face(indices: list)"},
        {"add_keyframe", "add_keyframe(frame: int, value: float)"},
        {"create_box", "create_box(width: float, height: float, depth: float)"},
        {"create_sphere", "create_sphere(radius: float, segments: int = 32)"}
    };

    return signatures.value(function, "function()");
}

void PythonBridge::setMeshData(const QVariant& meshData) {
    m_meshData = meshData;
    qDebug() << "[PythonBridge] Mesh data set";
}

QVariant PythonBridge::getMeshData() {
    return m_meshData;
}

void PythonBridge::setSceneNodes(const QVariant& nodes) {
    m_sceneNodes = nodes;
    qDebug() << "[PythonBridge] Scene nodes set";
}

QVariant PythonBridge::getSceneNodes() {
    return m_sceneNodes;
}

void PythonBridge::setSetting(const QString& key, const QVariant& value) {
    globalSettings()->setValue(key, value);
}

QVariant PythonBridge::getSetting(const QString& key) {
    return globalSettings()->value(key);
}

QString PythonBridge::runMacro(const QString& macroName, const QVariantList& args) {
    if (PythonMacroManager::instance()) {
        PythonMacroManager::instance()->runMacro(macroName, args);
        return "Macro executed: " + macroName;
    }
    return "Macro manager not available";
}

QStringList PythonBridge::getMacroList() {
    if (PythonMacroManager::instance()) {
        return PythonMacroManager::instance()->getMacroNames();
    }
    return QStringList();
}

QVariant ScriptMacro::run(const QVariantList& args) {
    // Prepare script with argument insertion
    QString script = m_script;
    if (!args.isEmpty() && !m_argNames.isEmpty()) {
        // Replace named placeholders with actual args (e.g., $argName -> value)
        for (int i = 0; i < qMin(args.size(), m_argNames.size()); ++i) {
            script.replace("$" + m_argNames[i], args[i].toString());
        }
    } else if (!args.isEmpty()) {
        // Positional args: replace $1, $2, etc.
        for (int i = 0; i < args.size(); ++i) {
            script.replace("$" + QString::number(i + 1), args[i].toString());
        }
    }
    return PythonBridge::instance()->evaluate(script);
}

PythonMacroManager::PythonMacroManager(QObject* parent)
    : QObject(parent)
{
    registerDefaultMacros();
}

PythonMacroManager::~PythonMacroManager() {
    s_macroManager = nullptr;
}

PythonMacroManager* PythonMacroManager::instance() {
    if (!s_macroManager) {
        s_macroManager = new PythonMacroManager();
    }
    return s_macroManager;
}

void PythonMacroManager::registerMacro(ScriptMacro* macro) {
    if (!macro) return;
    m_macros[macro->getName()] = macro;
    emit macroRegistered(macro->getName());
    qDebug() << "[PythonMacroManager] Registered:" << macro->getName();
}

void PythonMacroManager::unregisterMacro(const QString& name) {
    if (m_macros.contains(name)) {
        delete m_macros[name];
        m_macros.remove(name);
        emit macroUnregistered(name);
    }
}

ScriptMacro* PythonMacroManager::getMacro(const QString& name) {
    return m_macros.value(name);
}

QStringList PythonMacroManager::getMacroNames() {
    return m_macros.keys();
}

QStringList PythonMacroManager::getCategories() {
    return m_categories.keys();
}

void PythonMacroManager::importMacros(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonArray macros = doc.array();
    for (const QJsonValue& val : macros) {
        QJsonObject obj = val.toObject();
        QString name = obj["name"].toString();
        QString script = obj["script"].toString();
        QString category = obj["category"].toString("Default");

        if (!name.isEmpty() && !script.isEmpty()) {
            registerMacro(new ScriptMacro(name, script, category));
        }
    }
}

void PythonMacroManager::exportMacros(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;

    QJsonArray macros;
    for (ScriptMacro* macro : m_macros.values()) {
        QJsonObject obj;
        obj["name"] = macro->getName();
        obj["script"] = macro->getScript();
        obj["category"] = macro->getCategory();
        macros.append(obj);
    }

    QJsonDocument doc(macros);
    file.write(doc.toJson());
    file.close();
}

void PythonMacroManager::runMacro(const QString& name, const QVariantList& args) {
    ScriptMacro* macro = getMacro(name);
    if (macro) {
        macro->run(args);
    }
}

void PythonMacroManager::registerDefaultMacros() {
    registerMacro(new ScriptMacro("Reset View",
        "translate(0, 0, 0)\nrotate(0, 0, 0)\nscale(1, 1, 1)",
        "View"));

    registerMacro(new ScriptMacro("Center Origin",
        "x = vertices.x\ny = vertices.y\nz = vertices.z\ntranslate(-x, -y, -z)",
        "Mesh"));

    registerMacro(new ScriptMacro("Wireframe Toggle",
        "toggle_wireframe()",
        "Display"));

    registerMacro(new ScriptMacro("Select All",
        "select_vert(list(range(vertices.length)))",
        "Selection"));

    registerMacro(new ScriptMacro("Delete Selection",
        "delete()",
        "Edit"));

    registerMacro(new ScriptMacro("Duplicate Selection",
        "duplicate()",
        "Edit"));

    registerMacro(new ScriptMacro("Flip Normals",
        "normals = normals.map(lambda n: n * -1)",
        "Mesh"));

    registerMacro(new ScriptMacro("Triangulate",
        "faces = faces.map(lambda f: triangulate(f))",
        "Mesh"));
}

}