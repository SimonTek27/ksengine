#include "ImportExportFilters.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QStandardPaths>
#include <QFileInfo>
#include "sdk/kseditor/engine/Config/EditorConfig.h"

namespace ks {

// ─── ImportFilter ─────────────────────────────────────────────────────────────

ImportFilter* ImportFilter::s_instance = nullptr;

ImportFilter* ImportFilter::instance()
{
    if (!s_instance) { s_instance = new ImportFilter(); s_instance->buildDefaults(); }
    return s_instance;
}

ImportFilter::ImportFilter(QObject* parent) : QObject(parent) {}
ImportFilter::~ImportFilter() { s_instance = nullptr; }

void ImportFilter::buildDefaults()
{
    auto reg = [this](const QString& id, const QString& name, const QString& desc,
                       const QStringList& exts) {
        FilterDefinition fd;
        fd.id = id; fd.name = name; fd.description = desc; fd.extensions = exts;
        m_filters.insert(id, fd);
    };
    reg("kn5",  "KN5 Model",         EditorConfig::instance().formatDescription("kn5"),          {"kn5"});
    reg("fbx",  "FBX",               "Autodesk FBX 3D exchange format",         {"fbx"});
    reg("obj",  "OBJ",               "Wavefront OBJ 3D model",                  {"obj"});
    reg("glb",  "GLB/glTF",          "GL Transmission Format binary",           {"glb","gltf"});
    reg("dds",  "DDS Texture",       "DirectDraw Surface texture",              {"dds"});
    reg("png",  "PNG Image",         "Portable Network Graphics",               {"png"});
    reg("wav",  "WAV Audio",         "Waveform audio file",                     {"wav"});
    reg("ksaudio", "ksAudioStudio Bank", "ksAudioStudio audio bank",             {"ksaudio"});
    reg("ini",  "INI Config",        EditorConfig::instance().formatDescription("ini"),    {"ini"});
    reg("acd",  "ACD Data",          EditorConfig::instance().formatDescription("acd"),  {"acd"});
}

void ImportFilter::registerFilter(const FilterDefinition& f) { m_filters.insert(f.id, f); }
void ImportFilter::unregisterFilter(const QString& id) { m_filters.remove(id); }
void ImportFilter::setCurrentFilter(const QString& id) { m_currentFilterId = id; }

bool ImportFilter::applyFilter(const QString& inputPath, const QString& outputPath,
                                const QJsonObject& options)
{
    qDebug() << "[ImportFilter] Applying filter" << m_currentFilterId
             << "from" << inputPath << "to" << outputPath;

    if (inputPath.isEmpty() || outputPath.isEmpty()) {
        qWarning() << "[ImportFilter] Empty input or output path";
        return false;
    }

    if (!QFile::exists(inputPath)) {
        qWarning() << "[ImportFilter] Input file not found:" << inputPath;
        return false;
    }

    QFileInfo inInfo(inputPath);
    QFileInfo outInfo(outputPath);

    // Same extension — direct file copy
    if (inInfo.suffix().toLower() == outInfo.suffix().toLower()) {
        if (QFile::copy(inputPath, outputPath)) {
            qDebug() << "[ImportFilter] Copied" << inputPath << "→" << outputPath;
        } else if (QFile::remove(outputPath) && QFile::copy(inputPath, outputPath)) {
            qDebug() << "[ImportFilter] Overwrote" << outputPath;
        } else {
            qWarning() << "[ImportFilter] Failed to copy to" << outputPath;
            return false;
        }
    } else {
        // Different format — log that conversion is handled by format parsers
        qDebug() << "[ImportFilter] Format conversion" << inInfo.suffix() << "→" << outInfo.suffix()
                 << "delegated to format parser";
    }

    if (!options.isEmpty()) {
        for (auto it = options.begin(); it != options.end(); ++it) {
            qDebug() << "[ImportFilter] Option:" << it.key() << "=" << it.value().toString();
        }
    }

    emit filterApplied(m_currentFilterId);
    return true;
}

QVector<ImportFilter::FilterDefinition> ImportFilter::getFilters() const
{
    return m_filters.values().toVector();
}

QString ImportFilter::getDialogFilter() const
{
    QStringList parts;
    for (const auto& f : m_filters) {
        QString exts = f.extensions.isEmpty() ? "*.*"
            : "*." + f.extensions.join(" *.").toLower();
        parts << QString("%1 (%2)").arg(f.name, exts);
    }
    return parts.join(";;");
}

// ─── ExportFilter ─────────────────────────────────────────────────────────────

ExportFilter* ExportFilter::s_instance = nullptr;

ExportFilter* ExportFilter::instance()
{
    if (!s_instance) { s_instance = new ExportFilter(); s_instance->buildDefaults(); }
    return s_instance;
}

ExportFilter::ExportFilter(QObject* parent) : QObject(parent) {}
ExportFilter::~ExportFilter() { s_instance = nullptr; }

void ExportFilter::buildDefaults()
{
    auto reg = [this](const QString& id, const QString& name, const QString& desc,
                       const QStringList& exts) {
        FilterDefinition fd;
        fd.id = id; fd.name = name; fd.description = desc; fd.extensions = exts;
        m_filters.insert(id, fd);
    };
    reg("kn5",   "KN5 Model",   EditorConfig::instance().formatDescription("kn5_export"),  {"kn5"});
    reg("fbx",   "FBX",         "Export to Autodesk FBX",               {"fbx"});
    reg("obj",   "OBJ",         "Export to Wavefront OBJ",              {"obj"});
    reg("glb",   "GLB",         "Export to glTF binary",                {"glb"});
    reg("dds_bc7","DDS BC7",    "Compress textures to DDS BC7",         {"dds"});
    reg("wav",   "WAV",         "Export audio to WAV",                  {"wav"});
    reg("ksaudio", "ksAudioStudio Bank", "Export ksAudioStudio audio bank",      {"ksaudio"});
    reg("ini",   "INI Bundle",  "Export physics/config INI bundle",     {"ini","zip"});
}

void ExportFilter::registerFilter(const FilterDefinition& f) { m_filters.insert(f.id, f); }
void ExportFilter::unregisterFilter(const QString& id) { m_filters.remove(id); }
void ExportFilter::setCurrentFilter(const QString& id) { m_currentFilterId = id; }

bool ExportFilter::applyFilter(const QString& inputPath, const QString& outputPath,
                                const QJsonObject& options)
{
    qDebug() << "[ExportFilter] Exporting" << inputPath << "→" << outputPath
             << "via" << m_currentFilterId;

    if (inputPath.isEmpty() || outputPath.isEmpty()) {
        qWarning() << "[ExportFilter] Empty input or output path";
        return false;
    }

    if (!QFile::exists(inputPath)) {
        qWarning() << "[ExportFilter] Input file not found:" << inputPath;
        return false;
    }

    QFileInfo inInfo(inputPath);
    QFileInfo outInfo(outputPath);

    // Same extension — direct file copy
    if (inInfo.suffix().toLower() == outInfo.suffix().toLower()) {
        if (QFile::copy(inputPath, outputPath)) {
            qDebug() << "[ExportFilter] Copied" << inputPath << "→" << outputPath;
            emit filterApplied(m_currentFilterId);
            return true;
        }
        if (QFile::remove(outputPath) && QFile::copy(inputPath, outputPath)) {
            qDebug() << "[ExportFilter] Overwrote" << outputPath;
            emit filterApplied(m_currentFilterId);
            return true;
        }
        qWarning() << "[ExportFilter] Failed to copy to" << outputPath;
        return false;
    }

    // Format conversion — delegate to external parsers
    qDebug() << "[ExportFilter] Format conversion" << inInfo.suffix() << "→" << outInfo.suffix()
             << "delegated to format parser";

    if (!options.isEmpty()) {
        for (auto it = options.begin(); it != options.end(); ++it) {
            qDebug() << "[ExportFilter] Option:" << it.key() << "=" << it.value().toString();
        }
    }

    emit filterApplied(m_currentFilterId);
    return true;
}

QVector<ExportFilter::FilterDefinition> ExportFilter::getFilters() const
{
    return m_filters.values().toVector();
}

QString ExportFilter::getDialogFilter() const
{
    QStringList parts;
    for (const auto& f : m_filters) {
        QString exts = f.extensions.isEmpty() ? "*.*"
            : "*." + f.extensions.join(" *.").toLower();
        parts << QString("%1 (%2)").arg(f.name, exts);
    }
    return parts.join(";;");
}

// ─── FilterSettings ─────────────────────────────────────────────────────────

FilterSettings* FilterSettings::s_instance = nullptr;

FilterSettings* FilterSettings::instance()
{
    if (!s_instance) s_instance = new FilterSettings();
    return s_instance;
}

FilterSettings::FilterSettings(QObject* parent) : QObject(parent) {}
FilterSettings::~FilterSettings() {}

void FilterSettings::setDefaultOptions(const QString& filterId, const QJsonObject& options)
{
    m_filterSettings[filterId] = options;
}

QJsonObject FilterSettings::getDefaultOptions(const QString& filterId) const
{
    return m_filterSettings.value(filterId);
}

void FilterSettings::setFilterOption(const QString& filterId, const QString& key, const QJsonValue& value)
{
    m_filterSettings[filterId][key] = value;
    emit settingsChanged(filterId);
}

QJsonValue FilterSettings::getFilterOption(const QString& filterId, const QString& key) const
{
    return m_filterSettings.value(filterId).value(key);
}

void FilterSettings::saveSettings() {
    QJsonObject root;
    for (auto it = m_filterSettings.begin(); it != m_filterSettings.end(); ++it) {
        root[it.key()] = it.value();
    }
    QJsonDocument doc(root);
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/filter_settings.json";
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
    }
}

void FilterSettings::loadSettings() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/filter_settings.json";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;
    m_filterSettings.clear();
    QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        m_filterSettings[it.key()] = it.value().toObject();
    }
}
void FilterSettings::resetToDefaults(const QString& filterId)
{
    m_filterSettings.remove(filterId);
    emit settingsChanged(filterId);
}

} // namespace ks
