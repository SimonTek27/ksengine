#pragma once

#include <QString>
#include <QMap>
#include <QVariant>
#include <QStringList>

/**
 * @brief INI File Parser
 * 
 * Supports standard INI format with sections and key-value pairs.
 * Used for Assetto Corsa config files, physics parameters, etc.
 */
class INIParser {
public:
    explicit INIParser();
    explicit INIParser(const QString& filePath);
    
    // File operations
    bool load(const QString& filePath);
    bool save(const QString& filePath) const;
    void clear();
    
    // Section operations
    QStringList sections() const;
    bool hasSection(const QString& section) const;
    void removeSection(const QString& section);
    
    // Key operations
    QStringList keys(const QString& section) const;
    bool hasKey(const QString& section, const QString& key) const;
    void removeKey(const QString& section, const QString& key);
    
    // Value operations
    QVariant value(const QString& section, const QString& key, 
                   const QVariant& defaultValue = QVariant()) const;
    void setValue(const QString& section, const QString& key, const QVariant& value);
    
    // Convenience methods
    QString string(const QString& section, const QString& key, 
                   const QString& defaultValue = QString()) const;
    int integer(const QString& section, const QString& key, int defaultValue = 0) const;
    bool boolean(const QString& section, const QString& key, bool defaultValue = false) const;
    double real(const QString& section, const QString& key, double defaultValue = 0.0) const;
    
    // String list operations
    QStringList stringList(const QString& section, const QString& key,
                           const QStringList& defaultValue = QStringList()) const;
    
    // Comments
    void setComment(const QString& section, const QString& key, const QString& comment);
    void setSectionComment(const QString& section, const QString& comment);
    QString comment(const QString& section, const QString& key) const;
    QString sectionComment(const QString& section) const;

    // Assetto Corsa specific helpers
    static QMap<QString, QString> parseCAR(const QString& carFolder);
    static QMap<QString, QString> parseCONTENT(const QString& contentFolder);

private:
    struct KeyData {
        QVariant value;
        QString comment;
    };
    
    struct SectionData {
        QMap<QString, KeyData> keys;
        QString comment;
    };
    
    QMap<QString, SectionData> m_sections;
    mutable QString m_lastError;
    
    QStringList splitPath(const QString& path) const;
    QVariant parseValue(const QString& str) const;
    QString formatValue(const QVariant& value) const;
};

namespace KsConfig {

struct CarPhysics {
    QString section;
    float mass;
    float inertia;
    QMap<QString, float> aero;
};

QMap<QString, QString> parsePhysicsSection(const INIParser& ini, const QString& section);

} // namespace KsConfig
