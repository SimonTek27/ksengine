#include "SetupComparisonTool.h"
#include "sdk/kseditor/engine/FileFormat/INIParser.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <algorithm>

// ============================================================================
// Comparison operations
// ============================================================================

SetupComparisonTool::ComparisonReport SetupComparisonTool::compareSetups(const SetupData& setupA, const SetupData& setupB) {
    ComparisonReport report;
    report.setupA = setupA;
    report.setupB = setupB;
    report.differences = findDifferences(setupA, setupB);
    report.totalDifferences = report.differences.size();

    for (const auto& diff : report.differences) {
        if (diff.impact == "high") report.highImpactCount++;
        else if (diff.impact == "medium") report.mediumImpactCount++;
        else report.lowImpactCount++;
    }

    report.summary = QString("%1 differences found (%2 high, %3 medium, %4 low impact)")
        .arg(report.totalDifferences)
        .arg(report.highImpactCount)
        .arg(report.mediumImpactCount)
        .arg(report.lowImpactCount);

    return report;
}

QVector<SetupComparisonTool::ComparisonDiff> SetupComparisonTool::findDifferences(const SetupData& setupA, const SetupData& setupB) {
    QVector<ComparisonDiff> diffs;

    QMap<QString, float> allKeys;
    for (auto it = setupA.values.begin(); it != setupA.values.end(); ++it)
        allKeys[it.key()] = it.value();
    for (auto it = setupB.values.begin(); it != setupB.values.end(); ++it)
        allKeys[it.key()] = it.value();

    for (auto it = allKeys.begin(); it != allKeys.end(); ++it) {
        const QString& param = it.key();
        float valA = setupA.values.value(param, 0.0f);
        float valB = setupB.values.value(param, 0.0f);

        if (std::abs(valA - valB) > 1e-6f) {
            ComparisonDiff diff;
            diff.parameter = param;
            diff.category = getCategoryForParameter(param);
            diff.valueA = valA;
            diff.valueB = valB;
            diff.difference = calculateDifference(valA, valB);
            diff.percentChange = calculatePercentChange(valA, valB);
            diff.impact = analyzeImpact(diff);
            diff.recommendation = generateRecommendation(diff);
            diffs.append(diff);
        }
    }

    return diffs;
}

// ============================================================================
// Data loading
// ============================================================================

SetupComparisonTool::SetupData SetupComparisonTool::loadSetup(const QString& setupPath) {
    return loadSetupFromIni(setupPath);
}

SetupComparisonTool::SetupData SetupComparisonTool::loadSetupFromIni(const QString& iniPath) {
    SetupData data;
    data.name = iniPath;

    INIParser ini;
    if (!ini.load(iniPath)) {
        LOG_WARNING("SetupComparisonTool", QString("Failed to load setup: %1").arg(iniPath));
        return data;
    }

    QStringList sections = ini.sections();
    for (const auto& section : sections) {
        QStringList keys = ini.keys(section);
        for (const auto& key : keys) {
            QString fullKey = section + "/" + key;
            QVariant val = ini.value(section, key);
            if (val.canConvert<double>()) {
                data.values[fullKey] = static_cast<float>(val.toDouble());
            } else {
                data.stringValues[fullKey] = val.toString();
            }
        }
    }

    return data;
}

bool SetupComparisonTool::saveSetup(const SetupData& setup, const QString& setupPath) {
    INIParser ini;

    for (auto it = setup.values.begin(); it != setup.values.end(); ++it) {
        QStringList parts = it.key().split('/');
        if (parts.size() == 2) {
            ini.setValue(parts[0], parts[1], static_cast<double>(it.value()));
        }
    }

    for (auto it = setup.stringValues.begin(); it != setup.stringValues.end(); ++it) {
        QStringList parts = it.key().split('/');
        if (parts.size() == 2) {
            ini.setValue(parts[0], parts[1], it.value());
        }
    }

    return ini.save(setupPath);
}

// ============================================================================
// Analysis
// ============================================================================

QString SetupComparisonTool::analyzeImpact(const ComparisonDiff& diff) {
    float absPercent = std::abs(diff.percentChange);

    if (absPercent > 30.0f) return "high";
    if (absPercent > 10.0f) return "medium";
    return "low";
}

QString SetupComparisonTool::generateRecommendation(const ComparisonDiff& diff) {
    QString category = diff.category;
    QString param = diff.parameter.split('/').last().toLower();

    if (diff.impact == "low") {
        return "Minor difference, no action needed.";
    }

    if (category == "tyres") {
        if (param.contains("pressure")) {
            if (diff.difference > 0) return "Higher tyre pressure may reduce grip in corners.";
            return "Lower tyre pressure may improve grip but increase wear.";
        }
        if (param.contains("camber")) {
            if (diff.difference > 0) return "More negative camber improves cornering grip.";
            return "Less negative camber improves straight-line braking.";
        }
    }

    if (category == "suspension") {
        if (param.contains("spring") || param.contains("rate")) {
            if (diff.difference > 0) return "Stiffer springs improve responsiveness but reduce compliance.";
            return "Softer springs improve compliance over bumps.";
        }
        if (param.contains("damper") || param.contains("bump") || param.contains("rebound")) {
            if (diff.difference > 0) return "Stiffer damping improves control at limit.";
            return "Softer damping improves ride quality.";
        }
    }

    if (category == "aero") {
        if (diff.difference > 0) return "More downforce improves cornering but increases drag.";
        return "Less downforce improves top speed but reduces cornering grip.";
    }

    if (category == "brakes") {
        if (param.contains("bias")) {
            if (diff.difference > 0) return "More front bias improves stability under braking.";
            return "More rear bias improves rotation under braking.";
        }
    }

    if (category == "differential") {
        if (diff.difference > 0) return "Stiffer differential improves traction on exit.";
        return "Looser differential improves turn-in response.";
    }

    return QString("Review the %1% change in %2.").arg(QString::number(diff.percentChange, 'f', 1), diff.parameter);
}

QVector<QString> SetupComparisonTool::getHighImpactParameters() {
    return {
        "TYRES/PRESSURE_LF", "TYRES/PRESSURE_RF", "TYRES/PRESSURE_LR", "TYRES/PRESSURE_RR",
        "TYRES/CAMBER_LF", "TYRES/CAMBER_RF", "TYRES/CAMBER_LR", "TYRES/CAMBER_RR",
        "SUSPENSION/SPRING_RATE_LF", "SUSPENSION/SPRING_RATE_RF",
        "SUSPENSION/SPRING_RATE_LR", "SUSPENSION/SPRING_RATE_RR",
        "AERO/FRONT_WING", "AERO/REAR_WING",
        "BRAKES/BIAS", "BRAKES/PRESSURE",
        "DIFFERENTIAL/POWER", "DIFFERENTIAL/COAST"
    };
}

// ============================================================================
// Export
// ============================================================================

bool SetupComparisonTool::exportComparison(const ComparisonReport& report, const QString& filePath) {
    if (filePath.endsWith(".html", Qt::CaseInsensitive)) {
        return exportToHtml(report, filePath);
    } else if (filePath.endsWith(".csv", Qt::CaseInsensitive)) {
        return exportToCsv(report, filePath);
    }
    return exportToHtml(report, filePath);
}

bool SetupComparisonTool::exportToHtml(const ComparisonReport& report, const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream stream(&file);
    stream << "<!DOCTYPE html><html><head><style>\n";
    stream << "body { font-family: Arial; margin: 20px; }\n";
    stream << "table { border-collapse: collapse; width: 100%; }\n";
    stream << "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n";
    stream << "th { background: #252525; color: white; }\n";
    stream << ".high { background: #ff4444; color: white; }\n";
    stream << ".medium { background: #ffaa00; }\n";
    stream << ".low { background: #44aa44; color: white; }\n";
    stream << "</style></head><body>\n";
    stream << "<h1>Setup Comparison</h1>\n";
    stream << "<p>" << report.summary << "</p>\n";
    stream << "<table><tr><th>Parameter</th><th>Category</th><th>Setup A</th><th>Setup B</th><th>Diff</th><th>% Change</th><th>Impact</th></tr>\n";

    for (const auto& diff : report.differences) {
        QString impactClass = diff.impact;
        stream << "<tr><td>" << diff.parameter << "</td><td>" << diff.category << "</td>";
        stream << "<td>" << QString::number(diff.valueA, 'f', 3) << "</td>";
        stream << "<td>" << QString::number(diff.valueB, 'f', 3) << "</td>";
        stream << "<td>" << QString::number(diff.difference, 'f', 3) << "</td>";
        stream << "<td>" << QString::number(diff.percentChange, 'f', 1) << "%</td>";
        stream << "<td class=\"" << impactClass << "\">" << diff.impact << "</td></tr>\n";
    }

    stream << "</table></body></html>\n";
    file.close();
    return true;
}

bool SetupComparisonTool::exportToCsv(const ComparisonReport& report, const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream stream(&file);
    stream << "Parameter,Category,Setup A,Setup B,Difference,Percent Change,Impact\n";

    for (const auto& diff : report.differences) {
        stream << diff.parameter << "," << diff.category << ","
               << QString::number(diff.valueA, 'f', 3) << ","
               << QString::number(diff.valueB, 'f', 3) << ","
               << QString::number(diff.difference, 'f', 3) << ","
               << QString::number(diff.percentChange, 'f', 1) << ","
               << diff.impact << "\n";
    }

    file.close();
    return true;
}

// ============================================================================
// Utility
// ============================================================================

QString SetupComparisonTool::formatDifference(float diff, const QString& unit) {
    QString sign = (diff > 0) ? "+" : "";
    return QString("%1%2 %3").arg(sign).arg(QString::number(diff, 'f', 3), unit);
}

QString SetupComparisonTool::formatPercentChange(float percent) {
    QString sign = (percent > 0) ? "+" : "";
    return QString("%1%2%").arg(sign).arg(QString::number(percent, 'f', 1));
}

QColor SetupComparisonTool::getImpactColor(const QString& impact) {
    if (impact == "high") return QColor(255, 68, 68);
    if (impact == "medium") return QColor(255, 170, 0);
    return QColor(68, 170, 68);
}

// ============================================================================
// Private helpers
// ============================================================================

float SetupComparisonTool::calculateDifference(float valueA, float valueB) {
    return valueB - valueA;
}

float SetupComparisonTool::calculatePercentChange(float valueA, float valueB) {
    if (std::abs(valueA) < 1e-6f) return (valueB != 0.0f) ? 100.0f : 0.0f;
    return ((valueB - valueA) / std::abs(valueA)) * 100.0f;
}

QString SetupComparisonTool::getCategoryForParameter(const QString& parameter) {
    QString upper = parameter.toUpper();
    if (upper.contains("TYRES") || upper.contains("TIRE")) return "tyres";
    if (upper.contains("SUSPENSION") || upper.contains("SPRING") || upper.contains("DAMPER")) return "suspension";
    if (upper.contains("AERO") || upper.contains("WING")) return "aero";
    if (upper.contains("BRAKE")) return "brakes";
    if (upper.contains("DIFF")) return "differential";
    if (upper.contains("ENGINE") || upper.contains("ECU")) return "engine";
    if (upper.contains("GEAR")) return "gearing";
    return "other";
}

// ============================================================================
// SetupComparisonManager
// ============================================================================

SetupComparisonManager::SetupComparisonManager() {}

bool SetupComparisonManager::loadSetupA(const QString& path) {
    m_setupA = SetupComparisonTool::loadSetup(path);
    return !m_setupA.values.isEmpty();
}

bool SetupComparisonManager::loadSetupB(const QString& path) {
    m_setupB = SetupComparisonTool::loadSetup(path);
    return !m_setupB.values.isEmpty();
}

SetupComparisonTool::ComparisonReport SetupComparisonManager::compare() {
    m_lastReport = SetupComparisonTool::compareSetups(m_setupA, m_setupB);
    return m_lastReport;
}

bool SetupComparisonManager::exportReport(const QString& filePath, const QString& format) {
    if (format.toLower() == "csv") {
        return SetupComparisonTool::exportToCsv(m_lastReport, filePath);
    }
    return SetupComparisonTool::exportToHtml(m_lastReport, filePath);
}
