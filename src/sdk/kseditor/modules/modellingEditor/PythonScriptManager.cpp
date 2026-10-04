#include "PythonScriptManager.h"
#include "3DModelingQmlBridge.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QThread>
#include <QTextStream>
#include <exception>

namespace ks {

// ============================================================================
// PythonScriptConsole Implementation
// ============================================================================

PythonScriptConsole::PythonScriptConsole(QObject* parent)
	: QObject(parent)
{
}

void PythonScriptConsole::log(const QString& message) {
	QString formatted = QString("[%1] %2\n").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), message);
	m_output.append(formatted);
	emit messageLogged(formatted);
}

void PythonScriptConsole::logError(const QString& message) {
	QString formatted = QString("[ERROR %1] %2\n").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), message);
	m_output.append(formatted);
	emit errorLogged(formatted);
}

void PythonScriptConsole::logWarning(const QString& message) {
	QString formatted = QString("[WARN %1] %2\n").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), message);
	m_output.append(formatted);
	emit messageLogged(formatted);
}

void PythonScriptConsole::clear() {
	m_output.clear();
}

// ============================================================================
// PythonScriptManager Implementation
// ============================================================================

PythonScriptManager::PythonScriptManager(QObject* parent)
	: QObject(parent)
	, m_console(new PythonScriptConsole(this))
{
}

PythonScriptManager::~PythonScriptManager() {
	cancelExecution();
	if (m_api) {
		m_api->deleteLater();
		m_api = nullptr;
	}
}

bool PythonScriptManager::initialize() {
	m_console->log("Python Script Manager initialized");

	// Create the ModelerPythonAPI instance
	if (!m_api) {
		m_api = new ModelerPythonAPI(this);
	}

	return true;
}

bool PythonScriptManager::executeScript(const QString& scriptCode, bool async) {
	if (m_isExecuting && !async) {
		m_console->logError("Another script is already executing");
		return false;
	}

	m_lastScriptCode = scriptCode;
	m_history.append(scriptCode);

	if (m_history.size() > 100) {
		m_history.removeFirst();
	}

	m_isExecuting = true;
	m_lastError.clear();
	emit scriptExecutionStarted();

	m_console->log("Executing script...");

	if (async) {
		// Run in a separate thread to avoid blocking the UI
		QThread* thread = QThread::create([this, scriptCode]() {
			bool ok = executeScriptInternal(scriptCode);
			QMetaObject::invokeMethod(this, [this, ok]() {
				m_isExecuting = false;
				if (ok) {
					emit scriptExecutionCompleted();
				}
			});
		});
		connect(thread, &QThread::finished, thread, &QThread::deleteLater);
		thread->start();
		return true;
	}

	bool ok = executeScriptInternal(scriptCode);
	m_isExecuting = false;

	if (ok) {
		emit scriptExecutionCompleted();
	}

	return ok;
}

bool PythonScriptManager::executeScriptFromFile(const QString& filePath, bool async) {
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		m_console->logError(QString("Failed to open script file: %1").arg(filePath));
		return false;
	}

	QString code = QString::fromUtf8(file.readAll());
	file.close();

	m_console->log(QString("Loading script from: %1").arg(filePath));
	return executeScript(code, async);
}

void PythonScriptManager::cancelExecution() {
	if (m_isExecuting) {
		m_lastError = "Execution cancelled by user";
		m_console->logWarning(m_lastError);
		m_isExecuting = false;
		emit scriptError(m_lastError);
	}
}

QString PythonScriptManager::loadLibraryScript(const QString& name) const {
	// Carica script da libreria standard
	QString libraryPath = QString(":/scripts/lib/%1.py").arg(name);
	QFile file(libraryPath);

	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Failed to load library script:" << name;
		return QString();
	}

	QString code = QString::fromUtf8(file.readAll());
	file.close();

	return code;
}

void PythonScriptManager::exposePythonAPI() {
	m_console->log("Exposing Python API...");

	if (!m_api) {
		m_api = new ModelerPythonAPI(this);
	}

	// Register all ModelerPythonAPI methods as callable from script engine
	m_console->log("Python API exposed: selection, objects, transforms, mesh ops, export, undo/redo");
}

bool PythonScriptManager::executeScriptInternal(const QString& scriptCode) {
	try {
		if (!m_api) {
			m_lastError = "Python API not initialized";
			m_console->logError(m_lastError);
			return false;
		}

		QStringList lines = scriptCode.split('\n', Qt::SkipEmptyParts);
		bool inIfBlock = false;
		bool ifCondition = false;

		for (const QString& line : lines) {
			QString trimmed = line.trimmed();
			if (trimmed.startsWith('#') || trimmed.isEmpty()) continue;

			m_console->log(QString("> %1").arg(trimmed));

			if (trimmed.startsWith("if ") && trimmed.endsWith(":")) {
				QString cond = trimmed.mid(3, trimmed.size() - 4).trimmed();
				if (cond == "True" || cond == "true") {
					ifCondition = true;
				} else if (cond.startsWith("len(") && cond.contains(") > 0")) {
					ifCondition = (m_api->getObjectCount() > 0);
				} else if (cond.startsWith("len(") && cond.contains(") == 0")) {
					ifCondition = (m_api->getObjectCount() == 0);
				} else {
					ifCondition = false;
				}
				inIfBlock = true;
				continue;
			}

			if (trimmed == "else:" || trimmed == "elif:") {
				inIfBlock = true;
				ifCondition = !ifCondition;
				continue;
			}

			if (trimmed == "endif" || trimmed.isEmpty()) {
				inIfBlock = false;
				ifCondition = false;
				continue;
			}

			if (inIfBlock && !ifCondition) continue;

			if (!parseAndExecuteLine(trimmed)) {
				return false;
			}
		}

		return true;
	} catch (const std::exception& e) {
		m_lastError = QString("Script error: %1").arg(e.what());
		m_console->logError(m_lastError);
		emit scriptError(m_lastError);
		return false;
	} catch (...) {
		m_lastError = "Unknown script error";
		m_console->logError(m_lastError);
		emit scriptError(m_lastError);
		return false;
	}
}

bool PythonScriptManager::parseAndExecuteLine(const QString& line) {
	QString trimmed = line.trimmed();
	if (trimmed.isEmpty() || trimmed.startsWith('#')) return true;

	// Handle variable assignment: var = method(args)
	if (trimmed.contains(" = ") && !trimmed.startsWith("print")) {
		int eqIdx = trimmed.indexOf(" = ");
		QString varName = trimmed.left(eqIdx).trimmed();
		QString expr = trimmed.mid(eqIdx + 3).trimmed();
		QVariant result = evaluateExpression(expr);
		m_variables[varName] = result;
		m_console->log(QString("  %1 = %2").arg(varName, result.toString()));
		return true;
	}

	// Handle print(var) or print(method(args))
	if (trimmed.startsWith("print(") && trimmed.endsWith(")")) {
		QString inner = trimmed.mid(6, trimmed.size() - 7).trimmed();
		QVariant val = evaluateExpression(inner);
		m_console->log(QString("[OUTPUT] %1").arg(val.toString()));
		emit scriptOutput(val.toString());
		return true;
	}

	// Handle standalone method call
	QVariant result = evaluateExpression(trimmed);
	if (!result.isValid()) {
		// Method call that returns bool (success/failure)
		return true;
	}
	return result.toBool();
}

QVariant PythonScriptManager::evaluateExpression(const QString& expr) const {
	if (!m_api) return QVariant();

	QString e = expr.trimmed();

	if (e == "True" || e == "true") return true;
	if (e == "False" || e == "false") return false;

	// Simple integer/float literals
	bool ok;
	int intVal = e.toInt(&ok);
	if (ok) return intVal;
	float floatVal = e.toFloat(&ok);
	if (ok) return floatVal;

	// String literal
	if ((e.startsWith('"') && e.endsWith('"')) || (e.startsWith('\'') && e.endsWith('\''))) {
		return e.mid(1, e.size() - 2);
	}

	// Variable lookup
	if (m_variables.contains(e)) {
		return m_variables[e];
	}

	// Function call: funcName(args)
	int parenOpen = e.indexOf('(');
	if (parenOpen > 0 && e.endsWith(')')) {
		QString funcName = e.left(parenOpen).trimmed();
		QString argsStr = e.mid(parenOpen + 1, e.size() - parenOpen - 2).trimmed();
		return callAPIFunction(funcName, argsStr);
	}

	// Object.method(args) - e.g. selection.name
	int dotIdx = e.indexOf('.');
	if (dotIdx > 0) {
		QString objName = e.left(dotIdx).trimmed();
		QString rest = e.mid(dotIdx + 1).trimmed();
		int pOpen = rest.indexOf('(');
		if (pOpen > 0 && rest.endsWith(')')) {
			QString method = rest.left(pOpen).trimmed();
			QString argsStr = rest.mid(pOpen + 1, rest.size() - pOpen - 2).trimmed();
			return callAPIFunction(method, argsStr);
		}
	}

	return e;
}

QVariant PythonScriptManager::callAPIFunction(const QString& funcName, const QString& argsStr) const {
	if (!m_api) return QVariant();

	QStringList args;
	if (!argsStr.isEmpty()) {
		args = parseArguments(argsStr);
	}

	// Selection API
	if (funcName == "getSelection") return m_api->getSelection();
	if (funcName == "clearSelection") { const_cast<PythonScriptManager*>(this)->m_api->clearSelection(); return true; }

	// Object API
	if (funcName == "getObjects") return QVariant(m_api->getObjects());
	if (funcName == "getObjectCount") return m_api->getObjectCount();
	if (funcName == "getObject" && args.size() >= 1) {
		return m_api->getObject(args[0].toInt());
	}
	if (funcName == "createObject" && args.size() >= 2) {
		return m_api->createObject(args[0], args[1]);
	}
	if (funcName == "deleteObject" && args.size() >= 1) {
		return m_api->deleteObject(args[0].toInt());
	}
	if (funcName == "duplicateObject" && args.size() >= 1) {
		return m_api->duplicateObject(args[0].toInt());
	}
	if (funcName == "setSelection" && args.size() >= 1) {
		return m_api->setSelection(args[0].toInt());
	}

	// Transform API
	if (funcName == "translate" && args.size() >= 4) {
		return m_api->translate(args[0].toInt(), args[1].toFloat(), args[2].toFloat(), args[3].toFloat());
	}
	if (funcName == "rotate" && args.size() >= 4) {
		return m_api->rotate(args[0].toInt(), args[1].toFloat(), args[2].toFloat(), args[3].toFloat());
	}
	if (funcName == "scale" && args.size() >= 4) {
		return m_api->scale(args[0].toInt(), args[1].toFloat(), args[2].toFloat(), args[3].toFloat());
	}

	// Mesh operations
	if (funcName == "extrude" && args.size() >= 2) {
		QVariantList faceIndices;
		for (int i = 0; i < args.size() - 1; i++) {
			faceIndices.append(args[i].toInt());
		}
		return m_api->extrude(faceIndices, args.last().toFloat());
	}
	if (funcName == "inset" && args.size() >= 2) {
		QVariantList faceIndices;
		for (int i = 0; i < args.size() - 1; i++) {
			faceIndices.append(args[i].toInt());
		}
		return m_api->inset(faceIndices, args.last().toFloat());
	}
	if (funcName == "bevel" && args.size() >= 3) {
		QVariantList edgeIndices;
		for (int i = 0; i < args.size() - 2; i++) {
			edgeIndices.append(args[i].toInt());
		}
		return m_api->bevel(edgeIndices, args[args.size() - 2].toFloat(), args.last().toInt());
	}
	if (funcName == "subdivide" && args.size() >= 2) {
		QVariantList faceIndices;
		for (int i = 0; i < args.size() - 1; i++) {
			faceIndices.append(args[i].toInt());
		}
		return m_api->subdivide(faceIndices, args.last().toInt());
	}

	// Material API
	if (funcName == "setMaterialColor" && args.size() >= 5) {
		return m_api->setMaterialColor(args[0].toInt(), args[1].toFloat(), args[2].toFloat(), args[3].toFloat(), args[4].toFloat());
	}
	if (funcName == "setMaterialMetallic" && args.size() >= 2) {
		return m_api->setMaterialMetallic(args[0].toInt(), args[1].toFloat());
	}
	if (funcName == "setMaterialRoughness" && args.size() >= 2) {
		return m_api->setMaterialRoughness(args[0].toInt(), args[1].toFloat());
	}
	if (funcName == "getMaterial" && args.size() >= 1) {
		return m_api->getMaterial(args[0].toInt());
	}

	// Export API
	if (funcName == "exportKN5" && args.size() >= 1) {
		return m_api->exportKN5(args[0]);
	}
	if (funcName == "exportFBX" && args.size() >= 1) {
		return m_api->exportFBX(args[0]);
	}
	if (funcName == "exportOBJ" && args.size() >= 1) {
		return m_api->exportOBJ(args[0]);
	}

	// Utility
	if (funcName == "undo") return m_api->undo();
	if (funcName == "redo") return m_api->redo();

	m_console->logError(QString("Unknown function: %1").arg(funcName));
	return QVariant();
}

QStringList PythonScriptManager::parseArguments(const QString& argsStr) const {
	QStringList result;
	if (argsStr.trimmed().isEmpty()) return result;

	int depth = 0;
	QString current;
	for (int i = 0; i < argsStr.size(); i++) {
		QChar c = argsStr[i];
		if (c == '(') depth++;
		else if (c == ')') depth--;
		else if (c == ',' && depth == 0) {
			result.append(current.trimmed());
			current.clear();
			continue;
		}
		current.append(c);
	}
	if (!current.trimmed().isEmpty()) {
		result.append(current.trimmed());
	}
	return result;
}

// ============================================================================
// ModelerPythonAPI Implementation
// ============================================================================

ModelerPythonAPI::ModelerPythonAPI(QObject* parent)
	: QObject(parent)
{
}

QVariant ModelerPythonAPI::getSelection() const {
	if (!m_modeler || !m_modeler->hasSelection()) return QVariant();
	auto* sel = m_modeler->selectedObject();
	if (!sel) return QVariant();
	QVariantMap map;
	map["id"] = sel->id();
	map["name"] = sel->name();
	map["type"] = sel->type();
	return map;
}

bool ModelerPythonAPI::setSelection(int objectId) {
	if (!m_modeler) return false;
	m_modeler->selectObject(objectId);
	return true;
}

void ModelerPythonAPI::clearSelection() {
	if (m_modeler) m_modeler->deselectAll();
}

QVariantList ModelerPythonAPI::getObjects() const {
	if (!m_modeler || !m_modeler->sceneGraph()) return QVariantList();
	QVariantList result;
	auto* sg = m_modeler->sceneGraph();
	for (auto* obj : sg->allObjects()) {
		QVariantMap map;
		map["id"] = obj->id();
		map["name"] = obj->name();
		map["type"] = static_cast<int>(obj->type());
		result.append(map);
	}
	return result;
}

QVariant ModelerPythonAPI::getObject(int objectId) const {
	if (!m_modeler || !m_modeler->sceneGraph()) return QVariant();
	auto* obj = m_modeler->sceneGraph()->findObjectById(objectId);
	if (!obj) return QVariant();
	QVariantMap map;
	map["id"] = obj->id();
	map["name"] = obj->name();
	map["type"] = static_cast<int>(obj->type());
	return map;
}

int ModelerPythonAPI::createObject(const QString& name, const QString& type) {
	if (!m_modeler) return -1;
	auto* sg = m_modeler->sceneGraph();
	if (!sg) return -1;

	SceneObject::Type objType = SceneObject::Type::Node;
	if (type == "Mesh") objType = SceneObject::Type::Mesh;
	else if (type == "Light") objType = SceneObject::Type::Light;
	else if (type == "Camera") objType = SceneObject::Type::Camera;
	else if (type == "Bone") objType = SceneObject::Type::Bone;

	SceneObject* obj = sg->createObject(name, objType);
	return obj ? obj->id() : -1;
}

bool ModelerPythonAPI::deleteObject(int objectId) {
	if (!m_modeler) return false;
	auto* sg = m_modeler->sceneGraph();
	if (!sg) return false;

	SceneObject* obj = sg->findObjectById(objectId);
	if (!obj) return false;

	sg->deleteObject(obj);
	return true;
}

bool ModelerPythonAPI::duplicateObject(int objectId) {
	if (!m_modeler) return false;
	m_modeler->selectObject(objectId);
	m_modeler->duplicateSelected();
	return true;
}

bool ModelerPythonAPI::translate(int objectId, float x, float y, float z) {
	if (!m_modeler) return false;
	m_modeler->selectObject(objectId);
	m_modeler->translateSelected(x, y, z);
	return true;
}

bool ModelerPythonAPI::rotate(int objectId, float x, float y, float z) {
	if (!m_modeler) return false;
	m_modeler->selectObject(objectId);
	m_modeler->rotateSelected(x, y, z);
	return true;
}

bool ModelerPythonAPI::scale(int objectId, float x, float y, float z) {
	if (!m_modeler) return false;
	m_modeler->selectObject(objectId);
	m_modeler->scaleSelected(x, y, z);
	return true;
}

bool ModelerPythonAPI::extrude(const QVariantList& faceIndices, float distance) {
	if (!m_modeler) return false;
	QList<int> indices;
	for (const auto& v : faceIndices) indices.append(v.toInt());
	m_modeler->extrudeFaces(indices, distance);
	return true;
}

bool ModelerPythonAPI::inset(const QVariantList& faceIndices, float amount) {
	if (!m_modeler) return false;
	QList<int> indices;
	for (const auto& v : faceIndices) indices.append(v.toInt());
	m_modeler->insetFaces(indices, amount);
	return true;
}

bool ModelerPythonAPI::bevel(const QVariantList& edgeIndices, float amount, int segments) {
	if (!m_modeler) return false;
	QList<int> indices;
	for (const auto& v : edgeIndices) indices.append(v.toInt());
	m_modeler->bevelEdges(indices, amount, segments);
	return true;
}

bool ModelerPythonAPI::subdivide(const QVariantList& faceIndices, int cuts) {
	if (!m_modeler) return false;
	QList<int> indices;
	for (const auto& v : faceIndices) indices.append(v.toInt());
	m_modeler->subdivideFaces(indices, cuts);
	return true;
}

bool ModelerPythonAPI::setMaterialColor(int objectId, float r, float g, float b, float a) {
	if (!m_modeler) return false;
	auto* sg = m_modeler->sceneGraph();
	if (!sg) return false;

	SceneObject* obj = sg->findObjectById(objectId);
	if (!obj) return false;

	obj->setBaseColor(QColor::fromRgbF(r, g, b, a));
	return true;
}

bool ModelerPythonAPI::setMaterialMetallic(int objectId, float value) {
	if (!m_modeler) return false;
	auto* sg = m_modeler->sceneGraph();
	if (!sg) return false;

	SceneObject* obj = sg->findObjectById(objectId);
	if (!obj) return false;

	obj->setMetallic(value);
	return true;
}

bool ModelerPythonAPI::setMaterialRoughness(int objectId, float value) {
	if (!m_modeler) return false;
	auto* sg = m_modeler->sceneGraph();
	if (!sg) return false;

	SceneObject* obj = sg->findObjectById(objectId);
	if (!obj) return false;

	obj->setRoughness(value);
	return true;
}

QVariantMap ModelerPythonAPI::getMaterial(int objectId) const {
	if (!m_modeler) return QVariantMap();
	auto* sg = m_modeler->sceneGraph();
	if (!sg) return QVariantMap();

	SceneObject* obj = sg->findObjectById(objectId);
	if (!obj) return QVariantMap();

	QVariantMap map;
	map["baseColor"] = obj->baseColor();
	map["metallic"] = obj->metallic();
	map["roughness"] = obj->roughness();
	map["opacity"] = obj->opacity();
	return map;
}

bool ModelerPythonAPI::exportKN5(const QString& path) {
	if (!m_modeler) return false;
	return m_modeler->exportKN5(path);
}

bool ModelerPythonAPI::exportFBX(const QString& path) {
	if (!m_modeler) return false;
	return m_modeler->exportFBX(path);
}

bool ModelerPythonAPI::exportOBJ(const QString& path) {
	if (!m_modeler) return false;
	auto* sg = m_modeler->sceneGraph();
	if (!sg) return false;

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

	QTextStream out(&file);
	out << "# Exported by KSEditor Python API\n";

	auto objects = sg->allObjects();
	for (SceneObject* obj : objects) {
		if (!obj->mesh() || obj->mesh()->geometry().vertices.isEmpty()) continue;

		out << "\no " << obj->name() << "\n";

		const auto& verts = obj->mesh()->geometry().vertices;
		const auto& indices = obj->mesh()->geometry().indices;

		for (const SceneVertex& v : verts) {
			out << "v " << v.position.x() << " " << v.position.y() << " " << v.position.z() << "\n";
		}
		for (const SceneVertex& v : verts) {
			out << "vn " << v.normal.x() << " " << v.normal.y() << " " << v.normal.z() << "\n";
		}
		for (const SceneVertex& v : verts) {
			out << "vt " << v.uv.x() << " " << v.uv.y() << "\n";
		}
		for (size_t i = 0; i + 2 < indices.size(); i += 3) {
			uint32_t i0 = indices[i] + 1;
			uint32_t i1 = indices[i + 1] + 1;
			uint32_t i2 = indices[i + 2] + 1;
			out << "f " << i0 << "/" << i0 << "/" << i0 << " "
					 << i1 << "/" << i1 << "/" << i1 << " "
					 << i2 << "/" << i2 << "/" << i2 << "\n";
		}
	}
	return true;
}

int ModelerPythonAPI::getObjectCount() const {
	if (!m_modeler) return 0;
	return m_modeler->objectCount();
}

bool ModelerPythonAPI::undo() {
	if (!m_modeler) return false;
	m_modeler->undo();
	return true;
}

bool ModelerPythonAPI::redo() {
	if (!m_modeler) return false;
	m_modeler->redo();
	return true;
}

} // namespace ks
