#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>

namespace ks {

class ImportFilter : public QObject
{
    Q_OBJECT

public:
    static ImportFilter* instance();

    struct FilterDefinition {
        QString id;
        QString name;
        QStringList extensions;
        QString description;
    };

    void registerFilter(const FilterDefinition& filter);
    void unregisterFilter(const QString& filterId);

    void buildDefaults();

    QVector<FilterDefinition> getFilters() const;
    QVector<FilterDefinition> getFiltersForExtension(const QString& extension) const;

    void setCurrentFilter(const QString& filterId);
    QString getCurrentFilterId() const { return m_currentFilterId; }

    QString getDialogFilter() const;

    bool applyFilter(const QString& inputPath, const QString& outputPath, const QJsonObject& options);

signals:
    void filterApplied(const QString& filterId);
    void filterError(const QString& error);

private:
    ImportFilter(QObject* parent = nullptr);
    ~ImportFilter();
    Q_DISABLE_COPY(ImportFilter)

    static ImportFilter* s_instance;

    QString m_currentFilterId;
    QMap<QString, FilterDefinition> m_filters;
};

class ExportFilter : public QObject
{
    Q_OBJECT

public:
    static ExportFilter* instance();

    struct FilterDefinition {
        QString id;
        QString name;
        QStringList extensions;
        QString description;
        QJsonObject defaultOptions;
    };

    void registerFilter(const FilterDefinition& filter);
    void unregisterFilter(const QString& filterId);

    void buildDefaults();

    QVector<FilterDefinition> getFilters() const;
    QVector<FilterDefinition> getFiltersForExtension(const QString& extension) const;

    void setCurrentFilter(const QString& filterId);
    QString getCurrentFilterId() const { return m_currentFilterId; }

    QString getDialogFilter() const;

    bool applyFilter(const QString& inputPath, const QString& outputPath, const QJsonObject& options);

signals:
    void filterApplied(const QString& filterId);
    void filterError(const QString& error);

private:
    ExportFilter(QObject* parent = nullptr);
    ~ExportFilter();
    Q_DISABLE_COPY(ExportFilter)

    static ExportFilter* s_instance;

    QString m_currentFilterId;
    QMap<QString, FilterDefinition> m_filters;
};

class FilterSettings : public QObject
{
    Q_OBJECT

public:
    static FilterSettings* instance();

    void setDefaultOptions(const QString& filterId, const QJsonObject& options);
    QJsonObject getDefaultOptions(const QString& filterId) const;

    void setFilterOption(const QString& filterId, const QString& key, const QJsonValue& value);
    QJsonValue getFilterOption(const QString& filterId, const QString& key) const;

    void saveSettings();
    void loadSettings();

    void resetToDefaults(const QString& filterId);

signals:
    void settingsChanged(const QString& filterId);

private:
    FilterSettings(QObject* parent = nullptr);
    ~FilterSettings();
    Q_DISABLE_COPY(FilterSettings)

    static FilterSettings* s_instance;

    QMap<QString, QJsonObject> m_filterSettings;
};

} // namespace ks