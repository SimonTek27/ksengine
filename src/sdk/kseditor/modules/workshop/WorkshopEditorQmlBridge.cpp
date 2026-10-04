#include "WorkshopEditorQmlBridge.h"
#include "WorkshopEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QDir>
#include <QWidget>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>

namespace ks {

WorkshopEditorQmlBridge* WorkshopEditorQmlBridge::s_instance = nullptr;

WorkshopEditorQmlBridge* WorkshopEditorQmlBridge::instance() {
    if (!s_instance) {
        s_instance = new WorkshopEditorQmlBridge();
    }
    return s_instance;
}

WorkshopEditorQmlBridge::WorkshopEditorQmlBridge(QObject* parent)
    : QObject(parent)
{
    m_manager = WorkshopManager::instance();

    connect(m_manager, &WorkshopManager::itemPublished, this, [this](const QString& id) {
        refreshItems();
        setStatus("Item published: " + id);
        emit publishFinished(id);
    });
    connect(m_manager, &WorkshopManager::itemRemoved, this, [this](const QString& id) {
        refreshItems();
        setStatus("Item removed: " + id);
    });
    connect(m_manager, &WorkshopManager::itemInstalled, this, [this](const QString& id) {
        refreshItems();
        setStatus("Item installed: " + id);
        emit installFinished(id);
    });
    connect(m_manager, &WorkshopManager::itemUninstalled, this, [this](const QString& id) {
        refreshItems();
        setStatus("Item uninstalled: " + id);
    });
    connect(m_manager, &WorkshopManager::packagingError, this, [this](const QString& error) {
        m_isLoading = false;
        emit loadingChanged();
        setStatus("Packaging error: " + error);
        emit operationFailed(error);
    });
    connect(m_manager, &WorkshopManager::packagingFinished, this, [this](const QString& path) {
        m_isLoading = false;
        emit loadingChanged();
        setStatus("Package created: " + path);
    });

    // Profile signals
    connect(m_manager, &WorkshopManager::profileCreated, this, [this](const QString& name) {
        emit profileCreated(name);
        emit profileListChanged();
    });
    connect(m_manager, &WorkshopManager::profileDeleted, this, [this](const QString& name) {
        emit profileDeleted(name);
        emit profileListChanged();
    });
    connect(m_manager, &WorkshopManager::profileActivated, this, [this](const QString& name) {
        emit profileActivated(name);
        emit activeProfileChanged();
    });
    connect(m_manager, &WorkshopManager::profileSaved, this, [this](const QString& name) {
        emit profileListChanged();
    });

    m_currentCategory = "cars";
    refreshItems();
}

void WorkshopEditorQmlBridge::setCurrentCategory(const QString& category) {
    if (m_currentCategory != category) {
        m_currentCategory = category;
        emit categoryChanged();
        browseCategory(category);
    }
}

void WorkshopEditorQmlBridge::browseCategory(const QString& category) {
    m_currentCategory = category;
    WorkshopManager::BrowseQuery query;
    query.category = category;
    query.sortBy = "name";
    m_currentItems = m_manager->browse(query);
    emit categoryChanged();
    emit itemCountChanged();
    setStatus("Browsing: " + category + " (" + QString::number(m_currentItems.size()) + " items)");
}

void WorkshopEditorQmlBridge::browseAll() {
    m_currentCategory.clear();
    WorkshopManager::BrowseQuery query;
    query.sortBy = "date";
    query.ascending = false;
    m_currentItems = m_manager->browse(query);
    emit categoryChanged();
    emit itemCountChanged();
    setStatus("All items (" + QString::number(m_currentItems.size()) + ")");
}

void WorkshopEditorQmlBridge::searchItems(const QString& query) {
    if (query.isEmpty()) {
        browseCategory(m_currentCategory);
        return;
    }
    m_currentItems = m_manager->search(query);
    emit itemCountChanged();
    setStatus("Found " + QString::number(m_currentItems.size()) + " items for \"" + query + "\"");
}

QVariantList WorkshopEditorQmlBridge::getItems() const {
    QVariantList list;
    for (const auto& item : m_currentItems) {
        list.append(itemToVariant(item));
    }
    return list;
}

QVariantMap WorkshopEditorQmlBridge::getItem(int index) const {
    if (index >= 0 && index < m_currentItems.size()) {
        return itemToVariant(m_currentItems[index]);
    }
    return QVariantMap();
}

QVariantMap WorkshopEditorQmlBridge::getItemById(const QString& id) const {
    WorkshopItem item = m_manager->getItem(id);
    if (item.id.isEmpty()) return QVariantMap();
    return itemToVariant(item);
}

void WorkshopEditorQmlBridge::publishMod(const QString& sourcePath, const QString& name,
                                          const QString& version, const QString& author,
                                          const QString& description, const QString& category)
{
    m_isLoading = true;
    emit loadingChanged();

    WorkshopItem item;
    item.name = name;
    item.version = version;
    item.author = author;
    item.description = description;
    item.category = category;

    auto result = m_manager->createWorkshopPackage(sourcePath, m_manager->dataDir() + "/packages", item);
    if (!result.success) {
        m_isLoading = false;
        emit loadingChanged();
        setStatus("Publishing failed: " + result.errors.join("; "));
        emit operationFailed(result.errors.join("; "));
        return;
    }

    item.id = WorkshopItem::generateId();
    m_manager->publishItem(item, result.packagePath);
}

void WorkshopEditorQmlBridge::installItem(int index, bool autoInstallDeps) {
    if (index < 0 || index >= m_currentItems.size()) return;

    const auto& item = m_currentItems[index];
    if (item.packagePath.isEmpty()) {
        setStatus("No package available for: " + item.name);
        return;
    }

    auto result = m_manager->installPackage(item.packagePath, m_manager->dataDir() + "/installed", autoInstallDeps);
    if (result.success) {
        m_manager->setInstalled(item.id, true);
        refreshItems();
        QString msg = "Installed: " + item.name;
        if (!result.autoInstalledDeps.isEmpty())
            msg += " (+ deps: " + result.autoInstalledDeps.join(", ") + ")";
        setStatus(msg);
        emit installFinished(item.name);
    } else {
        setStatus("Install failed: " + result.errors.join("; "));
        emit operationFailed(result.errors.join("; "));
    }
}

void WorkshopEditorQmlBridge::installItemById(const QString& id, bool autoInstallDeps) {
    int idx = -1;
    for (int i = 0; i < m_currentItems.size(); ++i) {
        if (m_currentItems[i].id == id) {
            idx = i;
            break;
        }
    }
    if (idx >= 0) installItem(idx, autoInstallDeps);
}

void WorkshopEditorQmlBridge::uninstallItem(int index) {
    if (index < 0 || index >= m_currentItems.size()) return;
    const auto& item = m_currentItems[index];
    m_manager->setInstalled(item.id, false);
    refreshItems();
    setStatus("Uninstalled: " + item.name);
}

void WorkshopEditorQmlBridge::removeItem(int index) {
    if (index < 0 || index >= m_currentItems.size()) return;
    const auto& item = m_currentItems[index];
    m_manager->removeItem(item.id);
}

void WorkshopEditorQmlBridge::rateItem(int index, int rating) {
    if (index < 0 || index >= m_currentItems.size()) return;
    m_manager->rateItem(m_currentItems[index].id, rating);
    refreshItems();
    setStatus("Rated: " + m_currentItems[index].name + " (" + QString::number(rating) + "/5)");
}

void WorkshopEditorQmlBridge::rateItemById(const QString& id, int rating) {
    if (m_manager->rateItem(id, rating)) {
        refreshItems();
        setStatus("Rating updated");
    }
}

void WorkshopEditorQmlBridge::downloadItem(int index) {
    if (index < 0 || index >= m_currentItems.size()) return;
    const auto& item = m_currentItems[index];
    if (item.packagePath.isEmpty()) {
        setStatus("No package to download for: " + item.name);
        return;
    }
    emit downloadStarted(index);
    auto result = m_manager->installPackage(item.packagePath, m_manager->dataDir() + "/installed");
    if (result.success) {
        m_manager->setInstalled(item.id, true);
        refreshItems();
        emit downloadFinished(index, result.installPath);
        setStatus("Downloaded: " + item.name);
    } else {
        emit downloadFailed(index, result.errors.join("; "));
        setStatus("Download failed: " + result.errors.join("; "));
    }
}

void WorkshopEditorQmlBridge::cancelDownload(int index) {
    if (index < 0 || index >= m_currentItems.size()) return;
    const auto& item = m_currentItems[index];
    emit downloadCancelled(index);
    m_manager->setInstalled(item.id, false);
    refreshItems();
    setStatus("Download cancelled: " + item.name);
}

QVariantList WorkshopEditorQmlBridge::getInstalledItems() const {
    QVariantList list;
    auto installed = m_manager->getInstalledItems();
    for (const auto& item : installed) {
        list.append(itemToVariant(item));
    }
    return list;
}

void WorkshopEditorQmlBridge::openInBrowser(int index) {
    if (index < 0 || index >= m_currentItems.size()) {
        setStatus("Invalid item index");
        return;
    }
    const auto& item = m_currentItems[index];
    QString url = item.website.isEmpty() ? item.previewUrl : item.website;
    if (url.isEmpty()) {
        setStatus("No website URL available for \"" + item.name + "\"");
        return;
    }
    if (!url.startsWith("http://") && !url.startsWith("https://")) {
        url = "https://" + url;
    }
    if (QDesktopServices::openUrl(QUrl(url))) {
        setStatus("Opened in browser: " + url);
    } else {
        setStatus("Failed to open browser for: " + url);
    }
}

void WorkshopEditorQmlBridge::importMod(const QString& filePath) {
    QFileInfo src(filePath);
    if (!src.exists()) {
        setStatus("File not found: " + filePath);
        return;
    }

    if (src.suffix().toLower() == "kspack" || src.suffix().toLower() == "zip") {
        auto result = m_manager->installPackage(filePath, m_manager->dataDir() + "/installed");
        if (result.success) {
            WorkshopItem item;
            item.name = result.modName;
            item.packagePath = filePath;
            item.category = "imported";
            item.isInstalled = true;
            m_manager->publishItem(item, filePath);
            refreshItems();
            setStatus("Imported: " + result.modName);
            emit installFinished(result.modName);
        } else {
            setStatus("Import failed: " + result.errors.join("; "));
        }
    } else {
        setStatus("Unsupported package format: " + src.suffix());
    }
}

void WorkshopEditorQmlBridge::exportMod(const QString& filePath) {
    if (m_currentItems.isEmpty()) {
        setStatus("No items to export");
        return;
    }

    const auto& item = m_currentItems[0];
    auto result = m_manager->createWorkshopPackage(
        item.packagePath, QFileInfo(filePath).absolutePath(), item);
    if (result.success) {
        setStatus("Exported to: " + result.packagePath);
    } else {
        setStatus("Export failed: " + result.errors.join("; "));
    }
}

QVariantMap WorkshopEditorQmlBridge::itemToVariant(const WorkshopItem& item) const {
    QVariantMap m;
    m["id"] = item.id;
    m["name"] = item.name;
    m["version"] = item.version;
    m["author"] = item.author;
    m["description"] = item.description;
    m["category"] = item.category;
    m["tags"] = QVariant::fromValue(item.tags);
    m["preview"] = item.previewUrl;
    m["screenshots"] = QVariant::fromValue(item.screenshots);
    m["dependencies"] = QVariant::fromValue(item.dependencies);
    m["conflicts"] = QVariant::fromValue(item.conflicts);
    m["license"] = item.license;
    m["website"] = item.website;
    m["fileSize"] = item.fileSize;
    m["packagePath"] = item.packagePath;
    m["createdAt"] = item.createdAt.toString(Qt::ISODate);
    m["updatedAt"] = item.updatedAt.toString(Qt::ISODate);
    m["downloadCount"] = item.downloadCount;
    m["rating"] = static_cast<double>(item.rating);
    m["ratingCount"] = item.ratingCount;
    m["isInstalled"] = item.isInstalled;
    return m;
}

void WorkshopEditorQmlBridge::refreshItems() {
    if (m_currentCategory.isEmpty()) {
        browseAll();
    } else {
        WorkshopManager::BrowseQuery query;
        query.category = m_currentCategory;
        query.sortBy = "name";
        m_currentItems = m_manager->browse(query);
    }
    emit itemCountChanged();
}

void WorkshopEditorQmlBridge::setStatus(const QString& msg) {
    m_status = msg;
    emit statusMessageChanged();
}

// ============================================================================
// Profile System (QML bridge)
// ============================================================================

QString WorkshopEditorQmlBridge::activeProfile() const
{
    return m_manager ? m_manager->activeProfile() : "default";
}

QStringList WorkshopEditorQmlBridge::profileNames() const
{
    if (!m_manager) return {};
    auto profiles = m_manager->listProfiles();
    QStringList names;
    for (const auto& p : profiles)
        names.append(p.name);
    return names;
}

bool WorkshopEditorQmlBridge::createProfile(const QString& name, const QString& description)
{
    return m_manager && m_manager->createProfile(name, description);
}

bool WorkshopEditorQmlBridge::deleteProfile(const QString& name)
{
    return m_manager && m_manager->deleteProfile(name);
}

bool WorkshopEditorQmlBridge::renameProfile(const QString& oldName, const QString& newName)
{
    return m_manager && m_manager->renameProfile(oldName, newName);
}

bool WorkshopEditorQmlBridge::activateProfile(const QString& name)
{
    return m_manager && m_manager->activateProfile(name);
}

bool WorkshopEditorQmlBridge::saveCurrentStateAsProfile(const QString& name)
{
    if (!m_manager) return false;
    auto entries = m_manager->snapshotCurrentState();
    return m_manager->saveProfile(name, entries);
}

QVariantMap WorkshopEditorQmlBridge::getProfile(const QString& name) const
{
    if (!m_manager) return {};
    auto p = m_manager->getProfile(name);
    QVariantMap m;
    m["name"] = p.name;
    m["description"] = p.description;
    m["created"] = p.created.toString(Qt::ISODate);
    m["modified"] = p.modified.toString(Qt::ISODate);
    m["entryCount"] = static_cast<int>(p.entries.size());
    return m;
}

QVariantList WorkshopEditorQmlBridge::getProfileEntries(const QString& name) const
{
    if (!m_manager) return {};
    auto p = m_manager->getProfile(name);
    QVariantList list;
    for (const auto& e : p.entries) {
        QVariantMap m;
        m["itemId"] = e.itemId;
        m["version"] = e.version;
        m["enabled"] = e.enabled;
        list.append(m);
    }
    return list;
}

// ── Dependency resolution bridge ──────────────────────────────────────────

QVariantMap WorkshopEditorQmlBridge::resolveDependencies(int index) {
    if (index < 0 || index >= m_currentItems.size())
        return { {"error", "Invalid index"} };
    return resolveDependenciesForId(m_currentItems[index].id);
}

QVariantMap WorkshopEditorQmlBridge::resolveDependenciesForId(const QString& id) {
    if (!m_manager) return { {"error", "WorkshopManager not available"} };

    DepResolution depRes = m_manager->resolveDependencies(id);
    QVariantMap result;
    result["allSatisfied"] = depRes.allSatisfied;

    QVariantList deps;
    for (const auto& d : depRes.dependencies) {
        QVariantMap dm;
        dm["name"] = d.depName;
        dm["spec"] = d.spec.raw;
        dm["satisfied"] = d.satisfied;
        dm["resolvedBy"] = d.resolvedBy;
        dm["resolvedVersion"] = d.resolvedVersion;
        deps.append(dm);
    }
    result["dependencies"] = deps;
    result["missingDeps"] = QVariant::fromValue(depRes.missingDeps);
    result["conflictingItems"] = QVariant::fromValue(depRes.conflictingItems);
    return result;
}

bool WorkshopEditorQmlBridge::areDependenciesSatisfied(int index) {
    if (index < 0 || index >= m_currentItems.size()) return false;
    if (!m_manager) return false;
    return m_manager->areDependenciesSatisfied(m_currentItems[index].id);
}

QVariantList WorkshopEditorQmlBridge::checkForUpdates() const {
    if (!m_manager) return {};
    auto updates = m_manager->checkForUpdates();
    QVariantList result;
    for (const auto& u : updates) {
        QVariantMap um;
        um["itemId"] = u.itemId;
        um["name"] = u.name;
        um["currentVersion"] = u.currentVersion;
        um["availableVersion"] = u.availableVersion;
        result.append(um);
    }
    return result;
}

bool WorkshopEditorQmlBridge::resolveConflict(const QString& itemId, const QString& conflictingId, bool keepItem) {
    if (!m_manager) {
        emit operationFailed("WorkshopManager not available");
        return false;
    }

    WorkshopItem item = m_manager->getItem(itemId);
    WorkshopItem conflict = m_manager->getItem(conflictingId);
    if (item.id.isEmpty() || conflict.id.isEmpty()) {
        emit operationFailed("Item or conflicting item not found");
        return false;
    }

    // Remove the conflicting item from the item's conflict list
    item.conflicts.removeAll(conflictingId);
    conflict.conflicts.removeAll(itemId);

    if (keepItem) {
        // Uninstall the conflicting item
        m_manager->setInstalled(conflictingId, false);
        // Remove conflicting from database
        m_manager->removeItem(conflictingId);
        setStatus(QString("Resolved: kept '%1', removed '%2'").arg(item.name, conflict.name));
    } else {
        // Uninstall the current item, keep the conflicting one
        m_manager->setInstalled(itemId, false);
        m_manager->removeItem(itemId);
        setStatus(QString("Resolved: kept '%1', removed '%2'").arg(conflict.name, item.name));
    }

    // Save updated items
    m_manager->saveDatabase();
    refreshItems();

    emit conflictResolved(itemId, conflictingId, keepItem);
    return true;
}

QVariantList WorkshopEditorQmlBridge::getConflicts(const QString& itemId) const {
    QVariantList result;
    if (!m_manager) return result;

    WorkshopItem item = m_manager->getItem(itemId);
    for (const auto& conflictId : item.conflicts) {
        WorkshopItem conflict = m_manager->getItem(conflictId);
        QVariantMap cm;
        cm["id"] = conflict.id;
        cm["name"] = conflict.name;
        cm["version"] = conflict.version;
        cm["author"] = conflict.author;
        cm["isInstalled"] = conflict.isInstalled;
        result.append(cm);
    }
    return result;
}

} // namespace ks
