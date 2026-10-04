#include "ModManager.h"
#include "ModManagerQmlBridge.h"
#include "ModCollection.h"
#include "ConflictResolutionDialog.h"
#include "sdk/kseditor/engine/archive/SevenZipLibrary.h"
#include "workshop/WorkshopManager.h"
#include "workshop/WorkshopItem.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QSet>
#include <QQueue>
#include <QApplication>
#include <QDebug>
#include <QCryptographicHash>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QAction>
#include <QFileSystemWatcher>
#include <QFileDialog>
#include <QIcon>
#include <QComboBox>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QFormLayout>
#include <QGroupBox>
#include <QTimer>
#include <QEventLoop>
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QDialogButtonBox>
#include "Config/EditorConfig.h"

namespace ks {

// ============================================================================
// Version
// ============================================================================

Version Version::fromString(const QString& str)
{
    Version v;
    QString s = str.trimmed();
    // strip leading 'v' or 'V'
    if (s.startsWith('v', Qt::CaseInsensitive)) s = s.mid(1);
    // strip trailing prerelease
    int preIdx = -1;
    for (int i = 0; i < s.size(); ++i) {
        if (!s[i].isDigit() && s[i] != '.') { preIdx = i; break; }
    }
    QString verPart = s;
    if (preIdx >= 0) {
        verPart = s.left(preIdx);
        v.preRelease = s.mid(preIdx);
    }
    QStringList parts = verPart.split('.');
    v.major = parts.size() > 0 ? parts[0].toInt() : 0;
    v.minor = parts.size() > 1 ? parts[1].toInt() : 0;
    v.patch = parts.size() > 2 ? parts[2].toInt() : 0;
    return v;
}

int Version::compare(const Version& other) const
{
    if (major != other.major) return major - other.major;
    if (minor != other.minor) return minor - other.minor;
    if (patch != other.patch) return patch - other.patch;
    // pre-release versions have lower precedence
    if (preRelease.isEmpty() != other.preRelease.isEmpty())
        return preRelease.isEmpty() ? 1 : -1;
    return preRelease.compare(other.preRelease);
}

bool Version::operator==(const Version& other) const
{
    return compare(other) == 0;
}

QString Version::toString() const
{
    QString s = QString("%1.%2.%3").arg(major).arg(minor).arg(patch);
    if (!preRelease.isEmpty()) s += preRelease;
    return s;
}

// ============================================================================
// VersionSpec
// ============================================================================

VersionSpec VersionSpec::fromString(const QString& spec)
{
    VersionSpec vs;
    vs.raw = spec.trimmed();
    if (vs.raw.isEmpty() || vs.raw == "*") { vs.op = Any; return vs; }

    struct { QString tok; Op op; } prefixes[] = {
        { ">=", Ge }, { "<=", Le }, { "!=", Neq },
        { ">", Gt },  { "<", Lt },  { "==", Eq },
        { "~>", Tilde }, { "^", Caret }
    };

    for (const auto& pfx : prefixes) {
        if (vs.raw.startsWith(pfx.tok)) {
            vs.op = pfx.op;
            vs.version = Version::fromString(vs.raw.mid(pfx.tok.size()));
            return vs;
        }
    }
    // bare version means exact match
    vs.op = Eq;
    vs.version = Version::fromString(vs.raw);
    return vs;
}

bool VersionSpec::matches(const Version& v) const
{
    if (op == Any) return true;
    int cmp = version.compare(v);
    switch (op) {
    case Eq:    return cmp == 0;
    case Neq:   return cmp != 0;
    case Gt:    return cmp < 0;
    case Lt:    return cmp > 0;
    case Ge:    return cmp <= 0;
    case Le:    return cmp >= 0;
    case Tilde: // ~>1.2.3 = >=1.2.3 and <1.3.0
        if (cmp > 0) return false;
        return v.major == version.major && v.minor == version.minor && v.patch >= version.patch;
    case Caret: // ^1.2.3 = >=1.2.3 and <2.0.0; ^0.2.3 = >=0.2.3 and <0.3.0
        if (cmp > 0) return false;
        if (version.major != 0) return v.major == version.major;
        if (version.minor != 0) return v.minor == version.minor;
        return v.patch == version.patch;
    default: return false;
    }
}

bool VersionSpec::isValid() const
{
    return op == Any || version.isValid();
}

QString VersionSpec::opToString(Op o)
{
    switch (o) {
    case Any:   return "*";
    case Eq:    return "==";
    case Neq:   return "!=";
    case Gt:    return ">";
    case Lt:    return "<";
    case Ge:    return ">=";
    case Le:    return "<=";
    case Tilde: return "~>";
    case Caret: return "^";
    default:    return "?";
    }
}

// ============================================================================
// ModScanner
// ============================================================================

ModScanner::ModScanner(QObject* parent) : QObject(parent) {}

void ModScanner::scanDirectory(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) return;
    m_scannedPaths.clear();
    m_cancelled = false;
    scanForMods(path, "");
    emit scanFinished();
}

void ModScanner::scanACInstallation(const QString& acRoot) {
    m_cancelled = false;
    m_scannedPaths.clear();

    // Scan standard AC mod locations
    QStringList searchPaths = {
        acRoot + "/content/cars",
        acRoot + "/content/tracks",
        acRoot + "/content/skins",
        acRoot + "/content/weather",
        acRoot + "/extension/config",
        acRoot + "/system/cfg",
        EditorConfig::instance().modCfgPath()
    };

    for (const QString& searchPath : searchPaths) {
        if (!m_cancelled) {
            scanForMods(searchPath, detectModCategory(searchPath));
        }
    }

    if (!m_cancelled) {
        emit scanFinished();
    }
}

void ModScanner::cancelScan() {
    m_cancelled = true;
}

void ModScanner::scanForMods(const QString& dir, const QString& category) {
    QDirIterator it(dir, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    int total = 0;

    // First pass: count directories
    QStringList allDirs;
    while (it.hasNext()) {
        allDirs.append(it.next());
    }

    total = allDirs.size();
    int current = 0;

    for (const QString& subDir : allDirs) {
        if (m_cancelled) return;

        QFileInfo fi(subDir);
        QString name = fi.fileName();
        if (m_scannedPaths.contains(subDir)) continue;
        m_scannedPaths.insert(subDir);

        if (name.startsWith('.')) continue; // hidden dirs

        // Look for mod indicators: manifest.json, data.acd, or known structure
        bool hasManifest = QFile::exists(subDir + "/manifest.json");
        bool hasDataFile = QFile::exists(subDir + "/data.acd");
        bool isMod = hasManifest || hasDataFile;

        if (isMod) {
            emit modFound(name, subDir, category);
        }

        emit scanProgress(current++, total);
    }
}

void ModScanner::scanSingleFile(const QString& filePath) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    if (ext == "zip" || ext == "7z" || ext == "rar") {
        QString category = detectModCategory(filePath);
        emit modFound(fi.completeBaseName(), filePath, category);
    } else if (ext == "kn5" || ext == "fbx" || ext == "obj" || ext == "glb") {
        emit modFound(fi.completeBaseName(), filePath, "models");
    }
}

QJsonObject ModScanner::readModManifest(const QString& manifestPath) {
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

QString ModScanner::detectModCategory(const QString& path) {
    QString lower = path.toLower();
    if (lower.contains("cars")) return "cars";
    if (lower.contains("tracks")) return "tracks";
    if (lower.contains("skins")) return "skins";
    if (lower.contains("weather")) return "weather";
    if (lower.contains("config")) return "config";
    if (lower.contains("cfg")) return "config";
    if (lower.contains("models")) return "models";
    if (lower.contains("fonts")) return "fonts";
    if (lower.contains("audio") || lower.contains("sound")) return "audio";
    return "other";
}

// ============================================================================
// DependencyResolver
// ============================================================================

DependencyResolver::DependencyResolver(QObject* parent) : QObject(parent) {}

bool DependencyResolver::checkConstraint(const VersionSpec& spec, const Version& installed) const
{
    return spec.matches(installed);
}

QVector<DependencyResolver::ResolvedDep> DependencyResolver::resolveDependencyDetails(
    const QVector<ModEntry>& allMods, const QString& modName) const
{
    QVector<ResolvedDep> result;
    auto it = std::find_if(allMods.begin(), allMods.end(),
        [&](const ModEntry& m) { return m.name == modName; });
    if (it == allMods.end()) return result;

    QSet<QString> depSet; // avoid infinite loops for circular deps

    std::function<void(const ModEntry&, int)> collect = [&](const ModEntry& mod, int depth) {
        if (depth > 20) return;
        for (const QString& dep : mod.dependencies) {
            if (depSet.contains(dep)) continue;
            depSet.insert(dep);

            ResolvedDep rd;
            rd.depName = dep;
            rd.spec = mod.dependencySpecs.value(dep);

            auto prov = std::find_if(allMods.begin(), allMods.end(),
                [&](const ModEntry& m) { return m.name == dep && m.enabled; });
            if (prov != allMods.end()) {
                rd.installedVersion = prov->version.toString();
                rd.resolvedBy = prov->name;
                rd.satisfied = rd.spec.isValid()
                    ? checkConstraint(rd.spec, prov->version)
                    : true;
            } else {
                rd.satisfied = false;
            }
            result.append(rd);
            if (prov != allMods.end()) collect(*prov, depth + 1);
        }
    };
    collect(*it, 0);
    return result;
}

QVector<QPair<QString, QStringList>> DependencyResolver::buildDependencyTree(
    const QVector<ModEntry>& allMods, const QString& rootMod, int maxDepth) const
{
    QVector<QPair<QString, QStringList>> tree;
    QSet<QString> visited;

    std::function<void(const QString&, int)> walk = [&](const QString& modName, int depth) {
        if (visited.contains(modName) || (maxDepth >= 0 && depth > maxDepth)) return;
        visited.insert(modName);

        auto it = std::find_if(allMods.begin(), allMods.end(),
            [&](const ModEntry& m) { return m.name == modName; });
        if (it == allMods.end()) {
            tree.append({modName, {"[MISSING]"}});
            return;
        }

        QStringList deps;
        for (const QString& d : it->dependencies) {
            auto depIt = std::find_if(allMods.begin(), allMods.end(),
                [&](const ModEntry& m) { return m.name == d && m.enabled; });
            if (depIt != allMods.end()) {
                deps.append(d + " " + depIt->version.toString());
            } else {
                deps.append(d + " [MISSING]");
            }
        }
        tree.append({modName + " " + it->version.toString(), deps});

        for (const QString& d : it->dependencies) {
            walk(d, depth + 1);
        }
    };

    walk(rootMod, 0);
    return tree;
}

QStringList DependencyResolver::reverseDependencies(
    const QVector<ModEntry>& allMods, const QString& modName) const
{
    QStringList reverse;
    for (const auto& mod : allMods) {
        if (!mod.enabled) continue;
        for (const QString& dep : mod.dependencies) {
            if (dep == modName) {
                reverse.append(mod.name);
                break;
            }
        }
    }
    return reverse;
}

QStringList DependencyResolver::findVersionConflicts(
    const QVector<ModEntry>& allMods, const QStringList& targetMods) const
{
    // Check if two mods require incompatible versions of the same dependency
    QMap<QString, QVector<QPair<QString, VersionSpec>>> depUsers;
    for (const QString& t : targetMods) {
        auto it = std::find_if(allMods.begin(), allMods.end(),
            [&](const ModEntry& m) { return m.name == t; });
        if (it == allMods.end()) continue;
        for (auto ds = it->dependencySpecs.begin(); ds != it->dependencySpecs.end(); ++ds) {
            depUsers[ds.key()].append({t, ds.value()});
        }
    }

    QStringList conflicts;
    for (auto it = depUsers.begin(); it != depUsers.end(); ++it) {
        const QString& depName = it.key();
        auto& users = it.value();
        if (users.size() < 2) continue;

        // Check installed version
        auto instIt = std::find_if(allMods.begin(), allMods.end(),
            [&](const ModEntry& m) { return m.name == depName && m.enabled; });
        if (instIt == allMods.end()) continue;

        for (const auto& user : users) {
            if (!checkConstraint(user.second, instIt->version)) {
                conflicts.append(QString("%1 requires %2 %3 %4, but installed version is %5")
                    .arg(user.first, depName,
                         VersionSpec::opToString(user.second.op),
                         user.second.version.toString(),
                         instIt->version.toString()));
            }
        }
    }
    return conflicts;
}

DependencyResolver::Resolution DependencyResolver::resolve(
    const QVector<ModEntry>& allMods, const QStringList& targetMods)
{
    Resolution result;
    result.missingDeps = findMissing(allMods, targetMods);
    result.conflictingMods = findConflicts(allMods, targetMods);
    result.satisfied = result.missingDeps.isEmpty() && result.conflictingMods.isEmpty();

    // Check for circular dependencies
    QSet<QString> visited;
    QSet<QString> inStack;
    std::function<bool(const QString&)> detectCycle = [&](const QString& modName) -> bool {
        if (inStack.contains(modName)) return true;
        if (visited.contains(modName)) return false;
        visited.insert(modName);
        inStack.insert(modName);

        auto it = std::find_if(allMods.begin(), allMods.end(),
            [&](const ModEntry& m) { return m.name == modName; });

        if (it != allMods.end()) {
            for (const QString& dep : it->dependencies) {
                if (detectCycle(dep)) return true;
            }
        }

        inStack.remove(modName);
        return false;
    };

    for (const QString& t : targetMods) {
        if (detectCycle(t)) {
            result.circularDeps.append(t);
            result.satisfied = false;
        }
    }

    if (result.satisfied) {
        result.resolvedOrder = topologicalSort(allMods);
    }

    // Populate depDetails for each target mod
    for (const QString& t : targetMods) {
        result.depDetails[t] = resolveDependencyDetails(allMods, t);
    }

    // Also check for version conflicts among enabled mods
    QStringList versionConflicts = findVersionConflicts(allMods, targetMods);
    if (!versionConflicts.isEmpty()) {
        result.conflictingMods.append(versionConflicts);
        result.satisfied = false;
    }

    return result;
}

QStringList DependencyResolver::topologicalSort(const QVector<ModEntry>& mods) {
    QMap<QString, int> inDegree;
    QMap<QString, QStringList> adj;

    for (const auto& mod : mods) {
        if (!inDegree.contains(mod.name)) inDegree[mod.name] = 0;
        for (const QString& dep : mod.dependencies) {
            adj[dep].append(mod.name);
            inDegree[mod.name]++;
        }
    }

    QQueue<QString> queue;
    for (auto it = inDegree.begin(); it != inDegree.end(); ++it) {
        if (it.value() == 0) queue.enqueue(it.key());
    }

    QStringList sorted;
    while (!queue.isEmpty()) {
        QString node = queue.dequeue();
        sorted.append(node);

        for (const QString& neighbor : adj[node]) {
            inDegree[neighbor]--;
            if (inDegree[neighbor] == 0) queue.enqueue(neighbor);
        }
    }

    return sorted;
}

QStringList DependencyResolver::findMissing(
    const QVector<ModEntry>& allMods, const QStringList& targetMods)
{
    QSet<QString> available;
    for (const auto& mod : allMods) {
        available.insert(mod.name);
    }

    QStringList missing;
    QSet<QString> checked;

    std::function<void(const QString&)> checkDeps = [&](const QString& modName) {
        if (checked.contains(modName)) return;
        checked.insert(modName);

        auto it = std::find_if(allMods.begin(), allMods.end(),
            [&](const ModEntry& m) { return m.name == modName; });

        if (it == allMods.end()) {
            if (!available.contains(modName)) {
                missing.append(modName);
            }
            return;
        }

        for (const QString& dep : it->dependencies) {
            if (!available.contains(dep)) {
                missing.append(dep);
            }
            checkDeps(dep);
        }
    };

    for (const QString& t : targetMods) {
        checkDeps(t);
    }

    missing.removeDuplicates();
    return missing;
}

QStringList DependencyResolver::findConflicts(
    const QVector<ModEntry>& allMods, const QStringList& targetMods)
{
    QStringList conflicts;
    QSet<QString> targetSet(targetMods.begin(), targetMods.end());

    for (const auto& mod : allMods) {
        if (!targetSet.contains(mod.name) && mod.enabled) {
            for (const QString& c : mod.conflicts) {
                if (targetSet.contains(c)) {
                    conflicts.append(c + " conflicts with " + mod.name);
                }
            }
        }
    }

    return conflicts;
}

// ============================================================================
// ModConflictDetector
// ============================================================================

ModConflictDetector::ModConflictDetector(QObject* parent) : QObject(parent) {}

QStringList ModConflictDetector::getModFiles(const ModEntry& mod) const {
    QStringList files;
    QDir modDir(mod.path);
    if (!modDir.exists()) return files;

    QDirIterator it(modDir.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = it.next();
        QString relPath = modDir.relativeFilePath(filePath);
        // Skip metadata files
        if (relPath.startsWith(".integrity.json") || relPath.startsWith(".state.json") ||
            relPath.startsWith("manifest.json") || relPath.startsWith("workshop_manifest.json")) {
            continue;
        }
        files.append(relPath);
    }
    return files;
}

ModConflictDetector::ConflictReport ModConflictDetector::detectConflicts(const QVector<ModEntry>& mods, const QString& gameRoot) const {
    ConflictReport report;

    // Map of file path -> list of mods that contain it
    QMap<QString, QStringList> fileToMods;

    for (const auto& mod : mods) {
        if (!mod.enabled) continue;

        QStringList files = getModFiles(mod);
        for (const QString& file : files) {
            fileToMods[file].append(mod.name);
        }
    }

    // Find conflicts (files in more than one mod)
    for (auto it = fileToMods.begin(); it != fileToMods.end(); ++it) {
        const QStringList& modNames = it.value();
        if (modNames.size() > 1) {
            FileConflict conflict;
            conflict.filePath = it.key();
            conflict.modNames = modNames;

            // Determine conflict type based on file extension and path
            QString ext = QFileInfo(it.key()).suffix().toLower();
            if (ext == "ini" || ext == "cfg" || ext == "json" || ext == "lua") {
                conflict.conflictType = "merge"; // Config files can often be merged
            } else if (ext == "acd" || ext == "kn5" || ext == "fbx" || ext == "obj" ||
                       ext == "dds" || ext == "png" || ext == "tga" || ext == "jpg") {
                conflict.conflictType = "overwrite"; // Binary assets overwrite
            } else {
                conflict.conflictType = "duplicate";
            }

            // Critical if it's an AC core file or binary asset
            if (conflict.conflictType == "overwrite" ||
                it.key().contains("system/") ||
                it.key().contains("extension/config/")) {
                report.criticalConflicts.append(it.key());
                report.hasCritical = true;
            }

            report.conflicts.append(conflict);
        }
    }

    return report;
}

ModConflictDetector::ConflictReport ModConflictDetector::checkModConflicts(const ModEntry& newMod,
                                                                           const QVector<ModEntry>& installedMods,
                                                                           const QString& gameRoot) const {
    ConflictReport report;

    QStringList newModFiles = getModFiles(newMod);

    for (const QString& file : newModFiles) {
        for (const auto& mod : installedMods) {
            if (!mod.enabled) continue;
            QStringList modFiles = getModFiles(mod);
            if (modFiles.contains(file)) {
                FileConflict conflict;
                conflict.filePath = file;
                conflict.modNames = {newMod.name, mod.name};

                QString ext = QFileInfo(file).suffix().toLower();
                if (ext == "ini" || ext == "cfg" || ext == "json" || ext == "lua") {
                    conflict.conflictType = "merge";
                } else {
                    conflict.conflictType = "overwrite";
                }

                if (conflict.conflictType == "overwrite" ||
                    file.contains("system/") ||
                    file.contains("extension/config/")) {
                    report.criticalConflicts.append(file);
                    report.hasCritical = true;
                }

                report.conflicts.append(conflict);
                break; // Only report first conflict per file
            }
        }
    }

    return report;
}

// ============================================================================
// ModUpdateChecker
// ============================================================================

const QString ModUpdateChecker::s_updateEndpoint = "https://api.kseditor.com/mods/check-update";

ModUpdateChecker::ModUpdateChecker(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

void ModUpdateChecker::checkForUpdates(const QVector<ModEntry>& mods) {
    if (m_checking) return;
    m_checking = true;
    m_updates.clear();
    m_checkQueue.clear();
    m_modVersionMap.clear();
    m_current = 0;
    m_total = mods.size();

    for (const auto& mod : mods) {
        if (!mod.isBuiltIn && !mod.updateUrl.isEmpty()) {
            m_checkQueue.enqueue(mod.name);
            m_modVersionMap[mod.name] = mod.versionStr;
        }
    }

    m_total = m_checkQueue.size();
    if (m_total == 0) {
        m_checking = false;
        emit checkFinished(0);
        return;
    }
    emit checkProgress(0, m_total);
    processNext();
}

void ModUpdateChecker::checkSingleMod(const ModEntry& mod) {
    if (mod.updateUrl.isEmpty()) return;

    QNetworkRequest request(QUrl(mod.updateUrl));
    request.setRawHeader("User-Agent", "ksEditor-ModManager/2.0");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject payload;
    payload["mod"] = mod.name;
    payload["version"] = mod.versionStr;
    payload["action"] = "check_update";

    QNetworkReply* reply = m_networkManager->post(request, QJsonDocument(payload).toJson());
    reply->setProperty("modName", mod.name);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QString modName = reply->property("modName").toString();
        parseUpdateResponse(reply, modName);
        reply->deleteLater();
    });
}

void ModUpdateChecker::cancelAll() {
    m_checkQueue.clear();
    m_checking = false;
    emit checkFinished(m_updates.size());
}

void ModUpdateChecker::processNext() {
    if (m_checkQueue.isEmpty() || !m_checking) {
        m_checking = false;
        emit checkFinished(m_updates.size());
        return;
    }

    QString modName = m_checkQueue.dequeue();
    m_current++;

    QString version = m_modVersionMap.value(modName);

    QJsonObject payload;
    payload["mod"] = modName;
    payload["version"] = version;
    payload["action"] = "check_update";

    QNetworkRequest request{QUrl(s_updateEndpoint)};
    request.setRawHeader("User-Agent", "ksEditor-ModManager/2.0");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = m_networkManager->post(request, QJsonDocument(payload).toJson());
    reply->setProperty("modName", modName);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QString modName = reply->property("modName").toString();
        parseUpdateResponse(reply, modName);
        reply->deleteLater();
        emit checkProgress(m_current, m_total);
        processNext();
    });
}

void ModUpdateChecker::parseUpdateResponse(QNetworkReply* reply, const QString& modName) {
    if (reply->error() != QNetworkReply::NoError) {
        emit checkError(modName, reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    if (obj["updateAvailable"].toBool()) {
        QString newVersion = obj["newVersion"].toString();
        QString downloadUrl = obj["downloadUrl"].toString();
        QString currentVersion = m_modVersionMap.value(modName);
        m_updates.append({modName, newVersion});
        emit updateFound(modName, currentVersion, newVersion);
    }
}

// ============================================================================
// ModUpdateChecker — Download Updates
// ============================================================================

void ModUpdateChecker::downloadUpdate(const QString& modName, const QString& downloadUrl) {
    if (downloadUrl.isEmpty()) {
        emit downloadError(modName, "No download URL provided");
        return;
    }

    if (m_downloadDir.isEmpty()) {
        m_downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/kseditor_updates";
    }

    QDir().mkpath(m_downloadDir);
    QString savePath = m_downloadDir + "/" + modName + ".zip";

    QUrl url(downloadUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "ksEditor-ModManager/2.0");

    QNetworkReply* reply = m_networkManager->get(request);
    reply->setProperty("modName", modName);
    reply->setProperty("savePath", savePath);

    m_activeDownloads[modName] = reply;
    m_downloading = true;

    connect(reply, &QNetworkReply::downloadProgress, this, [this, modName](qint64 received, qint64 total) {
        emit downloadProgress(modName, received, total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, modName]() {
        handleDownloadReply(reply, modName);
    });
}

void ModUpdateChecker::downloadAllUpdates() {
    if (m_downloading) return;
    if (m_updates.isEmpty()) {
        emit allDownloadsFinished(0, 0);
        return;
    }

    m_downloadSuccessCount = 0;
    m_downloadFailCount = 0;
    m_downloadQueue.clear();

    for (const auto& update : m_updates) {
        // Find the mod's update URL from the version map
        DownloadItem item;
        item.modName = update.first;
        item.url = s_updateEndpoint + "/download/" + update.first + "?version=" + update.second;
        item.savePath = (m_downloadDir.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/kseditor_updates"
            : m_downloadDir) + "/" + update.first + ".zip";
        m_downloadQueue.enqueue(item);
    }

    processNextDownload();
}

void ModUpdateChecker::processNextDownload() {
    if (m_downloadQueue.isEmpty()) {
        m_downloading = false;
        emit allDownloadsFinished(m_downloadSuccessCount, m_downloadFailCount);
        return;
    }

    DownloadItem item = m_downloadQueue.dequeue();

    QDir().mkpath(QFileInfo(item.savePath).absolutePath());

    QNetworkRequest request(QUrl(item.url));
    request.setRawHeader("User-Agent", "ksEditor-ModManager/2.0");

    QNetworkReply* reply = m_networkManager->get(request);
    reply->setProperty("modName", item.modName);
    reply->setProperty("savePath", item.savePath);

    m_activeDownloads[item.modName] = reply;

    connect(reply, &QNetworkReply::downloadProgress, this, [this, modName = item.modName](qint64 received, qint64 total) {
        emit downloadProgress(modName, received, total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, modName = item.modName]() {
        handleDownloadReply(reply, modName);
        processNextDownload();
    });
}

void ModUpdateChecker::handleDownloadReply(QNetworkReply* reply, const QString& modName) {
    reply->deleteLater();
    m_activeDownloads.remove(modName);

    if (reply->error() != QNetworkReply::NoError) {
        m_downloadFailCount++;
        emit downloadError(modName, reply->errorString());
        return;
    }

    QString savePath = reply->property("savePath").toString();
    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(reply->readAll());
        file.close();
        m_downloadSuccessCount++;
        emit downloadFinished(modName, true, savePath);
    } else {
        m_downloadFailCount++;
        emit downloadError(modName, "Failed to save file: " + savePath);
    }
}

// ============================================================================
// ProfileManager
// ============================================================================

ProfileManager::ProfileManager(QObject* parent)
    : QObject(parent)
{
    loadProfileList();
}

QStringList ProfileManager::listProfiles() const {
    return m_profiles.keys();
}

bool ProfileManager::createProfile(const QString& name, const QString& description) {
    if (m_profiles.contains(name)) return false;

    Profile profile;
    profile.name = name;
    profile.description = description;
    profile.created = QDateTime::currentDateTime();
    profile.modified = QDateTime::currentDateTime();
    m_profiles[name] = profile;

    saveProfileList();
    emit profileCreated(name);
    emit profilesChanged();
    return true;
}

bool ProfileManager::deleteProfile(const QString& name) {
    if (name == "Default") return false;
    if (!m_profiles.contains(name)) return false;

    QFile::remove(profileFilePath(name));
    m_profiles.remove(name);

    saveProfileList();
    emit profileDeleted(name);
    emit profilesChanged();
    return true;
}

bool ProfileManager::renameProfile(const QString& oldName, const QString& newName) {
    if (!m_profiles.contains(oldName) || m_profiles.contains(newName)) return false;

    QFile::rename(profileFilePath(oldName), profileFilePath(newName));
    Profile profile = m_profiles.take(oldName);
    profile.name = newName;
    profile.modified = QDateTime::currentDateTime();
    m_profiles[newName] = profile;

    saveProfileList();
    emit profileRenamed(oldName, newName);
    emit profilesChanged();
    return true;
}

bool ProfileManager::duplicateProfile(const QString& name, const QString& newName) {
    if (!m_profiles.contains(name) || m_profiles.contains(newName)) return false;

    Profile profile = m_profiles[name];
    profile.name = newName;
    profile.created = QDateTime::currentDateTime();
    profile.modified = QDateTime::currentDateTime();
    m_profiles[newName] = profile;

    QString srcPath = profileFilePath(name);
    QString dstPath = profileFilePath(newName);
    QFile::copy(srcPath, dstPath);

    saveProfileList();
    emit profileCreated(newName);
    emit profilesChanged();
    return true;
}

bool ProfileManager::saveProfile(const QString& name, const QVector<ModEntry>& mods, const QMap<QString, int>& priorities) {
    if (!m_profiles.contains(name)) return false;

    QJsonObject root;
    root["name"] = name;
    root["created"] = m_profiles[name].created.toString(Qt::ISODate);
    root["modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray modsArray;
    for (const auto& mod : mods) {
        QJsonObject m;
        m["name"] = mod.name;
        m["enabled"] = mod.enabled;
        if (priorities.contains(mod.name))
            m["priority"] = priorities[mod.name];
        modsArray.append(m);
    }
    root["mods"] = modsArray;

    QFile file(profileFilePath(name));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
        m_profiles[name].modified = QDateTime::currentDateTime();
        emit profilesChanged();
        return true;
    }
    return false;
}

bool ProfileManager::loadProfile(const QString& name, QVector<ModEntry>& mods, QMap<QString, int>& priorities) {
    if (!m_profiles.contains(name)) return false;

    QFile file(profileFilePath(name));
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonArray modsArray = root["mods"].toArray();

    for (const auto& mVal : modsArray) {
        QJsonObject m = mVal.toObject();
        QString modName = m["name"].toString();

        auto it = std::find_if(mods.begin(), mods.end(),
            [&](const ModEntry& e) { return e.name == modName; });
        if (it != mods.end()) {
            it->enabled = m["enabled"].toBool(true);
        }
        if (m.contains("priority")) {
            priorities[modName] = m["priority"].toInt();
        }
    }

    m_currentProfile = name;
    saveProfileList();
    emit profileLoaded(name);
    return true;
}

ProfileManager::Profile ProfileManager::getProfile(const QString& name) const {
    return m_profiles.value(name);
}

void ProfileManager::setCurrentProfile(const QString& name) {
    if (m_profiles.contains(name)) {
        m_currentProfile = name;
        emit profileLoaded(name);
    }
}

bool ProfileManager::exportProfile(const QString& name, const QString& filePath) {
    if (!m_profiles.contains(name)) return false;

    QString srcPath = profileFilePath(name);
    return QFile::copy(srcPath, filePath);
}

bool ProfileManager::importProfile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    QString name = root["name"].toString();
    if (name.isEmpty()) return false;

    if (m_profiles.contains(name)) {
        QString newName = name + "_imported";
        int counter = 1;
        while (m_profiles.contains(newName)) {
            newName = name + "_imported_" + QString::number(counter++);
        }
        name = newName;
    }

    Profile profile;
    profile.name = name;
    profile.description = "Imported";
    profile.created = QDateTime::currentDateTime();
    profile.modified = QDateTime::currentDateTime();
    m_profiles[name] = profile;

    QString dstPath = profileFilePath(name);
    QFile::copy(filePath, dstPath);

    saveProfileList();
    emit profileCreated(name);
    emit profilesChanged();
    return true;
}

QString ProfileManager::profilesDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/mods/profiles";
}

QString ProfileManager::profileFilePath(const QString& name) const {
    return profilesDir() + "/" + name + ".json";
}

void ProfileManager::loadProfileList() {
    m_profiles.clear();
    QDir dir(profilesDir());
    if (!dir.exists()) {
        dir.mkpath(".");
        Profile defaultProfile;
        defaultProfile.name = "Default";
        defaultProfile.created = QDateTime::currentDateTime();
        defaultProfile.modified = QDateTime::currentDateTime();
        m_profiles["Default"] = defaultProfile;
        saveProfileList();
        return;
    }

    for (const QFileInfo& fi : dir.entryInfoList({"*.json"}, QDir::Files)) {
        QFile file(fi.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        if (!doc.isObject()) continue;

        QJsonObject root = doc.object();
        Profile profile;
        profile.name = root["name"].toString(fi.completeBaseName());
        profile.description = root["description"].toString();
        profile.created = QDateTime::fromString(root["created"].toString(), Qt::ISODate);
        profile.modified = QDateTime::fromString(root["modified"].toString(), Qt::ISODate);
        m_profiles[profile.name] = profile;
    }

    if (!m_profiles.contains("Default")) {
        Profile defaultProfile;
        defaultProfile.name = "Default";
        defaultProfile.created = QDateTime::currentDateTime();
        defaultProfile.modified = QDateTime::currentDateTime();
        m_profiles["Default"] = defaultProfile;
    }
}

void ProfileManager::saveProfileList() {
    // Profiles are saved as individual files, but we maintain an index
    QDir dir(profilesDir());
    if (!dir.exists()) dir.mkpath(".");

    // Save each profile as a metadata-only file if the full data file doesn't exist yet
    for (auto it = m_profiles.begin(); it != m_profiles.end(); ++it) {
        QString path = profileFilePath(it.key());
        if (!QFile::exists(path)) {
            QJsonObject root;
            root["name"] = it.key();
            root["description"] = it.value().description;
            root["created"] = it.value().created.toString(Qt::ISODate);
            root["modified"] = it.value().modified.toString(Qt::ISODate);
            root["mods"] = QJsonArray();

            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(root).toJson());
                file.close();
            }
        }
    }
}

// ============================================================================
// ModInstallEngine
// ============================================================================

ModInstallEngine::ModInstallEngine(QObject* parent) : QObject(parent) {
    m_modDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/mods";
}

void ModInstallEngine::setModDirectory(const QString& dir) {
    m_modDir = dir;
}

void ModInstallEngine::setUseHardLinks(bool useHardLinks) {
    m_useHardLinks = useHardLinks;
}

bool ModInstallEngine::installMod(const QString& zipPath) {
    QFileInfo fi(zipPath);
    if (!fi.exists()) {
        emit installProgress(0, "File not found: " + zipPath);
        emit installFinished(fi.completeBaseName(), false);
        return false;
    }

    QString modName = fi.completeBaseName();
    QString installPath = getModInstallPath(modName);

    emit installProgress(10, "Creating directory...");
    QDir().mkpath(installPath);

    emit installProgress(30, "Extracting mod...");
    if (!extractZip(zipPath, installPath)) {
        emit installFinished(modName, false);
        return false;
    }

    // Read manifest if present
    QString manifestPath = installPath + "/manifest.json";
    if (QFile::exists(manifestPath)) {
        QJsonObject manifest = ModScanner().readModManifest(manifestPath);
        m_modMetadata[modName] = manifest;
    }

    m_installedMods[modName] = installPath;
    emit installProgress(100, "Installation complete");
    emit installFinished(modName, true);
    return true;
}

bool ModInstallEngine::uninstallMod(const QString& modName) {
    if (!m_installedMods.contains(modName)) return false;

    QString path = m_installedMods[modName];
    QDir dir(path);
    bool removed = true;
    if (dir.exists()) {
        createBackup(modName);
        removed = dir.removeRecursively();
        if (!removed) {
            qWarning() << "ModManager: Failed to remove directory" << path;
        }
    }

    m_installedMods.remove(modName);
    emit uninstallFinished(modName, removed);
    return removed;
}

bool ModInstallEngine::enableMod(const QString& modName) {
    if (!m_installedMods.contains(modName)) return false;
    QString path = m_installedMods[modName];
    if (path.endsWith(".disabled")) {
        QString newPath = path.chopped(9);
        if (!QFile::rename(path, newPath)) {
            qWarning() << "ModManager: Failed to enable mod" << modName << "- rename failed";
            return false;
        }
        m_installedMods[modName] = newPath;
    }
    return true;
}

bool ModInstallEngine::disableMod(const QString& modName) {
    if (!m_installedMods.contains(modName)) return false;
    QString path = m_installedMods[modName];
    if (!path.endsWith(".disabled")) {
        QString newPath = path + ".disabled";
        if (!QFile::rename(path, newPath)) {
            qWarning() << "ModManager: Failed to disable mod" << modName << "- rename failed";
            return false;
        }
        m_installedMods[modName] = newPath;
    }
    return true;
}

bool ModInstallEngine::createBackup(const QString& modName) {
    if (!m_installedMods.contains(modName)) return false;

    QString srcPath = m_installedMods[modName];
    QString backupPath = getBackupPath(modName);

    QDir().mkpath(QFileInfo(backupPath).absolutePath());

    // Copy recursively
    QDir srcDir(srcPath);
    QDir dstDir(backupPath);
    dstDir.removeRecursively();

    QDirIterator it(srcPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString srcFile = it.next();
        QString relPath = srcDir.relativeFilePath(srcFile);
        QString dstFile = backupPath + "/" + relPath;

        if (QFileInfo(srcFile).isDir()) {
            QDir().mkpath(dstFile);
        } else {
            QFile::copy(srcFile, dstFile);
        }
    }

    m_backupPaths[modName] = backupPath;
    return true;
}

bool ModInstallEngine::restoreBackup(const QString& modName) {
    if (!m_backupPaths.contains(modName)) return false;

    QString backupPath = m_backupPaths[modName];
    QString targetPath = getModInstallPath(modName);

    QDir targetDir(targetPath);
    targetDir.removeRecursively();

    QDir backupDir(backupPath);
    QDirIterator it(backupPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    bool allOk = true;
    while (it.hasNext()) {
        QString srcFile = it.next();
        QString relPath = backupDir.relativeFilePath(srcFile);
        QString dstFile = targetPath + "/" + relPath;

        if (QFileInfo(srcFile).isDir()) {
            QDir().mkpath(dstFile);
        } else {
            if (!QFile::copy(srcFile, dstFile)) {
                qWarning() << "ModManager: Failed to restore" << srcFile;
                allOk = false;
            }
        }
    }

    return allOk;
}

QString ModInstallEngine::getModPath(const QString& modName) const {
    return m_installedMods.value(modName);
}

bool ModInstallEngine::extractZip(const QString& zipPath, const QString& outputDir) {
    // Try using 7-Zip C++ library first (direct integration)
    QJsonObject result = ks::archive::SevenZipLibrary::instance()->extract(zipPath, outputDir);
    if (result.value("success").toBool()) {
        return true;
    }

    // Fallback: Try using external 7-Zip command line tool
    QStringList pathsToTry = {
        "C:/Program Files/7-Zip/7z.exe",
        "C:/Program Files (x86)/7-Zip/7z.exe",
        "7z"
    };

    for (const QString& path : pathsToTry) {
        QProcess proc;
        QStringList args;
        args << "x" << zipPath << "-o" + outputDir << "-y";
        proc.start(path, args);
        if (proc.waitForFinished(60000) && proc.exitCode() == 0) {
            return true;
        }
    }

    // Fallback: minimal ZIP extraction using QIODevice
    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    // Try using PowerShell's Expand-Archive
    QProcess ps;
    QStringList psArgs;
    psArgs << "-Command"
           << QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
              .arg(zipPath).arg(outputDir);
    ps.start("powershell", psArgs);
    if (ps.waitForFinished(60000) && ps.exitCode() == 0) {
        return true;
    }

    return false;
}

bool ModInstallEngine::createHardLink(const QString& target, const QString& link) {
    QProcess proc;
    QStringList args;
    args << "/c" << "mklink" << "/H" << link << target;
    proc.start("cmd", args);
    return proc.waitForFinished(5000) && proc.exitCode() == 0;
}

QString ModInstallEngine::getModInstallPath(const QString& modName) const {
    return m_modDir + "/" + modName;
}

QString ModInstallEngine::getBackupPath(const QString& modName) const {
    return m_modDir + "/.backups/" + modName;
}

// ============================================================================
// ModInstallEngine — Batch Operations
// ============================================================================

bool ModInstallEngine::installMods(const QStringList& zipPaths) {
    m_lastBatchResult = BatchResult();
    m_batchTotal = zipPaths.size();
    m_batchCurrent = 0;

    for (const QString& zipPath : zipPaths) {
        QFileInfo fi(zipPath);
        QString modName = fi.completeBaseName();

        emit batchInstallProgress(m_batchCurrent, m_batchTotal, modName);

        if (m_installedMods.contains(modName)) {
            m_lastBatchResult.skipped.append(modName);
        } else if (installMod(zipPath)) {
            m_lastBatchResult.succeeded.append(modName);
        } else {
            m_lastBatchResult.failed.append(modName);
        }
        m_batchCurrent++;
    }

    emit batchInstallFinished(m_lastBatchResult);
    return m_lastBatchResult.failed.isEmpty();
}

bool ModInstallEngine::uninstallMods(const QStringList& modNames) {
    m_lastBatchResult = BatchResult();
    m_batchTotal = modNames.size();
    m_batchCurrent = 0;

    for (const QString& modName : modNames) {
        if (!m_installedMods.contains(modName)) {
            m_lastBatchResult.skipped.append(modName);
            continue;
        }

        if (uninstallMod(modName)) {
            m_lastBatchResult.succeeded.append(modName);
        } else {
            m_lastBatchResult.failed.append(modName);
        }
        m_batchCurrent++;
    }

    return m_lastBatchResult.failed.isEmpty();
}

// ============================================================================
// ModInstallEngine — File Integrity
// ============================================================================

QString ModInstallEngine::calculateFileHash(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (hash.addData(&file)) {
        return hash.result().toHex();
    }
    return QString();
}

bool ModInstallEngine::verifySingleFile(const QString& filePath, const QString& expectedHash) const {
    if (expectedHash.isEmpty()) return true; // No hash to verify against
    QString actualHash = calculateFileHash(filePath);
    return actualHash == expectedHash;
}

void ModInstallEngine::saveIntegrityManifest(const QString& modName, const QMap<QString, QString>& hashes) {
    QString manifestPath = getModInstallPath(modName) + "/.integrity.json";
    QJsonObject root;
    QJsonObject hashesObj;
    for (auto it = hashes.begin(); it != hashes.end(); ++it) {
        hashesObj[it.key()] = it.value();
    }
    root["hashes"] = hashesObj;
    root["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["modName"] = modName;

    QFile file(manifestPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

QMap<QString, QString> ModInstallEngine::loadIntegrityManifest(const QString& modName) const {
    QMap<QString, QString> hashes;
    QString manifestPath = getModInstallPath(modName) + "/.integrity.json";
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) return hashes;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return hashes;

    QJsonObject root = doc.object();
    QJsonObject hashesObj = root["hashes"].toObject();
    for (auto it = hashesObj.begin(); it != hashesObj.end(); ++it) {
        hashes[it.key()] = it.value().toString();
    }
    return hashes;
}

ModInstallEngine::IntegrityResult ModInstallEngine::verifyModIntegrity(const QString& modName) {
    IntegrityResult result;
    result.modName = modName;

    QString modPath = getModInstallPath(modName);
    if (!QDir(modPath).exists()) {
        result.intact = false;
        return result;
    }

    QMap<QString, QString> savedHashes = loadIntegrityManifest(modName);
    bool hasManifest = !savedHashes.isEmpty();

    QDirIterator it(modPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = it.next();
        QString relPath = QDir(modPath).relativeFilePath(filePath);

        // Skip integrity manifest itself and state files
        if (relPath.startsWith(".integrity.json") || relPath.startsWith(".state.json")) continue;

        result.totalFiles++;
        result.checkedFiles++;

        if (hasManifest && savedHashes.contains(relPath)) {
            if (!verifySingleFile(filePath, savedHashes[relPath])) {
                result.intact = false;
                result.corruptedFiles++;
                result.corruptedPaths.append(relPath);
            }
        }

        // Always compute hash for manifest building
        result.fileHashes[relPath] = calculateFileHash(filePath);
    }

    // Save manifest if we don't have one, or update if files changed
    if (!hasManifest || result.corruptedFiles > 0) {
        saveIntegrityManifest(modName, result.fileHashes);
    }

    return result;
}

QVector<ModInstallEngine::IntegrityResult> ModInstallEngine::verifyAllModsIntegrity() {
    QVector<IntegrityResult> results;
    QStringList mods = getInstalledMods();
    int total = mods.size();
    int current = 0;

    for (const QString& modName : mods) {
        emit integrityCheckProgress(current, total, modName);
        IntegrityResult result = verifyModIntegrity(modName);
        results.append(result);
        emit integrityCheckFinished(result);
        current++;
    }

    return results;
}

bool ModInstallEngine::repairMod(const QString& modName) {
    if (!m_installedMods.contains(modName)) return false;

    IntegrityResult result = verifyModIntegrity(modName);
    if (result.intact) return true;

    // Try to restore from backup
    if (m_backupPaths.contains(modName) || QFile::exists(getBackupPath(modName))) {
        return restoreBackup(modName);
    }

    // If no backup, we can only report the issue
    return false;
}

// ============================================================================
// ModRepository
// ============================================================================

ModRepository::ModRepository(QObject* parent) : QObject(parent) {
    m_networkManager = new QNetworkAccessManager(this);
}

ModRepository::~ModRepository() {
    delete m_networkManager;
}

void ModRepository::setWorkshopManager(WorkshopManager* workshop) {
    m_workshop = workshop;
    if (m_workshop) {
        buildIndexFromWorkshop();
    }
}

void ModRepository::addRemoteSource(const QString& name, const QString& baseUrl) {
    m_remoteSources[name] = baseUrl;
}

void ModRepository::buildIndexFromWorkshop() {
    m_modIndex.clear();
    if (!m_workshop) return;

    auto items = m_workshop->getPublishedItems();
    for (const auto& item : items) {
        auto infos = convertWorkshopItem(item);
        for (const auto& info : infos) {
            m_modIndex[info.name].append(info);
        }
    }
    emit repositoryUpdated();
}

QVector<ModRepository::ModInfo> ModRepository::convertWorkshopItem(const WorkshopItem& item) const {
    QVector<ModInfo> result;
    ModInfo info;
    info.name = item.name;
    info.version = item.version;
    info.id = item.id;
    info.author = item.author;
    info.category = item.category;
    info.description = item.description;
    info.tags = item.tags;
    info.fileSize = item.fileSize;
    info.rating = item.rating;
    info.downloadCount = item.downloadCount;
    info.downloadUrl = item.packagePath; // Local path for now

    // Convert simple dependency strings to VersionSpec (exact match by default)
    for (const QString& dep : item.dependencies) {
        VersionSpec spec;
        spec.op = VersionSpec::Eq;
        spec.raw = dep;
        // Try to parse version constraint like "name (>= 1.0.0)"
        QRegularExpression re(R"(^(\S+)\s*\(([^)]+)\)\s*$)");
        auto match = re.match(dep);
        if (match.hasMatch()) {
            info.dependencies[match.captured(1)] = VersionSpec::fromString(match.captured(2).trimmed());
        } else {
            info.dependencies[dep] = spec;
        }
    }

    // Convert conflicts
    info.conflicts = item.conflicts;

    result.append(info);
    return result;
}

QVector<ModRepository::ModInfo> ModRepository::search(const QString& query,
                                                      const QString& category,
                                                      const QStringList& tags) const {
    QVector<ModInfo> results;

    for (auto it = m_modIndex.begin(); it != m_modIndex.end(); ++it) {
        for (const auto& info : it.value()) {
            if (!query.isEmpty() &&
                !info.name.contains(query, Qt::CaseInsensitive) &&
                !info.description.contains(query, Qt::CaseInsensitive) &&
                !info.author.contains(query, Qt::CaseInsensitive)) {
                continue;
            }
            if (!category.isEmpty() && info.category.compare(category, Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (!tags.isEmpty()) {
                bool hasTag = false;
                for (const QString& tag : tags) {
                    if (info.tags.contains(tag, Qt::CaseInsensitive)) {
                        hasTag = true;
                        break;
                    }
                }
                if (!hasTag) continue;
            }
            results.append(info);
        }
    }
    return results;
}

QVector<ModRepository::ModInfo> ModRepository::getModVersions(const QString& name) const {
    return m_modIndex.value(name);
}

ModRepository::ModInfo* ModRepository::resolveDependency(const QString& depName, const VersionSpec& spec) {
    auto it = m_modIndex.find(depName);
    if (it == m_modIndex.end()) return nullptr;

    ModInfo* best = nullptr;
    for (auto& info : it.value()) {
        if (spec.op == VersionSpec::Any || spec.matches(Version::fromString(info.version))) {
            if (!best || Version::fromString(info.version).compare(Version::fromString(best->version)) > 0) {
                best = &info;
            }
        }
    }
    return best;
}

bool ModRepository::fetchAndInstall(const QString& modName, const VersionSpec& spec) {
    auto it = m_modIndex.find(modName);
    if (it == m_modIndex.end() || it.value().isEmpty()) {
        emit fetchFinished(modName, false, "Mod not found in repository");
        return false;
    }

    // Find best matching version
    ModInfo* best = nullptr;
    for (auto& info : it.value()) {
        Version v = Version::fromString(info.version);
        if (!v.isValid()) continue;
        if (spec.op == VersionSpec::Any || spec.matches(v)) {
            if (!best || v.compare(Version::fromString(best->version)) > 0) {
                best = &info;
            }
        }
    }

    if (!best) {
        emit fetchFinished(modName, false, "No matching version found");
        return false;
    }

    QString packagePath;
    if (!downloadMod(*best, packagePath)) {
        emit fetchFinished(modName, false, "Download failed");
        return false;
    }

    // Install via ModManagerModule if available
    if (auto* mod = ModManagerModule::instance()) {
        mod->installModWithDependencies(packagePath);
        emit fetchFinished(modName, true);
        return true;
    }

    emit fetchFinished(modName, false, "ModManager not initialized");
    return false;
}

bool ModRepository::downloadMod(const ModInfo& info, QString& outputPath) {
    // For local workshop packages, just copy
    if (info.downloadUrl.startsWith("file://") || QFileInfo(info.downloadUrl).exists()) {
        outputPath = info.downloadUrl;
        if (outputPath.startsWith("file://")) outputPath = outputPath.mid(7);
        emit fetchProgress(info.name, 100);
        return true;
    }

    // For remote URLs, download via network
    if (info.downloadUrl.startsWith("http://") || info.downloadUrl.startsWith("https://")) {
        QString tempDir = QDir::tempPath() + "/kseditor_downloads/";
        QDir().mkpath(tempDir);
        QString fileName = info.name + "_" + info.version + ".zip";
        outputPath = tempDir + fileName;

        bool success = downloadRemoteFile(info.downloadUrl, outputPath);
        if (success) {
            emit fetchProgress(info.name, 100);
        }
        return success;
    }

    return false;
}

bool ModRepository::downloadRemoteFile(const QString& url, const QString& outputPath)
{
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(30000);

    QNetworkReply* reply = m_networkManager->get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    // Timeout safety
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(60000);

    loop.exec();

    if (!reply->isFinished() || reply->error() != QNetworkReply::NoError) {
        reply->abort();
        reply->deleteLater();
        return false;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(data);
    file.close();

    return true;
}

void ModRepository::refresh() {
    buildIndexFromWorkshop();
}

// ============================================================================
// DependencyTreeDialog
// ============================================================================

DependencyTreeDialog::DependencyTreeDialog(const QVector<ModEntry>& allMods,
                                           const QString& rootMod,
                                           QWidget* parent)
    : QDialog(parent), m_mods(allMods), m_rootMod(rootMod)
{
    setWindowTitle("Dependency Tree: " + rootMod);
    setMinimumSize(500, 400);
    resize(600, 500);
    setupUI();
    populateTree();
}

void DependencyTreeDialog::setupUI()
{
    auto* layout = new QVBoxLayout(this);

    auto* header = new QLabel(QString("<b>Dependency Tree</b> — %1").arg(m_rootMod));
    layout->addWidget(header);

    m_tree = new QTreeWidget();
    m_tree->setHeaderLabels({tr("Mod"), tr("Version"), tr("Status")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setAnimated(true);
    m_tree->setIndentation(20);
    m_tree->setColumnWidth(0, 200);
    m_tree->setColumnWidth(1, 100);
    layout->addWidget(m_tree, 1);

    auto* btnRow = new QHBoxLayout();
    auto* expandBtn = new QPushButton(tr("Expand All"));
    auto* collapseBtn = new QPushButton(tr("Collapse All"));
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setStyleSheet("background: #4a6a8a; color: white;");
    btnRow->addWidget(expandBtn);
    btnRow->addWidget(collapseBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    connect(expandBtn, &QPushButton::clicked, m_tree, &QTreeWidget::expandAll);
    connect(collapseBtn, &QPushButton::clicked, m_tree, &QTreeWidget::collapseAll);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void DependencyTreeDialog::populateTree()
{
    m_tree->clear();
    if (m_rootMod.isEmpty()) return;

    QSet<QString> visited;
    auto rootIt = std::find_if(m_mods.begin(), m_mods.end(),
        [&](const ModEntry& m) { return m.name == m_rootMod; });

    auto* rootItem = new QTreeWidgetItem(m_tree);
    if (rootIt != m_mods.end()) {
        rootItem->setText(0, rootIt->name);
        rootItem->setText(1, rootIt->version.toString());
        rootItem->setText(2, rootIt->enabled ? "Enabled" : "Disabled");
        if (!rootIt->enabled)
            rootItem->setForeground(2, QColor(180, 120, 40));
    } else {
        rootItem->setText(0, m_rootMod);
        rootItem->setText(2, "Not installed");
        rootItem->setForeground(2, QColor(200, 60, 60));
    }

    visited.insert(m_rootMod);
    addDepChildren(rootItem, m_rootMod, visited, 0);
    m_tree->expandToDepth(1);
}

void DependencyTreeDialog::addDepChildren(QTreeWidgetItem* parent,
                                          const QString& modName,
                                          QSet<QString>& visited, int depth)
{
    if (m_maxDepth >= 0 && depth >= m_maxDepth) return;

    auto it = std::find_if(m_mods.begin(), m_mods.end(),
        [&](const ModEntry& m) { return m.name == modName; });
    if (it == m_mods.end()) return;

    for (const QString& dep : it->dependencies) {
        auto* child = new QTreeWidgetItem(parent);

        auto depIt = std::find_if(m_mods.begin(), m_mods.end(),
            [&](const ModEntry& m) { return m.name == dep; });

        bool satisfied = false;
        if (depIt != m_mods.end() && depIt->enabled) {
            VersionSpec spec = it->dependencySpecs.value(dep);
            if (!spec.isValid() || spec.matches(depIt->version)) {
                satisfied = true;
            }
        }

        child->setText(0, dep);
        if (depIt != m_mods.end()) {
            child->setText(1, depIt->version.toString());
        } else {
            child->setText(1, "-");
        }

        if (!satisfied) {
            child->setText(2, "MISSING / VERSION MISMATCH");
            child->setForeground(2, QColor(200, 60, 60));
            child->setForeground(0, QColor(200, 60, 60));
        } else if (depIt != m_mods.end() && !depIt->enabled) {
            child->setText(2, "Disabled");
            child->setForeground(2, QColor(180, 120, 40));
        } else {
            child->setText(2, "OK");
            child->setForeground(2, QColor(80, 180, 80));
        }

        // Add version spec info as tooltip
        VersionSpec spec = it->dependencySpecs.value(dep);
        if (spec.isValid() && spec.op != VersionSpec::Any) {
            QString tip = QString("Required: %1 %2\nInstalled: %3\nSatisfied: %4")
                .arg(VersionSpec::opToString(spec.op))
                .arg(spec.version.toString())
                .arg(depIt != m_mods.end() ? depIt->version.toString() : "-")
                .arg(satisfied ? "Yes" : "No");
            child->setToolTip(0, tip);
            child->setToolTip(1, tip);
            child->setToolTip(2, tip);
        }

        if (!visited.contains(dep)) {
            visited.insert(dep);
            addDepChildren(child, dep, visited, depth + 1);
        }
    }
}

// ============================================================================
// ModManagerModule
// ============================================================================

static ModManagerModule* s_modManagerInstance = nullptr;

ModManagerModule* ModManagerModule::instance() {
    return s_modManagerInstance;
}

ModManagerModule::ModManagerModule(QWidget* parent)
    : EditorModule(parent)
{
    s_modManagerInstance = this;

    m_modDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/mods";
    m_installEngine = new ModInstallEngine(this);
    m_dependencyResolver = new DependencyResolver(this);
    m_updateChecker = new ModUpdateChecker(this);
    m_profileManager = new ProfileManager(this);
    m_repository = new ModRepository(this);
    m_conflictDetector = new ModConflictDetector(this);
    m_collectionManager = new CollectionManager(this);
    m_collectionManager->loadCollections();
    m_installEngine->setModDirectory(m_modDirectory);
    m_pendingUpdates = 0;

    // Connect repository to WorkshopManager if available
    if (WorkshopManager::instance()) {
        m_repository->setWorkshopManager(WorkshopManager::instance());
    }
}

ModManagerModule::~ModManagerModule() = default;

bool ModManagerModule::initialize() {
    refreshMods();
    loadModState();
    syncModsWithState();
    scanACContentMods();
    return true;
}

void ModManagerModule::shutdown() {
    saveCurrentProfile();
    saveModState();
}

void ModManagerModule::checkForUpdates() {
    m_updateChecker->checkForUpdates(m_mods);
}

void ModManagerModule::switchProfile(const QString& name) {
    if (m_profileManager->currentProfile() == name) return;
    saveCurrentProfile();
    m_profileManager->setCurrentProfile(name);
    loadModsFromDisk();
    m_profileManager->loadProfile(name, m_mods, m_priorities);
    emit profileChanged(name);
    emit modsChanged();
}

void ModManagerModule::createProfile(const QString& name) {
    if (m_profileManager->createProfile(name)) {
        populateProfileCombo();
    }
}

void ModManagerModule::deleteProfile(const QString& name) {
    if (m_profileManager->deleteProfile(name)) {
        populateProfileCombo();
    }
}

void ModManagerModule::saveCurrentProfile() {
    QString current = m_profileManager->currentProfile();
    m_profileManager->saveProfile(current, m_mods, m_priorities);
}

void ModManagerModule::showUpdateDetails() {
    if (m_pendingUpdates > 0) {
        QMessageBox::information(nullptr, tr("Updates Available"),
            tr("%1 update(s) available.\n\nCheck the mod list for highlighted items.")
            .arg(m_pendingUpdates));
    } else {
        QMessageBox::information(nullptr, tr("No Updates"), tr("All mods are up to date."));
    }
}

QDockWidget* ModManagerModule::getOrCreateDockWidget(QMainWindow* mainWindow) {
    auto* dock = new QDockWidget(tr("Mod Manager"), mainWindow);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // Header with profile selector
    auto* headerRow = new QHBoxLayout();
    auto* header = new QLabel(tr("<b>Mod Manager</b>"));
    headerRow->addWidget(header);

    m_profileCombo = new QComboBox();
    m_profileCombo->setToolTip(tr("Switch mod profile"));
    m_profileCombo->setMinimumWidth(100);
    headerRow->addWidget(m_profileCombo);
    layout->addLayout(headerRow);

    // Search bar
    auto* searchBar = new QLineEdit();
    searchBar->setPlaceholderText(tr("Search mods..."));
    searchBar->setClearButtonEnabled(true);
    layout->addWidget(searchBar);

    // Mod list
    m_modList = new QListWidget();
    m_modList->setAlternatingRowColors(true);
    m_modList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_modList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_modList->setDragDropMode(QAbstractItemView::InternalMove);
    m_modList->setDefaultDropAction(Qt::MoveAction);
    m_modList->setDragEnabled(true);
    m_modList->setAcceptDrops(true);
    m_modList->setDropIndicatorShown(true);
    layout->addWidget(m_modList, 1);

    // File system watcher for auto-refresh
    m_fsWatcher = new QFileSystemWatcher(this);
    QString modDir = m_modDirectory;
    if (!modDir.isEmpty() && QDir(modDir).exists()) {
        m_fsWatcher->addPath(modDir);
    }

    // Summary
    m_summaryLabel = new QLabel(tr("No mods loaded"));
    m_summaryLabel->setStyleSheet("color: #888; font-size: 11px;");
    layout->addWidget(m_summaryLabel);

    // Conflict warning
    m_conflictLabel = new QLabel();
    m_conflictLabel->setStyleSheet("color: #ff6b35; font-size: 11px; padding: 2px;");
    m_conflictLabel->setVisible(false);
    layout->addWidget(m_conflictLabel);

    // Button row 1: Install / Uninstall / Refresh
    auto* row1 = new QHBoxLayout();
    auto* installBtn = new QPushButton(QIcon(":/icons/add.svg"), tr("Install"));
    row1->addWidget(installBtn);

    auto* uninstallBtn = new QPushButton(tr("Uninstall"));
    uninstallBtn->setEnabled(false);
    row1->addWidget(uninstallBtn);

    auto* refreshBtn = new QPushButton(tr("Refresh"));
    row1->addWidget(refreshBtn);
    layout->addLayout(row1);

    // Button row 2: Enable/Disable / Backup / Restore / Updates
    auto* row2 = new QHBoxLayout();
    m_toggleBtn = new QPushButton(tr("Disable"));
    m_toggleBtn->setEnabled(false);
    row2->addWidget(m_toggleBtn);

    auto* backupBtn = new QPushButton(tr("Backup"));
    backupBtn->setEnabled(false);
    row2->addWidget(backupBtn);

    auto* restoreBtn = new QPushButton(tr("Restore"));
    restoreBtn->setEnabled(false);
    row2->addWidget(restoreBtn);

    m_updateBtn = new QPushButton(tr("Check Updates"));
    m_updateBtn->setStyleSheet("background: #2a6a9a; color: white;");
    row2->addWidget(m_updateBtn);
    layout->addLayout(row2);

    // Button row 3: Integrity / Batch / Download
    auto* row3 = new QHBoxLayout();
    auto* integrityBtn = new QPushButton(tr("Verify Integrity"));
    integrityBtn->setToolTip(tr("Verify all installed mods for corruption"));
    integrityBtn->setStyleSheet("background: #6a4a8a; color: white;");
    row3->addWidget(integrityBtn);

    auto* batchInstallBtn = new QPushButton(tr("Batch Install"));
    batchInstallBtn->setToolTip(tr("Install multiple mods at once"));
    row3->addWidget(batchInstallBtn);

    auto* selectAllBtn = new QPushButton(tr("Select All"));
    row3->addWidget(selectAllBtn);

    auto* deselectAllBtn = new QPushButton(tr("Deselect All"));
    row3->addWidget(deselectAllBtn);
    layout->addLayout(row3);

    // Button row 4: Download Updates
    auto* row4 = new QHBoxLayout();
    auto* downloadUpdatesBtn = new QPushButton(tr("Download Updates"));
    downloadUpdatesBtn->setToolTip(tr("Download all available updates"));
    downloadUpdatesBtn->setStyleSheet("background: #2a8a4a; color: white;");
    row4->addWidget(downloadUpdatesBtn);

    auto* repairBtn = new QPushButton(tr("Repair All"));
    repairBtn->setToolTip(tr("Repair corrupted mods from backup"));
    repairBtn->setStyleSheet("background: #8a6a2a; color: white;");
    row4->addWidget(repairBtn);
    layout->addLayout(row4);

    // Button row 5: Dependencies & Load Order
    auto* row5 = new QHBoxLayout();
    auto* depTreeBtn = new QPushButton(tr("Dep Tree"));
    depTreeBtn->setToolTip(tr("Show dependency tree for selected mod"));
    depTreeBtn->setEnabled(false);
    depTreeBtn->setStyleSheet("background: #4a6a8a; color: white;");
    row5->addWidget(depTreeBtn);

    auto* resolveVCBtn = new QPushButton(tr("Fix Versions"));
    resolveVCBtn->setToolTip(tr("Resolve version conflicts among enabled mods"));
    resolveVCBtn->setStyleSheet("background: #8a4a4a; color: white;");
    row5->addWidget(resolveVCBtn);

    auto* applyLoadOrderBtn = new QPushButton(tr("Apply Load Order"));
    applyLoadOrderBtn->setToolTip(tr("Calculate and apply optimal mod load order based on dependencies"));
    applyLoadOrderBtn->setStyleSheet("background: #4a6a8a; color: white;");
    row5->addWidget(applyLoadOrderBtn);
    layout->addLayout(row5);

    // Button row 6: AC Sync
    auto* row6 = new QHBoxLayout();
    auto* syncACBtn = new QPushButton(tr("Sync with AC"));
    syncACBtn->setToolTip(tr("Write priority.ini for Assetto Corsa"));
    syncACBtn->setStyleSheet("background: #6a4a8a; color: white;");
    row6->addWidget(syncACBtn);

    auto* validateACBtn = new QPushButton(tr("Validate AC"));
    validateACBtn->setToolTip(tr("Validate AC priority.ini"));
    row6->addWidget(validateACBtn);
    layout->addLayout(row6);

    // Button row 7: Collections & Graph
    auto* row7 = new QHBoxLayout();
    auto* collectionsBtn = new QPushButton(tr("Collections"));
    collectionsBtn->setToolTip(tr("Manage mod collections/groups"));
    collectionsBtn->setStyleSheet("background: #3b82f6; color: white;");
    row7->addWidget(collectionsBtn);

    auto* depGraphBtn = new QPushButton(tr("Dep Graph"));
    depGraphBtn->setToolTip(tr("Show dependency graph for selected mod"));
    depGraphBtn->setStyleSheet("background: #4a6a8a; color: white;");
    row7->addWidget(depGraphBtn);

    auto* statsBtn = new QPushButton(tr("Stats"));
    statsBtn->setToolTip(tr("Show mod statistics dashboard"));
    statsBtn->setStyleSheet("background: #6a4a8a; color: white;");
    row7->addWidget(statsBtn);

    layout->addLayout(row7);

    // Button row 8: Conflict Detection
    auto* row8 = new QHBoxLayout();
    m_scanConflictsBtn = new QPushButton(tr("Scan Conflicts"));
    m_scanConflictsBtn->setToolTip(tr("Scan for file overlaps between mods"));
    m_scanConflictsBtn->setStyleSheet("background: #8a2a2a; color: white;");
    row7->addWidget(m_scanConflictsBtn);
    layout->addLayout(row7);

    dock->setWidget(widget);

    // ── Connections ─────────────────────────────────────────────────────
    connect(installBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(nullptr, tr("Install Mod"), QString(),
            "Archives (*.zip *.7z *.rar);;All Files (*)");
        if (!path.isEmpty()) installMod(path);
    });

    connect(uninstallBtn, &QPushButton::clicked, this, [this]() {
        auto* item = m_modList->currentItem();
        if (item) {
            QString name = item->data(Qt::UserRole).toString();
            uninstallMod(name);
        }
    });

    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        refreshMods();
        checkForConflicts();
    });

    connect(m_toggleBtn, &QPushButton::clicked, this, [this]() {
        auto* item = m_modList->currentItem();
        if (!item) return;
        QString name = item->data(Qt::UserRole).toString();
        bool enabled = item->data(Qt::UserRole + 1).toBool();
        if (enabled)
            disableMod(name);
        else
            enableMod(name);
    });

    connect(backupBtn, &QPushButton::clicked, this, [this]() {
        auto* item = m_modList->currentItem();
        if (item) m_installEngine->createBackup(item->data(Qt::UserRole).toString());
    });

    connect(restoreBtn, &QPushButton::clicked, this, [this]() {
        auto* item = m_modList->currentItem();
        if (item) m_installEngine->restoreBackup(item->data(Qt::UserRole).toString());
    });

    connect(m_modList, &QListWidget::currentItemChanged, this,
        [this, uninstallBtn, backupBtn, restoreBtn, depTreeBtn](QListWidgetItem* current, QListWidgetItem*) {
            bool hasSel = current != nullptr;
            uninstallBtn->setEnabled(hasSel);
            m_toggleBtn->setEnabled(hasSel);
            backupBtn->setEnabled(hasSel);
            restoreBtn->setEnabled(hasSel);
            depTreeBtn->setEnabled(hasSel);
            if (hasSel) {
                bool enabled = current->data(Qt::UserRole + 1).toBool();
                m_toggleBtn->setText(enabled ? "Disable" : "Enable");
            }
        });

    connect(searchBar, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int i = 0; i < m_modList->count(); ++i) {
            auto* item = m_modList->item(i);
            item->setHidden(text.length() > 0 &&
                !item->text().contains(text, Qt::CaseInsensitive));
        }
    });

    connect(this, &ModManagerModule::modsChanged, this, [this]() {
        populateModList();
        checkForConflicts();
    });

    // Update checker connections
    connect(m_updateBtn, &QPushButton::clicked, this, [this]() {
        if (m_updateChecker->isChecking()) {
            m_updateChecker->cancelAll();
        } else {
            checkForUpdates();
        }
    });

    connect(m_updateChecker, &ModUpdateChecker::checkProgress, this, [this](int current, int total) {
        if (total > 0) {
            m_updateBtn->setText(tr("Checking... %1/%2").arg(current).arg(total));
        }
    });

    connect(m_updateChecker, &ModUpdateChecker::checkFinished, this, [this](int updatesFound) {
        m_pendingUpdates = updatesFound;
        if (updatesFound > 0) {
            m_updateBtn->setText(tr("%1 Update(s) Available")
                .arg(updatesFound));
            m_updateBtn->setStyleSheet("background: #4CAF50; color: white; font-weight: bold;");
            emit updatesAvailable(updatesFound);
        } else {
            m_updateBtn->setText(tr("Check Updates"));
            m_updateBtn->setStyleSheet("background: #2a6a9a; color: white;");
        }
        handleUpdateResults();
    });

    connect(m_updateChecker, &ModUpdateChecker::updateFound, this, [this](const QString& modName, const QString&, const QString&) {
        for (auto& mod : m_mods) {
            if (mod.name == modName) {
                mod.hasUpdate = true;
                break;
            }
        }
    });

    // New button connections
    connect(integrityBtn, &QPushButton::clicked, this, [this, integrityBtn]() {
        integrityBtn->setText(tr("Verifying..."));
        integrityBtn->setEnabled(false);
        verifyAllIntegrity();
        QTimer::singleShot(100, this, [integrityBtn]() {
            integrityBtn->setText(tr("Verify Integrity"));
            integrityBtn->setEnabled(true);
        });
    });

    connect(batchInstallBtn, &QPushButton::clicked, this, [this]() {
        QStringList paths = QFileDialog::getOpenFileNames(nullptr, tr("Select Mods to Install"), QString(),
            "Archives (*.zip *.7z *.rar);;All Files (*)");
        if (!paths.isEmpty()) {
            installModsBatch(paths);
        }
    });

    connect(selectAllBtn, &QPushButton::clicked, this, &ModManagerModule::selectAllMods);
    connect(deselectAllBtn, &QPushButton::clicked, this, &ModManagerModule::deselectAllMods);

    connect(downloadUpdatesBtn, &QPushButton::clicked, this, [this, downloadUpdatesBtn]() {
        downloadUpdatesBtn->setText(tr("Downloading..."));
        downloadUpdatesBtn->setEnabled(false);
        downloadAllUpdates();
    });

    connect(m_updateChecker, &ModUpdateChecker::allDownloadsFinished, this,
        [downloadUpdatesBtn](int success, int fail) {
            downloadUpdatesBtn->setText(tr("Download Updates (%1 ok, %2 failed)").arg(success).arg(fail));
            downloadUpdatesBtn->setEnabled(true);
            QTimer::singleShot(3000, downloadUpdatesBtn, [downloadUpdatesBtn]() {
                downloadUpdatesBtn->setText(tr("Download Updates"));
            });
        });

    connect(repairBtn, &QPushButton::clicked, this, [this, repairBtn]() {
        repairBtn->setText(tr("Repairing..."));
        repairBtn->setEnabled(false);
        repairAllMods();
        QTimer::singleShot(100, this, [repairBtn]() {
            repairBtn->setText(tr("Repair All"));
            repairBtn->setEnabled(true);
        });
    });

    connect(depTreeBtn, &QPushButton::clicked, this, [this]() {
        auto* item = m_modList->currentItem();
        if (item) showDependencyTree(item->data(Qt::UserRole).toString());
    });

    connect(resolveVCBtn, &QPushButton::clicked, this, [this, resolveVCBtn]() {
        resolveVCBtn->setText(tr("Resolving..."));
        resolveVCBtn->setEnabled(false);
        resolveVersionConflicts();
        QTimer::singleShot(500, this, [resolveVCBtn]() {
            resolveVCBtn->setText(tr("Fix Versions"));
            resolveVCBtn->setEnabled(true);
        });
    });

    connect(applyLoadOrderBtn, &QPushButton::clicked, this, [this, applyLoadOrderBtn]() {
        applyLoadOrderBtn->setText(tr("Applying..."));
        applyLoadOrderBtn->setEnabled(false);
        applyLoadOrder();
        QTimer::singleShot(100, this, [applyLoadOrderBtn]() {
            applyLoadOrderBtn->setText(tr("Apply Load Order"));
            applyLoadOrderBtn->setEnabled(true);
        });
    });

    connect(syncACBtn, &QPushButton::clicked, this, [this, syncACBtn]() {
        syncACBtn->setText(tr("Syncing..."));
        syncACBtn->setEnabled(false);
        writeACPriorityIni();
        QMessageBox::information(nullptr, tr("AC Sync"), tr("priority.ini written successfully"));
        QTimer::singleShot(100, this, [syncACBtn]() {
            syncACBtn->setText(tr("Sync with AC"));
            syncACBtn->setEnabled(true);
        });
    });

    connect(validateACBtn, &QPushButton::clicked, this, [this, validateACBtn]() {
        bool valid = validateACPriorityIni();
        QMessageBox::information(nullptr, tr("AC Validation"),
            valid ? tr("priority.ini is valid") : tr("priority.ini is invalid or missing"));
    });

    connect(collectionsBtn, &QPushButton::clicked, this, [this]() {
        // Show collections dialog
        QStringList colList = m_collectionManager ? m_collectionManager->listCollections() : QStringList();
        if (colList.isEmpty()) {
            QMessageBox::StandardButton reply = QMessageBox::question(nullptr, tr("Collections"), tr("No collections yet. Create your first collection?"),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                createCollection("My Collection");
            }
            return;
        }

        auto* item = m_modList ? m_modList->currentItem() : nullptr;
        QString currentMod = item ? item->data(Qt::UserRole).toString() : QString();

        QStringList msgs;
        for (const QString& colId : colList) {
            ModCollection col = m_collectionManager->getCollection(colId);
            QString count = QString::number(col.modNames.size());
            bool hasMod = !currentMod.isEmpty() && col.modNames.contains(currentMod);
            msgs << QString("%1 (%2)%3").arg(col.name).arg(count)
                    .arg(hasMod ? " [contains current mod]" : "");
        }

        QMessageBox::information(nullptr, tr("Collections"),
            tr("Collections:\n\n%1").arg(msgs.join("\n")));
    });

    connect(depGraphBtn, &QPushButton::clicked, this, [this]() {
        showDependencyGraph();
    });

    connect(statsBtn, &QPushButton::clicked, this, [this]() {
        ModStats s = calculateStats();
        QString msg = QString(
            "<h3>Mod Statistics</h3>"
            "<table>"
            "<tr><td>Total Mods:</td><td><b>%1</b></td></tr>"
            "<tr><td>Enabled:</td><td><b>%2</b></td></tr>"
            "<tr><td>Disabled:</td><td><b>%3</b></td></tr>"
            "<tr><td>Built-in:</td><td><b>%4</b></td></tr>"
            "<tr><td>Updates Available:</td><td><b>%5</b></td></tr>"
            "<tr><td>With Dependencies:</td><td><b>%6</b></td></tr>"
            "<tr><td>With Conflicts:</td><td><b>%7</b></td></tr>"
            "<tr><td>Total Size:</td><td><b>%8</b></td></tr>"
            "<tr><td>Avg Mod Size:</td><td><b>%9</b></td></tr>"
            "</table>")
            .arg(s.totalMods).arg(s.enabledMods).arg(s.disabledMods)
            .arg(s.builtInMods).arg(s.hasUpdates).arg(s.withDependencies)
            .arg(s.withConflicts)
            .arg(s.totalSizeBytes > 1024LL * 1024 * 1024
                ? QString::number(s.totalSizeBytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB"
                : QString::number(s.totalSizeBytes / (1024.0 * 1024.0), 'f', 1) + " MB")
            .arg(QString::number(s.avgModSizeMB, 'f', 1) + " MB");

        // Category breakdown
        if (!s.categoryCounts.isEmpty()) {
            msg += "<h4>By Category</h4><table>";
            for (auto it = s.categoryCounts.begin(); it != s.categoryCounts.end(); ++it) {
                qint64 catSize = s.categorySizes.value(it.key(), 0);
                msg += QString("<tr><td>%1:</td><td>%2 mods, %3</td></tr>")
                    .arg(it.key()).arg(it.value())
                    .arg(catSize > 1024 * 1024
                        ? QString::number(catSize / (1024.0 * 1024.0), 'f', 1) + " MB"
                        : QString::number(catSize / 1024.0, 'f', 1) + " KB");
            }
            msg += "</table>";
        }

        QMessageBox::information(nullptr, "Mod Manager Stats", msg);
    });

    connect(m_modList->model(), &QAbstractItemModel::rowsMoved, this, [this](const QModelIndex&, int start, int, const QModelIndex&, int dest) {
        if (!m_dragDropEnabled) return;
        // After a drag-drop reorder, update priorities
        for (int i = 0; i < m_modList->count(); ++i) {
            auto* item = m_modList->item(i);
            if (item) {
                QString name = item->data(Qt::UserRole).toString();
                if (!name.isEmpty()) m_priorities[name] = i;
            }
        }
        saveModState();
        emit modsChanged();
    });

    connect(m_scanConflictsBtn, &QPushButton::clicked, this, [this]() {
        m_scanConflictsBtn->setText(tr("Scanning..."));
        m_scanConflictsBtn->setEnabled(false);
        scanForFileConflicts();
        QTimer::singleShot(100, this, [this]() {
            m_scanConflictsBtn->setText(tr("Scan Conflicts"));
            m_scanConflictsBtn->setEnabled(true);
        });
    });

    // Batch progress connections
    connect(this, &ModManagerModule::batchInstallProgress, this,
        [this](int current, int total, const QString& modName) {
            m_summaryLabel->setText(tr("Installing %1/%2: %3").arg(current).arg(total).arg(modName));
        });

    connect(this, &ModManagerModule::batchInstallFinished, this,
        [this](int success, int fail) {
            m_summaryLabel->setText(tr("Batch complete: %1 succeeded, %2 failed").arg(success).arg(fail));
        });

    // Integrity check connections
    connect(m_installEngine, &ModInstallEngine::integrityCheckProgress, this,
        [this](int current, int total, const QString& modName) {
            m_summaryLabel->setText(tr("Checking integrity %1/%2: %3").arg(current).arg(total).arg(modName));
        });

    connect(this, &ModManagerModule::integrityCheckFinished, this,
        [this](int totalChecked, int totalCorrupted) {
            if (totalCorrupted > 0) {
                m_summaryLabel->setText(tr("Integrity check: %1 files checked, %2 corrupted")
                    .arg(totalChecked).arg(totalCorrupted));
                m_conflictLabel->setText(tr("%1 corrupted file(s) detected").arg(totalCorrupted));
                m_conflictLabel->setVisible(true);
            } else {
                m_summaryLabel->setText(tr("Integrity check: %1 files verified, all OK").arg(totalChecked));
            }
        });

    // Profile connections
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx >= 0) {
            QString name = m_profileCombo->itemText(idx);
            if (name != m_profileManager->currentProfile()) {
                switchProfile(name);
            }
        }
    });

    // Context menu
    connect(m_modList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_modList->itemAt(pos);
        if (!item) return;

        QString name = item->data(Qt::UserRole).toString();
        bool enabled = item->data(Qt::UserRole + 1).toBool();

        QMenu menu(m_modList);
        QAction* toggleAction = menu.addAction(enabled ? tr("Disable") : tr("Enable"));
        menu.addSeparator();
        QAction* backupAction = menu.addAction(tr("Create Backup"));
        QAction* restoreAction = menu.addAction(tr("Restore Backup"));
            QMenu* collectionsMenu = menu.addMenu(tr("Add to Collection"));
            if (m_collectionManager) {
                QStringList colList = m_collectionManager->listCollections();
                if (colList.isEmpty()) {
                    collectionsMenu->addAction(tr("(No collections)"))->setEnabled(false);
                } else {
                    for (const QString& colId : colList) {
                        ModCollection col = m_collectionManager->getCollection(colId);
                        bool hasMod = col.modNames.contains(name);
                        QAction* colAction = collectionsMenu->addAction(
                            (hasMod ? QString::fromUtf8("✓ ") : "") + col.name);
                        if (hasMod) {
                            connect(colAction, &QAction::triggered, this, [this, colId, name]() {
                                removeModFromCollection(colId, name);
                            });
                        } else {
                            connect(colAction, &QAction::triggered, this, [this, colId, name]() {
                                addModToCollection(colId, name);
                            });
                        }
                    }
                }
                collectionsMenu->addSeparator();
                QAction* newColAction = collectionsMenu->addAction(tr("+ New Collection..."));
                connect(newColAction, &QAction::triggered, this, [this, name]() {
                    QString colName = QString("Collection for %1").arg(name);
                    createCollection(colName);
                    // Add current mod to new collection
                    QStringList cols = m_collectionManager->listCollections();
                    for (const QString& id : cols) {
                        ModCollection col = m_collectionManager->getCollection(id);
                        if (col.name == colName) {
                            addModToCollection(id, name);
                            break;
                        }
                    }
                });
            }

            menu.addSeparator();
            QAction* checkUpdateAction = menu.addAction(tr("Check for Updates"));
            menu.addSeparator();
            QAction* integrityAction = menu.addAction(tr("Verify Integrity"));
            menu.addSeparator();
            QAction* uninstallAction = menu.addAction(tr("Uninstall"));

        QAction* chosen = menu.exec(m_modList->mapToGlobal(pos));
        if (chosen == toggleAction) {
            if (enabled) disableMod(name); else enableMod(name);
        } else if (chosen == backupAction) {
            m_installEngine->createBackup(name);
        } else if (chosen == restoreAction) {
            m_installEngine->restoreBackup(name);
        } else if (chosen == checkUpdateAction) {
            for (const auto& m : m_mods) { if (m.name == name) { m_updateChecker->checkSingleMod(m); break; } }
        } else if (chosen == integrityAction) {
            verifyModIntegrity(name);
        } else if (chosen == uninstallAction) {
            auto reply = QMessageBox::question(m_modList, tr("Confirm"),
                tr("Uninstall %1?").arg(name), QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) uninstallMod(name);
        }
    });

    // Watch for file system changes
    connect(m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        if (!m_preloadScheduled) {
            m_preloadScheduled = true;
            QTimer::singleShot(2000, this, [this]() {
                m_preloadScheduled = false;
                refreshMods();
            });
        }
    });

    // Initial population
    populateProfileCombo();
    populateModList();

    return dock;
}

void ModManagerModule::refreshMods() {
    loadModsFromDisk();
    emit modsChanged();
    QTimer::singleShot(100, this, &ModManagerModule::preloadModDetails);
}

void ModManagerModule::installMod(const QString& zipPath) {
    QFileInfo fi(zipPath);
    QString modName = fi.completeBaseName();

    // Check for missing dependencies
    QStringList missingDeps = m_dependencyResolver->findMissing(m_mods, {modName});
    if (!missingDeps.isEmpty()) {
        QString message = QString("Mod '%1' has missing dependencies:\n\n%2\n\n")
            .arg(modName)
            .arg(missingDeps.join("\n"));
        message += "Install anyway? (Dependencies may need to be installed separately)";

        QMessageBox::StandardButton reply = QMessageBox::question(nullptr, tr("Missing Dependencies"), message,
            QMessageBox::Yes | QMessageBox::No);

        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    m_installEngine->installMod(zipPath);
    refreshMods();
    applyLoadOrder();
}

void ModManagerModule::uninstallMod(const QString& modName) {
    m_installEngine->uninstallMod(modName);

    auto it = std::find_if(m_mods.begin(), m_mods.end(),
        [&](const ModEntry& e) { return e.name == modName; });
    if (it != m_mods.end()) {
        m_mods.erase(it);
    }

    emit modsChanged();
    emit modUninstalled(modName);
}

void ModManagerModule::enableMod(const QString& modName) {
    m_installEngine->enableMod(modName);

    for (auto& m : m_mods) {
        if (m.name == modName) {
            m.enabled = true;
            break;
        }
    }

    saveModState();
    emit modsChanged();
}

void ModManagerModule::disableMod(const QString& modName) {
    m_installEngine->disableMod(modName);

    for (auto& m : m_mods) {
        if (m.name == modName) {
            m.enabled = false;
            break;
        }
    }

    saveModState();
    emit modsChanged();
}

void ModManagerModule::setModPriority(const QString& modName, int priority) {
    m_priorities[modName] = priority;
    saveModState();
}

void ModManagerModule::loadModsFromDisk() {
    m_mods.clear();
    QDir dir(m_modDirectory);

    if (!dir.exists()) {
        QDir().mkpath(m_modDirectory);
        return;
    }

    for (const QFileInfo& fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        ModEntry mod;
        mod.name = fi.fileName();
        if (mod.name.endsWith(".disabled")) {
            mod.name = mod.name.chopped(9);
            mod.enabled = false;
        }
        mod.path = fi.absoluteFilePath();
        mod.installDate = fi.lastModified();

        // Calculate size
        QDirIterator sizeIt(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
        while (sizeIt.hasNext()) {
            sizeIt.next();
            mod.sizeBytes += sizeIt.fileInfo().size();
        }

        // Read manifest
        QString manifestPath = fi.absoluteFilePath() + "/manifest.json";
        if (QFile::exists(manifestPath)) {
            QJsonObject manifest = ModScanner().readModManifest(manifestPath);
            mod.versionStr = manifest["version"].toString();
            mod.version = Version::fromString(mod.versionStr);
            mod.author = manifest["author"].toString();
            mod.description = manifest["description"].toString();
            mod.category = manifest["category"].toString();

            QJsonArray deps = manifest["dependencies"].toArray();
            for (const auto& d : deps) {
                QString depStr = d.toString();
                // Support "name (>= version)" and "name" formats
                QRegularExpression re(R"(^(\S+)\s*\(([^)]+)\)\s*$)");
                auto match = re.match(depStr);
                if (match.hasMatch()) {
                    QString depName = match.captured(1);
                    QString specStr = match.captured(2).trimmed();
                    mod.dependencies.append(depName);
                    mod.dependencySpecs[depName] = VersionSpec::fromString(specStr);
                } else {
                    mod.dependencies.append(depStr);
                }
            }

            QJsonArray conflicts = manifest["conflicts"].toArray();
            for (const auto& c : conflicts) mod.conflicts.append(c.toString());
        }

        // Restore saved priority
        if (m_priorities.contains(mod.name)) {
            mod.metadata["priority"] = QString::number(m_priorities[mod.name]);
        }

        m_mods.append(mod);
    }

    // Sort by priority
    std::sort(m_mods.begin(), m_mods.end(),
        [this](const ModEntry& a, const ModEntry& b) {
            int pa = m_priorities.value(a.name, 999);
            int pb = m_priorities.value(b.name, 999);
            return pa < pb;
        });
}

void ModManagerModule::saveModState() {
    QJsonObject state;
    QJsonArray mods;

    for (const auto& mod : m_mods) {
        QJsonObject m;
        m["name"] = mod.name;
        m["enabled"] = mod.enabled;
        if (m_priorities.contains(mod.name)) {
            m["priority"] = m_priorities[mod.name];
        }
        mods.append(m);
    }

    state["mods"] = mods;
    state["useHardLinks"] = m_installEngine->property("useHardLinks").toBool();

    QString statePath = m_modDirectory + "/.state.json";
    QFile file(statePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(state).toJson());
    }
}

void ModManagerModule::populateModList() {
    if (!m_modList) return;
    m_modList->clear();
    for (const auto& mod : m_mods) {
        QString label = mod.name;
        if (!mod.versionStr.isEmpty()) label += "  v" + mod.versionStr;
        if (!mod.author.isEmpty()) label += "  [" + mod.author + "]";
        if (mod.hasUpdate) label += "  [UPDATE: " + mod.newVersion + "]";
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, mod.name);
        item->setData(Qt::UserRole + 1, mod.enabled);
        if (!mod.enabled) {
            item->setForeground(QColor(140, 140, 140));
        }
        if (mod.hasUpdate) {
            item->setBackground(QColor(60, 100, 60));
            item->setForeground(QColor(200, 255, 200));
        }
        m_modList->addItem(item);
    }
    m_summaryLabel->setText(
        QString("%1 mods (%2 enabled, %3 disabled, %4 updates)")
        .arg(m_mods.size())
        .arg(std::count_if(m_mods.begin(), m_mods.end(), [](const ModEntry& e) { return e.enabled; }))
        .arg(std::count_if(m_mods.begin(), m_mods.end(), [](const ModEntry& e) { return !e.enabled; }))
        .arg(std::count_if(m_mods.begin(), m_mods.end(), [](const ModEntry& e) { return e.hasUpdate; })));
}

void ModManagerModule::populateProfileCombo() {
    if (!m_profileCombo) return;
    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    QStringList profiles = m_profileManager->listProfiles();
    for (const QString& name : profiles) {
        m_profileCombo->addItem(name);
    }
    int idx = m_profileCombo->findText(m_profileManager->currentProfile());
    if (idx >= 0) m_profileCombo->setCurrentIndex(idx);
    m_profileCombo->blockSignals(false);
}

void ModManagerModule::handleUpdateResults() {
    populateModList();
}


// ============================================================================
// ModManagerModule — Batch Operations
// ============================================================================

void ModManagerModule::installModsBatch(const QStringList& zipPaths) {
    m_pendingBatchInstalls = zipPaths;
    m_batchTotal = zipPaths.size();
    m_batchCurrent = 0;
    processBatchInstall();
}

void ModManagerModule::processBatchInstall() {
    if (m_pendingBatchInstalls.isEmpty()) {
        int successCount = m_installEngine->getLastBatchResult().succeeded.size();
        int failCount = m_installEngine->getLastBatchResult().failed.size();
        emit batchInstallFinished(successCount, failCount);
        refreshMods();
        return;
    }

    QString zipPath = m_pendingBatchInstalls.takeFirst();
    QString modName = QFileInfo(zipPath).completeBaseName();

    emit batchInstallProgress(m_batchCurrent, m_batchTotal, modName);

    m_installEngine->installMod(zipPath);

    // Verify integrity after install if enabled
    if (m_installEngine->autoVerifyIntegrity()) {
        m_installEngine->verifyModIntegrity(modName);
    }

    m_batchCurrent++;

    // Process next with a brief delay to keep UI responsive
    QTimer::singleShot(10, this, &ModManagerModule::processBatchInstall);
}

void ModManagerModule::uninstallModsBatch(const QStringList& modNames) {
    m_pendingBatchUninstalls = modNames;
    m_batchTotal = modNames.size();
    m_batchCurrent = 0;
    processBatchUninstall();
}

void ModManagerModule::processBatchUninstall() {
    if (m_pendingBatchUninstalls.isEmpty()) {
        int successCount = m_installEngine->getLastBatchResult().succeeded.size();
        int failCount = m_installEngine->getLastBatchResult().failed.size();
        emit batchInstallFinished(successCount, failCount);
        refreshMods();
        return;
    }

    QString modName = m_pendingBatchUninstalls.takeFirst();
    m_installEngine->uninstallMod(modName);

    auto it = std::find_if(m_mods.begin(), m_mods.end(),
        [&](const ModEntry& e) { return e.name == modName; });
    if (it != m_mods.end()) {
        m_mods.erase(it);
    }

    m_batchCurrent++;
    QTimer::singleShot(10, this, &ModManagerModule::processBatchUninstall);
}

void ModManagerModule::selectAllMods() {
    for (auto& mod : m_mods) {
        mod.enabled = true;
    }
    saveModState();
    emit modsChanged();
}

void ModManagerModule::deselectAllMods() {
    for (auto& mod : m_mods) {
        mod.enabled = false;
    }
    saveModState();
    emit modsChanged();
}

QStringList ModManagerModule::getSelectedMods() const {
    QStringList selected;
    for (const auto& mod : m_mods) {
        if (mod.enabled) selected.append(mod.name);
    }
    return selected;
}

// ============================================================================
// ModManagerModule — Integrity Operations
// ============================================================================

void ModManagerModule::verifyAllIntegrity() {
    QVector<ModInstallEngine::IntegrityResult> results = m_installEngine->verifyAllModsIntegrity();
    int totalChecked = 0;
    int totalCorrupted = 0;
    for (const auto& result : results) {
        totalChecked += result.checkedFiles;
        totalCorrupted += result.corruptedFiles;
    }
    emit integrityCheckFinished(totalChecked, totalCorrupted);
}

void ModManagerModule::verifyModIntegrity(const QString& modName) {
    ModInstallEngine::IntegrityResult result = m_installEngine->verifyModIntegrity(modName);
    handleIntegrityResult(result);
}

void ModManagerModule::repairAllMods() {
    QStringList mods = m_installEngine->getInstalledMods();
    for (const QString& modName : mods) {
        m_installEngine->repairMod(modName);
    }
    refreshMods();
}

void ModManagerModule::repairMod(const QString& modName) {
    m_installEngine->repairMod(modName);
    refreshMods();
}

void ModManagerModule::handleIntegrityResult(const ModInstallEngine::IntegrityResult& result) {
    if (!result.intact) {
        qWarning() << "Integrity check failed for" << result.modName
                    << "- corrupted files:" << result.corruptedFiles;
        for (const QString& path : result.corruptedPaths) {
            qWarning() << "  Corrupted:" << path;
        }
    }
}

// ============================================================================
// ModManagerModule — Download Operations
// ============================================================================

void ModManagerModule::downloadAllUpdates() {
    m_updateChecker->downloadAllUpdates();
}

void ModManagerModule::downloadModUpdate(const QString& modName) {
    // Find the update URL for this mod
    for (const auto& mod : m_mods) {
        if (mod.name == modName && !mod.updateUrl.isEmpty()) {
            m_updateChecker->downloadUpdate(modName, mod.updateUrl);
            return;
        }
    }
}

// ============================================================================
// ModManagerModule — Performance: Lazy Load Details
// ============================================================================

void ModManagerModule::loadModDetailsAsync(int index) {
    if (index < 0 || index >= m_mods.size()) return;

    ModEntry& mod = m_mods[index];
    if (!mod.description.isEmpty() && !mod.metadata.isEmpty()) return; // Already loaded

    // Load manifest details in background
    QString manifestPath = mod.path + "/manifest.json";
    if (QFile::exists(manifestPath)) {
        QJsonObject manifest = ModScanner().readModManifest(manifestPath);
        mod.versionStr = manifest["version"].toString();
        mod.version = Version::fromString(mod.versionStr);
        mod.author = manifest["author"].toString();
        mod.description = manifest["description"].toString();
        mod.category = manifest["category"].toString();

        QJsonArray deps = manifest["dependencies"].toArray();
        for (const auto& d : deps) {
            QString depStr = d.toString();
            QRegularExpression re(R"(^(\S+)\s*\(([^)]+)\)\s*$)");
            auto match = re.match(depStr);
            if (match.hasMatch()) {
                QString depName = match.captured(1);
                mod.dependencies.append(depName);
                mod.dependencySpecs[depName] = VersionSpec::fromString(match.captured(2).trimmed());
            } else {
                mod.dependencies.append(depStr);
            }
        }

        QJsonArray conflicts = manifest["conflicts"].toArray();
        for (const auto& c : conflicts) mod.conflicts.append(c.toString());
    }
}

void ModManagerModule::preloadModDetails() {
    if (m_detailsPreloaded) return;
    m_detailsPreloaded = true;

    for (int i = 0; i < m_mods.size(); ++i) {
        loadModDetailsAsync(i);
    }
}

void ModManagerModule::loadModState() {
    QString statePath = m_modDirectory + "/.state.json";
    QFile file(statePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;

    QJsonObject state = doc.object();
    QJsonArray mods = state["mods"].toArray();

    for (const auto& mVal : mods) {
        QJsonObject m = mVal.toObject();
        QString name = m["name"].toString();
        m_priorities[name] = m["priority"].toInt();

        // Restore enabled state for existing mods
        if (m.contains("enabled")) {
            auto it = std::find_if(m_mods.begin(), m_mods.end(),
                [&](const ModEntry& e) { return e.name == name; });
            if (it != m_mods.end()) {
                it->enabled = m["enabled"].toBool(true);
            }
        }
    }
}

void ModManagerModule::syncModsWithState() {
    for (auto& mod : m_mods) {
        QString disableSuffix = ".disabled";
        bool onDiskDisabled = mod.path.endsWith(disableSuffix);

        if (mod.enabled && onDiskDisabled) {
            // State says enabled but disk says disabled -> re-enable
            QString newPath = mod.path.chopped(disableSuffix.length());
            if (QFile::rename(mod.path, newPath)) {
                mod.path = newPath;
                m_installEngine->enableMod(mod.name);
            }
        } else if (!mod.enabled && !onDiskDisabled) {
            // State says disabled but disk says enabled -> disable
            QString newPath = mod.path + disableSuffix;
            if (QFile::rename(mod.path, newPath)) {
                mod.path = newPath;
                m_installEngine->disableMod(mod.name);
            }
        }
    }
    populateModList();
}

void ModManagerModule::scanACContentMods() {
    QString acRoot = EditorConfig::instance().simInstallPath();
    if (acRoot.isEmpty()) return;

    ModScanner scanner;
    QStringList acModPaths = {
        acRoot + "/content/cars",
        acRoot + "/content/tracks",
        acRoot + "/extension/config",
        acRoot + "/system/cfg"
    };

    for (const QString& modPath : acModPaths) {
        QDir dir(modPath);
        if (!dir.exists()) continue;

        QDirIterator it(modPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString subDir = it.next();
            if (QFileInfo(subDir).fileName().startsWith('.')) continue;

            QString category = scanner.detectModCategory(subDir);
            QString name = QFileInfo(subDir).fileName();

            bool alreadyTracked = std::any_of(m_mods.begin(), m_mods.end(),
                [&](const ModEntry& e) { return e.name == name; });
            if (alreadyTracked) continue;

            ModEntry mod;
            mod.name = name;
            mod.path = subDir;
            mod.category = category;
            mod.isBuiltIn = true;
            mod.enabled = true;

            QDirIterator sizeIt(subDir, QDir::Files, QDirIterator::Subdirectories);
            while (sizeIt.hasNext()) { sizeIt.next(); mod.sizeBytes += sizeIt.fileInfo().size(); }

            QString manifestPath = subDir + "/manifest.json";
            if (QFile::exists(manifestPath)) {
                QJsonObject manifest = scanner.readModManifest(manifestPath);
                mod.versionStr = manifest["version"].toString();
                mod.version = Version::fromString(mod.versionStr);
                mod.author = manifest["author"].toString();
                mod.description = manifest["description"].toString();
            }

            m_mods.append(mod);
        }
    }

    populateModList();
}

// ============================================================================
// ModManagerModule -- Load Order Management
// ============================================================================
// ModManagerModule — Load Order Management
// ============================================================================

void ModManagerModule::applyLoadOrder() {
    QStringList enabledMods;
    for (const auto& mod : m_mods) {
        if (mod.enabled) enabledMods.append(mod.name);
    }

    DependencyResolver::Resolution res = m_dependencyResolver->resolve(m_mods, enabledMods);
    if (res.satisfied && !res.resolvedOrder.isEmpty()) {
        QStringList loadOrder = res.resolvedOrder;

        // Update priorities based on load order
        for (int i = 0; i < loadOrder.size(); ++i) {
            m_priorities[loadOrder[i]] = i;
        }

        // Save to AC priority.ini
        writeACPriorityIni();

        saveModState();
        emit modsChanged();
    }
}

void ModManagerModule::recalculateLoadOrder() {
    applyLoadOrder();
}

QStringList ModManagerModule::getCurrentLoadOrder() const {
    QStringList enabledMods;
    for (const auto& mod : m_mods) {
        if (mod.enabled) enabledMods.append(mod.name);
    }

    DependencyResolver::Resolution res = m_dependencyResolver->resolve(m_mods, enabledMods);
    return res.resolvedOrder;
}

void ModManagerModule::saveLoadOrder() {
    QStringList loadOrder = getCurrentLoadOrder();
    QJsonObject state;
    QJsonArray orderArray;
    for (const QString& mod : loadOrder) {
        orderArray.append(mod);
    }
    state["loadOrder"] = orderArray;

    QString statePath = m_modDirectory + "/.loadorder.json";
    QFile file(statePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(state).toJson());
    }
}

void ModManagerModule::loadLoadOrder() {
    QString statePath = m_modDirectory + "/.loadorder.json";
    QFile file(statePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;

    QJsonObject state = doc.object();
    QJsonArray orderArray = state["loadOrder"].toArray();

    for (int i = 0; i < orderArray.size(); ++i) {
        m_priorities[orderArray[i].toString()] = i;
    }
}

// ============================================================================
// ModManagerModule — Dependency Management
// ============================================================================

void ModManagerModule::installModWithDependencies(const QString& zipPath) {
    QFileInfo fi(zipPath);
    if (!fi.exists()) return;

    QString modName = fi.completeBaseName();

    // First check if we can find dependencies in available sources
    QStringList missingDeps = m_dependencyResolver->findMissing(m_mods, {modName});
    if (!missingDeps.isEmpty()) {
        // Show dialog to install missing dependencies
        showDependencyDialog(modName);
        return;
    }

    m_installEngine->installMod(zipPath);
    refreshMods();
    applyLoadOrder();
}

QStringList ModManagerModule::resolveDependencies(const QStringList& targetMods) {
    return m_dependencyResolver->findMissing(m_mods, targetMods);
}

void ModManagerModule::showDependencyDialog(const QString& modName) {
    QStringList missingDeps = m_dependencyResolver->findMissing(m_mods, {modName});

    QString message = QString("Mod '%1' requires the following missing dependencies:\n\n%2\n\n")
        .arg(modName)
        .arg(missingDeps.join("\n"));

    message += "Would you like to search for these dependencies online?";

    QMessageBox::StandardButton reply = QMessageBox::question(nullptr, tr("Missing Dependencies"), message,
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // Could trigger a search in the Workshop module
        emit updatesAvailable(missingDeps.size());
    }
}

void ModManagerModule::showDependencyTree(const QString& modName) {
    DependencyTreeDialog dialog(m_mods, modName);
    dialog.exec();
}

void ModManagerModule::resolveVersionConflicts() {
    QStringList enabledMods;
    for (const auto& mod : m_mods) {
        if (mod.enabled) enabledMods.append(mod.name);
    }

    DependencyResolver::Resolution res = m_dependencyResolver->resolve(m_mods, enabledMods);
    QStringList vc = m_dependencyResolver->findVersionConflicts(m_mods, enabledMods);

    if (vc.isEmpty()) {
        QMessageBox::information(nullptr, tr("Version Conflicts"), tr("No version conflicts detected."));
        return;
    }

    QString msg = "Version conflicts detected:\n\n" + vc.join("\n") + "\n\n"
                  "Disable conflicting mods to resolve?";
    auto reply = QMessageBox::question(nullptr, tr("Version Conflicts"), msg,
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        // Disable mods with unsatisfied constraints
        for (auto it = res.depDetails.begin(); it != res.depDetails.end(); ++it) {
            for (const auto& rd : it.value()) {
                if (!rd.satisfied) {
                    disableMod(it.key());
                    break;
                }
            }
        }
    }
}

// ============================================================================
// ModManagerModule — Conflict Management
// ============================================================================

void ModManagerModule::checkForConflicts() {
    QStringList enabledMods;
    for (const auto& mod : m_mods) {
        if (mod.enabled) enabledMods.append(mod.name);
    }

    DependencyResolver::Resolution res = m_dependencyResolver->resolve(m_mods, enabledMods);
    if (!res.conflictingMods.isEmpty()) {
        m_conflictLabel->setText(tr("%1 conflict(s) detected").arg(res.conflictingMods.size()));
        m_conflictLabel->setVisible(true);
        m_conflictLabel->setToolTip(res.conflictingMods.join("\n"));
        if (!m_suppressConflictDialog) {
            m_suppressConflictDialog = true;
            showConflictDialog(res.conflictingMods);
            QTimer::singleShot(5000, this, [this]() { m_suppressConflictDialog = false; });
        }
    } else {
        m_conflictLabel->setVisible(false);
    }

    if (!res.circularDeps.isEmpty()) {
        QString msg = "Circular dependencies detected:\n" + res.circularDeps.join(", ");
        qWarning() << msg;
    }
}

void ModManagerModule::showConflictDialog(const QStringList& conflicts) {
    if (conflicts.isEmpty()) return;

    // Build conflict report from m_mods and conflicts list
    QString reportTitle = QObject::tr("Mod Conflict Report");
    QString reportText;

    // Group conflicts by type
    QStringList fileConflicts;
    QStringList dependencyConflicts;
    QStringList versionConflicts;

    for (const QString& conflict : conflicts) {
        // Analyze conflict type
        bool found = false;
        for (const auto& mod : m_mods) {
            if (mod.name == conflict) {
                if (!mod.dependencies.isEmpty()) {
                    dependencyConflicts << QObject::tr("%1 requires dependencies not satisfied").arg(conflict);
                    found = true;
                }
                if (!mod.version.isEmpty()) {
                    versionConflicts << QObject::tr("%1 v%2 may conflict with installed version").arg(conflict).arg(mod.version.toString());
                    found = true;
                }
                break;
            }
        }
        if (!found) {
            fileConflicts << QObject::tr("%1 has overlapping files").arg(conflict);
        }
    }

    // Build report text
    if (!fileConflicts.isEmpty()) {
        reportText += QObject::tr("File Conflicts:\n") + fileConflicts.join("\n") + "\n\n";
    }
    if (!dependencyConflicts.isEmpty()) {
        reportText += QObject::tr("Dependency Conflicts:\n") + dependencyConflicts.join("\n") + "\n\n";
    }
    if (!versionConflicts.isEmpty()) {
        reportText += QObject::tr("Version Conflicts:\n") + versionConflicts.join("\n") + "\n\n";
    }

    if (reportText.isEmpty()) {
        reportText = QObject::tr("No specific conflict details available for the %1 conflicting mods.").arg(conflicts.size());
    }

    // Show conflict dialog with resolution options
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(reportTitle);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText(QObject::tr("Found %1 mod conflict(s)").arg(conflicts.size()));
    msgBox.setInformativeText(reportText);

    QPushButton* disableAll = msgBox.addButton(QObject::tr("Disable All Conflicting"), QMessageBox::AcceptRole);
    QPushButton* keepFirst = msgBox.addButton(QObject::tr("Keep Enabled (First Loaded)"), QMessageBox::ActionRole);
    QPushButton* cancelBtn = msgBox.addButton(QMessageBox::Cancel);

    msgBox.exec();

    if (msgBox.clickedButton() == disableAll) {
        QStringList toDisable;
        bool firstFound = false;
        for (const auto& mod : m_mods) {
            if (conflicts.contains(mod.name)) {
                if (!firstFound) {
                    firstFound = true;
                    continue; // Keep first enabled
                }
                toDisable.append(mod.name);
            }
        }
        for (const QString& name : toDisable) {
            disableMod(name);
        }
        m_summaryLabel->setText(QObject::tr("Disabled %1 conflicting mods").arg(toDisable.size()));
        m_conflictLabel->setVisible(false);
        emit modsChanged();
    } else if (msgBox.clickedButton() == keepFirst) {
        // Keep all enabled but suppress warnings
        m_summaryLabel->setText(QObject::tr("Conflicts acknowledged - keeping all mods enabled"));
        m_conflictLabel->setVisible(false);
    }
    // Cancel: do nothing
}

bool ModManagerModule::hasConflicts(const QString& modName) const {
    QStringList enabledMods;
    for (const auto& mod : m_mods) {
        if (mod.enabled) enabledMods.append(mod.name);
    }

    DependencyResolver::Resolution res = m_dependencyResolver->resolve(m_mods, enabledMods);
    return res.conflictingMods.contains(modName);
}

// ============================================================================
// ModManagerModule — AC Integration
// ============================================================================

void ModManagerModule::syncWithACPriorityIni() {
    writeACPriorityIni();
}

void ModManagerModule::writeACPriorityIni() {
    QString acRoot = EditorConfig::instance().simContentCarsPath();
    if (acRoot.isEmpty()) return;

    QString priorityPath = acRoot + "/priority.ini";
    QFile file(priorityPath);

    QStringList loadOrder = getCurrentLoadOrder();

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "; AC Mod Load Priority\n";
        out << "; Generated by ksEditor ModManager\n";
        out << "; DO NOT EDIT MANUALLY - changes will be overwritten\n\n";

        for (int i = 0; i < loadOrder.size(); ++i) {
            out << QString("[%1]\n").arg(i);
            out << QString("mod=%1\n").arg(loadOrder[i]);
            out << "\n";
        }

        file.close();
    }
}

bool ModManagerModule::validateACPriorityIni() {
    QString acRoot = EditorConfig::instance().simContentCarsPath();
    if (acRoot.isEmpty()) return false;

    QString priorityPath = acRoot + "/priority.ini";
    if (!QFile::exists(priorityPath)) return false;

    QFile file(priorityPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Basic validation
    return content.contains("mod=");
}

// ============================================================================
// ModManagerModule — Performance
// ============================================================================

void ModManagerModule::enableLazyLoading(bool enabled) {
    m_detailsPreloaded = !enabled;
}

// ============================================================================
// ModManagerModule — Repository
// ============================================================================

void ModManagerModule::refreshRepository() {
    if (m_repository) {
        m_repository->refresh();
    }
}

void ModManagerModule::installModFromRepository(const QString& modName) {
    if (!m_repository) return;

    // Get latest version
    auto versions = m_repository->getModVersions(modName);
    if (versions.isEmpty()) {
        QMessageBox::warning(nullptr, tr("Not Found"),
            QString("Mod '%1' not found in repository").arg(modName));
        return;
    }

    // Find highest version
    ModRepository::ModInfo* best = nullptr;
    for (auto& v : versions) {
        if (!best || Version::fromString(v.version).compare(Version::fromString(best->version)) > 0) {
            best = &v;
        }
    }

    if (!best) return;

    // Check dependencies first
    QStringList missingDeps;
    for (auto it = best->dependencies.begin(); it != best->dependencies.end(); ++it) {
        const QString& depName = it.key();
        const VersionSpec& spec = it.value();

        // Check if already installed and satisfies
        bool satisfied = false;
        for (const auto& mod : m_mods) {
            if (mod.name == depName && mod.enabled) {
                if (spec.op == VersionSpec::Any || spec.matches(mod.version)) {
                    satisfied = true;
                    break;
                }
            }
        }

        if (!satisfied) {
            // Try to resolve from repository
            ModRepository::ModInfo* depInfo = m_repository->resolveDependency(depName, spec);
            if (!depInfo) {
                missingDeps.append(depName + " (" + VersionSpec::opToString(spec.op) + " " + spec.version.toString() + ")");
            }
        }
    }

    if (!missingDeps.isEmpty()) {
        QString msg = QString("Mod '%1' requires missing dependencies:\n\n%2\n\n")
            .arg(modName).arg(missingDeps.join("\n"));
        msg += "Install missing dependencies automatically?";

        auto reply = QMessageBox::question(nullptr, tr("Missing Dependencies"), msg,
                                           QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            // Recursively install dependencies first
            for (auto it = best->dependencies.begin(); it != best->dependencies.end(); ++it) {
                const QString& depName = it.key();
                const VersionSpec& spec = it.value();

                bool satisfied = false;
                for (const auto& mod : m_mods) {
                    if (mod.name == depName && mod.enabled) {
                        if (spec.op == VersionSpec::Any || spec.matches(mod.version)) {
                            satisfied = true;
                            break;
                        }
                    }
                }

                if (!satisfied) {
                    installModFromRepository(depName);
                }
            }
        } else {
            return;
        }
    }

    // Download and install the mod
    QString packagePath;
    if (m_repository->downloadMod(*best, packagePath)) {
        m_installEngine->installMod(packagePath);
        refreshMods();
        applyLoadOrder();
    } else {
        QMessageBox::warning(nullptr, tr("Install Failed"),
            QString("Failed to download mod '%1'").arg(modName));
    }
}

void ModManagerModule::installMissingDependencies(const QStringList& targetMods) {
    QStringList allMissing;

    for (const QString& target : targetMods) {
        QStringList missing = m_dependencyResolver->findMissing(m_mods, {target});
        for (const QString& dep : missing) {
            if (!allMissing.contains(dep)) {
                allMissing.append(dep);
            }
        }
    }

    if (allMissing.isEmpty()) {
        QMessageBox::information(nullptr, tr("Dependencies"), tr("All dependencies are satisfied."));
        return;
    }

    QString msg = "The following dependencies are missing:\n\n" + allMissing.join("\n") + "\n\n";
    msg += "Search and install from repository?";

    auto reply = QMessageBox::question(nullptr, tr("Missing Dependencies"), msg,
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        for (const QString& dep : allMissing) {
            installModFromRepository(dep);
        }
    }
}

// ============================================================================
// ModManagerModule — Conflict Detection
// ============================================================================

void ModManagerModule::scanForFileConflicts() {
    if (!m_conflictDetector) return;

    QString acRoot = EditorConfig::instance().simInstallPath();
    if (acRoot.isEmpty()) {
        QMessageBox::warning(nullptr, tr("Scan Failed"), tr("Assetto Corsa installation not found."));
        return;
    }

    m_summaryLabel->setText(tr("Scanning for file conflicts..."));
    QApplication::processEvents();

    ModConflictDetector::ConflictReport report = m_conflictDetector->detectConflicts(m_mods, acRoot);

    showConflictReport(report);

    m_summaryLabel->setText(tr("Conflict scan complete: %1 conflicts found (%2 critical)")
        .arg(report.conflicts.size()).arg(report.criticalConflicts.size()));
}

void ModManagerModule::showConflictReport(const ModConflictDetector::ConflictReport& report) {
    if (report.conflicts.isEmpty()) {
        QMessageBox::information(nullptr, tr("Conflict Scan"), tr("No file conflicts detected."));
        return;
    }

    QString msg = QString("Found %1 file conflict(s):\n\n").arg(report.conflicts.size());

    for (const auto& conflict : report.conflicts) {
        msg += QString("• %1\n  Mods: %2\n  Type: %3\n\n")
            .arg(conflict.filePath)
            .arg(conflict.modNames.join(", "))
            .arg(conflict.conflictType);
    }

    if (report.hasCritical) {
        msg += "\n⚠ CRITICAL CONFLICTS DETECTED:\n";
        for (const QString& critical : report.criticalConflicts) {
            msg += "  " + critical + "\n";
        }
        msg += "\nThese files cannot be safely merged and will cause issues in-game.";
    }

    QMessageBox::StandardButton reply = QMessageBox::question(nullptr, tr("File Conflicts"),
        msg + "\n\nOpen Mod Manager to resolve?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // Could open a detailed conflict resolution dialog here
        checkForConflicts(); // This will show the existing conflict dialog
    }
}

// ============================================================================
// ModManagerModule — Collection Management
// ============================================================================

void ModManagerModule::addModToCollection(const QString& collectionId, const QString& modName)
{
    if (m_collectionManager) {
        m_collectionManager->addModToCollection(collectionId, modName);
        m_collectionManager->saveCollections();
    }
}

void ModManagerModule::removeModFromCollection(const QString& collectionId, const QString& modName)
{
    if (m_collectionManager) {
        m_collectionManager->removeModFromCollection(collectionId, modName);
        m_collectionManager->saveCollections();
    }
}

void ModManagerModule::createCollection(const QString& name, const QString& description)
{
    if (m_collectionManager) {
        m_collectionManager->createCollection(name, description);
        m_collectionManager->saveCollections();
    }
}

void ModManagerModule::deleteCollection(const QString& id)
{
    if (m_collectionManager) {
        m_collectionManager->deleteCollection(id);
        m_collectionManager->saveCollections();
    }
}

QStringList ModManagerModule::modCollections(const QString& modName) const
{
    return m_collectionManager
        ? m_collectionManager->findCollectionsForMod(modName)
        : QStringList();
}

QStringList ModManagerModule::collectionMods(const QString& collectionId) const
{
    return m_collectionManager
        ? m_collectionManager->getModsForCollection(collectionId)
        : QStringList();
}

// ============================================================================
// ModManagerModule — Statistics
// ============================================================================

ModManagerModule::ModStats ModManagerModule::calculateStats() const
{
    ModStats stats;
    stats.totalMods = m_mods.size();

    for (const auto& mod : m_mods) {
        if (mod.enabled) stats.enabledMods++;
        else stats.disabledMods++;

        if (mod.isBuiltIn) stats.builtInMods++;
        if (mod.hasUpdate) stats.hasUpdates++;
        if (!mod.dependencies.isEmpty()) stats.withDependencies++;
        if (!mod.conflicts.isEmpty()) stats.withConflicts++;

        stats.totalSizeBytes += mod.sizeBytes;

        QString cat = mod.category.isEmpty() ? "other" : mod.category;
        stats.categoryCounts[cat]++;
        stats.categorySizes[cat] += mod.sizeBytes;
    }

    if (stats.totalMods > 0) {
        stats.avgModSizeMB = (stats.totalSizeBytes / (double)stats.totalMods) / (1024.0 * 1024.0);
    }

    return stats;
}

QVariantMap ModManagerModule::getStats() const
{
    ModStats s = calculateStats();
    QVariantMap m;
    m["totalMods"] = s.totalMods;
    m["enabledMods"] = s.enabledMods;
    m["disabledMods"] = s.disabledMods;
    m["builtInMods"] = s.builtInMods;
    m["hasUpdates"] = s.hasUpdates;
    m["withDependencies"] = s.withDependencies;
    m["withConflicts"] = s.withConflicts;
    m["integrityFailed"] = s.integrityFailed;
    m["totalSizeBytes"] = (qint64)s.totalSizeBytes;
    m["totalSizeMB"] = QString::number(s.totalSizeBytes / (1024.0 * 1024.0), 'f', 1);
    m["avgModSizeMB"] = QString::number(s.avgModSizeMB, 'f', 1);

    QVariantMap cats;
    for (auto it = s.categoryCounts.begin(); it != s.categoryCounts.end(); ++it) {
        QVariantMap cat;
        cat["count"] = it.value();
        cat["size"] = (qint64)s.categorySizes.value(it.key());
        cats[it.key()] = cat;
    }
    m["categories"] = cats;
    return m;
}

// ============================================================================
// ModManagerModule — Dependency Graph
// ============================================================================

void ModManagerModule::showDependencyGraph(const QString& rootMod)
{
    QString target = rootMod;
    if (target.isEmpty()) {
        auto* item = m_modList ? m_modList->currentItem() : nullptr;
        if (item) target = item->data(Qt::UserRole).toString();
    }
    if (target.isEmpty()) return;

    auto dialog = new QDialog(
        m_modList ? m_modList->window() : nullptr);
    dialog->setWindowTitle("Dependency Graph: " + target);
    dialog->setMinimumSize(700, 500);
    dialog->resize(800, 600);

    auto* layout = new QVBoxLayout(dialog);

    auto* header = new QLabel(QString("<b>Dependency Graph</b> — %1").arg(target));
    layout->addWidget(header);

    auto* scene = new QGraphicsScene(dialog);
    auto* view = new QGraphicsView(scene);
    view->setRenderHint(QPainter::Antialiasing);
    view->setDragMode(QGraphicsView::ScrollHandDrag);
    view->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    layout->addWidget(view, 1);

    auto* btnRow = new QHBoxLayout();
    auto* resetBtn = new QPushButton(tr("Reset View"));
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setStyleSheet("background: #4a6a8a; color: white;");
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    connect(resetBtn, &QPushButton::clicked, [view]() {
        view->fitInView(view->sceneRect(), Qt::KeepAspectRatio);
    });
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);

    // Build graph layout
    auto result = m_dependencyResolver->buildDependencyTree(m_mods, target);
    if (result.isEmpty()) {
        scene->addText("No dependencies found");
    } else {
        qreal x = 20, y = 20;
        qreal nodeW = 200, nodeH = 50;
        qreal hSpacing = 40, vSpacing = 30;

        QMap<QString, QGraphicsRectItem*> nodes;
        for (int i = 0; i < result.size(); ++i) {
            QString modText = result[i].first;
            bool isMissing = modText.contains("[MISSING]");

            auto* rect = scene->addRect(QRectF(x, y, nodeW, nodeH),
                isMissing ? QPen(QColor(200, 60, 60), 2) : QPen(QColor(100, 180, 100), 2),
                isMissing ? QBrush(QColor(60, 20, 20)) : QBrush(QColor(30, 50, 30)));
            auto* text = scene->addText(modText, QFont("Segoe UI", 9));
            text->setDefaultTextColor(isMissing ? QColor(255, 100, 100) : QColor(200, 255, 200));
            text->setPos(x + 4, y + nodeH / 2 - text->boundingRect().height() / 2);
            rect->setToolTip(modText);

            // Truncate long names
            if (text->boundingRect().width() > nodeW - 8) {
                QString shortText = modText.left(modText.length() * nodeW / text->boundingRect().width());
                text->setPlainText(shortText + "...");
            }

            // Draw dependency edges to children
            QStringList deps = result[i].second;
            qreal childX = x + nodeW + hSpacing;
            for (int j = 0; j < deps.size(); ++j) {
                QPointF parentRight(x + nodeW, y + nodeH / 2);
                QPointF childLeft(childX, y + j * (nodeH + vSpacing) + nodeH / 2);
                scene->addLine(QLineF(parentRight, childLeft), QPen(QColor(80, 120, 180), 1));
            }

            y += nodeH + vSpacing;
        }
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-20, -20, 20, 20));
    }

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

// ============================================================================
// ModManagerModule — Load Order Drag & Drop
// ============================================================================

void ModManagerModule::moveModInLoadOrder(const QString& modName, int newIndex)
{
    if (newIndex < 0 || newIndex >= m_mods.size() || modName.isEmpty()) return;

    int oldIndex = -1;
    for (int i = 0; i < m_mods.size(); ++i) {
        if (m_mods[i].name == modName) { oldIndex = i; break; }
    }
    if (oldIndex < 0 || oldIndex == newIndex) return;

    ModEntry mod = m_mods[oldIndex];
    m_mods.removeAt(oldIndex);
    m_mods.insert(newIndex, mod);

    // Recalculate priorities
    for (int i = 0; i < m_mods.size(); ++i) {
        m_priorities[m_mods[i].name] = i;
    }

    saveModState();
    emit modsChanged();
}

void ModManagerModule::setModListDragDropEnabled(bool enabled)
{
    m_dragDropEnabled = enabled;
    if (m_modList) {
        m_modList->setDragDropMode(enabled
            ? QAbstractItemView::InternalMove
            : QAbstractItemView::NoDragDrop);
        m_modList->setDefaultDropAction(enabled
            ? Qt::MoveAction
            : Qt::IgnoreAction);
    }
}

QJsonObject ModManagerModule::serializeProject() const
{
    QJsonObject data;
    data["modDirectory"] = m_modDirectory;
    QJsonObject prioritiesObj;
    for (auto it = m_priorities.constBegin(); it != m_priorities.constEnd(); ++it)
        prioritiesObj[it.key()] = it.value();
    data["priorities"] = prioritiesObj;
    QJsonArray modsArray;
    for (const auto& mod : m_mods) {
        QJsonObject modObj;
        modObj["name"] = mod.name;
        modObj["version"] = mod.version.toString();
        modObj["enabled"] = mod.enabled;
        modObj["path"] = mod.path;
        modsArray.append(modObj);
    }
    data["mods"] = modsArray;
    return data;
}

void ModManagerModule::deserializeProject(const QJsonObject& data)
{
    m_modDirectory = data["modDirectory"].toString();
    m_priorities.clear();
    QJsonObject prioritiesObj = data["priorities"].toObject();
    for (auto it = prioritiesObj.constBegin(); it != prioritiesObj.constEnd(); ++it)
        m_priorities[it.key()] = it.value().toInt();
}

} // namespace ks
