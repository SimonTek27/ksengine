#include "QualitySystem.h"

#include <QDebug>
#include <QDateTime>
#include <QUuid>
#include <QDir>
#include <QDirIterator>
#include <algorithm>

namespace ks {

// ─── ValidationResult ────────────────────────────────────────────────────────

ValidationResult::ValidationResult(ValidationSeverity sev,
                                   ValidationCategory cat,
                                   const QString& code,
                                   const QString& message,
                                   const QString& objectId)
    : severity(sev), category(cat), code(code), message(message), objectId(objectId)
{}

bool ValidationResult::isError() const
{
    return severity == ValidationSeverity::Error ||
           severity == ValidationSeverity::Critical;
}

QJsonObject ValidationResult::toJson() const
{
    QJsonObject obj;
    obj["code"]     = code;
    obj["message"]  = message;
    obj["objectId"] = objectId;
    obj["severity"] = static_cast<int>(severity);
    obj["category"] = static_cast<int>(category);
    return obj;
}

// ─── ValidationManager ───────────────────────────────────────────────────────

static ValidationManager* s_validationInstance = nullptr;

ValidationManager* ValidationManager::instance()
{
    if (!s_validationInstance)
        s_validationInstance = new ValidationManager();
    return s_validationInstance;
}

ValidationManager::ValidationManager(QObject* parent)
    : QObject(parent)
{}

ValidationManager::~ValidationManager()
{
    s_validationInstance = nullptr;
}

void ValidationManager::registerRule(const QString& id,
                                      const QString& description,
                                      ValidationCategory category,
                                      std::function<QVector<ValidationResult>(const QVariantMap&)> fn)
{
    Rule rule;
    rule.id          = id;
    rule.description = description;
    rule.category    = category;
    rule.enabled     = true;
    rule.fn          = std::move(fn);
    m_rules.insert(id, rule);
}

void ValidationManager::enableRule(const QString& id, bool enabled)
{
    if (m_rules.contains(id))
        m_rules[id].enabled = enabled;
}

QVector<ValidationResult> ValidationManager::validate(const QVariantMap& context,
                                                       ValidationCategory category)
{
    QVector<ValidationResult> results;
    for (const auto& rule : m_rules) {
        if (!rule.enabled) continue;
        if (category != ValidationCategory::General && rule.category != category) continue;
        results << rule.fn(context);
    }

    m_lastResults = results;
    emit validationComplete(results);

    int errors = 0, warnings = 0;
    for (const auto& r : results) {
        if (r.isError())                                   ++errors;
        else if (r.severity == ValidationSeverity::Warning) ++warnings;
    }
    qDebug() << "[Validation]" << results.size() << "issues –"
             << errors << "errors," << warnings << "warnings";

    return results;
}

QVector<ValidationResult> ValidationManager::getLastResults() const
{
    return m_lastResults;
}

QVector<ValidationResult> ValidationManager::getErrors() const
{
    QVector<ValidationResult> out;
    for (const auto& r : m_lastResults)
        if (r.isError()) out << r;
    return out;
}

QVector<ValidationResult> ValidationManager::getWarnings() const
{
    QVector<ValidationResult> out;
    for (const auto& r : m_lastResults)
        if (r.severity == ValidationSeverity::Warning) out << r;
    return out;
}

bool ValidationManager::hasErrors() const
{
    for (const auto& r : m_lastResults)
        if (r.isError()) return true;
    return false;
}

void ValidationManager::clearResults()
{
    m_lastResults.clear();
    emit resultsCleared();
}

void ValidationManager::clearRules()
{
    m_rules.clear();
}

void ValidationManager::registerBuiltinRules()
{
    registerRule("GEO_001", "Mesh must have at least one vertex", ValidationCategory::Geometry,
        [](const QVariantMap& ctx) -> QVector<ValidationResult> {
            int verts = ctx.value("vertexCount", 0).toInt();
            if (verts == 0)
                return { ValidationResult(ValidationSeverity::Error,
                    ValidationCategory::Geometry, "GEO_001",
                    "Mesh has no vertices", ctx.value("objectId").toString()) };
            return {};
        });

    registerRule("GEO_002", "Mesh must have UV coordinates for export", ValidationCategory::Geometry,
        [](const QVariantMap& ctx) -> QVector<ValidationResult> {
            if (!ctx.value("hasUV", false).toBool())
                return { ValidationResult(ValidationSeverity::Warning,
                    ValidationCategory::Geometry, "GEO_002",
                    "Mesh has no UV coordinates – textures will not render correctly",
                    ctx.value("objectId").toString()) };
            return {};
        });

    registerRule("MAT_001", "Material must have a shader assigned", ValidationCategory::Material,
        [](const QVariantMap& ctx) -> QVector<ValidationResult> {
            if (ctx.value("shaderName").toString().isEmpty())
                return { ValidationResult(ValidationSeverity::Error,
                    ValidationCategory::Material, "MAT_001",
                    "Material has no shader assigned", ctx.value("objectId").toString()) };
            return {};
        });

    registerRule("PHY_001", "Car must have 4 wheel nodes", ValidationCategory::Physics,
        [](const QVariantMap& ctx) -> QVector<ValidationResult> {
            int wheels = ctx.value("wheelCount", 0).toInt();
            if (wheels != 4)
                return { ValidationResult(ValidationSeverity::Error,
                    ValidationCategory::Physics, "PHY_001",
                    QString("Expected 4 wheel nodes, found %1").arg(wheels),
                    ctx.value("objectId").toString()) };
            return {};
        });
}

// ─── IntegrityManager ────────────────────────────────────────────────────────

IntegrityManager* IntegrityManager::s_instance = nullptr;

IntegrityManager::IntegrityManager(QObject* parent)
    : QObject(parent)
{}

IntegrityManager* IntegrityManager::instance() {
    if (!s_instance) {
        s_instance = new IntegrityManager();
    }
    return s_instance;
}

QString IntegrityManager::computeSHA256(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

QString IntegrityManager::computeHash(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open file:" << filePath;
        return QString();
    }

    QByteArray data = file.readAll();
    file.close();

    return computeSHA256(data);
}

bool IntegrityManager::verifyHash(const QString& filePath, const QString& expectedHash) {
    QString actualHash = computeHash(filePath);
    return actualHash == expectedHash;
}

IntegrityResult IntegrityManager::checkFileIntegrity(const QString& filePath) {
    IntegrityResult result;
    result.filePath = filePath;

    QFile file(filePath);
    if (!file.exists()) {
        result.status = IntegrityStatus::Missing;
        result.message = "File does not exist";
        return result;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        result.status = IntegrityStatus::Corrupted;
        result.message = "Cannot read file";
        return result;
    }

    file.close();

    result.status = IntegrityStatus::Verified;
    result.message = "File verified";
    result.actualHash = computeHash(filePath);

    return result;
}

QList<IntegrityResult> IntegrityManager::scanProject(const QString& projectPath) {
    QList<IntegrityResult> results;

    QDir dir(projectPath);
    QStringList filters;
    filters << "*.kn5" << "*.fbx" << "*.obj" << "*.ini" << "*.json" << "*.acd";

    QFileInfoList files;
    QDirIterator it(projectPath, filters, QDir::Files);
    while (it.hasNext()) {
        it.next();
        files.append(it.fileInfo());
    }

    for (const QFileInfo& info : files) {
        IntegrityResult result = checkFileIntegrity(info.absoluteFilePath());
        results.append(result);
    }

    emit integrityCheckComplete(results);
    return results;
}

bool IntegrityManager::signMod(const QString& modPath, const QString& privateKey) {
    if (!QFile::exists(modPath)) return false;

    QFile file(modPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray data = file.readAll();
    file.close();

    QString hash = computeSHA256(data);
    QString signaturePath = modPath + ".sig";

    QFile sigFile(signaturePath);
    if (!sigFile.open(QIODevice::WriteOnly)) return false;

    QString signature = computeSHA256(hash.toUtf8() + privateKey.toUtf8());
    sigFile.write(signature.toUtf8());
    sigFile.close();

    qDebug() << "Signed mod:" << modPath;
    emit modSigned(modPath);
    return true;
}

bool IntegrityManager::verifySignature(const QString& modPath, const QString& publicKey) {
    if (!QFile::exists(modPath)) return false;

    QString signaturePath = modPath + ".sig";
    if (!QFile::exists(signaturePath)) return false;

    QFile file(modPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QFile sigFile(signaturePath);
    if (!sigFile.open(QIODevice::ReadOnly)) return false;
    QString storedSignature = QString::fromUtf8(sigFile.readAll());
    sigFile.close();

    QString hash = computeSHA256(data);
    QString expectedSignature = computeSHA256(hash.toUtf8() + publicKey.toUtf8());
    bool valid = (storedSignature == expectedSignature);
    emit signatureVerified(modPath, valid);
    return valid;
}

QString IntegrityManager::generateLicenseKey(const QString& hardwareId) {
    QString salt = "kseditor_license_salt";
    QString data = hardwareId + salt;
    return computeSHA256(data.toUtf8()).left(32).toUpper();
}

bool IntegrityManager::validateLicense(const QString& licenseKey, const QString& hardwareId) {
    QString expectedKey = generateLicenseKey(hardwareId);
    bool valid = (licenseKey == expectedKey);
    emit licenseValidated(valid);
    return valid;
}

} // namespace ks
