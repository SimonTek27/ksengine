#pragma once

#include <QString>
#include <QMap>
#include <QVector>
#include <QJsonObject>

/**
 * @brief CSP Extension Configuration Parser
 *
 * Parses and manages Custom Shaders Patch (CSP) extension configurations.
 * Based on:
 * - acc-extension-config (github.com/ac-custom-shaders-patch/acc-extension-config)
 * - acc-lua-sdk (github.com/ac-custom-shaders-patch/acc-lua-sdk)
 *
 * CSP extensions include:
 * - WeatherFX settings
 * - Lighting FX
 * - Particles FX
 * - Physics extensions
 * - Car extensions (reverse lights, signals, etc.)
 * - Track extensions
 */
class CspConfigParser {
public:
    struct CspExtension {
        QString name;
        QString version;
        QString author;
        QString description;
        bool enabled = true;

        // Extension-specific settings
        QMap<QString, QString> stringSettings;
        QMap<QString, float> floatSettings;
        QMap<QString, bool> boolSettings;
        QMap<QString, int> intSettings;
        QMap<QString, QVector<float>> vectorSettings;
    };

    struct CspWeatherFx {
        bool enabled = false;
        QString scriptName;
        QString scriptPath;
        float timeMultiplier = 1.0f;
        bool useRealWeather = false;
        QMap<QString, float> parameters;
    };

    struct CspLightingFx {
        bool enabled = false;
        bool dynamicLights = true;
        bool enableOcclusion = true;
        float ambientMultiplier = 1.0f;
        float sunMultiplier = 1.0f;
    };

    struct CspParticlesFx {
        bool enabled = false;
        bool enableSmoke = true;
        bool enableSparks = true;
        bool enableGrass = true;
        float smokeIntensity = 1.0f;
        float sparkIntensity = 1.0f;
    };

    struct CspPhysicsExtensions {
        bool enabled = false;
        bool enableAero = true;
        bool enableSuspension = true;
        bool enableTires = true;
        float aeroMultiplier = 1.0f;
    };

    struct CspCarExtensions {
        bool enabled = false;
        bool enableReverseLights = true;
        bool enableTurnSignals = true;
        bool enableOdometer = true;
        bool enableWorkingWipers = true;
    };

    struct CspTrackExtensions {
        bool enabled = false;
        bool enableGrassFx = true;
        bool enableParticles = true;
        float grassDistance = 100.0f;
    };

    // Main parsing operations
    static bool parseConfigFile(const QString& filePath, CspExtension& extension);
    static bool saveConfigFile(const CspExtension& extension, const QString& filePath);
    static QVector<CspExtension> parseDirectory(const QString& dirPath);

    // CSP config sections
    static CspWeatherFx parseWeatherFx(const QString& configPath);
    static CspLightingFx parseLightingFx(const QString& configPath);
    static CspParticlesFx parseParticlesFx(const QString& configPath);
    static CspPhysicsExtensions parsePhysicsExtensions(const QString& configPath);
    static CspCarExtensions parseCarExtensions(const QString& configPath);
    static CspTrackExtensions parseTrackExtensions(const QString& configPath);

    // Save sections
    static bool saveWeatherFx(const CspWeatherFx& config, const QString& configPath);
    static bool saveLightingFx(const CspLightingFx& config, const QString& configPath);
    static bool saveParticlesFx(const CspParticlesFx& config, const QString& configPath);
    static bool savePhysicsExtensions(const CspPhysicsExtensions& config, const QString& configPath);
    static bool saveCarExtensions(const CspCarExtensions& config, const QString& configPath);
    static bool saveTrackExtensions(const CspTrackExtensions& config, const QString& configPath);

    // Car-specific CSP config
    static bool parseCarCspConfig(const QString& carPath, CspExtension& extension);
    static bool saveCarCspConfig(const CspExtension& extension, const QString& carPath);

    // Track-specific CSP config
    static bool parseTrackCspConfig(const QString& trackPath, CspExtension& extension);
    static bool saveTrackCspConfig(const CspExtension& extension, const QString& trackPath);

    // Validation
    static bool validateExtension(const CspExtension& extension, QString* error = nullptr);
    static bool validateWeatherFx(const CspWeatherFx& config, QString* error = nullptr);

    // Utility
    static QStringList getAvailableExtensions(const QString& cspPath);
    static QString getCspVersion(const QString& cspPath);
    static bool isCspInstalled(const QString& acPath);

private:
    static bool parseYamlSection(const QString& content, const QString& section, QMap<QString, QString>& settings);
    static bool writeYamlSection(const QString& section, const QMap<QString, QString>& settings, QString& content);
};

/**
 * @brief CSP Config Manager - High-level interface for CSP configuration
 */
class CspConfigManager {
public:
    explicit CspConfigManager(const QString& acPath);

    // Detection
    bool isCspInstalled() const;
    QString getCspVersion() const;
    QString getCspPath() const;

    // Configuration
    bool loadGlobalConfig();
    bool saveGlobalConfig();
    bool loadCarConfig(const QString& carName);
    bool saveCarConfig(const QString& carName);
    bool loadTrackConfig(const QString& trackName);
    bool saveTrackConfig(const QString& trackName);

    // Access
    CspConfigParser::CspWeatherFx getWeatherFx() const { return m_weatherFx; }
    CspConfigParser::CspLightingFx getLightingFx() const { return m_lightingFx; }
    CspConfigParser::CspParticlesFx getParticlesFx() const { return m_particlesFx; }
    CspConfigParser::CspPhysicsExtensions getPhysicsExtensions() const { return m_physicsExtensions; }
    CspConfigParser::CspCarExtensions getCarExtensions() const { return m_carExtensions; }
    CspConfigParser::CspTrackExtensions getTrackExtensions() const { return m_trackExtensions; }

    // Modification
    void setWeatherFx(const CspConfigParser::CspWeatherFx& config) { m_weatherFx = config; }
    void setLightingFx(const CspConfigParser::CspLightingFx& config) { m_lightingFx = config; }
    void setParticlesFx(const CspConfigParser::CspParticlesFx& config) { m_particlesFx = config; }
    void setPhysicsExtensions(const CspConfigParser::CspPhysicsExtensions& config) { m_physicsExtensions = config; }
    void setCarExtensions(const CspConfigParser::CspCarExtensions& config) { m_carExtensions = config; }
    void setTrackExtensions(const CspConfigParser::CspTrackExtensions& config) { m_trackExtensions = config; }

    // Extension management
    QStringList getAvailableExtensions() const;
    bool isExtensionEnabled(const QString& extensionName) const;
    bool setExtensionEnabled(const QString& extensionName, bool enabled);
    QJsonObject getExtensionConfig(const QString& extensionName) const;
    bool setExtensionConfig(const QString& extensionName, const QJsonObject& config);
    QStringList getEnabledExtensions() const;

private:
    QString m_acPath;
    QString m_cspPath;
    CspConfigParser::CspWeatherFx m_weatherFx;
    CspConfigParser::CspLightingFx m_lightingFx;
    CspConfigParser::CspParticlesFx m_particlesFx;
    CspConfigParser::CspPhysicsExtensions m_physicsExtensions;
    CspConfigParser::CspCarExtensions m_carExtensions;
    CspConfigParser::CspTrackExtensions m_trackExtensions;
};
