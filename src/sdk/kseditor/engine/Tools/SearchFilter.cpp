#include "SearchFilter.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QStandardPaths>
#include <algorithm>

namespace ks {

static SearchEngine* s_instance = nullptr;

SearchEngine* SearchEngine::instance()
{
    if (!s_instance) s_instance = new SearchEngine();
    return s_instance;
}

SearchEngine::SearchEngine(QObject* parent) : QObject(parent) {}
SearchEngine::~SearchEngine() { s_instance = nullptr; }

void SearchEngine::setSearchablePaths(const QStringList& paths)
{
    m_paths = paths;
}

void SearchEngine::addSearchablePath(const QString& path)
{
    if (!m_paths.contains(path)) m_paths << path;
}

void SearchEngine::removeSearchablePath(const QString& path)
{
    m_paths.removeAll(path);
}

void SearchEngine::setSearchableExtensions(const QStringList& exts)
{
    m_extensions = exts;
}

void SearchEngine::index(const QString& path)
{
    QDirIterator it(path,
        m_extensions.isEmpty() ? QStringList{"*"} : m_extensions,
        QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fi(filePath);
        IndexEntry e;
        e.id   = filePath;
        e.name = fi.fileName();
        e.path = filePath;
        e.type = fi.suffix().toLower();
        m_index.insert(filePath, e);
    }
    m_indexed = true;
    emit indexComplete(m_index.size());
}

void SearchEngine::reindex()
{
    m_index.clear();
    for (const auto& path : m_paths) index(path);
}

void SearchEngine::optimizeIndex()
{
    // Remove entries for files that no longer exist
    QStringList toRemove;
    for (auto it = m_index.begin(); it != m_index.end(); ++it)
        if (!QFile::exists(it.key())) toRemove << it.key();
    for (const auto& k : toRemove) m_index.remove(k);
}

QVector<SearchResult> SearchEngine::search(const QString& query, int maxResults) const
{
    QVector<SearchResult> results;
    const QString q = query.toLower();
    if (q.isEmpty()) return results;

    for (const auto& e : m_index) {
        if (e.name.toLower().contains(q) || e.path.toLower().contains(q)) {
            SearchResult r;
            r.id   = e.id;
            r.name = e.name;
            r.path = e.path;
            r.type = e.type;
            // Relevance: starts-with scores higher
            r.relevance = e.name.toLower().startsWith(q) ? 2.0f : 1.0f;
            results << r;
            if (results.size() >= maxResults) break;
        }
    }

    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b){
        return a.relevance > b.relevance;
    });
    return results;
}

QVector<SearchResult> SearchEngine::searchByFilter(const QVector<FilterCondition>& filters,
                                                    int maxResults) const
{
    QVector<SearchResult> results;
    for (const auto& e : m_index) {
        bool match = true;
        for (const auto& f : filters) {
            QString val;
            if      (f.fieldId == "name") val = e.name;
            else if (f.fieldId == "type") val = e.type;
            else if (f.fieldId == "path") val = e.path;

            bool cond = false;
            switch (f.type) {
            case FilterType::Contains:   cond = val.contains(f.value, Qt::CaseInsensitive); break;
            case FilterType::Equals:     cond = val.compare(f.value, Qt::CaseInsensitive) == 0; break;
            case FilterType::StartsWith: cond = val.startsWith(f.value, Qt::CaseInsensitive); break;
            case FilterType::EndsWith:   cond = val.endsWith(f.value, Qt::CaseInsensitive); break;
            default: cond = true; break;
            }
            if (f.invert) cond = !cond;
            if (!cond) { match = false; break; }
        }
        if (match) {
            SearchResult r;
            r.id = r.path = e.path;
            r.name = e.name;
            r.type = e.type;
            results << r;
            if (results.size() >= maxResults) break;
        }
    }
    return results;
}

void SearchEngine::removeFromIndex(const QString& id)
{
    m_index.remove(id);
}

void SearchEngine::setSearchableFields(const QStringList& fields) { m_searchableFields = fields; }
float SearchEngine::calculateRelevance(const QString& id, const QString& query) const
{
    if (!m_index.contains(id) || query.isEmpty()) return 0.0f;

    const IndexEntry& entry = m_index[id];
    float score = 0.0f;
    QString queryLower = query.toLower();
    QString nameLower = entry.name.toLower();
    QString pathLower = entry.path.toLower();

    // Exact name match = highest score
    if (nameLower == queryLower) return 1.0f;

    // Name starts with query
    if (nameLower.startsWith(queryLower)) score = 0.9f;

    // Name contains query
    else if (nameLower.contains(queryLower)) score = 0.7f;

    // Path contains query
    else if (pathLower.contains(queryLower)) score = 0.5f;

    // Partial word match (split query by spaces)
    else {
        QStringList words = queryLower.split(' ', Qt::SkipEmptyParts);
        int matches = 0;
        for (const auto& word : words) {
            if (nameLower.contains(word) || pathLower.contains(word))
                matches++;
        }
        if (!words.isEmpty())
            score = 0.3f * static_cast<float>(matches) / static_cast<float>(words.size());
    }

    // Boost for type matches
    if (m_extensions.isEmpty()) return score;
    for (const auto& ext : m_extensions) {
        if (entry.path.endsWith("." + ext, Qt::CaseInsensitive)) {
            score = qMin(1.0f, score + 0.1f);
            break;
        }
    }

    return score;
}

QVector<SearchResult> SearchEngine::searchAdvanced(const QVector<FilterCondition>& filters, int maxResults) const
{
    return searchByFilter(filters, maxResults);
}

void SearchEngine::clearIndex() { m_index.clear(); m_indexed = false; }

// ─── SearchFilter ────────────────────────────────────────────────────────────

SearchFilter::SearchFilter(QObject* parent) : QObject(parent) {}
SearchFilter::~SearchFilter() = default;

void SearchFilter::addCondition(const FilterCondition& condition) { m_conditions.append(condition); emit filterChanged(); }
void SearchFilter::removeCondition(const QString& fieldId) { m_conditions.erase(std::remove_if(m_conditions.begin(), m_conditions.end(), [&](const FilterCondition& c){ return c.fieldId == fieldId; }), m_conditions.end()); emit filterChanged(); }
void SearchFilter::clearConditions() { m_conditions.clear(); emit filterChanged(); }
void SearchFilter::setSortField(const QString& field, bool ascending) { m_sortField = field; m_sortAscending = ascending; }
void SearchFilter::setLimit(int limit) { m_limit = limit; }

bool SearchFilter::matches(const QJsonObject& item) const
{
    for (const auto& cond : m_conditions)
        if (!evaluateCondition(cond, item)) return false;
    return true;
}

bool SearchFilter::evaluateCondition(const FilterCondition& condition, const QJsonObject& item) const
{
    if (!item.contains(condition.fieldId)) return condition.invert;

    QJsonValue fieldValue = item[condition.fieldId];
    bool result = false;

    switch (condition.type) {
    case FilterType::Text:
    case FilterType::Contains:
        result = fieldValue.toString().contains(condition.value, Qt::CaseInsensitive);
        break;

    case FilterType::Equals:
        result = (fieldValue.toString().compare(condition.value, Qt::CaseInsensitive) == 0);
        break;

    case FilterType::StartsWith:
        result = fieldValue.toString().startsWith(condition.value, Qt::CaseInsensitive);
        break;

    case FilterType::EndsWith:
        result = fieldValue.toString().endsWith(condition.value, Qt::CaseInsensitive);
        break;

    case FilterType::Number: {
        double val = fieldValue.toDouble();
        double target = condition.value.toDouble();
        result = qFuzzyCompare(val, target);
        break;
    }

    case FilterType::Range: {
        double val = fieldValue.toDouble();
        double min = condition.value.toDouble();
        double max = condition.value2.toDouble();
        result = (val >= min && val <= max);
        break;
    }

    case FilterType::Bool:
        result = (fieldValue.toBool() == static_cast<bool>(condition.value.toInt()));
        break;

    case FilterType::Date: {
        QDate date = QDate::fromString(fieldValue.toString(), Qt::ISODate);
        QDate from = QDate::fromString(condition.value, Qt::ISODate);
        QDate to = QDate::fromString(condition.value2, Qt::ISODate);
        result = date.isValid() && (!from.isValid() || date >= from) && (!to.isValid() || date <= to);
        break;
    }

    case FilterType::Enum: {
        QStringList allowed = condition.value.split(',', Qt::SkipEmptyParts);
        result = allowed.contains(fieldValue.toString(), Qt::CaseInsensitive);
        break;
    }

    case FilterType::Tag: {
        QStringList tags;
        if (fieldValue.isArray()) {
            for (const auto& v : fieldValue.toArray()) tags.append(v.toString());
        } else {
            tags = fieldValue.toString().split(',', Qt::SkipEmptyParts);
        }
        QStringList required = condition.value.split(',', Qt::SkipEmptyParts);
        for (const auto& req : required) {
            if (!tags.contains(req.trimmed(), Qt::CaseInsensitive))
                return condition.invert;
        }
        result = true;
        break;
    }
    }

    return condition.invert ? !result : result;
}

// ─── QuickFilter ─────────────────────────────────────────────────────────────

QuickFilter::QuickFilter(QObject* parent) : QObject(parent) {}
QuickFilter::~QuickFilter() = default;

void QuickFilter::setTextFilter(const QString& text) { m_textFilter = text; emit filterChanged(); }
void QuickFilter::setTypeFilter(const QStringList& types) { m_typeFilter = types; emit filterChanged(); }
void QuickFilter::setTagFilter(const QStringList& tags) { m_tagFilter = tags; emit filterChanged(); }
void QuickFilter::setDateFilter(const QDate& from, const QDate& to) { m_dateFrom = from; m_dateTo = to; emit filterChanged(); }
void QuickFilter::setRated(bool rated) { m_rated = rated; emit filterChanged(); }
void QuickFilter::setFavorited(bool favorited) { m_favorited = favorited; emit filterChanged(); }

void QuickFilter::clear()
{
    m_textFilter.clear();
    m_typeFilter.clear();
    m_tagFilter.clear();
    m_rated = false;
    m_favorited = false;
    emit filterChanged();
}

bool QuickFilter::isActive() const
{
    return !m_textFilter.isEmpty() || !m_typeFilter.isEmpty() || !m_tagFilter.isEmpty() || m_rated || m_favorited;
}

QString QuickFilter::getDescription() const
{
    if (m_textFilter.isEmpty()) return {};
    return m_textFilter;
}

// ─── FilterProxyModel ────────────────────────────────────────────────────────

FilterProxyModel::FilterProxyModel(QObject* parent) : QObject(parent) {}
FilterProxyModel::~FilterProxyModel() = default;

void FilterProxyModel::setSourceModel(QAbstractItemModel* model) { m_sourceModel = model; }
void FilterProxyModel::setFilter(SearchFilter* filter) { m_filter = filter; }
void FilterProxyModel::setQuickFilter(QuickFilter* filter) { m_quickFilter = filter; }
QVector<int> FilterProxyModel::getFilteredRows() const { return m_filteredRows; }
int FilterProxyModel::getFilteredCount() const { return m_filteredRows.size(); }
void FilterProxyModel::invalidate() { emit filterInvalidated(); }
void FilterProxyModel::refresh() { invalidate(); }

// ─── SearchHistory ───────────────────────────────────────────────────────────

SearchHistory::SearchHistory(QObject* parent) : QObject(parent) {}
SearchHistory::~SearchHistory() = default;

void SearchHistory::addSearch(const QString& query, int resultCount)
{
    for (auto& e : m_history) {
        if (e.query == query) {
            e.useCount++;
            e.lastUsed = QDateTime::currentDateTime();
            return;
        }
    }
    SearchEntry entry;
    entry.query = query;
    entry.count = resultCount;
    entry.useCount = 1;
    entry.lastUsed = QDateTime::currentDateTime();
    m_history.append(entry);
    if (m_history.size() > m_maxHistory) m_history.removeFirst();
    emit historyChanged();
}

QStringList SearchHistory::getRecent(int maxCount) const
{
    QStringList result;
    for (int i = m_history.size() - 1; i >= 0 && result.size() < maxCount; --i)
        result << m_history[i].query;
    return result;
}

QStringList SearchHistory::getPopular(int maxCount) const
{
    QList<QPair<int, QString>> sorted;
    for (const auto& e : m_history)
        sorted.append({e.useCount, e.query});
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b){ return a.first > b.first; });
    QStringList result;
    for (int i = 0; i < qMin(maxCount, sorted.size()); ++i)
        result << sorted[i].second;
    return result;
}

void SearchHistory::clearHistory() { m_history.clear(); emit historyChanged(); }
void SearchHistory::removeFromHistory(const QString& query) { m_history.erase(std::remove_if(m_history.begin(), m_history.end(), [&](const SearchEntry& e){ return e.query == query; }), m_history.end()); emit historyChanged(); }
void SearchHistory::setMaxHistory(int max) { m_maxHistory = max; }

// ─── SmartFilter ─────────────────────────────────────────────────────────────

SmartFilter::SmartFilter(QObject* parent) : QObject(parent) {}
SmartFilter::~SmartFilter() = default;

void SmartFilter::addRule(const SmartFilterRule& rule) { m_rules.append(rule); emit ruleAdded(rule.id); }
void SmartFilter::removeRule(const QString& ruleId) { m_rules.erase(std::remove_if(m_rules.begin(), m_rules.end(), [&](const SmartFilterRule& r){ return r.id == ruleId; }), m_rules.end()); emit ruleRemoved(ruleId); }
void SmartFilter::updateRule(const SmartFilterRule& rule)
{
    for (auto& r : m_rules) { if (r.id == rule.id) { r = rule; emit ruleUpdated(rule.id); return; } }
}

SmartFilter::SmartFilterRule SmartFilter::getRule(const QString& ruleId) const
{
    for (const auto& r : m_rules) if (r.id == ruleId) return r;
    return {};
}

void SmartFilter::setEnabled(const QString& ruleId, bool enabled)
{
    for (auto& r : m_rules) { if (r.id == ruleId) { r.isEnabled = enabled; emit ruleUpdated(ruleId); return; } }
}

bool SmartFilter::isEnabled(const QString& ruleId) const
{
    for (const auto& r : m_rules) if (r.id == ruleId) return r.isEnabled;
    return false;
}

void SmartFilter::saveRules() {
    QJsonArray arr;
    for (const auto& rule : m_rules) {
        QJsonObject obj;
        obj["id"] = rule.id;
        obj["name"] = rule.name;
        obj["condition"] = rule.condition;
        obj["parameters"] = rule.parameters;
        obj["isEnabled"] = rule.isEnabled;
        arr.append(obj);
    }
    QJsonObject root;
    root["rules"] = arr;
    QJsonDocument doc(root);
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/smart_filters.json";
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
    }
}

void SmartFilter::loadRules() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/smart_filters.json";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;
    m_rules.clear();
    QJsonArray arr = doc.object()["rules"].toArray();
    for (const auto& v : arr) {
        QJsonObject obj = v.toObject();
        SmartFilterRule rule;
        rule.id = obj["id"].toString();
        rule.name = obj["name"].toString();
        rule.condition = obj["condition"].toString();
        rule.parameters = obj["parameters"].toObject();
        rule.isEnabled = obj["isEnabled"].toBool(true);
        m_rules.append(rule);
    }
}

} // namespace ks
