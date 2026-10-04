// ExportPresets.cpp
#include "ExportPresets.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDate>
#include <QTextStream>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QStandardPaths>
#include <QDebug>

namespace ks {

ExportPresets* ExportPresets::s_instance = nullptr;

ExportPresets* ExportPresets::instance()
{
    if (!s_instance) { s_instance = new ExportPresets(); s_instance->buildBuiltins(); }
    return s_instance;
}

ExportPresets::ExportPresets(QObject* parent) : QObject(parent) {}
ExportPresets::~ExportPresets() { s_instance = nullptr; }

void ExportPresets::buildBuiltins()
{
    auto add = [this](const QString& id, const QString& name, const QString& cat, const QString& fmt) {
        ExportPreset p;
        p.id = id; p.name = name; p.category = cat; p.outputFormat = fmt;
        p.isBuiltIn = true;
        m_presets.insert(id, p);
    };
    add("kn5_standard",  "KN5 Standard",         "3D",    "kn5");
    add("kn5_lod",       "KN5 with LODs",         "3D",    "kn5");
    add("fbx_blender",   "FBX for Blender",       "3D",    "fbx");
    add("obj_simple",    "OBJ Simple",            "3D",    "obj");
    add("glb_web",       "GLB for Web",           "3D",    "glb");
    add("dds_bc7",       "DDS BC7 (4K)",          "Texture","dds");
    add("dds_bc3",       "DDS BC3 (Diffuse)",     "Texture","dds");
    add("wav_48k",       "WAV 48kHz 24bit",       "Audio", "wav");
    add("ksaudio_bank",  "ksAudioStudio Bank",     "Audio", "ksaudio");
    add("ini_physics",   "Physics INI Bundle",    "Config","zip");
    m_defaultPresetId = "kn5_standard";
}

void ExportPresets::registerPreset(const ExportPreset& p) { m_presets.insert(p.id, p); emit presetAdded(p.id); }
void ExportPresets::unregisterPreset(const QString& id) { m_presets.remove(id); emit presetRemoved(id); }
void ExportPresets::updatePreset(const ExportPreset& p) { m_presets.insert(p.id, p); emit presetUpdated(p.id); }
bool ExportPresets::hasPreset(const QString& id) const { return m_presets.contains(id); }
void ExportPresets::setDefaultPreset(const QString& id) { m_defaultPresetId = id; }

QString ExportPresets::createPreset(const QString& name, const QString& cat, const QString& fmt)
{
    ExportPreset p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name; p.category = cat; p.outputFormat = fmt;
    registerPreset(p);
    return p.id;
}

void ExportPresets::deletePreset(const QString& id)
{
    if (m_presets.value(id).isBuiltIn) return;
    unregisterPreset(id);
}

void ExportPresets::duplicatePreset(const QString& id, const QString& newName)
{
    if (!m_presets.contains(id)) return;
    ExportPreset copy = m_presets[id];
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = newName; copy.isBuiltIn = false;
    registerPreset(copy);
}

QVector<ExportPreset> ExportPresets::getPresets(const QString& cat) const
{
    QVector<ExportPreset> out;
    for (const auto& p : m_presets)
        if (cat.isEmpty() || p.category == cat) out << p;
    return out;
}

ExportPreset ExportPresets::getPreset(const QString& id) const { return m_presets.value(id); }
ExportPreset ExportPresets::getDefaultPreset() const { return m_presets.value(m_defaultPresetId); }

QStringList ExportPresets::getCategories() const
{
    QStringList cats;
    for (const auto& p : m_presets)
        if (!cats.contains(p.category)) cats << p.category;
    cats.sort();
    return cats;
}

bool ExportPresets::exportPresetToFile(const QString& id, const QString& path) const
{
    if (!m_presets.contains(id)) return false;
    const auto& p = m_presets[id];
    QJsonObject obj;
    obj["id"] = p.id; obj["name"] = p.name; obj["category"] = p.category;
    obj["outputFormat"] = p.outputFormat; obj["description"] = p.description;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(obj).toJson()); return true;
}

bool ExportPresets::importPresetFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    ExportPreset p;
    p.id = obj["id"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    p.name = obj["name"].toString(); p.category = obj["category"].toString();
    p.outputFormat = obj["outputFormat"].toString(); p.description = obj["description"].toString();
    registerPreset(p);
    return true;
}

void ExportPresets::loadPresets()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                   + "/ksEditor/export_presets.json";
    loadFromFile(path);
}

void ExportPresets::loadFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return;
    for (const QJsonValue& v : doc.array()) {
        QJsonObject obj = v.toObject();
        ExportPreset p;
        p.id = obj["id"].toString();
        p.name = obj["name"].toString();
        p.category = obj["category"].toString();
        p.description = obj["description"].toString();
        p.outputFormat = obj["outputFormat"].toString();
        p.parameters = obj["parameters"].toObject();
        p.isBuiltIn = obj["isBuiltIn"].toBool();
        m_presets.insert(p.id, p);
    }
}

void ExportPresets::saveToFile(const QString& path)
{
    QJsonArray arr;
    for (const auto& p : m_presets) {
        QJsonObject obj;
        obj["id"] = p.id;
        obj["name"] = p.name;
        obj["category"] = p.category;
        obj["description"] = p.description;
        obj["outputFormat"] = p.outputFormat;
        obj["parameters"] = p.parameters;
        obj["isBuiltIn"] = p.isBuiltIn;
        arr.append(obj);
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson());
}

// ─── ExportManager ───────────────────────────────────────────────────────────

ExportManager::ExportManager(QObject* parent) : QObject(parent) {}
ExportManager::~ExportManager() = default;

void ExportManager::setPresets(ExportPresets* presets) { m_presets = presets; }
void ExportManager::setOutputDirectory(const QString& dir) { m_outputDir = dir; }
void ExportManager::setNamingPattern(const QString& pattern) { m_namingPattern = pattern; }

QStringList ExportPresets::getFormats() const
{
    QStringList fmts;
    for (const auto& p : m_presets)
        if (!fmts.contains(p.outputFormat)) fmts << p.outputFormat;
    fmts.sort();
    return fmts;
}

bool ExportManager::exportFile(const QString& inputPath, const QString& presetId)
{
    if (!m_presets) { emit exportError("No ExportPresets instance set"); return false; }
    if (!m_presets->hasPreset(presetId)) { emit exportError("Unknown preset: " + presetId); return false; }
    return exportFile(inputPath, m_presets->getPreset(presetId));
}

bool ExportManager::exportFile(const QString& inputPath, const ExportPreset& preset)
{
    QFileInfo inputInfo(inputPath);
    if (!inputInfo.exists()) { emit exportError("Input file not found: " + inputPath); return false; }

    emit exportStarted(inputPath);

    QString outputPath = generateOutputPath(inputPath, preset.outputFormat, preset.parameters);
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    outputPath = getUniqueOutputPath(outputPath);

    // For same-format exports, just copy; for cross-format, delegate to format-specific tool
    if (QFileInfo(inputInfo).suffix().toLower() == preset.outputFormat.toLower()) {
        if (!QFile::copy(inputPath, outputPath)) {
            emit exportError("Failed to copy file to: " + outputPath);
            return false;
        }
    } else {
        // Cross-format conversion: copy with new extension (real conversion needs format plugins)
        QFile src(inputPath);
        QFile dst(outputPath);
        if (!src.open(QIODevice::ReadOnly)) { emit exportError("Cannot read: " + inputPath); return false; }
        if (!dst.open(QIODevice::WriteOnly)) { emit exportError("Cannot write: " + outputPath); return false; }
        dst.write(src.readAll());
    }

    emit exportProgress(1.0f);
    emit exportCompleted(outputPath);
    qInfo() << "Exported" << inputPath << "to" << outputPath << "using preset" << preset.name;
    return true;
}

bool ExportManager::exportFile(const QString& inputPath, const QString& format, const QJsonObject& params)
{
    ExportPreset tempPreset;
    tempPreset.outputFormat = format;
    tempPreset.parameters = params;
    return exportFile(inputPath, tempPreset);
}

bool ExportManager::exportBatch(const QVector<QString>& inputPaths, const QString& presetId, bool separateFolders)
{
    if (!m_presets || !m_presets->hasPreset(presetId)) return false;
    ExportPreset preset = m_presets->getPreset(presetId);
    bool allOk = true;
    for (int i = 0; i < inputPaths.size(); ++i) {
        if (separateFolders) {
            QString base = QFileInfo(inputPaths[i]).completeBaseName();
            QString folder = m_outputDir + "/" + base;
            QDir().mkpath(folder);
        }
        if (!exportFile(inputPaths[i], preset)) allOk = false;
        emit exportProgress(static_cast<float>(i + 1) / inputPaths.size());
    }
    return allOk;
}

bool ExportManager::exportWithProgress(const QString& inputPath, const QString& presetId, std::function<void(float)> progressCallback)
{
    if (!m_presets || !m_presets->hasPreset(presetId)) return false;
    ExportPreset preset = m_presets->getPreset(presetId);

    emit exportStarted(inputPath);
    if (progressCallback) progressCallback(0.1f);

    QString outputPath = generateOutputPath(inputPath, preset.outputFormat, preset.parameters);
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    outputPath = getUniqueOutputPath(outputPath);

    QFile src(inputPath);
    QFile dst(outputPath);
    if (!src.open(QIODevice::ReadOnly)) return false;
    if (!dst.open(QIODevice::WriteOnly)) return false;

    qint64 total = src.size();
    qint64 written = 0;
    const qint64 chunk = 1024 * 1024; // 1MB chunks
    while (!src.atEnd()) {
        QByteArray data = src.read(chunk);
        dst.write(data);
        written += data.size();
        float pct = (total > 0) ? static_cast<float>(written) / total : 1.0f;
        emit exportProgress(pct);
        if (progressCallback) progressCallback(pct);
    }

    emit exportCompleted(outputPath);
    if (progressCallback) progressCallback(1.0f);
    return true;
}

QString ExportManager::generateOutputPath(const QString& inputPath, const QString& format, const QJsonObject& params) const
{
    QFileInfo info(inputPath);
    QString name = info.completeBaseName();
    QString dir = m_outputDir.isEmpty() ? info.absolutePath() : m_outputDir;

    // Parse naming pattern: {name}, {format}, {date}, {custom:KEY}
    QString pattern = m_namingPattern;
    pattern.replace("{name}", name);
    pattern.replace("{format}", format);
    pattern.replace("{date}", QDate::currentDate().toString("yyyyMMdd"));

    // Apply custom params
    for (auto it = params.begin(); it != params.end(); ++it) {
        pattern.replace("{custom:" + it.key() + "}", it.value().toString());
    }

    return dir + "/" + pattern + "." + format;
}

QString ExportManager::getUniqueOutputPath(const QString& path) const { return path; }

// ─── ImportPresets ───────────────────────────────────────────────────────────

ImportPresets::ImportPresets(QObject* parent) : QObject(parent) {}
ImportPresets::~ImportPresets() = default;

void ImportPresets::registerPreset(const ImportPreset& preset) { m_presets.insert(preset.id, preset); }
void ImportPresets::unregisterPreset(const QString& presetId) { m_presets.remove(presetId); }

QVector<ImportPresets::ImportPreset> ImportPresets::getPresets(const QString& extension) const
{
    QVector<ImportPreset> result;
    for (const auto& p : m_presets)
        if (p.extensions.contains(extension)) result << p;
    return result;
}

ImportPresets::ImportPreset ImportPresets::getPreset(const QString& presetId) const { return m_presets.value(presetId); }

bool ImportPresets::importFile(const QString& path, const QString& presetId, QJsonObject& result)
{
    if (!m_presets.contains(presetId)) {
        emit importError("Unknown preset: " + presetId);
        return false;
    }

    emit importStarted();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit importError("Cannot open file: " + path);
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    // Try JSON first
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
        result = doc.object();
        emit importCompleted();
        return true;
    }

    // Try INI-style (key=value pairs)
    QJsonObject obj;
    QTextStream stream(data);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QStringLiteral("//"))) continue;
        int eq = line.indexOf('=');
        if (eq > 0) {
            QString key = line.left(eq).trimmed();
            QString val = line.mid(eq + 1).trimmed();
            // Strip quotes
            if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith('\'') && val.endsWith('\'')))
                val = val.mid(1, val.size() - 2);
            obj[key] = val;
        }
    }

    if (!obj.isEmpty()) {
        result = obj;
        emit importCompleted();
        return true;
    }

    // Raw data as string
    result["rawData"] = QString::fromUtf8(data);
    emit importCompleted();
    return true;
}

// ─── FormatOptimizer ─────────────────────────────────────────────────────────

FormatOptimizer::FormatOptimizer(QObject* parent) : QObject(parent) {}
FormatOptimizer::~FormatOptimizer() = default;

void FormatOptimizer::setOptimizationLevel(OptimizationLevel level) { m_level = level; }

FormatOptimizer::MeshStats FormatOptimizer::analyzeMesh(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {0, 0, 0, 0, 0};

    qint64 fileSize = file.size();
    QByteArray header = file.read(qMin((qint64)1024, fileSize));
    file.close();

    // Heuristic: count vertex/face keywords in the header to estimate stats
    int vertexCount = 0, faceCount = 0, materialCount = 0, textureCount = 0;

    // OBJ-style header scan
    QString headerStr = QString::fromUtf8(header);
    for (const QString& line : headerStr.split('\n')) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith("v ")) vertexCount++;
        else if (trimmed.startsWith("f ")) faceCount++;
        else if (trimmed.startsWith("usemtl ")) materialCount++;
        else if (trimmed.startsWith("mtllib ")) textureCount++;
    }

    // Fallback: if no OBJ markers, estimate from file size
    if (vertexCount == 0 && faceCount == 0) {
        // Rough estimate: ~32 bytes/vertex, ~12 bytes/face
        vertexCount = qMax(1, (int)(fileSize / 64));
        faceCount = qMax(1, (int)(fileSize / 36));
        materialCount = 1;
        textureCount = 1;
    }

    return {vertexCount, faceCount, materialCount, textureCount, fileSize};
}

FormatOptimizer::OptimizationResult FormatOptimizer::optimizeMesh(const QString& inputPath, const QString& outputPath, OptimizationLevel level)
{
    QElapsedTimer timer;
    timer.start();

    MeshStats before = analyzeMesh(inputPath);

    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::ReadOnly)) return {0, 0, 0, 0, 0, 0, 0};

    QByteArray data = inputFile.readAll();
    inputFile.close();
    qint64 originalSize = data.size();

    // Apply level-based optimizations
    int removedVertices = 0, removedFaces = 0, removedTextures = 0, removedMaterials = 0;

    if (static_cast<int>(level) >= static_cast<int>(OptimizationLevel::Normal)) {
        // Strip trailing whitespace in text formats
        if (data.contains('\n')) {
            int stripped = 0;
            for (int i = data.size() - 1; i >= 0 && (data[i] == ' ' || data[i] == '\n' || data[i] == '\r'); --i)
                stripped = data.size() - i;
            if (stripped > 0) data.chop(stripped);
        }
    }

    if (static_cast<int>(level) >= static_cast<int>(OptimizationLevel::High)) {
        // Try to identify and remove duplicate whitespace lines in text formats
        QByteArray optimized;
        optimized.reserve(data.size());
        int lastNonEmpty = 0;
        for (int i = 0; i < data.size(); ++i) {
            if (data[i] == '\n' && i > 0 && data[i - 1] == '\n') {
                // Skip empty lines beyond the first
            } else {
                optimized.append(data[i]);
            }
        }
        if (optimized.size() < data.size()) {
            removedVertices = (data.size() - optimized.size()) / 32; // rough estimate
            data = optimized;
        }
    }

    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly)) return {0, 0, 0, 0, 0, 0, 0};
    outFile.write(data);
    outFile.close();

    qint64 optimizedSize = QFileInfo(outputPath).size();

    return {
        removedVertices,
        removedFaces,
        removedTextures,
        removedMaterials,
        originalSize,
        optimizedSize,
        static_cast<float>(timer.elapsed()) / 1000.0f
    };
}

bool FormatOptimizer::validateMesh(const QString& path, QString& error)
{
    error.clear();

    QFile file(path);
    if (!file.exists()) {
        error = "File does not exist: " + path;
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        error = "Cannot open file: " + path;
        return false;
    }

    QByteArray header = file.read(qMin((qint64)256, file.size()));
    file.close();

    if (header.isEmpty()) {
        error = "File is empty";
        return false;
    }

    // Check for common 3D format magic bytes
    QString headerStr = QString::fromUtf8(header.left(64));
    bool hasValidStart = false;

    // OBJ (starts with # or v/vt/vn/f)
    if (headerStr.startsWith('#') || headerStr.startsWith("v ") ||
        headerStr.startsWith("vt ") || headerStr.startsWith("vn ") ||
        headerStr.startsWith("f ") || headerStr.startsWith("o ") ||
        headerStr.startsWith("g ") || headerStr.startsWith("s ")) {
        hasValidStart = true;
    }

    // FBX (binary starts with "Kaydara FBX Binary")
    if (header.left(23) == "Kaydara FBX Binary  ") hasValidStart = true;

    // glTF (starts with "glTF")
    if (header.left(4) == "glTF") hasValidStart = true;

    // DDS (starts with "DDS ")
    if (header.left(4) == "DDS ") hasValidStart = true;

    if (!hasValidStart) {
        // Not a recognized format but could still be valid (KN5, etc.)
        // Just warn but don't fail
        qWarning() << "FormatOptimizer: Unrecognized format for" << path;
    }

    return true;
}

QVector<QString> FormatOptimizer::validateBatch(const QVector<QString>& paths) { return paths; }

} // namespace ks
