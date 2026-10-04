#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>

namespace ks { namespace fileformat {

// ============================================================================
// KSAudioValidator — validate .ksaudio project files
//
// Checks:
//   - JSON structure and schema version
//   - Required fields (name, guid, format)
//   - GUID format validity
//   - Event/audio file references and existence
//   - Bank → event linkage consistency
//   - Bus hierarchy validity
//   - Duplicate name detection
//   - Sound metadata consistency
// ============================================================================

class KSAudioValidator : public QObject {
    Q_OBJECT
public:
    explicit KSAudioValidator(QObject* parent = nullptr);

    enum Severity {
        Info,
        Warning,
        Error
    };

    struct Issue {
        Severity severity;
        QString category;
        QString message;
        QString path;       // JSON path where issue was found
        QString suggestion; // Fix suggestion
    };

    struct ValidationResult {
        bool valid = true;
        int errorCount = 0;
        int warningCount = 0;
        int infoCount = 0;
        QVector<Issue> issues;

        bool hasErrors() const { return errorCount > 0; }
        bool hasWarnings() const { return warningCount > 0; }
    };

    // Validate a .ksaudio file (full validation)
    ValidationResult validate(const QString& projectPath);

    // Validate from JSON data
    ValidationResult validateJson(const QJsonObject& root,
                                  const QString& projectDir = QString());

    // Quick structural check (no file system access)
    ValidationResult validateStructure(const QJsonObject& root);

    // Validate GUID format
    static bool isValidGuid(const QString& guid);

    // Get last validation result
    ValidationResult lastResult() const { return m_lastResult; }

signals:
    void validationStarted(const QString& path);
    void issueFound(const Issue& issue);
    void validationCompleted(const ValidationResult& result);

private:
    ValidationResult m_lastResult;

    void addIssue(ValidationResult& result, Severity severity,
                  const QString& category, const QString& message,
                  const QString& path = QString(),
                  const QString& suggestion = QString());

    void validateSchema(ValidationResult& result, const QJsonObject& root);
    void validateMetadata(ValidationResult& result, const QJsonObject& root);
    void validateEvents(ValidationResult& result, const QJsonObject& root,
                        const QString& projectDir);
    void validateBanks(ValidationResult& result, const QJsonObject& root);
    void validateBuses(ValidationResult& result, const QJsonObject& root);
    void validateSounds(ValidationResult& result, const QJsonObject& root,
                        const QString& projectDir);
    void validateCrossReferences(ValidationResult& result, const QJsonObject& root);

    QJsonObject loadJson(const QString& path);
};

}} // namespace ks::fileformat
