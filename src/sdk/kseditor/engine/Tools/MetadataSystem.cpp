// ──────────────────────────────────────────────────────────────────────────────
// MetadataSystem.cpp
// ──────────────────────────────────────────────────────────────────────────────
#include "MetadataSystem.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QDateTime>
#include <QDebug>

namespace ks {

MetadataCatalog* MetadataCatalog::s_instance = nullptr;

MetadataCatalog* MetadataCatalog::instance()
{
    if (!s_instance) s_instance = new MetadataCatalog();
    return s_instance;
}

MetadataCatalog::MetadataCatalog(QObject* parent) : QObject(parent) {}
MetadataCatalog::~MetadataCatalog() { s_instance = nullptr; }

void MetadataCatalog::registerSchema(const MetadataSchema& schema)
{
    m_schemas.insert(schema.id, schema);
    emit schemaRegistered(schema.id);
}

void MetadataCatalog::unregisterSchema(const QString& id) { m_schemas.remove(id); }
bool MetadataCatalog::hasSchema(const QString& id) const { return m_schemas.contains(id); }

MetadataEntry MetadataCatalog::getEntry(const QString& id) const { return m_entries.value(id); }

QVector<MetadataEntry> MetadataCatalog::getEntriesForAsset(const QString& assetId) const
{
    QVector<MetadataEntry> out;
    for (const auto& e : m_entries)
        if (e.assetId == assetId) out << e;
    return out;
}

void MetadataCatalog::updateEntry(const MetadataEntry& e) { m_entries.insert(e.id, e); emit entryUpdated(e.id); }

void MetadataCatalog::deleteEntry(const QString& id) { m_entries.remove(id); emit entryDeleted(id); }

void MetadataCatalog::setFieldValue(const QString& entryId, const QString& fieldId, const QVariant& value)
{
    if (m_entries.contains(entryId)) {
        m_entries[entryId].values.insert(fieldId, QJsonValue::fromVariant(value));
        emit entryUpdated(entryId);
    }
}

bool MetadataCatalog::validateEntry(const QString& schemaId, const QJsonObject& values, QString& error) const
{
    if (!m_schemas.contains(schemaId)) { error = "Unknown schema: " + schemaId; return false; }
    const auto& schema = m_schemas[schemaId];
    for (const auto& field : schema.fields) {
        if (field.required && !values.contains(field.id)) {
            error = "Required field missing: " + field.name;
            return false;
        }
    }
    return true;
}

QString MetadataCatalog::createEntry(const QString& assetId, const QString& schemaId)
{
    MetadataEntry e;
    e.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.assetId   = assetId;
    e.schemaId  = schemaId;
    e.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_entries.insert(e.id, e);
    emit entryCreated(e.id);
    return e.id;
}

void MetadataCatalog::save()
{
    QJsonObject root;
    // Schemas
    QJsonArray schemasArr;
    for (const auto& s : m_schemas) {
        QJsonObject sObj;
        sObj["id"] = s.id;
        sObj["name"] = s.name;
        sObj["description"] = s.description;
        sObj["isBuiltIn"] = s.isBuiltIn;
        QJsonArray fieldsArr;
        for (const auto& f : s.fields) {
            QJsonObject fObj;
            fObj["id"] = f.id;
            fObj["name"] = f.name;
            fObj["type"] = f.type;
            fObj["description"] = f.description;
            fObj["required"] = f.required;
            fObj["multiValue"] = f.multiValue;
            fObj["defaultValue"] = QJsonValue::fromVariant(f.defaultValue);
            if (!f.allowedValues.isEmpty()) {
                QJsonArray av;
                for (const auto& v : f.allowedValues) av.append(v);
                fObj["allowedValues"] = av;
            }
            fieldsArr.append(fObj);
        }
        sObj["fields"] = fieldsArr;
        schemasArr.append(sObj);
    }
    root["schemas"] = schemasArr;

    // Entries
    QJsonArray entriesArr;
    for (const auto& e : m_entries) {
        QJsonObject eObj;
        eObj["id"] = e.id;
        eObj["assetId"] = e.assetId;
        eObj["schemaId"] = e.schemaId;
        eObj["createdAt"] = e.createdAt;
        eObj["values"] = e.values;
        entriesArr.append(eObj);
    }
    root["entries"] = entriesArr;

    QFile f(m_savePath);
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(root).toJson());
}

void MetadataCatalog::load()
{
    QFile f(m_savePath);
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();

    // Load schemas
    m_schemas.clear();
    QJsonArray schemasArr = root["schemas"].toArray();
    for (const auto& sv : schemasArr) {
        QJsonObject sObj = sv.toObject();
        MetadataSchema s;
        s.id = sObj["id"].toString();
        s.name = sObj["name"].toString();
        s.description = sObj["description"].toString();
        s.isBuiltIn = sObj["isBuiltIn"].toBool();
        for (const auto& fv : sObj["fields"].toArray()) {
            QJsonObject fObj = fv.toObject();
            MetadataField f;
            f.id = fObj["id"].toString();
            f.name = fObj["name"].toString();
            f.type = fObj["type"].toString();
            f.description = fObj["description"].toString();
            f.required = fObj["required"].toBool();
            f.multiValue = fObj["multiValue"].toBool();
            f.defaultValue = fObj["defaultValue"].toVariant();
            for (const auto& av : fObj["allowedValues"].toArray())
                f.allowedValues.append(av.toString());
            s.fields.append(f);
        }
        m_schemas.insert(s.id, s);
    }

    // Load entries
    m_entries.clear();
    QJsonArray entriesArr = root["entries"].toArray();
    for (const auto& ev : entriesArr) {
        QJsonObject eObj = ev.toObject();
        MetadataEntry e;
        e.id = eObj["id"].toString();
        e.assetId = eObj["assetId"].toString();
        e.schemaId = eObj["schemaId"].toString();
        e.createdAt = eObj["createdAt"].toString();
        e.values = eObj["values"].toObject();
        m_entries.insert(e.id, e);
    }
}

} // namespace ks
