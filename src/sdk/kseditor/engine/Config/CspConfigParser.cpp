#include "CspConfigParser.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QFileInfo>

// ============================================================================
// Main parsing operations
// ============================================================================

bool CspConfigParser::parseConfigFile(const QString& filePath, CspExtension& extension) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    // Parse YAML-like format
    QRegularExpression kvRegex("^(\\w[\\w\\.]*):\\s*(.+)$");
    QStringList lines = content.split('\n');

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith('#') || trimmed.isEmpty()) continue;

        QRegularExpressionMatch match = kvRegex.match(trimmed);
        if (match.hasMatch()) {
            QString key = match.captured(1).trimmed();
            QString value = match.captured(2).trimmed();

            // Determine type based on value
            if (value == "true" || value == "1") {
                extension.boolSettings[key] = true;
            } else if (value == "false" || value == "0") {
                extension.boolSettings[key] = false;
            } else if (value.contains('.')) {
                bool ok;
                float fVal = value.toFloat(&ok);
                if (ok) {
                    extension.floatSettings[key] = fVal;
                } else {
                    extension.stringSettings[key] = value;
                }
            } else {
                bool ok;
                int iVal = value.toInt(&ok);
                if (ok) {
                    extension.intSettings[key] = iVal;
                } else {
                    extension.stringSettings[key] = value;
                }
            }
        }
    }

    // Extract metadata from file name or content
    QFileInfo fileInfo(filePath);
    extension.name = fileInfo.baseName();

    return true;
}

bool CspConfigParser::saveConfigFile(const CspExtension& extension, const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# CSP Extension Configuration\n";
    stream << "# " << extension.description << "\n\n";

    // Write string settings
    for (auto it = extension.stringSettings.begin(); it != extension.stringSettings.end(); ++it) {
        stream << it.key() << ": " << it.value() << "\n";
    }

    // Write float settings
    for (auto it = extension.floatSettings.begin(); it != extension.floatSettings.end(); ++it) {
        stream << it.key() << ": " << QString::number(it.value(), 'f', 4) << "\n";
    }

    // Write bool settings
    for (auto it = extension.boolSettings.begin(); it != extension.boolSettings.end(); ++it) {
        stream << it.key() << ": " << (it.value() ? "true" : "false") << "\n";
    }

    // Write int settings
    for (auto it = extension.intSettings.begin(); it != extension.intSettings.end(); ++it) {
        stream << it.key() << ": " << it.value() << "\n";
    }

    file.close();
    return true;
}

QVector<CspConfigParser::CspExtension> CspConfigParser::parseDirectory(const QString& dirPath) {
    QVector<CspExtension> extensions;

    QDir dir(dirPath);
    if (!dir.exists()) return extensions;

    // Look for .ini, .cfg, and .yml files
    QStringList filters;
    filters << "*.ini" << "*.cfg" << "*.yml" << "*.yaml";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    for (const QFileInfo& fileInfo : files) {
        CspExtension extension;
        if (parseConfigFile(fileInfo.absoluteFilePath(), extension)) {
            extensions.append(extension);
        }
    }

    return extensions;
}

// ============================================================================
// CSP config sections
// ============================================================================

CspConfigParser::CspWeatherFx CspConfigParser::parseWeatherFx(const QString& configPath) {
    CspWeatherFx config;

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    // Parse WeatherFX specific settings
    QRegularExpression kvRegex("^(\\w[\\w\\s]*):\\s*(.+)$");
    QStringList lines = content.split('\n');

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith('#') || trimmed.isEmpty()) continue;

        QRegularExpressionMatch match = kvRegex.match(trimmed);
        if (match.hasMatch()) {
            QString key = match.captured(1).trimmed();
            QString value = match.captured(2).trimmed();

            if (key == "ENABLED") config.enabled = (value == "true" || value == "1");
            else if (key == "WEATHER_SCRIPT") config.scriptName = value;
            else if (key == "SCRIPT_PATH") config.scriptPath = value;
            else if (key == "TIME_MULTIPLIER") config.timeMultiplier = value.toFloat();
            else if (key == "USE_REAL_WEATHER") config.useRealWeather = (value == "true" || value == "1");
            else config.parameters[key] = value.toFloat();
        }
    }

    return config;
}

CspConfigParser::CspLightingFx CspConfigParser::parseLightingFx(const QString& configPath) {
    CspLightingFx config;

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    QRegularExpression kvRegex("^(\\w[\\w\\s]*):\\s*(.+)$");
    QStringList lines = content.split('\n');

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith('#') || trimmed.isEmpty()) continue;

        QRegularExpressionMatch match = kvRegex.match(trimmed);
        if (match.hasMatch()) {
            QString key = match.captured(1).trimmed();
            QString value = match.captured(2).trimmed();

            if (key == "ENABLED") config.enabled = (value == "true" || value == "1");
            else if (key == "DYNAMIC_LIGHTS") config.dynamicLights = (value == "true" || value == "1");
            else if (key == "ENABLE_OCCLUSION") config.enableOcclusion = (value == "true" || value == "1");
            else if (key == "AMBIENT_MULTIPLIER") config.ambientMultiplier = value.toFloat();
            else if (key == "SUN_MULTIPLIER") config.sunMultiplier = value.toFloat();
        }
    }

    return config;
}

CspConfigParser::CspParticlesFx CspConfigParser::parseParticlesFx(const QString& configPath) {
    CspParticlesFx config;

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    QRegularExpression kvRegex("^(\\w[\\w\\s]*):\\s*(.+)$");
    QStringList lines = content.split('\n');

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith('#') || trimmed.isEmpty()) continue;

        QRegularExpressionMatch match = kvRegex.match(trimmed);
        if (match.hasMatch()) {
            QString key = match.captured(1).trimmed();
            QString value = match.captured(2).trimmed();

            if (key == "ENABLED") config.enabled = (value == "true" || value == "1");
            else if (key == "ENABLE_SMOKE") config.enableSmoke = (value == "true" || value == "1");
            else if (key == "ENABLE_SPARKS") config.enableSparks = (value == "true" || value == "1");
            else if (key == "ENABLE_GRASS") config.enableGrass = (value == "true" || value == "1");
            else if (key == "SMOKE_INTENSITY") config.smokeIntensity = value.toFloat();
            else if (key == "SPARK_INTENSITY") config.sparkIntensity = value.toFloat();
        }
    }

    return config;
}

CspConfigParser::CspPhysicsExtensions CspConfigParser::parsePhysicsExtensions(const QString& configPath) {
    CspPhysicsExtensions config;

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    QRegularExpression kvRegex("^(\\w[\\w\\s]*):\\s*(.+)$");
    QStringList lines = content.split('\n');

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith('#') || trimmed.isEmpty()) continue;

        QRegularExpressionMatch match = kvRegex.match(trimmed);
        if (match.hasMatch()) {
            QString key = match.captured(1).trimmed();
            QString value = match.captured(2).trimmed();

            if (key == "ENABLED") config.enabled = (value == "true" || value == "1");
            else if (key == "ENABLE_AERO") config.enableAero = (value == "true" || value == "1");
            else if (key == "ENABLE_SUSPENSION") config.enableSuspension = (value == "true" || value == "1");
            else if (key == "ENABLE_TIRES") config.enableTires = (value == "true" || value == "1");
            else if (key == "AERO_MULTIPLIER") config.aeroMultiplier = value.toFloat();
        }
    }

    return config;
}

CspConfigParser::CspCarExtensions CspConfigParser::parseCarExtensions(const QString& configPath) {
    CspCarExtensions config;

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    QRegularExpression kvRegex("^(\\w[\\w\\s]*):\\s*(.+)$");
    QStringList lines = content.split('\n');

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith('#') || trimmed.isEmpty()) continue;

        QRegularExpressionMatch match = kvRegex.match(trimmed);
        if (match.hasMatch()) {
            QString key = match.captured(1).trimmed();
            QString value = match.captured(2).trimmed();

            if (key == "ENABLED") config.enabled = (value == "true" || value == "1");
            else if (key == "ENABLE_REVERSE_LIGHTS") config.enableReverseLights = (value == "true" || value == "1");
            else if (key == "ENABLE_TURN_SIGNALS") config.enableTurnSignals = (value == "true" || value == "1");
            else if (key == "ENABLE_ODOMETER") config.enableOdometer = (value == "true" || value == "1");
            else if (key == "ENABLE_WORKING_WIPERS") config.enableWorkingWipers = (value == "true" || value == "1");
        }
    }

    return config;
}

CspConfigParser::CspTrackExtensions CspConfigParser::parseTrackExtensions(const QString& configPath) {
    CspTrackExtensions config;

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    QRegularExpression kvRegex("^(\\w[\\w\\s]*):\\s*(.+)$");
    QStringList lines = content.split('\n');

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith('#') || trimmed.isEmpty()) continue;

        QRegularExpressionMatch match = kvRegex.match(trimmed);
        if (match.hasMatch()) {
            QString key = match.captured(1).trimmed();
            QString value = match.captured(2).trimmed();

            if (key == "ENABLED") config.enabled = (value == "true" || value == "1");
            else if (key == "ENABLE_GRASS_FX") config.enableGrassFx = (value == "true" || value == "1");
            else if (key == "ENABLE_PARTICLES") config.enableParticles = (value == "true" || value == "1");
            else if (key == "GRASS_DISTANCE") config.grassDistance = value.toFloat();
        }
    }

    return config;
}

// ============================================================================
// Save sections
// ============================================================================

bool CspConfigParser::saveWeatherFx(const CspWeatherFx& config, const QString& configPath) {
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# WeatherFX Configuration\n\n";
    stream << "ENABLED: " << (config.enabled ? "true" : "false") << "\n";
    stream << "WEATHER_SCRIPT: " << config.scriptName << "\n";
    stream << "TIME_MULTIPLIER: " << QString::number(config.timeMultiplier, 'f', 2) << "\n";
    stream << "USE_REAL_WEATHER: " << (config.useRealWeather ? "true" : "false") << "\n";

    for (auto it = config.parameters.begin(); it != config.parameters.end(); ++it) {
        stream << it.key() << ": " << QString::number(it.value(), 'f', 4) << "\n";
    }

    file.close();
    return true;
}

bool CspConfigParser::saveLightingFx(const CspLightingFx& config, const QString& configPath) {
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# LightingFX Configuration\n\n";
    stream << "ENABLED: " << (config.enabled ? "true" : "false") << "\n";
    stream << "DYNAMIC_LIGHTS: " << (config.dynamicLights ? "true" : "false") << "\n";
    stream << "ENABLE_OCCLUSION: " << (config.enableOcclusion ? "true" : "false") << "\n";
    stream << "AMBIENT_MULTIPLIER: " << QString::number(config.ambientMultiplier, 'f', 2) << "\n";
    stream << "SUN_MULTIPLIER: " << QString::number(config.sunMultiplier, 'f', 2) << "\n";

    file.close();
    return true;
}

bool CspConfigParser::saveParticlesFx(const CspParticlesFx& config, const QString& configPath) {
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# ParticlesFX Configuration\n\n";
    stream << "ENABLED: " << (config.enabled ? "true" : "false") << "\n";
    stream << "ENABLE_SMOKE: " << (config.enableSmoke ? "true" : "false") << "\n";
    stream << "ENABLE_SPARKS: " << (config.enableSparks ? "true" : "false") << "\n";
    stream << "ENABLE_GRASS: " << (config.enableGrass ? "true" : "false") << "\n";
    stream << "SMOKE_INTENSITY: " << QString::number(config.smokeIntensity, 'f', 2) << "\n";
    stream << "SPARK_INTENSITY: " << QString::number(config.sparkIntensity, 'f', 2) << "\n";

    file.close();
    return true;
}

bool CspConfigParser::savePhysicsExtensions(const CspPhysicsExtensions& config, const QString& configPath) {
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# Physics Extensions Configuration\n\n";
    stream << "ENABLED: " << (config.enabled ? "true" : "false") << "\n";
    stream << "ENABLE_AERO: " << (config.enableAero ? "true" : "false") << "\n";
    stream << "ENABLE_SUSPENSION: " << (config.enableSuspension ? "true" : "false") << "\n";
    stream << "ENABLE_TIRES: " << (config.enableTires ? "true" : "false") << "\n";
    stream << "AERO_MULTIPLIER: " << QString::number(config.aeroMultiplier, 'f', 2) << "\n";

    file.close();
    return true;
}

bool CspConfigParser::saveCarExtensions(const CspCarExtensions& config, const QString& configPath) {
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# Car Extensions Configuration\n\n";
    stream << "ENABLED: " << (config.enabled ? "true" : "false") << "\n";
    stream << "ENABLE_REVERSE_LIGHTS: " << (config.enableReverseLights ? "true" : "false") << "\n";
    stream << "ENABLE_TURN_SIGNALS: " << (config.enableTurnSignals ? "true" : "false") << "\n";
    stream << "ENABLE_ODOMETER: " << (config.enableOdometer ? "true" : "false") << "\n";
    stream << "ENABLE_WORKING_WIPERS: " << (config.enableWorkingWipers ? "true" : "false") << "\n";

    file.close();
    return true;
}

bool CspConfigParser::saveTrackExtensions(const CspTrackExtensions& config, const QString& configPath) {
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# Track Extensions Configuration\n\n";
    stream << "ENABLED: " << (config.enabled ? "true" : "false") << "\n";
    stream << "ENABLE_GRASS_FX: " << (config.enableGrassFx ? "true" : "false") << "\n";
    stream << "ENABLE_PARTICLES: " << (config.enableParticles ? "true" : "false") << "\n";
    stream << "GRASS_DISTANCE: " << QString::number(config.grassDistance, 'f', 2) << "\n";

    file.close();
    return true;
}

// ============================================================================
// Car/Track-specific CSP config
// ============================================================================

bool CspConfigParser::parseCarCspConfig(const QString& carPath, CspExtension& extension) {
    QString extDir = carPath + "/extension";
    if (!QDir(extDir).exists()) {
        return false;
    }

    // Look for ext_config.ini
    QString configPath = extDir + "/ext_config.ini";
    if (QFile::exists(configPath)) {
        return parseConfigFile(configPath, extension);
    }

    return false;
}

bool CspConfigParser::saveCarCspConfig(const CspExtension& extension, const QString& carPath) {
    QString extDir = carPath + "/extension";
    QDir().mkpath(extDir);

    QString configPath = extDir + "/ext_config.ini";
    return saveConfigFile(extension, configPath);
}

bool CspConfigParser::parseTrackCspConfig(const QString& trackPath, CspExtension& extension) {
    QString extDir = trackPath + "/extension";
    if (!QDir(extDir).exists()) {
        return false;
    }

    QString configPath = extDir + "/ext_config.ini";
    if (QFile::exists(configPath)) {
        return parseConfigFile(configPath, extension);
    }

    return false;
}

bool CspConfigParser::saveTrackCspConfig(const CspExtension& extension, const QString& trackPath) {
    QString extDir = trackPath + "/extension";
    QDir().mkpath(extDir);

    QString configPath = extDir + "/ext_config.ini";
    return saveConfigFile(extension, configPath);
}

// ============================================================================
// Validation
// ============================================================================

bool CspConfigParser::validateExtension(const CspExtension& extension, QString* error) {
    if (extension.name.isEmpty()) {
        if (error) *error = "Extension name is empty";
        return false;
    }

    return true;
}

bool CspConfigParser::validateWeatherFx(const CspWeatherFx& config, QString* error) {
    if (config.enabled && config.scriptName.isEmpty()) {
        if (error) *error = "WeatherFX enabled but no script specified";
        return false;
    }

    if (config.timeMultiplier < 0.0f || config.timeMultiplier > 100.0f) {
        if (error) *error = "Time multiplier out of range (0-100)";
        return false;
    }

    return true;
}

// ============================================================================
// Utility
// ============================================================================

QStringList CspConfigParser::getAvailableExtensions(const QString& cspPath) {
    QStringList extensions;

    QDir extDir(cspPath + "/extensions");
    if (extDir.exists()) {
        QFileInfoList dirs = extDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& dirInfo : dirs) {
            extensions.append(dirInfo.fileName());
        }
    }

    return extensions;
}

QString CspConfigParser::getCspVersion(const QString& cspPath) {
    QString versionFile = cspPath + "/version.txt";
    QFile file(versionFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString version = file.readLine().trimmed();
        file.close();
        return version;
    }

    return "Unknown";
}

bool CspConfigParser::isCspInstalled(const QString& acPath) {
    QDir cspDir(acPath + "/extension");
    return cspDir.exists();
}

// ============================================================================
// Private helpers
// ============================================================================

bool CspConfigParser::parseYamlSection(const QString& content, const QString& section, QMap<QString, QString>& settings) {
    QRegularExpression sectionRegex(section + "\\s*:\\s*\\{([\\s\\S]*?)\\}");
    QRegularExpressionMatch match = sectionRegex.match(content);

    if (!match.hasMatch()) return false;

    QString sectionContent = match.captured(1);
    QRegularExpression kvRegex("(\\w+)\\s*:\\s*([^,\\n]+)");
    QRegularExpressionMatchIterator it = kvRegex.globalMatch(sectionContent);

    while (it.hasNext()) {
        QRegularExpressionMatch kvMatch = it.next();
        settings[kvMatch.captured(1)] = kvMatch.captured(2).trimmed();
    }

    return true;
}

bool CspConfigParser::writeYamlSection(const QString& section, const QMap<QString, QString>& settings, QString& content) {
    QString sectionStr = section + ": {\n";
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        sectionStr += "  " + it.key() + ": " + it.value() + ",\n";
    }
    sectionStr += "}\n";

    content += sectionStr;
    return true;
}

// ============================================================================
// CspConfigManager implementation
// ============================================================================

CspConfigManager::CspConfigManager(const QString& acPath)
    : m_acPath(acPath) {
    m_cspPath = acPath + "/extension";
}

bool CspConfigManager::isCspInstalled() const {
    return CspConfigParser::isCspInstalled(m_acPath);
}

QString CspConfigManager::getCspVersion() const {
    return CspConfigParser::getCspVersion(m_cspPath);
}

QString CspConfigManager::getCspPath() const {
    return m_cspPath;
}

bool CspConfigManager::loadGlobalConfig() {
    if (!isCspInstalled()) return false;

    QString configDir = m_cspPath + "/common";
    if (!QDir(configDir).exists()) return true; // No config is OK

    // Load weather fx
    QString weatherPath = configDir + "/weatherfx.ini";
    if (QFile::exists(weatherPath)) {
        m_weatherFx = CspConfigParser::parseWeatherFx(weatherPath);
    }

    // Load lighting fx
    QString lightingPath = configDir + "/lightingfx.ini";
    if (QFile::exists(lightingPath)) {
        m_lightingFx = CspConfigParser::parseLightingFx(lightingPath);
    }

    // Load particles fx
    QString particlesPath = configDir + "/particlesfx.ini";
    if (QFile::exists(particlesPath)) {
        m_particlesFx = CspConfigParser::parseParticlesFx(particlesPath);
    }

    // Load physics extensions
    QString physicsPath = configDir + "/physics.ini";
    if (QFile::exists(physicsPath)) {
        m_physicsExtensions = CspConfigParser::parsePhysicsExtensions(physicsPath);
    }

    return true;
}

bool CspConfigManager::saveGlobalConfig() {
    if (!isCspInstalled()) return false;

    QString configDir = m_cspPath + "/common";
    QDir().mkpath(configDir);

    // Save weather fx
    QString weatherPath = configDir + "/weatherfx.ini";
    CspConfigParser::saveWeatherFx(m_weatherFx, weatherPath);

    // Save lighting fx
    QString lightingPath = configDir + "/lightingfx.ini";
    CspConfigParser::saveLightingFx(m_lightingFx, lightingPath);

    // Save particles fx
    QString particlesPath = configDir + "/particlesfx.ini";
    CspConfigParser::saveParticlesFx(m_particlesFx, particlesPath);

    // Save physics extensions
    QString physicsPath = configDir + "/physics.ini";
    CspConfigParser::savePhysicsExtensions(m_physicsExtensions, physicsPath);

    return true;
}

bool CspConfigManager::loadCarConfig(const QString& carName) {
    QString carPath = m_acPath + "/content/cars/" + carName;
    CspConfigParser::CspExtension extension;

    if (CspConfigParser::parseCarCspConfig(carPath, extension)) {
        // Map extension settings to car extensions
        m_carExtensions.enabled = extension.boolSettings.value("ENABLED", false);
        m_carExtensions.enableReverseLights = extension.boolSettings.value("ENABLE_REVERSE_LIGHTS", true);
        m_carExtensions.enableTurnSignals = extension.boolSettings.value("ENABLE_TURN_SIGNALS", true);
        m_carExtensions.enableOdometer = extension.boolSettings.value("ENABLE_ODOMETER", true);
        m_carExtensions.enableWorkingWipers = extension.boolSettings.value("ENABLE_WORKING_WIPERS", true);
        return true;
    }

    return false;
}

bool CspConfigManager::saveCarConfig(const QString& carName) {
    QString carPath = m_acPath + "/content/cars/" + carName;
    CspConfigParser::CspExtension extension;

    extension.name = carName;
    extension.boolSettings["ENABLED"] = m_carExtensions.enabled;
    extension.boolSettings["ENABLE_REVERSE_LIGHTS"] = m_carExtensions.enableReverseLights;
    extension.boolSettings["ENABLE_TURN_SIGNALS"] = m_carExtensions.enableTurnSignals;
    extension.boolSettings["ENABLE_ODOMETER"] = m_carExtensions.enableOdometer;
    extension.boolSettings["ENABLE_WORKING_WIPERS"] = m_carExtensions.enableWorkingWipers;

    return CspConfigParser::saveCarCspConfig(extension, carPath);
}

bool CspConfigManager::loadTrackConfig(const QString& trackName) {
    QString trackPath = m_acPath + "/content/tracks/" + trackName;
    CspConfigParser::CspExtension extension;

    if (CspConfigParser::parseTrackCspConfig(trackPath, extension)) {
        m_trackExtensions.enabled = extension.boolSettings.value("ENABLED", false);
        m_trackExtensions.enableGrassFx = extension.boolSettings.value("ENABLE_GRASS_FX", true);
        m_trackExtensions.enableParticles = extension.boolSettings.value("ENABLE_PARTICLES", true);
        m_trackExtensions.grassDistance = extension.floatSettings.value("GRASS_DISTANCE", 100.0f);
        return true;
    }

    return false;
}

bool CspConfigManager::saveTrackConfig(const QString& trackName) {
    QString trackPath = m_acPath + "/content/tracks/" + trackName;
    CspConfigParser::CspExtension extension;

    extension.name = trackName;
    extension.boolSettings["ENABLED"] = m_trackExtensions.enabled;
    extension.boolSettings["ENABLE_GRASS_FX"] = m_trackExtensions.enableGrassFx;
    extension.boolSettings["ENABLE_PARTICLES"] = m_trackExtensions.enableParticles;
    extension.floatSettings["GRASS_DISTANCE"] = m_trackExtensions.grassDistance;

    return CspConfigParser::saveTrackCspConfig(extension, trackPath);
}

// ============================================================================
// Extension management
// ============================================================================

QStringList CspConfigManager::getAvailableExtensions() const {
    QStringList extensions;
    QDir extDir(m_cspPath + "/extensions");
    if (extDir.exists()) {
        QFileInfoList dirs = extDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& dirInfo : dirs) {
            extensions.append(dirInfo.fileName());
        }
    }
    return extensions;
}

bool CspConfigManager::isExtensionEnabled(const QString& extensionName) const {
    QString configPath = m_cspPath + "/extensions/" + extensionName + "/extension.ini";
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    QRegularExpression regex("^ENABLED\\s*=\\s*(\\S+)", QRegularExpression::MultilineOption);
    QRegularExpressionMatch match = regex.match(content);
    if (match.hasMatch()) {
        QString value = match.captured(1).trimmed();
        return (value == "1" || value.toLower() == "true");
    }

    return false;
}

bool CspConfigManager::setExtensionEnabled(const QString& extensionName, bool enabled) {
    QString configPath = m_cspPath + "/extensions/" + extensionName + "/extension.ini";
    QFile file(configPath);

    QString content;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        content = stream.readAll();
        file.close();
    }

    QRegularExpression regex("^ENABLED\\s*=\\s*\\S+", QRegularExpression::MultilineOption);
    QString newValue = enabled ? "ENABLED=1" : "ENABLED=0";

    if (regex.match(content).hasMatch()) {
        content.replace(regex, newValue);
    } else {
        if (!content.isEmpty() && !content.endsWith('\n')) {
            content += "\n";
        }
        content += newValue + "\n";
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << content;
    file.close();
    return true;
}

QJsonObject CspConfigManager::getExtensionConfig(const QString& extensionName) const {
    QJsonObject config;
    QString configPath = m_cspPath + "/extensions/" + extensionName + "/extension.ini";
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    QRegularExpression regex("^(\\w[\\w\\.]*)\\s*=\\s*(.+)$", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = regex.globalMatch(content);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString key = match.captured(1).trimmed();
        QString value = match.captured(2).trimmed();

        if (value == "true" || value == "1") {
            config[key] = true;
        } else if (value == "false" || value == "0") {
            config[key] = false;
        } else {
            bool ok;
            int iVal = value.toInt(&ok);
            if (ok) {
                config[key] = iVal;
            } else {
                double dVal = value.toDouble(&ok);
                if (ok) {
                    config[key] = dVal;
                } else {
                    config[key] = value;
                }
            }
        }
    }

    return config;
}

bool CspConfigManager::setExtensionConfig(const QString& extensionName, const QJsonObject& config) {
    QString configPath = m_cspPath + "/extensions/" + extensionName + "/extension.ini";
    QDir().mkpath(QFileInfo(configPath).absolutePath());

    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# Extension configuration\n";

    QJsonObject::const_iterator it;
    for (it = config.constBegin(); it != config.constEnd(); ++it) {
        QString value;
        if (it.value().isBool()) {
            value = it.value().toBool() ? "1" : "0";
        } else if (it.value().isDouble()) {
            double d = it.value().toDouble();
            if (d == static_cast<int>(d)) {
                value = QString::number(static_cast<int>(d));
            } else {
                value = QString::number(d, 'f', 6);
            }
        } else {
            value = it.value().toString();
        }
        stream << it.key() << "=" << value << "\n";
    }

    file.close();
    return true;
}

QStringList CspConfigManager::getEnabledExtensions() const {
    QStringList allExtensions = getAvailableExtensions();
    QStringList enabledExtensions;

    for (const QString& ext : allExtensions) {
        if (isExtensionEnabled(ext)) {
            enabledExtensions.append(ext);
        }
    }

    return enabledExtensions;
}
