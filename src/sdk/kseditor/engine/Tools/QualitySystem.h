#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QVariantMap>
#include <QByteArray>
#include <QFile>
#include <QCryptographicHash>
#include <functional>

namespace ks {

// ─── Validation System ───────────────────────────────────────────────────────

enum class ValidationSeverity {
    Info,
    Warning,
    Error,
    Critical
};

enum class ValidationCategory {
    General,
    Geometry,
    Material,
    Texture,
    Animation,
    Physics,
    Audio,
    Export
};

class ValidationResult
{
public:
    ValidationResult() = default;

    ValidationResult(ValidationSeverity sev,
                     ValidationCategory cat,
                     const QString &code,
                     const QString &message,
                     const QString &objectId);

    bool isError() const;
    QJsonObject toJson() const;

    ValidationSeverity severity = ValidationSeverity::Info;
    ValidationCategory category = ValidationCategory::General;
    QString code;
    QString message;
    QString objectId;
};

class ValidationManager : public QObject
{
    Q_OBJECT

public:
    static ValidationManager *instance();

    explicit ValidationManager(QObject *parent = nullptr);
    ~ValidationManager();

    struct Rule {
        QString id;
        QString description;
        ValidationCategory category;
        bool enabled = true;
        std::function<QVector<ValidationResult>(const QVariantMap &)> fn;
    };

    void registerRule(const QString &id,
                      const QString &description,
                      ValidationCategory category,
                      std::function<QVector<ValidationResult>(const QVariantMap &)> fn);

    void enableRule(const QString &id, bool enabled);

    QVector<ValidationResult> validate(const QVariantMap &context,
                                       ValidationCategory category = ValidationCategory::General);

    QVector<ValidationResult> getLastResults() const;
    QVector<ValidationResult> getErrors() const;
    QVector<ValidationResult> getWarnings() const;

    bool hasErrors() const;

    void clearResults();
    void clearRules();

    void registerBuiltinRules();

signals:
    void validationComplete(const QVector<ValidationResult> &results);
    void resultsCleared();

private:
    QMap<QString, Rule> m_rules;
    QVector<ValidationResult> m_lastResults;
};

// ─── Integrity System ────────────────────────────────────────────────────────

enum class IntegrityStatus {
    Verified,
    Corrupted,
    Missing,
    Pirated,
    Signed
};

struct IntegrityResult {
    IntegrityStatus status;
    QString filePath;
    QString expectedHash;
    QString actualHash;
    QString message;
};

struct ModSignature {
    QString author;
    QString version;
    QString timestamp;
    QByteArray signature;
    QString publicKey;
};

class IntegrityManager : public QObject {
    Q_OBJECT

public:
    static IntegrityManager* instance();

    QString computeHash(const QString& filePath);
    bool verifyHash(const QString& filePath, const QString& expectedHash);

    bool signMod(const QString& modPath, const QString& privateKey);
    bool verifySignature(const QString& modPath, const QString& publicKey);

    QString generateLicenseKey(const QString& hardwareId);
    bool validateLicense(const QString& licenseKey, const QString& hardwareId);

    IntegrityResult checkFileIntegrity(const QString& filePath);
    QList<IntegrityResult> scanProject(const QString& projectPath);

    void setDRMEnabled(bool enabled) { m_drmEnabled = enabled; }
    bool isDRMEnabled() const { return m_drmEnabled; }

signals:
    void integrityCheckComplete(const QList<IntegrityResult>& results);
    void verificationFailed(const QString& message);
    void licenseValidated(bool success);
    void modSigned(const QString& modPath);
    void signatureVerified(const QString& modPath, bool valid);

private:
    IntegrityManager(QObject* parent = nullptr);
    static IntegrityManager* s_instance;

    bool m_drmEnabled = false;
    QMap<QString, QString> m_hashCache;

    QString computeSHA256(const QByteArray& data);
};

} // namespace ks
