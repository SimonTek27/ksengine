#include "INIParser.h"
#include "../sys/LogManager.h"

#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QRegularExpression>

INIParser::INIParser() = default;

INIParser::INIParser(const QString& filePath)
{
    load(filePath);
}

bool INIParser::load(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = "Failed to open file: " + filePath;
        return false;
    }

    clear();
    
    QString currentSection;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    
    QRegularExpression sectionRegex(R"(^\s*\[([^\]]+)\]\s*$)");
    QRegularExpression commentRegex(R"(^\s*;(.+)$)");
    QRegularExpression keyValueRegex(R"(^\s*([^=]+?)\s*=\s*(.*)$)");
    
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        
        // Check for section
        QRegularExpressionMatch sectionMatch = sectionRegex.match(line);
        if (sectionMatch.hasMatch()) {
            currentSection = sectionMatch.captured(1).trimmed();
            continue;
        }
        
        // Check for comment
        QRegularExpressionMatch commentMatch = commentRegex.match(line);
        if (commentMatch.hasMatch()) {
            if (!currentSection.isEmpty()) {
                m_sections[currentSection].comment += commentMatch.captured(1).trimmed() + "\n";
            }
            continue;
        }
        
        // Check for key=value
        QRegularExpressionMatch kvMatch = keyValueRegex.match(line);
        if (kvMatch.hasMatch()) {
            QString key = kvMatch.captured(1).trimmed();
            QString value = kvMatch.captured(2).trimmed();
            
            // Remove trailing comment
            int commentPos = value.indexOf(';');
            if (commentPos >= 0) {
                // Could store inline comments
                value = value.left(commentPos).trimmed();
            }
            
            // Remove quotes
            if ((value.startsWith('"') && value.endsWith('"')) ||
                (value.startsWith('\'') && value.endsWith('\''))) {
                value = value.mid(1, value.length() - 2);
            }
            
            if (currentSection.isEmpty()) {
                currentSection = "_global";
            }
            
            m_sections[currentSection].keys[key] = {parseValue(value), ""};
        }
    }
    
    file.close();
    LOG_INFO("INIParser", "Loaded INI: " + filePath);
    return true;
}

bool INIParser::save(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastError = "Failed to create file: " + filePath;
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    
    for (auto sectionIt = m_sections.constBegin(); sectionIt != m_sections.constEnd(); ++sectionIt) {
        QString sectionName = sectionIt.key();
        const SectionData& section = sectionIt.value();
        
        // Write section comment
        if (!section.comment.isEmpty()) {
            QStringList comments = section.comment.split('\n', Qt::SkipEmptyParts);
            for (const QString& c : comments) {
                stream << "; " << c.trimmed() << "\n";
            }
        }
        
        // Write section header
        if (sectionName == "_global") {
            // Write keys without section header
            for (auto keyIt = section.keys.constBegin(); keyIt != section.keys.constEnd(); ++keyIt) {
                if (!keyIt.value().comment.isEmpty()) {
                    stream << "; " << keyIt.value().comment << "\n";
                }
                stream << keyIt.key() << "=" << formatValue(keyIt.value().value) << "\n";
            }
        } else {
            stream << "[" << sectionName << "]\n";
            for (auto keyIt = section.keys.constBegin(); keyIt != section.keys.constEnd(); ++keyIt) {
                if (!keyIt.value().comment.isEmpty()) {
                    stream << "; " << keyIt.value().comment << "\n";
                }
                stream << keyIt.key() << "=" << formatValue(keyIt.value().value) << "\n";
            }
        }
        stream << "\n";
    }
    
    file.close();
    LOG_INFO("INIParser", "Saved INI: " + filePath);
    return true;
}

void INIParser::clear()
{
    m_sections.clear();
}

QStringList INIParser::sections() const
{
    QStringList result;
    for (auto it = m_sections.constBegin(); it != m_sections.constEnd(); ++it) {
        if (it.key() != "_global") {
            result.append(it.key());
        }
    }
    return result;
}

bool INIParser::hasSection(const QString& section) const
{
    return m_sections.contains(section);
}

void INIParser::removeSection(const QString& section)
{
    m_sections.remove(section);
}

QStringList INIParser::keys(const QString& section) const
{
    if (!m_sections.contains(section)) {
        return QStringList();
    }
    return m_sections[section].keys.keys();
}

bool INIParser::hasKey(const QString& section, const QString& key) const
{
    if (!m_sections.contains(section)) {
        return false;
    }
    return m_sections[section].keys.contains(key);
}

void INIParser::removeKey(const QString& section, const QString& key)
{
    if (m_sections.contains(section)) {
        m_sections[section].keys.remove(key);
    }
}

QVariant INIParser::value(const QString& section, const QString& key, 
                          const QVariant& defaultValue) const
{
    if (!m_sections.contains(section)) {
        return defaultValue;
    }
    if (!m_sections[section].keys.contains(key)) {
        return defaultValue;
    }
    return m_sections[section].keys[key].value;
}

void INIParser::setValue(const QString& section, const QString& key, const QVariant& value)
{
    if (section.isEmpty() || section == "_global") {
        m_sections["_global"].keys[key] = {value, ""};
    } else {
        m_sections[section].keys[key] = {value, ""};
    }
}

QString INIParser::string(const QString& section, const QString& key, 
                          const QString& defaultValue) const
{
    return value(section, key, defaultValue).toString();
}

int INIParser::integer(const QString& section, const QString& key, int defaultValue) const
{
    bool ok;
    int result = value(section, key, defaultValue).toInt(&ok);
    return ok ? result : defaultValue;
}

bool INIParser::boolean(const QString& section, const QString& key, bool defaultValue) const
{
    QString val = string(section, key).toLower();
    if (val == "true" || val == "1" || val == "yes" || val == "on") {
        return true;
    }
    if (val == "false" || val == "0" || val == "no" || val == "off") {
        return false;
    }
    return defaultValue;
}

double INIParser::real(const QString& section, const QString& key, double defaultValue) const
{
    bool ok;
    double result = value(section, key, defaultValue).toDouble(&ok);
    return ok ? result : defaultValue;
}

QStringList INIParser::stringList(const QString& section, const QString& key,
                                   const QStringList& defaultValue) const
{
    QString val = string(section, key);
    if (val.isEmpty()) {
        return defaultValue;
    }
    return val.split(',', Qt::SkipEmptyParts);
}

void INIParser::setComment(const QString& section, const QString& key, const QString& comment)
{
    if (m_sections.contains(section) && m_sections[section].keys.contains(key)) {
        m_sections[section].keys[key].comment = comment;
    }
}

void INIParser::setSectionComment(const QString& section, const QString& comment)
{
    if (m_sections.contains(section)) {
        m_sections[section].comment = comment;
    }
}

QString INIParser::comment(const QString& section, const QString& key) const
{
    if (m_sections.contains(section) && m_sections[section].keys.contains(key)) {
        return m_sections[section].keys[key].comment;
    }
    return QString();
}

QString INIParser::sectionComment(const QString& section) const
{
    if (m_sections.contains(section)) {
        return m_sections[section].comment;
    }
    return QString();
}

QVariant INIParser::parseValue(const QString& str) const
{
    QString s = str.trimmed();
    
    // Check for boolean
    if (s.toLower() == "true") return true;
    if (s.toLower() == "false") return false;
    
    // Check for integer
    bool ok;
    int intVal = s.toInt(&ok);
    if (ok) return intVal;
    
    // Check for float
    double floatVal = s.toDouble(&ok);
    if (ok) return floatVal;
    
    // Return as string
    return s;
}

QString INIParser::formatValue(const QVariant& value) const
{
    switch (value.typeId()) {
        case QMetaType::Bool:
            return value.toBool() ? "true" : "false";
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
            return QString::number(value.toLongLong());
        case QMetaType::Double:
        case QMetaType::Float:
            return QString::number(value.toDouble(), 'f', 6);
        default:
            QString s = value.toString();
            if (s.contains(' ') || s.contains(',') || s.contains('=')) {
                return "\"" + s + "\"";
            }
            return s;
    }
}

QStringList INIParser::splitPath(const QString& path) const
{
    return path.split('/', Qt::SkipEmptyParts);
}

QMap<QString, QString> INIParser::parseCAR(const QString& carFolder)
{
    QMap<QString, QString> result;
    QStringList files = {"car.ini", "engine.ini", "drivetrain.ini", "brakes.ini",
                         "suspensions.ini", "tyres.ini", "aero.ini", "setup.ini"};
    for (const QString& file : files) {
        INIParser ini(carFolder + "/" + file);
        for (const QString& section : ini.sections()) {
            for (const QString& key : ini.keys(section)) {
                result[section + "/" + key] = ini.string(section, key);
            }
        }
    }
    return result;
}

QMap<QString, QString> INIParser::parseCONTENT(const QString& contentFolder)
{
    QMap<QString, QString> result;
    QDir dir(contentFolder);
    QStringList iniFiles = dir.entryList({"*.ini"}, QDir::Files, QDir::Name);
    for (const QString& file : iniFiles) {
        INIParser ini(contentFolder + "/" + file);
        for (const QString& section : ini.sections()) {
            for (const QString& key : ini.keys(section)) {
                result[file + "/" + section + "/" + key] = ini.string(section, key);
            }
        }
    }
    return result;
}

QMap<QString, QString> KsConfig::parsePhysicsSection(const INIParser& ini, const QString& section)
{
    QMap<QString, QString> result;
    QStringList keys = ini.keys(section);
    for (const QString& key : keys) {
        result[key] = ini.string(section, key);
    }
    return result;
}
