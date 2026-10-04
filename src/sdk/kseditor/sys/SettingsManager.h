#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QMap>

class SettingsManager : public QObject
{
    Q_OBJECT

public:
    explicit SettingsManager(QObject* parent = nullptr);
    ~SettingsManager() override;

    // Value access
    QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void setValue(const QString& key, const QVariant& value);

    // Convenience methods
    QString string(const QString& key, const QString& defaultValue = QString()) const;
    int integer(const QString& key, int defaultValue = 0) const;
    bool boolean(const QString& key, bool defaultValue = false) const;
    double real(const QString& key, double defaultValue = 0.0) const;

    // Group management
    void beginGroup(const QString& group);
    void endGroup();
    QString group() const;

    // Key management
    bool contains(const QString& key) const;
    void remove(const QString& key);
    QStringList childKeys() const;
    QStringList childGroups() const;

    // Reset
    void reset(const QString& key);
    void resetAll();
    void resetToDefaults();

    // Override support (temporary, not persisted)
    void setTemporaryOverride(const QString& key, const QVariant& value);
    void clearOverride(const QString& key);

    // Persistence
    void sync();
    QString fileName() const;

    // Import/Export
    bool exportToFile(const QString& path) const;
    bool importFromFile(const QString& path);

signals:
    void settingChanged(const QString& key, const QVariant& value);
    void groupChanged(const QString& group);
    void settingsReset();

private:
    void setupDefaults();
    void applyDefaults();

    QSettings* m_settings;
    QString m_currentGroup;
    QMap<QString, QVariant> m_overrides;
};

SettingsManager* globalSettings();
