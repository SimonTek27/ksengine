#pragma once

#include <QString>
#include <QVariant>
#include <QMap>
#include <QVector>
#include <QJsonValue>

/**
 * @brief JSON File Parser
 * 
 * Wrapper around Qt's JSON functionality with additional features
 * like path-based access, validation, and schema support.
 * Used for configuration files, data exchange, and serialization.
 */

class JSONParser {
public:
    explicit JSONParser();
    explicit JSONParser(const QString& filePath);
    
    // File operations
    bool load(const QString& filePath);
    bool save(const QString& filePath) const;
    bool savePretty(const QString& filePath) const;
    void clear();
    
    // Parse from string
    bool parse(const QString& jsonString);
    QString toString(bool pretty = false) const;
    
    // Validation
    bool isValid() const;
    QString lastError() const { return m_lastError; }
    
    // Root value access
    QVariant root() const { return m_root; }
    void setRoot(const QVariant& value) { m_root = value; }
    
    // Path-based access (e.g., "objects[0].name")
    QVariant value(const QString& path, const QVariant& defaultValue = QVariant()) const;
    void setValue(const QString& path, const QVariant& value);
    bool contains(const QString& path) const;
    void remove(const QString& path);
    
    // Type checking
    bool isObject() const;
    bool isArray() const;
    bool isNull() const;
    
    // Object operations
    QStringList keys() const;
    QVariant objectValue(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void setObjectValue(const QString& key, const QVariant& value);
    bool hasObjectKey(const QString& key) const;
    
    // Array operations
    int arraySize() const;
    QVariant arrayAt(int index, const QVariant& defaultValue = QVariant()) const;
    void arrayAppend(const QVariant& value);
    void arrayInsert(int index, const QVariant& value);
    void arrayRemoveAt(int index);
    void arrayClear();
    
    // Convenience methods for primitive types
    bool toBool(const QString& path = QString(), bool defaultValue = false) const;
    int toInt(const QString& path = QString(), int defaultValue = 0) const;
    double toDouble(const QString& path = QString(), double defaultValue = 0.0) const;
    QString toString(const QString& path = QString(), const QString& defaultValue = QString()) const;
    QStringList toStringList(const QString& path = QString()) const;
    QVariantList toList(const QString& path = QString()) const;
    QVariantMap toMap(const QString& path = QString()) const;
    
    // Schema validation
    bool validateSchema(const QVariantMap& schema, QStringList& errors) const;
    
    // Merge with another JSON
    void merge(const JSONParser& other, bool overwrite = true);
    
    // Query helpers
    QVector<QVariant> findValues(const QString& key) const;
    QVector<QString> findPaths(const QString& key) const;
    
    // Static helpers
    static QVariant parseString(const QString& jsonString, QString* error = nullptr);
    static QString stringify(const QVariant& value, bool pretty = false);
    static bool isValidJson(const QString& jsonString);

private:
    QVariant m_root;
    QString m_lastError;
    QString m_currentFile;
    
    // Internal path parsing
    struct PathToken {
        enum Type { Key, Index };
        Type type;
        QString key;
        int index;
    };
    
    QVector<PathToken> parsePath(const QString& path) const;
    QVariant* navigateTo(const QVector<PathToken>& tokens, int depth, bool createIfMissing);
    const QVariant* navigateToConst(const QVector<PathToken>& tokens, int depth) const;
    
    // Type conversion helpers
    QVariant jsonValueToVariant(const QJsonValue& value) const;
    QJsonValue variantToJsonValue(const QVariant& value) const;
};

/**
 * @brief JSON Schema Validator
 * 
 * Validates JSON data against a JSON Schema (draft-07 subset)
 */
class JSONSchemaValidator {
public:
    explicit JSONSchemaValidator(const QVariantMap& schema);
    
    bool validate(const QVariant& data, QStringList& errors) const;
    bool validate(const JSONParser& parser, QStringList& errors) const;
    
    // Schema properties
    QString getTitle() const { return m_schema.value("title").toString(); }
    QString getDescription() const { return m_schema.value("description").toString(); }
    QString getType() const { return m_schema.value("type").toString(); }
    
private:
    QVariantMap m_schema;
    
    bool validateType(const QVariant& data, const QString& expectedType, QStringList& errors) const;
    bool validateProperties(const QVariantMap& data, const QVariantMap& schema, QStringList& errors) const;
    bool validateRequired(const QVariantMap& data, const QStringList& required, QStringList& errors) const;
    bool validateItems(const QVariantList& data, const QVariantMap& schema, QStringList& errors) const;
    bool validateMinMax(const QVariant& data, const QVariantMap& schema, QStringList& errors) const;
    bool validatePattern(const QString& data, const QString& pattern, QStringList& errors) const;
    bool validateEnum(const QVariant& data, const QVariantList& enumValues, QStringList& errors) const;
};

/**
 * @brief JSON Utility Functions
 */
namespace JSONUtils {
    // Flatten nested JSON to dot notation (e.g., "a.b.c": value)
    QVariantMap flatten(const QVariant& data, const QString& prefix = QString());
    
    // Expand dot notation back to nested structure
    QVariant expand(const QVariantMap& flat);
    
    // Deep clone
    QVariant deepCopy(const QVariant& data);
    
    // Compare two JSON values (deep equality)
    bool deepEquals(const QVariant& a, const QVariant& b);
    
    // Get value type as string
    QString typeToString(const QVariant& value);
    
    // Escape JSON string
    QString escapeString(const QString& str);
    
    // Unescape JSON string
    QString unescapeString(const QString& str);
}