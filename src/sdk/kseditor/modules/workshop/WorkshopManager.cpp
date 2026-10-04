#include "WorkshopManager.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QFileInfo>
#include <QDirIterator>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

namespace ks {

// ── DepVersion ─────────────────────────────────────────────────────────────

DepVersion DepVersion::fromString(const QString& str) {
    DepVersion v;
    QString s = str.trimmed();
    if (s.startsWith('v', Qt::CaseInsensitive)) s = s.mid(1);
    int preIdx = s.indexOf('-');
    if (preIdx >= 0) {
        v.preRelease = s.mid(preIdx + 1);
        s = s.left(preIdx);
    }
    QStringList parts = s.split('.');
    if (parts.size() > 0) v.major = parts[0].toInt();
    if (parts.size() > 1) v.minor = parts[1].toInt();
    if (parts.size() > 2) v.patch = parts[2].toInt();
    return v;
}

int DepVersion::compare(const DepVersion& other) const {
    if (major != other.major) return major - other.major;
    if (minor != other.minor) return minor - other.minor;
    if (patch != other.patch) return patch - other.patch;
    if (preRelease.isEmpty() && !other.preRelease.isEmpty()) return 1;
    if (!preRelease.isEmpty() && other.preRelease.isEmpty()) return -1;
    return preRelease.compare(other.preRelease);
}

QString DepVersion::toString() const {
    QString s = QString("%1.%2.%3").arg(major).arg(minor).arg(patch);
    if (!preRelease.isEmpty()) s += "-" + preRelease;
    return s;
}

// ── WorkshopVersionSpec ────────────────────────────────────────────────────

WorkshopVersionSpec WorkshopVersionSpec::fromString(const QString& spec) {
    WorkshopVersionSpec vs;
    vs.raw = spec.trimmed();
    QString s = vs.raw;

    if (s == "*" || s.isEmpty()) {
        vs.op = Any;
        return vs;
    }
    if (s.startsWith(">=")) { vs.op = Ge; s = s.mid(2); }
    else if (s.startsWith("<=")) { vs.op = Le; s = s.mid(2); }
    else if (s.startsWith("~>")) { vs.op = Tilde; s = s.mid(2); }
    else if (s.startsWith("^")) { vs.op = Caret; s = s.mid(1); }
    else if (s.startsWith(">")) { vs.op = Gt; s = s.mid(1); }
    else if (s.startsWith("<")) { vs.op = Lt; s = s.mid(1); }
    else if (s.startsWith("!=")) { vs.op = Neq; s = s.mid(2); }
    else if (s.startsWith("==")) { vs.op = Eq; s = s.mid(2); }
    else { vs.op = Eq; }

    vs.version = DepVersion::fromString(s.trimmed());
    return vs;
}

bool WorkshopVersionSpec::matches(const DepVersion& v) const {
    switch (op) {
        case Any:   return true;
        case Eq:    return version.compare(v) == 0;
        case Neq:   return version.compare(v) != 0;
        case Gt:    return version.compare(v) < 0;
        case Lt:    return version.compare(v) > 0;
        case Ge:    return version.compare(v) <= 0;
        case Le:    return version.compare(v) >= 0;
        case Tilde: return version.compare(v) <= 0 && DepVersion{ version.major, version.minor + 1, 0 }.compare(v) > 0;
        case Caret: {
            if (version.major != 0)
                return version.compare(v) <= 0 && DepVersion{ version.major + 1, 0, 0 }.compare(v) > 0;
            if (version.minor != 0)
                return version.compare(v) <= 0 && DepVersion{ 0, version.minor + 1, 0 }.compare(v) > 0;
            return version.compare(v) <= 0 && DepVersion{ 0, 0, version.patch + 1 }.compare(v) > 0;
        }
    }
    return false;
}

// ── Dependency parsing helpers (anonymous) ────────────────────────────────

static QPair<QString, WorkshopVersionSpec> parseDepString(const QString& dep) {
    static QRegularExpression re(R"(^(\S+)\s*\(([^)]+)\)\s*$)");
    auto m = re.match(dep.trimmed());
    if (m.hasMatch())
        return { m.captured(1), WorkshopVersionSpec::fromString(m.captured(2).trimmed()) };
    return { dep.trimmed(), WorkshopVersionSpec::fromString("*") };
}

// ── WorkshopManager ───────────────────────────────────────────────────────

WorkshopManager* WorkshopManager::s_instance = nullptr;

WorkshopManager* WorkshopManager::instance() {
    if (!s_instance) {
        s_instance = new WorkshopManager();
    }
    return s_instance;
}

void WorkshopManager::destroyInstance() {
    if (s_instance) {
        s_instance->m_items.clear();
        s_instance->m_itemIndex.clear();
        delete s_instance;
        s_instance = nullptr;
    }
}

WorkshopManager::WorkshopManager(QObject* parent)
    : QObject(parent)
{
    m_dataDir = QDir::homePath() + "/ksEditor/workshop";
    m_dbPath = m_dataDir + "/workshop_db.json";
    m_packagesDir = m_dataDir + "/packages";
    m_installedDir = m_dataDir + "/installed";
    m_profilesDir = m_dataDir + "/profiles";

    QDir().mkpath(m_dataDir);
    QDir().mkpath(m_packagesDir);
    QDir().mkpath(m_installedDir);
    QDir().mkpath(m_profilesDir);

    loadDatabase();
    loadProfiles();
}

void WorkshopManager::reset() {
    if (!m_dbPath.isEmpty()) {
        QFile::remove(m_dbPath);
    }
    m_items.clear();
    m_itemIndex.clear();
    m_dataDir.clear();
    m_dbPath.clear();
    m_packagesDir.clear();
    m_installedDir.clear();
    m_profiles.clear();
    m_activeProfile = "default";
}

void WorkshopManager::setDataDir(const QString& dir) {
    // Save current state to old path before switching (skip if reset or first init)
    QString prevDbPath = m_dbPath;
    QString prevDataDir = m_dataDir;

    m_dataDir = dir;
    m_dbPath = dir + "/workshop_db.json";
    m_packagesDir = dir + "/packages";
    m_installedDir = dir + "/installed";
    m_profilesDir = dir + "/profiles";

    QDir().mkpath(m_dataDir);
    QDir().mkpath(m_packagesDir);
    QDir().mkpath(m_installedDir);
    QDir().mkpath(m_profilesDir);

    // Save to previous location only if it was a real path (not reset-empty)
    if (!prevDbPath.isEmpty() && QDir(prevDataDir).exists()) {
        QJsonArray arr;
        for (const auto& item : m_items) arr.append(item.toJson());
        QJsonObject root;
        root["items"] = arr;
        root["updated"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root["format_version"] = "1.0";
        writeJson(prevDbPath, QJsonDocument(root));
    }

    // Load from new directory (if no DB exists, start fresh)
    m_items.clear();
    rebuildIndex();
    QJsonDocument doc = readJson(m_dbPath);
    if (!doc.isNull()) {
        QJsonObject root = doc.object();
        QJsonArray arr = root["items"].toArray();
        for (const auto& v : arr) {
            m_items.append(WorkshopItem::fromJson(v.toObject()));
        }
        rebuildIndex();
        emit databaseLoaded();
    }
}

void WorkshopManager::rebuildIndex() {
    m_itemIndex.clear();
    for (int i = 0; i < m_items.size(); ++i) {
        m_itemIndex[m_items[i].id] = i;
    }
}

int WorkshopManager::findItem(const QString& id) const {
    auto it = m_itemIndex.find(id);
    return it != m_itemIndex.end() ? it.value() : -1;
}

bool WorkshopManager::writeJson(const QString& path, const QJsonDocument& doc) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR("WorkshopManager", "Failed to write: " + path);
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QJsonDocument WorkshopManager::readJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonDocument();
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    return doc;
}

static bool copyDirectoryContents(const QString& src, const QString& dst) {
    QDir srcDir(src);
    if (!srcDir.exists()) return false;
    QDir dstDir(dst);
    if (!dstDir.exists()) dstDir.mkpath(".");

    for (const auto& info : srcDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString target = dstDir.filePath(info.fileName());
        if (info.isDir()) {
            QDir().mkpath(target);
            if (!copyDirectoryContents(info.absoluteFilePath(), target))
                return false;
        } else {
            if (!QFile::copy(info.absoluteFilePath(), target))
                return false;
        }
    }
    return true;
}

WorkshopManager::PackageResult WorkshopManager::createWorkshopPackage(
    const QString& sourcePath, const QString& outputDir, const WorkshopItem& item)
{
    PackageResult result;
    QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists()) {
        result.errors.append("Source path does not exist: " + sourcePath);
        return result;
    }

    QDir outDir(outputDir);
    if (!outDir.exists()) outDir.mkpath(".");

    QString dirName = item.name + "_v" + item.version;
    dirName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString packagePath = outDir.filePath(dirName);

    emit packagingProgress("Creating package...");

    if (QFileInfo(packagePath).exists()) {
        QDir(packagePath).removeRecursively();
    }

    if (srcInfo.isDir()) {
        if (!copyDirectoryContents(sourcePath, packagePath)) {
            result.errors.append("Failed to copy mod files");
            emit packagingError("Failed to copy mod files");
            return result;
        }
    } else {
        QDir().mkpath(packagePath);
        QString destFile = QDir(packagePath).filePath(srcInfo.fileName());
        if (!QFile::copy(sourcePath, destFile)) {
            result.errors.append("Failed to copy mod file");
            emit packagingError("Failed to copy mod file");
            return result;
        }
    }

    QJsonObject manifest;
    manifest["format_version"] = "1.0";
    manifest["id"] = item.id.isEmpty() ? WorkshopItem::generateId() : item.id;
    manifest["name"] = item.name;
    manifest["version"] = item.version;
    manifest["author"] = item.author;
    manifest["description"] = item.description;
    manifest["category"] = item.category;
    QJsonArray tags;
    for (const auto& t : item.tags) tags.append(t);
    manifest["tags"] = tags;
    QJsonArray deps;
    for (const auto& d : item.dependencies) deps.append(d);
    manifest["dependencies"] = deps;
    QJsonArray confs;
    for (const auto& c : item.conflicts) confs.append(c);
    manifest["conflicts"] = confs;
    manifest["license"] = item.license;
    manifest["website"] = item.website;
    manifest["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QString manifestPath = QDir(packagePath).filePath("workshop_manifest.json");
    QFile mf(manifestPath);
    if (mf.open(QIODevice::WriteOnly)) {
        mf.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
        mf.close();
    }

    result.success = true;
    result.packagePath = packagePath;
    emit packagingFinished(packagePath);
    return result;
}

WorkshopManager::InstallResult WorkshopManager::installPackage(
    const QString& packagePath, const QString& installDir, bool autoInstallDeps)
{
    InstallResult result;
    QFileInfo pkgInfo(packagePath);
    if (!pkgInfo.exists()) {
        result.errors.append("Package not found: " + packagePath);
        return result;
    }

    // Read manifest if present to check dependencies
    QString manifestPath = pkgInfo.isDir()
        ? QDir(packagePath).filePath("workshop_manifest.json")
        : QString();

    WorkshopItem manifestItem;
    if (!manifestPath.isEmpty()) {
        QJsonDocument doc = readJson(manifestPath);
        if (!doc.isNull()) {
            manifestItem = WorkshopItem::fromJson(doc.object());
            if (!manifestItem.dependencies.isEmpty()) {
                DepResolution depRes = resolveDependencies(manifestItem);
                for (const auto& dep : depRes.dependencies) {
                    if (!dep.satisfied) {
                        if (autoInstallDeps) {
                            // Try to find and install the dependency automatically
                            QVector<WorkshopItem> providers = findDependencyProviders(dep.depName);
                            for (const auto& provider : providers) {
                                DepVersion pv = DepVersion::fromString(provider.version);
                                if (!dep.spec.matches(pv))
                                    continue;
                                if (provider.packagePath.isEmpty())
                                    continue;
                                QFileInfo depPkg(provider.packagePath);
                                if (!depPkg.exists())
                                    continue;
                                InstallResult depResult = installPackage(
                                    provider.packagePath, installDir, true);
                                if (depResult.success) {
                                    result.autoInstalledDeps << provider.name + " v" + provider.version;
                                    // Mark as installed
                                    setInstalled(provider.id, true);
                                } else {
                                    result.errors << "Auto-install failed for dep: " + dep.depName
                                                  + " - " + depResult.errors.join("; ");
                                }
                                break;
                            }
                        } else {
                            result.errors << "Missing dependency: " + dep.depName;
                        }
                    }
                }
                for (const auto& c : depRes.conflictingItems)
                    result.errors << "Conflict: " + c;
                for (const auto& w : result.errors)
                    LOG_WARNING("WorkshopManager", w);
            }
        }
    }

    QDir destDir(installDir);
    if (!destDir.exists()) destDir.mkpath(".");

    QString itemName = pkgInfo.completeBaseName();
    if (itemName.contains("_v")) {
        itemName = itemName.left(itemName.lastIndexOf("_v"));
    }

    QString installPath = destDir.filePath(itemName);
    if (QFileInfo(installPath).exists()) {
        QString backupDir = installPath + ".bak." + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QDir().rename(installPath, backupDir);
        LOG_INFO("WorkshopManager", "Backed up existing install to: " + backupDir);
    }

    if (pkgInfo.isDir()) {
        if (!copyDirectoryContents(packagePath, installPath)) {
            result.errors.append("Failed to copy package directory");
            return result;
        }
    } else {
        QDir().mkpath(installPath);
        if (!QFile::copy(packagePath, QDir(installPath).filePath(pkgInfo.fileName()))) {
            result.errors.append("Failed to copy package file");
            return result;
        }
    }

    result.success = true;
    result.modName = itemName;
    result.installPath = installPath;
    return result;
}

bool WorkshopManager::publishItem(const WorkshopItem& item, const QString& packagePath) {
    WorkshopItem newItem = item;

    if (newItem.id.isEmpty()) {
        newItem.id = WorkshopItem::generateId();
    }

    newItem.packagePath = packagePath;
    newItem.createdAt = QDateTime::currentDateTime();
    newItem.updatedAt = newItem.createdAt;

    QFileInfo pkgInfo(packagePath);
    if (pkgInfo.exists()) {
        newItem.fileSize = pkgInfo.size();
    }

    int idx = findItem(newItem.id);
    if (idx >= 0) {
        m_items[idx] = newItem;
    } else {
        m_items.append(newItem);
    }

    rebuildIndex();
    saveDatabase();
    emit itemPublished(newItem.id);
    return true;
}

bool WorkshopManager::removeItem(const QString& id) {
    int idx = findItem(id);
    if (idx < 0) return false;

    m_items.removeAt(idx);
    rebuildIndex();
    saveDatabase();
    emit itemRemoved(id);
    return true;
}

QVector<WorkshopItem> WorkshopManager::browse(const BrowseQuery& query) {
    QVector<WorkshopItem> results = filterItems(query);

    if (query.sortBy == "name") {
        std::sort(results.begin(), results.end(), [&](const WorkshopItem& a, const WorkshopItem& b) {
            return query.ascending ? a.name.compare(b.name, Qt::CaseInsensitive) < 0
                                   : a.name.compare(b.name, Qt::CaseInsensitive) > 0;
        });
    } else if (query.sortBy == "date") {
        std::sort(results.begin(), results.end(), [&](const WorkshopItem& a, const WorkshopItem& b) {
            return query.ascending ? a.updatedAt < b.updatedAt : a.updatedAt > b.updatedAt;
        });
    } else if (query.sortBy == "rating") {
        std::sort(results.begin(), results.end(), [&](const WorkshopItem& a, const WorkshopItem& b) {
            return query.ascending ? a.rating < b.rating : a.rating > b.rating;
        });
    } else if (query.sortBy == "downloads") {
        std::sort(results.begin(), results.end(), [&](const WorkshopItem& a, const WorkshopItem& b) {
            return query.ascending ? a.downloadCount < b.downloadCount : a.downloadCount > b.downloadCount;
        });
    }

    if (query.maxResults > 0 && results.size() > query.maxResults) {
        results.resize(query.maxResults);
    }

    return results;
}

WorkshopItem WorkshopManager::getItem(const QString& id) const {
    int idx = findItem(id);
    return idx >= 0 ? m_items[idx] : WorkshopItem();
}

QVector<WorkshopItem> WorkshopManager::getPublishedItems() const {
    return m_items;
}

QVector<WorkshopItem> WorkshopManager::getInstalledItems() const {
    QVector<WorkshopItem> installed;
    for (const auto& item : m_items) {
        if (item.isInstalled) installed.append(item);
    }
    return installed;
}

QVector<WorkshopItem> WorkshopManager::getItemsByAuthor(const QString& author) const {
    QVector<WorkshopItem> results;
    for (const auto& item : m_items) {
        if (item.author.compare(author, Qt::CaseInsensitive) == 0) {
            results.append(item);
        }
    }
    return results;
}

QVector<WorkshopItem> WorkshopManager::search(const QString& text) const {
    if (text.isEmpty()) return m_items;

    QVector<WorkshopItem> results;
    for (const auto& item : m_items) {
        if (item.name.contains(text, Qt::CaseInsensitive) ||
            item.description.contains(text, Qt::CaseInsensitive) ||
            item.author.contains(text, Qt::CaseInsensitive) ||
            item.category.contains(text, Qt::CaseInsensitive))
        {
            results.append(item);
        }
    }
    return results;
}

bool WorkshopManager::rateItem(const QString& id, int rating) {
    if (rating < 1 || rating > 5) return false;
    int idx = findItem(id);
    if (idx < 0) return false;

    auto& item = m_items[idx];
    float total = item.rating * item.ratingCount + rating;
    item.ratingCount++;
    item.rating = total / item.ratingCount;

    saveDatabase();
    return true;
}

bool WorkshopManager::setInstalled(const QString& id, bool installed) {
    int idx = findItem(id);
    if (idx < 0) return false;

    m_items[idx].isInstalled = installed;
    if (installed) {
        m_items[idx].downloadCount++;
        emit itemInstalled(id);
    } else {
        emit itemUninstalled(id);
    }

    saveDatabase();
    return true;
}

// ── Dependency resolution ─────────────────────────────────────────────────

DepResolution WorkshopManager::resolveDependencies(const QString& itemId) {
    int idx = findItem(itemId);
    if (idx < 0) {
        DepResolution res;
        res.allSatisfied = false;
        res.missingDeps << "(item not found: " + itemId + ")";
        return res;
    }
    return resolveDependencies(m_items[idx]);
}

DepResolution WorkshopManager::resolveDependencies(const WorkshopItem& item) {
    DepResolution result;

    for (const QString& depStr : item.dependencies) {
        auto parsed = parseDepString(depStr);
        const QString& depName = parsed.first;
        const WorkshopVersionSpec& spec = parsed.second;

        DepInfo dep;
        dep.depName = depName;
        dep.spec = spec;

        QVector<WorkshopItem> providers = findDependencyProviders(depName);
        for (const auto& provider : providers) {
            DepVersion pv = DepVersion::fromString(provider.version);
            if (spec.matches(pv)) {
                dep.satisfied = true;
                dep.resolvedBy = provider.name;
                dep.resolvedVersion = provider.version;
                break;
            }
        }

        if (!dep.satisfied) {
            result.allSatisfied = false;
            result.missingDeps << depStr;
        }

        result.dependencies.append(dep);
    }

    // Check conflicts
    for (const QString& conflict : item.conflicts) {
        QVector<WorkshopItem> conflicting = findDependencyProviders(conflict);
        for (const auto& c : conflicting) {
            if (c.isInstalled) {
                result.conflictingItems << c.name + " (v" + c.version + ")";
                result.allSatisfied = false;
            }
        }
    }

    emit dependenciesResolved(item.id, result.allSatisfied);
    return result;
}

bool WorkshopManager::areDependenciesSatisfied(const QString& itemId) {
    return resolveDependencies(itemId).allSatisfied;
}

QVector<WorkshopItem> WorkshopManager::findDependencyProviders(const QString& depName) const {
    QVector<WorkshopItem> results;
    for (const auto& item : m_items) {
        if (item.name.compare(depName, Qt::CaseInsensitive) == 0)
            results.append(item);
    }
    return results;
}

// ── Update checking ────────────────────────────────────────────────────────

QVector<WorkshopManager::UpdateInfo> WorkshopManager::checkForUpdates() {
    QVector<UpdateInfo> updates;
    for (const auto& item : m_items) {
        if (!item.isInstalled) continue;
        UpdateInfo info;
        info.itemId = item.id;
        info.name = item.name;
        info.currentVersion = item.version;
        info.availableVersion = item.version;

        // Look for a newer version among published items with the same name
        QVector<WorkshopItem> sameName = findDependencyProviders(item.name);
        DepVersion current = DepVersion::fromString(item.version);
        DepVersion best = current;

        for (const auto& candidate : sameName) {
            if (candidate.id == item.id) continue;
            DepVersion cv = DepVersion::fromString(candidate.version);
            if (cv.compare(current) > 0 && cv.compare(best) > 0) {
                best = cv;
                info.availableVersion = candidate.version;
            }
        }

        if (best.compare(current) > 0) {
            updates.append(info);
            emit updateAvailable(item.id, info.availableVersion);
        }
    }
    return updates;
}

bool WorkshopManager::hasUpdate(const QString& itemId) const {
    int idx = findItem(itemId);
    if (idx < 0) return false;
    const auto& item = m_items[idx];
    if (!item.isInstalled) return false;

    DepVersion current = DepVersion::fromString(item.version);
    QVector<WorkshopItem> sameName = findDependencyProviders(item.name);
    for (const auto& candidate : sameName) {
        if (candidate.id == itemId) continue;
        if (DepVersion::fromString(candidate.version).compare(current) > 0)
            return true;
    }
    return false;
}

bool WorkshopManager::updateItem(const QString& itemId, const QString& newVersion, const QString& packagePath) {
    int idx = findItem(itemId);
    if (idx < 0) return false;

    WorkshopItem& item = m_items[idx];
    QString oldVersion = item.version;

    item.version = newVersion;
    item.updatedAt = QDateTime::currentDateTime();
    item.packagePath = packagePath;

    QFileInfo pkgInfo(packagePath);
    if (pkgInfo.exists()) {
        item.fileSize = pkgInfo.size();
    }

    if (saveDatabase()) {
        LOG_INFO("WorkshopManager", "Updated item " + item.name + " from " + oldVersion + " to " + newVersion);
        emit updateAvailable(itemId, newVersion);
        return true;
    }
    return false;
}

// ============================================================================
// Profile System
// ============================================================================

bool WorkshopManager::createProfile(const QString& name, const QString& description)
{
    if (name.isEmpty() || m_profiles.contains(name))
        return false;

    WorkshopProfile p;
    p.name = name;
    p.description = description;
    p.created = QDateTime::currentDateTime();
    p.modified = p.created;
    m_profiles[name] = p;
    saveProfiles();
    emit profileCreated(name);
    return true;
}

bool WorkshopManager::deleteProfile(const QString& name)
{
    if (name == "default" || !m_profiles.contains(name))
        return false;

    m_profiles.remove(name);
    if (m_activeProfile == name)
        m_activeProfile = "default";
    saveProfiles();
    emit profileDeleted(name);
    return true;
}

bool WorkshopManager::renameProfile(const QString& oldName, const QString& newName)
{
    if (oldName == "default" || !m_profiles.contains(oldName) || m_profiles.contains(newName))
        return false;

    WorkshopProfile p = m_profiles.take(oldName);
    p.name = newName;
    p.modified = QDateTime::currentDateTime();
    m_profiles[newName] = p;
    if (m_activeProfile == oldName)
        m_activeProfile = newName;
    saveProfiles();
    emit profileSaved(newName);
    return true;
}

bool WorkshopManager::saveProfile(const QString& name, const QVector<ProfileEntry>& entries)
{
    if (!m_profiles.contains(name))
        return false;

    m_profiles[name].entries = entries;
    m_profiles[name].modified = QDateTime::currentDateTime();
    saveProfiles();
    emit profileSaved(name);
    return true;
}

bool WorkshopManager::activateProfile(const QString& name)
{
    if (!m_profiles.contains(name))
        return false;

    const auto& profile = m_profiles[name];

    // Snapshot current state
    auto currentState = snapshotCurrentState();

    // Build set of intended items from profile
    QMap<QString, ProfileEntry> intended;
    for (const auto& e : profile.entries)
        intended[e.itemId] = e;

    // Disable items not in profile
    for (const auto& state : currentState) {
        if (!intended.contains(state.itemId) && m_itemIndex.contains(state.itemId)) {
            int idx = m_itemIndex[state.itemId];
            if (idx >= 0 && idx < m_items.size()) {
                m_items[idx].isInstalled = false;
                emit itemUninstalled(state.itemId);
            }
        }
    }

    // Enable items in profile
    for (const auto& e : profile.entries) {
        if (e.enabled && m_itemIndex.contains(e.itemId)) {
            int idx = m_itemIndex[e.itemId];
            if (idx >= 0 && idx < m_items.size()) {
                m_items[idx].isInstalled = true;
                emit itemInstalled(e.itemId);
            }
        }
    }

    m_activeProfile = name;
    saveDatabase();
    emit profileActivated(name);
    return true;
}

WorkshopManager::WorkshopProfile WorkshopManager::getProfile(const QString& name) const
{
    return m_profiles.value(name);
}

QVector<WorkshopManager::WorkshopProfile> WorkshopManager::listProfiles() const
{
    return m_profiles.values();
}

QVector<WorkshopManager::ProfileEntry> WorkshopManager::snapshotCurrentState() const
{
    QVector<ProfileEntry> entries;
    for (const auto& item : m_items) {
        if (item.isInstalled) {
            ProfileEntry e;
            e.itemId = item.id;
            e.version = item.version;
            e.enabled = true;
            entries.append(e);
        }
    }
    return entries;
}

void WorkshopManager::loadProfiles()
{
    m_profiles.clear();
    QDir pd(m_profilesDir);
    if (!pd.exists()) {
        pd.mkpath(".");
        // Ensure default profile exists
        WorkshopProfile def;
        def.name = "default";
        def.description = "Default profile - all installed items";
        def.created = QDateTime::currentDateTime();
        def.modified = def.created;
        // Snapshot current installed state
        def.entries = snapshotCurrentState();
        m_profiles["default"] = def;
        saveProfiles();
        return;
    }

    for (const auto& fi : pd.entryInfoList({"*.json"}, QDir::Files)) {
        QJsonDocument doc = readJson(fi.absoluteFilePath());
        if (doc.isNull() || !doc.isObject()) continue;
        WorkshopProfile p = profileFromJson(doc.object());
        m_profiles[p.name] = p;
    }

    if (!m_profiles.contains("default")) {
        WorkshopProfile def;
        def.name = "default";
        def.description = "Default profile";
        def.created = QDateTime::currentDateTime();
        def.modified = def.created;
        m_profiles["default"] = def;
    }

    // Read active profile from .active file
    QString activePath = m_profilesDir + "/.active";
    QFile af(activePath);
    if (af.open(QIODevice::ReadOnly)) {
        m_activeProfile = QString::fromUtf8(af.readAll()).trimmed();
        if (!m_profiles.contains(m_activeProfile))
            m_activeProfile = "default";
    }
}

void WorkshopManager::saveProfiles()
{
    QDir pd(m_profilesDir);
    if (!pd.exists())
        pd.mkpath(".");

    for (auto it = m_profiles.begin(); it != m_profiles.end(); ++it) {
        QString path = m_profilesDir + "/" + it.key() + ".json";
        QJsonObject obj = profileToJson(it.value());
        writeJson(path, QJsonDocument(obj));
    }

    // Write active profile marker
    QString activePath = m_profilesDir + "/.active";
    QFile af(activePath);
    if (af.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        af.write(m_activeProfile.toUtf8());
    }

    // Clean up orphaned profile files
    for (const auto& fi : pd.entryInfoList({"*.json"}, QDir::Files)) {
        QString name = fi.completeBaseName();
        if (!m_profiles.contains(name))
            QFile::remove(fi.absoluteFilePath());
    }
}

QJsonObject WorkshopManager::profileEntryToJson(const ProfileEntry& e) const
{
    QJsonObject obj;
    obj["itemId"] = e.itemId;
    obj["version"] = e.version;
    obj["enabled"] = e.enabled;
    return obj;
}

WorkshopManager::ProfileEntry WorkshopManager::profileEntryFromJson(const QJsonObject& obj) const
{
    ProfileEntry e;
    e.itemId = obj["itemId"].toString();
    e.version = obj["version"].toString();
    e.enabled = obj["enabled"].toBool(true);
    return e;
}

QJsonObject WorkshopManager::profileToJson(const WorkshopProfile& p) const
{
    QJsonObject obj;
    obj["name"] = p.name;
    obj["description"] = p.description;
    obj["created"] = p.created.toString(Qt::ISODate);
    obj["modified"] = p.modified.toString(Qt::ISODate);
    QJsonArray arr;
    for (const auto& e : p.entries)
        arr.append(profileEntryToJson(e));
    obj["entries"] = arr;
    return obj;
}

WorkshopManager::WorkshopProfile WorkshopManager::profileFromJson(const QJsonObject& obj) const
{
    WorkshopProfile p;
    p.name = obj["name"].toString();
    p.description = obj["description"].toString();
    p.created = QDateTime::fromString(obj["created"].toString(), Qt::ISODate);
    p.modified = QDateTime::fromString(obj["modified"].toString(), Qt::ISODate);
    for (const auto& e : obj["entries"].toArray())
        p.entries.append(profileEntryFromJson(e.toObject()));
    return p;
}

bool WorkshopManager::saveDatabase() {
    QJsonArray arr;
    for (const auto& item : m_items) {
        arr.append(item.toJson());
    }
    QJsonObject root;
    root["items"] = arr;
    root["updated"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["format_version"] = "1.0";

    // Include active profile name
    root["active_profile"] = m_activeProfile;

    bool ok = writeJson(m_dbPath, QJsonDocument(root));
    if (ok) emit databaseSaved();
    return ok;
}

bool WorkshopManager::loadDatabase() {
    m_items.clear();
    QJsonDocument doc = readJson(m_dbPath);
    if (doc.isNull()) {
        rebuildIndex();
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray arr = root["items"].toArray();
    for (const auto& v : arr) {
        m_items.append(WorkshopItem::fromJson(v.toObject()));
    }

    rebuildIndex();
    emit databaseLoaded();
    return true;
}

QVector<WorkshopItem> WorkshopManager::filterItems(const BrowseQuery& query) const {
    QVector<WorkshopItem> results;

    for (const auto& item : m_items) {
        if (!query.category.isEmpty() &&
            item.category.compare(query.category, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (!query.searchText.isEmpty() &&
            !item.name.contains(query.searchText, Qt::CaseInsensitive) &&
            !item.description.contains(query.searchText, Qt::CaseInsensitive) &&
            !item.author.contains(query.searchText, Qt::CaseInsensitive)) {
            continue;
        }
        if (!query.tags.isEmpty()) {
            bool hasTag = false;
            for (const auto& tag : query.tags) {
                if (item.tags.contains(tag, Qt::CaseInsensitive)) {
                    hasTag = true;
                    break;
                }
            }
            if (!hasTag) continue;
        }
        results.append(item);
    }

    return results;
}

}
