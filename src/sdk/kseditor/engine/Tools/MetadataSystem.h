#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QVariant>
#include <QFile>
#include <QDir>

namespace ks {

struct MetadataField {
    QString id;
    QString name;
    QString type;
    QString description;
    bool required = false;
    bool multiValue = false;
    QVariant defaultValue;
    QStringList allowedValues;
};

struct MetadataSchema {
    QString id;
    QString name;
    QString description;
    QVector<MetadataField> fields;
    bool isBuiltIn = false;
};

struct MetadataEntry {
    QString id;
    QString assetId;
    QString schemaId;
    QString createdAt;
    QJsonObject values;
};

class MetadataCatalog : public QObject
{
    Q_OBJECT

public:
    static MetadataCatalog* instance();

    explicit MetadataCatalog(QObject* parent = nullptr);
    ~MetadataCatalog();

    void registerSchema(const MetadataSchema& schema);
    void unregisterSchema(const QString& schemaId);

    bool hasSchema(const QString& schemaId) const;
    MetadataSchema getSchema(const QString& schemaId) const;
    QVector<MetadataSchema> getSchemas() const { return m_schemas.values(); }

    MetadataEntry getEntry(const QString& entryId) const;
    QVector<MetadataEntry> getEntriesForAsset(const QString& assetId) const;
    QVector<MetadataEntry> getEntries() const { return m_entries.values(); }

    QString createEntry(const QString& assetId, const QString& schemaId);
    void updateEntry(const MetadataEntry& entry);
    void deleteEntry(const QString& entryId);

    QVariant getFieldValue(const QString& entryId, const QString& fieldId) const;
    void setFieldValue(const QString& entryId, const QString& fieldId, const QVariant& value);

    bool validateEntry(const QString& schemaId, const QJsonObject& values, QString& error) const;

    void save();
    void load();
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

signals:
    void entryCreated(const QString& entryId);
    void entryUpdated(const QString& entryId);
    void entryDeleted(const QString& entryId);
    void schemaRegistered(const QString& schemaId);

private:
    void ensurePathExists();
    QString getDefaultDataPath() const;

    static MetadataCatalog* s_instance;

    QMap<QString, MetadataSchema> m_schemas;
    QMap<QString, MetadataEntry> m_entries;
    QString m_savePath;
};

} // namespace ks
