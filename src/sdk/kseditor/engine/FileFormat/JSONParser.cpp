#include "JSONParser.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

// ============================================================
// JSONParser Implementation
// ============================================================

JSONParser::JSONParser() : m_root(QVariant()), m_currentFile() {}

JSONParser::JSONParser(const QString& filePath) : JSONParser() {
    load(filePath);
}

bool JSONParser::load(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = QString("Cannot open file: %1").arg(file.errorString());
        return false;
    }
    
    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    
    if (!parse(content)) {
        return false;
    }
    
    m_currentFile = filePath;
    return true;
}

bool JSONParser::save(const QString& filePath) const {
    return savePretty(filePath);
}

bool JSONParser::savePretty(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream stream(&file);
    stream << toString(true);
    file.close();
    return true;
}

void JSONParser::clear() {
    m_root = QVariant();
    m_lastError.clear();
    m_currentFile.clear();
}

bool JSONParser::parse(const QString& jsonString) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &error);
    
    if (error.error != QJsonParseError::NoError) {
        m_lastError = QString("JSON parse error at %1: %2")
                          .arg(error.offset)
                          .arg(error.errorString());
        return false;
    }
    
    if (doc.isObject()) {
        m_root = doc.object().toVariantMap();
    } else if (doc.isArray()) {
        m_root = doc.array().toVariantList();
    } else if (doc.isNull()) {
        m_root = QVariant();
    }
    
    m_lastError.clear();
    return true;
}

QString JSONParser::toString(bool pretty) const {
    QJsonDocument doc;
    
    if (m_root.typeId() == QMetaType::QVariantMap) {
        doc = QJsonDocument(QJsonObject::fromVariantMap(m_root.toMap()));
    } else if (m_root.typeId() == QMetaType::QVariantList) {
        doc = QJsonDocument(QJsonArray::fromVariantList(m_root.toList()));
    } else {
        return QString();
    }
    
    return pretty ? doc.toJson(QJsonDocument::Indented) : doc.toJson(QJsonDocument::Compact);
}

bool JSONParser::isValid() const {
    return !m_root.isNull();
}

bool JSONParser::isObject() const {
    return m_root.typeId() == QMetaType::QVariantMap;
}

bool JSONParser::isArray() const {
    return m_root.typeId() == QMetaType::QVariantList;
}

bool JSONParser::isNull() const {
    return m_root.isNull();
}

QStringList JSONParser::keys() const {
    if (!isObject()) return QStringList();
    return m_root.toMap().keys();
}

QVariant JSONParser::objectValue(const QString& key, const QVariant& defaultValue) const {
    if (!isObject()) return defaultValue;
    return m_root.toMap().value(key, defaultValue);
}

void JSONParser::setObjectValue(const QString& key, const QVariant& value) {
    if (!isObject()) {
        m_root = QVariantMap();
    }
    QVariantMap map = m_root.toMap();
    map[key] = value;
    m_root = map;
}

bool JSONParser::hasObjectKey(const QString& key) const {
    return isObject() && m_root.toMap().contains(key);
}

int JSONParser::arraySize() const {
    return isArray() ? m_root.toList().size() : 0;
}

QVariant JSONParser::arrayAt(int index, const QVariant& defaultValue) const {
    if (!isArray()) return defaultValue;
    QVariantList list = m_root.toList();
    if (index < 0 || index >= list.size()) return defaultValue;
    return list.at(index);
}

void JSONParser::arrayAppend(const QVariant& value) {
    if (!isArray()) {
        m_root = QVariantList();
    }
    QVariantList list = m_root.toList();
    list.append(value);
    m_root = list;
}

void JSONParser::arrayInsert(int index, const QVariant& value) {
    if (!isArray()) {
        m_root = QVariantList();
    }
    QVariantList list = m_root.toList();
    if (index >= 0 && index <= list.size()) {
        list.insert(index, value);
        m_root = list;
    }
}

void JSONParser::arrayRemoveAt(int index) {
    if (!isArray()) return;
    QVariantList list = m_root.toList();
    if (index >= 0 && index < list.size()) {
        list.removeAt(index);
        m_root = list;
    }
}

void JSONParser::arrayClear() {
    if (isArray()) {
        m_root = QVariantList();
    }
}

QVector<JSONParser::PathToken> JSONParser::parsePath(const QString& path) const {
    QVector<PathToken> tokens;
    QRegularExpression regex(R"(([a-zA-Z_][a-zA-Z0-9_]*)|\[(\d+)\])");
    QRegularExpressionMatchIterator it = regex.globalMatch(path);
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        if (match.captured(1).length() > 0) {
            PathToken token;
            token.type = PathToken::Key;
            token.key = match.captured(1);
            tokens.append(token);
        } else if (match.captured(2).length() > 0) {
            PathToken token;
            token.type = PathToken::Index;
            token.index = match.captured(2).toInt();
            tokens.append(token);
        }
    }
    
    return tokens;
}

QVariant* JSONParser::navigateTo(const QVector<PathToken>& tokens, int depth, bool createIfMissing) {
    if (depth >= tokens.size()) return &m_root;
    
    QVariant* current = &m_root;
    
    for (int i = 0; i < tokens.size(); ++i) {
        const PathToken& token = tokens[i];
        
        if (token.type == PathToken::Key) {
            if (current->typeId() != QMetaType::QVariantMap) {
                if (createIfMissing) {
                    *current = QVariantMap();
                } else {
                    return nullptr;
                }
            }
            
            QVariantMap* map = static_cast<QVariantMap*>(current->data());
            if (!map->contains(token.key) && createIfMissing) {
                map->insert(token.key, QVariant());
            }
            current = &((*map)[token.key]);
            
        } else if (token.type == PathToken::Index) {
            if (current->typeId() != QMetaType::QVariantList) {
                if (createIfMissing) {
                    *current = QVariantList();
                } else {
                    return nullptr;
                }
            }
            
            QVariantList* list = static_cast<QVariantList*>(current->data());
            if (token.index >= list->size() && createIfMissing) {
                list->resize(token.index + 1);
            }
            
            if (token.index < list->size()) {
                current = &((*list)[token.index]);
            } else {
                return nullptr;
            }
        }
    }
    
    return current;
}

const QVariant* JSONParser::navigateToConst(const QVector<PathToken>& tokens, int depth) const {
    return const_cast<JSONParser*>(this)->navigateTo(tokens, depth, false);
}

QVariant JSONParser::value(const QString& path, const QVariant& defaultValue) const {
    QVector<PathToken> tokens = parsePath(path);
    const QVariant* result = navigateToConst(tokens, 0);
    return result ? *result : defaultValue;
}

void JSONParser::setValue(const QString& path, const QVariant& value) {
    QVector<PathToken> tokens = parsePath(path);
    QVariant* target = navigateTo(tokens, 0, true);
    if (target) {
        *target = value;
    }
}

bool JSONParser::contains(const QString& path) const {
    QVector<PathToken> tokens = parsePath(path);
    const QVariant* result = navigateToConst(tokens, 0);
    return result != nullptr && !result->isNull();
}

void JSONParser::remove(const QString& path) {
    QVector<PathToken> tokens = parsePath(path);
    if (tokens.isEmpty()) return;
    
    QVector<PathToken> parentTokens = tokens;
    parentTokens.removeLast();
    
    QVariant* parent = navigateTo(parentTokens, 0, false);
    if (!parent) return;
    
    const PathToken& lastToken = tokens.last();
    if (lastToken.type == PathToken::Key && parent->typeId() == QMetaType::QVariantMap) {
        parent->toMap().remove(lastToken.key);
    } else if (lastToken.type == PathToken::Index && parent->typeId() == QMetaType::QVariantList) {
        QVariantList list = parent->toList();
        if (lastToken.index >= 0 && lastToken.index < list.size()) {
            list.removeAt(lastToken.index);
            *parent = list;
        }
    }
}

bool JSONParser::toBool(const QString& path, bool defaultValue) const {
    QVariant val = value(path);
    if (val.typeId() == QMetaType::Bool) return val.toBool();
    if (val.typeId() == QMetaType::Int) return val.toInt() != 0;
    if (val.typeId() == QMetaType::Double) return val.toDouble() != 0.0;
    return defaultValue;
}

int JSONParser::toInt(const QString& path, int defaultValue) const {
    QVariant val = value(path);
    if (val.typeId() == QMetaType::Int) return val.toInt();
    if (val.typeId() == QMetaType::Double) return static_cast<int>(val.toDouble());
    if (val.typeId() == QMetaType::Bool) return val.toBool() ? 1 : 0;
    return defaultValue;
}

double JSONParser::toDouble(const QString& path, double defaultValue) const {
    QVariant val = value(path);
    if (val.typeId() == QMetaType::Double) return val.toDouble();
    if (val.typeId() == QMetaType::Int) return static_cast<double>(val.toInt());
    return defaultValue;
}

QString JSONParser::toString(const QString& path, const QString& defaultValue) const {
    QVariant val = value(path);
    return val.isValid() ? val.toString() : defaultValue;
}

QStringList JSONParser::toStringList(const QString& path) const {
    QVariant val = value(path);
    if (val.typeId() == QMetaType::QStringList) return val.toStringList();
    if (val.typeId() == QMetaType::QVariantList) {
        QStringList result;
        for (const auto& item : val.toList()) {
            result.append(item.toString());
        }
        return result;
    }
    return QStringList();
}

QVariantList JSONParser::toList(const QString& path) const {
    QVariant val = value(path);
    return val.typeId() == QMetaType::QVariantList ? val.toList() : QVariantList();
}

QVariantMap JSONParser::toMap(const QString& path) const {
    QVariant val = value(path);
    return val.typeId() == QMetaType::QVariantMap ? val.toMap() : QVariantMap();
}

QVariant JSONParser::jsonValueToVariant(const QJsonValue& value) const {
    if (value.isBool()) return value.toBool();
    if (value.isDouble()) return value.toDouble();
    if (value.isString()) return value.toString();
    if (value.isArray()) {
        QVariantList list;
        for (const auto& item : value.toArray()) {
            list.append(jsonValueToVariant(item));
        }
        return list;
    }
    if (value.isObject()) {
        QVariantMap map;
        for (auto it = value.toObject().begin(); it != value.toObject().end(); ++it) {
            map[it.key()] = jsonValueToVariant(it.value());
        }
        return map;
    }
    return QVariant();
}

QJsonValue JSONParser::variantToJsonValue(const QVariant& value) const {
    switch (value.typeId()) {
        case QMetaType::Bool: return QJsonValue(value.toBool());
        case QMetaType::Int: return QJsonValue(value.toInt());
        case QMetaType::Double: return QJsonValue(value.toDouble());
        case QMetaType::QString: return QJsonValue(value.toString());
        case QMetaType::QVariantList: {
            QJsonArray array;
            for (const auto& item : value.toList()) {
                array.append(variantToJsonValue(item));
            }
            return array;
        }
        case QMetaType::QVariantMap: {
            QJsonObject obj;
            for (auto it = value.toMap().begin(); it != value.toMap().end(); ++it) {
                obj[it.key()] = variantToJsonValue(it.value());
            }
            return obj;
        }
        default: return QJsonValue();
    }
}

QVariant JSONParser::parseString(const QString& jsonString, QString* error) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = parseError.errorString();
        return QVariant();
    }
    
    if (doc.isObject()) return doc.object().toVariantMap();
    if (doc.isArray()) return doc.array().toVariantList();
    return QVariant();
}

QString JSONParser::stringify(const QVariant& value, bool pretty) {
    QJsonDocument doc;
    
    if (value.typeId() == QMetaType::QVariantMap) {
        doc = QJsonDocument(QJsonObject::fromVariantMap(value.toMap()));
    } else if (value.typeId() == QMetaType::QVariantList) {
        doc = QJsonDocument(QJsonArray::fromVariantList(value.toList()));
    } else {
        return QString();
    }
    
    return pretty ? doc.toJson(QJsonDocument::Indented) : doc.toJson(QJsonDocument::Compact);
}

bool JSONParser::isValidJson(const QString& jsonString) {
    QJsonParseError error;
    QJsonDocument::fromJson(jsonString.toUtf8(), &error);
    return error.error == QJsonParseError::NoError;
}

bool JSONParser::validateSchema(const QVariantMap& schema, QStringList& errors) const {
    JSONSchemaValidator validator(schema);
    return validator.validate(*this, errors);
}

void JSONParser::merge(const JSONParser& other, bool overwrite) {
    if (!other.isObject() || !isObject()) return;
    
    QVariantMap target = m_root.toMap();
    QVariantMap source = other.m_root.toMap();
    
    for (auto it = source.begin(); it != source.end(); ++it) {
        if (overwrite || !target.contains(it.key())) {
            target[it.key()] = it.value();
        }
    }
    
    m_root = target;
}

QVector<QVariant> JSONParser::findValues(const QString& key) const {
    QVector<QVariant> results;
    
    std::function<void(const QVariant&, const QString&)> search = [&](const QVariant& node, const QString& currentPath) {
        if (node.typeId() == QMetaType::QVariantMap) {
            QVariantMap map = node.toMap();
            for (auto it = map.begin(); it != map.end(); ++it) {
                if (it.key() == key) {
                    results.append(it.value());
                }
                search(it.value(), currentPath + "/" + it.key());
            }
        } else if (node.typeId() == QMetaType::QVariantList) {
            QVariantList list = node.toList();
            for (int i = 0; i < list.size(); ++i) {
                search(list[i], currentPath + QString("/[%1]").arg(i));
            }
        }
    };
    
    search(m_root, "");
    return results;
}

QVector<QString> JSONParser::findPaths(const QString& key) const {
    QVector<QString> results;
    
    std::function<void(const QVariant&, const QString&)> search = [&](const QVariant& node, const QString& currentPath) {
        if (node.typeId() == QMetaType::QVariantMap) {
            QVariantMap map = node.toMap();
            for (auto it = map.begin(); it != map.end(); ++it) {
                if (it.key() == key) {
                    results.append(currentPath.isEmpty() ? it.key() : currentPath + "." + it.key());
                }
                search(it.value(), currentPath.isEmpty() ? it.key() : currentPath + "." + it.key());
            }
        } else if (node.typeId() == QMetaType::QVariantList) {
            QVariantList list = node.toList();
            for (int i = 0; i < list.size(); ++i) {
                search(list[i], currentPath + QString("[%1]").arg(i));
            }
        }
    };
    
    search(m_root, "");
    return results;
}

// ============================================================
// JSONSchemaValidator Implementation
// ============================================================

JSONSchemaValidator::JSONSchemaValidator(const QVariantMap& schema) : m_schema(schema) {}

bool JSONSchemaValidator::validate(const QVariant& data, QStringList& errors) const {
    bool valid = true;
    
    if (m_schema.contains("type")) {
        QString expectedType = m_schema["type"].toString();
        if (!validateType(data, expectedType, errors)) {
            valid = false;
        }
    }
    
    if (data.typeId() == QMetaType::QVariantMap && m_schema.contains("properties")) {
        if (!validateProperties(data.toMap(), m_schema["properties"].toMap(), errors)) {
            valid = false;
        }
    }
    
    if (data.typeId() == QMetaType::QVariantMap && m_schema.contains("required")) {
        if (!validateRequired(data.toMap(), m_schema["required"].toStringList(), errors)) {
            valid = false;
        }
    }
    
    if (data.typeId() == QMetaType::QVariantList && m_schema.contains("items")) {
        if (!validateItems(data.toList(), m_schema["items"].toMap(), errors)) {
            valid = false;
        }
    }
    
    if (m_schema.contains("minimum") || m_schema.contains("maximum")) {
        if (!validateMinMax(data, m_schema, errors)) {
            valid = false;
        }
    }
    
    if (data.typeId() == QMetaType::QString && m_schema.contains("pattern")) {
        if (!validatePattern(data.toString(), m_schema["pattern"].toString(), errors)) {
            valid = false;
        }
    }
    
    if (m_schema.contains("enum")) {
        if (!validateEnum(data, m_schema["enum"].toList(), errors)) {
            valid = false;
        }
    }
    
    return valid;
}

bool JSONSchemaValidator::validate(const JSONParser& parser, QStringList& errors) const {
    return validate(parser.root(), errors);
}

bool JSONSchemaValidator::validateType(const QVariant& data, const QString& expectedType, QStringList& errors) const {
    QString actualType = JSONUtils::typeToString(data);
    
    if (expectedType == "object" && data.typeId() != QMetaType::QVariantMap) {
        errors << QString("Expected object, got %1").arg(actualType);
        return false;
    }
    if (expectedType == "array" && data.typeId() != QMetaType::QVariantList) {
        errors << QString("Expected array, got %1").arg(actualType);
        return false;
    }
    if (expectedType == "string" && data.typeId() != QMetaType::QString) {
        errors << QString("Expected string, got %1").arg(actualType);
        return false;
    }
    if (expectedType == "integer" && data.typeId() != QMetaType::Int && data.typeId() != QMetaType::Double) {
        errors << QString("Expected integer, got %1").arg(actualType);
        return false;
    }
    if (expectedType == "number" && data.typeId() != QMetaType::Int && data.typeId() != QMetaType::Double) {
        errors << QString("Expected number, got %1").arg(actualType);
        return false;
    }
    if (expectedType == "boolean" && data.typeId() != QMetaType::Bool) {
        errors << QString("Expected boolean, got %1").arg(actualType);
        return false;
    }
    if (expectedType == "null" && !data.isNull()) {
        errors << "Expected null";
        return false;
    }
    
    return true;
}

bool JSONSchemaValidator::validateProperties(const QVariantMap& data, const QVariantMap& schema, QStringList& errors) const {
    bool valid = true;
    
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        const QString& propName = it.key();
        const QVariantMap& propSchema = it.value().toMap();
        
        if (data.contains(propName)) {
            JSONSchemaValidator subValidator(propSchema);
            if (!subValidator.validate(data[propName], errors)) {
                valid = false;
            }
        }
    }
    
    return valid;
}

bool JSONSchemaValidator::validateRequired(const QVariantMap& data, const QStringList& required, QStringList& errors) const {
    bool valid = true;
    
    for (const QString& req : required) {
        if (!data.contains(req)) {
            errors << QString("Required property missing: %1").arg(req);
            valid = false;
        }
    }
    
    return valid;
}

bool JSONSchemaValidator::validateItems(const QVariantList& data, const QVariantMap& schema, QStringList& errors) const {
    bool valid = true;
    JSONSchemaValidator itemValidator(schema);
    
    for (int i = 0; i < data.size(); ++i) {
        if (!itemValidator.validate(data[i], errors)) {
            errors << QString("Error at index %1").arg(i);
            valid = false;
        }
    }
    
    return valid;
}

bool JSONSchemaValidator::validateMinMax(const QVariant& data, const QVariantMap& schema, QStringList& errors) const {
    double value = 0.0;
    
    if (data.typeId() == QMetaType::Int) value = data.toInt();
    else if (data.typeId() == QMetaType::Double) value = data.toDouble();
    else return true;
    
    if (schema.contains("minimum") && value < schema["minimum"].toDouble()) {
        errors << QString("Value %1 is below minimum %2").arg(value).arg(schema["minimum"].toDouble());
        return false;
    }
    
    if (schema.contains("maximum") && value > schema["maximum"].toDouble()) {
        errors << QString("Value %1 exceeds maximum %2").arg(value).arg(schema["maximum"].toDouble());
        return false;
    }
    
    return true;
}

bool JSONSchemaValidator::validatePattern(const QString& data, const QString& pattern, QStringList& errors) const {
    QRegularExpression regex(pattern);
    if (!regex.match(data).hasMatch()) {
        errors << QString("String '%1' does not match pattern '%2'").arg(data).arg(pattern);
        return false;
    }
    return true;
}

bool JSONSchemaValidator::validateEnum(const QVariant& data, const QVariantList& enumValues, QStringList& errors) const {
    for (const QVariant& enumValue : enumValues) {
        if (JSONUtils::deepEquals(data, enumValue)) {
            return true;
        }
    }
    
    errors << QString("Value is not in enum list");
    return false;
}

// ============================================================
// JSONUtils Implementation
// ============================================================

QVariantMap JSONUtils::flatten(const QVariant& data, const QString& prefix) {
    QVariantMap result;
    
    std::function<void(const QVariant&, const QString&)> flattenRecursive = [&](const QVariant& node, const QString& currentPath) {
        if (node.typeId() == QMetaType::QVariantMap) {
            QVariantMap map = node.toMap();
            for (auto it = map.begin(); it != map.end(); ++it) {
                QString newPath = currentPath.isEmpty() ? it.key() : currentPath + "." + it.key();
                flattenRecursive(it.value(), newPath);
            }
        } else if (node.typeId() == QMetaType::QVariantList) {
            QVariantList list = node.toList();
            for (int i = 0; i < list.size(); ++i) {
                QString newPath = currentPath + QString("[%1]").arg(i);
                flattenRecursive(list[i], newPath);
            }
        } else {
            result[currentPath] = node;
        }
    };
    
    flattenRecursive(data, prefix);
    return result;
}

QVariant JSONUtils::expand(const QVariantMap& flat) {
    QVariantMap result;
    
    for (auto it = flat.begin(); it != flat.end(); ++it) {
        QStringList parts = it.key().split('.');
        QVariantMap* current = &result;
        
        for (int i = 0; i < parts.size() - 1; ++i) {
            QString part = parts[i];
            if (!current->contains(part)) {
                (*current)[part] = QVariantMap();
            }
            current = reinterpret_cast<QVariantMap*>((*current)[part].data());
        }
        
        (*current)[parts.last()] = it.value();
    }
    
    return result;
}

QVariant JSONUtils::deepCopy(const QVariant& data) {
    if (data.typeId() == QMetaType::QVariantMap) {
        QVariantMap copy;
        for (auto it = data.toMap().begin(); it != data.toMap().end(); ++it) {
            copy[it.key()] = deepCopy(it.value());
        }
        return copy;
    } else if (data.typeId() == QMetaType::QVariantList) {
        QVariantList copy;
        for (const auto& item : data.toList()) {
            copy.append(deepCopy(item));
        }
        return copy;
    }
    return data;
}

bool JSONUtils::deepEquals(const QVariant& a, const QVariant& b) {
    if (a.typeId() != b.typeId()) return false;
    
    if (a.typeId() == QMetaType::QVariantMap) {
        QVariantMap mapA = a.toMap();
        QVariantMap mapB = b.toMap();
        if (mapA.size() != mapB.size()) return false;
        
        for (auto it = mapA.begin(); it != mapA.end(); ++it) {
            if (!mapB.contains(it.key())) return false;
            if (!deepEquals(it.value(), mapB[it.key()])) return false;
        }
        return true;
    } else if (a.typeId() == QMetaType::QVariantList) {
        QVariantList listA = a.toList();
        QVariantList listB = b.toList();
        if (listA.size() != listB.size()) return false;
        
        for (int i = 0; i < listA.size(); ++i) {
            if (!deepEquals(listA[i], listB[i])) return false;
        }
        return true;
    }
    
    return a == b;
}

QString JSONUtils::typeToString(const QVariant& value) {
    switch (value.typeId()) {
        case QMetaType::QVariantMap: return "object";
        case QMetaType::QVariantList: return "array";
        case QMetaType::QString: return "string";
        case QMetaType::Int: return "integer";
        case QMetaType::Double: return "number";
        case QMetaType::Bool: return "boolean";
        case QMetaType::UnknownType: return "null";
        default: return "unknown";
    }
}

QString JSONUtils::escapeString(const QString& str) {
    QString escaped;
    for (QChar ch : str) {
        if (ch == '"') escaped += "\\\"";
        else if (ch == '\\') escaped += "\\\\";
        else if (ch == '/') escaped += "\\/";
        else if (ch == '\b') escaped += "\\b";
        else if (ch == '\f') escaped += "\\f";
        else if (ch == '\n') escaped += "\\n";
        else if (ch == '\r') escaped += "\\r";
        else if (ch == '\t') escaped += "\\t";
        else escaped += ch;
    }
    return escaped;
}

QString JSONUtils::unescapeString(const QString& str) {
    QString unescaped;
    for (int i = 0; i < str.length(); ++i) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            QChar next = str[i + 1];
            if (next == '"') unescaped += '"';
            else if (next == '\\') unescaped += '\\';
            else if (next == '/') unescaped += '/';
            else if (next == 'b') unescaped += '\b';
            else if (next == 'f') unescaped += '\f';
            else if (next == 'n') unescaped += '\n';
            else if (next == 'r') unescaped += '\r';
            else if (next == 't') unescaped += '\t';
            else unescaped += next;
            i++;
        } else {
            unescaped += str[i];
        }
    }
    return unescaped;
}