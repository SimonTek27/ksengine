#include "CommandHistory.h"
#include "3DModelingQmlBridge.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include <QDebug>
#include <QVariant>
#include <QVector3D>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDataStream>

namespace ks {

// ============================================================================
// CommandHistory Implementation
// ============================================================================

CommandHistory::CommandHistory(QObject* parent)
	: QObject(parent) {}

CommandHistory::~CommandHistory() {
	clear();
}

void CommandHistory::execute(CommandPtr command) {
	if (!command) return;

	// Prova a merge con l'ultimo comando se possibile
	if (!m_undoStack.isEmpty() && m_undoStack.back()->canMergeWith(command.get())) {
		m_undoStack.back()->mergeWith(command.get());
		emit historyChanged();
		return;
	}

	// Esegui il comando
	command->execute();
	m_undoStack.append(command);

	// Limita la memoria: rimuovi comandi vecchi
	while (m_undoStack.size() > m_maxCommands) {
		m_undoStack.removeFirst();
	}

	// Ripulisci redo stack quando esegui un nuovo comando
	m_redoStack.clear();

	emit commandExecuted(command->description());
	emit historyChanged();
}

void CommandHistory::undo() {
	if (!canUndo()) return;

	CommandPtr cmd = m_undoStack.takeLast();
	cmd->undo();
	m_redoStack.append(cmd);

	emit commandUndone(cmd->description());
	emit historyChanged();
}

void CommandHistory::redo() {
	if (!canRedo()) return;

	CommandPtr cmd = m_redoStack.takeLast();
	cmd->redo();
	m_undoStack.append(cmd);

	emit commandRedone(cmd->description());
	emit historyChanged();
}

bool CommandHistory::canUndo() const {
	return !m_undoStack.isEmpty();
}

bool CommandHistory::canRedo() const {
	return !m_redoStack.isEmpty();
}

QString CommandHistory::undoDescription() const {
	if (m_undoStack.isEmpty()) return QString();
	return "Undo: " + m_undoStack.back()->description();
}

QString CommandHistory::redoDescription() const {
	if (m_redoStack.isEmpty()) return QString();
	return "Redo: " + m_redoStack.back()->description();
}

void CommandHistory::clear() {
	m_undoStack.clear();
	m_redoStack.clear();
	emit historyChanged();
}

void CommandHistory::setMaxCommands(int maxCount) {
	m_maxCommands = qMax(10, maxCount);
}

// ============================================================================
// ModelPropertyCommand Implementation
// ============================================================================

ModelPropertyCommand::ModelPropertyCommand(
	const QString& objectName,
	const QString& propertyName,
	const QVariant& oldValue,
	const QVariant& newValue,
	const QString& description)
	: m_objectName(objectName)
	, m_propertyName(propertyName)
	, m_oldValue(oldValue)
	, m_newValue(newValue)
	, m_description(description.isEmpty() ? QString("Change %1").arg(propertyName) : description)
{
}

void ModelPropertyCommand::execute() {
	applyValue(m_newValue);
}

void ModelPropertyCommand::undo() {
	applyValue(m_oldValue);
}

void ModelPropertyCommand::redo() {
	applyValue(m_newValue);
}

void ModelPropertyCommand::applyValue(const QVariant& value) {
	KSModelerQml& modeler = KSModelerQml::instance();
	auto* sg = modeler.sceneGraph();
	if (!sg) return;

	SceneObject* obj = sg->findObjectByName(m_objectName);
	if (!obj) return;

	if (m_propertyName == "position") {
		QVector3D v = value.value<QVector3D>();
		obj->setPosition(v);
	} else if (m_propertyName == "rotation") {
		QVector3D v = value.value<QVector3D>();
		obj->setRotationEuler(v);
	} else if (m_propertyName == "scale") {
		QVector3D v = value.value<QVector3D>();
		obj->setScale(v);
	} else if (m_propertyName == "visible") {
		obj->setVisible(value.toBool());
	} else if (m_propertyName == "baseColor") {
		obj->setBaseColor(value.value<QColor>());
	} else if (m_propertyName == "metallic") {
		obj->setMetallic(value.toFloat());
	} else if (m_propertyName == "roughness") {
		obj->setRoughness(value.toFloat());
	} else if (m_propertyName == "opacity") {
		obj->setOpacity(value.toFloat());
	} else if (m_propertyName == "name") {
		obj->setName(value.toString());
	}
}

bool ModelPropertyCommand::canMergeWith(const Command* other) const {
	const ModelPropertyCommand* pc = dynamic_cast<const ModelPropertyCommand*>(other);
	if (!pc) return false;

	// Merge if same property of same object
	return m_objectName == pc->m_objectName && 
		   m_propertyName == pc->m_propertyName;
}

void ModelPropertyCommand::mergeWith(const Command* other) {
	const ModelPropertyCommand* pc = dynamic_cast<const ModelPropertyCommand*>(other);
	if (pc) {
		m_newValue = pc->m_newValue;
	}
}

// ============================================================================
// TransformCommand Implementation
// ============================================================================

TransformCommand::TransformCommand(
	int objectId,
	Type type,
	const QVector3D& oldValue,
	const QVector3D& newValue)
	: m_objectId(objectId)
	, m_type(type)
	, m_oldValue(oldValue)
	, m_newValue(newValue)
{
}

void TransformCommand::execute() {
	// Already applied by the UI
}

void TransformCommand::undo() {
	KSModelerQml& modeler = KSModelerQml::instance();
	modeler.selectObject(m_objectId);

	switch (m_type) {
		case Translate:
			modeler.setSelectedPosition(m_oldValue.x(), m_oldValue.y(), m_oldValue.z());
			break;
		case Rotate:
			modeler.setSelectedRotation(m_oldValue.x(), m_oldValue.y(), m_oldValue.z());
			break;
		case Scale:
			modeler.setSelectedScale(m_oldValue.x(), m_oldValue.y(), m_oldValue.z());
			break;
	}
}

void TransformCommand::redo() {
	KSModelerQml& modeler = KSModelerQml::instance();
	modeler.selectObject(m_objectId);

	switch (m_type) {
		case Translate:
			modeler.setSelectedPosition(m_newValue.x(), m_newValue.y(), m_newValue.z());
			break;
		case Rotate:
			modeler.setSelectedRotation(m_newValue.x(), m_newValue.y(), m_newValue.z());
			break;
		case Scale:
			modeler.setSelectedScale(m_newValue.x(), m_newValue.y(), m_newValue.z());
			break;
	}
}

QString TransformCommand::description() const {
	switch (m_type) {
		case Translate: return "Translate";
		case Rotate: return "Rotate";
		case Scale: return "Scale";
		default: return "Transform";
	}
}

bool TransformCommand::canMergeWith(const Command* other) const {
	const TransformCommand* tc = dynamic_cast<const TransformCommand*>(other);
	if (!tc) return false;

	// Merge transform dello stesso oggetto e tipo
	return m_objectId == tc->m_objectId && m_type == tc->m_type;
}

void TransformCommand::mergeWith(const Command* other) {
	const TransformCommand* tc = dynamic_cast<const TransformCommand*>(other);
	if (tc) {
		m_newValue = tc->m_newValue;
	}
}

// ============================================================================
// CreateObjectCommand Implementation
// ============================================================================

CreateObjectCommand::CreateObjectCommand(
	const QString& name,
	const QString& type,
	int generatedId)
	: m_name(name)
	, m_type(type)
	, m_createdId(generatedId)
{
}

void CreateObjectCommand::execute() {
	// Object is already created
}

void CreateObjectCommand::undo() {
	KSModelerQml& modeler = KSModelerQml::instance();
	if (m_createdId >= 0) {
		modeler.selectObject(m_createdId);
		modeler.deleteSelected();
	}
}

void CreateObjectCommand::redo() {
	KSModelerQml& modeler = KSModelerQml::instance();
	auto* sg = modeler.sceneGraph();
	if (!sg) return;

	SceneObject::Type objType = SceneObject::Type::Node;
	if (m_type == "Mesh") objType = SceneObject::Type::Mesh;
	else if (m_type == "Light") objType = SceneObject::Type::Light;
	else if (m_type == "Camera") objType = SceneObject::Type::Camera;
	else if (m_type == "Bone") objType = SceneObject::Type::Bone;

	SceneObject* obj = sg->createObject(m_name, objType);
	if (obj) {
		m_createdId = obj->id();
	}
}

QString CreateObjectCommand::description() const {
	return QString("Create %1 (%2)").arg(m_type, m_name);
}

// ============================================================================
// DeleteObjectCommand Implementation
// ============================================================================

DeleteObjectCommand::DeleteObjectCommand(int objectId)
	: m_objectId(objectId)
{
}

void DeleteObjectCommand::execute() {
	KSModelerQml& modeler = KSModelerQml::instance();
	auto* sg = modeler.sceneGraph();
	if (!sg) return;

	SceneObject* obj = sg->findObjectById(m_objectId);
	if (obj) {
		m_objectName = obj->name();
		m_savedState = obj->serialize();
	}
}

void DeleteObjectCommand::undo() {
	KSModelerQml& modeler = KSModelerQml::instance();
	auto* sg = modeler.sceneGraph();
	if (!sg) return;

	SceneObject* obj = sg->createObject(m_objectName, SceneObject::Type::Node);
	if (obj) {
		obj->deserialize(m_savedState);
		m_createdId = obj->id();
	}
}

void DeleteObjectCommand::redo() {
	KSModelerQml& modeler = KSModelerQml::instance();
	modeler.selectObject(m_objectId);
	modeler.deleteSelected();
}

QString DeleteObjectCommand::description() const {
	return QString("Delete %1").arg(m_objectName);
}

// ============================================================================
// MeshEditCommand Implementation
// ============================================================================

MeshEditCommand::MeshEditCommand(
	int objectId,
	Operation op,
	const QByteArray& originalMeshData,
	const QByteArray& modifiedMeshData)
	: m_objectId(objectId)
	, m_operation(op)
	, m_originalMeshData(originalMeshData)
	, m_modifiedMeshData(modifiedMeshData)
{
}

void MeshEditCommand::execute() {
	// Mesh is already modified
}

void MeshEditCommand::undo() {
	KSModelerQml& modeler = KSModelerQml::instance();
	auto* sg = modeler.sceneGraph();
	if (!sg) return;

	SceneObject* obj = sg->findObjectById(m_objectId);
	if (!obj || !obj->mesh()) return;

	// Restore original mesh from serialized data
	QDataStream ds(m_originalMeshData);
	quint32 vCount, iCount;
	ds >> vCount;
	obj->mesh()->geometry().vertices.resize(vCount);
		for (quint32 i = 0; i < vCount; ++i) {
		SceneVertex& v = obj->mesh()->geometry().vertices[i];
		ds >> v.position;
		ds >> v.normal;
		ds >> v.color;
		ds >> v.uv;
	}
	ds >> iCount;
	obj->mesh()->geometry().indices.resize(iCount);
	for (quint32 i = 0; i < iCount; ++i) {
		ds >> obj->mesh()->geometry().indices[i];
	}
}

void MeshEditCommand::redo() {
	KSModelerQml& modeler = KSModelerQml::instance();
	auto* sg = modeler.sceneGraph();
	if (!sg) return;

	SceneObject* obj = sg->findObjectById(m_objectId);
	if (!obj || !obj->mesh()) return;

	// Restore modified mesh from serialized data
	QDataStream ds(m_modifiedMeshData);
	quint32 vCount, iCount;
	ds >> vCount;
	obj->mesh()->geometry().vertices.resize(vCount);
	for (quint32 i = 0; i < vCount; ++i) {
		SceneVertex& v = obj->mesh()->geometry().vertices[i];
		ds >> v.position;
		ds >> v.normal;
		ds >> v.color;
		ds >> v.uv;
	}
	ds >> iCount;
	obj->mesh()->geometry().indices.resize(iCount);
	for (quint32 i = 0; i < iCount; ++i) {
		ds >> obj->mesh()->geometry().indices[i];
	}
}

QString MeshEditCommand::description() const {
	switch (m_operation) {
		case Extrude: return "Extrude";
		case Inset: return "Inset";
		case Bevel: return "Bevel";
		case Subdivide: return "Subdivide";
		default: return "Mesh Edit";
	}
}

// ============================================================================
// ModifierStackCommand Implementation
// ============================================================================

ModifierStackCommand::ModifierStackCommand(
	int objectId,
	const QJsonObject& before,
	const QJsonObject& after,
	const QString& description)
	: m_objectId(objectId)
	, m_before(before)
	, m_after(after)
	, m_description(description.isEmpty() ? "Modify modifier stack" : description)
{
}

void ModifierStackCommand::execute() {
	// The operation has already been applied by the UI.
}

void ModifierStackCommand::undo() {
	apply(m_before);
}

void ModifierStackCommand::redo() {
	apply(m_after);
}

QString ModifierStackCommand::description() const {
	return m_description;
}

void ModifierStackCommand::apply(const QJsonObject& state) {
	KSModelerQml& modeler = KSModelerQml::instance();
	modeler.modifierStackRestore(m_objectId, state);
}

} // namespace ks
