#include "PublishValidatorBridge.h"
#include "tools/PythonBridge.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace ks {
namespace fileformat {

PublishValidatorBridge::PublishValidatorBridge(QObject* parent)
    : QObject(parent)
{
}

void PublishValidatorBridge::validate(const QString& modPath, const QString& contentType)
{
    if (m_running) {
        emit errorOccurred("Validation already in progress");
        return;
    }

    if (modPath.isEmpty()) {
        emit errorOccurred("No mod folder specified");
        return;
    }

    QDir modDir(modPath);
    if (!modDir.exists()) {
        emit errorOccurred("Mod folder does not exist: " + modPath);
        return;
    }

    m_lastModPath = modPath;
    setRunning(true);

    QString appDir = QCoreApplication::applicationDirPath();
    QString validatorDir = appDir + "/../py";
#ifdef _WIN32
    validatorDir = appDir + "\\..\\py";
#endif

    PythonBridge* bridge = PythonBridge::instance();
    if (!bridge || !bridge->isAvailable()) {
        setRunning(false);
        emit errorOccurred("Python is not available. Check your Python installation.");
        return;
    }

    QString script = QString(
        "import sys, json, os\n"
        "sys.path.insert(0, r'%1')\n"
        "os.chdir(r'%1')\n"
        "try:\n"
        "    from publish_validator import run_validation\n"
        "    report = run_validation(r'%2', '%3', None)\n"
        "    result = report.to_dict()\n"
        "    print(json.dumps(result, ensure_ascii=False))\n"
        "except Exception as e:\n"
        "    print(json.dumps({'error': str(e)}, ensure_ascii=False))\n"
    ).arg(validatorDir, modPath, contentType);

    QVariant evalResult = bridge->evaluate(script);
    QVariantMap resultMap = evalResult.toMap();

    if (!resultMap["error"].toString().isEmpty()) {
        setRunning(false);
        emit errorOccurred(resultMap["error"].toString());
        return;
    }

    QString result = resultMap["result"].toString();
    if (result.isEmpty()) {
        setRunning(false);
        emit errorOccurred("No response from Python. Check if the mod folder is valid.");
        return;
    }

    parseResult(result);
    setRunning(false);
    emit validationComplete(!m_hasBlocking, m_hasBlocking
        ? "Validation blocked: fix errors before publishing"
        : "Validation passed: mod is ready for publishing");
}

void PublishValidatorBridge::cancel()
{
    if (m_running) {
        setRunning(false);
        emit errorOccurred("Validation cancelled");
    }
}

void PublishValidatorBridge::setRunning(bool running)
{
    if (m_running != running) {
        m_running = running;
        emit runningChanged();
    }
}

void PublishValidatorBridge::parseResult(const QString& jsonStr)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.trimmed().toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred("Failed to parse Python output: " + parseError.errorString());
        return;
    }

    QJsonObject root = doc.object();

    if (root.contains("error")) {
        emit errorOccurred(root["error"].toString());
        return;
    }

    m_issues.clear();
    m_errorCount = 0;
    m_warningCount = 0;
    m_infoCount = 0;
    m_hasBlocking = root["blocking"].toBool(false);

    QJsonArray issuesArray = root["issues"].toArray();
    for (const QJsonValue& val : issuesArray) {
        QJsonObject issueObj = val.toObject();
        QVariantMap issue;
        issue["rule_id"] = issueObj["rule_id"].toString();
        issue["severity"] = issueObj["severity"].toString();
        issue["message"] = issueObj["message"].toString();
        issue["path"] = issueObj["path"].toString();
        issue["auto_fixable"] = issueObj["auto_fixable"].toBool(false);
        issue["fix_hint"] = issueObj["fix_hint"].toString();
        m_issues.append(issue);

        QString sev = issueObj["severity"].toString();
        if (sev == "error") m_errorCount++;
        else if (sev == "warning") m_warningCount++;
        else if (sev == "info") m_infoCount++;
    }

    emit issuesChanged();
}

} // namespace fileformat
} // namespace ks
