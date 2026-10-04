#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QRegularExpression>
#include <QAbstractItemModel>
#include <QDate>
#include <QDateTime>

namespace ks {

enum class FilterType {
    Text,
    Number,
    Date,
    Enum,
    Range,
    Bool,
    Tag,
    Contains,
    Equals,
    StartsWith,
    EndsWith
};

struct FilterCondition {
    QString fieldId;
    FilterType type;
    QString value;
    QString value2;
    bool invert = false;
};

struct SearchResult {
    QString id;
    QString name;
    QString path;
    QString type;
    float relevance;
    QJsonObject metadata;
};

struct IndexEntry {
    QString id;
    QString name;
    QString path;
    QString type;
};

class SearchEngine : public QObject
{
    Q_OBJECT

public:
    static SearchEngine* instance();

    explicit SearchEngine(QObject* parent = nullptr);
    ~SearchEngine();

    void setSearchablePaths(const QStringList& paths);
    void addSearchablePath(const QString& path);
    void removeSearchablePath(const QString& path);

    void setSearchableFields(const QStringList& fields);
    void setSearchableExtensions(const QStringList& exts);

    void index(const QString& path);
    void reindex();
    void optimizeIndex();

    QVector<SearchResult> search(const QString& query, int maxResults = 50) const;
    QVector<SearchResult> searchAdvanced(const QVector<FilterCondition>& filters,
                                 int maxResults = 50) const;

    bool isIndexed() const { return m_indexed; }
    int getIndexedCount() const { return m_index.size(); }

    QVector<SearchResult> searchByFilter(const QVector<FilterCondition>& filters,
                                          int maxResults = 50) const;
    void removeFromIndex(const QString& id);
    void clearIndex();

signals:
    void indexingStarted();
    void indexingProgress(float progress);
    void indexingCompleted();
    void indexComplete(int count);

private:
    float calculateRelevance(const QString& id, const QString& query) const;

    QStringList m_paths;
    QStringList m_searchableFields;
    QStringList m_extensions;

    QMap<QString, IndexEntry> m_index;
    bool m_indexed = false;
};

class SearchFilter : public QObject
{
    Q_OBJECT

public:
    explicit SearchFilter(QObject* parent = nullptr);
    ~SearchFilter();

    void addCondition(const FilterCondition& condition);
    void removeCondition(const QString& fieldId);
    void clearConditions();

    QVector<FilterCondition> getConditions() const { return m_conditions; }

    void setSortField(const QString& field, bool ascending = true);
    QString getSortField() const { return m_sortField; }
    bool getSortAscending() const { return m_sortAscending; }

    void setLimit(int limit);
    int getLimit() const { return m_limit; }

    bool matches(const QJsonObject& item) const;

signals:
    void filterChanged();

private:
    bool evaluateCondition(const FilterCondition& condition, const QJsonObject& item) const;

    QVector<FilterCondition> m_conditions;
    QString m_sortField;
    bool m_sortAscending = true;
    int m_limit = 100;
};

class QuickFilter : public QObject
{
    Q_OBJECT

public:
    explicit QuickFilter(QObject* parent = nullptr);
    ~QuickFilter();

    void setTextFilter(const QString& text);
    QString getTextFilter() const { return m_textFilter; }

    void setTypeFilter(const QStringList& types);
    QStringList getTypeFilter() const { return m_typeFilter; }

    void setTagFilter(const QStringList& tags);
    QStringList getTagFilter() const { return m_tagFilter; }

    void setDateFilter(const QDate& from, const QDate& to);
    QDate getDateFrom() const { return m_dateFrom; }
    QDate getDateTo() const { return m_dateTo; }

    void setRated(bool rated);
    bool getRated() const { return m_rated; }

    void setFavorited(bool favorited);
    bool getFavorited() const { return m_favorited; }

    void clear();

    bool isActive() const;

    QString getDescription() const;

signals:
    void filterChanged();

private:
    QString m_textFilter;
    QStringList m_typeFilter;
    QStringList m_tagFilter;
    QDate m_dateFrom;
    QDate m_dateTo;
    bool m_rated = false;
    bool m_favorited = false;
};

class FilterProxyModel : public QObject
{
    Q_OBJECT

public:
    explicit FilterProxyModel(QObject* parent = nullptr);
    ~FilterProxyModel();

    void setSourceModel(QAbstractItemModel* model);
    QAbstractItemModel* getSourceModel() const { return m_sourceModel; }

    void setFilter(SearchFilter* filter);
    SearchFilter* getFilter() const { return m_filter; }

    void setQuickFilter(QuickFilter* filter);
    QuickFilter* getQuickFilter() const { return m_quickFilter; }

    QVector<int> getFilteredRows() const;
    int getFilteredCount() const;

    void invalidate();
    void refresh();

signals:
    void filterInvalidated();

private:
    QAbstractItemModel* m_sourceModel = nullptr;
    SearchFilter* m_filter = nullptr;
    QuickFilter* m_quickFilter = nullptr;
    QVector<int> m_filteredRows;
};

class SearchHistory : public QObject
{
    Q_OBJECT

public:
    explicit SearchHistory(QObject* parent = nullptr);
    ~SearchHistory();

    void addSearch(const QString& query, int resultCount);
    QStringList getRecent(int maxCount = 10) const;
    QStringList getPopular(int maxCount = 10) const;

    void clearHistory();
    void removeFromHistory(const QString& query);

    void setMaxHistory(int max);
    int getMaxHistory() const { return m_maxHistory; }

signals:
    void historyChanged();

private:
    struct SearchEntry {
        QString query;
        int count;
        int useCount;
        QDateTime lastUsed;
    };

    QVector<SearchEntry> m_history;
    int m_maxHistory = 50;
};

class SmartFilter : public QObject
{
    Q_OBJECT

public:
    explicit SmartFilter(QObject* parent = nullptr);
    ~SmartFilter();

    struct SmartFilterRule {
        QString id;
        QString name;
        QString condition;
        QJsonObject parameters;
        bool isEnabled = true;
    };

    void addRule(const SmartFilterRule& rule);
    void removeRule(const QString& ruleId);
    void updateRule(const SmartFilterRule& rule);

    QVector<SmartFilterRule> getRules() const { return m_rules; }
    SmartFilterRule getRule(const QString& ruleId) const;

    void setEnabled(const QString& ruleId, bool enabled);
    bool isEnabled(const QString& ruleId) const;

    void saveRules();
    void loadRules();

signals:
    void ruleAdded(const QString& ruleId);
    void ruleRemoved(const QString& ruleId);
    void ruleUpdated(const QString& ruleId);

private:
    QVector<SmartFilterRule> m_rules;
    QString m_nextRuleId;
};

} // namespace ks